// RWG P2b (2026-10-17) -- the off-vanilla roll for mode=rwg, placement v2 and apply-on-load.
// Brief: _team/nodeshuffle-rwg-p2-spec.md §2 + "R2 measurements" (binding: the height API is validated, so the
// DEFAULT resolves each dealt spot at roll time through the runtime-bound RWG exports; the existing
// trace-and-defer path is the fallback only when FRwgGroundQuery is not usable).
// Every function here is reached only behind a map-mode test at its NodeShuffleSubsystem.cpp call site, so the
// vanilla path never enters this file. Log lines report measured counts and the predicate that branched, never a cause.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffleConfig.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Resources/FGResourceNodeBase.h"
#include "Resources/FGResourceNode.h"
#include "FGWaterVolume.h"

bool ANodeShuffleSubsystem::RwgPipelineMayRun()
{
    // Unknown maps idle (P1 decision 2). The pipeline below RefreshTick's map-mode test runs ONLY on RWG, ONLY
    // once readiness latched (v2 clause A on any world, or clause B on a save that holds a layout: RwgObserveTick).
    if (GetMapMode() != ENodeShuffleMapMode::Rwg || !NodeShuffleMapMode::bRwgRollEnabled || !bRwgReady)
    {
        return false;
    }
    if (bRwgLegacyChecked)
    {
        return true;
    }
    if (!bLayoutGenerated)
    {
        bRwgLegacyChecked = true;
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGLAYOUT: no saved layout; first roll on this tick; clause=%s"), *RwgReadyBy);
        return true;
    }
    if (RwgLayoutVersion >= NodeShuffleMapMode::RwgLayoutVersionP2b)
    {
        bRwgLegacyChecked = true;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RWGLAYOUT: applying saved layout; clause=%s entries=%d hideRecords=%d rwgLayoutVersion=%d; no re-roll on load"),
            *RwgReadyBy, Layout.Num(), OriginalNodeRecord.Num(), RwgLayoutVersion);
        return true;
    }
    // Legacy layout (RwgLayoutVersion 0: rolled by the unguarded cl513885-2 build or any pre-P2b build). It is NOT
    // applied: the established re-roll path replaces it once, after the two preconditions the live re-roll has
    // (originals cached so RestoreOriginalsForReroll can find them; restored spawned nodes adopted so the re-roll
    // destroys them). The re-roll pool is the SAVED layout's originals (RollLayout bIsReroll); late scans append the rest.
    if (!IsWorldReadyForReroll())
    {
        return false; // waits (t < 8 s or < 50 loaded nodes); checked again next tick
    }
    bRwgLegacyChecked = true;
    const int32 Entries = Layout.Num();
    const int32 Records = OriginalNodeRecord.Num();
    int32 Cached = 0;
    for (TActorIterator<AFGResourceNodeBase> It(GetWorld()); It; ++It)
    {
        if (NodeShuffleMapMode::IsOursForRwg(*It)) { continue; }
        TWeakObjectPtr<AFGResourceNodeBase>& Slot = VanillaNodeCache.FindOrAdd(It->GetPathName());
        if (!Slot.IsValid()) { Slot = *It; ++Cached; }
    }
    if (!bAdoptedRestoredNodes)
    {
        AdoptRestoredSpawnedNodes();
        bAdoptedRestoredNodes = true;
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGLAYOUT: legacy layout ignored; rwgLayoutVersion=%d entries=%d hideRecords=%d originalsCached=%d adoptedSpawned=%d clause=%s; re-rolling once from the saved pool"),
        RwgLayoutVersion, Entries, Records, Cached, SpawnedNodes.Num(), *RwgReadyBy);
    RestoreOriginalsForReroll();
    const FNodeShuffleConfigStruct Config = FNodeShuffleConfigStruct::GetActiveConfig(this);
    const int32 Seed = Config.SeedOverride != 0 ? Config.SeedOverride : static_cast<int32>(FPlatformTime::Cycles());
    RollLayout(Seed, true);
    UE_LOG(LogNodeShuffle, Display, TEXT("RWGLAYOUT: legacy re-roll done; seed=%d entries=%d hideRecords=%d rwgLayoutVersion=%d"),
        Seed, Layout.Num(), OriginalNodeRecord.Num(), RwgLayoutVersion);
    return true;
}

