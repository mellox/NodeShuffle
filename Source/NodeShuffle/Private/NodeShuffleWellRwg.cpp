// RWG P4 (2026-10-18) -- resource wells on RWG maps. Brief: _team/nodeshuffle-rwg-fullshuffle-spec.md §H P4 + the
// 2026-10-18 P4 dispatch. The vanilla well pipeline (RollWellLayout / RollWellRelocation / ApplyWellRetype /
// ApplyWellRelocation) keys EVERYTHING on actor paths (CorePath, SatellitePath), which are stable for level actors and
// NOT stable on RWG: RWG re-spawns its fracking actors under new names on every load (R1). This file gives an RWG well
// an identity that survives that: a CLAIM (FNodeShuffleWellEntry::RwgClaimGuid + RwgClaimCoreLocation, and each
// satellite's RwgClaimLocation, all SaveGame). The claim is minted when RollWellLayout first enrols the core, and on a
// reload RwgRebindWellClaimsOnce re-binds each saved claim to the live core at its claim location (one-to-one) and
// rewrites the entry's paths to this session's paths -- so every path-keyed consumer of the vanilla pipeline then works
// unchanged for the session. The dealer below picks RWG well sites through the bound height/water/extent API with the
// well's whole footprint checked. Every function here is reached only behind a map-mode test at its call site.
// Log lines report measured counts and the predicate that branched, never a cause.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"
#include "NodeShuffleWellCensus.h"   // FNodeShuffleWellMember, the fracking core/satellite types
#include "NodeShuffleWellRetype.h"   // WellPathOf / WellClassPathOf
#include "NodeShuffleWellRelocate.h" // WellAdoptMatchRadiusCm
#include "EngineUtils.h"
#include "Engine/World.h"

namespace
{
    // A claim whose live actor cannot be found gets a path no actor can have (no '.' or ':' so no object lookup
    // parses it as a subobject path), so a stale name from an earlier session can never alias a different live actor.
    FString RwgUnboundPath(const FGuid& G, int32 SatIndex)
    {
        FString P = FString(TEXT("RWGUNBOUND_")) + G.ToString();
        if (SatIndex >= 0) { P += FString::Printf(TEXT("_sat%d"), SatIndex); }
        return P;
    }
}

bool ANodeShuffleSubsystem::RwgIsClaimedOriginal(const AActor* A) const
{
    // Empty on vanilla (only the RWG paths below add to it), so the four adoption loops in NodeShuffleWellLink.cpp
    // that consult this are unchanged there.
    return A && RwgClaimedOriginals.Num() > 0 && RwgClaimedOriginals.Contains(FObjectKey(A));
}

bool ANodeShuffleSubsystem::RwgWellCoreNearExistingClaim(const FVector& CoreLoc, const TArray<FNodeShuffleWellEntry>& Entries) const
{
    const float MatchSq = FMath::Square(WellAdoptMatchRadiusCm);
    for (const FNodeShuffleWellEntry& E : Entries)
    {
        if (!E.RwgClaimGuid.IsValid()) { continue; }
        if (FVector::DistSquared(E.RwgClaimCoreLocation, CoreLoc) < MatchSq) { return true; }
    }
    return false;
}

void ANodeShuffleSubsystem::RwgStampWellClaim(FNodeShuffleWellEntry& E, const AFGResourceNodeFrackingCore* Core,
                                              const TArray<FNodeShuffleWellMember>& Members)
{
    if (!IsValid(Core)) { return; }
    if (!E.RwgClaimGuid.IsValid())
    {
        E.RwgClaimGuid = FGuid::NewGuid();
        E.RwgClaimCoreLocation = Core->GetActorLocation();
        ++RwgWrClaimedNew;
    }
    RwgClaimedOriginals.Add(FObjectKey(Core));
    for (const FNodeShuffleWellMember& M : Members)
    {
        if (!IsValid(M.Actor) || !M.bLocationFinite) { continue; }
        RwgClaimedOriginals.Add(FObjectKey(M.Actor));
        const FString SatPath = WellPathOf(M.Actor);
        for (FNodeShuffleWellSatellite& S : E.Satellites)
        {
            if (S.SatellitePath == SatPath && S.RwgClaimLocation.IsZero()) { S.RwgClaimLocation = M.Location; break; }
        }
    }
}

