// RWG P2d (2026-10-18) -- rock-occlusion resettle for placed RWG node entries, and the well resettle via the despawn path.
// Spec: _team/nodeshuffle-state.md "R5 NEW GAME ... NEW DEFECT (P2d)" + the P2d dispatch. Every function here is reached only from
// RWG code (RwgSpawnPass -> RwgResettlePass / RwgRockOcclusionPass, both RWG-only). The hit classifier they share is
// RwgHitClassOf (NodeShuffleMapModeResettle.cpp). Log lines report counts and the predicate that branched, never a cause.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"
#include "NodeShuffleWellRelocate.h"   // IsWellMemberInUse, WellLayoutSeedFor
#include "NodeShuffleWellCensus.h"     // AFGResourceNodeFrackingCore / AFGResourceNodeFrackingSatellite complete types
#include "NodeShuffleWellRetype.h"     // WellShort
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "Components/PrimitiveComponent.h"
#include "Buildables/FGBuildable.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceNodeManager.h"
#include "FGActorRepresentationManager.h"
#include "Representation/FGResourceNodeRepresentation.h"

int32 ANodeShuffleSubsystem::RwgRockOcclusionProbe(const FVector& At, const AActor* IgnoreActor, FString& OutMeshPath) const
{
    // One vertical segment from RwgRockOcclusionHeightCm above At down to RwgRockOcclusionEndAboveCm above it (the ground the
    // node stands on is never part of the window). Canopy / water sheet / resource-node hits are ignored by component and the
    // segment is traced again (same cap as the terrain-only probe). Returns 1 on a SOLID hit (OutMeshPath = that hit's COMPONENT name; RWGHITCLASS has its mesh path),
    // 2 on a terrain-chunk or player-buildable hit (something above the node that is not rock: never redealt), 0 when the
    // segment is clear, 3 when the skip cap is exhausted.
    OutMeshPath.Reset();
    UWorld* World = GetWorld();
    if (!World) { return 0; }
    FCollisionQueryParams P(SCENE_QUERY_STAT(NodeShuffleRockOcclusion), true);
    if (IgnoreActor) { P.AddIgnoredActor(IgnoreActor); }
    const FVector S(At.X, At.Y, At.Z + NodeShuffleMapMode::RwgRockOcclusionHeightCm);
    const FVector En(At.X, At.Y, At.Z + NodeShuffleMapMode::RwgRockOcclusionEndAboveCm);
    for (int32 K = 0; K <= NodeShuffleMapMode::RwgTerrainSkipMax; ++K)
    {
        FHitResult H;
        if (!World->LineTraceSingleByChannel(H, S, En, ECC_WorldStatic, P)) { return 0; }
        const TCHAR* Rule = TEXT("");
        const int32 Cls = RwgHitClassOf(H, &Rule);
        if (Cls != 0) { RwgNoteHitClass(H, Cls, Rule); }
        const AActor* HitOwner = H.GetActor();
        if (Cls == 0 || (HitOwner && (HitOwner->IsA<AFGBuildable>() || HitOwner->GetClass()->GetName().Contains(TEXT("AbstractInstanceManager"))))) { return 2; }
        if (Cls == 4)
        {
            const UPrimitiveComponent* C = H.GetComponent();
            OutMeshPath = C ? C->GetName() : FString(TEXT("<no component>"));
            return 1;
        }
        P.AddIgnoredComponent(H.GetComponent());
    }
    return 3; // skip cap exhausted: nothing measured below the last skipped hit
}

