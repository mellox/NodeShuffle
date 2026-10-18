// RWG P2b (2026-10-17) -- the off-vanilla roll for mode=rwg, placement v2 and apply-on-load.
// Brief: _team/nodeshuffle-rwg-p2-spec.md §2 + "R2 measurements" (binding: the height API is validated, so the DEFAULT resolves each dealt spot
// at roll time through the runtime-bound RWG exports; the existing trace-and-defer path is the fallback only when FRwgGroundQuery is not usable).
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

namespace
{
    // RWG P2c (2026-10-18) roll-time slope reject. Mirror of NodeShuffleSubsystem.cpp's file-local settle gate
    // (anonymous namespace there: CliffSlopeDeg = 60, MinSettleNormalZ = cos(CliffSlopeDeg)): a ground normal whose Z is
    // below cos(60 deg) has rise/run above tan(60 deg) = 1.73, so the deal rejects on the same angle the settle calls a cliff.
    constexpr double RwgCliffSlopeDegMirror = 60.0; // = CliffSlopeDeg, NodeShuffleSubsystem.cpp
    constexpr double RwgSlopeSampleCm = 200.0;      // API gradient: samples at +-200 cm in X and in Y
}

bool ANodeShuffleSubsystem::RwgApiSlopeTooSteep(const NodeShuffleMapMode::FRwgGroundQuery& Q, double X, double Y, double& OutRiseRun) const
{
    OutRiseRun = 0.0;
    const double S = RwgSlopeSampleCm;
    // A sample outside the world is not measured and does not reject (the world edge is the Outside test's job).
    if (Q.Outside(X + S, Y) || Q.Outside(X - S, Y) || Q.Outside(X, Y + S) || Q.Outside(X, Y - S)) { return false; }
    const double Gx = (Q.SurfaceZ(X + S, Y) - Q.SurfaceZ(X - S, Y)) / (2.0 * S);
    const double Gy = (Q.SurfaceZ(X, Y + S) - Q.SurfaceZ(X, Y - S)) / (2.0 * S);
    OutRiseRun = FMath::Sqrt(Gx * Gx + Gy * Gy);
    return OutRiseRun > FMath::Tan(FMath::DegreesToRadians(RwgCliffSlopeDegMirror));
}

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
        int32 SeaN = 0, WetN = 0, WellSeaN = 0, WellWetN = 0;
        const bool bSeaCounted = RwgCountSeaEntries(SeaN, WetN, WellSeaN, WellWetN); // RWG P2e: measured; the re-deal is RwgSeaRedealPass
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RWGLAYOUT: applying saved layout; clause=%s entries=%d hideRecords=%d rwgLayoutVersion=%d seaEntries=%d wetEntries=%d seaWellCores=%d wetWellCores=%d seaCounted=%d seaUsable=%d; no re-roll on load"),
            *RwgReadyBy, Layout.Num(), OriginalNodeRecord.Num(), RwgLayoutVersion, SeaN, WetN, WellSeaN, WellWetN, bSeaCounted ? 1 : 0, bRwgSeaUsable ? 1 : 0);
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

void ANodeShuffleSubsystem::RwgNoteWaterSample(const NodeShuffleMapMode::FRwgGroundQuery& Q, const FVector& P, bool bVolumeWet, const TCHAR* Caller)
{
    // Coordinator addendum (2026-10-18, measured only, gates nothing). Two bounded samples, 8 lines each per session:
    // RWGWATERSAMPLE = a draw the volume test rejected while its API surface Z was >= 0; RWGSEASAMPLE = a spot that PASSED the
    // deal predicates with API surface Z < 0. Each prints the raw WaterLevelCmAt reading and whether any cached volume BOX
    // contains the point (the cheap prefilter the volume test runs before EncompassesPoint).
    int32& N = bVolumeWet ? RwgWaterSamples : RwgSeaSamples;
    if (N >= 8) { return; }
    ++N;
    bool bAnyBox = false;
    for (const TPair<FBox, TWeakObjectPtr<AFGWaterVolume>>& V : RwgWaterVolumes) { if (V.Key.IsInsideOrOn(P)) { bAnyBox = true; break; } }
    const double Wl = Q.WaterLevelZ(P.X, P.Y);
    if (bVolumeWet)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGWATERSAMPLE: caller=%s sample=%d x=%.0f y=%.0f surfaceZ=%.0f waterApiLevelSample=%.0f volumeTest=1 volumeBoxContains=%d"),
            Caller, N, P.X, P.Y, P.Z, Wl, bAnyBox ? 1 : 0);
    }
    else
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGDEALTBELOWZ0SAMPLE: caller=%s sample=%d x=%.0f y=%.0f surfaceZ=%.0f waterApiLevelSample=%.0f volumeTest=%d volumeBoxContains=%d"),
            Caller, N, P.X, P.Y, P.Z, Wl, RwgIsPointInWaterCached(P) ? 1 : 0, bAnyBox ? 1 : 0);
    }
}

