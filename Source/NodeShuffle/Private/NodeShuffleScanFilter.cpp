// ns-scanfilter-1 (2026-11-14): scanner-wheel SHOW/HIDE switches for modded resources. Design: NodeShuffleScanFilter.h.
// No code in this file adds a scanner unlock or writes to the save; tools/check_scanfilter_lint.ps1 forbids it.
// TODO(2026-11-14 ns-scanfilter-3 review F2): 560 lines, over the 500-line rule; split the pick answer into its own file with the shared snapshot exposed.
#include "NodeShuffleScanFilter.h"
#include "NodeShuffle.h"
#include "NodeShuffleSubsystem.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyArray.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyString.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "Equipment/FGResourceScanner.h"
#include "Resources/FGItemDescriptor.h"
#include "Resources/FGResourceDescriptor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "UObject/ObjectKey.h"
#include "UObject/SoftObjectPath.h"

namespace NodeShuffleScanFilter
{
    const TCHAR* const BaseGameContentRoot = TEXT("/Game/");
    const TCHAR* const BaseGameScriptRoot = TEXT("/Script/FactoryGame.");
    const TCHAR* const AllMinableMountRoot = TEXT("/AllMinable/"); // user decision 2026-11-14: All Minable rows start hidden
    const TCHAR* const RowsKey = TEXT("ScannerResourceVisibility");

    namespace
    {
        // ---- module-static state. Game thread only (the hook is reached from the scanner's Blueprint on the game thread). ----
        struct FClassInfo { FString Path; FString Short; bool bModded = false; };
        struct FCallFigures { int32 In = 0; int32 Out = 0; int32 HidByRow = 0; int32 HidByDefault = 0; int32 Modded = 0; int32 Vanilla = 0; bool bSnapshot = false; int32 AddedByRow = 0; };

        TWeakObjectPtr<UWorld> GSnapWorld;      // the world whose rows GRowShown holds; the hook filters only for this world
        TWeakObjectPtr<UWorld> GTickWorld;      // the world the periodic pass last ran for (per-world reset; never read by the hook)
        TMap<FString, bool> GRowShown;          // row path -> switch value, rebuilt from the live tree by every Tick
        TMap<FObjectKey, FClassInfo> GClassInfo; // hook-side cache: one GetPathName per class per process
        TSet<FString> GSeenNoRow;               // source (b): modded classes the hook met with no row; kept for the world session
        // ns-scanfilter-2: ON rows whose class the periodic pass resolved (in memory, never loaded), in row order. Weak, so a
        // class that goes away between Ticks is skipped by the hook rather than dereferenced.
        TArray<TWeakObjectPtr<UClass>> GAddOrder;
        FString GLastAddedNames; // the added=[...] set last seen by the hook; a change after the full lines prints addedSetChanged
        // ns-scanfilter-3: the closest-pick bracket and its per-pick counters (reset by BeginClosestPick).
        bool GInClosestPick = false;
        TWeakObjectPtr<UWorld> GPickWorld;
        int32 GPickAsked = 0;
        int32 GPickGameYes = 0;
        int32 GPickChangedYes = 0;
        int32 GCallsTotal = 0;
        int32 GCallsSinceLine = 0;
        double GLastCallLineTime = 0.0;
        FCallFigures GLatest;
        uint32 GLastRowsSignature = 0;
        bool GRowsLogged = false;
        int32 GLastRowCount = -1;
        bool GWarnedUnreachable = false;
        bool GLoggedDisabled = false;
        int32 GLastRefused = 0;
        int32 GLastWithdrawn = 0;

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

        // Second-segment-free mount label: the text between the first two slashes ("AllMinable"), or the whole path.
        FString MountLabel(const FString& Path)
        {
            int32 Slash = INDEX_NONE;
            const FString After = Path.Mid(1);
            if (!Path.StartsWith(TEXT("/")) || !After.FindChar(TEXT('/'), Slash) || Slash <= 0) { return Path; }
            return After.Left(Slash);
        }

