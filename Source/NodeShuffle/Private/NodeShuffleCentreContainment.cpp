// ns-t42-centreshadow: THE CANDIDATE CONTAINMENT TEST -- "IS THIS POINT ITSELF INSIDE SOLID GEOMETRY".
// A SHADOW METRIC. NOTHING GATES ON IT, NOTHING REFUSES BECAUSE OF IT, AND NO PLACEMENT PATH CALLS IT.
//
// THE RULE IT EVALUATES, in the author's words (2026-08-09): "what if we changed it so partially inside
// meant the CENTER of what it was couldn't be inside, the center being on the edge is fine. Since we snap
// to the center, if the center is on the edge then we should be able to see part of the building."
// So: centre on the surface = wanted. Centre buried = the defect. That is a question about ONE POINT, and
// this file answers only that question.
//
// WHY IT IS A SEPARATE FILE AND A SEPARATE FUNCTION. It knows nothing about wells, cores, satellites,
// layout records, resource classes or node classes -- a caller hands it a world location. A follow-up
// packet is expected to run it over solid nodes, oil/liquid nodes and modded node classes, and that must
// not require touching anything here. It also shares NO constant with the enclosure predicate in
// NodeShuffleWellFootprint.cpp: it derives no eye height from it, casts none of its rays, and reads none
// of its thresholds, so neither can drift into the other.
//
// WHY NOT A SPHERE OVERLAP, WHICH IS WHAT THE EXISTING EYE READING USES. The T39 cold review established
// that a small sphere overlap does not report containment inside a landscape heightfield or inside a
// complex-as-simple triangle mesh -- neither has an interior for an overlap to be inside of -- and those
// are exactly the two geometry types a member buried in a cliff lives in. An overlap's NO is therefore
// not evidence there. This needs a method whose NO can be trusted, or that says plainly when it cannot.
//
// THE METHOD: SIGNED SURFACE CROSSINGS, WALKED IN BOTH DIRECTIONS.
// One segment is walked from a start high above the point down to the point, stepping past each blocking
// hit and resuming just beyond it, so every crossing on the way is seen rather than only the first. Each
// counted crossing is classified by the sign of its impact normal against the direction of travel: a
// normal opposing travel is a surface being entered, a normal running with travel is one being left. The
// same segment is then walked in reverse, from the point back out to the start. Entries are the inbound
// crossings that opposed travel; exits are the LARGER of (inbound crossings that ran with travel) and
// (reverse-walk crossings that opposed travel). The point reads inside when entries exceed exits.
// The two-direction form is there for one reason: whether this engine reports a surface hit from its back
// side is not knowable from headers, and the reverse walk recovers the exits from their own front side
// when it does not. The max() makes the same arithmetic correct whichever of the two it turns out to be.
//
// WHAT THIS METHOD CAN AND CANNOT ESTABLISH -- stated because the packet that ordered it asked for it,
// and because a confident NO from a blind test is the failure mode being avoided:
//   * It reports surface crossings on ECC_WorldStatic with simple collision -- the same channel and the
//     same complexity flag the shipped enclosure predicate traces on. Geometry that does not block that
//     channel is invisible to it, and so is geometry a trace resolves differently from a sweep.
//   * It assumes the start point high above is in open air. If that start is itself inside something, the
//     crossing count is offset and the verdict is wrong. bFirstInboundCountedWasBackFace is reported so a
//     reader can see the one signature that betrays it.
//   * A step epsilon past each hit can skip a shell thinner than that epsilon, and a segment that hits the
//     iteration cap is truncated. Both are reported per reading.
//   * IT CANNOT PROVE ITS OWN NEGATIVE FROM CONSTRUCTION. So it does not try: every reading carries a
//     POSITIVE CONTROL -- the same walk run on a point placed deliberately below the first surface under
//     the sky above the tested point. When that control reads inside, this method demonstrably detects
//     containment in that geometry on that run. When it does not, the reading's NO is reported as NOT
//     TRUSTWORTHY rather than as open air.
//
// TWO MANDATORY EXCLUSIONS, BOTH FROM MEASUREMENTS ALREADY IN docs/TECH-DEBT.md:
//   * AFGBuildable -- T40 measured 24 of 28 blocked enclosure rays as the author's OWN
//     Build_FrackingExtractor_C. A member with a working extractor standing on it is the OPPOSITE of the
//     defect this test is looking for, so a player's own machine must not read as the world burying it.
//   * APawn -- T36 measured the author's own character blocking all 8 enclosure rays at 0 cm.
// Both are COUNTED, not silently ignored: an exclusion with no denominator is one nobody can check.
//
// STATES NO CAUSE. Every field is written from the traces this call made. Nothing here says why the world
// is shaped the way it is, why a surface is where it is, or why a member ended up where it did.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"

