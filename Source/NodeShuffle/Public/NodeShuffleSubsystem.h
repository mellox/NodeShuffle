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

    // FIX C (overlap guard): how many times this new node's location has been
    // spiral-nudged off an overlap. Persisted so retries don't restart each load.
    // When it exceeds the cap the entry is resolved (deactivated) instead of being
    // re-checked — and re-logged — every single tick forever.
    UPROPERTY(SaveGame) uint8 OverlapNudges = 0;

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
    // correct-visual-6: true when this record's node is MODDED-origin (original resource class path
    // not under /Game/). Such nodes are LEFT NATIVE and must NEVER be suppressed/hidden — set at
    // CaptureOriginalNodeRecord time so SuppressOriginalNodes can skip them in BOTH hide loops.
    UPROPERTY(SaveGame) bool bModdedOrigin = false;
};

// (2026-06-15-engine-reskin-1) The old FNodeShuffleDonorRecord + PersistedDonors
// SaveGame store was part of the deleted donor-CAPTURE system. It is gone; any old
// saved donor data is simply ignored on load (no migration needed — the property no
// longer exists, and visuals are now reapplied from authored data every session).

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
    virtual void PostLoadGame_Implementation(int32 saveVersion, int32 gameVersion) override;
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
    // redesign-1: before a re-roll, UN-HIDE every previously-suppressed original node (and its
    // mesh actor) that is streamed in, so the world returns to its pristine state before the new
    // layout re-hides per the new roll. Nothing was ever destroyed (whole-actor hide is reversible),
    // so this is a clean restore. The reroll pool itself is rebuilt from the saved Layout's stored
    // ORIGINAL resources (streaming-independent), not from a live rescan.
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
    // redesign-3 BUG B: ONE-TIME post-load reconciliation. Spawned nodes are now ANodeShuffleResourceNode
    // with a UPROPERTY(SaveGame) EntryGuid that survives reload (Tags do NOT). Iterate the subclass, read
    // each saved EntryGuid, repopulate SpawnedNodes[guid] so EnsureNewNodeSpawned's SpawnedNodes.Find
    // guard skips re-spawning. Also pins occupied restored nodes. Includes the VERIFY-FIRST count.
    void AdoptRestoredSpawnedNodes();
    bool bAdoptedRestoredNodes = false;
    // redesign-3 BUG C: originals already deregistered from the scanner this session (path set, so we
    // call RemoveResourceNodeScan_Local/UpdateNodeRepresentation once per original, not every pass) + a
    // running count for the log evidence (redesign-2's calls were silent).
    TSet<FString> ScannerDeregistered;
    int32 ScannerDeregisterCount = 0;
    // redesign-1: spawn one of OUR relocated nodes (the only kind of active node besides the
    // untouched occupied originals). Handles solid (authored rock or quartz placeholder) and oil.
    void EnsureNewNodeSpawned(FNodeShuffleEntry& Entry, bool& bOutChangedWorld);
    // redesign-3b BLOCKER FIX: the "Resource"-profile UseBox a spawned node needs for interaction
    // (look-at, build-gun, miner placement, hand-mine) is a runtime NewObject component and is NOT
    // serialized, so a restored/adopted node loses it on reload -> non-interactable. Idempotent helper
    // that creates the box if the node has none — called on BOTH spawn AND adopt/early-return paths.
    void EnsureNodeUseBox(AFGResourceNode* Node);
    // redesign-9 (MK1 RESOURCE-SNAP DETECTION). One-shot SNAPDIAG: when a spawned node AND a nearby
    // VANILLA node are both streamed in, log the FULL collision/component state of EACH so the log names
    // the EXACT vanilla-vs-ours delta the extractor hologram cares about. Friend access reads mBoxComponent.
    void DiagnoseSnapState();
    bool bSnapDiagLogged = false;
    // Helper: dump one node's components/collision for SNAPDIAG.
    void LogNodeSnapState(AFGResourceNodeBase* Node, const TCHAR* Label) const;
    // redesign-12 VALIDDIAG (diagnostics only): collision (r10) + manager registration (r11) are BOTH
    // ruled out — the Mk1 hologram FINDS our node but REJECTS it at VALIDATION. One-shot side-by-side log
    // of OURS vs a nearby VANILLA node on the validation-relevant props/methods the extractor hologram's
    // CanOccupyResource/IsAllowedOnResource path reads, so the next log names the exact differing gate.
    void DiagnoseValidationGate();
    bool bValidDiagLogged = false;
    void LogNodeValidationState(AFGResourceNode* Node, const TCHAR* Label) const;
    // redesign-11 (REGISTER NODES WITH THE RESOURCE-NODE MANAGER). The Miner Mk1 extractor hologram finds
    // the node to snap to via AFGResourceNodeManager::GetClosestNode over the manager's mResourceNodes
    // list. Our runtime-spawned nodes never auto-join it (it's built from level nodes at world init), so
    // Mk1 snap fails even with a vanilla collision byte-match. Register each spawned node into mResourceNodes
    // at spawn AND on adopt-after-reload (the list is runtime, not save-persisted). Friend access. Idempotent.
    // Resolve the live AFGResourceNodeManager instance by actor iteration (its static Get(UWorld*) is NOT
    // dll-exported — LNK2019 if called). One manager per world.
    class AFGResourceNodeManager* GetNodeManager() const;
    void RegisterNodeWithManager(AFGResourceNode* Node);
    // redesign-11 SECONDARY: when we hide an original (SuppressOriginalNodes), remove it from mResourceNodes
    // so the player can't place a Mk1 on an invisible ghost original. Only ever removes originals we hid.
    void DeregisterNodeFromManager(AFGResourceNodeBase* Node);
    // REGDIAG: one-shot — log the manager's mResourceNodes count + whether our node is Contains()'d, so the
    // next test confirms our nodes joined the manager (mirrors how SNAPDIAG confirmed the collision match).
    bool bRegDiagLogged = false;
    // Spawn-on-discovery: true if any player is within SpawnRadius of Loc.
    bool IsLocationNearAnyPlayer(const FVector& Loc, float RadiusCm) const;
    // redesign-1 (Hide & Replace): hide EVERY unoccupied original node (vanilla AND modded) + its
    // rock whenever it streams in. This IS the new core — all unoccupied originals are gone, their
    // resources live on as our spawned relocated nodes. Occupied/pinned originals are never touched.
    // Driven by the persistent OriginalNodeRecord so it works across sessions and stream-ins.
    void SuppressOriginalNodes();
    // Build/refresh the persistent record of EVERY unoccupied original node location (vanilla AND
    // modded) so SuppressOriginalNodes can hide them. Captured at roll time from the layout.
    void CaptureOriginalNodeRecord();
    void SettleNewNodesNearPlayers();
    void ReassociateOrphanedExtractors();
    void RefreshScannersAndRadarTowers();
    // Removes one-off resource deposits sitting on shuffled nodes when their
    // resource contradicts the node's assigned one (runs once per session).
    void SweepMismatchedDeposits();

    // ---- helpers ----
    // Property-based eligibility (no class-name/form-list hardcoding). Eligible =
    // a genuine AFGResourceNode with a UFGResourceDescriptor resource, plain Node
    // type (GetResourceNodeType() == EResourceNodeType::Node — no geyser/fracking/
    // deposit), not skeletal/transient. bIncludeModded admits non-/Game/ resource
    // descriptors (AllMinable etc.). bIncludeLiquid is the EXPERIMENTAL-form gate:
    // solid is always allowed; liquid (oil) AND gas (e.g. lithium, Desc_OreLithium
    // form=RF_GAS) join only when EnableExperimentalFeatures is on (both need
    // special extractors). Nothing is special-cased by mod/class name.
    static bool IsEligibleVanillaNode(const AFGResourceNode* Node, bool bIncludeModded, bool bIncludeLiquid);
    // Same gate, but reports WHY a node was rejected (for the once-per-class
    // modded-eligibility diagnostic). Accepts genuinely-functional modded nodes
    // (lithium's BP_ResourdeNode_Alkali_C, AllMinable item-style nodes) by driving
    // entirely off node PROPERTIES: relaxes purity/amount for non-/Game/ resources
    // (purity normalized at roll time) while keeping the principled junk exclusions
    // (skeletal, transient, non-resource-descriptor, deposit, geyser/fracking).
    static bool IsEligibleVanillaNodeReason(const AFGResourceNode* Node, bool bIncludeModded,
                                            bool bIncludeLiquid, const TCHAR*& OutReason);
    // FIX 4: once-per-node-class diagnostic naming why each MODDED node class is or
    // isn't shuffled. Turns a silent "lithium never shuffles" into a log line that
    // states the exact gate. Runs at roll time.
    void DiagnoseModdedNodeEligibility(bool bIncludeModded, bool bIncludeLiquid) const;
    TSet<FString> ModdedEligibilityLogged;
    // redesign-5 SECONDARY: distinct node-class|resource-class keys already logged by the UPSTREAM-SCAN
    // diagnostic at the top of the initial-roll node loop (so each class is reported once, not per node).
    TSet<FString> UpstreamScanLogged;
    // redesign-6 FIX 2: one-shot diagnostic — walk the full class hierarchy of every distinct actor whose
    // class name/path contains "esc_" or "AllMinable", so we learn their REAL base type (the analyst
    // proved esc_ nodes are NOT AFGResourceNode; this names what they actually are). Runs once per roll.
    void DiagnoseEscClassHierarchy();
    // True if the node has an extractor or a portable miner on it.
    bool IsNodeOccupiedAnyway(const AFGResourceNode* Node) const;
    // FIX B: true if this actor is a fracking core or satellite (by node TYPE, not
    // mesh name) — those wells (core + satellites + activator) are left EXACTLY as
    // vanilla: never deactivated, retyped, relocated, re-skinned, or rock-hidden.
    static bool IsFrackingActor(const AActor* Actor);

    // redesign-1 (Hide & Replace). The 21-build reskin saga is GONE. We no longer touch any
    // existing node's rock: every UNOCCUPIED original node is hidden whole-actor and its resource
    // is re-spawned as OUR OWN node at a relocated location. This is the visual for those spawned
    // nodes — one mod-owned AFGNodeMeshActor at the node's OWN transform (no significance fight, no
    // instanced wall, no offset). Vanilla resource -> authored ResourceNode_<X>_01 mesh+materials;
    // MODDED resource (no authored entry) -> the quartz placeholder (Desc_RawQuartz_C visual).
    // Strong-ref'd in SpawnedMeshActors (decay-proof). Liquids draw a decal instead (no rock).
    void SpawnVisualRockForNode(AFGResourceNode* Node, UClass* ResourceClass, const FGuid& EntryGuid);
    // Resolve & cache a resource's authored node MESH (the big node rock ResourceNode_<X>_01, NOT
    // the deposit outcrop). Null if the resource has no table entry (-> caller uses the quartz
    // placeholder for modded resources).
    UStaticMesh* ResolveNodeMesh(UClass* ResourceClass);
    TMap<FString, TWeakObjectPtr<UStaticMesh>> NodeMeshCache;
    // Resolve & cache a resource's authored PER-SLOT node materials from the table. Empty when the
    // resource has no table entry.
    const TArray<TWeakObjectPtr<UMaterialInterface>>* ResolveNodeMaterials(UClass* ResourceClass);
    TMap<FString, TArray<TWeakObjectPtr<UMaterialInterface>>> NodeMaterialCache;
    // The quartz placeholder visual (mesh + materials) used for any spawned node whose resource has
    // no authored table entry (modded resources: esc_/lithium/etc.). Resolved from Desc_RawQuartz_C.
    UStaticMesh* GetQuartzPlaceholderMesh();
    const TArray<TWeakObjectPtr<UMaterialInterface>>* GetQuartzPlaceholderMaterials();
    // redesign-1: place the starter node set (config) near the captured player-start on the FIRST
    // roll of a brand-new game. Drawn from the pool when possible. Spawned as normal new entries.
    void AppendStarterNodes(TArray<FNodeShuffleEntry>& NewLayout, FRandomStream& Rng,
                            const TArray<FVector>& AvoidLocations);
    // Re-applies the native visual (mesh-actor refresh or oil/gas decal) a node draws
    // for its CURRENT descriptor, by invoking the game's own OnRep_ResourceClassOverride via
    // ProcessEvent. Handles liquids (oil decal, no rock) and the no-mesh-actor case.
    void RebuildNodeNativeVisual(AFGResourceNode* Node);
    // Per-pass spawned-visual coverage counters (reset + logged each ApplyLayout).
    int32 SpawnedRockVanilla = 0;
    int32 SpawnedRockQuartz = 0;
    int32 SpawnedRockLiquid = 0;
    // redesign-6 FIX 1: limit the per-node RENDERDIAG runtime-state log to the first N spawned + first N
    // adopted nodes (so we get the truth without spamming thousands of lines). Session counters.
    int32 RenderDiagSpawnLogged = 0;
    int32 RenderDiagAdoptLogged = 0;
    static constexpr int32 RenderDiagMax = 10;
    // redesign-6 FIX 2: one-shot esc_/AllMinable class-hierarchy diagnostic done flag.
    bool bEscClassHierarchyLogged = false;

    float LastOrphanSweepSeconds = 0.f;        // last orphan-rock cleanup, for rate-limiting
    // redesign-1 backstop: hides any node-rock mesh left with no node behind it (a rare
    // actor-independent rock at a suppressed original spot). World-state based, rate-limited.
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

    AFGNodeMeshActor* FindMeshActorForNode(AFGResourceNodeBase* Node) const;
    // Returns the live original node for a path as AFGResourceNode (null for Base-only esc_ originals).
    AFGResourceNode* FindVanillaNodeByPath(const FString& Path) const;
    // redesign-6 FIX 2: the live original as AFGResourceNodeBase (covers esc_ Base-only originals too).
    AFGResourceNodeBase* FindOriginalBaseByPath(const FString& Path) const;
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
    // Current layout-FORMAT version. Bump this whenever FNodeShuffleEntry / the saved layout semantics
    // change, and add a migration case in PostLoadGame_Implementation. Saves stamp LayoutVersion with this
    // at roll time; on load, an older saved value triggers migration (see PostLoadGame_Implementation).
    static constexpr int32 CurrentLayoutVersion = 2;
    UPROPERTY(SaveGame) int32 LayoutVersion = 1;
    UPROPERTY(SaveGame) TArray<FNodeShuffleEntry> Layout;

    // redesign-1: persistent record of EVERY unoccupied original node location (vanilla AND
    // modded), used by SuppressOriginalNodes to hide each original whole-actor whenever it streams
    // in. Reliable across sessions; the catch-all for orphan rocks. Rebuilt from the layout each roll.
    UPROPERTY(SaveGame) TArray<FNodeShuffleSuppressedOriginal> OriginalNodeRecord;

    // redesign-1: STARTER NODES. Captured player-start world location (the player pawn's position at
    // the earliest tick it exists on a brand-new game) and whether starters have already been placed.
    // Both SaveGame so starters are placed exactly once per save and never on an existing save.
    UPROPERTY(SaveGame) bool bStarterNodesPlaced = false;
    UPROPERTY(SaveGame) bool bPlayerStartCaptured = false;
    UPROPERTY(SaveGame) FVector PlayerStartLocation = FVector::ZeroVector;

    // ---- session state ----
    FTimerHandle TickTimerHandle;
    bool bRerollCheckDone = false;
    bool bLoggedDisabled = false;
    bool bDidInitialApply = false;
    bool bDepositSweepDone = false;

    // Live nodes for new-node entries (spawned this session + adopted-on-load). redesign-5: the visual
    // rock is now a RockMesh subobject OF each node (no separate rock actors — that machinery is gone).
    UPROPERTY() TMap<FGuid, AFGResourceNode*> SpawnedNodes;

    // Cache: original-node path -> live BASE node, rebuilt when stale. redesign-6 FIX 2: broadened from
    // AFGResourceNode to AFGResourceNodeBase so esc_ (Base-only) originals can be found + hidden too.
    TMap<FString, TWeakObjectPtr<AFGResourceNodeBase>> VanillaNodeCache;
    // Node -> its own AFGNodeMeshActor (engine back/forward link), rebuilt every ApplyLayout pass.
    // Used by SuppressOriginalNodes to hide an original node's paired mesh actor on stream-in.
    TMap<TWeakObjectPtr<AFGResourceNodeBase>, TWeakObjectPtr<AFGNodeMeshActor>> MeshActorCache;
    void RebuildMeshActorCache();
};
