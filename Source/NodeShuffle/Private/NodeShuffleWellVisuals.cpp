// Packet H2b (ns-wells-h2b, branch wip/h2-relocation): THE ORIGIN SIDE -- capture a vanilla resource
// well's look from the live actors, then hide it (mesh AND collision) at the abandoned origin.
//
// THE MEASURED FINDING THIS WHOLE PACKET EXISTS FOR. A relocated well spawns correctly -- right resource,
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
// Pressurizer still will not snap, this packet has failed.
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
// WHY THE CAPTURE IS SaveGame. Relocation is spawn-on-discovery at the DESTINATION. The origin can be
// kilometres away and never stream again this session, so a capture held only in RAM would leave
// exactly the wells a tester flies to invisible and unbuildable. Asset paths + a relative transform
// survive the round trip; UObject pointers do not.
//
// WHERE THE REST OF IT LIVES (ns-t7-split, 2026-08-08 -- four files, seams named by two cold reviews):
//   NodeShuffleWellMeshIndex.cpp    THE INDEX. Which mesh pieces ARE a given vanilla well member: the
//                                   three pairing routes, the bystander contest, the mNodeMeshType
//                                   gate. Read its header for the pairing bug this index exists for.
//   NodeShuffleWellVisuals.cpp      THIS FILE. Capture from the live vanilla actors; hide them.
//   NodeShuffleWellVisualsApply.cpp THE DESTINATION SIDE. Put the pieces back on the actors we spawned.
//   NodeShuffleWellSnapBox.cpp      The collision recipe and the "Resource" collider an off-centre
//                                   build-gun hit resolves against. THAT is the acceptance criterion.
// They share one index and the ANodeShuffleSubsystem members declared in NodeShuffleSubsystem.h, and
// nothing else.
//
// IMPORT DISCIPLINE, RESIDUAL HALF (memory:sf-shipping-export-trap). The packet's discipline paragraph
// travelled to NodeShuffleWellMeshIndex.cpp with the code that reaches the new engine surface
// (AStaticMeshActor::StaticClass via route 3's iterator). What is reached FROM HERE, and is all that
// this file predicts: UStaticMeshComponent's visibility/collision setters and material/mesh getters
// (Engine), UMaterialInterface::GetPathName / UMaterialInstanceDynamic::IsA (Engine), and on the
// FactoryGame side only the AFGNodeMeshActor cast plus a read of mNodeMeshType -- the same surface the
// index reaches, so nothing here is new even if the two files are compiled into different unity blobs.
// That is the PREDICTION; the import table is MEASURED after the build and every new symbol named.
// Three predictions have been falsified on this machine.

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
        // ns-t23-rollhide REVIEW FIX (cold review F5) -- THE GATE IS PER COMPONENT, NOT PER MEMBER.
        // It used to be the member's first touch, which fires once ever -- so a piece that entered the
        // index later (measured: route 3 going 7 -> 8) or a piece RE-CREATED by a streaming round trip at
        // the origin was hidden with no record and could only be restored to the documented default. Two
        // guards make per-pass recording safe: a component already recorded is skipped, and a component
        // that is already invisible AND de-collided carries nothing worth recording (it is
        // indistinguishable from our own hide, and the restore default is strictly better than copying it).
        // Packed: low nibble = the ECollisionEnabled value, bit 7 = was visible. One map, not two.
        // SESSION-TRANSIENT AND UNAVOIDABLY SO: UStaticMeshComponent identity is not path-stable
        // (H2b-review F-3), so across a reload the restore uses the default and COUNTS what it guessed.
        const bool bLooksAlreadySuppressed =
            !bWasVisible && C->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
        if (!bLooksAlreadySuppressed && !WellMeshPriorCollision.Contains(C))
        {
            WellMeshPriorCollision.Add(C, static_cast<uint8>(
                (static_cast<uint8>(C->GetCollisionEnabled()) & 0x0F) | (bWasVisible ? 0x80 : 0x00)));
        }
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
    if (HiddenNow > 0) { WellMeshHiddenByUs.FindOrAdd(Path) += HiddenNow; }
    return HiddenNow;
}

