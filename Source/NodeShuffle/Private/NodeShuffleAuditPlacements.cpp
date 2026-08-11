// ns-t64-rock-hide-and-audit, WORK ITEM B: `NodeShuffle.AuditPlacements` -- THE WHOLE-POPULATION
// PLACEMENT AUDIT. LOG-ONLY. IT GATES NOTHING, PLACES NOTHING, MOVES NOTHING AND REFUSES NOTHING.
//
// WHY A POPULATION WALK RATHER THAN ANOTHER NEAREST-ONE PROBE. This mod already has four point probes
// (NodeShuffle.Here, PointAtHere, WellProbe, ProbeNearestNode) and every one of them answers about ONE
// point the player is next to. The defect class this workspace pays for most is a WRONG POPULATION that
// reads as a clean measurement -- a result measured where the player happens to be standing, reported as
// if it held for the map. So this command walks EVERY layout entry, INCLUDING the ones it cannot judge:
// an entry that is not ours, is inactive, or has never been settled gets its own bucket and is counted,
// never silently dropped. A bucket count with no denominator is the thing this file exists to avoid.
//
// FOUR READINGS PER ENTRY, REPORTED AS FOUR SEPARATE FIELDS BECAUSE THEY ARE KNOWN TO DISAGREE:
//   1. WATER, from the settle gate's own signal -- RaycastGroundAt's bOutWater out-param, the same probe
//      and the same flag RaycastSettle reads to reject a spot. Reported beside the LEARNED water grid's
//      verdict for the same point (IsKnownWaterCell), which is a different instrument answering a
//      different question (what the roll believed) and is NOT folded into the first.
//   2. BELOW TERRAIN, from that same probe's ground hit compared against the entry's RECORDED Z. The
//      tolerance is this command's own constant, stated in the census line rather than left implicit.
//   3. The SHIPPED 8-ray enclosure gate (ANodeShuffleSubsystem::IsSpotEnclosed), the same member
//      function both placement paths call, with the player's pawn on its ignore list exactly as the four
//      existing probe commands hand it.
//   4. The DEEP containment instrument (RunTotallyInsideProbe, ns-t53), whose verdict is positive-only
//      by construction and which is a SHADOW metric -- nothing gates on it.
// 3 and 4 disagree by design (docs/TECH-DEBT.md T41: the shipped gate read 0 of 8 blocked at a member
// sitting in a cliff face), so this command never merges them into one "is it bad" verdict.
//
// SIDE EFFECTS, STATED RATHER THAN ASSUMED ABSENT. RaycastGroundAt teaches the persistent, cross-save
// water grid one cell per call, and this command probes points the roll may never have sampled. That
// would be an audit changing what a later roll believes -- so each tick sets
// bWaterGridLearningSuppressed for its own duration and RecordWaterGridSample early-outs on it. With
// that latch the command's only effect on the process is log output and CPU.
//
// TIME-SLICED, AND THE SLICE IS SMALLER THAN THE BRIEF'S "~50 PER TICK" ON PURPOSE: the deep instrument
// issues roughly thirty traces/sweeps/overlaps PER ENTRY (8 directions x 4 instruments plus the centre
// overlap), so 50 entries would be ~1500 queries inside one frame -- the hitch the slicing exists to
// prevent. 25 keeps the stated purpose. The actual query and overlap totals are MEASURED and printed,
// so the cost is a number in the log rather than an estimate in a comment.
//
// NOT DIAGNOSTICS-GATED, matching NodeShuffle.Here / PointAtHere / WellProbe / ProbeNearestNode: a
// command a human types IS its own gate, and one that silently does nothing is worse than none.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleTotallyInside.h"

#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

#include "Resources/FGResourceNode.h"

namespace
{
    // How many entries one tick examines. See the file header for why this is 25 and not 50.
    constexpr int32 AuditEntriesPerTick = 25;
    // How far the ground surface must sit ABOVE an entry's recorded Z before this command calls the
    // centre below-terrain. This command's own constant; it is printed in the census so no reader has to
    // find it here. It is NOT a placement threshold and nothing in the mod gates on it.
    constexpr double AuditBelowTerrainToleranceCm = 100.0;
    constexpr int32 AuditMaxOffenderLines = 50;