// ns-t49-crossingdetail (2026-08-10): PER-CROSSING OBSERVATION ADDED, NO DECISION CHANGED.
// The walk above is known to report CENTRE INSIDE for a point measured to be in open air
// (docs/TECH-DEBT.md T48), and the only lines that produced a conclusion that survived were lines that
// printed WHAT THEY HIT. This packet therefore adds, per blocking hit: the actor and component and their
// classes, the distance along the segment and the resulting position, the trace fields the classification
// consulted and the ones it did not, and the running per-walk totals. NOTHING BELOW WAS ALTERED: the
// entries term, the exits term, the max(), the 2 cm step, the 100000 cm sky start, the three exclusions
// and the >= 1 threshold are byte-for-byte the same expressions. The one refactor is that the
// impact-normal dot product is now computed once above the exclusion chain instead of inside its final
// branch; `bBack` is still exactly `that dot product > 0.0`, and FVector::DotProduct has no side effect.
// NO FIX IS ATTEMPTED HERE. The walk is known to fail toward NOT INSIDE, so a change that made this one
// point read correctly could do it for the wrong reason and break the other direction unobserved.

// ns-t51-ignorelist (2026-08-10): THE FIX FOR docs/TECH-DEBT.md T50. THE VERDICT RULE IS UNCHANGED.
// T50 measured, at a point in open air, 28 of 32 inbound and 31 of 32 outbound blocking hits discarded --
// nearly all of them the caller's own pawn, hit again and again because the walk stepped only 2 cm past
// each discard. The exclusions were applied AFTER the trace, so a discarded hit still cost a step of the
// 32-hit budget and a ~180 cm capsule could absorb ~90 of them alone.
//
// WHAT CHANGED, AND ONLY THIS. The FCollisionQueryParams object is now built ONCE per walk instead of
// once per iteration, and the FIRST time a walk excludes an actor it hands that actor to
// Params.AddIgnoredActor and RE-TRACES FROM THE SAME CURSOR instead of stepping past it. An excluded
// actor is therefore never returned again by that walk and costs exactly ONE iteration in total,
// however tall it is. This is the same mechanism ANodeShuffleSubsystem::IsSpotEnclosed already uses for
// its pawn exclusion (T36); it is not a new one.
//
// WHY ALL THREE GO THROUGH THE SAME PATH RATHER THAN PRE-IGNORING THE SUBJECT ACTOR. Pre-ignoring the
// subject before the first trace would cost zero iterations instead of one, but it would also make
// ExcludedSubject structurally zero on every reading -- and the exclusion counts with their denominator
// are the instrument that found T50 at all. One path for all three also means the subject, the pawn and
// the buildable cannot drift apart. The price is one iteration per DISTINCT excluded actor per walk.
//
// WHAT THE BUDGET NOW BOUNDS. ShadowMaxHitsPerSegment is unchanged at 32 and still counts loop
// iterations, but an iteration is now spent on a COUNTED CROSSING or on the FIRST sight of one excluded
// actor -- no longer on the same actor over and over. A walk that still exhausts it is still recorded in
// bCapHit and still printed on every reading; this fix does not hide its own failure mode.
//
// ONE SIDE EFFECT, STATED BECAUSE IT CAN MOVE A COUNT. The old walk stepped 2 cm past every excluded hit
// and so could step over a real surface lying within those 2 cm; the new walk does not move the cursor
// on an excluded hit and cannot skip such a surface. Counts may therefore differ from a pre-fix reading
// for that reason as well as for the budget.
//
// THE ANOMALY GUARD. If a trace returns an actor this walk has ALREADY handed to the ignore list, the
// walk counts that in IgnoredRehits, steps past it as the old code did, and carries on -- so it can
// never stand still, and the condition is visible in the log rather than silent. Zero is the expected
// reading.
//
// UNTOUCHED, BYTE FOR BYTE: the entries term, the exits term, the max(), the >= 1 threshold, the
// classification (`bBack` is still exactly `NormalDotDir > 0.0`), the 2 cm step applied to counted hits,
// the 100000 cm sky start, ECC_WorldStatic, the trace-complexity flag, the trace tag, the exclusion SET
// (the same three predicates in the same order), the positive control, and the SHADOWCROSS detail block.