bool ANodeShuffleSubsystem::RwgRedealLiveEntry(FNodeShuffleEntry& E, const TCHAR* Why)
{
    AFGResourceNode* Node = SpawnedNodes.FindRef(E.EntryGuid);
    if (E.bPinned || (IsValid(Node) && IsNodeOccupiedAnyway(Node))) { return false; } // never under a player's building
    const FVector From = E.Location;
    // The SAME teardown as the re-roll wipe (NodeShuffleSubsystem.cpp, RollLayout, coexist-1 INVARIANT): Destroy, unregister
    // AFTER (a KBFL destroyed-listener still sees the node as managed), and the SpawnedNodes slot removed in this same block,
    // so EnsureNewNodeSpawned's external-destroy detection never reads this as an outside destroyer.
    if (Node)
    {
        MeshActorCache.Remove(TWeakObjectPtr<AFGResourceNodeBase>(Node));
        if (IsValid(Node))
        {
            // RWG P2d review MF1: the manager's Mk1 snap list holds a RAW pointer to every node we spawn (RegisterNodeWithManager) and
            // DeregisterNodeFromManager refuses our own nodes, so drop it here, with the scan reveal and the map/compass representation,
            // BEFORE Destroy -- the same registrations DestroyWellMemberIfUnused drops for a well member.
            if (AFGResourceNodeManager* Mgr = GetNodeManager()) { Mgr->mResourceNodes.RemoveSingleSwap(Node); }
            if (AFGActorRepresentationManager* RepMgr = AFGActorRepresentationManager::Get(GetWorld()))
            {
                RepMgr->RemoveRepresentationOfActor(Node);
                if (UFGResourceNodeRepresentation* NodeRep = RepMgr->FindResourceNodeRepresentation(Node)) { RepMgr->RemoveRepresentation(NodeRep); }
            }
            Node->RemoveResourceNodeScan_Local();
            Node->UpdateNodeRepresentation();
            Node->Destroy();
        }
        FNodeShuffleModule::UnregisterManagedNode(Node);
    }
    SpawnedNodes.Remove(E.EntryGuid);
    SteadyAliveNodes.Remove(E.EntryGuid);
    AdoptRotRefit.Remove(E.EntryGuid);
    E.bRwgSettledOnTerrain = false;
    E.bRwgRockChecked = false;
    E.bRwgVoidChecked = false; // RWG P2f: the new spot is re-checked by the re-deal pass
    E.bRayCasted = false;
    // The spawn path's own redeal (API dealer on RWG, RedealTries per call); a spot it cannot move is settled again by the spawn
    // pass, whose terrain-only probe now reports a solid stop as unplaceable -> spiral -> the same redeal.
    const bool bMoved = TryRedealWaterLockedEntry(E);
    if (FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("RWG-ROCK-REDEAL: entry=%s from=%s to=%s redealMoved=%d trigger='%s'"),
            *E.EntryGuid.ToString(), *From.ToCompactString(), *E.Location.ToCompactString(), bMoved ? 1 : 0, Why);
    }
    return true;
}

void ANodeShuffleSubsystem::RwgRockOcclusionPass()
{
    // Population: active RWG new-node entries with a LIVE actor and no bRwgRockChecked, settled or not, within the attempt radius,
    // nearest the pawn first, at most RwgResettlePerPass per pass. Idempotent: a clear probe sets the SaveGame flag; a redeal clears it.
    ++RwgRockPasses;
    TArray<TPair<double, int32>> Cands;
    for (int32 I = 0; I < Layout.Num(); ++I)
    {
        const FNodeShuffleEntry& E = Layout[I];
        if (!E.bIsNewNode || !E.bActive || E.bRwgRockChecked || E.bUnderground) { continue; }
        if (RwgRockRefused.Contains(E.EntryGuid)) { continue; }
        AFGResourceNode* Live = SpawnedNodes.FindRef(E.EntryGuid);
        if (!IsValid(Live)) { continue; }
        const double D = RwgNearestPawnDist2D(Live->GetActorLocation());
        if (D > NodeShuffleMapMode::RwgAttemptRadiusCm) { continue; }
        Cands.Emplace(D, I);
    }
    Cands.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
    const int32 N = FMath::Min(Cands.Num(), NodeShuffleMapMode::RwgResettlePerPass);
    const bool bDiag = FNodeShuffleModule::AreDiagnosticsEnabled();
    int32 Checked = 0, Occluded = 0, Redealt = 0, Clear = 0, NonRockAbove = 0, RefusedBuiltOn = 0;
    int32 CapExhausted = 0;
    for (int32 K = 0; K < N; ++K)
    {
        FNodeShuffleEntry& E = Layout[Cands[K].Value];
        AFGResourceNode* Node = SpawnedNodes.FindRef(E.EntryGuid);
        ++Checked;
        FString Mesh;
        const int32 R = RwgRockOcclusionProbe(Node->GetActorLocation(), Node, Mesh);
        if (R == 3) { ++CapExhausted; RwgRockRefused.Add(E.EntryGuid); continue; } // nothing measured: not flagged, re-probed next session
        if (R != 1)
        {
            E.bRwgRockChecked = true;
            if (R == 2) { ++NonRockAbove; } else { ++Clear; }
            continue;
        }
        ++Occluded;
        if (E.bPinned || IsNodeOccupiedAnyway(Node)) { ++RefusedBuiltOn; RwgRockRefused.Add(E.EntryGuid); continue; } // unflagged: next session re-checks
        if (bDiag)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("RESETTLE-ROCK-HIT: entry=%s at=%s solidComponent='%s'"),
                *E.EntryGuid.ToString(), *Node->GetActorLocation().ToCompactString(), *Mesh);
        }
        if (RwgRedealLiveEntry(E, TEXT("rock-occluded"))) { ++Redealt; }
    }
    if (Occluded > 0 || (Checked > 0 && (RwgRockPasses % 12) == 1)) // bounded: a pass that found rock, else once a minute while checking
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RESETTLE-ROCK: pass=%d checked=%d occluded=%d redealt=%d clear=%d terrainOrBuildableAbove=%d refusedBuiltOn=%d candidates=%d windowCm=%.0f skipCapExhausted=%d"),
            RwgRockPasses, Checked, Occluded, Redealt, Clear, NonRockAbove, RefusedBuiltOn, Cands.Num(), NodeShuffleMapMode::RwgRockOcclusionHeightCm, CapExhausted);
    }
}

