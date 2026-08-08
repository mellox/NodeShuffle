#include "NodeShuffleNodeComponent.h"
#include "NodeShuffle.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Resources/FGResourceDescriptor.h"
#include "Resources/FGResourceNode.h"

UNodeShuffleNodeComponent::UNodeShuffleNodeComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

UNodeShuffleNodeComponent* UNodeShuffleNodeComponent::Find(const AActor* Actor)
{
    if (!IsValid(Actor))
    {
        return nullptr;
    }
    return Actor->FindComponentByClass<UNodeShuffleNodeComponent>();
}

UNodeShuffleNodeComponent* UNodeShuffleNodeComponent::Attach(AFGResourceNode* Owner, const FGuid& Guid,
                                                             bool bVanillaOrigin, bool bForceAccept)
{
    if (!IsValid(Owner))
    {
        return nullptr;
    }
    // Idempotent: a re-adopted node already carries the component (re-stamp identity below).
    UNodeShuffleNodeComponent* Comp = Owner->FindComponentByClass<UNodeShuffleNodeComponent>();
    if (!Comp)
    {
        Comp = NewObject<UNodeShuffleNodeComponent>(Owner, TEXT("NodeShuffleNodeComp"));
        if (!Comp)
        {
            return nullptr;
        }
        Comp->RegisterComponent();
    }
    Comp->EntryGuid = Guid;
    Comp->bVanillaOrigin = bVanillaOrigin;
    Comp->bForceAccept = bForceAccept;
    Comp->EnsureVisuals();
    return Comp;
}

UNodeShuffleNodeComponent* UNodeShuffleNodeComponent::AttachIdentityOnly(AActor* Owner)
{
    if (!IsValid(Owner))
    {
        return nullptr;
    }
    UNodeShuffleNodeComponent* Comp = Owner->FindComponentByClass<UNodeShuffleNodeComponent>();
    const bool bCreated = (Comp == nullptr);
    if (!Comp)
    {
        Comp = NewObject<UNodeShuffleNodeComponent>(Owner, TEXT("NodeShuffleNodeComp"));
        if (!Comp)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2C-IDENTITY actor='%s': NewObject FAILED -- this well member stays UNRECOGNISED ")
                TEXT("(HOLOGRAMHOOK ... compFound=0) and its acceptance diagnostics stay dark."),
                *Owner->GetName());
            return nullptr;
        }
        Comp->RegisterComponent();
    }
    // Deliberately NOT set: EntryGuid (wells are keyed by CorePath, not a per-member GUID) and
    // bForceAccept (must stay false -- see the header). EnsureVisuals is deliberately NOT called: the
    // member is dressed by its own NodeShuffleWellMesh_* pieces and a fallback rock would double-render
    // AND would hand the portable-miner dispenser hook a rock surface a well member must not have.
    // GUARDED (ns-review-h2c F-2): only stamp a component we CREATED, or one already marked as a well
    // member. The previous form wrote these three unconditionally on any component it FOUND, every pass,
    // and the log below only fires on bCreated -- so if this ever ran against an ORDINARY node's
    // component (one carrying an EntryGuid, bVanillaOrigin, or a legitimately-true bForceAccept) it
    // would silently overwrite that node's identity with well-member identity, once per pass, invisibly.
    // Unreachable today because only well members reach this call; that is exactly the kind of
    // "unreachable by construction, unwritten, one refactor from live" trap this packet has already
    // paid for three times.
    if (bCreated || Comp->bWellMember)
    {
        Comp->bVanillaOrigin = false;
        Comp->bForceAccept = false;
        Comp->bWellMember = true;
    }
    else
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2C-IDENTITY actor='%s' class='%s': REFUSED to stamp -- this actor already carries ")
            TEXT("a NodeShuffleNodeComponent that is NOT a well member (vanillaOrigin=%d forceAccept=%d). ")
            TEXT("Left untouched. A well member and an ordinary managed node have collided on one actor; ")
            TEXT("that should be impossible, so report this line rather than working around it."),
            *Owner->GetName(), *Owner->GetClass()->GetName(),
            Comp->bVanillaOrigin ? 1 : 0, Comp->bForceAccept ? 1 : 0);
        return Comp;
    }
    // Once per actor per session by construction (bCreated), so this is not gated behind the diagnostics
    // flag -- it is the single line that proves the identity half of H2b-identity actually ran, and the
    // WELLH2B-* lines beside it in this same flow are ungated for the same reason.
    if (bCreated)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2C-IDENTITY actor='%s' class='%s': stamped UNodeShuffleNodeComponent ")
            TEXT("(wellMember=1 forceAccept=0 rockMesh=<none>) -- HOLOGRAMHOOK TrySnapToActor will now ")
            TEXT("read compFound=1 on this actor and emit ACCEPTANCE / ACCEPT-NODE / ACCEPT-EXT."),
            *Owner->GetName(), *Owner->GetClass()->GetName());
    }
    return Comp;
}

