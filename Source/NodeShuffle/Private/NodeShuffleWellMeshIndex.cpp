// Packet H2b (ns-wells-h2b, branch wip/h2-relocation) -- THE INDEX: which static-mesh pieces ARE a
// given vanilla well member. Split out of NodeShuffleWellVisuals.cpp by ns-t7-split (2026-08-08) at the
// seam two independent cold reviews named; nothing in this file changed meaning in the move.
//
// READ NodeShuffleWellVisuals.cpp's HEADER FIRST for the packet-level measured finding this all exists
// for (a relocated well that spawns correctly and cannot be built on, because the build gun's trace
// passes straight through it and lands on terrain). This file carries only the two halves of that
// header that describe THE INDEX -- the pairing bug and the three routes -- because they describe the
// code that moved, and a rationale left behind in the file its code left is a rationale nobody reads.
//
// THE PAIRING BUG, WHICH IS THE SAME BUG AT BOTH ENDS. WELLH2-SUPPRESS hides the vanilla group but
// found the members' MESH actors for only one group in six -- measured 2026-08-07:
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
// The same index therefore serves BOTH ends: capture-at-destination (CaptureWellGroupVisuals,
// NodeShuffleWellVisuals.cpp) and hide-at-origin (HideWellMemberMeshes, same file) are one problem, and
// fixing them separately would have left two mechanisms to disagree.
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
// IMPORT DISCIPLINE (memory:sf-shipping-export-trap). This file is where the H2b packet's NEW engine
// surface actually lives, which is why the discipline paragraph travelled here with it: it reaches
// AStaticMeshActor::StaticClass (Engine, not FactoryGame) via route 3's TActorIterator, and on the
// FactoryGame side only field reads (mMeshActor, mNodeActor, mNodeMeshType) plus the AFGNodeMeshActor
// StaticClass thunk that FindMeshActorForNode already uses. That is the PREDICTION. The import table is
// MEASURED after the build and every new symbol named; three predictions have been falsified on this
// machine. NodeShuffleWellVisuals.cpp keeps its own residual note for the surface that stayed there
// (UStaticMeshComponent's visibility/collision setters, and the AFGNodeMeshActor cast in the capture).
// ns-t7-split adds NO new symbol of its own: every symbol in this file was already reached from
// NodeShuffleWellVisuals.cpp before the move.

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
    // ns-t7-split: drop a previous world's T3 snapshot before this pass overwrites part of it. Cheap
    // and idempotent; ApplyWellGroupVisuals calls the same function, so whichever writer runs first in
    // a new world does the reset. See the declaration in NodeShuffleSubsystem.h.
    ResetWellSnapBoxDiagForWorld(World);
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
    TArray<FVector> UseBoxNodes;  // T3 (§6.3): the NARROWER subset that can carry a 650 cm use box
    int32 HiddenOriginalUseBoxNodes = 0; // T3 correction (ns-t7-split): the population removed, COUNTED
    for (TActorIterator<AFGResourceNodeBase> It(World); It; ++It)
    {
        AFGResourceNodeBase* N = *It;
        if (!IsValid(N) || Ours.Contains(N)) { continue; }
        if (IsFrackingActor(N)) { Members.Add({ N->GetActorLocation(), N, WellPathOf(N) }); }
        else
        {
            Bystanders.Add(N->GetActorLocation());
            // AFGResourceNode is the type EnsureNodeUseBox takes; deposits are AFGResourceNodeBase
            // but never AFGResourceNode, so this Cast IS the "carries a 650 cm box" filter.
            if (Cast<AFGResourceNode>(N))
            {
                // ---- T3 CORRECTION, ns-t7-split (docs/TECH-DEBT.md T3, the CORRECTION block) ----
                // THE ONLY INTENDED BEHAVIOUR CHANGE IN THE SPLIT PACKET, and it is a POPULATION fix,
                // not an arithmetic one. This filter had no IsHidden() test, so every original this mod
                // had already hidden -- 658 of them at load in the very session that produced T3's 8
                // "provable overlaps" -- stayed in the snapshot. A hidden original cannot be built on
                // regardless of any box, so an overlap against one is harmless and should never have
                // been counted; the instrument was a FALSE-POSITIVE GENERATOR and 2 of the 5 measured
                // members resolved to confirmed hidden originals (Resource_Stone_01, SM_LithiumNode),
                // 0 to an active node. IsHidden() is the same predicate SuppressOriginalNodes itself
                // uses to decide a node still needs hiding (NodeShuffleSubsystem.cpp), so the two
                // cannot disagree about what "hidden" means.
                //
                // BOTH COUNTS ARE PRINTED, DELIBERATELY. Silently shrinking a denominator is this
                // project's most-repeated defect: a reader who greps a smaller `nodes=` after this
                // build must be able to see WHY it shrank on the same line, not conclude the world
                // changed. activeMineable= and hiddenOriginal= sum to the old population exactly.
                if (N->IsHidden()) { ++HiddenOriginalUseBoxNodes; }
                else { UseBoxNodes.Add(N->GetActorLocation()); }
            }
        }
    }
    WellMeshIndexMembers = Members.Num();
    // T3: hand the ACTIVE mineable-node set (and the pass it was taken on) to EnsureWellMemberSnapBox.
    // One array copy per index rebuild -- once per apply pass, not per member -- and the copy is what
    // lets the snap-box measurement run with NO second TActorIterator on a per-member path. The pass
    // number travels with it so a stale or never-built snapshot is visible in the log instead of being
    // read as "no ordinary node nearby". The hidden-original count travels too, so the line can print
    // the population it EXCLUDED next to the one it used.
    //
    // ns-t7-split: `BystanderLocations()` used to be published here as well and is GONE. It was written
    // on this line and read NOWHERE (the T1/T2 review recorded it as "dead published state ... a later
    // packet may delete it"): route 3's contest uses the function-local `Bystanders` below, and T3 reads
    // UseBoxNodes. Removing it drops one ~36 KB array copy per apply pass and can change no observable
    // behaviour, because nothing observed it.
    SnapBoxUseBoxNodes = UseBoxNodes;
    SnapBoxHiddenOriginalNodes = HiddenOriginalUseBoxNodes;
    SnapBoxSnapshotPass = WellAuditPasses;

    TSet<UStaticMeshComponent*> Claimed;
    int32 ByOwn = 0, ByLink = 0, BySpatial = 0, RejectedBystander = 0, NarrowedByType = 0, WidenedByType = 0;
    // F-3 denominators: the candidate populations each of the two gate counters is measured against.
    int32 NameCandidates = 0, NameCandidatesInRadius = 0, TypeCandidates = 0, TypeCandidatesInRadius = 0;

    // T2 (ns-t2-meshtype): "COULD THIS BE A WELL PIECE AT ALL" IS NOW ASKED OF THE ENGINE'S OWN TYPE,
    // NOT OF THE MESH NAME. AFGNodeMeshActor derives from AStaticMeshActor (FGResourceNodeBase.h:61), so
    // route 3's TActorIterator<AStaticMeshActor> already visits every node mesh actor, and
    // ENodeMeshType mNodeMeshType (FGResourceNodeBase.h:71) states core / crack / satellite INCLUDING
    // the MT_Desert* variants. MT_Node is the ordinary-node value, so "owner is a node mesh actor AND
    // its type is not MT_Node" is the exact type-level statement of "this is well geometry".
    // WHICH member a piece belongs to is still decided by DISTANCE alone, never by name or by type
    // (memory:nodeshuffle-descriptor-visuals).
    //
    // THAT THE TYPE IS READABLE HERE IS MEASURED, NOT ASSUMED. TECH-DEBT T5 and H2b-fixes-handoff §4 both
    // state route 3's owner "is an AStaticMeshActor -> never counted", which conflates the ITERATOR'S
    // DECLARED TYPE with the RUNTIME CLASS. Falsified by arithmetic in
    // FactoryGame-backup-2026.08.08-15.53.04.log: group BP_FrackingCore15 captured 18 pieces of which 10
    // were MT_Crack while receiving 10 spatial and only 8 link pieces, and MT_Crack is counted only
    // inside Cast<AFGNodeMeshActor>(C->GetOwner()) (CaptureWellGroupVisuals, NodeShuffleWellVisuals.cpp).
    // 10 > 8, so spatial pieces DO cast. NOT proven: that EVERY spatial piece casts. Full working in
    // _team/nodeshuffle-followups/T2-meshtype-handoff.md.
    //
    // WHERE F-1's PROTECTION ACTUALLY LIVES (corrected by cold review 2026-08-08, F-1 -- the earlier
    // wording here was WRONG and said the cast does the work). THE FIRST TEST, THE CAST, REJECTS NOTHING
    // AMONG ORDINARY NODES: AFGNodeMeshActor is "a Static Mesh Actor that should be used for ALL node
    // meshes in the world" (FGResourceNodeBase.h:59), and this mod's own back-link sweep pairs those
    // actors to ORDINARY nodes (NodeShuffleSubsystem.cpp:7269). An ordinary node's rock therefore PASSES
    // the cast. 100% of the protection against it rests on the SECOND test, mNodeMeshType != MT_Node --
    // a UPROPERTY(EditInstanceOnly) per-instance value baked into cooked .uassets that no static read
    // here can see. That ordinary nodes carry MT_Node is therefore ASSUMED, NOT PROVEN, and it is
    // MEASURED rather than asserted: RebuildMeshActorCache (NodeShuffleSubsystem.cpp) emits
    // MESHTYPE-CENSUS at load, a histogram of mNodeMeshType split paired-to-fracking /
    // paired-to-ordinary / unpaired. A non-zero "pairedToOrdinaryNode ... non-MT_Node" there falsifies
    // this gate. F-1's only STRUCTURAL lock is A2, the bystander contest below.
    //
    // WHAT IT CANNOT SEE, REPORTED rather than assumed away: a well piece whose owner is a plain
    // AStaticMeshActor (cast fails) or whose mNodeMeshType was left at its MT_Node default. The old name
    // test took those when Frack-named. Every one inside the radius is printed by the WELLH2B-INDEX
    // spatial NARROWED line below -- and every piece this gate ADMITS that the name test would have
    // refused is printed by the WIDENED line. Those two lines plus their candidate denominators in the
    // pass summary are the whole measurement of what this change cost, in BOTH directions.
    // No new import: the Cast and mNodeMeshType are both already reached in this file.

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
            // Read the owner through the SAME expression CaptureWellGroupVisuals uses to classify the
            // piece, so the gate and the capture can never disagree about what owns a component.
            const AFGNodeMeshActor* MeshOwner = Cast<AFGNodeMeshActor>(C->GetOwner());
            const ENodeMeshType MeshType = MeshOwner ? MeshOwner->mNodeMeshType : ENodeMeshType::MT_Node;
            const bool bTypeSaysWellPiece = MeshOwner != nullptr && MeshType != ENodeMeshType::MT_Node;
            // The predicate T2 replaced. Evaluated ONLY to measure what the type gate drops -- it is
            // never an accept path, so route 3 admits nothing it did not admit before plus well-typed
            // pieces. Do not turn this into an OR without reading the F-1 note above.
            const bool bNameSaysWellPiece = MeshName.Contains(TEXT("Frack"));
            // F-3 DENOMINATORS. "0 narrowed-by-type" cannot distinguish "the gate dropped nothing" from
            // "nothing was ever a candidate to drop" -- TECH-DEBT T5, post-mortemed in this repo the same
            // day: a ratio whose numerator is structurally zero is not evidence. Counted over EVERY
            // component route 3 visits, so each gate count is read against a population.
            if (bNameSaysWellPiece) { ++NameCandidates; }
            if (bTypeSaysWellPiece) { ++TypeCandidates; }
            if (!bTypeSaysWellPiece && !bNameSaysWellPiece) { continue; }
            const FVector Loc = C->GetComponentLocation();
            const FMemberRec* Best = nullptr;
            float BestSq = TNumericLimits<float>::Max(), NextSq = TNumericLimits<float>::Max();
            for (const FMemberRec& M : Members)
            {
                const float D = FVector::DistSquared(M.Loc, Loc);
                if (D < BestSq) { NextSq = BestSq; BestSq = D; Best = &M; }
                else if (D < NextSq) { NextSq = D; }
            }
            const bool bInRadius = Best != nullptr && BestSq < FMath::Square(WellMeshOwnerRadiusCm);
            if (bInRadius)
            {
                if (bNameSaysWellPiece) { ++NameCandidatesInRadius; }
                if (bTypeSaysWellPiece) { ++TypeCandidatesInRadius; }
            }
            // F-2: THE DANGEROUS DIRECTION, COUNTED. NarrowedByType counts what the type gate LOST (safe
            // -- a lost piece is merely left undressed). This counts what it newly ADMITS: a piece the
            // old Contains("Frack") test refused and this build may claim, capture and hide. That is the
            // direction that produced ns-review-h2b F-1, and it had no counter at all. Superset of the
            // truly-gained set in the same conservative way NarrowedByType is (the bystander contest is
            // not applied), so widened == 0 proves nothing new was admitted.
            const bool bWidenedByType = bTypeSaysWellPiece && !bNameSaysWellPiece && bInRadius;
            if (bWidenedByType) { ++WidenedByType; }
            // T2: A NAME-ONLY CANDIDATE. The old Contains("Frack") predicate would have considered this
            // component; the mNodeMeshType gate does not. Reported, never claimed. Every field below is
            // something this pass MEASURED -- no reason for the mismatch is stated, because none was
            // tested ([[lessons-log-asserted-a-cause]]).
            if (!bTypeSaysWellPiece)
            {
                if (bInRadius)
                {
                    ++NarrowedByType;
                    const FString NKey = FString::Printf(TEXT("narrowed|%s|%s"), *SMA->GetPathName(), *MeshName);
                    if (bDiag && !WellVisualCaptureLogged.Contains(NKey))
                    {
                        WellVisualCaptureLogged.Add(NKey);
                        UE_LOG(LogNodeShuffle, Display,
                            TEXT("WELLH2B-INDEX spatial NARROWED: '%s' on '%s' at %s is %.0f cm from well ")
                            TEXT("member '%s' (inside the %.0f cm radius) and its name contains 'Frack', but ")
                            TEXT("meshActorOwner=%s nodeMeshType=%d -- NOT claimed by the mNodeMeshType gate. ")
                            TEXT("This line is the measured cost of T2's gate: it names every piece the old ")
                            TEXT("name test would have taken and this one does not. Said once per mesh."),
                            *MeshName, *SMA->GetName(), *Loc.ToCompactString(), FMath::Sqrt(BestSq),
                            *WellShort(Best->Path), WellMeshOwnerRadiusCm,
                            MeshOwner ? TEXT("AFGNodeMeshActor") : TEXT("NONE"), static_cast<int32>(MeshType));
                    }
                }
                continue;
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
            const bool bBystanderWins = bInRadius && !(BestSq < ByStSq);
            // Same shape and the same DISPLAY verbosity as NARROWED (F-4: a Verbose line is invisible at
            // the runtime default, and this is the direction that can strand a node in a live save).
            // Emitted whether or not A2 then vetoed the piece -- "newly admitted and vetoed" and "newly
            // admitted and claimed" are different facts. Measured fields only, no stated reason for the
            // name/type mismatch, because none was tested ([[lessons-log-asserted-a-cause]]).
            if (bWidenedByType)
            {
                const FString WKey = FString::Printf(TEXT("widened|%s|%s"), *SMA->GetPathName(), *MeshName);
                if (bDiag && !WellVisualCaptureLogged.Contains(WKey))
                {
                    WellVisualCaptureLogged.Add(WKey);
                    // THE FIELD THAT CLASSIFIES THIS LINE. A widened piece back-linked to a FRACKING
                    // node is T2 working; back-linked to nothing is the desert hypothesis; back-linked
                    // to an ORDINARY node is an ns-review-h2b F-1 recurrence. Without this the reader
                    // must go stand at the world location to tell them apart. mNodeActor is public
                    // (FGResourceNodeBase.h:70) -- no access transformer, no new import.
                    const AFGResourceNodeBase* WPaired = MeshOwner->mNodeActor.Get();
                    const FString WPairedName = WPaired ? WPaired->GetName() : FString(TEXT("NONE"));
                    UE_LOG(LogNodeShuffle, Display,
                        TEXT("WELLH2B-INDEX spatial WIDENED: '%s' on '%s' at %s is %.0f cm from well ")
                        TEXT("member '%s' (inside the %.0f cm radius); its name does NOT contain 'Frack' ")
                        TEXT("but meshActorOwner=AFGNodeMeshActor nodeMeshType=%d, so the mNodeMeshType ")
                        TEXT("gate admits it and the old name test did not. Nearest ordinary resource node ")
                        TEXT("is %.0f cm away; bystanderWins=%d (1 = A2 refused it, 0 = this pass may claim, ")
                        TEXT("capture and hide it). This mesh actor's own mNodeActor back-link is '%s' ")
                        TEXT("(frackingOwner=%d; 'NONE' = unset, which is the expected state for an ")
                        TEXT("unpaired well piece). A back-link to an ORDINARY node here is the ")
                        TEXT("stop-ship case. This line is T2's gate cost in the direction that can ")
                        TEXT("hide a node; NARROWED is the safe one. Said once per mesh."),
                        *MeshName, *SMA->GetName(), *Loc.ToCompactString(), FMath::Sqrt(BestSq),
                        *WellShort(Best->Path), WellMeshOwnerRadiusCm, static_cast<int32>(MeshType),
                        ByStSq < TNumericLimits<float>::Max() ? FMath::Sqrt(ByStSq) : -1.0f,
                        bBystanderWins ? 1 : 0,
                        *WPairedName,
                        (WPaired && IsFrackingActor(WPaired)) ? 1 : 0);
                }
            }
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
                        TEXT("WELLH2B-INDEX spatial: '%s' (nodeMeshType=%d) on actor '%s' at %s -> member ")
                        TEXT("'%s' at %.0f cm (runner-up %.0f cm, radius %.0f cm). Nearest-wins; a runner-up ")
                        TEXT("close to the winner means the H0 spacing measurement no longer holds. ")
                        TEXT("nodeMeshType is ENodeMeshType: 1=MT_Core 2=MT_Crack 3=MT_Satellite ")
                        TEXT("4=MT_DesertCore 5=MT_DesertCrack 6=MT_DesertSatellite."),
                        *MeshName, static_cast<int32>(MeshType), *SMA->GetName(), *Loc.ToCompactString(),
                        *WellShort(Best->Path),
                        FMath::Sqrt(BestSq), NextSq < TNumericLimits<float>::Max() ? FMath::Sqrt(NextSq) : -1.0f,
                        WellMeshOwnerRadiusCm);
                }
            }
            else if (bDiag && Best != nullptr)
            {
                UE_LOG(LogNodeShuffle, Verbose,
                    TEXT("WELLH2B-INDEX spatial REJECT: '%s' (nodeMeshType=%d) on '%s' at %s -- nearest well ")
                    TEXT("member '%s' is %.0f cm away, radius is %.0f cm, inRadius=%d. The type gate ")
                    TEXT("accepted this piece and this pass did not claim it; the fields above are what ")
                    TEXT("was measured, classify from them. A piece the TYPE gate rejected appears on a ")
                    TEXT("NARROWED line instead, one it newly ADMITTED on a WIDENED line. If well pieces ")
                    TEXT("are being missed, read all three."),
                    *MeshName, static_cast<int32>(MeshType), *SMA->GetName(), *Loc.ToCompactString(),
                    *WellShort(Best->Path), FMath::Sqrt(BestSq), WellMeshOwnerRadiusCm, bInRadius ? 1 : 0);
            }
        }
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2B-INDEX pass %d: %d vanilla well member(s) indexed, %d mesh piece(s) paired ")
        TEXT("(%d own, %d via engine link, %d spatial), %d bystander reject(s), ")
        TEXT("%d narrowed-by-type of %d name-matching candidate(s) (%d of them in radius), ")
        TEXT("%d widened-by-type of %d type-matching candidate(s) (%d of them in radius), ")
        TEXT("%d non-well node(s) in the ")
        TEXT("contest, of which activeMineable=%d and hiddenOriginal=%d (T3's snapshot is the ")
        TEXT("activeMineable set ONLY -- ns-t7-split removed hidden originals from it because a hidden ")
        TEXT("node cannot be built on regardless of any box, so an overlap against one was a FALSE ")
        TEXT("POSITIVE; the two numbers are printed together so a smaller snapshot on this build is ")
        TEXT("readable as THIS change and not as a world that lost nodes. They do NOT sum to the ")
        TEXT("contest count: the contest also holds AFGResourceDeposit, which is neither), ")
        TEXT("WHEN this was measured is load-bearing and is NOT an ignorable caveat: ")
        TEXT("RebuildWellMeshIndex runs inside ApplyWellRelocation, which ApplyLayout calls BEFORE ")
        TEXT("SuppressOriginalNodes (NodeShuffleSubsystem.cpp:2224 then :2244) -- so on the FIRST pass ")
        TEXT("of a session hiddenOriginal reads ~0 because nothing has been hidden YET this session ")
        TEXT("(hidden state is not persisted; that is why SuppressOriginalNodes re-runs on every load ")
        TEXT("and SteadyHiddenOriginals is emptied at :472). hiddenOriginal CLIMBING across passes is ")
        TEXT("correct and expected. hiddenOriginal==0 on the FIRST pass falsifies NOTHING. Only ")
        TEXT("hiddenOriginal==0 on EVERY pass of a session would mean IsHidden() is not the predicate ")
        TEXT("this world uses. MEASURED: the two counts at the instant of this sweep. NOT MEASURED: ")
        TEXT("what will be hidden later in this same ApplyLayout call. ")
        TEXT("%d member(s) with at least one piece. The link route is the one design 2.4 assumed; ")
        TEXT("a low 'via engine link' count next to a high 'spatial' count is the shape of the ")
        TEXT("suppression bug this index exists to make visible; compare the two numbers PRINTED ABOVE ")
        TEXT("ON THIS LINE against each other and against 'member(s) with at least one piece' -- no ")
        TEXT("constant from a previous build is cited here on purpose, because the one that used to be ")
        TEXT("was not re-derivable from the log it named. 'bystander reject' is ns-review-h2b ")
        TEXT("F-1's guard firing: a mesh that was closer to an ORDINARY resource node than to ")
        TEXT("any well member, left alone. A non-zero count is correct behaviour, not an error. ")
        TEXT("'narrowed-by-type' is T2's gate cost in the SAFE direction: in-radius pieces the old ")
        TEXT("Contains(\"Frack\") test would have taken whose owner is not an AFGNodeMeshActor or whose ")
        TEXT("mNodeMeshType is MT_Node, each named by a NARROWED line. 'widened-by-type' is the UNSAFE ")
        TEXT("direction: in-radius pieces this gate takes and the name test would not, each named by a ")
        TEXT("WIDENED line -- that is the direction that can hide an ordinary node's rock. READ EACH ")
        TEXT("COUNT AGAINST ITS CANDIDATE DENOMINATOR: a zero next to a zero denominator means nothing ")
        TEXT("was ever in range to be dropped or added on this save, which is not the same measurement ")
        TEXT("as the two gates agreeing."),
        WellAuditPasses, WellMeshIndexMembers, WellMeshIndexPieces, ByOwn, ByLink, BySpatial,
        RejectedBystander, NarrowedByType, NameCandidates, NameCandidatesInRadius,
        WidenedByType, TypeCandidates, TypeCandidatesInRadius, Bystanders.Num(),
        UseBoxNodes.Num(), HiddenOriginalUseBoxNodes, WellMeshIndex.Num());
}
