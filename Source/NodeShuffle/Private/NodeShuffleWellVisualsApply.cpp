// Packet H2b (ns-wells-h2b, branch wip/h2-relocation): THE DESTINATION HALF -- dressing a relocated
// resource well and, far more importantly, GIVING IT SOMETHING THE BUILD GUN CAN HIT.
//
// Split from NodeShuffleWellVisuals.cpp for the 500-line limit, and the seam is a real one rather than
// a convenience: that file answers "which mesh pieces ARE this vanilla well member, and hide them",
// this one answers "re-create them on the actor we spawned, with collision". Origin side and
// destination side. They share one index and nothing else.
//
// READ THE HEADER OF NodeShuffleWellVisuals.cpp FIRST -- it carries the measured finding this whole
// packet exists for (a relocated well that spawns correctly and cannot be built on, because the
// hologram's trace passes through it and lands on terrain) and the three-route pairing rationale.
//
// THE ONE-LINE VERSION: appearance is the by-product here. Collision is the acceptance criterion. If a
// relocated well looks perfect and a Resource Well Pressurizer still will not snap to its core, this
// file has failed and WELLH2B-COLLISION in the log is where to look first.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleWellRetype.h"   // WellShort

#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"

namespace
{
    // Every primitive on one of our spawned well actors, with the ONE channel that decides whether the
    // build gun can hit it. This is the diagnostic that makes a failed T1/T2 explicable without another
    // build: if the snap still fails, this line already names every surface the trace could have landed
    // on and what each one does with ECC_GameTraceChannel5.
    //
    // Said once per actor PER PIECE COUNT, not once per actor -- see the F-2 note at the call site. The
    // piece count is printed so a stale-looking dump can be told from a current one at a glance.
    void DumpWellActorCollision(AActor* Actor, const TCHAR* Tag, int32 PiecesThisPass)
    {
        if (!IsValid(Actor)) { return; }
        TInlineComponentArray<UPrimitiveComponent*> Prims(Actor);
        FString Desc;
        for (UPrimitiveComponent* P : Prims)
        {
            if (!IsValid(P)) { continue; }
            Desc += FString::Printf(
                TEXT("[%s cls='%s' coll=%d profile='%s' objType=%d BuildGun=%d Pawn=%d visible=%d] "),
                *P->GetName(), *P->GetClass()->GetName(),
                static_cast<int32>(P->GetCollisionEnabled()), *P->GetCollisionProfileName().ToString(),
                static_cast<int32>(P->GetCollisionObjectType()),
                static_cast<int32>(P->GetCollisionResponseToChannel(ECC_GameTraceChannel5)),
                static_cast<int32>(P->GetCollisionResponseToChannel(ECC_Pawn)),
                P->IsVisible() ? 1 : 0);
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2B-COLLISION [%s] actor='%s' class='%s' at %s actorCollision=%d hidden=%d ")
            TEXT("dressedPieces=%d :: %s")
            TEXT("(response codes: 0=Ignore 1=Overlap 2=Block. The build gun aims on ECC_GameTraceChannel5 -- ")
            TEXT("if NOTHING here reports BuildGun=2 the trace will pass through this actor and land on terrain, ")
            TEXT("which is the failure measured on the previous build and fixed here. EXPECT profile='Custom' on ")
            TEXT("our NodeShuffleWellMesh_* components: SetCollisionObjectType/SetCollisionResponseToChannel both ")
            TEXT("switch FBodyInstance's profile name to Custom, so 'Custom' is the recipe HAVING BEEN APPLIED, ")
            TEXT("not a failure -- objType and BuildGun are the authoritative fields. dressedPieces=0 means this ")
            TEXT("actor had no visual to apply on the pass that produced this line.)"),
            Tag, *Actor->GetName(), *Actor->GetClass()->GetName(),
            *Actor->GetActorLocation().ToCompactString(),
            Actor->GetActorEnableCollision() ? 1 : 0, Actor->IsHidden() ? 1 : 0, PiecesThisPass, *Desc);
    }
}

