// Packet ns-t35-gatereach: THE SOLID-NODE PATH'S GATE CENSUS. LOG ONLY.
//
// WHY THIS FILE EXISTS. docs/TECH-DEBT.md T35 records that the well path's enclosure gate reported a
// bare zero on both sides across 2,961 census lines of one session, with no way to tell a gate that
// refused nothing from a gate nothing ever reached. The same entry carries the author's hypothesis --
// "we may find that we are not doing the same checks as we do for solids, or that solids are having
// the same issue" -- and BOTH halves of that are about the SOLID-NODE path, which had no census of any
// kind. Counting only the well path answers neither half.
//
// WHAT IS AND IS NOT ANSWERABLE FROM HERE, stated before the numbers rather than after them.
//   * ns-t27-corefirst made IsSpotEnclosed one shared member and EnsureNewNodeSpawned's IsEnclosed
//     lambda delegates to it, so the two paths CANNOT be running different enclosure code. That is a
//     fact about the source, checkable by reading it, and this census neither adds to it nor tests it.
//   * What has never been measured is the RATE: how often each path reaches that gate at all. That is
//     what these counters are, and it is the only thing they are.
//   * The counters do NOT say why any gate did or did not run. Every field below is a count taken by
//     the run that emitted it.
//
// WHAT THIS PACKET DID NOT DO, because it would have been a behaviour change: the node path's
// occupancy test is ONE lambda (OverlapsAt) that folds the 800 cm resource-node radius and the 600 cm
// non-foundation buildable radius into a single boolean, and it is called from inside a short-circuit
// chain in the nudge loop. Splitting that boolean into two gates would mean rewriting the predicate,
// so the census reports the merged gate and says so. The well path's census does split them; that
// asymmetry is in the CODE, not in the measurement, and a reader comparing the two lists must not read
// the node path's single occupancy entry against the well path's two.
//
// IMPORT DISCIPLINE (memory: sf-shipping-export-trap). This file reaches no engine or FactoryGame
// entry point at all: it reads our own int32 members and calls UE_LOG and FString::Printf. That is the
// PREDICTION; the import table is MEASURED on the built DLL and any symbol that moves is named in the
// handoff. Predictions of this shape have been falsified on this machine before.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"