        FString LeafName(const FString& Path)
        {
            FString Leaf = Path;
            int32 Dot = INDEX_NONE;
            if (Leaf.FindLastChar(TEXT('.'), Dot)) { Leaf = Leaf.Mid(Dot + 1); }
            if (Leaf.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive)) { Leaf = Leaf.LeftChop(2); }
            return Leaf;
        }

        // The in-game item name when the class is already in memory (ResolveClass never loads); the class leaf otherwise.
        FString ReadableName(const FString& Path, bool& bOutFromItemName)
        {
            bOutFromItemName = false;
            UClass* Cls = FSoftClassPath(Path).ResolveClass();
            if (Cls && Cls->IsChildOf(UFGResourceDescriptor::StaticClass()))
            {
                const FString Name = UFGItemDescriptor::GetItemName(TSubclassOf<UFGItemDescriptor>(Cls)).ToString();
                if (!Name.IsEmpty()) { bOutFromItemName = true; return Name; }
            }
            return LeafName(Path);
        }

        UConfigPropertyArray* GetRows(UObject* WorldContext, UConfigManager** OutManager)
        {
            *OutManager = nullptr;
            if (!GEngine || !WorldContext) { return nullptr; }
            const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
            if (!World || !World->GetGameInstance()) { return nullptr; }
            UConfigManager* Manager = World->GetGameInstance()->GetSubsystem<UConfigManager>();
            *OutManager = Manager;
            UConfigPropertySection* Root = Manager ? Manager->GetConfigurationRootSection(FConfigId{"NodeShuffle", ""}) : nullptr;
            TObjectPtr<UConfigProperty>* Found = Root ? Root->SectionProperties.Find(RowsKey) : nullptr;
            return Found ? Cast<UConfigPropertyArray>(Found->Get()) : nullptr;
        }

        struct FRowProps { UConfigPropertySection* Section = nullptr; UConfigPropertyBool* Show = nullptr; UConfigPropertyString* Name = nullptr; UConfigPropertyString* Mod = nullptr; UConfigPropertyString* Resource = nullptr; };

        bool ReadRow(UConfigProperty* Element, FRowProps& Out)
        {
            Out.Section = Cast<UConfigPropertySection>(Element);
            if (!Out.Section) { return false; }
            const auto Get = [&Out](const TCHAR* Key) -> UConfigProperty*
            {
                TObjectPtr<UConfigProperty>* P = Out.Section->SectionProperties.Find(Key);
                return P ? P->Get() : nullptr;
            };
            Out.Show = Cast<UConfigPropertyBool>(Get(TEXT("ShowInScanner")));
            Out.Name = Cast<UConfigPropertyString>(Get(TEXT("Name")));
            Out.Mod = Cast<UConfigPropertyString>(Get(TEXT("Mod")));
            Out.Resource = Cast<UConfigPropertyString>(Get(TEXT("Resource")));
            return Out.Show && Out.Name && Out.Mod && Out.Resource;
        }

        void EmitCallLine(const FCallFigures& F, const FString& RemovedNames, const FString& AddedNames)
        {
            // Arity hand-counted: 12 format specifiers, 12 arguments. ns-scanfilter-2 appended addedByRow and added (new names).
            UE_LOG(LogNodeShuffle, Display,
                TEXT("SCANFILTER-CALL full call=%d snapshotForThisWorld=%d sizeIn=%d sizeOut=%d hiddenByRow=%d hiddenByNoRowDefault=%d moddedSeen=%d vanillaSeen=%d rowsInSnapshot=%d removed=[%s] addedByRow=%d added=[%s]"),
                GCallsTotal, F.bSnapshot ? 1 : 0, F.In, F.Out, F.HidByRow, F.HidByDefault, F.Modded, F.Vanilla, GRowShown.Num(), *RemovedNames,
                F.AddedByRow, *AddedNames);
        }

        void EmitAddedSetChanged(const FString& AddedNames, int32 AddedByRow)
        {
            // Arity hand-counted: 3 format specifiers, 3 arguments.
            UE_LOG(LogNodeShuffle, Display, TEXT("SCANFILTER-CALL addedSetChanged call=%d addedByRow=%d added=[%s]"), GCallsTotal, AddedByRow, *AddedNames);
        }

