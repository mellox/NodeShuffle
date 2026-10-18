// RWG P2c (2026-10-18) -- terrain-hit predicates, the API-vs-terrain dZ census, and the RESETTLE pass.
// Brief: _team/nodeshuffle-rwg-p2-spec.md, last section ("R4 finding ... FLOATING NODES"), binding.
// Every function here is reached only from RWG code: RwgSpawnPass (RWG only), RwgResolveAtSpawn, and RaycastGroundAt's
// bTerrainOnly branch, which only RWG call sites enable. Log lines report counts and the predicate that branched, never a cause.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffleWellRelocate.h"   // IsWellMemberInUse (the shared two-part in-use predicate)
#include "NodeShuffleWellCensus.h"     // AFGResourceNodeFrackingCore / AFGResourceNodeFrackingSatellite complete types
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Resources/FGResourceNode.h"

namespace
{
    // Session |dZ| sample cap per band (the census is a distribution, not a log of every spawn).
    constexpr int32 RwgDzSampleCap = 8192;
    constexpr int32 RwgSkipKindCap = 64;

    const TCHAR* RwgHitClassName(int32 C)
    {
        return C == 0 ? TEXT("terrain") : C == 1 ? TEXT("canopy") : C == 2 ? TEXT("water") : TEXT("other");
    }

    FString StripTrailingDigits(const FString& In)
    {
        int32 N = In.Len();
        while (N > 0 && (FChar::IsDigit(In[N - 1]) || In[N - 1] == TEXT('_'))) { --N; }
        return In.Left(N);
    }

    void BandStats(TArray<float> V, int32& OutN, double& OutMean, double& OutP95, double& OutMax)
    {
        OutN = V.Num(); OutMean = OutP95 = OutMax = 0.0;
        if (OutN == 0) { return; }
        V.Sort();
        double Sum = 0.0;
        for (const float X : V) { Sum += X; }
        OutMean = Sum / OutN;
        OutP95 = V[FMath::Clamp(static_cast<int32>(FMath::CeilToDouble(0.95 * OutN)) - 1, 0, OutN - 1)];
        OutMax = V.Last();
    }
}

bool ANodeShuffleSubsystem::RwgIsTerrainHit(const FHitResult& Hit)
{
    // Mirror of the HeightApi F1 split (NodeShuffleMapModeHeightApi.cpp, bTerrainMesh): the chunk actor also owns the water
    // mesh and the foliage instances, so the COMPONENT decides. Names measured by RWGHEIGHTAPI on 2026-10-18:
    // owner 'PWTerrainChunk', component 'TerrainMesh' (95 hits) vs 'HierarchicalInstancedStaticMeshComponent' (183).
    const AActor* A = Hit.GetActor();
    const UPrimitiveComponent* C = Hit.GetComponent();
    return A && C && A->GetClass()->GetName().Contains(TEXT("TerrainChunk")) && C->GetName().Contains(TEXT("Terrain"));
}

int32 ANodeShuffleSubsystem::RwgHitClassOf(const FHitResult& Hit)
{
    if (RwgIsTerrainHit(Hit)) { return 0; }
    const AActor* A = Hit.GetActor();
    const UPrimitiveComponent* C = Hit.GetComponent();
    // Class by NAME (no Cast<>, so no new UClass import): instanced meshes are foliage/trees on RWG chunks.
    if (C && C->GetClass()->GetName().Contains(TEXT("InstancedStaticMesh"))) { return 1; }
    if ((C && C->GetName().Contains(TEXT("Water"))) || (A && A->GetClass()->GetName().Contains(TEXT("Water")))) { return 2; }
    return 3;
}

void ANodeShuffleSubsystem::RwgNoteSkippedHit(const FHitResult& Hit, int32 Class) const
{
    if (RwgSkippedHitKinds.Num() >= RwgSkipKindCap) { return; }
    const AActor* A = Hit.GetActor();
    const UPrimitiveComponent* C = Hit.GetComponent();
    const FString OwnerClass = A ? A->GetClass()->GetName() : FString(TEXT("<no actor>"));
    const FString CompClass = C ? C->GetClass()->GetName() : FString(TEXT("<no component>"));
    const FString CompName = C ? C->GetName() : FString(TEXT("<no component>"));
    const FString Kind = OwnerClass + TEXT("|") + CompClass + TEXT("|") + StripTrailingDigits(CompName);
    if (RwgSkippedHitKinds.Contains(Kind)) { return; }
    RwgSkippedHitKinds.Add(Kind);
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGTERRAINSKIP: first skip of this kind (a terrain-only trace ignored the component and traced again); class=%s ownerClass='%s' componentClass='%s' component='%s' hitZ=%.0f kindsSoFar=%d"),
        RwgHitClassName(Class), *OwnerClass, *CompClass, *CompName, Hit.ImpactPoint.Z, RwgSkippedHitKinds.Num());
}

