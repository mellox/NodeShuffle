// ns-rocks-1 (2026-11-14) -- rock near search bookkeeping (A), the sunken solid-top column test (rule B) and the session censuses (C).
// Handoff: _team/test-logs/2026-11-14-nodeshuffle-rocks-1-handoff.md. Constants: NodeShuffleRocks.h (all design inputs).
// Reached only from RWG code: RaycastGroundAt's terrain-only trace (first-hit capture), RwgSolidTopTryAccept (rule B, armed callers only),
// RwgResolveAtSpawn (settle records) and the RWG spawn pass's PLACECENSUS block (census). Plain members, never saved.
// Log lines report what was measured and which predicate branched, never a cause.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffleRocks.h"
#include "NodeShuffle.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Buildables/FGBuildable.h"

namespace
{
    const TCHAR* const RockKindNames[9] = { TEXT("terrain"), TEXT("canopy"), TEXT("water"), TEXT("other"),
                                            TEXT("boulder"), TEXT("cliff"), TEXT("stone"), TEXT("rock"), TEXT("largeFlat") };
    const TCHAR* RockClassName(int32 C)
    {
        return C == 0 ? TEXT("terrain") : C == 1 ? TEXT("canopy") : C == 2 ? TEXT("water") : C == 4 ? TEXT("solid") : C < 0 ? TEXT("none") : TEXT("other");
    }
    int32 WidthBin(float M) { return M < 2.f ? 0 : M < 4.f ? 1 : M < 7.f ? 2 : M < 15.f ? 3 : M < 30.f ? 4 : M < 60.f ? 5 : 6; }
    int32 MoveBin(float M) { return M < 5.f ? 0 : M < 10.f ? 1 : M < 20.f ? 2 : M < 40.f ? 3 : M < 80.f ? 4 : M < 160.f ? 5 : 6; }
    int32 DepthBin(double Cm) { return Cm < 1000.0 ? 0 : Cm < 3000.0 ? 1 : Cm < 6000.0 ? 2 : 3; }
}

bool ANodeShuffleSubsystem::RwgRocksSampleSlot() const
{
    if (!FNodeShuffleModule::AreDiagnosticsEnabled()) { return false; }
    if (RwgRkSamplePass != RwgPcPasses) { RwgRkSamplePass = RwgPcPasses; RwgRkSamples = 0; }
    if (RwgRkSamples >= NodeShuffleRocks::SampleCapPerPass) { return false; }
    ++RwgRkSamples;
    return true;
}

void ANodeShuffleSubsystem::RwgRocksNoteFirstHit(const FHitResult& H) const
{
    // The FIRST blocking hit of RaycastSettle's primary trace (before any skip): its kind, and for a solid family the mesh name and the
    // per-INSTANCE world bounding box (instance transform applied for instanced meshes; the component's own bounds otherwise).
    const TCHAR* Rule = TEXT("");
    const int32 Cls = RwgHitClassOf(H, &Rule);
    RwgRocksLastKind = Cls == 0 ? 0 : Cls == 1 ? 1 : Cls == 2 ? 2 : Cls == 4 ? 8 : 3;
    if (Cls != 4) { return; }
    const UStaticMeshComponent* Smc = Cast<UStaticMeshComponent>(H.GetComponent());
    const UStaticMesh* Mesh = Smc ? Smc->GetStaticMesh() : nullptr;
    const FString Path = Mesh ? Mesh->GetPathName() : FString();
    // Family by mesh path, tested in this order (a path naming two families takes the first): boulder, cliff, stone, rock; a solid with
    // none of them is the classifier's large-flat unknown mesh.
    RwgRocksLastKind = Path.Contains(TEXT("/Boulder/")) ? 4 : Path.Contains(TEXT("/Cliff/")) ? 5 : Path.Contains(TEXT("/Stone/")) ? 6
                     : Path.Contains(TEXT("/Environment/Rock/")) ? 7 : 8;
    RwgRocksLastMesh = Mesh ? Mesh->GetName() : (H.GetComponent() ? H.GetComponent()->GetName() : FString(TEXT("<none>")));
    FBox Box(ForceInit);
    if (Mesh && Smc->GetClass()->GetName().Contains(TEXT("InstancedStaticMesh")))
    {
        // By class NAME, as RwgHitClassOf does; static_cast then reads the public instance array (no new UClass query).
        const UInstancedStaticMeshComponent* Ism = static_cast<const UInstancedStaticMeshComponent*>(Smc);
        if (Ism->PerInstanceSMData.IsValidIndex(H.Item))
        {
            const FMatrix InstanceToWorld = Ism->PerInstanceSMData[H.Item].Transform * Ism->GetComponentTransform().ToMatrixWithScale();
            Box = Mesh->GetBoundingBox().TransformBy(InstanceToWorld);
        }
    }
    else if (Smc)
    {
        Box = Smc->Bounds.GetBox();
    }
    if (Box.IsValid)
    {
        const FVector Size = Box.GetSize(); // FULL extents of the world-aligned box, cm (a rotated mesh reads larger than its own sides)
        RwgRocksLastWidthM = static_cast<float>(FMath::Max(Size.X, Size.Y) / 100.0);
        RwgRocksLastHeightM = static_cast<float>(Size.Z / 100.0);
    }
}

