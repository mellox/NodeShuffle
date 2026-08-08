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
// A3 (ns-review-h2-r3 §6) -- OWNERSHIP IS NOW A STORED FACT, NOT A DERIVED PREDICATE.
// ================================================================================================
// Seven review rounds each closed every prior finding and each found a NEW instance of ONE failure:
// "this entry has abandoned its coordinate" was INFERRED, independently at every reader, from a
// conjunction of bGroupPlaced / bRelocate / bRelocationFailed / Placed*-being-non-zero -- and every
// round found a new state tuple some reader mis-read. Patching instances is what kept failing.
// FNodeShuffleWellEntry::bPlacementClaimLive replaces the inference: ONE setter (the footprint commit
// in NodeShuffleWellRelocateApply.cpp), ONE clearer (ClearAbandonedWellPlacement, below), and every
// reader asks. ValidateWellClaimInvariant() checks `claim == false <=> coordinate is zero` at the top
// of every sweep so that a future edit desynchronising them shows up in the log instead of in round 9.
//
// WHAT A3 DOES NOT DO, stated here so nobody expects it to: it does not decide WHEN a claim ends.
// Three sites do that (the escalation ladder, re-enrolment, and ReconcileAbandonedWellClaims below),
// and the last of those is still a predicate over the lifecycle flags -- on purpose, because its job
// is to NOTICE an abandonment nobody instrumented. The win is that it WRITES the fact once instead of
// every reader re-deriving it. Nor does it say anything about actor IDENTITY ("is this actor ours"),
// which is what pass B, IsNetStartupActor and the deferred SaveGame identity component are about.

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
//
// A3 (ns-review-h2-r3 §6) -- THIS IS NOW THE SINGLE CLEARER OF bPlacementClaimLive, and the only
// function in the packet that zeroes Placed*. Readers no longer re-derive abandonment; they read the
// flag this function writes. See FNodeShuffleWellEntry::bPlacementClaimLive for the full argument.
bool ANodeShuffleSubsystem::ClearAbandonedWellPlacement(FNodeShuffleWellEntry& E, const TCHAR* Why)
{
    // THE ONE SAFETY GATE, kept: a placed group owns its coordinates and the spawn/adopt/link/suppress
    // paths all read them; clearing here would strand a working well instantly.
    //
    // ns-review-h2-r3 F-6: this used to be a SILENT `return false`, which made an ordering mistake at a
    // call site indistinguishable from "nothing to do". Both existing call sites are correct only
    // because bGroupPlaced happens to be false by the time they run -- the roll's by an explicit
    // `E.bGroupPlaced = false` three lines earlier, the escalation's by a caller-side precondition
    // three call frames up. Neither is enforced by anything. So the no-op is now LOUD: if a future
    // "re-validate a placed group" feature calls the escalation from the maintenance branch, the log
    // says so instead of silently reinstating h2-7 F-A at the escalation site.
    if (E.bGroupPlaced)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-ABANDON core='%s' (%s): *** CLAIM WITHDRAWAL REFUSED -- ORDERING BUG *** This ")
            TEXT("entry is still bGroupPlaced, so its coordinates are OWNED and must not be dropped, and ")
            TEXT("the caller's withdrawal has silently done NOTHING. Every caller must clear bGroupPlaced ")
            TEXT("FIRST (ns-review-h2-r2 F-A's ordering trap, ns-review-h2-r3 F-6). If you are reading ")
            TEXT("this line, a stale claim is about to outlive the entry that made it -- claimLive=%d, ")
            TEXT("core=%s."),
            *WellShort(E.CorePath), Why, E.bPlacementClaimLive ? 1 : 0,
            *E.PlacedCoreLocation.ToCompactString());
        return false;
    }

    // A3: the claim itself decides, not `Placed*` being non-zero. Identical outcome today (INVARIANT A3
    // makes them equivalent) but it is now ONE fact rather than a coordinate re-interpreted as a fact.
    if (!E.bPlacementClaimLive) { return false; } // nothing claimed -- idempotent, silent, no-op

    const FVector WasCore = E.PlacedCoreLocation;
    int32 SatsCleared = 0;
    for (FNodeShuffleWellSatellite& S : E.Satellites)
    {
        if (S.PlacedLocation.IsNearlyZero()) { continue; }
        S.PlacedLocation = FVector::ZeroVector;
        S.PlacedRotation = FRotator::ZeroRotator;
        ++SatsCleared;
    }

    // ns-review-h2-r3 F-2 (evidence integrity) -- REMEMBER THE COORDINATE INSTEAD OF ONLY DELETING IT.
    // The three fields pass B's discriminator is built from (PlacedCoreLocation, PlacedLocation,
    // DestCoreLocation) are ALL zeroed by the events that strand an actor -- this function zeroes the
    // first two and re-enrolment zeroes the third -- so at the exact instant an actor becomes strandable
    // the only coordinate that could identify it as OURS has just been deleted, and `nearestDealtXY`
    // then measured the distance to some unrelated destination. Session-scoped and capped; pass B folds
    // it into DestinationPoints. Costs 12 bytes per withdrawal and destroys nothing.
    if (!WasCore.IsNearlyZero() && AbandonedWellClaimCoords.Num() < WellAbandonedClaimCoordCap)
    {
        AbandonedWellClaimCoords.Add(WasCore);
    }

    E.PlacedCoreLocation = FVector::ZeroVector;
    E.PlacedCoreRotation = FRotator::ZeroRotator;
    E.bPlacementClaimLive = false; // A3: THE ONLY PLACE THIS IS EVER SET FALSE

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-ABANDON core='%s' (%s): dropped this entry's claim on its committed placement -- ")
        TEXT("core was %s, %d satellite coordinate(s) cleared, claimLive 1 -> 0 (relocate=%d failed=%d ")
        TEXT("placed=%d). Until this existed the stale claim made the orphan sweep report any actor ")
        TEXT("still standing there as 'mid-assembly: OWNED, not orphaned' forever (ns-review-h5 F2). ")
        TEXT("The coordinate is REMEMBERED for this session (%d recorded) so pass B can still tell a ")
        TEXT("stranded actor of ours from a mis-classified vanilla well (ns-review-h2-r3 F-2)."),
        *WellShort(E.CorePath), Why, *WasCore.ToCompactString(), SatsCleared,
        E.bRelocate ? 1 : 0, E.bRelocationFailed ? 1 : 0, E.bGroupPlaced ? 1 : 0,
        AbandonedWellClaimCoords.Num());
    return true;
}

