#include "NodeShuffleResourceNode.h"
#include "NodeShuffle.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

ANodeShuffleResourceNode::ANodeShuffleResourceNode()
{
    // redesign-21 (THE MK1 SNAP FIX — confirmed against prior art: TheRealBeef/Satisfactory-Resource-Roulette,
    // which spawns relocated nodes and sets BOTH mCanPlacePortableMiner AND mCanPlaceResourceExtractor = true).
    // mCanPlaceResourceExtractor is UPROPERTY(EditDefaultsOnly) — it is sourced from the BLUEPRINT class-default
    // object, NOT the C++ AFGResourceNode constructor (which leaves the bool false). The vanilla BP_ResourceNode*
    // blueprints flip it true in the editor; our pure-C++ subclass never inherited that, so the Mk1 extractor
    // hologram's CanPlaceResourceExtractor()/CanOccupyResource()/IsAllowedOnResource() saw FALSE and rejected the
    // snap — EXACTLY matching the symptom (portable miners place because we set mCanPlacePortableMiner=true at
    // spawn, but the Mk1 never could). Set it here in the ctor so it bakes into THIS subclass's CDO and applies to
    // EVERY instance — fresh-spawned and adopted-after-reload alike (EditDefaultsOnly = CDO-sourced, never per-
    // instance SaveGame). This supersedes the r18/r20 runtime InitResource re-init attempts. mCanPlaceResource
    // Extractor is protected on AFGResourceNode; our subclass can set it directly.
    mCanPlaceResourceExtractor = true;
    // mExtractMultiplier=1, mPurity=Normal, mAmount=Infinite come from base/runtime. The subsystem stamps
    // EntryGuid, sets mResourceClassOverride/mPurityOverride, and supplies the UseBox at spawn.
    //
    // redesign-10 (MIRROR VANILLA NODE STRUCTURE FOR MK1 SNAP). SNAPDIAG proved a vanilla node's ROOT IS
    // its BoxComponent (root='BoxComponent_0', mBoxComponent=BoxComponent_0, profile='Resource'). Mirror
    // that EXACTLY: the UseBox is a CONSTRUCTOR default subobject AND the actor root. The named "Resource"
    // profile is set on it here (the subsystem re-asserts extent/profile + wires mBoxComponent at spawn).
    UseBox = CreateDefaultSubobject<UBoxComponent>(TEXT("NodeShuffleUseBox"));
    SetRootComponent(UseBox);
    UseBox->InitBoxExtent(FVector(650.f, 650.f, 180.f));
    UseBox->SetCollisionProfileName(TEXT("Resource"));

    // redesign-7 (THE LOCATION BUG): RockMesh is a CONSTRUCTOR default subobject (renders reliably) and
    // MUST ALWAYS be a CHILD, NEVER the actor root — else DressRock's SetRelativeLocation((0,0,-40)) on a
    // root component == world-location and teleports the whole actor to the origin (the redesign-6 bug).
    // redesign-10: it is now a child of the BOX-ROOT (UseBox) instead of a separate DefaultSceneRoot.
    RockMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NodeShuffleRockMesh"));
    if (RockMesh)
    {
        RockMesh->SetupAttachment(UseBox); // child of the box-root → relative offset stays local
        RockMesh->SetMobility(EComponentMobility::Movable);
        // redesign-8: the rock is PURELY COSMETIC. "ResourceNoCollision" (ObjectType=Resource, QueryOnly,
        // ignores WorldStatic/BuildGun, overlaps Hologram) keeps it visible + look-at-able but it NEVER
        // blocks construction (the Clearance profile ignores Resource-objecttype). Do NOT reintroduce a
        // WorldStatic/clearance-blocking profile.
        RockMesh->SetCollisionProfileName(TEXT("ResourceNoCollision")); // objType=Resource (Clearance ignores
        // it by objtype → preserves the redesign-8 footprint fix), QueryOnly, Pawn-passthrough, all-ignore base.
        // redesign-16 (THE MK1 SNAP FIX — PROVEN by a perfect correlation in the r13/14/15 HOLOGRAMHOOK log).
        // The Mk1 hologram's aim trace is on the BUILDGUN channel (ECC_GameTraceChannel5). Evidence: EVERY object
        // the trace hit (terrain, foliage, cliffs, modded Res_ nodes, a placed Miner) has BuildGun=Block; EVERY
        // one of our components (box + rock, across r10–r15) has BuildGun=Ignore and was NEVER hit — a perfect
        // BuildGun=Block ⟺ hit correlation. r14 (block Visibility/Camera/WorldStatic) and r15 (objType=WorldDynamic)
        // both missed because NEITHER touched BuildGun. FIX: make the visible rock BLOCK the BuildGun channel so
        // the build-gun trace lands on it → hitResult.GetActor() = our node → the hologram resolves+snaps (the node
        // already PASSES validation, r13 VALIDDIAG). objType stays Resource so the miner CLEARANCE still ignores
        // the rock (no footprint block). Blocking BuildGun makes the rock a build-gun surface like any vanilla
        // node rock — you build around it; it does NOT gate the miner you snap onto it (snapped node is exempt).
        RockMesh->SetCollisionResponseToChannel(ECC_GameTraceChannel5, ECR_Block); // BuildGun <-- THE FIX
        RockMesh->SetGenerateOverlapEvents(false);
    }
}

