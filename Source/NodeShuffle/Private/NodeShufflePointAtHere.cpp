// ns-t38-pointathere: `NodeShuffle.PointAtHere` -- THE AIMED ENCLOSURE PROBE.
//
// WHY THIS EXISTS. docs/TECH-DEBT.md carries an AUTHOR RULING (2026-08-09, on T34/T37): a well member
// PARTLY embedded in terrain is wanted, and the only case that is a defect is a member FULLY INSIDE a
// rock outcrop. That case has never been measured, because NodeShuffle.Here probes the point the player
// is STANDING on and nobody can stand inside a rock. This command aims instead of stands: it traces
// from the player's view point along the aim direction and runs the enclosure gate at whatever that
// trace hit.
//
// WHAT IT IS NOT. It is not a replacement for NodeShuffle.Here, which is unchanged and still
// registered in NodeShuffle.cpp -- the author asked for both. It changes NO placement behaviour: it
// runs the SAME ANodeShuffleSubsystem::IsSpotEnclosed both placement paths call, with the same optional
// recorder and the same optional ignore-actor NodeShuffle.Here already passes, and it adds no gate, no
// threshold, no radius, no ray and no channel of its own to any placement path. The two read-back
// accessors it uses (GetEnclosureProbeGeometryForDiag, GetCliffSlopeDegForDiag) return constants and
// run nothing.
//
// IT DOES NOT SETTLE THE AIMED POINT. NodeShuffle.Here hands the enclosure predicate the impact of a
// long DOWNWARD ground trace from the player, and on the previous build that produced a 21 m difference
// from the player's own feet. Doing the same here would defeat the measurement this command is for: a
// downward trace aimed at a point inside a rock lands on that rock's upper surface, which is a
// different point from the one the author aimed at. So the predicate is handed the aim trace's own
// impact point, unmodified. The ground trace is still run, but ONLY to report the slope and the cliff
// verdict (so the two commands' output can be compared line for line), and the line that prints its
// result says in full that its point was not the point the predicate was given.
//
// LOG-ONLY and side-effect-free, so -- SAME PRECEDENT as NodeShuffle.Here (NodeShuffle.cpp:33-45),
// NodeShuffle.DumpWells and NodeShuffle.DumpExtractors -- it is NOT gated behind EnableDiagnostics.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"

#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

namespace
{
    // How far along the aim ray this command looks for something to test. Named and file-local so the
    // log line prints it back from the constant the trace was actually built from.
    constexpr float PointAtAimRangeCm = 50000.0f;   // 500 m

    // The aim trace runs against complex collision so the reported impact is the actual triangle the
    // player is looking at rather than a simplified hull. Named here for the same read-back reason.
    constexpr bool bPointAtAimTraceComplex = true;

    FString PointAtActorLabel(const AActor* A)
    {
        if (!A) { return FString(TEXT("<no actor on this hit>")); }
        return FString::Printf(TEXT("'%s' of class '%s'"), *A->GetName(), *A->GetClass()->GetName());
    }
    FString PointAtCompLabel(const UPrimitiveComponent* C)
    {
        if (!C) { return FString(TEXT("<no component on this hit>")); }
        return FString::Printf(TEXT("'%s' of class '%s'"), *C->GetName(), *C->GetClass()->GetName());
    }
}

static FAutoConsoleCommandWithWorldAndArgs GNodeShufflePointAtHereCmd(
    TEXT("NodeShuffle.PointAtHere"),
    TEXT("Aim at a spot and run the enclosure + slope diagnostics AT THAT SPOT (log-only)."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World) { return; }
        for (TActorIterator<ANodeShuffleSubsystem> It(World); It; ++It)
        {
            It->LogPointAtHereCensus();
            return;
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("POINTAT: NodeShuffle subsystem not found in this world (main menu / no session loaded?) -- ")
            TEXT("nothing was traced and no gate was run."));
    }));

