#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ModSubsystem.h"
#include "FGSaveInterface.h"
#include "Resources/FGResourceNode.h"
#include "NodeShuffleSubsystem.generated.h"

class AFGNodeMeshActor;
class AFGResourceScanner;

// One node-pool entry of the per-save layout. The layout is rolled exactly
// once per save (seeded) and afterwards only ever *applied*; it is the single
// source of truth for which nodes exist, are active, and what they carry.
USTRUCT()
struct FNodeShuffleEntry
{
    GENERATED_BODY()

    // Stable identity; spawned actors are tagged with it so live actors can be
    // matched back to entries across the session.
    UPROPERTY(SaveGame) FGuid EntryGuid;

    // True for mod-added node locations, false for vanilla level nodes.
    UPROPERTY(SaveGame) bool bIsNewNode = false;

    // GetPathName() of the vanilla level actor (empty for new nodes). Level
    // actor paths are stable across loads.
    UPROPERTY(SaveGame) FString VanillaNodePath;

    UPROPERTY(SaveGame) FVector Location = FVector::ZeroVector;
    UPROPERTY(SaveGame) FRotator Rotation = FRotator::ZeroRotator;

    // Vanilla state at roll time (empty/RP_MAX for new nodes).
    UPROPERTY(SaveGame) FString OriginalResourceClassPath;
    UPROPERTY(SaveGame) TEnumAsByte<EResourcePurity> OriginalPurity = RP_MAX;

    // Rolled state.
    UPROPERTY(SaveGame) FString AssignedResourceClassPath;
    UPROPERTY(SaveGame) TEnumAsByte<EResourcePurity> AssignedPurity = RP_Normal;

    // Inactive vanilla nodes are deactivated each session; inactive new nodes
    // are simply never spawned.
    UPROPERTY(SaveGame) bool bActive = true;

    // Had a miner/extractor at roll time: never retyped, never deactivated.
    UPROPERTY(SaveGame) bool bPinned = false;

    // New node has been settled onto the terrain (raycast done near a player).
    UPROPERTY(SaveGame) bool bRayCasted = false;

    // The vanilla node Blueprint class to spawn new nodes from.
    UPROPERTY(SaveGame) FString NodeClassPath;

    // Resource form of the assigned resource at roll time: 1 = solid, 2 = liquid
    // (matches EResourceForm). Lets the apply path treat liquid (oil) nodes
    // correctly without re-deriving the form from a possibly-unloaded class.
    // 0 = unknown/legacy (treated as solid). Phase 2.
    UPROPERTY(SaveGame) uint8 ResourceForm = 0;
};

// One original vanilla node location to suppress on stream-in after a wipe-on-
// reroll: the node itself plus its associated separate rock are hidden whenever
// they stream in, unless the spot is occupied or reused by the new layout. This
// record is persistent so suppression is reliable across sessions.
USTRUCT()
struct FNodeShuffleSuppressedOriginal
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) FString VanillaNodePath;
    UPROPERTY(SaveGame) FVector Location = FVector::ZeroVector;
};

// Server-side brain of NodeShuffle.
//
// Lifecycle per session:
//   BeginPlay -> repeating timer -> Tick():
//     1. (first tick) roll the layout if the save has none, or re-roll if the
//        user explicitly enabled AllowReroll with a new SeedOverride.
//     2. apply the layout idempotently: retype vanilla nodes via the game's
//        native SaveGame class/purity overrides, deactivate inactive vanilla
//        nodes, spawn + dress missing new nodes, settle new nodes near
//        players, re-associate extractors that lost their (respawned) node.
//
// Hard safety rule enforced at roll AND apply time: a node with a miner,
// extractor or portable miner on it is never changed in any way.
UCLASS()
class NODESHUFFLE_API ANodeShuffleSubsystem : public AModSubsystem, public IFGSaveInterface
{
    GENERATED_BODY()

public:
    ANodeShuffleSubsystem();

    // IFGSaveInterface
    virtual bool ShouldSave_Implementation() const override { return true; }
    virtual bool NeedTransform_Implementation() override { return false; }
    virtual void PreSaveGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void PostSaveGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void PreLoadGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void PostLoadGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void GatherDependencies_Implementation(TArray<UObject*>& out_dependentObjects) override {}

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    // ---- periodic driver ----
    // (Named to not hide AActor::Tick(float) — clang errors on that for the
    // Linux server target even though MSVC accepts it.)
    void RefreshTick();

