// Packet H2 (ns-wells-h2, branch feature/extractor-automatch): THE ORPHAN SWEEP -- teardown driven by
// STATE rather than by call sites.
//
// Split out of NodeShuffleWellDespawn.cpp for the 500-line limit, and the seam is the real distinction
// in this packet's teardown story:
//   * NodeShuffleWellDespawn.cpp -- "this code path is abandoning a group, tear it down NOW". Correct,
//     but it only ever covers the routes someone remembered to instrument.
//   * this file -- "scan the state and reclaim anything nobody owns", which covers routes nobody
//     remembered, including routes that do not exist yet.
// Two cold reviews in a row found a hole in the first kind. That is the argument for the second kind.
//
// TWO PASSES, because there are two ways to lose track of an actor and only one of them is visible
// from the handle maps:
//   * PASS A (every cadence) -- HANDLE-driven. Anything in SpawnedWellCores / SpawnedWellSatellites
//     that no entry owns. Catches the six roll-time `continue`s that return before
//     RollWellRelocation's despawn (ns-review-h4 F2).
//   * PASS B (settled phase only) -- LOCATION-driven, over the actor iterators. Catches an abandoned
//     ACTOR THAT HOLDS NO HANDLE (ns-review-h5 F-1), which pass A is structurally blind to and which
//     nothing else in the packet can see either.
//
// OWNERSHIP IS NOT JUST bGroupPlaced (ns-review-h5 F-2). That flag is set only inside
// `if (SpawnWellGroup(...))`, so an INCOMPLETE spawn leaves live, correctly-positioned handles with it
// false -- and this sweep, running later in the SAME pass, would destroy what was just spawned. An
// entry therefore also owns a handle whose actor stands at the entry's CURRENTLY COMMITTED coordinate.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleWellCensus.h"   // AFGResourceNodeFrackingCore / ...Satellite
#include "NodeShuffleWellRetype.h"   // WellShort
#include "NodeShuffleWellRelocate.h" // IsFiniteVector + WellAdoptMatchRadiusCm
#include "EngineUtils.h"             // TActorIterator for the location backstop

