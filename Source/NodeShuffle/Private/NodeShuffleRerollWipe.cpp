// rwg-rerollwipe-1 (2026-10-31) -- registration teardown for the re-roll wipe + its REROLLWIPE census. Header: NodeShuffleRerollWipe.h.
// Brief: _team/test-logs/2026-10-31-nodeshuffle-reroll-wipe-brief.md (review F1 of rwg-farspawn-1).
// THE DEFECT. RollLayout's re-roll wipe (NodeShuffleSubsystem.cpp, `if (bIsReroll)`) called Destroy() on every non-kept spawned node
// without dropping it from AFGResourceNodeManager::mResourceNodes, a RAW TArray<AFGResourceNode*> (no UPROPERTY, so GC never nulls
// it), the list the Miner Mk1 hologram's snap needs a node in (redesign-11 measured: registering into it enabled Mk1 snap; the read
// itself is closed source), and without removing its map/compass representation or scan reveal.
// THE MECHANISM. FGResourceNodeManager.h exposes NO removal API for that list (no Remove/Unregister member), and our own
// DeregisterNodeFromManager returns early for OUR nodes (NodeShuffleIsOurNode). The only removal open to us is the friend-granted
// raw edit `mResourceNodes.RemoveSingleSwap(Node)` (Config/AccessTransformers.ini), the same call RwgRedealLiveEntry
// (NodeShuffleMapModeRock.cpp) and RwgProvisionalDespawn (NodeShuffleMapModeProvisional.cpp) already make. Whether the closed-source
// AFGResourceNodeBase::EndPlay also removes the pointer is NOT known; removedFromManager on the census measures how many destroyed
// nodes were still listed at their removal.
// A THIRD COPY, deliberately: the two sibling teardowns live in rwg-farspawn-1f files under review (farspawn lint F6 pins
// RwgProvisionalDespawn's order), so they are not refactored onto this helper here. Same order as both: snap list, representation
// (by actor, then by node), scan reveal, UpdateNodeRepresentation. The caller calls Destroy() right after, then UnregisterManagedNode,
// then removes the SpawnedNodes slot (coexist-1 / coexist-veto-1 order, comments at the call site). Nothing here writes SpawnedNodes.
// Log lines print the predicates measured this run (counts, return values), never a cause.
#include "NodeShuffleRerollWipe.h"
#include "NodeShuffleSubsystem.h"
#include "NodeShuffle.h"
#include "Engine/World.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceNodeManager.h"
#include "FGActorRepresentationManager.h"
#include "Representation/FGResourceNodeRepresentation.h"

namespace
{
    constexpr int32 RerollWipePerNodeLogCap = 8; // gated per-node lines per wipe (a far-spawn world wipes ~900 nodes)
}

void ANodeShuffleSubsystem::RerollWipeBegin(FNodeShuffleRerollWipeTally& T)
{
    AFGResourceNodeManager* Mgr = GetNodeManager();
    T.ManagerCountBefore = Mgr ? Mgr->mResourceNodes.Num() : -1;
}

void ANodeShuffleSubsystem::RerollWipeTeardownRegistrations(AFGResourceNode* Node, FNodeShuffleRerollWipeTally& T)
{
    // The caller tested IsValid(Node) and calls Node->Destroy() immediately after this returns.
    T.DestroyedPtrs.Add(Node);
    ++T.Destroyed;
    int32 Removed = 0;
    if (AFGResourceNodeManager* Mgr = GetNodeManager()) { Removed = Mgr->mResourceNodes.RemoveSingleSwap(Node); T.RemovedFromManager += Removed; }
    else { ++T.NoManager; }
    bool bByActor = false;
    bool bByNode = false;
    if (AFGActorRepresentationManager* RepMgr = AFGActorRepresentationManager::Get(GetWorld()))
    {
        bByActor = RepMgr->RemoveRepresentationOfActor(Node);
        if (UFGResourceNodeRepresentation* NodeRep = RepMgr->FindResourceNodeRepresentation(Node)) { RepMgr->RemoveRepresentation(NodeRep); bByNode = true; }
    }
    else { ++T.NoRepManager; }
    if (bByActor) { ++T.RepByActor; }
    if (bByNode) { ++T.RepByNode; }
    if (bByActor || bByNode) { ++T.RepAny; }
    Node->RemoveResourceNodeScan_Local();
    Node->UpdateNodeRepresentation();
    // review F6: MEASURE (pre-Destroy) whether a node representation is present after UpdateNodeRepresentation (closed source).
    if (AFGActorRepresentationManager* RepMgr2 = AFGActorRepresentationManager::Get(GetWorld())) { if (RepMgr2->FindResourceNodeRepresentation(Node)) { ++T.RepPresentAfterUpdate; } }
    if (FNodeShuffleModule::AreDiagnosticsEnabled() && T.PerNodeLogged < RerollWipePerNodeLogCap)
    {
        ++T.PerNodeLogged;
        UE_LOG(LogNodeShuffle, Display, TEXT("REROLLWIPE-NODE %s at %s: snapListRemoved %d, repOfActorReturned %d, nodeRepFound %d (first %d per wipe, diagnostics only)"),
            *Node->GetName(), *Node->GetActorLocation().ToCompactString(), Removed, bByActor ? 1 : 0, bByNode ? 1 : 0, RerollWipePerNodeLogCap);
    }
}