bool ANodeShuffleSubsystem::RwgRedealWellGroup(FNodeShuffleWellEntry& E, const TCHAR* Why, bool& bOutDealt)
{
    // The re-enrolment order (NodeShuffleWellRelocateRoll.cpp, ns-review-h3 H1 + ns-t7b F7), never move-in-place
    // (NodeShuffleWellDespawn.cpp, DESPAWN-ON-MOVE): DespawnWellGroup FIRST and its bAllClear READ; only then bGroupPlaced=false,
    // then ClearAbandonedWellPlacement (it refuses a still-placed entry), then a fresh API-dealt destination. The group is
    // re-placed by ApplyWellRelocation's own loop on a later pass (WELLH2-GATE occupancy check -> TryPlaceWellGroup ->
    // SpawnWellGroup -> SuppressVanillaWellGroup), the only call site that follows TryPlaceWellGroup with the spawn.
    bOutDealt = false;
    const FVector WasAt = E.PlacedCoreLocation;
    if (!DespawnWellGroup(E, Why))
    {
        // A member was refused as occupied (DespawnWellGroup printed *** ABANDONED IN PLACE ***). Entry keeps bGroupPlaced.
        UE_LOG(LogNodeShuffle, Warning, TEXT("RESETTLE-WELL-REFUSED: core='%s' at=%s; DespawnWellGroup returned bAllClear=0, entry left placed"),
            *WellShort(E.CorePath), *WasAt.ToCompactString());
        return false;
    }
    E.bGroupPlaced = false;
    ClearAbandonedWellPlacement(E, Why);
    E.bRwgSettledOnTerrain = false;
    E.YawCursor = 0;
    E.GroupNudges = 0;
    E.bRelocationFailed = false;
    E.bRelocate = true;
    const NodeShuffleMapMode::FRwgGroundQuery Q(GetWorld(), RwgGenerator.Get());
    int32 Draws = 0;
    const int32 Salt = ++RwgWellResettleSalt.FindOrAdd(E.CorePath);
    if (Q.IsUsable())
    {
        RwgBuildWaterVolumeCache();
        TArray<FVector> Offs;
        for (const FNodeShuffleWellSatellite& S : E.Satellites) { if (S.bCaptured) { Offs.Add(S.LocalOffset); } }
        // The Escalate redeal's seed shape (persisted state only), salted for the resettle so it never replays that stream.
        FRandomStream Rng(WellLayoutSeedFor(SavedSeed, E.CorePath, E.GroupRedeals, 0) ^ 0x52534C31 ^ (Salt * 7919));
        FVector Site(FVector::ZeroVector);
        for (; Draws < WellRedealTries && !bOutDealt; ++Draws)
        {
            if (RwgDealWellSite(Q, Rng, Offs, Site)) { E.DestCoreLocation = Site; bOutDealt = true; }
        }
    }
    UE_LOG(LogNodeShuffle, Display, TEXT("RESETTLE-WELL-REDEAL: core='%s' from=%s dest=%s apiUsable=%d draws=%d dealtNewSite=%d trigger='%s'"),
        *WellShort(E.CorePath), *WasAt.ToCompactString(), *E.DestCoreLocation.ToCompactString(), Q.IsUsable() ? 1 : 0, Draws, bOutDealt ? 1 : 0, Why);
    return true;
}

