// ns-lead-restore-1 (2026-11-11): the far-restore argument of ANodeShuffleSubsystem::EnsureNewNodeSpawned. Plain C++ (not reflected:
// no reflection macro on the struct or its members). A non-null pointer is the ONLY switch: it is a function parameter, never a member toggled around a call.
// The callee writes this struct and nothing else on a held exit; the caller (NodeShuffleSettledRestore.cpp) prints it.
#pragma once

#include "CoreMinimal.h"

struct FNodeShuffleFarRestore
{
    // The refusing exit taken, by name; null after the call = no hold exit was taken (the caller tests the SpawnedNodes slot).
    const TCHAR* HeldGate = nullptr;
    // What the occupancy gate hit at the entry's own Location (primary spot only), as the gate recorded it.
    FString HitKind = TEXT("none");
    FString HitClass = TEXT("-");
    FString HitPath = TEXT("-");
    double HitDistCm = -1.0; // 3-D distance from the entry's Location to the hit actor's location; -1 = no hit recorded
    int32 PinnedBuildingHitsIgnored = 0; // building overlaps the gate skipped because the entry is pinned (I1 exemption)
    int32 EnclosedRaysBlocked = -1;      // enclosure test at the entry's Location: rays that hit (-1 = the test did not run)
    int32 EnclosedRaysTotal = -1;        // and rays cast
};
