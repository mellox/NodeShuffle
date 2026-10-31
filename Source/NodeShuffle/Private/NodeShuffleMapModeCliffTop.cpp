// rwg-clifftop-1 (2026-10-30) -- a gentle SOLID-mesh top is accepted as ground on RWG.
// User decision 1 (2026-10-30; reverses the 2026-10-18 P2d stop for the gentle case only): "There is nothing wrong with a node on top of a
// cliff." RaycastGroundAt's primary terrain-only trace that STOPS on a class-4 solid mesh (rock/cliff by mesh path) offers that stop here only
// while a caller has armed it (RwgSolidTopArm): RwgResolveAtSpawn around its RaycastSettle (primary AND spiral probes) and RwgLocalRedraw
// around its candidate traces. Every other trace (wells, resettle, rock pass, overlap nudge, probes, SettleNewNodesNearPlayers, caves) sees
// bRwgSolidTopArmed clear and keeps the P2d refusal byte-for-byte. Accepted only when ALL of these hold, each measured on THIS hit:
//   inside-start guard: Hit.bStartPenetrating is false; ImpactNormal.Z > 0; ImpactPoint.Z < trace start - RwgSolidTopStartMarginCm.
//   footprint: each of the 4 outer probes (RingCm = the terrain path's own smoothing ring, 3.5 m) first stops on the SAME component and
//     instance item as the hit. A miss, terrain, a buildable or another solid fails it: a cliff edge is never averaged into a plateau.
//   gentle: the smoothed normal (hit + the 4 outer normals, the terrain path's smoothing) has Z >= MinNormalZ (the same 60 deg cliff gate).
// On accept the node's rotation uses the terrain path's tilt clamp; water is the point-in-volume test plus a skipped water sheet; the water
// grid is NOT taught (a solid top says nothing about the terrain column). Log lines report measured values and the predicate, never a cause.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Buildables/FGBuildable.h"

namespace
{
    constexpr int32 RwgCliffTopSampleCap = 8; // RWGCLIFFTOP lines per spawn pass (counter zeroed with the PLACECENSUS counters)
    FString RwgCliffTopShort(const FString& P) { int32 Dot = INDEX_NONE; return P.FindLastChar(TEXT('.'), Dot) ? P.Mid(Dot + 1) : P; }
}

void ANodeShuffleSubsystem::RwgSolidTopArm(const FNodeShuffleEntry& Entry, const TCHAR* Via)
{
    bRwgSolidTopArmed = true;
    RwgSolidTopGuid = Entry.EntryGuid;
    RwgSolidTopRes = Entry.AssignedResourceClassPath;
    RwgSolidTopVia = Via;
}

