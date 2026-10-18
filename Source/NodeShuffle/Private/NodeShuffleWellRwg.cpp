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

void ANodeShuffleSubsystem::RwgRetryUnboundWellSatellites()
{
    // RWG P4b (2026-10-18): the once-per-session re-bind above runs at the first apply/roll pass; on a measured reload
    // (trwg_autosave_2) every claimed core re-bound by location while every satellite read RWGUNBOUND_ (satsViaCoreList=0),
    // and the WELLH2-SUPPRESS lines that followed hid 0 members per group. This pass re-tries ONLY those satellites, every
    // pass, for claims whose CORE is bound: the core's own satellite list first (Native_GetSatellites,
    // FGResourceNodeFrackingCore.h:42), then any live satellite within WellAdoptMatchRadiusCm of the satellite's claim
    // location. Bounded: at most RwgWellRetryClaimsPerPass claims per pass, nearest the pawn first (a claim that keeps
    // failing drops one tier per RwgWellRetryTierPasses attempts, so it can never hold a slot forever). A live actor is
    // bound at most once per session: RwgClaimedOriginals is the used set and every bind adds to it.
    // Not handled here, deliberately: a claim whose CORE is a sentinel (F1 keeps it unplaced) and legacy entries
    // (RWGUNBOUND_LEGACY_, no claim location). Session-only state; idempotent across reloads because the once-rebind
    // re-derives every path from claim LOCATIONS on the next load.
    if (GetMapMode() == ENodeShuffleMapMode::Vanilla) { return; }
    // Our own restored relocated satellites are not in the managed registry until AdoptRestoredWellGroups has run once
    // (first ApplyWellRelocation of the session); before that, a location match could not tell them from an original.
    if (!bRwgWellRebindDone || !bAdoptedRestoredWells) { return; }
    UWorld* World = GetWorld();
    if (!World) { return; }
    const FString Sentinel(TEXT("RWGUNBOUND_"));

    // Population: claimed entries with a bound core and >= 1 sentinel satellite that has a claim location to match against.
    TArray<TPair<double, int32>> Cands;
    int32 StillUnboundBefore = 0;
    for (int32 I = 0; I < WellLayout.Num(); ++I)
    {
        const FNodeShuffleWellEntry& E = WellLayout[I];
        if (!E.RwgClaimGuid.IsValid() || E.CorePath.StartsWith(Sentinel)) { continue; }
        bool bBindable = false;
        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            if (!S.SatellitePath.StartsWith(Sentinel)) { continue; }
            ++StillUnboundBefore;
            if (!S.RwgClaimLocation.IsZero()) { bBindable = true; }
        }
        if (!bBindable) { continue; }
        const int32 Tries = RwgWellRetryAttempts.FindRef(E.RwgClaimGuid);
        const double Tier = static_cast<double>(Tries / NodeShuffleMapMode::RwgWellRetryTierPasses);
        Cands.Emplace(Tier * 1.0e12 + RwgNearestPawnDist2D(E.RwgClaimCoreLocation), I);
    }
    if (Cands.Num() == 0 && StillUnboundBefore == 0 && bRwgWellRetryQuiet) { return; }
    ++RwgWellRetryPasses;
    Cands.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
    const int32 N = FMath::Min(Cands.Num(), NodeShuffleMapMode::RwgWellRetryClaimsPerPass);

    // Location candidates, built once per pass only when a claim is pending: live, not ours (managed registry or our spawned
    // map), and not already bound to a claim this session.
    TSet<const AActor*> Ours;
    for (const TPair<FString, AFGResourceNodeFrackingSatellite*>& KV : SpawnedWellSatellites) { if (KV.Value) { Ours.Add(KV.Value); } }
    TArray<AFGResourceNodeFrackingSatellite*> Loose;
    if (N > 0)
    {
        for (TActorIterator<AFGResourceNodeFrackingSatellite> It(World); It; ++It)
        {
            AFGResourceNodeFrackingSatellite* Sat = *It;
            if (!IsValid(Sat) || FNodeShuffleModule::IsManagedSpawnedNode(Sat) || Ours.Contains(Sat) || RwgIsClaimedOriginal(Sat)) { continue; }
            Loose.Add(Sat);
        }
    }

    const float MatchSq = FMath::Square(WellAdoptMatchRadiusCm);
    int32 BoundNow = 0, ViaList = 0, ViaLocation = 0, NowComplete = 0, SuppressedNow = 0, RekeyMoved = 0, RekeyCollision = 0;
    for (int32 K = 0; K < N; ++K)
    {
        FNodeShuffleWellEntry& E = WellLayout[Cands[K].Value];
        ++RwgWellRetryAttempts.FindOrAdd(E.RwgClaimGuid);
        AFGResourceNodeFrackingCore* Core = Cast<AFGResourceNodeFrackingCore>(FindOriginalBaseByPath(E.CorePath));
        TArray<AFGResourceNodeFrackingSatellite*> Listed;
        if (IsValid(Core))
        {
            for (const TWeakObjectPtr<AFGResourceNodeFrackingSatellite>& W : Core->Native_GetSatellites())
            {
                AFGResourceNodeFrackingSatellite* Sat = W.Get();
                if (IsValid(Sat) && !RwgIsClaimedOriginal(Sat) && !FNodeShuffleModule::IsManagedSpawnedNode(Sat) && !Ours.Contains(Sat)) { Listed.Add(Sat); }
            }
        }
        int32 BoundHere = 0, LeftHere = 0;
        for (int32 s = 0; s < E.Satellites.Num(); ++s)
        {
            FNodeShuffleWellSatellite& S = E.Satellites[s];
            if (!S.SatellitePath.StartsWith(Sentinel)) { continue; }
            if (S.RwgClaimLocation.IsZero()) { ++LeftHere; continue; }
            const auto Nearest = [&](const TArray<AFGResourceNodeFrackingSatellite*>& Pool) -> AFGResourceNodeFrackingSatellite*
            {
                AFGResourceNodeFrackingSatellite* Best = nullptr;
                float BestSq = MatchSq;
                for (AFGResourceNodeFrackingSatellite* Sat : Pool)
                {
                    if (RwgIsClaimedOriginal(Sat)) { continue; } // bound earlier in this pass
                    if (IsValid(Core)) { const AFGResourceNodeFrackingCore* SC = Sat->GetCore().Get(); if (SC && SC != Core) { continue; } } // linked to another core: never this claim's (RWG P2d, p2c2 rereview F-B2)
                    const float D = FVector::DistSquared(Sat->GetActorLocation(), S.RwgClaimLocation);
                    if (D < BestSq) { BestSq = D; Best = Sat; }
                }
                return Best;
            };
            AFGResourceNodeFrackingSatellite* Hit = Nearest(Listed);
            if (Hit) { ++ViaList; }
            else if ((Hit = Nearest(Loose)) != nullptr) { ++ViaLocation; }
            if (!Hit) { ++LeftHere; continue; }
            const FString OldPath = S.SatellitePath;
            const FString NewPath = WellPathOf(Hit);
            // A spawned copy already keyed by the live path means another record holds that key. Test BEFORE touching the map:
            // on a collision the sentinel, our copy's key and the used set stay as they are, and the bind is skipped.
            if (SpawnedWellSatellites.Contains(NewPath))
            {
                ++RekeyCollision;
                ++LeftHere;
                if (!WellSuppressLogged.Contains(OldPath + TEXT("|retry-collision")))
                {
                    WellSuppressLogged.Add(OldPath + TEXT("|retry-collision"));
                    UE_LOG(LogNodeShuffle, Display,
                        TEXT("WELLCLAIM-RETRY-COLLISION: core='%s' sentinel='%s' livePath='%s'; a spawned copy is already keyed by the live path, bind skipped (said once per satellite)"),
                        *E.CorePath, *OldPath, *NewPath);
                }
                continue;
            }
            // OUR relocated copy is keyed by the record's path (adopted under the sentinel at load); re-key it with the record.
            AFGResourceNodeFrackingSatellite* OurCopy = nullptr;
            if (SpawnedWellSatellites.RemoveAndCopyValue(OldPath, OurCopy)) { SpawnedWellSatellites.Add(NewPath, OurCopy); ++RekeyMoved; }
            S.SatellitePath = NewPath;
            RwgClaimedOriginals.Add(FObjectKey(Hit));
            ++BoundHere;
        }
        BoundNow += BoundHere;
        if (BoundHere == 0) { continue; }
        bool bAnyLeft = LeftHere > 0;
        if (!bAnyLeft) { ++NowComplete; RwgWellRetryAttempts.Remove(E.RwgClaimGuid); }
        // The group's suppression runs NOW for a group that already owns it: placed (its originals must be hidden while our
        // copy stands) or unplaced with a suppression record and no restore pending (the chain's re-assert arm). Any other
        // unplaced group is left to ApplyWellRelocation's own arms later in this pass.
        if (E.bGroupPlaced || (!E.bUnhidePending && WellGroupHasSuppressedMember(E)))
        {
            SuppressVanillaWellGroup(E, EWellSuppressPhase::Apply);
            ++SuppressedNow;
        }
    }
    int32 StillUnbound = 0, Pending = 0;
    for (const FNodeShuffleWellEntry& E : WellLayout)
    {
        if (!E.RwgClaimGuid.IsValid() || E.CorePath.StartsWith(Sentinel)) { continue; }
        bool bP = false;
        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            if (S.SatellitePath.StartsWith(Sentinel)) { ++StillUnbound; if (!S.RwgClaimLocation.IsZero()) { bP = true; } }
        }
        if (bP) { ++Pending; }
    }
    // Bounded: a pass that bound something, else once a minute while claims are pending, and one closing line when none are.
    const bool bQuietNow = (Pending == 0);
    if (BoundNow > 0 || (Pending > 0 && (RwgWellRetryPasses % 12) == 1) || (bQuietNow && !bRwgWellRetryQuiet))
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLCLAIM-RETRY: pass=%d groupsPending=%d satsBoundNow=%d satsStillUnbound=%d groupsNowComplete=%d suppressCallsNow=%d claimsTriedThisPass=%d viaCoreList=%d viaLocation=%d looseCandidates=%d ourCopyRekeyed=%d rekeyCollision=%d"),
            RwgWellRetryPasses, Pending, BoundNow, StillUnbound, NowComplete, SuppressedNow, N, ViaList, ViaLocation, Loose.Num(),
            RekeyMoved, RekeyCollision);
    }
    bRwgWellRetryQuiet = bQuietNow;
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
    const int32 WetCore = RwgRejectV3(Q, FVector(X, Y, Zc)); // RWG P2f: predicate v3 (v2 sea | volume | inland level, | void | below killbox)
    if (WetCore != 0 && (WetCore & 7) == 0) { if ((WetCore & 8) != 0) { ++RwgWrRejectedVoid; } else { ++RwgWrRejectedKillbox; } return false; } // RWG P2f: not water
    if (WetCore != 0)
    {
        if ((WetCore & 1) != 0) { ++RwgWrRejectedSea; }
        // Coordinator addendum (measured only, gates nothing): rejectedWater split by the sign of the API surface Z.
        if (Zc < 0.0) { ++RwgWrWaterZBelow0; } else { ++RwgWrWaterZAtOrAbove0; if ((WetCore & 2) != 0) { RwgNoteWaterSample(Q, FVector(X, Y, Zc), true, TEXT("wellDeal")); } }
        ++RwgWrWater;
        return false;
    }
    double CoreRiseRun = 0.0; // RWG P2c: core-site API gradient above the settle cliff angle (satellites are redrawn at placement)
    if (RwgApiSlopeTooSteep(Q, X, Y, CoreRiseRun)) { ++RwgWrSlope; return false; }
    double ZMin = Zc, ZMax = Zc;
    for (const FVector& Off : SatOffsets)
    {
        const double Sx = X + Off.X, Sy = Y + Off.Y;
        if (Q.Outside(Sx, Sy)) { ++RwgWrSatOutside; return false; }
        const double Sz = Q.SurfaceZ(Sx, Sy);
        const int32 WetSat = RwgRejectV3(Q, FVector(Sx, Sy, Sz));
        if (WetSat != 0 && (WetSat & 7) == 0) { if ((WetSat & 8) != 0) { ++RwgWrRejectedVoid; } else { ++RwgWrRejectedKillbox; } return false; } // RWG P2f: not water
        if (WetSat != 0) { if ((WetSat & 1) != 0) { ++RwgWrRejectedSea; } ++RwgWrSatWater; return false; }
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
    if (Zc < 0.0) { ++RwgWrDealtBelowZ0; RwgNoteWaterSample(Q, OutCore, false, TEXT("wellDeal")); } // coordinator addendum, measured only
    if (bRwgKillboxKnown && Zc < RwgKillboxTopZ) { ++RwgWrDealtBelowKillbox; } // RWG P2f: measured control (v3 rejects these; expect 0)
    return true;
}

