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
// ------------------------------------------------------------------------------------------------
// ns-review-h5 F2 -- DROP A PLACEMENT THE ENTRY NO LONGER OWNS  (the ROOT-CAUSE half)
// ------------------------------------------------------------------------------------------------
// PlacedCoreLocation / PlacedLocation are the entry's claim on a coordinate. Nothing ever WITHDREW that
// claim: the roll's ten refusal `continue`s clear bRelocate and leave the coordinates behind, and the
// escalation ladder rewrites DestCoreLocation while leaving Placed* pointing at the destination it just
// abandoned. So IsAtTarget() -- the sweep's "this group is mid-assembly, leave it alone" test -- kept
// answering true for pinned, permanently-failed and refused entries FOREVER, and the sweep reported
// their stranded actors as "mid-assembly: OWNED, not orphaned". That is a confident falsehood produced
// by the one function in the packet whose job is to disbelieve the others.
//
// The one-line alternative (widen the predicate to `bRelocate && !bRelocationFailed`) is ALSO taken, in
// SweepOrphanedWellActors below -- not instead of this. They fail differently and neither subsumes the
// other: the predicate covers a route that never calls this, and this covers a route whose flags do not
// distinguish it (an escalating entry keeps bRelocate true and bRelocationFailed false while its old
// coordinates go stale). Same argument as pass A vs pass B one level up.
//
// A PLACED GROUP IS NEVER TOUCHED. bGroupPlaced true means the entry owns those coordinates and
// SpawnWellGroup, AdoptRestoredWellGroups, EnsureSatelliteLinked, DespawnStaleWellMembers and
// SuppressVanillaWellGroup all read them; clearing there would strand a working well instantly.
bool ANodeShuffleSubsystem::ClearAbandonedWellPlacement(FNodeShuffleWellEntry& E, const TCHAR* Why)
{
    if (E.bGroupPlaced) { return false; } // it owns them -- see above, this is the whole safety gate

    const FVector WasCore = E.PlacedCoreLocation;
    const bool bHadCore = !WasCore.IsNearlyZero();
    int32 SatsCleared = 0;
    for (FNodeShuffleWellSatellite& S : E.Satellites)
    {
        if (S.PlacedLocation.IsNearlyZero()) { continue; }
        S.PlacedLocation = FVector::ZeroVector;
        S.PlacedRotation = FRotator::ZeroRotator;
        ++SatsCleared;
    }
    if (!bHadCore && SatsCleared == 0) { return false; }

    E.PlacedCoreLocation = FVector::ZeroVector;
    E.PlacedCoreRotation = FRotator::ZeroRotator;

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-ABANDON core='%s' (%s): dropped this entry's claim on its committed placement -- ")
        TEXT("core was %s, %d satellite coordinate(s) cleared (relocate=%d failed=%d placed=%d). Until ")
        TEXT("this existed the stale claim made the orphan sweep report any actor still standing there ")
        TEXT("as 'mid-assembly: OWNED, not orphaned' forever (ns-review-h5 F2). Those actors are now ")
        TEXT("visible to the sweep as what they are."),
        *WellShort(E.CorePath), Why, *WasCore.ToCompactString(), SatsCleared,
        E.bRelocate ? 1 : 0, E.bRelocationFailed ? 1 : 0, E.bGroupPlaced ? 1 : 0);
    return true;
}

