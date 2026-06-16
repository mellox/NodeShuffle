#pragma once

#include "CoreMinimal.h"
#include "Resources/FGResourceNode.h"
#include "NodeShuffleResourceNode.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UStaticMeshComponent;
class UBoxComponent;

// redesign-3 (SAVED NODE IDENTITY). Our OWN concrete resource-node class for every relocated/spawned
// NodeShuffle node. The redesign-2 nodes were identified by AActor::Tags (EntryGuid), but Tags are NOT
// SaveGame-serialized — wiped on restore — so AdoptRestoredSpawnedNodes matched 0 of N on reload and the
// whole layout re-spawned (moving occupied nodes off their miners). A real UPROPERTY(SaveGame) FGuid
// persists our identity through a save/reload round-trip (the Turtlefight "own node type" pattern).
//
// AFGResourceNode is UCLASS(Abstract); this concrete subclass is spawnable. It inherits the base
// constructor defaults (mCanPlaceResourceExtractor=true, mExtractMultiplier=1, etc.), so miners/portable
// miners place exactly as on a vanilla node. The visual rock + UseBox are still supplied by the subsystem.
UCLASS()
class NODESHUFFLE_API ANodeShuffleResourceNode : public AFGResourceNode
{
    GENERATED_BODY()

public:
    ANodeShuffleResourceNode();

    // Our stable identity, persisted across save/reload (Tags are not). Set by the subsystem at spawn;
    // read by AdoptRestoredSpawnedNodes to repopulate SpawnedNodes[guid] without re-spawning.
    UPROPERTY(SaveGame)
    FGuid EntryGuid;

    // Set true once a miner/extractor occupies this spawned node. A pinned spawned node is NEVER
    // relocated again (excluded from re-settle / re-roll relocation), so a built miner keeps its node.
    UPROPERTY(SaveGame)
    bool bNodeShuffleOccupiedPinned = false;

    // redesign-10 (MIRROR VANILLA NODE STRUCTURE FOR MK1 SNAP): the interaction/collision box is now a
    // CONSTRUCTOR default subobject AND the ACTOR ROOT — exactly like a vanilla node (whose root IS its
    // BoxComponent). SNAPDIAG proved the Mk1 extractor hologram needs root==box + the named "Resource"
    // profile + mBoxComponent wired, all together. The box answers look-at/hand-mine and is the resource
    // collider the hologram snaps to. The subsystem configures extent/profile + wires mBoxComponent.
    UPROPERTY()
    TObjectPtr<UBoxComponent> UseBox;

    // redesign-5 PRIMARY: the visual rock mesh lives ON the node as a CONSTRUCTOR-created default
    // subobject. A constructor subobject is guaranteed to register + render and moves/persists with the
    // actor. redesign-10: it is now a CHILD of the box-root (UseBox) — still a CHILD (preserving the
    // redesign-7 fix: its SetRelativeLocation offset can never move the actor). The subsystem dresses it
    // (mesh/materials/scale/offset) at spawn AND on adopt via DressRock().
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> RockMesh;

    // Dress the node's own RockMesh with the resolved visual: static mesh, per-slot materials, the
    // table scale, and a centered relative offset (lateral 0, small Z sink). Idempotent / cost-guarded.
    void DressRock(UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials,
                   const FVector& Scale, const FVector& RelativeOffset);

    // redesign-6 FIX 1 (THE BLOCKER): AFGResourceNode actors are LOGICAL — their visual normally comes
    // from a SEPARATE engine mesh actor, and the node actor itself (and/or significance management) may
    // leave the actor HIDDEN, which hides our child RockMesh regardless of component SetVisibility. Force
    // the WHOLE chain visible: actor un-hidden, RockMesh registered + un-hidden + visible + render-dirty.
    // Called AFTER OnRep_ResourceClassOverride (which may re-assert the engine's empty visual) so we win.
    void ForceVisible();

    // redesign-7 FIX 2 (defensive): the base AFGResourceNode may wire its OWN root component AFTER our
    // subclass ctor, which could leave RockMesh as the root (or orphaned). Call this at spawn AND on
    // adopt to GUARANTEE RockMesh is a child of the actor's current root (and the root is NOT RockMesh),
    // re-attaching with KeepWorldTransform if needed. Without this, a root-RockMesh's relative offset
    // would teleport the whole node to the world origin (the redesign-6 bug).
    void EnsureRockChildOfRoot();

    // redesign-6 FIX 1 diagnostic: log the actual runtime render-state truth (actor hidden? being
    // destroyed? RockMesh registered/visible? mesh/bounds/world-loc? separate engine mMeshActor + its
    // hidden state?). redesign-7 also prints the node actor location + rock world X/Y so we can CONFIRM
    // RockMesh worldLoc == node location + offset (X/Y match the node, NOT 0,0).
    void LogRenderState(const TCHAR* Phase) const;

    // Guarantee save-collection rather than relying on inherited behavior.
    virtual bool ShouldSave_Implementation() const override { return true; }
};
