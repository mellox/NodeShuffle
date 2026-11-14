#pragma once
// ns-rocks-1 (2026-11-14): rock-refusal near search (A), sunken solid-top column test (B), and their session censuses (C).
// Handoff: _team/test-logs/2026-11-14-nodeshuffle-rocks-1-handoff.md. Every value here is a DESIGN INPUT (no measured threshold
// exists yet); the censuses in NodeShuffleMapModeRocks.cpp print the distributions a later packet would pick a threshold from.
#include "CoreMinimal.h"

namespace NodeShuffleRocks
{
    // A. Near search: two rings tried nearest-first, before the 40 m spiral, only after the PRIMARY probe was refused on a rock.
    // Ring radii vs the node footprint: the solid-top footprint ring is 3.5 m (SmoothNormalRingCm), so a node needs about 7 m of
    // single surface. At 10 m a near probe's own footprint ring (3.5 m) stays 3 m clear of the refused spot's ring; 6 bearings at
    // 60 deg give a 10 m chord, more than one 7 m footprint, so neighbouring near footprints do not overlap. Ring 2 (20 m) is
    // turned 30 deg to cover ring 1's gaps and stays half way to the spiral's first point (40 m).
    constexpr int32 NearPerRing = 6;
    constexpr int32 NearProbeCount = 12;          // 2 rings x 6 bearings; each is ONE RaycastGroundAt call, the same as one spiral probe
    constexpr float NearRing1Cm = 1000.0f;        // 10 m
    constexpr float NearRing2Cm = 2000.0f;        // 20 m
    constexpr float NearRing2TurnDeg = 30.0f;

    // Deterministic per entry: the base bearing comes from the entry id (EntryGuid.A, no hashing), so entries do not all try north first,
    // and the same entry with the same dealt spot always tries the same points in the same order.
    inline FVector NearProbe(const FVector& Origin, const FGuid& EntryGuid, int32 Step)
    {
        const float BaseDeg = static_cast<float>(EntryGuid.A % 360u);
        const bool bRing2 = Step >= NearPerRing;
        const int32 K = bRing2 ? Step - NearPerRing : Step;
        const float Deg = BaseDeg + 60.0f * static_cast<float>(K) + (bRing2 ? NearRing2TurnDeg : 0.0f);
        const float Rad = FMath::DegreesToRadians(Deg);
        const float R = bRing2 ? NearRing2Cm : NearRing1Cm;
        return FVector(Origin.X + R * FMath::Cos(Rad), Origin.Y + R * FMath::Sin(Rad), Origin.Z);
    }

    // B. A solid top that passed every existing gate but lies MORE than this far below RWG's API surface AT THE TOP'S OWN XY (the
    // settled XY, never the dealt XY) gets rule B (NodeShuffleMapModeRocks.cpp): a terrain-presence trace, then the column test.
    // 0.5 m: wider than the measured API-vs-terrain spread of terrain settles (54 of 54 within -28..+50 cm, 2026-11-14 sunk-in-terrain
    // report), and below the 0.5-1 m burial measured for entry 22863CAF. Design input from one night's numbers.
    constexpr double SunkenMarginCm = 50.0;
    // Terrain-presence trace: from the trace start down, stepping through EVERY non-terrain stop (rock meshes included), at most this many.
    constexpr int32 TerrainProbeSkipCap = 16;
    // Terrain found higher than the top by more than this refuses the top (terrain over it at its own XY).
    constexpr double TerrainAboveTolCm = 50.0;
    // A/C spiral and near probes trace from max(dealt-spot Z, API Z at the probe XY) + the primary trace height (RwgPrimaryTraceUpCm).
    // Column test window: a footprint column passes only when its first stop from the trace start is the SAME component and instance
    // AND no higher than this above the hit (the existing footprint probe's own start height, CliffTop.cpp P.Z + 400).
    constexpr double SunkenColumnTolCm = 400.0;

    constexpr int32 SampleCapPerPass = 8;         // ROCKSUNKEN / ROCKNEAR sample lines per spawn pass (diagnostics on)
    constexpr int32 MeshTopN = 12;                // ROCKSIZE-MESH prints this many mesh names
}
