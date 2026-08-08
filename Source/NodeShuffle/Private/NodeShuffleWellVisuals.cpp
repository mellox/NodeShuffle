// Packet H2b (ns-wells-h2b, branch wip/h2-relocation): GROUP VISUALS AND COLLISION for a relocated
// resource well, plus the origin-side mesh hide that failed for five well groups out of six.
//
// THE MEASURED FINDING THIS FILE EXISTS FOR. A relocated well spawns correctly -- right resource,
// right satellite count, at the dealt destination -- and NOTHING CAN BE BUILT ON IT, because there is
// nothing there for the build gun to aim at. Measured in-game 2026-08-07 from our own hologram hook,
// on two independent relocated wells (a chlorine group and a water group, both confirmed ours by
// NodeShuffle.Here):
//     HOLOGRAMHOOK IsValidHitResult -> 1 | hitActor='LandscapeStreamingProxy_...'
//     HOLOGRAMHOOK HIT comp='LandscapeHeightfieldCollisionComponent_2' profile='BlockAll'
//     HOLOGRAMHOOK TrySnapToActor  -> 0 | hitActor='LandscapeStreamingProxy_...'
// The trace passed straight THROUGH our spawned fracking actors and landed on terrain. So this packet
// is not a cosmetics packet with a buildability side effect; it is a BUILDABILITY packet whose
// mechanism happens to be the mesh. Appearance is the by-product. If the well looks right and a
// Pressurizer still will not snap, this file has failed.
//
// WHY A RELOCATED WELL HAS NOTHING TO HIT. A vanilla core/satellite is a level actor paired with a
// separate AFGNodeMeshActor that carries the rock, the crack graphic and the collision. A
// runtime-SpawnActorDeferred'd core/satellite gets the class's own components and NO mesh actor at
// all -- exactly the same reason NodeShuffle already spawns its own rock for ordinary relocated nodes
// (UNodeShuffleNodeComponent::EnsureVisuals). Wells simply never got the equivalent.
//
// ONE OBJECT CARRIES BOTH. The component that shows the rock is the component that blocks the build
// gun. That is deliberate: a separate invisible collider could be forgotten, mis-sized, or hidden by
// a later visibility change while the visual stayed, and nothing would report it.
//
// THE PAIRING BUG, WHICH IS THE SAME BUG AT THE OTHER END. WELLH2-SUPPRESS hides the vanilla group
// but found the members' MESH actors for only one group in six -- measured 2026-08-07:
//     core='BP_FrackingCore17'        : hid 8 vanilla member(s) + 8 mesh actor(s)
//     core='BP_FrackingCore6_UAID_...': hid 7 vanilla member(s) + 0 mesh actor(s)   (x5 groups)
// all while reporting "0 not streamed yet". The cause is not streaming and not timing. It is that
// SuppressVanillaWellGroup asked FindMeshActorForNode, which reads MeshActorCache, which is built by
// RebuildMeshActorCache -- and that function's forward-link sweep iterates TActorIterator<
// AFGResourceNode> and CONTINUES on IsFrackingActor(Node), while a fracking CORE is an
// AFGResourceNodeBase and is not in that iterator's type at all. So for a well member only the
// back-link sweep (AFGNodeMeshActor::mNodeActor, unset for ~98% of level nodes in this world --
// memory:nodeshuffle-descriptor-visuals) could ever fire. One group in six had it set. Six for six is
// what the numbers say, not a coincidence.
//
// The same index therefore serves BOTH ends: capture-at-destination and hide-at-origin are one
// problem, and fixing them separately would have left two mechanisms to disagree.
//
// THREE PAIRING ROUTES, IN ORDER OF TRUST (memory:nodeshuffle-descriptor-visuals' "spatial pairing
// only" lesson applies -- pair by STABLE PHYSICAL POSITION, never by a mesh-name/resource key that
// mutates after the shuffle):
//   own     -- a static-mesh component of the member actor itself. Intrinsic; cannot be wrong.
//   link    -- AFGResourceNodeBase::mMeshActor (friend-read) or the existing back-link cache.
//   spatial -- an AStaticMeshActor whose mesh name is in the node-rock/fracking family, within
//              WellMeshOwnerRadiusCm of the member AND STRICTLY NEARER TO IT THAN TO ANY OTHER well
//              member. H0 measured min inter-satellite 1818.8 cm and min core->satellite 2076 cm over
//              401 pairs, and the crack graphic that prompted this sits 940 cm from its core -- so at
//              a 1200 cm radius nearest-wins has roughly 9 m of margin. That margin is MEASURED, not
//              assumed, and the log prints the runner-up distance so it can be re-measured.
//
// WHY THE CAPTURE IS SaveGame. Relocation is spawn-on-discovery at the DESTINATION. The origin can be
// kilometres away and never stream again this session, so a capture held only in RAM would leave
// exactly the wells a tester flies to invisible and unbuildable. Asset paths + a relative transform
// survive the round trip; UObject pointers do not.
//
// THE OTHER HALF IS NodeShuffleWellVisualsApply.cpp. This file is the ORIGIN side -- which mesh pieces
// ARE a given vanilla well member, capture them, hide them. That file is the DESTINATION side -- put
// them back on the actor we spawned, with the collision recipe that is this packet's actual acceptance
// criterion. Split at the 500-line limit, along a seam that was already there.
//
// IMPORT DISCIPLINE (memory:sf-shipping-export-trap). New engine surface reached from here:
// AStaticMeshActor::StaticClass (Engine, not FactoryGame), UStaticMeshComponent's setters (Engine),
// and on the FactoryGame side only field reads (mMeshActor, mNodeMeshType) plus the AFGNodeMeshActor
// StaticClass thunk that FindMeshActorForNode already uses. That is the PREDICTION. The import table
// is MEASURED after the build and every new symbol named; three predictions have been falsified on
// this machine.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleWellCensus.h"   // AFGResourceNodeFrackingCore / ...Satellite
#include "NodeShuffleWellRetype.h"   // WellPathOf / WellShort

