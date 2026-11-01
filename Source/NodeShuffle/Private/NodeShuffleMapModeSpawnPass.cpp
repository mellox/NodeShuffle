// rwg-farspawn-1f F10 (2026-10-31, no behaviour change): RwgSpawnPass + RwgEmitPlaceCensus moved here VERBATIM from
// NodeShuffleMapModePlace.cpp (521 lines, over the 500-line rule; 1e review F10). Lints that load Place.cpp now load this file as its
// continuation (rwgtrace D3 pins this span's sha256, unchanged by the move). Same contract as Place.cpp: reached only behind a map-mode
// test at its NodeShuffleSubsystem.cpp call site; log lines report measured counts and the predicate that branched, never a cause.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffleRwgTraceDiag.h" // rwg-trace-diag-1: RWGTRACEMISS / RWGTRACECENSUS (diagnostics only)
#include "NodeShuffleMapModeProvisional.h" // rwg-farspawn-1: provisional far spawn + settle on approach
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Resources/FGResourceNodeBase.h"
#include "Resources/FGResourceNode.h"

void ANodeShuffleSubsystem::RwgSpawnPass(float SpawnRadiusCm, bool& bChangedWorld)
{
    if (RwgWaterVolumes.Num() == 0) { RwgBuildWaterVolumeCache(); }
    RwgSeaRedealPass(); // RWG P2e (+P2f void/killbox): one-time re-deal of unplaced entries, before this pass picks spawn candidates (NodeShuffleMapModeSea.cpp)
    TArray<FVector> Pawns;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr;
        if (P) { Pawns.Add(P->GetActorLocation()); }
    }
    NodeShuffleRwgTraceDiag::BeginPass(RwgPcPasses + 1, Pawns); // rwg-trace-diag-1: per-pass reset; RwgPcPasses is incremented after the loop, so +1 = PLACECENSUS numbering
    const float AttemptRadiusCm = FMath::Min(SpawnRadiusCm, NodeShuffleMapMode::RwgAttemptRadiusCm);
    const int32 DeferredBefore = DeferredThisPass;
    const int32 WaterLockedBefore = WaterLockedThisSession.Num();
    TArray<TPair<double, int32>> Cands;
    for (int32 I = 0; I < Layout.Num(); ++I)
    {
        FNodeShuffleEntry& E = Layout[I];
        if (!E.bIsNewNode || !E.bActive) { continue; }
        if (DormantThisSession.Num() > 0 && DormantThisSession.Contains(E.EntryGuid)) { continue; }
        if (SpawnParkedThisSession.Num() > 0 && SpawnParkedThisSession.Contains(E.EntryGuid)) { continue; }
        AFGResourceNode* const* Live = SpawnedNodes.Find(E.EntryGuid);
        if (Live && IsValid(*Live))
        {
            // rwg-farspawn-1: a provisional actor (not trace-settled) within the attempt radius is settled by the attempt loop below, nearest first, same budget.
            if (E.bRwgProvisional && IsLocationNearAnyPlayer(E.Location, AttemptRadiusCm) && !RwgProvisionalPinWins(E)) { Cands.Emplace(FMath::Square(RwgNearestPawnDist2D(E.Location)), I); continue; }
            // Live actors keep the vanilla per-pass re-assert chain, uncapped, at the vanilla radius.
            if (IsLocationNearAnyPlayer(E.Location, SpawnRadiusCm)) { ++RwgPcLive; EnsureNewNodeSpawned(E, bChangedWorld); bRwgSteepArmed = false; }
            continue;
        }
        if (E.bRwgProvisional) { E.bRwgProvisional = false; ++RwgProvSessStale; } // rwg-farspawn-1: the flag without a live actor is cleared (counted in PROVCENSUS)
        if (!IsLocationNearAnyPlayer(E.Location, SpawnRadiusCm)) { continue; }
        if (!IsLocationNearAnyPlayer(E.Location, AttemptRadiusCm)) { ++RwgPcFarNoAttempt; continue; }
        double Best = TNumericLimits<double>::Max();
        for (const FVector& P : Pawns) { Best = FMath::Min(Best, FVector::DistSquared2D(P, E.Location)); }
        Cands.Emplace(Best, I);
    }
    Cands.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
    const int32 N = FMath::Min(Cands.Num(), NodeShuffleMapMode::RwgMaxSpawnAttemptsPerPass);
    TSet<int32> FreshNoActor; // rwg-farspawn-1e C1: fresh attempts this pass that ended with no actor (RwgProvisionalSpawnPass filters them)
    int32 K = 0;
    for (; K < N && K + RwgPcSteepRingRuns < NodeShuffleMapMode::RwgMaxSpawnAttemptsPerPass; ++K) // rwg-steep-1: each local ring run spends one more slot of the attempt budget
    {
        FNodeShuffleEntry& E = Layout[Cands[K].Value];
        ++RwgPcAttempted;
        if (E.bRwgProvisional) { RwgStuckBeforeAttempt(E); RwgSettleProvisional(E, bChangedWorld); AFGResourceNode* const* Now = SpawnedNodes.Find(E.EntryGuid); if (Now && IsValid(*Now) && !E.bRwgProvisional) { ++RwgPcSpawned; } RwgStuckAfterAttempt(E); continue; } // rwg-farspawn-1: settle on approach, tracked by RWGSTUCK (fix F3)
        RwgStuckBeforeAttempt(E); // rwg-density-1: RWGSTUCK snapshot (diagnostics only, NodeShuffleMapModeDensity.cpp)
        EnsureNewNodeSpawned(E, bChangedWorld);
        bRwgSteepArmed = false; // rwg-steep-1: an arm never outlives the attempt that set it (a redeal that returned at !Q.IsUsable() did not consume it)
        AFGResourceNode* const* Now = SpawnedNodes.Find(E.EntryGuid);
        if (Now && IsValid(*Now)) { ++RwgPcSpawned; } else { FreshNoActor.Add(Cands[K].Value); } // rwg-farspawn-1e C1
        RwgStuckAfterAttempt(E); // rwg-density-1: per-entry failed-attempt count (reads only)
    }
    RwgPcBudgetSkipped += Cands.Num() - K;
    ++RwgPcPasses;
    RwgProvisionalSpawnPass(Pawns, AttemptRadiusCm, FreshNoActor, bChangedWorld); // rwg-farspawn-1 (+1e C1 fresh no-actor): unspawned entries beyond the attempt radius (NodeShuffleMapModeProvisional.cpp)
    RwgResettlePass(); // RWG P2c: entries placed at the API Z by builds <= rwg-p4-2 (NodeShuffleMapModeResettle.cpp)
    RwgRockOcclusionPass(); // RWG P2d: placed entries under a SOLID mesh are redealt (NodeShuffleMapModeRock.cpp)
    RwgKillboxResettlePass(); // RWG P2f: placed entries whose actor stands below the killbox top are torn down + redealt (NodeShuffleMapModeVoid.cpp)
    if ((RwgPcPasses % 12) == 1)
    {
        // RWG P2b review SF4 (spec P2b section 3): hide-stickiness sample. Up to 50 hide records at stride Num/50.
        // RWG P4 (2026-10-18): P2b re-review S1 applied verbatim -- read BEFORE this pass's SuppressOriginalNodes;
        // identityOk mirrors its class/resource/300 cm test (AdoptMatchRadiusCm, Subsystem.cpp:191); hidden counts
        // non-occupied identityOk only. The line reports counts only.
        const int32 Records = OriginalNodeRecord.Num();
        const int32 Stride = FMath::Max(1, Records / 50);
        constexpr float HideCheckMatchCm = 300.0f; // mirrors AdoptMatchRadiusCm (Subsystem.cpp:191)
        int32 Sampled = 0, Resolved = 0, IdentityOk = 0, Occupied = 0, Hidden = 0;
        for (int32 Ri = 0; Ri < Records && Sampled < 50; Ri += Stride)
        {
            ++Sampled;
            const FNodeShuffleSuppressedOriginal& Rec = OriginalNodeRecord[Ri];
            const AFGResourceNodeBase* Orig = FindOriginalBaseByPath(Rec.VanillaNodePath);
            if (!Orig) { continue; }
            ++Resolved;
            if (!Rec.OriginalNodeClassPath.IsEmpty())
            {
                const UClass* Res = Orig->GetResourceClass();
                const FVector Anchor = Rec.TrueLocation.IsNearlyZero() ? Rec.Location : Rec.TrueLocation;
                if (Orig->GetClass()->GetPathName() != Rec.OriginalNodeClassPath || !Res || Res->GetPathName() != Rec.OriginalResourceClassPath
                    || FVector::Dist(Orig->GetActorLocation(), Anchor) >= HideCheckMatchCm) { continue; }
            }
            ++IdentityOk;
            const AFGResourceNode* AsNode = Cast<AFGResourceNode>(Orig);
            if (Orig->IsOccupied() || (AsNode && IsNodeOccupiedAnyway(AsNode))) { ++Occupied; continue; }
            if (Orig->IsHidden()) { ++Hidden; }
        }
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGHIDECHECK: pass=%d sampled=%d resolvedByPath=%d identityOk=%d occupied=%d hidden=%d records=%d"),
            RwgPcPasses, Sampled, Resolved, IdentityOk, Occupied, Hidden, Records);
        RwgSteepEmitSession(); // rwg-steep-1: RWGSTEEPSUM session totals (first pass, then 12th passes when changed)
    }
    if (RwgPcSpawned > 0 || RwgPcProvSpawned > 0 || RwgPcProvSettled > 0 || (RwgPcPasses % 12) == 1) // rwg-farspawn-1c C3: provisional passes print too. bounded: a pass that spawned, else once a minute. unplaceableNew counts first-trace unplaceable spots (wet or cliff with no spiral land)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("PLACECENSUS: pass=%d attempted=%d spawned=%d notSpawned=%d deferredSettle=%d unplaceableNew=%d apiRedealCalls=%d budgetSkipped=%d farNoAttempt=%d liveReasserted=%d spawnTraceHit=%d spawnTraceMiss=%d spawnTraceFar=%d farTerrain=%d farCanopy=%d farWater=%d farOther=%d farSolid=%d rejectedRock=%d settledOnTerrain=%d noTerrainDefer=%d spawnSettlesWithAnySkip=%d terrainOnlyCapHitsAllCallers=%d fallbackNoApi=%d attemptRadiusM=%.0f spawnRadiusM=%.0f layoutEntries=%d caveSettleTried=%d caveSettled=%d caveDeferred=%d caveEscaped=%d caveFar=%d caveSolid=%d caveWater=%d caveRedealCave=%d caveRedealSurface=%d insideCaveChecked=%d settledInsideCave=%d settledInsideCaveSurfaceEntry=%d steepRing=%d steepMapwide=%d steepOccludedMapwide=%d steepOccludedRing=%d settledOnSolidTop=%d solidTopAcceptedProbes=%d solidTopRejectedGuardOrAbove=%d solidTopRejectedNormal=%d solidTopRejectedFootprint=%d provisionalSpawned=%d provisionalSettled=%d"),
            RwgPcPasses, RwgPcAttempted, RwgPcSpawned, RwgPcAttempted - RwgPcSpawned, DeferredThisPass - DeferredBefore,
            WaterLockedThisSession.Num() - WaterLockedBefore, RwgPcRedealApi, RwgPcBudgetSkipped, RwgPcFarNoAttempt, RwgPcLive,
            RwgPcTraceHit, RwgPcTraceMiss, RwgPcTraceFar, RwgPcFarTerrain, RwgPcFarCanopy, RwgPcFarWater, RwgPcFarOther, RwgPcFarSolid, RwgPcRejectedRock,
            RwgPcSettledTerrain, RwgPcNoTerrainDefer, RwgPcSkippedToTerrain, RwgProbeCapHits,
            RwgPcFallback, AttemptRadiusCm / 100.f, SpawnRadiusCm / 100.f, Layout.Num(), RwgPcCaveSettleTried, RwgPcCaveSettled, RwgPcCaveDeferred, RwgPcCaveEscaped,
            RwgPcCaveFar, RwgPcCaveSolid, RwgPcCaveWater, RwgPcCaveRedealCave, RwgPcCaveRedealSurface, RwgPcInsideCaveChecked, RwgPcSettledInsideCave, RwgPcSettledInsideCaveSurfaceEntry,
            RwgPcSteepRing, RwgPcSteepMapwide, RwgPcSteepOccluded, RwgPcSteepOccludedRing, RwgPcSettledOnSolidTop, RwgPcSolidTopAccepted, RwgPcSolidTopRejStartInside, RwgPcSolidTopRejNormal, RwgPcSolidTopRejFootprint, RwgPcProvSpawned, RwgPcProvSettled);
        RwgEmitDzCensus(); // RWG P2c: session apiToTerrainDz distribution by band, emitted with PLACECENSUS
    }
    NodeShuffleRwgTraceDiag::EmitPassCensus(RwgPcPasses, RwgPcAttempted, RwgPcSettledTerrain, RwgPcNoTerrainDefer, DeferredThisPass - DeferredBefore); // rwg-trace-diag-1: before RwgEmitPlaceCensus resets the counters
    RwgDensityPassHook(Pawns, AttemptRadiusCm); // rwg-density-1: RWGDENSITY on load (first pass, after RwgSeaRedealPass) + RWGSTUCK every 12th pass
    RwgEmitPlaceCensus();
}