void ANodeShuffleSubsystem::RwgBuildWaterVolumeCache()
{
    RwgWaterVolumes.Reset();
    for (TActorIterator<AFGWaterVolume> It(GetWorld()); It; ++It)
    {
        if (IsValid(*It)) { RwgWaterVolumes.Emplace(It->GetComponentsBoundingBox(true), TWeakObjectPtr<AFGWaterVolume>(*It)); }
    }
    if (RwgWaterVolumes.Num() > 0) { UE_LOG(LogNodeShuffle, Display, TEXT("RWGWATERCACHE: built volumes=%d"), RwgWaterVolumes.Num()); }
}

bool ANodeShuffleSubsystem::RwgIsPointInWaterCached(const FVector& P) const
{
    // Same predicate as IsPointInWater (EncompassesPoint on every AFGWaterVolume), bounds-prefiltered: only
    // volumes whose box contains P are asked. Vanilla keeps the uncached IsPointInWater loop.
    for (const TPair<FBox, TWeakObjectPtr<AFGWaterVolume>>& V : RwgWaterVolumes)
    {
        if (!V.Key.IsInsideOrOn(P)) { continue; }
        AFGWaterVolume* W = V.Value.Get();
        if (IsValid(W) && W->EncompassesPoint(P)) { return true; }
    }
    return false;
}

bool ANodeShuffleSubsystem::RwgDealOneSpot(const NodeShuffleMapMode::FRwgGroundQuery& Q, FRandomStream& Rng, FVector& OutSpot,
                                           int32& Outside, int32& Water, int32& WaterApiDisagree)
{
    const double H = Q.HalfExtent();
    const double X = Rng.FRandRange(-H, H);
    const double Y = Rng.FRandRange(-H, H);
    if (Q.Outside(X, Y)) { ++Outside; return false; }
    const double Z = Q.SurfaceZ(X, Y);
    OutSpot = FVector(X, Y, Z);
    const bool bVolume = RwgIsPointInWaterCached(OutSpot);
    const bool bApiWet = Q.WaterLevelZ(X, Y) > Z + NodeShuffleMapMode::RwgWaterApiMarginCm;
    if (bVolume != bApiWet) { ++WaterApiDisagree; }
    if (bVolume) { ++Water; return false; } // the volume test decides until R3 shows the two agree
    return true;
}

