// RWG density + stuck census (2026-10-19, packet rwg-density-1) -- DIAGNOSTICS ONLY: nothing here gates, moves, spawns or redeals.
// Motivation: _team/test-logs/2026-10-19-nodeshuffle-r16-local-density.md (only a handful of layout entries within 1 km of the pawn, 1-5
// repeatedly deferred, no per-entry deferred list and no per-region density figure in the log).
// Three lines, all Display, same gate as PLACECENSUS / ROLLCENSUS (no config flag; reached only from RWG call sites):
//   RWGDENSITY      once per roll / re-roll after the deal (RollLayout, behind an Rwg map-mode test) and once on load (first RwgSpawnPass
//                   of a session that did not roll, after RwgSeaRedealPass): a 10 x 10 grid over the deal square, per cell the active
//                   entries and the land samples of a fixed 8 x 8 sub-sample through the deal's own land test (RwgDensityLandSample).
//   RWGSTUCK        every 12th RwgSpawnPass: entries that failed RwgStuckMinFailedAttempts or more spawn attempts, with this pass's API reading.
//   HERE-DENSITY    one line appended to NodeShuffle.Here on RWG.
// Every value printed is a count or a predicate evaluated in THIS session; no line states why an entry did not spawn.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"
#include "Engine/World.h"
#include "Resources/FGResourceNode.h"

namespace
{
    constexpr int32 RwgDensityGridN = 10;            // grid cells per side over the deal square (30 km square -> 3 km cells)
    constexpr int32 RwgDensitySubSample = 8;         // land sub-sample points per cell side (8 x 8 = 64 per cell, 6400 per census) // TODO(2026-10-19 rwg-density-1): cost unmeasured (predicted 200-400 ms/census); read landSampleMs, drop to 4 if > 400, gate or remove before a public release.
    constexpr int32 RwgStuckMinFailedAttempts = 3;   // stuck = at least this many failed spawn-attempt passes since the entry was last live
    constexpr int32 RwgStuckMaxEntryLines = 10;      // RWGSTUCK-ENTRY lines per census (nearest first)
    // The PLACECENSUS field name each tracked counter prints under (killboxSettleRefused prints on RWGKILLBOXSETTLE).
    // Index order is RwgStuckCounter's switch order.
    const TCHAR* const RwgStuckCounterNames[] = {
        TEXT("spawnTraceHit"), TEXT("spawnTraceMiss"), TEXT("spawnTraceFar"), TEXT("farTerrain"), TEXT("farCanopy"), TEXT("farWater"),
        TEXT("farOther"), TEXT("farSolid"), TEXT("rejectedRock"), TEXT("settledOnTerrain"), TEXT("noTerrainDefer"), TEXT("apiRedealCalls"),
        TEXT("fallbackNoApi"), TEXT("caveSettleTried"), TEXT("caveSettled"), TEXT("caveDeferred"), TEXT("caveEscaped"), TEXT("caveFar"),
        TEXT("caveSolid"), TEXT("caveWater"), TEXT("caveRedealCave"), TEXT("caveRedealSurface"), TEXT("killboxSettleRefused"), TEXT("deferredSettle") };
    constexpr int32 RwgStuckCounterN = UE_ARRAY_COUNT(RwgStuckCounterNames);
    static_assert(RwgStuckCounterN == 24, "RwgStuckCounterNames must match RwgStuckCounter's switch (cases 0-23)");
    FString RwgDensityShortPath(const FString& P) { int32 Dot = INDEX_NONE; return P.FindLastChar(TEXT('.'), Dot) ? P.Mid(Dot + 1) : P; }
}

int32 ANodeShuffleSubsystem::RwgStuckCounter(int32 I) const
{
    switch (I)
    {
    case 0: return RwgPcTraceHit;       case 1: return RwgPcTraceMiss;       case 2: return RwgPcTraceFar;
    case 3: return RwgPcFarTerrain;     case 4: return RwgPcFarCanopy;       case 5: return RwgPcFarWater;
    case 6: return RwgPcFarOther;       case 7: return RwgPcFarSolid;        case 8: return RwgPcRejectedRock;
    case 9: return RwgPcSettledTerrain; case 10: return RwgPcNoTerrainDefer; case 11: return RwgPcRedealApi;
    case 12: return RwgPcFallback;      case 13: return RwgPcCaveSettleTried; case 14: return RwgPcCaveSettled;
    case 15: return RwgPcCaveDeferred;  case 16: return RwgPcCaveEscaped;    case 17: return RwgPcCaveFar;
    case 18: return RwgPcCaveSolid;     case 19: return RwgPcCaveWater;      case 20: return RwgPcCaveRedealCave;
    case 21: return RwgPcCaveRedealSurface; case 22: return RwgKbSettleRefused;
    case 23: return DeferredThisPass;
    default: return 0;
    }
}