void ANodeShuffleSubsystem::RwgRebindWellClaimsOnce(const TCHAR* Caller)
{
    if (bRwgWellRebindDone || GetMapMode() == ENodeShuffleMapMode::Vanilla) { return; }
    bRwgWellRebindDone = true;
    UWorld* World = GetWorld();
    if (!World) { return; }
    const double T0 = FPlatformTime::Seconds();

    // Live RWG cores (ours excluded by the managed registry; on a reload our restored relocated cores are not yet
    // registered, so the dealer keeps every relocated site >= 2 x WellSatMaxRadiusCm from every claim location and a
    // claim match is limited to WellAdoptMatchRadiusCm).
    TArray<AFGResourceNodeFrackingCore*> Live;
    for (TActorIterator<AFGResourceNodeFrackingCore> It(World); It; ++It)
    {
        if (IsValid(*It) && !FNodeShuffleModule::IsManagedSpawnedNode(*It)) { Live.Add(*It); }
    }
    // Satellites per core: the core's OWN registration list first (Native_GetSatellites, FGResourceNodeFrackingCore.h:42),
    // then any satellite whose GetCore() points at the core and is not already listed (the authored link).
    TMap<AFGResourceNodeFrackingCore*, TArray<AFGResourceNodeFrackingSatellite*>> SatsByCore;
    int32 ViaList = 0, ViaLink = 0;
    for (AFGResourceNodeFrackingCore* C : Live)
    {
        TArray<AFGResourceNodeFrackingSatellite*>& L = SatsByCore.FindOrAdd(C);
        for (const TWeakObjectPtr<AFGResourceNodeFrackingSatellite>& W : C->Native_GetSatellites())
        {
            if (W.IsValid() && !L.Contains(W.Get())) { L.Add(W.Get()); ++ViaList; }
        }
    }
    for (TActorIterator<AFGResourceNodeFrackingSatellite> It(World); It; ++It)
    {
        AFGResourceNodeFrackingSatellite* Sat = *It;
        if (!IsValid(Sat)) { continue; }
        AFGResourceNodeFrackingCore* C = Sat->GetCore().Get();
        TArray<AFGResourceNodeFrackingSatellite*>* L = C ? SatsByCore.Find(C) : nullptr;
        if (L && !L->Contains(Sat)) { L->Add(Sat); ++ViaLink; }
    }

    const float MatchSq = FMath::Square(WellAdoptMatchRadiusCm);
    TSet<int32> UsedLive;
    int32 Saved = 0, Rebound = 0, Unchanged = 0, UnmatchedSaved = 0, Ambiguous = 0, SatsRebound = 0, SatsUnmatched = 0, Legacy = 0;
    for (FNodeShuffleWellEntry& E : WellLayout)
    {
        if (!E.RwgClaimGuid.IsValid())
        {
            // RWG P4 review F3: an unclaimed entry off vanilla holds actor names from the session that rolled it, and
            // RWG names are not keyed to identity across loads; neutralised exactly like an unmatched claim.
            ++Legacy;
            if (!E.CorePath.StartsWith(TEXT("RWGUNBOUND_")))
            {
                const FString Key = FString::Printf(TEXT("RWGUNBOUND_LEGACY_%d"), Legacy);
                E.CorePath = Key;
                for (int32 s = 0; s < E.Satellites.Num(); ++s) { E.Satellites[s].SatellitePath = FString::Printf(TEXT("%s_sat%d"), *Key, s); }
            }
            continue;
        }
        ++Saved;
        int32 Best = INDEX_NONE, InTol = 0;
        float BestSq = MatchSq;
        for (int32 i = 0; i < Live.Num(); ++i)
        {
            if (UsedLive.Contains(i)) { continue; }
            if (!E.CoreNodeClassPath.IsEmpty() && WellClassPathOf(Live[i]) != E.CoreNodeClassPath) { continue; }
            const float D = FVector::DistSquared(Live[i]->GetActorLocation(), E.RwgClaimCoreLocation);
            if (D >= MatchSq) { continue; }
            ++InTol;
            if (D < BestSq) { BestSq = D; Best = i; }
        }
        if (InTol > 1) { ++Ambiguous; Best = INDEX_NONE; }
        else if (Best == INDEX_NONE) { ++UnmatchedSaved; }
        if (Best == INDEX_NONE)
        {
            E.CorePath = RwgUnboundPath(E.RwgClaimGuid, -1);
            for (int32 s = 0; s < E.Satellites.Num(); ++s) { E.Satellites[s].SatellitePath = RwgUnboundPath(E.RwgClaimGuid, s); ++SatsUnmatched; }
            continue;
        }
        UsedLive.Add(Best);
        AFGResourceNodeFrackingCore* Core = Live[Best];
        const FString NewPath = WellPathOf(Core);
        if (NewPath == E.CorePath) { ++Unchanged; } else { ++Rebound; }
        E.CorePath = NewPath;
        RwgClaimedOriginals.Add(FObjectKey(Core));
        const TArray<AFGResourceNodeFrackingSatellite*>& Cands = SatsByCore.FindOrAdd(Core);
        TSet<int32> UsedSat;
        for (int32 s = 0; s < E.Satellites.Num(); ++s)
        {
            FNodeShuffleWellSatellite& S = E.Satellites[s];
            int32 SBest = INDEX_NONE;
            float SBestSq = MatchSq;
            if (!S.RwgClaimLocation.IsZero())
            {
                for (int32 c = 0; c < Cands.Num(); ++c)
                {
                    if (UsedSat.Contains(c)) { continue; }
                    const float D = FVector::DistSquared(Cands[c]->GetActorLocation(), S.RwgClaimLocation);
                    if (D < SBestSq) { SBestSq = D; SBest = c; }
                }
            }
            if (SBest == INDEX_NONE) { S.SatellitePath = RwgUnboundPath(E.RwgClaimGuid, s); ++SatsUnmatched; continue; }
            UsedSat.Add(SBest);
            S.SatellitePath = WellPathOf(Cands[SBest]);
            RwgClaimedOriginals.Add(FObjectKey(Cands[SBest]));
            ++SatsRebound;
        }
    }
    const int32 Claimed = Rebound + Unchanged;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLCLAIM: caller=%s savedClaims=%d claimed=%d rebound=%d pathUnchanged=%d unmatched=%d unmatchedSaved=%d ambiguous=%d unmatchedLive=%d liveCores=%d satsRebound=%d satsUnmatched=%d satsViaCoreList=%d satsViaCoreLink=%d legacyNoClaim=%d durationMs=%d"),
        Caller, Saved, Claimed, Rebound, Unchanged, UnmatchedSaved + Ambiguous, UnmatchedSaved, Ambiguous,
        Live.Num() - UsedLive.Num(), Live.Num(), SatsRebound, SatsUnmatched, ViaList, ViaLink, Legacy,
        static_cast<int32>((FPlatformTime::Seconds() - T0) * 1000.0));
    if (UnmatchedSaved + Ambiguous + SatsUnmatched > 0)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLCLAIM-UNBOUND: caller=%s cores=%d (noLiveCoreInTolerance=%d twoOrMoreInTolerance=%d) satellites=%d of savedClaims=%d hold an RWGUNBOUND_ path this session; no actor lookup by that path resolves, and an unplaced entry with one is not placed"),
            Caller, UnmatchedSaved + Ambiguous, UnmatchedSaved, Ambiguous, SatsUnmatched, Saved);
    }
}