void ANodeShuffleSubsystem::RerollWipeNoteInvalidSlot(AFGResourceNode* Node, FNodeShuffleRerollWipeTally& T)
{
    // No Destroy (unchanged). review F2: a NON-NULL invalid slot is a node another mod destroyed; the wipe's RemoveCurrent drops the UPROPERTY
    // hard ref that kept its address uncollected, so drop it from the raw snap list now. Address compare only (Contains/RemoveSingleSwap
    // test element == Node and never dereference). Contains is measured BEFORE the removal.
    ++T.InvalidSlots;
    if (!Node) { ++T.InvalidSlotsNull; return; }
    T.InvalidPtrs.Add(Node);
    if (AFGResourceNodeManager* Mgr = GetNodeManager())
    {
        if (Mgr->mResourceNodes.Contains(Node)) { ++T.InvalidInManagerAtWipe; }
        T.InvalidRemovedFromManager += Mgr->mResourceNodes.RemoveSingleSwap(Node);
    }
}

void ANodeShuffleSubsystem::RerollWipeEmitCensus(const FNodeShuffleRerollWipeTally& T)
{
    // Every count below that reads the snap list is a POINTER COMPARE (TArray::Contains on raw addresses). The addresses are never
    // dereferenced: the destroyed ones are pending-kill actors by now. -1 = no resource-node manager this run.
    AFGResourceNodeManager* Mgr = GetNodeManager();
    int32 ManagerCountAfter = -1;
    int32 StaleAfter = -1;
    int32 InvalidStillIn = -1;
    if (Mgr)
    {
        ManagerCountAfter = Mgr->mResourceNodes.Num();
        StaleAfter = 0;
        for (AFGResourceNode* Ptr : T.DestroyedPtrs) { if (Mgr->mResourceNodes.Contains(Ptr)) { ++StaleAfter; } }
        InvalidStillIn = 0;
        for (AFGResourceNode* Ptr : T.InvalidPtrs) { if (Mgr->mResourceNodes.Contains(Ptr)) { ++InvalidStillIn; } }
    }
    // review F2: the invalid-slot still-listed count likewise re-reads a list the invalid-slot path just removed those addresses from;
    // invalidInManagerAtWipe (Contains before the removal) is the measurement for that population, read not graded.
    // UNGATED: once per re-roll; it is the packet's control line.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("REROLLWIPE: destroyed=%d removedFromManager=%d staleInManagerAfter=%d representationsRemoved=%d managerCountBefore=%d managerCountAfter=%d kept=%d repByActor=%d repByNode=%d invalidSlots=%d invalidSlotsStillInManager=%d noManager=%d noRepManager=%d invalidSlotsNull=%d invalidInManagerAtWipe=%d invalidRemovedFromManager=%d destroyRefused=%d repPresentAfterUpdate=%d"),
        T.Destroyed, T.RemovedFromManager, StaleAfter, T.RepAny, T.ManagerCountBefore, ManagerCountAfter, T.Kept,
        T.RepByActor, T.RepByNode, T.InvalidSlots, InvalidStillIn, T.NoManager, T.NoRepManager, T.InvalidSlotsNull, T.InvalidInManagerAtWipe, T.InvalidRemovedFromManager,
        T.DestroyRefused, T.RepPresentAfterUpdate);
}
