// ns-scanfilter-4 (2026-11-14, user: "Merge anyway"): the scanner switches are ONE field of the protection rows. This file holds the
// periodic row pass (rows from the managed set and the scanner list, the one-time carry-over, the snapshot the hook reads), the
// AUTO-value rule every row creator uses (round 3: AutoShowValue; row states in NodeShuffleScanFilterState.cpp), and the
// registration of the RETIRED list and the hidden carry-over flag.
// Shared state: NodeShuffleScanFilterInternal.h. Protection reads only 'Protected' (NodeShuffleForeignProtectConfig.cpp); every row
// this file creates is written Protected=true, which the latch treats exactly like no row (handoff: item-3 proof).
#include "NodeShuffleScanFilter.h"
#include "NodeShuffleScanFilterInternal.h"
#include "NodeShuffle.h"
#include "NodeShuffleSubsystem.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyArray.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyString.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "FGGameState.h"
#include "FGUnlockSubsystem.h" // also declares the pair type it returns (GetScannableResourcePairs is inline there)
#include "Resources/FGItemDescriptor.h"
#include "Resources/FGResourceDescriptor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "UObject/SoftObjectPath.h"

namespace NodeShuffleScanFilter
{
    const TCHAR* const MergedRowsKey = TEXT("ProtectedForeignResources");   // the one list (registered in NodeShuffleConfig.cpp)
    const TCHAR* const RetiredRowsKey = TEXT("ScannerResourceVisibility");  // retired: read once by the carry-over, then emptied
    const TCHAR* const MergedFlagKey = TEXT("ScannerSwitchesMerged");       // hidden: true once the carry-over has run

    namespace
    {
        // ---- row-side state. Game thread only. ----
        TWeakObjectPtr<UWorld> GTickWorld;      // the world the periodic pass last ran for (per-world reset; never read by the hook)
        uint32 GLastRowsSignature = 0;
        bool GRowsLogged = false;
        int32 GLastRowCount = -1;
        bool GWarnedUnreachable = false;
        bool GLoggedDisabled = false;
        int32 GLastRefused = 0;
        int32 GLastWithdrawn = 0;
        bool GCarryLogged = false;              // the "nothing to carry" line: once per world session
        bool GCarryWaitLogged = false;          // the "waiting for the scannable list" line: once per world session
        TMap<FString, int32> GDeferPasses;      // per sighted path: passes the scanner side has left it to the protection pass
        constexpr int32 MaxDeferPasses = 24;    // TODO(2026-11-14, ns-scanfilter-4 review F3): 2 min at 5 s, unmeasured; bounds the wait where ApplyLayout never runs (Unknown map, RWG before its pipeline latches)
        // The AUTO-value context, captured by Tick for one world: the unlock subsystem's scannable list (round 3: the managed set is
        // no longer part of it).
        TWeakObjectPtr<UWorld> GStartWorld;
        TSet<FString> GStartGameList;
        bool GStartGameListRead = false;

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
        UConfigPropertySection* GetRoot(UObject* WorldContext, UConfigManager** OutManager)
        {
            *OutManager = nullptr;
            if (!GEngine || !WorldContext) { return nullptr; }
            const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
            if (!World || !World->GetGameInstance()) { return nullptr; }
            UConfigManager* Manager = World->GetGameInstance()->GetSubsystem<UConfigManager>();
            *OutManager = Manager;
            return Manager ? Manager->GetConfigurationRootSection(FConfigId{"NodeShuffle", ""}) : nullptr;
        }
        UConfigProperty* FindChild(UConfigPropertySection* Section, const TCHAR* Key)
        {
            TObjectPtr<UConfigProperty>* P = Section ? Section->SectionProperties.Find(Key) : nullptr;
            return P ? P->Get() : nullptr;
        }
        // A merged row: {Protected, ShowInScanner, Resource}. False when the element is not that shape.
        struct FRowProps { UConfigPropertySection* Section = nullptr; UConfigPropertyBool* Show = nullptr; UConfigPropertyBool* Protected = nullptr; UConfigPropertyString* Resource = nullptr; };

