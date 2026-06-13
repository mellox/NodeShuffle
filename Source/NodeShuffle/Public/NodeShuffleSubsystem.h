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
    bool IsWorldReadyForRoll() const;
    void RollLayout(int32 Seed, bool bIsReroll);
    void GenerateNewLocations(FRandomStream& Rng, const TArray<FVector>& VanillaLocations,
                              int32 Count, TArray<FVector>& OutLocations) const;
    bool ReadCustomLocationsJson(TArray<FVector>& OutLocations) const;
    void WriteGeneratedLocationsJson(const TArray<FVector>& Locations) const;

    // ---- apply ----
    void ApplyLayout();
    void ApplyVanillaRetype(FNodeShuffleEntry& Entry, AFGResourceNode* Node, bool& bOutChangedWorld);
    void ApplyVanillaDeactivate(FNodeShuffleEntry& Entry, AFGResourceNode* Node, bool& bOutChangedWorld);
    void EnsureNewNodeSpawned(FNodeShuffleEntry& Entry, bool& bOutChangedWorld);
    void SettleNewNodesNearPlayers();
    void ReassociateOrphanedExtractors();
    void RefreshScannersAndRadarTowers();
    // Removes one-off resource deposits sitting on shuffled nodes when their
    // resource contradicts the node's assigned one (runs once per session).
    void SweepMismatchedDeposits();

    // ---- helpers ----
    // Eligible = solid, infinite, plain Node type (no oil/geyser/fracking/deposit).
    // bIncludeModded admits non-/Game/ resource descriptors (AllMinable etc.).
    static bool IsEligibleVanillaNode(const AFGResourceNode* Node, bool bIncludeModded);
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
    void DressSpawnedNode(AFGResourceNode* Node, FNodeShuffleEntry& Entry, UClass* ResourceClass);
    void LogLayoutSummary() const;
    // Diagnostic: names rock-like meshes near players and why they aren't paired
    // (distance to nearest entry, that entry's active/paired state). Once each.
    void DiagnoseRocksNearPlayers();
    TSet<TWeakObjectPtr<UStaticMeshComponent>> DiagnosedComponents;

    // ---- persisted state ----
    UPROPERTY(SaveGame) int32 SavedSeed = 0;
    UPROPERTY(SaveGame) bool bLayoutGenerated = false;
    UPROPERTY(SaveGame) int32 LayoutVersion = 1;
    UPROPERTY(SaveGame) TArray<FNodeShuffleEntry> Layout;

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
