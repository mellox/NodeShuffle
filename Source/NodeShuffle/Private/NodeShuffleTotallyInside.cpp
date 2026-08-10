// ns-t53-totallyinside (2026-08-10): the queries. See NodeShuffleTotallyInside.h for why this instrument
// is shaped the way it is and why nothing gates on it.
//
// WHAT IT DOES NOT INHERIT, DELIBERATELY. The shipped enclosure predicate casts its rays from 200 cm
// ABOVE the point it is handed (NodeShuffleWellFootprint.cpp), and that offset is a prime suspect for a
// member in a cliff face reading 0 of 8 blocked (docs/TECH-DEBT.md T41). Every segment here begins a
// SMALL FIXED DISTANCE from the tested point ALONG ITS OWN RAY'S DIRECTION -- not 200 cm above it, and
// not at the point itself. The first cut of this file did use the tested point as an endpoint, and the
// 2026-08-10 run showed why that cannot stand: a probe origin sitting exactly ON a surface reads
// contact at 0 cm in every direction at once, so plain open ground with sky above read TOTALLY INSIDE
// (all 14 rays solid, RAY CLEAR zero times in 140 readings). The start offset is TIRayStartEpsilonCm
// below, with the measured basis for its value. No constant below is read from that predicate, from
// the centre-shadow walk, or from any placement path, so none of them can drift into this one.
//
// THE CENTRE-SHADOW WALK IN NodeShuffleCentreContainment.cpp IS UNTOUCHED BY THIS FILE. It is still the
// subject of the comparison and nothing here reads, calls, or modifies it.

#include "NodeShuffleTotallyInside.h"

#include "NodeShuffle.h"

#include "Buildables/FGBuildable.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

namespace
{
    // How far each direction is probed from the tested point. CHOSEN, not derived: it is the same
    // magnitude as the footprint radius this project already treats as one node's own neighbourhood, so
    // a reader comparing this instrument with the shipped gate is not also comparing two different
    // reaches. It is printed back from this constant on every reading, and each reading also reports the
    // furthest distance any blocked direction had to go, so how sensitive a verdict is to this number is
    // visible rather than argued.
    constexpr float TIProbeReachCm = 500.0f;

    // Where each ray STARTS: this far from the tested point, along that ray's own direction. Zero was
    // measured wrong on 2026-08-10: an origin exactly ON a surface read contact at 0 cm on all 14 rays,
    // so open ground with sky above read TOTALLY INSIDE. The value is picked from that run's printed
    // hit distances, not from taste. Of the 282 kept readings across the 10 points, 250 named
    // LandscapeHeightfieldCollisionComponent on ONE actor (LandscapeStreamingProxy_CI1M39Z1I4): 226 of
    // those lay under 25 cm and 7 lay between 25 and 73 cm. Every reading naming anything else was an
    // FGCliffActor StaticMeshComponent at 87 cm or more. NOTHING IN THAT LOG SAYS WHICH LANDSCAPE
    // READING CAME FROM THE SURFACE THE POINT ITSELF SAT ON -- they all name the same actor -- so 25 cm
    // is where the bulk of the landscape readings stop, not a proven artefact boundary, and the 7 above
    // it are not removed by it. It is under every reading that named anything but the landscape by a
    // factor of three. It is also 5 sweep-radii, so on any ray leaving that surface steeply the inward
    // sweep's end sphere clears it; on a ray running PARALLEL to it -- the four horizontals on level
    // ground -- that sphere is still centred on the surface and can still touch it, which this offset
    // does not fix and does not claim to. Distances in every reading are still measured from the
    // tested point, so readings before and after this constant existed compare directly.
    constexpr float TIRayStartEpsilonCm = 25.0f;

    // The sweep's radius. Small on purpose: its job is to report solid where a line query starting inside
    // a body reports nothing, not to fatten the probe. A sweep of any radius can report blocked where a
    // line is clear, which is why it is one of four instruments and never the only one.
    constexpr float TISweepRadiusCm = 5.0f;

    // The overlap radius, at the tested point and at each direction's outer point.
    constexpr float TIOverlapRadiusCm = 1.0f;

    // Most times one query may be re-run to get past excluded actors. One distinct excluded actor costs
    // one re-run, the way ns-t51-ignorelist does it, so this bounds distinct excluded actors per query.
    constexpr int32 TIRetraceCap = 8;

