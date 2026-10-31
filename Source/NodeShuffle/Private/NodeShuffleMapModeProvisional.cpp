// rwg-farspawn-1 (2026-10-31) -- provisional far spawn on RWG + settle on approach. Header: NodeShuffleMapModeProvisional.h.
// Reached only from RwgSpawnPass (NodeShuffleMapModePlace.cpp), which runs only off vanilla; every entry point below also tests
// GetMapMode() == Rwg itself, so the vanilla map and Unknown maps never act here. Log lines print the predicate measured this
// run (counts, Z values, which branch ran), never a cause.
#include "NodeShuffleMapModeProvisional.h"
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceNodeManager.h"
#include "FGActorRepresentationManager.h"
#include "Representation/FGResourceNodeRepresentation.h"

namespace
{
    TAutoConsoleVariable<int32> CVarNodeShuffleRwgSpawnAllAtRoll(
        TEXT("NodeShuffle.RwgSpawnAllAtRoll"),
        1,
        TEXT("RWG maps only. 1 (default): every new node spot farther than 1 km from every player gets its node placed at once, at a ")
        TEXT("height read from the world generator, and is fitted to the ground when a player comes within 1 km. 0: such spots get ")
        TEXT("their node only when a player comes within 1 km. Read each spawn pass."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarNodeShuffleRwgProvisionalBatch(
        TEXT("NodeShuffle.RwgProvisionalBatchPerPass"),
        64,
        TEXT("RWG maps only. How many far node spots get their node placed per spawn pass (about every 5 seconds) while ")
        TEXT("NodeShuffle.RwgSpawnAllAtRoll is 1. Default 64. Values are clamped to 8..512. Read each spawn pass."),
        ECVF_Default);

    FString ProvShort(const FString& P) { int32 Dot = INDEX_NONE; return P.FindLastChar(TEXT('.'), Dot) ? P.Mid(Dot + 1) : P; }

    float ProvPercentile(TArray<float> V, float P)
    {
        if (V.Num() == 0) { return 0.f; }
        V.Sort();
        const int32 I = FMath::Clamp(FMath::CeilToInt(P * V.Num()) - 1, 0, V.Num() - 1);
        return V[I];
    }
}

double NodeShuffleMapMode::RwgProvisionalZ(const FRwgGroundQuery& Q, double X, double Y, FString& OutSource)
{
    // TODO(2026-10-31, rwg-farspawn-1 -> RWG mesh-stack binding packet): replace this body with the mesh-aware answer and
    // OutSource = "meshStack". Until then the provisional Z is the API surface, which sits 1-190 m BELOW an RWG cliff-mesh top
    // where one stands (2026-10-31 walk test) -- the settle on approach is what corrects it.
    OutSource = TEXT("api");
    return Q.SurfaceZ(X, Y);
}

bool NodeShuffleMapMode::RwgSpawnAllAtRollOn() { return CVarNodeShuffleRwgSpawnAllAtRoll.GetValueOnGameThread() != 0; }

int32 NodeShuffleMapMode::RwgProvisionalBatchPerPass()
{
    return FMath::Clamp(CVarNodeShuffleRwgProvisionalBatch.GetValueOnGameThread(), RwgProvisionalBatchMin, RwgProvisionalBatchMax);
}

void ANodeShuffleSubsystem::RwgProvisionalDespawn(FNodeShuffleEntry& E)
{
    // The teardown half of RwgRedealLiveEntry (NodeShuffleMapModeRock.cpp), same order: Mk1 snap list, representation, scan reveal,
    // Destroy, unregister AFTER Destroy, and the SpawnedNodes slot removed in this same block (coexist-1 INVARIANT: a lingering
    // invalid slot reads as an outside destroyer in EnsureNewNodeSpawned). No redeal here: the caller decides what follows.
    AFGResourceNode* Node = SpawnedNodes.FindRef(E.EntryGuid);
    if (Node)
    {
        MeshActorCache.Remove(TWeakObjectPtr<AFGResourceNodeBase>(Node));
        if (IsValid(Node))
        {
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
}

bool ANodeShuffleSubsystem::RwgProvisionalPinWins(FNodeShuffleEntry& E)
{
    // Pin wins: a provisional node a player built on (ApplyLayout's occupancy pin ran before this pass, or the node is occupied now)
    // is never moved. The flag is cleared and the node stays where it stands.
    AFGResourceNode* Node = SpawnedNodes.FindRef(E.EntryGuid);
    const bool bOccupied = IsValid(Node) && IsNodeOccupiedAnyway(Node);
    if (!E.bPinned && !bOccupied) { return false; }
    E.bRwgProvisional = false;
    ++RwgProvSessPinned;
    if (RwgProvSettleSamplePass != RwgPcPasses) { RwgProvSettleSamplePass = RwgPcPasses; RwgProvSettleSamples = 0; }
    if (RwgProvSettleSamples < NodeShuffleMapMode::RwgProvisionalSettleSamplesPerPass)
    {
        ++RwgProvSettleSamples;
        const double Z = IsValid(Node) ? Node->GetActorLocation().Z : E.Location.Z;
        UE_LOG(LogNodeShuffle, Display, TEXT("PROVSETTLE: entry=%s res=%s provZ=%.0f settledZ=%.0f dzCm=0 movedXYm=0.0 outcome=pinnedKept hitClass=-1 pinned=%d occupied=%d"),
            *E.EntryGuid.ToString(), *ProvShort(E.AssignedResourceClassPath), Z, Z, E.bPinned ? 1 : 0, bOccupied ? 1 : 0);
    }
    return true;
}

void ANodeShuffleSubsystem::RwgSettleProvisional(FNodeShuffleEntry& E, bool& bChangedWorld)
{
    if (GetMapMode() != ENodeShuffleMapMode::Rwg || !E.bRwgProvisional) { return; }
    AFGResourceNode* Node = SpawnedNodes.FindRef(E.EntryGuid);
    if (!IsValid(Node)) { E.bRwgProvisional = false; ++RwgProvSessStale; return; } // the caller collected it live; counted, not settled
    if (RwgProvisionalPinWins(E)) { return; }
    const FVector ProvAt = Node->GetActorLocation();
    // fix F6 (measure only): the node's scanner / radar reveal counts (AFGResourceNodeBase mLocalMapReveals / mServerMapReveals), read
    // BEFORE the settle can despawn it; the respawned actor's counts are read after its spawn (-1 = no respawned actor).
    const int32 LocalRevealsBefore = Node->mLocalMapReveals, ServerRevealsBefore = Node->mServerMapReveals;
    int32 LocalRevealsAfter = -1, ServerRevealsAfter = -1;
    const FNodeShuffleEntry Snap = E;
    // The settle's probes must not meet the provisional node's own body: collision off for the synchronous settle only, back on
    // before anything else can read it (the scanner hook keeps a node only while its collision is enabled).
    Node->SetActorEnableCollision(false);
    ++NodeGateSettleReached; // the same gate denominator EnsureNewNodeSpawned counts before its settle
    E.bRayCasted = false;
    const int32 RingBefore = RwgPcSteepRing;
    bool bApiUsed = false, bWaterNoLand = false;
    bool bSettled = RwgResolveAtSpawn(E, bApiUsed, bWaterNoLand); // THE EXISTING SETTLE PATH, unchanged (trace, cliff top, spiral, water/void/killbox)
    if (!bApiUsed) { bSettled = RaycastSettle(E, nullptr, nullptr, &bWaterNoLand); } // as EnsureNewNodeSpawned: unusable API -> RaycastSettle
    const int32 HitClass = RwgProbeFirstClass;
    if (IsValid(Node)) { Node->SetActorEnableCollision(true); }
    const TCHAR* Outcome = TEXT("deferred");
    double SettledZ = NodeShuffleMapMode::RwgNaN(); // the respawned ACTOR's Z only; nan on every other outcome (fix F4)
    FVector FreshAt = FVector::ZeroVector;
    bool bFresh = false;
    if (bSettled)
    {
        // (a) settled: the provisional actor is torn down FIRST, then the unchanged spawn path materializes the node at the settled
        // spot with bRayCasted already true. Counted as settled only when that spawn produced an actor (tested just below).
        RwgProvisionalDespawn(E);
        E.bRwgProvisional = false;
        E.bRayCasted = true;
        EnsureNewNodeSpawned(E, bChangedWorld);
        bChangedWorld = true;
        AFGResourceNode* Fresh = SpawnedNodes.FindRef(E.EntryGuid);
        const bool bRespawned = IsValid(Fresh);
        if (bRespawned)
        {
            FreshAt = Fresh->GetActorLocation(); // after any overlap nudge the spawn path applied
            bFresh = true;
            LocalRevealsAfter = Fresh->mLocalMapReveals; ServerRevealsAfter = Fresh->mServerMapReveals; // fix F6
            SettledZ = FreshAt.Z;
            RwgProvSessDzCm.Add(static_cast<float>(FMath::Abs(FreshAt.Z - ProvAt.Z)));
            RwgProvSessMovedXYmMax = FMath::Max(RwgProvSessMovedXYmMax, FVector::Dist2D(FreshAt, ProvAt) / 100.0);
            ++RwgProvSessSettled; ++RwgPcProvSettled;
        }
        Outcome = bRespawned ? TEXT("inPlace") : TEXT("settledNoActor");
    }
    else if (bWaterNoLand)
    {
        // (b) unplaceable here: despawn BEFORE the redeal moves the entry, then the spawn path's own redeal (ring, else map-wide).
        // settledZ stays nan: a redeal is not a settle. redealNotMoved = the redeal call returned false (the entry did not move).
        RwgProvisionalDespawn(E);
        E.bRwgProvisional = false;
        ++NodeGateSettleRejectedWater;
        WaterLockedThisSession.Add(E.EntryGuid);
        const bool bMoved = TryRedealWaterLockedEntry(E);
        bChangedWorld = true;
        if (bMoved) { ++RwgProvSessRedealt; }
        Outcome = !bMoved ? TEXT("redealNotMoved") : (RwgPcSteepRing > RingBefore) ? TEXT("ring") : TEXT("mapwide");
    }
    else
    {
        // (c) no terrain-mesh hit in the window: the entry and the actor stay exactly as they were (provisional); retried next pass.
        E = Snap;
        ++NodeGateSettleRejectedNoWater;
        ++DeferredThisPass;
        ++RwgProvSessDeferred;
    }
    bRwgSteepArmed = false; // as the attempt loop: an arm never outlives the attempt that set it
    if (RwgProvSettleSamplePass != RwgPcPasses) { RwgProvSettleSamplePass = RwgPcPasses; RwgProvSettleSamples = 0; }
    if (RwgProvSettleSamples < NodeShuffleMapMode::RwgProvisionalSettleSamplesPerPass)
    {
        ++RwgProvSettleSamples;
        // movedXYm: from the provisional actor to the respawned actor (inPlace), else to the entry's location after this call.
        const FVector MovedTo = bFresh ? FreshAt : E.Location;
        UE_LOG(LogNodeShuffle, Display, TEXT("PROVSETTLE: entry=%s res=%s provZ=%.0f settledZ=%.0f dzCm=%.0f movedXYm=%.1f outcome=%s hitClass=%d apiUsed=%d onSolidTop=%d localMapRevealsBefore=%d localMapRevealsAfter=%d serverMapRevealsBefore=%d serverMapRevealsAfter=%d"),
            *E.EntryGuid.ToString(), *ProvShort(E.AssignedResourceClassPath), ProvAt.Z, SettledZ, SettledZ - ProvAt.Z,
            FVector::Dist2D(MovedTo, ProvAt) / 100.0, Outcome, HitClass, bApiUsed ? 1 : 0, E.bRwgOnSolidTop ? 1 : 0,
            LocalRevealsBefore, LocalRevealsAfter, ServerRevealsBefore, ServerRevealsAfter);
    }
}

void ANodeShuffleSubsystem::RwgProvisionalSpawnPass(const TArray<FVector>& Pawns, float AttemptRadiusCm, bool& bChangedWorld)
{
    if (GetMapMode() != ENodeShuffleMapMode::Rwg) { return; } // RWG-only gate
    if ((RwgPcPasses % 12) == 1) { RwgProvisionalEmitCensus(); } // fix F5: before the toggle/pawn/query returns, so spawnAllAtRoll=0 can print
    if (!NodeShuffleMapMode::RwgSpawnAllAtRollOn() || Pawns.Num() == 0) { return; }
    const NodeShuffleMapMode::FRwgGroundQuery Q(GetWorld(), RwgGenerator.Get());
    if (!Q.IsUsable()) { return; }
    const double T0 = FPlatformTime::Seconds();
    // The sea/void re-deal pass (RwgSeaRedealPass) flags a placed entry without checking it, so an entry it has not checked yet is
    // left to it first: same per-entry done test it uses, plus its own idle state (sea grade unusable -> it never runs).
    const bool bVoidMaybe = !(bRwgVoidCheckDone && bRwgKillboxDone) || RwgVoidPassTermsOn();
    const bool bSeaPassIdle = bRwgSeaCheckDone && !bRwgSeaUsable;
    TArray<TPair<double, int32>> Cands;
    int32 SeaPending = 0, CaveDealtZ = 0;
    for (int32 I = 0; I < Layout.Num(); ++I)
    {
        const FNodeShuffleEntry& E = Layout[I];
        if (!E.bIsNewNode || !E.bActive || E.bPinned) { continue; }
        if (DormantThisSession.Num() > 0 && DormantThisSession.Contains(E.EntryGuid)) { continue; }
        if (SpawnParkedThisSession.Num() > 0 && SpawnParkedThisSession.Contains(E.EntryGuid)) { continue; }
        if (RwgProvRefused.Contains(E.EntryGuid)) { continue; }
        if (IsValid(SpawnedNodes.FindRef(E.EntryGuid))) { continue; }
        if (IsLocationNearAnyPlayer(E.Location, AttemptRadiusCm)) { continue; } // within: the attempt loop spawns it (settled)
        const bool bSeaDone = bSeaPassIdle || RwgSeaDealFailed.Contains(E.EntryGuid) || (E.bRwgSeaChecked && (E.bRwgVoidChecked || !bVoidMaybe));
        if (!bSeaDone) { ++SeaPending; continue; }
        double Best = TNumericLimits<double>::Max();
        for (const FVector& P : Pawns) { Best = FMath::Min(Best, FVector::DistSquared2D(P, E.Location)); }
        Cands.Emplace(Best, I);
    }
    Cands.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; }); // nearest to any player first
    const int32 Batch = NodeShuffleMapMode::RwgProvisionalBatchPerPass();
    const int32 N = FMath::Min(Cands.Num(), Batch);
    int32 Spawned = 0, Refused = 0;
    FString ZSource = TEXT("api");
    for (int32 K = 0; K < N; ++K)
    {
        FNodeShuffleEntry& E = Layout[Cands[K].Value];
        // A provisional attempt never changes the entry unless it produced an actor: snapshot, restore on no actor (an overlap nudge
        // counter, an inside-gate or overlap give-up, a moved location are all undone; the normal spawn on approach runs them for real).
        const FNodeShuffleEntry Snap = E;
        if (!E.bRwgCave)
        {
            const double Z = NodeShuffleMapMode::RwgProvisionalZ(Q, E.Location.X, E.Location.Y, ZSource);
            if (!FMath::IsFinite(Z)) { ++Refused; ++RwgProvSessRefused; RwgProvRefused.Add(E.EntryGuid); continue; }
            E.Location.Z = Z;
        }
        else { ++CaveDealtZ; } // a carved-cave entry keeps its dealt cave-floor Z (its settle reads it as the floor hint)
        E.bRayCasted = true; // skips the settle block only: the spawn below is the unchanged EnsureNewNodeSpawned chain
        EnsureNewNodeSpawned(E, bChangedWorld);
        AFGResourceNode* Node = SpawnedNodes.FindRef(E.EntryGuid);
        if (!IsValid(Node))
        {
            E = Snap;
            ++Refused; ++RwgProvSessRefused;
            RwgProvRefused.Add(E.EntryGuid);
            continue;
        }
        E.bRayCasted = false;      // never trace-settled
        E.bRwgProvisional = true;
        if (Node->IsHidden()) { Node->SetActorHiddenInGame(false); } // provisional actors are NOT hidden (scanner hook, radar, marker)
        if (!Node->GetActorEnableCollision()) { Node->SetActorEnableCollision(true); } // and keep collision ON (scanner hook test)
        ++Spawned; ++RwgProvSessSpawned; ++RwgPcProvSpawned;
    }
    const double MsBatch = (FPlatformTime::Seconds() - T0) * 1000.0;
    if (Spawned > 0 || Refused > 0)
    {
        int32 Total = 0;
        for (const FNodeShuffleEntry& E : Layout) { if (E.bRwgProvisional && IsValid(SpawnedNodes.FindRef(E.EntryGuid))) { ++Total; } }
        UE_LOG(LogNodeShuffle, Display, TEXT("PROVSPAWN: pass=%d spawnedThisPass=%d remainingUnspawned=%d provisionalTotal=%d zSource=%s msBatch=%.1f refusedThisPass=%d refusedSession=%d batch=%d seaPending=%d caveDealtZ=%d"),
            RwgPcPasses, Spawned, Cands.Num() - Spawned - Refused, Total, *ZSource, MsBatch, Refused, RwgProvSessRefused, Batch, SeaPending, CaveDealtZ);
    }
}

void ANodeShuffleSubsystem::RwgProvisionalEmitCensus()
{
    int32 Live = 0, FlagNoActor = 0;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bRwgProvisional) { continue; }
        if (IsValid(SpawnedNodes.FindRef(E.EntryGuid))) { ++Live; } else { ++FlagNoActor; }
    }
    UE_LOG(LogNodeShuffle, Display, TEXT("PROVCENSUS: pass=%d provisional=%d settledFromProvisional=%d redealtFromProvisional=%d deferredProvisional=%d pinnedProvisional=%d dzP50Cm=%.0f dzP90Cm=%.0f dzMaxCm=%.0f movedXYmMax=%.1f spawnedSession=%d refusedSession=%d staleFlagCleared=%d flagNoActor=%d spawnAllAtRoll=%d"),
        RwgPcPasses, Live, RwgProvSessSettled, RwgProvSessRedealt, RwgProvSessDeferred, RwgProvSessPinned,
        ProvPercentile(RwgProvSessDzCm, 0.5f), ProvPercentile(RwgProvSessDzCm, 0.9f), ProvPercentile(RwgProvSessDzCm, 1.0f), RwgProvSessMovedXYmMax,
        RwgProvSessSpawned, RwgProvSessRefused, RwgProvSessStale, FlagNoActor, NodeShuffleMapMode::RwgSpawnAllAtRollOn() ? 1 : 0);
}
