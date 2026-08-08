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
//     nothing else in the packet can see either. LIVES IN NodeShuffleWellBackstop.cpp since
//     ns-review-h2-r2 (the 500-line rule), and it is LOG-ONLY in this build.
//
// ns-review-h2-r2 F-B: THE TWO GATES BELONG TO PASS B ONLY. Pass A and the claim reconciliation run on
// every call, whatever the config says and whatever placedGroups is -- pass A's safety is a property of
// the handle maps, not of the feature toggle. Gating it disabled the h4-F2 reclamation in exactly the
// window that creates abandoned handles, while the log printed a line that read as healthy.
//
// OWNERSHIP IS NOT JUST bGroupPlaced (ns-review-h5 F-2). That flag is set only inside
// `if (SpawnWellGroup(...))`, so an INCOMPLETE spawn leaves live, correctly-positioned handles with it
// false -- and this sweep, running later in the SAME pass, would destroy what was just spawned. An
// entry therefore also owns a handle whose actor stands at the entry's CURRENTLY COMMITTED coordinate.
//
// ================================================================================================
// A3 (ns-review-h2-r3 §6) -- OWNERSHIP IS A STORED FACT, NOT A DERIVED PREDICATE.
// ================================================================================================
// THE CLAIM ITSELF NOW LIVES IN NodeShuffleWellClaim.cpp (ns-review-h2-r4 §4C + the 500-line rule):
// ClearAbandonedWellPlacement (the ONE clearer), ValidateWellClaimInvariant (the enforcement),
// MigratePreA3PlacementClaimsOnce (the labelled, version-gated pre-A3 migration) and
// ReconcileAbandonedWellClaims (the ONE remaining derived predicate, which WRITES the fact). That
// seam is the distinction A3 introduced: the CLAIM vs THE SWEEP THAT READS IT -- and round 8's
// amendment is why the reconciliation went with them rather than staying here among the readers.
// THIS FILE IS NOW READERS ONLY. Do not add a claim writer to it.
//
// WHAT A3 DOES NOT DO, stated here so nobody expects it to: it does not say anything about actor
// IDENTITY ("is this actor ours"), which is what pass B, IsNetStartupActor and the deferred SaveGame
// identity component are about.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleConfig.h"       // FinishWellRollTeardown's post-roll sweep gate reads ShuffleResourceWells
#include "NodeShuffleWellCensus.h"   // AFGResourceNodeFrackingCore / ...Satellite
#include "NodeShuffleWellRetype.h"   // WellShort
#include "NodeShuffleWellRelocate.h" // IsFiniteVector + WellAdoptMatchRadiusCm
// PASS B (the location backstop) and its TActorIterator moved to NodeShuffleWellBackstop.cpp for the
// 500-line rule -- so EngineUtils.h is deliberately NOT included here any more. Nothing in this file
// iterates the world: pass A is handle-driven, which is the whole point of its safety argument.

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

