// rwg-farspawn-1c C1 (2026-10-31): keep the provisional actor when its settle on approach kept the same XY, and MOVE it to the settled
// spot instead of despawn + respawn, so the compass marker and any scanner / radar reveal on that actor survive (round-0 review F6).
// Brief: _team/test-logs/2026-10-31-nodeshuffle-farspawn-1c-brief.md. Called only from RwgSettleProvisional
// (NodeShuffleMapModeProvisional.cpp), which runs only on RWG and only after RwgResolveAtSpawn returned true.
//
// WHAT THE RESPAWN PATH (EnsureNewNodeSpawned, fresh branch) DID AFTER THE SETTLE, AND WHAT THIS DOES INSTEAD:
//   spawn at Entry.Location/Rotation      -> SetActorLocationAndRotation(Entry.Location, Entry.Rotation) on the same actor
//   InitResource / overrides / purity     -> unchanged actor, unchanged class + resource (MoveCheck refuses otherwise)
//   InitRadioactivity + UpdateRadioactivity -> emitters removed and re-initialised at the new spot when the actor had one
//   UpdateNodeRepresentation              -> called (the representation is NOT removed: that removal is what lost the marker), then
//                                            AFGActorRepresentationManager::UpdateRepresentation on the node's own representation
//                                            (FindResourceNodeRepresentation), the public path to the representation's cached
//                                            mActorLocation / mLocalActorLocation (FGActorRepresentation.h:251,254); whether it
//                                            re-reads the actor's location is closed source (rwg-farspawn-1d F3, runtime H2/H5)
//   UseBox / manager / SpawnedNodes / veto registry -> same actor pointer, already registered (nothing to re-register)
//   component attach + dress              -> component already attached; the fallback rock is re-dressed when one is dressed
//   SteadyAliveNodes.Add                  -> same instance, already steady
//   despawn's MeshActorCache / AdoptRotRefit removal -> repeated here (same state the respawn left)
//   overlap gates at the settled spot     -> NOT run (lambda-local to EnsureNewNodeSpawned). The gates are 3-D and the provisional
//                                            spawn ran them at the provisional Z, so a settle that rose onto a cliff top was never gated.
//   enclosure / T54 inside-shadow gates   -> MEASURED at the settled spot (rwg-farspawn-1d F2, round-1c review option c):
//                                            IsSpotEnclosed + EvaluateInsideShadowGate, this actor excluded; printed on PROVMOVE and
//                                            counted on PROVCENSUS. rwg-farspawn-1f C2: enclosed refuses the MOVE (gatedRespawn):
//                                            the caller despawns and respawns through the gated spawn path. rwg-farspawn-1f OPTION B:
//                                            allSolid on an entry the gate does not exempt as a cave refuses it too (insideRedeal):
//                                            the caller redeals the entry (RwgProvisionalInsideRedeal below), ring first then map-wide.
// READBACK (rwg-farspawn-1d F1, the RESETTLE-REFUSED precedent, NodeShuffleMapModeResettle.cpp): whether the engine applies the move
// is closed source (a non-Movable root refuses it with a warning only). A move the actor did not take returns moveNotApplied; the caller then
// prints moveCheck=moveNotApplied and takes the despawn + respawn branch, so an entry is never committed settled at a Z its actor
// is not at (the save adopt matches in 3-D within 300 cm, NodeShuffleSubsystem.cpp, and would otherwise respawn a duplicate).
// This REVERSES the DESPAWN-ON-MOVE rule (NodeShuffleWellDespawn.cpp:23,70; NodeShuffleSubsystem.h DespawnWellGroup comment) for
// provisional settles only; its closed-source risks are runtime-checked (round-1c review H2, H5), not proven.
#include "NodeShuffleMapModeProvisional.h"
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleNodeComponent.h"
#include "NodeShuffle.h"
#include "Resources/FGResourceNode.h"
#include "FGRadioactivitySubsystem.h"
#include "FGActorRepresentationManager.h"
#include "Representation/FGResourceNodeRepresentation.h"
#include "Components/StaticMeshComponent.h"

const TCHAR* ANodeShuffleSubsystem::RwgProvisionalMoveCheck(const FNodeShuffleEntry& E, AFGResourceNode* Node, const FVector& ProvAt) const
{
    // The predicate that decides keep-and-move vs respawn; the returned word is printed as PROVSETTLE moveCheck.
    if (!IsValid(Node)) { return TEXT("noActor"); }
    if (FVector::Dist2D(E.Location, ProvAt) > NodeShuffleMapMode::RwgProvisionalMoveTolCm) { return TEXT("xyShift"); }
    if (Node->GetClass()->GetPathName() != ResolveSpawnNodeClassPath(E)) { return TEXT("classDiffers"); }
    const UClass* Res = Node->GetResourceClass();
    if (!Res || Res->GetPathName() != E.AssignedResourceClassPath) { return TEXT("resourceDiffers"); }
    return TEXT("moved");
}