void ANodeShuffleResourceNode::DressRock(UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials,
                                         const FVector& Scale, const FVector& RelativeOffset)
{
    if (!IsValid(RockMesh) || !Mesh)
    {
        return;
    }
    if (RockMesh->GetStaticMesh() != Mesh)
    {
        RockMesh->SetStaticMesh(Mesh);
    }
    const int32 Slots = FMath::Max(1, RockMesh->GetNumMaterials());
    for (int32 i = 0; i < Slots; i++)
    {
        UMaterialInterface* Pick = nullptr;
        if (i < Materials.Num()) { Pick = Materials[i]; }
        else if (Materials.Num() > 0) { Pick = Materials.Last(); }
        if (Pick && RockMesh->GetMaterial(i) != Pick) { RockMesh->SetMaterial(i, Pick); }
    }
    // Centered on the node (relative to root): lateral 0, small Z sink only. Table scale applied.
    if (!RockMesh->GetRelativeLocation().Equals(RelativeOffset, 1.0f))
    {
        RockMesh->SetRelativeLocation(RelativeOffset);
    }
    if (!RockMesh->GetRelativeScale3D().Equals(Scale, 0.01f))
    {
        RockMesh->SetRelativeScale3D(Scale);
    }
    // redesign-6: force the whole chain visible — actor un-hidden + component registered/visible.
    ForceVisible();
}

void ANodeShuffleResourceNode::EnsureRockChildOfRoot()
{
    // redesign-7 FIX 2 (defensive). Guarantee RockMesh is a child of the actor's CURRENT root and that
    // the root is NOT RockMesh — even if the base wired its own root after our ctor, or a prior build
    // left RockMesh as the root. A root-RockMesh would make DressRock's relative offset move the whole
    // node to the world origin. Re-attach with KeepWorldTransform so we never move the node here.
    if (!IsValid(RockMesh))
    {
        return;
    }
    USceneComponent* Root = GetRootComponent();

    // If RockMesh somehow IS the root (no other root exists), give the actor a dedicated scene root and
    // re-parent RockMesh under it, keeping RockMesh's world transform so nothing teleports.
    if (Root == RockMesh)
    {
        USceneComponent* NewRoot = NewObject<USceneComponent>(this, TEXT("NodeShuffleDefaultSceneRoot_Rt"));
        NewRoot->SetWorldLocationAndRotation(RockMesh->GetComponentLocation(), RockMesh->GetComponentRotation());
        NewRoot->RegisterComponent();
        SetRootComponent(NewRoot);
        RockMesh->AttachToComponent(NewRoot, FAttachmentTransformRules::KeepWorldTransform);
        return;
    }

    // Normal case: ensure RockMesh is attached to the current root (re-attach if orphaned or mis-parented).
    if (Root && RockMesh->GetAttachParent() != Root)
    {
        RockMesh->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
    }
}

void ANodeShuffleResourceNode::ForceVisible()
{
    // redesign-6 FIX 1 (THE BLOCKER). AFGResourceNode actors are logical; the node actor and/or
    // significance management may leave the ACTOR hidden, which hides our child RockMesh regardless of
    // component visibility. Un-hide the WHOLE chain and force the component into the render scene.
    if (IsHidden())
    {
        SetActorHiddenInGame(false);
    }
    if (!IsValid(RockMesh))
    {
        return;
    }
    if (!RockMesh->IsRegistered())
    {
        RockMesh->RegisterComponent();
    }
    RockMesh->SetHiddenInGame(false, true);
    if (!RockMesh->IsVisible())
    {
        RockMesh->SetVisibility(true, true);
    }
    RockMesh->MarkRenderStateDirty();
}

void ANodeShuffleResourceNode::LogRenderState(const TCHAR* Phase) const
{
    const AActor* Mesh = GetMeshActor(); // separate engine mesh actor (logical nodes render via it); protected getter
    const bool bHaveRock = IsValid(RockMesh);
    const FVector NodeLoc = GetActorLocation();
    const FVector RockLoc = bHaveRock ? RockMesh->GetComponentLocation() : FVector::ZeroVector;
    const USceneComponent* Root = GetRootComponent();
    // redesign-7: also print the NODE actor location + the rock WORLD location and whether RockMesh is the
    // root, so the log CONFIRMS rock worldLoc X/Y == node X/Y (NOT 0,0). The redesign-6 bug showed
    // rock worldLoc=(0,0,-40) while node=(-267337,66938,-1356) — rock was the root, offset teleported it.
    const bool bRockIsRoot = (Root == RockMesh);
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RENDERDIAG[%s] node='%s' actorHidden=%d beingDestroyed=%d nodeLoc=%s | RockMesh: valid=%d isRoot=%d attachOK=%d registered=%d visible=%d hiddenInGame=%d mesh='%s' boundsR=%.0f rockWorldLoc=%s | engineMeshActor=%s%s"),
        Phase, *GetName(),
        IsHidden() ? 1 : 0, IsActorBeingDestroyed() ? 1 : 0, *NodeLoc.ToCompactString(),
        bHaveRock ? 1 : 0,
        bRockIsRoot ? 1 : 0,
        (bHaveRock && Root && RockMesh->GetAttachParent() == Root) ? 1 : 0,
        bHaveRock && RockMesh->IsRegistered() ? 1 : 0,
        bHaveRock && RockMesh->IsVisible() ? 1 : 0,
        bHaveRock && RockMesh->bHiddenInGame ? 1 : 0,
        (bHaveRock && RockMesh->GetStaticMesh()) ? *RockMesh->GetStaticMesh()->GetName() : TEXT("<none>"),
        bHaveRock ? RockMesh->Bounds.SphereRadius : 0.f,
        bHaveRock ? *RockLoc.ToCompactString() : TEXT("<n/a>"),
        Mesh ? *Mesh->GetName() : TEXT("<none>"),
        Mesh ? (Mesh->IsHidden() ? TEXT(" (engineMeshActor HIDDEN)") : TEXT(" (engineMeshActor visible)")) : TEXT(""));
}
