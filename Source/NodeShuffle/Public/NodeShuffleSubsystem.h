#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ModSubsystem.h"
#include "FGSaveInterface.h"
#include "Resources/FGResourceNode.h"
#include "NodeShuffle.h" // ns-h1b-notice: FNodeShufflePendingEntry, held by value in a member below
#include "NodeShuffleSubsystem.generated.h"

class AFGNodeMeshActor;
class AFGResourceScanner;
class UFGResourceDescriptor; // scanregen-1: TSubclassOf<> member below only needs the forward decl

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

    // cave-nodes-1/2: this entry's location is a discovered CAVERN FLOOR cell. Its settle (and any
    // nudge probes) use a SHORT local trace instead of the 200 m top-down ray — the long ray would
    // hit the cave ROOF and strand the node on the surface. Set when a deal draw (roll, relocation,
    // water-locked redeal) randomly picks a cave cell at the natural share; never by a quota fill.
    UPROPERTY(SaveGame) bool bUnderground = false;

    // rehide-1: the TRUE live location this entry's original was captured at (before any relocation),
    // stamped at every live-capture site (initial roll, re-scan augment, experimental augment) and
    // carried through re-roll rebuilds (opportunistically backfilled there too). ZeroVector = unset
    // (pre-rehide-1 saves). Feeds Rec.TrueLocation at the Hide & Replace conversion so a runtime-
    // foreign twin can be re-matched by location once its record's VanillaNodePath goes stale (spawner
    // mods re-create their nodes with a fresh auto-numbered id every process boot).
    UPROPERTY(SaveGame) FVector OriginalTrueLocation = FVector::ZeroVector;
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
    // rehide-1: the durable cross-session anchor for location-based re-matching once VanillaNodePath
    // goes stale (runtime-foreign spawner nodes get a fresh auto-numbered id every process boot, so
    // path resolution alone dies at every restart). ZeroVector = unset (legacy sentinel) — falls back
    // to Location, which is TRUE at initial capture and STALE (the previous relocated dest) after a
    // re-roll rebuild; see SuppressOriginalNodes/TryRematchStaleRecord in NodeShuffleSubsystem.cpp.
    UPROPERTY(SaveGame) FVector TrueLocation = FVector::ZeroVector;
    // correct-visual-6: true when this record's node is MODDED-origin (original resource class path
    // not under /Game/). Such nodes are LEFT NATIVE and must NEVER be suppressed/hidden — set inside
    // RollLayout's Hide & Replace conversion (NodeShuffleSubsystem.cpp, the ONLY place OriginalNodeRecord
    // is built; P5: the dead CaptureOriginalNodeRecord() this comment used to name has been removed) so
    // SuppressOriginalNodes can skip them in BOTH hide loops.
    UPROPERTY(SaveGame) bool bModdedOrigin = false;
};

// (2026-06-15-engine-reskin-1) The old FNodeShuffleDonorRecord + PersistedDonors
// SaveGame store was part of the deleted donor-CAPTURE system. It is gone; any old
// saved donor data is simply ignored on load (no migration needed — the property no
// longer exists, and visuals are now reapplied from authored data every session).

// playtest-fixes-1 (modded-descriptor visuals): one visual captured from an ORIGINAL node's own
// look — NOT from neighbors (the deleted neighbor-proximity capture mispaired). Sources, in order:
//   1. the node's PAIRED AFGNodeMeshActor (MeshActorCache engine-link pairing) — vanilla-class
//      modded-resource nodes (RefinedPower thorium, bamrenew lead) whose look lives on the level's
//      mesh actor;
//   2. dirtdress-1: a static-mesh component OWNED BY (or attached to) the node actor itself —
//      self-rendering modded node BPs (FicsitFarming dirt mounds, KLib crystals) that have no
//      engine mesh-actor links, so source 1 never fires for them (the 98-quartz-dirt-nodes bug).
//      An original's own mesh IS its look by definition, so no rock-name pattern gate applies.
// Keyed by resource class SHORT name; only real UFGResourceDescriptor resources are captured, and
// an own-mesh candidate identical to the quartz placeholder is never stored (esc_ item nodes keep
// their dirty-quartz identity by design). Persisted so visuals resolve by path next session even
// before any original streams in.
USTRUCT()
struct FNodeShuffleCapturedVisual
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) FString ResourceClassName;      // short class name, e.g. Desc_RP_Thorium_C
    UPROPERTY(SaveGame) FString MeshPath;
    UPROPERTY(SaveGame) TArray<FString> MaterialPaths;  // per-slot, slot order
    UPROPERTY(SaveGame) FVector MeshScale = FVector(1.0f, 1.0f, 1.0f);
};

// ns-review-g G1 (Packet G, CRITICAL fix): one (node class, resource class, form) group derived from
// the ROLLED LAYOUT, not from live/spawned actors -- see BuildManagedNodeGroupsFromLayout's own comment
// in NodeShuffleSubsystem.cpp for the full "why". Plain (not reflected -- this never crosses SaveGame or
// Blueprint) so it can be returned across the public/private header boundary without pulling in
// NodeShuffleExtractorDiscovery.h (a Private header) from this Public one.
struct FNodeShuffleManagedGroup
{
    UClass* NodeClass = nullptr;
    UClass* ResourceClass = nullptr;
    int32 Form = -1;
    int32 Count = 0; // number of ACTIVE layout entries in this group -- loaded or not
};

// Packet H1 (ns-wells-h1): ONE SATELLITE of a managed resource well. Purity is recorded and NEVER
// written back -- H0 measured that vanilla wells MIX purities across their satellites (design §Q2),
// so there is no shared purity to normalise and normalising one would be a silent balance change.
// The field exists purely so the log (and H2) can state what the vanilla purity vector was without
// re-reading a possibly-unstreamed actor.
USTRUCT()
struct FNodeShuffleWellSatellite
{
    GENERATED_BODY()

    // GetPathName() of the LEVEL satellite actor. Wells are never spawned or moved by H1, so this is
    // a level-actor path and is stable across loads -- the same identity idiom FNodeShuffleEntry uses
    // for a vanilla original.
    UPROPERTY(SaveGame) FString SatellitePath;

    // RECORD ONLY -- read at roll time, never applied. See the struct comment.
    UPROPERTY(SaveGame) TEnumAsByte<EResourcePurity> OriginalPurity = RP_MAX;
};

// Packet H1: one resource well (fracking core + its satellites) as a unit of the per-save layout.
// Deliberately a SEPARATE array from Layout rather than an FNodeShuffleEntry: a well is one thing
// with N members, and FNodeShuffleEntry is one-node-shaped (design §2.2).
//
// H1 scope, stated so the next reader does not look for the missing half: the well is RETYPED IN
// PLACE. There is no location, no rotation, no group yaw, no offsets and no satellite spawn state
// here, because H1 never moves a well -- all of that arrives with H2.
USTRUCT()
struct FNodeShuffleWellEntry
{
    GENERATED_BODY()

