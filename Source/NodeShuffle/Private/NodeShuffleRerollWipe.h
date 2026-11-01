// Copyright. NodeShuffle packet rwg-rerollwipe-1 (2026-10-31).
// Per-wipe tally for the re-roll wipe in ANodeShuffleSubsystem::RollLayout (NodeShuffleSubsystem.cpp, `if (bIsReroll)` block).
// Filled by RerollWipeBegin / RerollWipeTeardownRegistrations / RerollWipeNoteInvalidSlot, printed once by RerollWipeEmitCensus
// (NodeShuffleRerollWipe.cpp). A stack local of RollLayout; never saved, never outlives the wipe.
#pragma once

#include "CoreMinimal.h"

class AFGResourceNode;

struct FNodeShuffleRerollWipeTally
{
    int32 Destroyed = 0;            // slots whose node was valid: registrations torn down, then the caller called Destroy()
    int32 RemovedFromManager = 0;   // sum of mResourceNodes.RemoveSingleSwap returns (entries that were in the Mk1 snap list)
    int32 RepByActor = 0;           // RemoveRepresentationOfActor returned true
    int32 RepByNode = 0;            // FindResourceNodeRepresentation found one, and it was removed
    int32 RepAny = 0;               // nodes where either representation removal acted
    int32 Kept = 0;                 // slots skipped because the new layout carries them (KeptGuids)
    int32 InvalidSlots = 0;         // slots whose node failed IsValid: no Destroy (unchanged); non-null ones dropped from the snap list by address (review F2)
    int32 InvalidSlotsNull = 0;     // review F2: invalid slots whose pointer was null (cannot be compared)
    int32 InvalidInManagerAtWipe = 0;     // review F2: non-null invalid slots whose address was in mResourceNodes (Contains, before the removal)
    int32 InvalidRemovedFromManager = 0;  // review F2: sum of mResourceNodes.RemoveSingleSwap returns for non-null invalid slots
    int32 NoManager = 0;            // destroyed nodes for which GetNodeManager() returned null
    int32 NoRepManager = 0;         // destroyed nodes for which the representation manager was null
    int32 ManagerCountBefore = -1;  // mResourceNodes.Num() at RerollWipeBegin (-1: no manager)
    int32 PerNodeLogged = 0;        // gated per-node lines printed (cap in NodeShuffleRerollWipe.cpp)
    // Raw addresses, compared by pointer only after the loop. NEVER dereferenced: they are destroyed actors by then.
    TArray<AFGResourceNode*> DestroyedPtrs;
    TArray<AFGResourceNode*> InvalidPtrs;
};
