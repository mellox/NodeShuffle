// PASS B -- THE LOCATION BACKSTOP. Split out of NodeShuffleWellSweep.cpp for the 500-line rule when
// ns-review-h2-r2 F-C's instrumentation landed. Called ONLY from SweepOrphanedWellActors, and only
// when the settled phase and BOTH pass-B gates hold (ns-review-h2-r2 F-B put those gates on this call
// and nowhere else -- pass A is handle-driven and needs neither).
//
// ---- WHY IT EXISTS (ns-review-h5 F-1, HIGH/silent) ----
// The handle sweep is blind to an abandoned ACTOR THAT HOLDS NO HANDLE, and that state is reachable
// and completely silent. Our well actors are deliberately NOT RF_Transient (a machine built on one must
// survive a reload), so they are save-collected. If a group spawns INCOMPLETE and the player saves and
// quits inside that window, then on reload AdoptRestoredWellGroups skips the entry entirely -- all four
// of its loops gate on `if (!E.bGroupPlaced) continue;` -- so both maps are empty for it. The ladder
// later re-commits the destination, DespawnWellGroup FindRef's an empty map and destroys nothing, and
// the old actors remain: live, snappable, contributing rate, in no map and no entry. The audit walks
// placed entries and cannot see them; pass A walks handles and cannot see them; IsManagedSpawnedNode is
// session-scoped so it will not even suppress re-enrolment. Nothing else in the packet reports them.
//
// So this pass is driven by the WORLD: iterate the actors themselves and keep only what some entry can
// account for. SETTLED PASS ONLY -- it is a full actor iteration crossed with the layout, it must not
// run before the world has streamed (everything would look accounted-for or not, both wrongly), and the
// same iterators already run in AdoptRestoredWellGroups so this is no new cost pattern and no new
// engine symbol.
//
// ==================================================================================================
// ns-review-h5 F1 -- PASS B IS LOG-ONLY. WIP 2026-08-07, DELIBERATELY PARKED. DO NOT ARM THIS
// WITHOUT READING THE NEXT PARAGRAPH.
// ==================================================================================================
// It used to call DestroyWellMemberIfUnused on every actor it could not account for. Ungated, that
// deleted the world's vanilla wells. Gated, it is still the wrong risk to take at THIS stage, for a
// reason the gates cannot fix: the ONLY thing separating a vanilla level actor from an abandoned
// runtime one here is `IsNetStartupActor()`, an ENGINE-INTERNAL boolean evaluated during sublevel
// AddToWorld. We cannot verify from headers what it returns mid-stream for a partially-added sublevel
// -- and this workspace has had THREE reasoned predictions about engine internals falsified by
// measurement in one week. One false negative there deletes a resource well permanently, and there is
// no undo in a player's save.
//
// So the backstop REPORTS and destroys nothing. That costs the h5 F-1 reclamation (an abandoned actor
// holding no handle stays in the world) and buys back the whole node-destroyer risk class.
// WHAT WOULD ARM IT: the SaveGame identity component (a UNodeShuffleWellComponent carrying a group id),
// which answers "is this actor ours" DIRECTLY instead of geometrically. Until that exists, arming this
// trades a certain, silent, unrecoverable failure mode for an uncertain benefit.
// TODO(2026-08-07, H2b): re-evaluate ONLY after the SaveGame identity component lands.
//
// ==================================================================================================
// ns-review-h2-r2 F-C (HIGH, evidence integrity) -- THIS PASS MUST BE ABLE TO MEASURE ITS OWN
// LOAD-BEARING ASSUMPTION. That is the entire reason this file has more counters than code.
// ==================================================================================================
// The previous build could not. It had ONE exclusion counter (`BackstopKnownPath`), incremented BEFORE
// the gate, so it conflated "gate 2 excluded it" with "gate 1 excluded it" and there was no counter at
// all for `IsNetStartupActor`. The runtime checklist asked a tester to conclude "if M is large,
// IsNetStartupActor is returning false for level actors" -- a conclusion the log could not support.
//
// Worse, the two gates are ANTI-CORRELATED. `KnownLayoutPaths` is built from WellLayout, which is a
// MERGE over the census: only wells streamed during some roll are ever added. A vanilla well in a
// region the player has never visited at roll time is therefore NOT in KnownLayoutPaths -- and that is
// exactly the sublevel-AddToWorld window in which gate 1 is suspect. So for an unenrolled, freshly
// streaming vanilla well there is only ONE layer, and it is the assumed one. That is not fixed here
// (it would need gate 2 rebuilt from a live census read -- see the handoff); it is MEASURED here, so
// the decision to fix it is made on numbers.
//
// FIVE POPULATIONS, each separately counted, and they sum to the total iterated:
//   levelActorOnly  -- gate 1 alone excluded it        (a vanilla well gate 2 does NOT know about)
//   knownPathOnly   -- gate 2 alone excluded it        (gate 1 said "not a level actor" about a well
//                                                       the layout knows -- IF THIS IS NON-ZERO,
//                                                       IsNetStartupActor has produced a FALSE NEGATIVE
//                                                       on a vanilla well and pass B must never be armed
//                                                       as designed)
//   bothGates       -- both excluded it                (the healthy, expected case for vanilla wells)
//   examined        -- passed both gates, then matched a handle or an accounted-for coordinate
//   unaccounted     -- passed both gates and nothing accounts for it  <-- the RT-6 number
//
// And each unaccounted actor is reported with its FULL object path plus TWO horizontal distances:
// to the nearest VANILLA well position the layout knows, and to the nearest DESTINATION the layout has
// dealt or placed. A stranded actor of OURS sits at a dealt destination; a mis-classified vanilla well
// sits on top of a vanilla position. That one field is what makes RT-6's count self-classifying instead
// of ambiguous between "the stranded-actor class is real" and "IsNetStartupActor returned false".

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleWellCensus.h"   // AFGResourceNodeFrackingCore / ...Satellite
#include "NodeShuffleWellRetype.h"   // WellPathOf / WellShort
#include "NodeShuffleWellRelocate.h" // IsFiniteVector, WellAdoptMatchRadiusCm, RotateWellOffsetXY
#include "EngineUtils.h"             // TActorIterator -- the ONE world scan in the packet's teardown
#include "Engine/Level.h"            // ns-review-h2-r3 F-2: ULevel, for the `level=` discriminator
#include "Engine/World.h"            // ... and UWorld::PersistentLevel, which it is compared against

