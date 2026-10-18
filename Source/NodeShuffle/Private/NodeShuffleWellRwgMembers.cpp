// RWG P4c (2026-10-18) -- a reloaded RWG well's satellites come from its CLAIM, not from the engine's core<->satellite links.
//
// MEASURED (FactoryGame.log, rwg-p5-2, reload then Re-roll at 22:20:01 UTC): the re-roll census grouped 83 satellites under
// 90 cores with orphanSats=539 (every original RWG satellite had a null GetCore() and no core listed it), so every one of the
// 78 claimed wells reached RollWellRelocation's capture gate with 0 live members and was refused "0 satellite(s) live vs N in
// the layout" (78 lines) -> WELLROLL eligible=0 dealt=0. The claim registry had all 539 satellite paths bound by
// WELLCLAIM-RETRY pass 3 (satsStillUnbound=0) before the re-roll. The authored resource was NOT the blocker: the same re-roll's
// WELLH1-ROLL summary reads 78 managed + 0 with no authored resource (the SaveGame field carried it).
//
// So, OFF VANILLA ONLY, a census record whose member count is below its claimed entry's satellite count is topped up from
// the entry's BOUND satellite paths (resolved against the live satellite actors by path). Exclusions mirror the retry's
// (NodeShuffleWellRwg.cpp RwgRetryUnboundWellSatellites): never a sentinel path, never our own spawned copy (managed
// registry or SpawnedWellSatellites), never a satellite whose GetCore() names ANOTHER core (p2c2 rereview F-B2). A census
// record that already has every member is untouched, so a fresh RWG world (census linked, 539 of 539) runs as before.
//
// RWG P4c review (2026-10-18): a short well with an UNCLAIMED live satellite (not ours, not bound to any claim) inside
// WellSatMaxRadiusCm of its core is left short (F2), so the capture gate refuses it as its count check would on vanilla.
// Also here: the WELLROLL authoredFrom* accounting (live core read / saved field); a non-empty bound satellite read on an
// entry with neither is COUNTED (noAuthoredSatelliteReadNonEmpty), never written (F4).
// Log lines report measured counts and the predicate that branched, never a cause.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"
#include "NodeShuffleWellCensus.h"   // FNodeShuffleWellCensus / Record / Member, the fracking core/satellite types
#include "NodeShuffleWellRetype.h"   // WellPathOf, WellAuthoredResourcePath, IsFiniteVector (via NodeShuffleWellRelocate.h)
#include "EngineUtils.h"
#include "Engine/World.h"