bool ANodeShuffleSubsystem::RwgDealOneSpot(const NodeShuffleMapMode::FRwgGroundQuery& Q, FRandomStream& Rng, FVector& OutSpot,
                                           int32& Outside, int32& Water, int32& WaterApiDisagree, int32& Slope)
{
    const double H = Q.HalfExtent();
    const double X = Rng.FRandRange(-H, H);
    const double Y = Rng.FRandRange(-H, H);
    if (Q.Outside(X, Y)) { ++Outside; return false; }
    const double Z = Q.SurfaceZ(X, Y);
    OutSpot = FVector(X, Y, Z);
    // RWG P2f (2026-10-18): predicate v3 (NodeShuffleMapModeVoid.cpp) = water predicate v2 (P2e: sea OR the volume test OR the inland level
    // above the surface) OR the graded VoidAt term OR bBelowKillbox. waterApiDisagree keeps its P2b meaning (volume vs inland level, measured).
    // Counted once each, in this order: water (bits 1|2|4) -> rejectedWater; else void (8) -> rejectedVoid; else below killbox (16) -> rejectedKillbox.
    const int32 Wet = RwgRejectV3(Q, OutSpot);
    const bool bVolume = (Wet & 2) != 0;
    const bool bApiWet = (Wet & 4) != 0;
    if (bVolume != bApiWet) { ++WaterApiDisagree; }
    if (Wet != 0)
    {
        if ((Wet & 7) != 0)
        {
            // Coordinator addendum (measured only, gates nothing): rejectedWater split by the sign of the API surface Z.
            if (Z < 0.0) { ++RwgRcWaterZBelow0; } else { ++RwgRcWaterZAtOrAbove0; if (bVolume) { RwgNoteWaterSample(Q, OutSpot, true, TEXT("nodeDeal")); } }
            if ((Wet & 1) != 0) { ++RwgRcRejectedSea; } else if ((Wet & 7) == 4) { ++RwgRcApiLevelOnly; }
            ++Water;
        }
        else if ((Wet & 8) != 0) { ++RwgRcRejectedVoid; }
        else { ++RwgRcRejectedKillbox; }
        return false;
    }
    double RiseRun = 0.0;
    if (RwgApiSlopeTooSteep(Q, X, Y, RiseRun)) { ++Slope; return false; } // RWG P2c: API gradient above the cliff angle
    return true;
}