void ANodeShuffleSubsystem::RwgStuckBeforeAttempt(const FNodeShuffleEntry& E)
{
    // Snapshot only: the counters are read, never written, and the attempt itself is the caller's unchanged EnsureNewNodeSpawned.
    RwgStuckSnap.SetNumZeroed(RwgStuckCounterN);
    for (int32 I = 0; I < RwgStuckCounterN; ++I) { RwgStuckSnap[I] = RwgStuckCounter(I); }
    bRwgStuckSnapWaterLocked = WaterLockedThisSession.Contains(E.EntryGuid);
    RwgStuckSnapLoc = E.Location;
}

void ANodeShuffleSubsystem::RwgStuckAfterAttempt(const FNodeShuffleEntry& E)
{
    // Live after the attempt -> forgotten (the count is "failed attempts since last live"). Not live -> +1, and the names of the
    // counters this one attempt moved are kept ("none" when the attempt moved no tracked counter, e.g. an already-settled entry).
    AFGResourceNode* const* Now = SpawnedNodes.Find(E.EntryGuid);
    if (Now && IsValid(*Now)) { RwgStuckTracker.Remove(E.EntryGuid); return; }
    FString Moved;
    for (int32 I = 0; I < RwgStuckCounterN && I < RwgStuckSnap.Num(); ++I)
    {
        if (RwgStuckCounter(I) != RwgStuckSnap[I]) { if (!Moved.IsEmpty()) { Moved += TEXT("+"); } Moved += RwgStuckCounterNames[I]; }
    }
    if (!bRwgStuckSnapWaterLocked && WaterLockedThisSession.Contains(E.EntryGuid)) { if (!Moved.IsEmpty()) { Moved += TEXT("+"); } Moved += TEXT("waterLockedNew"); }
    TPair<int32, FString>& T = RwgStuckTracker.FindOrAdd(E.EntryGuid, TPair<int32, FString>(0, FString()));
    if (FVector::DistSquared2D(RwgStuckSnapLoc, E.Location) > 1.0) { T.Key = 0; } else { ++T.Key; } // moved in XY by this attempt: no attempt has run at the new spot
    T.Value = Moved.IsEmpty() ? FString(TEXT("none")) : Moved;
}

bool ANodeShuffleSubsystem::RwgDensityLandSample(const NodeShuffleMapMode::FRwgGroundQuery& Q, double X, double Y)
{
    // The deal's land test (RwgDealOneSpot), in its order, through the SAME functions -- not a copy: Outside, then predicate v3 at the API
    // surface, then the API slope. Spacing is not a land property and is not tested. No roll counter is touched.
    if (Q.Outside(X, Y)) { return false; }
    if (RwgRejectV3(Q, FVector(X, Y, Q.SurfaceZ(X, Y))) != 0) { return false; }
    double RiseRun = 0.0;
    return !RwgApiSlopeTooSteep(Q, X, Y, RiseRun);
}

void ANodeShuffleSubsystem::RwgDensityOnRoll()
{
    // A roll mints new entry guids: the per-entry attempt history of the previous layout no longer names anything.
    RwgStuckTracker.Reset();
    RwgEmitDensityCensus(TEXT("roll"));
}

