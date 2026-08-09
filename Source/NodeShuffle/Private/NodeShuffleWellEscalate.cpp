// Packet H2 (ns-wells-h2, branch feature/extractor-automatch): WHAT TO DO WHEN A PLACEMENT FAILS --
// the bounded void-defer counter and the single escalation ladder (nudge -> re-deal -> leave the well
// vanilla for good).
//
// Split out of NodeShuffleWellRelocateApply.cpp for the 500-line limit once the ns-review-h2 fixes
// landed. The seam is real and, in this packet, load-bearing: NodeShuffleWellRelocateApply.cpp decides
// WHERE to try and whether a footprint fits; this file decides WHAT HAPPENS NEXT WHEN IT DOES NOT. The
// cold review's F3 was precisely a consequence of that decision living in two places at once.
//
// ns-review-h2 F3 (HIGH) -- THE BUG THIS FILE EXISTS TO MAKE IMPOSSIBLE. There used to be two ladders:
// one inline in the core-rejection branch, one at the end of the yaw search. Only the second was
// complete. The first nudged, reached the cap, and then simply STOPPED MOVING THE DESTINATION --
// re-probing the identical rejected spot on every pass forever, with no re-deal and no give-up. And
// because GroupNudges is a SaveGame uint8, it wrapped at 256 back to 0, where the nudge radius is
// 4000*sqrt(0) = 0, so even the branch that was supposed to move stopped moving. A well dealt into a
// lake was trapped permanently, and the trap was written into the player's save. IsKnownWaterCell is a
// LEARNED grid, so a fresh save deals into lakes routinely: this was not an exotic path.
//
// Two copies of a ladder where only one is complete IS the shape of the bug, so there is now exactly
// one copy and both failure sites call it.
//
// A MEMBER function: it reads and writes the layout entry and reaches the deal box and the saved seed.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleWellRetype.h"   // WellShort
#include "NodeShuffleWellRelocate.h" // WellYawSeedFor

namespace
{
    // Mirrors of NodeShuffleSubsystem.cpp's file-local placement constants (anonymous namespace there,
    // so not reachable here). Same values, named to the same things, so the two stay greppable.
    constexpr float WellGroupNudgeStepCm = 4000.0f;       // LandRelocationStepCm
    constexpr float WellGroupNudgeMaxRadiusCm = 30000.0f; // LandRelocationMaxRadiusCm
    constexpr float WellGoldenAngleRad = 2.39996323f;
}

// ------------------------------------------------------------------------------------------------
// BOUNDED, AUDIBLE VOID DEFERS  (ns-review-h2 F8)
// ------------------------------------------------------------------------------------------------
// A void probe means "the terrain is not streamed here", which is legitimate and common -- the group
// is waiting for a player to walk closer. Deferring on it is correct. What was NOT correct was
// deferring on it FOREVER, at Verbose, with no counter: a destination that will never produce a
// terrain hit (a probe over a hole in the world, a Z the long trace cannot bracket) is then
// indistinguishable from one the player simply has not reached, and the well never relocates and
// never says why. So: count per group, name the offending coordinate once at 20, and at 60 charge a
// nudge so the escalation ladder can move the group somewhere it CAN settle.
void ANodeShuffleSubsystem::NoteWellVoidDefer(FNodeShuffleWellEntry& E, const TCHAR* Which,
                                              const FVector& Probe, const TCHAR* Who, bool bDiag)
{
    int32& Count = WellVoidDefers.FindOrAdd(E.CorePath);
    ++Count;

    if (Count == WellVoidDeferNoticeAt)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-VOID core='%s': %d consecutive probes have found NO TERRAIN -- last was the %s ")
            TEXT("probe for '%s' at %s. Normal if no player has been near that destination; if a player ")
            TEXT("IS there, the spot cannot be settled and the group will be nudged at %d."),
            *WellShort(E.CorePath), Count, Which, Who, *Probe.ToCompactString(), WellVoidDeferEscalateAt);
    }
    else if (Count >= WellVoidDeferEscalateAt)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-VOID core='%s': %d void probes -- escalating rather than deferring forever. Last ")
            TEXT("was the %s probe for '%s' at %s."),
            *WellShort(E.CorePath), Count, Which, Who, *Probe.ToCompactString());
        Count = 0; // the ladder moves the destination; the new spot gets a fresh allowance
        EscalateWellPlacement(E, E.DestCoreLocation, /*bHaveSettledCore=*/false,
                              TEXT("probes never found terrain"));
    }
    else if (bDiag)
    {
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("WELLH2-SEARCH core='%s': %s probe for '%s' at %s found no terrain -- deferred (%d), no ")
            TEXT("budget spent, cursor held at %d."),
            *WellShort(E.CorePath), Which, Who, *Probe.ToCompactString(), Count, E.YawCursor);
    }
}