FString ANodeShuffleSubsystem::RwgRocksLastFirstHitText() const
{
    if (RwgRocksLastKind < 0) { return FString(TEXT("none")); }
    if (RwgRocksLastKind < 4) { return FString(RockKindNames[RwgRocksLastKind]); }
    return FString::Printf(TEXT("%s:%s:w%.1fm:h%.1fm"), RockKindNames[RwgRocksLastKind], *RwgRocksLastMesh, RwgRocksLastWidthM, RwgRocksLastHeightM);
}

const TCHAR* ANodeShuffleSubsystem::RwgRocksLastMoveName() const
{
    return RwgRocksLastMove == 1 ? TEXT("near") : RwgRocksLastMove == 2 ? TEXT("spiral") : TEXT("none");
}

void ANodeShuffleSubsystem::RwgRocksRecordSettle(bool bDecided, bool bCompleted)
{
    if (bCompleted)
    {
        // C2: a sideways move the resolve KEPT (RwgResolveAtSpawn's success return); moves later refused there are not counted.
        if (RwgRocksLastMove == 0) { return; }
        if (RwgRocksLastMove == 1) { ++RwgRkMoveNear; } else if (bRwgRocksLastSpiralAfterRock) { ++RwgRkMoveSpiralRock; } else { ++RwgRkMoveSpiralOther; }
        ++RwgRkMoveBins[MoveBin(RwgRocksLastMoveCm / 100.0f)];
        return;
    }
    // C1: one count per DECIDED settle (settled, or unplaceable -> redeal); a settle with no hit defers and is retried every pass,
    // so it is counted apart and never binned.
    if (!bDecided || RwgRocksLastKind < 0) { ++RwgRkDeferredNotBinned; return; }
    ++RwgRkFirstKind[RwgRocksLastKind];
    if (RwgRocksLastKind < 4) { return; }
    const int32 Fam = RwgRocksLastKind - 4;
    ++RwgRkWidthBins[Fam][WidthBin(RwgRocksLastWidthM)];
    RwgRkWidthMaxM[Fam] = FMath::Max(RwgRkWidthMaxM[Fam], RwgRocksLastWidthM);
    FIntPoint& Row = RwgRkMeshCounts.FindOrAdd(RwgRocksLastMesh);
    ++Row.X;
    Row.Y = FMath::Max(Row.Y, FMath::RoundToInt(RwgRocksLastWidthM * 100.0f));
}