    // Bucket slots of ANodeShuffleSubsystem::PlacementAuditCounts. Kept as one list so the census's
    // field order and these indices cannot drift apart.
    enum EAuditBucket : int32
    {
        AB_Examined = 0,        // entries walked -- reconciles against Layout.Num()
        AB_NotOurs,             // bIsNewNode false: the entry's Location is a vanilla original's
        AB_Inactive,            // bActive false
        AB_UnsettledNoActor,    // never ground-settled AND no live spawned actor: no centre to judge
        AB_Audited,             // DENOMINATOR for every bucket below
        AB_Water,               // the settle probe reported its ground hit underwater
        AB_KnownWaterCell,      // the LEARNED grid calls this cell water (a different instrument)
        AB_NoGroundHit,         // the probe found no ground: the below-terrain test could not run
        AB_BelowTerrain,        // ground surface above the recorded Z by more than the tolerance
        AB_EnclosureRefuse,     // the SHIPPED 8-ray gate refuses this point
        AB_TotallyInside,       // the deep instrument's positive-only verdict
        AB_InsideNotMeasured,   // that instrument made no query, so it returned no verdict
        AB_VerdictsDisagreed,   // gate verdict != deep verdict, counted because they are known to differ
        AB_Offenders,           // entries with at least one adverse reading
        AB_BucketCount
    };

    // Severity ranks. Printed in the census so the ordering of the offender lines is stated rather than
    // inferred. These rank the OFFENDER LIST only; nothing in the mod reads them.
    constexpr int32 AuditSevTotallyInside = 4;
    constexpr int32 AuditSevBelowTerrain  = 3;
    constexpr int32 AuditSevWater         = 2;
    constexpr int32 AuditSevEnclosure     = 1;
}

static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleAuditPlacementsCmd(
    TEXT("NodeShuffle.AuditPlacements"),
    TEXT("Walk EVERY layout entry and report, per entry's recorded centre, four separate readings -- the ")
    TEXT("settle gate's water signal, a below-terrain comparison, the shipped 8-ray enclosure gate's ")
    TEXT("verdict, and the deep containment instrument's verdict -- as bucket counts with denominators ")
    TEXT("plus the worst offenders. Log-only; time-sliced across ticks."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
    {
        if (!World)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("AUDITPLACEMENTS: no world (main menu / no session loaded?) -- nothing was walked ")
                TEXT("and no query was made."));
            return;
        }
        for (TActorIterator<ANodeShuffleSubsystem> It(World); It; ++It)
        {
            It->StartPlacementAudit();
            return;
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("AUDITPLACEMENTS: NodeShuffle subsystem not found in this world -- no entry was walked ")
            TEXT("and no gate was run."));
    }));

void ANodeShuffleSubsystem::StartPlacementAudit()
{
    UWorld* W = GetWorld();
    if (!W)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("AUDITPLACEMENTS: no world -- nothing was walked and no query was made."));
        return;
    }
    if (bPlacementAuditRunning)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("AUDITPLACEMENTS: an audit is already running -- it has examined %d of %d entr(ies) so ")
            TEXT("far. This request started nothing; wait for the completion line."),
            PlacementAuditCursor, PlacementAuditLayoutNumAtStart);
        return;
    }

    bPlacementAuditRunning = true;
    PlacementAuditCursor = 0;
    PlacementAuditOffenders.Reset();
    for (int32 i = 0; i < AB_BucketCount && i < UE_ARRAY_COUNT(PlacementAuditCounts); ++i) { PlacementAuditCounts[i] = 0; }
    PlacementAuditQueries = 0;
    PlacementAuditOverlaps = 0;
    PlacementAuditLayoutNumAtStart = Layout.Num();
    PlacementAuditStartSeconds = W->GetTimeSeconds();

    UE_LOG(LogNodeShuffle, Display,
        TEXT("===== AUDITPLACEMENTS BEGIN ===== %d layout entr(ies) to walk, %d per tick. It reads only: ")
        TEXT("it spawns nothing, moves nothing, writes no layout field, refuses nothing, and its water ")
        TEXT("probes are prevented from teaching the persistent water grid for the duration. Entries it ")
        TEXT("cannot judge are counted in their own buckets, not dropped."),
        PlacementAuditLayoutNumAtStart, AuditEntriesPerTick);

    W->GetTimerManager().SetTimer(PlacementAuditTimer, this,
        &ANodeShuffleSubsystem::PlacementAuditTick, 0.05f, true);
}