const TCHAR* ANodeShuffleSubsystem::RwgProvisionalMoveInPlace(FNodeShuffleEntry& E, AFGResourceNode* Node)
{
    if (!IsValid(Node)) { return TEXT("moveNotApplied"); } // MoveCheck returned noActor before this can run; belt only
    const FVector From = Node->GetActorLocation();
    const FRotator RotFrom = Node->GetActorRotation();
    Node->SetActorLocationAndRotation(E.Location, E.Rotation);
    // rwg-farspawn-1d F1 readback: the entry follows the ACTOR's measured location, never the request (see the file header).
    const FVector After = Node->GetActorLocation();
    if (FVector::Dist(After, E.Location) > 10.0)
    {
        ++RwgProvSessMoveNotApplied;
        UE_LOG(LogNodeShuffle, Display, TEXT("PROVMOVE: entry=%s moveCheck=moveNotApplied requested=%s actorAfter=%s distCm=%.0f notAppliedSession=%d"),
            *E.EntryGuid.ToString(), *E.Location.ToCompactString(), *After.ToCompactString(), FVector::Dist(After, E.Location), RwgProvSessMoveNotApplied);
        return TEXT("moveNotApplied"); // nothing below ran; the caller despawns this actor and respawns at the settled spot
    }
    // rwg-farspawn-1d F2: the respawn path's enclosure + inside gates at the settled spot, this actor excluded. shadowGate is read
    // from the inside gate's own per-pass counters (which one this call raised), so it is the instrument's verdict even while the
    // refusal CVar is off; refuseIgnored is the gate's return (1 only while that CVar is set). rwg-farspawn-1f C2 + OPTION B: a reading of
    // enclosed, or of allSolid on an entry the gate does not exempt as a cave, refuses the MOVE (gatedRespawn / insideRedeal below); this
    // call itself never refuses the node.
    const bool bCaveFlagged = RwgInsideCaveExempt(E); // the gate's own refusal exemption (InsideGate.cpp:134), the same predicate the gate is handed
    int32 EncBlocked = 0, EncTotal = 0;
    const bool bEnclosed = IsSpotEnclosed(E.Location, EncBlocked, EncTotal, nullptr, nullptr, Node);
    const int32 GateInsideBefore = InsideGateWouldRefuseThisPass, GateOutsideBefore = InsideGateNegativeThisPass;
    const bool bRefuseIgnored = EvaluateInsideShadowGate(E.Location, Node, FString::Printf(TEXT("provisional move '%s'"), *E.EntryGuid.ToString()),
        /*bAdoptedNotPlaced=*/false, /*bCaveFlagged=*/bCaveFlagged);
    // allSolid = every probed direction met solid; escapeFound = at least one direction escaped (rwg-farspawn-1d F3, verbatim)
    const bool bAllSolid = InsideGateWouldRefuseThisPass > GateInsideBefore;
    const TCHAR* ShadowGate = bAllSolid ? TEXT("allSolid") : (InsideGateNegativeThisPass > GateOutsideBefore) ? TEXT("escapeFound") : TEXT("notMeasured");
    if (bRefuseIgnored)
    {
        // rwg-farspawn-1d F1 (decrement verbatim; line text adapted to 1f C2, where this move is NOT kept): this call never refuses.
        --InsideGateRefusedThisPass; // this caller never refuses: keep the census's actually-refused count true
        UE_LOG(LogNodeShuffle, Display, TEXT("PROVMOVE: entry=%s insideGateReturnedRefuse=1 moveKept=0 (the INSIDEGATE-WOULDREFUSE line above says IS BEING REFUSED; this call only measures, and the despawn + respawn that follows evaluates the gate again)"),
            *E.EntryGuid.ToString());
    }
    if (bEnclosed) { ++RwgProvSessMovedEnclosed; }
    if (bAllSolid) { ++RwgProvSessMovedShadowInside; }
    const bool bInsideRedeal = bAllSolid && !bCaveFlagged; // rwg-farspawn-1f OPTION B (round-1f review F1/F2 option B predicate, word renamed)
    if (bEnclosed || bInsideRedeal)
    {
        // rwg-farspawn-1f C2: an enclosed reading refuses the move in place; the caller despawns this actor and respawns the node at the
        // settled spot through EnsureNewNodeSpawned, which runs the overlap and enclosure gates (nudge spiral). OPTION B: an allSolid reading
        // on an entry the gate does not exempt as a cave takes precedence: the caller redeals the entry instead of placing it here (its
        // PROVINSIDE line prints where it went). A cave-flagged allSolid reading is NOT refused (the author's cave ruling outranks the gate).
        // UNGATED, one per refused move; it prints the measured readings for either word.
        const TCHAR* Word = bInsideRedeal ? TEXT("insideRedeal") : TEXT("gatedRespawn");
        if (!bInsideRedeal) { ++RwgProvSessGatedRespawn; }
        UE_LOG(LogNodeShuffle, Display, TEXT("PROVMOVE: entry=%s moveCheck=%s enclosed=%d encBlocked=%d/%d shadowGate=%s refuseIgnored=%d caveFlagged=%d at=%s gatedRespawnSession=%d"),
            *E.EntryGuid.ToString(), Word, bEnclosed ? 1 : 0, EncBlocked, EncTotal, ShadowGate, bRefuseIgnored ? 1 : 0, bCaveFlagged ? 1 : 0, *E.Location.ToCompactString(), RwgProvSessGatedRespawn);
        return Word; // nothing below ran (radiation, representation, rock): the caller's despawn / redeal tears the actor down
    }
    MeshActorCache.Remove(TWeakObjectPtr<AFGResourceNodeBase>(Node)); // as RwgProvisionalDespawn
    AdoptRotRefit.Remove(E.EntryGuid);                                // as RwgProvisionalDespawn (the respawn never re-added it)

    // Radiation: the emitter registered at spawn is keyed by this actor; re-run the spawn path's init at the new spot.
    int32 RadHad = -1; // -1 = no radioactivity subsystem in this world
    if (AFGRadioactivitySubsystem* RadSub = GetRadSubsystem())
    {
        RadHad = RadSub->mSources.Contains(Node) ? 1 : 0;
        if (RadHad == 1)
        {
            RadSub->RemoveEmitters(Node);
            Node->InitRadioactivity();
            Node->UpdateRadioactivity();
        }
    }
    Node->UpdateNodeRepresentation();
    // rwg-farspawn-1d F3: the public path to the node representation's cached location. nodeRepUpdateReturned prints
    // (-1 = no representation manager, or no node representation found after UpdateNodeRepresentation; 1/0 = UpdateRepresentation's return, whose effect on the cached location is closed source)
    int32 NodeRepUpdated = -1;
    if (AFGActorRepresentationManager* RepMgr = AFGActorRepresentationManager::Get(GetWorld()))
    {
        if (UFGResourceNodeRepresentation* NodeRep = RepMgr->FindResourceNodeRepresentation(Node)) { NodeRepUpdated = RepMgr->UpdateRepresentation(NodeRep) ? 1 : 0; }
    }

    // Dress: the rock is a component on the actor, so it moved with it; a changed rotation re-runs the rock dress the alive chain
    // runs after a rotation refit (SpawnVisualRockForNode), only when a rock is dressed. Oil decal / native visual: moved with the actor.
    UNodeShuffleNodeComponent* Comp = UNodeShuffleNodeComponent::Find(Node);
    const bool bRockDressed = Comp && IsValid(Comp->RockMesh) && Comp->RockMesh->GetStaticMesh() != nullptr;
    const bool bRotChanged = !RotFrom.Equals(E.Rotation, 1.0f);
    UClass* RC = (bRockDressed && bRotChanged) ? LoadClassByPath(E.AssignedResourceClassPath) : nullptr;
    if (RC) { SpawnVisualRockForNode(Node, RC, E.EntryGuid); }
    if (Comp) { Comp->EnsureAttachedToRoot(); }

    if (FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("PROVMOVE: entry=%s from=%s to=%s dzCm=%.0f rotChanged=%d rockDressed=%d rockRedressed=%d radEmitter=%d onSolidTop=%d enclosed=%d encBlocked=%d/%d shadowGate=%s refuseIgnored=%d nodeRepUpdateReturned=%d"),
            *E.EntryGuid.ToString(), *From.ToCompactString(), *Node->GetActorLocation().ToCompactString(), Node->GetActorLocation().Z - From.Z,
            bRotChanged ? 1 : 0, bRockDressed ? 1 : 0, RC ? 1 : 0, RadHad, E.bRwgOnSolidTop ? 1 : 0,
            bEnclosed ? 1 : 0, EncBlocked, EncTotal, ShadowGate, bRefuseIgnored ? 1 : 0, NodeRepUpdated);
    }
    return nullptr; // kept and moved
}