bool ANodeShuffleSubsystem::GenerateRwgLocations(FRandomStream& Rng, const TArray<FVector>& AvoidLocations, int32 Count,
                                                 float SpacingCm, TArray<FVector>& OutLocations)
{
    const double T0 = FPlatformTime::Seconds();
    const NodeShuffleMapMode::FRwgGroundQuery Q(GetWorld(), RwgGenerator.Get());
    if (!Q.IsUsable())
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("ROLLCENSUS: map=rwg deal=fallback apiStatus='%s' requested=%d; the percentile-box deal and trace-and-defer spawn run instead"),
            *Q.Status(), Count);
        return false;
    }
    RwgBuildWaterVolumeCache();
    const double H = Q.HalfExtent();
    int32 Outside = 0, Water = 0, Disagree = 0, Spacing = 0, Drawn = 0;
    double ZSum = 0.0, ZMin = TNumericLimits<double>::Max(), ZMax = TNumericLimits<double>::Lowest();
    const float SpacingSq = FMath::Square(SpacingCm);
    int32 Attempts = Count * 60; // the vanilla deal's attempt budget; API rejects consume it too (counted)
    while (OutLocations.Num() < Count && Attempts-- > 0)
    {
        ++Drawn;
        FVector C;
        if (!RwgDealOneSpot(Q, Rng, C, Outside, Water, Disagree)) { continue; }
        // Spacing: identical to the vanilla deal's surface rule (2D, MinNodeSpacing, Avoid then Out).
        bool bTooClose = false;
        for (const FVector& E : AvoidLocations) { if (FVector::DistSquared2D(E, C) < SpacingSq) { bTooClose = true; break; } }
        if (!bTooClose) { for (const FVector& E : OutLocations) { if (FVector::DistSquared2D(E, C) < SpacingSq) { bTooClose = true; break; } } }
        if (bTooClose) { ++Spacing; continue; }
        OutLocations.Add(C);
        ZSum += C.Z;
        ZMin = FMath::Min(ZMin, C.Z);
        ZMax = FMath::Max(ZMax, C.Z);
    }
    // The redeal (TryRedealRwgEntry) and the fallback redeal read these, as on vanilla.
    DealBoundsMin = FVector(-H, -H, 0.0);
    DealBoundsMax = FVector(H, H, 0.0);
    DealMeanZ = OutLocations.Num() > 0 ? static_cast<float>(ZSum / OutLocations.Num()) : RwgMeanZ;
    if (OutLocations.Num() == 0) { ZMin = ZMax = 0.0; }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("ROLLCENSUS: map=rwg deal=api extentHalfCm=%.0f requested=%d dealt=%d drawn=%d rejectedOutside=%d rejectedWater=%d waterApiDisagree=%d rejectedSpacing=%d resolvedZ=%d resolvedZMin=%.0f resolvedZMax=%.0f waterVolumes=%d caveSpots=0 rollDurationMs=%.0f"),
        H, Count, OutLocations.Num(), Drawn, Outside, Water, Disagree, Spacing, OutLocations.Num(), ZMin, ZMax,
        RwgWaterVolumes.Num(), (FPlatformTime::Seconds() - T0) * 1000.0);
    return true;
}

bool ANodeShuffleSubsystem::TryRedealRwgEntry(FNodeShuffleEntry& Entry, float SpacingCm, int32 Tries, bool& bOutMoved)
{
    bOutMoved = false;
    const NodeShuffleMapMode::FRwgGroundQuery Q(GetWorld(), RwgGenerator.Get());
    if (!Q.IsUsable()) { return false; }
    if (RwgWaterVolumes.Num() == 0) { RwgBuildWaterVolumeCache(); }
    // Same salted per-entry stream as the vanilla redeal; no cave branch off vanilla.
    int32& Salt = RedealAttempts.FindOrAdd(Entry.EntryGuid);
    Salt++;
    FRandomStream Rng(static_cast<int32>(GetTypeHash(Entry.EntryGuid)) ^ (SavedSeed * 31) ^ (Salt * 7919));
    Entry.bUnderground = false;
    int32 Outside = 0, Water = 0, Disagree = 0, Spacing = 0;
    const float SpacingSq = FMath::Square(SpacingCm);
    for (int32 I = 0; I < Tries && !bOutMoved; ++I)
    {
        FVector C;
        if (!RwgDealOneSpot(Q, Rng, C, Outside, Water, Disagree)) { continue; }
        bool bTooClose = false;
        for (const FNodeShuffleEntry& Other : Layout)
        {
            if (&Other != &Entry && FVector::DistSquared2D(Other.Location, C) < SpacingSq) { bTooClose = true; break; }
        }
        if (!bTooClose) { for (const FNodeShuffleSuppressedOriginal& Rec : OriginalNodeRecord) { const FVector A = Rec.TrueLocation.IsNearlyZero() ? Rec.Location : Rec.TrueLocation; if (FVector::DistSquared2D(A, C) < SpacingSq) { bTooClose = true; break; } } }
        if (bTooClose) { ++Spacing; continue; }
        Entry.Location = C;
        Entry.Rotation = FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f);
        Entry.bRayCasted = false;
        bOutMoved = true;
    }
    ++RwgPcRedealApi;
    UE_LOG(LogNodeShuffle, Verbose, TEXT("RWG-REDEAL: entry %s moved=%d salt=%d rejectedOutside=%d rejectedWater=%d waterApiDisagree=%d rejectedSpacing=%d"),
        *Entry.EntryGuid.ToString(), bOutMoved ? 1 : 0, Salt, Outside, Water, Disagree, Spacing);
    return true;
}