namespace
{
    // Horizontal distance only, and deliberately so: a satellite's VANILLA position is DERIVED here
    // (core + rotated rigid-body offset) and its Z was never recorded -- every relocated member
    // re-settles its own Z on new terrain, which is why the packet stores LocalOffset's Z as a
    // capture-time record only. Comparing a Z we do not have would put noise into the one number RT-6
    // reads. Returns -1.0 for an empty candidate set, which the log prints as "n/a".
    double NearestXY(const FVector& From, const TArray<FVector>& Candidates)
    {
        double Best = -1.0;
        for (const FVector& V : Candidates)
        {
            const double D = FVector2D(From.X - V.X, From.Y - V.Y).Size();
            if (Best < 0.0 || D < Best) { Best = D; }
        }
        return Best;
    }
}

void ANodeShuffleSubsystem::RunWellLocationBackstop(const TCHAR* Phase, int32& OutExamined,
                                                    int32& OutUnaccounted)
{
    OutExamined = 0;
    OutUnaccounted = 0;

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-ORPHAN [backstop LOG-ONLY, %s]: no world -- pass B did not run. Nothing was ")
            TEXT("examined and nothing was destroyed (this pass never destroys anything in this build)."),
            Phase);
        return;
    }

    const float MatchSq = FMath::Square(WellAdoptMatchRadiusCm);

    // ---- Build the three reference sets from the layout, in one walk. ----
    TSet<FString> KnownLayoutPaths;   // GATE 2's set: every core/satellite path the layout knows
    TArray<FVector> AccountedFor;     // committed placements: an actor here belongs to a live claim
    TArray<FVector> VanillaPoints;    // F-C: where the layout says VANILLA wells are
    TArray<FVector> DestinationPoints;// F-C: where the layout has dealt or placed OUR wells
    for (const FNodeShuffleWellEntry& E : WellLayout)
    {
        KnownLayoutPaths.Add(E.CorePath);
        for (const FNodeShuffleWellSatellite& S : E.Satellites) { KnownLayoutPaths.Add(S.SatellitePath); }

        // A3: "does this entry claim a coordinate" is READ, not inferred from the coordinate being
        // non-zero. Same set today (INVARIANT A3 makes them equivalent, and ValidateWellClaimInvariant
        // proves that in the log every sweep) -- but this set decides what counts as accounted-for, and
        // it must not be the last place in the packet that re-derives the claim from a coordinate.
        if (E.bPlacementClaimLive && IsFiniteVector(E.PlacedCoreLocation)
            && !E.PlacedCoreLocation.IsNearlyZero())
        {
            AccountedFor.Add(E.PlacedCoreLocation);
            DestinationPoints.Add(E.PlacedCoreLocation);
        }
        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            if (E.bPlacementClaimLive && IsFiniteVector(S.PlacedLocation)
                && !S.PlacedLocation.IsNearlyZero())
            {
                AccountedFor.Add(S.PlacedLocation);
                DestinationPoints.Add(S.PlacedLocation);
            }
        }
        // The CURRENTLY DEALT destination is NOT an accounted-for coordinate (nothing has been placed
        // there yet), but it IS where a stranded actor of ours would have been spawned by an INCOMPLETE
        // spawn that was then abandoned -- which is precisely the RT-6 scenario.
        if (IsFiniteVector(E.DestCoreLocation) && !E.DestCoreLocation.IsNearlyZero())
        {
            DestinationPoints.Add(E.DestCoreLocation);
        }

        if (IsFiniteVector(E.VanillaCoreLocation) && !E.VanillaCoreLocation.IsNearlyZero())
        {
            VanillaPoints.Add(E.VanillaCoreLocation);
            for (const FNodeShuffleWellSatellite& S : E.Satellites)
            {
                if (!S.bCaptured) { continue; } // no rigid-body offset: rotating a zero re-probes the core
                const FVector2D RotXY = RotateWellOffsetXY(S.LocalOffset, E.VanillaCoreYawDeg);
                VanillaPoints.Add(FVector(E.VanillaCoreLocation.X + RotXY.X,
                                          E.VanillaCoreLocation.Y + RotXY.Y,
                                          E.VanillaCoreLocation.Z));
            }
        }
    }

    // ns-review-h2-r3 F-2 -- THE WITHDRAWN CLAIMS OF THIS SESSION, folded in as destinations.
    // Without this the discriminator's "ours" branch was ERASED AT THE MOMENT IT BECAME TRUE: the three
    // fields DestinationPoints is built from are all zeroed by the events that strand an actor, so at the
    // instant an actor became strandable the coordinate identifying it as ours had just been deleted.
    for (const FVector& V : AbandonedWellClaimCoords)
    {
        if (IsFiniteVector(V) && !V.IsNearlyZero()) { DestinationPoints.Add(V); }
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

    // ns-review-h2-r2 F-C: the five populations. Iterated == LevelActorOnly + KnownPathOnly + BothGates
    // + Examined, by construction, and the summary line asserts that sum so a future edit that drops an
    // actor into no bucket is visible in the log rather than silent.
    int32 Iterated = 0, LevelActorOnly = 0, KnownPathOnly = 0, BothGates = 0;
    int32 Examined = 0, Unaccounted = 0, InUse = 0;
    int32 HeldByUs = 0, StrandedAtOurClaim = 0;
    int32 ProvenOursNoHandle = 0; // ROUND 9 §3b-B: unaccounted AND in the spawn-time registry
    int32 LiveCores = 0, LiveSats = 0;
    FString Detail;

    const ULevel* const PersistentLevel = World->PersistentLevel;

    // ONE examination routine for both actor classes, so the two cannot drift apart in which gates they
    // apply -- the two-copies-of-a-rule shape this packet has already been bitten by three times
    // (ns-review-h2 F3, h3's two health rules, h4 F4's two teardowns, and F-D's three IsAtTarget copies).
    const auto Examine = [&](AFGResourceNodeBase* A) -> void
    {
        if (!IsValid(A)) { return; }
        ++Iterated;

        // GATE 1 (engine-internal, ASSUMED not proven): a level actor is never ours.
        // GATE 2 (ours, statically provable): a path the layout knows is a vanilla member, never ours.
        const bool bLevelActor = A->IsNetStartupActor();
        const FString Path = WellPathOf(A);
        const bool bKnownPath = KnownLayoutPaths.Contains(Path);
        if (bLevelActor || bKnownPath)
        {
            // F-C(a): counted SEPARATELY and AFTER the gates, so "gate 1 excluded N" and "gate 2
            // excluded N" are distinguishable. The previous build incremented one counter BEFORE the
            // gate and had none for gate 1 at all, which is why RT-3 asked for a conclusion the log
            // could not support.
            if (bLevelActor && bKnownPath)  { ++BothGates; }
            else if (bLevelActor)           { ++LevelActorOnly; }
            else                            { ++KnownPathOnly; }
            return;
        }

        ++Examined;

        // ==========================================================================================
        // ns-review-h2-r3 F-1 (HIGH, evidence integrity) -- "ACCOUNTED FOR" WAS ABSORBING THE VERY
        // ACTORS RT-6 EXISTS TO COUNT, AND THEN REPORTING unaccounted=0 AS A CLEAN RESULT.
        // ==========================================================================================
        // The old line was `if (Held.Contains(A) || IsAccountedFor(A)) return;` -- one gate for two
        // completely different questions:
        //   Held         -- "WE HOLD A HANDLE TO THIS ACTOR." We know about it; nothing is lost.
        //   IsAccountedFor -- "SOME ENTRY CLAIMS THIS SPOT." Says nothing at all about the ACTOR.
        // The class this pass was built for (ns-review-h5 F-1) is exactly "an actor of ours holding NO
        // HANDLE": a group spawns INCOMPLETE, the player saves and quits inside that window, and on
        // reload AdoptRestoredWellGroups skips the entry (all four of its loops gate on bGroupPlaced),
        // so both handle maps are empty for it. But the ENTRY still claims the coordinate -- correctly,
        // it is mid-assembly and will retry there -- and the stranded actors are standing ON it. So
        // IsAccountedFor returned true, they were counted as Examined, and they NEVER REACHED
        // Unaccounted. RT-6 would then read 0 in the exact scenario it prescribes and step 8's own rule
        // ("0 across a few sessions means log-only is a defensible permanent answer") would license
        // doing nothing about a class that was real and present in every one of those sessions.
        //
        // That is the round-7 instance of this packet's recurring shape: a thing abandoned somewhere
        // invisible, with the acceptance gate reporting healthy. The gate now separates the questions.
        // A CLAIM ON A COORDINATE IS NOT KNOWLEDGE OF AN ACTOR -- which is precisely what A3 makes
        // sayable: the entry owning a coordinate and us holding a handle to what stands on it are two
        // different facts, and only the second one means the actor is accounted for.
        if (Held.Contains(A)) { ++HeldByUs; return; }

        const bool bAtOurClaim = IsAccountedFor(A);
        ++Unaccounted;                        // <-- the RT-6 number. Both sub-cases count.
        if (bAtOurClaim) { ++StrandedAtOurClaim; }

        // ==========================================================================================
        // ns-review-h2-r4 ROUND 9 §3b-B -- THE SPAWN-TIME REGISTRY, THE ONLY SOUND POSITIVE HERE.
        // ==========================================================================================
        // Every other field on this line is either a distance to a coordinate or `level=`, and round 9
        // established that the whole ladder can FALSIFY "ours" soundly but cannot AUTHORISE it: the
        // positive rests on "CSS authors every fracking well into a streaming sublevel", a fact about
        // somebody else's map. This one bit rests on our own SpawnActorDeferred call and nothing else.
        // TWeakObjectPtr, so a destroyed-and-recycled address can never answer true by accident.
        const bool bSpawnedByUs = WellActorsSpawnedThisSession.Contains(A);
        if (bSpawnedByUs) { ++ProvenOursNoHandle; }

        const TCHAR* Why = TEXT("");
        const bool bInUse = IsWellMemberInUse(A, Why);
        if (bInUse) { ++InUse; }
        if (Unaccounted <= 5) // name the first few in full; the totals are in the summary line
        {
            const FVector Loc = A->GetActorLocation();
            const double DVan = NearestXY(Loc, VanillaPoints);
            const double DDst = NearestXY(Loc, DestinationPoints);
            const double Radius = static_cast<double>(WellAdoptMatchRadiusCm);

            // ns-review-h2-r3 F-2 -- A COMPUTED VERDICT, NOT TWO UNBOUNDED NUMBERS AND A HUMAN EYEBALL.
            // The previous build printed nearestVanillaXY / nearestDealtXY with NO THRESHOLD and asked
            // the tester to judge "near". Both of its branches were dead: the vanilla one is unreachable
            // (gate 2 excludes every path the layout knows BEFORE this point, and VanillaPoints is built
            // only from layout entries, so an actor that gets here is by construction one whose position
            // the layout does not know), and the dealt one was erased by this packet's own claim
            // withdrawal. The withdrawn coordinates are now remembered (see above), which revives the
            // dealt branch; the vanilla branch is still structurally near-unreachable and is therefore
            // NOT the discriminator -- `level=` is.
            //
            // `level=` IS THE WORKING DISCRIMINATOR AND IT IS NOT PROVEN. A vanilla well is authored
            // into the map and arrives with its streaming sublevel; SpawnWellGroup passes no
            // OverrideLevel, so ours land in the persistent level. That makes `persistentLevel=0` strong
            // evidence of a level-placed actor -- but "Satisfactory authors its fracking wells into
            // sublevels" is a fact about somebody else's map, unverifiable from our headers. So it is
            // REPORTED and used only as a verdict HINT; it is never a gate and nothing destructive
            // reads it. RT-6 tells the tester to read it, which is the point round 7 made: the one
            // discriminating field was already being printed and no test step named it.
            const ULevel* const Lvl = A->GetLevel();
            const bool bPersistent = (Lvl != nullptr && Lvl == PersistentLevel);
            const FString LevelName = Lvl ? Lvl->GetOutermost()->GetName() : FString(TEXT("<none>"));

            // ns-review-h2-r4 F-4 -- ORDERING. h2-8 tested `bAtOurClaim` FIRST and `!bPersistent`
            // THIRD, i.e. it put the only field the code itself calls "never dead" behind two distance
            // tests. A vanilla well that was not streamed when the census ran is not in the layout, so
            // a destination can be validated on top of it; it then passes gate 2 (unknown path) and, if
            // IsNetStartupActor returns false for it, gate 1 -- and being within 300 cm of a live claim
            // of ours it was reported `OURS-STRANDED (... MEASURED)`. Step 8 tells the tester that
            // verdict means "real evidence, build the identity component". What they actually found is
            // the gate-1 false negative that means PASS B MUST NEVER BE ARMED. Opposite conclusions
            // from one line, and that line is the deliverable two rounds have failed to produce.
            //
            // So `!bPersistent` is hoisted, AND the both-true case gets its own compound verdict rather
            // than silently resolving to one of them: an actor that is on our claim AND in a streaming
            // sublevel is a CONTRADICTION, and naming it as one is strictly more informative than
            // either branch alone.
            //
            // ns-review-h2-r4 ROUND 9 §3b-B -- `bSpawnedByUs` IS TESTED FIRST, AND IT IS THE ONLY
            // BRANCH HERE THAT IS PROOF RATHER THAN EVIDENCE. It is above the contradiction branch
            // deliberately: if the registry says we spawned this actor, then a `persistentLevel=0`
            // reading is not a contradiction to puzzle over -- it is a MEASURED gate-1/level-premise
            // failure, and that is a strictly more useful thing to print than "CONTRADICTION".
            const TCHAR* Verdict =
                  (bSpawnedByUs && !bPersistent)   ? TEXT("OURS-PROVEN-AND-NOT-PERSISTENT (*** WE SPAWNED THIS ACTOR THIS SESSION (spawn-time registry) AND IT READS AS NOT IN THE PERSISTENT LEVEL. The registry is proof; `level=` is a premise. So the LEVEL PREMISE IS FALSIFIED -- persistentLevel=0 does NOT imply vanilla. Every VANILLA-SUSPECT verdict in this session is now UNSAFE and pass B must never be armed on `level=` ***)")
                : bSpawnedByUs                     ? TEXT("OURS-PROVEN (we spawned this actor THIS SESSION and then dropped the handle -- the h5 F-1 stranded class, PROVED from our own spawn call with no engine premise, no distance threshold and no save round-trip. This is authorising evidence for the SaveGame identity component; ns-review-h2-r4 round 9 alternative 3b-B)")
                : (bAtOurClaim && !bPersistent)    ? TEXT("OURS-STRANDED-BUT-NOT-PERSISTENT (*** CONTRADICTION: standing on a LIVE claim of ours AND authored into a streaming sublevel. Ours spawn with no OverrideLevel, so this is a gate-1 FALSE NEGATIVE over a vanilla well, NOT the h5 F-1 class -- do NOT arm pass B and do NOT count this as evidence for the identity component ***)")
                : !bPersistent                     ? TEXT("VANILLA-SUSPECT (not in the persistent level -- gate 1 FALSE NEGATIVE, do not arm pass B)")
                : bAtOurClaim                      ? TEXT("OURS-STRANDED-CROSS-RELOAD-SUSPECT (no handle, standing on a LIVE claim of ours, IN THE PERSISTENT LEVEL where ours spawn -- BUT THE SPAWN-TIME REGISTRY DOES NOT KNOW IT, which is PROOF it was not spawned this session. So it is either ours from a PREVIOUS session -- the cross-reload half only the SaveGame identity component can settle -- or a vanilla well the level premise mis-sorts. NOT sufficient on its own to authorise the identity component; ns-review-h2-r4 round 9 P-20)")
                : (DDst >= 0.0 && DDst < Radius)   ? TEXT("OURS-ABANDONED (within the adopt radius of a dealt or WITHDRAWN destination -- core AND satellite coordinates since ns-review-h2-r4 F-5)")
                : (DVan >= 0.0 && DVan < Radius)   ? TEXT("VANILLA-SUSPECT (sitting on a vanilla position the layout knows)")
                :                                    TEXT("AMBIGUOUS (read level= and path= by hand; neither distance is inside the adopt radius)");

            Detail += FString::Printf(
                TEXT(" [%s VERDICT=%s path='%s' @%s level='%s' persistentLevel=%d spawnedThisSession=%d ")
                TEXT("nearestVanillaXY=%s nearestDealtXY=%s adoptRadius=%.0fcm%s%s]"),
                *A->GetName(), Verdict, *Path, *Loc.ToCompactString(), *LevelName, bPersistent ? 1 : 0,
                bSpawnedByUs ? 1 : 0,
                DVan < 0.0 ? TEXT("n/a") : *FString::Printf(TEXT("%.0fcm"), DVan),
                DDst < 0.0 ? TEXT("n/a") : *FString::Printf(TEXT("%.0fcm"), DDst), Radius,
                bInUse ? TEXT(" IN-USE:") : TEXT(""), bInUse ? Why : TEXT(""));
        }
    };

    for (TActorIterator<AFGResourceNodeFrackingCore> It(World); It; ++It)      { ++LiveCores; Examine(*It); }
    for (TActorIterator<AFGResourceNodeFrackingSatellite> It(World); It; ++It) { ++LiveSats;  Examine(*It); }

    OutExamined = Examined;
    OutUnaccounted = Unaccounted;

    // ---- F-C(1) + F-C(3): the measurement line. ALWAYS emitted when the pass runs, because RT-3 needs
    // the four gate populations even on a clean pass -- the previous build printed the clean case once
    // per session and threw the numbers away for every later pass. Throttled only in the sense that the
    // pass itself runs at the settled phase.
    const int32 BucketSum = LevelActorOnly + KnownPathOnly + BothGates + Examined;
    // ================================================================================================
    // ns-review-h2-r4 F-8 -- THE SECOND SUM WAS VACUOUS. THIS ONE CAN ACTUALLY FAIL.
    // ================================================================================================
    // h2-8 asserted `Examined == HeldByUs + Unaccounted` and the commit message called it a guard
    // against "a future edit that drops an actor into no bucket". It was neither: `++Examined` is
    // followed immediately by exactly one of `++HeldByUs` / `++Unaccounted` on every path, so the
    // equality held BY CONSTRUCTION and was arithmetically incapable of failing. (`BucketSum ==
    // Iterated` is by-construction in the same way -- one `++Iterated`, then exactly one bucket. It is
    // kept because it is pre-existing and cheap, but it is decoration, not a check, and the line below
    // now says so rather than presenting three equal-weight verdicts.)
    //
    // THE ONE THAT CAN DIVERGE was sitting right there unwritten: LiveCores / LiveSats are incremented
    // by the two TActorIterator loops BEFORE Examine() runs, while Iterated is incremented INSIDE
    // Examine AFTER its `if (!IsValid(A)) return;`. So any iterated actor that fails IsValid makes
    // LiveCores + LiveSats EXCEED Iterated -- a real, reachable divergence that nothing asserted,
    // while the census line printed "Iterated %d ... (%d core(s) + %d satellite(s))" as though the
    // three numbers agreed. They are not the same measurement and now the log says which is which.
    const int32 IterSum = LiveCores + LiveSats;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-BACKSTOP [%s]: GATE CENSUS (ns-review-h2-r2 F-C -- this line is the ONLY evidence ")
        TEXT("that IsNetStartupActor works, and RT-3/RT-6 are unreadable without it). Iterated %d live ")
        TEXT("fracking actor(s) (%d core(s) + %d satellite(s)). Excluded: %d by IsNetStartupActor ONLY ")
        TEXT("(gate 1, ENGINE-INTERNAL and ASSUMED), %d by the layout-path cross-check ONLY (gate 2, ")
        TEXT("ours and provable), %d by BOTH. Passed both gates: %d examined -> %d HELD BY US (a handle ")
        TEXT("points at it) + %d UNACCOUNTED (%d of those are STRANDED ON A LIVE CLAIM OF OURS -- ")
        TEXT("ns-review-h2-r3 F-1: these used to be silently counted as accounted-for and are the whole ")
        TEXT("RT-6 class; %d in use by a player). SPAWN-TIME REGISTRY (ns-review-h2-r4 round 9 3b-B): ")
        TEXT("%d actor(s) recorded as spawned by us THIS SESSION, of which %d are among the unaccounted ")
        TEXT("and therefore PROVABLY OURS with no engine premise. IF THAT FIRST NUMBER IS 0, the ")
        TEXT("registry has recorded nothing and `spawnedThisSession=0` on the verdict lines below ")
        TEXT("proves NOTHING -- check whether any group spawned this session before reading it as ")
        TEXT("'not ours'. Sums: gates %d vs iterated %d (%s); ")
        TEXT("liveCores+liveSats %d vs iterated %d (%s). Layout coverage: %d entry(ies), %d known path(s) vs %d live ")
        TEXT("core(s) in the world -- gate 2 can only cover wells the layout has ever seen, so a ")
        TEXT("shortfall here is the exact window gate 1 is suspect in. NOTHING WAS DESTROYED. ")
        TEXT("(ns-review-h2-r4 F-8: the old `examined vs held+unaccounted` sum was VACUOUS and has been ")
        TEXT("REPLACED by `liveCores+liveSats vs iterated`, which genuinely diverges when an iterated ")
        TEXT("actor fails IsValid.)"),
        Phase, Iterated, LiveCores, LiveSats,
        LevelActorOnly, KnownPathOnly, BothGates,
        Examined, HeldByUs, Unaccounted, StrandedAtOurClaim, InUse,
        WellActorsSpawnedThisSession.Num(), ProvenOursNoHandle,
        BucketSum, Iterated, (BucketSum == Iterated) ? TEXT("by construction, DECORATION not a check -- ns-review-h2-r4 F-8") : TEXT("*** MISMATCH: a population is uncounted ***"),
        IterSum, Iterated, (IterSum == Iterated) ? TEXT("OK") : TEXT("*** MISMATCH: an ITERATED ACTOR FAILED IsValid -- the class counts and the examined count disagree ***"),
        WellLayout.Num(), KnownLayoutPaths.Num(), LiveCores);

    // GATE 1 FALSE NEGATIVE, CALLED OUT BY NAME. If gate 2 alone excluded an actor, then gate 1 said
    // "this is not a level actor" about a well the LAYOUT knows is vanilla. Gate 2 caught it here, but
    // gate 2 provably cannot cover a vanilla well the layout has never seen -- so this is direct
    // evidence that an armed pass B would eventually delete somebody's well.
    if (KnownPathOnly > 0)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-BACKSTOP [%s]: *** IsNetStartupActor FALSE NEGATIVE *** -- %d vanilla well ")
            TEXT("member(s) the LAYOUT knows were NOT recognised as level actors by gate 1. Gate 2 ")
            TEXT("caught them, but gate 2 is built from WellLayout and therefore cannot cover a vanilla ")
            TEXT("well that has never streamed during a roll. REPORT THIS BEFORE ANYONE ARMS PASS B: it ")
            TEXT("is the falsification of INV-3's assumption, measured rather than reasoned."),
            Phase, KnownPathOnly);
    }

    if (Unaccounted > 0)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-ORPHAN [backstop LOG-ONLY, %s]: %d RUNTIME fracking actor(s) passed both gates ")
            TEXT("and WE HOLD NO HANDLE TO THEM (%d of them are standing on a coordinate an entry still ")
            TEXT("claims -- ns-review-h2-r3 F-1, these were previously absorbed into 'accounted for' and ")
            TEXT("are the exact class RT-6 exists to count; %d in use by a player). NOTHING WAS ")
            TEXT("DESTROYED -- this backstop reports only (WIP 2026-08-07, ns-review-h5 F1). EACH ACTOR ")
            TEXT("BELOW CARRIES A COMPUTED VERDICT. READ THEM ASYMMETRICALLY (ns-review-h2-r4 round 9 ")
            TEXT("P-20): the NEGATIVES are sound and the POSITIVES mostly are not. OURS-PROVEN / ")
            TEXT("OURS-PROVEN-AND-NOT-PERSISTENT => the spawn-time registry says WE SPAWNED IT; this is ")
            TEXT("PROOF, and it is the only authorising evidence on this line for the SaveGame identity ")
            TEXT("component. OURS-STRANDED-CROSS-RELOAD-SUSPECT / OURS-ABANDONED => suggestive only, ")
            TEXT("because both rest on 'CSS authors wells into sublevels', which is unverifiable from ")
            TEXT("our headers. VANILLA-SUSPECT => gate 1 mis-classified a vanilla well and PASS B MUST ")
            TEXT("NEVER BE ARMED AS DESIGNED (a sound NEGATIVE, full confidence). AMBIGUOUS => read ")
            TEXT("level=, path= and spawnedThisSession= by hand. First %d:%s"),
            Phase, Unaccounted, StrandedAtOurClaim, InUse, FMath::Min(Unaccounted, 5), *Detail);
    }
    else if (!bWellBackstopLogOnlyLogged)
    {
        bWellBackstopLogOnlyLogged = true;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-ORPHAN [backstop LOG-ONLY, %s]: clean -- %d runtime fracking actor(s) passed ")
            TEXT("both gates and WE HOLD A HANDLE TO EVERY ONE (%d held). ns-review-h2-r3 F-1: 'clean' ")
            TEXT("now means we KNOW ABOUT the actor, not merely that some entry claims the spot it ")
            TEXT("stands on -- the old test conflated those and could not report a stranded actor at ")
            TEXT("all. This backstop never destroys anything in this build. The GATE CENSUS line above ")
            TEXT("carries the numbers RT-3 needs and is NOT throttled. Said once."),
            Phase, Examined, HeldByUs);
    }
}