        bool ReadRow(UConfigProperty* Element, FRowProps& Out)
        {
            Out.Section = Cast<UConfigPropertySection>(Element);
            if (!Out.Section) { return false; }
            Out.Show = Cast<UConfigPropertyBool>(FindChild(Out.Section, TEXT("ShowInScanner")));
            Out.Protected = Cast<UConfigPropertyBool>(FindChild(Out.Section, TEXT("Protected")));
            Out.Resource = Cast<UConfigPropertyString>(FindChild(Out.Section, TEXT("Resource")));
            return Out.Show && Out.Protected && Out.Resource;
        }
        // A retired row: only the two fields the carry-over copies.
        bool ReadRetiredRow(UConfigProperty* Element, UConfigPropertyBool*& OutShow, UConfigPropertyString*& OutResource)
        {
            UConfigPropertySection* Section = Cast<UConfigPropertySection>(Element);
            OutShow = Cast<UConfigPropertyBool>(FindChild(Section, TEXT("ShowInScanner")));
            OutResource = Cast<UConfigPropertyString>(FindChild(Section, TEXT("Resource")));
            return OutShow && OutResource;
        }
        // THE ONLY PLACE THIS FILE CREATES A ROW. Same guard as T60 (cold review F8): never write through a value object the
        // template owns. Protected is written TRUE: a ticked row and an absent row give the latch the same set (handoff, item 3).
        bool AppendRow(UConfigPropertyArray* Rows, const FRowProps& Tpl, bool bTplReadable, const FString& Path, bool bShow, int32& InOutWithdrawn)
        {
            UConfigProperty* NewElement = Rows->AddNewElement();
            FRowProps R;
            if (!ReadRow(NewElement, R) || (bTplReadable && (R.Show == Tpl.Show || R.Resource == Tpl.Resource || R.Protected == Tpl.Protected)))
            {
                if (NewElement) { Rows->RemoveElementAtIndex(Rows->Values.Num() - 1); }
                ++InOutWithdrawn;
                return false;
            }
            R.Resource->Value = Path;
            R.Protected->Value = true; // inert for protection: the latch adds only UNticked rows to its opt-out set
            R.Show->Value = bShow;
            FNodeShuffleModule::StampResourceRowLabel(R.Section, Path);
            return true;
        }
        void CaptureStartContext(UWorld* World)
        {
            GStartWorld = World;
            GStartGameList.Reset();
            GStartGameListRead = false;
            const AFGGameState* GS = World->GetGameState<AFGGameState>();
            AFGUnlockSubsystem* Unlocks = GS ? GS->GetUnlockSubsystem() : nullptr;
            if (!Unlocks) { return; }
            GStartGameListRead = true;
            // Read only: both views, as the knowledge pass checks both (NodeShuffleSubsystem.cpp, UnlockModdedScannerKnowledge).
            for (const TSubclassOf<UFGResourceDescriptor>& Cls : Unlocks->GetScannableResources()) { if (Cls.Get()) { GStartGameList.Add(Cls.Get()->GetPathName()); } }
            for (const FScannableResourcePair& Pair : Unlocks->GetScannableResourcePairs()) { if (Pair.ResourceDescriptor.Get()) { GStartGameList.Add(Pair.ResourceDescriptor.Get()->GetPathName()); } }
        }
        void ResetForWorld()
        {
            ResetHookSideForWorld();
            GLastRowsSignature = 0;
            GRowsLogged = false;
            GLastRowCount = -1;
            GWarnedUnreachable = false;
            GLastRefused = 0;
            GLastWithdrawn = 0;
            GLoggedDisabled = false;
            GCarryLogged = false;
            GCarryWaitLogged = false;
            GDeferPasses.Reset();
            GStartWorld.Reset();
            GStartGameList.Reset();
            GStartGameListRead = false;
        }
        // ---- THE ONE-TIME CARRY-OVER. Runs while the hidden flag is false; sets it true in the same pass, so it never runs again. ----
        bool RunCarryOverOnce(UConfigPropertySection* Root, UConfigPropertyArray* Rows, const FRowProps& Tpl, bool bTplReadable,
            int32& InOutWithdrawn, int32& InOutRefused)
        {
            UConfigPropertyBool* Flag = Cast<UConfigPropertyBool>(FindChild(Root, MergedFlagKey));
            UConfigPropertyArray* Retired = Cast<UConfigPropertyArray>(FindChild(Root, RetiredRowsKey));
            if (!Flag || Flag->Value)
            {
                if (!GCarryLogged)
                {
                    // Arity hand-counted: 3 format specifiers, 3 arguments.
                    UE_LOG(LogNodeShuffle, Display,
                        TEXT("SCANFILTER-CARRY none flagReachable=%d flagAlreadySet=%d retiredRowsPresent=%d: no switch value copied by the carry-over this world"),
                        Flag ? 1 : 0, (Flag && Flag->Value) ? 1 : 0, Retired ? Retired->Values.Num() : -1);
                    GCarryLogged = true;
                }
                return false;
            }
            TMap<FString, TArray<int32>> ByPath; // merged rows by path (several when a hand-edited file duplicates one)
            for (int32 i = 0; i < Rows->Values.Num(); ++i)
            {
                FRowProps M;
                if (!ReadRow(Rows->Values[i], M)) { continue; }
                const FString Path = M.Resource->Value.TrimStartAndEnd();
                if (!Path.IsEmpty()) { ByPath.FindOrAdd(Path).Add(i); }
            }
            TSet<FString> Carried;
            int32 Read = 0, Unreadable = 0, Copied = 0, Created = 0;
            const int32 WithdrawnBefore = InOutWithdrawn;
            const int32 RefusedBefore = InOutRefused;
            const TArray<TObjectPtr<UConfigProperty>> NoRetiredRows;
            const TArray<TObjectPtr<UConfigProperty>>& RetiredRows = Retired ? Retired->Values : NoRetiredRows;
            for (UConfigProperty* Element : RetiredRows)
            {
                UConfigPropertyBool* OldShow = nullptr;
                UConfigPropertyString* OldResource = nullptr;
                if (!ReadRetiredRow(Element, OldShow, OldResource)) { ++Unreadable; continue; }
                const FString Path = OldResource->Value.TrimStartAndEnd();
                if (Path.IsEmpty()) { ++Unreadable; continue; }
                ++Read;
                Carried.Add(Path);
                if (TArray<int32>* Indices = ByPath.Find(Path)) // a later retired row for the same path wins, as in the snapshot
                {
                    for (const int32 i : *Indices) { FRowProps M; if (ReadRow(Rows->Values[i], M)) { M.Show->Value = OldShow->Value; ++Copied; } }
                }
                else if (Rows->Values.Num() >= MaxRows) { ++InOutRefused; }
                else if (AppendRow(Rows, Tpl, bTplReadable, Path, OldShow->Value, InOutWithdrawn)) { ByPath.Add(Path).Add(Rows->Values.Num() - 1); ++Created; }
                NoteCarriedRow(Path, OldShow->Value); // round 3: a carried-over row is PLAYER-SET with its old value
            }
            int32 LeftAuto = 0; // round 3: every row the retired list did not name is AUTO; its box is not written here
            for (const TPair<FString, TArray<int32>>& Pair : ByPath) { if (!Carried.Contains(Pair.Key)) { LeftAuto += Pair.Value.Num(); } }
            const int32 Emptied = Retired ? Retired->Values.Num() : 0;
            if (Retired) { Retired->Values.Empty(); }
            Flag->Value = true;
            // Arity hand-counted: 9 format specifiers, 9 arguments. Round 3 replaced the starting-value counts with otherRowsLeftAuto.
            UE_LOG(LogNodeShuffle, Display,
                TEXT("SCANFILTER-CARRY done retiredRowsRead=%d retiredRowsUnreadable=%d valuesCopied=%d rowsCreated=%d rowsNotCreatedOverCap=%d rowsWithdrawn=%d otherRowsLeftAuto=%d retiredRowsEmptied=%d gameListRead=%d"),
                Read, Unreadable, Copied, Created, InOutRefused - RefusedBefore, InOutWithdrawn - WithdrawnBefore, LeftAuto, Emptied,
                GStartGameListRead ? 1 : 0);
            GCarryLogged = true;
            return true;
        }
    }
    bool AutoShowValue(const UObject* WorldContext, const FString& Path, const TCHAR** OutBasis, bool* bOutKnown)
    {
        const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
        const TCHAR* Basis = TEXT("notInGameScannableList");
        bool bOn = false;
        bool bKnown = true;
        if (IsModdedPath(Path) && !DefaultShownForPath(Path)) { Basis = TEXT("allMinable"); }
        else if (World == nullptr || !GStartWorld.IsValid() || GStartWorld.Get() != World) { bKnown = false; Basis = TEXT("noContextForThisWorld"); }
        else if (!GStartGameListRead) { bKnown = false; Basis = TEXT("gameListUnread"); }
        else if (GStartGameList.Contains(Path)) { bOn = true; Basis = TEXT("gameScannableList"); }
        if (OutBasis) { *OutBasis = Basis; }
        if (bOutKnown) { *bOutKnown = bKnown; }
        return bOn;
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
        UConfigPropertySection* Root = GetRoot(WorldContext, &Manager);
        UConfigPropertyArray* Rows = Cast<UConfigPropertyArray>(FindChild(Root, MergedRowsKey));
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

        // A count drop to zero (the list's Reset is refused since review F2; deleting every row still gets here). The add loop
        // below re-creates the rows it knows about; a re-created row has no record, so it is AUTO.
        if (GLastRowCount > 0 && Rows->Values.Num() == 0)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("SCANFILTER-ROWS emptied: the list held %d row(s) at the previous pass and holds 0 now; rows are re-created below for the managed set and the classes seen in the scanner list"),
                GLastRowCount);
        }

        CaptureStartContext(World);
        // The carry-over waits until the scannable list was read; until it has run no snapshot is published.
        UConfigPropertyBool* CarryFlag = Cast<UConfigPropertyBool>(FindChild(Root, MergedFlagKey));
        if (CarryFlag && !CarryFlag->Value && !GStartGameListRead)
        {
            GSnapWorld.Reset();
            if (!GCarryWaitLogged)
            {
                UE_LOG(LogNodeShuffle, Display, TEXT("SCANFILTER-CARRY waiting gameListRead=0: the carry-over and the snapshot wait for the unlock subsystem; every scanner list is returned unfiltered until then"));
                GCarryWaitLogged = true;
            }
            return;
        }
        FRowProps Tpl;
        const bool bTplReadable = ReadRow(Rows->DefaultValue.Get(), Tpl);
        int32 RefusedOverCap = 0; int32 Withdrawn = 0;
        EnsureRowStateRecordLoaded(); // round 3: before the carry-over, so the carried rows' records are not overwritten by the file
        ApplyPendingRowStateForgets(); // review r2 F1: before the carry-over, so a carried row's record is never forgotten
        const bool bCarried = RunCarryOverOnce(Root, Rows, Tpl, bTplReadable, Withdrawn, RefusedOverCap);

        // Existing rows (by path), and a first-pass label stamp (the in-game name may be known by now).
        const bool bFirstPassThisWorld = !GRowsLogged;
        TSet<FString> Existing;
        int32 Malformed = 0; int32 Blank = 0;
        for (UConfigProperty* Element : Rows->Values)
        {
            FRowProps R;
            if (!ReadRow(Element, R)) { ++Malformed; continue; }
            const FString Path = R.Resource->Value.TrimStartAndEnd();
            if (Path.IsEmpty()) { ++Blank; continue; }
            Existing.Add(Path);
            if (bFirstPassThisWorld) { FNodeShuffleModule::StampResourceRowLabel(R.Section, Path); }
        }
        // Round 3: a missing or unreadable record file is decided here, once, on the rows that exist BEFORE this pass's adds.
        ResolveMissingRecordOnce(Existing, bCarried);
        DropRecordsWithoutRows(Existing);

        // Source (a) first, then (b). Both through the same modded predicate the hook uses.
        TSet<FString> Managed;
        for (const FString& P : ManagedModdedPaths) { if (IsModdedPath(P)) { Managed.Add(P); } }
        TArray<FString> Wanted;
        for (const FString& P : Managed) { Wanted.Add(P); }
        for (const FString& P : GSeenNoRow) { if (IsModdedPath(P) && !Managed.Contains(P)) { Wanted.Add(P); } }

        int32 Added = 0; int32 Deferred = 0;
        for (const FString& Path : Wanted)
        {
            if (Existing.Contains(Path)) { continue; }
            // A resource the protection side has sighted, while its pass is pending, gets its row from that side
            // (SyncForeignResourceRowsToConfig), which announces a new protection row; creating it here first would skip that.
            if (FNodeShuffleModule::IsForeignRowPendingFromProtectionSync(Path))
            {
                int32& DeferPasses = GDeferPasses.FindOrAdd(Path);
                if (++DeferPasses <= MaxDeferPasses) { ++Deferred; continue; }
                if (DeferPasses == MaxDeferPasses + 1)
                {
                    // Arity hand-counted: 2 format specifiers, 2 arguments.
                    UE_LOG(LogNodeShuffle, Display, TEXT("SCANFILTER-ROWS deferralEnded resource=%s passesDeferred=%d: the protection pass had not added a row for this sighted resource, so the scanner side adds it"), *Path, MaxDeferPasses);
                }
            }
            if (Rows->Values.Num() >= MaxRows) { ++RefusedOverCap; continue; }
            const TCHAR* Basis = nullptr;
            const bool bDefaultShown = AutoShowValue(World, Path, &Basis, nullptr);
            if (!AppendRow(Rows, Tpl, bTplReadable, Path, bDefaultShown, Withdrawn)) { continue; }
            FString Name;
            const bool bFromItemName = InGameNameIfLoaded(Path, Name);
            if (!bFromItemName) { Name = LeafName(Path); }
            Existing.Add(Path);
            ++Added;
            const bool bA = Managed.Contains(Path);
            const bool bB = GSeenNoRow.Contains(Path);
            // Arity hand-counted: 7 format specifiers, 7 arguments. ns-scanfilter-4 appended startBasis.
            if (NsDiag(ENsDiag::Scanner)) { UE_LOG(LogNodeShuffle, Display,
                TEXT("SCANFILTER-ROWADD resource=%s name='%s' nameFromItemName=%d mount=%s startsShown=%d source=%s startBasis=%s"),
                *Path, *Name, bFromItemName ? 1 : 0, *MountLabel(Path), bDefaultShown ? 1 : 0,
                (bA && bB) ? TEXT("managed+scannerList") : (bA ? TEXT("managed") : TEXT("scannerList")), Basis ? Basis : TEXT("<none>")); }
        }
        if (Added > 0 || bCarried) { FNodeShuffleModule::SortResourceRows(Rows, TEXT("scanner-add")); } // T68 sort-last, after every append above
        // Round 3: every row's box, by path, for touch detection, the mirror and the record (NodeShuffleScanFilterState.cpp).
        TArray<FShowRow> Shows;
        for (UConfigProperty* Element : Rows->Values)
        {
            FRowProps R;
            if (!ReadRow(Element, R) || (bTplReadable && R.Show == Tpl.Show)) { continue; } // never write through the template's box
            FShowRow S; S.Path = R.Resource->Value.TrimStartAndEnd(); S.Show = R.Show;
            if (!S.Path.IsEmpty()) { Shows.Add(MoveTemp(S)); }
        }
        const FRowStateCounts States = UpdateRowStates(World, Shows);
        if (Added > 0 || bCarried || States.Mirrored > 0)
        {
            // The T60 three-call sequence (NodeShuffleForeignProtectConfig.cpp): live property -> save handler, cached
            // structs re-initialised, file written now. Runs only on a pass that added a row, ran the carry-over or mirrored a box.
            Rows->MarkDirty();
            Manager->MarkConfigurationDirty(FConfigId{"NodeShuffle", ""});
            Manager->FlushPendingSaves();
        }
        FlushRowStateRecordIfDirty(); // after the config file, so the record never runs ahead of the boxes it describes
        if ((RefusedOverCap > 0 || Withdrawn > 0) && (RefusedOverCap != GLastRefused || Withdrawn != GLastWithdrawn))
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("SCANFILTER-ROWS notAdded: refusedOverCap=%d cap=%d withdrawnSharedWithTemplate=%d; those resources keep the hook's no-row rule"),
                RefusedOverCap, MaxRows, Withdrawn);
        }
        GLastRefused = RefusedOverCap; GLastWithdrawn = Withdrawn;

        // Rebuild the snapshot from the LIVE tree: this is what delivers a switch flipped in the settings screen.
        GRowShown.Reset();
        GAutoRow.Reset();
        GAddOrder.Reset();
        int32 OnNotFound = 0; int32 OnFoundNotAddable = 0; FString OnNotFoundNames; // ON rows: path found no class (ResolveClass never loads) / found a class that is not a concrete UFGResourceDescriptor child
        int32 Shown = 0; int32 Hidden = 0;
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
            const bool bPlayerSet = IsRowPlayerSet(Path); // round 3: only a PLAYER-SET row adds, removes or answers the pick
            if (bPlayerSet) { GRowShown.Add(Path, bShow); } else { GAutoRow.Add(Path); }
            if (bShow && bPlayerSet)
            {
                UClass* RowCls = FSoftClassPath(Path).ResolveClass();
                if (RowCls && RowCls->IsChildOf(UFGResourceDescriptor::StaticClass()) && !RowCls->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) { GAddOrder.Add(RowCls); }
                else if (RowCls) { ++OnFoundNotAddable; }
                else { ++OnNotFound; OnNotFoundNames += (OnNotFoundNames.IsEmpty() ? TEXT("") : TEXT(" ")) + LeafName(Path); }
            }
            FMountCount& M = PerMount.FindOrAdd(MountLabel(Path));
            ++M.Total;
            if (bShow) { ++Shown; ++M.Shown; } else { ++Hidden; ++M.Hidden; }
            Signature = HashCombine(Signature, HashCombine(GetTypeHash(Path), bPlayerSet ? (bShow ? 1u : 2u) : (bShow ? 3u : 4u)));
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
            // Arity hand-counted: 18 format specifiers, 18 arguments. ns-scanfilter-4 appended deferredToProtectionSync; round 3
            // appended the row-state counts (every non-blank row, base-game included) and counts GAutoRow in rowsInSnapshot.
            UE_LOG(LogNodeShuffle, Display,
                TEXT("SCANFILTER-ROWS %s rowsTotal=%d rowsInSnapshot=%d shown=%d hidden=%d malformed=%d blankPath=%d addedThisPass=%d perMount:%s onRowsClassNotFound=%d onRowsFoundNotAddable=%d classNotFound=[%s] deferredToProtectionSync=%d autoRows=%d playerSetRows=%d becamePlayerSetThisPass=%d mirroredThisPass=%d mirrorValueUnknown=%d"),
                GRowsLogged ? TEXT("changed") : TEXT("loaded"), Rows->Values.Num(), GRowShown.Num() + GAutoRow.Num(), Shown, Hidden, Malformed, Blank,
                Added, *PerMountText, OnNotFound, OnFoundNotAddable, *OnNotFoundNames, Deferred, States.AutoRows, States.PlayerSetRows,
                States.BecamePlayerSet, States.Mirrored, States.MirrorUnknown);
            GRowsLogged = true;
            GLastRowsSignature = Signature;
        }
    }

    // ---- THE FIELD AND THE RETIRED ROWS ----
    void AddShowFieldToRowTemplate(UConfigPropertySection* RowTemplate, UClass* BoolClass)
    {
        UConfigPropertyBool* Show = NewObject<UConfigPropertyBool>(RowTemplate, BoolClass, TEXT("ShowInScanner"));
        Show->Value = false;        // a row read from a file without this key reads false; an AUTO row's box is then set by the mirror
        Show->DefaultValue = false;
        Show->DisplayName = FText::FromString(TEXT("Show in scanner"));
        // Every factual assertion is graded in the ns-scanfilter-4 handoff.
        Show->Tooltip = FText::FromString(TEXT("Takes effect in a running game: reopen the scanner. Until you change this box, NodeShuffle leaves this resource's scanner entry to the game and, in a world you host with 'Enabled' on, keeps the box matching the game's own list of scannable resources once it has read that list; a resource from All Minable is kept out of the list NodeShuffle hands the scanner, and its box is kept OFF. Once you change the box, it stays as you set it (a value carried over from the old scanner list counts as changed). ON: NodeShuffle adds this resource to the list it hands the resource scanner, even if you have not researched it, once the game has loaded the resource. OFF: NodeShuffle takes it out of that list. Has no effect on a base-game resource. Nothing is written to your save."));
        Show->bRequiresWorldReload = false; // the one field of the row that is editable in a running world
        RowTemplate->SectionProperties.Add(TEXT("ShowInScanner"), Show);
    }

    void RegisterConfigRows(UConfigPropertySection* Root, UClass* ArrayClass, UClass* SectionClass, UClass* StringClass,
        UClass* BoolClass)
    {
        // THE RETIRED LIST. Still registered so SML reads its rows from an existing file (an unregistered key is dropped on the next
        // save); hidden, and emptied by the carry-over. Its struct mirror stays in NodeShuffleConfig.h for the same reason.
        UConfigPropertyArray* Retired = NewObject<UConfigPropertyArray>(Root, ArrayClass, RetiredRowsKey);
        Retired->bRequiresWorldReload = false;
        Retired->bHidden = true;
        Retired->DisplayName = FText::FromString(TEXT("Retired scanner list (moved into the protection list)"));
        Retired->Tooltip = FText::FromString(TEXT("Not a setting. NodeShuffle copies any rows here into 'Show in scanner' in the per-resource list once, then empties this list."));
        UConfigPropertySection* RowTemplate = NewObject<UConfigPropertySection>(Retired, SectionClass, TEXT("ScannerResourceRow"));
        RowTemplate->bRequiresWorldReload = false;
        RowTemplate->bHidden = true;
        UConfigPropertyBool* OldShow = NewObject<UConfigPropertyBool>(RowTemplate, BoolClass, TEXT("ShowInScanner"));
        OldShow->Value = true; OldShow->DefaultValue = true; OldShow->bRequiresWorldReload = false; OldShow->bHidden = true;
        RowTemplate->SectionProperties.Add(TEXT("ShowInScanner"), OldShow);
        for (const TCHAR* Key : { TEXT("Name"), TEXT("Mod"), TEXT("Resource") }) // the old row shape, so old rows still deserialize
        {
            UConfigPropertyString* P = NewObject<UConfigPropertyString>(RowTemplate, StringClass, FName(Key));
            P->Value = TEXT(""); P->DefaultValue = TEXT(""); P->bRequiresWorldReload = false; P->bHidden = true;
            RowTemplate->SectionProperties.Add(Key, P);
        }
        Retired->DefaultValue = RowTemplate;
        Root->SectionProperties.Add(RetiredRowsKey, Retired);

        // THE CARRY-OVER FLAG. Hidden, never reset by the player, so the carry-over cannot run twice.
        UConfigPropertyBool* Flag = NewObject<UConfigPropertyBool>(Root, BoolClass, MergedFlagKey);
        Flag->Value = false;
        Flag->DefaultValue = false;
        Flag->bRequiresWorldReload = false;
        Flag->bHidden = true;
        Flag->bAllowUserReset = false;
        Flag->DisplayName = FText::FromString(TEXT("Scanner switches carried over (internal)"));
        Flag->Tooltip = FText::FromString(TEXT("Not a setting. NodeShuffle sets this once, when it gives every row of the per-resource list its 'Show in scanner' value."));
        Root->SectionProperties.Add(MergedFlagKey, Flag);
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