        FCallFigures GLastCensus;
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

        void ResetForWorld()
        {
            GRowShown.Reset();
            GAddOrder.Reset();
            GLastAddedNames.Reset();
            GSeenNoRow.Reset();
            GCallsTotal = 0;
            GCallsSinceLine = 0;
            GLastCallLineTime = 0.0;
            GLatest = FCallFigures();
            GLastRowsSignature = 0;
            GRowsLogged = false;
            GLastRowCount = -1;
            GWarnedUnreachable = false;
            GLastCensus = FCallFigures();
            GLastRefused = 0;
            GLastWithdrawn = 0;
            GLoggedDisabled = false;
        }
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
                else
                {
                    GSeenNoRow.Add(Info.Path); // source (b): the periodic pass gives it a row
                    bShown = DefaultShownForPath(Info.Path); // the same rule a new row is created with
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

    // ---- THE PERIODIC PASS ----
    void Tick(UObject* WorldContext, const TArray<FString>& ManagedModdedPaths, bool bModEnabled)
    {
        UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
        if (!World) { return; }
        if (GTickWorld.Get() != World) { ResetForWorld(); GSnapWorld.Reset(); GTickWorld = World; }
        const double Now = FPlatformTime::Seconds();
        if (GCallsSinceLine > 0 && GCallsTotal > FullCallLines && Now - GLastCallLineTime >= CallCensusPeriodSeconds && (FNodeShuffleModule::AreDiagnosticsEnabled() || !SameFigures(GLatest, GLastCensus))) { EmitCallCensus(Now); }

        if (!bModEnabled)
        {
            GSnapWorld.Reset();
            if (!GLoggedDisabled)
            {
                UE_LOG(LogNodeShuffle, Display, TEXT("SCANFILTER-ROWS modEnabled=0: no snapshot is published, so every scanner list is returned unfiltered"));
                GLoggedDisabled = true;
            }
            return;
        }
        GLoggedDisabled = false;

        UConfigManager* Manager = nullptr;
        UConfigPropertyArray* Rows = GetRows(WorldContext, &Manager);
        if (!Rows || !Manager)
        {
            GSnapWorld.Reset();
            if (!GWarnedUnreachable)
            {
                UE_LOG(LogNodeShuffle, Warning, TEXT("SCANFILTER-ROWS rowsReachable=0 managerFound=%d: no snapshot is published, so every scanner list is returned unfiltered"), Manager ? 1 : 0);
                GWarnedUnreachable = true;
            }
            return;
        }

        // An in-world Reset empties the array (DefaultValues is empty). Measured here as a count drop to zero; the add loop
        // below re-creates the rows it knows about with their defaults.
        if (GLastRowCount > 0 && Rows->Values.Num() == 0)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("SCANFILTER-ROWS emptied: the list held %d row(s) at the previous pass and holds 0 now; rows are re-created below with their starting values from the managed set and the classes seen in the scanner list"),
                GLastRowCount);
        }

        // Existing rows (by path), the template's own value objects (a new row must not share them), and a first-pass stamp.
        const bool bFirstPassThisWorld = !GRowsLogged;
        TSet<FString> Existing;
        int32 Malformed = 0;
        int32 Blank = 0;
        for (UConfigProperty* Element : Rows->Values)
        {
            FRowProps R;
            if (!ReadRow(Element, R)) { ++Malformed; continue; }
            const FString Path = R.Resource->Value.TrimStartAndEnd();
            if (Path.IsEmpty()) { ++Blank; continue; }
            Existing.Add(Path);
            if (bFirstPassThisWorld) { R.Section->DisplayName = FText::FromString(R.Name->Value + TEXT(" (") + R.Mod->Value + TEXT(")")); }
        }
        FRowProps Tpl;
        const bool bTplReadable = ReadRow(Rows->DefaultValue.Get(), Tpl);