    // The probed directions: the six axes -- INCLUDING STRAIGHT UP AND STRAIGHT DOWN, which every prior
    // attempt in this project lacked -- and the eight corner diagonals. Fourteen in total.
    struct FTIDirDef { const TCHAR* Label; double X; double Y; double Z; };
    const FTIDirDef TIDirections[] =
    {
        { TEXT("horizontal, along +X"),           1.0,  0.0,  0.0 },
        { TEXT("horizontal, along -X"),          -1.0,  0.0,  0.0 },
        { TEXT("horizontal, along +Y"),           0.0,  1.0,  0.0 },
        { TEXT("horizontal, along -Y"),           0.0, -1.0,  0.0 },
        { TEXT("straight up"),                    0.0,  0.0,  1.0 },
        { TEXT("straight down"),                  0.0,  0.0, -1.0 },
        { TEXT("corner diagonal, up and +X +Y"),  1.0,  1.0,  1.0 },
        { TEXT("corner diagonal, up and +X -Y"),  1.0, -1.0,  1.0 },
        { TEXT("corner diagonal, up and -X +Y"), -1.0,  1.0,  1.0 },
        { TEXT("corner diagonal, up and -X -Y"), -1.0, -1.0,  1.0 },
        { TEXT("corner diagonal, down and +X +Y"),  1.0,  1.0, -1.0 },
        { TEXT("corner diagonal, down and +X -Y"),  1.0, -1.0, -1.0 },
        { TEXT("corner diagonal, down and -X +Y"), -1.0,  1.0, -1.0 },
        { TEXT("corner diagonal, down and -X -Y"), -1.0, -1.0, -1.0 },
    };

    // Names an actor and a component from one hit, or says plainly that the hit named none.
    FString TIWhatWasHit(const FHitResult& Hit)
    {
        const AActor* A = Hit.GetActor();
        const UPrimitiveComponent* C = Hit.GetComponent();
        return FString::Printf(
            TEXT("actor '%s' of class %s, component '%s' of class %s"),
            A ? *A->GetName() : TEXT("<this hit named no actor>"),
            A ? *A->GetClass()->GetName() : TEXT("<none, no actor>"),
            C ? *C->GetName() : TEXT("<this hit named no component>"),
            C ? *C->GetClass()->GetName() : TEXT("<none, no component>"));
    }

    enum class ETIQueryKind : uint8 { Line, Sweep };

    // ONE query, re-run past excluded actors the way ns-t51-ignorelist does it: the first time an
    // excluded actor is returned it is handed to this query's own ignore list and the query is re-run
    // FROM THE SAME TWO ENDPOINTS, so it costs one re-run in total however tall that actor is and the
    // probe can never step over geometry to get past it.
    //
    // Returns true only when a blocking result was returned by an actor none of the three exclusions
    // removed. Two conditions end it with false and are COUNTED rather than assumed away: a result on an
    // actor already on this query's ignore list, and a query that used its whole retrace budget. Both
    // make this instrument report nothing for that direction, which the union with the other three
    // instruments is there to absorb.
    bool TIQuery(UWorld* World, ETIQueryKind Kind, const FVector& From, const FVector& To,
                 const AActor* SubjectActor, FNodeShuffleTotallyInsideReading& Out, FHitResult& OutHit)
    {
        FCollisionQueryParams Params(FName(TEXT("NodeShuffleTotallyInside")), false);
        TSet<const AActor*> AlreadyIgnored;

        for (int32 Attempt = 0; Attempt < TIRetraceCap; ++Attempt)
        {
            FHitResult Hit;
            ++Out.QueriesMade;
            const bool bHit = (Kind == ETIQueryKind::Line)
                ? World->LineTraceSingleByChannel(Hit, From, To, ECC_WorldStatic, Params)
                : World->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_WorldStatic,
                                              FCollisionShape::MakeSphere(TISweepRadiusCm), Params);
            if (!bHit) { return false; }

            ++Out.HitsSeen;
            const AActor* A = Hit.GetActor();
            const bool bSubject = (A != nullptr) && (A == SubjectActor);
            const bool bPawn = (A != nullptr) && A->IsA<APawn>();
            const bool bBuildable = (A != nullptr) && A->IsA<AFGBuildable>();
            if (!bSubject && !bPawn && !bBuildable)
            {
                OutHit = Hit;
                return true;
            }

            if (AlreadyIgnored.Contains(A))
            {
                ++Out.IgnoredRehits;
                return false;
            }
            AlreadyIgnored.Add(A);
            Params.AddIgnoredActor(A);
            if (bSubject)        { ++Out.ExcludedSubjectHits; }
            else if (bPawn)      { ++Out.ExcludedPawnHits; }
            else                 { ++Out.ExcludedBuildableHits; }
        }