double ANodeShuffleSubsystem::RwgNearestPawnDist2D(const FVector& At) const
{
    double Best = TNumericLimits<double>::Max();
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr;
        if (P) { Best = FMath::Min(Best, FVector::Dist2D(P->GetActorLocation(), At)); }
    }
    return Best;
}

void ANodeShuffleSubsystem::RwgRecordDz(const FVector& At, double DzCm)
{
    const double D = RwgNearestPawnDist2D(At);
    TArray<float>* Band = D < 25000.0 ? &RwgDzBand0 : D < 50000.0 ? &RwgDzBand1 : D < 100000.0 ? &RwgDzBand2 : nullptr;
    if (!Band) { ++RwgDzBeyond; return; }
    if (Band->Num() < RwgDzSampleCap) { Band->Add(static_cast<float>(FMath::Abs(DzCm))); }
}

void ANodeShuffleSubsystem::RwgEmitDzCensus()
{
    int32 N0, N1, N2;
    double M0, P0, X0, M1, P1, X1, M2, P2, X2;
    BandStats(RwgDzBand0, N0, M0, P0, X0);
    BandStats(RwgDzBand1, N1, M1, P1, X1);
    BandStats(RwgDzBand2, N2, M2, P2, X2);
    UE_LOG(LogNodeShuffle, Display,
        TEXT("PLACECENSUS: apiToTerrainDz session samples (terrain-mesh hit Z minus RWG surface Z, from spawn settles and resettle probes), bands by distance to the nearest pawn, 0 is 0-250 m, 1 is 250-500 m, 2 is 500-1000 m; dzN0=%d dzMeanAbsCm0=%.0f dzP95AbsCm0=%.0f dzMaxAbsCm0=%.0f dzN1=%d dzMeanAbsCm1=%.0f dzP95AbsCm1=%.0f dzMaxAbsCm1=%.0f dzN2=%d dzMeanAbsCm2=%.0f dzP95AbsCm2=%.0f dzMaxAbsCm2=%.0f dzBeyond1000m=%d"),
        N0, M0, P0, X0, N1, M1, P1, X1, N2, M2, P2, X2, RwgDzBeyond);
}