void UNodeShuffleNodeComponent::EnsureVisuals()
{
    AActor* Owner = GetOwner();
    if (!IsValid(Owner))
    {
        return;
    }
    USceneComponent* Root = Owner->GetRootComponent();

    // RockMesh: OWNER-actor-owned, attached to the node root (so its relative offset behaves like the old
    // ctor subobject and it is NEVER the root — the redesign-7 teleport bug is impossible). Collision is
    // identical to the proven ANodeShuffleResourceNode rock.
    if (!IsValid(RockMesh))
    {
        RockMesh = NewObject<UStaticMeshComponent>(Owner, TEXT("NodeShuffleRockMesh_Rt"));
        if (RockMesh)
        {
            RockMesh->SetMobility(EComponentMobility::Movable);
            if (Root) { RockMesh->SetupAttachment(Root); }
            RockMesh->RegisterComponent();
            if (Root && RockMesh->GetAttachParent() != Root)
            {
                RockMesh->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
            }
            // cosmetic (no clearance block); BuildGun=Block so the Mk1 hologram aim-trace lands on it and
            // snaps (vanilla-origin); Pawn=Block via objType=WorldDynamic so the player is solid against it.
            RockMesh->SetCollisionProfileName(TEXT("ResourceNoCollision"));
            RockMesh->SetCollisionResponseToChannel(ECC_GameTraceChannel5, ECR_Block); // BuildGun (Mk1 snap)
            RockMesh->SetCollisionObjectType(ECC_WorldDynamic);
            RockMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block); // standable like a vanilla node rock
            RockMesh->SetGenerateOverlapEvents(false);
        }
    }

    // OilDecal: OWNER-actor-owned. UDecalComponent projects along its LOCAL +X, so pitch -90 aims +X straight
    // down to project onto terrain. Hidden until a liquid node dresses it.
    if (!IsValid(OilDecal))
    {
        OilDecal = NewObject<UDecalComponent>(Owner, TEXT("NodeShuffleOilDecal_Rt"));
        if (OilDecal)
        {
            if (Root) { OilDecal->SetupAttachment(Root); }
            OilDecal->RegisterComponent();
            if (Root && OilDecal->GetAttachParent() != Root)
            {
                OilDecal->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
            }
            OilDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
            OilDecal->SetVisibility(false);
        }
    }
}

void UNodeShuffleNodeComponent::EnsureAttachedToRoot()
{
    AActor* Owner = GetOwner();
    if (!IsValid(Owner))
    {
        return;
    }
    USceneComponent* Root = Owner->GetRootComponent();
    if (!Root)
    {
        return;
    }
    if (IsValid(RockMesh) && RockMesh != Root && RockMesh->GetAttachParent() != Root)
    {
        RockMesh->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
    }
    if (IsValid(OilDecal) && OilDecal->GetAttachParent() != Root)
    {
        OilDecal->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
    }
}

