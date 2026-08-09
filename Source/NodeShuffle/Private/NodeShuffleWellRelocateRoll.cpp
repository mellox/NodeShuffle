// Packet H2 (ns-wells-h2, branch feature/extractor-automatch): the DEAL half of RIGID resource-well
// RELOCATION -- capturing each eligible well's rigid body from the live actors, and dealing it a
// destination. NodeShuffleWellRelocateApply.cpp owns the placement/spawn half; NodeShuffleWellLink.cpp
// owns the mCore lifecycle; NodeShuffleWellRelocate.h says what lives where and holds the shared pure
// helpers (the yaw permutation in particular has exactly one implementation, on purpose).
//
// WHAT H2 ADDS, AND WHAT IT LEAVES ALONE. H1's in-place retype is untouched and still runs on its own
// toggle. Relocation is strictly additive, behind a SECOND toggle (RelocateResourceWells, default OFF)
// that additionally REQUIRES ShuffleResourceWells: a well NodeShuffle does not manage is not a well it
// may move. With either toggle off nothing in this file writes anything.
//
// ================================================================================================
// T7b (ns-t7b-reroll, 2026-08-08) -- A RELOCATED WELL NOW RE-ROLLS LIKE ANY OTHER NODE. READ THIS.
// ================================================================================================
// The author's decision: an unpinned well's GEOGRAPHY re-rolls; a pinned one (built on) does not move
// at all. Until T7b a guard at the top of the loop below (`if (E.bGroupPlaced) { ...; continue; }`)
// sent every already-placed entry home, so a relocated well's location never churned. Three things
// landed together and each is load-bearing:
//
//   A2 (commit 5ed3ac2, T16, NOT in this file) -- a relocated well can be PINNED again. The pin used
//      to be read off the VANILLA core, which relocation has hidden, de-collided and deregistered, so
//      it reported unoccupied forever: the author's log printed "20 wells (20 managed, 0 pinned)" over
//      a save holding 17 relocated wells. EvaluateWellPin now reads the actors WE spawned, which is
//      what makes the `!E.bManaged` guard below an honest gate for this population.
//
//   A3 (this file) -- DEAL FIRST, DESPAWN SECOND. The old block despawned the group, cleared
//      bGroupPlaced and withdrew the claim BEFORE the deal ran; a well whose deal then failed was
//      logged "left vanilla this roll", and that was false -- nothing here ever un-suppresses a
//      vanilla group (ScannerDeregistered is a permanent set), so the well ended up NOWHERE. The
//      capture is now held in a LOCAL pending record and nothing is written to the entry until a
//      destination exists: capture -> entry untouched; deal fails -> entry untouched, nothing to undo;
//      deal wins -> gate, teardown, claim withdrawal and new destination in ONE commit block. There is
//      no instant at which the entry names no place at all.
//
//   A4 (NodeShuffleConfig.*) -- RerollRelocatedWells, DEFAULT OFF. With it off this file behaves
//      exactly as it did before T7b, so an existing save does not churn ~17 wells on one keypress.
//
// THE RE-ENROLMENT PATH HAD NEVER EXECUTED BEFORE T7b -- the re-capture, the DespawnWellGroup
// ("re-enrolled by a new roll"), the search-state reset and the claim withdrawal were written,
// reviewed twice and never once reached (zero WELLH2-ABANDON events with that reason in the author's
// session log). It is all first-run code and is instrumented as such: every counter whose healthy
// value is 0 prints beside its denominator.
//
// WHAT H0 MEASURED, AND WHAT THIS FILE IS THEREFORE ALLOWED TO RELY ON (design §4b, 20 wells /
// 135 satellites, two identical runs):
//   * satsPerWell min 4 / mean 6.75 / max 10. The MINIMUM is used here as a STREAMING COMPLETENESS
//     gate, not as a guess about geometry -- see WellMinSatellitesForRelocation below.
//   * minimum inter-satellite distance 1818.8 cm over 401 pairs, min core->satellite 2076 cm, against
//     OverlapsAt's 800 cm reject radius. That is why H2 builds NO same-group overlap exemption. It is
//     asserted per well here rather than assumed forever.
//   * resourceMismatchWells = 0. Relied on by H1's write; H2 re-reads the resource from the layout
//     entry (one value for the whole group), so a relocated group cannot split by construction.
//
// THE ORDER OF E.Satellites IS NOT MEANINGFUL AND IS NEVER USED AS ONE. H1 left a block comment headed
// "*** H2, READ THIS BEFORE USING E.Satellites AS A VECTOR ***" (NodeShuffleWellRoll.cpp): the array is
// in census/merge order, which is ACTOR-ITERATION order for newly seen satellites, i.e.
// streaming-dependent and not stable across sessions or vantage points. Design §Q2 calls it "the
// ordered purity vector"; THAT PHRASING IS WRONG and H1 deliberately did not follow it. Every access in
// H2 is keyed by SatellitePath -- the capture below writes each satellite's offset/purity INTO the
// record found by its own path, and the spawn reads them back the same way. Nothing is ever indexed.
//
// IMPORT DISCIPLINE (memory: sf-shipping-export-trap). No new engine entry point is reached here:
// discovery is H0's CollectWellCensus, and everything else is GetActorLocation/GetActorRotation/
// GetResourcePurity, all already imported. That is the PREDICTION, not the claim -- the import table is
// MEASURED after the build, never predicted. H0 falsified "inline in the header => no import" with
// GetCore() on this very class hierarchy.
//
// FILE LENGTH (ns-t7b, deliberate): this file is over the workspace's 500-line limit and T7b makes it
// longer. The natural extraction -- ReEnrolPlacedWellGroup(E) -- has to be a MEMBER (DespawnWellGroup
// and ClearAbandonedWellPlacement are private and depend on this class's AccessTransformers Friend
// grants), so it needs a declaration in NodeShuffleSubsystem.h, which packet ns-t7b-reroll does not
// own. Deferred with that reason rather than worked around.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleConfig.h"
#include "NodeShuffleWellCensus.h"   // H0's grouping -- CollectWellCensus + the well record/member types
#include "NodeShuffleWellRetype.h"   // WellPathOf / WellClassPathOf / WellShort
#include "NodeShuffleWellRelocate.h" // the H2 pure helpers (incl. IsWellMemberInUse)

namespace
{
    // Mirrors of the file-local constants in NodeShuffleSubsystem.cpp (which are in that translation
    // unit's anonymous namespace and therefore not reachable from here). Named and sourced explicitly
    // rather than re-derived, so a change there is a grep away from a change here.
    constexpr float WellMinNodeSpacingCm = 2500.0f;      // NodeShuffleSubsystem.cpp: MinNodeSpacing
    // A well's footprint is far bigger than a node's. H0 measured bounding radii 3566-6447 cm, so a
    // destination must clear the deal-box edge and other layout entries by the LARGEST radius plus the
    // node spacing, or the outer satellites start their search already on top of something.
    constexpr float WellMaxBoundRadiusCm = 6500.0f;      // design §Q3a: measured max 6447 cm

    // T7b/A3: a rigid-body capture held OUTSIDE the layout entry until a destination exists.
    //
    // This struct IS the fix. Every field in it used to be written straight into
    // FNodeShuffleWellEntry at capture time, which is why a failed deal could leave an
    // already-relocated well with its actors destroyed, its claim withdrawn and no destination. Held
    // here, a failed deal costs exactly one discarded local.
    struct FNodeShuffleWellPendingCapture
    {
        int32 EntryIndex = INDEX_NONE;
        FString CorePath;                 // the deal's deterministic sort key
        bool bWasPlaced = false;          // an ALREADY-RELOCATED well being re-enrolled
        FVector CoreLoc = FVector::ZeroVector;
        float CoreYaw = 0.0f;
        double MinIntra = 0.0;
        int32 SelfSiteIndex = INDEX_NONE; // this well's own vacating site inside WellDestinations
        bool bDealt = false;
        FVector Dest = FVector::ZeroVector;
        TMap<FString, TPair<FVector, float>> OffsetByPath; // path -> (offset, local yaw)
    };
}

