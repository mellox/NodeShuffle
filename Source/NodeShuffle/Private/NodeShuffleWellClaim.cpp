// THE PLACEMENT CLAIM (A3) -- the fact itself, split out of NodeShuffleWellSweep.cpp.
// ================================================================================================
// ns-review-h2-r4 §4C (the eighth cold review) and the 500-line rule. Sweep.cpp had reached 620
// lines, and the seam the reviewer endorsed is the real distinction A3 introduced:
//   * THIS FILE -- the CLAIM. Who owns a coordinate, when that ownership starts, when it ends, and
//     the enforcement that the stored fact and the coordinate never disagree.
//   * NodeShuffleWellSweep.cpp -- the SWEEP THAT READS IT. Ownership consumers only.
// Round 8's amendment, taken: ReconcileAbandonedWellClaims moved here TOO. It is a WRITER of the
// claim, not a reader, and leaving the packet's last admitted derivation sitting among the readers
// re-creates the exact adjacency A3 was built to break.
//
// A3 in one paragraph (the full argument is on FNodeShuffleWellEntry::bPlacementClaimLive):
// "this entry has abandoned its coordinate" used to be INFERRED, independently at every reader, from
// bGroupPlaced / bRelocate / bRelocationFailed / Placed*-being-non-zero, and seven review rounds each
// found a new state tuple some reader mis-read. The claim is now a STORED FACT: ONE setter (the
// footprint commit in NodeShuffleWellRelocateApply.cpp), ONE clearer (ClearAbandonedWellPlacement,
// below), every reader asks. A3 does NOT decide when a claim ends -- ReconcileAbandonedWellClaims
// still derives that, on purpose, because its job is to NOTICE abandonments nobody instrumented; the
// win is that it WRITES the fact once instead of every reader re-deriving it. It says nothing about
// actor IDENTITY, which is pass B / IsNetStartupActor / the deferred SaveGame identity component.
//
// WHAT ROUND 8 CHANGED HERE (each one is structural, not cosmetic):
// * F-2 -- A3 DELETED A SELF-HEAL AND SHIPPED NO PARITY ROW FOR IT. Pre-A3 the reconciliation
//   repaired a stale non-zero coordinate by READING THE COORDINATE, so a wrong bool could not defeat
//   it. A3 put `if (!bPlacementClaimLive) continue/return` in front of both halves, and h2-7 F-A's
//   exact state became DETECTED THREE WAYS AND REPAIRED BY NOTHING -- while the migration "repaired"
//   it in the OPPOSITE polarity. Restored below, LOUDLY (the principle forbids SILENT repair).
// * D-2 -- A CLAIM WITH NO EXPIRY, round 8's predicted round-9 finding, fixed in advance. See the
//   block inside ReconcileAbandonedWellClaims.
// * D-1 -- THE MIGRATION WAS THE OLD DERIVATION, UNLABELLED AND UNGATED. Now version-gated, renamed,
//   and logging on BOTH branches so its absence can never be mistaken for a pass (F-3).
// * F-7 -- INVARIANT A3 as CHECKED was cores only while as DEFINED it covered satellites too. The
//   satellite half is the half whose asymmetry WAS h2-6 F-A. Checked now.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleWellRetype.h" // WellShort

// F-2 -- "DOES THIS ENTRY NAME A COORDINATE?", asked of the COORDINATES and not of the flag.
// This is the whole point of the restored self-heal: the repair path must not be defeatable by the
// one bool it exists to correct. Satellites count, because h2-6 F-A was precisely the group whose
// core and satellites landed on two different verdicts.
static bool WellEntryNamesAnyPlacedCoordinate(const FNodeShuffleWellEntry& E)
{
    if (!E.PlacedCoreLocation.IsNearlyZero()) { return true; }
    for (const FNodeShuffleWellSatellite& S : E.Satellites)
    {
        if (!S.PlacedLocation.IsNearlyZero()) { return true; }
    }
    return false;
}