#include "Resources/FGResourceNodeBase.h" // AFGNodeMeshActor / ENodeMeshType

#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

// 12 m. Deliberately the SAME constant the ordinary-node orphan sweep uses for "a real node owns this
// rock" (OrphanOwnerRadius), because it is answering the same question about the same world, and two
// different radii for one question is how the two passes would come to disagree about who owns a rock.
static constexpr float WellMeshOwnerRadiusCm = 1200.0f;

namespace
{
    // A material we can name in a save file. A UMaterialInstanceDynamic is created at runtime and its
    // path names nothing on a later load, so it is recorded as "use the mesh's own default" rather
    // than as a path that will silently fail to resolve.
    FString SaveableMaterialPath(UMaterialInterface* Mat)
    {
        if (!IsValid(Mat) || Mat->IsA<UMaterialInstanceDynamic>()) { return FString(); }
        const FString Path = Mat->GetPathName();
        if (Path.IsEmpty() || Path.Contains(TEXT("/Engine/Transient"))) { return FString(); }
        return Path;
    }

}


// ------------------------------------------------------------------------------------------------
// THE INDEX: vanilla well member -> the static-mesh pieces that ARE its look
// ------------------------------------------------------------------------------------------------
void ANodeShuffleSubsystem::EnsureWellMeshIndex()
{
    if (WellMeshIndexPass == WellAuditPasses) { return; } // at most once per apply pass
    WellMeshIndexPass = WellAuditPasses;
    RebuildWellMeshIndex();
}