#include "Buildables/FGBuildable.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

namespace
{
    // How far above the tested point the inbound walk begins. Chosen to be well clear of the map's
    // terrain rather than derived from anything measured; it is printed back from this constant on every
    // reading so a reader never has to trust a number typed into a log string.
    constexpr float ShadowSkyOffsetCm = 100000.0f;

    // How far past each blocking hit the walk resumes. A shell thinner than this can be stepped over.
    constexpr float ShadowStepEpsilonCm = 2.0f;

    // Per-segment iteration budget. A segment that uses it all is reported as truncated.
    constexpr int32 ShadowMaxHitsPerSegment = 32;

    // How far the POSITIVE CONTROL point is placed below the first surface found under the sky above the
    // tested point. A rock shell thinner than this puts the control back in open air, which makes the
    // control fail and the reading be labelled untrustworthy -- an under-claim, never an over-claim.
    constexpr float ShadowControlDepthCm = 500.0f;

    // How far below the tested point the control's surface-finder walk is allowed to reach.
    constexpr float ShadowControlFinderDropCm = 20000.0f;

    // ns-t49-crossingdetail: most blocking hits reported individually per walk. Below the segment's own
    // 32-hit iteration budget, so a capped list is possible; how many were not reported is printed rather
    // than left implied, because a truncated list that does not say it was truncated is a bare count.
    constexpr int32 ShadowCrossingDetailCap = 24;

    // One walked segment's tally. Every field is a count of what a trace returned on THIS walk.
    struct FShadowSegment
    {
        int32 HitsSeen = 0;
        // ns-t51-ignorelist: each of the three below is still literally "blocking hits this walk removed
        // for that reason". Because an excluded actor is handed to the trace's own ignore list the first
        // time it is seen, that is now also the number of DISTINCT actors excluded for that reason.
        int32 ExcludedBuildable = 0;
        int32 ExcludedPawn = 0;
        int32 ExcludedSubject = 0;
        // ns-t51-ignorelist: hits returned on an actor this walk had ALREADY put on the trace's ignore
        // list. Not expected; counted rather than assumed away. Reported on every reading.
        int32 IgnoredRehits = 0;
        int32 FrontFaces = 0;          // counted crossings whose impact normal OPPOSED the travel
        int32 BackFaces = 0;           // counted crossings whose impact normal RAN WITH the travel
        bool bCapHit = false;
        bool bAnyCounted = false;
        bool bFirstCountedWasBackFace = false;
        bool bHaveFirstCountedImpact = false;
        FVector FirstCountedImpact = FVector::ZeroVector;
        FString Actors;

        // ns-t49-crossingdetail. Written after each hit has already been classified; read by nothing.
        int32 DetailReported = 0;
        int32 DetailSuppressed = 0;
    };

