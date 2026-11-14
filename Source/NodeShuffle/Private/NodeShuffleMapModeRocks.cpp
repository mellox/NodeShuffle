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
    int32 DepthBin(double Cm) { return Cm < 300.0 ? 0 : Cm < 1000.0 ? 1 : Cm < 3000.0 ? 2 : Cm < 6000.0 ? 3 : 4; } // cm below the API Z; the caller tests > SunkenMarginCm first
    int32 SettleDzBin(float Cm) { return Cm < -300.f ? 0 : Cm < -100.f ? 1 : Cm < -50.f ? 2 : Cm <= 50.f ? 3 : Cm <= 300.f ? 4 : 5; }
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
    if (Cls != 4)
    {
        // Round 2: non-solid first hits carry the component name (ROCKSETTLE); solids carry the mesh name below.
        RwgRocksLastMesh = H.GetComponent() ? H.GetComponent()->GetName() : FString(TEXT("<none>"));
        return;
    }
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

void ANodeShuffleSubsystem::RwgRocksRecordSettle(bool bDecided, bool bCompleted, float SettledDzCm, bool bOnSolidTop)
{
    if (bCompleted)
    {
        // C3: every completed RwgResolveAtSpawn settle, settled Z minus the API Z at the SETTLED XY, terrain and solid top apart.
        ++RwgRkSettleDzBins[bOnSolidTop ? 1 : 0][SettleDzBin(SettledDzCm)];
        // C2: a sideways move the resolve KEPT (its success return); moves later refused there are not counted.
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

int32 ANodeShuffleSubsystem::RwgRocksSunkenVerdict(const FHitResult& Hit, double TraceStartZ, const FCollisionQueryParams& Params, float RingCm) const
{
    // Rule B. Population: solid tops that passed every existing RwgSolidTopTryAccept gate. Tested only when the top lies more than
    // SunkenMarginCm below RWG's API surface AT THE TOP'S OWN XY (the XY being settled; never the dealt XY). Then, in order:
    //  1. terrain presence: a trace at the top's XY from the trace start down that steps through EVERY non-terrain stop (this rock
    //     included). No terrain stop -> 2 (defer: the caller sees no hit, as for unstreamed terrain). Terrain more than TerrainAboveTolCm
    //     ABOVE the top -> 1 (refuse).
    //  2. columns: each of the 4 footprint columns (same ring and bearings as the footprint) traced from the trace start down, stepping
    //     through skip-class hits as the footprint does; a column passes only when its first stop is the SAME component and instance
    //     item, no higher than SunkenColumnTolCm above the top. Any other stop, a start inside geometry or no stop -> 1 (refuse).
    // Returns 0 = keep. A ROCKTOP sample line (diagnostics on, capped per pass) is printed for any top reaching here, tested or not.
    UWorld* World = GetWorld();
    if (!World) { return 0; }
    const FVector P = Hit.ImpactPoint;
    const NodeShuffleMapMode::FRwgGroundQuery Q(World, RwgGenerator.Get());
    const double ApiZ = Q.IsUsable() ? Q.SurfaceZ(P.X, P.Y) : NodeShuffleMapMode::RwgNaN();
    const bool bApiKnown = FMath::IsFinite(ApiZ);
    if (!bApiKnown) { ++RwgRkSunkApiUnknown; }
    const double DepthCm = bApiKnown ? ApiZ - P.Z : NodeShuffleMapMode::RwgNaN();
    const bool bTest = bApiKnown && DepthCm > NodeShuffleRocks::SunkenMarginCm;
    const bool bSample = RwgRocksSampleSlot();
    // Round 2: the terrain trace below runs for EVERY top reaching here (tested or not), so ROCKSETTLE can print it for every rock-top settle.

    double TerrainZ = NodeShuffleMapMode::RwgNaN();
    int32 TerrainSkips = 0;
    {
        FCollisionQueryParams DownParams = Params;
        FHitResult D;
        for (int32 K = 0; K < NodeShuffleRocks::TerrainProbeSkipCap; ++K)
        {
            if (!World->LineTraceSingleByChannel(D, FVector(P.X, P.Y, TraceStartZ), FVector(P.X, P.Y, P.Z - 40000.0), ECC_WorldStatic, DownParams)) { break; }
            if (RwgHitClassOf(D) == 0) { TerrainZ = D.ImpactPoint.Z; break; }
            ++TerrainSkips;
            DownParams.AddIgnoredComponent(D.GetComponent());
        }
    }
    RwgRocksProbeTerrainState = FMath::IsFinite(TerrainZ) ? 1 : 2;
    RwgRocksProbeTerrainZ = FMath::IsFinite(TerrainZ) ? TerrainZ : 0.0;

    const UPrimitiveComponent* HitComp = Hit.GetComponent();
    int32 Verdict = 0, Blocked = 0, StartInside = 0, NoStop = 0, Skipped = 0, TopClass = -1;
    double TopAboveCm = NodeShuffleMapMode::RwgNaN();
    const TCHAR* Why = TEXT("notTested");
    if (bTest)
    {
        const int32 Bin = DepthBin(DepthCm);
        ++RwgRkSunkTested; ++RwgRkSunkBins[Bin];
        if (!FMath::IsFinite(TerrainZ)) { Verdict = 2; ++RwgRkSunkDeferred; Why = TEXT("noTerrainUnder"); }
        else if (FMath::IsFinite(TerrainZ) && TerrainZ > P.Z + NodeShuffleRocks::TerrainAboveTolCm) { Verdict = 1; ++RwgRkSunkTerrainAbove; Why = TEXT("terrainAboveTop"); }
        else
        {
            FCollisionQueryParams ColParams = Params;
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
                    ColParams.AddIgnoredComponent(R.GetComponent());
                }
                if (!bStopped) { ++NoStop; ++Blocked; continue; }
                if (R.bStartPenetrating) { ++StartInside; ++Blocked; continue; }
                const double AboveCm = R.ImpactPoint.Z - P.Z;
                if (!FMath::IsFinite(TopAboveCm) || AboveCm > TopAboveCm) { TopAboveCm = AboveCm; TopClass = RwgHitClassOf(R); }
                const bool bSame = HitComp && R.GetComponent() == HitComp && R.Item == Hit.Item && AboveCm <= NodeShuffleRocks::SunkenColumnTolCm;
                if (!bSame) { ++Blocked; }
            }
            Verdict = Blocked > 0 ? 1 : 0;
            Why = Blocked > 0 ? TEXT("columnBlocked") : TEXT("columnsClear");
        }
        if (Verdict == 1) { ++RwgRkSunkRefused; ++RwgRkSunkRefusedBins[Bin]; }
    }
    if (bSample)
    {
        const UStaticMeshComponent* Smc = Cast<UStaticMeshComponent>(HitComp);
        const UStaticMesh* Mesh = Smc ? Smc->GetStaticMesh() : nullptr;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("ROCKTOP: entry=%s via=%s at=%.0f/%.0f hitComponent=%s hitMesh=%s topZ=%.0f apiZAtTopXY=%.0f topMinusApiAtTopXYCm=%.0f terrainZAtTopXY=%.0f terrainTraceSkips=%d tested=%d verdict=%s columnsBlocked=%d/4 columnsStartInside=%d columnsNoStop=%d highestColumnStopAboveTopM=%.1f highestColumnStopClass=%s columnSkippedHits=%d"),
            *RwgSolidTopGuid.ToString().Left(8), RwgSolidTopVia, P.X, P.Y, HitComp ? *HitComp->GetName() : TEXT("<none>"), Mesh ? *Mesh->GetName() : TEXT("<none>"),
            P.Z, ApiZ, P.Z - ApiZ, TerrainZ, TerrainSkips, bTest ? 1 : 0, Why, Blocked, StartInside, NoStop, TopAboveCm / 100.0, RockClassName(TopClass), Skipped);
    }
    return Verdict;
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
        Rows.Sort([](const TPair<FString, FIntPoint>& L, const TPair<FString, FIntPoint>& Rr) { return L.Value.X != Rr.Value.X ? L.Value.X > Rr.Value.X : L.Key < Rr.Key; });
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
        TEXT("ROCKMOVE: session sideways moves kept by completed settles; total=%d near=%d spiralAfterRock=%d spiralAfterWaterOrSteep=%d lt5m=%d 5to10m=%d 10to20m=%d 20to40m=%d 40to80m=%d 80to160m=%d ge160m=%d | near searches run=%d foundNear=%d exhaustedToSpiral=%d spiralFoundAfterNear=%d noSpotAfterNear=%d probeStartsRaisedToApiAtProbeXY=%d"),
        RwgRkMoveNear + RwgRkMoveSpiralRock + RwgRkMoveSpiralOther, RwgRkMoveNear, RwgRkMoveSpiralRock, RwgRkMoveSpiralOther,
        M[0], M[1], M[2], M[3], M[4], M[5], M[6], RwgRkNearRun, RwgRkNearFound, RwgRkNearToSpiral, RwgRkSpiralAfterNear, RwgRkNoSpotAfterNear, RwgRkStartRaised);
    UE_LOG(LogNodeShuffle, Display,
        TEXT("ROCKSUNKEN-CENSUS: session solid-top probes that passed every earlier gate and lie more than %.1f m below the API Z at their own XY; tested=%d refused=%d terrainAboveTop=%d deferredNoTerrainUnder=%d kept=%d apiUnknown=%d below0p5to3m=%d below3to10m=%d below10to30m=%d below30to60m=%d belowGe60m=%d refused0p5to3m=%d refused3to10m=%d refused10to30m=%d refused30to60m=%d refusedGe60m=%d"),
        NodeShuffleRocks::SunkenMarginCm / 100.0, RwgRkSunkTested, RwgRkSunkRefused, RwgRkSunkTerrainAbove, RwgRkSunkDeferred,
        RwgRkSunkTested - RwgRkSunkRefused - RwgRkSunkDeferred, RwgRkSunkApiUnknown,
        RwgRkSunkBins[0], RwgRkSunkBins[1], RwgRkSunkBins[2], RwgRkSunkBins[3], RwgRkSunkBins[4],
        RwgRkSunkRefusedBins[0], RwgRkSunkRefusedBins[1], RwgRkSunkRefusedBins[2], RwgRkSunkRefusedBins[3], RwgRkSunkRefusedBins[4]);
    for (int32 Sx = 0; Sx < 2; ++Sx)
    {
        const int32* C = RwgRkCand[Sx];
        UE_LOG(LogNodeShuffle, Display,
            TEXT("ROCKCAND: search=%s tried=%d terrain=%d rockTop=%d noHit=%d ruleBNoTerrainUnder=%d water=%d steepTerrain=%d rockRefused=%d ruleBRefused=%d (session candidates of the shared near/spiral loop; the eight outcomes sum to tried)"),
            Sx == 0 ? TEXT("near") : TEXT("spiral"), C[0], C[1], C[2], C[3], C[4], C[5], C[6], C[7], C[8]);
    }
    for (int32 T = 0; T < 2; ++T)
    {
        const int32* Z = RwgRkSettleDzBins[T];
        UE_LOG(LogNodeShuffle, Display,
            TEXT("ROCKSETTLEDZ: session completed settles on %s, settled Z minus the API Z at the SETTLED XY; n=%d belowMinus3m=%d minus3toMinus1m=%d minus1toMinus0p5m=%d within0p5m=%d plus0p5to3m=%d abovePlus3m=%d"),
            T == 0 ? TEXT("terrain") : TEXT("a solid top"), Z[0] + Z[1] + Z[2] + Z[3] + Z[4] + Z[5], Z[0], Z[1], Z[2], Z[3], Z[4], Z[5]);
    }
}