        ++Out.RetraceCapHits;
        return false;
    }

    // ONE overlap, with the same three exclusions applied to its results. An overlap consumes no retrace
    // budget: everything it returns is examined and the excluded ones are counted.
    bool TIOverlap(UWorld* World, const FVector& At, const AActor* SubjectActor,
                   FNodeShuffleTotallyInsideReading& Out,
                   int32& OutResults, int32& OutBlocking, FString& OutWhat)
    {
        TArray<FOverlapResult> Hits;
        FCollisionQueryParams Params(FName(TEXT("NodeShuffleTotallyInsideOverlap")), false);
        ++Out.OverlapsMade;
        World->OverlapMultiByChannel(Hits, At, FQuat::Identity, ECC_WorldStatic,
                                     FCollisionShape::MakeSphere(TIOverlapRadiusCm), Params);
        OutResults = Hits.Num();
        Out.OverlapResultsSeen += Hits.Num();
        OutBlocking = 0;
        for (const FOverlapResult& H : Hits)
        {
            if (!H.bBlockingHit) { continue; }
            const AActor* A = H.GetActor();
            if ((A != nullptr) && (A == SubjectActor)) { ++Out.ExcludedOverlapSubject; continue; }
            if ((A != nullptr) && A->IsA<APawn>())     { ++Out.ExcludedOverlapPawn; continue; }
            if ((A != nullptr) && A->IsA<AFGBuildable>()) { ++Out.ExcludedOverlapBuildable; continue; }
            ++OutBlocking;
            if (OutBlocking <= 3)
            {
                const UPrimitiveComponent* C = H.GetComponent();
                OutWhat += FString::Printf(TEXT("actor '%s' of class %s, component '%s' of class %s; "),
                    A ? *A->GetName() : TEXT("<this overlap named no actor>"),
                    A ? *A->GetClass()->GetName() : TEXT("<none, no actor>"),
                    C ? *C->GetName() : TEXT("<this overlap named no component>"),
                    C ? *C->GetClass()->GetName() : TEXT("<none, no component>"));
            }
        }
        if (OutBlocking == 0)
        {
            OutWhat = TEXT("<no blocking overlap left to name once the three exclusions were applied>");
        }
        return OutBlocking > 0;
    }
}