bool ANodeShuffleSubsystem::RwgDealWellSite(const NodeShuffleMapMode::FRwgGroundQuery& Q, FRandomStream& Rng,
                                            const TArray<FVector>& SatOffsets, FVector& OutCore)
{
    // One draw. The core and every captured satellite offset (capture frame, yaw 0) must be inside the world and dry, and the site must be
    // >= 2 x WellSatMaxRadiusCm from every claimed original core. Placement (ns-t27-corefirst) does NOT replay these offsets: it settles the
    // core and redraws each satellite in the WellSatMinRadiusCm..WellSatMaxRadiusCm annulus, so this is a dry-land proxy around the core and
    // ValidateWellMemberSpot is the gate for the spots satellites take. The members' surface-height spread is MEASURED and gates nothing.
    ++RwgWrDrawn;
    const double H = Q.HalfExtent() - WellSatMaxRadiusCm;
    if (H <= 0.0) { ++RwgWrOutside; return false; }
    const double X = Rng.FRandRange(-H, H);
    const double Y = Rng.FRandRange(-H, H);
    if (Q.Outside(X, Y)) { ++RwgWrOutside; return false; }
    const double Zc = Q.SurfaceZ(X, Y);
    if (RwgIsPointInWaterCached(FVector(X, Y, Zc))) { ++RwgWrWater; return false; }
    double CoreRiseRun = 0.0; // RWG P2c: core-site API gradient above the settle cliff angle (satellites are redrawn at placement)
    if (RwgApiSlopeTooSteep(Q, X, Y, CoreRiseRun)) { ++RwgWrSlope; return false; }
    double ZMin = Zc, ZMax = Zc;
    for (const FVector& Off : SatOffsets)
    {
        const double Sx = X + Off.X, Sy = Y + Off.Y;
        if (Q.Outside(Sx, Sy)) { ++RwgWrSatOutside; return false; }
        const double Sz = Q.SurfaceZ(Sx, Sy);
        if (RwgIsPointInWaterCached(FVector(Sx, Sy, Sz))) { ++RwgWrSatWater; return false; }
        ZMin = FMath::Min(ZMin, Sz);
        ZMax = FMath::Max(ZMax, Sz);
    }
    const float KeepOutSq = FMath::Square(2.0f * WellSatMaxRadiusCm);
    for (const FNodeShuffleWellEntry& E : WellLayout)
    {
        if (E.RwgClaimGuid.IsValid() && FVector::DistSquared2D(E.RwgClaimCoreLocation, FVector(X, Y, 0.0)) < KeepOutSq)
        {
            ++RwgWrNearOriginal;
            return false;
        }
    }
    RwgWrZSpreadMaxCm = FMath::Max(RwgWrZSpreadMaxCm, static_cast<int32>(ZMax - ZMin));
    ++RwgWrResolvedZ;
    OutCore = FVector(X, Y, Zc);
    return true;
}