void ANodeShuffleSubsystem::RwgEmitDensityCensus(const TCHAR* Trigger)
{
    bRwgDensityEmitted = true;
    const NodeShuffleMapMode::FRwgGroundQuery Q(GetWorld(), RwgGenerator.Get());
    if (!Q.IsUsable())
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGDENSITY-SKIP: trigger=%s apiUsable=%d apiStatus='%s' layoutEntries=%d; no grid printed (the cell frame is the API half extent)"),
            Trigger, Q.IsUsable() ? 1 : 0, *Q.Status(), Layout.Num());
        return;
    }
    if (RwgWaterVolumes.Num() == 0) { RwgBuildWaterVolumeCache(); }
    constexpr int32 N = RwgDensityGridN;
    constexpr int32 S = RwgDensitySubSample;
    const double H = Q.HalfExtent();
    const double Cell = 2.0 * H / N;
    int32 Active[N * N] = {};
    int32 Land[N * N] = {};
    int32 TotalActive = 0, InSquare = 0, CaveActive = 0;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bIsNewNode || !E.bActive) { continue; }
        ++TotalActive;
        if (E.bRwgCave) { ++CaveActive; }
        const int32 Cx = FMath::FloorToInt32((E.Location.X + H) / Cell);
        const int32 Cy = FMath::FloorToInt32((E.Location.Y + H) / Cell);
        if (Cx < 0 || Cx >= N || Cy < 0 || Cy >= N) { continue; }
        ++InSquare;
        ++Active[Cy * N + Cx];
    }
    const int32 LatchMaskBefore = (bRwgSeaUsable ? 1 : 0) | (bRwgVoidUsable ? 2 : 0) | (bRwgKillboxKnown ? 4 : 0);
    const double T0 = FPlatformTime::Seconds();
    for (int32 Cy = 0; Cy < N; ++Cy)
    {
        for (int32 Cx = 0; Cx < N; ++Cx)
        {
            for (int32 Sy = 0; Sy < S; ++Sy)
            {
                for (int32 Sx = 0; Sx < S; ++Sx)
                {
                    const double X = -H + (Cx + (Sx + 0.5) / S) * Cell;
                    const double Y = -H + (Cy + (Sy + 0.5) / S) * Cell;
                    if (RwgDensityLandSample(Q, X, Y)) { ++Land[Cy * N + Cx]; }
                }
            }
        }
    }
    const double LandMs = (FPlatformTime::Seconds() - T0) * 1000.0;
    const int32 LatchMaskAfter = (bRwgSeaUsable ? 1 : 0) | (bRwgVoidUsable ? 2 : 0) | (bRwgKillboxKnown ? 4 : 0);
    double LandWeighted = 0.0;
    int32 LandTotal = 0;
    for (int32 I = 0; I < N * N; ++I) { LandTotal += Land[I]; LandWeighted += static_cast<double>(Land[I]) / (S * S); }
    const double ExpectedFull = LandWeighted > 0.0 ? InSquare / LandWeighted : 0.0;
    TArray<int32> Cells50;
    double Expected50Sum = 0.0;
    for (int32 Cy = 0; Cy < N; ++Cy)
    {
        FString Pairs;
        for (int32 Cx = 0; Cx < N; ++Cx)
        {
            const int32 I = Cy * N + Cx;
            Pairs += FString::Printf(TEXT("%s%d/%d"), Cx == 0 ? TEXT("") : TEXT(" "), Active[I], Land[I]);
            if (Land[I] * 2 >= S * S) { Cells50.Add(Active[I]); Expected50Sum += ExpectedFull * Land[I] / (S * S); }
        }
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGDENSITY-ROW: trigger=%s row=%d yMinM=%.0f xMinM=%.0f cellM=%.0f landSamplesPerCell=%d activeSlashLand=%s"),
            Trigger, Cy, (-H + Cy * Cell) / 100.0, -H / 100.0, Cell / 100.0, S * S, *Pairs);
    }
    Cells50.Sort();
    const int32 C50 = Cells50.Num();
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGDENSITY: trigger=%s totalActive=%d inSquare=%d caveActive=%d gridN=%d cellM=%.0f landSamplesPerCell=%d landSamples=%d samples=%d landWeightedCells=%.2f expectedPerFullLandCell=%.2f landCells50=%d minEntriesLandCells50=%d lowerMedianEntriesLandCells50=%d maxEntriesLandCells50=%d meanExpectedLandCells50=%.2f seaUsable=%d voidUsable=%d killboxKnown=%d landSampleMs=%.0f layoutEntries=%d latchesChangedDuringSample=%d"),
        Trigger, TotalActive, InSquare, CaveActive, N, Cell / 100.0, S * S, LandTotal, N * N * S * S, LandWeighted, ExpectedFull, C50,
        C50 > 0 ? Cells50[0] : 0, C50 > 0 ? Cells50[(C50 - 1) / 2] : 0, C50 > 0 ? Cells50.Last() : 0, C50 > 0 ? Expected50Sum / C50 : 0.0,
        bRwgSeaUsable ? 1 : 0, bRwgVoidUsable ? 1 : 0, bRwgKillboxKnown ? 1 : 0, LandMs, Layout.Num(), LatchMaskBefore != LatchMaskAfter ? 1 : 0);
}