bool ANodeShuffleSubsystem::GenerateRwgLocations(FRandomStream& Rng, const TArray<FVector>& AvoidLocations, int32 Count,
                                                 float SpacingCm, TArray<FVector>& OutLocations, int32 PoolSize, TSet<int32>* OutCaveIndices)
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
    RwgRcWaterZBelow0 = RwgRcWaterZAtOrAbove0 = RwgRcDealtBelowZ0 = 0; // coordinator addendum: this roll only
    RwgRcRejectedSea = RwgRcApiLevelOnly = 0; // RWG P2e: this roll only
    RwgRcRejectedVoid = RwgRcRejectedKillbox = RwgRcDealtBelowKillbox = 0; // RWG P2f: this roll only
    RwgRcCaveSpots = RwgRcCaveDrawsTried = RwgRcCaveDealFailed = RwgRcCaveAnyNearCalls = RwgRcCaveNearHits = RwgRcCaveFloorHits = RwgRcCaveCandidatesTried = 0; // RWG P5: this roll only
    RwgRcCaveRejInside = RwgRcCaveRejHeadroom = RwgRcCaveRejCeiling = RwgRcCaveRejSurface = RwgRcCaveRejKillbox = RwgRcCaveRejSpacing = 0; bRwgRcCaveBudgetSpent = false;
    // RWG P5-2: the cave share by CAPACITY (RwgCaveShare: min(0.25, capacity / PoolSize)); only when the caller takes cave indices.
    const float CaveChance = (OutCaveIndices && RwgEnsureCaveCheck(Q)) ? RwgCaveShare(PoolSize) : 0.0f;
    const int32 CaveRollBudget = FMath::Max(NodeShuffleMapMode::RwgCaveAnyNearPerRoll, FMath::CeilToInt(Count * CaveChance) * NodeShuffleMapMode::RwgCaveAnyNearPerSpot); // TODO(2026-10-18 P5 review F3): re-check vs rollDurationMs
    int32 Outside = 0, Water = 0, Disagree = 0, Spacing = 0, Drawn = 0, Slope = 0;
    double ZSum = 0.0, ZMin = TNumericLimits<double>::Max(), ZMax = TNumericLimits<double>::Lowest();
    const float SpacingSq = FMath::Square(SpacingCm);
    int32 Attempts = Count * 60; // the vanilla deal's attempt budget; API rejects consume it too (counted)
    int32 CaveSlot = -1; bool bSlotIsCave = false; // RWG P5 review F2: share rolled once per OUTPUT slot
    while (OutLocations.Num() < Count && Attempts-- > 0)
    {
        ++Drawn;
        FVector C;
        bool bCavePick = false; // RWG P5: a failed cave draw falls through to a surface draw in the SAME iteration (vanilla: TryPickRawCaveCell's else)
        if (CaveSlot != OutLocations.Num()) { CaveSlot = OutLocations.Num(); bSlotIsCave = CaveChance > 0.0f && Rng.FRand() < CaveChance; }
        if (bSlotIsCave && !bRwgRcCaveBudgetSpent)
        {
            ++RwgRcCaveDrawsTried;
            int32 Calls = 0;
            bCavePick = RwgDrawCaveSpot(Q, Rng, FMath::Min(NodeShuffleMapMode::RwgCaveAnyNearPerSpot, CaveRollBudget - RwgRcCaveAnyNearCalls), C, Calls);
            RwgRcCaveAnyNearCalls += Calls;
            if (!bCavePick) { ++RwgRcCaveDealFailed; bSlotIsCave = false; bRwgRcCaveBudgetSpent = RwgRcCaveAnyNearCalls >= CaveRollBudget; }
        }
        if (!bCavePick && !RwgDealOneSpot(Q, Rng, C, Outside, Water, Disagree, Slope)) { continue; }
        // Spacing: identical to the vanilla deal's rule (MinNodeSpacing, Avoid then Out; 2D, or 3D for a cave pick as in cave-nodes-2).
        bool bTooClose = false;
        const auto Near = [&](const FVector& E) { return bCavePick ? FVector::DistSquared(E, C) < SpacingSq : FVector::DistSquared2D(E, C) < SpacingSq; };
        for (const FVector& E : AvoidLocations) { if (Near(E)) { bTooClose = true; break; } }
        if (!bTooClose) { for (const FVector& E : OutLocations) { if (Near(E)) { bTooClose = true; break; } } }
        if (bTooClose) { ++Spacing; if (bCavePick) { ++RwgRcCaveRejSpacing; bSlotIsCave = false; } continue; }
        if (bCavePick) { OutCaveIndices->Add(OutLocations.Num()); ++RwgRcCaveSpots; }
        OutLocations.Add(C);
        if (C.Z < 0.0) { ++RwgRcDealtBelowZ0; RwgNoteWaterSample(Q, C, false, TEXT("nodeDeal")); } // coordinator addendum, measured only
        if (bRwgKillboxKnown && C.Z < RwgKillboxTopZ) { ++RwgRcDealtBelowKillbox; } // RWG P2f: measured control (v3 rejects these; expect 0)
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
        TEXT("ROLLCENSUS: map=rwg deal=api extentHalfCm=%.0f requested=%d dealt=%d drawn=%d rejectedOutside=%d rejectedWater=%d rejectedWaterSurfaceZBelow0=%d rejectedWaterSurfaceZAtOrAbove0=%d dealtBelowZ0=%d rejectedSea=%d rejectedApiLevelOnly=%d waterApiDisagree=%d rejectedSlope=%d rejectedSpacing=%d resolvedZ=%d resolvedZMin=%.0f resolvedZMax=%.0f waterVolumes=%d caveSpots=%d caveDrawsTried=%d caveDealFailed=%d caveAnyNearCalls=%d caveNearHits=%d caveFloorHits=%d caveRejInside=%d caveRejHeadroom=%d caveRejCeiling=%d caveRejSurface=%d caveRejKillbox=%d caveRejSpacing=%d caveBudgetSpent=%d caveShare=%.4f caveCapacity=%d poolSize=%d originalsInsideCave=%d caveCandidateSource=%s caveCandidatesTried=%d caveRollBudget=%d caveNum=%d caveUsable=%d rollDurationMs=%.0f rejectedVoid=%d rejectedKillbox=%d dealtBelowKillbox=%d killboxTopZ=%.0f killboxTopActorZ=%.0f killboxTopApiZ=%.0f voidUsable=%d"),
        H, Count, OutLocations.Num(), Drawn, Outside, Water, RwgRcWaterZBelow0, RwgRcWaterZAtOrAbove0, RwgRcDealtBelowZ0, RwgRcRejectedSea, RwgRcApiLevelOnly, Disagree, Slope, Spacing, OutLocations.Num(), ZMin, ZMax,
        RwgWaterVolumes.Num(), RwgRcCaveSpots, RwgRcCaveDrawsTried, RwgRcCaveDealFailed, RwgRcCaveAnyNearCalls, RwgRcCaveNearHits, RwgRcCaveFloorHits, RwgRcCaveRejInside,
        RwgRcCaveRejHeadroom, RwgRcCaveRejCeiling, RwgRcCaveRejSurface, RwgRcCaveRejKillbox, RwgRcCaveRejSpacing, bRwgRcCaveBudgetSpent ? 1 : 0, CaveChance, RwgCaveCapacity, PoolSize, RwgCaveSeedCount,
        bRwgCaveJsonCandidates ? TEXT("json") : TEXT("random"), RwgRcCaveCandidatesTried, CaveRollBudget, RwgCaveNumAtCheck, bRwgCaveUsable ? 1 : 0, (FPlatformTime::Seconds() - T0) * 1000.0, RwgRcRejectedVoid, RwgRcRejectedKillbox, RwgRcDealtBelowKillbox,
        bRwgKillboxKnown ? RwgKillboxTopZ : NodeShuffleMapMode::RwgNaN(), bRwgKillboxActorKnown ? RwgKillboxTopActorZ : NodeShuffleMapMode::RwgNaN(),
        bRwgKillboxApiKnown ? RwgKillboxTopApiZ : NodeShuffleMapMode::RwgNaN(), bRwgVoidUsable ? 1 : 0);
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
    if (RwgTryCaveRedeal(Q, Entry, Rng, SpacingCm)) { bOutMoved = true; ++RwgPcRedealApi; return true; } // RWG P5: vanilla redeal cave rule; cave entries try the cave pool first
    int32 Outside = 0, Water = 0, Disagree = 0, Spacing = 0, Slope = 0;
    const float SpacingSq = FMath::Square(SpacingCm);
    for (int32 I = 0; I < Tries && !bOutMoved; ++I)
    {
        FVector C;
        if (!RwgDealOneSpot(Q, Rng, C, Outside, Water, Disagree, Slope)) { continue; }
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
    UE_LOG(LogNodeShuffle, Verbose, TEXT("RWG-REDEAL: entry %s moved=%d salt=%d rejectedOutside=%d rejectedWater=%d waterApiDisagree=%d rejectedSlope=%d rejectedSpacing=%d"),
        *Entry.EntryGuid.ToString(), bOutMoved ? 1 : 0, Salt, Outside, Water, Disagree, Slope, Spacing);
    return true;
}

