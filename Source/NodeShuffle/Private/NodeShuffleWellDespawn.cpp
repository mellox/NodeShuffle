// Packet H2 (ns-wells-h2, branch feature/extractor-automatch): TEARING A RELOCATED WELL GROUP DOWN.
//
// This whole file is ns-review-h3 H1, the blocking finding of the second cold review, and it is split
// out of NodeShuffleWellSpawn.cpp both for the 500-line limit and because it is the counterpart that
// packet H2 shipped twice without: nothing in the packet ever DESTROYED a spawned well actor.
// SpawnedWellCores / SpawnedWellSatellites were only ever Add / FindRef / Contains -- no Remove, no
// Destroy -- so every path that MOVED a group left its previous actors alive at the abandoned
// coordinates. A spawn without a despawn is not an oversight in one function; it is a missing half of
// the lifecycle, so it gets its own file and its own name.
//
// THE THREE ROUTES IN, all closed by the call sites listed below:
//   (a) a partial spawn leaves bGroupPlaced false, the next pass advances YawCursor and commits NEW
//       coordinates, and the already-spawned members are then reused at their OLD transforms;
//   (b) a re-roll re-enrolment resets the destination and deals a fresh one -- and that is precisely
//       the sequence this feature's own config tooltip instructs, run twice;
//   (c) the give-up branch logged "LEFT EXACTLY WHERE THE LEVEL AUTHOR PUT IT" while our core and any
//       satellites stayed live at the abandoned destination -- a permanent duplicate well that the
//       audit could not even see, because it only walks bGroupPlaced entries and that one is false.
// The output of (a)/(b) was the worst state this packet can reach: core at old coordinates, satellites
// at new ones, every count matching, vanilla group suppressed, and the audit printing OK for a well
// whose members are kilometres apart. The gate rebuilt to be able to fail could not see position.
//
// DESPAWN-ON-MOVE, DELIBERATELY NOT SetActorLocationAndRotation. Move-in-place is the tempting
// one-liner and the highest-risk option available: a teleported AFGResourceNode may not update its
// paired mesh actor, its scanner representation, or its entry in the resource-node manager, and every
// one of those paths is closed-source -- we could not verify the claim, only hope. Destroy-and-respawn
// re-runs the birth sequence we have already reasoned about and instrumented end to end.
//
// THE OCCUPANCY GATE IS THE SAFETY PROPERTY OF THIS FILE. If a player has built on a member we do NOT
// destroy it: destroying an actor out from under someone's machine is the one way this fix could be
// worse than the bug it closes. Such a group is ABANDONED IN PLACE -- the actors stay, the vanilla
// group stays suppressed (un-suppressing would hand the player two working wells), and it is logged
// loudly enough to be impossible to miss.
//
// CALL SITES, all of them BEFORE any destination is rewritten:
//   * NodeShuffleWellEscalate.cpp   -- the top of EscalateWellPlacement (covers (a) and (c))
//   * NodeShuffleWellRelocateRoll.cpp -- the re-enrolment reset (covers (b))
//   * NodeShuffleWellSpawn.cpp      -- DespawnStaleWellMembers, the structural belt to those braces
//
// AND THE THING THIS FILE CANNOT DO, which is why NodeShuffleWellSweep.cpp exists. Everything here is
// call-site-driven: it only ever covers routes someone remembered to instrument. Two consecutive cold
// reviews found a route nobody had -- six roll-time `continue`s that return before the re-enrolment
// despawn (h4 F2), and an abandoned ACTOR that holds no handle at all after a save inside an incomplete
// spawn window (h5 F-1). The sweep reclaims by scanning STATE instead, so it covers routes nobody
// remembered, including ones that do not exist yet. Both kinds are needed; neither replaces the other.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleWellCensus.h"   // AFGResourceNodeFrackingCore / ...Satellite
#include "NodeShuffleWellRetype.h"   // WellShort
#include "NodeShuffleWellRelocate.h" // IsFiniteVector + WellAdoptMatchRadiusCm
#include "Resources/FGResourceNode.h"