    // Walks From -> To on ECC_WorldStatic, stepping past every blocking hit, classifying and counting.
    // It never decides anything: it fills the tally and returns.
    //
    // ns-t49-crossingdetail: WalkLabel and DetailSink are OBSERVATION ONLY. DetailSink is null on every
    // call that does not want a per-hit list, and no statement below reads either of them when deciding
    // anything -- each detail string is built from values the classification has already produced.
    void WalkShadowSegment(UWorld* World, const FVector& From, const FVector& To,
                           const AActor* SubjectActor, FShadowSegment& Tally,
                           const TCHAR* WalkLabel = nullptr, TArray<FString>* DetailSink = nullptr)
    {
        const double Span = FVector::Dist(From, To);
        if (Span <= UE_KINDA_SMALL_NUMBER) { return; }
        const FVector Dir = (To - From) / Span;
        FVector Cursor = From;

        // ns-t51-ignorelist: ONE params object for the whole walk, so an actor added to its ignore list
        // stays ignored for every remaining trace of this walk. Constructed with exactly the trace tag
        // and the trace-complexity flag (false) the per-iteration object was constructed with before --
        // the same complexity flag the enclosure predicate uses, so both instruments resolve the same
        // geometry and a disagreement between them cannot be an artefact of that setting.
        FCollisionQueryParams Params(FName(TEXT("NodeShuffleCentreShadow")), false);
        // Which actors this walk has already excluded and handed to Params. Read only by the anomaly
        // guard below; it classifies nothing and enters no term of the verdict.
        TSet<const AActor*> AlreadyIgnored;

        for (int32 Step = 0; Step < ShadowMaxHitsPerSegment; ++Step)
        {
            // Stop once the cursor has been advanced past the far end; a trace with a reversed segment
            // would silently walk backwards and count the same surfaces again.
            if (FVector::DotProduct(To - Cursor, Dir) <= 0.0) { return; }

            FHitResult Hit;
            if (!World->LineTraceSingleByChannel(Hit, Cursor, To, ECC_WorldStatic, Params)) { return; }

            ++Tally.HitsSeen;
            const AActor* HitActor = Hit.GetActor();
            const bool bSubject = (HitActor != nullptr) && (HitActor == SubjectActor);
            const bool bPawn = (HitActor != nullptr) && HitActor->IsA<APawn>();
            const bool bBuildable = (HitActor != nullptr) && HitActor->IsA<AFGBuildable>();

            // ns-t49-crossingdetail: computed here rather than inside the final branch below so the
            // per-hit line can report it for an excluded hit too. The branch below still tests exactly
            // `this value > 0.0`, which is the expression it tested before, and FVector::DotProduct has
            // no side effect, so no hit is classified differently for having been measured earlier.
            const double NormalDotDir = FVector::DotProduct(Hit.ImpactNormal, Dir);
            const TCHAR* Classification = TEXT("<unset>");
            // ns-t51-ignorelist: an excluded actor is added to the trace's ignore list and the walk
            // re-traces from the SAME cursor, so it costs one iteration in total instead of one per
            // step of its own height. Counted hits still advance the cursor exactly as they did.
            bool bAdvanceCursor = true;

            if (bSubject || bPawn || bBuildable)
            {
                if (AlreadyIgnored.Contains(HitActor))
                {
                    // The trace returned an actor this walk had already given it to ignore. Not
                    // expected. Counted on its own, and the cursor IS advanced so the walk cannot
                    // stand still on it.
                    ++Tally.IgnoredRehits;
                    Classification = TEXT("EXCLUDED AGAIN on an actor this walk had already added to ")
                                     TEXT("the trace's own ignore list, which is not expected; this ")
                                     TEXT("hit is not counted as a crossing and the walk stepped past ")
                                     TEXT("it rather than re-tracing from the same position");
                }
                else
                {
                    AlreadyIgnored.Add(HitActor);
                    Params.AddIgnoredActor(HitActor);
                    bAdvanceCursor = false;
                    if (bSubject)
                    {
                        ++Tally.ExcludedSubject;
                        Classification = TEXT("EXCLUDED, being a hit on the actor this call named as ")
                                         TEXT("the tested point's own subject, and so not counted as ")
                                         TEXT("a crossing; it was added to the trace's ignore list and ")
                                         TEXT("this walk re-traced from the same position");
                    }
                    else if (bPawn)
                    {
                        ++Tally.ExcludedPawn;
                        Classification = TEXT("EXCLUDED, being a hit on an APawn, and so not counted ")
                                         TEXT("as a crossing; it was added to the trace's ignore list ")
                                         TEXT("and this walk re-traced from the same position");
                    }
                    else
                    {
                        ++Tally.ExcludedBuildable;
                        Classification = TEXT("EXCLUDED, being a hit on an AFGBuildable, and so not ")
                                         TEXT("counted as a crossing; it was added to the trace's ")
                                         TEXT("ignore list and this walk re-traced from the same ")
                                         TEXT("position");
                    }
                }
            }
            else
            {
                const bool bBack = (NormalDotDir > 0.0);
                if (bBack) { ++Tally.BackFaces; } else { ++Tally.FrontFaces; }
                Classification = bBack
                    ? TEXT("COUNTED as a BACK face, the impact-normal dot product below being greater ")
                      TEXT("than zero")
                    : TEXT("COUNTED as a FRONT face, the impact-normal dot product below not being ")
                      TEXT("greater than zero");
                if (!Tally.bAnyCounted)
                {
                    Tally.bAnyCounted = true;
                    Tally.bFirstCountedWasBackFace = bBack;
                    Tally.FirstCountedImpact = Hit.ImpactPoint;
                    Tally.bHaveFirstCountedImpact = true;
                }
                if (Tally.FrontFaces + Tally.BackFaces <= 4)
                {
                    Tally.Actors += FString::Printf(TEXT("'%s' whose normal %s "),
                        HitActor ? *HitActor->GetName() : TEXT("<crossing with no actor>"),
                        bBack ? TEXT("ran with the walk") : TEXT("opposed the walk"));
                }
            }

            // ---- ns-t49-crossingdetail: WHAT THIS HIT WAS. Pure observation, after the fact ----
            // Everything read here has already been decided above. The cursor advance below is outside
            // this block and identical whether or not it runs.
            if (DetailSink != nullptr)
            {
                if (Tally.DetailReported >= ShadowCrossingDetailCap)
                {
                    ++Tally.DetailSuppressed;
                }
                else
                {
                    ++Tally.DetailReported;
                    const UPrimitiveComponent* HitComp = Hit.GetComponent();
                    const FString ActorName = HitActor ? HitActor->GetName()
                                                       : FString(TEXT("<this hit named no actor>"));
                    const FString ActorClass = HitActor ? HitActor->GetClass()->GetName()
                                                        : FString(TEXT("<none, no actor>"));
                    const FString CompName = HitComp ? HitComp->GetName()
                                                     : FString(TEXT("<this hit named no component>"));
                    const FString CompClass = HitComp ? HitComp->GetClass()->GetName()
                                                      : FString(TEXT("<none, no component>"));
                    const double AlongCm = FVector::DotProduct(Hit.ImpactPoint - From, Dir);
                    DetailSink->Add(FString::Printf(
                        TEXT("SHADOWCROSS %d of the %s walk: %s. Actor '%s' of class %s, component ")
                        TEXT("'%s' of class %s. Impact at %s, %.1f cm along this walk's %.1f cm ")
                        TEXT("segment. The trace reported an impact normal of %s, a normal of %s, a ")
                        TEXT("dot product of that impact normal with this walk's direction of %+.6f, ")
                        TEXT("and start-penetrating %s; of those, the classification above read the ")
                        TEXT("sign of the dot product and nothing else. Running for this walk after ")
                        TEXT("this hit: %d front face(s), %d back face(s), and %d hit(s) not counted ")
                        TEXT("as a crossing at all, over %d blocking hit(s) seen."),
                        Tally.HitsSeen,
                        WalkLabel ? WalkLabel : TEXT("<unlabelled>"),
                        Classification,
                        *ActorName, *ActorClass, *CompName, *CompClass,
                        *Hit.ImpactPoint.ToCompactString(), AlongCm, Span,
                        *Hit.ImpactNormal.ToCompactString(), *Hit.Normal.ToCompactString(),
                        NormalDotDir,
                        Hit.bStartPenetrating ? TEXT("true") : TEXT("false"),
                        Tally.FrontFaces, Tally.BackFaces,
                        Tally.ExcludedBuildable + Tally.ExcludedPawn + Tally.ExcludedSubject
                            + Tally.IgnoredRehits,
                        Tally.HitsSeen));
                }
            }

            // Resume just beyond the hit. Forced forward by at least the epsilon so a hit reported at the
            // cursor itself cannot stall the walk on one surface forever.
            // ns-t51-ignorelist: skipped for a newly-excluded actor only. That actor is now on the
            // trace's ignore list, so the next trace runs from THIS SAME cursor and returns the next
            // blocking hit past it -- which also means the walk can no longer step over a surface that
            // lies within the epsilon beyond an excluded hit. Every other path through this loop, and
            // every counted crossing, advances exactly as it did before.
            if (bAdvanceCursor)
            {
                const FVector Next = Hit.ImpactPoint + Dir * ShadowStepEpsilonCm;
                Cursor = (FVector::DotProduct(Next - Cursor, Dir) > 0.0)
                           ? Next
                           : (Cursor + Dir * ShadowStepEpsilonCm);
            }
        }
        // Reached only by exhausting the loop; every other way out of it is an early return above.
        Tally.bCapHit = true;
    }

