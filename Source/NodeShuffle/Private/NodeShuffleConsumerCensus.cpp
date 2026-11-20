// ns-quietlog-1 (2026-11-20): NSCONSUMER, the census of the reflected layout state another mod (PrimalFauna) reads by name:
// ANodeShuffleSubsystem::Layout / SpawnedNodes and FNodeShuffleEntry::bActive / bIsNewNode / bRayCasted / bPinned.
// Always on; printed at the end of an ApplyLayout pass only when one of the counts differs from the last printed line
// (the first call of a subsystem prints). Reads only; writes nothing but its own statics.
#include "NodeShuffleSubsystem.h"

void ANodeShuffleSubsystem::EmitConsumerCensus()
{
    static TWeakObjectPtr<ANodeShuffleSubsystem> LastOwner;
    static int32 Pass = 0;
    static int32 Last[7] = { -1, -1, -1, -1, -1, -1, -1 };
    if (LastOwner.Get() != this)
    {
        LastOwner = this; Pass = 0;
        for (int32& V : Last) { V = -1; }
    }
    ++Pass;
    int32 Now[7] = { Layout.Num(), 0, 0, 0, 0, 0, SpawnedNodes.Num() };
    for (const FNodeShuffleEntry& E : Layout)
    {
        const bool bNewNotCast = E.bIsNewNode && !E.bRayCasted;
        if (E.bActive) { ++Now[1]; }
        if (E.bIsNewNode) { ++Now[2]; }
        if (bNewNotCast) { ++Now[3]; }
        if (E.bActive && bNewNotCast) { ++Now[4]; }
        if (E.bPinned) { ++Now[5]; }
    }
    bool bChanged = false;
    for (int32 i = 0; i < 7; ++i) { if (Now[i] != Last[i]) { bChanged = true; } Last[i] = Now[i]; }
    if (!bChanged) { return; }
    // pass counts this subsystem's ApplyLayout passes. layoutEntries: entries in Layout; active: bActive set; newNode: bIsNewNode set;
    // newNodeNotRayCasted: bIsNewNode set and bRayCasted clear; activeNewNodeNotRayCasted: the same with bActive set;
    // pinned: bPinned set; spawnedNodes: entries in the SpawnedNodes map.
    UE_LOG(LogNodeShuffle, Display, TEXT("NSCONSUMER: pass=%d layoutEntries=%d active=%d newNode=%d newNodeNotRayCasted=%d activeNewNodeNotRayCasted=%d pinned=%d spawnedNodes=%d"),
        Pass, Now[0], Now[1], Now[2], Now[3], Now[4], Now[5], Now[6]);
}