// ------------------------------------------------------------------------------------------------
// THE ESCALATION LADDER -- ONE implementation, reached from BOTH failure sites
// ------------------------------------------------------------------------------------------------
// ns-review-h2 F3 (HIGH). This used to exist twice: once inline in STEP 1 for a rejected core spot,
// once at the end of the yaw search. Only the second one was a complete ladder. The first nudged, and
// at the cap simply STOPPED MOVING THE DESTINATION -- re-probing the identical rejected spot on every
// pass, forever, with no redeal and no give-up. Worse, GroupNudges is a SaveGame uint8: at 256 it
// wrapped to 0, where the nudge radius is 4000*sqrt(0) = 0, so even the "moving" branch stopped
// moving. A well dealt into a lake was permanently trapped and the trap was written into the save.
//
// Two copies of a ladder where only one is complete is the shape of the bug, so there is now one
// copy. bHaveSettledCore says whether AnchorLoc is a real settled point (the yaw-exhaustion caller
// has one) or merely the last probe (the core-rejection caller does not) -- the nudge spirals out
// from whichever is meaningful.
// ------------------------------------------------------------------------------------------------
// VALIDATED BUT NOT ASSEMBLED  (ns-review-h5 judgement call 2) -- bounded and audible, NOT a latch
// ------------------------------------------------------------------------------------------------
// Every other repeating condition in this packet has a bounded, audible counter: void probes escalate
// at 60, nudges cap at 8, redeals at 3, spawn failures park a node after N attempts. This one had
// none, and it is a genuine infinite loop with no distinguishing signal: TryPlaceWellGroup does NOT
// advance YawCursor on success and spends no nudge or redeal budget, so a group that VALIDATES and
// then fails to ASSEMBLE re-validates the same yaw and re-fails on every pass, forever. Two identical
// log lines every ~5 s is the only trace, and nothing says how long it has been going on.
//
// DELIBERATELY NOT A LATCH. This is diagnosis, not retirement, so it carries none of the risk of
// alternative (4) -- which both the reviewer and I rejected because under spawn-on-discovery the
// causes of an incomplete spawn are overwhelmingly TRANSIENT (a satellite whose terrain had not
// streamed, one SpawnActorDeferred that returned null), and a first-failure latch would retire wells
// because a player flew past at the wrong moment. Retrying is what makes relocation work at all under
// lazy streaming. What was missing was not a stopping rule but a MEASUREMENT -- and this is the
// counter a real policy would be gated on if evidence ever demands one.
void ANodeShuffleSubsystem::NoteWellIncompleteSpawn(const FNodeShuffleWellEntry& E)
{
    int32& Count = WellIncompleteSpawnCounts.FindOrAdd(E.CorePath);
    ++Count;

    // Name the first member that has no live actor -- that is the thing failing, and without it the
    // line says only "something did not assemble".
    FString Missing = TEXT("<none identified>");
    for (const FNodeShuffleWellSatellite& S : E.Satellites)
    {
        if (!S.bCaptured) { continue; }
        if (!SpawnedWellSatellites.FindRef(S.SatellitePath))
        {
            Missing = WellShort(S.SatellitePath);
            break;
        }
    }
    if (!SpawnedWellCores.FindRef(E.CorePath)) { Missing = TEXT("<core>"); }

    if (Count == WellIncompleteSpawnNoticeAt)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-STUCK core='%s': validated a footprint %d times and failed to assemble the group ")
            TEXT("every time (first member with no live actor: '%s', destination %s). Normal for a while ")
            TEXT("-- the usual cause is a satellite whose terrain has not streamed, and it clears itself. ")
            TEXT("Escalates to a warning at %d."),
            *WellShort(E.CorePath), Count, *Missing, *E.PlacedCoreLocation.ToCompactString(),
            WellIncompleteSpawnWarnAt);
    }
    else if (Count >= WellIncompleteSpawnWarnAt && (Count % WellIncompleteSpawnWarnAt) == 0)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-STUCK core='%s': *** VALIDATED BUT NEVER ASSEMBLED *** -- %d consecutive passes ")
            TEXT("(~%d s) have validated a footprint and failed to spawn the group. First member with no ")
            TEXT("live actor: '%s'; destination %s. The search does NOT advance its yaw cursor or spend ")
            TEXT("nudge/redeal budget on this path, so it will retry this same placement indefinitely. ")
            TEXT("This is DIAGNOSIS, not a stopping rule -- the well is still retried, deliberately, ")
            TEXT("because incomplete spawns are usually transient under spawn-on-discovery."),
            *WellShort(E.CorePath), Count, Count * 5, *Missing,
            *E.PlacedCoreLocation.ToCompactString());
    }
}