// ------------------------------------------------------------------------------------------------
// ns-review-h5 F2 -- DROP A PLACEMENT THE ENTRY NO LONGER OWNS  (the ROOT-CAUSE half)
// ------------------------------------------------------------------------------------------------
// PlacedCoreLocation / PlacedLocation are the entry's claim on a coordinate, and nothing ever
// WITHDREW that claim: the roll's refusal `continue`s clear bRelocate and leave the coordinates
// behind, and the escalation ladder rewrites DestCoreLocation while leaving Placed* pointing at the
// destination it just abandoned. IsAtTarget() then kept answering "mid-assembly: OWNED" for pinned,
// failed and refused entries FOREVER -- a confident falsehood from the one function whose job is to
// disbelieve the others.
//
// A PLACED GROUP IS NEVER TOUCHED. bGroupPlaced true means the entry owns those coordinates and
// SpawnWellGroup, AdoptRestoredWellGroups, EnsureSatelliteLinked, DespawnStaleWellMembers and
// SuppressVanillaWellGroup all read them; clearing there would strand a working well instantly.
//
// A3: THIS IS THE SINGLE CLEARER of bPlacementClaimLive, and the only function in the packet that
// zeroes Placed*. Readers no longer re-derive abandonment; they read the flag this function writes.
bool ANodeShuffleSubsystem::ClearAbandonedWellPlacement(FNodeShuffleWellEntry& E, const TCHAR* Why)
{
    // THE ONE SAFETY GATE, kept: a placed group owns its coordinates and the spawn/adopt/link/suppress
    // paths all read them; clearing here would strand a working well instantly.
    //
    // ns-review-h2-r3 F-6: this used to be a SILENT `return false`, which made an ordering mistake at
    // a call site indistinguishable from "nothing to do". Both call sites are correct only because
    // bGroupPlaced happens to be false by the time they run, and neither is enforced by anything --
    // so the no-op is now LOUD.
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

    // ================================================================================================
    // ns-review-h2-r4 F-2 (HIGH) -- THE SELF-HEAL A3 DELETED, RESTORED, AND LOUD.
    // ================================================================================================
    // h2-8 had a bare `if (!E.bPlacementClaimLive) { return false; }` here, and the reconciliation had
    // the matching `continue` one function down. Between them they made h2-7 F-A's EXACT state -- a
    // dead claim on a coordinate that is still non-zero -- unreachable by the only code that repairs
    // it, while three separate detectors reported it and the pre-A3 migration asserted the claim back
    // to LIVE on a coordinate whose ownership is unknown. The pre-A3 code could not be defeated that
    // way because it read the COORDINATE (`const bool bHadCore = !WasCore.IsNearlyZero();`), never a
    // flag. So: nothing named -> silent idempotent no-op, exactly as before; something named with a
    // dead claim -> INVARIANT A3 IS BROKEN, say so at Warning level and repair it.
    //
    // Repair, not silence: the packet's principle is "no SILENT auto-repair", because a silent repair
    // hides the edit that broke it. A repair that names the entry, the caller and the coordinate does
    // not hide anything -- and the alternative (detect-only) means the mod knowingly carries a
    // permanent stale coordinate that its own log calls a defect.
    if (!E.bPlacementClaimLive)
    {
        if (!WellEntryNamesAnyPlacedCoordinate(E))
        {
            return false; // nothing claimed and nothing named -- idempotent, silent, no-op
        }
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-CLAIM core='%s' (%s): *** CLAIM INVARIANT VIOLATED -- REPAIRING *** claimLive=0 ")
            TEXT("but this entry still NAMES a coordinate (core=%s). That is h2-7 F-A's exact signature: ")
            TEXT("the coordinate outlives the entry that owned it, and until ns-review-h2-r4 F-2 NOTHING ")
            TEXT("repaired it -- A3 short-circuited the only repair on the very bool that had gone wrong. ")
            TEXT("Zeroing it now, which is what the pre-A3 code did by reading the coordinate instead of ")
            TEXT("a flag. FIND THE WRITER: the ONLY setter is NodeShuffleWellRelocateApply.cpp's commit ")
            TEXT("and the ONLY clearer is this function. flags: placed=%d relocate=%d failed=%d."),
            *WellShort(E.CorePath), Why, *E.PlacedCoreLocation.ToCompactString(),
            E.bGroupPlaced ? 1 : 0, E.bRelocate ? 1 : 0, E.bRelocationFailed ? 1 : 0);
        // fall through to the zeroing
    }

    // ns-review-h2-r3 F-2 -- REMEMBER THE COORDINATE INSTEAD OF ONLY DELETING IT.
    // The three fields pass B's discriminator is built from (PlacedCoreLocation, PlacedLocation,
    // DestCoreLocation) are ALL zeroed by the events that strand an actor -- this function zeroes the
    // first two and re-enrolment zeroes the third -- so at the exact instant an actor becomes
    // strandable the only coordinate that could identify it as OURS has just been deleted, and
    // `nearestDealtXY` then measured the distance to some unrelated destination.
    //
    // ns-review-h2-r4 F-5: SATELLITES ARE RECORDED TOO, and this is not a nicety. H0 measured a MINIMUM
    // core->satellite distance of 2076 cm against a 300 cm adopt radius, and satellites outnumber cores
    // ~4-8:1 -- so a core-only memory left F-2's revived branch DEAD FOR THE MORE NUMEROUS CLASS, the
    // same shape F-2 existed to fix, one record type over. (The other half of that fix is in
    // NodeShuffleWellRelocateRoll.cpp, which used to zero every satellite 62 lines BEFORE this
    // function ran, destroying the coordinates before they could be recorded.)
    const auto RememberCoord = [&](const FVector& V)
    {
        if (V.IsNearlyZero()) { return; }
        if (AbandonedWellClaimCoords.Num() < WellAbandonedClaimCoordCap)
        {
            AbandonedWellClaimCoords.Add(V);
            return;
        }
        // ns-review-h2-r4 F-6: THE CAP WAS SILENT. Saturating it restores the exact dead-branch
        // condition F-2 was written to fix, while the line below still prints "(N recorded)" as though
        // it were working -- the workspace's stopgap-constant-ships shape. Said once.
        // TODO (2026-08-07, WIP): 512 is a pathological-loop guard chosen with no measurement behind
        // it. If this line ever appears in a real session, size it from that session's withdrawal
        // count rather than raising it by feel.
        if (!bWellClaimCoordCapLogged)
        {
            bWellClaimCoordCapLogged = true;
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-CLAIM: *** WITHDRAWN-COORDINATE MEMORY SATURATED *** at %d entries. Pass B's ")
                TEXT("'is this stranded actor OURS' discriminator is now DEGRADED -- every further ")
                TEXT("withdrawal is forgotten, so RT-6's VERDICT= for actors abandoned from here on is ")
                TEXT("unreliable and this session's verdicts should be treated as VOID. Said once."),
                WellAbandonedClaimCoordCap);
        }
    };

    const FVector WasCore = E.PlacedCoreLocation;
    int32 SatsCleared = 0;
    for (FNodeShuffleWellSatellite& S : E.Satellites)
    {
        if (S.PlacedLocation.IsNearlyZero()) { continue; }
        RememberCoord(S.PlacedLocation); // F-5: BEFORE the zeroing, same as the core
        S.PlacedLocation = FVector::ZeroVector;
        S.PlacedRotation = FRotator::ZeroRotator;
        ++SatsCleared;
    }
    RememberCoord(WasCore);

    E.PlacedCoreLocation = FVector::ZeroVector;
    E.PlacedCoreRotation = FRotator::ZeroRotator;
    E.bPlacementClaimLive = false; // A3: THE ONLY PLACE THIS IS EVER SET FALSE

    // D-2: a withdrawn claim is no longer mid-assembly, so its expiry counter must not survive to
    // expire the NEXT claim this entry makes the moment it is committed.
    WellClaimMidAssemblyPasses.Remove(E.CorePath);

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-ABANDON core='%s' (%s): dropped this entry's claim on its committed placement -- ")
        TEXT("core was %s, %d satellite coordinate(s) cleared, claimLive 1 -> 0 (relocate=%d failed=%d ")
        TEXT("placed=%d). Until this existed the stale claim made the orphan sweep report any actor ")
        TEXT("still standing there as 'mid-assembly: OWNED, not orphaned' forever (ns-review-h5 F2). ")
        TEXT("The coordinate(s) are REMEMBERED for this session (%d recorded, core AND satellites since ")
        TEXT("ns-review-h2-r4 F-5) so pass B can still tell a stranded actor of ours from a ")
        TEXT("mis-classified vanilla well (ns-review-h2-r3 F-2)."),
        *WellShort(E.CorePath), Why, *WasCore.ToCompactString(), SatsCleared,
        E.bRelocate ? 1 : 0, E.bRelocationFailed ? 1 : 0, E.bGroupPlaced ? 1 : 0,
        AbandonedWellClaimCoords.Num());
    return true;
}

