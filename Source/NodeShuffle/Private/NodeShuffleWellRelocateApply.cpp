// Packet H2 (ns-wells-h2, branch feature/extractor-automatch): the PLACEMENT half of rigid
// resource-well relocation -- the per-pass driver, the §Q3a rotate-before-you-nudge search,
// all-or-nothing footprint validation, and suppression of the vanilla group it replaces.
// NodeShuffleWellRelocateRoll.cpp owns the capture/deal half; NodeShuffleWellSpawn.cpp materialises a
// validated placement; NodeShuffleWellEscalate.cpp owns what happens when one does not fit;
// NodeShuffleWellLink.cpp owns the mCore lifecycle; NodeShuffleWellAudit.cpp owns the acceptance gate.
// NodeShuffleWellRelocate.h holds the full file map and the shared pure helpers.
//
// ALL-OR-NOTHING IS THE WHOLE DESIGN OF THIS FILE (design §Q3). Left to the ordinary per-node
// machinery, a well dropped on awkward terrain would quietly become a 2-satellite well: each
// individual deactivation looks routine, so the shrink is invisible in the log. So NOTHING is written
// to the world until the ENTIRE footprint -- core plus every satellite -- has settled, cleared water,
// slope, resource-node overlap and buildable overlap. Any member fails => next yaw => nudge the group
// => re-deal the group => leave the well at its VANILLA location.
//
// ns-review-h2 F15/F7 -- THE PRECISE CLAIM. This file used to end that paragraph with "there is no
// partial-well state", which was true of VALIDATION and FALSE OF SPAWN: SpawnWellGroup returned "did
// anything spawn", so one satellite out of ten marked the group placed and the suppression then hid
// the entire vanilla well. A partial-well state existed and was reachable. It no longer does -- the
// spawn is all-or-nothing too, and nothing is suppressed until the group is COMPLETE -- but the
// property is now enforced in two places rather than asserted in a comment, and a TRANSIENT partial
// group can still exist for one pass while an incomplete spawn is retried. During that window the
// vanilla well is still fully visible, which is the correct direction to fail in.
//
// SEARCH ORDER IS ROTATE, THEN MOVE (design §Q3a). A well's footprint is anisotropic and, more to the
// point, often LOPSIDED -- H0 measured max angular gaps of 263, 220, 155 and 151 degrees on wells 19,
// 14, 18 and 20, i.e. crescents and fans with every satellite on one side. Those can be tucked into a
// valley by rotation in a way that moving them cannot. Rotation also re-uses the core's
// already-validated position and only re-tests the satellites, so it is the cheap axis as well as the
// effective one.
//
// K = 36 (10 deg), NOT 12. Measured twice over, independently: arc displacement r*delta at the mean
// bounding radius (4587 cm) stays inside the 800 cm reject radius only up to ~10 deg, and the smallest
// measured angular gap between neighbouring satellites is 9.6 deg. K=12's 30 deg step moves an outer
// satellite ~20 m per attempt and skips valid pockets wholesale. The footprint test early-outs on the
// first failing satellite, so a bad yaw usually dies after one or two traces -- K=36 is affordable.
//
// NO SAME-GROUP OVERLAP EXEMPTION IS BUILT, and that is deliberate rather than an omission. H0
// measured min inter-satellite 1818.8 cm over 401 pairs and min core->satellite 2076 cm against the
// 800 cm reject radius: satellites cannot reject each other. The roll ASSERTS that per well at capture
// (MinIntraGroupDistance) and refuses any group that violates it, so by the time a group reaches this
// file the property holds by construction. The only self-exclusion here is of the group's OWN already
// spawned actors, which exist for a different reason entirely (re-validation after a spawn).
//
// IMPORT DISCIPLINE (memory: sf-shipping-export-trap): the new engine surface this file reaches for is
// SpawnActorDeferred/FinishSpawning on the two fracking classes plus their StaticClass thunks, all of
// which design §M1 dumpbin-verified present in the shipping export table. That is the PREDICTION, not
// the claim -- the import table is MEASURED after the build and every new symbol is named. Three
// predictions have been falsified on this machine this week.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleWellCensus.h"   // AFGResourceNodeFrackingCore / ...Satellite
#include "NodeShuffleWellRetype.h"   // WellPathOf / WellShort
#include "NodeShuffleWellRelocate.h" // the H2 pure helpers

