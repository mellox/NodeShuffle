#pragma once

// rwg-farspawn-1 (2026-10-31): provisional far spawn on RWG. Brief: _team/test-logs/2026-10-31-nodeshuffle-farspawn-brief.md.
// Every unspawned, active, unpinned RWG entry BEYOND the attempt radius gets a real live node at its dealt X/Y and the Z this seam
// returns, so the hand scanner, the radar tower and the map marker can find it; the spawn pass settles it on approach through the
// unchanged RwgResolveAtSpawn. The subsystem side lives in NodeShuffleMapModeProvisional.cpp.

#include "CoreMinimal.h"
#include "NodeShuffleMapMode.h"

namespace NodeShuffleMapMode
{
    // THE SEAM. Pure: the bound RWG height API is its only input besides X/Y (no world, no actor, no trace).
    // Today: the RWG surface height (the same SurfaceZ call the roll's deal uses), OutSource = "api".
    // A follow-up packet (RWG mesh-stack binding) replaces the BODY only, answering a mesh-aware Z with OutSource = "meshStack".
    double RwgProvisionalZ(const FRwgGroundQuery& Q, double X, double Y, FString& OutSource);

    // Console variables (the RwgRingRadiusM precedent: a console variable, not a settings-panel row). Read each pass.
    bool RwgSpawnAllAtRollOn();          // NodeShuffle.RwgSpawnAllAtRoll, default 1 (ON)
    int32 RwgProvisionalBatchPerPass();  // NodeShuffle.RwgProvisionalBatchPerPass, default 64, clamped 8..512
    constexpr int32 RwgProvisionalBatchMin = 8;
    constexpr int32 RwgProvisionalBatchMax = 512;
    constexpr int32 RwgProvisionalSettleSamplesPerPass = 8; // PROVSETTLE lines per pass (counters carry the rest)
}