    // ---- roll ----
    // Strict gate for the INITIAL roll: requires 50+ pristine /Game/ vanilla
    // nodes streamed in, proving the world is fully loaded before a first shuffle.
    bool IsWorldReadyForRoll() const;
    // Shuffle-aware gate for the RE-ROLL path. A shuffled save has most of its
    // originally-vanilla nodes retyped (no longer /Game/) or destroyed, so the
    // pristine-vanilla count never reaches 50 and the strict gate above can never
    // pass on reload. Instead require enough loaded resource nodes of ANY kind to
    // prove the world has streamed in, after a short post-load settle.
    bool IsWorldReadyForReroll() const;
    void RollLayout(int32 Seed, bool bIsReroll);
    // Before a re-roll rescans the vanilla pool, restore each non-new live node's
    // resource class/purity to the original values stored in its layout entry, so
    // the rescan sees the true vanilla pool instead of the previously-shuffled one.
    // Destroyed nodes cannot return; that is logged and accepted.
    void RestoreOriginalsForReroll();
    // Map-wide spread: distribute new locations across the bounding box of the
    // full known-node set (whole playable map) rather than clustered around the
    // currently-loaded vanilla nodes, so the initial roll is not bunched at the
    // player's load point. Streaming-independent on reroll (saved locations span
    // the map).
    // VanillaLocations defines the map bounding box; AvoidLocations is the full
    // union of occupied/kept/original/pinned locations new nodes must be spaced
    // away from (FIX 2 overlap guard).
    void GenerateNewLocations(FRandomStream& Rng, const TArray<FVector>& VanillaLocations,
                              const TArray<FVector>& AvoidLocations,
                              int32 Count, TArray<FVector>& OutLocations) const;
    bool ReadCustomLocationsJson(TArray<FVector>& OutLocations) const;
    void WriteGeneratedLocationsJson(const TArray<FVector>& Locations) const;

    // ---- apply ----
    void ApplyLayout();
    void ApplyVanillaRetype(FNodeShuffleEntry& Entry, AFGResourceNode* Node, bool& bOutChangedWorld);
    // LOSSLESS DEACTIVATION (architectural change, build lossless-5): a vanilla
    // node is NEVER destroyed. It is made reversibly inactive — hidden, no
    // collision, and re-hidden idempotently every tick (the runtime hide reverts
    // on reload AND on stream-in). Its path is tracked in DeactivatedNodePaths
    // (SaveGame) so it can be fully reactivated by a later re-roll. The node's
    // separate ore-rock (ore-style nodes) is hidden the same way.
    void ApplyVanillaDeactivate(FNodeShuffleEntry& Entry, AFGResourceNode* Node, bool& bOutChangedWorld);
    // Reverse of deactivation: un-hide, re-enable collision, drop from the
    // deactivated set, and (via the normal retype path) restore the live node to
    // its assigned resource. Called when a previously-deactivated node is ACTIVE
    // in the current layout. Idempotent.
    void ReactivateVanillaNode(FNodeShuffleEntry& Entry, AFGResourceNode* Node, bool& bOutChangedWorld);
    // Comprehensive respawn (task #3): an ACTIVE non-new entry whose live actor is
    // genuinely missing (never streams in — destroyed in a pre-lossless save, or
    // simply gone) is materialized as a managed node from its recorded location +
    // original resource, via the new-node spawn machinery. Generalizes the old
    // FIX-3 resurrect to ALL active entries with no live node.
    void EnsureMissingVanillaRespawned(FNodeShuffleEntry& Entry, bool& bOutChangedWorld);
    void EnsureNewNodeSpawned(FNodeShuffleEntry& Entry, bool& bOutChangedWorld);
    // Spawn-on-discovery: true if any player is within SpawnRadius of Loc.
    bool IsLocationNearAnyPlayer(const FVector& Loc, float RadiusCm) const;
    // FIX 1: DespawnFarNewNodes removed — materialized new nodes are kept for the
    // session (despawn-back-to-data caused disappearing/only-visual nodes).
    // Wipe-on-reroll: hide every original vanilla node + its rock when it streams
    // in, unless the spot is occupied or reused by the active new layout. Driven
    // by the persistent OriginalNodeRecord so it works across sessions.
    void SuppressOriginalNodes();
    // Build/refresh the persistent record of all original vanilla node locations
    // (used by SuppressOriginalNodes). Captured at roll time from the layout.
    void CaptureOriginalNodeRecord();
    void SettleNewNodesNearPlayers();
    void ReassociateOrphanedExtractors();
    void RefreshScannersAndRadarTowers();
    // Removes one-off resource deposits sitting on shuffled nodes when their
    // resource contradicts the node's assigned one (runs once per session).
    void SweepMismatchedDeposits();