void ANodeShuffleSubsystem::RebuildWellMeshIndex()
{
    WellMeshIndex.Reset();
    WellMeshIndexMembers = 0;
    WellMeshIndexPieces = 0;
    UWorld* World = GetWorld();
    if (!World) { return; }
    const bool bDiag = FNodeShuffleModule::AreDiagnosticsEnabled();

    // OUR OWN spawned well actors are never "vanilla members" and their pieces are never indexed --
    // otherwise a relocated well landing within 12 m of a vanilla one could have ITS OWN new visual
    // assigned to that vanilla member and then hidden by the suppression it triggered.
    TSet<const AActor*> Ours;
    for (const auto& P : SpawnedWellCores) { if (IsValid(P.Value)) { Ours.Add(P.Value); } }
    for (const auto& P : SpawnedWellSatellites) { if (IsValid(P.Value)) { Ours.Add(P.Value); } }

    struct FMemberRec { FVector Loc; AFGResourceNodeBase* Node; FString Path; };
    TArray<FMemberRec> Members;
    for (TActorIterator<AFGResourceNodeBase> It(World); It; ++It)
    {
        AFGResourceNodeBase* N = *It;
        if (!IsValid(N) || !IsFrackingActor(N) || Ours.Contains(N)) { continue; }
        Members.Add({ N->GetActorLocation(), N, WellPathOf(N) });
    }
    WellMeshIndexMembers = Members.Num();

    TSet<UStaticMeshComponent*> Claimed;
    int32 ByOwn = 0, ByLink = 0, BySpatial = 0;

    // The fracking family by name (SM_FrackingNode_Crack_01 / _Mid_01 / _Small_01 and friends), plus
    // anything the shared node-rock recogniser already accepts. Name matching decides ONLY "could this
    // be a well piece at all" -- WHICH member it belongs to is decided by DISTANCE, never by name
    // (memory:nodeshuffle-descriptor-visuals: pairing by a mesh-name/resource key decays to nothing
    // once the shuffle has retyped the world). A lambda rather than a free function because
    // IsNodeRockMeshName is a private member of this class.
    const auto IsWellPieceMeshName = [](const FString& MeshName) -> bool
    {
        return MeshName.Contains(TEXT("Frack")) || IsNodeRockMeshName(MeshName, TArray<FString>());
    };

    const auto AddPiece = [&](const FString& Path, UStaticMeshComponent* C) -> bool
    {
        if (!IsValid(C) || Claimed.Contains(C)) { return false; }
        Claimed.Add(C);
        WellMeshIndex.FindOrAdd(Path).Add(C);
        ++WellMeshIndexPieces;
        return true;
    };
    const auto IsUsableMesh = [](UStaticMeshComponent* C) -> bool
    {
        return IsValid(C) && !Cast<UInstancedStaticMeshComponent>(C) && C->GetStaticMesh() != nullptr
            && !C->GetName().StartsWith(TEXT("NodeShuffleWellMesh"));
    };

    // ---- ROUTE 1: the member's OWN components ----
    for (const FMemberRec& M : Members)
    {
        TInlineComponentArray<UStaticMeshComponent*> Own(M.Node);
        for (UStaticMeshComponent* C : Own)
        {
            if (IsUsableMesh(C) && AddPiece(M.Path, C)) { ++ByOwn; }
        }
    }

    // ---- ROUTE 2: the engine links (forward mMeshActor, then the existing back-link cache) ----
    for (const FMemberRec& M : Members)
    {
        TArray<AActor*> LinkActors;
        // Friend-granted read of the private soft pointer -- the same access RebuildMeshActorCache
        // already uses for ordinary nodes. This is the link design 2.4 assumed would be there; it is
        // kept as route 2 rather than dropped, because when it IS set it is authoritative.
        if (AActor* Fwd = M.Node->mMeshActor.Get()) { LinkActors.Add(Fwd); }
        if (AFGNodeMeshActor* Back = FindMeshActorForNode(M.Node)) { LinkActors.AddUnique(Back); }
        for (AActor* LA : LinkActors)
        {
            if (!IsValid(LA) || Ours.Contains(LA)) { continue; }
            TInlineComponentArray<UStaticMeshComponent*> Comps(LA);
            for (UStaticMeshComponent* C : Comps)
            {
                if (IsUsableMesh(C) && AddPiece(M.Path, C)) { ++ByLink; }
            }
        }
    }

    // ---- ROUTE 3: SPATIAL, nearest-member-wins, with the runner-up printed ----
    for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
    {
        AStaticMeshActor* SMA = *It;
        if (!IsValid(SMA) || Ours.Contains(SMA)) { continue; }
        TInlineComponentArray<UStaticMeshComponent*> Comps(SMA);
        for (UStaticMeshComponent* C : Comps)
        {
            if (!IsUsableMesh(C) || Claimed.Contains(C)) { continue; }
            const FString MeshName = C->GetStaticMesh()->GetName();
            if (!IsWellPieceMeshName(MeshName)) { continue; }
            const FVector Loc = C->GetComponentLocation();
            const FMemberRec* Best = nullptr;
            float BestSq = TNumericLimits<float>::Max(), NextSq = TNumericLimits<float>::Max();
            for (const FMemberRec& M : Members)
            {
                const float D = FVector::DistSquared(M.Loc, Loc);
                if (D < BestSq) { NextSq = BestSq; BestSq = D; Best = &M; }
                else if (D < NextSq) { NextSq = D; }
            }
            const bool bOwned = Best != nullptr && BestSq < FMath::Square(WellMeshOwnerRadiusCm);
            if (bOwned && AddPiece(Best->Path, C))
            {
                ++BySpatial;
                if (bDiag)
                {
                    UE_LOG(LogNodeShuffle, Verbose,
                        TEXT("WELLH2B-INDEX spatial: '%s' on actor '%s' at %s -> member '%s' at %.0f cm ")
                        TEXT("(runner-up %.0f cm, radius %.0f cm). Nearest-wins; a runner-up close to the ")
                        TEXT("winner means the H0 spacing measurement no longer holds."),
                        *MeshName, *SMA->GetName(), *Loc.ToCompactString(), *WellShort(Best->Path),
                        FMath::Sqrt(BestSq), NextSq < TNumericLimits<float>::Max() ? FMath::Sqrt(NextSq) : -1.0f,
                        WellMeshOwnerRadiusCm);
                }
            }
            else if (bDiag && Best != nullptr)
            {
                UE_LOG(LogNodeShuffle, Verbose,
                    TEXT("WELLH2B-INDEX spatial REJECT: '%s' on '%s' at %s -- nearest well member '%s' is ")
                    TEXT("%.0f cm away, outside the %.0f cm radius. If well pieces are being missed, THIS ")
                    TEXT("line is where to see it."),
                    *MeshName, *SMA->GetName(), *Loc.ToCompactString(), *WellShort(Best->Path),
                    FMath::Sqrt(BestSq), WellMeshOwnerRadiusCm);
            }
        }
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2B-INDEX pass %d: %d vanilla well member(s) indexed, %d mesh piece(s) paired ")
        TEXT("(%d own, %d via engine link, %d spatial), %d member(s) with at least one piece. The link ")
        TEXT("route is the one design 2.4 assumed; a low 'via engine link' count next to a high 'spatial' ")
        TEXT("count IS the suppression bug measured on the previous build (5 of 6 groups hid 0 mesh actors)."),
        WellAuditPasses, WellMeshIndexMembers, WellMeshIndexPieces, ByOwn, ByLink, BySpatial,
        WellMeshIndex.Num());
}