bool ANodeShuffleSubsystem::RwgResolveAtSpawn(FNodeShuffleEntry& Entry, bool& bOutApiUsed, bool& bOutWaterNoLand)
{
    bOutApiUsed = false;
    bOutWaterNoLand = false;
    const NodeShuffleMapMode::FRwgGroundQuery Q(GetWorld(), RwgGenerator.Get());
    if (!Q.IsUsable()) { ++RwgPcFallback; return false; }
    bOutApiUsed = true;
    if (RwgWaterVolumes.Num() == 0) { RwgBuildWaterVolumeCache(); }
    // The deal predicates again at this XY (starter, fallback-dealt and spiral-moved spots never passed RwgDealOneSpot).
    const double X0 = Entry.Location.X, Y0 = Entry.Location.Y;
    if (Q.Outside(X0, Y0)) { bOutWaterNoLand = true; return false; }
    const double ApiZ0 = Q.SurfaceZ(X0, Y0);
    if (RwgIsPointInWaterCached(FVector(X0, Y0, ApiZ0))) { bOutWaterNoLand = true; return false; }
    Entry.Location.Z = ApiZ0;
    // Agreement BEFORE classification: a far hit (canopy, water sheet, overhang) or a miss spawns at the API Z, yaw-only.
    FVector HitLoc = Entry.Location;
    FRotator HitRot = Entry.Rotation;
    bool bHitWater = false, bHitSteep = false;
    const bool bHit = RaycastGroundAt(Entry.Location, Entry.Location.Z, nullptr, nullptr, HitLoc, HitRot, bHitWater, false, &bHitSteep);
    if (!bHit || FMath::Abs(HitLoc.Z - ApiZ0) > NodeShuffleMapMode::RwgTraceAgreeCm)
    {
        Entry.Rotation = FRotator(0.f, Entry.Rotation.Yaw, 0.f);
        if (bHit) { ++RwgPcTraceFar; return true; }
        ++RwgPcTraceMiss;
        return true;
    }
    if (!bHitWater && !bHitSteep) { Entry.Location = HitLoc; Entry.Rotation = HitRot; ++RwgPcTraceHit; return true; }
    // Terrain agreeing with the API but wet or steep: the established spiral looks for flat dry land nearby.
    if (!RaycastSettle(Entry, nullptr, nullptr, &bOutWaterNoLand)) { return false; } // no spot: redeal; all probes missed: defer
    const double X1 = Entry.Location.X, Y1 = Entry.Location.Y;
    if (Q.Outside(X1, Y1)) { bOutWaterNoLand = true; return false; }
    const double ApiZ1 = Q.SurfaceZ(X1, Y1);
    if (FMath::Abs(Entry.Location.Z - ApiZ1) <= NodeShuffleMapMode::RwgTraceAgreeCm) { ++RwgPcTraceHit; return true; }
    if (RwgIsPointInWaterCached(FVector(X1, Y1, ApiZ1))) { bOutWaterNoLand = true; return false; }
    Entry.Location.Z = ApiZ1;
    Entry.Rotation = FRotator(0.f, Entry.Rotation.Yaw, 0.f);
    ++RwgPcTraceFar;
    return true;
}

