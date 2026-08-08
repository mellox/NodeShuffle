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

// 12 m. The SAME numeric constant the ordinary-node orphan sweep uses for "a real node owns this rock"
// (OrphanOwnerRadius).
//
// ns-review-h2b F-1 CORRECTION -- THE ORIGINAL RATIONALE HERE WAS FALSE AND IT COST A BLOCKER.
// This comment used to claim the shared radius meant the two passes "answer the same question about
// the same world" and so could not disagree about who owns a rock. Equal radii were never sufficient,
// because the two passes had DIFFERENT OWNER SETS:
//   * OrphanRockCleanup's owner set  = every AFGResourceNode (minus deposits) PLUS every fracking
//     actor (NodeShuffleSubsystem.cpp, "FIX 3" and "FIX B").
//   * this index's contest set (before this fix) = fracking well members ONLY.
// So an ordinary coal node's rock 1100 cm from a fracking satellite was KEPT by the orphan sweep as
// "owned by a live node" and simultaneously CLAIMED, captured and hidden-with-collision-off by this
// index -- an invisible, un-minable coal node in the player's live save. Route 3 below now contests
// against ALL resource nodes (A2) and only accepts Frack* meshes (A3), which is what actually makes
// the "one question, one answer" claim true.
//
// The two passes still measure distance differently and this is DELIBERATE, not an oversight:
// OrphanRockCleanup uses DistSquared2D (a rock and its node share a map spot but not an altitude, and
// a 2-D test is the forgiving direction for "do not hide a live node's rock"), while this index uses
// full 3-D DistSquared (the strict direction for "may I claim and hide this"). Where they differ, the
// orphan sweep protects MORE rocks and this index claims FEWER -- both erring toward leaving a
// bystander's rock alone, which is the only direction that is safe in a save the player keeps.
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

    // ns-review-h2b F-1 (A2): THE CONTEST SET IS EVERY RESOURCE NODE, NOT ONLY WELL MEMBERS.
    // TActorIterator<AFGResourceNodeBase> visits ordinary nodes, deposits AND fracking cores/satellites
    // (that is precisely the type-coverage fact this whole packet turns on). One sweep therefore fills
    // both lists at no extra cost: Members drives routes 1-3, Bystanders exists only so route 3's
    // nearest-wins contest can LOSE. Without it, a coal node's rock 1100 cm from a fracking satellite
    // was claimed, captured, hidden and de-collided -- an invisible, un-minable node in a live save.
    struct FMemberRec { FVector Loc; AFGResourceNodeBase* Node; FString Path; };
    TArray<FMemberRec> Members;
    TArray<FVector> Bystanders;   // every NON-well resource node's location; contest losers, never owners
    for (TActorIterator<AFGResourceNodeBase> It(World); It; ++It)
    {
        AFGResourceNodeBase* N = *It;
        if (!IsValid(N) || Ours.Contains(N)) { continue; }
        if (IsFrackingActor(N)) { Members.Add({ N->GetActorLocation(), N, WellPathOf(N) }); }
        else { Bystanders.Add(N->GetActorLocation()); }
    }
    WellMeshIndexMembers = Members.Num();

    TSet<UStaticMeshComponent*> Claimed;
    int32 ByOwn = 0, ByLink = 0, BySpatial = 0, RejectedBystander = 0;

    // The fracking family by name (SM_FrackingNode_Crack_01 / _Mid_01 / _Small_01 and friends). Name
    // matching decides ONLY "could this be a well piece at all" -- WHICH member it belongs to is decided
    // by DISTANCE, never by name (memory:nodeshuffle-descriptor-visuals: pairing by a mesh-name/resource
    // key decays to nothing once the shuffle has retyped the world).
    //
    // ns-review-h2b F-1 (A3): THIS USED TO ALSO ACCEPT IsNodeRockMeshName(), WHICH MATCHES EVERY
    // ORDINARY NODE ROCK IN THE GAME (ResourceNode* / CoalResource* / SulfurResource* / Resource_* /
    // SAM_* / SM_*Node*). Route 3 therefore offered a bystander's rock to a well-members-only contest it
    // could not lose. Narrowed to "Frack" alone. The cost of being wrong now runs the SAFE way: a
    // fracking piece whose mesh is named without "Frack" is simply not captured, so the destination well
    // is under-dressed but still buildable (routes 1 and 2 are unaffected and do not consult this at
    // all), whereas the old failure mode stranded an unrelated node in the player's save. If a well
    // origin is ever seen with rock left standing, the WELLH2B-INDEX spatial REJECT line names the mesh.
    const auto IsWellPieceMeshName = [](const FString& MeshName) -> bool
    {
        return MeshName.Contains(TEXT("Frack"));
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
            // ns-review-h2b F-1 (A2): THE BYSTANDER CONTEST. Nearest well member is not enough -- ask
            // whether ANY ordinary resource node is nearer. No radius on this half deliberately: the
            // question is not "is a node close" but "is the well member the closest thing that could own
            // this rock". A tie loses, because leaving a well origin slightly under-suppressed is
            // recoverable and stranding a bystander's node in the save is not.
            float ByStSq = TNumericLimits<float>::Max();
            for (const FVector& B : Bystanders)
            {
                const float D = FVector::DistSquared(B, Loc);
                if (D < ByStSq) { ByStSq = D; }
            }
            const bool bInRadius = Best != nullptr && BestSq < FMath::Square(WellMeshOwnerRadiusCm);
            const bool bBystanderWins = bInRadius && !(BestSq < ByStSq);
            if (bBystanderWins)
            {
                ++RejectedBystander;
                // Said once per (actor, mesh) for the session. Display, not Verbose, and gated only on
                // the diagnostics flag: this line is the PROOF that F-1's guard fired, and F-4 showed a
                // Verbose line is invisible at the runtime default verbosity, so the one line that says
                // "a bystander was about to be hidden" must not need a console command to exist.
                const FString Key = FString::Printf(TEXT("bystander|%s|%s"), *SMA->GetPathName(), *MeshName);
                if (bDiag && !WellVisualCaptureLogged.Contains(Key))
                {
                    WellVisualCaptureLogged.Add(Key);
                    UE_LOG(LogNodeShuffle, Display,
                        TEXT("WELLH2B-INDEX spatial BYSTANDER: '%s' on '%s' at %s is %.0f cm from well ")
                        TEXT("member '%s' (inside the %.0f cm radius) but only %.0f cm from an ordinary ")
                        TEXT("resource node -- NOT claimed, NOT captured, NOT hidden. Before ns-review-h2b ")
                        TEXT("F-1 this rock would have been hidden with its collision disabled, which is an ")
                        TEXT("invisible un-minable node in a saved world. Said once per mesh."),
                        *MeshName, *SMA->GetName(), *Loc.ToCompactString(), FMath::Sqrt(BestSq),
                        *WellShort(Best->Path), WellMeshOwnerRadiusCm,
                        ByStSq < TNumericLimits<float>::Max() ? FMath::Sqrt(ByStSq) : -1.0f);
                }
                continue;
            }
            const bool bOwned = bInRadius;
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
        TEXT("(%d own, %d via engine link, %d spatial), %d bystander reject(s), %d non-well node(s) in the ")
        TEXT("contest, %d member(s) with at least one piece. The link route is the one design 2.4 assumed; ")
        TEXT("a low 'via engine link' count next to a high 'spatial' count IS the suppression bug measured ")
        TEXT("on the previous build (5 of 6 groups hid 0 mesh actors). 'bystander reject' is ns-review-h2b ")
        TEXT("F-1's guard firing: a Frack-named mesh that was closer to an ORDINARY resource node than to ")
        TEXT("any well member, left alone. A non-zero count is correct behaviour, not an error."),
        WellAuditPasses, WellMeshIndexMembers, WellMeshIndexPieces, ByOwn, ByLink, BySpatial,
        RejectedBystander, Bystanders.Num(), WellMeshIndex.Num());
}

