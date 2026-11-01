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
        // fix F9 (2026-10-31): every clause states what RwgProvisionalSpawnPass / the attempt loop do; no universal or simultaneous claim.
        // rwg-farspawn-1e C1: also the within-radius spots whose ground fit this pass left no node (RwgProvisionalSpawnPass FreshNoActor).
        TEXT("RWG maps only. 1 (default): new node spots that have no node yet, farther than the attempt radius (1 km, or the ")
        TEXT("spawn radius if smaller) from every player or within it after this pass's ground fit left no node there, get a node placed, up to a batch per spawn pass, once the sea check has ")
        TEXT("cleared them (or without it where this world has no usable sea check); spots set aside this session (parked, dormant, refused, pinned, or already fitted to the ground) are skipped, ")
        TEXT("and so are spots still where a ground fit this session found them unplaceable, and underground spots within the radius. ")
        TEXT("They are placed at the world generator's surface height (cave spots at their dealt cave floor). On each later spawn pass with a player ")
        TEXT("within that radius, the normal spawn check tries to re-fit each one to the ground, which can also move it. 0: no new ")
        TEXT("nodes are placed this way; nodes already placed stay and are still re-fitted on approach. Read each spawn pass."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarNodeShuffleRwgProvisionalBatch(
        TEXT("NodeShuffle.RwgProvisionalBatchPerPass"),
        16, // rwg-farspawn-1c C2: was 64 (run 1 measured 44-63 ms per 64-node batch)
        TEXT("RWG maps only. How many far node spots get their node placed per spawn pass (about every 5 seconds) while ")
        TEXT("NodeShuffle.RwgSpawnAllAtRoll is 1. Default 16. Values are clamped to 8..512. A hard cap: the time budget ")
        TEXT("NodeShuffle.RwgProvisionalBatchMs can end a pass's batch sooner. Read each spawn pass."),
        ECVF_Default);
    // rwg-farspawn-1e C2: the time budget beside the count cap.
    TAutoConsoleVariable<float> CVarNodeShuffleRwgProvisionalBatchMs(
        TEXT("NodeShuffle.RwgProvisionalBatchMs"),
        8.0f,
        TEXT("RWG maps only. Milliseconds a spawn pass may spend placing nodes while NodeShuffle.RwgSpawnAllAtRoll is 1, counted from ")
        TEXT("the start of the pass's candidate scan. Placing stops when this is spent or NodeShuffle.RwgProvisionalBatchPerPass nodes ")
        TEXT("are placed, whichever comes first; the first node of a pass is always tried. Default 8. Values are clamped to 1..50. Read each spawn pass."),
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

float NodeShuffleMapMode::RwgProvisionalBatchMs()
{
    return FMath::Clamp(CVarNodeShuffleRwgProvisionalBatchMs.GetValueOnGameThread(), RwgProvisionalBatchMsMin, RwgProvisionalBatchMsMax);
}

void ANodeShuffleSubsystem::RwgNoteWetSpot(const FNodeShuffleEntry& E)
{
    // rwg-farspawn-1e C1: called beside every WaterLockedThisSession.Add (the fresh spawn path, settle-near-players, branch (b)
    // below) BEFORE the redeal moves the entry, so the recorded spot is the one the ground fit found unplaceable.
    RwgWetSpotThisSession.Add(E.EntryGuid, E.Location);
}

bool ANodeShuffleSubsystem::RwgProvisionalAtWetSpot(const FNodeShuffleEntry& E) const
{
    // Water-locked this session AND still at the lock spot (2-D, RwgProvisionalMoveTolCm), or locked with no recorded spot
    // (conservative). A redeal that moved the entry off the spot makes it eligible at its new spot.
    if (!WaterLockedThisSession.Contains(E.EntryGuid)) { return false; }
    const FVector* At = RwgWetSpotThisSession.Find(E.EntryGuid);
    return !At || FVector::Dist2D(*At, E.Location) <= NodeShuffleMapMode::RwgProvisionalMoveTolCm;
}

const TCHAR* ANodeShuffleSubsystem::RwgProvisionalOriginName(const FGuid& G) const
{
    // notThisSession = the actor was provisioned before this session's load (the origin map is session-only, never saved).
    const bool* W = RwgProvOriginWithin.Find(G);
    return !W ? TEXT("notThisSession") : *W ? TEXT("withinRadius") : TEXT("far");
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
        UE_LOG(LogNodeShuffle, Display, TEXT("PROVSETTLE: entry=%s res=%s provZ=%.0f settledZ=%.0f dzCm=0 movedXYm=0.0 outcome=pinnedKept hitClass=-1 pinned=%d occupied=%d origin=%s"),
            *E.EntryGuid.ToString(), *ProvShort(E.AssignedResourceClassPath), Z, Z, E.bPinned ? 1 : 0, bOccupied ? 1 : 0, RwgProvisionalOriginName(E.EntryGuid));
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
    // rwg-farspawn-1c C1: which settle branch may keep the actor. moveCheck prints the predicate that decided it (moved / xyShift /
    // classDiffers / resourceDiffers / noActor / notSettled); only "moved" keeps the actor, every other value takes the respawn below.
    const TCHAR* MoveCheck = bSettled ? RwgProvisionalMoveCheck(E, Node, ProvAt) : TEXT("notSettled");
    // Readback (the RESETTLE-REFUSED precedent, NodeShuffleMapModeResettle.cpp): whether the engine applies a move is closed-source (a
    // non-Movable root refuses it with a warning only). A move the actor did not take prints moveNotApplied and takes the respawn branch.
    if (bSettled && FCString::Strcmp(MoveCheck, TEXT("moved")) == 0 && !RwgProvisionalMoveInPlace(E, Node)) { MoveCheck = TEXT("moveNotApplied"); }
    if (bSettled && FCString::Strcmp(MoveCheck, TEXT("moved")) == 0)
    {
        // (a1) settled at the SAME XY (shift <= RwgProvisionalMoveTolCm), same node class + resource, and the move read back: the SAME
        // actor now stands at the settled spot (identity, compass marker and radar reveal kept; NodeShuffleMapModeProvisionalMove.cpp
        // lists what it re-ran).
        E.bRwgProvisional = false;
        E.bRayCasted = true;
        bChangedWorld = true; // scanner cluster midpoints + radar tower lists are rebuilt from the moved actor
        FreshAt = Node->GetActorLocation();
        bFresh = true;
        LocalRevealsAfter = Node->mLocalMapReveals; ServerRevealsAfter = Node->mServerMapReveals; // fix F6: same actor, so Before==After is the expected read
        SettledZ = FreshAt.Z;
        RwgProvSessDzCm.Add(static_cast<float>(FMath::Abs(FreshAt.Z - ProvAt.Z)));
        RwgProvSessMovedXYmMax = FMath::Max(RwgProvSessMovedXYmMax, FVector::Dist2D(FreshAt, ProvAt) / 100.0);
        ++RwgProvSessSettled; ++RwgPcProvSettled; ++RwgProvSessMovedInPlace;
        Outcome = TEXT("movedInPlace");
    }
    else if (bSettled)
    {
        // (a2) settled at a different XY (or class/resource differ, or the move did not read back): the provisional actor is torn down FIRST, then the unchanged spawn
        // path materializes the node at the settled spot with bRayCasted already true. Counted as settled only when that spawn produced
        // an actor (tested just below).
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
        else { ++RwgProvSessSettledNoActor; }
        Outcome = bRespawned ? TEXT("inPlace") : TEXT("settledNoActor");
    }
    else if (bWaterNoLand)
    {
        // (b) unplaceable here: despawn BEFORE the redeal moves the entry, then the spawn path's own redeal (ring, else map-wide).
        // settledZ stays nan: a redeal is not a settle. redealNotMoved = the redeal call returned false (the entry did not move).
        RwgNoteWetSpot(E); // rwg-farspawn-1e C1: the lock spot, before the redeal moves the entry
        RwgProvisionalDespawn(E);
        E.bRwgProvisional = false;
        ++NodeGateSettleRejectedWater;
        WaterLockedThisSession.Add(E.EntryGuid);
        const bool bMoved = TryRedealWaterLockedEntry(E);
        bChangedWorld = true;
        if (bMoved) { ++RwgProvSessRedealt; } else { ++RwgProvSessRedealNotMoved; }
        ++DeferredThisPass; // parity with EnsureNewNodeSpawned: its unsettled water branch counts as a deferral (NodeShuffleSubsystem.cpp DeferredThisPass++)
        Outcome = !bMoved ? TEXT("redealNotMoved") : (RwgPcSteepRing > RingBefore) ? TEXT("ring") : TEXT("mapwide");
    }
    else
    {
        // (c) no terrain-mesh hit in the window: the entry and the actor stay exactly as they were (provisional); retried next pass.
        E = Snap;
        ++NodeGateSettleRejectedNoWater;
        ++DeferredThisPass;
        ++RwgProvSessDeferred;
        RwgProvSessDeferredEntries.Add(E.EntryGuid); // rwg-farspawn-1e C4: distinct entries (deferredEntries) beside the event count
    }
    bRwgSteepArmed = false; // as the attempt loop: an arm never outlives the attempt that set it
    if (RwgProvSettleSamplePass != RwgPcPasses) { RwgProvSettleSamplePass = RwgPcPasses; RwgProvSettleSamples = 0; }
    if (RwgProvSettleSamples < NodeShuffleMapMode::RwgProvisionalSettleSamplesPerPass)
    {
        ++RwgProvSettleSamples;
        // movedXYm: from the provisional actor to the respawned actor (inPlace), else to the entry's location after this call.
        const FVector MovedTo = bFresh ? FreshAt : E.Location;
        UE_LOG(LogNodeShuffle, Display, TEXT("PROVSETTLE: entry=%s res=%s provZ=%.0f settledZ=%.0f dzCm=%.0f movedXYm=%.1f outcome=%s hitClass=%d apiUsed=%d onSolidTop=%d localMapRevealsBefore=%d localMapRevealsAfter=%d serverMapRevealsBefore=%d serverMapRevealsAfter=%d moveCheck=%s origin=%s"),
            *E.EntryGuid.ToString(), *ProvShort(E.AssignedResourceClassPath), ProvAt.Z, SettledZ, SettledZ - ProvAt.Z,
            FVector::Dist2D(MovedTo, ProvAt) / 100.0, Outcome, HitClass, bApiUsed ? 1 : 0, E.bRwgOnSolidTop ? 1 : 0,
            LocalRevealsBefore, LocalRevealsAfter, ServerRevealsBefore, ServerRevealsAfter, MoveCheck, RwgProvisionalOriginName(E.EntryGuid));
    }
}

void ANodeShuffleSubsystem::RwgProvisionalSpawnPass(const TArray<FVector>& Pawns, float AttemptRadiusCm, const TSet<int32>& FreshNoActor, bool& bChangedWorld)
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
    TSet<int32> WithinCands; // rwg-farspawn-1e C1: candidates within the attempt radius (origin=withinRadius)
    int32 SeaPending = 0, CaveDealtZ = 0, RayCastedNoActor = 0, SeaDealFailed = 0, WetSpot = 0, WithinUnderground = 0;
    for (int32 I = 0; I < Layout.Num(); ++I)
    {
        const FNodeShuffleEntry& E = Layout[I];
        if (!E.bIsNewNode || !E.bActive || E.bPinned) { continue; }
        if (DormantThisSession.Num() > 0 && DormantThisSession.Contains(E.EntryGuid)) { continue; }
        if (SpawnParkedThisSession.Num() > 0 && SpawnParkedThisSession.Contains(E.EntryGuid)) { continue; }
        if (RwgProvRefused.Contains(E.EntryGuid)) { continue; }
        if (IsValid(SpawnedNodes.FindRef(E.EntryGuid))) { continue; }
        // rwg-farspawn-1e C1: within the attempt radius, only an entry whose FRESH attempt this pass (the attempt loop, which ran
        // first) left no actor; a within entry that loop did not reach this pass (budget) is left to it, so the fresh attempt comes first.
        const bool bWithin = IsLocationNearAnyPlayer(E.Location, AttemptRadiusCm);
        if (bWithin && !FreshNoActor.Contains(I)) { continue; } // within: the attempt loop spawns it (settled)
        if (bWithin && E.bUnderground) { ++WithinUnderground; continue; } // the fresh path's close-miss cave escape (8 misses) counts only there
        if (E.bRayCasted) { ++RayCastedNoActor; continue; } // fix F7 (V7): a settled entry keeps its settled spot; it spawns through the attempt loop
        if (RwgSeaDealFailed.Contains(E.EntryGuid)) { ++SeaPending; ++SeaDealFailed; continue; } // fix F7 (V7b): flagged by the sea pass, not re-dealt: no provisional node
        const bool bSeaDone = bSeaPassIdle || (E.bRwgSeaChecked && (E.bRwgVoidChecked || !bVoidMaybe));
        if (!bSeaDone) { ++SeaPending; continue; }
        if (RwgProvisionalAtWetSpot(E)) { ++WetSpot; continue; } // rwg-farspawn-1e C1, both origins: never at a spot a ground fit found unplaceable
        double Best = TNumericLimits<double>::Max();
        for (const FVector& P : Pawns) { Best = FMath::Min(Best, FVector::DistSquared2D(P, E.Location)); }
        Cands.Emplace(Best, I);
        if (bWithin) { WithinCands.Add(I); }
    }
    Cands.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; }); // nearest to any player first
    const int32 Batch = NodeShuffleMapMode::RwgProvisionalBatchPerPass();
    const int32 N = FMath::Min(Cands.Num(), Batch);
    const double BudgetMs = NodeShuffleMapMode::RwgProvisionalBatchMs(); // rwg-farspawn-1e C2
    const TCHAR* BudgetHit = TEXT("none");
    int32 Spawned = 0, Refused = 0, WithinSpawned = 0, K = 0;
    FString ZSource = TEXT("api");
    for (; K < N; ++K)
    {
        if (K > 0 && (FPlatformTime::Seconds() - T0) * 1000.0 >= BudgetMs) { BudgetHit = TEXT("time"); break; } // rwg-farspawn-1e C2: the first is always tried
        FNodeShuffleEntry& E = Layout[Cands[K].Value];
        // A provisional attempt never changes the entry unless it produced an actor: snapshot, restore on no actor (an overlap nudge
        // counter, an inside-gate or overlap give-up, a moved location are all undone; the normal spawn on approach runs them for real).
        const FNodeShuffleEntry Snap = E;
        if (!E.bRwgCave)
        {
            const double Z = NodeShuffleMapMode::RwgProvisionalZ(Q, E.Location.X, E.Location.Y, ZSource);
            if (!FMath::IsFinite(Z)) { ++Refused; ++RwgProvSessRefused; RwgProvRefused.Add(E.EntryGuid); RwgProvisionalLogRefusal(E, TEXT("zNonFinite")); continue; }
            E.Location.Z = Z;
        }
        else { ++CaveDealtZ; } // a carved-cave entry keeps its dealt cave-floor Z (its settle reads it as the floor hint)
        E.bRayCasted = true; // skips the settle block only: the spawn below is the unchanged EnsureNewNodeSpawned chain
        const int32 FailsBefore = SpawnFailCounts.FindRef(E.EntryGuid); // rwg-farspawn-1c C6: read before the spawn for refusedReason
        EnsureNewNodeSpawned(E, bChangedWorld);
        AFGResourceNode* Node = SpawnedNodes.FindRef(E.EntryGuid);
        if (!IsValid(Node))
        {
            // rwg-farspawn-1c C6: the predicate the entry's own state shows after the spawn call, read BEFORE the snapshot restore.
            const TCHAR* Reason = !E.bActive ? TEXT("inactiveAfterSpawn")
                : (E.OverlapNudges != Snap.OverlapNudges) ? TEXT("overlapNudgeFailed")
                : (SpawnFailCounts.FindRef(E.EntryGuid) > FailsBefore) ? TEXT("spawnActorNull")
                : TEXT("noActorOtherReturn");
            E = Snap;
            ++Refused; ++RwgProvSessRefused;
            RwgProvRefused.Add(E.EntryGuid);
            RwgProvisionalLogRefusal(E, Reason);
            continue;
        }
        E.bRayCasted = false;      // never trace-settled
        E.bRwgProvisional = true;
        if (Node->IsHidden()) { Node->SetActorHiddenInGame(false); } // provisional actors are NOT hidden (scanner hook, radar, marker)
        if (!Node->GetActorEnableCollision()) { Node->SetActorEnableCollision(true); } // and keep collision ON (scanner hook test)
        ++Spawned; ++RwgProvSessSpawned; ++RwgPcProvSpawned;
        const bool bWithinCand = WithinCands.Contains(Cands[K].Value);
        RwgProvOriginWithin.Add(E.EntryGuid, bWithinCand); // PROVSETTLE origin (session)
        if (bWithinCand) { ++WithinSpawned; ++RwgProvSessWithinRadius; }
    }
    if (K == N && Cands.Num() > N) { BudgetHit = TEXT("count"); } // rwg-farspawn-1e C2: the count cap ended the batch with candidates left
    const double MsBatch = (FPlatformTime::Seconds() - T0) * 1000.0;
    if (Spawned > 0 || Refused > 0)
    {
        int32 Total = 0;
        for (const FNodeShuffleEntry& E : Layout) { if (E.bRwgProvisional && IsValid(SpawnedNodes.FindRef(E.EntryGuid))) { ++Total; } }
        // remainingCandidates (fix F10): entries that passed every skip and the sea check this pass, minus this pass's spawned and
        // refused; it does NOT include seaPending entries. rwg-farspawn-1e (prose only): it includes the within-radius candidates (fresh
        // attempts this pass that left no actor), whose count is withinRadiusCandidates; withinRadiusProvisioned counts those given a node
        // this pass; wetSpotSkipped counts entries skipped because they are water-locked and still at their lock spot; withinUndergroundSkipped
        // counts within-radius underground entries left to the fresh path. budgetHit names what ended the batch: time (the time budget),
        // count (the count cap, with candidates left) or none (candidates ran out first); msBatch and budgetMs share one clock, started
        // before the candidate scan.
        // seaPending (re-review R7): entries (far, or within after a fresh attempt that left no actor) held back for the sea/void pass -- those it has not checked yet, PLUS the entries whose
        // sea re-deal found no spot (RwgSeaDealFailed), which the sea pass skips for the rest of the session, so the sea pass never clears them from this count
        // this session; seaDealFailed prints that subset.
        UE_LOG(LogNodeShuffle, Display, TEXT("PROVSPAWN: pass=%d spawnedThisPass=%d remainingCandidates=%d provisionalTotal=%d zSource=%s msBatch=%.1f refusedThisPass=%d refusedSession=%d batch=%d seaPending=%d caveDealtZ=%d rayCastedNoActor=%d seaDealFailed=%d withinRadiusProvisioned=%d withinRadiusCandidates=%d wetSpotSkipped=%d withinUndergroundSkipped=%d budgetHit=%s budgetMs=%.1f"),
            RwgPcPasses, Spawned, Cands.Num() - Spawned - Refused, Total, *ZSource, MsBatch, Refused, RwgProvSessRefused, Batch, SeaPending, CaveDealtZ,
            RayCastedNoActor, SeaDealFailed, WithinSpawned, WithinCands.Num(), WetSpot, WithinUnderground, BudgetHit, BudgetMs);
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
    // Legend (re-review V-R3, prose only): the settledNoActor and redealNotMoved fields are session counts of two settle outcomes. settledNoActor counts settles
    // where the settle call returned true but no valid actor existed after the respawn. redealNotMoved counts unplaceable-branch
    // settles where the redeal call returned false (the entry did not move); a redeal that returned true also leaves no actor until
    // the next spawn and is counted in redealtFromProvisional, not here. Both match the PROVSETTLE outcome strings of the same names,
    // which are a sample; these are the census.
    // rwg-farspawn-1c C1 (prose only): the movedInPlace tail is the session count of settles that kept and moved the same actor; it is a
    // subset of settledFromProvisional (the rest of that count were respawned).
    // rwg-farspawn-1d F1/F2 (prose only): the moveNotApplied, movedEnclosed and movedShadowInside fields are session counts.
    // moveNotApplied counts moves whose readback found the actor more than 10 cm from the settled spot (each was then respawned,
    // so it is NOT in the movedInPlace count). movedEnclosed and movedShadowInside count kept-and-moved settles whose shadow
    // measurement at the settled spot read enclosed, and read totally inside by the inside gate's instrument; neither refused the move.
    // rwg-farspawn-1e C1/C4 (prose only): deferredProvisional counts deferred-branch settles, one per attempt, so an entry deferred on
    // several passes is counted each time; deferredEntries counts the distinct entries among them this session.
    // withinRadiusProvisionedSession counts this session's provisional spawns of entries within the attempt radius (fresh attempts
    // that left no actor); it is a subset of spawnedSession.
    UE_LOG(LogNodeShuffle, Display, TEXT("PROVCENSUS: pass=%d provisional=%d settledFromProvisional=%d redealtFromProvisional=%d deferredProvisional=%d deferredEntries=%d pinnedProvisional=%d dzP50Cm=%.0f dzP90Cm=%.0f dzMaxCm=%.0f movedXYmMax=%.1f spawnedSession=%d withinRadiusProvisionedSession=%d refusedSession=%d staleFlagCleared=%d flagNoActor=%d spawnAllAtRoll=%d settledNoActor=%d redealNotMoved=%d movedInPlace=%d moveNotApplied=%d movedEnclosed=%d movedShadowInside=%d"),
        RwgPcPasses, Live, RwgProvSessSettled, RwgProvSessRedealt, RwgProvSessDeferred, RwgProvSessDeferredEntries.Num(), RwgProvSessPinned,
        ProvPercentile(RwgProvSessDzCm, 0.5f), ProvPercentile(RwgProvSessDzCm, 0.9f), ProvPercentile(RwgProvSessDzCm, 1.0f), RwgProvSessMovedXYmMax,
        RwgProvSessSpawned, RwgProvSessWithinRadius, RwgProvSessRefused, RwgProvSessStale, FlagNoActor, NodeShuffleMapMode::RwgSpawnAllAtRollOn() ? 1 : 0, RwgProvSessSettledNoActor, RwgProvSessRedealNotMoved,
        RwgProvSessMovedInPlace, RwgProvSessMoveNotApplied, RwgProvSessMovedEnclosed, RwgProvSessMovedShadowInside);
}