#include "Resources/FGResourceDescriptor.h"

// The reject radii live in NodeShuffleWellFootprint.cpp with the test that uses them; the nudge/redeal
// constants live in NodeShuffleWellEscalate.cpp with the ladder that uses them. Nothing this file does
// needs either, which is a decent sign the split fell where it should.

// ------------------------------------------------------------------------------------------------
// THE §Q3a SEARCH FOR ONE GROUP
// ------------------------------------------------------------------------------------------------
bool ANodeShuffleSubsystem::TryPlaceWellGroup(FNodeShuffleWellEntry& E, UClass* ResourceClass)
{
    const bool bDiag = FNodeShuffleModule::AreDiagnosticsEnabled();
    const FString CoreLabel = WellShort(E.CorePath);

    // ---- STEP 1: settle and validate the CORE at the dealt spot ----
    // ns-review-h4 style: zero-initialised. ValidateWellMemberSpot writes OutLoc only on a hit, and the
    // failure path below passes CoreLoc straight to EscalateWellPlacement -- which ignores it when
    // bHaveSettledCore is false, but an uninitialised FVector reaching any call at all is not a thing
    // to leave resting on a parameter's semantics.
    FVector CoreLoc = FVector::ZeroVector;
    FRotator CoreRot = FRotator::ZeroRotator;
    FString CoreReason;
    const float StartZ = static_cast<float>(E.DestCoreLocation.Z);
    if (!ValidateWellMemberSpot(E.DestCoreLocation, StartZ, CoreLoc, CoreRot, CoreReason))
    {
        if (CoreReason.StartsWith(TEXT("void")))
        {
            // Terrain not streamed. DEFER without spending any budget -- charging a nudge here would
            // burn 8 nudges and 3 redeals on a group the player simply has not walked to yet.
            // ns-review-h2 F8: bounded, and audible. An UNBOUNDED Verbose-only defer is indistinguishable
            // from a working feature that has simply not been reached, so a genuinely undiscoverable
            // destination (a probe that will never hit terrain) would retry silently forever.
            NoteWellVoidDefer(E, TEXT("core"), E.DestCoreLocation, TEXT("<core>"), bDiag);
            return false;
        }
        // ns-review-h2 F3 (HIGH): a real rejection of the core's spot escalates through the SAME
        // ladder the yaw-exhaustion path uses. The previous version had its own inline nudge with no
        // redeal and no give-up: once GroupNudges reached the cap it stopped moving the destination
        // and re-probed the identical spot every pass forever -- and because GroupNudges is a SaveGame
        // uint8, it wrapped at 256 back to 0, where the nudge radius is 4000*sqrt(0) = 0. A well dealt
        // into a lake was trapped permanently, and the corruption persisted across saves.
        // IsKnownWaterCell is a LEARNED grid, so a fresh save deals into lakes routinely; this is not
        // an exotic path.
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("WELLH2-SEARCH core='%s': CORE rejected at %s (%s) -- escalating (nudge %d/%d, redeal %d/%d)."),
            *CoreLabel, *E.DestCoreLocation.ToCompactString(), *CoreReason,
            E.GroupNudges + 1, WellMaxGroupNudges, E.GroupRedeals, WellMaxGroupRedeals);
        EscalateWellPlacement(E, CoreLoc, /*bHaveSettledCore=*/false, TEXT("core-spot rejected"));
        return false;
    }

    // ---- STEP 2: the core is good; walk the SEEDED yaw permutation ----
    // The order is recomputed from persisted state (see WellYawSeedFor in NodeShuffleWellRelocate.h),
    // never carried in a live stream and never derived from frame time or actor iteration. YawCursor
    // persists so a group deferred mid-search RESUMES rather than restarting.
    const int32 YawSeed = WellYawSeedFor(SavedSeed, E.CorePath, E.GroupRedeals, E.GroupNudges);
    TArray<int32> YawOrder;
    BuildWellYawOrder(YawSeed, WellYawSteps, YawOrder);

    int32 Attempts = 0;
    while (E.YawCursor < WellYawSteps && Attempts < WellYawAttemptsPerPass)
    {
        const int32 YawIdx = YawOrder[E.YawCursor];
        const float YawDeg = WellYawDegForIndex(YawIdx, WellYawSteps);
        ++Attempts;

        // ---- THE FULL FOOTPRINT, VALIDATED BEFORE ANYTHING IS COMMITTED ----
        TArray<FVector> MemberLocs;
        TArray<FRotator> MemberRots;
        MemberLocs.Reserve(E.Satellites.Num());
        MemberRots.Reserve(E.Satellites.Num());
        bool bAllOk = true;
        bool bVoid = false;          // an unstreamed member is a DEFER, never a yaw rejection
        FString FailReason, FailWho;
        FVector FailProbe = FVector::ZeroVector; // ns-review-h2 F8: the coordinate to name in the log

        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            // ns-review-h3 H10: an UNCAPTURED record has no rigid-body offset, so rotating its zero
            // offset re-probes the CORE's own spot -- and then the commit below wrote PlacedLocation =
            // CoreLoc into it. That silently disarmed half of the spawn guard's belt-and-braces (the
            // `LocalOffset.IsNearlyZero() && PlacedLocation.IsNearlyZero()` term stopped holding), and
            // it spent a trace per yaw testing a point we had already tested. These records are never
            // spawned; they must not be validated either.
            if (!S.bCaptured) { continue; }
            const FVector2D RotXY = RotateWellOffsetXY(S.LocalOffset, YawDeg);
            const FVector Probe(CoreLoc.X + RotXY.X, CoreLoc.Y + RotXY.Y, CoreLoc.Z);
            FVector SatLoc; FRotator SatRot = FRotator::ZeroRotator; FString Reason;
            // Z is re-settled PER MEMBER from the core's Z. H0 measured four wells with more than 12 m
            // of vertical spread, so carrying the captured Z across would bury or float those members.
            if (!ValidateWellMemberSpot(Probe, static_cast<float>(CoreLoc.Z), SatLoc, SatRot, Reason))
            {
                bAllOk = false;
                bVoid = Reason.StartsWith(TEXT("void"));
                FailReason = Reason;
                FailWho = WellShort(S.SatellitePath);
                FailProbe = Probe;
                break; // EARLY-OUT: this is what makes K=36 affordable
            }
            // Rotate the satellite's OWN actor yaw with its offset, so the rocks turn with the group
            // (design §Q3a). Without it a rotated well's meshes all face the original direction --
            // subtly wrong in a way that is hard to name when you see it.
            SatRot.Yaw = SatRot.Yaw + S.LocalYawDeg + YawDeg;
            MemberLocs.Add(SatLoc);
            MemberRots.Add(SatRot);
        }

        if (bAllOk)
        {
            // COMMIT. Nothing before this line wrote to the world or to the entry's placement fields.
            WellVoidDefers.Remove(E.CorePath); // ns-review-h2 F8: a settled group starts fresh
            E.GroupYawDeg = YawDeg;
            E.PlacedCoreLocation = CoreLoc;
            E.PlacedCoreRotation = FRotator(CoreRot.Pitch, CoreRot.Yaw + YawDeg, CoreRot.Roll);
            // A3 -- THE ONLY PLACE bPlacementClaimLive IS EVER SET TRUE, deliberately on the line after
            // the only non-zero write of PlacedCoreLocation in the packet. Seven review rounds' worth of
            // bugs came from readers INFERRING this from bGroupPlaced/bRelocate/bRelocationFailed; the
            // claim is now a stored fact written where it becomes true. Note it is set HERE, at the
            // footprint commit, NOT at `bGroupPlaced = true` in ApplyWellRelocation: between those two
            // points the group spawns, and an INCOMPLETE spawn leaves real actors of ours standing at
            // exactly these coordinates with bGroupPlaced still false. That window is the h5 F-1 /
            // RT-6 stranded-actor class, and it is precisely the window the claim must cover.
            E.bPlacementClaimLive = true;
            // ns-review-h2-r4 D-2 -- THE COMMIT IS THE PROGRESS SIGNAL THAT RESETS THE EXPIRY CLOCK.
            // ReconcileAbandonedWellClaims now counts consecutive passes an entry spends "mid-search,
            // the claim is live" and withdraws the claim at WellClaimMidAssemblyMaxPasses, because
            // that state previously had NO expiry at all (spawn INCOMPLETE, then disable relocation or
            // never return, and the claim was live for the life of the save). A group that is actually
            // being assembled re-reaches THIS line every pass a player is near it -- TryPlaceWellGroup
            // does not advance the yaw cursor on the INCOMPLETE-spawn path, so the same footprint is
            // re-validated and re-committed -- so resetting here is what makes the bound apply ONLY to
            // entries making no progress. Without this reset the bound would expire a group a player
            // is standing next to, and pass A would then destroy and respawn it: h5 F-2's cycle.
            WellClaimMidAssemblyPasses.Remove(E.CorePath);
            // ns-review-h3 H10: MemberLocs/MemberRots hold ONLY the captured members now, so the commit
            // walks its own cursor rather than indexing E.Satellites -- indexing would misalign the
            // moment any record is uncaptured, and would write a coordinate into a record that must
            // keep none. An uncaptured record's PlacedLocation stays ZeroVector, which is exactly what
            // the spawn guard tests for.
            int32 Cursor = 0;
            for (FNodeShuffleWellSatellite& S : E.Satellites)
            {
                if (!S.bCaptured) { continue; }
                S.PlacedLocation = MemberLocs[Cursor];
                S.PlacedRotation = MemberRots[Cursor];
                ++Cursor;
            }
            UE_LOG(LogNodeShuffle, Display,
                // ns-review-h4 F6: print the count actually TRACED (the bCaptured subset, after h3 H10
                // stopped validating uncaptured records) and the raw record count separately. Saying
                // "N satellites all settled" with N = Satellites.Num() claimed validation of members
                // this loop deliberately never touched.
                TEXT("WELLH2-SEARCH core='%s': FOOTPRINT VALIDATED at %s with yaw=%.1f deg (permutation ")
                TEXT("index %d of %d, cursor %d, yawSeed=%d, nudges=%d, redeals=%d) -- %d captured ")
                TEXT("satellite(s) all settled, dry, off cliffs and clear of nodes and buildings (%d ")
                TEXT("record(s) in the entry; any difference is uncaptured and is neither traced nor spawned)."),
                *CoreLabel, *CoreLoc.ToCompactString(), YawDeg, YawIdx, WellYawSteps, E.YawCursor,
                YawSeed, E.GroupNudges, E.GroupRedeals, ExpectedRelocatedSatelliteCount(E),
                E.Satellites.Num());
            return true;
        }

        if (bVoid)
        {
            // Part of the footprint has not streamed. Do NOT advance the cursor: the same yaw deserves
            // a fair retry once the terrain is there, and advancing would silently consume the search.
            // ns-review-h2 F8: bounded and audible, same as the core-probe defer above.
            NoteWellVoidDefer(E, TEXT("satellite"), FailProbe, *FailWho, bDiag);
            return false;
        }

        if (bDiag)
        {
            UE_LOG(LogNodeShuffle, Verbose,
                TEXT("WELLH2-SEARCH core='%s': yaw=%.1f deg REJECTED by satellite '%s' (%s) -- cursor %d/%d."),
                *CoreLabel, YawDeg, *FailWho, *FailReason, E.YawCursor + 1, WellYawSteps);
        }
        ++E.YawCursor;
    }

    if (E.YawCursor < WellYawSteps)
    {
        // Per-pass budget spent, search not finished. Resume next pass from the same cursor.
        if (bDiag)
        {
            UE_LOG(LogNodeShuffle, Verbose,
                TEXT("WELLH2-SEARCH core='%s': %d yaws tried this pass, cursor now %d/%d -- resuming next pass."),
                *CoreLabel, Attempts, E.YawCursor, WellYawSteps);
        }
        return false;
    }

    // ---- STEP 3/4/5: all K yaws failed here -> the shared escalation ladder ----
    EscalateWellPlacement(E, CoreLoc, /*bHaveSettledCore=*/true,
        *FString::Printf(TEXT("all %d yaws failed"), WellYawSteps));
    return false;
}