// ------------------------------------------------------------------------------------------------
// DESPAWN  (ns-review-h3 H1, BLOCKING) -- the counterpart the packet was missing entirely
// ------------------------------------------------------------------------------------------------
// Until now nothing in H2 ever destroyed a spawned well actor. SpawnedWellCores / SpawnedWellSatellites
// were only ever Add / FindRef / Contains -- no Remove, no Destroy -- so every path that MOVED a group
// left its previous actors alive at the abandoned coordinates:
//   (a) a partial spawn leaves bGroupPlaced false, the next pass advances YawCursor and commits NEW
//       coordinates, and the already-spawned members are then reused at their OLD transforms;
//   (b) a re-roll re-enrolment resets the destination and deals a fresh one without touching either
//       map -- and that is precisely the sequence this feature's own config tooltip instructs, run
//       twice;
//   (c) the give-up branch logs "LEFT EXACTLY WHERE THE LEVEL AUTHOR PUT IT" while our core and any
//       satellites stay live at the abandoned destination -- a permanent duplicate well that the
//       audit cannot even see, because it only walks bGroupPlaced entries.
//
// DESPAWN-ON-MOVE, not move-in-place. SetActorLocationAndRotation is the tempting one-liner and the
// highest-risk option available: a teleported AFGResourceNode may not update its paired mesh actor,
// its scanner representation, or its entry in the resource-node manager -- and every one of those
// paths is closed-source, so we could not verify the claim, only hope. Destroy-and-respawn re-runs
// the whole birth sequence we have already reasoned about and instrumented.
//
// THE OCCUPANCY GATE IS THE SAFETY PROPERTY. If a player has built a Pressurizer or an Extractor on a
// member, we do NOT destroy it -- destroying an actor out from under a player's machine is the one way
// this fix could be worse than the bug it closes. Such a group is ABANDONED IN PLACE: the actors stay,
// the vanilla group stays suppressed (un-suppressing would give the player two wells), and it is
// logged loudly enough to be impossible to miss.

// ns-review-h4 F4: ONE teardown sequence, called from BOTH despawn paths.
//
// They previously tore down the same actor classes with DIFFERENT sequences -- DespawnWellGroup did
// UnregisterManagedNode -> DeregisterNodeFromManager -> RemoveResourceNodeScan_Local ->
// UpdateNodeRepresentation -> Destroy(), while DespawnStaleWellMembers did only the first two. One of
// them had to be wrong, and given this mod's scanner-phantom-ping history (a hidden node that stayed in
// the scanner's cluster list kept pinging empty ground, fixed three separate times) the four-call form
// is assumed load-bearing. Two copies of a teardown where only one is complete is the same shape as the
// two-ladders bug (ns-review-h2 F3) and the two-copies-of-the-health-rule bug (h3), so it is now one
// copy that both callers share.
//
// Order matters: every registration is dropped BEFORE Destroy(), while the object is still addressable
// -- the ordinary node path documents that constraint for UnregisterManagedNode (the hard reference is
// what keeps a destroyed object's memory addressable for the key computation).
//
// Returns true when the actor is gone (or was already). On a refusal, OutWhy names WHICH occupancy
// signal fired, so an ABANDONED IN PLACE line is diagnosable rather than merely alarming.
// A MEMBER, not a free function: DeregisterNodeFromManager is private and depends on this class's
// AccessTransformers Friend grant on AFGResourceNodeManager, and C++ friendship is class-to-class.
bool ANodeShuffleSubsystem::DestroyWellMemberIfUnused(AFGResourceNodeBase* Member, const TCHAR*& OutWhy)
{
    OutWhy = TEXT("");
    if (!IsValid(Member)) { return true; } // already gone -- the caller still drops the handle

    // ns-review-h4 F1: the SHARED two-part predicate, not IsOccupied() alone. See its comment in
    // NodeShuffleWellRelocate.h for why the weaker test is unsafe on a destructive path.
    if (IsWellMemberInUse(Member, OutWhy)) { return false; }

    // THE one teardown sequence (F4). All four registrations are dropped BEFORE Destroy(), while the
    // object is still addressable.
    FNodeShuffleModule::UnregisterManagedNode(Member);
    DeregisterNodeFromManager(Member);
    Member->RemoveResourceNodeScan_Local();
    Member->UpdateNodeRepresentation();

    Member->Destroy();
    return true;
}

