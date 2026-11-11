// ns-lead-restore-1 (2026-11-11): THE SETTLED RESTORE, option (i)-HOLD of _team/test-logs/2026-11-11-nodeshuffle-lead-restore-design.md as
// amended by the packet brief. Measured on one RWG load (build 2026-11-10-mcp-regrade-1): ADOPTCLASS printed 25 lead_C entries with 0 bound
// after adopt-on-load, and a pinned lead entry under a Miner Mk1 had no node (EXTRACTOR-UNBOUND-NOSPAWNEDNODE). The RWG provisional pass
// refuses pinned entries (NodeShuffleMapModeProvisionalScan.cpp, set-aside test) and bRayCasted entries (its per-pass test), so such an
// entry got an actor only from RwgSpawnPass's attempt loop, which needs a pawn within the attempt radius. Why those entries had no actor
// after adopt was not determined; nothing in this file states a cause.
//
// WHAT RUNS. Once per load, from ApplyLayout after adopt-on-load and the pinned heal and before RwgSpawnPass (off vanilla only):
//   1. ADOPTWORLD: one line per spawn class of any active new-node entry, before anything here spawns.
//   2. Every candidate (predicate below) is passed to EnsureNewNodeSpawned with a far-restore argument, pinned candidates first, each group
//      in Layout order. No pawn distance test, no attempt budget. Any refusing exit HOLDS: EnsureNewNodeSpawned records the exit in the
//      argument and returns before any write, so the entry keeps today's attempt-loop path on approach.
//   3. SETTLEDRESTORE census (always printed, also with zero candidates), per-entry SPAWNED/HELD lines (capped).
// CANDIDATE PREDICATE (the provisional scan's refusals by bPinned or bRayCasted, with the attempt loop's own exclusions):
//   bIsNewNode && bActive && no SpawnedNodes slot && !DormantThisSession && !SpawnParkedThisSession
//   && (bPinned || (bRayCasted && !RwgProvRefused))
// On a spawn, a set bRwgProvisional is cleared: RwgSpawnPass clears it on every actorless entry before its attempt loop, and a live actor
// carrying it would be re-settled (moved) by that loop.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffle.h"
#include "NodeShuffleSettledRestore.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffleResourceNode.h"
#include "NodeShuffleNodeComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceDeposit.h"
#include "Misc/PackageName.h"

namespace
{
    constexpr float SettledRestoreAdoptMirrorCm = 300.0f; // mirrors AdoptMatchRadiusCm (NodeShuffleSubsystem.cpp); lint leadrestore pins both
    constexpr float SettledRestoreGateRadiusCm = 800.0f;  // mirrors OverlapRejectRadius in EnsureNewNodeSpawned; lint leadrestore pins both
    constexpr int32 SettledRestoreSpawnedLinesMax = 60;
    constexpr int32 SettledRestoreHeldLinesMax = 40;

    // Persisted fields of one entry that differ between two copies (the hold contract's field half).
    int32 SettledRestoreFieldDiffs(const FNodeShuffleEntry& A, const FNodeShuffleEntry& B)
    {
        int32 D = 0;
        D += A.Location.Equals(B.Location, 0.0) ? 0 : 1;
        D += A.Rotation.Equals(B.Rotation, 0.0) ? 0 : 1;
        D += (A.AssignedResourceClassPath != B.AssignedResourceClassPath) ? 1 : 0;
        D += (A.AssignedPurity != B.AssignedPurity) ? 1 : 0;
        D += (A.NodeClassPath != B.NodeClassPath || A.SpawnNodeClassPath != B.SpawnNodeClassPath || A.RepClassSpawnPath != B.RepClassSpawnPath) ? 1 : 0;
        D += (A.OverlapNudges != B.OverlapNudges) ? 1 : 0;
        D += (A.bIsNewNode != B.bIsNewNode || A.bActive != B.bActive || A.bPinned != B.bPinned || A.bRayCasted != B.bRayCasted) ? 1 : 0;
        D += (A.bUnderground != B.bUnderground || A.bRwgCave != B.bRwgCave || A.bRwgOnSolidTop != B.bRwgOnSolidTop) ? 1 : 0;
        D += (A.bRwgSettledOnTerrain != B.bRwgSettledOnTerrain || A.bRwgRockChecked != B.bRwgRockChecked) ? 1 : 0;
        D += (A.bRwgSeaChecked != B.bRwgSeaChecked || A.bRwgVoidChecked != B.bRwgVoidChecked || A.bRwgProvisional != B.bRwgProvisional) ? 1 : 0;
        return D;
    }
}

