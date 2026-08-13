// Packet ns-t54-immediate-hide (branch wip/h2-relocation): THE AUTHOR'S RULING, and the instruments
// that make it falsifiable.
//
// THE RULING, VERBATIM (author, 2026-08-10): "It is a shuffle, I've repeatedly said I want things
// hidden immediately on a shuffle and that includes ALL things we shuffle."
//
// WHAT WAS ACTUALLY WRONG, MEASURED BEFORE ANY CODE WAS WRITTEN -- and it is NOT what T54's own
// pre-scoped starting point said. Two suppression call sites gated on bGroupPlaced was the picture
// before ns-t23-rollhide landed. Since that packet there are FOUR, and the ROLL one
// (NodeShuffleWellRelocateRoll.cpp, phase-3 commit) is not gated on placement at all. The real gap is
// narrower and worse:
//   * the roll-time hide is behind a DEFAULT-OFF toggle (CommitWellsAtRoll), so on the author's save
//     it never ran;
//   * the roll-time hide can only ever hide what is RESIDENT AT THE INSTANT OF THE ROLL, and a roll
//     runs once. An origin that streams in a minute later was never revisited;
//   * the apply pass's unplaced branch only ever RE-ASSERTS an existing suppression
//     (WellGroupHasSuppressedMember) -- it never INITIATES one. So an entry the roll marked as moving
//     but did not hide stayed vanilla-visible and vanilla-buildable until its replacement placed,
//     which is presence-gated and therefore unbounded.
// The fix is a third arm in that branch: INITIATE the suppression at load/stream-in for every entry
// the roll marked as moving. It runs every pass, so an origin that streams in later is caught, which
// the roll-time path structurally cannot do.
//
// THE ORDINARY-NODE PATH NEEDED NO CHANGE AND WAS VERIFIED, NOT ASSUMED. SuppressOriginalNodes
// (NodeShuffleSubsystem.cpp) walks OriginalNodeRecord -- built once at roll time,
// NodeShuffleSubsystem.cpp:1601 -- and hides every record the moment its path resolves to a live
// actor. Nothing in it consults whether the replacement spawned. Solid nodes AND oil (a liquid entry
// in the same Layout, same record, same loop) were already immediate. Wells were the only population
// coupled to replacement existence.
//
// WHAT THIS FILE HOLDS, and why none of it is in the two files that already carry this feature:
// NodeShuffleWellRelocateApply.cpp is 1200+ lines and NodeShuffleWellUnhide.cpp is 590, both already
// over this project's 500-line rule. Nothing here is new machinery -- it is the SHARED OCCUPANCY
// LOOKUP (so the immediate hide and the placement gate cannot drift apart), the per-pass census, and
// the opposite-polarity pair.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleWellCensus.h"   // AFGResourceNodeFrackingCore / ...Satellite
#include "NodeShuffleWellRetype.h"   // FNodeShuffleWellPinCheck / EvaluateWellPin / WellShort

// ------------------------------------------------------------------------------------------------
// THE SHARED OCCUPANCY LOOKUP
// ------------------------------------------------------------------------------------------------
// SYMMETRY, which is this workspace's most-repeated defect class: the immediate hide and the
// placement gate ask the SAME question -- "has the player built on any member of this vanilla
// group?" -- and before this packet the actor-gathering half of it existed exactly once, inline in
// ApplyWellRelocation. A second inline copy at the hide site is how the two acquire different
// populations through an edit that touches only one of them. So the gathering lives here, once, and
// both sites call it; the TEST itself was already single-sourced (EvaluateWellPin -> IsWellMemberInUse)
// and is untouched.
//
// It resolves the VANILLA level actors only. That is not a choice this function makes: EvaluateWellPin
// selects its own source from E.bGroupPlaced, and both callers are inside the `!E.bGroupPlaced` branch,
// so the selection resolves to ENodeShuffleWellPinSource::Original either way. The originals are also
// exactly the population the hide is about to act on.
//
// NOTHING HERE MEASURES WHY A MEMBER DID NOT RESOLVE. CoresTested / SatellitesTested report how many
// paths resolved to a live actor at this instant; whether an unresolved one is unstreamed, destroyed
// or holding a stale path is untested here and no caller may claim otherwise.
void ANodeShuffleSubsystem::EvaluateVanillaWellGroupOccupancy(const FNodeShuffleWellEntry& E,
                                                              FNodeShuffleWellPinCheck& OutCheck)
{
    AFGResourceNodeFrackingCore* GateCore =
        Cast<AFGResourceNodeFrackingCore>(FindOriginalBaseByPath(E.CorePath));
    TArray<AFGResourceNodeFrackingSatellite*> GateSats;
    for (const FNodeShuffleWellSatellite& S : E.Satellites)
    {
        // SYMMETRY: UNCAPTURED records are included. They are never spawned at the destination but they
        // ARE hidden by HideOne, so an Extractor standing on one is the mirrored version of the T24
        // defect and must refuse the hide identically.
        AFGResourceNodeFrackingSatellite* Sat =
            Cast<AFGResourceNodeFrackingSatellite>(FindOriginalBaseByPath(S.SatellitePath));
        if (IsValid(Sat)) { GateSats.Add(Sat); }
    }
    OutCheck = EvaluateWellPin(E, SpawnedWellCores, SpawnedWellSatellites, GateCore, GateSats);
}