    // Both walks for one point, plus the arithmetic that turns them into a verdict.
    struct FShadowParity
    {
        FShadowSegment Inbound;
        FShadowSegment Outbound;
        int32 Entries = 0;
        int32 Exits = 0;
        int32 Net = 0;
        bool bInside = false;
    };

    // ns-t49-crossingdetail: DetailSink is null on the positive control's own parity run and on the
    // control's surface-finder walk, so the per-hit list describes the two walks of the tested point
    // only. That is stated in the emitted legend rather than left for a reader to infer from absence.
    void RunShadowParity(UWorld* World, const FVector& Point, const FVector& SkyStart,
                         const AActor* SubjectActor, FShadowParity& R,
                         TArray<FString>* DetailSink = nullptr)
    {
        WalkShadowSegment(World, SkyStart, Point, SubjectActor, R.Inbound,
                          TEXT("inbound (sky start down to the tested point)"), DetailSink);
        WalkShadowSegment(World, Point, SkyStart, SubjectActor, R.Outbound,
                          TEXT("outbound (the tested point back up to the sky start)"), DetailSink);
        R.Entries = R.Inbound.FrontFaces;
        // The larger of the two readings of the same quantity: inbound back faces are the exits when this
        // engine reports a surface from behind, and the reverse walk's front faces are the same exits seen
        // from their own front when it does not. Taking the larger is correct in either case and does not
        // require knowing which one is in force.
        R.Exits = FMath::Max(R.Inbound.BackFaces, R.Outbound.FrontFaces);
        R.Net = R.Entries - R.Exits;
        R.bInside = (R.Net >= 1);
    }
}

