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
    int32 RwgProvisionalBatchPerPass();  // NodeShuffle.RwgProvisionalBatchPerPass, default 16 (rwg-farspawn-1c C2; was 64), clamped 8..512
    constexpr int32 RwgProvisionalBatchMin = 8;
    constexpr int32 RwgProvisionalBatchMax = 512;
    constexpr int32 RwgProvisionalSettleSamplesPerPass = 8; // PROVSETTLE lines per pass (counters carry the rest)
    // rwg-farspawn-1c C1: a settle whose 2-D shift from the provisional actor is at most this keeps and moves the SAME actor; a larger
    // shift (a spiral step, a redeal) respawns. Same value as AdoptMatchRadiusCm (NodeShuffleSubsystem.cpp), the radius the save adopt
    // already treats as "the same node spot"; a separate constant because that one is file-local there.
    constexpr double RwgProvisionalMoveTolCm = 300.0;
}