void ANodeShuffleSubsystem::RwgDensityPassHook(const TArray<FVector>& Pawns, float AttemptRadiusCm)
{
    if (!bRwgDensityEmitted) { RwgEmitDensityCensus(TEXT("load")); } // first pass of a session that did not roll (after RwgSeaRedealPass)
    if ((RwgPcPasses % 12) == 1) { RwgEmitStuckCensus(Pawns, AttemptRadiusCm); } // same cadence as RWGHIDECHECK / REHIDE-CENSUS
}

void ANodeShuffleSubsystem::RwgEmitStuckCensus(const TArray<FVector>& Pawns, float AttemptRadiusCm)
{
    const float R2 = FMath::Square(AttemptRadiusCm);
    int32 StuckMap = 0, StuckLocal = 0;
    TArray<TPair<double, int32>> Local;
    for (int32 I = 0; I < Layout.Num(); ++I)
    {
        const FNodeShuffleEntry& E = Layout[I];
        if (!E.bIsNewNode || !E.bActive) { continue; }
        const TPair<int32, FString>* T = RwgStuckTracker.Find(E.EntryGuid);
        if (!T || T->Key < RwgStuckMinFailedAttempts) { continue; }
        AFGResourceNode* const* Live = SpawnedNodes.Find(E.EntryGuid);
        if (Live && IsValid(*Live)) { continue; }
        ++StuckMap;
        double Best = TNumericLimits<double>::Max();
        for (const FVector& P : Pawns) { Best = FMath::Min(Best, FVector::DistSquared2D(P, E.Location)); }
        if (Best >= R2) { continue; } // same 2-D strict test as IsLocationNearAnyPlayer
        ++StuckLocal;
        Local.Emplace(Best, I);
    }
    Local.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
    const int32 Lines = FMath::Min(Local.Num(), RwgStuckMaxEntryLines);
    UE_LOG(LogNodeShuffle, Display, TEXT("RWGSTUCK: pass=%d threshold=%d attemptRadiusM=%.0f pawns=%d stuckInAttemptRadius=%d stuckTotalMap=%d trackedEntries=%d entryLinesPrinted=%d"),
        RwgPcPasses, RwgStuckMinFailedAttempts, AttemptRadiusCm / 100.f, Pawns.Num(), StuckLocal, StuckMap, RwgStuckTracker.Num(), Lines);
    if (Lines == 0) { return; }
    const NodeShuffleMapMode::FRwgGroundQuery Q(GetWorld(), RwgGenerator.Get());
    const bool bQ = Q.IsUsable();
    for (int32 K = 0; K < Lines; ++K)
    {
        const FNodeShuffleEntry& E = Layout[Local[K].Value];
        const TPair<int32, FString>& T = RwgStuckTracker.FindChecked(E.EntryGuid);
        const double X = E.Location.X, Y = E.Location.Y;
        const bool bOut = bQ && Q.Outside(X, Y);
        double ApiZ = NodeShuffleMapMode::RwgNaN(), RiseRun = NodeShuffleMapMode::RwgNaN();
        int32 Bits = -1;
        if (bQ && !bOut)
        {
            ApiZ = Q.SurfaceZ(X, Y);
            Bits = RwgRejectV3(Q, FVector(X, Y, ApiZ));
            RiseRun = 0.0;
            RwgApiSlopeTooSteep(Q, X, Y, RiseRun);
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RWGSTUCK-ENTRY: pass=%d entry=%s res=%s loc=%.0f/%.0f/%.0f distM=%.0f failedAttemptsAtLoc=%d lastAttemptCounters=%s settled=%d caveEntry=%d apiUsable=%d apiOutside=%d apiSurfaceZ=%.0f apiWater=%d volumeWater=%d rejectV3Bits=%d apiSlopeRiseRun=%.2f parkedThisSession=%d dormantThisSession=%d"),
            RwgPcPasses, *E.EntryGuid.ToString().Left(8), *RwgDensityShortPath(E.AssignedResourceClassPath), X, Y, E.Location.Z,
            FMath::Sqrt(Local[K].Key) / 100.0, T.Key, *T.Value, E.bRayCasted ? 1 : 0, E.bRwgCave ? 1 : 0, bQ ? 1 : 0, bQ ? (bOut ? 1 : 0) : -1, ApiZ,
            Bits < 0 ? -1 : ((Bits & 5) != 0 ? 1 : 0), Bits < 0 ? -1 : ((Bits & 2) != 0 ? 1 : 0), Bits, RiseRun,
            SpawnParkedThisSession.Contains(E.EntryGuid) ? 1 : 0, DormantThisSession.Contains(E.EntryGuid) ? 1 : 0);
    }
}

