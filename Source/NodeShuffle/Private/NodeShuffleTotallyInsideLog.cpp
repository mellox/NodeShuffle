// ns-t53-totallyinside (2026-08-10): the ONE emitter for this instrument, declared in
// NodeShuffleTotallyInside.h and shared by all three probe commands (NodeShuffle.Here,
// NodeShuffle.PointAtHere, NodeShuffle.WellProbe). One definition, three callers, for the reason
// docs/TECH-DEBT.md T26 exists. It prints; it decides nothing, traces nothing and gates nothing.
//
// ON THE WORDING OF THE VERDICT TOKENS. The token this instrument prints for a positive point appears in
// exactly ONE place -- the value slot of a point's own verdict line -- and is spelled in no legend,
// heading or explanation anywhere below, so counting positives is a grep that cannot match its own
// legend. The negative token is deliberately NOT the positive token with a word in front of it, for the
// same reason: a negative that contains the positive as a substring makes every count of positives wrong
// by the number of negatives. The per-ray tokens are a separate pair chosen the same way. Five prior
// sightings of a legend colliding with its own field are recorded in this project's CLAUDE.md; this file
// is written not to be the sixth.

#include "NodeShuffleTotallyInside.h"

#include "NodeShuffle.h"

ETotallyInsideAgreement LogTotallyInsideReading(const TCHAR* Prefix, const FString& Tag,
                                                const FNodeShuffleTotallyInsideReading& R,
                                                const FString& SubjectName,
                                                bool bGateRefuses, int32 Blocked, int32 Total,
                                                int32 Threshold)
{
    // ---- THE COMPARISON, DECIDED ONCE AND PRINTED ON THE LAST LINE ----
    ETotallyInsideAgreement Agreement;
    const TCHAR* Direction;
    if (!R.bRan)
    {
        Agreement = ETotallyInsideAgreement::NotComparable;
        Direction = TEXT("NOT COMPARABLE -- no query was made for this point");
    }
    else if (bGateRefuses == R.bTotallyInside)
    {
        Agreement = bGateRefuses ? ETotallyInsideAgreement::AgreeRefuse
                                 : ETotallyInsideAgreement::AgreePass;
        Direction = bGateRefuses
            ? TEXT("AGREE -- the shipped gate refuses this point and this instrument is positive here")
            : TEXT("AGREE -- the shipped gate does not refuse this point and this instrument is not ")
              TEXT("positive here");
    }
    else if (R.bTotallyInside)
    {
        Agreement = ETotallyInsideAgreement::CandidateAdds;
        Direction = TEXT("DISAGREE -- this instrument is positive at a point the shipped gate accepts");
    }
    else
    {
        Agreement = ETotallyInsideAgreement::CandidateDrops;
        Direction = TEXT("DISAGREE -- this instrument is not positive at a point the shipped gate ")
                    TEXT("refuses");
    }

    if (!R.bRan)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("%s: %s -- TOTALLYINSIDE verdict at %s: UNMEASURED. No line query, no sweep and no ")
            TEXT("overlap was made for this point at all, so no count on any line is a reading of it ")
            TEXT("and neither answer is reported. What the probe recorded instead: %s. This line ")
            TEXT("reports that nothing ran; it states no cause."),
            Prefix, *Tag, *R.Point.ToCompactString(), *R.CentreOverlapWhat);

        UE_LOG(LogNodeShuffle, Display,
            TEXT("%s: %s -- TOTALLYINSIDE against the shipped enclosure gate: %s. The shipped gate ")
            TEXT("blocked %d of %d ray(s) against a threshold of %d and so %s this point. NOTHING IN ")
            TEXT("THIS BUILD ACTS ON THIS INSTRUMENT -- no placement path, no gate and no refusal reads ")
            TEXT("it. This line reports what each side returned and states no cause."),
            Prefix, *Tag, Direction, Blocked, Total, Threshold,
            bGateRefuses ? TEXT("refuses") : TEXT("does not refuse"));

        return Agreement;
    }

    // ---- THE VERDICT ----
    const TCHAR* Verdict = R.bTotallyInside ? TEXT("TOTALLY INSIDE") : TEXT("AN ESCAPE WAS FOUND");
    const TCHAR* CentreOverlapWords =
        !R.bCentreOverlapRan
            ? TEXT("was not run")
            : (R.bCentreOverlapSolid
                   ? TEXT("found solid there, which supports this verdict")
                   : TEXT("found none there, which does not contradict it"));

    UE_LOG(LogNodeShuffle, Display,
        TEXT("%s: %s -- TOTALLYINSIDE verdict at %s: %s. Every one of the %d direction(s) probed has to ")
        TEXT("report solid before the positive verdict is given; %d of them did and %d did not, and one ")
        TEXT("direction reporting clear is enough on its own to withhold it. Each direction was probed ")
        TEXT("%.0f cm from THE TESTED POINT ITSELF and never from an offset above it. Of the directions ")
        TEXT("that reported solid, the furthest any of them had to go to meet it was %.0f cm, so a ")
        TEXT("reach shorter than that figure would have left at least one direction reading clear; a ")
        TEXT("figure of -1 there means no direction reported solid at all. THE OVERLAP AT THE TESTED ")
        TEXT("POINT, which is corroboration and never a veto: it %s. An overlap cannot report ")
        TEXT("containment inside a landscape heightfield or a complex-as-simple triangle mesh (the T39 ")
        TEXT("cold review), so a negative from it is not evidence of open air, and NEITHER of its ")
        TEXT("outcomes enters the verdict on this line. WHAT THIS SHAPE OF TEST CANNOT ESTABLISH: ")
        TEXT("geometry that does not block this channel is invisible to all four of its instruments, a ")
        TEXT("direction whose solid lies beyond the reach is counted as clear, and a query that spent ")
        TEXT("its whole re-run budget reports nothing for its own ray. It reports what these queries ")
        TEXT("returned and states no cause."),
        Prefix, *Tag, *R.Point.ToCompactString(), Verdict,
        R.DirectionCount, R.BlockedDirections, R.ClearDirections,
        R.ProbeReachCm, R.FurthestNearestSolidCm,
        CentreOverlapWords);

    // ---- WHAT THE PER-DIRECTION LINES BELOW SAY ----
    UE_LOG(LogNodeShuffle, Display,
        TEXT("%s: %s -- TOTALLYINSIDE ray legend. One line follows per probed direction, in the order ")
        TEXT("they were probed: the four horizontal axes, then straight up, then straight down, then ")
        TEXT("the eight corner diagonals -- fourteen in all, and the vertical pair is what every earlier ")
        TEXT("attempt in this project lacked. FOUR INSTRUMENTS ARE RUN ON EACH RAY and any ONE of them ")
        TEXT("reporting solid is enough for that ray; a ray is called clear only when all four say ")
        TEXT("nothing. (1) A line query from the tested point outward to the far end of the ray -- the ")
        TEXT("reading a query beginning inside a body has never once been observed to give in this ")
        TEXT("project, run and reported anyway because which instrument fires is the thing being ")
        TEXT("learned. (2) A line query from that far end back to the tested point, so a surface ")
        TEXT("between the two is met from the side this engine has been observed to report. (3) A ")
        TEXT("sphere sweep of radius %.0f cm along that same inward segment, which also reports whether ")
        TEXT("it was already overlapping at its own start -- that is a statement about the far end of ")
        TEXT("the ray being inside something, which counts toward containment rather than against it. A ")
        TEXT("sweep of any radius can report solid where a line query is clear, which is why it is one ")
        TEXT("of four and never the only one. (4) A sphere overlap of radius %.0f cm at that far end, ")
        TEXT("whose positive is a real statement and whose negative is not. All four run on the same ")
        TEXT("channel and with the same trace complexity the shipped enclosure predicate uses, so a ")
        TEXT("disagreement between the two instruments cannot be an artefact of that setting. Each line ")
        TEXT("names the actor and the component every instrument that reported solid returned. These ")
        TEXT("lines report what the queries returned and state no cause."),
        Prefix, *Tag, R.SweepRadiusCm, R.OverlapRadiusCm);

    // ---- EVERY RAY ----
    for (const FNodeShuffleTotallyInsideRay& Ray : R.Rays)
    {
        const FString OutPhrase = Ray.bOutwardBlocked
            ? FString::Printf(TEXT("the outward line query met solid %.0f cm out, at %s."),
                              Ray.OutwardSolidAtCm, *Ray.OutwardWhat)
            : FString::Printf(TEXT("the outward line query kept no blocking result: %s."),
                              *Ray.OutwardWhat);

        const FString InPhrase = Ray.bInwardBlocked
            ? FString::Printf(TEXT("The inward line query met solid %.0f cm from the tested point, at ")
                              TEXT("%s."),
                              Ray.InwardSolidAtCm, *Ray.InwardWhat)
            : FString::Printf(TEXT("The inward line query kept no blocking result: %s."),
                              *Ray.InwardWhat);

        const FString SweepWhere = Ray.bSweepStartPenetrating
            ? FString(TEXT("reporting it was ALREADY OVERLAPPING at its own start, so this is a ")
                      TEXT("statement about the far end of the ray and no distance closer than the ")
                      TEXT("reach is claimed"))
            : FString::Printf(TEXT("%.0f cm from the tested point"), Ray.SweepSolidAtCm);

        const FString SweepPhrase = Ray.bSweepBlocked
            ? FString::Printf(TEXT("The inward sweep met solid, %s, at %s."),
                              *SweepWhere, *Ray.SweepWhat)
            : FString::Printf(TEXT("The inward sweep kept no blocking result: %s."), *Ray.SweepWhat);

        const FString OverlapPhrase = FString::Printf(
            TEXT("The overlap at the far end returned %d result(s), of which %d reported blocking once ")
            TEXT("the three exclusions had been applied: %s"),
            Ray.OuterOverlapResults, Ray.OuterOverlapBlocking, *Ray.OuterOverlapWhat);

        UE_LOG(LogNodeShuffle, Display,
            TEXT("%s: %s -- TOTALLYINSIDE direction %d of %d, %s, unit vector %s: %s. The nearest solid ")
            TEXT("any instrument put on this ray was %.0f cm from the tested point, and a figure of -1 ")
            TEXT("means none of them put any there. What reported it: %s. THE FOUR INSTRUMENTS: %s %s ")
            TEXT("%s %s"),
            Prefix, *Tag, Ray.Index, R.DirectionCount, *Ray.Label, *Ray.Unit.ToCompactString(),
            Ray.bBlocked ? TEXT("RAY SOLID") : TEXT("RAY CLEAR"),
            Ray.NearestSolidAtCm, *Ray.BlockedBy,
            *OutPhrase, *InPhrase, *SweepPhrase, *OverlapPhrase);
    }

    // ---- WHICH INSTRUMENT DID THE WORK, AND WHAT THE EXCLUSIONS TOOK ----
    UE_LOG(LogNodeShuffle, Display,
        TEXT("%s: %s -- TOTALLYINSIDE instruments and exclusions. Of the %d direction(s) that reported ")
        TEXT("solid, the outward line query would have carried %d on its own, the inward line query %d, ")
        TEXT("the inward sweep %d, of which %d were that sweep reporting it was already overlapping at ")
        TEXT("its own start, and the overlap at the far end %d; one direction can be carried by more ")
        TEXT("than one instrument, so those figures need not sum to %d. QUERIES: %d line and sweep ")
        TEXT("queries were issued including every re-run, %d of them returned a blocking result, and of ")
        TEXT("those %d were dropped for being the actor this caller named as the subject, which was %s, ")
        TEXT("%d for being an APawn (the reason recorded in docs/TECH-DEBT.md T36) and %d for being an ")
        TEXT("AFGBuildable (the reason recorded in T40). Each dropped actor was then handed to that ")
        TEXT("query's OWN ignore list and the query re-run from the same two endpoints, so it cost one ")
        TEXT("re-run however tall it is and no geometry was stepped over to get past it. %d result(s) ")
        TEXT("came back on an actor already on that list and %d query(ies) spent the whole %d-re-run ")
        TEXT("budget; for either of those this instrument reports nothing for that one ray, which is ")
        TEXT("why there are four instruments and not one. OVERLAPS: %d were issued, they returned %d ")
        TEXT("result(s) in total, and of the blocking ones %d were dropped as the subject, %d as an ")
        TEXT("APawn and %d as an AFGBuildable. This line counts what the queries returned and states no ")
        TEXT("cause."),
        Prefix, *Tag,
        R.BlockedDirections,
        R.CarriedByOutward, R.CarriedByInward, R.CarriedBySweep, R.CarriedBySweepStart,
        R.CarriedByOuterOverlap, R.BlockedDirections,
        R.QueriesMade, R.HitsSeen,
        R.ExcludedSubjectHits, *SubjectName, R.ExcludedPawnHits, R.ExcludedBuildableHits,
        R.IgnoredRehits, R.RetraceCapHits, R.RetraceCap,
        R.OverlapsMade, R.OverlapResultsSeen,
        R.ExcludedOverlapSubject, R.ExcludedOverlapPawn, R.ExcludedOverlapBuildable);

    // ---- THE TWO VERDICTS SIDE BY SIDE ----
    UE_LOG(LogNodeShuffle, Display,
        TEXT("%s: %s -- TOTALLYINSIDE against the shipped enclosure gate: %s. The shipped gate blocked ")
        TEXT("%d of %d ray(s) against a threshold of %d and so %s this point; this instrument %s it. ")
        TEXT("NOTHING IN THIS BUILD ACTS ON THIS INSTRUMENT -- no placement path, no gate and no ")
        TEXT("refusal reads it, and the centre-containment walk it is printed beside is unchanged by ")
        TEXT("its existence. This line reports two verdicts and the counts behind them; it does not say ")
        TEXT("why they differ."),
        Prefix, *Tag, Direction, Blocked, Total, Threshold,
        bGateRefuses ? TEXT("refuses") : TEXT("does not refuse"),
        R.bTotallyInside ? TEXT("gives its positive verdict for") : TEXT("does not give it for"));

    return Agreement;
}
