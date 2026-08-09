// Packet H2 (ns-wells-h2, branch feature/extractor-automatch): the PLACEMENT half of rigid
// resource-well relocation -- the per-pass driver, the §Q3a rotate-before-you-nudge search,
// all-or-nothing footprint validation, and suppression of the vanilla group it replaces.
// NodeShuffleWellRelocateRoll.cpp owns the capture/deal half; NodeShuffleWellSpawn.cpp materialises a
// validated placement; NodeShuffleWellEscalate.cpp owns what happens when one does not fit;
// NodeShuffleWellLink.cpp owns the mCore lifecycle; NodeShuffleWellAudit.cpp owns the acceptance gate.
// NodeShuffleWellRelocate.h holds the full file map and the shared pure helpers.
//
// ALL-OR-NOTHING IS THE WHOLE DESIGN OF THIS FILE (design §Q3). Left to the ordinary per-node
// machinery, a well dropped on awkward terrain would quietly become a 2-satellite well: each
// individual deactivation looks routine, so the shrink is invisible in the log. So NOTHING is written
// to the world until the ENTIRE footprint -- core plus every satellite -- has settled, cleared water,
// slope, resource-node overlap and buildable overlap. Any member fails => next yaw => nudge the group
// => re-deal the group => leave the well at its VANILLA location.
//
// ns-review-h2 F15/F7 -- THE PRECISE CLAIM. This file used to end that paragraph with "there is no
// partial-well state", which was true of VALIDATION and FALSE OF SPAWN: SpawnWellGroup returned "did
// anything spawn", so one satellite out of ten marked the group placed and the suppression then hid
// the entire vanilla well. A partial-well state existed and was reachable. It no longer does -- the
// spawn is all-or-nothing too, and nothing is suppressed until the group is COMPLETE -- but the
// property is now enforced in two places rather than asserted in a comment, and a TRANSIENT partial
// group can still exist for one pass while an incomplete spawn is retried. During that window the
// vanilla well is still fully visible, which is the correct direction to fail in.
//
// ns-t27-corefirst -- SEARCH ORDER IS NOW: PLACE THE CORE, THEN EACH SATELLITE WHEREVER IT CAN LAND.
// The author's directive, 2026-08-09, after flying to a relocated Water core and being unable to reach
// it: "the check of placement like on solids to the core and independently to the wells. The core
// needs placing first and the wells somewhere around it wherever they can land."
//
// WHAT IT REPLACES, and why the replacement is not a smaller version of it. The retired search rotated
// the CAPTURED CLOUD about the core through a 36-step seeded yaw permutation and asked whether the
// WHOLE footprint fitted at that angle. Rotation was the right cheap axis for a RIGID body -- H0
// measured max angular gaps of 263, 220, 155 and 151 degrees, i.e. crescents with every satellite on
// one side, which rotation can tuck into a valley and translation cannot -- but rigidity was the
// problem itself: ONE blocked satellite refused the entire destination, at all 36 angles, and the
// group then spent a nudge and eventually a redeal over it. Independence removes the coupling rather
// than searching harder around it.
//
// THE DIFFERENCE IN WHAT AN ATTEMPT COSTS. Retired: 36 yaws x (probes until the first satellite fails,
// usually 1-2). Now: WellLayoutAttempts full re-draws, each drawing every captured satellite from its
// own budget of WellSatPlacementTries polar candidates around the settled core. A re-draw re-draws
// EVERY satellite, including the ones that already succeeded -- that is deliberate and is the cheap
// form of backtracking, because a greedy sequence can wall itself in with its own earlier siblings and
// only a full re-draw escapes that.
//
// ns-t27-review F1: AND IT COSTS 4x-20x MORE PER PASS THAN THE SEARCH IT REPLACED. This paragraph and
// NodeShuffleSubsystem.h both used to claim one attempt was "roughly the cost of the whole old per-pass
// budget"; counted, that is false. Retired, per pass: 6 yaws x (probes until the FIRST satellite fails)
// ~= 6-12 probes, ~48 worst case. Now, per pass: one attempt x up to 10 satellites x 24 draws = up to
// 240 probes.
//
// ns-t27-fixes-review F-B(i): a satellite does NOT give up on its first rejected draw -- it retries up
// to its whole budget, which the retired search never did. It is bounded the other way by a break: the
// FIRST satellite that exhausts its budget ends the attempt, so satellites after it are never drawn,
// and a satellite that succeeds stops at its first accepted draw. The 240 is therefore a CEILING, not
// the typical cost; a typical failing attempt is (a few draws per earlier satellite) + one full budget.
// (The previous version of this paragraph said there was NO early-out and that every satellite spent
// its whole budget. Both halves were false; the break is at the bottom of the satellite loop below and
// the inner loop's own condition carries `&& !bPlaced`.)
//
// ns-t27-fixes-review F-B(ii): TEN satellites, not eight. H0's measured maximum is 10 -- the figure
// NodeShuffleSubsystem.h and NodeShuffleWellFootprint.cpp both already state -- so the ceiling is 240.
//
// ns-t27-fixes-review F-B(iii): each probe is up to 13 line traces -- 1 settle + 4 ring in
// RaycastGroundAt, and, on any candidate that reaches gate six, 8 horizontal enclosure rays, which
// satellites now run because F3 in this same packet compiled WellEnclosureGateOnSatellites true --
// plus one overlap sphere and one resource-node scan. The previous version said 5 traces, which was
// the count from before F3 turned the enclosure gate on in this same build.
//
// The loop below this one puts no cap on how many GROUPS a pass searches.
//
// ns-t27-perf: THE RESOURCE-NODE SCAN IS NO LONGER PER PROBE. It was a
// TActorIterator<AFGResourceNode> over the entire world inside every probe -- the cold review
// ESTIMATED that as the dominant term (T27-fixes-review.md section 5, under a heading that says "The
// estimate"): an order-of-magnitude argument from an assumed actor count and an assumed per-actor
// cost. NOTHING WAS TIMED -- there was no clock on this path until F-A added one in this same packet
// (PlaceStartSec below), so no millisecond figure for the OLD binary can exist. The probe COUNT is
// measured; the milliseconds are not. Runtime step 1 is what turns the estimate into a number.
// What IS structural rather than estimated: the iterator walks the level's actor list, so its cost
// scales with the player's FACTORY actor count rather than with node count, and it therefore grows
// for the whole life of a save. It is now built ONCE per group per
// pass (BuildWellNodeScanCache) and the gate tests against that set. The POPULATION IS UNCHANGED and
// the argument for that is written out at the scan function; the broadphase replacement, which would
// change the population, is still refused and is still its own packet. What each pass now costs is
// MEASURED rather than argued: WELLH2-PROBECENSUS carries elapsed wall-clock ms for the call and the
// size of the scanned set, which is what T27 runtime checklist step 4 reads.
//
// SIBLING CLEARANCE IS NOW OURS, AND THAT IS THE LOAD-BEARING CHANGE IN THIS FILE. The retired
// version deliberately built no same-group overlap exemption, and was RIGHT not to: H0 measured min
// inter-satellite 1818.8 cm over 401 pairs against the 800 cm reject radius, the roll ASSERTS it per
// well at capture (MinIntraGroupDistance), and members moved RIGIDLY -- so a group was self-clear by
// construction and could not reject itself. Independent placement destroys that argument outright.
// Two satellites drawn from the same disc can land on top of each other, and ValidateWellMemberSpot
// cannot catch it: the sibling is not spawned yet, so there is no actor for its node-overlap gate to
// find. Left unhandled this converts an EXTERNAL failure (the terrain refused us) into an INTERNAL one
// (we refused ourselves), which is strictly worse because it looks like bad luck. Hence the layout
// gates below, applied to every candidate after the footprint test clears it.
//
// ALL-OR-NOTHING STILL GOVERNS THE GROUP -- see the paragraph above; independence is per SATELLITE, not
// per COMMIT. A satellite that exhausts its draw budget kills the whole ATTEMPT; a group that exhausts
// its attempts escalates through the same nudge/redeal ladder. NOTHING places short. Placing short is
// docs/TECH-DEBT.md T15's permanent-shrink defect, where a well silently produces less than its
// vanilla counterpart forever, and this packet refuses to create a second instance of it. The cost of
// that refusal is COUNTED, not hidden: WELLH2-PROBECENSUS carries the per-satellite budget-exhaustion
// count and the worst per-satellite draw count on every line.
//
// IMPORT DISCIPLINE (memory: sf-shipping-export-trap): the new engine surface this file reaches for is
// SpawnActorDeferred/FinishSpawning on the two fracking classes plus their StaticClass thunks, all of
// which design §M1 dumpbin-verified present in the shipping export table. That is the PREDICTION, not
// the claim -- the import table is MEASURED after the build and every new symbol is named. Three
// predictions have been falsified on this machine this week.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleConfig.h"       // ns-t23-rollhide: CommitWellsAtRoll, read once per pass for the TEST line
#include "NodeShuffleWellCensus.h"   // AFGResourceNodeFrackingCore / ...Satellite
#include "NodeShuffleWellRetype.h"   // WellPathOf / WellShort
#include "NodeShuffleWellRelocate.h" // the H2 pure helpers
#include "NodeShuffleWellStage0.h"   // ns-t23-stage0: the per-attempt rejection census (LOG ONLY)

#include "Resources/FGResourceDescriptor.h"
#include "Components/StaticMeshComponent.h" // H2b: WellMeshIndex holds mesh-component weak pointers

// The reject radii live in NodeShuffleWellFootprint.cpp with the test that uses them; the nudge/redeal
// constants live in NodeShuffleWellEscalate.cpp with the ladder that uses them. Nothing this file does
// needs either, which is a decent sign the split fell where it should.