void ANodeShuffleSubsystem::PlacementAuditTick()
{
    UWorld* W = GetWorld();
    if (!W || !bPlacementAuditRunning)
    {
        if (W) { W->GetTimerManager().ClearTimer(PlacementAuditTimer); }
        bPlacementAuditRunning = false;
        return;
    }

    // The pawn is used for exactly what the four existing probe commands use it for: the SHIPPED
    // enclosure predicate's ignore list. It is not the containment instrument's subject and it is not a
    // distance filter -- this command walks the whole population regardless of where the player is.
    APawn* Pawn = nullptr;
    for (FConstPlayerControllerIterator PIt = W->GetPlayerControllerIterator(); PIt; ++PIt)
    {
        if (APlayerController* Pc = PIt->Get())
        {
            if (APawn* P = Pc->GetPawn()) { Pawn = P; break; }
        }
    }

    // THE LATCH THAT MAKES "MEASUREMENT ONLY" LITERALLY TRUE -- see the file header.
    // T64 cold review F5: RAII, not a paired assignment. "Cleared on every exit path" is true of today's
    // body and is exactly the claim a later early-return falsifies silently -- the symptom would be the
    // persistent water grid quietly never learning again this session.
    struct FScopedWaterGridSuppress
    {
        bool& Flag;
        explicit FScopedWaterGridSuppress(bool& InFlag) : Flag(InFlag) { Flag = true; }
        ~FScopedWaterGridSuppress() { Flag = false; }
    } WaterGridGuard(bWaterGridLearningSuppressed);

    const int32 SliceEnd = FMath::Min(PlacementAuditCursor + AuditEntriesPerTick, Layout.Num());
    for (int32 i = PlacementAuditCursor; i < SliceEnd; ++i)
    {
        const FNodeShuffleEntry& E = Layout[i];
        ++PlacementAuditCounts[AB_Examined];

        if (!E.bIsNewNode) { ++PlacementAuditCounts[AB_NotOurs]; continue; }
        if (!E.bActive)    { ++PlacementAuditCounts[AB_Inactive]; continue; }

        AFGResourceNode* const* Found = SpawnedNodes.Find(E.EntryGuid);
        AFGResourceNode* LiveActor = (Found != nullptr && IsValid(*Found)) ? *Found : nullptr;
        if (!E.bRayCasted && LiveActor == nullptr)
        {
            ++PlacementAuditCounts[AB_UnsettledNoActor];
            continue;
        }
        ++PlacementAuditCounts[AB_Audited];

        int32 Severity = 0;
        FString What;

        // ---- READINGS 1 + 2: the settle gate's own probe, at the recorded centre ----
        // Its own live actor is on the ignore list so the trace cannot terminate on the node it is
        // measuring. bShortTrace mirrors what RaycastSettle does for a cave entry: a long top-down ray
        // from a cavern floor hits the cave ROOF and would report a ground surface that is not this
        // entry's ground.
        FVector GroundLoc = FVector::ZeroVector;
        FRotator GroundRot = FRotator::ZeroRotator;
        bool bWater = false, bTooSteep = false;
        const bool bGroundHit = RaycastGroundAt(E.Location, static_cast<float>(E.Location.Z), LiveActor,
                                                nullptr, GroundLoc, GroundRot, bWater,
                                                /*bShortTrace=*/E.bUnderground, &bTooSteep);
        if (bWater)
        {
            ++PlacementAuditCounts[AB_Water];
            Severity = FMath::Max(Severity, AuditSevWater);
            What += TEXT("water(settle-probe) ");
        }
        if (IsKnownWaterCell(E.Location))
        {
            // A different instrument from the one above; counted, never merged into it, and it does not
            // make an entry an offender on its own.
            ++PlacementAuditCounts[AB_KnownWaterCell];
            What += TEXT("cell-learned-as-water ");
        }
        double GroundDeltaCm = 0.0;
        if (!bGroundHit)
        {
            ++PlacementAuditCounts[AB_NoGroundHit];
            What += TEXT("no-ground-hit(below-terrain-not-testable) ");
        }
        else
        {
            GroundDeltaCm = GroundLoc.Z - E.Location.Z;
            if (GroundDeltaCm > AuditBelowTerrainToleranceCm)
            {
                ++PlacementAuditCounts[AB_BelowTerrain];
                Severity = FMath::Max(Severity, AuditSevBelowTerrain);
                What += FString::Printf(TEXT("below-terrain-by-%.0fcm "), GroundDeltaCm);
            }
        }

        // ---- READING 3: the SHIPPED enclosure gate, unmodified, at the same point ----
        // T66 cold review F1: pass the entry's own live actor as the second ignore, or every audited
        // entry with a live node standing at its centre reads a self-hit 8-of-8 refusal -- the exact
        // wrong-population signal this command exists to prevent (measured on entry 526, 2026-08-11).
        int32 Blocked = 0, Total = 0, Threshold = -1;
        const bool bEnclosed = IsSpotEnclosed(E.Location, Blocked, Total, nullptr, &Threshold, Pawn, LiveActor);
        if (bEnclosed)
        {
            ++PlacementAuditCounts[AB_EnclosureRefuse];
            Severity = FMath::Max(Severity, AuditSevEnclosure);
            What += FString::Printf(TEXT("enclosure-gate-refuses(%d-of-%d-rays-blocked) "), Blocked, Total);
        }

        // ---- READING 4: the deep containment instrument, at the same point ----
        FNodeShuffleTotallyInsideReading Inside;
        RunTotallyInsideProbe(W, E.Location, LiveActor, Inside);
        PlacementAuditQueries += Inside.QueriesMade;
        PlacementAuditOverlaps += Inside.OverlapsMade;
        if (!Inside.bRan)
        {
            ++PlacementAuditCounts[AB_InsideNotMeasured];
            What += TEXT("containment-instrument-made-no-query ");
        }
        else
        {
            if (Inside.bTotallyInside)
            {
                ++PlacementAuditCounts[AB_TotallyInside];
                Severity = FMath::Max(Severity, AuditSevTotallyInside);
                What += FString::Printf(TEXT("totally-inside(%d-of-%d-directions-blocked) "),
                                        Inside.BlockedDirections, Inside.DirectionCount);
            }
            if (Inside.bTotallyInside != bEnclosed) { ++PlacementAuditCounts[AB_VerdictsDisagreed]; }
        }

        if (Severity > 0)
        {
            ++PlacementAuditCounts[AB_Offenders];
            FNodeShuffleAuditOffender O;
            O.Severity = Severity;
            O.LayoutIndex = i;
            O.Guid = E.EntryGuid.ToString();
            O.Resource = E.AssignedResourceClassPath;
            O.At = E.Location;
            O.What = What.TrimEnd();
            PlacementAuditOffenders.Add(MoveTemp(O));
        }
    }
    PlacementAuditCursor = SliceEnd;
    // T64 cold review F5: the latch clear moved into ~FScopedWaterGridSuppress above.

    if (PlacementAuditCursor < Layout.Num()) { return; }

    // ---- DONE: census first, then the offender lines, then the completion line ----
    W->GetTimerManager().ClearTimer(PlacementAuditTimer);
    bPlacementAuditRunning = false;

    UE_LOG(LogNodeShuffle, Display,
        TEXT("AUDITCENSUS: walked %d of %d layout entr(ies). NOT JUDGED, each with its predicate: %d were ")
        TEXT("not ours to place, %d were inactive, %d had never been ground-settled and had no live ")
        TEXT("spawned actor, so their recorded centre is not a placement this mod chose. AUDITED (the ")
        TEXT("DENOMINATOR for what follows, except the two counts that state a narrower one inline): %d. ")
        TEXT("Of those -- the settle gate's own probe ")
        TEXT("reported its ground hit underwater at %d; the separately-learned water grid calls the cell ")
        TEXT("water at %d (a different instrument, not folded into the first); the probe found no ground ")
        TEXT("at all at %d, so the below-terrain test could not run there; of the %d where it COULD run, ")
        TEXT("the ground surface sat more than %.0f cm ABOVE the recorded centre at %d; the SHIPPED 8-ray ")
        TEXT("enclosure gate refuses %d; the deep containment instrument reads TOTALLY INSIDE at %d and ")
        TEXT("made no query at all at %d. Of the %d where it DID make a query, the last two verdicts ")
        TEXT("differ from each other at %d entr(ies) -- they are known to disagree ")
        TEXT("(docs/TECH-DEBT.md T41) and are reported as separate fields for that reason, never merged. ")
        TEXT("OFFENDERS (at least one adverse reading): %d, ranked totally-inside > below-terrain > ")
        TEXT("water > enclosure-refusal, worst first, at most %d printed. MEASURED COST: %d trace/sweep ")
        TEXT("queries and %d overlaps issued by the containment instrument. MEASURED: every count above ")
        TEXT("is a predicate this walk evaluated just now. NOT MEASURED, AND NOT CLAIMED: why any entry ")
        TEXT("reads the way it does, whether any of these is visible to a player, and whether any of ")
        TEXT("them was placed there by this mod's roll or moved afterwards."),
        PlacementAuditCounts[AB_Examined], PlacementAuditLayoutNumAtStart,
        PlacementAuditCounts[AB_NotOurs], PlacementAuditCounts[AB_Inactive],
        PlacementAuditCounts[AB_UnsettledNoActor], PlacementAuditCounts[AB_Audited],
        PlacementAuditCounts[AB_Water], PlacementAuditCounts[AB_KnownWaterCell],
        PlacementAuditCounts[AB_NoGroundHit],
        // T64 cold review F4: the two conditional tests get their true denominators stated inline --
        // "below terrain" can only run where the probe hit ground, "verdicts disagree" only where the
        // containment instrument made a query. Against AB_Audited alone a 2/20 rate reads as 2/400.
        PlacementAuditCounts[AB_Audited] - PlacementAuditCounts[AB_NoGroundHit],
        AuditBelowTerrainToleranceCm,
        PlacementAuditCounts[AB_BelowTerrain], PlacementAuditCounts[AB_EnclosureRefuse],
        PlacementAuditCounts[AB_TotallyInside], PlacementAuditCounts[AB_InsideNotMeasured],
        PlacementAuditCounts[AB_Audited] - PlacementAuditCounts[AB_InsideNotMeasured],
        PlacementAuditCounts[AB_VerdictsDisagreed], PlacementAuditCounts[AB_Offenders],
        AuditMaxOffenderLines, PlacementAuditQueries, PlacementAuditOverlaps);

    PlacementAuditOffenders.Sort([](const FNodeShuffleAuditOffender& A, const FNodeShuffleAuditOffender& B)
    {
        if (A.Severity != B.Severity) { return A.Severity > B.Severity; }
        return A.LayoutIndex < B.LayoutIndex;
    });
    const int32 ToPrint = FMath::Min(PlacementAuditOffenders.Num(), AuditMaxOffenderLines);
    for (int32 i = 0; i < ToPrint; ++i)
    {
        const FNodeShuffleAuditOffender& O = PlacementAuditOffenders[i];
        UE_LOG(LogNodeShuffle, Display,
            TEXT("AUDITPLACEMENTS offender %d of %d (rank %d): layout entry %d, guid %s, resource '%s', ")
            TEXT("recorded centre %s -- %s"),
            i + 1, PlacementAuditOffenders.Num(), O.Severity, O.LayoutIndex, *O.Guid, *O.Resource,
            *O.At.ToCompactString(), *O.What);
    }

    const double Elapsed = GetWorld() ? (GetWorld()->GetTimeSeconds() - PlacementAuditStartSeconds) : -1.0;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("===== AUDITPLACEMENTS COMPLETE ===== %d entr(ies) walked in %.1f s of world time across ")
        TEXT("slices of %d. %d offender(s) found, %d printed above. The layout held %d entr(ies) when ")
        TEXT("this started and holds %d now -- if those differ, the apply pass changed it mid-walk and ")
        TEXT("the counts span both. Nothing was placed, moved, refused or persisted by this command."),
        PlacementAuditCounts[AB_Examined], Elapsed, AuditEntriesPerTick,
        PlacementAuditOffenders.Num(), ToPrint, PlacementAuditLayoutNumAtStart, Layout.Num());
}
