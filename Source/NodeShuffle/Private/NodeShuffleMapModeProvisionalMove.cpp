// rwg-farspawn-1c C1 (2026-10-31): keep the provisional actor when its settle on approach kept the same XY, and MOVE it to the settled
// spot instead of despawn + respawn, so the compass marker and any scanner / radar reveal on that actor survive (round-0 review F6).
// Brief: _team/test-logs/2026-10-31-nodeshuffle-farspawn-1c-brief.md. Called only from RwgSettleProvisional
// (NodeShuffleMapModeProvisional.cpp), which runs only on RWG and only after RwgResolveAtSpawn returned true.
//
// WHAT THE RESPAWN PATH (EnsureNewNodeSpawned, fresh branch) DID AFTER THE SETTLE, AND WHAT THIS DOES INSTEAD:
//   spawn at Entry.Location/Rotation      -> SetActorLocationAndRotation(Entry.Location, Entry.Rotation) on the same actor
//   InitResource / overrides / purity     -> unchanged actor, unchanged class + resource (MoveCheck refuses otherwise)
//   InitRadioactivity + UpdateRadioactivity -> emitters removed and re-initialised at the new spot when the actor had one
//   UpdateNodeRepresentation              -> called (the representation is NOT removed: that removal is what lost the marker)
//   UseBox / manager / SpawnedNodes / veto registry -> same actor pointer, already registered (nothing to re-register)
//   component attach + dress              -> component already attached; the fallback rock is re-dressed when one is dressed
//   SteadyAliveNodes.Add                  -> same instance, already steady
//   despawn's MeshActorCache / AdoptRotRefit removal -> repeated here (same state the respawn left)
//   overlap / enclosure / T54 inside-shadow gates at the settled spot -> NOT re-run (the provisional spawn ran them at the same XY,
//                                            provisional Z); listed as a runtime check in the round-1c handoff.
#include "NodeShuffleMapModeProvisional.h"
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleNodeComponent.h"
#include "NodeShuffle.h"
#include "Resources/FGResourceNode.h"
#include "FGRadioactivitySubsystem.h"
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

void ANodeShuffleSubsystem::RwgProvisionalMoveInPlace(FNodeShuffleEntry& E, AFGResourceNode* Node)
{
    if (!IsValid(Node)) { return; } // MoveCheck returned noActor before this can run; belt only
    const FVector From = Node->GetActorLocation();
    const FRotator RotFrom = Node->GetActorRotation();
    Node->SetActorLocationAndRotation(E.Location, E.Rotation);
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
        UE_LOG(LogNodeShuffle, Display, TEXT("PROVMOVE: entry=%s from=%s to=%s dzCm=%.0f rotChanged=%d rockDressed=%d rockRedressed=%d radEmitter=%d onSolidTop=%d"),
            *E.EntryGuid.ToString(), *From.ToCompactString(), *Node->GetActorLocation().ToCompactString(), Node->GetActorLocation().Z - From.Z,
            bRotChanged ? 1 : 0, bRockDressed ? 1 : 0, RC ? 1 : 0, RadHad, E.bRwgOnSolidTop ? 1 : 0);
    }
}

void ANodeShuffleSubsystem::RwgProvisionalLogRefusal(const FNodeShuffleEntry& E, const TCHAR* Reason)
{
    // rwg-farspawn-1c C6: one line per provisional refusal (an entry is refused at most once per session: RwgProvRefused).
    // refusedReason is the predicate read this run: zNonFinite (the seam returned a non-finite Z), overlapGaveUp (the spawn call left
    // the entry inactive), overlapNudgeFailed (its nudge count rose), spawnActorNull (its spawn-failure count rose), noActorOtherReturn
    // (none of those changed; another early return of the spawn call ran).
    UE_LOG(LogNodeShuffle, Display, TEXT("PROVSPAWN: refused entry=%s res=%s at=%s refusedReason=%s refusedSession=%d"),
        *E.EntryGuid.ToString(), *E.AssignedResourceClassPath, *E.Location.ToCompactString(), Reason, RwgProvSessRefused);
}