void ANodeShuffleSubsystem::RwgWellRollBegin()
{
    RwgWrCores = RwgWrNoAuthored = RwgWrClaimedNew = RwgWrMintRefused = RwgWrDrawn = 0;
    RwgWrOutside = RwgWrWater = RwgWrSatOutside = RwgWrSatWater = RwgWrNearOriginal = 0;
    RwgWrSpacing = RwgWrResolvedZ = RwgWrZSpreadMaxCm = 0;
    RwgWrSlope = RwgWrCoresOurs = 0; // RWG P2c
    RwgWrT0 = FPlatformTime::Seconds();
}

void ANodeShuffleSubsystem::RwgEmitWellRollCensus(bool bApiDeal, const FString& ApiStatus, int32 Eligible, int32 Dealt, int32 DealFailed)
{
    int32 BuiltOn = 0, Placed = 0;
    for (const FNodeShuffleWellEntry& E : WellLayout)
    {
        if (E.bPinned) { ++BuiltOn; }
        if (E.bGroupPlaced) { ++Placed; }
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLROLL: map=rwg deal=%s apiStatus='%s' cores=%d coresOurs=%d entries=%d eligible=%d builtOn=%d dealt=%d dealFailed=%d drawn=%d rejectedOutside=%d rejectedWater=%d rejectedSlope=%d rejectedSatOutside=%d rejectedSatWater=%d rejectedNearOriginal=%d rejectedSpacing=%d resolvedZ=%d zSpreadMaxCm=%d noAuthoredResource=%d claimedNew=%d mintRefusedNearClaim=%d placedBefore=%d durationMs=%d"),
        bApiDeal ? TEXT("api") : TEXT("fallback"), *ApiStatus, RwgWrCores, RwgWrCoresOurs, WellLayout.Num(), Eligible, BuiltOn, Dealt, DealFailed,
        RwgWrDrawn, RwgWrOutside, RwgWrWater, RwgWrSlope, RwgWrSatOutside, RwgWrSatWater, RwgWrNearOriginal, RwgWrSpacing, RwgWrResolvedZ,
        RwgWrZSpreadMaxCm, RwgWrNoAuthored, RwgWrClaimedNew, RwgWrMintRefused, Placed,
        static_cast<int32>((FPlatformTime::Seconds() - RwgWrT0) * 1000.0));
}

void ANodeShuffleSubsystem::RwgEmitWellPlace(int32 Searching, int32 PlacedNow, int32 Deferred)
{
    ++RwgWpPasses;
    if (Searching > 0 || PlacedNow > 0 || (RwgWpPasses % 12) == 1) // bounded: a pass that searched/placed, else once a minute
    {
        int32 Dealt = 0, Placed = 0, Failed = 0;
        for (const FNodeShuffleWellEntry& E : WellLayout)
        {
            if (E.bGroupPlaced) { ++Placed; }
            else if (E.bDestDealt && !E.bRelocationFailed) { ++Dealt; }
            if (E.bRelocationFailed) { ++Failed; }
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLPLACE: pass=%d searching=%d placedNow=%d deferred=%d farNoAttempt=%d dealtUnplaced=%d placedTotal=%d relocationFailed=%d attemptRadiusM=%.0f claimedOriginals=%d"),
            RwgWpPasses, Searching, PlacedNow, Deferred, RwgWpFar, Dealt, Placed, Failed,
            NodeShuffleMapMode::RwgAttemptRadiusCm / 100.f, RwgClaimedOriginals.Num());
    }
    RwgWpFar = 0;
}
