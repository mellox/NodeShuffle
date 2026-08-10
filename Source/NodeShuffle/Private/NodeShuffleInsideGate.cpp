// ns-t54-immediate-hide, SCOPE ADDITION 2 (author, 2026-08-10): THE CENTRE-TOTALLY-INSIDE SHADOW GATE.
//
// THE AUTHOR'S QUESTION, WHICH NO EXISTING GATE COULD ANSWER: why did nothing catch a node placed
// almost entirely inside rock? Because nothing tests containment. The shipped enclosure predicate
// (ANodeShuffleSubsystem::IsSpotEnclosed) casts 8 HORIZONTAL rays at ONE height and refuses at 7 of 8
// (docs/TECH-DEBT.md T37); it has no vertical sampling at all (T41). It ran 153 times this session and
// refused 0. "Is this centre inside solid geometry" has never existed as a gate in this mod.
//
// SHADOW-FIRST, AND THE REASON IS A STANDING RULE, NOT CAUTION FOR ITS OWN SAKE: never gate on an
// instrument before it has been graded. T53's instrument is the first trustworthy measurer of this
// question, and as of 2026-08-10 its grading is one-sided -- NEGATIVE on everything the author wants
// kept (caves, satellite 81, open ground) and with ZERO graded TRUE POSITIVES. An instrument with no
// graded positive cannot be allowed to refuse a placement: the first thing it refused would be
// unfalsifiable. So the evaluation and the logging are unconditional and the REFUSAL is behind a CVar,
// default OFF.
//
// A DEFAULT-OFF CVar IS NOT ISOLATION, AND THIS FILE DOES NOT PRETEND OTHERWISE (workspace rule, four
// sightings). The evaluation below runs on EVERY placed entry whether the CVar is on or off. It is live
// code in the apply path and must be cold-reviewed as live code. What the CVar gates is exactly one
// thing: whether a positive verdict is allowed to change a decision.
//
// ONE INSTRUMENT, ONE WORDING (T26). This file calls RunTotallyInsideProbe and, when there is something
// to say, LogTotallyInsideReading. It does NOT re-word the instrument. What it adds is its own
// per-placement summary line and its own per-pass census, both of which are ABOUT the gate rather than
// about the reading.
//
// THE WOULD-REFUSE TOKEN. The whole-line prefix below is unique in this source tree and appears exactly
// once per line, and no legend anywhere exemplifies a field in the shape it prints. To count would-refuse
// events, anchor on that prefix; do not grep a field name (this repo has shipped that trap twice, once
// into a commit message's own recommended grep).

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleTotallyInside.h"

#include "HAL/IConsoleManager.h"
#include "Engine/World.h"

// The refusal switch. DEFAULT 0. Named in the family of this mod's existing CVars.
static TAutoConsoleVariable<int32> CVarNodeShuffleInsideGateRefuse(
    TEXT("NodeShuffle.InsideGateRefuse"),
    0,
    TEXT("0 (default) = the centre-totally-inside instrument is evaluated at every final accepted ")
    TEXT("placement spot and every result is LOGGED, but no placement is refused because of it. ")
    TEXT("1 = a positive verdict also REFUSES that spot.\n")
    TEXT("WHAT IS MEASURED: whether the placement's own final centre is enclosed by solid geometry in ")
    TEXT("EVERY probed direction. A direction counts as solid if any one of four independent queries ")
    TEXT("reports solid along or at the end of it; the verdict is positive only when no direction ")
    TEXT("escapes.\n")
    TEXT("WHAT IS NOT MEASURED: whether a centre merely touches or sits on an edge. That question is ")
    TEXT("deferred by the author's own ruling and this switch does not decide it.\n")
    TEXT("OFF STILL LOGS. Turning this off does not turn the measurement off -- it only stops the ")
    TEXT("measurement from changing a decision. Grade the log first: as of 2026-08-10 this instrument ")
    TEXT("has no graded true positive, so turning this on can refuse a spot on a reading nobody has ")
    TEXT("confirmed."),
    ECVF_Default);