    // GetPathName() of the LEVEL core actor -- this entry's identity. Also the deterministic SORT KEY
    // for dealing (see RollWellLayout): dealing in actor-iteration order would make the same seed
    // produce different worlds depending on what had streamed in, which is the exact class of bug
    // design §Q3a's "deterministic" rule exists to prevent.
    UPROPERTY(SaveGame) FString CorePath;

    // Captured from the LIVE actors at roll time -- never hardcoded /Game/FactoryGame/... well paths
    // (design §2.2). Consumed by BuildManagedNodeGroupsFromLayout for the SF+ auto-allow extension.
    UPROPERTY(SaveGame) FString CoreNodeClassPath;
    UPROPERTY(SaveGame) FString SatelliteNodeClassPath;

    // The AUTHORED resource (AFGResourceNodeBase::mResourceClass, via GetResourceClassOriginal()),
    // never the effective one -- on a re-roll the core already carries OUR override, and reading the
    // effective class would feed our own previous output back into the deck. That is the
    // "output-derived input is a loop" failure mode (memory: lessons-output-derived-input-is-a-loop).
    UPROPERTY(SaveGame) FString OriginalResourceClassPath;

    // What this well was dealt. Equal to OriginalResourceClassPath for a pinned well.
    UPROPERTY(SaveGame) FString AssignedResourceClassPath;

    UPROPERTY(SaveGame) TArray<FNodeShuffleWellSatellite> Satellites;

    // Someone has already built on this well (activator on the core, extractor on ANY satellite, or
    // either reporting IsOccupied()). Mirrors FNodeShuffleEntry::bPinned: a well a player has built on
    // is not ours to change. Set at roll time AND re-checked at apply time.
    UPROPERTY(SaveGame) bool bPinned = false;

    // True only when NodeShuffle actually owns this well's resource this save. False for a pinned
    // well and for one whose resource could not be resolved -- both fail SAFE to "left vanilla".
    // Also the gate on the SF+ auto-allow contribution: we only claim to manage what we retype.
    UPROPERTY(SaveGame) bool bManaged = false;
};