void ANodeShuffleSubsystem::RwgEmitPlaceCensus()
{
    // Rock census (once per session, first pass): measures how many cached originals carry an engine-linked mesh
    // actor (mMeshActor). Population: every non-ours node in VanillaNodeCache at that pass. rwg-rockcache-1: RWG rocks
    // are now paired by 2-D proximity in RebuildRwgRockCache (census RWGROCKCACHE), not by this link; OrphanRockCleanup
    // stays vanilla-only. This census still reads the engine link only.
    if (RwgRockChecked == 0)
    {
        // RWG P2b review SF5: the P3 rule needs the recorded originals, so a second count keeps only cache keys whose
        // path is in OriginalNodeRecord.
        TSet<FString> RecordedPaths;
        RecordedPaths.Reserve(OriginalNodeRecord.Num());
        for (const FNodeShuffleSuppressedOriginal& Rec : OriginalNodeRecord) { RecordedPaths.Add(Rec.VanillaNodePath); }
        int32 RecChecked = 0, RecLinked = 0;
        for (const TPair<FString, TWeakObjectPtr<AFGResourceNodeBase>>& KV : VanillaNodeCache)
        {
            const AFGResourceNodeBase* Node = KV.Value.Get();
            if (!IsValid(Node)) { continue; }
            ++RwgRockChecked;
            const bool bLinked = Node->mMeshActor.Get() != nullptr;
            if (bLinked) { ++RwgRockLinked; }
            if (RecordedPaths.Contains(KV.Key)) { ++RecChecked; if (bLinked) { ++RecLinked; } }
        }
        UE_LOG(LogNodeShuffle, Display, TEXT("PLACECENSUS: rockLinkedAll=%d of %d cached non-ours nodes (deposits, fracking and modded included; engine link only, RWG proximity pairing is RWGROCKCACHE)"),
            RwgRockLinked, RwgRockChecked);
        UE_LOG(LogNodeShuffle, Display, TEXT("PLACECENSUS: rockLinkedRecorded=%d of %d cached nodes whose path is in the hide records; records=%d"),
            RecLinked, RecChecked, OriginalNodeRecord.Num());
        if (RwgRockChecked == 0) { RwgRockChecked = -1; } // empty cache: do not rescan every pass
    }
    RwgPcAttempted = RwgPcSpawned = RwgPcBudgetSkipped = RwgPcFarNoAttempt = RwgPcLive = 0;
    RwgPcTraceHit = RwgPcTraceMiss = RwgPcTraceFar = RwgPcFallback = RwgPcRedealApi = 0;
    RwgPcFarTerrain = RwgPcFarCanopy = RwgPcFarWater = RwgPcFarOther = 0;
    RwgPcFarSolid = RwgPcRejectedRock = 0; // RWG P2d
    RwgPcSteepRing = RwgPcSteepMapwide = RwgPcSteepOccluded = RwgPcSteepRingRuns = 0; RwgPcSteepOccludedRing = RwgPcSettledOnSolidTop = 0; RwgPcSolidTopAccepted = RwgPcSolidTopRejStartInside = RwgPcSolidTopRejNormal = RwgPcSolidTopRejFootprint = RwgCliffTopSamples = 0; // rwg-clifftop-1
    RwgPcSettledTerrain = RwgPcNoTerrainDefer = RwgPcSkippedToTerrain = 0;
    RwgPcProvSpawned = RwgPcProvSettled = 0; // rwg-farspawn-1
    RwgProbeCapHits = 0;
    RwgPcCaveSettleTried = RwgPcCaveSettled = RwgPcCaveDeferred = RwgPcCaveEscaped = RwgPcCaveFar = RwgPcCaveSolid = RwgPcCaveWater = 0; // RWG P5
    RwgPcCaveRedealCave = RwgPcCaveRedealSurface = RwgPcInsideCaveChecked = RwgPcSettledInsideCave = RwgPcSettledInsideCaveSurfaceEntry = 0;
}

// rwg-farspawn-1f F10: end of the span moved from NodeShuffleMapModePlace.cpp