// ------------------------------------------------------------------------------------------------
// THE COLLISION RECIPE -- THE ACCEPTANCE CRITERION, IN ONE PLACE
// ------------------------------------------------------------------------------------------------
// Channel by channel, and every choice here is a previously-paid-for lesson rather than a preference:
//
//  * BASE PROFILE "ResourceNoCollision" -- objType=Resource, QueryOnly, ignores everything, overlaps
//    Hologram. The proven COSMETIC base: the building-clearance profile ignores the Resource object
//    type, so a rock on this profile can never block the footprint of the machine being built on it.
//    (redesign-8: a WorldStatic rock made the Miner Mk1 footprint reject its own node.)
//
//  * ECC_GameTraceChannel5 (BuildGun) = BLOCK -- THIS IS THE FIX. redesign-16 established the
//    mechanism by a perfect correlation in the r13/14/15 HOLOGRAMHOOK logs: every object the build-gun
//    trace hit had BuildGun=Block, every component of ours had BuildGun=Ignore and was never hit.
//    Blocking it makes hitResult.GetActor() our fracking core/satellite, which IS an
//    IFGExtractableResourceInterface, which is what AFGResourceExtractorHologram::
//    TrySnapToExtractableResource needs. r14 (Visibility/Camera/WorldStatic) and r15 (objType alone)
//    both missed because neither touched this channel.
//
//  * objType ECC_WorldDynamic + ECC_Pawn = BLOCK -- player solidity, so a relocated well reads as
//    ground you stand on like a vanilla one. DELIBERATELY NOT WorldStatic: WorldStatic is the one
//    object type the extractor encroachment test rejects (memory:nodeshuffle-rock-miner-polish), and
//    the Pawn preset IGNORES the Resource object type, which is why the profile alone leaves a rock
//    walk-through. This exact pair is what shipped for ordinary node rocks and has been in the
//    user's world since polish-2.
//
//  * EVERYTHING ELSE STAYS IGNORE -- Clearance included. We add a build-gun surface; we do not add an
//    obstacle. Overlap events off: nothing subscribes and they are not free.
//
// WHAT IS NOT PROVABLE HERE. That this makes a Resource Well Pressurizer and a Well Extractor actually
// snap runs through the hologram's closed-source placement path. It is graded ASSUMED and it is test
// step 1 of the runtime script, not a footnote.
void ANodeShuffleSubsystem::ConfigureWellMeshCollision(UStaticMeshComponent* Comp)
{
    if (!IsValid(Comp)) { return; }
    Comp->SetCollisionProfileName(TEXT("ResourceNoCollision"));
    Comp->SetCollisionResponseToChannel(ECC_GameTraceChannel5, ECR_Block); // BuildGun -- THE FIX
    Comp->SetCollisionObjectType(ECC_WorldDynamic);
    Comp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);              // standable, like vanilla
    Comp->SetGenerateOverlapEvents(false);
}

// ------------------------------------------------------------------------------------------------
// APPLY -- dress the RELOCATED actors, and give them something to hit
// ------------------------------------------------------------------------------------------------
int32 ANodeShuffleSubsystem::DressWellActor(AActor* Actor, const TArray<FNodeShuffleWellVisual>& Visuals,
                                            int32& OutPieces)
{
    if (!IsValid(Actor) || Visuals.Num() == 0) { return 0; }
    USceneComponent* Root = Actor->GetRootComponent();
    if (!Root)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2B-APPLY actor='%s': NO ROOT COMPONENT -- cannot attach a well mesh, so this ")
            TEXT("member stays invisible AND unbuildable. Nothing else in this packet can compensate."),
            *Actor->GetName());
        return 0;
    }
    int32 Created = 0;
    for (int32 i = 0; i < Visuals.Num(); ++i)
    {
        const FNodeShuffleWellVisual& V = Visuals[i];
        UStaticMesh* Mesh = V.MeshPath.IsEmpty()
            ? nullptr : LoadObject<UStaticMesh>(nullptr, *V.MeshPath);
        if (!Mesh) { continue; }
        const FName CompName(*FString::Printf(TEXT("NodeShuffleWellMesh_%d"), i));
        UStaticMeshComponent* Comp = nullptr;
        TInlineComponentArray<UStaticMeshComponent*> Existing(Actor);
        for (UStaticMeshComponent* C : Existing)
        {
            if (IsValid(C) && C->GetFName() == CompName) { Comp = C; break; }
        }
        if (!Comp)
        {
            Comp = NewObject<UStaticMeshComponent>(Actor, CompName);
            if (!Comp) { continue; }
            Comp->SetMobility(EComponentMobility::Movable);
            Comp->SetupAttachment(Root);
            Comp->RegisterComponent();
            if (Comp->GetAttachParent() != Root)
            {
                Comp->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
            }
            ConfigureWellMeshCollision(Comp);
            ++Created;
        }
        if (Comp->GetStaticMesh() != Mesh) { Comp->SetStaticMesh(Mesh); }
        // Slot-exact materials. A slot recorded EMPTY keeps the mesh's own default -- never back-filled
        // with the previous slot's material, which is the flat-colour regression DressRock records
        // against (memory:nodeshuffle-rock-miner-polish).
        const int32 Slots = FMath::Max(1, Comp->GetNumMaterials());
        for (int32 s = 0; s < Slots; ++s)
        {
            UMaterialInterface* Want = nullptr;
            if (s < V.MaterialPaths.Num() && !V.MaterialPaths[s].IsEmpty())
            {
                Want = LoadObject<UMaterialInterface>(nullptr, *V.MaterialPaths[s]);
            }
            if (!Want) { Want = Mesh->GetMaterial(s); }
            if (Want && Comp->GetMaterial(s) != Want) { Comp->SetMaterial(s, Want); }
        }
        // The RELATIVE transform is what makes the group yaw and the per-member terrain settle come out
        // right for free: the actor already carries both, so re-applying the captured local offset
        // rotates and tilts the piece with it. No second rotation path means no second thing to disagree.
        if (!Comp->GetRelativeLocation().Equals(V.RelLocation, 1.0f)) { Comp->SetRelativeLocation(V.RelLocation); }
        if (!Comp->GetRelativeRotation().Equals(V.RelRotation, 0.5f)) { Comp->SetRelativeRotation(V.RelRotation); }
        if (!Comp->GetRelativeScale3D().Equals(V.RelScale, 0.01f)) { Comp->SetRelativeScale3D(V.RelScale); }
        // Re-assert every pass: the collision, not just the look, is what the acceptance test measures,
        // and a streamed/significance round trip must not be able to quietly take it away.
        ConfigureWellMeshCollision(Comp);
        if (!Comp->IsVisible()) { Comp->SetVisibility(true, true); }
        Comp->SetHiddenInGame(false, true);
        ++OutPieces;
    }
    if (Actor->IsHidden()) { Actor->SetActorHiddenInGame(false); }
    if (!Actor->GetActorEnableCollision()) { Actor->SetActorEnableCollision(true); }
    return Created;
}