// Tears the whole relocated group down. Called at EVERY point that moves a group: the escalation
// ladder (before either destination write), the roll's re-enrolment reset, and the give-up branch.
// Returns false when at least one member was occupied and therefore left alone.
bool ANodeShuffleSubsystem::DespawnWellGroup(FNodeShuffleWellEntry& E, const TCHAR* Why)
{
    int32 Destroyed = 0, RefusedOccupied = 0, AlreadyGone = 0;
    FString RefusalReasons;

    const auto Drop = [&](AFGResourceNodeBase* Member) -> bool
    {
        if (!IsValid(Member)) { ++AlreadyGone; return true; }
        const TCHAR* Why = TEXT("");
        // ns-review-h4 F1/F4: one predicate, one teardown sequence -- both live in
        // DestroyWellMemberIfUnused now, so this lambda cannot drift from the stale-member path.
        if (!DestroyWellMemberIfUnused(Member, Why))
        {
            ++RefusedOccupied;
            const FString Line = FString::Printf(TEXT("%s(%s)"), *Member->GetName(), Why);
            RefusalReasons += (RefusalReasons.IsEmpty() ? TEXT("") : TEXT(", ")) + Line;
            return false;
        }
        ++Destroyed;
        return true;
    };

    bool bAllClear = true;
    if (AFGResourceNodeFrackingCore* Core = SpawnedWellCores.FindRef(E.CorePath))
    {
        if (Drop(Core)) { SpawnedWellCores.Remove(E.CorePath); }
        else { bAllClear = false; }
    }
    else
    {
        SpawnedWellCores.Remove(E.CorePath); // stale/null slot -- drop it either way
    }

    for (FNodeShuffleWellSatellite& S : E.Satellites)
    {
        AFGResourceNodeFrackingSatellite* Sat = SpawnedWellSatellites.FindRef(S.SatellitePath);
        if (!Sat) { SpawnedWellSatellites.Remove(S.SatellitePath); S.bPlaced = false; continue; }
        if (Drop(Sat))
        {
            SpawnedWellSatellites.Remove(S.SatellitePath);
            // ns-review-h3 H9: bPlaced was written and never read once the F10 guard was rewritten.
            // It now means exactly one thing -- "a relocated actor exists for this record" -- set at
            // spawn/adopt and cleared here, and the audit cross-checks it against the live handle.
            S.bPlaced = false;
        }
        else { bAllClear = false; }
    }

    if (Destroyed > 0 || RefusedOccupied > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-DESPAWN core='%s' (%s): destroyed %d relocated member(s), %d already gone, %d ")
            TEXT("REFUSED (occupied). Without this the old actors would stay live at the abandoned ")
            TEXT("coordinates while the entry moved on -- a duplicate well the audit cannot see."),
            *WellShort(E.CorePath), Why, Destroyed, AlreadyGone, RefusedOccupied);
    }

    if (RefusedOccupied > 0)
    {
        // The one case where we must not clean up. Loud, ungated, and it names the consequence: the
        // group is abandoned in place and the vanilla well STAYS suppressed, because un-suppressing it
        // would hand the player two working wells instead of one.
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-DESPAWN core='%s': *** ABANDONED IN PLACE *** -- %d relocated member(s) are ")
            TEXT("OCCUPIED (a player has built on them), so they were NOT destroyed. Destroying an actor ")
            TEXT("under someone's machine would be worse than the duplicate it prevents. In use: [%s] ")
            TEXT("(ns-review-h4 F1 -- the signal is named because IsOccupied() alone is NOT the test; ")
            TEXT("the activator/extractor half exists because IsOccupied() was not trusted). Those ")
            TEXT("actors stay live at %s, this well is no longer managed from that position, and its ")
            TEXT("vanilla group REMAINS suppressed. Reason for the move: %s."),
            *WellShort(E.CorePath), RefusedOccupied, *RefusalReasons,
            *E.PlacedCoreLocation.ToCompactString(), Why);
    }

    return bAllClear;
}

