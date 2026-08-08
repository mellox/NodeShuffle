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

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleConfig.h"
#include "NodeShuffleWellCensus.h"   // H0's grouping -- CollectWellCensus + the well record/member types
#include "NodeShuffleWellRetype.h"   // WellPathOf / WellClassPathOf / WellShort
#include "NodeShuffleWellRelocate.h" // the H2 pure helpers

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
    WellIncompleteSpawnCounts.Empty();  // h5 (2): a re-roll re-decides every placement
    bWellOrphanInUseLogged = false;     // h5 F-4
    bWellRelocDisabledLogged = false;
    // h5 F1: the three new throttles reset too -- silencing a GATE line across a re-roll hides a gate.
    bWellSweepGatedLogged = false;
    bWellSweepCapLogged = false;
    bWellBackstopLogOnlyLogged = false;

    FNodeShuffleWellCensus Census;
    CollectWellCensus(GetWorld(), Census);

    // ns-review-h2 F1 (CRITICAL): exclude OUR OWN spawned cores. CollectWellCensus is deliberately
    // unfiltered (NodeShuffle.DumpWells depends on that), so the filter lives at every DECISION site
    // instead -- here and in RollWellLayout's merge loop. Without it a re-roll re-enrols the wells H2
    // already relocated and relocates them again, doubling the actor count each time.
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
            TEXT("and are excluded from re-enrolment (ns-review-h2 F1)."), SkippedOurSpawned);
    }

    int32 Enrolled = 0, RefusedUnstreamed = 0, RefusedTooFew = 0, RefusedPinned = 0,
          RefusedGeometry = 0, RefusedNonFinite = 0, AlreadyCaptured = 0, PermanentlyFailed = 0;

    for (FNodeShuffleWellEntry& E : WellLayout)
    {
        // A well that already exhausted every placement budget in an earlier session stays vanilla for
        // good. Re-enrolling it would restart a search we already proved cannot succeed at this seed,
        // once per re-roll, forever.
        if (E.bRelocationFailed)
        {
            ++PermanentlyFailed;
            E.bRelocate = false;
            continue;
        }

        // A well someone has built on is not ours to MOVE, for a stronger reason than it is not ours
        // to retype: relocating it would leave the pressurizer and every extractor standing on nothing.
        // bManaged is already false for a pinned well (RollWellLayout sets it), so this is the same
        // rule read through the same field, not a second one that could diverge.
        if (!E.bManaged)
        {
            ++RefusedPinned;
            E.bRelocate = false;
            continue;
        }

        // A group already placed keeps its placement across re-rolls of the RESOURCE. The resource may
        // change under a re-roll (that is H1's business and it applies to relocated wells too, through
        // the same AssignedResourceClassPath); the GEOGRAPHY does not churn, because a player who has
        // walked to a relocated well should not find it gone after a re-roll they ran for other
        // reasons. If that is ever wanted it is a deliberate, separate decision.
        if (E.bGroupPlaced)
        {
            ++AlreadyCaptured;
            E.bRelocate = true;
            // ns-review-h2 F2 (CRITICAL): the entry is placed, so nothing above re-captures it -- but
            // RollWellLayout's merge, which ran moments ago, may have APPENDED satellites that streamed
            // in for the first time since the relocation. Those records carry no offset at all. They
            // must never reach the spawn (they would materialise at world origin as live,
            // extractor-snappable nodes and inflate the well's rate), and they must never be silently
            // topped up from the CURRENT world either -- the vanilla group they belong to is suppressed,
            // so their live transforms no longer describe the rigid body we placed. They stay
            // UNCAPTURED: refused at spawn, still suppressed at the original site, and named here.
            if (E.Satellites.Num() != E.CapturedSatelliteCount)
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
                    TEXT("well keeps the %d satellites it was actually placed with. Re-roll after ")
                    TEXT("un-relocating if you want the full group."),
                    *WellShort(E.CorePath), E.CapturedSatelliteCount, E.Satellites.Num(), Uncaptured,
                    *Names, Uncaptured > 5 ? TEXT(", ...") : TEXT(""), E.CapturedSatelliteCount);
            }
            continue;
        }

        const FNodeShuffleWellRecord* const* Found = ByCorePath.Find(E.CorePath);
        if (!Found || !*Found)
        {
            // Not streamed in when the roll ran. NOT an error and NOT a failure -- the well simply
            // stays vanilla until a later roll catches it loaded. Stated, because "quietly left out"
            // is exactly how a relocation feature comes to look like it half-works.
            ++RefusedUnstreamed;
            E.bRelocate = false;
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
            E.bRelocate = false;
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-ROLL core='%s': REFUSED -- the core's location is not finite. Left vanilla."),
                *WellShort(E.CorePath));
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
            E.bRelocate = false;
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-ROLL core='%s': REFUSED -- at least one satellite reported a non-finite ")
                TEXT("location. Left vanilla (a NaN offset would poison every distance comparison in the ")
                TEXT("footprint test and route the well into the PASSED branch of its own validation)."),
                *WellShort(E.CorePath));
            continue;
        }

        if (LiveSats < WellMinSatellitesForRelocation || LiveSats != E.Satellites.Num())
        {
            ++(LiveSats < WellMinSatellitesForRelocation ? RefusedTooFew : RefusedUnstreamed);
            E.bRelocate = false;
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH2-ROLL core='%s': NOT relocated this roll -- %d satellite(s) live vs %d in the ")
                TEXT("layout (floor is %d, H0's measured minimum over 20 wells). This is a STREAMING ")
                TEXT("verdict, not a failure: the well stays exactly vanilla and a later re-roll with the ")
                TEXT("well loaded will enrol it. Moving a well we only partly know would shrink it ")
                TEXT("permanently, and every missing satellite would look routine in the log."),
                *WellShort(E.CorePath), LiveSats, E.Satellites.Num(), WellMinSatellitesForRelocation);
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
            E.bRelocate = false;
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-ROLL core='%s': REFUSED -- its own members sit %.1f cm apart, inside the ")
                TEXT("%.0f cm reject radius, so this group can never validate its own footprint. H0 ")
                TEXT("measured a minimum of 1818.8 cm over 401 pairs and design §4b concluded no ")
                TEXT("same-group exemption was needed; IF YOU ARE READING THIS LINE, that measurement no ")
                TEXT("longer holds and the exemption question is reopened. Left vanilla."),
                *WellShort(E.CorePath), MinIntra, WellSelfOverlapFloorCm);
            continue;
        }

        // -------- COMMIT THE RIGID BODY, KEYED BY PATH, NEVER BY INDEX --------
        E.VanillaCoreLocation = CoreLoc;
        E.VanillaCoreYawDeg = CoreYaw;
        int32 Written = 0;
        for (FNodeShuffleWellSatellite& S : E.Satellites)
        {
            const TPair<FVector, float>* Rec = OffsetByPath.Find(S.SatellitePath);
            if (!Rec) { continue; } // impossible under the count check above; costs nothing to be sure
            S.LocalOffset = Rec->Key;
            S.LocalYawDeg = Rec->Value;
            S.bPlaced = false;
            S.bCaptured = true; // ns-review-h2 F2: the ONLY place this is ever set true
            S.PlacedLocation = FVector::ZeroVector;
            ++Written;
        }
        if (Written != E.Satellites.Num())
        {
            // Reachable only if a path in the layout is not a path in the census while the counts match
            // -- i.e. the two views name different satellites. Refuse: a partial rigid body is exactly
            // the shrunk well this gate exists to prevent.
            ++RefusedUnstreamed;
            E.bRelocate = false;
            E.bOffsetsCaptured = false;
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-ROLL core='%s': REFUSED -- only %d of %d satellite records matched a live ")
                TEXT("actor BY PATH even though the counts agree. The layout and the census name ")
                TEXT("different satellites; left vanilla rather than moving a partial group."),
                *WellShort(E.CorePath), Written, E.Satellites.Num());
            continue;
        }

        E.bOffsetsCaptured = true;
        E.CapturedSatelliteCount = Written; // ns-review-h2 F2: the count the audit is measured against
        E.bRelocate = true;

        // ns-review-h3 H1 (BLOCKING) -- THE MOST REACHABLE OF THE THREE ROUTES INTO THAT BUG.
        // This block resets the destination and the whole search state, and the deal below then hands
        // the group a fresh spot. Until this call existed it did that WITHOUT TOUCHING EITHER RUNTIME
        // MAP, so a previously-relocated well's actors stayed live at the old destination while the
        // entry moved somewhere new -- and the reuse in SpawnWellGroup then happily adopted them
        // there. The sequence that triggers it is the one this feature's own config tooltip
        // instructs, run twice: enable both toggles, re-roll, let a well relocate, re-roll again.
        DespawnWellGroup(E, TEXT("re-enrolled by a new roll"));

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
        // ELSE EXCEPT. :300 above already zeroes every satellite's PlacedLocation; PlacedCoreLocation
        // was left naming the destination this entry has just walked away from, while bRelocate stayed
        // true and bRelocationFailed false. That combination defeats BOTH of F2's layers -- the roll
        // tail below and the sweep's reconciliation both skip on `bRelocate && !failed`, and
        // EntryIsMidAssembly() returns true -- so an occupied core DespawnWellGroup had just REFUSED to
        // destroy (its handle still in the map, *** ABANDONED IN PLACE ***) went on being reported as
        // "genuinely retrying (mid-assembly at its committed coordinate: OWNED, not orphaned)" forever.
        // Dismantle the machine on it and it is still never reclaimed: a permanent, snappable duplicate
        // core at an abandoned destination, invisible to an audit that walks bGroupPlaced entries.
        //
        // ORDERING, TWICE LOAD-BEARING -- this call must sit HERE and nowhere else in this branch:
        //   * AFTER DespawnWellGroup (:330), because its *** ABANDONED IN PLACE *** line prints
        //     PlacedCoreLocation to say WHERE it left occupied actors, and that must be the real
        //     coordinate, not a zero. Same rationale as NodeShuffleWellEscalate.cpp:181.
        //   * AFTER `E.bGroupPlaced = false` above, because ClearAbandonedWellPlacement RETURNS EARLY
        //     on bGroupPlaced -- placed at the despawn it would silently no-op for exactly the
        //     previously-placed wells this fix exists for.
        ClearAbandonedWellPlacement(E, TEXT("re-enrolled by a new roll"));

        ++Enrolled;

        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-ROLL core='%s' ENROLLED: %d satellites, rigid body captured about the core at %s ")
            TEXT("(coreYaw=%.1f deg, minIntraGroup=%.0f cm vs %.0f cm reject radius)."),
            *WellShort(E.CorePath), E.Satellites.Num(), *CoreLoc.ToCompactString(), CoreYaw,
            MinIntra, WellSelfOverlapFloorCm);
    }

    // -------- DEAL A DESTINATION TO EVERY NEWLY ENROLLED GROUP --------
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
    // exists to prevent. Sorted by CorePath, as RollWellLayout already sorts its deal.
    TArray<int32> DealOrder;
    for (int32 i = 0; i < WellLayout.Num(); ++i)
    {
        if (WellLayout[i].bRelocate && !WellLayout[i].bDestDealt && !WellLayout[i].bGroupPlaced)
        {
            DealOrder.Add(i);
        }
    }
    DealOrder.Sort([this](int32 A, int32 B) { return WellLayout[A].CorePath < WellLayout[B].CorePath; });

    int32 Dealt = 0, DealFailed = 0;
    TArray<FVector> WellDestinations;
    for (const FNodeShuffleWellEntry& E : WellLayout)
    {
        if (E.bGroupPlaced) { WellDestinations.Add(E.PlacedCoreLocation); }
        else if (E.bDestDealt) { WellDestinations.Add(E.DestCoreLocation); }
    }

    for (int32 Idx : DealOrder)
    {
        FNodeShuffleWellEntry& E = WellLayout[Idx];
        if (!bBoxUsable)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2-ROLL core='%s': no usable deal box (degenerate layout) -- cannot deal a ")
                TEXT("destination. Left vanilla."), *WellShort(E.CorePath));
            E.bRelocate = false;
            ++DealFailed;
            continue;
        }

        // A well needs clearance for its WHOLE footprint, not for a point. Inset the box by the largest
        // measured bounding radius so the outer satellites of a group dealt near the edge are still
        // inside the playable area, and space destinations by that radius plus the node spacing.
        const float Inset = WellMaxBoundRadiusCm;
        const float MinX = BoxMin.X + Inset, MaxX = BoxMax.X - Inset;
        const float MinY = BoxMin.Y + Inset, MaxY = BoxMax.Y - Inset;
        bool bPicked = false;
        for (int32 Try = 0; Try < WellRedealTries && !bPicked; ++Try)
        {
            const FVector Cand(DealRng.FRandRange(MinX, MaxX), DealRng.FRandRange(MinY, MaxY), ProbeZ);
            if (IsKnownWaterCell(Cand)) { continue; }
            bool bTooClose = false;
            for (const FVector& Other : WellDestinations)
            {
                if (FVector::DistSquared2D(Other, Cand) < FMath::Square(2.0f * WellMaxBoundRadiusCm))
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
            E.DestCoreLocation = Cand;
            E.bDestDealt = true;
            WellDestinations.Add(Cand);
            bPicked = true;
            ++Dealt;
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH2-ROLL core='%s' DEALT %s -> %s (attempt %d; settles + rotates on discovery)."),
                *WellShort(E.CorePath), *E.VanillaCoreLocation.ToCompactString(),
                *Cand.ToCompactString(), Try + 1);
        }
        if (!bPicked)
        {
            // Not a permanent failure -- the next roll draws again with a different salt. The well
            // simply stays vanilla in the meantime, which is the fail-safe direction.
            ++DealFailed;
            E.bRelocate = false;
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH2-ROLL core='%s': no destination cleared the water grid and spacing filters in ")
                TEXT("%d draws -- left vanilla this roll."), *WellShort(E.CorePath), WellRedealTries);
        }
    }

    // ns-review-h2-r2 F-I: the roll's TEARDOWN TAIL lives in NodeShuffleWellSweep.cpp next to the sweep
    // it drives (this file was at exactly 500 lines and the F-A fix pushed it over). It withdraws the
    // claim of every entry this roll abandoned, computes the post-roll sweep gate, and runs the sweep.
    FinishWellRollTeardown(bRelocationEnabled);

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-ROLL: %s complete -- %d newly enrolled (%d destinations dealt, %d deal-failed), ")
        TEXT("%d already placed and kept, %d not streamed, %d below the %d-satellite floor, %d pinned/")
        TEXT("unmanaged, %d geometry-refused, %d non-finite, %d permanently failed. Seed %d (relocation ")
        TEXT("stream %d). K=%d yaw steps, %d tried per pass, %d nudges, %d redeals before a well is left ")
        TEXT("vanilla for good."),
        bIsReroll ? TEXT("re-roll") : TEXT("initial roll"), Enrolled, Dealt, DealFailed, AlreadyCaptured,
        RefusedUnstreamed, RefusedTooFew, WellMinSatellitesForRelocation, RefusedPinned, RefusedGeometry,
        RefusedNonFinite, PermanentlyFailed, Seed, Seed ^ 0x574C5232,
        WellYawSteps, WellYawAttemptsPerPass, WellMaxGroupNudges, WellMaxGroupRedeals);
}