void ANodeShuffleSubsystem::ApplyWellGroupVisuals(FNodeShuffleWellEntry& E)
{
    int32 Created = 0, Pieces = 0, Members = 0, FromTemplate = 0, NoVisual = 0;

    const auto DressOne = [&](AActor* Actor, const TArray<FNodeShuffleWellVisual>& Own,
                              const TArray<FNodeShuffleWellVisual>& Template, const TCHAR* Tag) -> void
    {
        if (!IsValid(Actor)) { return; }
        ++Members;
        const TArray<FNodeShuffleWellVisual>& Use = Own.Num() > 0 ? Own : Template;
        if (Use.Num() == 0) { ++NoVisual; }
        else if (Own.Num() == 0) { ++FromTemplate; }
        const int32 PiecesBefore = Pieces;
        Created += DressWellActor(Actor, Use, Pieces);
        const int32 MyPieces = Pieces - PiecesBefore;
        // ns-review-h2b F-2: KEYED ON THE PATH **AND THE PIECE COUNT**, NOT THE PATH ALONE.
        // Keyed on the path alone this dump fired once per actor per session -- which is the exact
        // throttle bug the same commit fixed in WELLH2-SUPPRESS, re-introduced in a brand-new line. A
        // group whose origin has not streamed dresses with zero pieces, and the dump then recorded an
        // actor with NO NodeShuffleWellMesh_* components at all and never fired again; three passes
        // later the origin streams and the actor is dressed properly, but the only dump anyone can grep
        // still says "nothing here blocks BuildGun". That is the wrong diagnosis handed to a tester in
        // precisely the case that needed the diagnostic. With the count in the key the dump re-fires the
        // moment the actor gains (or loses) pieces, and stays silent while nothing changes.
        const FString DumpKey = FString::Printf(TEXT("%s|%d"), *Actor->GetPathName(), MyPieces);
        if (!WellVisualCompDumped.Contains(DumpKey))
        {
            WellVisualCompDumped.Add(DumpKey);
            DumpWellActorCollision(Actor, Tag, MyPieces);
        }
    };

    DressOne(SpawnedWellCores.FindRef(E.CorePath), E.CoreVisuals, WellVisualTemplateCore, TEXT("core"));
    for (const FNodeShuffleWellSatellite& S : E.Satellites)
    {
        if (!S.bCaptured) { continue; }
        DressOne(SpawnedWellSatellites.FindRef(S.SatellitePath), S.Visuals,
                 WellVisualTemplateSatellite, TEXT("satellite"));
    }

    // One summary per group per piece-count, so the line reappears if the group gains pieces later
    // (a member whose original streamed in after the first dress) but does not repeat every ~5 s.
    const FString Key = FString::Printf(TEXT("%s|%d|%d"), *E.CorePath, Members, Pieces);
    if (Members > 0 && !WellVisualLogged.Contains(Key))
    {
        WellVisualLogged.Add(Key);
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2B-APPLY core='%s' at %s: dressed %d relocated member(s) with %d mesh piece(s) ")
            TEXT("(%d component(s) created this pass); %d member(s) fell back to the SESSION TEMPLATE ")
            TEXT("(their own origin never streamed -- the look may be the wrong biome variant), %d ")
            TEXT("member(s) have NO visual at all and remain invisible AND unbuildable. Every piece ")
            TEXT("blocks ECC_GameTraceChannel5 (BuildGun), which is what a Pressurizer/Well Extractor ")
            TEXT("hologram traces on -- see WELLH2B-COLLISION for the per-component proof."),
            *WellShort(E.CorePath), *E.PlacedCoreLocation.ToCompactString(), Members, Pieces, Created,
            FromTemplate, NoVisual);
    }
}