void ANodeShuffleSubsystem::RwgWellRollBegin()
{
    RwgWrCores = RwgWrNoAuthored = RwgWrClaimedNew = RwgWrMintRefused = RwgWrDrawn = 0;
    RwgWrOutside = RwgWrWater = RwgWrSatOutside = RwgWrSatWater = RwgWrNearOriginal = 0;
    RwgWrSpacing = RwgWrResolvedZ = RwgWrZSpreadMaxCm = 0;
    RwgWrSlope = RwgWrCoresOurs = 0; // RWG P2c
    RwgWrWaterZBelow0 = RwgWrWaterZAtOrAbove0 = RwgWrDealtBelowZ0 = 0; // coordinator addendum
    RwgWrRejectedSea = 0; // RWG P2e
    RwgWrRejectedVoid = RwgWrRejectedKillbox = RwgWrDealtBelowKillbox = 0; // RWG P2f
    RwgWrAuthoredLive = RwgWrAuthoredSaved = RwgWrAuthoredSatellite = RwgWrMembersFromClaimWells = RwgWrMembersFromClaimSats = 0; // RWG P4c
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
        TEXT("WELLROLL: map=rwg deal=%s apiStatus='%s' cores=%d coresOurs=%d entries=%d eligible=%d builtOn=%d dealt=%d dealFailed=%d drawn=%d rejectedOutside=%d rejectedWater=%d rejectedWaterSurfaceZBelow0=%d rejectedWaterSurfaceZAtOrAbove0=%d dealtBelowZ0=%d rejectedSlope=%d rejectedSatOutside=%d rejectedSatWater=%d rejectedSea=%d rejectedNearOriginal=%d rejectedSpacing=%d resolvedZ=%d zSpreadMaxCm=%d noAuthoredResource=%d claimedNew=%d mintRefusedNearClaim=%d placedBefore=%d durationMs=%d rejectedVoid=%d rejectedKillbox=%d dealtBelowKillbox=%d killboxTopZ=%.0f killboxTopActorZ=%.0f killboxTopApiZ=%.0f voidUsable=%d authoredFromLive=%d authoredFromSaved=%d authoredFromSatellite=%d membersFromClaimWells=%d membersFromClaimSats=%d"),
        bApiDeal ? TEXT("api") : TEXT("fallback"), *ApiStatus, RwgWrCores, RwgWrCoresOurs, WellLayout.Num(), Eligible, BuiltOn, Dealt, DealFailed,
        RwgWrDrawn, RwgWrOutside, RwgWrWater, RwgWrWaterZBelow0, RwgWrWaterZAtOrAbove0, RwgWrDealtBelowZ0, RwgWrSlope, RwgWrSatOutside, RwgWrSatWater, RwgWrRejectedSea, RwgWrNearOriginal, RwgWrSpacing, RwgWrResolvedZ,
        RwgWrZSpreadMaxCm, RwgWrNoAuthored, RwgWrClaimedNew, RwgWrMintRefused, Placed,
        static_cast<int32>((FPlatformTime::Seconds() - RwgWrT0) * 1000.0), RwgWrRejectedVoid, RwgWrRejectedKillbox, RwgWrDealtBelowKillbox,
        bRwgKillboxKnown ? RwgKillboxTopZ : NodeShuffleMapMode::RwgNaN(), bRwgKillboxActorKnown ? RwgKillboxTopActorZ : NodeShuffleMapMode::RwgNaN(),
        bRwgKillboxApiKnown ? RwgKillboxTopApiZ : NodeShuffleMapMode::RwgNaN(), bRwgVoidUsable ? 1 : 0,
        RwgWrAuthoredLive, RwgWrAuthoredSaved, RwgWrAuthoredSatellite, RwgWrMembersFromClaimWells, RwgWrMembersFromClaimSats); // RWG P4c
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
            TEXT("WELLPLACE: pass=%d searching=%d placedNow=%d deferred=%d farNoAttempt=%d dealtUnplaced=%d placedTotal=%d relocationFailed=%d rejectedRock=%d attemptRadiusM=%.0f claimedOriginals=%d"),
            RwgWpPasses, Searching, PlacedNow, Deferred, RwgWpFar, Dealt, Placed, Failed, RwgWpRejectedRock,
            NodeShuffleMapMode::RwgAttemptRadiusCm / 100.f, RwgClaimedOriginals.Num());
    }
    RwgWpFar = 0;
    RwgWpRejectedRock = 0; // RWG P2d: per pass
}