bool ANodeShuffleSubsystem::RwgRocksSunkenRefuses(const FHitResult& Hit, double TraceStartZ, const FCollisionQueryParams& Params, float RingCm) const
{
    // Rule B. Population: solid tops that passed every existing RwgSolidTopTryAccept gate. Applies only when the top lies more than
    // SunkenMarginCm below RWG's API surface at the hit XY. Then each of the 4 footprint columns (same ring, same bearings as the
    // footprint) is traced from the PRIMARY TRACE START down, stepping through skip-class hits as the footprint does; a column passes
    // only when its first stop is the SAME component and instance item, no higher than SunkenColumnTolCm above the hit. Any column that
    // stops on terrain, another solid, a buildable, higher on the same mesh, starts inside geometry, or finds no stop refuses the top.
    UWorld* World = GetWorld();
    if (!World) { return false; }
    const FVector P = Hit.ImpactPoint;
    const NodeShuffleMapMode::FRwgGroundQuery Q(World, RwgGenerator.Get());
    const double ApiZ = Q.IsUsable() ? Q.SurfaceZ(P.X, P.Y) : NodeShuffleMapMode::RwgNaN();
    if (!FMath::IsFinite(ApiZ)) { ++RwgRkSunkApiUnknown; return false; }
    const double DepthCm = ApiZ - P.Z;
    if (DepthCm <= NodeShuffleRocks::SunkenMarginCm) { return false; }
    const int32 Bin = DepthBin(DepthCm);
    ++RwgRkSunkTested; ++RwgRkSunkBins[Bin];
    const UPrimitiveComponent* HitComp = Hit.GetComponent();
    FCollisionQueryParams ColParams = Params;
    int32 Blocked = 0, StartInside = 0, NoStop = 0, Skipped = 0, TopClass = -1, SkipClass = -1;
    double TopAboveCm = NodeShuffleMapMode::RwgNaN(), SkipZ = NodeShuffleMapMode::RwgNaN();
    for (int32 I = 0; I < 4; ++I)
    {
        const float Ang = PI * 0.5f * static_cast<float>(I);
        const FVector S(P.X + RingCm * FMath::Cos(Ang), P.Y + RingCm * FMath::Sin(Ang), TraceStartZ);
        const FVector En(S.X, S.Y, P.Z - 600.0);
        FHitResult R;
        bool bStopped = false;
        for (int32 K = 0; K <= NodeShuffleMapMode::RwgTerrainSkipMax; ++K)
        {
            if (!World->LineTraceSingleByChannel(R, S, En, ECC_WorldStatic, ColParams)) { break; }
            if (R.bStartPenetrating) { bStopped = true; break; }
            const int32 Cls = RwgHitClassOf(R);
            const AActor* StopOwner = R.GetActor();
            if (Cls == 0 || Cls == 4 || (StopOwner && (StopOwner->IsA<AFGBuildable>() || StopOwner->GetClass()->GetName().Contains(TEXT("AbstractInstanceManager")))))
            {
                bStopped = true;
                break;
            }
            ++Skipped;
            if (!FMath::IsFinite(SkipZ) || R.ImpactPoint.Z > SkipZ) { SkipZ = R.ImpactPoint.Z; SkipClass = Cls; }
            ColParams.AddIgnoredComponent(R.GetComponent());
        }
        if (!bStopped) { ++NoStop; ++Blocked; continue; }
        if (R.bStartPenetrating) { ++StartInside; ++Blocked; continue; }
        const double AboveCm = R.ImpactPoint.Z - P.Z;
        if (!FMath::IsFinite(TopAboveCm) || AboveCm > TopAboveCm) { TopAboveCm = AboveCm; TopClass = RwgHitClassOf(R); }
        const bool bSame = HitComp && R.GetComponent() == HitComp && R.Item == Hit.Item && AboveCm <= NodeShuffleRocks::SunkenColumnTolCm;
        if (!bSame) { ++Blocked; }
    }
    const bool bRefused = Blocked > 0;
    if (bRefused) { ++RwgRkSunkRefused; ++RwgRkSunkRefusedBins[Bin]; }
    if (RwgRocksSampleSlot())
    {
        // terrainBelowHitZ: a trace from just under the hit down 400 m that steps through every non-terrain stop (the hit's own mesh
        // included), so it reads where terrain collision is under this XY; nan = none found within the skip cap.
        double TerrainBelowZ = NodeShuffleMapMode::RwgNaN();
        FCollisionQueryParams DownParams = Params;
        FHitResult D;
        for (int32 K = 0; K <= NodeShuffleMapMode::RwgTerrainSkipMax; ++K)
        {
            if (!World->LineTraceSingleByChannel(D, FVector(P.X, P.Y, P.Z - 50.0), FVector(P.X, P.Y, P.Z - 40000.0), ECC_WorldStatic, DownParams)) { break; }
            if (RwgHitClassOf(D) == 0) { TerrainBelowZ = D.ImpactPoint.Z; break; }
            DownParams.AddIgnoredComponent(D.GetComponent());
        }
        const UStaticMeshComponent* Smc = Cast<UStaticMeshComponent>(HitComp);
        const UStaticMesh* Mesh = Smc ? Smc->GetStaticMesh() : nullptr;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("ROCKSUNKEN: entry=%s via=%s at=%.0f/%.0f hitMesh=%s hitZ=%.0f apiZ=%.0f depthBelowApiM=%.1f columnsBlocked=%d/4 columnsStartInside=%d columnsNoStop=%d highestFirstStopAboveHitM=%.1f highestFirstStopClass=%s skippedHits=%d highestSkippedZ=%.0f highestSkippedClass=%s terrainBelowHitZ=%.0f refused=%d"),
            *RwgSolidTopGuid.ToString().Left(8), RwgSolidTopVia, P.X, P.Y, Mesh ? *Mesh->GetName() : TEXT("<none>"), P.Z, ApiZ, DepthCm / 100.0,
            Blocked, StartInside, NoStop, TopAboveCm / 100.0, RockClassName(TopClass), Skipped, SkipZ, RockClassName(SkipClass), TerrainBelowZ, bRefused ? 1 : 0);
    }
    return bRefused;
}