bool ANodeShuffleSubsystem::RwgProvisionalInsideRedeal(FNodeShuffleEntry& E, const TCHAR* Path, const FVector From, int32 LocalRevealsBefore, int32 ServerRevealsBefore)
{
    // rwg-farspawn-1f OPTION B (2026-10-31, user decision; round-1f review options table). The ONE redeal both callers use: the settle's move
    // path (Path settleMove: RwgProvisionalMoveInPlace returned insideRedeal) and the provisional spawn path (Path provisionalSpawn: the inside
    // gate counted an all-solid reading during EnsureNewNodeSpawned). Each caller has tested !RwgInsideCaveExempt(E) first. The redeal is
    // the rock pass's primitive, RwgRedealLiveEntry (teardown of the live actor if any: Mk1 snap list, representation, scan reveal, Destroy,
    // unregister after, slot removal; then TryRedealWaterLockedEntry), with the ring armed first (RwgSteepArmForInside), so the order is
    // the local ring, then map-wide, as rwg-clifftop-1 does for an occluded settle. BY DESIGN the torn-down actor's compass marker and any
    // scanner / radar reveal are LOST (a fresh actor is spawned at the new spot later); revealLostOnRedeal prints whether the torn-down actor
    // had a reveal count above zero (-1 = no actor existed). redealMoved is measured from the entry's location, never the redeal's return
    // (RwgRedealLiveEntry returns true even when the deal did not move it). UNGATED, one line per redeal.
    const FVector Before = E.Location;
    const int32 OccludedRingBefore = RwgPcSteepOccludedRing;
    RwgSteepArmForInside(E);
    const bool bRedealt = RwgRedealLiveEntry(E, TEXT("inside gate all-solid (rwg-farspawn-1f option B)"));
    bRwgSteepArmed = false; // a refused redeal, or a redeal that returned at !Q.IsUsable(), did not consume the arm
    if (!bRedealt)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("PROVINSIDE: entry=%s path=%s moveCheck=insideRedeal redealRefused=1 pinned=%d at=%s (RwgRedealLiveEntry refused: the entry is pinned or its live actor is occupied; nothing was changed)"),
            *E.EntryGuid.ToString(), Path, E.bPinned ? 1 : 0, *Before.ToCompactString());
        return false;
    }
    const bool bRing = RwgPcSteepOccludedRing > OccludedRingBefore;
    const bool bMoved = FVector::Dist2D(Before, E.Location) > 1.0;
    ++RwgProvSessInsideRedeal;
    if (bRing) { ++RwgProvSessInsideRedealRing; }
    const int32 RevealLost = (LocalRevealsBefore < 0 && ServerRevealsBefore < 0) ? -1 : (LocalRevealsBefore > 0 || ServerRevealsBefore > 0) ? 1 : 0;
    UE_LOG(LogNodeShuffle, Display, TEXT("PROVINSIDE: entry=%s res=%s path=%s moveCheck=insideRedeal from=%s to=%s redealRing=%d redealMoved=%d movedM=%.1f revealLostOnRedeal=%d localMapRevealsBefore=%d serverMapRevealsBefore=%d insideRedealSession=%d insideRedealRingSession=%d"),
        *E.EntryGuid.ToString(), *E.AssignedResourceClassPath, Path, *From.ToCompactString(), *E.Location.ToCompactString(), bRing ? 1 : 0, bMoved ? 1 : 0,
        FVector::Dist2D(From, E.Location) / 100.0, RevealLost, LocalRevealsBefore, ServerRevealsBefore, RwgProvSessInsideRedeal, RwgProvSessInsideRedealRing);
    return true;
}

void ANodeShuffleSubsystem::RwgProvisionalLogRefusal(const FNodeShuffleEntry& E, const TCHAR* Reason)
{
    // rwg-farspawn-1c C6: one line per provisional refusal (an entry is refused at most once per session: RwgProvRefused).
    // refusedReason is the predicate read this run: zNonFinite (the seam returned a non-finite Z), inactiveAfterSpawn (the spawn call
    // left the entry inactive: overlap give-up; since rwg-farspawn-1f OPTION B an inside-gate positive at a spot not dealt as a cave is redealt
    // before this line can run, and the gate never refuses a cave-flagged spot), overlapNudgeFailed (its nudge count rose), spawnActorNull (its spawn-failure count rose), noActorOtherReturn
    // (none of those changed; another early return of the spawn call ran).
    UE_LOG(LogNodeShuffle, Display, TEXT("PROVSPAWN: refused entry=%s res=%s at=%s refusedReason=%s refusedSession=%d"),
        *E.EntryGuid.ToString(), *E.AssignedResourceClassPath, *E.Location.ToCompactString(), Reason, RwgProvSessRefused);
}