bool ANodeShuffleSubsystem::IsPointInsideSolidShadowForDiag(const FVector& At, const AActor* SubjectActor,
                                                            FNodeShufflePointInsideReading& Out,
                                                            bool bWantCrossingDetail) const
{
    Out = FNodeShufflePointInsideReading();
    Out.Point = At;
    Out.SkyOffsetCm = ShadowSkyOffsetCm;
    Out.StepEpsilonCm = ShadowStepEpsilonCm;
    Out.MaxHitsPerSegment = ShadowMaxHitsPerSegment;
    Out.ControlDepthCm = ShadowControlDepthCm;
    Out.SkyStart = FVector(At.X, At.Y, At.Z + ShadowSkyOffsetCm);
    Out.bCrossingDetailRequested = bWantCrossingDetail;
    Out.CrossingDetailCap = ShadowCrossingDetailCap;

    UWorld* World = GetWorld();
    if (!World)
    {
        // Left bRan false with every count at zero: a caller must be able to tell "the walk was not run"
        // from "the walk ran and crossed nothing", and a zero that cannot say which is a zero with no
        // denominator behind it.
        Out.CrossingActors = TEXT("<the walk was NOT run: this subsystem has no world>");
        Out.ControlDetail = TEXT("<the control was NOT built: this subsystem has no world>");
        return false;
    }

    FShadowParity Main;
    RunShadowParity(World, At, Out.SkyStart, SubjectActor, Main,
                    bWantCrossingDetail ? &Out.CrossingDetail : nullptr);

    Out.bRan = true;
    Out.HitsSeen = Main.Inbound.HitsSeen + Main.Outbound.HitsSeen;
    Out.ExcludedBuildableHits = Main.Inbound.ExcludedBuildable + Main.Outbound.ExcludedBuildable;
    Out.ExcludedPawnHits = Main.Inbound.ExcludedPawn + Main.Outbound.ExcludedPawn;
    Out.ExcludedSubjectHits = Main.Inbound.ExcludedSubject + Main.Outbound.ExcludedSubject;
    Out.IgnoredRehits = Main.Inbound.IgnoredRehits + Main.Outbound.IgnoredRehits;
    Out.InboundFrontFaces = Main.Inbound.FrontFaces;
    Out.InboundBackFaces = Main.Inbound.BackFaces;
    Out.OutboundFrontFaces = Main.Outbound.FrontFaces;
    Out.OutboundBackFaces = Main.Outbound.BackFaces;
    Out.CountedCrossings = Main.Inbound.FrontFaces + Main.Inbound.BackFaces
                         + Main.Outbound.FrontFaces + Main.Outbound.BackFaces;
    Out.Entries = Main.Entries;
    Out.Exits = Main.Exits;
    Out.Net = Main.Net;
    Out.bInside = Main.bInside;
    Out.bBackFacesObserved = (Main.Inbound.BackFaces > 0) || (Main.Outbound.BackFaces > 0);
    Out.bSegmentCapHit = Main.Inbound.bCapHit || Main.Outbound.bCapHit;
    Out.bFirstInboundCountedWasBackFace = Main.Inbound.bAnyCounted && Main.Inbound.bFirstCountedWasBackFace;
    Out.CrossingActors = Main.Inbound.Actors + Main.Outbound.Actors;
    // ns-t49-crossingdetail: the per-walk denominators the per-hit list is checked against.
    Out.InboundHitsSeen = Main.Inbound.HitsSeen;
    Out.OutboundHitsSeen = Main.Outbound.HitsSeen;
    Out.InboundExcludedHits = Main.Inbound.ExcludedBuildable + Main.Inbound.ExcludedPawn
                            + Main.Inbound.ExcludedSubject;
    Out.OutboundExcludedHits = Main.Outbound.ExcludedBuildable + Main.Outbound.ExcludedPawn
                             + Main.Outbound.ExcludedSubject;
    Out.InboundIgnoredRehits = Main.Inbound.IgnoredRehits;
    Out.OutboundIgnoredRehits = Main.Outbound.IgnoredRehits;
    Out.InboundDetailReported = Main.Inbound.DetailReported;
    Out.OutboundDetailReported = Main.Outbound.DetailReported;
    Out.InboundDetailSuppressed = Main.Inbound.DetailSuppressed;
    Out.OutboundDetailSuppressed = Main.Outbound.DetailSuppressed;
    if (Out.CountedCrossings == 0)
    {
        Out.CrossingActors = TEXT("<no counted crossing to name>");
    }

    // ---- THE POSITIVE CONTROL ----
    // A surface-finder walk straight down from the same sky start, past the tested point, so a point in
    // open air and a point already buried both produce one. The control sits a fixed distance below the
    // FIRST counted surface that walk found, and is then run through the identical parity test. Its result
    // is what makes this reading's negative worth anything: it is the run's own evidence that the method
    // can see containment in the geometry above this point at all.
    {
        FShadowSegment Finder;
        const FVector FinderEnd(At.X, At.Y, At.Z - ShadowControlFinderDropCm);
        WalkShadowSegment(World, Out.SkyStart, FinderEnd, SubjectActor, Finder);
        if (!Finder.bHaveFirstCountedImpact)
        {
            Out.ControlDetail = FString::Printf(
                TEXT("<not built: a walk from the sky start down to %.0f cm below the tested point found ")
                TEXT("no counted surface at all, so there was nothing to place a control beneath>"),
                ShadowControlFinderDropCm);
        }
        else
        {
            Out.ControlPoint = Finder.FirstCountedImpact - FVector(0.0, 0.0, ShadowControlDepthCm);
            FShadowParity Control;
            RunShadowParity(World, Out.ControlPoint,
                            FVector(Out.ControlPoint.X, Out.ControlPoint.Y,
                                    Out.ControlPoint.Z + ShadowSkyOffsetCm),
                            SubjectActor, Control);
            Out.bControlRan = true;
            Out.ControlNet = Control.Net;
            Out.bControlInside = Control.bInside;
            Out.ControlDetail = FString::Printf(
                TEXT("placed %.0f cm below %s, the first counted surface a downward walk from the sky ")
                TEXT("start met; that walk crossed %d counted surface(s) in total"),
                ShadowControlDepthCm, *Finder.FirstCountedImpact.ToCompactString(),
                Finder.FrontFaces + Finder.BackFaces);
        }
    }

    UE_LOG(LogNodeShuffle, Verbose,
        TEXT("CENTRESHADOW at %s: entries %d, exits %d, net %d of the %d counted crossing(s) on both ")
        TEXT("walks; %d hit(s) were seen in total and %d of those were not counted as a crossing -- ")
        TEXT("%d on a buildable, %d on a pawn, %d on the named subject, each of those being the first ")
        TEXT("sight of a distinct actor which was then added to that walk's trace ignore list, and %d ")
        TEXT("returned again after having been added. A walk %s its %d-iteration budget, which since ")
        TEXT("ns-t51-ignorelist bounds counted crossings plus distinct excluded actors rather than raw ")
        TEXT("hits. The positive control %s. This is a shadow reading: no gate, no placement path and ")
        TEXT("no refusal reads it. It reports what these walks returned and states no cause."),
        *At.ToCompactString(), Out.Entries, Out.Exits, Out.Net, Out.CountedCrossings,
        Out.HitsSeen,
        Out.ExcludedBuildableHits + Out.ExcludedPawnHits + Out.ExcludedSubjectHits + Out.IgnoredRehits,
        Out.ExcludedBuildableHits, Out.ExcludedPawnHits, Out.ExcludedSubjectHits,
        Out.IgnoredRehits,
        Out.bSegmentCapHit ? TEXT("used all of") : TEXT("stayed inside"),
        Out.MaxHitsPerSegment,
        !Out.bControlRan
            ? TEXT("was not built")
            : (Out.bControlInside
                   ? TEXT("read inside, so this method detected containment in that geometry on this run")
                   : TEXT("did NOT read inside, so this method did not demonstrate it can detect ")
                     TEXT("containment there")));

    return Out.bInside;
}