void ANodeShuffleSubsystem::RwgResettleWells(int32& Checked, int32& Redealt, int32& Kept, int32& Deferred, int32& Occupied, int32& MaxDzCm)
{
    // RWG P2d (replacing P2c's count-only pass): placed RWG well groups without the flag, nearest first, <= RwgResettleWellsPerPass
    // probed and <= RwgResettleWellRedealsPerPass redealt per pass. A group whose core or any member is FLOATING (its terrain-only
    // probe hit -- terrain or a solid stop -- lies more than RwgWellFloatCm from the member) or ROCK-OCCLUDED is torn down by
    // DespawnWellGroup and re-dealt (RwgRedealWellGroup). Built-on / pinned groups are never touched. A clean group is flagged.
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
    int32 RedealsLeft = NodeShuffleMapMode::RwgResettleWellRedealsPerPass;
    int32 Floating = 0, RockOccluded = 0, DealFailed = 0, WaitingBudget = 0;
    for (int32 K = 0; K < N; ++K)
    {
        FNodeShuffleWellEntry& E = WellLayout[Cands[K].Value];
        ++Checked;
        TArray<AFGResourceNodeBase*> Members;
        Members.Add(SpawnedWellCores.FindRef(E.CorePath));
        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            if (!S.bPlaced) { continue; }
            AFGResourceNodeFrackingSatellite* Sat = SpawnedWellSatellites.FindRef(S.SatellitePath);
            if (IsValid(Sat)) { Members.Add(Sat); }
        }
        bool bInUse = E.bPinned;
        for (AFGResourceNodeBase* M : Members) { const TCHAR* WhyUse = TEXT(""); if (IsWellMemberInUse(M, WhyUse)) { bInUse = true; } }
        if (bInUse) { ++Occupied; RwgResettleWellGaveUp.Add(E.CorePath); continue; } // never touch a well someone built on
        bool bAllHit = true;
        int32 FloatM = 0, OccM = 0, WorstDz = 0;
        for (AFGResourceNodeBase* M : Members)
        {
            const FVector Cur = M->GetActorLocation();
            FVector HitLoc = Cur;
            FRotator Rot = M->GetActorRotation();
            bool bWater = false, bSteep = false;
            if (!RaycastGroundAt(Cur, static_cast<float>(Cur.Z), M, nullptr, HitLoc, Rot, bWater, false, &bSteep,
                                 nullptr, nullptr, /*bTerrainOnly=*/true)) { bAllHit = false; break; }
            const bool bSolidColumn = bRwgLastProbeSolid; // written by the RaycastGroundAt just above
            const double Dz = HitLoc.Z - Cur.Z;
            WorstDz = FMath::Max(WorstDz, static_cast<int32>(FMath::Abs(Dz)));
            if (!bWater && FMath::Abs(Dz) > NodeShuffleMapMode::RwgWellFloatCm) { ++FloatM; }
            FString Mesh;
            if (bSolidColumn || RwgRockOcclusionProbe(Cur, M, Mesh) == 1) { ++OccM; } // P2d review S1: a member standing on a solid mesh is redealt, as a node is
        }
        if (!bAllHit)
        {
            ++Deferred; if (Cands[K].Key < 25000.0) { int32& V = RwgResettleWellVoids.FindOrAdd(E.CorePath); if (++V >= NodeShuffleMapMode::RwgResettleVoidGiveUp) { RwgResettleWellGaveUp.Add(E.CorePath); } }
            continue;
        }
        MaxDzCm = FMath::Max(MaxDzCm, WorstDz);
        if (FloatM == 0 && OccM == 0) { ++Kept; E.bRwgSettledOnTerrain = true; continue; }
        if (FloatM > 0) { ++Floating; }
        if (OccM > 0) { ++RockOccluded; }
        if (RedealsLeft <= 0) { ++WaitingBudget; continue; } // not given up: a later pass redeals it
        if (RwgWellResettleSalt.FindRef(E.CorePath) >= 2) { RwgResettleWellGaveUp.Add(E.CorePath); UE_LOG(LogNodeShuffle, Display, TEXT("RESETTLE-WELL-GAVEUP: core='%s' resettleRedealsThisSession=%d membersDzOverLimit=%d membersSolid=%d; not redealt again this session"), *WellShort(E.CorePath), RwgWellResettleSalt.FindRef(E.CorePath), FloatM, OccM); continue; }
        --RedealsLeft;
        bool bDealt = false;
        if (!RwgRedealWellGroup(E, TEXT("RWG resettle: member probe dZ over limit or solid mesh above"), bDealt)) { ++Occupied; RwgResettleWellGaveUp.Add(E.CorePath); continue; }
        ++Redealt;
        if (!bDealt) { ++DealFailed; }
    }
    if (Redealt > 0 || Kept > 0 || Occupied > 0 || Floating > 0 || RockOccluded > 0 || (Checked > 0 && (RwgRsPasses % 12) == 1))
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RESETTLE-WELL: pass=%d checked=%d redealt=%d kept=%d refusedBuiltOn=%d deferred=%d groupsMemberDzOverLimit=%d groupsRockOccluded=%d waitingRedealBudget=%d newSiteNotDealt=%d maxDzCm=%d"),
            RwgRsPasses, Checked, Redealt, Kept, Occupied, Deferred, Floating, RockOccluded, WaitingBudget, DealFailed, MaxDzCm);
    }
}