// ------------------------------------------------------------------------------------------------
// ns-t36-probefix ITEM 2 -- MEASUREMENT ONLY. NOTHING HERE HIDES ANYTHING.
// ------------------------------------------------------------------------------------------------
// THE REPORT: a relocated well whose rocks vanished on a manual re-roll kept playing its water-spout
// effect at the origin. THE STATE OF THE SEARCH, stated plainly because the packet that found it was
// told not to guess: the suppression path hides the member ACTOR (SetActorHiddenInGame), hides the
// UStaticMeshComponent pieces the index paired to it, deregisters it from the scanner and removes its
// RADIOACTIVITY emitters -- and the mod's source contains no reference to any particle, Niagara or
// FX-system component anywhere. The index (NodeShuffleWellMeshIndex.cpp) is typed
// TWeakObjectPtr<UStaticMeshComponent>, so a non-mesh component sitting on a paired mesh ACTOR is
// outside every hide this mod performs. That is a POPULATION FACT ABOUT OUR CODE, checkable by
// reading it. WHICH COMPONENT ACTUALLY DRAWS THE SPOUT IS NOT ESTABLISHED, and a hide aimed at a
// guessed class or a guessed name would be worse than the spout, so this packet ships the measurement
// and no behaviour change. This line is what a later packet needs in order to stop guessing: the real
// component inventory of a member and of every actor its indexed pieces live on, taken at the moment
// the hide runs. It states no cause and names no culprit; every field is a count or a class name read
// off the live objects.
void ANodeShuffleSubsystem::LogWellMemberComponentCensus(AFGResourceNodeBase* Node)
{
    if (!IsValid(Node) || !FNodeShuffleModule::AreDiagnosticsEnabled()) { return; }
    const FString Path = WellPathOf(Node);
    constexpr int32 WellComponentCensusNameCap = 24;

    // Counts the components of one actor and appends the class names of the SCENE components that are
    // still visible. Static-mesh components are named too, and deliberately: the reader has to be able
    // to tell "the mesh hide missed a piece" from "there is a non-mesh component here".
    int32 TotalComps = 0, SceneComps = 0, VisibleScene = 0, VisibleNonMesh = 0;
    FString VisibleNames;
    const auto Tally = [&](const AActor* A) -> void
    {
        if (!IsValid(A)) { return; }
        for (const UActorComponent* C : A->GetComponents())
        {
            if (!IsValid(C)) { continue; }
            ++TotalComps;
            const USceneComponent* S = Cast<USceneComponent>(C);
            if (!S) { continue; }
            ++SceneComps;
            if (!S->IsVisible()) { continue; }
            ++VisibleScene;
            const bool bMesh = (Cast<UStaticMeshComponent>(S) != nullptr);
            if (!bMesh) { ++VisibleNonMesh; }
            // Capped so one pathological actor cannot produce an unbounded log line. The cap is printed
            // on the line FROM THIS CONSTANT, never typed into the prose.
            if (VisibleScene <= WellComponentCensusNameCap)
            {
                VisibleNames += FString::Printf(TEXT("[%s on %s] "), *S->GetClass()->GetName(),
                                                *A->GetName());
            }
        }
    };

    Tally(Node);
    const int32 MemberComps = TotalComps;

    // The distinct owner actors of this member's indexed pieces. A vanilla member is paired with a
    // separate AFGNodeMeshActor, so this is where a non-mesh component would sit unseen by every hide
    // this mod performs.
    int32 IndexedPieces = 0, DistinctOwners = 0;
    if (const TArray<TWeakObjectPtr<UStaticMeshComponent>>* Pieces = WellMeshIndex.Find(Path))
    {
        TSet<const AActor*> Owners;
        for (const TWeakObjectPtr<UStaticMeshComponent>& Weak : *Pieces)
        {
            const UStaticMeshComponent* C = Weak.Get();
            if (!IsValid(C)) { continue; }
            ++IndexedPieces;
            // Named PieceOwner, not Owner: AActor has an `Owner` member and this file is compiled with
            // warnings as errors, so the shadowing name fails the build.
            const AActor* PieceOwner = C->GetOwner();
            if (PieceOwner && PieceOwner != Node && !Owners.Contains(PieceOwner))
            {
                Owners.Add(PieceOwner);
                Tally(PieceOwner);
            }
        }
        DistinctOwners = Owners.Num();
    }

    // Throttled on the counts, not on the path: keyed on the path alone this would print once per
    // member per session and a piece streaming in later would never be reported. WellSuppressLogged is
    // the existing session-scoped set for exactly this idiom on this path; the key is prefixed so it
    // cannot collide with the suppression line's own keys.
    const FString CensusKey = FString::Printf(TEXT("T36FX|%s|%d|%d|%d|%d|%d|%d"), *Path, MemberComps,
                                              TotalComps, SceneComps, VisibleScene, VisibleNonMesh,
                                              IndexedPieces);
    if (WellSuppressLogged.Contains(CensusKey)) { return; }
    WellSuppressLogged.Add(CensusKey);

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-COMPONENTCENSUS member='%s': taken just after this member was suppressed, over the ")
        TEXT("member actor itself plus the %d distinct other actor(s) that own its %d live indexed mesh ")
        TEXT("piece(s). The member actor carries %d component(s) of its own; across all of those actors ")
        TEXT("there are %d component(s), of which %d are scene components, of which %d are still ")
        TEXT("visible right now, of which %d are NOT static-mesh components. The class name and owner ")
        TEXT("of each still-visible scene component, up to the first %d of the %d: %s. WHY THIS LINE ")
        TEXT("EXISTS: a relocated well was reported still playing a water-spout effect at its abandoned ")
        TEXT("origin, and this mod's suppression covers the member actor, the indexed static-mesh ")
        TEXT("pieces, the scanner registration and the radioactivity emitters -- the index is typed to ")
        TEXT("static-mesh components, so anything else on any of these actors is outside all of it. ")
        TEXT("THIS LINE DOES NOT SAY WHICH COMPONENT DRAWS THE SPOUT, and nothing in this build hides ")
        TEXT("any component named above that the mesh hide did not already cover. A count of zero ")
        TEXT("still-visible non-mesh scene components means the spout is not on any actor this census ")
        TEXT("reached, which is a different statement from it not existing."),
        *WellShort(Path),
        DistinctOwners, IndexedPieces, MemberComps, TotalComps, SceneComps, VisibleScene, VisibleNonMesh,
        WellComponentCensusNameCap, VisibleScene,
        VisibleNames.IsEmpty() ? TEXT("<no still-visible scene component on any of them>")
                               : *VisibleNames);
}