// ------------------------------------------------------------------------------------------------
// THE §Q3a SEARCH FOR ONE GROUP
// ------------------------------------------------------------------------------------------------
bool ANodeShuffleSubsystem::TryPlaceWellGroup(FNodeShuffleWellEntry& E, UClass* ResourceClass)
{
    const bool bDiag = FNodeShuffleModule::AreDiagnosticsEnabled();
    const FString CoreLabel = WellShort(E.CorePath);

    // ns-t23-stage0 INSTRUMENT 3 -- LOG ONLY. Counters for this ONE placement attempt, emitted on every
    // return path below. Nothing here changes a decision, a budget, a cursor or a probe: each counter is
    // incremented beside a call that was already being made. The emitted line's own text carries the
    // asymmetry between the two probe populations; NodeShuffleWellStage0.h carries the reasoning.
    FNodeShuffleWellProbeCensus Probes;
    // ns-t27-fixes-review F-A: the packet's own headline open question (checklist step 4) is a TIMING
    // question and this path emitted no timing. FPlatformTime::Seconds is already used by the roll's
    // commit timing in this module, so this reaches no new engine entry point. Reports elapsed
    // wall-clock for THIS call only; it states no cause and names no culprit.
    const double PlaceStartSec = FPlatformTime::Seconds();
    const auto EmitProbes = [&](const TCHAR* Outcome, const TCHAR* TerminatedBy) -> void
    {
        if (bDiag)
        {
            // ns-t23-stage0 REVIEW FIX (F2): how many CONSECUTIVE attempts a yaw-exhaustion outcome
            // costs, against one for a core rejection -- the emission-rate asymmetry the line discloses.
            // Computed here because both constants are private to this class and the emitter is free.
            const int32 AttemptsToExhaust = (WellLayoutAttemptsPerPass > 0)
                ? ((WellLayoutAttempts + WellLayoutAttemptsPerPass - 1) / WellLayoutAttemptsPerPass)
                : -1;
            LogWellProbeCensus(CoreLabel, Probes, Outcome, TerminatedBy, E.YawCursor, WellLayoutAttempts,
                               ExpectedRelocatedSatelliteCount(E), AttemptsToExhaust,
                               // ns-t27-fixes-review F-E: MaxDrawsAnySat printed with the budget it is
                               // read against, on the same line. Private static, member caller, so it
                               // is in scope here -- the same argument the 6500 merge used.
                               WellSatPlacementTries,
                               // ns-t27-fixes-review F-A: measured at the emission point, so a line
                               // emitted on an early return reports only what that early return cost.
                               (FPlatformTime::Seconds() - PlaceStartSec) * 1000.0);
        }
    };

    // ---- STEP 1: settle and validate the CORE at the dealt spot ----
    // ns-review-h4 style: zero-initialised. ValidateWellMemberSpot writes OutLoc only on a hit, and the
    // failure path below passes CoreLoc straight to EscalateWellPlacement -- which ignores it when
    // bHaveSettledCore is false, but an uninitialised FVector reaching any call at all is not a thing
    // to leave resting on a parameter's semantics.
    FVector CoreLoc = FVector::ZeroVector;
    FRotator CoreRot = FRotator::ZeroRotator;
    FString CoreReason;
    const float StartZ = static_cast<float>(E.DestCoreLocation.Z);
    ++Probes.CoreProbes; // ns-t23-stage0: the core is probed exactly once per call -- the denominator
    // ns-t27-corefirst: THE CORE ALWAYS GETS THE ENCLOSURE GATE. This literal `true` is the whole of
    // T26's fix on the core side, and it is written at the call site rather than defaulted inside the
    // callee so that it is visible to anyone reading the placement, not only to anyone reading the
    // footprint test. The author flew to a relocated Water core, circled a rock column and could not
    // reach it; a core nobody can reach cannot take a Pressurizer, so the well produces nothing and
    // looks like a bug in the mod rather than a bad roll.
    // ns-t27-perf: /*NodeScanCache=*/nullptr IS DELIBERATE AND IS NOT AN OVERSIGHT. The scan is centred
    // on the SETTLED core, and this call is what settles it -- there is nothing to centre on yet. The
    // core is probed exactly once per call, so this is one whole-level walk per call either way, which
    // is not the term the review's cost ESTIMATE named (that term was the PER-SATELLITE-PROBE walk;
    // nothing on either path has ever been timed -- see the head of this file). Once per call is what
    // this path already cost, so the hoist has nothing to buy here. Written as an explicit nullptr
    // rather than a defaulted
    // parameter for the T26 reason: a call site must say which path it takes.
    if (!ValidateWellMemberSpot(E.DestCoreLocation, StartZ, /*bApplyEnclosureGate=*/true,
                                /*NodeScanCache=*/nullptr,
                                CoreLoc, CoreRot, CoreReason))
    {
        ++Probes.CoreRejects[WellRejectGateIndex(CoreReason)];
        if (CoreReason.StartsWith(TEXT("void")))
        {
            // Terrain not streamed. DEFER without spending any budget -- charging a nudge here would
            // burn 8 nudges and 3 redeals on a group the player simply has not walked to yet.
            // ns-review-h2 F8: bounded, and audible. An UNBOUNDED Verbose-only defer is indistinguishable
            // from a working feature that has simply not been reached, so a genuinely undiscoverable
            // destination (a probe that will never hit terrain) would retry silently forever.
            NoteWellVoidDefer(E, TEXT("core"), E.DestCoreLocation, TEXT("<core>"), bDiag);
            EmitProbes(TEXT("DEFERRED-void"), TEXT("core"));
            return false;
        }
        // ns-review-h2 F3 (HIGH): a real rejection of the core's spot escalates through the SAME
        // ladder the yaw-exhaustion path uses. The previous version had its own inline nudge with no
        // redeal and no give-up: once GroupNudges reached the cap it stopped moving the destination
        // and re-probed the identical spot every pass forever -- and because GroupNudges is a SaveGame
        // uint8, it wrapped at 256 back to 0, where the nudge radius is 4000*sqrt(0) = 0. A well dealt
        // into a lake was trapped permanently, and the corruption persisted across saves.
        // IsKnownWaterCell is a LEARNED grid, so a fresh save deals into lakes routinely; this is not
        // an exotic path.
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("WELLH2-SEARCH core='%s': CORE rejected at %s (%s) -- escalating (nudge %d/%d, redeal %d/%d)."),
            *CoreLabel, *E.DestCoreLocation.ToCompactString(), *CoreReason,
            E.GroupNudges + 1, WellMaxGroupNudges, E.GroupRedeals, WellMaxGroupRedeals);
        // ns-t23-stage0 REVIEW FIX: emit BEFORE the ladder. EscalateWellPlacement sets E.YawCursor = 0
        // (NodeShuffleWellEscalate.cpp), so emitting after it prints "cursor now 0" on every escalating
        // line and deletes the one field that says how far this search had got.
        EmitProbes(TEXT("ESCALATED-core-rejected"), TEXT("core"));
        EscalateWellPlacement(E, CoreLoc, /*bHaveSettledCore=*/false, TEXT("core-spot rejected"));
        return false;
    }

    // ---- STEP 2: the core is good; draw each satellite its OWN spot around it ----
    // The candidate sequence is recomputed from persisted state (WellLayoutSeedFor in
    // NodeShuffleWellRelocate.h), never carried in a live stream and never derived from frame time or
    // actor iteration. YawCursor -- the ATTEMPT cursor now, see its header comment -- persists so a
    // group deferred mid-search RESUMES rather than restarting.
    const int32 LayoutSeed = WellLayoutSeedFor(SavedSeed, E.CorePath, E.GroupRedeals, E.GroupNudges);

    // ns-t27-perf: THE ONE WORLD SCAN. Built HERE -- after the core has settled, so there is a centre,
    // and before the attempt loop, so every attempt and every draw inside it shares one walk instead of
    // making its own. This is the whole cost fix: 25-240 whole-level walks per group per pass become 1.
    // It is placed OUTSIDE the attempt loop on purpose; putting it inside would still be correct and
    // would still be ~6x better than per-probe, but it would leave the dominant term multiplied by the
    // attempt count for no gain, since nothing between attempts can change the set.
    // The population argument, and why the radius is measured in XY, are written out at
    // BuildWellNodeScanCache in NodeShuffleWellFootprint.cpp. Read it before changing this radius --
    // it is derived from WellSatMaxRadiusCm and the gate's own reject radius, and shrinking it would
    // silently make the node gate blind rather than making it fail.
    // TODO(ns-t27-truth 2026-08-09): THE WARNING ABOVE POINTS AT ONLY ONE OF THE TWO SIDES. It warns
    // about shrinking the SCAN radius; the direction this coupling actually breaks is GROWING the DRAW
    // radius, at the satellite draw below (search: "UNIFORM IN AREA"), which does not touch this call
    // at all. A per-well satellite radius -- which the short-wells packet is named as the next owner of
    // this function to want -- would compile, would leave probes outside the scanned set, and the node
    // gate would then PASS a spot it should refuse with NO reason string changing and NO rejection to
    // grep for. Nothing enforces the coupling today: it is two reads of WellSatMaxRadiusCm in two
    // files. DEFERRED FROM ns-t27-truth BECAUSE THE FIX IS EXECUTABLE CODE AND THAT PACKET WAS
    // COMMENT-ONLY. The fix is an assertion, not more prose -- see docs/TECH-DEBT.md, T27-PERF entry
    // "the superset proof is unenforced", and T27-perf-review-2.md F2, which carries the reviewer's
    // verbatim implementation (a per-probe scope check inside ValidateWellMemberSpot).
    TArray<TWeakObjectPtr<AFGResourceNode>> NodeScanCache;
    BuildWellNodeScanCache(CoreLoc, NodeScanCache);
    Probes.NodesInScope = NodeScanCache.Num(); // ns-t27-perf: LOG ONLY; the census prints it per call

    int32 Attempts = 0;
    while (E.YawCursor < WellLayoutAttempts && Attempts < WellLayoutAttemptsPerPass)
    {
        const int32 AttemptIndex = E.YawCursor;
        ++Attempts;
        Probes.AttemptsTried = Attempts; // ns-t23-stage0 / renamed by ns-t27-corefirst

        // ONE stream per attempt, seeded from persisted state plus the attempt index, and consumed in
        // E.Satellites order -- which is itself persisted. Both halves are needed for T8: a stream
        // seeded per attempt but consumed in a non-persisted order would still drift.
        FRandomStream Rng(LayoutSeed ^ ((AttemptIndex + 1) * 15485863));

        // ---- EVERY MEMBER PLACED AND CHECKED BEFORE ANYTHING IS COMMITTED ----
        TArray<FVector> MemberLocs;
        TArray<FRotator> MemberRots;
        MemberLocs.Reserve(E.Satellites.Num());
        MemberRots.Reserve(E.Satellites.Num());
        // ns-t27-corefirst: the sibling-clearance reference set, and THE CORE IS ITS FIRST ENTRY.
        // Two reasons it must be, and only the first is obvious. (1) The core-to-satellite minimum is
        // a real gate, not a construction argument -- the draw's inner radius already guarantees it in
        // XY, and this makes the guarantee a line of code that a reviewer can see fail. (2) The
        // footprint test's node-overlap gate CANNOT catch a satellite drawn onto our own core: that
        // gate deliberately self-excludes our already-spawned cores and satellites (F7), so on a
        // re-validation pass after a partial spawn our own actors are invisible to it.
        // ns-t27-review F6: THE SENTENCE THAT USED TO END THIS COMMENT -- "the only thing standing
        // between a satellite and its own core is this array" -- WAS FALSE AND IS STRUCK. What stands
        // between them is the DRAW'S INNER RADIUS: every candidate is generated at XY radius >=
        // WellSatMinRadiusCm from CoreLoc and RaycastGroundAt traces straight down at the probe's X/Y
        // and never rewrites XY, so Dist2D(CoreLoc, SatLoc) == Radius >= 2076 always. Reason (2) above
        // still stands unchanged and is why the entry stays in the array.
        TArray<FVector> PlacedPoints;
        PlacedPoints.Reserve(E.Satellites.Num() + 1);
        PlacedPoints.Add(CoreLoc);
        bool bAllOk = true;
        bool bVoid = false;          // an unstreamed member is a DEFER, never a placement rejection
        FString FailReason, FailWho;
        FVector FailProbe = FVector::ZeroVector; // ns-review-h2 F8: the coordinate to name in the log

        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            // ns-review-h3 H10: an UNCAPTURED record is never spawned, so it must not be placed
            // either. Under the retired rigid search the danger was concrete -- rotating its zero
            // offset re-probed the CORE's own spot and the commit then wrote PlacedLocation = CoreLoc
            // into it, disarming half of the spawn guard's belt-and-braces. Under an independent draw
            // it would instead consume a real spot in the world for a record nothing will ever stand
            // at, which is a different failure with the same fix.
            if (!S.bCaptured) { continue; }

            bool bPlaced = false;
            int32 Draws = 0;
            int32 VoidDraws = 0;
            FString LastReason = TEXT("no-draw-made");
            FVector LastProbe = CoreLoc;

            for (int32 Try = 0; Try < WellSatPlacementTries && !bPlaced; ++Try)
            {
                ++Draws;
                // UNIFORM IN AREA, not uniform in radius. r = sqrt(lerp(rMin^2, rMax^2, u)) spreads
                // candidates evenly over the annulus; drawing r uniformly would pile them against the
                // inner edge and produce the tight ring that reads as generated rather than authored.
                // TODO(ns-t27-truth 2026-08-09): WellSatMaxRadiusCm HERE IS ONE HALF OF AN UNENFORCED
                // COUPLING. BuildWellNodeScanCache (called above, defined in NodeShuffleWellFootprint.cpp)
                // scopes its node scan to this same constant plus the gate's reject radius, and the
                // superset proof that makes the node-overlap gate correct holds ONLY while every probe
                // drawn here stays inside that scope. Raising this radius -- or overriding it per well,
                // which is what a short-wells packet wants -- silently breaks the gate: it would PASS a
                // spot beside a real node, with no reason string changing and nothing logged. Nothing
                // checks it. DEFERRED FROM ns-t27-truth (that packet was comment-only); the fix is a
                // compile-time or check-time assertion, not this comment. See docs/TECH-DEBT.md and
                // T27-perf-review-2.md F2 for the verbatim implementation.
                const float U = Rng.FRand();
                const float Radius = FMath::Sqrt(FMath::Lerp(FMath::Square(WellSatMinRadiusCm),
                                                             FMath::Square(WellSatMaxRadiusCm), U));
                const float Ang = Rng.FRandRange(0.0f, 2.0f * PI);
                // The satellite's OWN actor yaw, drawn per candidate. The retired search turned every
                // member by the one group yaw so the rocks stayed in formation; there is no formation
                // any more, so each rock faces its own way -- which is what vanilla looks like.
                const float DrawYawDeg = Rng.FRandRange(0.0f, 360.0f);
                const FVector Probe(CoreLoc.X + Radius * FMath::Cos(Ang),
                                    CoreLoc.Y + Radius * FMath::Sin(Ang),
                                    CoreLoc.Z);
                LastProbe = Probe;

                FVector SatLoc; FRotator SatRot = FRotator::ZeroRotator; FString Reason;
                ++Probes.SatProbes; // ns-t23-stage0: the satellite-side denominator, counted per PROBE
                // Z is re-settled PER MEMBER from the core's Z. H0 measured four wells with more than
                // 12 m of vertical spread, so a shared Z would bury or float members either way.
                // The enclosure gate is a NAMED CONSTANT here, never a literal: see
                // WellEnclosureGateOnSatellites for why the two sides differ and what it costs.
                // ns-t27-perf: &NodeScanCache is the hoisted set built once above. Same gate, same
                // order, same reason strings -- only the source of the actors changed.
                if (!ValidateWellMemberSpot(Probe, static_cast<float>(CoreLoc.Z),
                                            WellEnclosureGateOnSatellites, &NodeScanCache,
                                            SatLoc, SatRot, Reason))
                {
                    ++Probes.SatRejects[WellRejectGateIndex(Reason)]; // ns-t23-stage0
                    LastReason = Reason;
                    if (Reason.StartsWith(TEXT("void"))) { ++VoidDraws; }
                    continue;
                }

                // ---- LAYOUT GATE 1: SIBLING CLEARANCE ----
                // Measured in XY, not in 3-D, and that is the STRICTER of the two: XY distance is
                // never greater than 3-D distance, so clearing this clears the 3-D form as well. The
                // reason to want the stricter one is ns-review-h2 F13's: two members 300 cm apart in
                // XY and 1500 cm apart in Z read as 1529 cm in 3-D and look comfortably separated,
                // right up until you stand between them and see one node stacked above the other.
                {
                    // ns-t27-review F6: THE CORE'S FLOOR IS THE CORE'S CONSTANT. PlacedPoints[0] is the
                    // core and the rest are siblings, and the two have DIFFERENT minima: the packet's
                    // stated Pressurizer/Extractor clearance assumption is WellSatMinRadiusCm (2076),
                    // while WellSiblingMinSeparationCm (1818) is H0's inter-SATELLITE measurement.
                    // Testing the core against 1818 made this guard looser than the draw that feeds it,
                    // so it could never fire and would have admitted an under-clearance core the moment
                    // anything upstream changed.
                    bool bTooClose = false;
                    double Closest = TNumericLimits<double>::Max();
                    double ClosestFloor = WellSiblingMinSeparationCm;
                    for (int32 i = 0; i < PlacedPoints.Num(); ++i)
                    {
                        const double D = FVector::Dist2D(PlacedPoints[i], SatLoc);
                        const double Floor = (i == 0) ? WellSatMinRadiusCm : WellSiblingMinSeparationCm;
                        if (D < Floor) { bTooClose = true; }
                        if (D < Closest) { Closest = D; ClosestFloor = Floor; }
                    }
                    if (bTooClose)
                    {
                        LastReason = FString::Printf(TEXT("sibling(nearest %.0f cm, floor %.0f cm)"),
                                                     Closest, ClosestFloor);
                        ++Probes.SatRejects[FNodeShuffleWellProbeCensus::Gate_Sibling];
                        continue;
                    }
                }

                // ---- LAYOUT GATE 2: RELATIVE Z ----
                // Per-member Z settle is unbounded on its own: an independently drawn satellite can
                // settle on a clifftop or a ravine floor with nothing tying it to its core's height.
                {
                    // ns-t27-review F4: the cap is RADIUS-RELATIVE, not flat. A flat 2500 cm cap applied
                    // to a candidate drawn up to 6500 cm out is a grade limit in disguise -- it first
                    // bites at 21 degrees and, because the draw is uniform in AREA (half of all
                    // candidates land beyond 4825 cm), it refuses more than half the budget on a 30
                    // degree hillside while the log reads as terrain hostility. The flat term is still
                    // the bound on the absurd for a close-in satellite; the grade term is what keeps an
                    // outer satellite on the same hillside rather than on a different landform.
                    const double DZ = FMath::Abs(SatLoc.Z - CoreLoc.Z);
                    const double R  = FVector::Dist2D(CoreLoc, SatLoc);
                    const double ZCap = FMath::Max<double>(WellSatMaxRelativeZCm,
                                                           WellSatMaxRelativeGrade * R);
                    if (DZ > ZCap)
                    {
                        LastReason = FString::Printf(
                            TEXT("relZ(%.0f cm above/below core at %.0f cm out, cap %.0f cm)"),
                            DZ, R, ZCap);
                        ++Probes.SatRejects[FNodeShuffleWellProbeCensus::Gate_RelativeZ];
                        continue;
                    }
                }

                // ACCEPTED.
                // ns-t27-review F12 (style note, no behaviour change): S.LocalYawDeg is a NO-OP TERM in
                // this sum and its presence must not be read as the captured vanilla orientation being
                // honoured -- it is not. DrawYawDeg is uniform on the whole circle, so adding a fixed
                // per-record offset to it changes nothing distributionally; the authored orientation is
                // destroyed either way, which is the design (there is no group to stay in formation
                // with). The term is kept so that a future packet narrowing DrawYawDeg to a band has to
                // decide explicitly which of the two yaws wins, rather than silently re-imposing the
                // authored one. Second, related note: SatRot comes back from RaycastGroundAt SURFACE
                // ALIGNED, so writing .Yaw on it does not compose as a world-Z rotation once pitch/roll
                // are non-zero. Harmless while the yaw is uniform random; the retired path did the same
                // thing with GroupYawDeg, so this is NOT a regression -- it is flagged here so nobody
                // discovers it later as new. Build the rotation as a quaternion composition the moment a
                // specific yaw is intended.
                SatRot.Yaw = SatRot.Yaw + S.LocalYawDeg + DrawYawDeg;
                MemberLocs.Add(SatLoc);
                MemberRots.Add(SatRot);
                PlacedPoints.Add(SatLoc);
                bPlaced = true;
                if (bDiag)
                {
                    UE_LOG(LogNodeShuffle, Verbose,
                        TEXT("WELLH2-SATDRAW core='%s' satellite='%s': placed at %s on draw %d of a ")
                        TEXT("budget of %d, %.0f cm from the core in XY and %.0f cm from it in Z, own ")
                        TEXT("yaw %.1f deg. The draw envelope this candidate came from is the annulus ")
                        TEXT("between the two satellite radius constants, sampled uniformly by AREA."),
                        *CoreLabel, *WellShort(S.SatellitePath), *SatLoc.ToCompactString(), Draws,
                        WellSatPlacementTries, FVector::Dist2D(CoreLoc, SatLoc),
                        FMath::Abs(SatLoc.Z - CoreLoc.Z), DrawYawDeg);
                }
            }

            // ns-t27-review F5: RECORDED HERE, OUTSIDE THE ACCEPT BLOCK, SO THE EXHAUSTED CASE COUNTS
            // TOO. While this line sat beside the accepted candidate it recorded successes only, which
            // made it structurally incapable of answering the one question it exists to answer --
            // whether WellSatPlacementTries is binding -- because the binding case was exactly the case
            // it excluded.
            Probes.MaxDrawsAnySat = FMath::Max(Probes.MaxDrawsAnySat, Draws);

            if (!bPlaced)
            {
                bAllOk = false;
                ++Probes.SatBudgetExhausted;
                FailReason = LastReason;
                FailWho = WellShort(S.SatellitePath);
                FailProbe = LastProbe;
                // WHEN EVERY DRAW FOUND NO GROUND AT ALL, this is a streaming state, not a refusal --
                // the same distinction the retired search drew on its single probe. The predicate is
                // "every draw this satellite made was void", from THIS attempt's own two counters, and
                // it is stated rather than inferred: a satellite that was refused for any other reason
                // even once is NOT a streaming case and must spend budget like any other failure.
                bVoid = (Draws > 0 && VoidDraws == Draws);
                if (bDiag)
                {
                    UE_LOG(LogNodeShuffle, Verbose,
                        TEXT("WELLH2-SATDRAW core='%s' satellite='%s': NO SPOT FOUND in %d draw(s) ")
                        TEXT("(budget %d); %d of those draw(s) found no ground under the probe. Last ")
                        TEXT("refusal was %s at %s. This ends the whole attempt: the group is placed ")
                        TEXT("entire or not at all, and is never placed short."),
                        *CoreLabel, *FailWho, Draws, WellSatPlacementTries, VoidDraws, *FailReason,
                        *FailProbe.ToCompactString());
                }
                break;
            }
        }

        if (bAllOk)
        {
            // COMMIT. Nothing before this line wrote to the world or to the entry's placement fields.
            WellVoidDefers.Remove(E.CorePath); // ns-review-h2 F8: a settled group starts fresh
            // ns-t27-corefirst: WRITTEN AS ZERO, AND THAT IS THE TRUTH RATHER THAN A LEFTOVER. There is
            // no rigid-body rotation any more, so there is no group yaw to record; each satellite
            // carries its own drawn yaw in its PlacedRotation. Three log lines still print this field
            // (well spawn x2, well audit x1) and they will print zero for every T27-placed group. See
            // the field's own comment in NodeShuffleSubsystem.h for the dated retirement note.
            E.GroupYawDeg = 0.0f;
            E.PlacedCoreLocation = CoreLoc;
            E.PlacedCoreRotation = CoreRot;
            // A3 -- THE ONLY PLACE bPlacementClaimLive IS EVER SET TRUE, deliberately on the line after
            // the only non-zero write of PlacedCoreLocation in the packet. Seven review rounds' worth of
            // bugs came from readers INFERRING this from bGroupPlaced/bRelocate/bRelocationFailed; the
            // claim is now a stored fact written where it becomes true. Note it is set HERE, at the
            // footprint commit, NOT at `bGroupPlaced = true` in ApplyWellRelocation: between those two
            // points the group spawns, and an INCOMPLETE spawn leaves real actors of ours standing at
            // exactly these coordinates with bGroupPlaced still false. That window is the h5 F-1 /
            // RT-6 stranded-actor class, and it is precisely the window the claim must cover.
            E.bPlacementClaimLive = true;
            // ns-review-h2-r4 D-2 -- THE COMMIT IS THE PROGRESS SIGNAL THAT RESETS THE EXPIRY CLOCK.
            // ReconcileAbandonedWellClaims now counts consecutive passes an entry spends "mid-search,
            // the claim is live" and withdraws the claim at WellClaimMidAssemblyMaxPasses, because
            // that state previously had NO expiry at all (spawn INCOMPLETE, then disable relocation or
            // never return, and the claim was live for the life of the save). A group that is actually
            // being assembled re-reaches THIS line every pass a player is near it -- TryPlaceWellGroup
            // does not advance the yaw cursor on the INCOMPLETE-spawn path, so the same footprint is
            // re-validated and re-committed -- so resetting here is what makes the bound apply ONLY to
            // entries making no progress. Without this reset the bound would expire a group a player
            // is standing next to, and pass A would then destroy and respawn it: h5 F-2's cycle.
            WellClaimMidAssemblyPasses.Remove(E.CorePath);
            // ns-review-h3 H10: MemberLocs/MemberRots hold ONLY the captured members now, so the commit
            // walks its own cursor rather than indexing E.Satellites -- indexing would misalign the
            // moment any record is uncaptured, and would write a coordinate into a record that must
            // keep none. An uncaptured record's PlacedLocation stays ZeroVector, which is exactly what
            // the spawn guard tests for.
            int32 Cursor = 0;
            for (FNodeShuffleWellSatellite& S : E.Satellites)
            {
                if (!S.bCaptured) { continue; }
                S.PlacedLocation = MemberLocs[Cursor];
                S.PlacedRotation = MemberRots[Cursor];
                ++Cursor;
            }
            UE_LOG(LogNodeShuffle, Display,
                // ns-review-h4 F6: print the count actually PLACED (the bCaptured subset, after h3 H10
                // stopped validating uncaptured records) and the raw record count separately. Saying
                // "N satellites all settled" with N = Satellites.Num() claimed validation of members
                // this loop deliberately never touched.
                // ns-t27-corefirst: every field below is a measurement from THIS attempt. The list of
                // conditions is the list of predicates that returned false nowhere in it -- it names
                // the gates, and does not claim the site is good for any reason beyond them.
                TEXT("WELLH2-SEARCH core='%s': LAYOUT VALIDATED -- core settled at %s and every ")
                TEXT("satellite placed independently around it (layout attempt %d of a limit of %d, ")
                TEXT("cursor %d, layout seed %d, nudges %d, redeals %d). %d captured satellite(s) each ")
                TEXT("settled, dry, off cliffs, clear of nodes and buildings, at least the sibling ")
                TEXT("floor from the core and from every other placed member, and within the relative ")
                TEXT("Z cap THAT APPLIED TO IT -- ns-t27-review F4 made that cap two terms, a flat one ")
                TEXT("and a grade one, so it is a per-candidate number and the WELLH2-SATDRAW refusal ")
                TEXT("line prints the one that applied (%d record(s) in the entry; any difference is ")
                TEXT("uncaptured and ")
                TEXT("is neither placed nor spawned). The core additionally passed the enclosure test; ")
                TEXT("whether the satellites did depends on WellEnclosureGateOnSatellites, which is ")
                TEXT("compiled %s in this build."),
                *CoreLabel, *CoreLoc.ToCompactString(), AttemptIndex + 1, WellLayoutAttempts,
                E.YawCursor, LayoutSeed, E.GroupNudges, E.GroupRedeals,
                ExpectedRelocatedSatelliteCount(E), E.Satellites.Num(),
                WellEnclosureGateOnSatellites ? TEXT("on") : TEXT("off"));
            EmitProbes(TEXT("VALIDATED"), TEXT("no-side(whole group placed)"));
            return true;
        }

        if (bVoid)
        {
            // Every draw this satellite made found no ground. Do NOT advance the cursor: the same
            // attempt deserves a fair retry once the terrain is there, and advancing would silently
            // consume the search. ns-review-h2 F8: bounded and audible, same as the core-probe defer.
            NoteWellVoidDefer(E, TEXT("satellite"), FailProbe, *FailWho, bDiag);
            EmitProbes(TEXT("DEFERRED-void"), TEXT("satellite"));
            return false;
        }

        if (bDiag)
        {
            UE_LOG(LogNodeShuffle, Verbose,
                TEXT("WELLH2-SEARCH core='%s': layout attempt REJECTED -- satellite '%s' found no spot ")
                TEXT("in its whole draw budget, last refusal %s. Cursor %d of %d. The other satellites' ")
                TEXT("placements from this attempt are DISCARDED and every one of them is re-drawn next ")
                TEXT("attempt, because a sequence can wall itself in with its own earlier members."),
                *CoreLabel, *FailWho, *FailReason, E.YawCursor + 1, WellLayoutAttempts);
        }
        ++E.YawCursor;
    }

    if (E.YawCursor < WellLayoutAttempts)
    {
        // Per-pass budget spent, search not finished. Resume next pass from the same cursor.
        if (bDiag)
        {
            UE_LOG(LogNodeShuffle, Verbose,
                TEXT("WELLH2-SEARCH core='%s': %d layout attempt(s) made this pass, cursor now %d of ")
                TEXT("%d -- resuming next pass."),
                *CoreLabel, Attempts, E.YawCursor, WellLayoutAttempts);
        }
        // The per-pass budget ended this call, not a rejection: the search is unfinished and resumes
        // next pass from the same cursor. Named apart from exhaustion so the two are never summed
        // together as "failed".
        EmitProbes(TEXT("BUDGET-SPENT-resuming"), TEXT("no-side(per-pass budget)"));
        return false;
    }

    // ---- STEP 3/4/5: every layout attempt failed here -> the shared escalation ladder ----
    // Every refusal counted in THIS call came from the satellite side: the core settled at the top of
    // this call and was not re-probed. Attempts refused on EARLIER passes of the same search are on
    // those passes' own lines. Emitted BEFORE the ladder, which resets E.YawCursor to 0.
    EmitProbes(TEXT("ESCALATED-layouts-exhausted"), TEXT("satellite"));
    EscalateWellPlacement(E, CoreLoc, /*bHaveSettledCore=*/true,
        *FString::Printf(TEXT("all %d independent-layout attempts failed"), WellLayoutAttempts));
    return false;
}