bool ANodeShuffleSubsystem::IsInsideGateRefusalLive() const
{
    return CVarNodeShuffleInsideGateRefuse.GetValueOnAnyThread() != 0;
}

// ------------------------------------------------------------------------------------------------
// ONE EVALUATION, AT ONE POINT
// ------------------------------------------------------------------------------------------------
// CALLED ONLY AT A PLACEMENT'S FINAL ACCEPTED SPOT -- post-settle, post-nudge, pre-commit -- and never
// inside a settle spiral or per candidate probe.
//
// COST, STATED AS A RANGE PER PLACEMENT (ns-t54 cold review F4/F9, then the scoped re-review's C; the
// first wording said "on the order of sixty queries" without saying sixty of WHAT and cited 13
// directions when NodeShuffleTotallyInside.cpp declares FOURTEEN, and the second presented a FLOOR as
// the expectation).
//
// ONE CALL IS >=57 QUERIES AND UP TO ~351. The floor is 14 directions x [outward line + inward line +
// inward sweep + outer overlap] + 1 centre overlap = 57, and it is reached ONLY when no query has to
// retrace. TIRetraceCap is 8 (same file): each of the 3 traces per direction may re-run up to 8 times to
// get past an excluded actor, and the exclusions are the subject, APawn and AFGBuildable -- i.e. the
// designed case NEAR A PLAYER, which is where placements happen. Do not plan against 57.
//
// AN ORDINARY-NODE placement is one call. A WELL placement is one call for the core plus one per
// captured member, so >=57 x (1 + N).
//
// AND IT IS NOT ONCE PER GROUP. TryPlaceWellGroup re-reaches this commit EVERY PASS for a group whose
// spawn keeps coming back INCOMPLETE -- that path deliberately does not advance the yaw cursor, so the
// same footprint is re-validated and re-gated at ~5 s intervals. An 8-satellite stuck well is therefore
// AT LEAST ~510 queries per pass and can approach ~3160 with retraces, WITH THE CVar AT ITS DEFAULT,
// because the evaluation is unconditional and only the refusal is gated.
//
// EVERY ONE OF THOSE FIGURES IS A DERIVATION, NOT A MEASUREMENT. The census's trace/overlap fields count
// every attempt including retraces, so they are the numbers to budget against; read them before a
// grading session. None of this is a reason to move the call: running it inside the settle or
// satellite-draw spirals would multiply the same range by every candidate.
//
// RETURNS "would this gate refuse", NOT "does it refuse". The caller decides what to do with it and the
// caller is the only place the CVar changes behaviour. Nothing in this function reads the CVar for any
// purpose other than LABELLING the log, so an evaluation is identical in both modes.
bool ANodeShuffleSubsystem::EvaluateInsideShadowGate(const FVector& At, const class AActor* Subject,
                                                     const FString& Who, bool bAdoptedNotPlaced,
                                                     bool bCaveFlagged)
{
    ++InsideGateEvaluatedThisPass;
    if (bAdoptedNotPlaced) { ++InsideGateAdoptedThisPass; }
    if (bCaveFlagged)      { ++InsideGateCaveFlaggedThisPass; }

    FNodeShuffleTotallyInsideReading R;
    RunTotallyInsideProbe(GetWorld(), At, Subject, R);
    InsideGateQueriesThisPass += R.QueriesMade;
    InsideGateOverlapsThisPass += R.OverlapsMade;

    if (!R.bRan)
    {
        ++InsideGateNotMeasuredThisPass;
        return false;
    }
    if (!R.bTotallyInside)
    {
        ++InsideGateNegativeThisPass;
        return false;
    }

    ++InsideGateWouldRefuseThisPass;

    // THE CAVE CARVE-OUT IS A COUNT, NOT AN EXCLUSION, AND THAT IS STATED RATHER THAN QUIETLY CHOSEN.
    // The author's cave ruling outranks this gate, and a cave-floor placement is one the author asked to
    // KEEP. What this packet can honestly do is carry the entry's own persisted bUnderground flag through
    // and report the split. What it CANNOT honestly claim is that bUnderground identifies every placement
    // the author's cave ruling protects: that flag is set when the deal picked a cave CELL, and a
    // placement that ended up under a natural overhang without going through that path does not carry it.
    // So a caller that hands bCaveFlagged=true is REFUSED-EXEMPT below, and every other positive is
    // reported as-is with no claim that it is not a cave. IF THE CENSUS SHOWS POSITIVES THE AUTHOR
    // RECOGNISES AS CAVES, THE EXCLUSION IS WRONG AND MUST BE WIDENED BEFORE THE CVar IS EVER TURNED ON.
    const bool bRefusalLive = IsInsideGateRefusalLive();
    const bool bWouldActuallyRefuse = bRefusalLive && !bCaveFlagged && !bAdoptedNotPlaced;
    if (bCaveFlagged)        { ++InsideGatePositiveOnCaveThisPass; }
    if (bAdoptedNotPlaced)   { ++InsideGatePositiveOnAdoptedThisPass; }
    if (bWouldActuallyRefuse) { ++InsideGateRefusedThisPass; }

    // The full reading, through the ONE emitter. Printed only for a POSITIVE, which is what keeps this
    // affordable per placement: a negative contributes its counts to the census and nothing else. The
    // wording is the instrument's, not this file's.
    LogTotallyInsideReading(
        TEXT("INSIDEGATE"),
        FString::Printf(TEXT("the final accepted placement spot of %s"), *Who),
        R,
        Subject ? Subject->GetName() : FString(TEXT("<none: no live actor was handed as the subject>")),
        /*bGateRefuses=*/false, /*Blocked=*/0, /*Total=*/0, /*Threshold=*/-1);

    UE_LOG(LogNodeShuffle, Display,
        TEXT("INSIDEGATE-WOULDREFUSE %s at %s: the containment instrument reports every one of %d probed ")
        TEXT("direction(s) meeting solid geometry and %d escaping, with the furthest of those first ")
        TEXT("contacts at %.0f cm, so this centre met solid in every probed direction. Subject actor excluded from ")
        TEXT("the reading: %s. This placement %s: refusal is %s, this spot is cave-flagged %d and ")
        TEXT("adopted-rather-than-placed %d. MEASURED: the queries above. NOT MEASURED and NOT CLAIMED: ")
        TEXT("whether a player could reach or build here, and whether the geometry met is terrain, a ")
        TEXT("rock mesh or something else -- the reading above names each contact's actor and that is ")
        TEXT("the whole of what is known. The shipped enclosure predicate is a separate, horizontal-only ")
        TEXT("test and its verdict for this spot is NOT reported on this line, so a zero here is not its ")
        TEXT("zero: the comparison fields on the reading above are passed as neutral placeholders by ")
        TEXT("this caller, which does not run that predicate."),
        *Who, *At.ToCompactString(), R.BlockedDirections, R.ClearDirections,
        R.FurthestNearestSolidCm,
        Subject ? *Subject->GetName() : TEXT("<none>"),
        bWouldActuallyRefuse ? TEXT("IS BEING REFUSED") : TEXT("PROCEEDS"),
        bRefusalLive ? TEXT("LIVE") : TEXT("OFF (shadow)"),
        bCaveFlagged ? 1 : 0, bAdoptedNotPlaced ? 1 : 0);

    return bWouldActuallyRefuse;
}

