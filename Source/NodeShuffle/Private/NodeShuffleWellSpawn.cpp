// Packet H2 (ns-wells-h2, branch feature/extractor-automatch): THE GROUP-ATOMIC SPAWN.
//
// Split out of NodeShuffleWellLink.cpp for the 500-line limit once the ns-review-h2 fixes landed --
// the same reason, and the same kind of seam, as every other split in the well packets. The seam is
// real: this file MATERIALISES a validated placement, while NodeShuffleWellLink.cpp owns the mCore
// link itself (EnsureSatelliteLinked, the lazy-adoption helpers, the cross-session re-match) and
// NodeShuffleWellAudit.cpp owns the acceptance gate. This file calls into the first and is measured
// by the second.
//
// WHY THE SPAWN IS DEFERRED, in one sentence, because it is the whole reason the link works:
// mCore has to be written BETWEEN SpawnActorDeferred and FinishSpawning, so that the satellite's own
// BeginPlay -- which runs inside FinishSpawning -- finds a core to register itself with. Setting it
// afterwards leaves the satellite unregistered, and nothing anywhere reports that.
//
// GROUP-ATOMIC, IN BOTH SENSES (design Q1; ns-review-h2 F7):
//   * ORDERING -- the core and every satellite materialise inside ONE synchronous block, so there is
//     no window in which a player can build a Pressurizer on a core that has no satellites.
//   * COMMITMENT -- this returns true only when the group is COMPLETE. It used to return "did
//     anything spawn at all", so one satellite out of ten flipped bGroupPlaced and the caller then
//     suppressed the ENTIRE vanilla well: a short well with no visible original. An incomplete group
//     is now retried next pass with whatever it did get reused, and nothing is suppressed until it
//     is whole.
//
// A MEMBER function for the reason NodeShuffleWellRetype.h states for H1's: it writes
// AFGResourceNodeBase::mResourceClassOverride, AFGResourceNode::mPurityOverride and
// AFGResourceNodeFrackingSatellite::mCore, all of which depend on this class's AccessTransformers
// Friend grants, and C++ friendship is class-to-class, not file-to-file.
//
// IMPORT DISCIPLINE (memory: sf-shipping-export-trap): the new engine surface is SpawnActorDeferred/
// FinishSpawning plus the two StaticClass thunks. MEASURED after every build, never predicted.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleWellCensus.h"   // AFGResourceNodeFrackingCore / ...Satellite
#include "NodeShuffleWellRetype.h"   // WellPathOf / WellShort
#include "NodeShuffleWellRelocate.h" // IsFiniteVector
#include "Resources/FGResourceNode.h"

