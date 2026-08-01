// Packet H2 (ns-wells-h2, branch feature/extractor-automatch): THE mCORE LIFECYCLE -- the
// link/registration funnel, the lazy-adoption helpers, and the cross-session re-match. This is design
// R1, the packet's highest-risk item. Its two nearest neighbours were split out for the 500-line limit
// and are named where they matter: NodeShuffleWellSpawn.cpp materialises a group (and is where mCore is
// written, between SpawnActorDeferred and FinishSpawning), NodeShuffleWellAudit.cpp is the acceptance
// gate that makes a missed link LOUD.
//
// ================================ WHY THIS FILE EXISTS ================================
// AFGResourceNodeFrackingSatellite::mCore is a PRIVATE UPROPERTY(EditInstanceOnly) pointer. It is
// LEVEL-AUTHORED INSTANCE DATA:
//   * it is NOT SaveGame -- so it does not come back on load;
//   * it is NOT replicated -- so a client never receives it;
//   * the reverse array AFGResourceNodeFrackingCore::mSatellites is a plain runtime TArray with NO
//     DEDUP, built by RegisterSatellite, which the satellite's own BeginPlay calls FROM mCore.
// Put those together and a relocated well has two ways to die, both of them SILENT:
//   (1) we spawn a satellite without setting mCore -> BeginPlay registers nothing;
//   (2) the save restores our satellite -> BeginPlay runs with mCore null -> registers nothing.
// In both cases there is no crash, no error, no warning anywhere. The Resource Well Pressurizer simply
// reports zero satellites and the well produces nothing. That is the exact shape of this workspace's
// worst historical bugs, so every path here is instrumented and the audit prints the numbers even when
// they are correct -- a well that is FINE must be visibly fine, or a well that is not will not stand out.
//
// ================================ THE TWO PATHS, AND ONE FUNNEL ================================
// FIRST SPAWN: SpawnActorDeferred -> write mCore -> FinishSpawning. BeginPlay then registers the
//   satellite itself. Design Q1 point 3 says: do NOT also call RegisterSatellite, because mSatellites
//   has no dedup and a second registration would inflate GetSatelliteNodeCount() and misreport the
//   well's rate.
// RELOAD: BeginPlay has ALREADY run, with mCore null. Nothing is registered and nothing ever will be.
//   Here we MUST write mCore and MUST register explicitly.
//
// So there is exactly one funnel, EnsureSatelliteLinked, and it registers ONLY IF the core's
// mSatellites does not already contain this satellite. That Contains() guard is what makes the funnel
// safe on both paths, and it does something better than obeying design Q1 point 3 -- it MEASURES it.
// On the spawn path the guard finds the satellite already present and logs "registered by BeginPlay",
// which is the design's assumption turning into evidence. If BeginPlay ever stops registering (a game
// patch, a mod hooking it), the same funnel repairs it and says so instead of shipping a dead well.
// This is a deliberate, guarded deviation from "never call RegisterSatellite": the reason that rule
// was given -- double counting -- is removed by the guard, and the reload path cannot work without it.
//
// ================================ IDENTITY ACROSS A RELOAD ================================
// Our spawned core and satellites are STOCK BP_FrackingCore_C / BP_FrackingSatellite_C actors. There is
// nowhere on them to stash a guid (no subclass of ours, and adding SaveGame fields to a game class is
// not available to us). So they are re-matched exactly as AdoptRestoredSpawnedNodes re-matches a
// real-class relocated node: by LOCATION, against the layout's stored Placed* coordinates, restricted
// to RUNTIME actors (!IsNetStartupActor -- a level-placed vanilla core can never be one of ours).
//
// IMPORT DISCIPLINE (memory: sf-shipping-export-trap; design §M1 dumpbin-verified RegisterSatellite,
// Native_GetSatellites, GetCore and the StaticClass thunks present in the shipping export table). That
// is the PREDICTION, not the claim -- the table is MEASURED after the build and every new symbol named.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleWellCensus.h"   // AFGResourceNodeFrackingCore / ...Satellite
#include "NodeShuffleWellRetype.h"   // WellPathOf / WellShort
#include "NodeShuffleWellRelocate.h" // IsFiniteVector

#include "EngineUtils.h"
#include "Resources/FGResourceDescriptor.h"

// WellAdoptMatchRadiusCm moved to NodeShuffleWellRelocate.h (ns-review-h3 H1): it is now used by four
// files -- adoption here, the stale-handle guard in the spawn, and the SCATTERED verdict in the audit --
// and a tolerance that decides "is this the same actor" must have exactly one value, not four copies.