// ================================================================================================
// A3 -- INVARIANT A3, ENFORCED IN THE LOG RATHER THAN ASSERTED IN PROSE.
// ================================================================================================
//   bPlacementClaimLive == false  <=>  PlacedCoreLocation.IsNearlyZero()
// This packet has TWICE written an invariant into a comment as "holds by construction" and then had a
// later change erode it silently (h2-6 added three ZeroVector writers to the convention
// DespawnStaleWellMembers rests on; h2-7 added a fourth). A3's whole value is that the claim is ONE
// fact -- which is worth nothing if a future edit can desynchronise it from the coordinate without
// anybody noticing. So it is CHECKED, every sweep, over ~20 entries.
//
// BOTH directions matter and they fail differently:
//   claim live + zero coordinate  -> a reader protects an actor standing at the WORLD ORIGIN, or
//                                    SpawnWellGroup spawns a live snappable node there (h3 H10).
//   claim dead + non-zero coord   -> exactly h2-7 F-A: the coordinate outlives the entry that owned it
//                                    and the sweep reports its actors as OWNED forever.
// It only ever LOGS -- it repairs nothing. A silent auto-repair would hide the edit that broke it,
// which is the failure mode this whole packet is parked on.
int32 ANodeShuffleSubsystem::ValidateWellClaimInvariant(const TCHAR* Where)
{
    int32 Violations = 0;
    for (const FNodeShuffleWellEntry& E : WellLayout)
    {
        const bool bCoordZero = E.PlacedCoreLocation.IsNearlyZero();
        if (E.bPlacementClaimLive == bCoordZero) // biconditional broken either way
        {
            ++Violations;
            if (Violations <= 5)
            {
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("WELLH2-CLAIM [%s] core='%s': *** CLAIM INVARIANT VIOLATED *** claimLive=%d but ")
                    TEXT("PlacedCoreLocation=%s (%s). INVARIANT A3 is `claimLive == false <=> the ")
                    TEXT("coordinate is zero`, and every ownership reader in the sweep and the backstop ")
                    TEXT("now trusts it. %s VOID THIS SESSION'S SWEEP NUMBERS and find the writer: the ")
                    TEXT("ONLY setter is NodeShuffleWellRelocateApply.cpp's commit and the ONLY clearer ")
                    TEXT("is ClearAbandonedWellPlacement. flags: placed=%d relocate=%d failed=%d."),
                    Where, *WellShort(E.CorePath), E.bPlacementClaimLive ? 1 : 0,
                    *E.PlacedCoreLocation.ToCompactString(), bCoordZero ? TEXT("zero") : TEXT("non-zero"),
                    E.bPlacementClaimLive
                        ? TEXT("A LIVE CLAIM ON A ZERO COORDINATE protects actors at the WORLD ORIGIN and ")
                          TEXT("can make SpawnWellGroup materialise a snappable node there (h3 H10).")
                        : TEXT("A DEAD CLAIM ON A REAL COORDINATE is h2-7 F-A exactly: the coordinate ")
                          TEXT("outlives the entry that owned it."),
                    E.bGroupPlaced ? 1 : 0, E.bRelocate ? 1 : 0, E.bRelocationFailed ? 1 : 0);
            }
        }
    }
    if (Violations == 0 && !bWellClaimInvariantOkLogged)
    {
        bWellClaimInvariantOkLogged = true;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-CLAIM [%s]: INVARIANT A3 HOLDS -- %d layout entry(ies) checked, 0 violations. ")
            TEXT("This line proves the check RAN; its absence means it did not, and every ownership ")
            TEXT("number below is then only as good as the old derived predicate. Said once per session; ")
            TEXT("a violation is logged EVERY time."), Where, WellLayout.Num());
    }
    else if (Violations > 0)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-CLAIM [%s]: *** %d of %d entry(ies) VIOLATE INVARIANT A3 *** (first %d named ")
            TEXT("above). A3 exists to stop abandonment being re-derived at every reader; a violation ")
            TEXT("means the single source has desynchronised and the derivation is back, unowned."),
            Where, Violations, WellLayout.Num(), FMath::Min(Violations, 5));
    }
    return Violations;
}

