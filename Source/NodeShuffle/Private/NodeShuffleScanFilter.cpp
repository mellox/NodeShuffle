// ns-scanfilter-1 (2026-11-14): scanner-wheel SHOW/HIDE switches for modded resources. Design: NodeShuffleScanFilter.h.
// No code in this file adds a scanner unlock or writes to the save; tools/check_scanfilter_lint.ps1 forbids it.
// ns-scanfilter-4 (2026-11-14): the periodic row pass, the carry-over and the row registration moved to NodeShuffleScanFilterRows.cpp;
// the state both halves share is declared in NodeShuffleScanFilterInternal.h. This file keeps the hook, the pick answer and the predicates.
#include "NodeShuffleScanFilter.h"
#include "NodeShuffleScanFilterInternal.h"
#include "NodeShuffle.h"
#include "Equipment/FGResourceScanner.h"
#include "Resources/FGResourceDescriptor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "UObject/ObjectKey.h"

namespace NodeShuffleScanFilter
{
    const TCHAR* const BaseGameContentRoot = TEXT("/Game/");
    const TCHAR* const BaseGameScriptRoot = TEXT("/Script/FactoryGame.");
    const TCHAR* const AllMinableMountRoot = TEXT("/AllMinable/"); // user decision 2026-11-14: All Minable rows start hidden

    // ---- shared state (declared in NodeShuffleScanFilterInternal.h). Game thread only (the hook is reached from the scanner's Blueprint on the game thread). ----
    TWeakObjectPtr<UWorld> GSnapWorld;
    TMap<FString, bool> GRowShown;
    TSet<FString> GAutoRow;
    TSet<FString> GSeenNoRow;
    TArray<TWeakObjectPtr<UClass>> GAddOrder;
    int32 GCallsTotal = 0;
    int32 GCallsSinceLine = 0;
    double GLastCallLineTime = 0.0;
    FCallFigures GLatest;
    FCallFigures GLastCensus;

    namespace
    {
        // ---- hook-side state ----
        struct FClassInfo { FString Path; FString Short; bool bModded = false; };

        TMap<FObjectKey, FClassInfo> GClassInfo; // hook-side cache: one GetPathName per class per process
        FString GLastAddedNames; // the added=[...] set last seen by the hook; a change after the full lines prints addedSetChanged
        // ns-scanfilter-3: the closest-pick bracket and its per-pick counters (reset by BeginClosestPick).
        bool GInClosestPick = false;
        TWeakObjectPtr<UWorld> GPickWorld;
        int32 GPickAsked = 0;
        int32 GPickGameYes = 0;
        int32 GPickChangedYes = 0;

        const FClassInfo& ClassInfoFor(UClass* Cls)
        {
            const FObjectKey Key(Cls);
            if (const FClassInfo* Found = GClassInfo.Find(Key)) { return *Found; }
            FClassInfo Info;
            Info.Path = Cls->GetPathName();
            Info.Short = Cls->GetName();
            Info.bModded = IsModdedPath(Info.Path);
            return GClassInfo.Add(Key, MoveTemp(Info));
        }

        void EmitCallLine(const FCallFigures& F, const FString& RemovedNames, const FString& AddedNames)
        {
            // Arity hand-counted: 12 format specifiers, 12 arguments. ns-scanfilter-2 appended addedByRow and added (new names).
            UE_LOG(LogNodeShuffle, Display,
                TEXT("SCANFILTER-CALL full call=%d snapshotForThisWorld=%d sizeIn=%d sizeOut=%d hiddenByRow=%d hiddenByNoRowDefault=%d moddedSeen=%d vanillaSeen=%d rowsInSnapshot=%d removed=[%s] addedByRow=%d added=[%s]"),
                GCallsTotal, F.bSnapshot ? 1 : 0, F.In, F.Out, F.HidByRow, F.HidByDefault, F.Modded, F.Vanilla, GRowShown.Num() + GAutoRow.Num(), *RemovedNames,
                F.AddedByRow, *AddedNames);
        }

        void EmitAddedSetChanged(const FString& AddedNames, int32 AddedByRow)
        {
            // Arity hand-counted: 3 format specifiers, 3 arguments.
            UE_LOG(LogNodeShuffle, Display, TEXT("SCANFILTER-CALL addedSetChanged call=%d addedByRow=%d added=[%s]"), GCallsTotal, AddedByRow, *AddedNames);
        }
    }

    bool SameFigures(const FCallFigures& A, const FCallFigures& B) { return A.In == B.In && A.Out == B.Out && A.HidByRow == B.HidByRow && A.HidByDefault == B.HidByDefault && A.Modded == B.Modded && A.Vanilla == B.Vanilla && A.bSnapshot == B.bSnapshot && A.AddedByRow == B.AddedByRow; }

