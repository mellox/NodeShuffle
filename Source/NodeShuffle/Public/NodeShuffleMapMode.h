#pragma once
#include <limits>

#include "CoreMinimal.h"

class APawn;
class UWorld;
class AFGResourceNodeBase;
class FPWDensityField; // RWG's type (P2b ground query); never completed in NodeShuffle
class FJsonObject;     // RWG P5-2: the per-world cave file's cave object (NodeShuffleMapModeCaveJson.cpp)

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
    // A saved layout latches (clause A or B) only once the non-ours count reaches half its hide records (our own restored,
    // not-yet-adopted nodes and modded fixed-coordinate nodes stand stable long before RWG's burst); clause B also never before
    // this world time. TODO(2026-10-17, RWG P2b -> R3 reload): re-derive both from the reload trace.
    constexpr double RwgApplyFallbackSeconds = 600.0;
    // A spawn-time trace hit counts as the ground only within this distance of the API surface Z; otherwise the
    // node spawns at the API Z (R2: terrain-mesh hits p95 |dZ| 37.5 cm; foliage/water hits differ by metres).
    constexpr double RwgTraceAgreeCm = 300.0;
    // RWG P2c (2026-10-18): RwgTraceAgreeCm no longer selects the API Z anywhere. It is the census split of the primary
    // probe's FIRST hit (spawnTraceHit vs spawnTraceFar); the resettle keeps within RwgResettleKeepCm (P2c review S1).
    // A terrain-only trace (RaycastGroundAt bTerrainOnly) ignores at most this many non-terrain blocking components
    // (canopy instanced meshes, water sheets, other nodes) before it reads as no hit. TODO(2026-10-18, RWG P2c -> R5):
    // re-check against RWGTERRAINSKIP lines and PLACECENSUS terrainOnlyCapHitsAllCallers.
    constexpr int32 RwgTerrainSkipMax = 8;
    // RWG P2c resettle: node entries probed per pass (nearest the pawn first), well groups per pass, and the number of
    // void probes (no terrain-mesh hit) one entry may take in a session before the pass stops re-probing it.
    constexpr int32 RwgResettlePerPass = 32;
    constexpr int32 RwgResettleWellsPerPass = 4;
    constexpr int32 RwgResettleVoidGiveUp = 12;
    // RWG P2d (2026-10-18): rock-occlusion probe window (from this far above a placed node/member down to this far above its Z),
    // well resettle redeals per pass, and the member-to-probe-hit distance above which a well member reads as floating.
    // TODO(2026-10-18, RWG P2d -> R6): re-check against RESETTLE-ROCK / RESETTLE-WELL.
    constexpr double RwgRockOcclusionHeightCm = 20000.0; // RWG P2d review MF2: = RaycastGroundAt's primary start (StartZ + 20000), so the resettle removes exactly what placement refuses and the segment never starts inside a tall rock
    constexpr double RwgRockOcclusionEndAboveCm = 50.0;
    constexpr int32 RwgResettleWellRedealsPerPass = 2;
    constexpr double RwgWellFloatCm = 300.0;
    // RWG P4b (2026-10-18): satellite re-bind retry -- claims tried per pass (nearest the pawn first), and the attempts after
    // which a still-unbound claim drops one priority tier. TODO(2026-10-18, P4b -> R5): re-check against WELLCLAIM-RETRY.
    constexpr int32 RwgWellRetryClaimsPerPass = 32;
    constexpr int32 RwgWellRetryTierPasses = 12;
    constexpr double RwgResettleKeepCm = 50.0; // RWG P2c review: resettle keep tolerance (terrain-hit p95 37.5 cm). TODO(2026-10-18 -> R5)
    // Disagreement predicate for the water census: WaterLevelCmAt(x,y,0) > SurfaceHeightCm(x,y) + this.
    constexpr double RwgWaterApiMarginCm = 50.0;
    // RWG P2e (2026-10-18): the one-time sea re-deal checks at most RwgSeaRedealPerPass entries per spawn pass; RWGSEACHECK samples
    // RwgSeaCheckPerClass points per class out of at most RwgSeaCheckMaxDraws random draws.
    // TODO(2026-10-18, RWG P2e -> R7): re-check against RWGSEACHECK durationMs and RWGSEAREDEAL remaining.
    constexpr int32 RwgSeaRedealPerPass = 64;
    constexpr int32 RwgSeaCheckPerClass = 32;
    constexpr int32 RwgSeaCheckMaxDraws = 12000;
    constexpr int32 RwgRedealTries = 80; // RWG P2e review: API redeal draws per call; P(no spot) ~0.6 % at the R6 ~6 % dealable share. TODO(2026-10-18 -> R7): re-size from RWGSEAREDEAL dealFailed/durationMs
    constexpr int32 RwgSeaWellDealDraws = 128; // RWG P2e review: ~0.8 % fail at the R6 ~3.7 % well-site share. TODO(2026-10-18 -> R7)
    // RWG P2f (2026-10-18): void + killbox predicate (NodeShuffleMapModeVoid.cpp). RwgVoidPlateauCm is the SurfaceHeightCm RWG reports over
    // its void (FPWBaseMap::SeabedM = -30 m; MEASURED on ONE world, seed 1687464) -- used ONLY to select RWGVOIDCHECK class (a), never as a
    // rule (the census also prints the top two Z modes so another world reads out). RwgKillboxMarginCm: a dealt spot whose API surface is
    // below killbox top + this is rejected (research suggestion). TODO(2026-10-18, RWG P2f -> R8): re-check against RWGVOIDCHECK / RWGKILLBOX
    // / ROLLCENSUS rejectedKillbox and RESETTLE-KILLBOX.
    constexpr double RwgVoidPlateauCm = -3000.0;
    constexpr int32 RwgVoidCheckPerClass = 32;
    constexpr double RwgKillboxMarginCm = 1000.0;
    constexpr int32 RwgKillboxResettlePerPass = 16;
    // RWG P2f (coordinator, rwg-export-catalog.md): the dealer's void term tests VoidAt at the point AND at +-RwgVoidRingCm on X and on Y -- the
    // same 5-point cross RWG's own node keep-out APWWorldGenerator::NearVoid uses (catalogue: disassembled, 1500 cm). Kept on the public VoidAt
    // instead of binding NearVoid (private export: a refactor can drop or rename it). RWGVOIDCHECK still grades the raw single-point VoidAt.
    constexpr double RwgVoidRingCm = 1500.0;
    // RWG P5 (2026-10-18, user decision: caves on RWG use the VANILLA rule, natural share capped at 25 %). Design inputs, not measurements.
    // TODO(2026-10-18, RWG P5 -> R10): re-check every value below against ROLLCENSUS caveDrawsTried/caveAnyNearCalls/caveDealFailed.
    constexpr double RwgCaveNearRadiusCm = 3000.0;    // AnyNear radius of a cave-designated draw (cave tube radii in the JSON dumps: ~600-800 cm)
    constexpr double RwgCaveFloorMatchCm = 300.0;     // cave settle: a terrain-mesh hit within this of the dealt floor Z settles
    constexpr double RwgCaveInsideProbeCm = 100.0;    // IsInsideAt verify height above a floor (brief) and the settledInsideCave probe height
    constexpr double RwgCaveHeadroomCmMirror = 350.0; // = CaveHeadroomCm (NodeShuffleSubsystem.cpp anonymous namespace), kept equal by hand
    constexpr double RwgCaveMinCeilingCmMirror = 1200.0; // = CaveMinPlaceCeilingCm (same file): vanilla places only where a Miner fits
    constexpr double RwgCaveShareCapMirror = 0.25;    // = CaveShareCap (same file)
    constexpr int32 RwgCaveAnyNearPerSpot = 20000;    // AnyNear calls one cave-designated spot may spend before it falls back to a surface draw
    constexpr int32 RwgCaveAnyNearPerRoll = 400000;   // per roll; once spent, the cave share is 0 for the rest of that roll (counted)
    constexpr int32 RwgCaveSettleMissEscape = 8;      // mirror of the vanilla 8-miss cave escape (Subsystem.cpp EnsureNewNodeSpawned)
    constexpr int32 RwgCaveSettleSkipMax = 8;         // non-terrain, non-solid, non-water hits skipped by one cave settle trace
    // RWG P5-2 (2026-10-18, user decisions): cave share numerator = cave CAPACITY (sum of the JSON `rooms`); candidates from the JSON file.
    // TODO(2026-10-18, RWG P5-2 -> R10): 2 = floor of the measured mean rooms per cave (12 PWMaps files, 31 caves, 74 rooms, read 2026-10-18).
    // Used ONLY when the per-world file is absent / unreadable (capacity = Num() * this); re-check against the next files RWG writes.
    constexpr int32 RwgCaveDefaultRoomsPerCave = 2;
    constexpr int32 RwgCaveJsonCandidatesPerSpot = 64; // JSON candidates one cave-designated spot verifies before it falls back to a surface draw
    // One candidate generator from the per-world cave file (NodeShuffleMapModeCaveJson.cpp). Kind 0 = a rooms_geom room (axis-aligned XY
    // ellipse Rx x Ry around A, hint = the room floor); 1 = a disc of radius Rx around A (stand / mouth, file without rooms_geom); 2 = the
    // segment A->B widened by Rx (c0 / c1, file without rooms_geom). Weight = the XY area the generator samples.
    struct FRwgCaveCandidateGen { FVector A = FVector::ZeroVector; FVector B = FVector::ZeroVector; double Rx = 0.0, Ry = 0.0, FloorHintZ = 0.0, Weight = 0.0; uint8 Kind = 0; };
    // Appends one cave object's generators (rooms_geom rooms; without rooms_geom: stand + mouth discs and c0 / c1 segments). Returns the count added.
    int32 RwgCaveGensFromJson(const TSharedPtr<FJsonObject>& Cave, TArray<FRwgCaveCandidateGen>& Out);

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
        bool HasSea() const;                             // RWG P2e: InSeaVolume bound (optional export) and the spliced world found
        bool InSea(double X, double Y, double Z) const;  // RWG P2e: raw InSeaVolume; false when !HasSea()
        double GlobalMinZ() const;                       // RWG P2e: GlobalMinSurfaceCm, NaN when that optional export was dropped
        bool HasVoid() const;                            // RWG P2f: VoidAt bound (optional export) and the spliced world found
        bool VoidAt(double X, double Y) const;           // RWG P2f: raw VoidAt; false when !HasVoid() or outside the world (our check first)
        bool KillboxTopApi(double& OutZ) const;          // RWG P2f: GetKillboxTop on the generator; false when unbound or it returned false
        // RWG P5 (2026-10-18): FPWCarvedCaves queries (NodeShuffleMapModeCave.cpp; optional group, own byte-signature gate). The cave object
        // is CarvedCaves(F) = F+0x28, taken per call (never cached). Every method answers the "no cave" value when !HasCaves().
        bool HasCaves() const;                                              // usable AND the optional cave group bound
        int32 CaveNum() const;                                              // Num(); -1 when !HasCaves()
        bool CaveAnyNear(double X, double Y, double RadiusCm) const;        // AnyNear(X, Y, R)
        bool CaveFloorAt(double X, double Y, double ZHint, double& OutFloorZ) const; // FloorCmAt(X, Y, ZHint, out): the floor NEAREST ZHint
        bool CaveInsideAt(const FVector& P) const;                          // IsInsideAt(P)
    private:
        TSharedPtr<const FPWDensityField, ESPMode::ThreadSafe> Field;
        const void* F = nullptr;
        const void* S = nullptr;
        const void* G = nullptr; // RWG P2f: the generator (GetKillboxTop's this); set only when usable
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
    FString RwgSeaBindStatus(); // RWG P2e: InSeaVolume binding status (NodeShuffleMapModeHeightApi.cpp)
    FString RwgVoidBindStatus();    // RWG P2f: VoidAt binding status (NodeShuffleMapModeHeightApi.cpp)
    inline double RwgNaN() { return std::numeric_limits<double>::quiet_NaN(); } // RWG P2f: census value printed for an unmeasured field
    FString RwgKillboxBindStatus(); // RWG P2f: GetKillboxTop binding status (NodeShuffleMapModeHeightApi.cpp)
    void* RwgHeightApiBoundModule(); // RWG P5: the RWG module handle, non-null ONLY when the P2b gates passed (version, exports, signatures)
    FString RwgCaveBindStatus();     // RWG P5: FPWCarvedCaves group binding status (NodeShuffleMapModeCave.cpp)
    bool IsOursForRwg(AFGResourceNodeBase* Node);
}
