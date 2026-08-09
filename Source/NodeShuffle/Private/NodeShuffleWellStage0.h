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
    enum EGate : int32
    {
        Gate_Void = 0,        // no terrain under the probe (the DEFER case, never a yaw rejection)
        Gate_Water = 1,
        Gate_Cliff = 2,
        Gate_NodeOverlap = 3,
        Gate_Buildable = 4,
        Gate_Unclassified = 5, // any reason string this classifier does not recognise, counted not dropped
        Gate_Count = 6
    };

    int32 CoreRejects[Gate_Count] = { 0, 0, 0, 0, 0, 0 };
    int32 SatRejects[Gate_Count] = { 0, 0, 0, 0, 0, 0 };
    // The denominators. Without them a zero in any bucket above cannot be told from a bucket that was
    // never given a chance to fire -- the failure mode this repo has now shipped twice.
    int32 CoreProbes = 0;
    int32 SatProbes = 0;
    int32 YawsTried = 0;
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
    return FNodeShuffleWellProbeCensus::Gate_Unclassified;
}

// The emitter for instrument 3. Free rather than a member because it reads nothing but its arguments.
// Outcome and TerminatedBy are supplied by the caller from the branch it is actually returning on --
// never inferred here from the counters, because inferring an outcome from counts is how a log line
// starts asserting a cause instead of reporting a measurement.
// AttemptsToExhaust is ceil(WellYawSteps / WellYawAttemptsPerPass): how many CONSECUTIVE attempts a
// yaw-exhaustion outcome costs, against one for a core rejection. Passed in because both constants are
// private to ANodeShuffleSubsystem and this is a free function; the caller already sees both.
void LogWellProbeCensus(const FString& CoreLabel, const FNodeShuffleWellProbeCensus& C,
                        const TCHAR* Outcome, const TCHAR* TerminatedBy,
                        int32 YawCursor, int32 YawSteps, int32 CapturedSatellites,
                        int32 AttemptsToExhaust);
