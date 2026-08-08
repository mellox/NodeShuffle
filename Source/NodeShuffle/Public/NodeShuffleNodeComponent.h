#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NodeShuffleNodeComponent.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UStaticMeshComponent;
class UDecalComponent;
class UFGResourceDescriptor;
class AFGResourceNode;

// real-class-spawn redesign: NodeShuffle now relocates each node AS ITS ORIGINAL CLASS (vanilla
// BP_ResourceNode*, or a modded node class like the Lithium/Alkali reactive-ore node) instead of a
// generic ANodeShuffleResourceNode. That makes modded extractors' casts/binds succeed, keeps each
// node's native accept rules and (where present) its native visual.
//
// But a runtime-spawned logical resource node still needs three things our old C++ subclass baked in:
//   1. a stable identity we can match back to a layout entry,
//   2. a "this node is one of OURS" marker (so re-rolls never re-shuffle it and the force-accept hook
//      can recognise it), and
//   3. a fallback VISUAL — a logical node normally renders via a separate engine mesh actor that a
//      runtime spawn may not receive, so a relocated node can be invisible without our own rock/decal.
//
// This component is attached to every spawned node and supplies all three. Identity/persistence lives
// in the subsystem's SaveGame Layout (NOT here) — this component is a pure runtime helper, re-attached
// from the Layout on every load, so we never depend on FG re-creating a runtime component's saved data.
//
// The fallback RockMesh/OilDecal are created as OWNER-ACTOR-owned scene components (NewObject on the node,
// attached to its root, explicitly registered) — the standard reliable runtime-component pattern — NOT as
// subobjects of this component (which would not reliably enter the actor's render/registration flow).
UCLASS()
class NODESHUFFLE_API UNodeShuffleNodeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UNodeShuffleNodeComponent();

    // The layout entry this spawned node belongs to. Runtime-set; the durable copy is
    // FNodeShuffleEntry::EntryGuid in the SaveGame Layout. Not a SaveGame field (a runtime component's saved
    // data is not guaranteed to round-trip) — identity is matched from the Layout by location on load.
    FGuid EntryGuid;

    // True when the relocated node's ORIGINAL class was a vanilla /Game/ node. Drives the VISUAL choice:
    // vanilla-origin -> our fallback rock; modded-origin -> the node's own native visual.
    bool bVanillaOrigin = false;

    // True when the regular-miner (Mk) hologram should be FORCE-ACCEPTED onto this node. Decoupled from
    // bVanillaOrigin (visuals) because acceptance is its own concern. Set at spawn/adopt as
    // (resource form != RF_GAS): vanilla, oil, and AllMinable nodes are force-accepted (a runtime-spawned
    // node otherwise fails the Mk hologram's IsA(BP_ResourceNode_C) check; this restores the old behavior
    // that made modded miners like Mk.8 snap on relocated nodes); only special GAS nodes (lithium's reactive-
    // ore node) are FALSE -> native rules stand so a normal miner is rejected and only its own extractor binds.
    bool bForceAccept = false;

    // H2b-identity (2026-08-08): TRUE when this component was stamped on a RELOCATED RESOURCE-WELL member
    // (fracking core or satellite) by AttachIdentityOnly. Such a component carries NO EntryGuid (well
    // identity lives in FNodeShuffleWellEntry, keyed by CorePath -- there is no per-member GUID) and NO
    // fallback RockMesh/OilDecal (a well member is dressed by its own captured NodeShuffleWellMesh_* pieces;
    // a second rock would double-render). It exists so `Find()` recognises a well member as OURS -- which is
    // what compFound=0 in the HOLOGRAMHOOK TrySnapToActor log meant, and what kept the ACCEPTANCE /
    // ACCEPT-NODE / ACCEPT-EXT diagnostics dark on the one actor class that needed them.
    bool bWellMember = false;

    // Fallback visual, owned by the node actor (see class comment). Created lazily by EnsureVisuals().
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> RockMesh;

    UPROPERTY()
    TObjectPtr<UDecalComponent> OilDecal;

    // Create the fallback RockMesh + OilDecal on the owner node (idempotent). Applies the proven collision
    // tuning (BuildGun=Block for Mk1 snap, Pawn=Block for solidity). Called from Attach().
    void EnsureVisuals();

    // Dress the fallback RockMesh: static mesh, per-slot materials (uncovered slots keep the mesh default —
    // no back-fill, the issue #3 flat-color fix), table scale, centered relative offset. Idempotent.
    // slopefit-1: RelativeRotation = the rock's FULL smoothed-slope alignment (the owning actor stays
    // tilt-clamped so the Miner hologram gets near-vanilla geometry).
    void DressRock(UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials,
                   const FVector& Scale, const FVector& RelativeOffset,
                   const FRotator& RelativeRotation = FRotator::ZeroRotator);

    // Dress the oil-puddle decal for a LIQUID node from its descriptor's decal material + size. No-op if
    // the descriptor has no decal (solids). Idempotent.
    void DressOilDecal(TSubclassOf<UFGResourceDescriptor> ResourceClass);

    // Force the whole chain visible: owner un-hidden, the rock registered, un-hidden, visible, render-dirty.
    void ForceVisible();

    // Guarantee RockMesh/OilDecal are attached to the owner's CURRENT root (real node classes wire their own
    // root, possibly after we attach). Re-attach with KeepWorldTransform so nothing teleports.
    void EnsureAttachedToRoot();

    // Diagnostic: log the runtime render-state of the fallback rock (gated by the caller).
    void LogRenderState(const TCHAR* Phase) const;

    // Find our component on an actor (null if the actor is not one of our spawned nodes).
    static UNodeShuffleNodeComponent* Find(const AActor* Actor);

    // Create + attach a component to a freshly-spawned (or adopted) node, stamp identity, ensure visuals.
    // Returns the component (existing one if already present — idempotent on adopt).
    static UNodeShuffleNodeComponent* Attach(AFGResourceNode* Owner, const FGuid& Guid, bool bVanillaOrigin,
                                             bool bForceAccept);

    // H2b-identity: IDENTITY ONLY -- no fallback visuals, no force-accept. For RELOCATED WELL MEMBERS.
    //
    // WHY A SECOND ENTRY POINT AND NOT A CALL TO Attach(): Attach() takes AFGResourceNode*, and a fracking
    // CORE is an AFGResourceNodeFrackingCore, which derives from AFGResourceNodeBase and is NOT an
    // AFGResourceNode. The call does not compile for a core -- the identity component could never have
    // reached one. (Satellites ARE AFGResourceNode and could have; nothing ever called it for them either.)
    // This is the same AFGResourceNode-typed blind spot that has now bitten this packet three times; the
    // parameter is AActor* deliberately so no node type can fall out of it again, and because every use
    // inside is AActor-level (NewObject outer + FindComponentByClass) -- zero new engine symbol.
    //
    // bForceAccept is HARD-CODED FALSE and is not a parameter. A well member must keep its NATIVE
    // acceptance rules: with bForceAccept=false the force-accept lambda in NodeShuffle.cpp returns exactly
    // what it returned before this component existed (false), so ordinary Miners are still natively rejected
    // by a fracking satellite and the shipped fracking crash guard is neither touched nor relied upon here.
    static UNodeShuffleNodeComponent* AttachIdentityOnly(AActor* Owner);
};
