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
#include "Components/StaticMeshComponent.h" // RWG P2c review MF5: mesh= on RWGTERRAINSKIP
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Resources/FGResourceNode.h"
#include "NodeShuffle.h"              // RWG P2d: FNodeShuffleModule::IsManagedSpawnedNode (classifier)

namespace
{
    // Session |dZ| sample cap per band (the census is a distribution, not a log of every spawn).
    constexpr int32 RwgDzSampleCap = 8192;
    constexpr int32 RwgSkipKindCap = 64;

    const TCHAR* RwgHitClassName(int32 C)
    {
        return C == 0 ? TEXT("terrain") : C == 1 ? TEXT("canopy") : C == 2 ? TEXT("water") : C == 4 ? TEXT("solid") : TEXT("other");
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

int32 ANodeShuffleSubsystem::RwgHitClassOf(const FHitResult& Hit, const TCHAR** OutRule)
{
    // RWG P2d (2026-10-18, state "R5 NEW DEFECT"): ONE classifier for the spawn resolver, the well member resolver, the resettle
    // and the rock-occlusion pass. R5 measured RWG dressing terrain with vanilla cliff meshes (CliffPillar_01, both as HISM and
    // as a plain StaticMeshComponent) that P2c skipped as canopy. Order: terrain chunk -> resource nodes (RWG's PWNodeOre, ours,
    // any AFGResourceNodeBase or node mesh actor) -> water sheet -> mesh path SOLID -> mesh path CANOPY -> unknown meshes.
    // FString::Contains is case-insensitive, so "Water" also matches PWWATERSHEET / WaterMesh.
    const auto Out = [OutRule](int32 Cls, const TCHAR* Rule) -> int32 { if (OutRule) { *OutRule = Rule; } return Cls; };
    if (RwgIsTerrainHit(Hit)) { return Out(0, TEXT("terrainChunk")); }
    const AActor* A = Hit.GetActor();
    const UPrimitiveComponent* C = Hit.GetComponent();
    if (A && (Cast<AFGResourceNodeBase>(A) || FNodeShuffleModule::IsManagedSpawnedNode(A)
              || A->GetClass()->GetName().Contains(TEXT("PWNode")) || A->GetClass()->GetName().Contains(TEXT("NodeMeshActor"))))
    {
        return Out(3, TEXT("resourceNode"));
    }
    if ((C && C->GetName().Contains(TEXT("Water"))) || (A && A->GetClass()->GetName().Contains(TEXT("Water")))) { return Out(2, TEXT("waterSheet")); }
    const UStaticMeshComponent* Smc = Cast<UStaticMeshComponent>(Hit.GetComponent()); // ISM/HISM derive from it
    const UStaticMesh* Mesh = Smc ? Smc->GetStaticMesh() : nullptr;
    if (!Mesh) { return Out(3, TEXT("nonMesh")); }
    const FString Path = Mesh->GetPathName();
    if (Path.Contains(TEXT("/Environment/Rock/")) || Path.Contains(TEXT("/Cliff/")) || Path.Contains(TEXT("/Boulder/")) || Path.Contains(TEXT("/Stone/")))
    {
        return Out(4, TEXT("pathSolid"));
    }
    if (Path.Contains(TEXT("/Foliage/")) || Path.Contains(TEXT("/Tree/")) || Path.Contains(TEXT("/Plant/")) || Path.Contains(TEXT("/Trees/")) || Path.Contains(TEXT("/Bush/"))
        || Path.Contains(TEXT("/Grass/")) || Path.Contains(TEXT("/Fern/")))
    {
        return Out(1, TEXT("pathCanopy"));
    }
    if (Smc->GetClass()->GetName().Contains(TEXT("InstancedStaticMesh"))) { return Out(1, TEXT("unknownInstanced")); }
    // Unknown non-instanced mesh: large AND flat-topped at the hit reads as solid ground cover; anything else as canopy.
    if (Hit.ImpactNormal.Z > 0.9 && C->Bounds.BoxExtent.GetMax() > 500.0) { return Out(4, TEXT("unknownLargeFlat")); }
    return Out(1, TEXT("unknownMesh"));
}

void ANodeShuffleSubsystem::RwgNoteHitClass(const FHitResult& Hit, int32 Class, const TCHAR* Rule) const
{
    if (RwgHitClassPaths.Num() >= RwgSkipKindCap) { return; }
    const AActor* A = Hit.GetActor();
    const UPrimitiveComponent* C = Hit.GetComponent();
    const UStaticMeshComponent* Smc = Cast<UStaticMeshComponent>(Hit.GetComponent());
    const UStaticMesh* Mesh = Smc ? Smc->GetStaticMesh() : nullptr;
    const FString CompClass = C ? C->GetClass()->GetName() : FString(TEXT("<no component>"));
    const FString Key = Mesh ? Mesh->GetPathName() : (FString(TEXT("<no mesh>|")) + CompClass);
    if (RwgHitClassPaths.Contains(Key)) { return; }
    RwgHitClassPaths.Add(Key);
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGHITCLASS: first hit on this mesh path; class=%s rule=%s mesh='%s' componentClass='%s' ownerClass='%s' hitNormalZ=%.2f boundsExtentMaxCm=%.0f pathsSoFar=%d"),
        RwgHitClassName(Class), Rule ? Rule : TEXT("?"), *Key, *CompClass, A ? *A->GetClass()->GetName() : TEXT("<no actor>"),
        Hit.ImpactNormal.Z, C ? C->Bounds.BoxExtent.GetMax() : 0.0, RwgHitClassPaths.Num());
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
    const UStaticMeshComponent* Smc = Cast<UStaticMeshComponent>(Hit.GetComponent()); // ISM/HISM derive from it (non-const source)
    const UStaticMesh* Mesh = Smc ? Smc->GetStaticMesh() : nullptr;
    const FString MeshPath = Mesh ? Mesh->GetPathName() : FString(TEXT("<none>"));
    const FVector Ext = C ? C->Bounds.BoxExtent : FVector::ZeroVector;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGTERRAINSKIP: first skip of this kind (a terrain-only trace ignored the component and traced again); class=%s ownerClass='%s' componentClass='%s' component='%s' mesh='%s' hitZ=%.0f hitNormalZ=%.2f boundsExtentCm=(%.0f,%.0f,%.0f) kindsSoFar=%d"),
        RwgHitClassName(Class), *OwnerClass, *CompClass, *CompName, *MeshPath, Hit.ImpactPoint.Z, Hit.ImpactNormal.Z, Ext.X, Ext.Y, Ext.Z, RwgSkippedHitKinds.Num());
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
        TEXT("PLACEDZ: apiToTerrainDz session samples (terrain-mesh hit Z minus RWG surface Z, from spawn settles and resettle probes), bands by distance to the nearest pawn, 0 is 0-250 m, 1 is 250-500 m, 2 is 500-1000 m; dzN0=%d dzMeanAbsCm0=%.0f dzP95AbsCm0=%.0f dzMaxAbsCm0=%.0f dzN1=%d dzMeanAbsCm1=%.0f dzP95AbsCm1=%.0f dzMaxAbsCm1=%.0f dzN2=%d dzMeanAbsCm2=%.0f dzP95AbsCm2=%.0f dzMaxAbsCm2=%.0f dzBeyond1000m=%d"),
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
        if (!E.bIsNewNode || !E.bActive || E.bRwgSettledOnTerrain || E.bUnderground || E.bRwgCave) { continue; } // RWG P5: a cave entry settles by the cave geometry only
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
    int32 Checked = 0, Moved = 0, Kept = 0, Deferred = 0, MaxDz = 0, Occupied = 0, Wet = 0, MovedSteep = 0, GaveUp = 0, MoveRefused = 0;
    int32 RejectedRock = 0, RockRedealt = 0; // RWG P2d
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
            ++Deferred; // no terrain-mesh hit in the window
            // Only a void within 250 m of a pawn counts toward the session give-up (measured residency 0.5-1 km: 11/16);
            // a void farther out is retried on later passes, so the node is still probed once the pawn closes in.
            if (Cands[K].Key < 25000.0)
            {
                int32& V = RwgResettleVoids.FindOrAdd(E.EntryGuid);
                if (++V >= NodeShuffleMapMode::RwgResettleVoidGiveUp) { RwgResettleGaveUp.Add(E.EntryGuid); ++GaveUp; }
            }
            continue;
        }
        if (bRwgLastProbeSolid)
        {
            // RWG P2d: the probe stopped on a SOLID mesh (rock/cliff) at this XY -- the node is under, inside or on that mesh. Never
            // moved onto it: the entry is redealt (actor despawned, API redeal, re-placed by the spawn pass). Occupied was refused above.
            ++RejectedRock;
            if (RwgRedealLiveEntry(E, TEXT("resettle probe stopped on a solid mesh"))) { ++RockRedealt; }
            continue;
        }
        const double DzMove = HitLoc.Z - Cur.Z;
        const double DzApi = Q.IsUsable() ? HitLoc.Z - Q.SurfaceZ(HitLoc.X, HitLoc.Y) : DzMove;
        RwgRecordDz(HitLoc, DzApi);
        MaxDz = FMath::Max(MaxDz, static_cast<int32>(FMath::Abs(DzMove)));
        if (FMath::Abs(DzMove) <= NodeShuffleMapMode::RwgResettleKeepCm)
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
        // Readback (P2c review): whether the engine applied the move is closed-source (a non-Movable root refuses it with a
        // warning only). The entry and the flag follow the ACTOR's measured Z, never the request.
        const double ActorZAfter = Node->GetActorLocation().Z;
        if (FMath::Abs(ActorZAfter - HitLoc.Z) > 10.0)
        {
            ++MoveRefused;
            RwgResettleGaveUp.Add(E.EntryGuid);
            UE_LOG(LogNodeShuffle, Display, TEXT("RESETTLE-REFUSED: entry=%s requestedZ=%.0f actorZAfter=%.0f (entry left unflagged)"),
                *E.EntryGuid.ToString(), HitLoc.Z, ActorZAfter);
            continue;
        }
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
    int32 WChecked = 0, WMoved = 0, WKept = 0, WDeferred = 0, WOccupied = 0, WMaxDz = 0; // WMoved = well groups redealt (P2d)
    RwgResettleWells(WChecked, WMoved, WKept, WDeferred, WOccupied, WMaxDz);
    if (Moved > 0 || Kept > 0 || MoveRefused > 0 || Occupied > 0 || Wet > 0 || GaveUp > 0 || Requeued > 0 || RejectedRock > 0 || WMoved > 0 || WKept > 0 || WOccupied > 0 || (RwgRsPasses % 12) == 1) // bounded: a pass that changed state, else once a minute
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RESETTLE: pass=%d checked=%d moved=%d kept=%d deferred=%d maxDzCm=%d occupiedSkipped=%d wetNotMoved=%d movedOntoCliff=%d moveRefused=%d rejectedRock=%d rockRedealt=%d voidGaveUpNow=%d requeuedUnspawned=%d unflaggedEntries=%d wellGroupsChecked=%d wellGroupsRedealt=%d wellGroupsKept=%d wellGroupsDeferred=%d wellGroupsInUse=%d wellMaxDzCm=%d"),
            RwgRsPasses, Checked, Moved, Kept, Deferred, MaxDz, Occupied, Wet, MovedSteep, MoveRefused, RejectedRock, RockRedealt, GaveUp, Requeued, Remaining,
            WChecked, WMoved, WKept, WDeferred, WOccupied, WMaxDz);
    }
}
// RwgResettleWells moved to NodeShuffleMapModeRock.cpp (RWG P2d: well resettle via the despawn path; 500-line limit).
