#pragma once

// Packet ns-t23-stage0 (branch wip/h2-relocation): the PURE half of T23 Stage 0's three instruments.
//
// STAGE 0 IS MEASUREMENT ONLY. _team/nodeshuffle-followups/T23-rolltime-commit-design.md proposes moving
// the vanilla-well capture and suppression to ROLL time, and its own §6 says stage 0 exists to produce
// the numbers that decide whether stages 1-3 happen at all. Nothing in this packet captures, suppresses,
// hides, spawns, destroys or writes a placement field. If a measurement could only be obtained by
// changing behaviour, it is NOT taken and the report says so.
//
// This header carries only what BOTH the collection site and the emission site need: the per-attempt
// counter block for instrument 3 and the gate classifier that fills it. It follows the same rule
// NodeShuffleWellRelocate.h states for itself -- everything here is pure and does not log; the emitters
// live in NodeShuffleWellStage0.cpp and each owns its own prefix and cadence.

#include "CoreMinimal.h"

// ---------------------------------------------------------------------------------------------
// INSTRUMENT 3 -- WHY A YAW WAS REJECTED, SPLIT CORE vs SATELLITE
// ---------------------------------------------------------------------------------------------
// ValidateWellMemberSpot (NodeShuffleWellFootprint.cpp) already returns a reason string for every
// refusal, and TryPlaceWellGroup already logs one Verbose line per rejected yaw. What has never existed
// is the AGGREGATE: over one placement attempt, how many refusals came from the CORE probe and how many
// from a SATELLITE probe, split by which of the five gates fired.
//
// WHY THAT SPLIT AND NOT ANOTHER. The T23 design's stage-4 option ("free satellite placement") rests on
// the premise that a well would fit almost anywhere if its satellites were placed independently. That
// premise can only hold for attempts whose refusals come from SATELLITES; a refusal of the CORE's own
// spot is not addressable by freeing the satellites, because the core is placed first and everything
// else hangs off it. This counter block is the cheapest test of that premise that exists, and it is
// capable of falsifying it.
//
// WHAT THE COUNTS CAN AND CANNOT BE COMPARED WITH -- READ BEFORE READING A RATIO OFF THEM.
// The two probe populations are NOT the same size and are not made the same size by any change here.
// TryPlaceWellGroup validates the core ONCE per attempt and then reuses that settled location for every
// yaw; satellites are re-probed on every yaw, and each yaw early-outs on its FIRST failing satellite.
// So per attempt: core probes <= 1, satellite probes <= yaws tried, and one satellite reject per yaw at
// most. A raw "satellites reject more than cores" therefore says almost nothing on its own. That is why
// each side carries its own probe denominator, and why the emitted line also names which side produced
// the rejection that ENDED the attempt -- the terminating side is the comparable fact.
struct FNodeShuffleWellProbeCensus
{
    // Index order is fixed and is the order the emitter prints in. It matches the order the gates are
    // evaluated in ValidateWellMemberSpot, which is deliberate: a reader comparing a rejection to the
    // source reads them top to bottom in the same sequence.
    // ns-t27-corefirst: the list grew by three and the ORDER CLAIM IS NOW TWO-PHASE. Gates 0-5 are
    // ValidateWellMemberSpot's own, in the order that function evaluates them (enclosure last, which
    // is where the ordinary node path applies it). Gates 6-7 are LAYOUT gates: they are evaluated by
    // TryPlaceWellGroup AFTER the footprint test returns true, because they are about the candidate's
    // relationship to the rest of ITS OWN GROUP, which the footprint test knows nothing about. So the
    // full list is still "in evaluation order", but it spans two functions and this is the seam.
    enum EGate : int32
    {
        Gate_Void = 0,        // no terrain under the probe (the DEFER case, never a placement rejection)
        Gate_Water = 1,
        Gate_Cliff = 2,
        Gate_NodeOverlap = 3,
        Gate_Buildable = 4,
        Gate_Enclosed = 5,    // ns-t27-corefirst: 7+ of 8 horizontal rays blocked (T26's missing gate)
        Gate_Sibling = 6,     // ns-t27-corefirst: too close to the core or to an already-placed sibling
        Gate_RelativeZ = 7,   // ns-t27-corefirst: settled too far above/below the core
        Gate_Unclassified = 8, // any reason string this classifier does not recognise, counted not dropped
        Gate_Count = 9
    };

