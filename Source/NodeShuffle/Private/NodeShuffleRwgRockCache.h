#pragma once

// rwg-rockcache-1: THE ONE DEFINITION of "a rock sits ON its node" -- 8 m, measured in 2-D.
// Two readers, one constant, so they can never disagree about what "at this original" means:
//   * the stray-rock backstop in SuppressOriginalNodes (NodeShuffleSubsystem.cpp, `RockOwnRange`), which
//     hides a rock-named StaticMeshComponent within this range of a processed original;
//   * RebuildRwgRockCache (NodeShuffleRwgRockCache.cpp), which pairs an RWG original with the nearest
//     rock-shaped AFGNodeMeshActor within this range.
// The value is the one the backstop carried as a function-local constexpr before this packet (800 cm) and
// the one the RWGROCK2 census used as its overlap radius (NodeShuffleMapModeR1.cpp). It is NOT the 8 m
// node footprint (OverlapRejectRadius) or the 8 m cave cell (CaveCellCm): same number, different meaning,
// deliberately not unified with either.
namespace NodeShuffleRock
{
    constexpr float RockOwnRangeCm = 800.0f;
}
