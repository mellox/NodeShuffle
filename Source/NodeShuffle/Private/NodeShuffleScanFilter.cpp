// ns-scanfilter-1 (2026-11-14): scanner-wheel SHOW/HIDE switches for modded resources. Design: NodeShuffleScanFilter.h.
// No code in this file adds a scanner unlock or writes to the save; tools/check_scanfilter_lint.ps1 forbids it.
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
        struct FCallFigures { int32 In = 0; int32 Out = 0; int32 HidByRow = 0; int32 HidByDefault = 0; int32 Modded = 0; int32 Vanilla = 0; bool bSnapshot = false; };

        TWeakObjectPtr<UWorld> GSnapWorld;      // the world whose rows GRowShown holds; the hook filters only for this world
        TWeakObjectPtr<UWorld> GTickWorld;      // the world the periodic pass last ran for (per-world reset; never read by the hook)
        TMap<FString, bool> GRowShown;          // row path -> switch value, rebuilt from the live tree by every Tick
        TMap<FObjectKey, FClassInfo> GClassInfo; // hook-side cache: one GetPathName per class per process
        TSet<FString> GSeenNoRow;               // source (b): modded classes the hook met with no row; kept for the world session
        int32 GCallsTotal = 0;
        int32 GCallsSinceLine = 0;
        double GLastCallLineTime = 0.0;
        FCallFigures GLatest;
        uint32 GLastRowsSignature = 0;
        bool GRowsLogged = false;
        int32 GLastRowCount = -1;
        bool GWarnedUnreachable = false;
        bool GLoggedDisabled = false;

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

        void EmitCallLine(const FCallFigures& F, const FString& RemovedNames)
        {
            // Arity hand-counted: 10 format specifiers, 10 arguments.
            UE_LOG(LogNodeShuffle, Display,
                TEXT("SCANFILTER-CALL full call=%d snapshotForThisWorld=%d sizeIn=%d sizeOut=%d hiddenByRow=%d hiddenByNoRowDefault=%d moddedSeen=%d vanillaSeen=%d rowsInSnapshot=%d removed=[%s]"),
                GCallsTotal, F.bSnapshot ? 1 : 0, F.In, F.Out, F.HidByRow, F.HidByDefault, F.Modded, F.Vanilla, GRowShown.Num(), *RemovedNames);
        }

        void EmitCallCensus(double Now)
        {
            // Arity hand-counted: 10 format specifiers, 10 arguments.
            UE_LOG(LogNodeShuffle, Display,
                TEXT("SCANFILTER-CALL census callsSinceLastLine=%d callsTotal=%d latestSnapshotForThisWorld=%d latestSizeIn=%d latestSizeOut=%d latestHiddenByRow=%d latestHiddenByNoRowDefault=%d latestModdedSeen=%d latestVanillaSeen=%d secondsSinceLastLine=%.1f"),
                GCallsSinceLine, GCallsTotal, GLatest.bSnapshot ? 1 : 0, GLatest.In, GLatest.Out, GLatest.HidByRow, GLatest.HidByDefault,
                GLatest.Modded, GLatest.Vanilla, Now - GLastCallLineTime);
            GCallsSinceLine = 0;
            GLastCallLineTime = Now;
        }

        void ResetForWorld()
        {
            GRowShown.Reset();
            GSeenNoRow.Reset();
            GCallsTotal = 0;
            GCallsSinceLine = 0;
            GLastCallLineTime = 0.0;
            GLatest = FCallFigures();
            GLastRowsSignature = 0;
            GRowsLogged = false;
            GLastRowCount = -1;
            GWarnedUnreachable = false;
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

    // ---- THE HOOK BODY. No config walk, no disk, no config write; logging = SCANFILTER-CALL only. ----
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
        bool bRemovedAny = false;
        if (F.bSnapshot)
        {
            TArray<TSubclassOf<UFGResourceDescriptor>> Kept;
            Kept.Reserve(InOutList.Num());
            for (const TSubclassOf<UFGResourceDescriptor>& Entry : InOutList) // original order; Kept only appends
            {
                UClass* Cls = Entry.Get();
                if (!Cls) { Kept.Add(Entry); continue; }
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
                bRemovedAny = true;
                if (bFullLine) { RemovedNames += (RemovedNames.IsEmpty() ? TEXT("") : TEXT(" ")) + Info.Short; }
            }
            if (bRemovedAny) { InOutList = MoveTemp(Kept); }
        }
        F.Out = InOutList.Num();
        GLatest = F;
        const double Now = FPlatformTime::Seconds();
        if (bFullLine)
        {
            EmitCallLine(F, RemovedNames);
            GCallsSinceLine = 0;
            GLastCallLineTime = Now;
        }
        else if (Now - GLastCallLineTime >= CallCensusPeriodSeconds)
        {
            EmitCallCensus(Now);
        }
        return bRemovedAny;
    }

    // ---- THE PERIODIC PASS ----
    void Tick(UObject* WorldContext, const TArray<FString>& ManagedModdedPaths, bool bModEnabled)
    {
        UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
        if (!World) { return; }
        if (GTickWorld.Get() != World) { ResetForWorld(); GSnapWorld.Reset(); GTickWorld = World; }
        const double Now = FPlatformTime::Seconds();
        if (GCallsSinceLine > 0 && GCallsTotal > FullCallLines && Now - GLastCallLineTime >= CallCensusPeriodSeconds) { EmitCallCensus(Now); }

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
        if (RefusedOverCap > 0 || Withdrawn > 0)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("SCANFILTER-ROWS notAdded: refusedOverCap=%d cap=%d withdrawnSharedWithTemplate=%d; those resources keep the starting-value rule in the hook"),
                RefusedOverCap, MaxRows, Withdrawn);
        }

        // Rebuild the snapshot from the LIVE tree: this is what delivers a switch flipped in the settings screen.
        GRowShown.Reset();
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
            FMountCount& M = PerMount.FindOrAdd(MountLabel(Path));
            ++M.Total;
            if (bShow) { ++Shown; ++M.Shown; } else { ++Hidden; ++M.Hidden; }
            Signature = HashCombine(Signature, HashCombine(GetTypeHash(Path), bShow ? 1u : 2u));
        }
        GSnapWorld = World;
        GLastRowCount = Rows->Values.Num();

        if (!GRowsLogged || Signature != GLastRowsSignature)
        {
            FString PerMountText;
            for (const TPair<FString, FMountCount>& M : PerMount)
            {
                PerMountText += FString::Printf(TEXT(" %s(total %d shown %d hidden %d)"), *M.Key, M.Value.Total, M.Value.Shown, M.Value.Hidden);
            }
            // Arity hand-counted: 9 format specifiers, 9 arguments.
            UE_LOG(LogNodeShuffle, Display,
                TEXT("SCANFILTER-ROWS %s rowsTotal=%d rowsInSnapshot=%d shown=%d hidden=%d malformed=%d blankPath=%d addedThisPass=%d perMount:%s"),
                GRowsLogged ? TEXT("changed") : TEXT("loaded"), Rows->Values.Num(), GRowShown.Num(), Shown, Hidden, Malformed, Blank,
                Added, *PerMountText);
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
            TEXT("One row per resource added by another mod. ON: the resource stays in the list the resource scanner offers. ")
            TEXT("OFF: NodeShuffle removes it from that list. A switch only hides or shows; it never makes a resource ")
            TEXT("scannable. A resource you have not researched will not appear in the scanner even when its switch is ON.\n\n")
            TEXT("THIS LIST FILLS ITSELF IN while a world is running: a row is added for each modded resource NodeShuffle ")
            TEXT("shuffles in that world, and for each modded resource that appears in the scanner's list. A new row starts OFF ")
            TEXT("for resources from All Minable and ON for every other mod. Resources from the base game are never listed and ")
            TEXT("never hidden.\n\n")
            TEXT("These switches apply to every world on this install, not to one save. A change made while you play is read ")
            TEXT("within about 5 seconds; reopen the scanner after changing one. While NodeShuffle's 'Enabled' is off, nothing ")
            TEXT("is hidden. In multiplayer only the host's scanner is filtered. Reset empties the list; the rows come back with ")
            TEXT("their starting values while a world is running. The text fields name the resource; only the switch is a setting."));

        UConfigPropertySection* RowTemplate = NewObject<UConfigPropertySection>(Rows, SectionClass, TEXT("ScannerResourceRow"));
        RowTemplate->bRequiresWorldReload = false;
        RowTemplate->DisplayName = FText::FromString(TEXT("scanner row"));
        if (UCP_Section* W = Cast<UCP_Section>(RowTemplate)) { W->WidgetType = ECP_SectionWidgetType::CPS_Horizontal; W->HasHeader = false; }

        // Field order is the panel order (TMap insertion order, as T65 records): the switch first.
        UConfigPropertyBool* Show = NewObject<UConfigPropertyBool>(RowTemplate, BoolClass, TEXT("ShowInScanner"));
        Show->Value = true;
        Show->DefaultValue = true;
        Show->DisplayName = FText::FromString(TEXT("Show in scanner"));
        Show->Tooltip = FText::FromString(TEXT("ON: this resource stays in the scanner's list. OFF: NodeShuffle removes it from that list. It never makes the resource scannable."));
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
        AddString(TEXT("Resource"), TEXT("Resource"), TEXT("The resource's full asset path. This is the row's identity; edit it and the row matches nothing."));

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
