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

#include "Buildables/FGBuildable.h"
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

    // One walked segment's tally. Every field is a count of what a trace returned on THIS walk.
    struct FShadowSegment
    {
        int32 HitsSeen = 0;
        int32 ExcludedBuildable = 0;
        int32 ExcludedPawn = 0;
        int32 ExcludedSubject = 0;
        int32 FrontFaces = 0;          // counted crossings whose impact normal OPPOSED the travel
        int32 BackFaces = 0;           // counted crossings whose impact normal RAN WITH the travel
        bool bCapHit = false;
        bool bAnyCounted = false;
        bool bFirstCountedWasBackFace = false;
        bool bHaveFirstCountedImpact = false;
        FVector FirstCountedImpact = FVector::ZeroVector;
        FString Actors;
    };

    // Walks From -> To on ECC_WorldStatic, stepping past every blocking hit, classifying and counting.
    // It never decides anything: it fills the tally and returns.
    void WalkShadowSegment(UWorld* World, const FVector& From, const FVector& To,
                           const AActor* SubjectActor, FShadowSegment& Tally)
    {
        const double Span = FVector::Dist(From, To);
        if (Span <= UE_KINDA_SMALL_NUMBER) { return; }
        const FVector Dir = (To - From) / Span;
        FVector Cursor = From;

        for (int32 Step = 0; Step < ShadowMaxHitsPerSegment; ++Step)
        {
            // Stop once the cursor has been advanced past the far end; a trace with a reversed segment
            // would silently walk backwards and count the same surfaces again.
            if (FVector::DotProduct(To - Cursor, Dir) <= 0.0) { return; }

            FHitResult Hit;
            // The same complexity flag the enclosure predicate uses (false), so both instruments resolve
            // the same geometry and a disagreement between them cannot be an artefact of that setting.
            FCollisionQueryParams Params(FName(TEXT("NodeShuffleCentreShadow")), false);
            if (!World->LineTraceSingleByChannel(Hit, Cursor, To, ECC_WorldStatic, Params)) { return; }

            ++Tally.HitsSeen;
            const AActor* HitActor = Hit.GetActor();
            const bool bSubject = (HitActor != nullptr) && (HitActor == SubjectActor);
            const bool bPawn = (HitActor != nullptr) && HitActor->IsA<APawn>();
            const bool bBuildable = (HitActor != nullptr) && HitActor->IsA<AFGBuildable>();

            if (bSubject)
            {
                ++Tally.ExcludedSubject;
            }
            else if (bPawn)
            {
                ++Tally.ExcludedPawn;
            }
            else if (bBuildable)
            {
                ++Tally.ExcludedBuildable;
            }
            else
            {
                const bool bBack = (FVector::DotProduct(Hit.ImpactNormal, Dir) > 0.0);
                if (bBack) { ++Tally.BackFaces; } else { ++Tally.FrontFaces; }
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

            // Resume just beyond the hit. Forced forward by at least the epsilon so a hit reported at the
            // cursor itself cannot stall the walk on one surface forever.
            const FVector Next = Hit.ImpactPoint + Dir * ShadowStepEpsilonCm;
            Cursor = (FVector::DotProduct(Next - Cursor, Dir) > 0.0)
                       ? Next
                       : (Cursor + Dir * ShadowStepEpsilonCm);
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

    void RunShadowParity(UWorld* World, const FVector& Point, const FVector& SkyStart,
                         const AActor* SubjectActor, FShadowParity& R)
    {
        WalkShadowSegment(World, SkyStart, Point, SubjectActor, R.Inbound);
        WalkShadowSegment(World, Point, SkyStart, SubjectActor, R.Outbound);
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
                                                            FNodeShufflePointInsideReading& Out) const
{
    Out = FNodeShufflePointInsideReading();
    Out.Point = At;
    Out.SkyOffsetCm = ShadowSkyOffsetCm;
    Out.StepEpsilonCm = ShadowStepEpsilonCm;
    Out.MaxHitsPerSegment = ShadowMaxHitsPerSegment;
    Out.ControlDepthCm = ShadowControlDepthCm;
    Out.SkyStart = FVector(At.X, At.Y, At.Z + ShadowSkyOffsetCm);

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
    RunShadowParity(World, At, Out.SkyStart, SubjectActor, Main);

    Out.bRan = true;
    Out.HitsSeen = Main.Inbound.HitsSeen + Main.Outbound.HitsSeen;
    Out.ExcludedBuildableHits = Main.Inbound.ExcludedBuildable + Main.Outbound.ExcludedBuildable;
    Out.ExcludedPawnHits = Main.Inbound.ExcludedPawn + Main.Outbound.ExcludedPawn;
    Out.ExcludedSubjectHits = Main.Inbound.ExcludedSubject + Main.Outbound.ExcludedSubject;
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
        TEXT("walks; %d hit(s) were seen in total and %d of those were excluded -- %d on a buildable, ")
        TEXT("%d on a pawn, %d on the named subject. The positive control %s. This is a shadow reading: ")
        TEXT("no gate, no placement path and no refusal reads it. It reports what these walks returned ")
        TEXT("and states no cause."),
        *At.ToCompactString(), Out.Entries, Out.Exits, Out.Net, Out.CountedCrossings,
        Out.HitsSeen,
        Out.ExcludedBuildableHits + Out.ExcludedPawnHits + Out.ExcludedSubjectHits,
        Out.ExcludedBuildableHits, Out.ExcludedPawnHits, Out.ExcludedSubjectHits,
        !Out.bControlRan
            ? TEXT("was not built")
            : (Out.bControlInside
                   ? TEXT("read inside, so this method detected containment in that geometry on this run")
                   : TEXT("did NOT read inside, so this method did not demonstrate it can detect ")
                     TEXT("containment there")));

    return Out.bInside;
}