// ------------------------------------------------------------------------------------------------
// THE ORPHAN SWEEP  (ns-review-h4 F2, BLOCKING) -- handle-set-driven, structurally immune
// ------------------------------------------------------------------------------------------------
// The despawn call sites close the three doors the previous review found. They do NOT close the door
// this review found, and they never could, because the shape of the fix was "remember to call this":
// RollWellRelocation reaches its despawn only after an entry survives SIX earlier `continue`s, every
// one of which clears bRelocate and touches neither map -- pinned, not-in-census, two non-finite
// branches, count mismatch, geometry refused. The reachable version is not exotic: an INCOMPLETE spawn
// leaves a core and some satellites live with the vanilla group unsuppressed, the player builds on the
// VANILLA core, the next re-roll finds the entry pinned, takes the first `continue`, and six of our
// actors live at the abandoned destination forever -- in the maps, and invisible to an audit that
// walks bGroupPlaced entries only.
//
// So the teardown is driven by the HANDLE SET rather than by call sites. Anything in either map that
// no entry currently owns is an orphan, whatever route produced it -- including routes that do not
// exist yet. That is the difference between a fix and a seventh thing to remember, and it is the same
// lesson as F4 and the two-ladders bug one level up: put the check where the state is, not where the
// code happens to pass.
//
// ns-review-h5 F-2 -- WHAT "OWNS" MEANS, AND WHY IT IS NOT JUST bGroupPlaced.
// The first version treated every handle of a non-placed entry as an orphan. But bGroupPlaced is set
// ONLY inside `if (SpawnWellGroup(...))`, so an INCOMPLETE spawn leaves live, correctly-positioned
// handles with the flag false -- and this sweep, running later in the SAME pass, destroyed what had
// just been spawned. With a persistent cause (StaleInUse > 0 from an occupied member, which never
// self-clears) that becomes a destroy/respawn cycle every ~60 s on live resource nodes next to a
// player. So an entry also owns a handle whose actor is AT THE ENTRY'S CURRENTLY COMMITTED COORDINATE,
// using the same IsAtTarget test DespawnStaleWellMembers already applies. That protects a group
// mid-assembly without protecting a genuinely stale handle of a distant retrying entry -- which the
// simpler "widen the predicate to !bRelocate" alternative would have left alive until a player arrived.
//
// A deliberate NON-fix: no guard at the TOP of the roll loop. It would have to run before those six
// `continue`s, which is also before the bGroupPlaced branch that legitimately KEEPS a placed group --
// so it would tear down exactly the wells the packet exists to preserve. ns-review-h5 judgement call
// (1) is the right version of that idea and is taken: the sweep is called at the roll's TAIL, after
// every entry's fate is decided, where there is no ordering hazard at all.
void ANodeShuffleSubsystem::SweepOrphanedWellActors(const TCHAR* Phase)
{
    const float MatchSq = FMath::Square(WellAdoptMatchRadiusCm);
    const auto IsAtTarget = [&](const AActor* A, const FVector& Target) -> bool
    {
        return IsValid(A) && IsFiniteVector(Target)
            && FVector::DistSquared(A->GetActorLocation(), Target) < MatchSq;
    };

    // ---- Ownership index. An entry owns a handle if the group is PLACED, or if the actor is standing
    // at the coordinate the entry has currently committed to (i.e. it is mid-assembly, not abandoned).
    TMap<FString, const FNodeShuffleWellEntry*> EntryByCorePath;
    TMap<FString, TPair<const FNodeShuffleWellEntry*, const FNodeShuffleWellSatellite*>> EntryBySatPath;
    for (const FNodeShuffleWellEntry& E : WellLayout)
    {
        EntryByCorePath.Add(E.CorePath, &E);
        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            EntryBySatPath.Add(S.SatellitePath, TPair<const FNodeShuffleWellEntry*,
                                                      const FNodeShuffleWellSatellite*>(&E, &S));
        }
    }

    int32 Destroyed = 0, RefusedInUse = 0, AlreadyGone = 0, Retrying = 0;
    FString InUseDetail;

    // ---- PASS A: handles no entry owns ----
    TArray<FString> OrphanCoreKeys, OrphanSatKeys;
    for (const TPair<FString, AFGResourceNodeFrackingCore*>& P : SpawnedWellCores)
    {
        const FNodeShuffleWellEntry* const* Found = EntryByCorePath.Find(P.Key);
        const FNodeShuffleWellEntry* E = Found ? *Found : nullptr;
        if (E && E->bGroupPlaced) { continue; }
        if (E && IsAtTarget(P.Value, E->PlacedCoreLocation)) { ++Retrying; continue; } // F-2: mid-assembly
        OrphanCoreKeys.Add(P.Key);
    }
    for (const TPair<FString, AFGResourceNodeFrackingSatellite*>& P : SpawnedWellSatellites)
    {
        const auto* Found = EntryBySatPath.Find(P.Key);
        const FNodeShuffleWellEntry* E = Found ? Found->Key : nullptr;
        const FNodeShuffleWellSatellite* S = Found ? Found->Value : nullptr;
        if (E && S && E->bGroupPlaced && S->bCaptured) { continue; }
        if (E && S && IsAtTarget(P.Value, S->PlacedLocation)) { ++Retrying; continue; } // F-2
        OrphanSatKeys.Add(P.Key);
    }

    const auto ClearSatFlag = [&](const FString& SatPath)
    {
        // ns-review-h5 F-5: bPlaced means "a relocated actor exists for this record", and the sweep
        // destroying that actor without clearing it broke the invariant F8 established -- the audit's
        // flagMismatch= cross-check would then read a stale true forever.
        for (FNodeShuffleWellEntry& E : WellLayout)
        {
            for (FNodeShuffleWellSatellite& S : E.Satellites)
            {
                if (S.SatellitePath == SatPath) { S.bPlaced = false; return; }
            }
        }
    };

    for (const FString& Key : OrphanCoreKeys)
    {
        AFGResourceNodeFrackingCore* Core = SpawnedWellCores.FindRef(Key);
        // ns-review-h5 F-4: DestroyWellMemberIfUnused returns true for a null/GC'd actor, so counting
        // that as Destroyed overstated what the sweep did. DespawnWellGroup already keeps an
        // AlreadyGone bucket; this one now does too, and the two agree.
        if (!IsValid(Core)) { SpawnedWellCores.Remove(Key); ++AlreadyGone; continue; }
        const TCHAR* Why = TEXT("");
        if (DestroyWellMemberIfUnused(Core, Why)) { SpawnedWellCores.Remove(Key); ++Destroyed; }
        else
        {
            ++RefusedInUse;
            InUseDetail += FString::Printf(TEXT(" core '%s' (%s);"), *WellShort(Key), Why);
        }
    }
    for (const FString& Key : OrphanSatKeys)
    {
        AFGResourceNodeFrackingSatellite* Sat = SpawnedWellSatellites.FindRef(Key);
        if (!IsValid(Sat)) { SpawnedWellSatellites.Remove(Key); ClearSatFlag(Key); ++AlreadyGone; continue; }
        const TCHAR* Why = TEXT("");
        if (DestroyWellMemberIfUnused(Sat, Why))
        {
            SpawnedWellSatellites.Remove(Key);
            ClearSatFlag(Key);
            ++Destroyed;
        }
        else
        {
            ++RefusedInUse;
            InUseDetail += FString::Printf(TEXT(" sat '%s' (%s);"), *WellShort(Key), Why);
        }
    }

    const int32 HandleOrphans = OrphanCoreKeys.Num() + OrphanSatKeys.Num();

    // ---- PASS B: THE LOCATION BACKSTOP (ns-review-h5 F-1, HIGH/silent) ----
    // The handle sweep is blind to an abandoned ACTOR THAT HOLDS NO HANDLE, and that state is
    // reachable and completely silent. Our well actors are deliberately NOT RF_Transient (a machine
    // built on one must survive a reload), so they are save-collected. If a group spawns INCOMPLETE
    // and the player saves and quits inside that window, then on reload AdoptRestoredWellGroups skips
    // the entry entirely -- all four of its loops gate on `if (!E.bGroupPlaced) continue;` -- so both
    // maps are empty for it. The ladder later re-commits the destination, DespawnWellGroup FindRef's
    // an empty map and destroys nothing, and the old actors remain: live, snappable, contributing
    // rate, in no map and no entry. The audit walks placed entries and cannot see them; pass A walks
    // handles and cannot see them; IsManagedSpawnedNode is session-scoped so it will not even suppress
    // re-enrolment. Nothing in the packet reports them.
    //
    // So the backstop is driven by the WORLD: iterate the actors themselves and keep only what some
    // entry can account for. SETTLED PASS ONLY -- it is a full actor iteration crossed with the layout,
    // it must not run before the world has streamed (everything would look accounted-for or not, both
    // wrongly), and the same iterators already run in AdoptRestoredWellGroups so this is no new cost
    // pattern and no new engine symbol.
    //
    // The real long-term answer is a SaveGame identity on the actor (a UNodeShuffleWellComponent
    // carrying a group id), which makes this question answerable directly instead of geometrically.
    // That is H2b's, deliberately not pulled in here.
    int32 BackstopDestroyed = 0, BackstopRefused = 0, BackstopSeen = 0;
    if (FCString::Strcmp(Phase, TEXT("settled")) == 0)
    {
        TArray<FVector> AccountedFor;
        for (const FNodeShuffleWellEntry& E : WellLayout)
        {
            if (IsFiniteVector(E.PlacedCoreLocation) && !E.PlacedCoreLocation.IsNearlyZero())
            {
                AccountedFor.Add(E.PlacedCoreLocation);
            }
            for (const FNodeShuffleWellSatellite& S : E.Satellites)
            {
                if (IsFiniteVector(S.PlacedLocation) && !S.PlacedLocation.IsNearlyZero())
                {
                    AccountedFor.Add(S.PlacedLocation);
                }
            }
        }

        TSet<const AActor*> Held;
        for (const TPair<FString, AFGResourceNodeFrackingCore*>& P : SpawnedWellCores) { Held.Add(P.Value); }
        for (const TPair<FString, AFGResourceNodeFrackingSatellite*>& P : SpawnedWellSatellites) { Held.Add(P.Value); }

        const auto IsAccountedFor = [&](const AActor* A) -> bool
        {
            const FVector Loc = A->GetActorLocation();
            for (const FVector& V : AccountedFor)
            {
                if (FVector::DistSquared(Loc, V) < MatchSq) { return true; }
            }
            return false;
        };

        TArray<AFGResourceNodeBase*> Unaccounted;
        for (TActorIterator<AFGResourceNodeFrackingCore> It(GetWorld()); It; ++It)
        {
            AFGResourceNodeFrackingCore* A = *It;
            if (!IsValid(A) || A->IsNetStartupActor()) { continue; } // level actor -- never ours
            ++BackstopSeen;
            if (Held.Contains(A) || IsAccountedFor(A)) { continue; }
            Unaccounted.Add(A);
        }
        for (TActorIterator<AFGResourceNodeFrackingSatellite> It(GetWorld()); It; ++It)
        {
            AFGResourceNodeFrackingSatellite* A = *It;
            if (!IsValid(A) || A->IsNetStartupActor()) { continue; }
            ++BackstopSeen;
            if (Held.Contains(A) || IsAccountedFor(A)) { continue; }
            Unaccounted.Add(A);
        }

        for (AFGResourceNodeBase* A : Unaccounted)
        {
            const FString Name = A->GetName();
            const FVector Where = A->GetActorLocation();
            const TCHAR* Why = TEXT("");
            if (DestroyWellMemberIfUnused(A, Why))
            {
                ++BackstopDestroyed;
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("WELLH2-ORPHAN [backstop]: destroyed RUNTIME fracking actor '%s' at %s -- it is in ")
                    TEXT("no handle map and no layout entry accounts for its position. Reachable by saving ")
                    TEXT("inside an INCOMPLETE spawn window: the actors are save-collected, the reload's ")
                    TEXT("adopt skips non-placed entries, and nothing else in this packet can see them."),
                    *Name, *Where.ToCompactString());
            }
            else
            {
                ++BackstopRefused;
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("WELLH2-ORPHAN [backstop]: RUNTIME fracking actor '%s' at %s is unaccounted for but ")
                    TEXT("IN USE (%s) -- NOT destroyed. Re-checked at the next settled pass."),
                    *Name, *Where.ToCompactString(), Why);
            }
        }
    }

    if (HandleOrphans == 0 && AlreadyGone == 0 && BackstopSeen == 0) { return; }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-ORPHAN [%s]: handles -- %d unowned (destroyed %d, refused %d in use, %d already ")
        TEXT("gone), %d retrying (a group mid-assembly at its committed coordinate: OWNED, not orphaned). ")
        TEXT("Backstop -- %d runtime fracking actor(s) examined, %d destroyed, %d refused (in use).%s%s"),
        Phase, HandleOrphans, Destroyed, RefusedInUse, AlreadyGone, Retrying,
        BackstopSeen, BackstopDestroyed, BackstopRefused,
        InUseDetail.IsEmpty() ? TEXT("") : TEXT(" In use:"), *InUseDetail);

    // ns-review-h5 F-4: throttled. "Orphaned but in use" is a state a player creates and only a player
    // clears, so an unthrottled Warning on a ~60 s cadence is a permanent drip -- the exact shape F7
    // fixed one function away.
    if (RefusedInUse > 0 && !bWellOrphanInUseLogged)
    {
        bWellOrphanInUseLogged = true;
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-ORPHAN [%s]: *** ORPHANED BUT IN USE *** -- %d actor(s) belong to no well yet a ")
            TEXT("player has built on them, so they were NOT destroyed. They stay in the handle maps so ")
            TEXT("the sweep keeps re-checking, and they are cleared the moment the machinery is removed. ")
            TEXT("Said once per roll."),
            Phase, RefusedInUse);
    }
}
