#pragma once

// rwg-trace-diag-1 (2026-10-27) -- DIAGNOSTICS ONLY. Measures what each failed FIRST spawn trace on RWG saw, so the log says it.
// Motivation: _team/test-logs/2026-10-27-nodeshuffle-few-nodes-forensics.md (265 of 308 post-roll spawn attempts unspawned, 250 of
// them spawnTraceMiss: the primary probe's first trace returned no blocking hit in the +200 m / -400 m window around the API Z).
// Nothing here decides, moves, spawns, redeals or writes a subsystem member or counter. Three hooks in NodeShuffleMapModePlace.cpp
// (RwgSpawnPass + RwgResolveAtSpawn) hand it values the caller already holds; the probes it runs are EXTRA raw line traces (never
// RaycastGroundAt, which teaches the persistent water grid and moves the probe counters) whose results feed only these log lines:
//   RWGTRACEMISS    a failed first trace (RwgProbeFirstClass < 0, the spawnTraceMiss predicate), rate-limited per entry.
//   RWGTRACECENSUS  once per RwgSpawnPass that attempted at least one candidate.
// State is file-static (one world at a time in the shipped game); BeginPass resets it every pass.
// check_rwgtrace_lint.ps1 pins the hooks and hashes the decision code around them against HEAD 65ab5b4.
#include "CoreMinimal.h"

class UWorld;

namespace NodeShuffleRwgTraceDiag
{
    // RwgSpawnPass, after the pawn list is built: resets the per-pass tallies and the probe budget; keeps the pawn positions.
    void BeginPass(int32 Pass, const TArray<FVector>& Pawns);

    // RwgResolveAtSpawn, right after the primary settle (bRwgProbeArmed = false): FirstClass / FirstZ are RwgProbeFirstClass /
    // RwgProbeFirstZ as the settle left them; Tracker is RwgStuckTracker's row for the entry BEFORE this attempt (null when untracked).
    void NoteFirstTrace(UWorld* World, const FGuid& Guid, const FString& ResPath, double X, double Y, double ApiZ, bool bShortWindow,
                        int32 FirstClass, double FirstZ, const TPair<int32, FString>* Tracker, double KillboxTopZ);

    // RwgSpawnPass, before RwgEmitPlaceCensus resets the PLACECENSUS counters: prints RWGTRACECENSUS when Attempted >= 1.
    void EmitPassCensus(int32 Pass, int32 Attempted, int32 SettledOnTerrain, int32 NoTerrainDefer, int32 DeferredSettle);
}