    // ---- helpers ----
    // Eligible = infinite, plain Node type (no geyser/fracking/deposit), solid by
    // default. bIncludeModded admits non-/Game/ resource descriptors (AllMinable
    // etc.). bIncludeLiquid (Phase 2, experimental) also admits RF_LIQUID (oil)
    // nodes — gated on EnableExperimentalFeatures so the stable mod is solid-only.
    static bool IsEligibleVanillaNode(const AFGResourceNode* Node, bool bIncludeModded, bool bIncludeLiquid);
    // True if the node has an extractor or a portable miner on it.
    bool IsNodeOccupiedAnyway(const AFGResourceNode* Node) const;
    // Swaps the rock mesh carried by the node actor itself (vanilla layouts
    // have no paired AFGNodeMeshActor). Visuals are donated by live nodes
    // that natively carry the target resource (version-proof, covers modded
    // ores); the static asset-path table is the fallback. Returns true if a
    // mesh was applied.
    bool ApplyNodeOwnMesh(AFGResourceNode* Node, UClass* ResourceClass, const FNodeShuffleEntry& Entry);
    void CaptureDonorVisuals();

    struct FDonorVisual
    {
        TWeakObjectPtr<UStaticMesh> Mesh;
        TArray<TWeakObjectPtr<UMaterialInterface>> Materials;
        FTransform RelativeTransform;
    };
    TMap<FString, FDonorVisual> DonorVisuals;   // resource class path -> look
    bool bDonorsCaptured = false;

    // Ore-style vanilla nodes carry no mesh of their own: their rock is a
    // separate level actor. Swept once per session by mesh-name + proximity.
    void SweepRockComponents();
    TMap<FGuid, TWeakObjectPtr<UStaticMeshComponent>> SweptRocks; // entry -> rock
    bool bRocksSwept = false;
    float LastRockSweepSeconds = 0.f;          // last (re)sweep time, for rate-limiting
    float LastOrphanSweepSeconds = 0.f;        // last orphan-rock cleanup, for rate-limiting
    TSet<FGuid> RockSearchExhausted;           // entries proven to have no findable rock
    // Hides node-rock meshes left with no node behind them (a node destroyed in
    // an earlier layout then dropped from the pool by a re-roll). World-state
    // based, so it needs no surviving entry. Rate-limited.
    void OrphanRockCleanup();
    // Rock components already explained by OrphanRockCleanup's per-rock reason
    // log, so the reason for each is stated at most once (hidden OR why-not).
    TSet<TWeakObjectPtr<UStaticMeshComponent>> OrphanReasonLogged;
    // Shared rock-mesh name predicate used by SweepRockComponents, OrphanRockCleanup
    // and the diagnostic. Matches the game's node-rock mesh naming families plus the
    // user's NodeShuffle_RockPatterns.json prefixes. ExtraPatterns may be empty.
    static bool IsNodeRockMeshName(const FString& MeshName, const TArray<FString>& ExtraPatterns);
    // Reads NodeShuffle_RockPatterns.json prefixes (best-effort; empty on miss).
    void LoadExtraRockPatterns(TArray<FString>& OutPatterns) const;
    // A non-new entry that still needs its separate rock paired (inactive nodes
    // to hide, or ore-style retypes to re-skin) sits near a player unpaired.
    bool HasNonNewEntryNeedingRock() const;

    // Correct world scale per rock-mesh name, measured once from real vanilla
    // rocks in the level — so spawned new nodes match vanilla size instead of
    // using guessed table scales (which were ~6x too big and got purged).
    void CaptureRockScales();
    TMap<FString, FVector> RockScaleByMesh;
    bool bRockScalesCaptured = false;

