// rwg-layout-dump-1 (2026-10-29): `NodeShuffle.DumpLayout` -- DIAGNOSTICS ONLY. LOG-ONLY: it gates nothing, places nothing, moves
// nothing, spawns nothing and writes no layout, well or original-record field.
//
// QUESTION IT FEEDS. Three 1 km disks around the player read 40-63% of "expected" while their 3 km RWGDENSITY cells read 92-103%
// (_team/test-logs/2026-10-29-nodeshuffle-trace-diag-2-run3.md D-E). This command prints the WHOLE population with positions so an
// offline script (tools/layout_spacing.py) can test clumping against a null that uses the dealer's own acceptance predicate,
// with no walking. It prints, in one frame:
//   LAYOUTDUMP-BEGIN   counts of every class it will print, then LAYOUTDUMP-LEGEND prose lines;
//   LAYOUTDUMP-E       one per ANodeShuffleSubsystem::Layout entry (every entry: active, inactive, cave, kept originals);
//   LAYOUTDUMP-W       one per WellLayout entry (well cores live in WellLayout, NOT in Layout; satellites are counted, not listed);
//   LAYOUTDUMP-O       one per OriginalNodeRecord (the hidden originals);
//   LAYOUTDUMP-G       one per 1000 m grid cell over the API deal square: land and dealable sample counts and fractions;
//   LAYOUTDUMP-END     the printed counts beside the BEGIN counts (the control: they must match), latch masks, timings.
//   LAYOUTDUMP-TWINS   (rwg-rerolldup-1, after CHAT) active entries sharing one VanillaNodePath, then LAYOUTDUMP-TWIN per shared path.
//
// NO TRACES. RaycastGroundAt teaches the persistent water grid (see NodeShuffleAuditPlacements.cpp's header), so the grid uses only
// the height API: the dealable test is RwgDensityLandSample (NodeShuffleMapModeDensity.cpp), the shared helper that runs the
// dealer's land chain from RwgDealOneSpot (NodeShuffleMapModePlace.cpp) -- Outside, RwgRejectV3 at the API surface, then
// RwgApiSlopeTooSteep -- through the same functions. The land fraction is a second reading of RwgRejectV3 alone. RwgRejectV3 is
// non-const for its session latches (sea / void / killbox checks); the latch mask is printed before and after the grid.
//
// SYNCHRONOUS, one frame, on purpose: a typed one-off. Measured predicate cost elsewhere is ~30 us per sample (RWGDENSITY 6400
// samples in 189 ms), so 900 cells x 36 samples x two predicate reads is a hitch of roughly one to three seconds; END prints the
// measured gridMs and elapsedMs so the cost is a number, not this estimate.
//
// NOT gated behind EnableDiagnostics, same precedent as NodeShuffle.Here / AuditPlacements: a typed command is its own gate.
// TODO(2026-10-29, rwg-layout-dump-1): diagnostic command -- gate behind EnableDiagnostics or strip before release.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffleRerollIdentity.h" // rwg-rerolldup-2: identity twins (resource + anchor) the path-keyed census cannot see
#include "NodeShuffle.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"
#include "Resources/FGResourceNode.h"

namespace
{
    constexpr double LayoutDumpCellCm = 100000.0;  // 1000 m grid cells
    constexpr int32 LayoutDumpSubSample = 6;        // samples per cell side (6 x 6 = 36 per cell, cell-centred lattice, ~167 m apart)
    int32 GLayoutDumpId = 0;                        // per process; BEGIN and END carry the same value

    FString LayoutDumpShortName(const FString& P)
    {
        int32 Dot = INDEX_NONE;
        const FString S = P.FindLastChar(TEXT('.'), Dot) ? P.Mid(Dot + 1) : P;
        return S.IsEmpty() ? FString(TEXT("none")) : S.Replace(TEXT(" "), TEXT("_"));
    }
}