void ANodeShuffleSubsystem::EmitAdoptWorldCensus()
{
    // Adopt pass 2 (AdoptRestoredSpawnedNodes) re-run in its own order on every AFGResourceNode whose class is the resolved spawn class of
    // an active new-node entry. This runs AFTER adopt, so an actor adopt bound is classified first (it now carries our component and its
    // entry is taken); every other actor takes adopt's tests in order: legacy class (pass 1's population), net-startup, fracking, our
    // component, then the entry search (active, slot present, 3-D distance under the adopt radius, resource agreement when both known).
    // The entry-search verdict is split: an untaken entry passes all tests (it would bind now), else an untaken entry within the radius
    // fails only the resource test, else an entry within the radius with agreeing resource has a slot, else no active entry is within it.
    struct FRow { int32 World = 0, Bound = 0, Legacy = 0, NetStartup = 0, Fracking = 0, Ours = 0, WouldBind = 0, ResDiffers = 0, Taken = 0, NoEntry = 0, NetStartupAll = 0, NullRes = 0; };
    TMap<FString, FRow> Rows;
    for (const FNodeShuffleEntry& E : Layout) { if (E.bIsNewNode && E.bActive) { Rows.FindOrAdd(ResolveSpawnNodeClassPath(E)); } }
    TSet<const AFGResourceNode*> BoundActors;
    for (const TPair<FGuid, AFGResourceNode*>& KV : SpawnedNodes) { if (IsValid(KV.Value)) { BoundActors.Add(KV.Value); } }
    const float MatchSq = FMath::Square(SettledRestoreAdoptMirrorCm);
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        AFGResourceNode* Node = *It;
        if (!IsValid(Node)) { continue; }
        FRow* Row = Rows.Find(Node->GetClass()->GetPathName());
        if (!Row) { continue; }
        ++Row->World;
        if (Node->IsNetStartupActor()) { ++Row->NetStartupAll; }
        if (!Node->GetResourceClass()) { ++Row->NullRes; }
        if (BoundActors.Contains(Node)) { ++Row->Bound; continue; }
        if (Node->IsA<ANodeShuffleResourceNode>()) { ++Row->Legacy; continue; }   // adopt: handled in pass 1
        if (Node->IsNetStartupActor()) { ++Row->NetStartup; continue; }           // adopt: level-placed original, not ours
        if (IsFrackingActor(Node)) { ++Row->Fracking; continue; }                 // adopt: ns-review-h2 F9
        if (UNodeShuffleNodeComponent::Find(Node)) { ++Row->Ours; continue; }     // adopt: already ours this session
        const FVector NodeLoc = Node->GetActorLocation();
        const FString NodeRes = Node->GetResourceClass() ? Node->GetResourceClass()->GetPathName() : FString();
        bool bWould = false, bResDiff = false, bTaken = false;
        for (const FNodeShuffleEntry& E : Layout)
        {
            if (!E.bIsNewNode || !E.bActive) { continue; }
            const float DSq = FVector::DistSquared(NodeLoc, E.Location);
            if (DSq >= MatchSq) { continue; }
            const bool bResOk = NodeRes.IsEmpty() || E.AssignedResourceClassPath.IsEmpty() || NodeRes == E.AssignedResourceClassPath;
            if (SpawnedNodes.Contains(E.EntryGuid)) { bTaken |= bResOk; continue; } // adopt: entry already has a live node (slot present)
            if (!bResOk) { bResDiff = true; continue; }
            bWould = true; break;
        }
        if (bWould) { ++Row->WouldBind; } else if (bResDiff) { ++Row->ResDiffers; } else if (bTaken) { ++Row->Taken; } else { ++Row->NoEntry; }
    }
    TArray<FString> Classes;
    Rows.GetKeys(Classes);
    Classes.Sort();
    for (const FString& C : Classes)
    {
        const FRow& R = Rows.FindChecked(C);
        UE_LOG(LogNodeShuffle, Display, TEXT("ADOPTWORLD class=%s worldActors=%d boundToEntry=%d unboundLegacyClass=%d unboundNetStartup=%d unboundFracking=%d unboundOurComponent=%d unboundWouldBindNow=%d unboundResourceDiffers=%d unboundEntryHasSlot=%d unboundNoActiveEntryIn300cm=%d netStartupAll=%d nullResourceAll=%d"),
            *FPackageName::ObjectPathToObjectName(C), R.World, R.Bound, R.Legacy, R.NetStartup, R.Fracking, R.Ours, R.WouldBind, R.ResDiffers, R.Taken, R.NoEntry,
            R.NetStartupAll, R.NullRes);
    }
}

