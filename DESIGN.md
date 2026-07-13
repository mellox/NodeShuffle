# NodeShuffle — Design

Server-side SML mod (mod reference `NodeShuffle`) for Satisfactory 1.1+
(`>=491125`), SML 3.12, UE 5.6.1-CSS. It relocates the world's resource nodes to
new, map-wide locations per save and shuffles which are active and what resource
and purity each carries — for solids, oil/liquids, and modded nodes — with
balance minimums that keep every playthrough completable.

## Model: Hide & Replace

Every **unoccupied** original node (vanilla *and* modded) is **hidden
whole-actor** (which also removes its rock, including instanced meshes) and
recorded persistently. Its resource lives on as one of **our own relocated
nodes**, spawned at a new location. **Occupied** originals — any node with a
miner, extractor, or portable miner — are left 100% untouched, at roll time and
re-checked continuously, so the player's factory is never disturbed and the save
stays safe. Hiding is reversible (nothing is destroyed), so disabling the mod
restores the vanilla world.

## Relocated nodes spawn as their original class

A relocated node is spawned **as its own resource-node class** (resolved from the
saved `NodeClassPath`), not a generic stand-in. This is what makes modded nodes
behave correctly: a lithium node spawns as its Alkali class, so it keeps its
native rules (rejects a normal Miner, accepts its intended extractor). Each
spawned node carries a `UNodeShuffleNodeComponent` — a runtime identity marker
that also hosts the fallback rock/oil-decal and a `bForceAccept` flag. Identity
lives in the SaveGame layout (a `FGuid` per entry), not the actor class, so nodes
re-adopt by GUID or by location across reloads.

### Visuals

In priority order: an **authored table** (`FNodeShuffleNodeAssets`) of the game's
own node meshes/materials for vanilla resources; a look **captured from the
original node's paired mesh actor** for modded resources the table doesn't cover
(e.g. RefinedPower thorium, modded lead); a **quartz placeholder** otherwise; and
an **oil decal** for liquids. When a modded-class node is dealt a *vanilla*
resource, the assigned resource's look wins (the native mesh is hidden and our
rock is dressed) so it doesn't wear the wrong appearance.

## Placement and terrain

New nodes use **spawn-on-discovery**: they materialize only once a player is
within range and the terrain has streamed in, so they always settle correctly on
the ground. Settling (`RaycastSettle`):

- **Slope-fit** — the rock takes the full (smoothed) slope so it beds into the
  hill; the node *actor* is tilt-clamped (~12°) so the Miner hologram gets
  near-vanilla geometry and can place. Hills of any steepness are fair game.
- **Cliff avoidance** — hits steeper than 60° re-deal to better ground.
- **Water avoidance** — a learned land/water map plus a depth guard and the
  game's water-volume test keep nodes off the seabed and out of lakes; a
  water-locked entry re-deals randomly within the map-wide deal box.
- **Caves** — the shuffle would otherwise empty caverns (it hides their
  originals). Roofed originals seed a budgeted trace flood-fill that maps cavern
  floors; nodes are then dealt onto them at their natural share, only where a
  Miner fits.
- Overlap/enclosure/machinery guards keep nodes off factories and out of boxed-in
  rock pockets.

### Learned + prebuilt terrain map

The land/water grid (100 m cells) and cave-floor cells (8 m) are learned from
every ground probe and persisted **globally** (`NodeShuffle_WaterGrid.json`,
`NodeShuffle_CaveFloors.json` — the terrain is the same across saves). A snapshot
is also **embedded in the mod** (generated into `NodeShuffleBakedData.h`) and
merged on load with **local knowledge winning**, so a fresh install places nodes
well from the first launch and the player's own exploration keeps refining it.

## Per-save layout

The single source of truth is a `UPROPERTY(SaveGame)` array of `FNodeShuffleEntry`
on the subsystem (plus seed and layout version). It is **rolled once** per save
from a seeded `FRandomStream` and thereafter only **re-applied idempotently**
every ~5 s (server-only). The roll preserves balance by dealing resources and
purities from the **vanilla multiset** (so overall balance is retained) and
enforces a per-resource active **floor** (`MinNodesPerResource`, and a separate
floor for modded resources) — the completability guarantee. New SaveGame fields
are additive with defaults, so older saves load unchanged.

## Function hooks (SML)

A small number of engine methods are hooked via `SUBSCRIBE_UOBJECT_METHOD`:

- **Miner hologram acceptance** — force-accept our relocated nodes so Miner
  buildings snap to them (never fracking or gas nodes, which would crash or must
  keep native rules).
- **Resource scanner** — strip hidden originals from the scan clusters at the
  source, so an emptied original doesn't leave a phantom ping or map marker.
- **Portable miner dispenser** — lift the spawn onto the rock surface the player
  aimed at instead of the terrain beneath it.

## Radiation

Radiation is positional data in `AFGRadioactivitySubsystem`. Hiding an original
**removes its emitter** (otherwise it would keep irradiating an empty spot), and
a relocated/restored node **re-registers** radiation at its new location — so
radiation moves with the shuffle instead of leaving invisible hot zones.

## Safety rules

- A node with a miner/extractor/portable miner is **never** retyped, moved,
  purity-changed, or deactivated — enforced at roll time and re-checked each pass.
- Re-rolling is never implicit: only the edge-triggered `RerollNow` one-shot
  toggle triggers it (live mid-session or on next load), and it clears itself.
  Occupied nodes are carried and pinned through a re-roll.
- With `Enabled=false` the subsystem does nothing; hidden originals return and no
  new nodes spawn.

## Console commands

- `NodeShuffle.Here` — logs the player's position, the ground slope at their feet
  (and whether the cliff gate accepts it), and a 300 m census of nearby nodes and
  originals (state, distance, radiation).
- `NodeShuffle.SeedHere` — marks a roofed spot (cave/arch/overhang) as a
  cave-placement seed.

## Access transformers

`Config/AccessTransformers.ini` friends `ANodeShuffleSubsystem` (and, for the
hooks, `FNodeShuffleModule`) to the engine members the design needs:
`AFGResourceNode`/`AFGResourceNodeBase` (overrides, radioactivity, mesh actor,
placement flags), `AFGResourceNodeManager` (registration), `AFGResourceScanner`
(cluster refresh + phantom-ping strip), `AFGRadioactivitySubsystem` (emitter
verification), `AFGPortableMinerDispenser`, and the Miner extractor hologram.

## Config and data files

See the [README](README.md) for the full in-game config table and the optional
`Configs/*.json` data files (generated-node report, custom node list, rock-pattern
overrides, and the learned terrain maps).