// ------------------------------------------------------------------------------------------------
// CAPTURE -- FROM THE LIVE VANILLA ACTORS, BEFORE ANYTHING IS HIDDEN
// ------------------------------------------------------------------------------------------------
bool ANodeShuffleSubsystem::CaptureWellGroupVisuals(FNodeShuffleWellEntry& E)
{
    if (E.bGroupVisualsComplete) { return true; }
    EnsureWellMeshIndex();

    int32 NewPieces = 0, MembersDone = 0, MembersMissing = 0, CrackPieces = 0;

    const auto CaptureMember = [&](const FString& Path, TArray<FNodeShuffleWellVisual>& Out,
                                   bool& bFlag, const TCHAR* Kind) -> void
    {
        if (bFlag) { ++MembersDone; return; }
        AFGResourceNodeBase* Node = FindOriginalBaseByPath(Path);
        const TArray<TWeakObjectPtr<UStaticMeshComponent>>* Pieces = WellMeshIndex.Find(Path);
        if (!IsValid(Node) || !Pieces || Pieces->Num() == 0)
        {
            ++MembersMissing;
            if (FNodeShuffleModule::AreDiagnosticsEnabled()
                && !WellVisualCaptureLogged.Contains(Path))
            {
                WellVisualCaptureLogged.Add(Path);
                UE_LOG(LogNodeShuffle, Verbose,
                    TEXT("WELLH2B-CAPTURE %s '%s': NOTHING CAPTURED -- node %s, indexed pieces %d. ")
                    TEXT("A zero here means the member is not streamed OR no mesh piece was paired to ")
                    TEXT("it by any of the three routes; the WELLH2B-INDEX lines above say which. Said once."),
                    Kind, *WellShort(Path), IsValid(Node) ? TEXT("IS live") : TEXT("is NOT streamed"),
                    Pieces ? Pieces->Num() : 0);
            }
            return;
        }
        const FTransform NodeXf = Node->GetActorTransform();
        for (const TWeakObjectPtr<UStaticMeshComponent>& Weak : *Pieces)
        {
            UStaticMeshComponent* C = Weak.Get();
            if (!IsValid(C) || !C->GetStaticMesh()) { continue; }
            FNodeShuffleWellVisual V;
            V.MeshPath = C->GetStaticMesh()->GetPathName();
            const FTransform Rel = C->GetComponentTransform().GetRelativeTransform(NodeXf);
            V.RelLocation = Rel.GetLocation();
            V.RelRotation = Rel.GetRotation().Rotator();
            V.RelScale = Rel.GetScale3D();
            const int32 Slots = FMath::Max(1, C->GetNumMaterials());
            for (int32 i = 0; i < Slots; ++i) { V.MaterialPaths.Add(SaveableMaterialPath(C->GetMaterial(i))); }
            if (const AFGNodeMeshActor* MA = Cast<AFGNodeMeshActor>(C->GetOwner()))
            {
                V.MeshType = static_cast<uint8>(MA->mNodeMeshType);
                if (MA->mNodeMeshType == ENodeMeshType::MT_Crack
                    || MA->mNodeMeshType == ENodeMeshType::MT_DesertCrack) { ++CrackPieces; }
            }
            V.RouteTag = C->GetOwner() == Node ? TEXT("own")
                       : (Cast<AFGNodeMeshActor>(C->GetOwner()) ? TEXT("link-or-spatial") : TEXT("spatial"));
            Out.Add(V);
            ++NewPieces;
        }
        if (Out.Num() > 0)
        {
            bFlag = true;
            ++MembersDone;
            // Opportunistic LAST-RESORT template for groups whose own origin never streams. Filled from
            // whatever we did capture, and used only when a group has nothing of its own -- see the
            // header declaration for why a template can be the wrong biome's rock.
            TArray<FNodeShuffleWellVisual>& Template =
                (Kind[0] == TEXT('c')) ? WellVisualTemplateCore : WellVisualTemplateSatellite;
            if (Template.Num() == 0) { Template = Out; }
        }
        else { ++MembersMissing; }
    };

    CaptureMember(E.CorePath, E.CoreVisuals, E.bCoreVisualsCaptured, TEXT("core"));
    for (FNodeShuffleWellSatellite& S : E.Satellites)
    {
        if (!S.bCaptured) { continue; } // never spawned, so never dressed -- see the struct comment
        CaptureMember(S.SatellitePath, S.Visuals, S.bVisualsCaptured, TEXT("satellite"));
    }

    const bool bComplete = E.bCoreVisualsCaptured && MembersMissing == 0;
    if (bComplete && !E.bGroupVisualsComplete) { E.bGroupVisualsComplete = true; }

    if (NewPieces > 0 || (MembersMissing > 0 && !WellVisualCaptureLogged.Contains(E.CorePath + TEXT("|grp"))))
    {
        WellVisualCaptureLogged.Add(E.CorePath + TEXT("|grp"));
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2B-CAPTURE core='%s': +%d piece(s) this pass (%d of them MT_Crack), %d member(s) ")
            TEXT("captured, %d still empty -> group %s. An empty member is retried every pass while its ")
            TEXT("original streams; a group that never completes is dressed from the session template ")
            TEXT("and says so."),
            *WellShort(E.CorePath), NewPieces, CrackPieces, MembersDone, MembersMissing,
            bComplete ? TEXT("COMPLETE") : TEXT("INCOMPLETE"));
    }
    return bComplete;
}

// ------------------------------------------------------------------------------------------------
// ORIGIN-SIDE HIDE -- every piece, by every route
// ------------------------------------------------------------------------------------------------
int32 ANodeShuffleSubsystem::HideWellMemberMeshes(AFGResourceNodeBase* Node, int32& OutAlreadyHidden)
{
    if (!IsValid(Node)) { return 0; }
    const FString Path = WellPathOf(Node);
    const TArray<TWeakObjectPtr<UStaticMeshComponent>>* Pieces = WellMeshIndex.Find(Path);
    if (!Pieces) { return 0; }
    int32 HiddenNow = 0;
    for (const TWeakObjectPtr<UStaticMeshComponent>& Weak : *Pieces)
    {
        UStaticMeshComponent* C = Weak.Get();
        if (!IsValid(C)) { continue; }
        if (!C->IsVisible()) { ++OutAlreadyHidden; continue; }
        // Component-level, exactly as the ordinary-node orphan sweep does it -- a piece can be one of
        // several on a shared actor, so hiding the whole actor is not always right. Collision goes with
        // it: a hidden rock that still blocks the build gun is a ghost you can build on.
        C->SetVisibility(false, true);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        ++HiddenNow;
    }
    return HiddenNow;
}