bool RunTotallyInsideProbe(UWorld* World, const FVector& At, const AActor* SubjectActor,
                           FNodeShuffleTotallyInsideReading& Out)
{
    Out = FNodeShuffleTotallyInsideReading();
    Out.Point = At;
    Out.ProbeReachCm = TIProbeReachCm;
    Out.RayStartEpsilonCm = TIRayStartEpsilonCm;
    Out.SweepRadiusCm = TISweepRadiusCm;
    Out.OverlapRadiusCm = TIOverlapRadiusCm;
    Out.RetraceCap = TIRetraceCap;
    Out.DirectionCount = static_cast<int32>(UE_ARRAY_COUNT(TIDirections));

    if (!World)
    {
        // bRan stays false with every count at zero. A caller must be able to tell "no query was made"
        // from "queries were made and found nothing".
        Out.CentreOverlapWhat = TEXT("<no query was made at all: this call was given no world>");
        return false;
    }

    for (int32 d = 0; d < Out.DirectionCount; ++d)
    {
        const FTIDirDef& Def = TIDirections[d];
        FNodeShuffleTotallyInsideRay Ray;
        Ray.Index = d + 1;
        Ray.Label = Def.Label;
        Ray.Unit = FVector(Def.X, Def.Y, Def.Z).GetSafeNormal();
        Ray.OuterPoint = At + Ray.Unit * TIProbeReachCm;

        // Where this ray's segments begin: TIRayStartEpsilonCm along its own direction, so the tested
        // point itself is an endpoint of no query. See that constant for the measured basis.
        const FVector RayStart = At + Ray.Unit * TIRayStartEpsilonCm;

        // A: from the ray start outward. This is the reading a query that starts inside a body has
        // been observed not to give in this project; it is run and reported anyway, because which
        // instrument fires is the thing this packet is trying to learn.
        {
            FHitResult Hit;
            if (TIQuery(World, ETIQueryKind::Line, RayStart, Ray.OuterPoint, SubjectActor, Out, Hit))
            {
                Ray.bOutwardBlocked = true;
                Ray.bOutwardStartPenetrating = Hit.bStartPenetrating;
                Ray.OutwardSolidAtCm = FVector::Dist(At, Hit.ImpactPoint);
                Ray.OutwardWhat = TIWhatWasHit(Hit);
            }
            else
            {
                Ray.OutwardWhat = TEXT("<this query returned no blocking result it could keep>");
            }
        }

        // B: from the outer point back to the ray start, so a surface between the two is met from the
        // side this engine has been observed to report.
        {
            FHitResult Hit;
            if (TIQuery(World, ETIQueryKind::Line, Ray.OuterPoint, RayStart, SubjectActor, Out, Hit))
            {
                Ray.bInwardBlocked = true;
                Ray.bInwardStartPenetrating = Hit.bStartPenetrating;
                Ray.InwardSolidAtCm = FVector::Dist(At, Hit.ImpactPoint);
                Ray.InwardWhat = TIWhatWasHit(Hit);
            }
            else
            {
                Ray.InwardWhat = TEXT("<this query returned no blocking result it could keep>");
            }
        }

        // C: the same inward segment as a sphere sweep. Its start-penetration flag is a statement about
        // the OUTER point being inside something, which is evidence FOR containment, not against it.
        {
            FHitResult Hit;
            if (TIQuery(World, ETIQueryKind::Sweep, Ray.OuterPoint, RayStart, SubjectActor, Out, Hit))
            {
                Ray.bSweepBlocked = true;
                Ray.bSweepStartPenetrating = Hit.bStartPenetrating;
                // A sweep that starts already overlapping reports an impact point that describes the
                // penetration rather than a position along the segment, so this claims only that solid
                // was met somewhere out to the reach and no distance closer than that.
                Ray.SweepSolidAtCm = Hit.bStartPenetrating ? static_cast<double>(TIProbeReachCm)
                                                           : FVector::Dist(At, Hit.ImpactPoint);
                Ray.SweepWhat = TIWhatWasHit(Hit);
            }
            else
            {
                Ray.SweepWhat = TEXT("<this query returned no blocking result it could keep>");
            }
        }

        // D: an overlap AT the outer point. Its YES is a real statement that the far end of this
        // direction is itself inside something; its NO is not evidence of open air there.
        Ray.bOuterOverlapSolid = TIOverlap(World, Ray.OuterPoint, SubjectActor, Out,
                                           Ray.OuterOverlapResults, Ray.OuterOverlapBlocking,
                                           Ray.OuterOverlapWhat);

        Ray.bBlocked = Ray.bOutwardBlocked || Ray.bInwardBlocked || Ray.bSweepBlocked
                     || Ray.bOuterOverlapSolid;

        // The closest distance any of the four put solid at, so the verdict's dependence on the reach
        // constant can be read off rather than assumed.
        auto Consider = [&Ray](bool bOn, double Cm)
        {
            if (!bOn || Cm < 0.0) { return; }
            if (Ray.NearestSolidAtCm < 0.0 || Cm < Ray.NearestSolidAtCm) { Ray.NearestSolidAtCm = Cm; }
        };
        Consider(Ray.bOutwardBlocked, Ray.OutwardSolidAtCm);
        Consider(Ray.bInwardBlocked, Ray.InwardSolidAtCm);
        Consider(Ray.bSweepBlocked, Ray.SweepSolidAtCm);
        Consider(Ray.bOuterOverlapSolid, static_cast<double>(TIProbeReachCm));

        if (Ray.bBlocked)
        {
            ++Out.BlockedDirections;
            if (Ray.bOutwardBlocked)      { ++Out.CarriedByOutward; }
            if (Ray.bInwardBlocked)       { ++Out.CarriedByInward; }
            if (Ray.bSweepBlocked)        { ++Out.CarriedBySweep; }
            if (Ray.bSweepStartPenetrating) { ++Out.CarriedBySweepStart; }
            if (Ray.bOuterOverlapSolid)   { ++Out.CarriedByOuterOverlap; }
            if (Ray.NearestSolidAtCm > Out.FurthestNearestSolidCm)
            {
                Out.FurthestNearestSolidCm = Ray.NearestSolidAtCm;
            }
            Ray.BlockedBy = FString::Printf(
                TEXT("%s%s%s%s"),
                Ray.bOutwardBlocked ? TEXT("the outward line query, ") : TEXT(""),
                Ray.bInwardBlocked ? TEXT("the inward line query, ") : TEXT(""),
                Ray.bSweepBlocked
                    ? (Ray.bSweepStartPenetrating
                        ? TEXT("the inward sweep, which reported it was already overlapping at its own ")
                          TEXT("start, ")
                        : TEXT("the inward sweep, "))
                    : TEXT(""),
                Ray.bOuterOverlapSolid ? TEXT("the overlap at the outer point, ") : TEXT(""));
        }
        else
        {
            ++Out.ClearDirections;
            Ray.BlockedBy = TEXT("<none of the four reported solid along or at the end of this ray>");
        }

        Out.Rays.Add(MoveTemp(Ray));
    }

    // The corroboration overlap at the tested point itself. POSITIVE-ONLY BY CONSTRUCTION: neither
    // outcome is read below, and the verdict on the next statement does not mention it.
    Out.bCentreOverlapSolid = TIOverlap(World, At, SubjectActor, Out,
                                        Out.CentreOverlapResults, Out.CentreOverlapBlocking,
                                        Out.CentreOverlapWhat);
    Out.bCentreOverlapRan = true;

    Out.bRan = true;
    Out.bTotallyInside = (Out.DirectionCount > 0) && (Out.BlockedDirections == Out.DirectionCount);
    return Out.bTotallyInside;
}