    // Catch-all: hide any NodeShuffle-owned mesh larger than a rock (a
    // mis-skinned scenery "shelf"), logging its source for diagnosis.
    void PurgePhantomMeshes();
    AFGNodeMeshActor* FindMeshActorForNode(AFGResourceNodeBase* Node) const;
    AFGResourceNode* FindVanillaNodeByPath(const FString& Path) const;
    static UClass* LoadClassByPath(const FString& Path);
    bool RaycastSettle(FNodeShuffleEntry& Entry, const AActor* IgnoreNode, const AActor* IgnoreMesh) const;
    // FIX A (land-only placement): true if a world point sits inside a streamed-in
    // water volume (ocean/lake/river). Used to reject settle hits on the seafloor:
    // the down-ray hits solid terrain, but if that impact point is underwater the
    // node is invisible to the player. Definitive (volume containment), not a
    // sea-level guess. Returns false when no water is streamed near the point.
    bool IsPointInWater(const FVector& Point) const;
    // FIX A: single raycast that also rejects water. Out params return the grounded
    // location/rotation/water-state. Helper that RaycastSettle's relocation loop
    // drives over offset candidates.
    bool RaycastGroundAt(const FVector& ProbeXY, float StartZ, const AActor* IgnoreNode,
                         const AActor* IgnoreMesh, FVector& OutLoc, FRotator& OutRot,
                         bool& bOutWater) const;
    void DressSpawnedNode(AFGResourceNode* Node, FNodeShuffleEntry& Entry, UClass* ResourceClass);
    void LogLayoutSummary() const;
    // Writes RerollNow=false back to the live config and flushes it to disk so the
    // one-shot "Re-roll Now" toggle fires exactly once per enable. Returns true on
    // success (property found and marked dirty).
    bool ClearRerollNowFlag();
    // Diagnostic: names rock-like meshes near players and why they aren't paired
    // (distance to nearest entry, that entry's active/paired state). Once each.
    void DiagnoseRocksNearPlayers();
    TSet<TWeakObjectPtr<UStaticMeshComponent>> DiagnosedComponents;

    // ---- persisted state ----
    UPROPERTY(SaveGame) int32 SavedSeed = 0;
    UPROPERTY(SaveGame) bool bLayoutGenerated = false;
    UPROPERTY(SaveGame) int32 LayoutVersion = 1;
    UPROPERTY(SaveGame) TArray<FNodeShuffleEntry> Layout;

    // Persistent record of every original vanilla node location, used to suppress
    // (hide node + rock) any original that streams in after a wipe-on-reroll —
    // reliable across sessions, and the catch-all for orphan-rock ghosts. Rebuilt
    // from the layout each roll.
    UPROPERTY(SaveGame) TArray<FNodeShuffleSuppressedOriginal> OriginalNodeRecord;

    // LOSSLESS DEACTIVATION (build lossless-5): paths of vanilla nodes the current
    // layout marks inactive. These nodes are HIDDEN, not destroyed, so they remain
    // in the world and can be reactivated by a later re-roll. Persisted so the hide
    // can be re-applied reliably across sessions and a reactivation knows exactly
    // which previously-hidden nodes to restore. Replaces the old "destroyed forever"
    // model entirely.
    UPROPERTY(SaveGame) TSet<FString> DeactivatedNodePaths;

    // ---- session state ----
    FTimerHandle TickTimerHandle;
    bool bRerollCheckDone = false;
    bool bLoggedDisabled = false;
    bool bDidInitialApply = false;
    bool bDepositSweepDone = false;

    // Live actors for new-node entries (spawned this session, transient).
    UPROPERTY() TMap<FGuid, AFGResourceNode*> SpawnedNodes;
    UPROPERTY() TMap<FGuid, AActor*> SpawnedMeshActors;

    // Cache: vanilla path -> live node, rebuilt when stale.
    TMap<FString, TWeakObjectPtr<AFGResourceNode>> VanillaNodeCache;
    TMap<TWeakObjectPtr<AFGResourceNodeBase>, TWeakObjectPtr<AFGNodeMeshActor>> MeshActorCache;
    bool bMeshActorCacheBuilt = false;
};
