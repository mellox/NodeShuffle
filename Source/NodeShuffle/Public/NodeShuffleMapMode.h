#pragma once

#include "CoreMinimal.h"

class APawn;
class UWorld;
class AFGResourceNodeBase;

// RWG P1 (2026-10-14, spec `_team/nodeshuffle-rwg-fullshuffle-spec.md` §A1/§B/§H): which map this world is.
// Latched once per world by ANodeShuffleSubsystem::GetMapMode() from the persistent level's package name --
// NEVER from node counts or `levelPlaced`. Unlatched is the pre-latch state only; GetMapMode() never returns it.
enum class ENodeShuffleMapMode : uint8
{
    Unlatched,
    Vanilla,
    Rwg,
    Unknown
};

namespace NodeShuffleMapMode
{
    // The two package names this build recognises. The RWG one is MEASURED (crash log 2026-10-13 L7964).
    // The vanilla one is MEASURED as the LoadMap URL in past FactoryGame logs; that GetOutermost()->GetName()
    // on the persistent level returns exactly this string is ASSUMED until the vanilla-control run prints
    // MAPMODE mode=vanilla. A mismatch classifies vanilla as Unknown (observe-only, no roll), so the vanilla
    // control is the first runtime step of every build that carries this file.
    inline const TCHAR* VanillaPackage() { return TEXT("/Game/FactoryGame/Map/GameLevel01/Persistent_Level"); }
    inline const TCHAR* RwgPackage() { return TEXT("/RandomWorldGenerator/Maps/ProceduralWorldLevel/ProceduralWorldLevel"); }

    // TODO(2026-10-14, RWG P1 -> P2): PARKED. Off-vanilla roll is OFF in P1 (observe-only). P2 flips this to
    // true together with B3/B5/C2; the lint tools/check_rwgp1_lint.ps1 pins it false and P2 must change that
    // pin deliberately. A code constant, not a config row (spec §H).
    constexpr bool bRwgRollEnabled = false;

    // RWG P2a (2026-10-17, brief `_team/nodeshuffle-rwg-p2-spec.md` §1): readiness v2 needs this many
    // consecutive 5 s ticks with an unchanged non-ours node count, counted ONLY from the first tick that read
    // the generator's EPWProgressStage as Ready (that tick zeroes the counter; 12 = 60 s after it). Ticks
    // before the first Ready never count. Read only by the observe-only readiness predicate in RwgObserveTick.
    constexpr int32 RwgStableTicksRequired = 12;
    // Consecutive unreadable-stage ticks before the one-shot RWGWAIT unreadable Warning (12 = 60 s).
    constexpr int32 RwgUnreadableWarnTicks = 12;
    // Post-Ready re-probes (RWGWATER2 / RWGROCK2 / RWGHEIGHTAPI): at the latch, then every 12 ticks (60 s)
    // until 120 ticks (10 min) after it.
    constexpr int32 RwgReprobeWindowTicks = 120;
    // Mirror of MinVanillaNodesForRoll (NodeShuffleSubsystem.cpp anonymous namespace, 50). Kept equal by hand.
    // Shared by NodeShuffleMapMode.cpp and NodeShuffleMapModeR1.cpp (moved here from the .cpp in P2a).
    constexpr int32 RwgMinNodes = 50;
    // RWGWAIT cadence (every 6th tick = 30 s), the post-ready observe cadence (60 s), the one-shot warning.
    constexpr int32 RwgWaitEveryTicks = 6;
    constexpr int32 RwgObserveEveryTicks = 12;
    constexpr double RwgWaitWarnSeconds = 900.0;

    // Shared by NodeShuffleMapMode.cpp and NodeShuffleMapModeR1.cpp. FirstPlayerPawn: the first controller's
    // pawn or null. IsOursForRwg: the same "ours" test as the late-node capture scan (managed registry, our
    // component, legacy class) -- a node it returns true for is never counted as an original.
    APawn* FirstPlayerPawn(UWorld* World);
    bool IsOursForRwg(AFGResourceNodeBase* Node);
}
