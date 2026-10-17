#pragma once

#include "CoreMinimal.h"

class APawn;
class UWorld;
class AFGResourceNodeBase;
class FPWDensityField; // RWG's type (P2b ground query); never completed in NodeShuffle

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

    // RWG P2b (2026-10-17, brief _team/nodeshuffle-rwg-p2-spec.md §2 + "R2 measurements"): the off-vanilla
    // roll is ON for mode=rwg ONLY (Unknown maps still idle). P1 parked this false; the lint
    // tools/check_rwgp1_lint.ps1 pin R1 was changed deliberately to pin it TRUE. A code constant, not a config row.
    constexpr bool bRwgRollEnabled = true;

    // ---- RWG P2b placement v2 (2026-10-17). Read only off vanilla (every reader sits behind a map-mode test). ----
    // SaveGame RwgLayoutVersion value stamped by a P2b roll on RWG. 0 (the default every older save loads with,
    // vanilla included) = legacy: on RWG such a layout is ignored and re-rolled once; on vanilla it is never read.
    constexpr int32 RwgLayoutVersionP2b = 1;
    // Actor creation on RWG: only entries within min(SpawnRadius, this) of a pawn, nearest first (R2 residency:
    // 0-500 m 16/16, 0.5-1 km 11/16, 1-2 km 2/16). TODO(2026-10-17, RWG P2b -> R3): re-check against PLACECENSUS.
    constexpr float RwgAttemptRadiusCm = 100000.f;
    // At most this many NOT-YET-LIVE entries reach EnsureNewNodeSpawned per pass on RWG (live entries are uncapped).
    constexpr int32 RwgMaxSpawnAttemptsPerPass = 32;
    // Apply-on-load clause B: a save holding a layout applies once the all-non-ours node count is unchanged for
    // this many consecutive 5 s ticks (any stage) AND at least RwgMinNodes. TODO(2026-10-17, RWG P2b -> R3 reload).
    constexpr int32 RwgApplyStableTicks = 12;
    // A spawn-time trace hit counts as the ground only within this distance of the API surface Z; otherwise the
    // node spawns at the API Z (R2: terrain-mesh hits p95 |dZ| 37.5 cm; foliage/water hits differ by metres).
    constexpr double RwgTraceAgreeCm = 300.0;
    // Disagreement predicate for the water census: WaterLevelCmAt(x,y,0) > SurfaceHeightCm(x,y) + this.
    constexpr double RwgWaterApiMarginCm = 50.0;

    // RWG P2b ground query over the runtime-bound RWG exports (NodeShuffleMapModeHeightApi.cpp). Construct once
    // per batch (it holds the density field's strong reference for its lifetime, never across frames).
    // IsUsable() is false unless the binding is accepted (version + exports + byte signatures), Get/GetField
    // returned non-null, the plausibility range holds and the half extent was read: then NO method may be called.
    class FRwgGroundQuery
    {
    public:
        FRwgGroundQuery(UWorld* World, UObject* Generator);
        ~FRwgGroundQuery();
        bool IsUsable() const { return F != nullptr && HalfExtentCm > 0.0; }
        const FString& Status() const { return StatusText; }
        double HalfExtent() const { return HalfExtentCm; }
        bool Outside(double X, double Y) const;
        double SurfaceZ(double X, double Y) const;
        double WaterLevelZ(double X, double Y) const;
    private:
        TSharedPtr<const FPWDensityField, ESPMode::ThreadSafe> Field;
        const void* F = nullptr;
        const void* S = nullptr;
        double HalfExtentCm = -1.0;
        FString StatusText;
    };

    // RWG P2a (2026-10-17, brief `_team/nodeshuffle-rwg-p2-spec.md` §1): readiness v2 needs this many
    // consecutive 5 s ticks with an unchanged non-ours node count, counted ONLY from the first tick that read
    // the generator's EPWProgressStage as Ready (that tick zeroes the counter; 12 = 60 s after it). Ticks
    // before the first Ready never count. Read only by the observe-only readiness predicate in RwgObserveTick.
    // TODO(2026-10-17, RWG P2a -> R2): 12 rests on ONE machine's 30.9 s post-plan burst (R1); RWG estimates 116 s on a 2700X. Re-check against R2's RWGSTAGE/RWGWAIT deltas before P2b.
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