// ------------------------------------------------------------------------------------------------
// THE PER-PASS CENSUS
// ------------------------------------------------------------------------------------------------
// ONE truthful census retires whole classes of log archaeology, and every count below carries the
// denominator it was drawn from -- "0 refused" says nothing unless the line states how many chances it
// had (memory: lessons-zero-needs-a-denominator, twice in one day in this same file family).
//
// LEGEND DISCIPLINE. Fields are described in PROSE and are never exemplified in the shape they print;
// this repo has shipped a legend token that made a commit message's own recommended grep match every
// line. To count occurrences of this census, anchor on the line prefix, which appears once per line.
//
// EVERY NUMBER IS A MEASUREMENT FROM THIS PASS. The line names the predicate that produced each bucket
// and never the world-state a reader might infer from it: "no core resolved to a live actor" is what
// was tested, and it is NOT a claim that the origin has not streamed.
void ANodeShuffleSubsystem::EmitWellImmediateHideCensus()
{
    if (WellImmediateCandidatesThisPass == 0 && WellImmediateDisableRestoreArmedThisPass == 0)
    {
        return; // nothing was asked this pass; a line here would be a zero with no question behind it
    }

    const FString Key = FString::Printf(TEXT("%d|%d|%d|%d|%d|%d|%d|%d|%d"),
                                        WellImmediateCandidatesThisPass,
                                        WellImmediateHiddenThisPass,
                                        WellImmediateGateRefusedThisPass,
                                        WellImmediateGateUnmeasuredThisPass,
                                        WellImmediateTookNothingThisPass,
                                        WellImmediateCaptureIncompleteThisPass,
                                        WellImmediateUncapturedHiddenThisPass,
                                        WellImmediateSatelliteRecordsHiddenThisPass,
                                        WellImmediateDisableRestoreArmedThisPass);
    if (Key == WellImmediateCensusLastKey) { return; }
    WellImmediateCensusLastKey = Key;

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-IMMEDIATE pass %d: %d unplaced well entr(ies) were marked as moving by the roll and ")
        TEXT("therefore eligible for an immediate origin hide on THIS pass (an entry already carrying a ")
        TEXT("suppression record is not among them -- it is re-asserted instead and counted on the ")
        TEXT("WELLH2 pass line). Of those %d: %d had the vanilla origin suppressed on this pass; %d were ")
        TEXT("REFUSED because the shared occupancy predicate returned true for a member of the vanilla ")
        TEXT("group (the whole group is left untouched, re-evaluated every pass); %d could not be ")
        TEXT("measured -- the predicate that produced that bucket is IsDecisive()==0, which on this path ")
        TEXT("means no vanilla CORE resolved to a live actor, and NOTHING here tested why; %d ran the ")
        TEXT("suppression and it took no member at all. Of the %d hidden on this pass, %d were hidden ")
        TEXT("while at least one member that HAS a mesh piece in the index held no captured look -- the ")
        TEXT("apply-phase capture retries every pass, unlike the roll's one shot, so this is reported ")
        TEXT("rather than refused. T15 BLAST RADIUS, counted rather than fixed: those entries carry %d ")
        TEXT("satellite record(s) in total and %d of them are UNCAPTURED, which means they are hidden at ")
        TEXT("the origin and will never be spawned at the destination. Disable-restore armed this pass: ")
        TEXT("%d entr(ies)."),
        // Twelve conversions above; twelve arguments below, hand-counted in order (arity.py is known
        // broken and its PASS is not evidence). The candidate count appears TWICE in the text -- once
        // as the population and once as the denominator of the split -- so it is passed twice.
        WellAuditPasses,
        WellImmediateCandidatesThisPass,
        WellImmediateCandidatesThisPass,
        WellImmediateHiddenThisPass,
        WellImmediateGateRefusedThisPass,
        WellImmediateGateUnmeasuredThisPass,
        WellImmediateTookNothingThisPass,
        WellImmediateHiddenThisPass,
        WellImmediateCaptureIncompleteThisPass,
        WellImmediateSatelliteRecordsHiddenThisPass,
        WellImmediateUncapturedHiddenThisPass,
        WellImmediateDisableRestoreArmedThisPass);
}