    void EmitCallCensus(double Now)
    {
        // Arity hand-counted: 11 format specifiers, 11 arguments. ns-scanfilter-2 appended latestAddedByRow.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("SCANFILTER-CALL census callsSinceLastLine=%d callsTotal=%d latestSnapshotForThisWorld=%d latestSizeIn=%d latestSizeOut=%d latestHiddenByRow=%d latestHiddenByNoRowDefault=%d latestModdedSeen=%d latestVanillaSeen=%d secondsSinceLastLine=%.1f latestAddedByRow=%d"),
            GCallsSinceLine, GCallsTotal, GLatest.bSnapshot ? 1 : 0, GLatest.In, GLatest.Out, GLatest.HidByRow, GLatest.HidByDefault,
            GLatest.Modded, GLatest.Vanilla, Now - GLastCallLineTime, GLatest.AddedByRow);
        GCallsSinceLine = 0;
        GLastCallLineTime = Now;
        GLastCensus = GLatest;
    }

    void ResetHookSideForWorld()
    {
        GRowShown.Reset();
        GAutoRow.Reset();
        GAddOrder.Reset();
        GLastAddedNames.Reset();
        GSeenNoRow.Reset();
        GCallsTotal = 0;
        GCallsSinceLine = 0;
        GLastCallLineTime = 0.0;
        GLatest = FCallFigures();
        GLastCensus = FCallFigures();
    }

    bool IsModdedPath(const FString& ClassPath)
    {
        return !ClassPath.IsEmpty() && !ClassPath.StartsWith(BaseGameContentRoot) && !ClassPath.StartsWith(BaseGameScriptRoot);
    }

    bool DefaultShownForPath(const FString& ClassPath)
    {
        return !ClassPath.StartsWith(AllMinableMountRoot);
    }

    // ---- THE HOOK BODY. No config walk, no class resolve, no disk, no config write; logging = SCANFILTER-CALL only. ----
    // ns-scanfilter-2 (user decision 2026-11-14, "Yes, ON always shows"): after the removal, ON rows whose class the
    // periodic pass resolved are APPENDED when absent. The game's own entries keep their order; added ones follow in row order.
    bool FilterScannableResources(const AFGResourceScanner* Scanner, TArray<TSubclassOf<UFGResourceDescriptor>>& InOutList)
    {
        ++GCallsTotal;
        ++GCallsSinceLine;
        FCallFigures F;
        F.In = InOutList.Num();
        const UWorld* World = IsValid(Scanner) ? Scanner->GetWorld() : nullptr;
        F.bSnapshot = World != nullptr && GSnapWorld.IsValid() && GSnapWorld.Get() == World;
        const bool bFullLine = GCallsTotal <= FullCallLines;
        FString RemovedNames;
        FString AddedNames;
        bool bRemovedAny = false;
        bool bAddedAny = false;
        if (F.bSnapshot)
        {
            TArray<TSubclassOf<UFGResourceDescriptor>> Kept;
            Kept.Reserve(InOutList.Num() + GAddOrder.Num());
            TSet<const UClass*> Present; // every class in Kept: the add step never duplicates one
            for (const TSubclassOf<UFGResourceDescriptor>& Entry : InOutList) // original order; Kept only appends
            {
                UClass* Cls = Entry.Get();
                if (!Cls) { Kept.Add(Entry); continue; }
                Present.Add(Cls);
                const FClassInfo& Info = ClassInfoFor(Cls);
                if (!Info.bModded) { ++F.Vanilla; Kept.Add(Entry); continue; } // vanilla: never hidden, row or no row
                ++F.Modded;
                bool bShown = true;
                if (const bool* RowShown = GRowShown.Find(Info.Path))
                {
                    bShown = *RowShown;
                    if (!bShown) { ++F.HidByRow; }
                }
                else if (GAutoRow.Contains(Info.Path))
                {
                    bShown = DefaultShownForPath(Info.Path); // AUTO row (round 3): the game's entry stays, except All Minable
                    if (!bShown) { ++F.HidByRow; }
                }
                else
                {
                    GSeenNoRow.Add(Info.Path); // source (b): the periodic pass gives it a row
                    bShown = DefaultShownForPath(Info.Path); // the hook's no-row rule; an AUTO row gets the same rule
                    if (!bShown) { ++F.HidByDefault; }
                }
                if (bShown) { Kept.Add(Entry); continue; }
                Present.Remove(Cls);
                bRemovedAny = true;
                if (bFullLine) { RemovedNames += (RemovedNames.IsEmpty() ? TEXT("") : TEXT(" ")) + Info.Short; }
            }
            // THE ADD STEP. GAddOrder holds only ON rows with a modded path (built in Tick), so an OFF row, a no-row class
            // and a base-game class can never be added here.
            for (const TWeakObjectPtr<UClass>& Weak : GAddOrder)
            {
                UClass* AddCls = Weak.Get(); // null when the class went away since the last Tick
                if (!AddCls || Present.Contains(AddCls)) { continue; }
                // Same key as the removal above: the class's OWN path must be modded and must have a row that is ON (last row
                // wins, as in GRowShown). A row whose text resolves to a class with a different path adds nothing.
                const FClassInfo& AddInfo = ClassInfoFor(AddCls);
                const bool* AddRow = GRowShown.Find(AddInfo.Path);
                if (!AddInfo.bModded || AddRow == nullptr || !*AddRow) { continue; }
                Present.Add(AddCls);
                Kept.Add(TSubclassOf<UFGResourceDescriptor>(AddCls));
                ++F.AddedByRow;
                bAddedAny = true;
                AddedNames += (AddedNames.IsEmpty() ? TEXT("") : TEXT(" ")) + AddInfo.Short;
            }
            if (bRemovedAny || bAddedAny) { InOutList = MoveTemp(Kept); }
        }
        F.Out = InOutList.Num();
        GLatest = F;
        const double Now = FPlatformTime::Seconds();
        if (bFullLine)
        {
            EmitCallLine(F, RemovedNames, AddedNames);
            GCallsSinceLine = 0;
            GLastCallLineTime = Now;
        }
        else if (Now - GLastCallLineTime >= CallCensusPeriodSeconds && (FNodeShuffleModule::AreDiagnosticsEnabled() || !SameFigures(GLatest, GLastCensus)))
        {
            EmitCallCensus(Now);
        }
        if (F.bSnapshot && AddedNames != GLastAddedNames) { if (!bFullLine) { EmitAddedSetChanged(AddedNames, F.AddedByRow); } GLastAddedNames = AddedNames; }
        return bRemovedAny || bAddedAny;
    }

    // ---- ns-scanfilter-3: THE CLOSEST-PICK ANSWER. No config walk, no class resolve, no disk, no config write, no unlock. ----
    void BeginClosestPick(const AFGResourceScanner* Scanner)
    {
        GInClosestPick = true;
        GPickWorld = IsValid(Scanner) ? Scanner->GetWorld() : nullptr;
        GPickAsked = 0;
        GPickGameYes = 0;
        GPickChangedYes = 0;
    }

    void EndClosestPick()
    {
        GInClosestPick = false;
        GPickWorld.Reset();
    }

    bool ShouldAnswerScannable(UClass* ResourceClass, bool bGameAnswer)
    {
        if (!GInClosestPick) { return false; } // a call made outside that pick gets the game's answer (a call made by other code while the pick runs gets the changed answer too); not counted
        ++GPickAsked;
        if (bGameAnswer) { ++GPickGameYes; return false; } // a true is never touched
        const UWorld* PickWorld = GPickWorld.Get();
        if (!ResourceClass || PickWorld == nullptr || !GSnapWorld.IsValid() || GSnapWorld.Get() != PickWorld) { return false; }
        // The add step's key (R2-A): the class's OWN path must be modded and must have a row that is ON (last row wins).
        const FClassInfo& PickInfo = ClassInfoFor(ResourceClass);
        const bool* PickRow = GRowShown.Find(PickInfo.Path);
        if (!PickInfo.bModded || PickRow == nullptr || !*PickRow) { return false; }
        ++GPickChangedYes;
        return true;
    }

    void EmitPickLine(const AFGResourceScanner* Scanner, int32 ClustersReturned)
    {
        const UClass* Res = IsValid(Scanner) ? Scanner->GetResourceDescriptorToScanFor().Get() : nullptr;
        const UWorld* World = IsValid(Scanner) ? Scanner->GetWorld() : nullptr;
        const bool bSnap = World != nullptr && GSnapWorld.IsValid() && GSnapWorld.Get() == World;
        // Arity hand-counted: 6 format specifiers, 6 arguments. Counts are this pick's IsNodeScannable questions only.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("SCANFILTER-PICK resource=%s scannableQuestions=%d gameAnsweredYes=%d changedToYes=%d clustersReturned=%d snapshotForThisWorld=%d"),
            Res ? *Res->GetName() : TEXT("<none>"), GPickAsked, GPickGameYes, GPickChangedYes, ClustersReturned, bSnap ? 1 : 0);
    }
}