void ANodeShuffleSubsystem::RollWellRelocation(int32 Seed, bool bIsReroll, bool bRelocationEnabled)
{
    if (!bRelocationEnabled)
    {
        // Same semantics as the master switch and as ShuffleResourceWells: turning the toggle off
        // stops us ACTING, it does not un-move a world the player has already been playing. Any
        // already-relocated group keeps its Placed* coordinates in the save and keeps being spawned
        // and linked by the apply pass -- withdrawing a well the player has found and built toward
        // would be strictly worse than either consistent state (the same reasoning H1's RT-6 note
        // records for the allow-list).
        int32 AlreadyPlaced = 0;
        for (const FNodeShuffleWellEntry& E : WellLayout) { if (E.bGroupPlaced) { ++AlreadyPlaced; } }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-ROLL: SKIPPED -- 'Relocate Resource Wells' is OFF. %d well(s) in this save are ")
            TEXT("ALREADY relocated and stay exactly where they are (the toggle stops us moving wells; it ")
            TEXT("was never a promise to move them back)."),
            AlreadyPlaced);
        return;
    }

    // The census is re-run rather than reusing RollWellLayout's, because this function is called from
    // its TAIL and the two want different things from it: the deal wants membership, this wants live
    // TRANSFORMS. Re-running is a single actor iteration and keeps the two independent -- H0's grouping
    // is used, never re-implemented (that is the whole reason it was factored out of the dump).

    // Per-ROLL log throttles, cleared here for the same reason RollWellLayout clears its own: a re-roll
    // re-decides every well's fate, so every well's outcome deserves to be announced again.
    WellRelocLogged.Empty();
    WellRelocFailLogged.Empty();
    WellSuppressLogged.Empty();
    // ns-review-h4 style: these two were added later and missed the reset, so their once-per-session
    // warnings would stay silenced across a re-roll that re-creates the very conditions they describe.
    WellUncapturedLogged.Empty();
    WellStaleInUseLogged.Empty();
    WellSuppressSkipLogged.Empty();
    // ns-t23-rollhide: same lifecycle as WellSuppressLogged above. A re-roll re-decides every well's
    // fate, so a restore that happens under the NEW roll deserves to be announced again; keeping the old
    // roll's keys would silence the un-hide lines for exactly the wells the re-roll just re-considered.
    WellUnhideLogged.Empty();
    WellIncompleteSpawnCounts.Empty();  // h5 (2): a re-roll re-decides every placement
    bWellOrphanInUseLogged = false;     // h5 F-4
    bWellRelocDisabledLogged = false;
    // h5 F1: the three new throttles reset too -- silencing a GATE line across a re-roll hides a gate.
    bWellSweepGatedLogged = false;
    bWellSweepCapLogged = false;
    bWellBackstopLogOnlyLogged = false;

    // T7b/A4. Read here rather than taken as a parameter because the signature lives in
    // NodeShuffleSubsystem.h, which this packet does not own; FinishWellRollTeardown reads its own gate
    // the same way (NodeShuffleWellSweep.cpp), so this is the file's existing idiom, not a new one.
    const FNodeShuffleConfigStruct RollConfig = FNodeShuffleConfigStruct::GetActiveConfig(this);
    const bool bRerollRelocated = RollConfig.RerollRelocatedWells;
    // ns-t23-rollhide (T23 stage 3): hide the VANILLA well at the roll instead of after the replacement
    // has been built. Read from the SAME config fetch as the toggle above so the two cannot describe
    // different config snapshots. ANDed with bRelocationEnabled because a roll that is not relocating
    // anything must never hide anything -- the parameter is the one the caller actually gated on, and
    // reading the config's own RelocateResourceWells here instead would let the two disagree.
    const bool bCommitAtRoll = RollConfig.CommitWellsAtRoll && bRelocationEnabled;

    FNodeShuffleWellCensus Census;
    CollectWellCensus(GetWorld(), Census);

    // ns-review-h2 F1 (CRITICAL): exclude OUR OWN spawned cores. CollectWellCensus is deliberately
    // unfiltered (NodeShuffle.DumpWells depends on that), so the filter lives at every DECISION site
    // instead -- here and in RollWellLayout's merge loop. Without it a re-roll would enrol the wells H2
    // already relocated FROM OUR OWN SPAWNS, doubling the actor count each time. T7b re-enrolment is a
    // different thing entirely: it re-captures the rigid body from the still-standing VANILLA actors,
    // which is why it needs this filter exactly as much as the first enrolment did.
    TMap<FString, const FNodeShuffleWellRecord*> ByCorePath;
    int32 SkippedOurSpawned = 0;
    for (const FNodeShuffleWellRecord& W : Census.Wells)
    {
        if (!IsValid(W.Core)) { continue; }
        if (FNodeShuffleModule::IsManagedSpawnedNode(W.Core)) { ++SkippedOurSpawned; continue; }
        ByCorePath.Add(WellPathOf(W.Core), &W);
    }
    if (SkippedOurSpawned > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-ROLL: skippedOurSpawned=%d -- that many live fracking cores are wells WE relocated ")
            TEXT("and are excluded from the census this roll reads (ns-review-h2 F1)."), SkippedOurSpawned);
    }

    int32 PlacedInSave = 0;
    for (const FNodeShuffleWellEntry& E : WellLayout) { if (E.bGroupPlaced) { ++PlacedInSave; } }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-ROLL: rerollRelocatedWells=%d -- %d of %d well(s) in this save are ALREADY relocated. ")
        TEXT("With that toggle OFF those %d keep their current spot and this roll does not touch them (the ")
        TEXT("pre-T7b behaviour); with it ON each of them is re-considered like any other well."),
        bRerollRelocated ? 1 : 0, PlacedInSave, WellLayout.Num(), PlacedInSave);

    // -------- PHASE 1 COUNTERS: these PARTITION WellLayout, and the partition is checked below --------
    // ns-t7b F5: before T7b, AlreadyCaptured was incremented at the TOP of the loop and six later
    // `continue`s could add the same entry to a refusal bucket -- disjoint only because the
    // bGroupPlaced guard sent placed entries home before any of them. With the guard gone the sets
    // would overlap silently, so the buckets are now mutually exclusive by construction and the sum is
    // asserted against WellLayout.Num() rather than trusted.
    int32 Captured = 0, CapturedWhilePlaced = 0, AlreadyKept = 0, PermanentlyFailed = 0,
          RefusedUnstreamed = 0, RefusedTooFew = 0, RefusedPinned = 0, RefusedPinnedWhilePlaced = 0,
          RefusedGeometry = 0, RefusedNonFinite = 0;
    // CROSS-CUTTING, deliberately NOT part of the partition: how many of ALL the refusals above landed
    // on a well that is standing at a relocated site and keeps it.
    int32 RefusedWhilePlaced = 0;

    // ns-t7b F8: the deleted guard used to guarantee `bGroupPlaced => bRelocate` for free, because a
    // placed entry never reached a refusal branch. It does now, so the invariant is restored HERE, at
    // every refusal, from MEASURED entry state instead of from the loop's shape.
    const auto NoteRefusal = [&RefusedWhilePlaced](FNodeShuffleWellEntry& Entry) -> void
    {
        Entry.bRelocate = Entry.bGroupPlaced;
        if (Entry.bGroupPlaced) { ++RefusedWhilePlaced; }
    };

    // ns-t7b F4: every refusal line ends with this sentence. All five refusal diagnostics were written
    // when only a NEVER-PLACED well could reach them, so each asserted some form of "the well stays
    // exactly vanilla" -- which is the opposite of the truth for an already-relocated well standing at
    // PlacedCoreLocation with its vanilla twin suppressed. One sentence, chosen by measured state, so
    // the five cannot drift apart again.
    const auto KeptOrVanilla = [](const FNodeShuffleWellEntry& Entry) -> FString
    {
        return Entry.bGroupPlaced
            ? FString::Printf(
                TEXT("It KEEPS the relocated placement it already has at %s and is not re-enrolled this ")
                TEXT("roll; its vanilla twin stays suppressed."),
                *Entry.PlacedCoreLocation.ToCompactString())
            : FString(TEXT("It stays exactly vanilla this roll."));
    };

    TArray<FNodeShuffleWellPendingCapture> Pending;

    for (int32 EntryIdx = 0; EntryIdx < WellLayout.Num(); ++EntryIdx)
    {
        FNodeShuffleWellEntry& E = WellLayout[EntryIdx];
        const bool bAlreadyPlaced = E.bGroupPlaced;

        // ns-t7b-r2 F-G: HOISTED ABOVE THE A4 TOGGLE GATE. This warning used to live inside the
        // `bAlreadyPlaced && !bRerollRelocated` branch, which silenced it exactly when the feature it
        // describes is ON -- a placed entry refused for pin, streaming, geometry or path mismatch kept
        // its stale, under-captured group and said nothing. It now runs for EVERY already-placed entry,
        // whatever the toggle.
        //
        // ns-review-h2 F2 (CRITICAL): the entry is placed, so nothing below re-captures it -- but
        // RollWellLayout's merge, which ran moments ago, may have APPENDED satellites that streamed
        // in for the first time since the relocation. Those records carry no offset at all. They
        // must never reach the spawn (they would materialise at world origin as live,
        // extractor-snappable nodes and inflate the well's rate), and they must never be silently
        // topped up from the CURRENT world either -- the vanilla group they belong to is suppressed,
        // so their live transforms no longer describe the rigid body we placed. They stay
        // UNCAPTURED: refused at spawn, still suppressed at the original site, and named here.
        if (bAlreadyPlaced && E.Satellites.Num() != E.CapturedSatelliteCount)
        {
            int32 Uncaptured = 0;
            FString Names;
            for (const FNodeShuffleWellSatellite& S : E.Satellites)
            {
                if (S.bCaptured) { continue; }
                ++Uncaptured;
                if (Uncaptured <= 5) { Names += (Names.IsEmpty() ? TEXT("") : TEXT(", ")) + WellShort(S.SatellitePath); }
            }
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-ROLL core='%s': this well is ALREADY RELOCATED but its satellite list has ")
                TEXT("grown from %d (captured) to %d -- %d record(s) have NO rigid-body capture [%s%s]. ")
                TEXT("They are NOT spawned (a zero offset would put a live node at world origin and ")
                TEXT("inflate this well's rate) and the audit counts them as expected-but-absent. The ")
                TEXT("well keeps the %d satellites it was actually placed with.%s"),
                *WellShort(E.CorePath), E.CapturedSatelliteCount, E.Satellites.Num(), Uncaptured,
                *Names, Uncaptured > 5 ? TEXT(", ...") : TEXT(""), E.CapturedSatelliteCount,
                bRerollRelocated
                    ? TEXT("")
                    : TEXT(" Turning ON 'Re-roll Wells That Have Already Moved' lets a later re-roll ")
                      TEXT("re-capture the whole group."));
        }

        // A well that already exhausted every placement budget in an earlier session stays vanilla for
        // good. Re-enrolling it would restart a search we already proved cannot succeed at this seed,
        // once per re-roll, forever. NOT routed through NoteRefusal: this branch sat ABOVE the deleted
        // guard and its reachability is unchanged by T7b, so its behaviour is left exactly as it was.
        if (E.bRelocationFailed)
        {
            ++PermanentlyFailed;
            E.bRelocate = false;
            continue;
        }

        // A well someone has built on is not ours to MOVE, for a stronger reason than it is not ours
        // to retype: relocating it would leave the pressurizer and every extractor standing on nothing.
        // bManaged is false for a pinned well (RollWellLayout sets it), so this is the same rule read
        // through the same field, not a second one that could diverge.
        //
        // T7b: THIS IS THE GUARD THAT PROTECTS A BUILT-ON RELOCATED WELL, and it only became capable of
        // doing so with A2/T16 -- the pin is now decided by EvaluateWellPin against the actors WE
        // spawned, not against the hidden vanilla core a player cannot reach.
        if (!E.bManaged)
        {
            ++RefusedPinned;
            // ns-t7b-r2 F-D: NoteRefusal writes bRelocate = bGroupPlaced, which flips a state T16
            // shipped at 5ed3ac2 (a placed pinned well got false). This branch sits ABOVE the A4 gate,
            // so applying it unconditionally makes a default-OFF toggle change behaviour. Gated.
            if (bRerollRelocated) { NoteRefusal(E); } else { E.bRelocate = false; }
            if (bAlreadyPlaced)
            {
                ++RefusedPinnedWhilePlaced;
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WELLH2-ROLL core='%s': NOT re-enrolled -- bManaged=0, bPinned=%d. For an ")
                    TEXT("already-relocated well the pin behind bManaged is read from the actors WE ")
                    TEXT("spawned (T16); this branch could not fire for this population before that. %s"),
                    *WellShort(E.CorePath), E.bPinned ? 1 : 0, *KeptOrVanilla(E));
            }
            continue;
        }

        // -------- T7b/A4: the geography gate --------
        // With the toggle OFF this is the pre-T7b behaviour, byte for byte: a placed group keeps its
        // placement across re-rolls of the RESOURCE (which still changes under H1's own toggle through
        // AssignedResourceClassPath), and the only thing said about it is the satellite-growth warning
        // -- which ns-t7b-r2 F-G HOISTED ABOVE this gate, so it is now printed whatever the toggle.
        if (bAlreadyPlaced && !bRerollRelocated)
        {
            ++AlreadyKept;
            E.bRelocate = true;
            continue;
        }

        const FNodeShuffleWellRecord* const* Found = ByCorePath.Find(E.CorePath);
        if (!Found || !*Found)
        {
            // Not streamed in when the roll ran. NOT an error and NOT a failure -- the well simply
            // stays vanilla until a later roll catches it loaded. Stated, because "quietly left out"
            // is exactly how a relocation feature comes to look like it half-works.
            ++RefusedUnstreamed;
            NoteRefusal(E);
            if (bAlreadyPlaced)
            {
                // Newly reachable in T7b, so it is announced. Silent for the never-placed population,
                // whose cadence is unchanged.
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WELLH2-ROLL core='%s': NOT re-enrolled -- its VANILLA core is not in this ")
                    TEXT("roll's census. Re-enrolment re-captures the rigid body from the vanilla ")
                    TEXT("actors, so it cannot run without them. %s"),
                    *WellShort(E.CorePath), *KeptOrVanilla(E));
            }
            continue;
        }
        const FNodeShuffleWellRecord& W = **Found;

        // -------- STREAMING COMPLETENESS: the gate that stops a well being permanently shrunk --------
        // Relocation replaces the well with a NEW group built from THIS layout entry's satellite list.
        // Anything not in that list at capture time is gone for good once the vanilla group is
        // suppressed. So the capture demands two things, and refuses on either:
        //   (a) every satellite the layout knows about is LIVE right now (so we can read its transform);
        //   (b) the census and the layout agree on the count (so the layout is not missing one the
        //       world can see).
        // Plus a floor of H0's measured minimum: a well reporting fewer than 4 satellites is far more
        // likely to be half-streamed than to be a genuinely tiny well, and there is no way to tell the
        // two apart from inside one roll. Refusing costs a re-roll; being wrong costs the satellites.
        int32 LiveSats = 0;
        TArray<FVector> Cloud;                 // core at the origin + every satellite offset
        TMap<FString, TPair<FVector, float>> OffsetByPath; // path -> (offset, local yaw)
        bool bNonFinite = false;

        if (!IsValid(W.Core) || !W.bCoreLocationFinite || !IsFiniteVector(W.CoreLocation))
        {
            ++RefusedNonFinite;
            NoteRefusal(E);
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-ROLL core='%s': REFUSED -- the core's location is not finite. %s"),
                *WellShort(E.CorePath), *KeptOrVanilla(E));
            continue;
        }
        const FVector CoreLoc = W.CoreLocation;
        const float CoreYaw = static_cast<float>(W.Core->GetActorRotation().Yaw);
        Cloud.Add(FVector::ZeroVector); // the core IS the origin of the rigid body -- design §Q3a
                                        // rotates about the CORE, so that is the correct origin.

        for (const FNodeShuffleWellMember& M : W.Members)
        {
            if (!IsValid(M.Actor)) { continue; }
            if (!M.bLocationFinite || !IsFiniteVector(M.Location)) { bNonFinite = true; continue; }
            const FVector Offset = M.Location - CoreLoc;
            const float LocalYaw = static_cast<float>(M.Actor->GetActorRotation().Yaw) - CoreYaw;
            OffsetByPath.Add(WellPathOf(M.Actor), TPair<FVector, float>(Offset, LocalYaw));
            Cloud.Add(Offset);
            ++LiveSats;
        }

        if (bNonFinite)
        {
            ++RefusedNonFinite;
            NoteRefusal(E);
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-ROLL core='%s': REFUSED -- at least one satellite reported a non-finite ")
                TEXT("location (a NaN offset would poison every distance comparison in the footprint ")
                TEXT("test and route the well into the PASSED branch of its own validation). %s"),
                *WellShort(E.CorePath), *KeptOrVanilla(E));
            continue;
        }

        if (LiveSats < WellMinSatellitesForRelocation || LiveSats != E.Satellites.Num())
        {
            ++(LiveSats < WellMinSatellitesForRelocation ? RefusedTooFew : RefusedUnstreamed);
            NoteRefusal(E);
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH2-ROLL core='%s': NOT enrolled this roll -- %d satellite(s) live vs %d in the ")
                TEXT("layout (floor is %d, H0's measured minimum over 20 wells). This is a STREAMING ")
                TEXT("verdict, not a failure: moving a well we only partly know would shrink it ")
                TEXT("permanently, and every missing satellite would look routine in the log. %s"),
                *WellShort(E.CorePath), LiveSats, E.Satellites.Num(), WellMinSatellitesForRelocation,
                *KeptOrVanilla(E));
            continue;
        }

        // -------- ASSERT H0's OVERLAP MEASUREMENT INSTEAD OF ASSUMING IT --------
        // H0 measured min inter-satellite 1818.8 cm and min core->satellite 2076 cm over the whole
        // world, against the 800 cm reject radius, and design §4b draws the conclusion that H2 needs NO
        // same-group overlap exemption. H2 builds no exemption. But a measurement is a measurement of
        // one world at one time, and a group whose own members sit inside the reject radius could never
        // validate its own footprint -- it would burn 36 yaws x 8 nudges x 3 redeals and then fail,
        // with nothing in the log saying why. So the measurement is re-taken per well, here, and a
        // violating well is refused with a line that says the measurement no longer holds.
        const double MinIntra = MinIntraGroupDistance(Cloud);
        if (MinIntra < WellSelfOverlapFloorCm)
        {
            ++RefusedGeometry;
            NoteRefusal(E);
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-ROLL core='%s': REFUSED -- its own members sit %.1f cm apart, inside the ")
                TEXT("%.0f cm reject radius, so this group can never validate its own footprint. H0 ")
                TEXT("measured a minimum of 1818.8 cm over 401 pairs and design §4b concluded no ")
                TEXT("same-group exemption was needed; IF YOU ARE READING THIS LINE, that measurement no ")
                TEXT("longer holds and the exemption question is reopened. %s"),
                *WellShort(E.CorePath), MinIntra, WellSelfOverlapFloorCm, *KeptOrVanilla(E));
            continue;
        }

        // Every layout record must match a live actor BY PATH. Counted, not written: under T7b/A3 the
        // entry is not touched until a destination exists, so this check has to answer from the map.
        int32 Matched = 0;
        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            if (OffsetByPath.Find(S.SatellitePath)) { ++Matched; }
        }
        if (Matched != E.Satellites.Num())
        {
            // Reachable only if a path in the layout is not a path in the census while the counts match
            // -- i.e. the two views name different satellites. Refuse: a partial rigid body is exactly
            // the shrunk well this gate exists to prevent.
            ++RefusedUnstreamed;
            NoteRefusal(E);
            // Only cleared for a never-placed entry. On an already-placed one this flag describes the
            // rigid body the standing well was built from, and that body is still correct -- clearing
            // it here would degrade a working well to satisfy a refusal about a capture we did not take.
            if (!bAlreadyPlaced) { E.bOffsetsCaptured = false; }
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-ROLL core='%s': REFUSED -- only %d of %d satellite records matched a live ")
                TEXT("actor BY PATH even though the counts agree. The layout and the census name ")
                TEXT("different satellites. %s"),
                *WellShort(E.CorePath), Matched, E.Satellites.Num(), *KeptOrVanilla(E));
            continue;
        }

        // -------- A CANDIDATE. NOTHING IS WRITTEN TO THE ENTRY HERE (T7b/A3) --------
        FNodeShuffleWellPendingCapture P;
        P.EntryIndex = EntryIdx;
        P.CorePath = E.CorePath;
        P.bWasPlaced = bAlreadyPlaced;
        P.CoreLoc = CoreLoc;
        P.CoreYaw = CoreYaw;
        P.MinIntra = MinIntra;
        P.OffsetByPath = MoveTemp(OffsetByPath);
        Pending.Add(MoveTemp(P));

        ++Captured;
        if (bAlreadyPlaced) { ++CapturedWhilePlaced; }

        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-ROLL core='%s' CAPTURED as a %s candidate: %d satellite(s), rigid body about the ")
            TEXT("core at %s (coreYaw=%.1f deg, minIntraGroup=%.0f cm vs %.0f cm reject radius). NOTHING ")
            TEXT("is written to this entry yet -- T7b/A3 holds the capture outside the save until a ")
            TEXT("destination exists, so a failed deal cannot leave the well nowhere."),
            *WellShort(E.CorePath),
            bAlreadyPlaced ? TEXT("RE-ENROLMENT (already relocated)") : TEXT("first-relocation"),
            E.Satellites.Num(), *CoreLoc.ToCompactString(), CoreYaw, MinIntra, WellSelfOverlapFloorCm);
    }

    // ns-t7b F5 / RT-7: the buckets must PARTITION the layout. Asserted, not assumed -- the old
    // counters overlapped and nothing said so.
    const int32 Accounted = PermanentlyFailed + RefusedPinned + AlreadyKept + RefusedUnstreamed
                          + RefusedTooFew + RefusedGeometry + RefusedNonFinite + Captured;
    if (Accounted != WellLayout.Num())
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-ROLL: *** COUNTER PARTITION BROKEN *** phase 1 accounted for %d outcome(s) ")
            TEXT("across %d layout entry(ies). Every entry must land in exactly ONE of: permanently ")
            TEXT("failed %d, pinned/unmanaged %d, kept-as-placed %d, not streamed %d, below the ")
            TEXT("satellite floor %d, geometry %d, non-finite %d, captured %d."),
            Accounted, WellLayout.Num(), PermanentlyFailed, RefusedPinned, AlreadyKept,
            RefusedUnstreamed, RefusedTooFew, RefusedGeometry, RefusedNonFinite, Captured);
    }

    // -------- PHASE 2: DEAL A DESTINATION TO EVERY CANDIDATE --------
    // Drawn from the SAME map-wide deal box the node roll uses, filtered by the learned water grid and
    // spaced from every other layout entry AND every other well destination. The stream is derived from
    // the layout seed but SEPARATE from it (salted 'WLR2'), for the reason H1 states for its own salted
    // stream: consuming draws from RollLayout's Rng would shift every subsequent draw and silently
    // re-shuffle the player's ordinary nodes just by turning this feature on.
    //
    // ns-review-h2 F15 -- THE PRECISE CLAIM, because the earlier wording overstated it. WITHIN A SINGLE
    // ROLL, enabling relocation shifts no draw of the node layout or of the well retype: the streams are
    // disjoint and this one runs last. ACROSS RE-ROLLS IT IS NOT ISOLATED, and saying otherwise would
    // leave a future reader designing against a guarantee that does not exist. H2's footprint probes call
    // RaycastGroundAt, which TEACHES the persistent learned water grid one cell per probe, and that grid
    // gates TryRedealWaterLockedEntry's candidate filter for ORDINARY nodes. So a save where relocation
    // ran has a better-populated water grid, and a later water-locked node redeal can therefore land
    // somewhere it would not otherwise have landed. The node layout is not a pure function of the seed
    // once ANY probing has happened -- that was already true of the mod before H2; H2 only adds probes.
    FRandomStream DealRng(Seed ^ 0x574C5232 /* 'WLR2' */);
    const FVector BoxMin = GetDealBoundsMin();
    const FVector BoxMax = GetDealBoundsMax();
    const bool bBoxUsable = (BoxMax.X > BoxMin.X && BoxMax.Y > BoxMin.Y);
    const float ProbeZ = (DealMeanZ != 0.0f) ? DealMeanZ : 0.0f;

    // Deterministic deal order. Actor-iteration order (and therefore WellLayout's own order after a
    // merge) is streaming-dependent, so dealing in it would make the same seed produce different
    // destinations from different vantage points -- the exact class of bug §Q3a's determinism rule
    // exists to prevent. Sorted by CorePath, as RollWellLayout already sorts its deal. The COMMIT phase
    // then walks this same sorted array, so both halves are order-stable.
    Pending.Sort([](const FNodeShuffleWellPendingCapture& A, const FNodeShuffleWellPendingCapture& B)
    {
        return A.CorePath < B.CorePath;
    });

    // ===============================================================================================
    // T7b/A3 ORDERING SUBTLETY -- A VACATING SITE IS EXCLUDED FROM ITS OWN DRAW, AND FROM NOBODY ELSE'S.
    // ===============================================================================================
    // Before T7b the re-enrolment block had already cleared bGroupPlaced by the time this array was
    // built, so a re-enrolling well's old site simply was not in it. Under A3 the entry is still placed
    // here, so its old site IS in the list -- and if it blocked the well's own draw, a well could never
    // be re-dealt anywhere within 2 x 6500 cm of where it stands.
    //
    // The alternative (drop every candidate's site from the list for everyone) was rejected: a
    // candidate whose deal FAILS keeps its site, and any other well already dealt into that vacated
    // space would then be sitting on a well that never moved. Excluding a site from ITS OWN draw only
    // cannot produce that state -- the only entry that may ignore the site is the entry that owns it,
    // and it either vacates the site itself or was never dealt anything. The cost is that a vacated
    // site stays reserved for the rest of this roll; with a map-wide deal box and ~20 wells at 130 m
    // spacing that is not a scarce resource.
    TArray<FVector> WellDestinations;
    TMap<int32, int32> SiteIndexByEntry;
    TSet<int32> PendingEntry;
    for (const FNodeShuffleWellPendingCapture& P : Pending) { PendingEntry.Add(P.EntryIndex); }
    for (int32 i = 0; i < WellLayout.Num(); ++i)
    {
        const FNodeShuffleWellEntry& E = WellLayout[i];
        if (E.bGroupPlaced) { SiteIndexByEntry.Add(i, WellDestinations.Add(E.PlacedCoreLocation)); }
        // ns-t7b-r2 F-B: a DEALT-BUT-UNPLACED site has NO actors on it, and pre-T7b the capture
        // cleared bDestDealt BEFORE this list was built -- so a candidate's stale destination was
        // never a blocking site for anybody. A3 moved the clear into phase 3, which silently made it
        // one. Excluded here so the toggle-OFF deal really is the pre-T7b deal.
        else if (E.bDestDealt && !PendingEntry.Contains(i))
        {
            SiteIndexByEntry.Add(i, WellDestinations.Add(E.DestCoreLocation));
        }
    }
    for (FNodeShuffleWellPendingCapture& P : Pending)
    {
        if (const int32* SiteIdx = SiteIndexByEntry.Find(P.EntryIndex)) { P.SelfSiteIndex = *SiteIdx; }
    }

    int32 Dealt = 0, DealFailed = 0, DealFailedWhilePlaced = 0;
    for (FNodeShuffleWellPendingCapture& P : Pending)
    {
        FNodeShuffleWellEntry& E = WellLayout[P.EntryIndex];
        if (!bBoxUsable)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-ROLL core='%s': no usable deal box (degenerate layout) -- no destination can ")
                TEXT("be drawn for any well this roll."), *WellShort(E.CorePath));
        }
        else
        {
            // A well needs clearance for its WHOLE footprint, not for a point. Inset the box by the
            // largest measured bounding radius so the outer satellites of a group dealt near the edge
            // are still inside the playable area, and space destinations by that radius plus the node
            // spacing.
            const float Inset = WellMaxBoundRadiusCm;
            const float MinX = BoxMin.X + Inset, MaxX = BoxMax.X - Inset;
            const float MinY = BoxMin.Y + Inset, MaxY = BoxMax.Y - Inset;
            for (int32 Try = 0; Try < WellRedealTries && !P.bDealt; ++Try)
            {
                const FVector Cand(DealRng.FRandRange(MinX, MaxX), DealRng.FRandRange(MinY, MaxY), ProbeZ);
                if (IsKnownWaterCell(Cand)) { continue; }
                bool bTooClose = false;
                for (int32 d = 0; d < WellDestinations.Num(); ++d)
                {
                    if (d == P.SelfSiteIndex) { continue; } // this well's own vacating site
                    if (FVector::DistSquared2D(WellDestinations[d], Cand) < FMath::Square(2.0f * WellMaxBoundRadiusCm))
                    {
                        bTooClose = true; break;
                    }
                }
                if (bTooClose) { continue; }
                for (const FNodeShuffleEntry& Node : Layout)
                {
                    if (!Node.bActive) { continue; }
                    if (FVector::DistSquared2D(Node.Location, Cand)
                        < FMath::Square(WellMaxBoundRadiusCm + WellMinNodeSpacingCm))
                    {
                        bTooClose = true; break;
                    }
                }
                if (bTooClose) { continue; }

                const int32 OtherSites = WellDestinations.Num() - (P.SelfSiteIndex != INDEX_NONE ? 1 : 0);
                P.Dest = Cand;
                P.bDealt = true;
                WellDestinations.Add(Cand);
                ++Dealt;
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WELLH2-ROLL core='%s' DEALT %s -> %s (attempt %d of %d; settles + rotates on ")
                    TEXT("discovery). Spaced against %d other well site(s); a well's own vacating site is ")
                    TEXT("excluded from its OWN draw only, never from another well's."),
                    *WellShort(E.CorePath), *P.CoreLoc.ToCompactString(), *Cand.ToCompactString(),
                    Try + 1, WellRedealTries, OtherSites);
            }
        }
        if (!P.bDealt)
        {
            ++DealFailed;
            if (P.bWasPlaced) { ++DealFailedWhilePlaced; }
        }
    }

    if (DealFailed > 0 && Captured > 5)
    {
        // ns-t7b F3: WellRedealTries is OUR number, not the game's. It was sized when a roll enrolled
        // 3-5 wells; T7b can hand it every unpinned well in the save. Reported with the denominator so
        // the cap can be re-sized from a real session instead of by feel. The cap itself lives in
        // NodeShuffleSubsystem.h, which this packet does not own.
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-ROLL: %d of %d candidate well(s) found no destination in %d draws each. That cap ")
            TEXT("(WellRedealTries) was chosen when a roll enrolled 3-5 wells and this roll asked it for ")
            TEXT("%d. A well that fails here is never left with nowhere to be (T7b/A3) -- it keeps ")
            TEXT("whatever place it already had."),
            DealFailed, Captured, WellRedealTries, Captured);
    }

    // -------- PHASE 3: COMMIT. THE ONLY PLACE THIS ROLL WRITES A RELOCATION DECISION --------
    // Everything destructive lives below this line, and it is reached only with a destination in hand.
    const double CommitStartSec = FPlatformTime::Seconds();
    int32 Enrolled = 0, ReEnrolled = 0, AbortedInUse = 0, RefusedNoHandles = 0, AbortedAfterTeardown = 0,
          TeardownMembers = 0;

    // ns-t23-stage0 INSTRUMENT 1 (K1) -- LOG ONLY, AND ONLY WHEN DIAGNOSTICS ARE ON.
    // State for the roll-time mesh-index probe below. Declared out here so the index is rebuilt ONCE per
    // roll rather than once per enrolled well: the rebuild is a two-iterator sweep of the whole world,
    // and charging it per well would both cost more and misreport the design's cost. The three locals
    // carry the measurement to every probe call so each printed line names the same rebuild.
    const bool bStage0Diag = FNodeShuffleModule::AreDiagnosticsEnabled();
    bool bStage0IndexBuilt = false;
    double Stage0IndexMs = 0.0;
    int32 Stage0BuiltAtCommit = 0;

    // ns-t23-rollhide (T23 stage 3) -- counters for the roll-time commitment, and ONE shared index build.
    // The index rebuild is now wanted by TWO consumers (stage 0's probe and the hide itself) and must
    // still happen at most ONCE per roll: it is a two-iterator sweep of the whole world, and building it
    // per enrolled well would cost ~17x and misreport the design's cost. So the build is a lambda both
    // consumers call, and bStage0IndexBuilt is the single latch.
    int32 RollHidden = 0, RollFellBack = 0;
    FString RollFellBackNames;
    const auto EnsureRollMeshIndexOnce = [&]() -> void
    {
        if (bStage0IndexBuilt) { return; }
        // RebuildWellMeshIndex prints its WELLH2B-INDEX summary keyed on WellAuditPasses, which the roll
        // does NOT increment. On a mid-session re-roll that is the PREVIOUS apply pass's number, so the
        // roll-time summary and that pass's summary are indistinguishable by pass number. Name the
        // roll-time one rather than changing the shared line.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2B-ROLLPROBE: the NEXT WELLH2B-INDEX line was produced by this ROLL, not by an ")
            TEXT("apply pass. It prints pass=%d because the roll does not increment WellAuditPasses; on a ")
            TEXT("mid-session re-roll that is the previous apply pass's number."),
            WellAuditPasses);
        const double T0 = FPlatformTime::Seconds();
        // RebuildWellMeshIndex, NEVER EnsureWellMeshIndex. The Ensure wrapper early-returns when
        // WellMeshIndexPass == WellAuditPasses, and this roll runs BEFORE ApplyLayout increments
        // WellAuditPasses -- so on a mid-session re-roll the wrapper would hand back an index built before
        // this roll's decisions, and the hide would then look for pieces of a world that no longer
        // matches. That is Appendix item 1 of the T23 design and a PRE-EXISTING defect of the wrapper.
        RebuildWellMeshIndex();
        Stage0IndexMs = (FPlatformTime::Seconds() - T0) * 1000.0;
        bStage0IndexBuilt = true;
        Stage0BuiltAtCommit = Enrolled + 1;
    };

    for (const FNodeShuffleWellPendingCapture& P : Pending)
    {
        FNodeShuffleWellEntry& E = WellLayout[P.EntryIndex];

        if (!P.bDealt)
        {
            // THE STATE T7b/A3 EXISTS FOR. The entry has not been touched since phase 1, so there is
            // nothing to roll back: an already-relocated well is still standing at its placement with a
            // live claim, and a never-placed one is still vanilla.
            E.bRelocate = P.bWasPlaced;
            if (P.bWasPlaced)
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WELLH2-ROLL core='%s': no destination cleared the water grid and spacing ")
                    TEXT("filters in %d draws -- NOT re-enrolled. It KEEPS the relocated placement it ")
                    TEXT("already has at %s: nothing was despawned, no claim was withdrawn and no state ")
                    TEXT("was rewritten (T7b/A3)."),
                    *WellShort(E.CorePath), WellRedealTries, *E.PlacedCoreLocation.ToCompactString());
            }
            else
            {
                // F-F: HEAD cleared these in the capture; A3 must clear them here or a dead
                // destination survives into the next roll's spacing list.
                E.bDestDealt = false;
                E.DestCoreLocation = FVector::ZeroVector;
                // ns-t23-rollhide -- TERMINAL FAILURE SITE 2 OF 2. This entry has just lost its
                // destination, and it was never placed. The invariant roll-time commitment depends on --
                // "suppressed implies a live destination" -- no longer holds for it, so any suppression a
                // PREVIOUS roll took must be given back. Attempted immediately because at THIS instant the
                // origin is provably resident (phase 1 refuses to enrol a well whose core is absent from
                // this roll's census or whose satellite records do not all match live actors), which is
                // the strongest residency guarantee anywhere in the lifecycle; the intent is still
                // PERSISTED first, so a member that nonetheless fails to resolve is re-attempted every
                // apply pass rather than silently dropped.
                //
                // A RE-DEAL DELIBERATELY DOES NOT COME HERE. An entry that gets a NEW destination keeps a
                // valid suppression, so this is reached only where the destination is GONE.
                if (WellGroupHasSuppressedMember(E))
                {
                    E.bUnhidePending = true;
                    TryUnhideWellGroup(E, TEXT("roll found no destination for a never-placed well"));
                }
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WELLH2-ROLL core='%s': no destination cleared the water grid and spacing ")
                    TEXT("filters in %d draws -- left vanilla this roll. The next roll draws again. ")
                    TEXT("Restore obligation outstanding after this roll: %d."),
                    *WellShort(E.CorePath), WellRedealTries, E.bUnhidePending ? 1 : 0);
            }
            continue;
        }

        // ===========================================================================================
        // GATE 1 -- OCCUPANCY, RE-ASKED ON THE SPAWNED HANDLES IMMEDIATELY BEFORE THE FIRST DESTROY.
        // ===========================================================================================
        // The roll-time pin (bManaged, via T16's EvaluateWellPin) already refused a built-on well far
        // above. This asks the same question again, of the same shared predicate, at the last instant
        // before anything is destroyed -- because the roll-time answer was taken before the deal loop
        // ran and this is the line after which a mistake is irreversible. Applied to ALREADY-PLACED
        // candidates only: a never-placed candidate's spawned handles come from an incomplete earlier
        // spawn, whose in-use case has its own established owner (the orphan sweep), and widening this
        // gate to that population is a behaviour change T7b was not asked for.
        if (P.bWasPlaced)
        {
            const TCHAR* InUseWhy = TEXT("");
            FString InUseActor;
            int32 Tested = 0;
            if (AFGResourceNodeFrackingCore* PlacedCore = SpawnedWellCores.FindRef(E.CorePath))
            {
                ++Tested;
                if (IsWellMemberInUse(PlacedCore, InUseWhy)) { InUseActor = PlacedCore->GetName(); }
            }
            if (InUseActor.IsEmpty())
            {
                for (const FNodeShuffleWellSatellite& S : E.Satellites)
                {
                    AFGResourceNodeFrackingSatellite* PlacedSat = SpawnedWellSatellites.FindRef(S.SatellitePath);
                    if (!PlacedSat) { continue; }
                    ++Tested;
                    if (IsWellMemberInUse(PlacedSat, InUseWhy)) { InUseActor = PlacedSat->GetName(); break; }
                }
            }
            if (!InUseActor.IsEmpty())
            {
                ++AbortedInUse;
                E.bRelocate = true;
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("WELLH2-ROLL core='%s': *** RE-ENROLMENT ABORTED -- A MEMBER IS IN USE *** %s ")
                    TEXT("reports '%s'. Tested %d of %d member handle(s) live for this group. The dealt ")
                    TEXT("destination %s is DISCARDED, nothing was despawned, the claim stands and the ")
                    TEXT("well keeps its placement at %s. bManaged=%d bPinned=%d -- with bManaged=1 the ")
                    TEXT("roll-time pin and this commit-time check disagreed."),
                    *WellShort(E.CorePath), *InUseActor, InUseWhy, Tested, 1 + E.Satellites.Num(),
                    *P.Dest.ToCompactString(), *E.PlacedCoreLocation.ToCompactString(),
                    E.bManaged ? 1 : 0, E.bPinned ? 1 : 0);
                continue;
            }
            // ns-t7b-r2 F-A (BLOCKER): 0 RESOLVED HANDLES IS NOT A CLEARANCE.
            // Both safety layers for this population read the SAME session-scoped maps: T16's
            // EvaluateWellPin falls back to the HIDDEN vanilla actors when the spawned core does not
            // resolve (source "original-FALLBACK(relocated, no spawned handle)"), and that verdict is
            // deliberately NON-DECISIVE, so E.bPinned keeps its SAVED value -- false for a well built
            // on since the last roll. This gate then tested nothing and reported PASSED. The maps are
            // UPROPERTY() and NOT SaveGame (NodeShuffleSubsystem.h:1830-1831); AdoptRestoredWellGroups
            // is single-shot at first apply (NodeShuffleWellLink.cpp:138), so an entry whose
            // destination has not been visited THIS SESSION has no handle at all.
            // MEASURED, not reasoned: nothing in phase 1 proves the DESTINATION is streamed -- the
            // census gate proves the VANILLA ORIGIN is, and those are different places.
            // Refusing costs one re-roll taken near the well. Passing costs the player's factory.
            if (Tested == 0)
            {
                ++RefusedNoHandles;
                E.bRelocate = true;
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("WELLH2-ROLL core='%s': *** RE-ENROLMENT REFUSED -- NOTHING TO ASK *** 0 of %d ")
                    TEXT("member handle(s) for this relocated group resolved in SpawnedWellCores/")
                    TEXT("SpawnedWellSatellites, so NO occupancy question was answered for it. That is ")
                    TEXT("NOT a clearance: bPinned=%d is the SAVED value and the roll-time pin resolved ")
                    TEXT("against %s. The dealt destination %s is DISCARDED and the well keeps its ")
                    TEXT("placement at %s. Visit the well once this session, then re-roll."),
                    *WellShort(E.CorePath), 1 + E.Satellites.Num(), E.bPinned ? 1 : 0,
                    TEXT("actors that may not be resident"), *P.Dest.ToCompactString(),
                    *E.PlacedCoreLocation.ToCompactString());
                continue;
            }
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH2-ROLL core='%s': re-enrolment occupancy gate PASSED -- %d of %d member ")
                TEXT("handle(s) were live and none reported in use, so the teardown may run."),
                *WellShort(E.CorePath), Tested, 1 + E.Satellites.Num());
        }

        // ns-review-h3 H1 -- THE MOST REACHABLE OF THE THREE ROUTES INTO THAT BUG.
        // This commit resets the destination and the whole search state, and hands the group a fresh
        // spot. Until this call existed it did that WITHOUT TOUCHING EITHER RUNTIME MAP, so a
        // previously-relocated well's actors stayed live at the old destination while the entry moved
        // somewhere new -- and the reuse in SpawnWellGroup then happily adopted them there.
        //
        // ns-t7b F7: THE RETURN VALUE IS NOW READ. It was computed, logged and thrown away at both call
        // sites; it is the one signal that says "a member was occupied and left standing", and on this
        // path it is the difference between stopping and stranding.
        const FVector WasPlacedAt = E.PlacedCoreLocation;
        const bool bDespawnAllClear = DespawnWellGroup(E, TEXT("re-enrolled by a new roll"));
        if (P.bWasPlaced) { TeardownMembers += 1 + E.Satellites.Num(); }

        if (!bDespawnAllClear && P.bWasPlaced)
        {
            ++AbortedAfterTeardown;
            E.bRelocate = true;
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-ROLL core='%s': *** RE-ENROLMENT ABORTED AFTER THE TEARDOWN *** ")
                TEXT("DespawnWellGroup returned bAllClear=0, so at least one member was refused as ")
                TEXT("occupied after the gate above passed, and some members of this group have already ")
                TEXT("been destroyed. This entry KEEPS bGroupPlaced and its live claim on %s, so its ")
                TEXT("committed coordinates are unchanged and no new destination was written. Grep ")
                TEXT("WELLH2-DESPAWN on this core for which members went and which were refused."),
                *WellShort(E.CorePath), *WasPlacedAt.ToCompactString());
            continue;
        }
        if (!bDespawnAllClear)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-ROLL core='%s': DespawnWellGroup returned bAllClear=0 on a well that was NOT ")
                TEXT("placed -- a member of an incomplete earlier spawn is occupied and was left ")
                TEXT("standing. The enrolment CONTINUES for this well (the pre-T7b behaviour for this ")
                TEXT("population is unchanged); the orphan sweep owns the member that stayed."),
                *WellShort(E.CorePath));
        }

        // -------- COMMIT THE RIGID BODY, KEYED BY PATH, NEVER BY INDEX --------
        // Deferred to here on purpose (T7b/A3): until a destination existed, an already-placed entry had
        // to keep the state that describes the well it is standing at -- S.bPlaced in particular, which
        // the audit cross-checks against the live handle.
        E.VanillaCoreLocation = P.CoreLoc;
        E.VanillaCoreYawDeg = P.CoreYaw;
        int32 Written = 0;
        for (FNodeShuffleWellSatellite& S : E.Satellites)
        {
            const TPair<FVector, float>* Rec = P.OffsetByPath.Find(S.SatellitePath);
            if (!Rec) { continue; } // impossible under phase 1's match check; costs nothing to be sure
            S.LocalOffset = Rec->Key;
            S.LocalYawDeg = Rec->Value;
            S.bPlaced = false;
            S.bCaptured = true; // ns-review-h2 F2: the ONLY place this is ever set true
            // ns-review-h2-r4 F-5 -- THE SATELLITE ZEROING THAT USED TO SIT HERE IS DELETED.
            // It ran before ClearAbandonedWellPlacement, which is the function that RECORDS a withdrawn
            // coordinate into AbandonedWellClaimCoords so pass B can tell one of our stranded actors
            // from a mis-classified vanilla well. Zeroing here destroyed every satellite coordinate
            // before the clearer could see it -- and with a measured minimum core->satellite distance of
            // 2076 cm against a 300 cm adopt radius, and satellites outnumbering cores ~4-8:1, that left
            // the discriminator DEAD FOR THE MORE NUMEROUS CLASS. It was also redundant: the clearer
            // zeroes every satellite itself.
            ++Written;
        }
        E.bOffsetsCaptured = true;
        E.CapturedSatelliteCount = Written; // ns-review-h2 F2: the count the audit is measured against
        E.bRelocate = true;

        // A fresh enrolment starts its search from scratch: no destination, cursor at zero, budgets
        // full. Explicit rather than relying on defaults, because a well can be re-enrolled after an
        // earlier roll left partial state.
        E.bDestDealt = false;
        E.DestCoreLocation = FVector::ZeroVector;
        E.YawCursor = 0;
        E.GroupNudges = 0;
        E.GroupRedeals = 0;
        E.GroupYawDeg = 0.0f;
        E.bGroupPlaced = false;

        // ns-review-h2-r2 F-A (BLOCKING) -- AND WITHDRAW THE CLAIM, WHICH THIS BRANCH RESET EVERYTHING
        // ELSE EXCEPT. On entry to this line PlacedCoreLocation AND every satellite PlacedLocation are
        // still NON-ZERO; PlacedCoreLocation names the destination this entry has just walked away from,
        // while bRelocate is true and bRelocationFailed false. That combination defeats both of F2's
        // layers, so an occupied core DespawnWellGroup had REFUSED to destroy went on being reported as
        // "mid-assembly at its committed coordinate: OWNED, not orphaned" forever.
        //
        // ORDERING, TWICE LOAD-BEARING -- this call must sit HERE and nowhere else:
        //   * AFTER DespawnWellGroup, because its *** ABANDONED IN PLACE *** line prints
        //     PlacedCoreLocation to say WHERE it left occupied actors, and that must be the real
        //     coordinate, not a zero. Same rationale as NodeShuffleWellEscalate.cpp:181.
        //   * AFTER `E.bGroupPlaced = false` above, because ClearAbandonedWellPlacement RETURNS EARLY
        //     on bGroupPlaced -- placed at the despawn it would silently no-op for exactly the
        //     previously-placed wells this fix exists for.
        // If you are here to "restore" a satellite zeroing above so that a comment becomes true again:
        // DON'T. You would be undoing ns-review-h2-r4 F-5.
        const bool bClaimWithdrawn = ClearAbandonedWellPlacement(E, TEXT("re-enrolled by a new roll"));

        E.DestCoreLocation = P.Dest;
        E.bDestDealt = true;
        // ns-t23-stage0 INSTRUMENT 2 (K3): the deferral window starts HERE, at the one site in the mod
        // where bDestDealt goes false -> true, and it is the same instant a roll-time commit design would
        // hide the vanilla well. Zeroed rather than left alone so a re-enrolment restarts the clock; the
        // previous enrolment's elapsed value has already been recorded in PassesFromDealToPlaced if that
        // enrolment ever placed. Diagnostic field, read by no decision.
        E.PassesSinceDealt = 0;
        E.PassesFromDealToPlaced = 0;

        // ns-t23-stage0 INSTRUMENT 1 (K1) -- THE DESIGN-KILLER PROBE. LOG ONLY. NOTHING BELOW CAPTURES,
        // SUPPRESSES OR HIDES ANYTHING; it counts what a roll-time capture WOULD have had to work with.
        //
        // RebuildWellMeshIndex, NEVER EnsureWellMeshIndex. The Ensure wrapper early-returns when
        // WellMeshIndexPass == WellAuditPasses (NodeShuffleWellMeshIndex.cpp), and this roll runs BEFORE
        // ApplyLayout increments WellAuditPasses -- so on a mid-session re-roll the wrapper would hand
        // back an index built before this roll's decisions and the probe would measure the wrong world.
        // That is Appendix item 1 of the T23 design, and it is a PRE-EXISTING defect of the wrapper, not
        // one this packet introduces: on the FIRST roll of a session the defaults make the wrapper
        // accidentally correct, which is why it has never been caught.
        //
        // Gated on the diagnostics flag because the rebuild is real new work on the roll path (see the
        // measured cost printed by the probe itself). With diagnostics off this branch does nothing at
        // all and the roll behaves exactly as it did before ns-t23-stage0.
        if (bStage0Diag)
        {
            EnsureRollMeshIndexOnce();
            ProbeRollTimeWellMeshIndex(E, Stage0IndexMs, Stage0BuiltAtCommit);
        }

        // ============ ns-t23-rollhide (T23 STAGE 3): COMMIT THE VANILLA WELL AT THE ROLL ============
        // THE ONE THING THE AUTHOR HAS ASKED FOR REPEATEDLY: when a shuffle happens, the vanilla
        // original DISAPPEARS NOW instead of standing there until the player travels to the destination.
        //
        // ORDER IS FORCED AND IS NOT AN ORDERING PREFERENCE:
        //   1. RebuildWellMeshIndex (via the shared latch above) -- the hide operates on INDEXED mesh
        //      pieces, and stage 0 MEASURED that the index is just as complete at roll time as at
        //      suppression time on the author's save (15 pieces / 135 members, route 3 going 7 -> 8).
        //   2. CaptureWellGroupVisuals -- the pieces are the last copy of the look we will need at the
        //      destination, so the capture must run while the originals are still standing.
        //   3. THE COMPLETENESS GATE -- roll-time capture is ONE SHOT (the apply-time capture retries
        //      every pass while the origin streams). An entry whose look is not completely captured does
        //      NOT get roll-time suppression and falls back to today's spawn-then-suppress path, COUNTED
        //      AND NAMED. That converts a silent fallback into an explicit, denominated refusal.
        //   4. Suppress, on the ROLL phase -- which is what stops SuppressVanillaWellGroup redoing 1 and
        //      2 per group, and what makes its summary line name the destination instead of a stale or
        //      zero PlacedCoreLocation.
        //
        // Occupancy is NOT re-tested here: HideOne refuses any member reporting IsWellMemberInUse, on
        // this path exactly as on the apply path, through the same shared predicate. One predicate,
        // every site -- do not add a second copy of that question here.
        if (bCommitAtRoll)
        {
            EnsureRollMeshIndexOnce();
            CaptureWellGroupVisuals(E);
            int32 CapMembers = 0, CapMissing = 0;
            if (IsWellGroupCaptureComplete(E, CapMembers, CapMissing))
            {
                SuppressVanillaWellGroup(E, EWellSuppressPhase::Roll);
                ++RollHidden;
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WELLH2-ROLLHIDE core='%s': COMMITTED AT THE ROLL -- the look of all %d dressable ")
                    TEXT("member(s) is captured, so the vanilla group is suppressed now rather than after ")
                    TEXT("the replacement is built. The replacement is still only built when a player ")
                    TEXT("reaches %s, so this well is absent from the world until then. Every member we ")
                    TEXT("hid carries a persisted restore obligation (grep WELLH2-STRANDED for the ")
                    TEXT("population that has not been given back)."),
                    *WellShort(E.CorePath), CapMembers, *P.Dest.ToCompactString());
            }
            else
            {
                ++RollFellBack;
                if (RollFellBackNames.Len() < 400)
                {
                    RollFellBackNames += (RollFellBackNames.IsEmpty() ? TEXT("") : TEXT(", "));
                    RollFellBackNames += WellShort(E.CorePath);
                }
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WELLH2-ROLLHIDE core='%s': NOT committed at the roll -- %d of %d dressable ")
                    TEXT("member(s) hold no captured look at this instant, and the roll-time capture is ")
                    TEXT("taken once and not retried. This entry keeps the ORIGINAL behaviour: the vanilla ")
                    TEXT("well stays standing and is suppressed only once the replacement has been built ")
                    TEXT("at %s. Nothing was hidden for it on this roll."),
                    *WellShort(E.CorePath), CapMissing, CapMembers, *P.Dest.ToCompactString());
            }
        }

        ++Enrolled;
        if (P.bWasPlaced) { ++ReEnrolled; }

        // ns-t7b F6: a re-enrolment and a first relocation used to print a byte-identical line, and the
        // only other evidence -- a WELLH2-DESPAWN with reason "re-enrolled by a new roll" -- is gated on
        // something actually being destroyed, so a placed entry whose handles were already dropped moved
        // again with nothing in the log saying so. The two are now named apart, and the previous
        // placement is printed beside the new destination.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-ROLL core='%s' %s: %d satellite(s), rigid body captured about the core at %s ")
            TEXT("(coreYaw=%.1f deg, minIntraGroup=%.0f cm vs %.0f cm reject radius), dealt %s. ")
            TEXT("previousPlacement=%s claimWithdrawn=%d."),
            *WellShort(E.CorePath),
            P.bWasPlaced ? TEXT("RE-ENROLLED (MOVED AGAIN)") : TEXT("ENROLLED (FIRST RELOCATION)"),
            Written, *P.CoreLoc.ToCompactString(), P.CoreYaw, P.MinIntra, WellSelfOverlapFloorCm,
            *P.Dest.ToCompactString(), *WasPlacedAt.ToCompactString(), bClaimWithdrawn ? 1 : 0);
    }

    const double CommitMs = (FPlatformTime::Seconds() - CommitStartSec) * 1000.0;

    // ns-t23-rollhide: the roll-level summary, WITH ITS DENOMINATOR. "0 fell back" means nothing unless
    // the line says how many entries were given the chance to; and with the toggle OFF the line states
    // that, rather than being absent and leaving a reader to guess which of the two it is looking at.
    // The index-rebuild cost is the one this design adds to the roll path and is reported as measured.
    if (Enrolled > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-ROLLHIDE summary: commit-at-roll was %s for this roll. Of %d committed well(s), ")
            TEXT("%d had the vanilla group suppressed AT THE ROLL and %d fell back to the original ")
            TEXT("suppress-after-the-replacement-is-built path because their look was not completely ")
            TEXT("captured at this instant (%s). Mesh index rebuilt %s for this roll, %.1f ms measured."),
            bCommitAtRoll ? TEXT("ON") : TEXT("OFF"), Enrolled, RollHidden, RollFellBack,
            RollFellBackNames.IsEmpty() ? TEXT("none named") : *RollFellBackNames,
            bStage0IndexBuilt ? TEXT("once") : TEXT("not at all"), Stage0IndexMs);
    }
    if (ReEnrolled > 0 || AbortedAfterTeardown > 0)
    {
        // ns-t7b RT-6: the first re-roll on a save full of relocated wells tears down every one of them
        // in a single frame. This is that number, measured, instead of an estimate in a review.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-ROLL: re-enrolment teardown -- %d already-relocated group(s) torn down this ")
            TEXT("roll, up to %d member Destroy() call(s) in this one frame, %.1f ms measured across the ")
            TEXT("whole commit phase. No per-pass destroy cap applies on this path ")
            TEXT("(WellSweepMaxDestroysPerPass gates the SWEEP, not DespawnWellGroup). If this is a ")
            TEXT("visible stall, this is the measurement to act on."),
            ReEnrolled, TeardownMembers, CommitMs);
    }

    // ns-review-h2-r2 F-I: the roll's TEARDOWN TAIL lives in NodeShuffleWellSweep.cpp next to the sweep
    // it drives (this file was at exactly 500 lines and the F-A fix pushed it over). It withdraws the
    // claim of every entry this roll abandoned, computes the post-roll sweep gate, and runs the sweep.
    FinishWellRollTeardown(bRelocationEnabled);

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-ROLL: %s complete over %d well(s) -- %d captured (%d of them ALREADY RELOCATED and ")
        TEXT("re-enrolled), %d dealt a destination, %d deal-failed (%d of those kept an existing ")
        TEXT("relocated placement), %d committed (%d moved again, %d first relocation), %d aborted ")
        TEXT("in-use before the teardown, %d aborted after it, %d refused because NO member handle ")
        TEXT("resolved (ns-t7b-r2 F-A: 0 handles is not a clearance). Kept-as-placed without ")
        TEXT("re-enrolment %d, ")
        TEXT("not streamed %d, below the %d-satellite floor %d, pinned/unmanaged %d (%d of them already ")
        TEXT("relocated), geometry-refused %d, non-finite %d, permanently failed %d; %d refusal(s) ")
        TEXT("landed on an already-relocated well and KEPT its placement. rerollRelocatedWells=%d. ")
        TEXT("Seed %d (relocation stream %d). K=%d yaw steps, %d tried per pass, %d nudges, %d redeals ")
        TEXT("before a well is left vanilla for good."),
        bIsReroll ? TEXT("re-roll") : TEXT("initial roll"), WellLayout.Num(),
        Captured, CapturedWhilePlaced, Dealt, DealFailed, DealFailedWhilePlaced,
        Enrolled, ReEnrolled, Enrolled - ReEnrolled, AbortedInUse, AbortedAfterTeardown, RefusedNoHandles,
        AlreadyKept, RefusedUnstreamed, WellMinSatellitesForRelocation, RefusedTooFew,
        RefusedPinned, RefusedPinnedWhilePlaced, RefusedGeometry, RefusedNonFinite, PermanentlyFailed,
        RefusedWhilePlaced, bRerollRelocated ? 1 : 0, Seed, Seed ^ 0x574C5232,
        WellYawSteps, WellYawAttemptsPerPass, WellMaxGroupNudges, WellMaxGroupRedeals);
}