// ------------------------------------------------------------------------------------------------
// CAPTURE -- FROM THE LIVE VANILLA ACTORS, BEFORE ANYTHING IS HIDDEN
// ------------------------------------------------------------------------------------------------
bool ANodeShuffleSubsystem::CaptureWellGroupVisuals(FNodeShuffleWellEntry& E)
{
    // ns-review-h2b F-7: WHAT THE SAVE ACTUALLY HANDED BACK, SAID BEFORE ANY CAPTURE CAN REFILL IT.
    // Test step 5 asks whether the SaveGame round trip preserved the captured visuals -- but the apply
    // path falls back to the session template and the capture re-runs whenever bGroupVisualsComplete is
    // false, so a well can be dressed correctly on a reload while the persisted fields are empty, and
    // the test would pass with the thing it tests falsified. This line is emitted the FIRST time this
    // session that any code looks at this group's visuals, which on a reload is before a single piece
    // can have been re-captured. Non-zero piece counts here ARE the round trip; zeros with a dressed
    // well afterwards mean the well was rebuilt from the world or the template, not from the save.
    const FString AdoptKey = E.CorePath + TEXT("|adopt");
    if (!WellVisualCaptureLogged.Contains(AdoptKey))
    {
        WellVisualCaptureLogged.Add(AdoptKey);
        int32 SatPieces = 0, SatCaptured = 0;
        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            SatPieces += S.Visuals.Num();
            if (S.bVisualsCaptured) { ++SatCaptured; }
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2B-ADOPT core='%s': as loaded, groupComplete=%d coreCaptured=%d corePieces=%d, ")
            TEXT("%d/%d satellite(s) captured with %d piece(s) total. This is the state BEFORE any capture ")
            TEXT("runs this session -- on a fresh roll it is all zeros, on a reload of an already-dressed ")
            TEXT("well it must NOT be. Said once per group per session."),
            *WellShort(E.CorePath), E.bGroupVisualsComplete ? 1 : 0, E.bCoreVisualsCaptured ? 1 : 0,
            E.CoreVisuals.Num(), SatCaptured, E.Satellites.Num(), SatPieces);
    }

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
    int32 HiddenNow = 0, DecolliedAlreadyHidden = 0;
    for (const TWeakObjectPtr<UStaticMeshComponent>& Weak : *Pieces)
    {
        UStaticMeshComponent* C = Weak.Get();
        if (!IsValid(C)) { continue; }
        // ns-review-h2b F-5: COLLISION FIRST, AND UNCONDITIONALLY.
        // This used to `continue` on an already-invisible piece BEFORE touching collision. The origin
        // site is by construction far from the player (the player is at the destination) and
        // AFGResourceNodeBase implements IFGSignificanceInterface, so "already invisible" is the COMMON
        // case here, not the rare one -- every such piece was counted as done while keeping the
        // build-gun response that makes it an invisible snappable ghost. That is strictly worse than the
        // bug this packet fixes, and it is exactly what the comment below claims to prevent.
        const bool bWasVisible = C->IsVisible();
        if (C->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
        {
            C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            if (!bWasVisible) { ++DecolliedAlreadyHidden; }
        }
        if (!bWasVisible) { ++OutAlreadyHidden; continue; }
        // Component-level, exactly as the ordinary-node orphan sweep does it -- a piece can be one of
        // several on a shared actor, so hiding the whole actor is not always right. Collision goes with
        // it: a hidden rock that still blocks the build gun is a ghost you can build on.
        C->SetVisibility(false, true);
        ++HiddenNow;
    }
    if (DecolliedAlreadyHidden > 0 && FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-SUPPRESS member='%s': %d already-invisible piece(s) still had collision and have ")
            TEXT("just been de-collided (ns-review-h2b F-5). Before this fix each of these was an INVISIBLE ")
            TEXT("SNAPPABLE GHOST at the abandoned origin, reported as 'already hidden' and never revisited."),
            *WellShort(Path), DecolliedAlreadyHidden);
    }
    return HiddenNow;
}