// Server-side brain of NodeShuffle.
//
// Lifecycle per session:
//   BeginPlay -> repeating timer -> Tick():
//     1. (first tick) roll the layout if the save has none, or re-roll if the
//        user turned on the Re-roll Now toggle.
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
    virtual void PreSaveGame_Implementation(int32 saveVersion, int32 gameVersion) override;
    virtual void PostSaveGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void PreLoadGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void PostLoadGame_Implementation(int32 saveVersion, int32 gameVersion) override;
    virtual void GatherDependencies_Implementation(TArray<UObject*>& out_dependentObjects) override {}

    // playtest-fixes-1: `NodeShuffle.Here` console command (registered by the module). Logs the
    // player's exact position plus a census of everything NodeShuffle-related within CensusRadius:
    // layout entries (state + distance), streamed originals (hidden? radioactive? emitter live?),
    // and the water/depth test at the player's feet. Log-only; safe anywhere.
    void LogHereCensus() const;

    // cave-nodes-1: `NodeShuffle.SeedHere` console command. Plants a manual cave seed at the player's
    // feet — for roofed spots vanilla never put a node under (rock bridges, shelves, side tunnels).
    // Same guarantees as automatic seeds: the player standing there proves reachability, the roof
    // check keeps surface spots out (a buildable roof is rejected), and the floor is re-sampled at
    // the cell center. Counts toward the underground placement quota.
    void SeedCaveCellAtPlayer();

    // ns-review-g G1 (Packet G, CRITICAL fix — mechanism A from the review). Builds the auto-allow
    // pass's managed-node census from the ROLLED LAYOUT rather than from live/spawned actors: NodeShuffle
    // spawns relocated nodes lazily, only within SpawnRadiusCm (~600 m) of a player ("far nodes stay as
    // data until explored" -- see EnsureNewNodeSpawned), so a live-actor census only sees whatever
    // happened to stream in near the load point -- non-deterministic, and unstable across sessions/
    // vantage points. Every active FNodeShuffleEntry is dealt at roll time regardless of streaming state,
    // so this is complete and deterministic: the result depends only on the rolled layout, never on
    // where the player is standing. Public so FNodeShuffleModule::RunAutoAllowExtractorsIfEnabled (a free
    // function in NodeShuffleAutoAllowExtractors.cpp) can call it via TActorIterator, the same idiom
    // NodeShuffle.Here already uses to reach the subsystem from a console-command-style entry point.
    // OutTotalActiveEntries / OutUnresolvedEntries: the residual, much-narrower honesty log the review
    // asked to keep (ns-review-g G1's "(C) honesty log" recommendation) -- an active entry whose
    // resource or node class fails to resolve THIS pass contributes no group and is not silently
    // dropped from the log, even though the census no longer depends on player position or streaming.
    void BuildManagedNodeGroupsFromLayout(TArray<FNodeShuffleManagedGroup>& OutGroups,
        int32& OutTotalActiveEntries, int32& OutUnresolvedEntries) const;

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
    // playtest-fixes-3: non-const — it now stores the computed deal box (SaveGame) so the water-locked
    // redeal can draw RANDOM map-wide candidates from the same box later, and consults/updates the
    // learned water grid.
    // cave-nodes-2: when OutUndergroundIndices is non-null (experimental placement on), each accepted
    // location had a natural-share chance of being a CAVE cell instead of a surface box draw; the
    // indices of cave picks are reported so the caller stamps Entry.bUnderground.
    void GenerateNewLocations(FRandomStream& Rng, const TArray<FVector>& VanillaLocations,
                              const TArray<FVector>& AvoidLocations,
                              int32 Count, TArray<FVector>& OutLocations,
                              TSet<int32>* OutUndergroundIndices = nullptr);
    bool ReadCustomLocationsJson(TArray<FVector>& OutLocations) const;
    void WriteGeneratedLocationsJson(const TArray<FVector>& Locations) const;

    // ---- apply ----
    void ApplyLayout();
    // redesign-3 BUG B: ONE-TIME post-load reconciliation. Spawned nodes are now ANodeShuffleResourceNode
    // with a UPROPERTY(SaveGame) EntryGuid that survives reload (Tags do NOT). Iterate the subclass, read
    // each saved EntryGuid, repopulate SpawnedNodes[guid] so EnsureNewNodeSpawned's SpawnedNodes.Find
    // guard skips re-spawning. Also pins occupied restored nodes. Includes the VERIFY-FIRST count.
    // real-class redesign: modded-origin nodes whose native visual we've already rebuilt this session
    // (keyed by entry guid), so the per-node ProcessEvent rebuild fires once, not every tick (W2).
    TSet<FGuid> ModdedVisualRebuilt;
    void AdoptRestoredSpawnedNodes();
    // coexist-veto-1 FIX A: REGISTRY-ONLY pre-pass run in BeginPlay, before the veto arms. Registers
    // restored spawned-node actors into the module's managed-node registry using the SAME identity
    // predicates as AdoptRestoredSpawnedNodes (legacy guid / runtime+location+resource), so an armed
    // KBFL destroyer's initial sweep — which runs at OnWorldBeginPlay, ~5 s before the first
    // RefreshTick adopts — cannot destroy restored nodes through an empty registry. Touches NOTHING
    // but the registry (no SpawnedNodes writes, no adopt logic, no entry mutations).
    void PreRegisterRestoredNodesForVeto();
    // real-class redesign: shared per-node adopt finalize (resource-complete, gates, register, attach
    // component, pin). A member function so it keeps the subsystem's Friend access to AFGResourceNode
    // internals. Returns true when it newly pins the entry as occupied.
    bool FinalizeAdoptedNode(class AFGResourceNode* Node, int32 EntryIdx);
    // real-class redesign: true if the node renders its OWN visual (self-rendering mesh component or a live
    // linked engine mesh actor), so we should keep its native look instead of dressing our fallback rock.
    // Lithium's Alkali node -> true; AllMinable item-nodes -> false (they get the fallback rock).
    bool NodeHasOwnVisual(class AActor* Node, class UStaticMeshComponent* ExcludeRock) const;
    // visfix-1 (user report: "coal node with quartz visual"): the deal deck assigns resources across
    // ALL entries, so a modded-CLASS node (AllMinable Res_*2_C, whose native mesh is a quartz
    // look-alike) can carry a VANILLA resource. When the ASSIGNED resource has a look we can render
    // (authored table row or captured visual), that look must win: hide the native mesh and dress our
    // rock. Native visuals only win for resources we cannot dress (lithium, uncaptured modded ores,
    // esc_ item resources keeping their dirty-quartz identity).
    bool ResourceHasAuthoredLook(UClass* ResourceClass);
    void HideNativeNodeMesh(class AFGResourceNode* Node, class UStaticMeshComponent* ExcludeRock);
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
    // rehide-1: called only from SuppressOriginalNodes' path-miss branch, when a stale record's
    // VanillaNodePath no longer resolves (a spawner mod re-created the node with a fresh id since last
    // process boot). Recovers identity from location (Rec.TrueLocation, or Rec.Location for legacy
    // unstamped records) + class + resource against CandidatePool (built once per pass by
    // BuildRematchCandidatePool). On a match: rebinds Rec's VanillaNodePath/TrueLocation AND the
    // originating layout entry's VanillaNodePath/OriginalTrueLocation (found by the OLD path), marks
    // the winning candidate in BoundCandidates (so a later record this pass can't claim it too), logs
    // once (REHIDE), and returns the live actor so the caller falls through the UNCHANGED hide funnel.
    // Returns null (zero mutation) when nothing matches within AdoptMatchRadiusCm — fail-closed: the
    // record stays stale and is retried next pass. Adds ZERO hide/suppress logic of its own.
    AFGResourceNodeBase* TryRematchStaleRecord(FNodeShuffleSuppressedOriginal& Rec,
        const TArray<AFGResourceNodeBase*>& CandidatePool, TSet<AFGResourceNodeBase*>& BoundCandidates);
    // rehide-1: candidate pool for TryRematchStaleRecord — VanillaNodeCache filtered down to the
    // actor-kind pre-gates that make a re-match safe (design §3.1/§3.4): valid, runtime-only
    // (!IsNetStartupActor — a level actor can NEVER be re-matched), non-transient (parity with capture
    // eligibility, :5822-5827), not one of our own nodes, not already managed/adopted, not fracking.
    // Built lazily by the caller: once per pass, only once a record's path misses.
    void BuildRematchCandidatePool(TArray<AFGResourceNodeBase*>& OutPool) const;
    // rehide-1: stale records already reported "no-match" this session — throttles the no-match REHIDE
    // log to once per record (the record itself still retries silently every pass, covering late/lazy
    // spawners; see TryRematchStaleRecord).
    TSet<FString> RematchNoMatchLogged;
    // P5 (addenda item 3): the standalone CaptureOriginalNodeRecord() declaration that used to sit
    // here was dead code (no callers) and has been removed. OriginalNodeRecord — the persistent record
    // of every unoccupied original node location (vanilla AND modded) that SuppressOriginalNodes hides
    // — is built inline inside RollLayout's Hide & Replace conversion instead.
    void SettleNewNodesNearPlayers();
    void ReassociateOrphanedExtractors();
    // knowledge-1 item 2: extractors whose resource binding this session already healed/refreshed
    // (bounded once-per-extractor logging + no re-heal churn). Weak keys — dead actors drop out.
    TSet<TWeakObjectPtr<const AActor>> ExtractorsHealed;
    // knowledge-2 item 3: extractors already truth-dumped this session (diag-gated once-per-extractor
    // ground-truth line: bound object validity, resolved resource, node-at-location match).
    TSet<TWeakObjectPtr<const AActor>> ExtractorsDumped;
    // knowledge-1 item 1: on authority, once per load after the layout is applied, register the
    // DISTINCT modded resources the shuffle actively manages with the game's scanner-unlock list
    // (AFGUnlockSubsystem::UnlockScannableResource — SaveGame + Replicated, so it persists). This is
    // what lets resource scanners AND mods that gate extractors on scanner knowledge (SF+'s Modular
    // Miner checks GetScannableResources().Contains via KLib's HasInformationAboutOre) recognize
    // shuffled modded resources whose own unlock schematics never ran in this save. Vanilla
    // resources are NEVER touched (their scanner unlocks are progression). Config-gated
    // (UnlockModdedKnowledge, default ON); idempotent per load (Contains gate).
    // knowledge-2 item 2 (CRASH-PROOF ORDERING): scanner knowledge now IMPLIES KAPI MinerInfo. When
    // KAPI is present, ProvideKAPIMinerInfo runs FIRST and only ores with an mMinerMapping entry
    // (pre-existing or freshly provided) are unlocked — KLib's AKLMMBuildableMiner::BeginPlay
    // fgcheckf's a valid description for any placeable ore, so unlocking without one ARMS a crash
    // (live: esc_CateriumIngot_C). Returns false only when the pass must be RETRIED next tick
    // (KAPI present but its scan hasn't populated yet); the caller latches bKnowledgeUnlockDone
    // only on true.
    bool UnlockModdedScannerKnowledge();
    bool bKnowledgeUnlockDone = false;
    // Packet G (ns-automatch): once per load (and once more after a re-roll -- re-armed alongside
    // bKnowledgeUnlockDone in RollLayout, same reasoning: a re-roll can change which node types are
    // actively MANAGED), run the auto-allow-extractors pass. Mirrors UnlockModdedScannerKnowledge()'s
    // own idiom exactly: FNodeShuffleModule::RunAutoAllowExtractorsIfEnabled() returns false only when a
    // dependency (the recipe manager) isn't ready yet, so RefreshTick retries; true means the pass
    // COMPLETED this tick (including "disabled" and "SF+ absent" -- there is nothing further to retry).
    bool bAutoAllowExtractorsDone = false;

    // ns-h1b-notice: the deferred emitter. Called every RefreshTick; does nothing at all unless a pass
    // queued something. Defined in NodeShufflePendingNoticeEmit.cpp (ns-review-notice2 F-B: it used to
    // say NodeShufflePendingNotice.cpp "next to the copy it delivers", which the file split inverted --
    // the copy now lives in the OTHER file). The seam between the two is WHEN to speak (this function,
    // in ...Emit.cpp) versus WHAT to say (the FNodeShuffleModule statics, in NodeShufflePendingNotice.cpp).
    // Neither lives in this file's already-7000-line .cpp.
    void EmitPendingNoticeIfReady(bool bNoticesEnabled);

    // ---- ns-h1b-notice: the deferred "restart required" player notice ----
    // The pass completes ~19-50 s after boot, when the world exists but the local player's chat widget
    // may not. So the pass QUEUES and RefreshTick emits, once the player has actually spawned.
    //
    // ALL THREE MEMBERS ARE TRANSIENT BY DESIGN -- no UPROPERTY(SaveGame) anywhere in this block, and
    // that is load-bearing, not an omission. PENDING is a STATE recomputed every pass (written documents
    // MINUS what SF+ already allows); it empties itself one boot after the documents land, which is the
    // only reason this notice cannot nag. Persisting any of this across boots would fight that mechanism
    // and could suppress a notice the player genuinely needs after a rebuild.
    TArray<FNodeShufflePendingEntry> PendingNoticeQueue;
    // LOG-ONLY BREADCRUMB -- NOT a decision input. ns-review-notice2 F-C: this used to claim it was what
    // stopped the measured double-pass from double-messaging. It WAS, until F4 moved that job to
    // AnnouncedPendingKeys below; since then it is written once and read only by log format strings.
    // Kept deliberately, because a human reading the log wants to see the previous set alongside the new
    // one when a decision line says EMIT or SUPPRESSED -- but a field documented as load-bearing while
    // nothing reads it is exactly the documentation falsehood this packet exists to stop, so it says so.
    FString LastNotifiedSignature;
    // ns-review-notice F4: the SET of pending keys already announced this session. The signature above is
    // a set IDENTITY, which makes any change look new -- including a re-roll that merely SHRINKS the
    // pending set, which would post a second "restart required" message naming a strict subset of what
    // was already said. This set turns the test into "does the new set contain anything unannounced?",
    // so a shrink is silent while a genuinely new entry still gets through. Transient, like everything
    // else in this block.
    TSet<FString> AnnouncedPendingKeys;
    // Two-stage emit gate: -1 = nothing armed; 0 = a valid local player+pawn was seen this tick, wait
    // one more tick (~5 s) so the chat widget is up before we post; 1 = emit now.
    int32 PendingNoticeGateTicks = -1;
    // ns-review-notice F3: bounded retries. If the chat manager never materialises after the player gate
    // has passed, an uncapped retry logs three lines every ~5 s for the rest of the session (~720/hour)
    // for a message that will never arrive. This workspace has already paid once for unbounded per-call
    // log output; give up loudly instead.
    int32 PendingNoticeEmitAttempts = 0;
    // scanregen-1 (P2 design §4 touch-point 1): knowledge-unlock -> scanner/radar-tower refresh
    // trigger. UnlockModdedScannerKnowledge() has TWO callers — RefreshTick (world-settled) and
    // PostLoadGame_Implementation (mid save-load, before actor settling) — so the producer only
    // RECORDS that an unlock landed; only RefreshTick ACTS on it (never PostLoadGame — see the
    // knowledge-3 save-crash ordering class this avoids repeating).
    bool bScannerClusterRefreshPending = false;
    // scanregen-1: per-pass collapse so a re-roll tick (whose OWN existing call sites already invoke
    // RefreshScannersAndRadarTowers up to twice) can't make the new consume point a third redundant
    // call in the same pass. Set INSIDE RefreshScannersAndRadarTowers itself; cleared at the top of
    // every RefreshTick, before any early-out (design §9 amendment relies on this surviving a SKIP).
    bool bScannerRefreshedThisPass = false;
    // scanregen-1: resource classes unlocked by the CURRENT pending batch — read once by the
    // diagnostics-gated pre-invalidate cluster census in RefreshScannersAndRadarTowers, guarded by
    // bScannerClusterRefreshPending so a stale leftover list from an already-consumed batch is never
    // read (the producer resets + repopulates this every pass it runs, whether or not it unlocks).
    TArray<TSubclassOf<UFGResourceDescriptor>> ScanRegenUnlockedClasses;
    // knowledge-2 item 1: runtime MinerInfo provisioning — PURE REFLECTION against KAPI's
    // UKAPIDataAssetSubsystem (a UGameInstanceSubsystem; no KAPI include/link/stub anywhere). For
    // each managed modded ore MISSING from mMinerMapping, template-clone an existing description
    // (preferring the Desc_Stone_C entry), rewire its key (mResourceClass) and every
    // FKAPIModuleItems.mProductionItem to the ore (the resource descriptor IS the item class —
    // UFGResourceDescriptor : UFGItemDescriptor), insert into mMinerMapping and
    // mAllowedScannableResources exactly as KAPI's own ScanForMinerAssets does. knowledge-3: each
    // clone lives in the /NodeShuffle/RuntimeMinerInfo runtime package under a deterministic
    // NSMinerInfo_<Ore> name (RF_Public|RF_Standalone + rooted) so KLib's SaveGame reference to it
    // (mExtractionInfo) serializes down FObjectReferenceDisc's ASSET branch (LevelName empty,
    // absolute path) — the GameInstance-outered knowledge-2 clone crashed the save writer's
    // level-resolution machinery. Returns false when KAPI is present but not yet scanned (defer).
    // When KAPI is absent: returns true with bOutFilterUnlocks=false (no filtering — no Modular
    // Miner exists to crash). Every reflection lookup is null-checked; any failure skips that ore
    // entirely (never partially-wired).
    bool ProvideKAPIMinerInfo(const TArray<UClass*>& ManagedModded, TSet<UClass*>& OutWithMinerInfo,
                              bool& bOutFilterUnlocks);
    // knowledge-2: bounded defer while KAPI's game-instance-init scan hasn't populated mMinerMapping
    // yet (in practice it has, ~70 s before our first pass; an installation with KAPI but zero miner
    // description assets would otherwise defer forever). After the cap: terminal withhold-all.
    int32 KnowledgeDeferPasses = 0;
    static constexpr int32 KnowledgeDeferMaxPasses = 24; // ~2 min at the 5 s tick
    void RefreshScannersAndRadarTowers();
    // Removes one-off resource deposits sitting on shuffled nodes when their
    // resource contradicts the node's assigned one (runs once per session).
    void SweepMismatchedDeposits();

    // ---- helpers ----
    // Property-based eligibility (no class-name/form-list hardcoding). Eligible =
    // a genuine AFGResourceNode with a UFGResourceDescriptor resource, plain Node
    // type (GetResourceNodeType() == EResourceNodeType::Node — no geyser/fracking/
    // deposit), not skeletal/transient. bIncludeModded admits non-/Game/ resource
    // descriptors (AllMinable etc.). bIncludeLiquid admits non-solid forms: solid is
    // always allowed; liquid (oil) AND gas (e.g. lithium, Desc_OreLithium form=RF_GAS)
    // join when it is true (callers now pass true). Nothing is special-cased by mod/class name.
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
    // playtest-fixes-1 (modded-descriptor visuals): capture the visual of a hidden ORIGINAL whose
    // resource is a real UFGResourceDescriptor with NO authored table entry (RP thorium, bamrenew
    // lead, FF dirt). Sources in order: its OWN paired AFGNodeMeshActor (engine links), then
    // dirtdress-1: its OWN static-mesh components / attached mesh actors (self-rendering node BPs
    // that have no engine links — FF dirt was 0-for-98 on the pairing lottery). Called from
    // SuppressOriginalNodes (pairing is fresh each pass). On a NEW capture, already-spawned nodes
    // of that resource are re-dressed so they swap quartz -> the real look without a respawn.
    // Returns TRUE when the resource is capture-ELIGIBLE but nothing could be captured YET (no
    // pairing, no own mesh) — the caller then defers the steady-hidden mark so the original is
    // re-attempted next pass instead of losing its one shot per session (Fertilized-dirt bug).
    bool CaptureOriginalVisualIfNeeded(class AFGResourceNodeBase* Node);
    // dirtdress-1: resources whose full capture-decision chain was already logged this session
    // (diagnostics are once per resource per session, so retries can't spam the log).
    TSet<FString> CaptureChainLogged;
    // dirtdress-1 (cold review): terminal cap for the capture retry loop — same count-then-tombstone
    // idiom as ExternalDestroyCounts/DormantThisSession. A resource that reports capture-PENDING for
    // CaptureGiveUpPasses consecutive attempt-passes (~3 min at the 5 s tick) goes terminal for the
    // session: its originals steady-mark as normal and the placeholder stays (e.g. a modded node
    // whose only visual is an instanced-mesh component). All session-only; a successful capture
    // clears its resource's counter, and RollLayout's clear block resets all three with the sibling
    // sets. PendingThisPass is the per-pass scratch that turns per-ORIGINAL pending reports into one
    // per-RESOURCE count (processed + cleared at the end of each SuppressOriginalNodes pass).
    TSet<FString> CapturePendingThisPass;
    TMap<FString, int32> CapturePendingPasses;
    TSet<FString> CaptureTerminalThisSession;
    static constexpr int32 CaptureGiveUpPasses = 36;
    // knowledge-1 item 3: bounded give-up for spawns that fail every pass (evidence: 675 identical
    // "Failed to spawn new node (Node_BioWaterSF+_C)" warnings in ~4 min — SpawnActor returns null
    // each attempt, likely spawn-gated by the owning mod). Consecutive per-ENTRY failures; at
    // SpawnGiveUpAttempts the entry gives up and parks. P5 (addenda item 1 / P3 design §2.9 decision
    // Q6): this is NOT a one-time terminal event per entry — P3's deckevict-1 (EvictSpawnRefusingClass)
    // can REVIVE a parked entry onto a substitute class when one exists, resetting this budget, so the
    // SAME entry can give up again on the new class. The give-up therefore fires ONCE PER (entry,
    // class), bounded at 1 + MaxSubstituteChain (currently 3) total give-ups before the entry parks for
    // good with no further substitute to try — not once per entry. Same lifecycle as the capture retry
    // budget otherwise: success clears the counter, RollLayout's clear block resets all, and everything
    // is session-only (no SaveGame). FlagsLogged bounds the one-shot per-CLASS class-flag breadcrumb
    // (diagnostics-gated) that hints WHY the class refuses to spawn.
    TMap<FGuid, int32> SpawnFailCounts;
    TSet<FGuid> SpawnParkedThisSession;
    TSet<FString> SpawnFailFlagsLogged;
    static constexpr int32 SpawnGiveUpAttempts = 10;
    // P3 (deckevict-1): node classes that PROVED unspawnable this session (SpawnActor returned null
    // through the whole give-up budget) -> the class path we substitute for them. An EMPTY value means
    // "evicted, but no viable substitute exists" (entries stay parked). Keyed by CLASS PATH, not by
    // entry guid, so it survives a re-roll (which mints new EntryGuids) with zero re-seeding and is
    // inherently idempotent. SESSION-ONLY BY DESIGN: spawn-gating is a property of the loaded MOD
    // STACK, not of the world, so nothing here may reach the save. Deliberately NOT cleared in
    // RollLayout's reset block even though its siblings are -- a re-roll does not un-gate a class.
    TMap<FString, FString> SpawnRefusedClassSubstitute;
    // Classes we have WATCHED spawn successfully this session, by resource form (1 solid / 2 liquid).
    // Only /Game/ (vanilla) paths are recorded: a vanilla-origin node takes our fallback rock (see the
    // bVanillaOrigin dress branch), so a substitute drawn from here is guaranteed visible.
    TMap<uint8, FString> ProvenSpawnClassByForm;
    static constexpr int32 MaxEvictedClassesPerSession = 8;
    static constexpr int32 MaxSubstituteChain = 2;
    // P3: log-once latch for the eviction-cap Display line (design §5 line B) — the cap condition
    // itself (SpawnRefusedClassSubstitute.Num() vs MaxEvictedClassesPerSession) needs no counter.
    bool bEvictionCapLogged = false;
    void RedressSpawnedOfResource(const FString& ResourceClassName);
    // Find a persisted capture for a resource short name (null when none).
    const FNodeShuffleCapturedVisual* FindCapturedVisual(const FString& ResourceClassName) const;
    // Resolved-capture runtime cache: mesh/materials LoadObject'd once per session per resource.
    struct FNodeShuffleResolvedCapture
    {
        TWeakObjectPtr<UStaticMesh> Mesh;
        TArray<TWeakObjectPtr<UMaterialInterface>> Materials;
        FVector Scale = FVector(1.0f, 1.0f, 1.0f);
    };
    TMap<FString, FNodeShuffleResolvedCapture> ResolvedCaptureCache;
    const FNodeShuffleResolvedCapture* ResolveCapturedVisual(const FString& ResourceClassName);
    // redesign-1: place the starter node set (config) near the captured player-start on the FIRST
    // roll of a brand-new game. Drawn from the pool when possible. Spawned as normal new entries.
    void AppendStarterNodes(TArray<FNodeShuffleEntry>& NewLayout, FRandomStream& Rng,
                            const TArray<FVector>& AvoidLocations);
    // Re-applies the native visual (mesh-actor refresh or oil/gas decal) a node draws
    // for its CURRENT descriptor, by invoking the game's own OnRep_ResourceClassOverride via
    // ProcessEvent. Handles liquids (oil decal, no rock) and the no-mesh-actor case.
    // Packet H1: parameter WIDENED AFGResourceNode* -> AFGResourceNodeBase* so a fracking CORE can use
    // it. A core is an AFGResourceNodeBase but NOT an AFGResourceNode (design §2.1 -- the single biggest
    // asymmetry in Packet H), so the narrower signature could not reach it. The body is unchanged and
    // touches only UObject methods (IsValid / FindFunction / ProcessEvent), so this is a pure widening:
    // every pre-existing caller passes an AFGResourceNode* and binds exactly as before, and the function
    // still dispatches to whichever OnRep_ResourceClassOverride override the concrete class has
    // (AFGResourceNode's for nodes and satellites, AFGResourceNodeBase's for cores).
    void RebuildNodeNativeVisual(AFGResourceNodeBase* Node);
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
    // P3 (deckevict-1): the class an entry should ACTUALLY spawn from. Identical to Entry.NodeClassPath
    // unless that class was evicted this session (SpawnActor proved it unspawnable). Never mutates the
    // entry -> nothing persists; the save stays honest about what the world originally held, and the
    // workaround evaporates when the stack changes.
    FString ResolveSpawnNodeClassPath(const FNodeShuffleEntry& Entry) const;
    // P3: called only from the give-up block, on the FIRST give-up for a class. Idempotent (a Contains
    // check is the first statement). Picks a substitute via PickSubstituteClass, records it (possibly
    // empty = no viable substitute), then sweeps Layout reviving every OTHER entry already parked or
    // mid-budget on the same refused class — the whole point of evicting class-wide instead of per-entry.
    void EvictSpawnRefusingClass(const FString& RefusedPath, const FNodeShuffleEntry& Cause);
    // P3: ordered substitute-selection ladder (design §2.4) — gas gate first (DESCRIPTOR form, never
    // the ResourceForm byte), then a class proven spawnable THIS session, then a data-only layout scan,
    // then our own always-spawnable C++ fallback (solid only), then park (liquid with nothing proven).
    // Returns empty when no viable candidate exists.
    FString PickSubstituteClass(const FString& RefusedPath, const FNodeShuffleEntry& Cause) const;
    // playtest-fixes-1 / steepfix-1: optional out-flag distinguishes the definitive UNPLACEABLE
    // failure (the probe HIT ground but it is underwater OR steeper than a Miner tolerates, and the
    // 300 m spiral found no flat land) from the ambiguous no-terrain-hit defer (unstreamed terrain or
    // true void). Unplaceable triggers an immediate redeal; void keeps defer-and-retry.
    // cave-nodes-1: underground entries settle via a SHORT local trace (RaycastSettle reads
    // Entry.bUnderground) so the probe stays inside the cavern instead of hitting the roof.
    bool RaycastSettle(FNodeShuffleEntry& Entry, const AActor* IgnoreNode, const AActor* IgnoreMesh,
                       bool* bOutWaterNoLand = nullptr) const;

    // ---- cave-nodes-1..4: cavern discovery + placement (both always-on; placement = drain fix) ----
    // The shuffle DRAINED caves: originals inside caverns are hidden and the 200 m top-down settle ray
    // can only reach the outermost surface, so replacements never land inside. Discovery maps cavern
    // floors from PROVEN seeds (hidden originals that sit under a roof — vanilla only put nodes where
    // players can go) via a budgeted trace flood-fill that follows the floor through long winding
    // passages (step <=2.5 m, headroom >=3.5 m, still-roofed, dry), stopping at cave mouths. Cells are
    // persisted GLOBALLY (Configs/NodeShuffle_CaveFloors.json — map-static, like the water grid).
    // PLACEMENT (always on — cave-nodes-4): the shuffle used to DRAIN caves (hid their originals,
    // never placed anything back), a regression this fixes. Caves are ADDITIONAL RANDOM AREAS —
    // every deal draw (roll, relocation spots, water-locked redeal) picks a cave cell with natural
    // probability CaveSeedCount/poolSize (capped 25%). No quota, no top-up, no preference.
    struct FNodeShuffleCaveCell
    {
        float FloorZ = 0.0f;
        uint8 State = 1; // 1=frontier (expandable), 2=expanded, 4=mouth (walkable; no expansion past)
        // cave-nodes-2: measured ceiling clearance (roof hit - floor). Placement requires enough for
        // a Miner building; low passages stay mapped for connectivity only. -1 = unknown (legacy
        // imports) = treated as tall (the user stood there and chose it).
        float CeilingCm = -1.0f;
    };
    mutable TMap<int64, FNodeShuffleCaveCell> CaveFloors;
    mutable TSet<FString> CaveSeedsDone; // original node paths already roof-classified (persisted)
    mutable int32 CaveSeedCount = 0;     // seeds that WERE under a roof (the placement quota)
    mutable bool bCaveStoreLoaded = false;
    mutable bool bCaveStoreDirty = false;
    mutable int32 CaveStoreNewRecords = 0;
    void EnsureCaveStoreLoaded() const;
    void FlushCaveStoreIfDirty() const;
    // Roof-classify one resolved original (once per path, persisted): an up-trace that hits within
    // 150 m means the node sits under a roof -> cave seed cell at its own floor.
    void ClassifyOriginalUnderground(class AFGResourceNodeBase* Node, const FString& Path);
    // Budgeted per-pass flood-fill from frontier cells near players (traces need streamed collision).
    void ExpandCaveFloorsBudgeted();
    // cave-nodes-2 (user call: caves are ADDITIONAL RANDOM AREAS, never a quota to fill): the forcing
    // machinery (CaveTopUpPass + cave-first redeal priority) is GONE. Instead, every location deal —
    // roll, relocation spots, water-locked redeal — draws a cave cell with natural probability
    // CaveSeedCount / poolSize (what vanilla's own cave-node density encodes), capped at 25%.
    // Pick a random placeable cave cell (fully roofed + ceiling tall enough for a Miner). Raw variant
    // does NO spacing (the deal loop applies its own avoid/spacing checks); the Layout variant spaces
    // MinNodeSpacing (3D) against the live layout for redeals.
    bool TryPickRawCaveCell(FRandomStream& Rng, FVector& OutLoc);
    bool TryPickCaveCell(FRandomStream& Rng, FVector& OutLoc);
    int32 CountUndergroundEntries() const; // census/diagnostic only

    // playtest-fixes-3 (water-locked redeal, UNANCHORED): an entry whose spot is confirmed water-locked
    // is RE-DEALT to a fresh RANDOM location in the SAME map-wide deal box the roll used — anchoring to
    // streamed originals (fixes-1) clustered relocations around original sites and defeated the mod's
    // randomization (user call). The new spot is filtered by the LEARNED WATER GRID + MinNodeSpacing,
    // then settles LAZILY like any dealt entry (spawn-on-discovery when a player nears it). A spot the
    // grid doesn't know yet may hit water again — that settle teaches the grid one more cell and hops
    // once more; convergence is exponential (~25% water share per blind draw) and fully random.
    // Returns true when the entry was moved (NOT settled — bRayCasted stays false).
    bool TryRedealWaterLockedEntry(FNodeShuffleEntry& Entry);
    // ---- playtest-fixes-3: learned water grid (the "excluded areas" store) ----
    // The game has NO complete pre-stream water data at runtime (AFGWorldSettings::mWaterVolumes is
    // transient, "currently streamed in"). So we LEARN it: every RaycastGroundAt hit records land/water
    // at a 100 m cell, persisted GLOBALLY (Configs/NodeShuffle_WaterGrid.json — the map is static, so
    // knowledge carries across saves and re-rolls). Rolls and redeals reject known-water cells up
    // front. Cell states: 1=land, 2=water, 3=mixed (coastline; NOT excluded — the settle probe decides).
    void RecordWaterGridSample(const FVector& Loc, bool bWater) const;
    bool IsKnownWaterCell(const FVector& Loc) const;
    void EnsureWaterGridLoaded() const;
    void FlushWaterGridIfDirty() const;
    // bakedmaps-2: the mod SHIPS snapshots of the learned water/cave knowledge EMBEDDED in the DLL
    // (NodeShuffleBakedData.h, regenerated by Scripts/bake_maps.ps1 — the packaging pipeline ships
    // only Binaries + Paks, so loose files can never reach users). Loaders merge baked AFTER local
    // with local-wins (veterans keep their own learning; a merge dirties the store so the union
    // persists into the user's local file once). Content variants parse a string; Json variants load
    // a file and delegate. Return cells added, -1 when absent/unparseable.
    int32 MergeWaterGridFromContent(const FString& Content, const TCHAR* SourceLabel, bool bKeepExisting) const;
    int32 MergeWaterGridFromJson(const FString& Path, bool bKeepExisting) const;
    int32 MergeCaveStoreFromContent(const FString& Content, const TCHAR* SourceLabel, bool bKeepExisting,
                                    int32* OutFileSeedCount = nullptr) const;
    int32 MergeCaveStoreFromJson(const FString& Path, bool bKeepExisting, int32* OutFileSeedCount = nullptr) const;
    mutable TMap<int64, uint8> WaterGrid;
    mutable bool bWaterGridLoaded = false;
    mutable bool bWaterGridDirty = false;
    mutable int32 WaterGridNewSamples = 0;
    // Deal box captured at roll time (SaveGame) so redeals draw from the same distribution; for saves
    // rolled before this build the box is derived lazily from the Layout's own locations (they span
    // the map). MeanZ seeds the redealt entry's probe height.
    FVector GetDealBoundsMin() const;
    FVector GetDealBoundsMax() const;
    void EnsureDealBoundsDerived() const;
    // Transient per-entry redeal attempt counter — salts the deterministic RNG so a failed redeal
    // tries different anchors/offsets next pass instead of repeating the same candidates forever.
    TMap<FGuid, int32> RedealAttempts;
    // Entries confirmed water-locked at least once this session (drives the deferral summary + census).
    mutable TSet<FGuid> WaterLockedThisSession;
    // playtest-fixes-1 (defer-log backoff): first-occurrence-only detail logs; repeats are silent.
    // 153k defer lines in one session came from re-logging every retry of every stuck entry.
    TSet<FGuid> DeferLoggedThisSession;
    mutable TSet<FGuid> WaterDeferLoggedThisSession;
    int32 DeferredThisPass = 0;
    int32 LastDeferSummary = -1;

    // ---- coexist-1: external-destroy backoff + idempotent maintenance pass (ALL transient) ----
    // Another installed mod can DESTROY resource-node actors outright (observed: a KBFL actor-listener
    // targeting FGResourceNodeBase). Re-materializing every pass against such a destroyer is a
    // destroy/respawn war: log firehose, scanner/radar refresh churn, multi-second stutters. Backoff:
    // count destroys of OUR spawned actors that NodeShuffle did NOT perform itself; after
    // ExternalDestroyTombstoneAt of them in one session the entry goes DORMANT (skipped entirely) until
    // the next world load. NOTHING here is SaveGame — every load resets the tombstones, so each session
    // makes one cheap attempt per node and records are never lost if the destroyer relents.
    // Self-vs-external discrimination: NodeShuffle's ONLY self-destroy of spawned nodes (the re-roll
    // wipe in RollLayout) removes the SpawnedNodes slot synchronously in the same block — so a slot
    // holding an invalid/null actor when the pass looks is proof of an EXTERNAL destroyer. Any future
    // self-destroy site MUST keep that invariant (destroy + remove the slot together).
    static constexpr int32 ExternalDestroyTombstoneAt = 2;
    TMap<FGuid, int32> ExternalDestroyCounts; // per-entry external destroys this session
    TSet<FGuid> DormantThisSession;           // tombstoned entries: no re-materialize until next load
    int32 LastDormantSummaryNum = 0;          // ungated coexistence summary fires only on count change
    // Idempotent-pass markers: work that only matters ONCE PER LIVE INSTANCE (dress, use-box, manager
    // registration, hide funnel) is skipped while the SAME instance stays valid — keyed by weak ptr so
    // a reload/respawn/re-stream (new instance) naturally falls through to the full path again.
    TMap<FGuid, TWeakObjectPtr<AFGResourceNode>> SteadyAliveNodes;          // spawned nodes fully asserted
    TMap<FString, TWeakObjectPtr<AFGResourceNodeBase>> SteadyHiddenOriginals; // originals fully hidden
    // One ungated hide-funnel totals line per LOAD (the per-pass HIDEDIAG stays diagnostics-gated and
    // now only logs when its numbers CHANGE); per-pass change tally feeds the gated "pass: 0 changes".
    bool bLoadFunnelLogged = false;
    int32 LastHideFunnel[8] = { -1, -1, -1, -1, -1, -1, -1, -1 }; // dirtdress-1: +capturePending slot; rehide-1: +rematched slot
    int32 SuppressChangesLastPass = 0;
    float LastRockBackstopSeconds = 0.f; // stray-rock backstop cooldown (forced when a node newly hides)
    // playtest-fixes-1 (ghost radiation): resolve the radioactivity subsystem via the GameState's
    // public inline getter (AFGRadioactivitySubsystem::Get is a static whose export is not trusted —
    // same LNK2019 class of problem as AFGResourceNodeManager::Get).
    class AFGRadioactivitySubsystem* GetRadSubsystem() const;
    int32 RadEmittersRemoved = 0; // running total, logged in the hide summary
    // FIX A (land-only placement): true if a world point sits inside a streamed-in
    // water volume (ocean/lake/river). Used to reject settle hits on the seafloor:
    // the down-ray hits solid terrain, but if that impact point is underwater the
    // node is invisible to the player. Definitive (volume containment), not a
    // sea-level guess. Returns false when no water is streamed near the point.
    bool IsPointInWater(const FVector& Point) const;
    // FIX A: single raycast that also rejects water. Out params return the grounded
    // location/rotation/water-state. Helper that RaycastSettle's relocation loop
    // drives over offset candidates.
    // cave-nodes-1: bShortTrace probes only +-4/8 m around StartZ (underground entries — the long ray
    // would hit the cave roof); short traces also skip the deep-water floor reclassification (the cell
    // was depth/water-verified at discovery) but keep the volume test.
    // slopefit-1: bOutTooSteep reports a CLIFF-face hit (steeper than CliffSlopeDeg; long traces only
    // — cave floors were walkability-checked at discovery). Cliff hits are LAND for the water grid
    // but unplaceable: the spiral/redeal machinery moves the entry. OutRot is the node-actor rotation
    // with tilt CLAMPED to NodeTiltClampDeg toward the SMOOTHED ground normal (4-probe ring average);
    // OutGroundNormal returns that full smoothed normal so the rock visual can take the whole slope.
    bool RaycastGroundAt(const FVector& ProbeXY, float StartZ, const AActor* IgnoreNode,
                         const AActor* IgnoreMesh, FVector& OutLoc, FRotator& OutRot,
                         bool& bOutWater, bool bShortTrace = false, bool* bOutTooSteep = nullptr,
                         FVector* OutGroundNormal = nullptr) const;
    // slopefit-1 RETRO-FIT: nodes settled under the old full-tilt rule get the clamped-actor rotation
    // + full-slope rock alignment re-applied ONCE on adopt (rotation only; never occupied/pinned).
    TSet<FGuid> AdoptRotRefit;
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

    // playtest-fixes-1: visuals captured from hidden originals' paired mesh actors (modded-descriptor
    // resources with no authored entry). Persisted: next session resolves by asset path immediately.
    UPROPERTY(SaveGame) TArray<FNodeShuffleCapturedVisual> CapturedVisuals;

    // playtest-fixes-3: the percentile deal box + mean probe height captured at roll time, reused by
    // the unanchored water-locked redeal. Zero when the save predates this build (derived lazily then).
    UPROPERTY(SaveGame) FVector DealBoundsMin = FVector::ZeroVector;
    UPROPERTY(SaveGame) FVector DealBoundsMax = FVector::ZeroVector;
    UPROPERTY(SaveGame) float DealMeanZ = 0.0f;
    // Lazily-derived fallback box for pre-fixes-3 saves (transient).
    mutable FVector DerivedBoundsMin = FVector::ZeroVector;
    mutable FVector DerivedBoundsMax = FVector::ZeroVector;
    mutable bool bDerivedBoundsReady = false;

    // redesign-1: STARTER NODES. Captured player-start world location (the player pawn's position at
    // the earliest tick it exists on a brand-new game) and whether starters have already been placed.
    // Both SaveGame so starters are placed exactly once per save and never on an existing save.
    UPROPERTY(SaveGame) bool bStarterNodesPlaced = false;
    UPROPERTY(SaveGame) bool bPlayerStartCaptured = false;
    UPROPERTY(SaveGame) FVector PlayerStartLocation = FVector::ZeroVector;

    // ---- session state ----
    FTimerHandle TickTimerHandle;
    // Previous-tick value of the Re-roll toggle, for EDGE-TRIGGERED re-roll: the re-roll fires on each OFF->ON
    // transition of Config.RerollNow — so it works the same whether the user set it and reloaded (load-time) or
    // toggled it mid-session (LIVE, no reload). Edge-triggering also prevents a loop if the config clear is slow.
    bool bPrevRerollNow = false;
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

    // ---- Packet H1 (ns-wells-h1): IN-PLACE RESOURCE-WELL RETYPE ----
    // Everything below is DEFINED IN NodeShuffleWellRoll.cpp (the deal) and NodeShuffleWellRetype.cpp
    // (the write), not in NodeShuffleSubsystem.cpp; NodeShuffleWellRetype.h holds the shared pure
    // helpers and states which file owns what. They must be MEMBERS (not free functions in those files)
    // because writing AFGResourceNodeBase's PRIVATE mResourceClassOverride needs this class's
    // AccessTransformers Friend grant, and C++ friendship is class-to-class -- exactly the constraint
    // FNodeShuffleModule::DbgLogAcceptance documents for the module side. Defining members across extra
    // translation units keeps the code out of an already 8000-line file without giving up that access.

    // Deals a resource to every non-pinned well. Called from RollLayout (initial roll AND re-roll) so
    // wells re-roll with the rest of the layout. No-op (and leaves any existing assignment untouched)
    // when the ShuffleResourceWells config toggle is off.
    void RollWellLayout(int32 Seed, bool bIsReroll);

    // Idempotent per-pass apply, called from ApplyLayout. Writes the dealt resource to the core AND
    // every satellite, asserts they agree afterwards, and NEVER touches purity. bWellShuffleEnabled is
    // passed in rather than re-read so ApplyLayout's single config fill stays the only one per pass.
    void ApplyWellRetype(bool bWellShuffleEnabled);

    // Writes ResourceClass onto ONE well member (core or satellite) and rebuilds its native visual.
    // Returns true when it actually changed something (so the caller's per-pass counters only count
    // real writes, keeping the steady-state log silent). MemberRole/CoreName are for the log line only.
    bool RetypeWellMember(AFGResourceNodeBase* Member, UClass* ResourceClass,
                          const TCHAR* MemberRole, const TCHAR* CoreName);

    // Packet H1: a well is one thing with N members, so it gets its own SaveGame array rather than
    // being squeezed into Layout. Empty on every save where ShuffleResourceWells was never turned on,
    // which is what makes the mod's stable core byte-identical with the feature off.
    UPROPERTY(SaveGame) TArray<FNodeShuffleWellEntry> WellLayout;
    UPROPERTY(SaveGame) bool bWellLayoutRolled = false;

    // Session-scoped log throttles -- the apply pass runs every ~5 s over every well, so any line that
    // is not delta-driven would be a firehose (the 153k-line / 35 MB precedent this file already
    // documents for the deferral log). None of these are persisted.
    TSet<FString> WellAppliedLogged;   // core paths whose successful retype has been announced
    TSet<FString> WellSkipLogged;      // core paths whose skip reason has been announced
    int32 WellMembersWrittenThisSession = 0;
    bool bWellDisabledLogged = false;  // "feature off but this save has well data" -- said once
    // "feature ON but this save has no roll yet" -- said once. Its own latch, not shared with the one
    // above: the two states are opposites and a user can move between them mid-session by toggling, so
    // one shared flag would silently suppress the second message.
    bool bWellNoRollLogged = false;
    // Apply-pass counter, used ONLY to fire the once-per-session live-vs-layout well census on a
    // settled world. Apply runs every ~5 s, so pass 6 is roughly 30 s after the layout starts applying
    // -- late enough that streaming has caught up, early enough to be in the log before the player
    // reaches a well. A boolean latch would have fired on the first pass, mid-load, and reported a
    // half-streamed world as the answer.
    int32 WellApplyPasses = 0;
    static constexpr int32 WellCensusScanPass = 6;
};
