# NodeShuffle — Design

Local-only SML mod (mod reference `NodeShuffle`) that adds ~100 new resource
node locations to the world and randomizes, per save, which nodes (vanilla +
new) are active and what resource/purity each active node carries — with
balance guarantees so every playthrough stays completable.

Game: Satisfactory 1.1+ (>=491125), SML 3.12, UE 5.6.1-CSS.

## What was learned from the reference mods

### Resource Roulette (TheRealBeef) — closest prior art
Cloned to `C:\Claude\Projects\reference-mods\ResourceRoulette`. **No license
file** → approach studied, no code copied verbatim.

Their architecture (built pre-1.1): scan all `AFGResourceNode` actors into
plain SaveGame structs, **destroy every vanilla node actor** (and sweep the
world for vanilla node *meshes*, which are separate actors), then respawn all
nodes fresh as `RF_Transient` actors each session, with hardcoded
mesh/material asset path tables for visuals. Because everything is respawned,
they must **re-associate every extractor and portable miner** with the nearest
respawned node on every load (`mExtractableResource` / `mExtractResourceNode`
saved references die with the destroyed actors). They defer terrain alignment:
nodes are spawned at the stored location, then **raycast-settled** (Vogel-disk
sample + RANSAC plane fit) when the player first comes within 250 m, because
distant terrain isn't streamed in for raycasts at load time. Purity balance is
preserved by **dealing from the vanilla purity multiset** rather than rolling
independently. Scanner clusters (`AFGResourceScanner::mNodeClusters`) and
radar towers must be refreshed after changing the node set.

Key takeaways adopted: deal-from-vanilla-multiset balancing, deferred raycast
settling, spawning the **vanilla node Blueprint class** (so look-at UI, purity
texts, build-gun interactions all work), `RF_Transient` on spawned nodes so the
save system doesn't create "zombie" duplicates, extractor re-association as a
recovery pass, scanner/radar refresh.

Key mistake avoided: destroying and respawning *all* vanilla nodes. That makes
every node in the save dependent on the mod forever and forces the fragile
global mesh sweep. We leave vanilla node actors in place.

### Lithium (AniViRus, MIT)
Creates a brand-new node type *with* visible meshes, but does it as editor
content (Blueprint/uasset nodes placed in a level). Confirms that visible new
nodes are expected to be `AFGResourceNode` + a static mesh; not usable
directly since our nodes are placed procedurally at runtime.

### AllMinable (DellAquila)
Blueprint-only (uassets, no C++ to study). Known flaw per spec: new nodes have
**no visible meshes** — the exact failure we avoid by dressing every spawned
node with the proper resource mesh + materials.

## The discovery that shaped the design: native 1.1 node-override machinery

Satisfactory 1.1 shipped a native "Random Nodes" game mode. Verified in the
real headers (`Resources/FGResourceNodeBase.h`, `Resources/FGResourceNode.h`,
`Resources/FGResourceNodeManager.h`):

- `AFGResourceNodeBase::SetResourceClassOverride()` — **SaveGame, replicated**
  resource-class override, separate from the original `mResourceClass`.
- `AFGResourceNode::SetResourcePurityOverride()` — SaveGame purity override
  (`GetResourcePurity()` returns the override when set).
- Node meshes are now separate `AFGNodeMeshActor` actors (paired to nodes via
  the public `mNodeActor` property) with a public
  `OverrideMeshAndMaterials(node, originalDesc, overrideDesc)` that swaps mesh,
  materials and offset from the game's own per-resource
  `FNodeMeshOverrides` data.
- `AFGResourceScanner` subscribes to override changes
  (`RequestNodeClustersUpdate`) and has a lazy `mNodeClustersUpToDate` flag.

So for **vanilla nodes we never destroy/respawn anything**: re-typing a node is
`SetResourceClassOverride` + `SetResourcePurityOverride` + broadcasting the
node's `OnResourceClassOverrideReplication` delegate + calling
`OverrideMeshAndMaterials` on its paired mesh actor. Both overrides are
SaveGame on the node itself, so they persist and restore natively. This is the
same mechanism the base game uses, which makes it the lowest-risk possible
implementation. (Note: `AFGResourceNodeManager` itself is **not**
`FACTORYGAME_API`-exported, so we cannot link against its helpers; we only use
exported classes.)

## Architecture

Standard scaffold (mirrors HostileScaling): runtime module + log category
(`LogNodeShuffle`), `URootInstance_NodeShuffle` (registers config),
`URootGameWorld_NodeShuffle` (registers subsystem), `UNodeShuffleConfig`
(SML config built in `PostInitProperties` per the three-trap-safe pattern),
and the brain: `ANodeShuffleSubsystem` (`AModSubsystem` + `IFGSaveInterface`).
No function hooks at all — the mod is purely a subsystem.

### Node pool and eligibility

