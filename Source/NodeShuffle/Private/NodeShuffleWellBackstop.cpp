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

        if (IsFiniteVector(E.PlacedCoreLocation) && !E.PlacedCoreLocation.IsNearlyZero())
        {
            AccountedFor.Add(E.PlacedCoreLocation);
            DestinationPoints.Add(E.PlacedCoreLocation);
        }
        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            if (IsFiniteVector(S.PlacedLocation) && !S.PlacedLocation.IsNearlyZero())
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
    int32 LiveCores = 0, LiveSats = 0;
    FString Detail;

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
        if (Held.Contains(A) || IsAccountedFor(A)) { return; }

        ++Unaccounted;
        const TCHAR* Why = TEXT("");
        const bool bInUse = IsWellMemberInUse(A, Why);
        if (bInUse) { ++InUse; }
        if (Unaccounted <= 5) // name the first few in full; the totals are in the summary line
        {
            const FVector Loc = A->GetActorLocation();
            const double DVan = NearestXY(Loc, VanillaPoints);
            const double DDst = NearestXY(Loc, DestinationPoints);
            // F-C(2): the full object path plus BOTH distances. This is what makes the RT-6 number
            // self-classifying: near a DEALT DESTINATION => a genuinely stranded actor of ours (the
            // thing being measured); near a VANILLA POSITION => IsNetStartupActor returned false for a
            // level actor and pass B must NEVER be armed as designed.
            Detail += FString::Printf(
                TEXT(" [%s path='%s' @%s nearestVanillaXY=%s nearestDealtXY=%s%s%s]"),
                *A->GetName(), *Path, *Loc.ToCompactString(),
                DVan < 0.0 ? TEXT("n/a") : *FString::Printf(TEXT("%.0fcm"), DVan),
                DDst < 0.0 ? TEXT("n/a") : *FString::Printf(TEXT("%.0fcm"), DDst),
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
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-BACKSTOP [%s]: GATE CENSUS (ns-review-h2-r2 F-C -- this line is the ONLY evidence ")
        TEXT("that IsNetStartupActor works, and RT-3/RT-6 are unreadable without it). Iterated %d live ")
        TEXT("fracking actor(s) (%d core(s) + %d satellite(s)). Excluded: %d by IsNetStartupActor ONLY ")
        TEXT("(gate 1, ENGINE-INTERNAL and ASSUMED), %d by the layout-path cross-check ONLY (gate 2, ")
        TEXT("ours and provable), %d by BOTH. Passed both gates: %d examined -> %d unaccounted (%d in ")
        TEXT("use). Bucket sum %d vs iterated %d (%s). Layout coverage: %d entry(ies), %d known path(s) ")
        TEXT("vs %d live core(s) in the world -- gate 2 can only cover wells the layout has ever seen, ")
        TEXT("so a shortfall here is the exact window gate 1 is suspect in. NOTHING WAS DESTROYED."),
        Phase, Iterated, LiveCores, LiveSats,
        LevelActorOnly, KnownPathOnly, BothGates,
        Examined, Unaccounted, InUse,
        BucketSum, Iterated, (BucketSum == Iterated) ? TEXT("OK") : TEXT("*** MISMATCH: a population is uncounted ***"),
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
            TEXT("WELLH2-ORPHAN [backstop LOG-ONLY, %s]: %d RUNTIME fracking actor(s) are in no handle ")
            TEXT("map and no layout entry accounts for their position (%d in use by a player). NOTHING ")
            TEXT("WAS DESTROYED -- this backstop reports only (WIP 2026-08-07, ns-review-h5 F1). ")
            TEXT("CLASSIFY EACH BY ITS TWO DISTANCES: near nearestDealtXY => a genuinely stranded actor ")
            TEXT("of ours, which is real evidence for the SaveGame identity component; near ")
            TEXT("nearestVanillaXY => gate 1 mis-classified a vanilla well and pass B must never be ")
            TEXT("armed as designed. First %d:%s"),
            Phase, Unaccounted, InUse, FMath::Min(Unaccounted, 5), *Detail);
    }
    else if (!bWellBackstopLogOnlyLogged)
    {
        bWellBackstopLogOnlyLogged = true;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-ORPHAN [backstop LOG-ONLY, %s]: clean -- %d runtime fracking actor(s) passed ")
            TEXT("both gates and every one is accounted for. This backstop never destroys anything in ")
            TEXT("this build. The GATE CENSUS line above carries the numbers RT-3 needs and is NOT ")
            TEXT("throttled. Said once."),
            Phase, Examined);
    }
}