// ------------------------------------------------------------------------------------------------
// THE OPPOSITE-POLARITY PAIR FOR T54
// ------------------------------------------------------------------------------------------------
// THE T23 PAIR (T23-A / T23-B) IS GONE -- do not go looking for it. It asked whether a suppression was
// taken on the ROLL phase, a question about the CommitWellsAtRoll toggle; T68 (2026-08-11) deleted that
// toggle feature-and-all and RETIRED the pair with it, along with tools/check_t23_writers.ps1. That is
// the one legitimate way a red/green pair dies: its question stopped existing. This banner claimed until
// 2026-08-13 that the pair had been left exactly as it was, and described which half of it stayed red
// with the toggle off -- stale since T68, and it pointed a future author at an instrument that is not
// there. The old wording is NOT quoted here: check_t71_lint.ps1's H9 greps for it, and a tombstone that
// exemplifies the string its own pin hunts is the legend-collision defect this repo has hit twice.
// (T71 cold review F2, second half -- AUTHORED, no reviewer text was supplied for this clause.)
//
// THIS pair is the T54 complement, over the population T54 is about:
//   T54-A  "no well the roll marked as moving is still standing at its vanilla origin while nothing
//           blocked hiding it."          RED before this packet lands. GREEN after.
//   T54-B  "at least one such well is still standing."   GREEN before. RED after, at the same instant.
// They are exact complements over ONE number, so both-green and both-red are unreachable without
// editing this function.
//
// A ZERO DENOMINATOR IS NOT A PASS. When no entry is eligible-and-unblocked the question was never
// asked and both print VACUOUS -- this repo has shipped a vacuous green twice. Neither assertion is
// ever skipped.
//
// THE BLOCKED POPULATION IS SUBTRACTED, NOT IGNORED, AND ITS SIZE IS PRINTED. An entry the occupancy
// gate refused, or whose core did not resolve this pass, is legitimately not hidden; folding it into
// the failing side would make T54-A permanently red for a reason T54 never promised to fix. Folding it
// into the passing side without printing it is how a gate that refuses everything reads as a pass.
void ANodeShuffleSubsystem::EmitWellImmediateHideTestPair(bool bRelocationOnThisPass)
{
    int32 Candidates = 0, Suppressed = 0, Blocked = 0, TookNothing = 0, Unexplained = 0;
    FString UnexplainedNames;

    for (const FNodeShuffleWellEntry& E : WellLayout)
    {
        // ns-t54 cold review F1: the arm gates on bOn AND E.bRelocate. A pair whose population is wider
        // than the arm's counts entries the arm was never offered, and reports them as holes in the arm.
        if (!bRelocationOnThisPass) { continue; }
        if (E.bGroupPlaced) { continue; }
        // The SAME predicate the immediate-hide arm gates on. If these two ever disagree, this pair
        // measures a different population than the code it is guarding, which is the failure mode the
        // pair exists to prevent -- so it is stated here in full rather than derived from a counter.
        if (!E.bRelocate || !E.bOffsetsCaptured || !E.bDestDealt || E.bRelocationFailed) { continue; }
        ++Candidates;

        if (WellGroupHasSuppressedMember(E)) { ++Suppressed; continue; }

        if (const uint8* Reason = WellImmediateHideReasonThisPass.Find(E.CorePath))
        {
            // ns-t54 cold review F2. Codes 1 and 2 are DECISIONS not to hide -- a member in use, or no
            // vanilla core resolved -- and are legitimately outside the denominator. Code 3 is NOT a
            // decision: the arm ran the suppression and it took nothing, which on a decisive verdict
            // means a live vanilla member is still standing. Subtracting it would let T54-A pass with
            // the exact well the player is looking at.
            if (*Reason != 3) { ++Blocked; continue; }
            ++TookNothing;
            // AUTHORED, NOT IN F2'S VERBATIM, AND REQUIRED BY THE SURROUNDING CODE. Without this the
            // entry falls through into the Unexplained block below and is counted TWICE -- once here and
            // once as a hole the arm is "supposed to make impossible", which would be a false structural
            // claim about the very entry the arm demonstrably did record a reason for. Reason 3 stays IN
            // the denominator (that is F2's whole point) and is simply not a hole.
            continue;
        }
        // No suppression AND the arm recorded nothing for this entry on this pass. Structurally this
        // should be unreachable: the arm sits above every `continue` in its branch, so an eligible
        // unplaced entry cannot bypass it. A non-zero here means that structural claim is false.
        ++Unexplained;
        if (UnexplainedNames.Len() < 400)
        {
            UnexplainedNames += (UnexplainedNames.IsEmpty() ? TEXT("") : TEXT(", "));
            UnexplainedNames += WellShort(E.CorePath);
        }
    }

    const int32 Eligible = Candidates - Blocked;          // the pair's denominator
    const int32 StillStanding = Eligible - Suppressed;    // the one number both verdicts read

    const TCHAR* VerdictA = (Eligible <= 0) ? TEXT("VACUOUS")
                          : (StillStanding == 0 ? TEXT("PASS") : TEXT("FAIL"));
    const TCHAR* VerdictB = (Eligible <= 0) ? TEXT("VACUOUS")
                          : (StillStanding > 0 ? TEXT("PASS") : TEXT("FAIL"));

    const FString Key = FString::Printf(TEXT("%d|%d|%d|%d|%d"), Candidates, Suppressed, Blocked,
                                        TookNothing, Unexplained);
    if (Key == WellImmediateTestLastKey) { return; }
    WellImmediateTestLastKey = Key;

    UE_LOG(LogNodeShuffle, Display,
        TEXT("[NodeShuffle][TEST] T54-A %s | T54-B %s -- population: %d unplaced well entr(ies) that the ")
        TEXT("roll marked as moving (relocating, offsets captured, a destination dealt, not terminally ")
        TEXT("failed; well relocation is hard-wired ON since T71, so this pair is measured on every ")
        TEXT("pass and a VACUOUS verdict now means the SAVE held no eligible entry, never a setting). ")
        TEXT("%d of them carry a suppression record on at least one member. %d were BLOCKED on ")
        TEXT("this pass for a stated, counted cause on the WELLH2-IMMEDIATE line (a member in use, or no ")
        TEXT("vanilla core resolved) and are excluded from the denominator, which leaves %d eligible and ")
        // The phrase "take no member at all" is kept CONTIGUOUS ON ONE SOURCE LINE deliberately:
        // tools/check_t54_lint.ps1 anchors on it, and Select-String is line-based, so splitting it across
        // a TEXT() continuation would silently make that half of the lint unable to see its own subject.
        TEXT("%d of those still standing at their origin, of which %d had the ")
        TEXT("suppression run and take no member at all. T54-A asserts that still-standing number is ")
        TEXT("zero, T54-B ")
        TEXT("asserts it is above zero: exact complements over one number, so neither can be quietly ")
        TEXT("skipped and exactly one is red at any time. VACUOUS means the denominator was zero and the ")
        TEXT("question was never asked -- it is not a pass. %d entr(ies) are neither suppressed nor ")
        TEXT("blocked-with-a-reason, which the immediate-hide arm's placement above every branch exit is ")
        TEXT("supposed to make impossible (%s). Before immediate hide landed T54-A was the DELIBERATELY ")
        TEXT("RED one; after it lands they swap."),
        VerdictA, VerdictB, Candidates, Suppressed, Blocked, Eligible, StillStanding, TookNothing,
        Unexplained, UnexplainedNames.IsEmpty() ? TEXT("none named") : *UnexplainedNames);
}