void ANodeShuffleSubsystem::LogHereDensity(const FVector& P, float HereRadiusCm) const
{
    // Population: layout entries with bIsNewNode && bActive; distance is 2-D from the HERE pawn. live + deferred + noFailedAttempt == active.
    const float Radii[2] = { NodeShuffleMapMode::RwgAttemptRadiusCm, HereRadiusCm };
    int32 A[2] = {}, L[2] = {}, D[2] = {}, St[2] = {};
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bIsNewNode || !E.bActive) { continue; }
        const double D2 = FVector::DistSquared2D(E.Location, P);
        AFGResourceNode* const* Live = SpawnedNodes.Find(E.EntryGuid);
        const bool bLive = Live && IsValid(*Live);
        const TPair<int32, FString>* T = bLive ? nullptr : RwgStuckTracker.Find(E.EntryGuid);
        for (int32 R = 0; R < 2; ++R)
        {
            if (D2 >= FMath::Square(static_cast<double>(Radii[R]))) { continue; }
            ++A[R];
            if (bLive) { ++L[R]; }
            else if (T && T->Key >= 1) { ++D[R]; if (T->Key >= RwgStuckMinFailedAttempts) { ++St[R]; } }
        }
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("HERE-DENSITY: within %.0f m: active=%d live=%d deferred=%d stuck=%d noFailedAttempt=%d; within %.0f m: active=%d live=%d deferred=%d stuck=%d noFailedAttempt=%d (deferred counts unspawned active entries with at least one failed spawn attempt since they were last live or last moved in XY, stuck ones included; stuck needs %d such passes; noFailedAttempt is the rest of the unspawned active entries)"),
        Radii[0] / 100.f, A[0], L[0], D[0], St[0], A[0] - L[0] - D[0], Radii[1] / 100.f, A[1], L[1], D[1], St[1], A[1] - L[1] - D[1], RwgStuckMinFailedAttempts);
    // HERE-LAND (cold review F1): the deal's land predicate measured at the player's own position -- a 100 m lattice inside the 1000 m attempt
    // radius, plus the RwgRejectV3 bits at the pawn's feet. Pure reads of the predicate; user-invoked only (NodeShuffle.Here on RWG).
    ANodeShuffleSubsystem* const Self = const_cast<ANodeShuffleSubsystem*>(this); // RwgRejectV3 is non-const only for its session latches; this object is not const
    const NodeShuffleMapMode::FRwgGroundQuery Q(GetWorld(), RwgGenerator.Get()); constexpr double Step = 10000.0; const double R = Radii[0]; int32 LandPts = 0, DiskPts = 0, PawnBits = -1; const double T0 = FPlatformTime::Seconds();
    if (Q.IsUsable()) { for (double Dy = -R + 0.5 * Step; Dy < R; Dy += Step) { for (double Dx = -R + 0.5 * Step; Dx < R; Dx += Step) { if (Dx * Dx + Dy * Dy >= R * R) { continue; } ++DiskPts; if (Self->RwgDensityLandSample(Q, P.X + Dx, P.Y + Dy)) { ++LandPts; } } } if (!Q.Outside(P.X, P.Y)) { PawnBits = Self->RwgRejectV3(Q, FVector(P.X, P.Y, Q.SurfaceZ(P.X, P.Y))); } }
    UE_LOG(LogNodeShuffle, Display, TEXT("HERE-LAND: within %.0f m: landPoints=%d diskPoints=%d spacingM=%.0f pawnRejectV3Bits=%d apiUsable=%d seaUsable=%d voidUsable=%d killboxKnown=%d landSampleMs=%.0f"), R / 100.0, LandPts, DiskPts, Step / 100.0, PawnBits, Q.IsUsable() ? 1 : 0, bRwgSeaUsable ? 1 : 0, bRwgVoidUsable ? 1 : 0, bRwgKillboxKnown ? 1 : 0, (FPlatformTime::Seconds() - T0) * 1000.0);
}