    int32 CoreRejects[Gate_Count] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    int32 SatRejects[Gate_Count] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    // ns-t35-gatereach: THE DENOMINATOR EACH REJECT COUNT WAS MISSING, and it is what T35 is. Element G
    // is incremented immediately BEFORE gate G is evaluated, so it counts the times that gate got a
    // CHANCE to fire. Measured 2026-08-09 on build 2026-08-09-t27-3: the enclosure bucket read zero on
    // both sides across 2,961 census lines while 2,032 of 2,042 attempts ended at the void gate, which
    // is gate index 0 -- and with only a reject count there is no way to tell a gate that passed
    // everything from a gate nothing ever reached. Gates 0-5 are filled inside ValidateWellMemberSpot
    // (which is handed a pointer to one of these arrays); gates 6-7 are filled by TryPlaceWellGroup at
    // the two layout gates it evaluates itself.
    // THE UNCLASSIFIED SLOT IS NOT AN EVALUATION POSITION AND ITS DENOMINATOR IS DIFFERENT: it is
    // incremented once per probe that ENTERED ValidateWellMemberSpot, because an unrecognised reason
    // string can be produced by any refusal inside that function -- including the no-world refusal that
    // happens before any gate runs. So its reached value should equal CoreProbes / SatProbes exactly,
    // and a disagreement is a defect in the counting, not a fact about the world.
    int32 CoreReached[Gate_Count] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    int32 SatReached[Gate_Count] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    // The denominators. Without them a zero in any bucket above cannot be told from a bucket that was
    // never given a chance to fire -- the failure mode this repo has now shipped twice.
    // Two of the buckets have a denominator of their OWN and it is not CoreProbes or SatProbes:
    // Gate_Sibling and Gate_RelativeZ can only fire on a candidate that already PASSED the footprint
    // test, so their chances-to-fire are (SatProbes - satellite footprint rejects), not SatProbes. The
    // emitter prints that subtraction rather than leaving a reader to make it.
    int32 CoreProbes = 0;
    int32 SatProbes = 0;
    // ns-t27-corefirst: renamed from YawsTried, because the thing being counted changed. It is now the
    // number of INDEPENDENT-LAYOUT ATTEMPTS made in this call, each attempt being a full re-draw of
    // every captured satellite. A field name that still said "yaws" would be the wrong-label failure
    // this repo has already shipped once (notStreamed=%d on a counter that tested path resolution).
    int32 AttemptsTried = 0;
    // ns-t27-corefirst: how many captured satellites failed to find ANY spot within their per-satellite
    // draw budget, summed over the attempts in this call. This is the cost of the all-or-nothing group
    // decision -- a well NEVER places short (that is T15's permanent-shrink defect and this design
    // refuses to create a second instance of it), so every one of these killed a whole attempt.
    int32 SatBudgetExhausted = 0;
    // ns-t27-review F5: the largest number of draws any ONE satellite spent in this call, WHETHER OR NOT
    // it succeeded. The previous version recorded successes only, which made it structurally incapable of
    // answering the one question it was added to answer -- whether WellSatPlacementTries is binding --
    // because the binding case is exactly the case it excluded. A satellite that exhausts its budget
    // records the full budget here, so MaxDrawsAnySat == WellSatPlacementTries beside a non-zero
    // SatBudgetExhausted is the signal to raise the budget, and a value well under it beside a non-zero
    // SatBudgetExhausted is impossible by construction.
    int32 MaxDrawsAnySat = 0;
    // ns-t27-perf: how many AFGResourceNode actors the ONE hoisted world scan kept for this call --
    // the size of the set every satellite probe's node-overlap gate then tested against, in place of
    // re-walking the whole level once per probe. -1 means NO CACHE WAS BUILT for this call (the call
    // returned at the core gate before the hoist point), which is a different fact from a cache of
    // size 0 and must not be read as one. It is a COUNT taken this run; it states no cause and makes
    // no claim about how long the scan took -- that is the elapsed-ms figure's job.
    int32 NodesInScope = -1;
};