Only **solid, infinite, plain nodes** participate:
`GetResourceNodeType() == Node && GetResourceForm() == RF_SOLID &&
GetResourceAmount() == RA_Infinite`. Oil (liquid/decal), fracking
cores/satellites, geysers and pickup deposits are untouched — they are where
Resource Roulette accumulated most of its complexity and bugs, and the spec's
goals (iron→caterium etc.) are fully met with solids. The resource-type
universe is discovered from the world scan (no hardcoded descriptor list), so
it automatically covers iron, copper, limestone, coal, caterium, quartz,
sulfur, bauxite, uranium, SAM.

### Per-save layout (the single source of truth)

`UPROPERTY(SaveGame)` on the subsystem: `SavedSeed`, `bLayoutGenerated`,
`LayoutVersion`, `TArray<FNodeShuffleEntry>`. Each entry:

| Field | Meaning |
|---|---|
| `EntryGuid` | stable identity, also used to find live spawned actors |
| `bIsNewNode` | vanilla node vs. mod-spawned node |
| `VanillaNodePath` | `GetPathName()` of the vanilla level actor (stable across loads) |
| `Location` / `Rotation` | world placement (new nodes) |
| `OriginalResourceClassPath` / `OriginalPurity` | vanilla state at roll time |
| `AssignedResourceClassPath` / `AssignedPurity` | rolled state |
| `bActive` | inactive vanilla nodes get deactivated; inactive new nodes are never spawned |
| `bPinned` | had a miner at roll time → never changed in any way |
| `bRayCasted` | new node has been terrain-settled |
| `NodeClassPath` | the vanilla node *Blueprint* class to spawn new nodes from |

The layout is rolled **once** per save (seeded `FRandomStream`) and then only
re-applied. Determinism: same layout every load by construction, because it is
data, not a re-roll.

### Roll algorithm (balance guarantees)

1. Scan vanilla pool; mark occupied nodes (`IsOccupied()` or referenced by a
   portable miner via public `mExtractResourceNode`) as **pinned**.
2. Generate `NewNodeCount` (default 100) candidate locations: seeded random
   vanilla anchor node + random direction, 50–150 m offset, rejected if within
   25 m of any other pool location. Z starts at the anchor's Z and is fixed
   later by raycast settling. If
   `FactoryGame/Configs/NodeShuffle_CustomNodes.json` exists, its locations
   are used **instead** (see "Editing the location list").
3. `TargetActive = round(PoolSize × ActivePercent%)`, clamped to at least
   (sum of per-resource minimums, pinned count, and — if vanilla nodes may not
   disappear — the vanilla count).
4. Per-resource quotas `T_r`: vanilla proportions scaled to `TargetActive`,
   then raised to `max(MinNodesPerResource, pinned_r)`, then the total is
   trimmed/padded (never below the floor) to hit `TargetActive` exactly.
   **This is the completability guarantee**: every resource type that exists
   in vanilla keeps at least `MinNodesPerResource` active nodes.
5. Active set: pinned nodes always; all vanilla nodes if
   `AllowVanillaDisappear` is off; the rest drawn randomly from the pool.
6. Resource deck: `T_r − pinned_r` copies of each resource, shuffled, dealt to
   the non-pinned active nodes. Purity deck: the vanilla purity multiset
   scaled to the deck size and dealt the same way (`RandomizePurity` on), or
   vanilla nodes keep their own purity and only new nodes draw from the
   vanilla purity distribution (off).

### Apply pass (idempotent, every 5 s, server-only)

For each entry, compare desired state to live state and fix only differences:

- **Vanilla, active, retyped** → set class/purity overrides if they differ,
  broadcast `OnResourceClassOverrideReplication`, call
  `OverrideMeshAndMaterials` on the paired mesh actor. All idempotent.
- **Vanilla, inactive** → remove map representation, hide + de-collide its
  `AFGNodeMeshActor`, destroy the node actor. Re-done each session (level
  actors reload each launch — same proven model as Resource Roulette). If the
  node somehow became occupied first, the entry is flipped to active+pinned
  instead (safety rule wins).
- **New, active** → if no live actor for `EntryGuid`, spawn the vanilla node
  BP class (`RF_Transient`), `InitResource`, init/update radioactivity,
  root + "Resource"-profile box collision, and dress it: spawn a paired
  `AFGNodeMeshActor` (deferred so `mNodeActor` is set before BeginPlay,
  mobility Movable) and use `OverrideMeshAndMaterials`; if the native path
  yields no mesh, fall back to a static-mesh component using an internal
  table of the game's node mesh/material asset paths.
- **New, not yet settled** → when a player is within 250 m, raycast-settle
  (ring sample, average-Z + best-fit normal) and persist `bRayCasted`.
- **Extractor recovery** (load-bearing for miners on new nodes, since those are
  respawned each session): any non-water, non-fracking extractor whose
  extractable resource is gone is re-pointed (`SetExtractableResource`) at the
  nearest matching active new node within 7 m; portable miners likewise within
  15 m.
- On any change: `mNodeClustersUpToDate = false` on all resource scanners
  (access transformer) and radar towers rescan.

### Safety rules (hard guarantees)

- A node with a miner/extractor (or portable miner) is **never** retyped,
  never purity-changed, never deactivated — at roll time *and* re-checked at
  apply time.