void ANodeShuffleSubsystem::RwgResettlePass()
{
    // Population: active new-node entries without bRwgSettledOnTerrain (every RWG entry placed by a build <= rwg-p4-2, which
    // sat at the API Z). A LIVE actor within the attempt radius is probed (nearest first, <= RwgResettlePerPass); an entry
    // with NO live actor that was marked settled is re-queued (bRayCasted = false) so resolver v3 settles it at its next spawn.
    // Idempotent across reloads: the flag is SaveGame; an unsaved world simply repeats the pass once.
    ++RwgRsPasses;
    const NodeShuffleMapMode::FRwgGroundQuery Q(GetWorld(), RwgGenerator.Get()); // dZ measurement only; gates nothing
    TArray<TPair<double, int32>> Cands;
    int32 Requeued = 0, Remaining = 0;
    for (int32 I = 0; I < Layout.Num(); ++I)
    {
        FNodeShuffleEntry& E = Layout[I];
        if (!E.bIsNewNode || !E.bActive || E.bRwgSettledOnTerrain || E.bUnderground) { continue; }
        ++Remaining;
        if (RwgResettleGaveUp.Contains(E.EntryGuid)) { continue; }
        AFGResourceNode* const* Live = SpawnedNodes.Find(E.EntryGuid);
        if (!Live || !IsValid(*Live))
        {
            if (E.bRayCasted) { E.bRayCasted = false; ++Requeued; }
            continue;
        }
        const double D = RwgNearestPawnDist2D((*Live)->GetActorLocation());
        if (D > NodeShuffleMapMode::RwgAttemptRadiusCm) { continue; }
        Cands.Emplace(D, I);
    }
    Cands.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
    const int32 N = FMath::Min(Cands.Num(), NodeShuffleMapMode::RwgResettlePerPass);
    const bool bDiag = FNodeShuffleModule::AreDiagnosticsEnabled();
    int32 Checked = 0, Moved = 0, Kept = 0, Deferred = 0, MaxDz = 0, Occupied = 0, Wet = 0, MovedSteep = 0, GaveUp = 0;
    for (int32 K = 0; K < N; ++K)
    {
        FNodeShuffleEntry& E = Layout[Cands[K].Value];
        AFGResourceNode* Node = SpawnedNodes.FindRef(E.EntryGuid);
        ++Checked;
        // A built-on node is never moved (it would leave the building behind); not flagged, so a later session re-checks it.
        if (E.bPinned || IsNodeOccupiedAnyway(Node)) { ++Occupied; RwgResettleGaveUp.Add(E.EntryGuid); continue; }
        const FVector Cur = Node->GetActorLocation();
        FVector HitLoc = Cur;
        FRotator HitRot = Node->GetActorRotation();
        bool bWater = false, bSteep = false;
        if (!RaycastGroundAt(Cur, static_cast<float>(Cur.Z), Node, nullptr, HitLoc, HitRot, bWater, false, &bSteep,
                             nullptr, nullptr, /*bTerrainOnly=*/true))
        {
            ++Deferred; // no terrain-mesh hit in the window: terrain not resident at this XY, or a column of skipped components
            int32& V = RwgResettleVoids.FindOrAdd(E.EntryGuid);
            if (++V >= NodeShuffleMapMode::RwgResettleVoidGiveUp) { RwgResettleGaveUp.Add(E.EntryGuid); ++GaveUp; }
            continue;
        }
        const double DzMove = HitLoc.Z - Cur.Z;
        const double DzApi = Q.IsUsable() ? HitLoc.Z - Q.SurfaceZ(HitLoc.X, HitLoc.Y) : DzMove;
        RwgRecordDz(HitLoc, DzApi);
        MaxDz = FMath::Max(MaxDz, static_cast<int32>(FMath::Abs(DzMove)));
        if (FMath::Abs(DzMove) <= NodeShuffleMapMode::RwgTraceAgreeCm)
        {
            E.bRwgSettledOnTerrain = true;
            E.RwgApiToTerrainDzCm = static_cast<float>(DzApi);
            ++Kept;
            continue;
        }
        // Terrain under water at this XY: not moved (a redeal of a live node is not part of this pass); not re-probed this session.
        if (bWater) { ++Wet; RwgResettleGaveUp.Add(E.EntryGuid); continue; }
        // Moved onto the terrain hit at the SAME XY, cliff or not: the actor rotation is kept and the established slopefit
        // refit (EnsureNewNodeSpawned, once per entry per session) re-derives it and re-dresses the rock on the next re-assert.
        Node->SetActorLocationAndRotation(HitLoc, Node->GetActorRotation(), false, nullptr, ETeleportType::TeleportPhysics);
        AdoptRotRefit.Remove(E.EntryGuid);
        E.Location = HitLoc;
        E.bRwgSettledOnTerrain = true;
        E.RwgApiToTerrainDzCm = static_cast<float>(DzApi);
        ++Moved;
        if (bSteep) { ++MovedSteep; }
        if (bDiag)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("RESETTLE-MOVE: entry=%s from=%s to=%s moveDzCm=%.0f apiDzCm=%.0f steep=%d"),
                *E.EntryGuid.ToString(), *Cur.ToCompactString(), *HitLoc.ToCompactString(), DzMove, DzApi, bSteep ? 1 : 0);
        }
    }
    int32 WChecked = 0, WMoved = 0, WKept = 0, WDeferred = 0, WOccupied = 0, WMaxDz = 0;
    RwgResettleWells(WChecked, WMoved, WKept, WDeferred, WOccupied, WMaxDz);
    if (Checked > 0 || Requeued > 0 || WChecked > 0 || (RwgRsPasses % 12) == 1) // bounded: a pass that did work, else once a minute
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RESETTLE: pass=%d checked=%d moved=%d kept=%d deferred=%d maxDzCm=%d occupiedSkipped=%d wetNotMoved=%d movedOntoCliff=%d voidGaveUpNow=%d requeuedUnspawned=%d unflaggedEntries=%d wellGroupsChecked=%d wellMembersMoved=%d wellMembersKept=%d wellGroupsDeferred=%d wellGroupsInUse=%d wellMaxDzCm=%d"),
            RwgRsPasses, Checked, Moved, Kept, Deferred, MaxDz, Occupied, Wet, MovedSteep, GaveUp, Requeued, Remaining,
            WChecked, WMoved, WKept, WDeferred, WOccupied, WMaxDz);
    }
}