// TODO(2026-10-29, rwg-layout-dump-1): diagnostic command -- gate behind EnableDiagnostics or strip before release.
static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleDumpLayoutCmd(
    TEXT("NodeShuffle.DumpLayout"),
    TEXT("RWG only. Log every layout entry, well core and hidden original with its position, plus a 1000 m grid of the dealer's ")
    TEXT("land and dealable fractions from the height API (no traces). Log-only; one frame; posts one chat line when done."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
    {
        if (!World)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-SKIP: no world; nothing was read."));
            return;
        }
        for (TActorIterator<ANodeShuffleSubsystem> It(World); It; ++It)
        {
            It->DumpLayout();
            return;
        }
        UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-SKIP: NodeShuffle subsystem not found in this world; nothing was read."));
    }));

void ANodeShuffleSubsystem::DumpLayout()
{
    const ENodeShuffleMapMode Mode = GetMapMode();
    if (Mode != ENodeShuffleMapMode::Rwg)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-SKIP: map mode is not RWG (mode enum %d); nothing was read."), static_cast<int32>(Mode));
        return;
    }
    const double T0 = FPlatformTime::Seconds();
    const int32 DumpId = ++GLayoutDumpId;

    // ---- class counts (every Layout entry lands in exactly one of four classes) ----
    int32 NSurface = 0, NCave = 0, NInactive = 0, NNotNew = 0;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bIsNewNode) { ++NNotNew; }
        else if (!E.bActive) { ++NInactive; }
        else if (E.bRwgCave) { ++NCave; }
        else { ++NSurface; }
    }
    int32 NSatellites = 0;
    for (const FNodeShuffleWellEntry& W : WellLayout) { NSatellites += W.Satellites.Num(); }

    const NodeShuffleMapMode::FRwgGroundQuery Q(GetWorld(), RwgGenerator.Get());
    const bool bApi = Q.IsUsable();
    // The grid reads RwgRejectV3, whose sea / void / killbox graders run, defer (5 s window) or spend a killbox try on their FIRST
    // call of a session. Sample only once all three have finished, so here they return latched verdicts and the dump never grades.
    const bool bGradesDone = bRwgSeaCheckDone && bRwgVoidCheckDone && bRwgKillboxDone;
    const bool bGrid = bApi && bGradesDone;
    const double H = bApi ? Q.HalfExtent() : 0.0;
    const int32 Cols = bGrid ? FMath::CeilToInt32((2.0 * H) / LayoutDumpCellCm) : 0;
    const int32 Rows = Cols;
    const int32 GridExpected = Cols * Rows;

    UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-BEGIN: dumpId=%d map=rwg seed=%d layoutEntries=%d surface=%d cave=%d inactive=%d notNew=%d wellCores=%d satellites=%d originals=%d gridCells=%d gridCols=%d gridRows=%d cellCm=%.0f samplesPerCell=%d halfExtentCm=%.0f apiUsable=%d gradesDone=%d"),
        DumpId, SavedSeed, Layout.Num(), NSurface, NCave, NInactive, NNotNew, WellLayout.Num(), NSatellites, OriginalNodeRecord.Num(), GridExpected, Cols, Rows, LayoutDumpCellCm, LayoutDumpSubSample * LayoutDumpSubSample, H, bApi ? 1 : 0, bGradesDone ? 1 : 0);
    UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-LEGEND: Positions are world centimetres. Entry lines give each layout entry's CURRENT saved location, after any redeal or resettle, not the spot first rolled; inactive entries are never spawned and are not dealt spots; notNew counts entries that are original level nodes, not dealt by the mod."));
    UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-LEGEND: Purity is the enum integer (0 impure, 1 normal, 2 pure, 3 unset); live means a spawned actor for the entry is valid in this session right now; rayCasted means the entry was settled by a trace at some earlier time."));
    UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-LEGEND: Well lines come from the separate well layout; their position source names which saved location was printed (placed core, dealt destination, RWG claim, vanilla core, or none). Original lines print the record's true location when set, else its recorded location, and the purity of the layout entry sharing its level path or minus one when no entry matches."));
    UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-LEGEND: Grid lines cover the API deal square in 1000 m cells from its minimum corner; edge cells may extend past it. Each cell samples a centred lattice through the height API only: land counts samples passing the RWG reject predicate at the API surface, dealable counts samples passing the dealer's whole land chain (outside, reject predicate, slope). Spacing and the cave quota are not part of dealable."));
    UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-LEGEND: The end line repeats every count beside what the begin line announced; countsMatch is one only when all of them agree. The dump runs in a single frame; it prints one line per layout entry, well core, original record and grid cell that the begin line announces, plus the begin, legend, end and chat lines. After the chat line it also prints one twin census line, and one twin line per level path that more than one active entry shares; those lines are outside the end line's counts."));
    UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-LEGEND: Grid lines print only when this session's sea, void and killbox checks have all finished; otherwise the grid count is zero. Latch masks add 1 sea usable, 2 void usable, 4 killbox known, 8 sea checked, 16 void checked, 32 killbox checked."));

    // ---- entries ----
    int32 EntriesPrinted = 0;
    for (int32 I = 0; I < Layout.Num(); ++I)
    {
        const FNodeShuffleEntry& E = Layout[I];
        AFGResourceNode* const* Live = SpawnedNodes.Find(E.EntryGuid);
        const bool bLive = Live && IsValid(*Live);
        // rwg-clifftop-1 E3: guid= prints the full 32-hex entry guid (was its first 8 hex digits).
        UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-E: i=%d guid=%s res=%s purity=%d x=%.0f y=%.0f z=%.0f cave=%d rayCasted=%d live=%d active=%d newNode=%d pinned=%d"),
            I, *E.EntryGuid.ToString(), *LayoutDumpShortName(E.AssignedResourceClassPath), static_cast<int32>(E.AssignedPurity.GetValue()),
            E.Location.X, E.Location.Y, E.Location.Z, E.bRwgCave ? 1 : 0, E.bRayCasted ? 1 : 0, bLive ? 1 : 0, E.bActive ? 1 : 0, E.bIsNewNode ? 1 : 0, E.bPinned ? 1 : 0);
        ++EntriesPrinted;
    }

    // ---- well cores ----
    int32 WellsPrinted = 0;
    for (int32 I = 0; I < WellLayout.Num(); ++I)
    {
        const FNodeShuffleWellEntry& W = WellLayout[I];
        FVector P = FVector::ZeroVector;
        const TCHAR* Src = TEXT("none");
        if (W.bGroupPlaced && !W.PlacedCoreLocation.IsNearlyZero()) { P = W.PlacedCoreLocation; Src = TEXT("placed"); }
        else if (W.bDestDealt && !W.DestCoreLocation.IsNearlyZero()) { P = W.DestCoreLocation; Src = TEXT("dest"); }
        else if (!W.RwgClaimCoreLocation.IsNearlyZero()) { P = W.RwgClaimCoreLocation; Src = TEXT("claim"); }
        else if (!W.VanillaCoreLocation.IsNearlyZero()) { P = W.VanillaCoreLocation; Src = TEXT("vanilla"); }
        UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-W: i=%d guid=%s res=%s x=%.0f y=%.0f z=%.0f src=%s satellites=%d placed=%d managed=%d pinned=%d"),
            I, *W.RwgClaimGuid.ToString().Left(8), *LayoutDumpShortName(W.AssignedResourceClassPath), P.X, P.Y, P.Z, Src, W.Satellites.Num(),
            W.bGroupPlaced ? 1 : 0, W.bManaged ? 1 : 0, W.bPinned ? 1 : 0);
        ++WellsPrinted;
    }

    // ---- hidden originals ----
    TMap<FString, int32> PurityByPath;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.VanillaNodePath.IsEmpty()) { PurityByPath.Add(E.VanillaNodePath, static_cast<int32>(E.OriginalPurity.GetValue())); }
    }
    int32 OriginalsPrinted = 0, OriginalsPurityKnown = 0;
    for (int32 I = 0; I < OriginalNodeRecord.Num(); ++I)
    {
        const FNodeShuffleSuppressedOriginal& R = OriginalNodeRecord[I];
        const bool bTrue = !R.TrueLocation.IsNearlyZero();
        const FVector A = bTrue ? R.TrueLocation : R.Location; // the same anchor TryRedealRwgEntry's spacing test uses
        const int32* Pur = R.VanillaNodePath.IsEmpty() ? nullptr : PurityByPath.Find(R.VanillaNodePath);
        if (Pur) { ++OriginalsPurityKnown; }
        UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-O: i=%d res=%s purity=%d x=%.0f y=%.0f z=%.0f trueSet=%d modded=%d"),
            I, *LayoutDumpShortName(R.OriginalResourceClassPath), Pur ? *Pur : -1, A.X, A.Y, A.Z, bTrue ? 1 : 0, R.bModdedOrigin ? 1 : 0);
        ++OriginalsPrinted;
    }

    // ---- grid (height API only; no traces) ----
    const int32 LatchMaskBefore = (bRwgSeaUsable ? 1 : 0) | (bRwgVoidUsable ? 2 : 0) | (bRwgKillboxKnown ? 4 : 0) | (bRwgSeaCheckDone ? 8 : 0) | (bRwgVoidCheckDone ? 16 : 0) | (bRwgKillboxDone ? 32 : 0);
    int32 GridPrinted = 0, DealableNotLand = 0;
    const double TG = FPlatformTime::Seconds();
    if (bGrid)
    {
        if (RwgWaterVolumes.Num() == 0) { RwgBuildWaterVolumeCache(); } // same cache build as RwgEmitDensityCensus
        constexpr int32 S = LayoutDumpSubSample;
        for (int32 Cy = 0; Cy < Rows; ++Cy)
        {
            for (int32 Cx = 0; Cx < Cols; ++Cx)
            {
                const double XMin = -H + Cx * LayoutDumpCellCm;
                const double YMin = -H + Cy * LayoutDumpCellCm;
                int32 Land = 0, Dealable = 0;
                for (int32 Sy = 0; Sy < S; ++Sy)
                {
                    for (int32 Sx = 0; Sx < S; ++Sx)
                    {
                        const double X = XMin + (Sx + 0.5) / S * LayoutDumpCellCm;
                        const double Y = YMin + (Sy + 0.5) / S * LayoutDumpCellCm;
                        const bool bLand = !Q.Outside(X, Y) && RwgRejectV3(Q, FVector(X, Y, Q.SurfaceZ(X, Y))) == 0;
                        const bool bDeal = RwgDensityLandSample(Q, X, Y); // the dealer's chain (RwgDealOneSpot), shared helper
                        if (bLand) { ++Land; }
                        if (bDeal) { ++Dealable; if (!bLand) { ++DealableNotLand; } }
                    }
                }
                UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-G: cx=%d cy=%d xMinCm=%.0f yMinCm=%.0f samples=%d land=%d dealable=%d landFrac=%.4f dealableFrac=%.4f"),
                    Cx, Cy, XMin, YMin, S * S, Land, Dealable, static_cast<double>(Land) / (S * S), static_cast<double>(Dealable) / (S * S));
                ++GridPrinted;
            }
        }
    }
    const double GridMs = (FPlatformTime::Seconds() - TG) * 1000.0;
    const int32 LatchMaskAfter = (bRwgSeaUsable ? 1 : 0) | (bRwgVoidUsable ? 2 : 0) | (bRwgKillboxKnown ? 4 : 0) | (bRwgSeaCheckDone ? 8 : 0) | (bRwgVoidCheckDone ? 16 : 0) | (bRwgKillboxDone ? 32 : 0);

    const bool bMatch = EntriesPrinted == Layout.Num() && WellsPrinted == WellLayout.Num() && OriginalsPrinted == OriginalNodeRecord.Num() && GridPrinted == GridExpected;
    UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-END: dumpId=%d entriesPrinted=%d entriesExpected=%d wellsPrinted=%d wellsExpected=%d originalsPrinted=%d originalsExpected=%d originalsPurityKnown=%d gridCellsPrinted=%d gridCellsExpected=%d countsMatch=%d dealableNotLand=%d latchMaskBefore=%d latchMaskAfter=%d gridMs=%.0f elapsedMs=%.0f"),
        DumpId, EntriesPrinted, Layout.Num(), WellsPrinted, WellLayout.Num(), OriginalsPrinted, OriginalNodeRecord.Num(), OriginalsPurityKnown,
        GridPrinted, GridExpected, bMatch ? 1 : 0, DealableNotLand, LatchMaskBefore, LatchMaskAfter, GridMs, (FPlatformTime::Seconds() - T0) * 1000.0);

    // The user must SEE that it ran (NodeShuffle.Here is log-only and was invisible): one chat line, through the shared emitter.
    const FString Body = FString::Printf(TEXT("NodeShuffle layout dump %d written to the log: %d entries, %d well cores, %d originals, %d grid cells, seed %d."),
        DumpId, EntriesPrinted, WellsPrinted, OriginalsPrinted, GridPrinted, SavedSeed);
    const bool bPosted = FNodeShuffleModule::PostChatNotice(GetWorld(), Body, TEXT("LAYOUTDUMP"));
    UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-CHAT: dumpId=%d posted=%d"), DumpId, bPosted ? 1 : 0);

    // rwg-rerolldup-1: TWINS -- active layout entries that share one original level path (VanillaNodePath). Measures, from the
    // saved layout alone, whether one original is carried by more than one active entry. Printed AFTER the END/CHAT pair so the
    // BEGIN..END block the offline parsers read is unchanged. One census line, then one line per shared path (no per-entry path).
    TMap<FString, TArray<int32>> ActiveByPath;
    int32 ActiveWithPath = 0;
    for (int32 I = 0; I < Layout.Num(); ++I)
    {
        const FNodeShuffleEntry& E = Layout[I];
        if (!E.bActive || E.VanillaNodePath.IsEmpty()) { continue; }
        ActiveByPath.FindOrAdd(E.VanillaNodePath).Add(I);
        ++ActiveWithPath;
    }
    TArray<FString> SharedPaths;
    int32 EntriesOnShared = 0, PinnedOnShared = 0;
    for (const TPair<FString, TArray<int32>>& KV : ActiveByPath)
    {
        if (KV.Value.Num() < 2) { continue; }
        SharedPaths.Add(KV.Key);
        EntriesOnShared += KV.Value.Num();
        for (const int32 I : KV.Value) { if (Layout[I].bPinned) { ++PinnedOnShared; } }
    }
    SharedPaths.Sort();
    UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-TWINS dumpId=%d pathsOnMultipleActiveEntries=%d entriesOnSharedPaths=%d pinnedOnSharedPaths=%d activeWithPath=%d"),
        DumpId, SharedPaths.Num(), EntriesOnShared, PinnedOnShared, ActiveWithPath);
    for (const FString& P : SharedPaths)
    {
        const TArray<int32>& Idx = ActiveByPath.FindChecked(P);
        int32 Pinned = 0;
        FString Guids;
        for (const int32 I : Idx)
        {
            if (Layout[I].bPinned) { ++Pinned; }
            if (!Guids.IsEmpty()) { Guids += TEXT(","); }
            Guids += Layout[I].EntryGuid.ToString();
        }
        UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-TWIN dumpId=%d path=%s entries=%d pinned=%d guids=%s"),
            DumpId, *FPackageName::ObjectPathToObjectName(P), Idx.Num(), Pinned, *Guids);
    }
    // rwg-rerolldup-2: the path-keyed census above cannot see two rows of one original that hold two different stale names (a
    // renamed runtime original whose load-time rematch rebound only one row). Count them by resource + anchor within the identity
    // radius; pathsOnMultipleActiveEntries alone is a lower bound on the twin population.
    const int32 IdentityTwinPaths = NodeShuffleRerollIdentity::ReportIdentityTwins(Layout, NodeShuffleRerollIdentity::OriginalIdentityRadiusCm, DumpId, 32);
    UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-IDTWINS dumpId=%d identityTwinPaths=%d radiusCm=%.0f"),
        DumpId, IdentityTwinPaths, NodeShuffleRerollIdentity::OriginalIdentityRadiusCm);
}