void ANodeShuffleSubsystem::LogPointAtHereCensus() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("POINTAT: this subsystem has no world -- no aim trace was cast and no gate was run. ")
            TEXT("Nothing below this line was measured."));
        return;
    }
    const APlayerController* Pc = World->GetFirstPlayerController();
    if (!Pc)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("POINTAT: no player controller in this world, and the aim origin and direction come ")
            TEXT("from one -- no aim trace was cast and no gate was run. This line reports that the ")
            TEXT("controller lookup returned nothing; it does not say why."));
        return;
    }
    // The pawn is optional here, unlike in NodeShuffle.Here. It is used for exactly two things: the
    // enclosure predicate's ignore list (the same one argument NodeShuffle.Here passes) and naming the
    // aim trace's hit when the trace terminates on the player's own body. A missing pawn degrades both
    // to an explicit sentinel rather than to a missing line.
    const APawn* Pawn = Pc->GetPawn();

    FVector ViewLoc = FVector::ZeroVector;
    FRotator ViewRot = FRotator::ZeroRotator;
    Pc->GetPlayerViewPoint(ViewLoc, ViewRot);
    const FVector AimDir = ViewRot.Vector();
    const FVector AimEnd = ViewLoc + AimDir * PointAtAimRangeCm;

    // THE AIM TRACE. Deliberately NOT given the pawn as an exclusion: whether the aim ray terminates on
    // the player's own body is one of the readings this command has to be able to report honestly, and
    // silently ignoring the pawn would hide it.
    FHitResult AimHit;
    FCollisionQueryParams AimParams(FName(TEXT("NodeShufflePointAtAim")), bPointAtAimTraceComplex);
    const bool bAimHit = World->LineTraceSingleByChannel(AimHit, ViewLoc, AimEnd, ECC_Visibility, AimParams);

    // A SECOND, PURELY INFORMATIONAL TRACE over the same segment on the channel the enclosure rays
    // themselves use. It decides nothing. It exists because the two channels can terminate on different
    // things, and a reader confirming "I hit the buried member and not the rock in front of it" needs to
    // see both rather than infer one from the other.
    FHitResult StaticHit;
    FCollisionQueryParams StaticParams(FName(TEXT("NodeShufflePointAtAimStatic")), bPointAtAimTraceComplex);
    const bool bStaticHit = World->LineTraceSingleByChannel(StaticHit, ViewLoc, AimEnd, ECC_WorldStatic, StaticParams);

    UE_LOG(LogNodeShuffle, Display,
        TEXT("POINTAT: aim trace -- origin %s, view rotation pitch %.1f yaw %.1f roll %.1f, unit ")
        TEXT("direction %s, reaching %.0f cm on trace channel ECC_Visibility with complex collision ")
        TEXT("%s. Of the 1 ray cast on that channel, %d reported a blocking hit. The origin and the ")
        TEXT("rotation are whatever the player controller returned for its view point this call; the ")
        TEXT("range and the complexity flag are read back from the constants this trace was built from."),
        *ViewLoc.ToCompactString(), ViewRot.Pitch, ViewRot.Yaw, ViewRot.Roll,
        *AimDir.ToCompactString(), PointAtAimRangeCm,
        bPointAtAimTraceComplex ? TEXT("enabled") : TEXT("disabled"),
        bAimHit ? 1 : 0);

    if (bAimHit)
    {
        const AActor* AimActor = AimHit.GetActor();
        const UPrimitiveComponent* AimComp = AimHit.GetComponent();
        UE_LOG(LogNodeShuffle, Display,
            TEXT("POINTAT: aim hit -- impact %s at %.0f cm from the origin. Actor %s. Component %s. ")
            TEXT("These four values are copied from the hit this trace reported and nothing here says ")
            TEXT("what that actor is for."),
            *AimHit.ImpactPoint.ToCompactString(), FVector::Dist(ViewLoc, AimHit.ImpactPoint),
            *PointAtActorLabel(AimActor), *PointAtCompLabel(AimComp));

        // The pawn case, reported rather than hidden. Compared by pointer identity against the pawn this
        // command resolved; when no pawn was resolved the comparison is not made and the line says so.
        if (Pawn && AimActor == Pawn)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("POINTAT: the actor that aim hit landed on is the SAME actor as the pawn this ")
                TEXT("command resolved for you ('%s'), so the point tested below sits on your own ")
                TEXT("body's collision and not on the world. This line reports an identity comparison ")
                TEXT("between two pointers and nothing else."),
                *Pawn->GetName());
        }
    }
    else
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("POINTAT: aim hit -- the 1 ray cast reported no blocking hit anywhere along its %.0f cm, ")
            TEXT("so there is no impact point, no hit actor and no hit component to report. This line ")
            TEXT("states what the trace returned; it does not say what you were pointing at."),
            PointAtAimRangeCm);
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("POINTAT: same segment on ECC_WorldStatic, which is the channel the enclosure rays below ")
        TEXT("are cast on -- of the 1 ray cast on that channel, %d reported a blocking hit%s. This ")
        TEXT("trace is informational: its result is not handed to any gate and does not affect the ")
        TEXT("point tested below. A difference between the two channels' hits is a difference in what ")
        TEXT("each channel blocks on, which this line does not evaluate."),
        bStaticHit ? 1 : 0,
        bStaticHit
            ? *FString::Printf(TEXT(", impact %s at %.0f cm from the origin, actor %s, component %s"),
                  *StaticHit.ImpactPoint.ToCompactString(), FVector::Dist(ViewLoc, StaticHit.ImpactPoint),
                  *PointAtActorLabel(StaticHit.GetActor()), *PointAtCompLabel(StaticHit.GetComponent()))
            : TEXT(""));

    if (!bAimHit)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("POINTAT: the aim trace produced no point, so the enclosure predicate was NOT CALLED ")
            TEXT("and the ground trace was NOT RUN. There is no verdict for this invocation -- not a ")
            TEXT("clear one and not a refused one. Aim at something within the range printed above and ")
            TEXT("run the command again."));
        return;
    }

    // THE POINT HANDED TO THE PREDICATE. The aim impact, unmodified.
    const FVector TestAt = AimHit.ImpactPoint;

    float EyeHeightCm = 0.0f;
    float ReachCm = 0.0f;
    GetEnclosureProbeGeometryForDiag(EyeHeightCm, ReachCm);

    UE_LOG(LogNodeShuffle, Display,
        TEXT("POINTAT: the point handed to the enclosure predicate is %s -- the aim trace's own impact ")
        TEXT("point, unmodified. NO GROUND SETTLE WAS APPLIED TO IT. That is the one place this command ")
        TEXT("deliberately differs from NodeShuffle.Here, which hands the predicate the impact of a long ")
        TEXT("downward ground trace instead. The predicate raises its own probe eye a fixed distance in ")
        TEXT("Z above whatever point it is given: %.0f cm here, so the rays below start at world height ")
        TEXT("%.1f and each reaches %.0f cm horizontally. Both of those distances were read back from ")
        TEXT("the constants the predicate is compiled with, not typed into this line."),
        *TestAt.ToCompactString(), EyeHeightCm, TestAt.Z + EyeHeightCm, ReachCm);

    // THE ENCLOSURE GATE, at the aimed point. The SAME member function both placement paths call, with
    // the same recorder and threshold out-params NodeShuffle.Here uses, and the pawn on the ignore list
    // exactly as NodeShuffle.Here passes it (ns-t36-probefix). Nothing here reimplements the predicate.
    TArray<FNodeShuffleEnclosureRay> Rays;
    int32 Blocked = 0, Total = 0, Threshold = -1;
    const bool bEnclosed = IsSpotEnclosed(TestAt, Blocked, Total, &Rays, &Threshold, Pawn);

    // ns-t39-wellprobe (T38 cold review F1): THE PROBE-EYE READING, PRINTED BEFORE THE VERDICT. It
    // matters most on THIS command: an aim ray terminates ON a surface, so the point handed to the
    // predicate sits on that surface and the eye above it can land inside the body behind it. Added to
    // all three probe commands rather than only to the new one -- a check present on one probe and
    // absent on its siblings is this project's most-repeated defect.
    FNodeShuffleProbeEyeReading Eye;
    const bool bEyeInside = IsProbeEyeInsideSolidForDiag(TestAt, Pawn, Eye);
    UE_LOG(LogNodeShuffle, Display,
        TEXT("POINTAT: probe eye inside solid geometry: %s. The eye sits at %s, which is where the ")
        TEXT("enclosure predicate below starts its rays. One sphere of radius %.0f cm was overlapped ")
        TEXT("there on the same channel those rays are cast on, with the same one actor on the ignore ")
        TEXT("list; it returned %d result(s), %d of which report blocking on that channel: %s. WHY THIS ")
        TEXT("COMES FIRST: an eye that starts inside a blocking body makes every ray below terminate at ")
        TEXT("once, and the verdict then reads as a confident full refusal containing no terrain. A ")
        TEXT("negative reading is a statement about this channel at this radius and is not a claim that ")
        TEXT("the eye stands in open air. This line states no cause."),
        !Eye.bRan ? TEXT("UNMEASURED -- the overlap did not run, so neither answer is reported")
                  : (bEyeInside ? TEXT("YES") : TEXT("NO")),
        *Eye.Eye.ToCompactString(), Eye.ProbeRadiusCm,
        Eye.OverlapResults, Eye.BlockingOverlaps, *Eye.BlockingActors);

    UE_LOG(LogNodeShuffle, Display,
        TEXT("POINTAT: enclosure probe exclusions -- %d of the 1 actor this command has to offer (the ")
        TEXT("pawn it resolved for you, named here) was handed to the trace's ignore list for the rays ")
        TEXT("below: %s. This line reports what was passed IN to the predicate. Whether any ray still ")
        TEXT("reported a hit on that actor is a separate question and the ray lines below answer it -- ")
        TEXT("each names the actor its own hit belonged to. The placement paths pass no exclusion at ")
        TEXT("all, so a spot this command calls clear is not thereby a spot they would call clear while ")
        TEXT("a pawn stands on it."),
        (Pawn != nullptr) ? 1 : 0,
        (Pawn != nullptr) ? *Pawn->GetName() : TEXT("<none: no pawn resolved>"));

    for (int32 i = 0; i < Rays.Num(); ++i)
    {
        const FNodeShuffleEnclosureRay& R = Rays[i];
        UE_LOG(LogNodeShuffle, Display,
            TEXT("POINTAT: enclosure ray %d of %d, bearing %.0f deg: %s%s%s"),
            i + 1, Rays.Num(), R.BearingDeg,
            R.bBlocked ? TEXT("BLOCKED") : TEXT("clear"),
            R.bBlocked ? *FString::Printf(TEXT(" at %.0f cm by "), R.HitDistanceCm) : TEXT(""),
            R.bBlocked ? *R.HitActor : TEXT(""));
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("POINTAT: ENCLOSURE GATE at %s (the aim trace's impact point, not settled) -- %d of %d ")
        TEXT("rays blocked, and this build refuses a spot at %d or more blocked, so the verdict here is ")
        TEXT("%s. The rays above are the ones this call cast: horizontal, evenly spaced in bearing, ")
        TEXT("from a fixed height above the tested point, each reaching a fixed distance. The threshold ")
        TEXT("and the ray count printed here were read back from the predicate this run. WHAT THIS ")
        TEXT("SHAPE OF TEST CANNOT SEE, by construction and not by observation: anything that blocks ")
        TEXT("beyond one ray's reach, anything above or below the ray height, and any gap that falls ")
        TEXT("between two bearings. The tested point is %+.0f m in Z from your own view origin. This is ")
        TEXT("the same function both placement paths call, but it is NOT called with the same arguments ")
        TEXT("here: this command hands it the pawn to ignore and the placement paths hand it nothing, ")
        TEXT("and the placement paths hand it a SETTLED location while this hands it an aimed impact. ")
        TEXT("So this is a statement about the terrain at the point you pointed at, not a prediction of ")
        TEXT("what a placement probe would return. WHETHER a placement path runs this gate at all is a ")
        TEXT("different question that this line does not answer -- the gate-reached counters on the ")
        TEXT("census lines are what do. It states no cause: each ray reports the trace it made and ")
        TEXT("nothing about why the world is shaped that way. A ray count of zero above means the ")
        TEXT("predicate cast no rays at all and the verdict is not a measurement of this spot."),
        *TestAt.ToCompactString(), Blocked, Total, Threshold,
        bEnclosed ? TEXT("ENCLOSED (this predicate, called with this command's exclusion, refuses this point)")
                  : TEXT("not enclosed (this predicate, called with this command's exclusion, does not refuse this point)"),
        (TestAt.Z - ViewLoc.Z) / 100.0);

    // SLOPE + CLIFF VERDICT at the aimed point, mirroring NodeShuffle.Here's line so the two commands'
    // output compares line for line. This runs the ground trace the enclosure call above deliberately
    // did NOT use, and the line says so in full.
    {
        FVector SlopeLoc = FVector::ZeroVector;
        FRotator SlopeRot = FRotator::ZeroRotator;
        bool bSlopeWater = false;
        bool bSlopeCliff = false;
        FVector SlopeN = FVector::UpVector;
        const bool bHaveSettled = RaycastGroundAt(TestAt, static_cast<float>(TestAt.Z), Pawn, nullptr,
                                                  SlopeLoc, SlopeRot, bSlopeWater,
                                                  /*bShortTrace=*/false, &bSlopeCliff, &SlopeN);
        if (bHaveSettled)
        {
            const float SlopeDeg = FMath::RadiansToDegrees(
                FMath::Acos(FMath::Clamp(static_cast<float>(SlopeN.Z), -1.0f, 1.0f)));
            UE_LOG(LogNodeShuffle, Display,
                TEXT("POINTAT: ground slope where a long downward trace from the aimed point landed = ")
                TEXT("%.1f deg -- cliff gate (%.0f deg) %s; water %d. THAT LANDING POINT IS %s, WHICH IS ")
                TEXT("%+.0f m in Z FROM THE AIMED POINT, AND IT WAS NOT THE POINT GIVEN TO THE ENCLOSURE ")
                TEXT("PREDICATE ABOVE. It is reported only so this command's output lines up with ")
                TEXT("NodeShuffle.Here's, which does hand its enclosure call a settled point. This line ")
                TEXT("does not explain the difference between the two points."),
                SlopeDeg, GetCliffSlopeDegForDiag(),
                bSlopeCliff ? TEXT("WOULD REJECT settles there") : TEXT("accepts settles there"),
                bSlopeWater ? 1 : 0,
                *SlopeLoc.ToCompactString(), (SlopeLoc.Z - TestAt.Z) / 100.0);
        }
        else
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("POINTAT: a long downward ground trace from the aimed point found nothing to settle ")
                TEXT("on, so there is no slope figure and no cliff-gate verdict for this invocation. ")
                TEXT("The enclosure verdict above is unaffected: it was given the aimed point and never ")
                TEXT("this trace's result. This line reports that the trace returned nothing; it does ")
                TEXT("not say why."));
        }
    }
}