// ------------------------------------------------------------------------------------------------
// THE LINK FUNNEL
// ------------------------------------------------------------------------------------------------
void ANodeShuffleSubsystem::EnsureSatelliteLinked(AFGResourceNodeFrackingCore* Core,
                                                  AFGResourceNodeFrackingSatellite* Sat,
                                                  const TCHAR* Phase, FWellLinkOutcome& Out)
{
    Out = FWellLinkOutcome();
    // ns-review-h2 F11: bRan stays FALSE on this path, and the caller must bucket it separately. The
    // previous code let the caller's else-branch count an early return as "already registered by
    // BeginPlay", which turned "we never looked" into positive evidence for the one assumption this
    // whole packet rests on.
    if (!IsValid(Core) || !IsValid(Sat)) { return; }
    Out.bRan = true;

    // 1. THE AUTHORED LINK. Friend-granted write of the private mCore -- the one new AccessTransformers
    //    grant this packet adds. GetCore() returns a TWeakObjectPtr BY VALUE, so it is copied, not
    //    bound; the comparison is on the resolved pointer.
    const AFGResourceNodeFrackingCore* Existing = Sat->GetCore().Get();
    Out.bCoreWasAlreadySet = (Existing == Core);
    if (Existing != Core)
    {
        Sat->mCore = Core;
        Out.bCoreWritten = true;
    }

    // 2. THE REGISTRATION. Count occurrences BEFORE acting, so the log can distinguish "already there
    //    once" (the healthy spawn path) from "already there twice" (the R2 double-count hazard, which
    //    would inflate the well's reported rate). Stale weak entries are counted too: mSatellites is a
    //    TArray of TWeakObjectPtr and nothing prunes it, so a dead entry is normal rather than alarming
    //    -- but it is worth being able to see.
    TArray<TWeakObjectPtr<AFGResourceNodeFrackingSatellite>>& Sats = Core->Native_GetSatellites();
    for (const TWeakObjectPtr<AFGResourceNodeFrackingSatellite>& W : Sats)
    {
        if (!W.IsValid()) { ++Out.StaleWeakEntries; continue; }
        if (W.Get() == Sat) { ++Out.RegistrationsBefore; }
    }
    Out.bWasAlreadyRegistered = (Out.RegistrationsBefore > 0);

    if (!Out.bWasAlreadyRegistered)
    {
        // Only reachable when BeginPlay did NOT register this satellite -- i.e. every reload, and any
        // future world where BeginPlay stops doing it. THE GUARD IS WHAT MAKES THIS SAFE: with
        // mSatellites having no dedup, an unguarded call here would double-count the well's rate.
        Core->RegisterSatellite(Sat);
        Out.bRegisteredNow = true;
    }
    Out.ArraySizeAfter = Sats.Num();

    if (Out.RegistrationsBefore > 1)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-LINK [%s] core='%s' sat='%s': DOUBLE REGISTRATION -- this satellite appears %d ")
            TEXT("times in the core's mSatellites. The well's satellite count and therefore its extraction ")
            TEXT("rate are INFLATED. mSatellites has no dedup and nothing here removes entries, so this ")
            TEXT("came from somewhere else registering it as well."),
            Phase, *Core->GetName(), *Sat->GetName(), Out.RegistrationsBefore);
    }
    else if (FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("WELLH2-LINK [%s] core='%s' sat='%s': mCore %s, registration %s (array now %d, %d stale weak)."),
            Phase, *Core->GetName(), *Sat->GetName(),
            Out.bCoreWritten ? TEXT("WRITTEN") : TEXT("already correct"),
            Out.bRegisteredNow ? TEXT("ADDED BY US (BeginPlay had not registered -- expected on a reload)")
                               : TEXT("already present (registered by BeginPlay -- the design's assumption, measured)"),
            Out.ArraySizeAfter, Out.StaleWeakEntries);
    }
}

// ------------------------------------------------------------------------------------------------
// LAZY ADOPTION HELPERS  (ns-review-h2 fix B / F5)
// ------------------------------------------------------------------------------------------------
// AdoptRestoredWellGroups is single-shot at first apply. A group whose actors had not streamed in by
// then was never adopted, and SpawnWellGroup would then spawn a DUPLICATE inside the existing actor --
// two cores at one coordinate, two satellites per socket, a doubled rate, and an audit that saw only
// the newest one. Sweeping for an existing runtime actor immediately before each spawn makes adoption
// lazy and closes the hole at its root rather than widening the single-shot window.
//
// Both helpers refuse level actors (IsNetStartupActor -- a vanilla well is never ours) and refuse any
// actor another entry has already claimed, so two entries dealt near each other cannot steal each
// other's members.
AFGResourceNodeFrackingCore* ANodeShuffleSubsystem::FindExistingRuntimeWellCoreAt(const FVector& At)
{
    UWorld* World = GetWorld();
    if (!World || !IsFiniteVector(At)) { return nullptr; }
    const float MatchSq = FMath::Square(WellAdoptMatchRadiusCm);
    for (TActorIterator<AFGResourceNodeFrackingCore> It(World); It; ++It)
    {
        AFGResourceNodeFrackingCore* Core = *It;
        if (!IsValid(Core) || Core->IsNetStartupActor()) { continue; }
        if (FVector::DistSquared(Core->GetActorLocation(), At) >= MatchSq) { continue; }
        bool bClaimed = false;
        for (const TPair<FString, AFGResourceNodeFrackingCore*>& P : SpawnedWellCores)
        {
            if (P.Value == Core) { bClaimed = true; break; }
        }
        if (bClaimed) { continue; }
        return Core;
    }
    return nullptr;
}