bool ANodeShuffleSubsystem::RwgSolidTopTryAccept(const FHitResult& Hit, double TraceStartZ, const FCollisionQueryParams& Params, float MinNormalZ,
                                                 float RingCm, float TiltClampDeg, bool bSkippedWater, FVector& OutLoc, FRotator& InOutRot,
                                                 bool& bOutWater, FVector* OutGroundNormal, FHitResult* OutGroundHit) const
{
    UWorld* World = GetWorld();
    const FVector P = Hit.ImpactPoint;
    // Inside-start guard, three predicates measured on this hit.
    const bool bStartPen = Hit.bStartPenetrating;
    const bool bNormalUp = Hit.ImpactNormal.Z > 0.0;
    const bool bBelowStart = P.Z < TraceStartZ - NodeShuffleMapMode::RwgSolidTopStartMarginCm;
    const bool bGuard = !bStartPen && bNormalUp && bBelowStart;
    // Footprint + smoothing: 4 outer probes over the terrain path's window (+4 m / -6 m around the hit). Canopy, water sheets and other
    // skip-class hits are stepped through as the terrain-only trace does; terrain, a buildable or a solid mesh stops the probe.
    const UPrimitiveComponent* HitComp = Hit.GetComponent();
    FCollisionQueryParams RingParams = Params;
    FVector Acc = Hit.ImpactNormal;
    int32 SameMesh = 0;
    for (int32 I = 0; I < 4 && World; ++I)
    {
        const float Ang = PI * 0.5f * static_cast<float>(I);
        const FVector S(P.X + RingCm * FMath::Cos(Ang), P.Y + RingCm * FMath::Sin(Ang), P.Z + 400.0);
        const FVector En(S.X, S.Y, P.Z - 600.0);
        FHitResult R;
        bool bStopped = false;
        for (int32 K = 0; K <= NodeShuffleMapMode::RwgTerrainSkipMax; ++K)
        {
            if (!World->LineTraceSingleByChannel(R, S, En, ECC_WorldStatic, RingParams)) { break; }
            const int32 Cls = RwgHitClassOf(R);
            const AActor* Owner = R.GetActor();
            if (Cls == 0 || Cls == 4 || (Owner && (Owner->IsA<AFGBuildable>() || Owner->GetClass()->GetName().Contains(TEXT("AbstractInstanceManager")))))
            {
                bStopped = true;
                break;
            }
            RingParams.AddIgnoredComponent(R.GetComponent());
        }
        if (bStopped && HitComp && R.GetComponent() == HitComp && R.Item == Hit.Item) { ++SameMesh; Acc += R.ImpactNormal; }
    }
    const bool bFootprint = SameMesh == 4; // mixed-class footprint fails gentle
    const FVector N = Acc.GetSafeNormal(SMALL_NUMBER, FVector::UpVector);
    const bool bGentle = N.Z >= MinNormalZ;
    const TCHAR* Reject = TEXT("none");
    if (!bGuard)
    {
        ++RwgPcSolidTopRejStartInside;
        Reject = bStartPen ? TEXT("startPenetrating") : (!bNormalUp ? TEXT("impactNormalNotUp") : TEXT("impactNotBelowStart"));
    }
    else if (!bFootprint) { ++RwgPcSolidTopRejFootprint; Reject = TEXT("footprint"); }
    else if (!bGentle) { ++RwgPcSolidTopRejNormal; Reject = TEXT("normal"); }
    const bool bAccepted = bGuard && bFootprint && bGentle;
    if (RwgCliffTopSamples < RwgCliffTopSampleCap && FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        ++RwgCliffTopSamples;
        const NodeShuffleMapMode::FRwgGroundQuery Q(World, RwgGenerator.Get());
        const double ApiZ = Q.IsUsable() ? Q.SurfaceZ(P.X, P.Y) : NodeShuffleMapMode::RwgNaN();
        const UStaticMeshComponent* Smc = Cast<UStaticMeshComponent>(HitComp);
        const UStaticMesh* Mesh = Smc ? Smc->GetStaticMesh() : nullptr;
        const FString MeshName = Mesh ? Mesh->GetName() : (HitComp ? HitComp->GetName() : FString(TEXT("<none>")));
        const double RiseRun = N.Z > 1e-6 ? FMath::Sqrt(N.X * N.X + N.Y * N.Y) / N.Z : 999.0;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RWGCLIFFTOP: sample=%d via=%s entry=%s res=%s hitMesh=%s smoothedRiseRun=%.2f hitZ=%.0f apiZ=%.0f dzCm=%.0f startZ=%.0f startPenetrating=%d normalZ=%.3f smoothedNormalZ=%.3f gateNormalZ=%.3f outerProbesSameMesh=%d/4 accepted=%d rejectReason=%s"),
            RwgCliffTopSamples, RwgSolidTopVia, *RwgSolidTopGuid.ToString().Left(8), *RwgCliffTopShort(RwgSolidTopRes), *MeshName, RiseRun, P.Z, ApiZ,
            P.Z - ApiZ, TraceStartZ, bStartPen ? 1 : 0, Hit.ImpactNormal.Z, N.Z, MinNormalZ, SameMesh, bAccepted ? 1 : 0, Reject);
    }
    if (!bAccepted) { return false; }
    // Accepted: the terrain path's outputs, from this hit.
    bRwgLastProbeTopAccepted = true;
    ++RwgPcSolidTopAccepted;
    if (OutGroundHit) { *OutGroundHit = Hit; }
    OutLoc = P;
    if (OutGroundNormal) { *OutGroundNormal = N; }
    FVector AlignN = N;
    const float TiltRad = FMath::Acos(FMath::Clamp(static_cast<float>(N.Z), -1.0f, 1.0f));
    if (TiltRad > FMath::DegreesToRadians(TiltClampDeg))
    {
        const FVector Axis = FVector::CrossProduct(FVector::UpVector, N).GetSafeNormal(SMALL_NUMBER, FVector::XAxisVector);
        AlignN = FVector::UpVector.RotateAngleAxis(TiltClampDeg, Axis);
    }
    const FQuat AlignQuat = FQuat::FindBetweenNormals(FVector::UpVector, AlignN);
    InOutRot = (AlignQuat * FQuat(FRotator(0.f, InOutRot.Yaw, 0.f))).Rotator();
    bOutWater = bSkippedWater || IsPointInWater(OutLoc);
    return true;
}