// ------------------------------------------------------------------------------------------------
// SUPPRESSING THE VANILLA GROUP
// ------------------------------------------------------------------------------------------------
// Narrow and self-contained ON PURPOSE: it never touches OriginalNodeRecord or SuppressOriginalNodes'
// machinery, whose rematch/capture/radiation paths explicitly exclude fracking actors. A leftover
// visible satellite beside a hidden core is the well-shaped version of the orphan-rock ghost, so the
// core, every satellite and their paired AFGNodeMeshActors are hidden as a UNIT (design §2.5).
// Re-asserted every pass because a level actor streams back in un-hidden.
void ANodeShuffleSubsystem::SuppressVanillaWellGroup(FNodeShuffleWellEntry& E)
{
    int32 Hidden = 0, MeshesHidden = 0, Occupied = 0, Unstreamed = 0;

    const auto HideOne = [&](AFGResourceNodeBase* Node) -> void
    {
        if (!IsValid(Node)) { ++Unstreamed; return; }
        // ns-review-h4 F1: the SAME two-part predicate the destructive paths use. Hiding is not
        // destructive, but it is exactly as wrong to hide a node a player has a Pressurizer or an
        // Extractor on, and the asymmetry -- pin logic tests activator/extractor||occupied, this tested
        // occupied alone -- is the thing that lets the two drift apart again. One predicate, every site.
        const TCHAR* InUseWhy = TEXT("");
        if (IsWellMemberInUse(Node, InUseWhy))
        {
            ++Occupied;
            if (!WellSuppressSkipLogged.Contains(WellPathOf(Node)))
            {
                WellSuppressSkipLogged.Add(WellPathOf(Node));
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WELLH2-SUPPRESS core='%s' member='%s': NOT hidden -- in use (%s). Said once."),
                    *WellShort(E.CorePath), *Node->GetName(), InUseWhy);
            }
            return;
        }
        bool bChanged = false;
        if (Node->GetActorEnableCollision()) { Node->SetActorEnableCollision(false); bChanged = true; }
        if (!Node->IsHidden()) { Node->SetActorHiddenInGame(true); bChanged = true; }
        if (AFGNodeMeshActor* MeshActor = FindMeshActorForNode(Node))
        {
            if (!MeshActor->IsHidden())
            {
                MeshActor->SetActorHiddenInGame(true);
                MeshActor->SetActorEnableCollision(false);
                ++MeshesHidden;
            }
        }
        if (bChanged) { ++Hidden; }
        // Take the hidden original out of the scanner and the node manager, once, so it cannot ping an
        // empty map spot or accept an extractor snap as an invisible ghost. Same idiom, same reasons,
        // as SuppressOriginalNodes -- a whole-actor hide does neither of these by itself.
        const FString Path = WellPathOf(Node);
        if (!ScannerDeregistered.Contains(Path))
        {
            Node->RemoveResourceNodeScan_Local();
            Node->UpdateNodeRepresentation();
            DeregisterNodeFromManager(Node);
            ScannerDeregistered.Add(Path);
            ++ScannerDeregisterCount;
        }
    };

    HideOne(FindOriginalBaseByPath(E.CorePath));
    for (const FNodeShuffleWellSatellite& S : E.Satellites)
    {
        HideOne(FindOriginalBaseByPath(S.SatellitePath));
    }

    if ((Hidden > 0 || MeshesHidden > 0 || Occupied > 0) && !WellSuppressLogged.Contains(E.CorePath))
    {
        WellSuppressLogged.Add(E.CorePath);
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-SUPPRESS core='%s': hid %d vanilla member(s) + %d mesh actor(s); %d occupied ")
            TEXT("(left alone), %d not streamed yet (retried every pass). The relocated group now lives ")
            TEXT("at %s."),
            *WellShort(E.CorePath), Hidden, MeshesHidden, Occupied, Unstreamed,
            *E.PlacedCoreLocation.ToCompactString());
    }
}