// ------------------------------------------------------------------------------------------------
// SUPPRESSING THE VANILLA GROUP
// ------------------------------------------------------------------------------------------------
// Narrow and self-contained ON PURPOSE: it never touches OriginalNodeRecord or SuppressOriginalNodes'
// machinery, whose rematch/capture/radiation paths explicitly exclude fracking actors. A leftover
// visible satellite beside a hidden core is the well-shaped version of the orphan-rock ghost, so the
// core, every satellite and their paired AFGNodeMeshActors are hidden as a UNIT (design §2.5).
// Re-asserted every pass because a level actor streams back in un-hidden.
void ANodeShuffleSubsystem::SuppressVanillaWellGroup(FNodeShuffleWellEntry& E, EWellSuppressPhase Phase)
{
    int32 Hidden = 0, MeshesHidden = 0, MeshesAlready = 0, Occupied = 0, Unstreamed = 0;
    // ns-t23-rollhide: how many members this call newly took OWNERSHIP of (the false->true transition of
    // bSuppressedByUs). Distinct from Hidden, which counts actor-flag changes and therefore also counts a
    // re-assertion after a member streamed back in un-hidden. Both are printed: a zero in one beside a
    // non-zero in the other is the difference between "first suppression" and "maintenance".
    int32 NewlyOwned = 0;
    const bool bRollPhase = (Phase == EWellSuppressPhase::Roll);

    // H2b -- THE INDEX FIRST, THEN CAPTURE, THEN HIDE, IN THAT ORDER AND FOR THAT REASON.
    //
    // Everything this function hides is the last copy of the look we are about to need at the
    // destination, so the capture has to happen while the originals are still standing. Design 2.4
    // says it in one line ("Capture before hiding the original") and this is the only site that can
    // honour it: the roll runs where the well is loaded, but a group can be re-enrolled, re-dealt and
    // re-placed long afterwards, and the suppression pass is the one that provably runs with the
    // vanilla member live (it is what hides it).
    //
    // The index also REPLACES FindMeshActorForNode below. That call is why five well groups out of six
    // reported "hid N vanilla member(s) + 0 mesh actor(s)" next to "0 not streamed yet" on 2026-08-07:
    // MeshActorCache's forward-link sweep iterates AFGResourceNode and skips fracking actors outright,
    // and a fracking CORE is an AFGResourceNodeBase that iterator never visits -- so only the rarely
    // set engine back-link could ever pair a well member, and it happened to be set for exactly one
    // group. The meshes were never missing; nothing was looking for them.
    //
    // ns-t23-rollhide: BOTH OF THOSE ARE SKIPPED ON THE ROLL PHASE, and skipping them is not an
    // optimisation -- it is required for correctness and for cost. The roll's phase-3 commit has already
    // called RebuildWellMeshIndex() ONCE for the whole roll and CaptureWellGroupVisuals() for this entry,
    // and it had to: EnsureWellMeshIndex() early-returns only when WellMeshIndexPass == WellAuditPasses,
    // and the roll does not increment WellAuditPasses, so on the FIRST roll of a session (defaults
    // WellMeshIndexPass = -1, WellAuditPasses = 0) it would rebuild the whole-world index once PER
    // ENROLLED WELL. Calling it here on the roll path would also silently re-run the capture the roll's
    // completeness gate has already adjudicated.
    if (!bRollPhase)
    {
        EnsureWellMeshIndex();
        CaptureWellGroupVisuals(E);
    }

    const auto HideOne = [&](AFGResourceNodeBase* Node, FNodeShuffleWellSuppressionRecord& Rec) -> void
    {
        if (!IsValid(Node)) { ++Unstreamed; return; }
        // ns-review-h4 F1: the SAME two-part predicate the destructive paths use. Hiding is not
        // destructive, but it is exactly as wrong to hide a node a player has a Pressurizer or an
        // Extractor on, and the asymmetry -- pin logic tests activator/extractor||occupied, this tested
        // occupied alone -- is the thing that lets the two drift apart again. One predicate, every site.
        const TCHAR* InUseWhy = TEXT("");
        if (IsWellMemberInUse(Node, InUseWhy))
        {
            ++Occupied;
            if (!WellSuppressSkipLogged.Contains(WellPathOf(Node)))
            {
                WellSuppressSkipLogged.Add(WellPathOf(Node));
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WELLH2-SUPPRESS core='%s' member='%s': NOT hidden -- in use (%s). Said once."),
                    *WellShort(E.CorePath), *Node->GetName(), InUseWhy);
            }
            return;
        }
        // ns-t23-rollhide -- THE PRIOR STATE IS READ BEFORE ANYTHING IS WRITTEN, AND ONLY ON THE
        // FALSE->TRUE TRANSITION. This block is re-entered on every re-assertion pass (a level actor
        // streams back in un-hidden), and on those passes the actor is already hidden BY US -- recording
        // that as "prior" would make the un-hide restore hidden+no-collision and leave the well
        // permanently invisible while reporting a successful restore. So the record is written exactly
        // once, at the instant we take ownership, and never touched again until UnhideWellMember clears it.
        //
        // THE ONLY SITE THAT SETS bSuppressedByUs TRUE. tools/check_t23_writers.ps1 fails the tree if a
        // second one appears: writing that flag without performing the hide beneath it is the one-line
        // shortcut that turns the deliberately-red T23-A assertion green while changing nothing in the
        // world, and it would also fabricate an un-hide obligation for a member we never touched.
        // ns-t23-rollhide REVIEW FIX (cold review F1): "first touch" is first touch OF THIS LEDGER, which
        // is NOT the first hide. bSuppressedByUs is a new SaveGame field, so every save written before it
        // existed holds members we hid under an earlier build with no record of it -- and reading the live
        // flags there records OUR OWN hidden+de-collided state as "prior", so the restore would re-hide the
        // well, report success and clear the obligation. When THIS ENTRY has ever been relocated by us, a
        // hidden or de-collided member is ours by construction, so the vanilla state is recorded instead.
        // The predicate is measured from this entry's own placement state, not inferred.
        const bool bFirstTouch = !Rec.bSuppressedByUs;
        if (bFirstTouch)
        {
            const bool bEverRelocatedByUs =
                E.bGroupPlaced || E.bDestDealt || !E.PlacedCoreLocation.IsNearlyZero();
            const bool bLiveHidden = Node->IsHidden();
            const bool bLiveNoCollision = !Node->GetActorEnableCollision();
            Rec.bWasActorHiddenBefore = bEverRelocatedByUs ? false : bLiveHidden;
            Rec.bWasCollisionDisabledBefore = bEverRelocatedByUs ? false : bLiveNoCollision;
            Rec.bSuppressedByUs = true;
            Rec.bSuppressedAtRoll = bRollPhase;
            ++NewlyOwned;
            if (bEverRelocatedByUs && (bLiveHidden || bLiveNoCollision))
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WELLH2-SUPPRESS core='%s' member='%s': taken into the ledger while ALREADY ")
                    TEXT("hidden %d and de-collided %d. The predicate this branch tested is that we have ")
                    TEXT("dealt or placed this entry (placed %d, dealt %d) -- at a roll-time commit the ")
                    TEXT("deal is set moments earlier and NOTHING has been relocated, so this is NOT a ")
                    TEXT("test of who hid the member and nothing here measured that. Recorded as ")
                    TEXT("vanilla-visible and vanilla-colliding BY DESIGN: handing back a hidden member ")
                    TEXT("is unrecoverable and handing back a visible one self-corrects. This is what a ")
                    TEXT("later restore will hand back."),
                    *WellShort(E.CorePath), *Node->GetName(), bLiveHidden ? 1 : 0,
                    bLiveNoCollision ? 1 : 0, E.bGroupPlaced ? 1 : 0, E.bDestDealt ? 1 : 0);
            }
            else if (!bEverRelocatedByUs && (bLiveHidden || bLiveNoCollision))
            {
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("WELLH2-SUPPRESS core='%s' member='%s': first touch on an entry that is NEITHER ")
                    TEXT("placed NOR dealt and names no previous placement, and the member is already ")
                    TEXT("hidden %d / de-collided %d. That LIVE state is being recorded as PRIOR, so a ")
                    TEXT("later restore will hand it back exactly as it is now. Cold review REVIEW-2 F-E ")
                    TEXT("could not construct this state from any call site; if this line prints, the F1 ")
                    TEXT("predicate is live and the trade it encodes needs re-deciding."),
                    *WellShort(E.CorePath), *Node->GetName(), bLiveHidden ? 1 : 0,
                    bLiveNoCollision ? 1 : 0);
            }
        }
        bool bChanged = false;
        if (Node->GetActorEnableCollision()) { Node->SetActorEnableCollision(false); bChanged = true; }
        if (!Node->IsHidden()) { Node->SetActorHiddenInGame(true); bChanged = true; }
        // H2b: every mesh piece paired to this member by ANY of the three routes (own / engine link /
        // spatial), hidden at COMPONENT level with its collision -- a hidden rock that still blocks the
        // build gun is a ghost you can build on. The player-visible symptom this fixes is the core's
        // cracked-ground graphic still sitting at the original site after the well moved, with the
        // satellites correctly gone.
        // ns-t23-rollhide REVIEW FIX (cold review F5): the prior-state gate is now PER COMPONENT inside
        // HideWellMemberMeshes, so bFirstTouch is no longer passed through. A member's first touch fires
        // once ever, so a piece that entered the index later -- or was re-created by a streaming round
        // trip at the origin -- was hidden with no record at all and could only be restored to the default.
        MeshesHidden += HideWellMemberMeshes(Node, MeshesAlready);
        if (bChanged) { ++Hidden; }
        // Take the hidden original out of the scanner and the node manager, once, so it cannot ping an
        // empty map spot or accept an extractor snap as an invisible ghost. Same idiom, same reasons,
        // as SuppressOriginalNodes -- a whole-actor hide does neither of these by itself.
        const FString Path = WellPathOf(Node);
        if (!ScannerDeregistered.Contains(Path))
        {
            Node->RemoveResourceNodeScan_Local();
            Node->UpdateNodeRepresentation();
            DeregisterNodeFromManager(Node);
            ScannerDeregistered.Add(Path);
            ++ScannerDeregisterCount;
            // ns-t23-rollhide: the SaveGame half of the same fact. ScannerDeregistered is session-scoped,
            // so after a reload it says nothing about a member suppressed in an earlier session -- and an
            // un-hide that skipped the re-registration on that basis would hand back a well that is
            // VISIBLE BUT NOT BUILDABLE, which is the gap RestoreOriginalsForReroll still has and a FAIL,
            // not a pass. Set inside this guard so the two can never disagree about what ran.
            Rec.bDeregisteredByUs = true;
        }
    };

    // SYMMETRY: the core's record lives on the entry and each satellite's on its own record, and both are
    // the SAME TYPE (FNodeShuffleWellSuppressionRecord), so HideOne cannot treat the two sides
    // differently even by accident. An UNCAPTURED satellite is suppressed here exactly as before -- it is
    // never spawned, so leaving it visible would strand it at the abandoned origin -- and it therefore
    // acquires an un-hide obligation exactly like every other member.
    HideOne(FindOriginalBaseByPath(E.CorePath), E.CoreSuppression);
    for (FNodeShuffleWellSatellite& S : E.Satellites)
    {
        HideOne(FindOriginalBaseByPath(S.SatellitePath), S.Suppression);
    }

    // H2b -- THE THROTTLE IS KEYED ON THE COUNTS, NOT ON THE CORE PATH.
    // Keyed on the path alone, this line printed ONCE per group per session, so the first pass's
    // numbers were the only numbers anyone ever saw -- and on 2026-08-07 that first pass reported
    // "0 mesh actor(s)" for five groups and the line never came back to say otherwise. A key that
    // includes the counts re-prints whenever the picture actually changes (a member streams in, a
    // piece is finally paired) and stays silent when it does not.
    //
    // meshesAlreadyHidden and indexedPieces are printed for one specific reason: a ZERO must now be
    // explicable from the line itself. "0 hidden, 0 already hidden, 0 indexed" says nothing was
    // paired; "0 hidden, 8 already hidden" says the work was done on an earlier pass. The old line
    // could not tell those apart, and printed the first next to "0 not streamed yet".
    //
    // ns-t23-rollhide: the PHASE and the ownership count join the key. Without the phase a roll-time
    // suppression and the first apply-pass re-assertion of the same group produce identical counts and
    // the second would be swallowed, so the log would never show that the re-assertion is running.
    const FString SuppressKey = FString::Printf(TEXT("%s|%d|%d|%d|%d|%d|%d|%d"), *E.CorePath, Hidden,
                                                MeshesHidden, MeshesAlready, Occupied, Unstreamed,
                                                NewlyOwned, bRollPhase ? 1 : 0);
    if ((Hidden > 0 || MeshesHidden > 0 || Occupied > 0 || Unstreamed > 0 || NewlyOwned > 0)
        && !WellSuppressLogged.Contains(SuppressKey))
    {
        WellSuppressLogged.Add(SuppressKey);
        int32 IndexedPieces = 0;
        if (const TArray<TWeakObjectPtr<UStaticMeshComponent>>* P = WellMeshIndex.Find(E.CorePath))
        {
            IndexedPieces += P->Num();
        }
        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            if (const TArray<TWeakObjectPtr<UStaticMeshComponent>>* P = WellMeshIndex.Find(S.SatellitePath))
            {
                IndexedPieces += P->Num();
            }
        }
        // ns-t23-rollhide -- WHICH COORDINATE THIS LINE MAY NAME DEPENDS ON THE PHASE, and getting it
        // wrong would make the line assert a world state that is not true. On the APPLY phase the group
        // has spawned and PlacedCoreLocation is where it stands. At ROLL time PlacedCoreLocation is the
        // PREVIOUS placement or zero and NOTHING has been built at the destination yet, so the roll
        // wording names DestCoreLocation and says explicitly that nothing is there.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-SUPPRESS core='%s' phase=%s: hid %d vanilla member(s) + %d mesh piece(s) (%d were ")
            TEXT("already hidden; %d piece(s) indexed for this group across all %d indexed member(s) ")
            TEXT("world-wide); %d newly taken into our suppression ledger this call; %d occupied (left ")
            TEXT("alone), %d not streamed yet (retried every pass). Visuals captured: core=%d, group=%s. %s"),
            *WellShort(E.CorePath), bRollPhase ? TEXT("ROLL") : TEXT("APPLY"),
            Hidden, MeshesHidden, MeshesAlready, IndexedPieces,
            WellMeshIndexMembers, NewlyOwned, Occupied, Unstreamed, E.bCoreVisualsCaptured ? 1 : 0,
            E.bGroupVisualsComplete ? TEXT("COMPLETE") : TEXT("INCOMPLETE"),
            bRollPhase
                ? *FString::Printf(TEXT("The group is DEALT to %s and NOTHING has been built there yet -- ")
                                   TEXT("it is built when a player reaches it."),
                                   *E.DestCoreLocation.ToCompactString())
                : *FString::Printf(TEXT("The relocated group now lives at %s."),
                                   *E.PlacedCoreLocation.ToCompactString()));
    }
}