        // Source (a) first, then (b). Both through the same modded predicate the hook uses.
        TSet<FString> Managed;
        for (const FString& P : ManagedModdedPaths) { if (IsModdedPath(P)) { Managed.Add(P); } }
        TArray<FString> Wanted;
        for (const FString& P : Managed) { Wanted.Add(P); }
        for (const FString& P : GSeenNoRow) { if (IsModdedPath(P) && !Managed.Contains(P)) { Wanted.Add(P); } }

        int32 Added = 0;
        int32 RefusedOverCap = 0;
        int32 Withdrawn = 0;
        for (const FString& Path : Wanted)
        {
            if (Existing.Contains(Path)) { continue; }
            if (Rows->Values.Num() >= MaxRows) { ++RefusedOverCap; continue; }
            UConfigProperty* NewElement = Rows->AddNewElement();
            FRowProps R;
            if (!ReadRow(NewElement, R) || (bTplReadable && (R.Show == Tpl.Show || R.Resource == Tpl.Resource || R.Name == Tpl.Name || R.Mod == Tpl.Mod)))
            {
                // Same guard as T60 (cold review F8): never write through a value object the template owns.
                if (NewElement) { Rows->RemoveElementAtIndex(Rows->Values.Num() - 1); }
                ++Withdrawn;
                continue;
            }
            bool bFromItemName = false;
            const bool bDefaultShown = DefaultShownForPath(Path);
            R.Resource->Value = Path;
            R.Name->Value = ReadableName(Path, bFromItemName);
            R.Mod->Value = MountLabel(Path);
            R.Show->Value = bDefaultShown;
            R.Section->DisplayName = FText::FromString(R.Name->Value + TEXT(" (") + R.Mod->Value + TEXT(")"));
            Existing.Add(Path);
            ++Added;
            const bool bA = Managed.Contains(Path);
            const bool bB = GSeenNoRow.Contains(Path);
            // Arity hand-counted: 6 format specifiers, 6 arguments.
            UE_LOG(LogNodeShuffle, Display,
                TEXT("SCANFILTER-ROWADD resource=%s name='%s' nameFromItemName=%d mount=%s startsShown=%d source=%s"),
                *Path, *R.Name->Value, bFromItemName ? 1 : 0, *R.Mod->Value, bDefaultShown ? 1 : 0,
                (bA && bB) ? TEXT("managed+scannerList") : (bA ? TEXT("managed") : TEXT("scannerList")));
        }
        if (Added > 0)
        {
            // The T60 three-call sequence (NodeShuffleForeignProtectConfig.cpp): live property -> save handler, cached
            // structs re-initialised, file written now. Runs only on a pass that added a row.
            Rows->MarkDirty();
            Manager->MarkConfigurationDirty(FConfigId{"NodeShuffle", ""});
            Manager->FlushPendingSaves();
        }
        if ((RefusedOverCap > 0 || Withdrawn > 0) && (RefusedOverCap != GLastRefused || Withdrawn != GLastWithdrawn))
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("SCANFILTER-ROWS notAdded: refusedOverCap=%d cap=%d withdrawnSharedWithTemplate=%d; those resources keep the starting-value rule in the hook"),
                RefusedOverCap, MaxRows, Withdrawn);
        }
        GLastRefused = RefusedOverCap; GLastWithdrawn = Withdrawn;

        // Rebuild the snapshot from the LIVE tree: this is what delivers a switch flipped in the settings screen.
        GRowShown.Reset();
        GAddOrder.Reset();
        int32 OnNotFound = 0; int32 OnFoundNotAddable = 0; FString OnNotFoundNames; // ON rows: path found no class (ResolveClass never loads) / found a class that is not a concrete UFGResourceDescriptor child
        int32 Shown = 0;
        int32 Hidden = 0;
        uint32 Signature = 0x5CA9u;
        struct FMountCount { int32 Total = 0; int32 Shown = 0; int32 Hidden = 0; };
        TMap<FString, FMountCount> PerMount;
        for (UConfigProperty* Element : Rows->Values)
        {
            FRowProps R;
            if (!ReadRow(Element, R)) { continue; }
            const FString Path = R.Resource->Value.TrimStartAndEnd();
            if (!IsModdedPath(Path)) { continue; }
            const bool bShow = R.Show->Value;
            GRowShown.Add(Path, bShow);
            if (bShow)
            {
                UClass* RowCls = FSoftClassPath(Path).ResolveClass();
                if (RowCls && RowCls->IsChildOf(UFGResourceDescriptor::StaticClass()) && !RowCls->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) { GAddOrder.Add(RowCls); }
                else if (RowCls) { ++OnFoundNotAddable; }
                else { ++OnNotFound; OnNotFoundNames += (OnNotFoundNames.IsEmpty() ? TEXT("") : TEXT(" ")) + LeafName(Path); }
            }
            FMountCount& M = PerMount.FindOrAdd(MountLabel(Path));
            ++M.Total;
            if (bShow) { ++Shown; ++M.Shown; } else { ++Hidden; ++M.Hidden; }
            Signature = HashCombine(Signature, HashCombine(GetTypeHash(Path), bShow ? 1u : 2u));
        }
        Signature = HashCombine(Signature, HashCombine(static_cast<uint32>(OnNotFound), static_cast<uint32>(OnFoundNotAddable)));
        GSnapWorld = World;
        GLastRowCount = Rows->Values.Num();

        if (!GRowsLogged || Signature != GLastRowsSignature)
        {
            FString PerMountText;
            for (const TPair<FString, FMountCount>& M : PerMount)
            {
                PerMountText += FString::Printf(TEXT(" %s(total %d shown %d hidden %d)"), *M.Key, M.Value.Total, M.Value.Shown, M.Value.Hidden);
            }
            // Arity hand-counted: 12 format specifiers, 12 arguments. ns-scanfilter-2 round 2 (R3): the two ON-row predicates, split.
            UE_LOG(LogNodeShuffle, Display,
                TEXT("SCANFILTER-ROWS %s rowsTotal=%d rowsInSnapshot=%d shown=%d hidden=%d malformed=%d blankPath=%d addedThisPass=%d perMount:%s onRowsClassNotFound=%d onRowsFoundNotAddable=%d classNotFound=[%s]"),
                GRowsLogged ? TEXT("changed") : TEXT("loaded"), Rows->Values.Num(), GRowShown.Num(), Shown, Hidden, Malformed, Blank,
                Added, *PerMountText, OnNotFound, OnFoundNotAddable, *OnNotFoundNames);
            GRowsLogged = true;
            GLastRowsSignature = Signature;
        }
    }

    // ---- THE ROWS ----
    void RegisterConfigRows(UConfigPropertySection* Root, UClass* ArrayClass, UClass* SectionClass, UClass* StringClass,
        UClass* BoolClass)
    {
        UConfigPropertyArray* Rows = NewObject<UConfigPropertyArray>(Root, ArrayClass, RowsKey);
        // false on the array, the row and every field: the list is meant to be edited in a running world. The cost (cold
        // review F10 for T60): an in-world Reset empties it (DefaultValues stays empty); Tick re-creates rows with defaults.
        Rows->bRequiresWorldReload = false;
        Rows->DisplayName = FText::FromString(TEXT("Show Modded Resources In Scanner (Per Resource)"));
        // Every factual assertion is graded in the ns-scanfilter-1 handoff.
        Rows->Tooltip = FText::FromString(
            TEXT("One row per resource added by another mod. ON: NodeShuffle adds the resource to the list of resources it hands ")
            TEXT("to the resource scanner, including a resource you have not researched. OFF: NodeShuffle takes it out of that list. ")
            TEXT("When you scan for an ON resource, NodeShuffle also lets the scan look for its nodes; it does not promise that ")
            TEXT("one is found. Nothing is written to your save: the switches change only what NodeShuffle tells the scanner, and no ")
            TEXT("research or scanner unlock is added or removed. An ON resource is added to that list only once the game has ")
            TEXT("loaded it.\n\n")
            TEXT("THIS LIST FILLS ITSELF IN in a world you host while NodeShuffle's 'Enabled' is on: a row is added for each modded resource NodeShuffle ")
            TEXT("shuffles in that world, and for each modded resource that appears in the scanner's list, up to 256 rows. A new row starts OFF ")
            TEXT("for resources from All Minable and ON for every other mod. Resources from the base game are never listed and ")
            TEXT("never hidden. A missing row is added again the same way, with its starting value.\n\n")
            TEXT("These switches apply to every world on this install, not to one save. After changing a switch, reopen the scanner. While NodeShuffle's 'Enabled' is off, nothing ")
            TEXT("is hidden or added. In multiplayer only the host's game applies these switches. The text fields name the resource; only the switch is a setting."));

        UConfigPropertySection* RowTemplate = NewObject<UConfigPropertySection>(Rows, SectionClass, TEXT("ScannerResourceRow"));
        RowTemplate->bRequiresWorldReload = false;
        RowTemplate->DisplayName = FText::FromString(TEXT("scanner row"));
        if (UCP_Section* W = Cast<UCP_Section>(RowTemplate)) { W->WidgetType = ECP_SectionWidgetType::CPS_Horizontal; W->HasHeader = false; }

        // Field order is the panel order (TMap insertion order, as T65 records): the switch first.
        UConfigPropertyBool* Show = NewObject<UConfigPropertyBool>(RowTemplate, BoolClass, TEXT("ShowInScanner"));
        Show->Value = true;
        Show->DefaultValue = true;
        Show->DisplayName = FText::FromString(TEXT("Show in scanner"));
        Show->Tooltip = FText::FromString(TEXT("ON: NodeShuffle adds this resource to the list it hands the resource scanner, even if you have not researched it, once the game has loaded the resource. OFF: NodeShuffle takes it out of that list. Nothing is written to your save."));
        Show->bRequiresWorldReload = false;
        RowTemplate->SectionProperties.Add(TEXT("ShowInScanner"), Show);

        const auto AddString = [&](const TCHAR* Key, const TCHAR* Display, const TCHAR* Tip)
        {
            UConfigPropertyString* P = NewObject<UConfigPropertyString>(RowTemplate, StringClass, FName(Key));
            P->Value = TEXT("");
            P->DefaultValue = TEXT("");
            P->DisplayName = FText::FromString(Display);
            P->Tooltip = FText::FromString(Tip);
            P->bRequiresWorldReload = false;
            RowTemplate->SectionProperties.Add(Key, P);
        };
        AddString(TEXT("Name"), TEXT("Name"), TEXT("The resource's in-game name, or its asset name when the game had not loaded it yet. Not a setting."));
        AddString(TEXT("Mod"), TEXT("Mod"), TEXT("The content folder the resource comes from (the first part of its path). Not a setting."));
        AddString(TEXT("Resource"), TEXT("Resource"), TEXT("The resource's full asset path. This is the row's identity: the switch acts only on a modded resource whose own path matches this text (capital letters and outer spaces aside). If you edit it, the row may match nothing; a base-game path never matches. Not a setting."));

        Rows->DefaultValue = RowTemplate;
        Root->SectionProperties.Add(RowsKey, Rows);
    }
}

// Source (a): distinct modded resources on ACTIVE layout entries -- the knowledge pass's population, through the hook's
// modded predicate. Member function only because Layout is private.
void ANodeShuffleSubsystem::ScanFilterTick(bool bModEnabled)
{
    TArray<FString> Managed;
    if (bModEnabled)
    {
        TSet<FString> Seen;
        for (const FNodeShuffleEntry& E : Layout)
        {
            if (!E.bActive || !NodeShuffleScanFilter::IsModdedPath(E.AssignedResourceClassPath)) { continue; }
            bool bAlready = false;
            Seen.Add(E.AssignedResourceClassPath, &bAlready);
            if (!bAlready) { Managed.Add(E.AssignedResourceClassPath); }
        }
    }
    NodeShuffleScanFilter::Tick(this, Managed, bModEnabled);
}
