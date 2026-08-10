// ns-t42-centreshadow: the one emitter declared in NodeShuffleCentreShadow.h. See that header for why it
// is one definition rather than three. It prints; it decides nothing and traces nothing.

#include "NodeShuffleCentreShadow.h"

#include "NodeShuffle.h"
#include "NodeShuffleSubsystem.h"   // FNodeShufflePointInsideReading

ECentreShadowAgreement LogCentreShadowReading(const TCHAR* Prefix, const FString& Tag,
                                              const FNodeShufflePointInsideReading& Centre,
                                              bool bCentreInside, const FString& SubjectName,
                                              bool bGateRefuses, int32 Blocked, int32 Total,
                                              int32 Threshold)
{
    UE_LOG(LogNodeShuffle, Display,
        TEXT("%s: %s -- SHADOW centre-containment candidate at %s: %s. Two walks were made along the ")
        TEXT("segment between that point and a point %.0f cm above it, stepping %.0f cm past each ")
        TEXT("blocking hit, on the same channel and with the same trace complexity the shipped enclosure ")
        TEXT("predicate uses, up to %d hit(s) per walk. They counted %d surface entries and %d surface ")
        TEXT("exits, a net of %d, over %d counted crossing(s): %s WHAT THIS SHAPE OF TEST CANNOT ")
        TEXT("ESTABLISH FROM CONSTRUCTION, which is why the control on the next line exists: geometry ")
        TEXT("that does not block that channel is invisible to it, a shell thinner than the step can be ")
        TEXT("walked over, a truncated walk is reported rather than corrected, and it assumes the point ")
        TEXT("it starts from high above is in open air. It states no cause."),
        Prefix, *Tag, *Centre.Point.ToCompactString(),
        !Centre.bRan ? TEXT("UNMEASURED -- the walks did not run, so neither answer is reported")
                     : (bCentreInside ? TEXT("CENTRE INSIDE") : TEXT("CENTRE NOT INSIDE")),
        Centre.SkyOffsetCm, Centre.StepEpsilonCm, Centre.MaxHitsPerSegment,
        Centre.Entries, Centre.Exits, Centre.Net, Centre.CountedCrossings,
        *Centre.CrossingActors);

    ECentreShadowAgreement Agreement;
    const TCHAR* Direction;
    if (!Centre.bRan)
    {
        Agreement = ECentreShadowAgreement::NotComparable;
        Direction = TEXT("NOT COMPARABLE -- the shadow walks did not run for this point");
    }
    else if (bGateRefuses == bCentreInside)
    {
        Agreement = bGateRefuses ? ECentreShadowAgreement::AgreeRefuse : ECentreShadowAgreement::AgreePass;
        Direction = bGateRefuses ? TEXT("AGREE -- both refuse this point")
                                 : TEXT("AGREE -- neither refuses this point");
    }
    else if (bCentreInside)
    {
        Agreement = ECentreShadowAgreement::CandidateAdds;
        Direction = TEXT("DISAGREE -- the candidate would REFUSE a point the shipped gate accepts");
    }
    else
    {
        Agreement = ECentreShadowAgreement::CandidateDrops;
        Direction = TEXT("DISAGREE -- the candidate would ACCEPT a point the shipped gate refuses");
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("%s: %s -- shadow trust, exclusions, and shipped gate versus candidate: %s. THE POSITIVE ")
        TEXT("CONTROL was %s, and it read %s. A control that does not read inside means this method did ")
        TEXT("not demonstrate, on this run and in this geometry, that it can detect containment at all -- ")
        TEXT("so a CENTRE NOT INSIDE beside a failed control is not evidence of open air and must not be ")
        TEXT("read as one. EXCLUSIONS: of %d blocking hit(s) the two walks reported, %d were removed -- ")
        TEXT("%d on an AFGBuildable (excluded for the reason recorded in docs/TECH-DEBT.md T40: a point ")
        TEXT("carrying the player's own working machine is the opposite of the case being looked for), ")
        TEXT("%d on an APawn (excluded for the reason recorded in T36), and %d on the actor named as ")
        TEXT("this point's own subject, which ")
        TEXT("was %s -- leaving %d counted crossing(s). ALSO MEASURED ON THIS RUN: a hit reported from a ")
        TEXT("surface's far side was %s, the first counted inbound crossing led %s, and a walk %s its ")
        TEXT("%d-hit budget. THE TWO VERDICTS: the shipped gate blocked %d of %d rays against a threshold ")
        TEXT("of %d and so %s this point; the candidate %s it. NOTHING IN THIS BUILD ACTS ON THE ")
        TEXT("CANDIDATE -- no placement path, no gate and no refusal reads it. This line reports two ")
        TEXT("verdicts and the counts behind them; it does not say why they differ."),
        Prefix, *Tag, Direction,
        *Centre.ControlDetail,
        !Centre.bControlRan ? TEXT("nothing, because it was not built")
                            : (Centre.bControlInside ? TEXT("INSIDE") : TEXT("NOT INSIDE")),
        Centre.HitsSeen,
        Centre.ExcludedBuildableHits + Centre.ExcludedPawnHits + Centre.ExcludedSubjectHits,
        Centre.ExcludedBuildableHits, Centre.ExcludedPawnHits, Centre.ExcludedSubjectHits,
        *SubjectName, Centre.CountedCrossings,
        Centre.bBackFacesObserved ? TEXT("seen") : TEXT("not seen"),
        Centre.bFirstInboundCountedWasBackFace ? TEXT("OUT of solid") : TEXT("into solid"),
        Centre.bSegmentCapHit ? TEXT("used all of") : TEXT("stayed inside"),
        Centre.MaxHitsPerSegment,
        Blocked, Total, Threshold,
        bGateRefuses ? TEXT("refuses") : TEXT("does not refuse"),
        !Centre.bRan ? TEXT("was not measured for")
                     : (bCentreInside ? TEXT("would refuse") : TEXT("would not refuse")));

    // ---- ns-t49-crossingdetail (2026-08-10): PER-HIT DETAIL, ONLY WHERE IT WAS ASKED FOR ----
    // The two lines above are totals. docs/TECH-DEBT.md T48 was established, and every conclusion about
    // this walk that survived was established, by lines that printed WHAT THEY HIT -- so this block
    // prints the hits. It runs only when the caller requested the detail, which is the two commands a
    // reader points at a SINGLE point (NodeShuffle.Here and NodeShuffle.PointAtHere). NodeShuffle.
    // WellProbe does not request it and its output above is unchanged, because 8 members times 2 walks
    // times N hits is not a reading anyone can hold. Nothing here decides, traces or gates.
    if (Centre.bCrossingDetailRequested)
    {
        if (!Centre.bRan)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("%s: %s -- SHADOWCROSS: a per-hit list was requested for this point and there is ")
                TEXT("none, because the walks did not run at all. No count on the lines above is a ")
                TEXT("reading of this point."),
                Prefix, *Tag);
        }
        else
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("%s: %s -- SHADOWCROSS legend. The lines that follow report one blocking hit ")
                TEXT("each, in the order the two walks of this point made them: an inbound walk from ")
                TEXT("the sky start down to the point, then an outbound walk from the point back up to ")
                TEXT("the sky start. Each carries this line's tag, and so do this line and the closing ")
                TEXT("summary line, so a count of tagged lines for one point is the number of reported ")
                TEXT("hits plus two. At most %d hit(s) are reported per walk and the closing line ")
                TEXT("states how many were not. WHAT DECIDED EACH CLASSIFICATION -- one input only: ")
                TEXT("the sign of the dot product of the trace's impact normal with that walk's own ")
                TEXT("direction; greater than zero is recorded as a back face and anything else as a ")
                TEXT("front face. Every other trace field on those lines is reported and was not ")
                TEXT("consulted. HOW THE RUNNING TOTALS REACH THE VERDICT: the inbound walk's front ")
                TEXT("faces are the entries; the inbound walk's back faces and the outbound walk's ")
                TEXT("front faces are two readings of the exits, of which the larger is taken; the ")
                TEXT("outbound walk's back faces enter no term. The positive control's own two walks ")
                TEXT("and the downward surface-finder walk that placed it are NOT reported here. These ")
                TEXT("lines report what the traces returned and how this code labelled it, and state ")
                TEXT("no cause."),
                Prefix, *Tag, Centre.CrossingDetailCap);

            for (const FString& Line : Centre.CrossingDetail)
            {
                UE_LOG(LogNodeShuffle, Display, TEXT("%s: %s -- %s"), Prefix, *Tag, *Line);
            }

            const TCHAR* TermTaken =
                (Centre.InboundBackFaces > Centre.OutboundFrontFaces)
                    ? TEXT("the inbound back-face count")
                    : ((Centre.InboundBackFaces < Centre.OutboundFrontFaces)
                           ? TEXT("the outbound front-face count")
                           : TEXT("either of them, the two being equal"));

            UE_LOG(LogNodeShuffle, Display,
                TEXT("%s: %s -- SHADOWCROSS summary, and the arithmetic that produced the verdict. ")
                TEXT("INBOUND walk: %d blocking hit(s), of which %d were excluded, leaving %d counted ")
                TEXT("crossing(s) -- %d front face(s) and %d back face(s); %d hit(s) were reported ")
                TEXT("above and %d were not. OUTBOUND walk: %d blocking hit(s), of which %d were ")
                TEXT("excluded, leaving %d counted crossing(s) -- %d front face(s) and %d back ")
                TEXT("face(s); %d hit(s) were reported above and %d were not. THE ARITHMETIC: entries ")
                TEXT("is the inbound front-face count, %d. Exits is the larger of the inbound ")
                TEXT("back-face count %d and the outbound front-face count %d, and the term taken was ")
                TEXT("%s, giving %d. Net is entries minus exits, %d. This build reports the point as ")
                TEXT("inside when net is at least 1, so the verdict is %s. A walk %s its %d-hit ")
                TEXT("iteration budget. This line reports counts and the arithmetic applied to them, ")
                TEXT("and states no cause."),
                Prefix, *Tag,
                Centre.InboundHitsSeen, Centre.InboundExcludedHits,
                Centre.InboundFrontFaces + Centre.InboundBackFaces,
                Centre.InboundFrontFaces, Centre.InboundBackFaces,
                Centre.InboundDetailReported, Centre.InboundDetailSuppressed,
                Centre.OutboundHitsSeen, Centre.OutboundExcludedHits,
                Centre.OutboundFrontFaces + Centre.OutboundBackFaces,
                Centre.OutboundFrontFaces, Centre.OutboundBackFaces,
                Centre.OutboundDetailReported, Centre.OutboundDetailSuppressed,
                Centre.Entries,
                Centre.InboundBackFaces, Centre.OutboundFrontFaces,
                TermTaken, Centre.Exits, Centre.Net,
                bCentreInside ? TEXT("CENTRE INSIDE") : TEXT("CENTRE NOT INSIDE"),
                Centre.bSegmentCapHit ? TEXT("used all of") : TEXT("stayed inside"),
                Centre.MaxHitsPerSegment);
        }
    }

    return Agreement;
}