void ANodeShuffleSubsystem::RwgRocksEmitCensus() const
{
    // Session totals since this world load (not reset by a roll), printed on the PLACECENSUS cadence. RWG settles through
    // RwgResolveAtSpawn only (the spawn path and the provisional settle).
    const int32* K = RwgRkFirstKind;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("ROCKFIRSTHIT: session first blocking hit of each decided settle's primary trace; decided=%d notBinned=%d terrain=%d canopy=%d water=%d other=%d boulder=%d cliff=%d stone=%d rock=%d largeFlat=%d"),
        K[0] + K[1] + K[2] + K[3] + K[4] + K[5] + K[6] + K[7] + K[8], RwgRkDeferredNotBinned, K[0], K[1], K[2], K[3], K[4], K[5], K[6], K[7], K[8]);
    for (int32 Fam = 0; Fam < 5; ++Fam)
    {
        const int32* B = RwgRkWidthBins[Fam];
        const int32 N = B[0] + B[1] + B[2] + B[3] + B[4] + B[5] + B[6];
        if (N == 0) { continue; }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("ROCKSIZE: family=%s settles=%d widthLt2m=%d width2to4m=%d width4to7m=%d width7to15m=%d width15to30m=%d width30to60m=%d widthGe60m=%d maxWidthM=%.1f (width: largest horizontal full side of the hit instance's world-aligned box)"),
            RockKindNames[Fam + 4], N, B[0], B[1], B[2], B[3], B[4], B[5], B[6], RwgRkWidthMaxM[Fam]);
    }
    if (RwgRkMeshCounts.Num() > 0)
    {
        TArray<TPair<FString, FIntPoint>> Rows;
        for (const TPair<FString, FIntPoint>& KV : RwgRkMeshCounts) { Rows.Emplace(KV.Key, KV.Value); }
        Rows.Sort([](const TPair<FString, FIntPoint>& A, const TPair<FString, FIntPoint>& B) { return A.Value.X != B.Value.X ? A.Value.X > B.Value.X : A.Key < B.Key; });
        FString Line;
        for (int32 I = 0; I < Rows.Num() && I < NodeShuffleRocks::MeshTopN; ++I)
        {
            Line += FString::Printf(TEXT(" %s %d/%.1fm"), *Rows[I].Key, Rows[I].Value.X, Rows[I].Value.Y / 100.0f);
        }
        UE_LOG(LogNodeShuffle, Display, TEXT("ROCKSIZE-MESH: meshNames=%d shown=%d (each: mesh name, settles, largest width):%s"),
            Rows.Num(), FMath::Min(Rows.Num(), NodeShuffleRocks::MeshTopN), *Line);
    }
    const int32* M = RwgRkMoveBins;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("ROCKMOVE: session sideways moves kept by completed settles; total=%d near=%d spiralAfterRock=%d spiralAfterWaterOrSteep=%d lt5m=%d 5to10m=%d 10to20m=%d 20to40m=%d 40to80m=%d 80to160m=%d ge160m=%d | near searches run=%d foundNear=%d exhaustedToSpiral=%d spiralFoundAfterNear=%d noSpotAfterNear=%d"),
        RwgRkMoveNear + RwgRkMoveSpiralRock + RwgRkMoveSpiralOther, RwgRkMoveNear, RwgRkMoveSpiralRock, RwgRkMoveSpiralOther,
        M[0], M[1], M[2], M[3], M[4], M[5], M[6], RwgRkNearRun, RwgRkNearFound, RwgRkNearToSpiral, RwgRkSpiralAfterNear, RwgRkNoSpotAfterNear);
    UE_LOG(LogNodeShuffle, Display,
        TEXT("ROCKSUNKEN-CENSUS: session solid-top probes that passed every earlier gate and lie more than %.0f m below the API surface; tested=%d refused=%d kept=%d apiUnknown=%d depth3to10m=%d depth10to30m=%d depth30to60m=%d depthGe60m=%d refused3to10m=%d refused10to30m=%d refused30to60m=%d refusedGe60m=%d"),
        NodeShuffleRocks::SunkenMarginCm / 100.0, RwgRkSunkTested, RwgRkSunkRefused, RwgRkSunkTested - RwgRkSunkRefused, RwgRkSunkApiUnknown,
        RwgRkSunkBins[0], RwgRkSunkBins[1], RwgRkSunkBins[2], RwgRkSunkBins[3],
        RwgRkSunkRefusedBins[0], RwgRkSunkRefusedBins[1], RwgRkSunkRefusedBins[2], RwgRkSunkRefusedBins[3]);
}