void ANodeShuffleSubsystem::RwgFillWellMembersFromClaims(FNodeShuffleWellCensus& Census, const TCHAR* Caller)
{
    if (GetMapMode() == ENodeShuffleMapMode::Vanilla) { return; }
    UWorld* World = GetWorld();
    if (!World) { return; }
    const FString Sentinel(TEXT("RWGUNBOUND_"));

    // Claimed entries whose CORE is bound this session, by core path.
    TMap<FString, const FNodeShuffleWellEntry*> ByCore;
    for (const FNodeShuffleWellEntry& E : WellLayout)
    {
        if (E.RwgClaimGuid.IsValid() && !E.CorePath.StartsWith(Sentinel)) { ByCore.Add(E.CorePath, &E); }
    }

    int32 WellsShort = 0, WellsFilled = 0, SatsAdded = 0, SatsAlready = 0, SatsSentinel = 0, SatsNoLive = 0;
    int32 SatsOurs = 0, SatsOtherCore = 0, SatsNonFinite = 0;
    int32 SatsPurityDiffers = 0;
    int32 WellsUnclaimedNearby = 0, SatsUnclaimedNearby = 0;
    TMap<FString, AFGResourceNodeFrackingSatellite*> LiveByPath; // RWG P4c review F5: built eagerly, so liveSatActors is true on every call
    TSet<const AActor*> Ours;
    for (TActorIterator<AFGResourceNodeFrackingSatellite> It(World); It; ++It)
    {
        if (IsValid(*It)) { LiveByPath.Add(WellPathOf(*It), *It); }
    }
    for (const TPair<FString, AFGResourceNodeFrackingSatellite*>& KV : SpawnedWellSatellites) { if (KV.Value) { Ours.Add(KV.Value); } }
    // RWG P4c review F2: live satellites bound to NO claim this session and not ours -- the world-side view the capture gate's count check used to
    // compare against. A short well with one inside its radius is left short, so the gate refuses it as it would have on vanilla.
    TArray<const AFGResourceNodeFrackingSatellite*> Unclaimed;
    for (const TPair<FString, AFGResourceNodeFrackingSatellite*>& KV : LiveByPath)
    {
        if (FNodeShuffleModule::IsManagedSpawnedNode(KV.Value) || Ours.Contains(KV.Value) || RwgIsClaimedOriginal(KV.Value)) { continue; }
        Unclaimed.Add(KV.Value);
    }
    for (FNodeShuffleWellRecord& W : Census.Wells)
    {
        if (!IsValid(W.Core) || FNodeShuffleModule::IsManagedSpawnedNode(W.Core)) { continue; }
        const FNodeShuffleWellEntry* const* Found = ByCore.Find(WellPathOf(W.Core));
        if (!Found || !*Found) { continue; }
        const FNodeShuffleWellEntry& E = **Found;
        if (W.Members.Num() >= E.Satellites.Num()) { continue; } // census already complete for this claim: untouched
        ++WellsShort;
        int32 UnclaimedNear = 0;
        const FVector CoreAt = W.Core->GetActorLocation();
        for (const AFGResourceNodeFrackingSatellite* U : Unclaimed)
        {
            if (FVector::DistSquared2D(U->GetActorLocation(), CoreAt) <= FMath::Square(WellSatMaxRadiusCm)) { ++UnclaimedNear; }
        }
        if (UnclaimedNear > 0) { ++WellsUnclaimedNearby; SatsUnclaimedNearby += UnclaimedNear; continue; } // left short: the capture gate refuses it
        TSet<const AActor*> Have;
        for (const FNodeShuffleWellMember& M : W.Members) { Have.Add(M.Actor); }
        int32 AddedHere = 0;
        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            if (S.SatellitePath.StartsWith(Sentinel)) { ++SatsSentinel; continue; }
            AFGResourceNodeFrackingSatellite* Sat = LiveByPath.FindRef(S.SatellitePath);
            if (!Sat) { ++SatsNoLive; continue; }
            if (Have.Contains(Sat)) { ++SatsAlready; continue; }
            if (FNodeShuffleModule::IsManagedSpawnedNode(Sat) || Ours.Contains(Sat)) { ++SatsOurs; continue; }
            const AFGResourceNodeFrackingCore* LinkedCore = Sat->GetCore().Get();
            if (LinkedCore && LinkedCore != W.Core) { ++SatsOtherCore; continue; } // linked to another core: never this claim's
            if (S.OriginalPurity != RP_MAX && Sat->GetResourcePurity() != S.OriginalPurity) { ++SatsPurityDiffers; } // measured only; F1 keeps the record
            FNodeShuffleWellMember M;
            M.Actor = Sat;
            M.Location = Sat->GetActorLocation();
            M.bLocationFinite = IsFiniteVector(M.Location); // the census keeps a non-finite member flagged; the capture refuses it
            if (!M.bLocationFinite) { ++SatsNonFinite; }
            W.Members.Add(M);
            Have.Add(Sat);
            ++AddedHere;
        }
        SatsAdded += AddedHere;
        if (AddedHere > 0) { ++WellsFilled; }
    }
    // WELLROLL reads the LAST call of the roll (RollWellRelocation's), which is the census its capture gate reads.
    RwgWrMembersFromClaimWells = WellsFilled;
    RwgWrMembersFromClaimSats = SatsAdded;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLCLAIMMEMBERS: caller=%s claimedBoundCores=%d censusShortWells=%d wellsFilled=%d satsAdded=%d satsAlreadyMember=%d satsSentinel=%d satsNoLiveActor=%d satsOurCopy=%d satsLinkedToOtherCore=%d satsNonFinite=%d liveSatActors=%d satsLivePurityDiffersFromRecord=%d wellsUnclaimedNearby=%d satsUnclaimedNearby=%d"),
        Caller, ByCore.Num(), WellsShort, WellsFilled, SatsAdded, SatsAlready, SatsSentinel, SatsNoLive, SatsOurs, SatsOtherCore,
        SatsNonFinite, LiveByPath.Num(), SatsPurityDiffers, WellsUnclaimedNearby, SatsUnclaimedNearby);
}

void ANodeShuffleSubsystem::RwgNoteWellAuthoredSource(FNodeShuffleWellEntry& E, bool bLiveCoreRead, const TArray<FNodeShuffleWellMember>& Members)
{
    // Order: the live core read (already written by the caller) > the saved field. A bound satellite's AUTHORED resource
    // (GetResourceClassOriginal, never the effective one) is only COUNTED when both are empty -- never written (review F4):
    // its mResourceClass is the same non-SaveGame field as the core's, so a non-empty read there can only be a CDO default.
    if (bLiveCoreRead) { ++RwgWrAuthoredLive; return; }
    if (!E.OriginalResourceClassPath.IsEmpty()) { ++RwgWrAuthoredSaved; return; }
    for (const FNodeShuffleWellMember& M : Members)
    {
        if (!IsValid(M.Actor) || FNodeShuffleModule::IsManagedSpawnedNode(M.Actor)) { continue; }
        const FString FromSat = WellAuthoredResourcePath(M.Actor);
        if (!FromSat.IsEmpty()) { ++RwgWrAuthoredSatellite; break; } // RWG P4c review F4: COUNTED, never written
    }
    ++RwgWrNoAuthored; // live core read and saved field both empty: the deck excludes this entry
}