// The belt to DespawnWellGroup's braces (ns-review-h3 H1): drop any handle whose actor is no longer at
// the coordinate the entry now names. This catches a destination rewritten by a path that did not go
// through one of the three despawn call sites -- exactly the class of hole that produced the finding,
// so it is guarded structurally rather than by remembering to call something.
// ns-review-h4 F3: RETURNS the number of stale members it could NOT clear. SpawnWellGroup requires
// that to be zero before it will call a group COMPLETE -- moving the drift check from DETECTION to
// PREVENTION, because suppression is the irreversible half. Previously a refused stale member was left
// in the map, FindRef'd on the next line, counted by ++ReusedSats with no position test, and could
// carry bComplete to true with a member kilometres away -> bGroupPlaced -> the vanilla well suppressed
// for good. The audit did print GROUP SCATTERED, but only after the irreversible act.
int32 ANodeShuffleSubsystem::DespawnStaleWellMembers(FNodeShuffleWellEntry& E)
{
    const float MatchSq = FMath::Square(WellAdoptMatchRadiusCm);
    int32 Stale = 0, RefusedInUse = 0;

    // ==============================================================================================
    // ns-review-h2-r2 F-D -- WRITE THE UNWRITTEN INVARIANT DOWN. READ THIS BEFORE "FIXING" THE
    // DIVERGENCE FROM THE SWEEP'S COPY OF THIS LAMBDA. DELIBERATELY NOT CHANGED (2026-08-07).
    // ==============================================================================================
    // The sweep's IsAtTarget (NodeShuffleWellSweep.cpp) gained `&& !Target.IsNearlyZero()` when F2
    // started clearing withdrawn claims TO ZeroVector. THIS copy did not, and the h2-6 handoff claimed
    // that was safe because it "fails in the opposite direction (an actor near the origin would be
    // judged at-target and KEPT), which is non-destructive". THAT CHARACTERISATION IS FALSE, and it
    // inverts the dominant case: both call sites below act on `!IsAtTarget`, i.e. they DESTROY. With
    // Target == ZeroVector and the actor anywhere except within 300 cm of the world origin, IsAtTarget
    // is FALSE, so the member is judged stale and DESTROYED (subject to the occupancy gate only).
    //
    // ==> PASTING THE SWEEP'S GUARD IN HERE WOULD MAKE THIS SITE ALWAYS-DESTROY ON A ZERO TARGET. <==
    // The correct polarity for a destructive site is the opposite one: a zero target means NO CLAIM
    // EXISTS, so "is this member stale?" is unanswerable and the member must be SKIPPED and logged,
    // never destroyed. That is a behaviour change to a destructive path and it is NOT taken in this
    // packet -- it needs its own review, and it is currently UNREACHABLE. Which is the invariant:
    //
    //   INVARIANT (load-bearing, previously unwritten, still unenforced):
    //   DespawnStaleWellMembers is only ever called with E.PlacedCoreLocation NON-ZERO, and only ever
    //   iterates satellite records whose PlacedLocation is non-zero or which hold no handle.
    //   WHY IT HOLDS TODAY: the sole caller is SpawnWellGroup (NodeShuffleWellSpawn.cpp:96), reached
    //   only (a) straight after TryPlaceWellGroup committed a non-zero PlacedCoreLocation
    //   (NodeShuffleWellRelocateApply.cpp), or (b) on the bGroupPlaced maintenance branch, where
    //   ClearAbandonedWellPlacement provably never fires (it returns early on bGroupPlaced). A
    //   satellite record with a permanently-zero PlacedLocation is !bCaptured and can never acquire a
    //   handle (spawn guard NodeShuffleWellSpawn.cpp:170-187; adopt filter NodeShuffleWellLink.cpp).
    //   WHAT ERODES IT: every new site that writes ZeroVector into Placed*. h2-6 added three.
    //   IF YOU ADD A FOURTH, re-derive this or make the skip-and-log change under its own review.
    const auto IsAtTarget = [&](const AActor* A, const FVector& Target) -> bool
    {
        return IsValid(A) && IsFiniteVector(Target)
            && FVector::DistSquared(A->GetActorLocation(), Target) < MatchSq;
    };

    // ns-review-h4 F7: the refusal state NEVER SELF-CLEARS (a player's machine stays built), so an
    // unthrottled Warning here is a line every ~5 s forever. Throttled per record per session; the
    // audit's *** GROUP SCATTERED *** is the standing indicator, this is the one-time explanation.
    const auto WarnStaleInUse = [&](const FString& Key, const FString& Label, const TCHAR* Why,
                                    const FVector& Was, const FVector& Want)
    {
        if (WellStaleInUseLogged.Contains(Key)) { return; }
        WellStaleInUseLogged.Add(Key);
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-STALE core='%s' member='%s': at STALE coordinates (%s, entry says %s) but IN USE ")
            TEXT("(%s) -- NOT destroyed. This group is genuinely scattered and will keep reporting *** ")
            TEXT("GROUP SCATTERED *** until the player removes what they built. It will NOT be marked ")
            TEXT("complete and its vanilla group will NOT be suppressed while this holds. Said once."),
            *WellShort(E.CorePath), *Label, *Was.ToCompactString(), *Want.ToCompactString(), Why);
    };

    if (AFGResourceNodeFrackingCore* Core = SpawnedWellCores.FindRef(E.CorePath))
    {
        if (IsValid(Core) && !IsAtTarget(Core, E.PlacedCoreLocation))
        {
            const FVector Was = Core->GetActorLocation();
            const TCHAR* Why = TEXT("");
            if (DestroyWellMemberIfUnused(Core, Why))
            {
                SpawnedWellCores.Remove(E.CorePath);
                ++Stale;
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("WELLH2-STALE core='%s': the live core was at %s but this entry now says %s -- ")
                    TEXT("destroyed and respawning at the current coordinate. A reused handle at stale ")
                    TEXT("coordinates is how a 'satellites=N/N/N OK' well ends up scattered."),
                    *WellShort(E.CorePath), *Was.ToCompactString(),
                    *E.PlacedCoreLocation.ToCompactString());
            }
            else
            {
                ++RefusedInUse;
                WarnStaleInUse(E.CorePath, FString(TEXT("<core>")), Why, Was, E.PlacedCoreLocation);
            }
        }
    }

    for (FNodeShuffleWellSatellite& S : E.Satellites)
    {
        AFGResourceNodeFrackingSatellite* Sat = SpawnedWellSatellites.FindRef(S.SatellitePath);
        if (!IsValid(Sat) || IsAtTarget(Sat, S.PlacedLocation)) { continue; }
        const FVector Was = Sat->GetActorLocation();
        const TCHAR* Why = TEXT("");
        if (DestroyWellMemberIfUnused(Sat, Why))
        {
            SpawnedWellSatellites.Remove(S.SatellitePath);
            S.bPlaced = false;
            ++Stale;
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-STALE core='%s' sat='%s': live at %s but the entry says %s -- destroyed and ")
                TEXT("respawning at the current coordinate."),
                *WellShort(E.CorePath), *WellShort(S.SatellitePath), *Was.ToCompactString(),
                *S.PlacedLocation.ToCompactString());
        }
        else
        {
            ++RefusedInUse;
            WarnStaleInUse(S.SatellitePath, WellShort(S.SatellitePath), Why, Was, S.PlacedLocation);
        }
    }

    return RefusedInUse;
}