void ANodeShuffleSubsystem::SweepOrphanedWellActors(const TCHAR* Phase, bool bRelocationOn)
{
    // ================================================================================================
    // ns-review-h5 F1 / ns-review-h2-r2 F-B -- THE TWO GATES, AND WHAT THEY ARE ALLOWED TO GATE.
    // ================================================================================================
    // GATE 1: the feature must be ON for this pass. ApplyWellRelocation's `!bOn` block does not return
    // (already-relocated wells must keep being maintained), so this used to run with both toggles off.
    // GATE 2: at least ONE group must actually be placed. With an empty or all-vanilla WellLayout the
    // "accounted for" set is empty, so a world-driven scan classifies EVERY runtime fracking actor as
    // unaccounted -- the entire vanilla well population, in a save where the player never enabled the
    // feature. Two independent gates on purpose: gate 1 is a config read and gate 2 is a fact about the
    // save, and the failure that parked this packet needed only ONE of them to be missing.
    //
    // ns-review-h2-r2 F-B (BLOCKING): THEY GATE PASS B ONLY. Both arguments above are arguments about a
    // WORLD SCAN crossed with an empty layout. Neither is an argument about pass A, whose entire safety
    // case (stated at the cap below) is "it only ever destroys actors whose handles WE created" -- a
    // property of the handle maps, independent of config and of placedGroups. Gating pass A killed the
    // h4-F2 reclamation in precisely the window that manufactures abandoned handles: ~20 wells enrolled,
    // none yet complete, placedGroups == 0, a re-roll refuses one, its actors stay standing, and the log
    // said "SWEEP SKIPPED ... placedGroups=0", which reads as healthy. Worse, in a save where every
    // enrolled well eventually fails, placedGroups is 0 forever and pass A never runs again.
    // The reconciliation and pass A therefore run UNCONDITIONALLY from here down; the gates are
    // evaluated where pass B is called, and the skip line says which pass was skipped.
    int32 PlacedGroups = 0;
    for (const FNodeShuffleWellEntry& E : WellLayout) { if (E.bGroupPlaced) { ++PlacedGroups; } }
    const bool bPassBGatesPass = bRelocationOn && PlacedGroups > 0;

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
    // UNGATED (F-B): withdrawing a claim destroys nothing.
    // A3: CHECK INVARIANT A3 BEFORE ANYTHING READS THE FLAG, and again it is deliberately placed before
    // the reconciliation -- the reconciliation WRITES claims, so checking after it would hide exactly the
    // desynchronisation the check exists to catch (a state that arrived from the save, from a roll, or
    // from a future writer would be repaired-then-verified, which verifies nothing).
    const int32 ClaimViolations = ValidateWellClaimInvariant(Phase);

    const int32 Reconciled = ReconcileAbandonedWellClaims(TEXT("state reconciliation at the orphan sweep"));

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

    // ================================================================================================
    // A3 -- PASS A NOW ASKS THE CLAIM, IT DOES NOT RE-DERIVE IT.
    // ================================================================================================
    // WHAT WAS HERE: a lambda `EntryIsMidAssembly(E) = !bGroupPlaced && bRelocate && !bRelocationFailed`,
    // i.e. the derived abandonment predicate, evaluated a THIRD time (the reconciliation above being the
    // first and the escalation ladder the second). It is retired.
    //
    // THE CONVERSION IS BEHAVIOUR-IDENTICAL, and here is why rather than an assertion that it is:
    // ReconcileAbandonedWellClaims runs UNCONDITIONALLY a few lines above, over the same WellLayout, and
    // its predicate `!bGroupPlaced && !(bRelocate && !bRelocationFailed)` is the EXACT COMPLEMENT of
    // EntryIsMidAssembly over non-placed entries. So by the time control reaches these loops, every
    // non-placed entry has either had its claim withdrawn (Placed* zeroed -> IsAtTarget false) or IS
    // mid-assembly. `EntryIsMidAssembly` was therefore ALREADY true for every entry that reached it, and
    // `NoLongerProtected` was ALREADY structurally zero -- a counter for a state the reconciliation had
    // just made unreachable. Now the same fact is read from one boolean instead of inferred from three.
    //
    // ns-review-h2-r4 -- AND ITS REPLACEMENT `ClaimDeadAtCoord` IS DELETED RATHER THAN KEPT.
    // h2-8 replaced `NoLongerProtected` with `ClaimDeadAtCoord` and argued it "counts a different
    // thing". Round 8 agreed only in a narrow sense and it was right to: for CORES the counter was
    // strictly a subset of what ValidateWellClaimInvariant already reports, so it was a second, weaker
    // detector of one state -- and this packet's own history says two views of one rule drift apart.
    // Its ONE non-redundant case was SATELLITES, which the checker did not cover. So the choice made
    // here is the one that leaves ONE detector rather than two: the checker was EXTENDED to bCaptured
    // satellites (F-7, in NodeShuffleWellClaim.cpp) and this counter is GONE. The state it named is
    // now reported by name AND repaired by the single clearer (F-2) instead of being tallied.
    int32 Destroyed = 0, RefusedInUse = 0, AlreadyGone = 0, Retrying = 0;
    FString InUseDetail;

    // ---- PASS A: handles no entry owns ----
    TArray<FString> OrphanCoreKeys, OrphanSatKeys;
    for (const TPair<FString, AFGResourceNodeFrackingCore*>& P : SpawnedWellCores)
    {
        const FNodeShuffleWellEntry* const* Found = EntryByCorePath.Find(P.Key);
        const FNodeShuffleWellEntry* E = Found ? *Found : nullptr;
        if (E && E->bGroupPlaced) { continue; } // placed: owns them, unchanged and never claim-driven
        if (E && E->bPlacementClaimLive && IsAtTarget(P.Value, E->PlacedCoreLocation))
        {
            ++Retrying; continue;               // A3: the entry OWNS this coordinate. One read, one fact.
        }
        // (ns-review-h2-r4: the `ClaimDeadAtCoord` tally that sat here is deleted -- see the block
        // above. A dead claim on a live coordinate is now NAMED by ValidateWellClaimInvariant and
        // REPAIRED by ClearAbandonedWellPlacement, which is strictly more than a counter did.)
        OrphanCoreKeys.Add(P.Key);
    }
    for (const TPair<FString, AFGResourceNodeFrackingSatellite*>& P : SpawnedWellSatellites)
    {
        const auto* Found = EntryBySatPath.Find(P.Key);
        const FNodeShuffleWellEntry* E = Found ? Found->Key : nullptr;
        const FNodeShuffleWellSatellite* S = Found ? Found->Value : nullptr;
        if (E && S && E->bGroupPlaced && S->bCaptured) { continue; }
        // A3: a satellite's PlacedLocation is part of the SAME claim as its core's -- they are written
        // and cleared together in one commit and one withdrawal, which is exactly why the claim is a
        // property of the ENTRY and not of the record. Reading the entry's flag here is what stops the
        // core and its satellites landing on two different verdicts (h2-6 F-A's "the group is split
        // across two verdicts" asymmetry, where satellites fell out of protection and the core did not).
        if (E && S && E->bPlacementClaimLive && IsAtTarget(P.Value, S->PlacedLocation))
        {
            ++Retrying; continue;
        }
        // (ns-review-h2-r4: `ClaimDeadAtCoord`'s satellite tally is deleted here too -- and THIS is the
        // case that used to be its only non-redundant contribution. ValidateWellClaimInvariant now
        // covers bCaptured satellites (F-7), so the state is named by the same detector that names the
        // core's, instead of being counted by a second one that could drift away from it.)
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
    // Split into NodeShuffleWellBackstop.cpp for the 500-line rule. The full rationale, the LOG-ONLY
    // parking note and the ns-review-h2-r2 F-C instrumentation all live in that file's banner.
    //
    // ns-review-h2-r2 F-B: THIS is where the two gates apply, and NOWHERE ELSE. Both gates are an
    // argument about a WORLD SCAN crossed with an empty layout; pass A above is handle-driven and needs
    // neither. `settled` is a third, pre-existing condition: pass B is a full actor iteration and must
    // not run before the world has streamed.
    int32 BackstopSeen = 0, BackstopUnaccounted = 0;
    const bool bSettledPhase = (FCString::Strcmp(Phase, TEXT("settled")) == 0);
    if (bSettledPhase && bPassBGatesPass)
    {
        RunWellLocationBackstop(Phase, BackstopSeen, BackstopUnaccounted);
    }
    else if (bSettledPhase && !bWellSweepGatedLogged)
    {
        // Said once per session, not per pass: this is a steady state, and a ~60 s drip is the shape
        // ns-review-h5 F-4/F7 already fixed twice in this packet. It now names WHICH pass was skipped
        // AND what pass A did anyway -- the old wording ("SWEEP SKIPPED and NOTHING was examined or
        // destroyed") was accurate about the whole sweep, and that accuracy WAS the F-B bug: a line
        // reading as healthy over an actor nobody reclaimed is the failure this packet is parked on.
        bWellSweepGatedLogged = true;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-ORPHAN [%s]: PASS B (the world-scan backstop) SKIPPED -- relocationOn=%d, ")
            TEXT("placedGroups=%d (layout entries %d, core handles %d, satellite handles %d). BOTH gates ")
            TEXT("must hold before a WORLD SCAN may run: ungated with an empty layout it destroyed every ")
            TEXT("runtime fracking actor in the world at apply pass 8 of every session, feature off ")
            TEXT("(ns-review-h5 F1). PASS A STILL RAN and is never gated (ns-review-h2-r2 F-B): it found ")
            TEXT("%d unowned handle(s) and destroyed %d, having reconciled %d abandoned claim(s). ")
            TEXT("Said once."),
            Phase, bRelocationOn ? 1 : 0, PlacedGroups, WellLayout.Num(),
            SpawnedWellCores.Num(), SpawnedWellSatellites.Num(),
            HandleOrphans, Destroyed, Reconciled);
    }

    if (HandleOrphans == 0 && AlreadyGone == 0 && Reconciled == 0
        && ClaimViolations == 0 && BackstopSeen == 0)
    {
        return;
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-ORPHAN [%s]: pass A ran (ALWAYS -- never gated, ns-review-h2-r2 F-B); pass B gates ")
        TEXT("relocationOn=%d placedGroups=%d -> %s. A3 claim state: %d invariant violation(s) (cores ")
        TEXT("AND bCaptured satellites since ns-review-h2-r4 F-7 -- MUST be 0; any other value is ")
        TEXT("h2-7 F-A's signature, grep WELLH2-CLAIM for the named entry), %d abandoned claim(s) ")
        TEXT("reconciled before reading ownership. Handles -- %d unowned (destroyed %d of a %d cap, %d ")
        TEXT("deferred by the cap, refused %d in use, %d already gone), %d protected by a LIVE CLAIM ")
        TEXT("(the entry owns that coordinate: OWNED, not orphaned). Backstop -- %d runtime fracking ")
        TEXT("actor(s) examined, %d unaccounted, 0 destroyed (LOG-ONLY).%s%s"),
        Phase, bRelocationOn ? 1 : 0, PlacedGroups,
        bSettledPhase ? (bPassBGatesPass ? TEXT("RAN") : TEXT("SKIPPED")) : TEXT("not this phase"),
        ClaimViolations, Reconciled,
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

// ns-review-h2-r2 F-I: THE ROLL'S TEARDOWN TAIL, moved here from NodeShuffleWellRelocateRoll.cpp
// (that file was at exactly 500 lines and the F-A fix pushed it over). It belongs next to the sweep
// it drives anyway: every line of it is about reclaiming what the roll abandoned.
// `bRelocationEnabled` is RelocateResourceWells as the caller received it -- see the gate note below
// for why that alone is NOT the sweep's gate.
void ANodeShuffleSubsystem::FinishWellRollTeardown(bool bRelocationEnabled)
{
    // ns-review-h5 judgement call (1): SWEEP AT THE ROLL'S TAIL, after every entry's fate is decided. A
    // guard at the loop TOP was correctly rejected (it would run before the bGroupPlaced branch and tear
    // down the wells the packet exists to preserve), but "no roll-time sweep at all" was the weakest
    // version of that idea. No ordering hazard here, and it closes a real window: WellAuditPasses is NOT
    // reset on a re-roll, so handles from wells refused by the `continue`s above would otherwise survive
    // ~60 s until the next cadence sweep, inside which SpawnWellGroup's lazy adoption can re-adopt them.
    // ns-review-h5 F2 -- FIRST, WITHDRAW THE PLACEMENT CLAIM OF EVERY ENTRY THIS ROLL ABANDONED. Ten
    // `continue`s above clear bRelocate and NOT ONE touched Placed*, so the sweep kept reporting their
    // stranded actors as "mid-assembly: OWNED, not orphaned" forever. ONE state-driven loop, not a clear
    // in each of the ten. UNCONDITIONAL, NOT under the sweep's gates below (it destroys nothing).
    // Rationale + the independent one-line second layer: NodeShuffleWellSweep.cpp.
    // ns-review-h2-r2 F-A: ONE copy of the rule, shared with the sweep's reconciliation. It was written
    // out twice with identical bodies, which is the drift shape this packet keeps being bitten by.
    // ================================================================================================
    // ns-review-h2-r4 F-1 (HIGH) -- CHECK THE INVARIANT *HERE*, BEFORE THE RECONCILIATION REPAIRS IT.
    // ================================================================================================
    // SweepOrphanedWellActors states the rule ("checking after a repair would be repaired-then-
    // verified, which verifies nothing") and honours it -- but this function called the reconciliation
    // FIRST and the sweep (which runs the check) SECOND, so at the post-roll sweep the guarantee was
    // INVERTED and nothing said so. That is the worst place for it to be inverted: the post-roll sweep
    // is the first sweep after twelve roll exits, i.e. the single sweep most likely to be looking at a
    // desynchronised claim.
    //
    // THE DIRECTION IT MASKED IS THE DANGEROUS ONE. `claim=true, coordinate=zero` (a future edit that
    // sets the flag on an error path without writing a coordinate) is repaired by the reconciliation
    // below -- ClearAbandonedWellPlacement sets claim=false on an already-zero coordinate -- so the
    // biconditional is restored before the checker ever looks, and the log then prints
    // `INVARIANT A3 HOLDS ... 0 violations` over a session in which a live claim on a ZERO coordinate
    // existed. That claim protects actors at the WORLD ORIGIN and can make SpawnWellGroup materialise
    // a snappable node there (h3 H10) -- the worst outcome in this packet's history.
    //
    // The in-sweep call is NOT removed: two calls cost ~20 comparisons each and they cover different
    // instants. The "INVARIANT A3 HOLDS" line is throttled once per session, so whichever call gets
    // there first names itself in the [tag]; both tags are valid evidence that the check ran.
    ValidateWellClaimInvariant(TEXT("post-roll pre-reconcile"));

    const int32 ClaimsWithdrawn = ReconcileAbandonedWellClaims(TEXT("abandoned by this roll"));
    // ns-review-h5 F1: the sweep refuses to look at the world unless relocation is on for this pass, and
    // bRelocationEnabled ALONE IS NOT that condition -- ApplyWellRelocation's gate is bWellShuffle &&
    // bRelocation while this function receives RelocateResourceWells only. Passing `true` would re-open
    // the blocker: a save with ShuffleResourceWells off but Relocate on reaches this line.
    const bool bSweepOn = FNodeShuffleConfigStruct::GetActiveConfig(this).ShuffleResourceWells
                       && bRelocationEnabled;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-ROLL: withdrew the placement claim of %d of %d entry(ies) here at the tail -- each ")
        TEXT("refused by one of the roll's ten branches (h5 F2). The ELEVENTH abandonment route, ")
        TEXT("RE-ENROLMENT, is withdrawn inside the enrolment loop instead and is NOT in this count ")
        TEXT("(ns-review-h2-r2 F-A) -- grep WELLH2-ABANDON with reason 're-enrolled by a new roll' for ")
        TEXT("those. Post-roll sweep gate wellShuffle&&relocate=%d; pass A runs regardless (F-B)."),
        ClaimsWithdrawn, WellLayout.Num(), bSweepOn ? 1 : 0);
    SweepOrphanedWellActors(TEXT("post-roll"), bSweepOn);
}