void UNodeShuffleNodeComponent::DressRock(UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials,
                                          const FVector& Scale, const FVector& RelativeOffset,
                                          const FRotator& RelativeRotation)
{
    if (!IsValid(RockMesh) || !Mesh)
    {
        return;
    }
    // slopefit-1: full-slope visual alignment (relative — the actor is tilt-clamped separately).
    if (!RockMesh->GetRelativeRotation().Equals(RelativeRotation, 0.5f))
    {
        RockMesh->SetRelativeRotation(RelativeRotation);
    }
    if (RockMesh->GetStaticMesh() != Mesh)
    {
        RockMesh->SetStaticMesh(Mesh);
    }
    const int32 Slots = FMath::Max(1, RockMesh->GetNumMaterials());
    for (int32 i = 0; i < Slots; i++)
    {
        // issue #3 (flat color): apply a table material ONLY to slots the table covers; uncovered slots
        // (e.g. the rock's "Middle"/core slot) keep the MESH'S OWN default material — never back-fill.
        if (i < Materials.Num())
        {
            UMaterialInterface* Pick = Materials[i];
            if (Pick && RockMesh->GetMaterial(i) != Pick) { RockMesh->SetMaterial(i, Pick); }
        }
        else
        {
            UMaterialInterface* MeshDef = Mesh->GetMaterial(i);
            if (RockMesh->GetMaterial(i) != MeshDef) { RockMesh->SetMaterial(i, MeshDef); }
        }
    }
    if (!RockMesh->GetRelativeLocation().Equals(RelativeOffset, 1.0f))
    {
        RockMesh->SetRelativeLocation(RelativeOffset);
    }
    if (!RockMesh->GetRelativeScale3D().Equals(Scale, 0.01f))
    {
        RockMesh->SetRelativeScale3D(Scale);
    }
    ForceVisible();
}

void UNodeShuffleNodeComponent::DressOilDecal(TSubclassOf<UFGResourceDescriptor> ResourceClass)
{
    if (!IsValid(OilDecal) || !ResourceClass)
    {
        return;
    }
    UMaterial* DecalMat = UFGResourceDescriptor::GetDecalMaterial(ResourceClass);
    if (!DecalMat)
    {
        return; // this resource has no decal (e.g. a solid) — nothing to show
    }
    const float Size = UFGResourceDescriptor::GetDecalSize(ResourceClass); // on-ground half-extent
    const float Depth = FMath::Max(400.f, Size);                           // projection thickness

    if (OilDecal->GetDecalMaterial() != DecalMat)
    {
        OilDecal->SetDecalMaterial(DecalMat);
    }
    // DecalSize half-extent in LOCAL space: X = projection depth (down, after the -90 pitch), Y/Z = puddle.
    OilDecal->DecalSize = FVector(Depth, Size, Size);
    OilDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
    if (!OilDecal->IsRegistered())
    {
        OilDecal->RegisterComponent();
    }
    OilDecal->SetVisibility(true, true);
    OilDecal->MarkRenderStateDirty();
    if (AActor* Owner = GetOwner())
    {
        if (Owner->IsHidden()) { Owner->SetActorHiddenInGame(false); }
    }
}

void UNodeShuffleNodeComponent::ForceVisible()
{
    if (AActor* Owner = GetOwner())
    {
        if (Owner->IsHidden()) { Owner->SetActorHiddenInGame(false); }
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

void UNodeShuffleNodeComponent::LogRenderState(const TCHAR* Phase) const
{
    const AActor* Owner = GetOwner();
    const bool bHaveRock = IsValid(RockMesh);
    const FVector NodeLoc = Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
    const FVector RockLoc = bHaveRock ? RockMesh->GetComponentLocation() : FVector::ZeroVector;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RENDERDIAG[%s] node='%s' class='%s' actorHidden=%d nodeLoc=%s | RockMesh: valid=%d registered=%d visible=%d mesh='%s' boundsR=%.0f rockWorldLoc=%s | vanillaOrigin=%d"),
        Phase,
        Owner ? *Owner->GetName() : TEXT("<none>"),
        Owner ? *Owner->GetClass()->GetName() : TEXT("<none>"),
        (Owner && Owner->IsHidden()) ? 1 : 0, *NodeLoc.ToCompactString(),
        bHaveRock ? 1 : 0,
        bHaveRock && RockMesh->IsRegistered() ? 1 : 0,
        bHaveRock && RockMesh->IsVisible() ? 1 : 0,
        (bHaveRock && RockMesh->GetStaticMesh()) ? *RockMesh->GetStaticMesh()->GetName() : TEXT("<none>"),
        bHaveRock ? RockMesh->Bounds.SphereRadius : 0.f,
        bHaveRock ? *RockLoc.ToCompactString() : TEXT("<n/a>"),
        bVanillaOrigin ? 1 : 0);
}