AFGResourceNodeFrackingSatellite* ANodeShuffleSubsystem::FindExistingRuntimeWellSatelliteAt(const FVector& At)
{
    UWorld* World = GetWorld();
    if (!World || !IsFiniteVector(At)) { return nullptr; }
    const float MatchSq = FMath::Square(WellAdoptMatchRadiusCm);
    for (TActorIterator<AFGResourceNodeFrackingSatellite> It(World); It; ++It)
    {
        AFGResourceNodeFrackingSatellite* Sat = *It;
        if (!IsValid(Sat) || Sat->IsNetStartupActor()) { continue; }
        if (FVector::DistSquared(Sat->GetActorLocation(), At) >= MatchSq) { continue; }
        bool bClaimed = false;
        for (const TPair<FString, AFGResourceNodeFrackingSatellite*>& P : SpawnedWellSatellites)
        {
            if (P.Value == Sat) { bClaimed = true; break; }
        }
        if (bClaimed) { continue; }
        return Sat;
    }
    return nullptr;
}

// ns-review-h2 F2/F7: how many satellites this group is ACTUALLY expected to have relocated. Records
// appended after the group was placed carry no rigid-body capture, are never spawned, and must not be
// counted as missing -- otherwise a correct group reports as broken forever and the acceptance gate
// becomes noise. They ARE reported separately at the spawn and in the audit line.
int32 ANodeShuffleSubsystem::ExpectedRelocatedSatelliteCount(const FNodeShuffleWellEntry& E)
{
    int32 N = 0;
    for (const FNodeShuffleWellSatellite& S : E.Satellites) { if (S.bCaptured) { ++N; } }
    return N;
}