// ================================================================================================
// A3 -- INVARIANT A3, ENFORCED IN THE LOG RATHER THAN ASSERTED IN PROSE.
// ================================================================================================
//   bPlacementClaimLive == false  <=>  PlacedCoreLocation.IsNearlyZero()
//   plus (ns-review-h2-r4 F-7): claim dead => no bCaptured satellite names a coordinate either.
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
//
// IT LOGS; IT DOES NOT REPAIR, and that division of labour is deliberate. The repair lives in
// ClearAbandonedWellPlacement (F-2), which is the SINGLE CLEARER -- making the checker a second writer
// would destroy A3's central property. ns-review-h2-r4 F-1: this must be called BEFORE any repair at
// every site that repairs, which as of this round includes FinishWellRollTeardown, where the
// reconciliation used to run first and erase a violation before the checker ever looked at it.
//
// THE SATELLITE HALF IS CHECKED IN ONE DIRECTION ONLY, and the header says so rather than promising
// more than is enforced: `claim dead && a bCaptured satellite names a coordinate` is a violation. The
// mirror ("claim live => every bCaptured satellite names one") is NOT checked, because the only state
// that produced it -- the roll zeroing satellites before the commit-refusal exit -- was removed this
// round (F-5), and an uncaptured record legitimately holds ZeroVector forever.
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
                          TEXT("outlives the entry that owned it. ClearAbandonedWellPlacement REPAIRS ")
                          TEXT("this state at the next reconciliation (ns-review-h2-r4 F-2)."),
                    E.bGroupPlaced ? 1 : 0, E.bRelocate ? 1 : 0, E.bRelocationFailed ? 1 : 0);
            }
            continue; // one violation per entry is enough; the satellite half below is the same defect
        }

        // ns-review-h2-r4 F-7: THE SATELLITE HALF, which was prose in the header and nothing in the
        // code. It is the half whose asymmetry WAS h2-6 F-A ("the group split across two verdicts"),
        // and pass A's satellite tripwire could fire while this function reported nothing -- sending a
        // tester to grep WELLH2-CLAIM and find silence.
        if (E.bPlacementClaimLive) { continue; }
        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            if (!S.bCaptured || S.PlacedLocation.IsNearlyZero()) { continue; }
            ++Violations;
            if (Violations <= 5)
            {
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("WELLH2-CLAIM [%s] core='%s' sat='%s': *** CLAIM INVARIANT VIOLATED (SATELLITE) ")
                    TEXT("*** claimLive=0 and PlacedCoreLocation is zero, but this bCaptured satellite ")
                    TEXT("still names %s. The group is SPLIT ACROSS TWO VERDICTS -- h2-6 F-A's exact ")
                    TEXT("asymmetry, one record type over. The satellite's PlacedLocation is part of the ")
                    TEXT("SAME claim as its core's; they are written and cleared together. Repaired at ")
                    TEXT("the next reconciliation (ns-review-h2-r4 F-2)."),
                    Where, *WellShort(E.CorePath), *WellShort(S.SatellitePath),
                    *S.PlacedLocation.ToCompactString());
            }
            break; // one report per entry
        }
    }

    if (Violations == 0 && !bWellClaimInvariantOkLogged)
    {
        bWellClaimInvariantOkLogged = true;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-CLAIM [%s]: INVARIANT A3 HOLDS -- %d layout entry(ies) checked (cores AND ")
            TEXT("bCaptured satellites since ns-review-h2-r4 F-7), 0 violations. This line proves the ")
            TEXT("check RAN; its absence means it did not, and every ownership number below is then only ")
            TEXT("as good as the old derived predicate. IF N IS 0 THE CHECK RAN OVER NOTHING and proves ")
            TEXT("nothing (ns-review-h2-r4 F-10). Said once per session; a violation is logged EVERY ")
            TEXT("time, and the [%s] tag names whichever call site got there first -- 'post-roll ")
            TEXT("pre-reconcile' and 'settled' are both valid."),
            Where, WellLayout.Num(), Where);
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

// ================================================================================================
// ns-review-h2-r4 §0.2 + D-1 -- THE PRE-A3 SAVE MIGRATION, WHICH IS THE OLD DERIVATION, SAID PLAINLY.
// ================================================================================================
// THIS FUNCTION IS `Placed*-non-zero-as-a-proxy-for-ownership` -- one of the four inputs to the very
// generator A3 exists to switch off -- and round 8 was right that calling it "a one-shot migration"
// was the mis-labelling that would let it rot. It ran unconditionally on every load, forever, with no
// version gate, no already-migrated marker and no bound on what it would assert ownership of.
//
// It is kept, because the alternative is worse: a save written by h2-7 or earlier deserialises
// bPlacementClaimLive FALSE while PlacedCoreLocation still names a real, built-on, producing well, and
// every reader A3 converted would then read "this entry claims nothing" about it. But it is now:
//   (1) VERSION-GATED on WellClaimMigrationVersion, a SaveGame int, so it is one-shot in an
//       ENFORCEABLE sense rather than by comment. The gate FAILS OPEN: if that int does not survive
//       the round trip either, the version reads 0 and the migration simply runs again -- i.e. exactly
//       h2-8's behaviour, never worse.
//   (2) NAMED for what it is, here and in the log.
//   (3) LOGGING ON BOTH BRANCHES (ns-review-h2-r4 F-3). It used to print nothing when it backfilled
//       nothing, which made RT-9's PASS -- "no [backfill] line" -- identical to "the migration never
//       ran", "AdoptRestoredWellGroups never ran" and "no entry had a coordinate to backfill". The one
//       invariant this packet can only grade ASSUMED was gated on a test that could not tell success
//       from the detector being absent. Every branch prints now, so absence means it did not run.
//
// AFTER THE GATE HAS FIRED ONCE, the detector for "the claim was lost across the save round-trip" is
// no longer this function -- it is ValidateWellClaimInvariant, which reports the same state LOUDLY and
// (for non-placed entries) F-2 now repairs it. That is a strictly better instrument: the migration
// masked the damage by silently asserting the claim back.
int32 ANodeShuffleSubsystem::MigratePreA3PlacementClaimsOnce()
{
    if (WellClaimMigrationVersion >= WellClaimMigrationCurrentVersion)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-CLAIM [backfill]: SKIPPED -- this save is already at claim-migration version %d ")
            TEXT("(current %d), %d layout entry(ies). THE PRE-A3 MIGRATION IS THE OLD ")
            TEXT("`Placed*-non-zero-means-ownership` DERIVATION and it is deliberately allowed to run ")
            TEXT("AT MOST ONCE PER SAVE (ns-review-h2-r4 D-1); from here on, a claim that goes missing ")
            TEXT("across a save round-trip shows up as *** CLAIM INVARIANT VIOLATED *** instead of being ")
            TEXT("silently asserted back. This line proves the gate RAN."),
            WellClaimMigrationVersion, WellClaimMigrationCurrentVersion, WellLayout.Num());
        return 0;
    }

    const int32 FromVersion = WellClaimMigrationVersion;
    int32 Backfilled = 0;
    for (FNodeShuffleWellEntry& E : WellLayout)
    {
        if (E.bPlacementClaimLive) { continue; }
        if (E.PlacedCoreLocation.IsNearlyZero()) { continue; }
        E.bPlacementClaimLive = true;
        ++Backfilled;
    }
    WellClaimMigrationVersion = WellClaimMigrationCurrentVersion;

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-CLAIM [backfill]: checked %d entry(ies), %d were BACKFILLED to claimLive=1 from a ")
        TEXT("non-zero PlacedCoreLocation; migration version %d -> %d. THIS LINE PROVES THE MIGRATION ")
        TEXT("RAN, and it is printed even when the count is 0 (ns-review-h2-r4 F-3) precisely so that ")
        TEXT("its ABSENCE cannot be read as a pass. A non-zero count is EXPECTED exactly once, on the ")
        TEXT("first load of a save written before A3 existed. A non-zero count on a save that was ")
        TEXT("already loaded by an A3 build would have meant the claim is being lost across the save ")
        TEXT("round-trip -- that scenario is now caught by *** CLAIM INVARIANT VIOLATED *** instead, ")
        TEXT("because this migration can no longer run twice on the same save."),
        WellLayout.Num(), Backfilled, FromVersion, WellClaimMigrationCurrentVersion);
    return Backfilled;
}