void ANodeShuffleSubsystem::EmitNodeGateCensus()
{
    // ns-t36-probefix (T35 cold review F5): GATED THE SAME WAY ITS OPPOSITE NUMBER IS.
    // This is a ~1.9 KB Display line whose throttle key includes the all-calls counters, and those move
    // on every spiral-nudge probe -- so the key changed on nearly every pass and the line emitted on
    // nearly every pass, for every player, with diagnostics off. The well path's equivalent
    // (LogWellProbeCensus, called from TryPlaceWellGroup behind `bDiag`) has always been behind
    // FNodeShuffleModule::AreDiagnosticsEnabled(), as is the ENCLOSURE: line in the same spawn
    // function this census counts. The gate is HERE rather than at the call site so any future caller
    // inherits it. Consequence, stated rather than left to be discovered: while diagnostics are off the
    // throttle key is not updated either, so the first line after diagnostics are turned on carries the
    // full session running totals -- which is what these counters have always been.
    if (!FNodeShuffleModule::AreDiagnosticsEnabled()) { return; }
    // FIRE CONDITION, and it is restated inside the line: called once per ApplyLayout pass, and emits
    // only when one of the counters below has moved since the last emission. A pass in which no entry
    // reached the spawn path at all therefore emits NOTHING, and that silence is the one thing this
    // line cannot tell you about -- it does not distinguish "no node placement was attempted" from
    // "this subsystem never ran a pass". The counters are cumulative for the life of this subsystem
    // instance, so every number is a session running total and not a per-pass figure.
    const FString Key = FString::Printf(TEXT("%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d"),
        NodeGateSettleReached, NodeGateSettleRejectedNoWater, NodeGateSettleRejectedWater,
        NodeGatePrimaryOccupiedReached, NodeGatePrimaryOccupiedRejected,
        NodeGatePrimaryEnclosedReached, NodeGatePrimaryEnclosedRejected,
        NodeGateCallOccupiedReached, NodeGateCallOccupiedRejected,
        NodeGateCallEnclosedReached, NodeGateCallEnclosedRejected);
    if (Key == NodeGateCensusLastKey) { return; }
    NodeGateCensusLastKey = Key;
    // Nothing has been counted yet: emitting a line of zeros with no denominators anywhere would be the
    // exact defect this packet exists to remove.
    if (NodeGateSettleReached == 0 && NodeGatePrimaryOccupiedReached == 0) { return; }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("NODEGATECENSUS: THE SOLID-NODE PLACEMENT GATES, EACH AS A REJECTED COUNT WITH ITS ")
        TEXT("OWN REACHED COUNT. Every figure is a running total over the life of this subsystem ")
        TEXT("instance, not a per-pass count. SETTLE GATE: reached %d time(s); of those it refused %d ")
        TEXT("without the water-no-land signal set and %d with it. That split is BY THAT SIGNAL and is ")
        TEXT("not a water-versus-cliff split: RaycastSettle tests terrain, water and slope inside ")
        TEXT("itself and returns one boolean plus that one flag, so this census cannot separate a void ")
        TEXT("probe from a steep one and does not pretend to. PRIMARY-SPOT GATES -- one test per entry ")
        TEXT("per visit, at the entry's location AS IT STANDS WHEN THE TEST RUNS. ns-t36-probefix ")
        TEXT("corrects the label these counters used to carry: they were called the DEALT spot, and ")
        TEXT("that is wrong for any entry the settle step has moved (it writes the impact point, and on ")
        TEXT("the surface it may spiral to another XY first) or that a previous pass nudged (the nudge ")
        TEXT("is persisted back onto the entry). The population is still the one comparable to a well ")
        TEXT("CORE -- one primary test per entry per visit against many nudge probes -- and that is why ")
        TEXT("it is counted apart: the ")
        TEXT("occupancy gate was reached %d time(s) and rejected %d of them; the enclosure gate was ")
        TEXT("reached %d time(s) and rejected %d of them. The enclosure gate is reached on the primary ")
        TEXT("spot only when the occupancy gate did not reject it, so its reached count can never be ")
        TEXT("the larger of the two, and the two are equal when the occupancy gate rejected nothing. ")
        TEXT("ALL CALLS IN THE SPAWN PATH, the primary spot and ")
        TEXT("every spiral-nudge probe together: occupancy reached %d and rejected %d; enclosure ")
        TEXT("reached %d and rejected %d. Those two populations are NOT interchangeable and must not be ")
        TEXT("pooled -- one nudging entry contributes many probes to the second pair and exactly one to ")
        TEXT("the first, which is the whole reason the second pair moves nearly every pass. WHAT THE ")
        TEXT("OCCUPANCY GATE IS HERE: a single predicate that folds the resource-")
        TEXT("node radius and the non-foundation buildable radius into one boolean, so a rejection ")
        TEXT("above does not say which of the two fired. The well path's census splits them; that ")
        TEXT("difference is in the code, not in this measurement, and the two must not be read against ")
        TEXT("each other entry for entry. WHAT THE ENCLOSURE GATE IS HERE: the shared ")
        TEXT("ANodeShuffleSubsystem::IsSpotEnclosed that the well path also calls, so a difference ")
        TEXT("between the two paths' reached counts is a difference in how often the predicate RUNS and ")
        TEXT("cannot be a difference in what it computes. THIS LINE STATES NO CAUSE. Every number on it ")
        TEXT("was counted by this run, and no field asserts why any gate did or did not fire. Fire ")
        TEXT("condition: once per apply pass, only while diagnostics are enabled, and only when one of ")
        TEXT("the counters above changed since the last line. While diagnostics are off the counters ")
        TEXT("still accumulate but nothing is emitted and the change-detector is not updated, so the ")
        TEXT("first line after they are turned on carries the totals accrued in the silence too. ")
        TEXT("There is deliberately NO pass ordinal on this line: the only pass counter this subsystem ")
        TEXT("keeps counts WELL apply passes, and putting it on a solid-node line would be a wrong ")
        TEXT("label. Order these lines by their timestamps."),
        NodeGateSettleReached, NodeGateSettleRejectedNoWater, NodeGateSettleRejectedWater,
        NodeGatePrimaryOccupiedReached, NodeGatePrimaryOccupiedRejected,
        NodeGatePrimaryEnclosedReached, NodeGatePrimaryEnclosedRejected,
        NodeGateCallOccupiedReached, NodeGateCallOccupiedRejected,
        NodeGateCallEnclosedReached, NodeGateCallEnclosedRejected);
}