// Map one ValidateWellMemberSpot reason string onto a gate index. The prefixes are the literal strings
// that function writes (NodeShuffleWellFootprint.cpp): the two overlap gates append the offending
// actor's name, so they are matched by prefix rather than by equality. An unrecognised string lands in
// Gate_Unclassified rather than being dropped -- a reason this classifier has not been taught about must
// show up as a number, not as a silently smaller total.
inline int32 WellRejectGateIndex(const FString& Reason)
{
    if (Reason.StartsWith(TEXT("void")))          { return FNodeShuffleWellProbeCensus::Gate_Void; }
    if (Reason.StartsWith(TEXT("water")))         { return FNodeShuffleWellProbeCensus::Gate_Water; }
    if (Reason.StartsWith(TEXT("cliff")))         { return FNodeShuffleWellProbeCensus::Gate_Cliff; }
    if (Reason.StartsWith(TEXT("node-overlap")))  { return FNodeShuffleWellProbeCensus::Gate_NodeOverlap; }
    if (Reason.StartsWith(TEXT("buildable")))     { return FNodeShuffleWellProbeCensus::Gate_Buildable; }
    // ns-t27-corefirst. The first is written by ValidateWellMemberSpot; the last two are written by
    // TryPlaceWellGroup's layout gates, which is why this classifier's header says "two phases".
    if (Reason.StartsWith(TEXT("enclosed")))      { return FNodeShuffleWellProbeCensus::Gate_Enclosed; }
    if (Reason.StartsWith(TEXT("sibling")))       { return FNodeShuffleWellProbeCensus::Gate_Sibling; }
    if (Reason.StartsWith(TEXT("relZ")))          { return FNodeShuffleWellProbeCensus::Gate_RelativeZ; }
    return FNodeShuffleWellProbeCensus::Gate_Unclassified;
}

// The emitter for instrument 3. Free rather than a member because it reads nothing but its arguments.
// Outcome and TerminatedBy are supplied by the caller from the branch it is actually returning on --
// never inferred here from the counters, because inferring an outcome from counts is how a log line
// starts asserting a cause instead of reporting a measurement.
// AttemptsToExhaust is ceil(WellLayoutAttempts / WellLayoutAttemptsPerPass): how many CONSECUTIVE
// passes a layout-exhaustion outcome costs, against one for a core rejection. Passed in because both
// constants are private to ANodeShuffleSubsystem and this is a free function; the caller sees both.
// ns-t27-corefirst: AttemptCursor / AttemptLimit were YawCursor / YawSteps. Same two positions, and
// the same SaveGame field still feeds the first, but the unit is now a full independent-layout attempt
// rather than one yaw of a 36-step permutation.
// ns-t27-fixes-review F-E: SatPlacementTries is passed in for the same reason AttemptsToExhaust is --
// it is private to ANodeShuffleSubsystem and this is a free function. It exists so MaxDrawsAnySat is
// printed WITH the budget it should be read against, on the same line. Without it that field carried a
// number and no scale on a default install, because the only other line that prints the budget
// (WELLH2-SATDRAW) is Verbose and diagnostics-gated.
// ns-t27-fixes-review F-A: ElapsedMs is wall-clock for the ONE TryPlaceWellGroup call being reported,
// measured with FPlatformTime::Seconds at the top of that function and again at each emission point. It
// is a MEASUREMENT of this call on this machine in this frame. It attributes nothing, names no culprit,
// and must never be quoted as the cost of a feature -- the census's probe counts are what divide into it.
void LogWellProbeCensus(const FString& CoreLabel, const FNodeShuffleWellProbeCensus& C,
                        const TCHAR* Outcome, const TCHAR* TerminatedBy,
                        int32 AttemptCursor, int32 AttemptLimit, int32 CapturedSatellites,
                        int32 AttemptsToExhaust, int32 SatPlacementTries, double ElapsedMs);