// ns-review-h2-r2 F-A -- THE SINGLE COPY OF THE CLAIM-WITHDRAWAL RULE.
// It existed twice (the roll tail and the sweep) with identical bodies, which is the duplicate-rule
// shape this packet has been bitten by three times already. The rule: an entry that is NOT placed and
// is NOT still searching has abandoned whatever coordinate it names, so the claim is withdrawn.
// Deliberately UNGATED at both call sites -- it destroys nothing, and gating it would leave stale
// claims in the save exactly when the sweep is least able to notice them.
//
// A3, AND THE ONE HONEST LIMIT OF IT. This function is the LAST derived predicate over the lifecycle
// flags in the packet, and it stays one ON PURPOSE. A3 removes the derivation from every READER; it
// cannot remove the need to decide WHEN a claim ends. Three sites know that directly (the escalation
// ladder, re-enrolment, and this) -- and this one exists precisely to catch a route the other two do
// not know about, including routes that do not exist yet. So it is a BACKSTOP that WRITES the fact,
// not a reader that infers it, and that distinction is the whole of A3: derive once, at the one place
// whose job is to notice, and let everything downstream read.
//
// ns-review-h2-r4: TWO CHANGES, both structural.
//   F-2 -- the entry test is no longer "is the claim flag set". It is "does this entry name a
//     coordinate, by ANY means", so the repair path below cannot be short-circuited by the one bool
//     it exists to correct. The lifecycle predicate underneath is UNCHANGED, which is what makes the
//     restored repair have EXACTLY the pre-A3 self-heal's domain (not placed, not searching) rather
//     than a wider one.
//   D-2 -- "mid-search: the claim is live" is no longer unbounded. See the block below.
int32 ANodeShuffleSubsystem::ReconcileAbandonedWellClaims(const TCHAR* Why)
{
    int32 Withdrawn = 0, Expired = 0, MidAssembly = 0, Repaired = 0;
    for (FNodeShuffleWellEntry& E : WellLayout)
    {
        // F-2: ask the COORDINATES, not the flag. `claim dead AND nothing named` is the only genuinely
        // nothing-to-do state; `claim dead AND something named` is h2-7 F-A and must reach the repair.
        if (!E.bPlacementClaimLive && !WellEntryNamesAnyPlacedCoordinate(E)) { continue; }

        if (E.bGroupPlaced)
        {
            // A placed group OWNS its coordinates. Note this branch is reached with a DEAD claim only
            // when the invariant is broken (the checker names it); repairing here would zero a working
            // well's coordinates, which is the one thing the safety gate exists to prevent.
            WellClaimMidAssemblyPasses.Remove(E.CorePath);
            continue;
        }

        if (E.bRelocate && !E.bRelocationFailed)
        {
            // ========================================================================================
            // ns-review-h2-r4 D-2 -- MID-ASSEMBLY NOW HAS A BOUNDED LIFETIME.
            // ========================================================================================
            // ROUND 8 PREDICTED THIS AS THE ROUND-9 FINDING AND WE ARE FIXING IT IN ADVANCE. The tuple
            // (placed=false, relocate=true, failed=false, claim=live) was skipped here as "mid-search"
            // with NO EXPIRY ANYWHERE. Two reachable routes: commit a footprint, spawn INCOMPLETE, then
            // (a) turn the feature off -- ApplyWellRelocation's per-entry gate `continue`s past this
            // entry every pass thereafter, so nothing ever advances it -- or (b) simply never walk back
            // to that destination. Pass A then protects whatever is standing there FOREVER, pass B
            // reports it OURS-STRANDED FOREVER, and the claim persists in the save FOREVER. A claim
            // with no expiry is the round-9 shape.
            //
            // WHY A PASS COUNTER RESET BY THE SETTER IS THE RIGHT BOUND, rather than a timer or a
            // save-persisted counter:
            //   * The reconciliation runs UNCONDITIONALLY on every sweep (~60 s cadence) and at every
            //     roll tail -- including with the feature OFF, which is precisely route (a). A counter
            //     driven from here therefore ticks in exactly the situations where nothing else does.
            //   * A group that is genuinely mid-assembly RE-COMMITS ITS FOOTPRINT EVERY PASS a player
            //     is near it (ApplyWellRelocation -> TryPlaceWellGroup -> the commit at
            //     NodeShuffleWellRelocateApply.cpp:170-180, which does not advance the yaw cursor on
            //     the INCOMPLETE-spawn path). That commit RESETS this counter. So progress is the reset
            //     signal, and an actively retrying group can never expire -- which is what stops this
            //     turning into the h5 F-2 destroy/respawn cycle.
            //   * It is deliberately NOT a UPROPERTY(SaveGame). Round 8's own recommendation was not to
            //     put a second unproven serialization surface in flight beside A3-5 before RT-9 answers.
            //     Session-scoped still bounds the failure: the claim can no longer be live-and-skipped
            //     for the life of the SAVE, only for at most WellClaimMidAssemblyMaxPasses passes of
            //     any one session. That is the difference between "forever" and "bounded", which is the
            //     property being bought. THE RESIDUAL IS NAMED IN THE HANDOFF, not hidden here.
            // WHAT EXPIRY COSTS when it fires on a legitimately-deferred group: the committed footprint
            // is dropped, so its actors (if any) lose protection and pass A reclaims them -- and the
            // entry keeps bRelocate/bDestDealt/DestCoreLocation, so the next pass with a player near
            // re-validates and re-commits from scratch. Nothing is retired and no budget is spent.
            if (!E.bPlacementClaimLive)
            {
                // Desync in the other direction (claim dead, coordinate live, still searching). NOT
                // repaired here on purpose: zeroing would delete a footprint the entry may legitimately
                // be assembling against. ValidateWellClaimInvariant reports it every sweep.
                continue;
            }
            ++MidAssembly;
            int32& Passes = WellClaimMidAssemblyPasses.FindOrAdd(E.CorePath);
            ++Passes;
            if (Passes < WellClaimMidAssemblyMaxPasses) { continue; }

            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-CLAIM core='%s' (%s): *** MID-ASSEMBLY CLAIM EXPIRED *** -- this entry has ")
                TEXT("been 'mid-search, the claim is live' for %d consecutive reconciliation pass(es) ")
                TEXT("with NO fresh footprint commit (the commit resets this counter). Until ")
                TEXT("ns-review-h2-r4 D-2 that state had no expiry at all: pass A protected anything ")
                TEXT("standing at %s forever and pass B reported it OURS-STRANDED forever. Reachable by ")
                TEXT("spawning INCOMPLETE and then disabling relocation, or by never returning to the ")
                TEXT("destination. Withdrawing the claim now; the entry keeps its dealt destination ")
                TEXT("(dealt=%d) and re-validates from scratch the next time a player is near. flags: ")
                TEXT("relocate=%d failed=%d placed=%d."),
                *WellShort(E.CorePath), Why, Passes, *E.PlacedCoreLocation.ToCompactString(),
                E.bDestDealt ? 1 : 0, E.bRelocate ? 1 : 0, E.bRelocationFailed ? 1 : 0,
                E.bGroupPlaced ? 1 : 0);

            if (ClearAbandonedWellPlacement(E, TEXT("mid-assembly claim expired (no commit for the bound)")))
            {
                ++Withdrawn;
                ++Expired;
            }
            continue;
        }

        // Not placed and not searching: this entry has abandoned whatever it names.
        if (!E.bPlacementClaimLive) { ++Repaired; } // F-2's self-heal is what is about to run
        if (ClearAbandonedWellPlacement(E, Why)) { ++Withdrawn; }
    }

    if (Expired > 0 || Repaired > 0)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-CLAIM [%s]: reconciliation withdrew %d claim(s) -- of which %d EXPIRED under ")
            TEXT("the mid-assembly bound of %d pass(es) (ns-review-h2-r4 D-2, the predicted round-9 ")
            TEXT("finding) and %d were F-2 SELF-HEALS of a dead claim that still named a coordinate ")
            TEXT("(h2-7 F-A's signature, which h2-8 detected three ways and repaired with nothing). ")
            TEXT("%d entry(ies) are still legitimately mid-assembly this pass."),
            Why, Withdrawn, Expired, WellClaimMidAssemblyMaxPasses, Repaired, MidAssembly);
    }
    return Withdrawn;
}