void ANodeShuffleSubsystem::EscalateWellPlacement(FNodeShuffleWellEntry& E, const FVector& AnchorLoc,
                                                  bool bHaveSettledCore, const TCHAR* Why)
{
    const FString CoreLabel = WellShort(E.CorePath);
    const FVector Anchor = bHaveSettledCore ? AnchorLoc : E.DestCoreLocation;

    // ns-review-h3 H1 (BLOCKING) -- TEAR THE OLD GROUP DOWN BEFORE EITHER DESTINATION WRITE.
    // Every branch below rewrites DestCoreLocation or gives up, and until this call existed the actors
    // spawned at the PREVIOUS destination stayed live: a partial spawn followed by a re-search left a
    // core at the old coordinates and satellites at the new ones, all counts matching, and the give-up
    // branch below logged "LEFT EXACTLY WHERE THE LEVEL AUTHOR PUT IT" while our own duplicate well
    // sat at the abandoned spot, invisible to an audit that only walks bGroupPlaced entries.
    // Positioned FIRST so no return path can skip it. Occupied members are refused and abandoned in
    // place -- see DespawnWellGroup, where that decision and its consequences are written down.
    DespawnWellGroup(E, Why);

    // ns-review-h5 F2 -- AND DROP THE ENTRY'S CLAIM ON THOSE COORDINATES, immediately after the
    // teardown and before either destination write. Every branch below abandons the committed placement:
    // a nudge and a re-deal both move the group, and the give-up branch leaves it vanilla. Until this
    // call existed, PlacedCoreLocation/PlacedLocation kept naming the ABANDONED destination forever, and
    // the orphan sweep's IsAtTarget() ownership test therefore reported any actor still standing there
    // -- the occupied members DespawnWellGroup just refused to destroy, in particular -- as "mid-assembly:
    // OWNED, not orphaned". Ordered AFTER the despawn on purpose: DespawnWellGroup's *** ABANDONED IN
    // PLACE *** line prints PlacedCoreLocation to say WHERE those actors were left, and that has to be
    // the real coordinate, not a zero.
    ClearAbandonedWellPlacement(E, Why);

    // A nudge always resets the yaw cursor: a new location deserves a fresh search, not the tail of
    // the old one. It also re-seeds the permutation (WellYawSeedFor folds GroupNudges in), so the new
    // spot is searched in a different -- but still fully deterministic -- order.
    ++E.GroupNudges;
    E.YawCursor = 0;

    if (E.GroupNudges < WellMaxGroupNudges)
    {
        const float Radius = FMath::Min(WellGroupNudgeMaxRadiusCm,
            WellGroupNudgeStepCm * FMath::Sqrt(static_cast<float>(E.GroupNudges)));
        const float Angle = WellGoldenAngleRad * E.GroupNudges;
        E.DestCoreLocation = FVector(Anchor.X + Radius * FMath::Cos(Angle),
                                     Anchor.Y + Radius * FMath::Sin(Angle), Anchor.Z);
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-SEARCH core='%s': %s at %s -- nudging the whole group to %s (nudge %d/%d, ")
            TEXT("redeal %d/%d). Yaw order re-seeded for the new spot, deterministically."),
            *CoreLabel, Why, *Anchor.ToCompactString(), *E.DestCoreLocation.ToCompactString(),
            E.GroupNudges, WellMaxGroupNudges, E.GroupRedeals, WellMaxGroupRedeals);
        return;
    }

    // Nudge budget exhausted -> re-deal the group somewhere else entirely.
    ++E.GroupRedeals;
    E.GroupNudges = 0;
    if (E.GroupRedeals < WellMaxGroupRedeals)
    {
        const FVector BoxMin = GetDealBoundsMin();
        const FVector BoxMax = GetDealBoundsMax();
        if (BoxMax.X > BoxMin.X && BoxMax.Y > BoxMin.Y)
        {
            // Deterministic per-group redeal stream: a pure function of persisted state, exactly like
            // the layout seed. Frame time or a shared live stream here would break T8 just as surely.
            // ns-t27-corefirst: renamed with the function (WellYawSeedFor -> WellLayoutSeedFor). The
            // value, the arguments and the XOR salt are untouched, so a save's redeal coordinates are
            // bit-identical across this change -- only the placement search downstream of them differs.
            FRandomStream Rng(WellLayoutSeedFor(SavedSeed, E.CorePath, E.GroupRedeals, 0) ^ 0x52444C31);
            E.DestCoreLocation = FVector(Rng.FRandRange(BoxMin.X, BoxMax.X),
                                         Rng.FRandRange(BoxMin.Y, BoxMax.Y),
                                         (DealMeanZ != 0.0f) ? DealMeanZ : E.DestCoreLocation.Z);
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH2-SEARCH core='%s': %s and the nudge budget is exhausted -- RE-DEALT to %s ")
                TEXT("(redeal %d/%d)."),
                *CoreLabel, Why, *E.DestCoreLocation.ToCompactString(), E.GroupRedeals, WellMaxGroupRedeals);
            return;
        }
        // No usable deal box: fall through to give-up rather than looping on an undealable entry.
    }

    // Everything exhausted -> LEAVE THE WELL AT ITS VANILLA LOCATION, for good.
    // ns-review-h3 H1: the despawn at the top of this function is what MAKES that sentence true. It
    // used to be a claim about the vanilla well only, while our own spawned core and satellites stayed
    // live at the abandoned destination forever -- a permanent duplicate well, unlogged, and outside
    // the audit's reach because the audit only walks bGroupPlaced entries and this one is now false.
    E.bRelocationFailed = true;
    E.bRelocate = false;
    E.bGroupPlaced = false;

    // ns-t23-rollhide -- TERMINAL FAILURE SITE 1 OF 2, AND THE SHARPEST CORRECTNESS EDGE IN T23.
    // The sentence this line used to print -- "the well is LEFT EXACTLY WHERE THE LEVEL AUTHOR PUT IT" --
    // was true only while suppression happened AFTER a complete spawn. Under roll-time commitment the
    // vanilla group may already be hidden, de-collided and de-registered at this point, so that sentence
    // would be FALSE at the exact moment it is printed, and the well would be gone from the save
    // permanently. It is now a CONDITIONAL statement of what this branch actually did, and the un-hide is
    // what makes the "left where the author put it" half true again.
    //
    // ATTEMPTED, THEN PERSISTED. This branch fires wherever the PLAYER is, which under spawn-on-discovery
    // is at the DESTINATION -- the origin can be kilometres away and unstreamed, in which case there is
    // nothing here to un-hide. So the intent is recorded first and re-attempted on every apply pass until
    // every member is discharged. A straight call would find nothing and silently drop the obligation.
    const bool bOwedRestore = WellGroupHasSuppressedMember(E);
    bool bRestoredNow = false;
    if (bOwedRestore)
    {
        E.bUnhidePending = true;
        bRestoredNow = TryUnhideWellGroup(E, TEXT("relocation gave up permanently"));
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-SEARCH core='%s': GIVING UP (%s) -- %d independent-layout attempts x %d nudges x ")
        TEXT("%d redeals all failed. Our ")
        TEXT("own spawned members at the abandoned destination were destroyed at the top of this ")
        TEXT("escalation (see any WELLH2-DESPAWN line above) and the well will not be re-enrolled. %s ")
        TEXT("This is the designed fail-safe: never a partial or broken well, only an untouched one."),
        *CoreLabel, Why, WellLayoutAttempts, WellMaxGroupNudges, WellMaxGroupRedeals,
        !bOwedRestore
            ? TEXT("We hold no suppression record on this well's vanilla members, so it is standing ")
              TEXT("exactly where the level author put it and nothing had to be given back.")
            : (bRestoredNow
                ? TEXT("We HAD suppressed this well's vanilla members and have just restored every one ")
                  TEXT("of them, so it is standing where the level author put it again -- grep ")
                  TEXT("WELLH2-UNHIDE on this core for what was restored member by member.")
                : TEXT("*** WE HAD SUPPRESSED THIS WELL'S VANILLA MEMBERS AND NOT ALL OF THEM COULD BE ")
                  TEXT("RESTORED ON THIS PASS *** so it is NOT yet standing where the level author put ")
                  TEXT("it. The restore is persisted and re-attempted every apply pass; until it ")
                  TEXT("completes this well is counted in WELLH2-STRANDED.")));
}