// ------------------------------------------------------------------------------------------------
// THE GROUP-ATOMIC SPAWN
// ------------------------------------------------------------------------------------------------
// Design Q1's decision, and the reason it is a decision: NodeShuffle's spawn-on-discovery is per-entry
// and asynchronous, so a core and its satellites could otherwise materialize passes apart, leaving a
// window in which a player builds a Pressurizer on a core that has no satellites yet. Spawning the
// whole group inside one synchronous block removes the window rather than mitigating it.
bool ANodeShuffleSubsystem::SpawnWellGroup(FNodeShuffleWellEntry& E, UClass* ResourceClass)
{
    UWorld* World = GetWorld();
    if (!World || !ResourceClass) { return false; }

    // ns-review-h2, LINK LIFECYCLE fix C: THE IDEMPOTENT FAST PATH IS GONE, DELIBERATELY.
    //
    // There used to be an early return here when every runtime handle was valid, so EnsureSatelliteLinked
    // ran only on the pass that spawned or adopted a group. That made the link a one-shot: any cause of
    // breakage after that pass -- another mod clearing mSatellites, a level-streaming round trip, an
    // engine path we cannot see -- was permanent and, because mCore is not SaveGame and nothing errors,
    // completely silent. What the fast path saved was a Contains() scan over at most 10 weak pointers
    // per well per ~5 s, which is nothing. The link now RE-ASSERTS EVERY PASS and is therefore
    // self-healing against any cause, known or not. The cost is measured in pointer compares; the
    // benefit is that the packet's single silent failure mode cannot survive one apply pass.
    UClass* CoreClass = LoadClassByPath(E.CoreNodeClassPath);
    UClass* SatClass = LoadClassByPath(E.SatelliteNodeClassPath);
    if (!CoreClass || !SatClass)
    {
        if (!WellRelocFailLogged.Contains(E.CorePath))
        {
            WellRelocFailLogged.Add(E.CorePath);
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-SPAWN core='%s': cannot resolve the node classes (core='%s' %s, sat='%s' %s) ")
                TEXT("-- group NOT spawned. The vanilla well is left exactly as it stands."),
                *WellShort(E.CorePath), *E.CoreNodeClassPath, CoreClass ? TEXT("ok") : TEXT("FAILED"),
                *E.SatelliteNodeClassPath, SatClass ? TEXT("ok") : TEXT("FAILED"));
        }
        return false;
    }

    // ---- 0a. A3: THE PLACEMENT CLAIM MUST BE LIVE BEFORE ANYTHING IS BUILT AT PlacedCoreLocation ----
    // Everything from here down reads E.PlacedCoreLocation and S.PlacedLocation as places to ADOPT an
    // existing actor or SPAWN a new one. With no live claim those fields are ZeroVector, and a zero is
    // FINITE -- which is exactly how h3 H10 put live, extractor-snappable nodes at the WORLD ORIGIN and
    // inflated a well's rate. The satellite loop below carries its own `LocalOffset.IsNearlyZero() &&
    // PlacedLocation.IsNearlyZero()` guard for that; the CORE has never had one, it is protected only
    // by the caller-side argument that SpawnWellGroup is unreachable without a committed placement.
    // A3 lets that argument be checked instead of trusted. UNREACHABLE TODAY via both callers (straight
    // after TryPlaceWellGroup's commit, and the bGroupPlaced maintenance branch); a tripwire, not a
    // behaviour change, and it fails toward "spawn nothing", which is always the safe direction here.
    if (!E.bPlacementClaimLive)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-SPAWN core='%s': *** GROUP NOT SPAWNED -- NO LIVE PLACEMENT CLAIM *** ")
            TEXT("claimLive=0, so PlacedCoreLocation=%s names nothing and spawning here would ")
            TEXT("materialise a live snappable node at that coordinate (h3 H10). Refused (A3). ")
            TEXT("UNREACHABLE via the two known callers -- if you are reading this, either a third ")
            TEXT("caller exists or the claim was lost across a save round-trip. placed=%d relocate=%d ")
            TEXT("failed=%d destDealt=%d."),
            *WellShort(E.CorePath), *E.PlacedCoreLocation.ToCompactString(), E.bGroupPlaced ? 1 : 0,
            E.bRelocate ? 1 : 0, E.bRelocationFailed ? 1 : 0, E.bDestDealt ? 1 : 0);
        return false;
    }

    // ---- 0. STALE-HANDLE GUARD (ns-review-h3 H1) ----
    // The reuse below used to trust the runtime handle unconditionally: FindRef, IsValid, reuse. It
    // never asked whether the actor was still WHERE THE ENTRY NOW SAYS IT SHOULD BE. Since a group's
    // destination can be rewritten under a live handle (a re-search after a partial spawn, a re-roll
    // re-enrolment, an escalation), that produced the worst state this packet can reach: a core at the
    // OLD coordinates, satellites at the NEW ones, every count matching, bGroupPlaced set, the vanilla
    // group suppressed -- and the audit printing OK for a well whose members are kilometres apart. The
    // gate we rebuilt to be able to fail could not see position at all.
    //
    // The despawn itself lives in DespawnWellGroup and is called at every point that MOVES a group;
    // this is the belt to that braces, catching any path that mutates a destination without going
    // through one of those three call sites.
    //
    // ns-review-h4 F3: a stale member it could NOT clear (because a player has built on it) BLOCKS
    // COMPLETION below. Detection was not enough: the refused member stayed in the map, was FindRef'd
    // straight afterwards, counted by ++ReusedSats with no position test, and could carry bComplete to
    // true with a member kilometres away -- which suppresses the vanilla well irreversibly. The audit
    // still printed GROUP SCATTERED, but only after the act that cannot be undone.
    const int32 StaleInUse = DespawnStaleWellMembers(E);

    // ---- 1. THE CORE ----
    AFGResourceNodeFrackingCore* Core = SpawnedWellCores.FindRef(E.CorePath);
    // ns-review-h2, LINK LIFECYCLE fix B / F5: LATE ADOPTION, IMMEDIATELY BEFORE ANY SPAWN.
    // AdoptRestoredWellGroups is single-shot at first apply, so a group whose actors had not streamed
    // in by then was never adopted -- and this function would then SPAWN A DUPLICATE INSIDE the
    // existing one. Sweeping for an existing runtime actor at the target coordinate right here makes
    // adoption lazy instead of one-shot and closes the duplicate-spawn hole at its root.
    if (!IsValid(Core))
    {
        Core = FindExistingRuntimeWellCoreAt(E.PlacedCoreLocation);
        if (IsValid(Core))
        {
            SpawnedWellCores.Add(E.CorePath, Core);
            FNodeShuffleModule::RegisterManagedNode(Core);
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH2-SPAWN core='%s': adopted-late -- a runtime fracking core was already at %s, ")
                TEXT("so it is OURS from an earlier session rather than something to spawn a duplicate of."),
                *WellShort(E.CorePath), *E.PlacedCoreLocation.ToCompactString());
        }
    }
    if (!IsValid(Core))
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        // NOT RF_Transient: the node must be collected by the save system. Same crash fix as the
        // ordinary node path (redesign-2 FIX 1) -- a Pressurizer the player builds on this core is
        // saved with a reference to it, and a transient core would be gone on reload.
        const FTransform CoreXf(E.PlacedCoreRotation, E.PlacedCoreLocation);
        {
            // spawnrace-1: KBFL's OnActorSpawned delegate fires INSIDE the spawn call, before we can
            // register the newborn as ours. Kept tight around the spawn only.
            FNodeShuffleSpawningScope SpawnScope;
            Core = World->SpawnActorDeferred<AFGResourceNodeFrackingCore>(
                CoreClass, CoreXf, nullptr, nullptr,
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        }
        if (!Core)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-SPAWN core='%s': SpawnActorDeferred returned null for class '%s' at %s -- ")
                TEXT("group NOT spawned, vanilla well left as it stands."),
                *WellShort(E.CorePath), *CoreClass->GetName(), *E.PlacedCoreLocation.ToCompactString());
            return false;
        }
        // The resource, written BEFORE FinishSpawning so it is correct the instant BeginPlay runs.
        // Friend-granted field write, not SetResourceClassOverride() -- the same import-discipline call
        // H1's RetypeWellMember documents. mResourceClassOverride is SaveGame, so it survives reload.
        Core->mResourceClassOverride = ResourceClass;
        Core->FinishSpawning(CoreXf);
        FNodeShuffleModule::RegisterManagedNode(Core); // coexist-veto-1: spawned = managed
        Core->SetActorHiddenInGame(false);
        SpawnedWellCores.Add(E.CorePath, Core);
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-SPAWN core='%s': spawned '%s' at %s yaw=%.1f res='%s'."),
            *WellShort(E.CorePath), *Core->GetName(), *E.PlacedCoreLocation.ToCompactString(),
            E.GroupYawDeg, *WellShort(ResourceClass->GetPathName()));
    }

    // ---- 2. EVERY SATELLITE, WITH mCore SET BEFORE FinishSpawning ----
    int32 SpawnedSats = 0, ReusedSats = 0, FailedSats = 0, AdoptedLate = 0;
    int32 RegisteredByBeginPlay = 0, RegisteredByUs = 0, LinkSkippedInvalid = 0;
    int32 PurityWritten = 0, PurityUnknown = 0, UncapturedRefused = 0;
    const int32 ExpectedSats = ExpectedRelocatedSatelliteCount(E);

    for (FNodeShuffleWellSatellite& S : E.Satellites)
    {
        // ns-review-h2 F2 (CRITICAL): NEVER SPAWN A SATELLITE WHOSE RIGID BODY WAS NOT CAPTURED.
        // RollWellLayout's merge can append satellite records to an already-placed well; those arrive
        // with LocalOffset and PlacedLocation both ZeroVector, and (0,0,0) IS FINITE, so the old
        // IsFiniteVector guard never fired. The result was a live, extractor-snappable satellite at
        // WORLD ORIGIN inflating the well's rate, with the acceptance line still printing 7/7/7 OK.
        // Refused here, and still suppressed at the vanilla site by SuppressVanillaWellGroup.
        if (!E.bOffsetsCaptured || !S.bCaptured
            || (S.LocalOffset.IsNearlyZero() && S.PlacedLocation.IsNearlyZero()))
        {
            ++UncapturedRefused;
            if (!WellUncapturedLogged.Contains(S.SatellitePath))
            {
                WellUncapturedLogged.Add(S.SatellitePath);
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("WELLH2-SPAWN core='%s' sat='%s': NOT SPAWNED -- this satellite has no rigid-body ")
                    TEXT("capture (captured=%d, groupCaptured=%d, offset=%s, placed=%s). Spawning it would ")
                    TEXT("put a live, extractor-snappable node at WORLD ORIGIN and inflate this well's ")
                    TEXT("rate. It is still hidden at the original site."),
                    *WellShort(E.CorePath), *WellShort(S.SatellitePath), S.bCaptured ? 1 : 0,
                    E.bOffsetsCaptured ? 1 : 0, *S.LocalOffset.ToCompactString(),
                    *S.PlacedLocation.ToCompactString());
            }
            continue;
        }

        AFGResourceNodeFrackingSatellite* Sat = SpawnedWellSatellites.FindRef(S.SatellitePath);
        // ns-review-h2 fix B / F5: late adoption before any spawn, same as the core above.
        if (!IsValid(Sat))
        {
            Sat = FindExistingRuntimeWellSatelliteAt(S.PlacedLocation);
            if (IsValid(Sat))
            {
                SpawnedWellSatellites.Add(S.SatellitePath, Sat);
                FNodeShuffleModule::RegisterManagedNode(Sat);
                S.bPlaced = true; // ns-review-h4 F8: set wherever a handle is established, not only on
                                  // a fresh spawn -- otherwise flagMismatch= in the audit cries wolf on
                                  // every adopted member and the cross-check stops being worth reading.
                ++AdoptedLate;
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WELLH2-SPAWN core='%s' sat='%s': adopted-late at %s (an existing runtime satellite ")
                    TEXT("was already there -- spawning would have made a duplicate inside it)."),
                    *WellShort(E.CorePath), *WellShort(S.SatellitePath), *S.PlacedLocation.ToCompactString());
            }
        }
        if (IsValid(Sat))
        {
            ++ReusedSats;
        }
        else
        {
            // ns-review-h2 F10: the finite guard now guards. It was `!S.bPlaced && !IsFiniteVector(...)`,
            // so a record already marked bPlaced could carry a NaN location straight into
            // SpawnActorDeferred. The placement flag has nothing to do with whether the coordinate is
            // a number.
            if (!IsFiniteVector(S.PlacedLocation))
            {
                ++FailedSats;
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("WELLH2-SPAWN core='%s' sat='%s': NOT SPAWNED -- PlacedLocation is not finite (%s). ")
                    TEXT("The group stays incomplete and is retried; nothing is suppressed."),
                    *WellShort(E.CorePath), *WellShort(S.SatellitePath), *S.PlacedLocation.ToCompactString());
                continue;
            }
            const FTransform SatXf(S.PlacedRotation, S.PlacedLocation);
            {
                FNodeShuffleSpawningScope SpawnScope;
                Sat = World->SpawnActorDeferred<AFGResourceNodeFrackingSatellite>(
                    SatClass, SatXf, nullptr, nullptr,
                    ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            }
            if (!Sat)
            {
                ++FailedSats;
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("WELLH2-SPAWN core='%s' sat='%s': SpawnActorDeferred returned null -- THE GROUP IS ")
                    TEXT("NOW SHORT ONE SATELLITE. Retried every pass; see the WELL audit line for the ")
                    TEXT("expected/spawned/registered counts."),
                    *WellShort(E.CorePath), *WellShort(S.SatellitePath));
                continue;
            }
            // ---- THE LINK, WRITTEN BEFORE FinishSpawning ----
            // This is the whole point of the deferred spawn. mCore is set here so the satellite's own
            // BeginPlay -- which runs inside FinishSpawning -- finds it and registers itself with the
            // core. Setting it AFTER would leave the satellite unregistered with nothing to notice.
            Sat->mCore = Core;
            // Resource and purity, also before FinishSpawning so they are valid the instant a restored
            // extractor's BeginPlay reads them.
            Sat->mResourceClassOverride = ResourceClass;
            if (S.OriginalPurity != RP_MAX)
            {
                // REPRODUCING the vanilla purity on the actor that replaces the level one -- see the
                // FNodeShuffleWellSatellite::OriginalPurity comment. H1 never writes purity because it
                // never replaces an actor; H2 must, or every relocated well would silently normalise to
                // the CDO's purity, which is the balance change H1 refused to make.
                Sat->mPurityOverride = S.OriginalPurity;
                ++PurityWritten;
            }
            else
            {
                ++PurityUnknown;
            }
            Sat->FinishSpawning(SatXf);
            FNodeShuffleModule::RegisterManagedNode(Sat);
            Sat->SetActorHiddenInGame(false);
            SpawnedWellSatellites.Add(S.SatellitePath, Sat);
            S.bPlaced = true;
            ++SpawnedSats;
        }

        // The funnel, on BOTH paths and EVERY pass (fix C). On a fresh spawn it MEASURES that BeginPlay
        // registered; on a reused/adopted actor it repairs the link the save could not carry; on a
        // steady one it re-asserts, so nothing can break the link permanently and silently.
        FWellLinkOutcome Link;
        EnsureSatelliteLinked(Core, Sat, TEXT("spawn"), Link);
        // ns-review-h2 F11: THREE buckets, not two. An early return from the funnel is now counted as
        // SKIPPED, never as "registered by BeginPlay" -- the old two-way split reported the packet's
        // central assumption as measured on the exact path where nothing was observed.
        if (!Link.bRan) { ++LinkSkippedInvalid; }
        else if (Link.bRegisteredNow) { ++RegisteredByUs; }
        else { ++RegisteredByBeginPlay; }

        // The satellite is a real AFGResourceNode, so it joins the resource-node manager exactly like
        // any relocated node -- without it the extractor hologram's GetClosestNode never finds it.
        //
        // TODO (2026-07-31, ns-review-h3 H6 -- A TEST, NOT A PATCH; DO NOT "FIX" THIS BLIND).
        // RegisterNodeWithManager puts the satellite into the manager's mResourceNodes, which is the
        // list the ORDINARY extractor hologram queries. AFGResourceNodeManager also keeps separate
        // fracking-specific state, and our relocated CORE is registered in no manager list at all.
        // Whether that matters is the feature's acceptance criterion -- can a Pressurizer actually be
        // built on a relocated core -- and there is NO static proof available either way: the
        // hologram's lookup path is closed-source. So this is deliberately left as written and
        // measured by in-game test T2. Do not add a second registration on the strength of reasoning;
        // this workspace has twice written a reasoned conclusion into the cookbook and had it
        // falsified by measurement. If T2 fails, the log will name which lookup came up empty.
        RegisterNodeWithManager(Sat);
    }

    // ns-review-h2 F7: COMPLETE means every CAPTURED satellite is live and none failed. Uncaptured
    // records are excluded from the expectation (they are not part of the placed rigid body) but are
    // reported, so "this well is short by design" and "this well is short by accident" stay distinct.
    // ns-review-h4 F3: StaleInUse must be ZERO. A member that is at the wrong coordinate and cannot be
    // cleared is still counted in ReusedSats above, so without this term the group could be called
    // COMPLETE -- and completion is what suppresses the vanilla well, which is irreversible.
    const bool bComplete = (FailedSats == 0) && (SpawnedSats + ReusedSats == ExpectedSats)
                           && (ExpectedSats > 0) && IsValid(Core) && (StaleInUse == 0);
    // ns-review-h5 F-3: THROTTLED, and the text no longer lies at the second call site.
    // (a) By this function's own F7 argument the state never self-clears -- a player's machine stays
    //     built -- so an unthrottled Warning is a line every ~5 s forever. That shape was fixed one
    //     function away in DespawnStaleWellMembers and then reintroduced here; it reuses the same set.
    // (b) The old wording said "held back from COMPLETE / nothing is suppressed" unconditionally, but
    //     ApplyWellRelocation also calls this for an ALREADY-PLACED entry, and there SuppressVanillaWellGroup
    //     runs one statement later. Asserting "nothing is suppressed" immediately before suppressing is
    //     exactly the kind of confident falsehood this packet's log rules exist to prevent.
    if (StaleInUse > 0 && !WellStaleInUseLogged.Contains(E.CorePath + TEXT("|held")))
    {
        WellStaleInUseLogged.Add(E.CorePath + TEXT("|held"));
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-SPAWN core='%s': %d member(s) sit at stale coordinates and are IN USE, so they ")
            TEXT("could not be cleared. %s Said once."),
            *WellShort(E.CorePath), StaleInUse,
            E.bGroupPlaced
                ? TEXT("This group is ALREADY PLACED, so its vanilla twin remains suppressed and the group "
                       "will keep reporting *** GROUP SCATTERED *** in the audit until the player removes "
                       "what they built -- this is the state h4 F3 prevents NEW groups from entering.")
                : TEXT("The group is therefore held back from COMPLETE: nothing is suppressed, the vanilla "
                       "well stays visible, which is the correct direction to fail."));
    }

    if (SpawnedSats > 0 || FailedSats > 0 || AdoptedLate > 0 || UncapturedRefused > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-SPAWN core='%s': %d spawned, %d reused (%d adopted-late), %d FAILED, %d refused ")
            TEXT("(no capture); expected %d of %d records -> group %s. Registration -- %d by BeginPlay ")
            TEXT("(mCore set pre-FinishSpawning, as designed), %d repaired by us, %d skipped (funnel did ")
            TEXT("not run -- NOT evidence either way); purity reproduced on %d, unknown on %d."),
            *WellShort(E.CorePath), SpawnedSats, ReusedSats, AdoptedLate, FailedSats, UncapturedRefused,
            ExpectedSats, E.Satellites.Num(), bComplete ? TEXT("COMPLETE") : TEXT("INCOMPLETE"),
            RegisteredByBeginPlay, RegisteredByUs, LinkSkippedInvalid, PurityWritten, PurityUnknown);
    }

    // ns-review-h2 (deferral gap): the coordinate prints even when the group did not fully materialise.
    // It used to be gated on something having spawned, so a well that failed to materialise left the
    // tester with no coordinate at all -- for a feature whose whole test procedure is "fly to the
    // logged coordinate", that removed the log line exactly when it was most needed.
    //
    // ns-review-h4 F5: the throttle is keyed on (CorePath, PlacedCoreLocation), NOT on CorePath alone.
    // Keying on the path meant the FIRST call won -- including an INCOMPLETE one -- and every later
    // move of that group was silent for the rest of the roll, so the only coordinate a tester ever saw
    // could be one the group had since been despawned from. With no visuals shipped, that sends them
    // to empty ground and there is nothing there to reveal the mistake.
    const FString PlacedKey = E.CorePath + TEXT("@") + E.PlacedCoreLocation.ToCompactString();
    if (!WellRelocLogged.Contains(PlacedKey))
    {
        WellRelocLogged.Add(PlacedKey);
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-PLACED core='%s' res='%s' yaw=%.1f: vanilla %s -> RELOCATED TO %s (group %s this ")
            TEXT("pass). NOTE: stage H2 ships NO VISUAL for a relocated well (design §2.4) -- fly to that ")
            TEXT("coordinate to test it."),
            *WellShort(E.CorePath), *WellShort(ResourceClass->GetPathName()), E.GroupYawDeg,
            *E.VanillaCoreLocation.ToCompactString(), *E.PlacedCoreLocation.ToCompactString(),
            bComplete ? TEXT("COMPLETE") : TEXT("INCOMPLETE -- retried next pass, nothing suppressed yet"));
    }

    return bComplete;
}