bool ANodeShuffleSubsystem::RwgResolveAtSpawn(FNodeShuffleEntry& Entry, bool& bOutApiUsed, bool& bOutWaterNoLand)
{
    bOutApiUsed = false;
    bOutWaterNoLand = false;
    const NodeShuffleMapMode::FRwgGroundQuery Q(GetWorld(), RwgGenerator.Get());
    if (!Q.IsUsable()) { if (Entry.bRwgCave) { bOutApiUsed = true; ++RwgPcCaveDeferred; return false; } ++RwgPcFallback; return false; } // P5-2 re-review: a cave defer is not a fallback
    bOutApiUsed = true;
    if (RwgWaterVolumes.Num() == 0) { RwgBuildWaterVolumeCache(); }
    if (Entry.bRwgCave) { return RwgResolveCaveAtSpawn(Q, Entry, bOutWaterNoLand); } // RWG P5: floor-up geometry, BEFORE the surface predicates below
    // The deal predicates again at this XY (starter, fallback-dealt and spiral-moved spots never passed RwgDealOneSpot).
    const double X0 = Entry.Location.X, Y0 = Entry.Location.Y;
    if (Q.Outside(X0, Y0)) { bOutWaterNoLand = true; return false; }
    const double ApiZ0 = Q.SurfaceZ(X0, Y0);
    if (RwgRejectV3(Q, FVector(X0, Y0, ApiZ0)) != 0) { bOutWaterNoLand = true; return false; } // RWG P2f: predicate v3 (v2 | void | below killbox)
    // RWG P2c (2026-10-18), spawn resolver v3 (spec "R4 finding"): the API Z is the trace WINDOW origin only
    // (RaycastGroundAt traces from +200 m to -400 m around it), never the final Z. The settle is the vanilla RaycastSettle
    // (primary probe, cliff/water spiral) against terrain-MESH hits only; every non-terrain blocking hit is skipped by
    // component. Contract unchanged for the caller: true = settled on terrain; false + bOutWaterNoLand = unplaceable
    // (wet/cliff with no spiral land, or off the world/wet by the API at the spiral's XY) -> redeal through the API
    // dealer under the existing cap; false without it = no terrain-mesh hit in the window -> the caller defers.
    Entry.Location.Z = ApiZ0;
    bRwgProbeArmed = true;
    RwgProbeFirstClass = -1;
    RwgProbeFirstZ = 0.0;
    RwgProbeSkipped = 0;
    RwgProbeSolidHits = 0; // RWG P2d: solid-mesh stops during this one settle (primary or spiral probe)
    const bool bSettled = RaycastSettle(Entry, nullptr, nullptr, &bOutWaterNoLand, /*bTerrainOnly=*/true);
    bRwgProbeArmed = false;
    if (RwgProbeSolidHits > 0) { ++RwgPcRejectedRock; } // a probe of this settle met a SOLID mesh: that spot was BLOCKED (spiral / redeal follow)
    // Census of the primary probe's FIRST blocking hit, any component (the P2b hit/far/miss meaning), far split by class.
    if (RwgProbeFirstClass < 0) { ++RwgPcTraceMiss; }
    else if (FMath::Abs(RwgProbeFirstZ - ApiZ0) <= NodeShuffleMapMode::RwgTraceAgreeCm) { ++RwgPcTraceHit; }
    else
    {
        ++RwgPcTraceFar;
        if (RwgProbeFirstClass == 0) { ++RwgPcFarTerrain; } else if (RwgProbeFirstClass == 1) { ++RwgPcFarCanopy; }
        else if (RwgProbeFirstClass == 2) { ++RwgPcFarWater; } else if (RwgProbeFirstClass == 4) { ++RwgPcFarSolid; } else { ++RwgPcFarOther; }
    }
    if (!bSettled)
    {
        if (!bOutWaterNoLand) { ++RwgPcNoTerrainDefer; }
        return false;
    }
    if (RwgProbeSkipped > 0) { ++RwgPcSkippedToTerrain; }
    // The spiral may have moved XY: the deal predicates again there (as P2b did after its spiral).
    const double X1 = Entry.Location.X, Y1 = Entry.Location.Y;
    if (Q.Outside(X1, Y1)) { bOutWaterNoLand = true; return false; }
    const double ApiZ1 = Q.SurfaceZ(X1, Y1);
    if (RwgRejectV3(Q, FVector(X1, Y1, ApiZ1)) != 0) { bOutWaterNoLand = true; return false; } // RWG P2f: predicate v3 (v2 | void | below killbox)
    if (bRwgKillboxKnown && Entry.Location.Z < RwgKillboxTopZ)
    {
        // RWG P2f: the SETTLED Z lies below the killbox top (inside RWG's damage volume): unplaceable -> the caller's redeal. Measured: v3 at the
        // API surface does not catch a void spot when VoidAt is unbound/ungraded (the API reports the plateau, far above the killbox).
        ++RwgKbSettleRefused;
        if (RwgKbSettleSamples < 8)
        {
            ++RwgKbSettleSamples;
            UE_LOG(LogNodeShuffle, Display, TEXT("RWGKILLBOXSETTLE: sample=%d entry=%s settledAt=%s apiSurfaceZ=%.0f killboxTopZ=%.0f; refused, redealt by the caller"),
                RwgKbSettleSamples, *Entry.EntryGuid.ToString(), *Entry.Location.ToCompactString(), ApiZ1, RwgKillboxTopZ);
        }
        bOutWaterNoLand = true;
        return false;
    }
    Entry.bRwgSettledOnTerrain = true;
    Entry.RwgApiToTerrainDzCm = static_cast<float>(Entry.Location.Z - ApiZ1);
    ++RwgPcSettledTerrain;
    RwgRecordDz(Entry.Location, Entry.RwgApiToTerrainDzCm);
    RwgNoteSettledInsideCave(Q, Entry); // RWG P5: settledInsideCave census (surface entries; cave entries count in RwgResolveCaveAtSpawn)
    return true;
}

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
    }
    if (RwgPcSpawned > 0 || (RwgPcPasses % 12) == 1) // bounded: a pass that spawned, else once a minute
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("PLACECENSUS: pass=%d attempted=%d spawned=%d notSpawned=%d deferredSettle=%d waterLockedNew=%d apiRedealCalls=%d budgetSkipped=%d farNoAttempt=%d liveReasserted=%d spawnTraceHit=%d spawnTraceMiss=%d spawnTraceFar=%d farTerrain=%d farCanopy=%d farWater=%d farOther=%d farSolid=%d rejectedRock=%d settledOnTerrain=%d noTerrainDefer=%d spawnSettlesWithAnySkip=%d terrainOnlyCapHitsAllCallers=%d fallbackNoApi=%d attemptRadiusM=%.0f spawnRadiusM=%.0f layoutEntries=%d caveSettleTried=%d caveSettled=%d caveDeferred=%d caveEscaped=%d caveFar=%d caveSolid=%d caveWater=%d caveRedealCave=%d caveRedealSurface=%d insideCaveChecked=%d settledInsideCave=%d settledInsideCaveSurfaceEntry=%d"),
            RwgPcPasses, RwgPcAttempted, RwgPcSpawned, RwgPcAttempted - RwgPcSpawned, DeferredThisPass - DeferredBefore,
            WaterLockedThisSession.Num() - WaterLockedBefore, RwgPcRedealApi, RwgPcBudgetSkipped, RwgPcFarNoAttempt, RwgPcLive,
            RwgPcTraceHit, RwgPcTraceMiss, RwgPcTraceFar, RwgPcFarTerrain, RwgPcFarCanopy, RwgPcFarWater, RwgPcFarOther, RwgPcFarSolid, RwgPcRejectedRock,
            RwgPcSettledTerrain, RwgPcNoTerrainDefer, RwgPcSkippedToTerrain, RwgProbeCapHits,
            RwgPcFallback, AttemptRadiusCm / 100.f, SpawnRadiusCm / 100.f, Layout.Num(), RwgPcCaveSettleTried, RwgPcCaveSettled, RwgPcCaveDeferred, RwgPcCaveEscaped,
            RwgPcCaveFar, RwgPcCaveSolid, RwgPcCaveWater, RwgPcCaveRedealCave, RwgPcCaveRedealSurface, RwgPcInsideCaveChecked, RwgPcSettledInsideCave, RwgPcSettledInsideCaveSurfaceEntry);
        RwgEmitDzCensus(); // RWG P2c: session apiToTerrainDz distribution by band, emitted with PLACECENSUS
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
    RwgPcFarTerrain = RwgPcFarCanopy = RwgPcFarWater = RwgPcFarOther = 0;
    RwgPcFarSolid = RwgPcRejectedRock = 0; // RWG P2d
    RwgPcSettledTerrain = RwgPcNoTerrainDefer = RwgPcSkippedToTerrain = 0;
    RwgProbeCapHits = 0;
    RwgPcCaveSettleTried = RwgPcCaveSettled = RwgPcCaveDeferred = RwgPcCaveEscaped = RwgPcCaveFar = RwgPcCaveSolid = RwgPcCaveWater = 0; // RWG P5
    RwgPcCaveRedealCave = RwgPcCaveRedealSurface = RwgPcInsideCaveChecked = RwgPcSettledInsideCave = RwgPcSettledInsideCaveSurfaceEntry = 0;
}

void ANodeShuffleSubsystem::RwgLogSkipOnce(int32 Bit, const TCHAR* What)
{
    if ((RwgSkipLogged & (1 << Bit)) != 0) { return; }
    RwgSkipLogged |= (1 << Bit);
    UE_LOG(LogNodeShuffle, Display, TEXT("RWGSKIP: %s skipped off vanilla; map=%s"), What,
        GetMapMode() == ENodeShuffleMapMode::Rwg ? TEXT("rwg") : TEXT("other"));
}