// ------------------------------------------------------------------------------------------------
// THE PER-PASS DRIVER
// ------------------------------------------------------------------------------------------------
void ANodeShuffleSubsystem::ApplyWellRelocation(bool bWellShuffleEnabled, bool bRelocationEnabled,
                                                float SpawnRadiusCm)
{
    // RELOCATION REQUIRES THE RETYPE TOGGLE TOO. A well NodeShuffle does not manage is not a well it
    // may move -- and with ShuffleResourceWells off, ApplyWellRetype has already returned without
    // resolving anything, so the entry's AssignedResourceClassPath is not being maintained.
    const bool bOn = bWellShuffleEnabled && bRelocationEnabled;

    // Once per session, BEFORE anything else touches a group: re-match our spawned well actors back to
    // their layout entries and re-establish every mCore link. This runs even when the toggles are off,
    // because a save that ALREADY holds relocated wells must keep them linked no matter what the
    // config now says -- a relocated well whose links are not restored dies silently, and "the user
    // turned the feature off" is not a reason to let that happen to wells already in their world.
    if (!bAdoptedRestoredWells && WellLayout.Num() > 0)
    {
        bAdoptedRestoredWells = true;
        AdoptRestoredWellGroups();
    }

    if (!bOn)
    {
        if (!bWellRelocDisabledLogged)
        {
            int32 Placed = 0;
            for (const FNodeShuffleWellEntry& E : WellLayout) { if (E.bGroupPlaced) { ++Placed; } }
            if (Placed > 0)
            {
                bWellRelocDisabledLogged = true;
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WELLH2: relocation is OFF (wellShuffle=%d relocate=%d) but this save holds %d ")
                    TEXT("already-relocated well(s). They are still SPAWNED and still LINKED every load -- ")
                    TEXT("turning the toggle off stops us moving wells, it does not move them back."),
                    bWellShuffleEnabled ? 1 : 0, bRelocationEnabled ? 1 : 0, Placed);
            }
        }
        // Still maintain placed groups: spawn-on-discovery, linking and suppression all continue.
    }

    ++WellAuditPasses;

    int32 Searching = 0, PlacedNow = 0, Spawned = 0, Deferred = 0, Maintained = 0, IncompleteSpawns = 0;

    for (FNodeShuffleWellEntry& E : WellLayout)
    {
        if (!E.bGroupPlaced)
        {
            if (!bOn || !E.bRelocate || !E.bOffsetsCaptured || !E.bDestDealt || E.bRelocationFailed)
            {
                continue;
            }
            // SPAWN-ON-DISCOVERY, exactly as for ordinary nodes: the search runs where a player is,
            // because that is where the terrain is streamed. Validation at roll time would trace into
            // unloaded regions and get void everywhere.
            if (!IsLocationNearAnyPlayer(E.DestCoreLocation, SpawnRadiusCm)) { ++Deferred; continue; }
            ++Searching;

            UClass* ResourceClass = LoadClassByPath(E.AssignedResourceClassPath);
            if (!ResourceClass || !ResourceClass->IsChildOf(UFGResourceDescriptor::StaticClass()))
            {
                if (!WellRelocFailLogged.Contains(E.CorePath))
                {
                    WellRelocFailLogged.Add(E.CorePath);
                    UE_LOG(LogNodeShuffle, Warning,
                        TEXT("WELLH2 core='%s': assigned resource '%s' %s -- not relocating. Left vanilla."),
                        *WellShort(E.CorePath), *E.AssignedResourceClassPath,
                        ResourceClass ? TEXT("is not a UFGResourceDescriptor") : TEXT("failed to load"));
                }
                continue;
            }

            if (!TryPlaceWellGroup(E, ResourceClass)) { continue; }
            ++PlacedNow;

            // ns-review-h2 F7 (HIGH): THE SPAWN, NOT ONLY THE VALIDATION, MUST BE ALL-OR-NOTHING.
            // SpawnWellGroup used to return "did anything spawn", so ONE successful satellite out of
            // ten flipped bGroupPlaced -- and SuppressVanillaWellGroup then hid EVERY vanilla member,
            // leaving a short well and no visible original. It now returns "is the group COMPLETE",
            // and nothing is suppressed until it is. An incomplete group is retried next pass with the
            // members it did get reused, so this costs a pass, not a re-search.
            if (SpawnWellGroup(E, ResourceClass))
            {
                E.bGroupPlaced = true;
                ++Spawned;
                ++WellGroupsPlacedThisSession;
                WellIncompleteSpawnCounts.Remove(E.CorePath); // assembled -- the counter starts fresh
                SuppressVanillaWellGroup(E);
                // ns-review-h2 F12: audit THIS group the moment it is placed. The fixed-pass audit
                // fires ~40 s after load, but relocation is spawn-on-discovery -- so the wells a
                // tester actually flies to are placed LONG after pass 8 and were never audited at all.
                // The acceptance gate has to cover the group the tester is standing in front of.
                AuditOneWellGroup(E, TEXT("just-placed"));
            }
            else
            {
                ++IncompleteSpawns;
                NoteWellIncompleteSpawn(E);
            }
            continue;
        }

        // ---- ALREADY PLACED: maintain it every pass ----
        // Idempotent by construction: the link funnel is cheap, and the suppression re-hides only what
        // has streamed back in un-hidden.
        ++Maintained;
        UClass* ResourceClass = LoadClassByPath(E.AssignedResourceClassPath);
        if (ResourceClass && IsLocationNearAnyPlayer(E.PlacedCoreLocation, SpawnRadiusCm))
        {
            if (SpawnWellGroup(E, ResourceClass)) { ++Spawned; }
        }
        SuppressVanillaWellGroup(E);
    }

    // THE LINK AUDIT -- design §Q3 point 5's "log at both ends every session".
    // ns-review-h2 F12: TWO cadences, because one was not enough. The fixed pass reports a settled
    // world shortly after load (a boolean latch would have fired mid-load and reported a half-streamed
    // world as the answer); the slow repeating cadence then keeps covering groups placed later, which
    // under spawn-on-discovery is most of them. Per-group audits also fire the instant a group is
    // placed, at the call site above.
    if (WellAuditPasses == WellLinkAuditPass) { AuditWellGroupLinks(TEXT("settled")); }
    else if (WellAuditPasses > WellLinkAuditPass && (WellAuditPasses % WellLinkAuditCadence) == 0)
    {
        AuditWellGroupLinks(TEXT("cadence"));
    }

    // ns-review-h4 F2 (BLOCKING): the ORPHAN SWEEP. Driven by the handle set, not by call sites, so it
    // catches every route that abandons a group without despawning it -- including the six roll-time
    // `continue`s that return before RollWellRelocation's despawn is ever reached, and including
    // routes nobody has written yet. Cadenced rather than per-pass because it is a set difference over
    // the whole layout; running it after the audit means a group placed THIS pass is already recorded
    // and can never be mistaken for an orphan.
    //
    // ns-review-h2-r2 F-B: bOn now gates PASS B ONLY -- the world scan. Pass A (handle-driven) and the
    // claim reconciliation run on EVERY call, including this one with both toggles off, because their
    // safety does not depend on the config: pass A only ever destroys actors whose handles WE created.
    // With the feature off over a save that still holds handles, that is the ONLY thing that reclaims
    // an abandoned partial group. Do not re-add a gate here.
    //
    // ns-review-h5 F1 (BLOCKING, the finding that parked this packet): bOn IS PASSED IN, and the sweep
    // refuses to look at the world without it. The `!bOn` block above deliberately does not return --
    // a save that already holds relocated wells must keep them spawned, linked and suppressed whatever
    // the config now says -- so this call site was reached with BOTH toggles off, on every session, in
    // saves that never enabled the feature. With an empty WellLayout the sweep's accounted-for set is
    // empty, and its location backstop then classified every runtime fracking actor in the world as an
    // orphan and destroyed it at pass 8, ~40 s in. That is the node-destroyer behaviour this mod ships
    // a two-layer defence AGAINST (cookbook §20), and it had no opt-out of its own. The gate is a
    // parameter rather than a config read inside the sweep so that it cannot disagree with the pass it
    // belongs to, and the sweep re-checks "at least one group is actually placed" on its own.
    if (WellAuditPasses == WellLinkAuditPass
        || (WellAuditPasses > WellLinkAuditPass && (WellAuditPasses % WellOrphanSweepCadence) == 0))
    {
        SweepOrphanedWellActors(WellAuditPasses == WellLinkAuditPass ? TEXT("settled") : TEXT("cadence"),
                                bOn);
    }

    if (Searching > 0 || PlacedNow > 0 || Spawned > 0 || IncompleteSpawns > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2 pass: %d group(s) searching, %d newly validated, %d spawned COMPLETE, ")
            TEXT("%d spawned INCOMPLETE (not marked placed, nothing suppressed, retried next pass), ")
            TEXT("%d maintained, %d deferred (no player near the destination). %d placed this session."),
            Searching, PlacedNow, Spawned, IncompleteSpawns, Maintained, Deferred,
            WellGroupsPlacedThisSession);
    }
}