void ANodeShuffleSubsystem::RunSettledRestoreOnce(float SpawnRadiusCm, bool& bChangedWorld)
{
    // Latch: armed by the first call that finds adopt-on-load done and a non-empty Layout (ApplyLayout always adopts first). Runtime only;
    // a re-roll later in the same session does not re-arm it.
    if (bSettledRestoreDone || !bAdoptedRestoredNodes || Layout.Num() == 0) { return; }
    bSettledRestoreDone = true;
    EmitAdoptWorldCensus();

    TArray<FVector> Pawns;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr;
        if (P) { Pawns.Add(P->GetActorLocation()); }
    }
    const float AttemptRadiusCm = FMath::Min(SpawnRadiusCm, NodeShuffleMapMode::RwgAttemptRadiusCm);

    int32 ActorlessActiveNew = 0, SkipDormant = 0, SkipParked = 0, SkipDeadSlot = 0, SkipProvRefused = 0, SkipUnpinnedUnsettled = 0;
    TArray<int32> Pinned, Rest;
    for (int32 I = 0; I < Layout.Num(); ++I)
    {
        const FNodeShuffleEntry& E = Layout[I];
        if (!E.bIsNewNode || !E.bActive) { continue; }
        AFGResourceNode* const* Slot = SpawnedNodes.Find(E.EntryGuid);
        if (Slot && IsValid(*Slot)) { continue; }
        ++ActorlessActiveNew;
        if (DormantThisSession.Contains(E.EntryGuid)) { ++SkipDormant; continue; }
        if (SpawnParkedThisSession.Contains(E.EntryGuid)) { ++SkipParked; continue; }
        if (Slot) { ++SkipDeadSlot; continue; } // a dead slot: EnsureNewNodeSpawned's external-destroy backoff owns it
        if (!E.bPinned && RwgProvRefused.Contains(E.EntryGuid)) { ++SkipProvRefused; continue; }
        if (!E.bPinned && !E.bRayCasted) { ++SkipUnpinnedUnsettled; continue; } // the provisional pass takes these at any distance
        (E.bPinned ? Pinned : Rest).Add(I);
    }
    TArray<int32> Cands = MoveTemp(Pinned);
    Cands.Append(Rest);

    // Session bookkeeping keyed by entry guid, as a bit mask (the hold contract's runtime half).
    const auto Memberships = [this](const FGuid& G) -> uint32
    {
        uint32 M = 0;
        M |= SpawnFailCounts.Contains(G) ? 1u << 0 : 0u;          M |= SpawnParkedThisSession.Contains(G) ? 1u << 1 : 0u;
        M |= DormantThisSession.Contains(G) ? 1u << 2 : 0u;       M |= ExternalDestroyCounts.Contains(G) ? 1u << 3 : 0u;
        M |= RwgProvRefused.Contains(G) ? 1u << 4 : 0u;           M |= WaterLockedThisSession.Contains(G) ? 1u << 5 : 0u;
        M |= RedealAttempts.Contains(G) ? 1u << 6 : 0u;           M |= DeferLoggedThisSession.Contains(G) ? 1u << 7 : 0u;
        M |= PinnedGateHoldLogged.Contains(G) ? 1u << 8 : 0u;     M |= PinnedBuildingSkipLogged.Contains(G) ? 1u << 9 : 0u;
        M |= SteadyAliveNodes.Contains(G) ? 1u << 10 : 0u;       M |= SpawnedNodes.Contains(G) ? 1u << 11 : 0u;
        M |= AdoptRotRefit.Contains(G) ? 1u << 12 : 0u;
        return M;
    };

    int32 Spawned = 0, HeldTotal = 0, HeldNoGateName = 0, HeldStateDiffs = 0, ProvFlagCleared = 0, SpawnedCut = 0, HeldCut = 0;
    int32 CandNoPawn = 0, CandBeyondAttempt = 0, CandWithinAttempt = 0;
    int32 Xtab[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }; // index = pinned*4 + rayCasted*2 + provisionalFlag
    TMap<FString, int32> HeldByGate;
    static const TCHAR* const GateNames[] = { TEXT("slotAlive"), TEXT("slotDead"), TEXT("classNull"), TEXT("notSettled"), TEXT("pinnedOccupied"),
        TEXT("occupiedNode"), TEXT("occupiedBuilding"), TEXT("enclosed"), TEXT("insideGate"), TEXT("spawnActorNull") }; // EnsureNewNodeSpawned's hold exits
    for (const TCHAR* G : GateNames) { HeldByGate.Add(G, 0); }
    for (const int32 I : Cands)
    {
        FNodeShuffleEntry& E = Layout[I];
        ++Xtab[(E.bPinned ? 4 : 0) + (E.bRayCasted ? 2 : 0) + (E.bRwgProvisional ? 1 : 0)];
        if (Pawns.Num() == 0) { ++CandNoPawn; }
        else if (!IsLocationNearAnyPlayer(E.Location, AttemptRadiusCm)) { ++CandBeyondAttempt; }
        else { ++CandWithinAttempt; }
        const double PawnDistM = (Pawns.Num() == 0) ? -1.0 : RwgNearestPawnDist2D(E.Location) / 100.0;
        const FNodeShuffleEntry Before = E;
        const uint32 MemBefore = Memberships(E.EntryGuid);
        const int32 DeferredBefore = DeferredThisPass;
        FNodeShuffleFarRestore Far;
        bool bLocalChanged = false;
        EnsureNewNodeSpawned(E, bLocalChanged, &Far);
        AFGResourceNode* Now = SpawnedNodes.FindRef(E.EntryGuid);
        if (IsValid(Now) && Far.HeldGate == nullptr)
        {
            ++Spawned;
            bChangedWorld |= bLocalChanged;
            if (E.bRwgProvisional) { E.bRwgProvisional = false; ++ProvFlagCleared; }
            if (Spawned > SettledRestoreSpawnedLinesMax) { ++SpawnedCut; continue; }
            int32 GateNodesWithin = 0; // the overlap gate's population: every AFGResourceNode except deposits, 3-D distance
            for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
            {
                if (!IsValid(*It) || It->IsA<AFGResourceDeposit>()) { continue; }
                if (FVector::DistSquared(It->GetActorLocation(), E.Location) < FMath::Square(SettledRestoreGateRadiusCm)) { ++GateNodesWithin; }
            }
            UE_LOG(LogNodeShuffle, Display, TEXT("SETTLEDRESTORE-SPAWNED guid=%s res=%s spawnClass=%s at=%s nearestPawnDist2DM=%.0f gateNodesWithin8mAfterSpawn=%d pinned=%d pinnedBuildingHitsIgnored=%d"),
                *E.EntryGuid.ToString(), *FPackageName::ObjectPathToObjectName(E.AssignedResourceClassPath), *Now->GetClass()->GetName(),
                *E.Location.ToCompactString(), PawnDistM, GateNodesWithin, E.bPinned ? 1 : 0, Far.PinnedBuildingHitsIgnored);
            continue;
        }
        ++HeldTotal;
        const int32 Diffs = SettledRestoreFieldDiffs(Before, E) + ((Memberships(E.EntryGuid) != MemBefore) ? 1 : 0)
            + ((DeferredThisPass != DeferredBefore) ? 1 : 0) + (bLocalChanged ? 1 : 0) + (IsValid(Now) ? 1 : 0);
        HeldStateDiffs += Diffs;
        const TCHAR* Gate = Far.HeldGate ? Far.HeldGate : TEXT("noGateRecorded");
        if (int32* Slot = HeldByGate.Find(Gate)) { ++*Slot; } else { ++HeldNoGateName; }
        if (HeldTotal > SettledRestoreHeldLinesMax) { ++HeldCut; continue; }
        UE_LOG(LogNodeShuffle, Display, TEXT("SETTLEDRESTORE-HELD guid=%s res=%s at=%s heldGate=%s hitKind=%s hitClass=%s hitPath=%s hitDistCm=%.0f pinned=%d nearestPawnDist2DM=%.0f entryStateDiffs=%d"),
            *E.EntryGuid.ToString(), *FPackageName::ObjectPathToObjectName(E.AssignedResourceClassPath), *E.Location.ToCompactString(), Gate,
            *Far.HitKind, *Far.HitClass, *Far.HitPath, Far.HitDistCm, E.bPinned ? 1 : 0, PawnDistM, Diffs);
    }
    // Legend (prose only). candidates counts the entries the predicate in this file's header took; the skip counters split the active
    // new-node entries with no valid actor that it did not take, by the first exclusion they met. Each held counter names the refusing exit
    // EnsureNewNodeSpawned recorded; heldNoGateRecorded counts held entries for which it recorded none. heldStateDiffs sums, over held
    // entries, the persisted fields and session bookkeeping that differ from their value before the call (the hold contract reads zero).
    // The cross-tab digits are pinned, rayCasted, provisional flag. The three distance counters split candidates by the pawns at this pass.
    UE_LOG(LogNodeShuffle, Display, TEXT("SETTLEDRESTORE candidates=%d spawned=%d held=%d heldSlotAlive=%d heldSlotDead=%d heldClassNull=%d heldNotSettled=%d heldPinnedOccupied=%d heldOccupiedNode=%d heldOccupiedBuilding=%d heldEnclosed=%d heldInsideGate=%d heldSpawnActorNull=%d heldNoGateRecorded=%d heldStateDiffs=%d xtabPinRayProv=111:%d/110:%d/101:%d/100:%d/011:%d/010:%d/001:%d/000:%d candNoPawn=%d candBeyondAttemptRadius=%d candWithinAttemptRadius=%d actorlessActiveNew=%d skipDormant=%d skipParked=%d skipDeadSlot=%d skipUnpinnedProvRefused=%d skipUnpinnedUnsettled=%d provFlagClearedOnSpawn=%d spawnedLinesCut=%d heldLinesCut=%d attemptRadiusM=%.0f pawns=%d"),
        Cands.Num(), Spawned, HeldTotal, HeldByGate[TEXT("slotAlive")], HeldByGate[TEXT("slotDead")], HeldByGate[TEXT("classNull")], HeldByGate[TEXT("notSettled")],
        HeldByGate[TEXT("pinnedOccupied")], HeldByGate[TEXT("occupiedNode")], HeldByGate[TEXT("occupiedBuilding")], HeldByGate[TEXT("enclosed")],
        HeldByGate[TEXT("insideGate")], HeldByGate[TEXT("spawnActorNull")], HeldNoGateName, HeldStateDiffs,
        Xtab[7], Xtab[6], Xtab[5], Xtab[4], Xtab[3], Xtab[2], Xtab[1], Xtab[0], CandNoPawn, CandBeyondAttempt, CandWithinAttempt,
        ActorlessActiveNew, SkipDormant, SkipParked, SkipDeadSlot, SkipProvRefused, SkipUnpinnedUnsettled, ProvFlagCleared, SpawnedCut, HeldCut,
        AttemptRadiusCm / 100.f, Pawns.Num());
}