void ANodeShuffleSubsystem::RwgResettleWells(int32& Checked, int32& Moved, int32& Kept, int32& Deferred, int32& Occupied, int32& MaxDzCm)
{
    // Placed RWG well groups without the flag (placed by builds <= rwg-p4-2 through the first-hit probe, which could end on a
    // canopy or water-sheet component). Each member is probed on its OWN terrain-mesh hit and moved individually (satellites
    // are redrawn per member at placement, not a rigid body). Any member void = the whole group waits for a later pass.
    TArray<TPair<double, int32>> Cands;
    for (int32 I = 0; I < WellLayout.Num(); ++I)
    {
        const FNodeShuffleWellEntry& E = WellLayout[I];
        if (!E.bGroupPlaced || E.bRwgSettledOnTerrain || RwgResettleWellGaveUp.Contains(E.CorePath)) { continue; }
        AFGResourceNodeFrackingCore* Core = SpawnedWellCores.FindRef(E.CorePath);
        if (!IsValid(Core)) { continue; }
        const double D = RwgNearestPawnDist2D(Core->GetActorLocation());
        if (D > NodeShuffleMapMode::RwgAttemptRadiusCm) { continue; }
        Cands.Emplace(D, I);
    }
    Cands.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
    const int32 N = FMath::Min(Cands.Num(), NodeShuffleMapMode::RwgResettleWellsPerPass);
    for (int32 K = 0; K < N; ++K)
    {
        FNodeShuffleWellEntry& E = WellLayout[Cands[K].Value];
        ++Checked;
        struct FMember { AFGResourceNodeBase* Actor; int32 SatIdx; FVector Hit; bool bWater; };
        TArray<FMember> Members;
        Members.Add({ SpawnedWellCores.FindRef(E.CorePath), INDEX_NONE, FVector::ZeroVector, false });
        for (int32 s = 0; s < E.Satellites.Num(); ++s)
        {
            if (!E.Satellites[s].bPlaced) { continue; }
            AFGResourceNodeFrackingSatellite* Sat = SpawnedWellSatellites.FindRef(E.Satellites[s].SatellitePath);
            if (IsValid(Sat)) { Members.Add({ Sat, s, FVector::ZeroVector, false }); }
        }
        bool bInUse = E.bPinned;
        for (const FMember& M : Members)
        {
            const TCHAR* Why = TEXT("");
            if (IsWellMemberInUse(M.Actor, Why)) { bInUse = true; }
        }
        if (bInUse) { ++Occupied; RwgResettleWellGaveUp.Add(E.CorePath); continue; } // never move a well someone built on
        bool bAllHit = true;
        for (FMember& M : Members)
        {
            const FVector Cur = M.Actor->GetActorLocation();
            FRotator Rot = M.Actor->GetActorRotation();
            bool bSteep = false;
            if (!RaycastGroundAt(Cur, static_cast<float>(Cur.Z), M.Actor, nullptr, M.Hit, Rot, M.bWater, false, &bSteep,
                                 nullptr, nullptr, /*bTerrainOnly=*/true)) { bAllHit = false; break; }
        }
        if (!bAllHit)
        {
            ++Deferred;
            int32& V = RwgResettleWellVoids.FindOrAdd(E.CorePath);
            if (++V >= NodeShuffleMapMode::RwgResettleVoidGiveUp) { RwgResettleWellGaveUp.Add(E.CorePath); }
            continue;
        }
        for (const FMember& M : Members)
        {
            const FVector Cur = M.Actor->GetActorLocation();
            const double Dz = M.Hit.Z - Cur.Z;
            MaxDzCm = FMath::Max(MaxDzCm, static_cast<int32>(FMath::Abs(Dz)));
            if (FMath::Abs(Dz) <= NodeShuffleMapMode::RwgTraceAgreeCm || M.bWater) { ++Kept; continue; } // wet terrain: not moved
            M.Actor->SetActorLocationAndRotation(M.Hit, M.Actor->GetActorRotation(), false, nullptr, ETeleportType::TeleportPhysics);
            if (M.SatIdx == INDEX_NONE) { E.PlacedCoreLocation = M.Hit; } else { E.Satellites[M.SatIdx].PlacedLocation = M.Hit; }
            ++Moved;
            UE_LOG(LogNodeShuffle, Verbose, TEXT("RESETTLE-WELL: core='%s' member=%d from=%s to=%s dzCm=%.0f"),
                *E.CorePath, M.SatIdx, *Cur.ToCompactString(), *M.Hit.ToCompactString(), Dz);
        }
        E.bRwgSettledOnTerrain = true;
    }
}