// A3 MIGRATION. See the header. One-shot, called from AdoptRestoredWellGroups before any reader runs.
int32 ANodeShuffleSubsystem::BackfillWellPlacementClaims()
{
    int32 Backfilled = 0;
    for (FNodeShuffleWellEntry& E : WellLayout)
    {
        if (E.bPlacementClaimLive) { continue; }
        if (E.PlacedCoreLocation.IsNearlyZero()) { continue; }
        E.bPlacementClaimLive = true;
        ++Backfilled;
    }
    if (Backfilled > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-CLAIM [backfill]: %d of %d entry(ies) named a committed coordinate with ")
            TEXT("claimLive=0 and were BACKFILLED to 1. Expected exactly once, on the first load of a ")
            TEXT("save written before A3 existed (the field deserialises false). If this line appears on ")
            TEXT("a save that has ALREADY been loaded by an A3 build, the claim is being lost across the ")
            TEXT("save round-trip -- the SaveGame serialization of bPlacementClaimLive is the suspect, ")
            TEXT("and that is the one thing about A3 no static reading can prove."),
            Backfilled, WellLayout.Num());
    }
    return Backfilled;
}

// ns-review-h2-r2 F-A -- THE SINGLE COPY OF THE CLAIM-WITHDRAWAL RULE.
// It existed twice (the roll tail and the sweep) with identical bodies, which is the duplicate-rule
// shape this packet has been bitten by three times already. The rule: an entry that is NOT placed and
// is NOT still searching has abandoned whatever coordinate it names, so the claim is withdrawn.
// Deliberately UNGATED at both call sites -- it destroys nothing, and gating it would leave stale
// claims in the save exactly when the sweep is least able to notice them.
// A3, AND THE ONE HONEST LIMIT OF IT. This function is the LAST derived predicate over the lifecycle
// flags in the packet, and it stays one ON PURPOSE. A3 removes the derivation from every READER; it
// cannot remove the need to decide WHEN a claim ends. Three sites know that directly (the escalation
// ladder, re-enrolment, and this) -- and this one exists precisely to catch a route the other two do
// not know about, including routes that do not exist yet. So it is a BACKSTOP that WRITES the fact,
// not a reader that infers it, and that distinction is the whole of A3: derive once, at the one place
// whose job is to notice, and let everything downstream read.
int32 ANodeShuffleSubsystem::ReconcileAbandonedWellClaims(const TCHAR* Why)
{
    int32 Withdrawn = 0;
    for (FNodeShuffleWellEntry& E : WellLayout)
    {
        if (!E.bPlacementClaimLive) { continue; }               // A3: nothing claimed, nothing to do
        if (E.bGroupPlaced) { continue; }                      // a placed group OWNS its coordinates
        if (E.bRelocate && !E.bRelocationFailed) { continue; } // mid-search: the claim is live
        if (ClearAbandonedWellPlacement(E, Why)) { ++Withdrawn; }
    }
    return Withdrawn;
}

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
    // THE COUNTER THAT REPLACES IT IS NOT THE SAME COUNTER. `NoLongerProtected` measured a state that
    // could not happen; `ClaimDeadAtCoord` measures INVARIANT A3 BEING BROKEN -- a handle standing at a
    // non-zero PlacedCoreLocation of an entry whose claim is dead. That is h2-7 F-A's exact signature,
    // and under A3 it is impossible, so a non-zero value here is a real defect report rather than noise.
    int32 Destroyed = 0, RefusedInUse = 0, AlreadyGone = 0, Retrying = 0, ClaimDeadAtCoord = 0;
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
        // A3 leak detector, not a behaviour branch: a dead claim must mean a zero coordinate, so this
        // cannot fire unless INVARIANT A3 has broken. WELLH2-CLAIM names the entry.
        if (E && !E->bPlacementClaimLive && !E->PlacedCoreLocation.IsNearlyZero()
            && IsAtTarget(P.Value, E->PlacedCoreLocation))
        {
            ++ClaimDeadAtCoord;
        }
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
        if (E && S && !E->bPlacementClaimLive && !S->PlacedLocation.IsNearlyZero()
            && IsAtTarget(P.Value, S->PlacedLocation))
        {
            ++ClaimDeadAtCoord;
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

    if (HandleOrphans == 0 && AlreadyGone == 0 && Reconciled == 0 && ClaimDeadAtCoord == 0
        && ClaimViolations == 0 && BackstopSeen == 0)
    {
        return;
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-ORPHAN [%s]: pass A ran (ALWAYS -- never gated, ns-review-h2-r2 F-B); pass B gates ")
        TEXT("relocationOn=%d placedGroups=%d -> %s. A3 claim state: %d invariant violation(s), %d ")
        TEXT("abandoned claim(s) reconciled before reading ownership, %d handle(s) standing at a ")
        TEXT("coordinate whose claim is DEAD (must be 0 under A3 -- any other value is h2-7 F-A's ")
        TEXT("signature; grep WELLH2-CLAIM). Handles -- %d unowned (destroyed %d of a %d cap, %d ")
        TEXT("deferred by the cap, refused %d in use, %d already gone), %d protected by a LIVE CLAIM ")
        TEXT("(the entry owns that coordinate: OWNED, not orphaned). Backstop -- %d runtime fracking ")
        TEXT("actor(s) examined, %d unaccounted, 0 destroyed (LOG-ONLY).%s%s"),
        Phase, bRelocationOn ? 1 : 0, PlacedGroups,
        bSettledPhase ? (bPassBGatesPass ? TEXT("RAN") : TEXT("SKIPPED")) : TEXT("not this phase"),
        ClaimViolations, Reconciled, ClaimDeadAtCoord,
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