// ------------------------------------------------------------------------------------------------
// THE PER-PASS DRIVER
// ------------------------------------------------------------------------------------------------
void ANodeShuffleSubsystem::ApplyWellRelocation(bool bWellShuffleEnabled, bool bRelocationEnabled,
                                                float SpawnRadiusCm)
{
    // RELOCATION REQUIRES THE RETYPE TOGGLE TOO. A well NodeShuffle does not manage is not a well it
    // may move -- and with ShuffleResourceWells off, ApplyWellRetype has already returned without
    // resolving anything, so the entry's AssignedResourceClassPath is not being maintained.
    const bool bOn = bWellShuffleEnabled && bRelocationEnabled;

    // ns-review-h2-r4 ROUND 9 R-1 -- DIAGNOSTICS ONLY (full rationale on the header declarations).
    // ReconcileAbandonedWellClaims reads these to label each mid-assembly tick OFF / AWAY / RETRY.
    // NOTHING BEHAVIOURAL READS EITHER FIELD; a stale value changes no decision, only a log line.
    // Cached rather than passed down because the reconciliation also runs from the roll tail.
    bWellLastApplyRelocationOn = bOn;
    WellLastApplySpawnRadiusCm = SpawnRadiusCm;

    // Once per session, BEFORE anything else touches a group: re-match our spawned well actors back to
    // their layout entries and re-establish every mCore link. This runs even when the toggles are off,
    // because a save that ALREADY holds relocated wells must keep them linked no matter what the
    // config now says -- a relocated well whose links are not restored dies silently, and "the user
    // turned the feature off" is not a reason to let that happen to wells already in their world.
    if (!bAdoptedRestoredWells && WellLayout.Num() > 0)
    {
        bAdoptedRestoredWells = true;
        AdoptRestoredWellGroups();
    }

    if (!bOn)
    {
        if (!bWellRelocDisabledLogged)
        {
            int32 Placed = 0;
            for (const FNodeShuffleWellEntry& E : WellLayout) { if (E.bGroupPlaced) { ++Placed; } }
            if (Placed > 0)
            {
                bWellRelocDisabledLogged = true;
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WELLH2: relocation is OFF (wellShuffle=%d relocate=%d) but this save holds %d ")
                    TEXT("already-relocated well(s). They are still SPAWNED and still LINKED every load -- ")
                    TEXT("turning the toggle off stops us moving wells, it does not move them back."),
                    bWellShuffleEnabled ? 1 : 0, bRelocationEnabled ? 1 : 0, Placed);
            }
        }
        // Still maintain placed groups: spawn-on-discovery, linking and suppression all continue.
    }

    ++WellAuditPasses;

    int32 Searching = 0, PlacedNow = 0, Spawned = 0, Deferred = 0, Maintained = 0, IncompleteSpawns = 0;
    // ns-t24-groupgate: entries whose placement the group-scoped occupancy gate refused this pass, and
    // entries it could not measure because nothing of the vanilla group resolved to a live actor. Kept
    // apart: a refusal is a decision, an unmeasured verdict is the absence of one, and summing them would
    // be the "zero with no denominator" this file already got wrong once.
    int32 GateRefused = 0, GateUnmeasured = 0;
    const bool bWellDiag = FNodeShuffleModule::AreDiagnosticsEnabled();
    // ns-t23-rollhide: how many UNPLACED entries had their suppression re-asserted this pass. A brand-new
    // population -- before roll-time commitment no unplaced entry was ever suppressed, so nothing ever
    // re-asserted for one, and a level actor that streams back in un-hidden would have undone the hide
    // with nothing to notice.
    int32 ReassertedUnplaced = 0;

    for (FNodeShuffleWellEntry& E : WellLayout)
    {
        // ns-t23-stage0 INSTRUMENT 2 (K3) -- THE ONLY INCREMENT SITE, AND IT IS BEFORE EVERY `continue`
        // BELOW ON PURPOSE. The window this measures is how long a well waits between being dealt a
        // destination and being placed, and the waiting a roll-time commit design would make expensive
        // is dominated by the passes an entry spends deferred with no player near its destination --
        // exactly the passes the `continue`s below skip. Counted for a relocation-FAILED entry too: that
        // is the population that would be absent permanently, and the census splits it out rather than
        // folding it into the total. Counted while the toggles are off as well, because an entry that is
        // dealt and unplaced is waiting regardless of what the config now says. Diagnostic only -- no
        // branch anywhere in this mod reads either field.
        if (E.bDestDealt && !E.bGroupPlaced) { ++E.PassesSinceDealt; }

        if (!E.bGroupPlaced)
        {
            // ================= ns-t23-rollhide: THE TWO OBLIGATIONS OF AN UNPLACED ENTRY =================
            // BOTH sit above every `continue` below, and that placement is load-bearing. The un-hide
            // intent is set at TERMINAL failure, which also sets bRelocationFailed -- so a drain placed
            // after the guard below would never run for the exact population it exists for. The
            // re-assertion likewise must cover an entry whose relocation is disabled, uncaptured or
            // failed: what makes a member need re-hiding is that a LEVEL ACTOR STREAMS BACK IN UN-HIDDEN,
            // and that has nothing to do with the entry's search state.
            //
            // MUTUALLY EXCLUSIVE, deliberately. An entry that owes a restore must not be re-suppressed on
            // the same pass -- the two would fight every 5 s and the well would flicker rather than come
            // back.
            if (E.bUnhidePending)
            {
                TryUnhideWellGroup(E, TEXT("deferred intent, re-attempted"));
            }
            else if (WellGroupHasSuppressedMember(E))
            {
                ++E.PassesSinceSuppressed;
                ++ReassertedUnplaced;
                SuppressVanillaWellGroup(E, EWellSuppressPhase::Apply);
            }

            if (!bOn || !E.bRelocate || !E.bOffsetsCaptured || !E.bDestDealt || E.bRelocationFailed)
            {
                continue;
            }
            // SPAWN-ON-DISCOVERY, exactly as for ordinary nodes: the search runs where a player is,
            // because that is where the terrain is streamed. Validation at roll time would trace into
            // unloaded regions and get void everywhere.
            if (!IsLocationNearAnyPlayer(E.DestCoreLocation, SpawnRadiusCm)) { ++Deferred; continue; }
            ++Searching;

            UClass* ResourceClass = LoadClassByPath(E.AssignedResourceClassPath);
            if (!ResourceClass || !ResourceClass->IsChildOf(UFGResourceDescriptor::StaticClass()))
            {
                if (!WellRelocFailLogged.Contains(E.CorePath))
                {
                    WellRelocFailLogged.Add(E.CorePath);
                    UE_LOG(LogNodeShuffle, Warning,
                        TEXT("WELLH2 core='%s': assigned resource '%s' %s -- not relocating. Left vanilla."),
                        *WellShort(E.CorePath), *E.AssignedResourceClassPath,
                        ResourceClass ? TEXT("is not a UFGResourceDescriptor") : TEXT("failed to load"));
                }
                continue;
            }

            // ============ ns-t24-groupgate: THE GROUP-SCOPED OCCUPANCY GATE ============
            // THE FIELD DEFECT THIS REMOVES, measured from the author's 2026-08-09 log. A player built a
            // Resource Well Pressurizer on the VANILLA ORIGIN core; eight seconds later the destination
            // validated after ~38 deferred attempts, the group spawned, and the suppression ran:
            //     "member='BP_FrackingCore18': NOT hidden -- in use (pressurizer-on-core)"
            //     "core='BP_FrackingCore18' phase=APPLY: hid 7 vanilla member(s) + 13 mesh piece(s) ...
            //      1 occupied (left alone)"
            // The core survived with the player's Pressurizer on it and all seven satellites vanished:
            // a well that can never produce, 990 m from the group that can.
            //
            // WHY IT HAPPENED, IN ONE SENTENCE: HideOne tests occupancy PER MEMBER and returns early for
            // that member only, but the invariant it protects -- do not take away a well the player has
            // built on -- is a GROUP property. This workspace's most-repeated defect: one rule applied to
            // one side of a relationship. It is PRE-EXISTING and is not a regression of the roll-time
            // commit feature; the suppression that fired was the apply phase, which predates it.
            //
            // THE GATE IS A REFUSAL TO START, NOT A REPAIR. Hiding an occupied member anyway would pull
            // the ground out from under a built Pressurizer, and un-hiding a group already relocated would
            // put two live copies in the world. The only safe direction is the mod's own stated fail-safe
            // (NodeShuffleWellEscalate.cpp): never a partial or broken well, only an untouched one.
            //
            // EvaluateWellPin IS REUSED RATHER THAN A NEW LOOP -- it is the same question, and this entry
            // is UNPLACED here (the enclosing branch tests that), so its source selection resolves to the
            // ORIGINAL level actors, which is exactly the population that is about to be hidden. It
            // already breaks on the first satellite in use and reports which actor and which signal.
            // SYMMETRY: the core AND every satellite record are handed to it -- INCLUDING uncaptured
            // records, which are never spawned but ARE hidden by HideOne, so an Extractor on one of those
            // is the mirrored version of the same defect and must refuse the placement identically.
            AFGResourceNodeFrackingCore* GateCore =
                Cast<AFGResourceNodeFrackingCore>(FindOriginalBaseByPath(E.CorePath));
            TArray<AFGResourceNodeFrackingSatellite*> GateSats;
            for (const FNodeShuffleWellSatellite& S : E.Satellites)
            {
                AFGResourceNodeFrackingSatellite* Sat =
                    Cast<AFGResourceNodeFrackingSatellite>(FindOriginalBaseByPath(S.SatellitePath));
                if (IsValid(Sat)) { GateSats.Add(Sat); }
            }
            const FNodeShuffleWellPinCheck GatePin =
                EvaluateWellPin(E, SpawnedWellCores, SpawnedWellSatellites, GateCore, GateSats);
            if (GatePin.IsPinned())
            {
                ++GateRefused;
                // Throttled on the VERDICT, not on the path: a refusal that changes member or changes
                // side is announced again, an unchanged one is said once. NOTHING announces a refusal
                // that STOPS -- the entry simply reaches TryPlaceWellGroup and the ordinary FOOTPRINT
                // VALIDATED / spawned COMPLETE lines are the evidence that it did. The state does not
                // self-clear -- the player's machine stays built -- so an unthrottled line prints forever.
                const FString GateKey = FString::Printf(TEXT("%s|%s|%d%d"), *E.CorePath,
                                                        *GatePin.FiredActorName,
                                                        GatePin.bCoreInUse ? 1 : 0,
                                                        GatePin.bSatelliteInUse ? 1 : 0);
                if (!WellGroupGateLogged.Contains(GateKey))
                {
                    WellGroupGateLogged.Add(GateKey);
                    UE_LOG(LogNodeShuffle, Display,
                        TEXT("WELLH2-GATE core='%s': PLACEMENT REFUSED THIS PASS. MEASURED -- the shared ")
                        TEXT("occupancy predicate returned true for actor '%s' of the VANILLA group ")
                        TEXT("(core signal '%s', satellite signal '%s'); the actors asked were %s, %d ")
                        TEXT("core and %d of %d satellite record(s) resolved live. The whole vanilla ")
                        TEXT("group is left UNTOUCHED -- nothing hidden, nothing spawned, nothing ")
                        TEXT("de-registered -- because hiding the rest of a group around a member a ")
                        TEXT("player has built on leaves a well that cannot produce. This is re-evaluated ")
                        TEXT("on every pass that reaches this gate, so the placement proceeds by itself ")
                        TEXT("once the building is gone. Said once per group per verdict."),
                        *WellShort(E.CorePath), *GatePin.FiredActorName,
                        GatePin.CoreWhy, GatePin.SatelliteWhy, WellPinSourceName(GatePin.Source),
                        GatePin.CoresTested, GatePin.SatellitesTested, E.Satellites.Num());
                }
                continue;
            }
            if (!GatePin.IsDecisive())
            {
                // NOT A REFUSAL, AND THE LINE MUST NOT READ AS ONE. Nothing of the vanilla group resolved
                // to a live actor, so no occupancy signal could be read -- and refusing here would refuse
                // every relocation whose ORIGIN is out of streaming range of its DESTINATION, which under
                // spawn-on-discovery is the normal case (the player stands at the destination). The
                // placement therefore proceeds. What is NOT measured is stated rather than implied: this
                // pass did not test whether anything is built on this well. Any member counted as
                // resolved above CAN still be hidden by the suppression that follows a successful spawn
                // on this pass. A member that streams in LATER carrying a building is refused by HideOne
                // alone, per member, which is the residual case named in the T24 handoff.
                ++GateUnmeasured;
                const FString UnmKey = E.CorePath + TEXT("|unmeasured");
                if (bWellDiag && !WellGroupGateLogged.Contains(UnmKey))
                {
                    WellGroupGateLogged.Add(UnmKey);
                    UE_LOG(LogNodeShuffle, Verbose,
                        TEXT("WELLH2-GATE core='%s': OCCUPANCY UNMEASURED, placement proceeding. The ")
                        TEXT("actors asked were %s and %d core plus %d of %d satellite record(s) ")
                        TEXT("resolved live. The predicate that produced this branch is IsDecisive()==0, ")
                        TEXT("which on this path means NO CORE resolved; any satellite(s) counted above ")
                        TEXT("were asked and reported not in use, and that is the whole of what was ")
                        TEXT("measured. NOT MEASURED: whether anything is built on the core, or on any ")
                        TEXT("satellite that did not resolve. Placement proceeds because refusing here ")
                        TEXT("would refuse every relocation whose origin is out of streaming range of ")
                        TEXT("its destination. Any member counted as resolved above CAN still be hidden ")
                        TEXT("by the suppression that follows a successful spawn on this pass. Said ")
                        TEXT("once per group per session."),
                        *WellShort(E.CorePath), WellPinSourceName(GatePin.Source), GatePin.CoresTested,
                        GatePin.SatellitesTested, E.Satellites.Num());
                }
            }

            if (!TryPlaceWellGroup(E, ResourceClass)) { continue; }
            ++PlacedNow;

            // ns-review-h2 F7 (HIGH): THE SPAWN, NOT ONLY THE VALIDATION, MUST BE ALL-OR-NOTHING.
            // SpawnWellGroup used to return "did anything spawn", so ONE successful satellite out of
            // ten flipped bGroupPlaced -- and SuppressVanillaWellGroup then hid EVERY vanilla member,
            // leaving a short well and no visible original. It now returns "is the group COMPLETE",
            // and nothing is suppressed until it is. An incomplete group is retried next pass with the
            // members it did get reused, so this costs a pass, not a re-search.
            if (SpawnWellGroup(E, ResourceClass))
            {
                E.bGroupPlaced = true;
                // ns-t23-stage0 INSTRUMENT 2: freeze the elapsed window, then zero the live counter so
                // its name stays literally true (a placed entry is not waiting). The increment above
                // runs before this line in the same pass, so the smallest value this can ever record is
                // 1 -- which makes 0 mean exactly one thing: no value was ever recorded for this entry.
                E.PassesFromDealToPlaced = E.PassesSinceDealt;
                E.PassesSinceDealt = 0;
                // ns-t23-rollhide: the group is placed, so the player has the well again and the
                // absence counter stops meaning anything. Zeroed so a non-zero value ALWAYS reads as
                // "absent right now" and never as "was absent once".
                E.PassesSinceSuppressed = 0;
                ++Spawned;
                ++WellGroupsPlacedThisSession;
                WellIncompleteSpawnCounts.Remove(E.CorePath); // assembled -- the counter starts fresh
                SuppressVanillaWellGroup(E);
                // H2b: dress the group IN THE SAME PASS it is suppressed, not on the next one.
                // SuppressVanillaWellGroup has just captured the look from the originals it hid, so
                // this is the first moment the capture exists -- and deferring it by a pass would put
                // a live, unmarked, unbuildable well in front of a player for ~5 s. Ordering matters
                // the other way too: suppression must run FIRST, or the capture would read a look we
                // had already hidden. Visuals are cosmetic-plus-collision and NEVER gate placement --
                // a group that cannot be dressed is still a placed group (design 2.4 vs Q3).
                ApplyWellGroupVisuals(E);
                // ns-review-h2 F12: audit THIS group the moment it is placed. The fixed-pass audit
                // fires ~40 s after load, but relocation is spawn-on-discovery -- so the wells a
                // tester actually flies to are placed LONG after pass 8 and were never audited at all.
                // The acceptance gate has to cover the group the tester is standing in front of.
                AuditOneWellGroup(E, TEXT("just-placed"));
            }
            else
            {
                ++IncompleteSpawns;
                NoteWellIncompleteSpawn(E);
            }
            continue;
        }

        // ---- ALREADY PLACED: maintain it every pass ----
        // Idempotent by construction: the link funnel is cheap, and the suppression re-hides only what
        // has streamed back in un-hidden.
        ++Maintained;
        UClass* ResourceClass = LoadClassByPath(E.AssignedResourceClassPath);
        if (ResourceClass && IsLocationNearAnyPlayer(E.PlacedCoreLocation, SpawnRadiusCm))
        {
            if (SpawnWellGroup(E, ResourceClass)) { ++Spawned; }
        }
        SuppressVanillaWellGroup(E);
        // H2b: re-assert the dressing every maintenance pass, for the same reason the link funnel
        // re-asserts every pass rather than once. A significance/streaming round trip, another mod, or
        // a reload can take a component's visibility or its collision response away, and the collision
        // is the acceptance criterion -- a well that silently stops being buildable is precisely the
        // failure this packet exists to remove. Idempotent: a dressed member costs a few pointer
        // compares. This is also the path that dresses a group RESTORED from a save, whose actors are
        // adopted rather than spawned.
        ApplyWellGroupVisuals(E);
    }

    // THE LINK AUDIT -- design §Q3 point 5's "log at both ends every session".
    // ns-review-h2 F12: TWO cadences, because one was not enough. The fixed pass reports a settled
    // world shortly after load (a boolean latch would have fired mid-load and reported a half-streamed
    // world as the answer); the slow repeating cadence then keeps covering groups placed later, which
    // under spawn-on-discovery is most of them. Per-group audits also fire the instant a group is
    // placed, at the call site above.
    if (WellAuditPasses == WellLinkAuditPass) { AuditWellGroupLinks(TEXT("settled")); }
    else if (WellAuditPasses > WellLinkAuditPass && (WellAuditPasses % WellLinkAuditCadence) == 0)
    {
        AuditWellGroupLinks(TEXT("cadence"));
    }

    // ns-review-h4 F2 (BLOCKING): the ORPHAN SWEEP. Driven by the handle set, not by call sites, so it
    // catches every route that abandons a group without despawning it -- including the six roll-time
    // `continue`s that return before RollWellRelocation's despawn is ever reached, and including
    // routes nobody has written yet. Cadenced rather than per-pass because it is a set difference over
    // the whole layout; running it after the audit means a group placed THIS pass is already recorded
    // and can never be mistaken for an orphan.
    //
    // ns-review-h2-r2 F-B: bOn now gates PASS B ONLY -- the world scan. Pass A (handle-driven) and the
    // claim reconciliation run on EVERY call, including this one with both toggles off, because their
    // safety does not depend on the config: pass A only ever destroys actors whose handles WE created.
    // With the feature off over a save that still holds handles, that is the ONLY thing that reclaims
    // an abandoned partial group. Do not re-add a gate here.
    //
    // ns-review-h5 F1 (BLOCKING, the finding that parked this packet): bOn IS PASSED IN, and the sweep
    // refuses to look at the world without it. The `!bOn` block above deliberately does not return --
    // a save that already holds relocated wells must keep them spawned, linked and suppressed whatever
    // the config now says -- so this call site was reached with BOTH toggles off, on every session, in
    // saves that never enabled the feature. With an empty WellLayout the sweep's accounted-for set is
    // empty, and its location backstop then classified every runtime fracking actor in the world as an
    // orphan and destroyed it at pass 8, ~40 s in. That is the node-destroyer behaviour this mod ships
    // a two-layer defence AGAINST (cookbook §20), and it had no opt-out of its own. The gate is a
    // parameter rather than a config read inside the sweep so that it cannot disagree with the pass it
    // belongs to, and the sweep re-checks "at least one group is actually placed" on its own.
    if (WellAuditPasses == WellLinkAuditPass
        || (WellAuditPasses > WellLinkAuditPass && (WellAuditPasses % WellOrphanSweepCadence) == 0))
    {
        SweepOrphanedWellActors(WellAuditPasses == WellLinkAuditPass ? TEXT("settled") : TEXT("cadence"),
                                bOn);
    }

    if (Searching > 0 || PlacedNow > 0 || Spawned > 0 || IncompleteSpawns > 0 || ReassertedUnplaced > 0
        || GateRefused > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2 pass: %d group(s) searching, %d newly validated, %d spawned COMPLETE, ")
            TEXT("%d spawned INCOMPLETE (not marked placed, nothing suppressed, retried next pass), ")
            TEXT("%d maintained, %d deferred (no player near the destination), %d unplaced group(s) had ")
            TEXT("an existing suppression re-asserted. ns-t24-groupgate, out of the %d searching: %d ")
            TEXT("refused by the group occupancy gate (the shared occupancy predicate returned true for ")
            TEXT("a member of the vanilla group; it is left untouched) and %d proceeded with occupancy ")
            TEXT("UNMEASURED (no vanilla CORE resolved to a live actor on that entry's pass). %d placed ")
            TEXT("this session."),
            Searching, PlacedNow, Spawned, IncompleteSpawns, Maintained, Deferred, ReassertedUnplaced,
            Searching, GateRefused, GateUnmeasured, WellGroupsPlacedThisSession);
    }

    // ns-t23-stage0 INSTRUMENT 2 (K3). LAST, so it reports the state this pass ended in rather than the
    // state it started in -- a group placed on this pass must appear in the placed population of this
    // pass's line, not in the next one's. Log only; see NodeShuffleWellStage0.cpp.
    EmitWellDeferralCensus();

    // ns-t23-rollhide: the stranding detector and the opposite-polarity pair, LAST and in that order, for
    // the same reason -- both must describe the state this pass ended in. Both are log-only.
    // The toggle is read here, once per pass, ONLY so the test pair can name it: no decision in this
    // packet reads it outside the roll, because the ledger -- not the config -- is the authority on what
    // we suppressed. A player who turns the toggle off mid-save still owes the un-hide of what was
    // already hidden, and gating the restore on the config would strand exactly that population.
    EmitWellStrandedCensus();
    EmitWellRollHideTestPair(FNodeShuffleConfigStruct::GetActiveConfig(this).CommitWellsAtRoll);
}