void ANodeShuffleSubsystem::RwgSpawnPass(float SpawnRadiusCm, bool& bChangedWorld)
{
    if (RwgWaterVolumes.Num() == 0) { RwgBuildWaterVolumeCache(); }
    TArray<FVector> Pawns;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr;
        if (P) { Pawns.Add(P->GetActorLocation()); }
    }
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
            // Live actors keep the vanilla per-pass re-assert chain, uncapped, at the vanilla radius.
            if (IsLocationNearAnyPlayer(E.Location, SpawnRadiusCm)) { ++RwgPcLive; EnsureNewNodeSpawned(E, bChangedWorld); }
            continue;
        }
        if (!IsLocationNearAnyPlayer(E.Location, SpawnRadiusCm)) { continue; }
        if (!IsLocationNearAnyPlayer(E.Location, AttemptRadiusCm)) { ++RwgPcFarNoAttempt; continue; }
        double Best = TNumericLimits<double>::Max();
        for (const FVector& P : Pawns) { Best = FMath::Min(Best, FVector::DistSquared2D(P, E.Location)); }
        Cands.Emplace(Best, I);
    }
    Cands.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
    const int32 N = FMath::Min(Cands.Num(), NodeShuffleMapMode::RwgMaxSpawnAttemptsPerPass);
    for (int32 K = 0; K < N; ++K)
    {
        FNodeShuffleEntry& E = Layout[Cands[K].Value];
        ++RwgPcAttempted;
        EnsureNewNodeSpawned(E, bChangedWorld);
        AFGResourceNode* const* Now = SpawnedNodes.Find(E.EntryGuid);
        if (Now && IsValid(*Now)) { ++RwgPcSpawned; }
    }
    RwgPcBudgetSkipped += Cands.Num() - N;
    ++RwgPcPasses;
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
    }
    if (RwgPcSpawned > 0 || (RwgPcPasses % 12) == 1) // bounded: a pass that spawned, else once a minute
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("PLACECENSUS: pass=%d attempted=%d spawned=%d notSpawned=%d deferredSettle=%d waterLockedNew=%d apiRedealCalls=%d budgetSkipped=%d farNoAttempt=%d liveReasserted=%d spawnTraceHit=%d spawnTraceMiss=%d spawnTraceFar=%d fallbackNoApi=%d attemptRadiusM=%.0f spawnRadiusM=%.0f layoutEntries=%d"),
            RwgPcPasses, RwgPcAttempted, RwgPcSpawned, RwgPcAttempted - RwgPcSpawned, DeferredThisPass - DeferredBefore,
            WaterLockedThisSession.Num() - WaterLockedBefore, RwgPcRedealApi, RwgPcBudgetSkipped, RwgPcFarNoAttempt, RwgPcLive,
            RwgPcTraceHit, RwgPcTraceMiss, RwgPcTraceFar, RwgPcFallback, AttemptRadiusCm / 100.f, SpawnRadiusCm / 100.f, Layout.Num());
    }
    RwgEmitPlaceCensus();
}

void ANodeShuffleSubsystem::RwgEmitPlaceCensus()
{
    // Rock census (once per session, first pass): rock hiding is NOT attempted on RWG in P2b (RebuildMeshActorCache
    // and OrphanRockCleanup are skipped off vanilla); this measures how many cached originals carry a linked mesh
    // actor so P3 can decide. Population: every non-ours node in VanillaNodeCache at that pass.
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
        UE_LOG(LogNodeShuffle, Display, TEXT("PLACECENSUS: rockLinkedAll=%d of %d cached non-ours nodes (deposits, fracking and modded included; rock hiding skipped on RWG this build)"),
            RwgRockLinked, RwgRockChecked);
        UE_LOG(LogNodeShuffle, Display, TEXT("PLACECENSUS: rockLinkedRecorded=%d of %d cached nodes whose path is in the hide records; records=%d"),
            RecLinked, RecChecked, OriginalNodeRecord.Num());
        if (RwgRockChecked == 0) { RwgRockChecked = -1; } // empty cache: do not rescan every pass
    }
    RwgPcAttempted = RwgPcSpawned = RwgPcBudgetSkipped = RwgPcFarNoAttempt = RwgPcLive = 0;
    RwgPcTraceHit = RwgPcTraceMiss = RwgPcTraceFar = RwgPcFallback = RwgPcRedealApi = 0;
}

void ANodeShuffleSubsystem::RwgLogSkipOnce(int32 Bit, const TCHAR* What)
{
    if ((RwgSkipLogged & (1 << Bit)) != 0) { return; }
    RwgSkipLogged |= (1 << Bit);
    UE_LOG(LogNodeShuffle, Display, TEXT("RWGSKIP: %s skipped off vanilla; map=%s"), What,
        GetMapMode() == ENodeShuffleMapMode::Rwg ? TEXT("rwg") : TEXT("other"));
}