void ANodeShuffleSubsystem::RwgRocksCountCandidate(bool bNear, bool bHit, bool bWater, bool bSteep) const
{
    // Round 2: the outcome of ONE near or spiral candidate, from the same booleans the shared loop decides on, in its order:
    // no hit (a rule B terrain-absent top reads as no hit), water, steep (a refused rock: rule B or an earlier gate; else steep terrain),
    // else accepted (rock top when the top was accepted, else terrain). The loop body tests nothing else (no spacing, no world edge).
    int32* C = RwgRkCand[bNear ? 0 : 1];
    ++C[0];
    int32 O = 1;
    if (!bHit) { O = bRwgRocksLastProbeDeferred ? 4 : 3; }
    else if (bWater) { O = 5; }
    else if (bSteep) { O = bRwgLastProbeSolid ? (bRwgRocksLastProbeSunkenRefused ? 8 : 7) : 6; }
    else { O = bRwgLastProbeTopAccepted ? 2 : 1; }
    ++C[O];
}

void ANodeShuffleSubsystem::RwgRocksEmitSettleLine(const FNodeShuffleEntry& E, double ApiZAtSettledXY) const
{
    // Round 2: one line for EVERY settle RwgResolveAtSpawn completes (spawn path and provisional settle), not a sample.
    // terrainUnderTop: the rule B terrain trace's Z for the winning rock top; tokens when there is no number to print.
    FString TerrainUnder = TEXT("notRockTop");
    if (E.bRwgOnSolidTop)
    {
        TerrainUnder = RwgRocksProbeTerrainState == 1 ? FString::Printf(TEXT("%.0f"), RwgRocksProbeTerrainZ)
                     : RwgRocksProbeTerrainState == 2 ? FString(TEXT("noTerrainFound")) : FString(TEXT("notTraced"));
    }
    int32 Dot = INDEX_NONE;
    const FString Res = E.AssignedResourceClassPath.FindLastChar(TEXT('.'), Dot) ? E.AssignedResourceClassPath.Mid(Dot + 1) : E.AssignedResourceClassPath;
    const TCHAR* Route = RwgRocksLastMove == 1 ? TEXT("near") : RwgRocksLastMove == 2 ? TEXT("spiral") : TEXT("primary");
    const TCHAR* Kind = RwgRocksLastKind >= 0 && RwgRocksLastKind < 9 ? RockKindNames[RwgRocksLastKind] : TEXT("none");
    UE_LOG(LogNodeShuffle, Display,
        TEXT("ROCKSETTLE: entry=%s res=%s settledX=%.0f settledY=%.0f settledZ=%.0f apiZAtSettledXY=%.0f settledMinusApiCm=%.0f route=%s sidewaysM=%.1f firstHitClass=%s firstHitName=%s onSolidTop=%d terrainUnderTop=%s"),
        *E.EntryGuid.ToString(), *Res, E.Location.X, E.Location.Y, E.Location.Z, ApiZAtSettledXY, E.Location.Z - ApiZAtSettledXY,
        Route, RwgRocksLastMoveCm / 100.0f, Kind, RwgRocksLastMesh.IsEmpty() ? TEXT("-") : *RwgRocksLastMesh, E.bRwgOnSolidTop ? 1 : 0, *TerrainUnder);
}