- Re-rolling never happens implicitly. Only when `AllowReroll` is enabled AND
  `SeedOverride` is non-zero AND differs from the seed stored in the save, and
  only at session start. During a re-roll every currently-occupied node
  (including occupied *new* nodes, detected via live actors or orphaned
  extractor positions) is pinned with its current resource/purity.
- Master `Enabled=false`: the subsystem does nothing. Note: class/purity
  overrides already written into the save persist (they are the game's own
  SaveGame properties); deactivated vanilla nodes come back and new nodes
  simply don't spawn.

### Access transformers

```ini
[AccessTransformers]
Friend=(Class="AFGResourceNode",     FriendClass="ANodeShuffleSubsystem")
Friend=(Class="AFGResourceNodeBase", FriendClass="ANodeShuffleSubsystem")
Friend=(Class="AFGResourceScanner",  FriendClass="ANodeShuffleSubsystem")
```

(`mMeshActor`, `InitRadioactivity`/`UpdateRadioactivity`, `mResourceNodeType`,
`mCanPlaceResourceExtractor`, `mNodeClusters`/`mNodeClustersUpToDate`.)

### Config (SML, all live-editable except where noted)

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | true | master switch |
| `SeedOverride` | 0 | 0 = roll a random seed at first generation; non-zero = use this seed |
| `AllowReroll` | false | allow `SeedOverride` ≠ saved seed to re-roll an existing save at next load |
| `ActivePercent` | 70 | % of the total pool (vanilla + new) that is active |
| `NewNodeCount` | 100 | candidate new locations (used at generation time only) |
| `MinNodesPerResource` | 5 | per-resource-type active minimum (completability floor) |
| `RandomizePurity` | true | also shuffle purities (dealt from the vanilla distribution) |
| `AllowVanillaDisappear` | true | when off, every vanilla node stays active |

Generation-time options (`SeedOverride`, `NewNodeCount`, `ActivePercent`,
`MinNodesPerResource`, `AllowVanillaDisappear`) affect a save only at first
roll or an explicit re-roll.

### Editing the location list (data-driven new nodes)

At first roll the mod writes the generated candidates to
`<game>\FactoryGame\Configs\NodeShuffle_GeneratedNodes.json` as a report. To
hand-tune locations: copy that file to
`<game>\FactoryGame\Configs\NodeShuffle_CustomNodes.json`, edit it, and
start a **new** save (or re-roll an existing one). Schema:

```json
{ "Nodes": [ { "X": -123400.0, "Y": 45600.0, "Z": 12000.0 } ] }
```

Coordinates are Unreal world units (1 m = 100 uu); Z only needs to be roughly
right — nodes settle onto the terrain when first approached.

## Known risks / deliberate trade-offs

- `OverrideMeshAndMaterials` and the server-side behavior of
  `SetResourceClassOverride` are binary-only — header evidence strongly
  suggests they do the right thing (it is the native random-mode path), but it
  needs the in-game check. Fallback visual path exists for new nodes.
- New nodes floating/odd on extreme terrain until approached (raycast settle),
  same visual quirk Resource Roulette ships with.
- Removing the mod from a save: vanilla nodes all come back (deactivation is
  per-session), but miners built on *new* nodes will reference nothing —
  dismantle miners on mod-added nodes before removing the mod.
- Multiplayer untested; all logic is server-side and overrides replicate, but
  client-side visuals for *new* nodes rely on the spawned actors replicating.

## Future work (tracked)

- Proper visuals for non-vanilla node resources (AllMinable items, modded ores): extend the FNodeShuffleNodeAssets table or generate generic 'modded resource' rocks; currently such assignments keep the previous rock appearance or are invisible.
- Retype deposits sitting on shuffled nodes to match instead of removing them (needs per-resource deposit meshes).


## NEXT ITERATION (not yet implemented - agent died on spend limit)

Rock sweep for ore-node visuals. CONFIRMED at runtime: ore node actors (iron/gold/copper/bauxite/uranium/quartz/SAM, ~280) carry NO StaticMeshComponent - their rocks are separate level actors; coal/sulfur/stone/AllMinable nodes self-mesh (donor swap already works for those, 123 confirmed). Plan: once per session, sweep actors with StaticMeshComponents whose mesh names match the node-mesh names in NodeShuffleNodeAssets.cpp; associate nearest within 15m of each pool node; capture ore donor looks from swept rocks; on retype swap the swept rock's mesh/materials (Movable first); on deactivate hide+decollide it; handle/skip-with-log instanced static mesh components. Also add: log spawned new-node coordinates so testers can find them.


## Known issues (for the future GitHub repo)

1. One specific spawn-area coal node consistently retypes with no visual (possibly buried) while other conversions including new locations render correctly — needs per-node investigation (verbose log of its donor/placement values at that spot).
2. Oversized donor mis-captures rendered as phantom walk-through 'rock shelves' — mitigated by the 12m bounds rejection (donor skipped, node may show no visual); root-cause is auto-learn or donor capture adopting an oversized mesh; consider logging which resource captured which donor mesh at Display to identify it.