// ------------------------------------------------------------------------------------------------
// THE PER-PASS CENSUS
// ------------------------------------------------------------------------------------------------
// EVERY COUNT CARRIES ITS DENOMINATOR. A "0 would-refuse" is evidence of nothing unless the line says
// how many placements were evaluated and how many of those the instrument could even measure -- this
// repo has shipped that exact zero twice in one day.
void ANodeShuffleSubsystem::EmitInsideShadowGateCensus()
{
    if (InsideGateEvaluatedThisPass == 0) { return; }

    // ns-t54 cold review F5: the cave-flagged denominator joins the key. A key that cannot see a field
    // cannot re-print when that field changes, which would freeze the one number step 7's decision rests on.
    const FString Key = FString::Printf(TEXT("%d|%d|%d|%d|%d|%d|%d|%d|%d|%d"),
                                        InsideGateEvaluatedThisPass, InsideGateNotMeasuredThisPass,
                                        InsideGateNegativeThisPass, InsideGateWouldRefuseThisPass,
                                        InsideGateRefusedThisPass, InsideGatePositiveOnCaveThisPass,
                                        InsideGatePositiveOnAdoptedThisPass, InsideGateAdoptedThisPass,
                                        InsideGateCaveFlaggedThisPass,
                                        IsInsideGateRefusalLive() ? 1 : 0);
    if (Key != InsideGateCensusLastKey)
    {
        InsideGateCensusLastKey = Key;
        UE_LOG(LogNodeShuffle, Display,
            // ns-t54 cold review F3 -- NO VERDICT TOKEN IS REPRODUCED ON THIS LINE, IN ANY CASE. The
            // emitter's four tokens are TOTALLY INSIDE / AN ESCAPE WAS FOUND / RAY SOLID / RAY CLEAR
            // (NodeShuffleTotallyInsideLog.cpp:75,181). The reviewer's own replacement kept the words
            // "totally inside" in lower case, which still collides under a case-insensitive grep -- the
            // exact trap this repo has shipped twice -- so the outcome is described by its PREDICATE
            // instead: bTotallyInside is BlockedDirections == DirectionCount with DirectionCount > 0,
            // i.e. every probed direction met solid. Counting positives means grepping the WOULDREFUSE
            // prefix, never this census.
            TEXT("INSIDEGATE census: %d placement spot(s) evaluated this pass. Of those, %d could not be ")
            TEXT("measured (no query ran), %d had at least one direction escape, and %d met solid in ")
            TEXT("every probed direction. Of ")
            TEXT("those %d positive(s): %d carried the entry's own cave flag and %d were adopted rather ")
            TEXT("than placed -- both are exempt from refusal -- leaving %d that were actually refused ")
            TEXT("this pass. Refusal is %s. %d entr(ies) evaluated this pass were adopted rather than ")
            TEXT("placed in total. Cost, measured rather than estimated: %d trace/sweep quer(ies) and %d ")
            TEXT("overlap(s) issued across all evaluations on this pass. %d of the entr(ies) evaluated ")
            TEXT("this pass carried the cave flag at all, positive or not; the well path never sets it, ")
            TEXT("so this counts ordinary-node entries only."),
            InsideGateEvaluatedThisPass, InsideGateNotMeasuredThisPass, InsideGateNegativeThisPass,
            InsideGateWouldRefuseThisPass, InsideGateWouldRefuseThisPass,
            InsideGatePositiveOnCaveThisPass, InsideGatePositiveOnAdoptedThisPass,
            InsideGateRefusedThisPass,
            IsInsideGateRefusalLive() ? TEXT("LIVE -- positives change placements") : TEXT("OFF -- shadow only"),
            InsideGateAdoptedThisPass, InsideGateQueriesThisPass, InsideGateOverlapsThisPass,
            InsideGateCaveFlaggedThisPass);
    }

    InsideGateEvaluatedThisPass = 0;
    InsideGateNotMeasuredThisPass = 0;
    InsideGateNegativeThisPass = 0;
    InsideGateWouldRefuseThisPass = 0;
    InsideGateRefusedThisPass = 0;
    InsideGatePositiveOnCaveThisPass = 0;
    InsideGatePositiveOnAdoptedThisPass = 0;
    InsideGateAdoptedThisPass = 0;
    InsideGateCaveFlaggedThisPass = 0;
    InsideGateQueriesThisPass = 0;
    InsideGateOverlapsThisPass = 0;
}