// ------------------------------------------------------------------------------------------------
// CROSS-SESSION RE-MATCH -- THE FUNCTION THAT STOPS A RELOCATED WELL DYING SILENTLY ON RELOAD
// ------------------------------------------------------------------------------------------------
void ANodeShuffleSubsystem::AdoptRestoredWellGroups()
{
    UWorld* World = GetWorld();
    if (!World) { return; }

    int32 ExpectedGroups = 0, ExpectedSats = 0;
    for (const FNodeShuffleWellEntry& E : WellLayout)
    {
        if (!E.bGroupPlaced) { continue; }
        ++ExpectedGroups;
        // ns-review-h3 H7: count only CAPTURED records. An uncaptured one was never spawned by design,
        // so counting it here made a perfectly healthy group report "N satellite(s) not streamed" on the
        // load-time line forever. SpawnWellGroup and the audit already filter on bCaptured; this was the
        // third view of membership and it disagreed with the other two.
        ExpectedSats += ExpectedRelocatedSatelliteCount(E);
    }
    if (ExpectedGroups == 0) { return; }

    const float MatchSq = FMath::Square(WellAdoptMatchRadiusCm);
    int32 CoresAdopted = 0, SatsAdopted = 0, RuntimeCoresSeen = 0, RuntimeSatsSeen = 0;

    // ---- CORES ----
    for (TActorIterator<AFGResourceNodeFrackingCore> It(World); It; ++It)
    {
        AFGResourceNodeFrackingCore* Core = *It;
        if (!IsValid(Core)) { continue; }
        if (Core->IsNetStartupActor()) { continue; } // level-placed vanilla core -- never ours
        ++RuntimeCoresSeen;
        const FVector Loc = Core->GetActorLocation();
        for (FNodeShuffleWellEntry& E : WellLayout)
        {
            if (!E.bGroupPlaced) { continue; }
            if (SpawnedWellCores.Contains(E.CorePath)) { continue; }
            if (FVector::DistSquared(Loc, E.PlacedCoreLocation) >= MatchSq) { continue; }
            SpawnedWellCores.Add(E.CorePath, Core);
            FNodeShuffleModule::RegisterManagedNode(Core);
            ++CoresAdopted;
            break;
        }
    }

    // ---- SATELLITES, then the LINK REPAIR ----
    for (TActorIterator<AFGResourceNodeFrackingSatellite> It(World); It; ++It)
    {
        AFGResourceNodeFrackingSatellite* Sat = *It;
        if (!IsValid(Sat)) { continue; }
        if (Sat->IsNetStartupActor()) { continue; }
        ++RuntimeSatsSeen;
        const FVector Loc = Sat->GetActorLocation();
        bool bMatched = false;
        for (FNodeShuffleWellEntry& E : WellLayout)
        {
            if (!E.bGroupPlaced) { continue; }
            for (FNodeShuffleWellSatellite& S : E.Satellites)
            {
                if (!S.bCaptured) { continue; } // h3 H7: never spawned, so never adoptable
                if (SpawnedWellSatellites.Contains(S.SatellitePath)) { continue; }
                if (FVector::DistSquared(Loc, S.PlacedLocation) >= MatchSq) { continue; }
                SpawnedWellSatellites.Add(S.SatellitePath, Sat);
                FNodeShuffleModule::RegisterManagedNode(Sat);
                S.bPlaced = true; // ns-review-h4 F8: third handle-establishing path -- see the note at
                                  // the late-adopt site in NodeShuffleWellSpawn.cpp.
                ++SatsAdopted;
                bMatched = true;
                break;
            }
            if (bMatched) { break; }
        }
    }

    // Now every adopted satellite gets its link back. THIS is the repair that BeginPlay could not do:
    // it already ran, with mCore null, and registered nothing.
    int32 LinksRepaired = 0, LinksAlreadyGood = 0, CoresMissing = 0, SatsMissing = 0;
    for (FNodeShuffleWellEntry& E : WellLayout)
    {
        if (!E.bGroupPlaced) { continue; }
        AFGResourceNodeFrackingCore* Core = SpawnedWellCores.FindRef(E.CorePath);
        if (!IsValid(Core)) { ++CoresMissing; continue; }
        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            if (!S.bCaptured) { continue; } // h3 H7: not missing -- never spawned, by design
            AFGResourceNodeFrackingSatellite* Sat = SpawnedWellSatellites.FindRef(S.SatellitePath);
            if (!IsValid(Sat)) { ++SatsMissing; continue; }
            FWellLinkOutcome Link;
            EnsureSatelliteLinked(Core, Sat, TEXT("adopt"), Link);
            if (Link.bRegisteredNow || Link.bCoreWritten) { ++LinksRepaired; } else { ++LinksAlreadyGood; }
            RegisterNodeWithManager(Sat);
        }
    }

    // ns-review-h3 H2 -- THIS FUNCTION REPORTS COUNTS AND REACHES NO VERDICT, DELIBERATELY.
    //
    // Two rounds of review have now caught a verdict in the wrong place here. The original alarm
    // required RuntimeCoresSeen > 0, so it could never fire in the case its own text described ("our
    // spawned wells may not be save-collected" produces exactly ZERO runtime cores). The F6 fix
    // replaced it with `CoresAdopted < ExpectedGroups` -- which is the right QUESTION but asked at the
    // wrong TIME: this runs once, ~5 s after load, when almost nothing has streamed, so it fired on
    // essentially every load. Same defect, opposite polarity.
    //
    // The reason both attempts were wrong is that the question is not answerable HERE. "Did we get
    // back what we placed" needs a settled world, and the only place that exists is the audit -- which
    // runs on the settled pass and again on a slow cadence, and now owns the verdict (see the NoteCore
    // block at the end of AuditWellGroupLinks). So this line states what it saw and stops.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-ADOPT (load-time snapshot, NOT a verdict -- almost nothing has streamed this early; ")
        TEXT("the settled/cadence WELLH2-AUDIT owns the pass/fail call): %d/%d relocated well core(s) and ")
        TEXT("%d/%d captured satellite(s) re-matched by location (%d runtime cores / %d runtime satellites ")
        TEXT("seen). mCore links: %d REPAIRED, %d already good, %d group(s) with no core streamed yet, %d ")
        TEXT("satellite(s) not streamed yet. mCore is EditInstanceOnly -- not SaveGame, not replicated -- ")
        TEXT("so EVERY relocated satellite arrives unlinked and BeginPlay has already run; repairing them ")
        TEXT("here (and lazily, every pass, as more stream in) is the only thing standing between a ")
        TEXT("relocated well and a silent death."),
        CoresAdopted, ExpectedGroups, SatsAdopted, ExpectedSats, RuntimeCoresSeen, RuntimeSatsSeen,
        LinksRepaired, LinksAlreadyGood, CoresMissing, SatsMissing);
}