void ANodeShuffleSubsystem::SweepOrphanedWellActors(const TCHAR* Phase, bool bRelocationOn)
{
    // ================================================================================================
    // ns-review-h5 F1 (BLOCKING) -- THE TWO GATES, BEFORE ANY STATE IS READ AND LONG BEFORE ANY DESTROY
    // ================================================================================================
    // GATE 1: the feature must be ON for this pass. ApplyWellRelocation's `!bOn` block does not return
    // (already-relocated wells must keep being maintained), so this used to run with both toggles off.
    // GATE 2: at least ONE group must actually be placed. With an empty or all-vanilla WellLayout the
    // "accounted for" set is empty, so a world-driven scan classifies EVERY runtime fracking actor as
    // unaccounted -- the entire vanilla well population, in a save where the player never enabled the
    // feature. Two independent gates on purpose: gate 1 is a config read and gate 2 is a fact about the
    // save, and the failure that parked this packet needed only ONE of them to be missing.
    int32 PlacedGroups = 0;
    for (const FNodeShuffleWellEntry& E : WellLayout) { if (E.bGroupPlaced) { ++PlacedGroups; } }

    if (!bRelocationOn || PlacedGroups <= 0)
    {
        // Said once per session, not per pass: this is a steady state, and a ~60 s drip is the shape
        // ns-review-h5 F-4/F7 already fixed twice in this packet.
        if (!bWellSweepGatedLogged)
        {
            bWellSweepGatedLogged = true;
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH2-ORPHAN [%s]: SWEEP SKIPPED and NOTHING was examined or destroyed -- ")
                TEXT("relocationOn=%d, placedGroups=%d (layout entries %d, core handles %d, satellite ")
                TEXT("handles %d). BOTH gates must hold before this sweep may look at the world. ")
                TEXT("ns-review-h5 F1: ungated, with an empty layout it destroyed every runtime fracking ")
                TEXT("actor in the world at apply pass 8 of every session, feature off. Said once."),
                Phase, bRelocationOn ? 1 : 0, PlacedGroups, WellLayout.Num(),
                SpawnedWellCores.Num(), SpawnedWellSatellites.Num());
        }
        return;
    }

    const float MatchSq = FMath::Square(WellAdoptMatchRadiusCm);
    // `!Target.IsNearlyZero()` is NEW and it is load-bearing now that F2 clears a withdrawn claim TO
    // ZeroVector. ZeroVector means "this entry names no coordinate" everywhere else in the packet (pass
    // B already filters on exactly this, and the spawn guard's `LocalOffset.IsNearlyZero() &&
    // PlacedLocation.IsNearlyZero()` term is the same convention) -- but this lambda tested only
    // finiteness, and (0,0,0) IS finite. Without the check, every cleared claim would start protecting
    // any actor within 300 cm of the WORLD ORIGIN, which is precisely where h3 H10's uncaptured records
    // would have materialised. It only ever makes ownership STRICTER, which is the safe direction here:
    // a false "owned" hides an orphan, a false "orphan" is caught by the occupancy gate and the cap.
    const auto IsAtTarget = [&](const AActor* A, const FVector& Target) -> bool
    {
        return IsValid(A) && IsFiniteVector(Target) && !Target.IsNearlyZero()
            && FVector::DistSquared(A->GetActorLocation(), Target) < MatchSq;
    };

    // ---- ns-review-h5 F2, ROOT CAUSE: reconcile abandoned placements BEFORE reading them as ownership.
    // State-driven, so it covers all ten of the roll's refusal `continue`s and any route added later,
    // rather than being a clear someone must remember to call -- the same argument this file makes for
    // existing at all. An entry that is still genuinely searching (bRelocate, not failed, not placed)
    // DOES own its committed coordinate and is skipped: that is the F-2 mid-assembly protection, kept.
    int32 Reconciled = 0;
    for (FNodeShuffleWellEntry& E : WellLayout)
    {
        if (E.bGroupPlaced) { continue; }
        if (E.bRelocate && !E.bRelocationFailed) { continue; } // mid-search: the claim is live
        if (ClearAbandonedWellPlacement(E, TEXT("state reconciliation at the orphan sweep"))) { ++Reconciled; }
    }

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

    // ns-review-h5 F2, the ONE-LINE half, applied as a SECOND layer to the reconciliation above.
    // An entry may only claim "mid-assembly" while it is actually mid-assembly. Pinned/unmanaged
    // (bRelocate cleared by the roll) and permanently failed (bRelocationFailed) entries are abandoned
    // by definition and must never protect a handle.
    const auto EntryIsMidAssembly = [](const FNodeShuffleWellEntry* E) -> bool
    {
        return E != nullptr && !E->bGroupPlaced && E->bRelocate && !E->bRelocationFailed;
    };

    int32 Destroyed = 0, RefusedInUse = 0, AlreadyGone = 0, Retrying = 0, NoLongerProtected = 0;
    FString InUseDetail;

    // ---- PASS A: handles no entry owns ----
    TArray<FString> OrphanCoreKeys, OrphanSatKeys;
    for (const TPair<FString, AFGResourceNodeFrackingCore*>& P : SpawnedWellCores)
    {
        const FNodeShuffleWellEntry* const* Found = EntryByCorePath.Find(P.Key);
        const FNodeShuffleWellEntry* E = Found ? *Found : nullptr;
        if (E && E->bGroupPlaced) { continue; }
        if (E && IsAtTarget(P.Value, E->PlacedCoreLocation))
        {
            if (EntryIsMidAssembly(E)) { ++Retrying; continue; }  // F-2: genuinely mid-assembly
            ++NoLongerProtected;                                  // F2: it was standing there, and lying
        }
        OrphanCoreKeys.Add(P.Key);
    }
    for (const TPair<FString, AFGResourceNodeFrackingSatellite*>& P : SpawnedWellSatellites)
    {
        const auto* Found = EntryBySatPath.Find(P.Key);
        const FNodeShuffleWellEntry* E = Found ? Found->Key : nullptr;
        const FNodeShuffleWellSatellite* S = Found ? Found->Value : nullptr;
        if (E && S && E->bGroupPlaced && S->bCaptured) { continue; }
        if (E && S && IsAtTarget(P.Value, S->PlacedLocation))
        {
            if (EntryIsMidAssembly(E)) { ++Retrying; continue; } // F-2
            ++NoLongerProtected;                                 // F2
        }
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

    // ns-review-h5 F1 -- THE DESTROY CAP. Pass A only ever destroys actors whose handles WE created, so
    // it stays destructive; but "provably ours" is a property of the ownership index, and an index that
    // is wrong should cost one group, not a save. Deferring is free here: the handle stays in the map,
    // so the very next sweep retries it -- unlike pass B, where a deferral means the actor is unseen for
    // another cadence.
    int32 CapDeferred = 0;
    const auto CapReached = [&]() -> bool { return Destroyed >= WellSweepMaxDestroysPerPass; };

    for (const FString& Key : OrphanCoreKeys)
    {
        if (CapReached()) { ++CapDeferred; continue; }
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
        if (CapReached()) { ++CapDeferred; continue; }
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

    if (CapDeferred > 0 && !bWellSweepCapLogged)
    {
        bWellSweepCapLogged = true;
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-ORPHAN [%s]: *** DESTROY CAP REACHED *** -- %d handle(s) destroyed this pass ")
            TEXT("(cap %d) and %d deferred to the next sweep. One maximal well is 11 members, so hitting ")
            TEXT("this means either a genuine mass re-enrolment or a WRONG OWNERSHIP INDEX. The handles ")
            TEXT("stay in the maps and are retried, so nothing is lost either way. Said once."),
            Phase, Destroyed, WellSweepMaxDestroysPerPass, CapDeferred);
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
    //
    // ================================================================================================
    // ns-review-h5 F1 -- PASS B IS LOG-ONLY. WIP 2026-08-07, DELIBERATELY PARKED. DO NOT ARM THIS
    // WITHOUT READING THE NEXT PARAGRAPH.
    // ================================================================================================
    // It used to call DestroyWellMemberIfUnused on every actor it could not account for. Ungated, that
    // deleted the world's vanilla wells (see the gates at the top of this function). Gated, it is still
    // the wrong risk to take at THIS stage, for a reason the gates cannot fix: the ONLY thing separating
    // a vanilla level actor from an abandoned runtime one here is `IsNetStartupActor()`, an
    // ENGINE-INTERNAL boolean evaluated during sublevel AddToWorld. We cannot verify from headers what
    // it returns mid-stream for a partially-added sublevel -- and this workspace has had THREE reasoned
    // predictions about engine internals falsified by measurement in one week. One false negative
    // there deletes a resource well permanently, and there is no undo in a player's save.
    //
    // So the backstop now REPORTS and destroys nothing. That costs the h5 F-1 reclamation (an abandoned
    // actor holding no handle stays in the world) and buys back the whole node-destroyer risk class. The
    // report is what makes the trade honest: if the state is real, it will be in the log and we will
    // know how often, with what counts, before anything is armed.
    //
    // WHAT WOULD ARM IT: the SaveGame identity component (a UNodeShuffleWellComponent carrying a group
    // id), which answers "is this actor ours" DIRECTLY instead of geometrically and retires this entire
    // question. Until that exists, arming this is trading a certain, silent, unrecoverable failure mode
    // for an uncertain benefit. A destroy cap and the layout-path cross-check below are both already in
    // place for whoever does arm it.
    // TODO(2026-08-07, H2b): re-evaluate ONLY after the SaveGame identity component lands.
    //
    // THE PATH CROSS-CHECK (ns-review-h5 F1, "cross-check paths against the layout"): a runtime actor
    // whose object path is a path the LAYOUT knows -- a vanilla core or satellite we enrolled or
    // considered -- is by definition not one of ours, whatever IsNetStartupActor says. That is a second,
    // NON-engine-internal gate on the same question, and it is the one that would have to fail before
    // an armed backstop could touch a level actor.
    int32 BackstopUnaccounted = 0, BackstopSeen = 0, BackstopKnownPath = 0, BackstopInUse = 0;
    FString BackstopDetail;
    if (FCString::Strcmp(Phase, TEXT("settled")) == 0)
    {
        TSet<FString> KnownLayoutPaths;
        TArray<FVector> AccountedFor;
        for (const FNodeShuffleWellEntry& E : WellLayout)
        {
            KnownLayoutPaths.Add(E.CorePath);
            for (const FNodeShuffleWellSatellite& S : E.Satellites) { KnownLayoutPaths.Add(S.SatellitePath); }
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

        // ONE examination routine for both actor classes, so the two cannot drift apart in which gates
        // they apply -- the two-copies-of-a-rule shape this packet has already been bitten by twice
        // (ns-review-h2 F3, h3's two health rules, h4 F4's two teardowns).
        const auto Examine = [&](AFGResourceNodeBase* A) -> void
        {
            if (!IsValid(A)) { return; }
            const bool bLevelActor = A->IsNetStartupActor();
            const FString Path = WellPathOf(A);
            const bool bKnownPath = KnownLayoutPaths.Contains(Path);
            if (bKnownPath) { ++BackstopKnownPath; }
            // GATE 1 (engine-internal, ASSUMED not proven): a level actor is never ours.
            // GATE 2 (ours, provable): a path the layout knows is a vanilla member, never ours.
            if (bLevelActor || bKnownPath) { return; }
            ++BackstopSeen;
            if (Held.Contains(A) || IsAccountedFor(A)) { return; }

            ++BackstopUnaccounted;
            const TCHAR* Why = TEXT("");
            const bool bInUse = IsWellMemberInUse(A, Why);
            if (bInUse) { ++BackstopInUse; }
            if (BackstopUnaccounted <= 5) // name the first few; the total is in the summary line
            {
                BackstopDetail += FString::Printf(TEXT(" '%s'@%s%s%s;"), *A->GetName(),
                    *A->GetActorLocation().ToCompactString(),
                    bInUse ? TEXT(" IN-USE:") : TEXT(""), bInUse ? Why : TEXT(""));
            }
        };

        for (TActorIterator<AFGResourceNodeFrackingCore> It(GetWorld()); It; ++It) { Examine(*It); }
        for (TActorIterator<AFGResourceNodeFrackingSatellite> It(GetWorld()); It; ++It) { Examine(*It); }

        if (BackstopUnaccounted > 0)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-ORPHAN [backstop LOG-ONLY]: %d RUNTIME fracking actor(s) are in no handle map ")
                TEXT("and no layout entry accounts for their position (%d in use by a player). NOTHING WAS ")
                TEXT("DESTROYED -- this backstop reports only (WIP 2026-08-07, ns-review-h5 F1: the ")
                TEXT("vanilla/ours discrimination rests on IsNetStartupActor, an engine-internal boolean ")
                TEXT("we cannot verify statically during sublevel AddToWorld, and a false negative there ")
                TEXT("deletes a player's well with no undo). Examined %d runtime actor(s); %d were ")
                TEXT("excluded by the layout-path cross-check. First few:%s"),
                BackstopUnaccounted, BackstopInUse, BackstopSeen, BackstopKnownPath, *BackstopDetail);
        }
        else if (!bWellBackstopLogOnlyLogged)
        {
            bWellBackstopLogOnlyLogged = true;
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH2-ORPHAN [backstop LOG-ONLY]: clean -- %d runtime fracking actor(s) examined, ")
                TEXT("all accounted for; %d excluded by the layout-path cross-check. This backstop never ")
                TEXT("destroys anything in this build. Said once."),
                BackstopSeen, BackstopKnownPath);
        }
    }

    if (HandleOrphans == 0 && AlreadyGone == 0 && Reconciled == 0 && NoLongerProtected == 0
        && BackstopSeen == 0)
    {
        return;
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-ORPHAN [%s]: gates PASSED (relocationOn=1, placedGroups=%d). Reconciled %d ")
        TEXT("abandoned placement claim(s) before reading ownership; %d handle(s) were standing at a ")
        TEXT("claimed coordinate but their entry is NOT mid-assembly (pinned/failed/refused) so the ")
        TEXT("claim no longer protects them -- ns-review-h5 F2. Handles -- %d unowned (destroyed %d of ")
        TEXT("a %d cap, %d deferred by the cap, refused %d in use, %d already gone), %d genuinely ")
        TEXT("retrying (mid-assembly at its committed coordinate: OWNED, not orphaned). Backstop -- ")
        TEXT("%d runtime fracking actor(s) examined, %d unaccounted, 0 destroyed (LOG-ONLY).%s%s"),
        Phase, PlacedGroups, Reconciled, NoLongerProtected,
        HandleOrphans, Destroyed, WellSweepMaxDestroysPerPass, CapDeferred, RefusedInUse, AlreadyGone,
        Retrying, BackstopSeen, BackstopUnaccounted,
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
