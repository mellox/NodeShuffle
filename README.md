# Node Shuffle

A Satisfactory mod (SML 3.12+, game version ≥ 491125 / Satisfactory 1.1) that
**randomizes the planet's resource nodes per save**. It **relocates nodes to new
locations** across the map and shuffles which are active, what resource each
carries, and their purity — for **solids, oil/liquids, and modded nodes** alike —
so every world is a fresh puzzle, while staying completable.

## Compatibility note — Satisfactory Plus

There is a known interaction between NodeShuffle and some Satisfactory Plus ecosystem versions
that can cause occasional stuttering and some modded nodes not appearing. **Confirmed compatible
with NodeShuffle:** KBFL 2026.2.267, MkPlusSFPlus 1.3.0-alpha, KPatchwork 2026.3.26, and earlier.
This note will be updated as newer versions are confirmed.

## What it does

- **Relocates & shuffles nodes** — nodes move to new map-wide locations and may
  change resource and purity. This covers solid ore, **oil/liquid and gas nodes**
  (minable with the matching extractors), and **modded nodes** added by other
  mods. Originals are hidden, never destroyed, so it stays save-safe and
  reversible (disabling the mod restores the vanilla world).
- **Adds ~100+ new node locations** — spawned across the map, settled onto the
  terrain, visible, solid, and mineable like any vanilla node.
- **Places nodes where they belong** — relocated nodes **conform to slopes and
  hillsides**, stay **out of water** and **off sheer cliff faces**, and can land
  on **cavern floors** the mod maps as you explore (so caves keep resources
  instead of being emptied by the shuffle). A node that lands somewhere a miner
  can't go re-deals itself to better ground.
- **Ships a prebuilt terrain map** — a snapshot of discovered water and cave
  areas travels inside the mod, so a **fresh install places nodes well from the
  first launch** instead of learning the map from scratch; your own exploration
  keeps refining it, and each update folds in more.
- **Fully buildable** — relocated and new nodes accept hand-mining, portable
  miners, **and Miner buildings (Mk.1 and up)**; placed miners keep their node
  through save/load and re-roll.
- **Starter nodes (optional)** — on a brand-new game, a small guaranteed set
  near spawn (Iron/Limestone/Copper) keeps the early game playable.
- **Per-save, deterministic** — the layout is rolled once per save from a seed
  and persists; identical on every reload. Never re-rolled unless you opt in.
- **Balance guarantees** — every vanilla resource keeps a configurable minimum
  number of active nodes, so no playthrough becomes impossible.
- **Respects your factory** — any node with a miner or extractor on it is never
  changed, retyped, or removed (checked at roll time and continuously).
- **Radioactivity follows the shuffle** — a node retyped to uranium irradiates;
  one retyped away from it stops.
- **Multiplayer-aware** — all logic is server-side; overrides replicate.

> **Note on player-built nodes** (if you use a mod that lets you *build* resource
> nodes): the shuffle is rolled once per save, so a node you build during normal
> play is left alone. But if you **re-roll**, an unoccupied built node is shuffled
> just like any world node — to keep one exactly as built, place a miner on it
> first (occupied nodes are always pinned and never moved or retyped).

## Configuration

All settings are in the in-game **Mods → Node Shuffle** panel (and persist to
`<game>/FactoryGame/Configs/NodeShuffle.cfg`), in three groups: **the shuffle**,
**other mods**, and **troubleshooting** — 15 settings plus the protected-resource
list. There is no resource-wells group: since 1.4.0 wells are part of the shuffle
and have no switches of their own (see **Resource wells** below).

### The shuffle

| Setting | Default | Meaning |
|---|---|---|
| Enabled | on | Master switch. Off = nothing spawns and no vanilla nodes deactivate (resource changes already stored in the save persist). |
| Seed Override | 0 | 0 = random seed at first roll; non-zero = fixed seed. To apply a new seed to an existing save, set it and use **Re-roll Layout**. |
| Re-roll Layout | off | Turn on to re-roll the whole layout **once** (using Seed Override, or a fresh random seed if 0), then it auto-turns itself off. Applies **live** within a few seconds if toggled in-game, or on the next load otherwise. New locations are map-wide and **reveal as you explore near them** (even previously-visited areas), so the world looks emptier right after. Nodes with a miner on them are kept. |
| Active Percent Of Node Pool | 70 | % of all locations (vanilla + new) that are active. |
| New Node Locations | 100 | How many extra locations to generate (0–300). |
| Minimum Active Nodes Per Resource | 5 | Completability floor for vanilla resources. |
| Minimum Active Nodes Per Modded Resource | 2 | Same floor for resources added by other mods (0 = no floor). |
| Randomize Purity | on | Shuffle purities, dealt from the vanilla distribution (overall balance preserved). |
| Starter Nodes Near Spawn | on | New game only: place a small starter set (2 Iron, 2 Limestone, 1 Copper, Pure) near spawn. |
| Starter Node Radius (m) | 200 | How far from spawn the starter nodes may sit. |
| Spawn-On-Discovery Radius (m) | 600 | New nodes materialize once you come within this range and the terrain has streamed in. |

### Other mods

| Setting | Default | Meaning |
|---|---|---|
| Include Modded Nodes | on | Shuffle nodes added by other mods too; their solid nodes also **relocate** on a re-roll, like vanilla nodes. |
| Unlock Scanner Knowledge For Shuffled Modded Resources | on | Registers shuffled modded resources with the resource scanner so it can find them. Some overhaul mods also gate **miner placement** on scanner knowledge, so this can allow placing miners on modded ores earlier than that overhaul's own research intended (crafting stays gated). Turn off to let each mod's own progression grant this. Vanilla resources are never affected. |
| Protect Other Mods' Nodes | **on** | Some overhaul mods — Satisfactory Plus is the known case — remove other mods' resource nodes. When on, NodeShuffle **answers those removal checks** for the resources ticked in the list below (refusing the handler's condition — never a claim that it "stops removal" outright). Off, NodeShuffle answers nothing and each mod's own handler decides; the list still fills itself in either way. Takes effect the next time you load the save; doesn't bring back nodes already removed. |

**Protect Other Mods' Nodes (Per Resource)** — the self-filling list under the checkbox above: a
resource appears once another mod's node handler has checked nodes of that resource at least once,
so it starts empty and grows as you play alongside other mods. Every row defaults to ticked
(protected); untick one to let that resource's own mod decide for itself. The console variable
`NodeShuffle.DestroyerVeto` still overrides the checkbox above whenever it has been explicitly set.

### Resource wells (no settings — always on since 1.4.0)

Resource wells shuffle with everything else. They used to be behind two opt-in
toggles; **both are gone**, and both behaviours now happen on every roll — on a
new save immediately, and on a save started before 1.4.0 from its first
**Re-roll Layout** onwards (nothing about an existing save's wells changes just
by loading it):

- **What a well produces is re-rolled** — a nitrogen well may become a water
  well — dealt from the wells' own existing mix, so a well-only resource never
  runs short.
- **A whole well moves** — core and every satellite, as a rigid body to a new
  site, keeping its exact pattern. All-or-nothing: if the full footprint won't
  fit, no partial well appears.

**A well you have built on is never touched** — a Resource Well Pressurizer on
the core or any Resource Well Extractor on a satellite leaves that whole well
exactly where it is, and the mod re-checks that continuously, so the move
happens by itself once you take the building down.

**Read "Known behaviour" below before you start a save.** A relocated well is
genuinely absent from the world for an **unbounded** time before its replacement
appears, and that now applies to every save rather than to people who opted in.
Two edges are still unverified: **desert-biome wells** may arrive without their
rock graphics, and a relocated well's build area has **not** been tested against
ordinary resource nodes closer than about 15 m — if a Miner won't place on an
ordinary node right beside a relocated well, please report it.

### Troubleshooting

| Setting | Default | Meaning |
|---|---|---|
| Enable Diagnostic Logging | off | Verbose placement/node logging to `FactoryGame.log` for troubleshooting. The mod's fixes work whether this is on or off. |

Generation-time settings (seed, counts, percentages, purity) affect a save only
at its first roll or an explicit re-roll. Toggles like Diagnostic Logging apply
live. Old settings files upgrade automatically — a setting removed in a newer
version is simply dropped from `NodeShuffle.cfg` the next time you load.

### Advanced data files

- `<game>/FactoryGame/Configs/NodeShuffle_GeneratedNodes.json` — a report of the
  generated new-node locations for the current save.
- `<game>/FactoryGame/Configs/NodeShuffle_CustomNodes.json` — optional; provide
  your own `{ "Nodes": [ {"X":..,"Y":..,"Z":..} ] }` to hand-place new nodes
  (applied on a new save or re-roll).
- `<game>/FactoryGame/Configs/NodeShuffle_RockPatterns.json` — optional override
  of rock-mesh name patterns; rarely needed (the mod auto-learns them).
- `<game>/FactoryGame/Configs/NodeShuffle_WaterGrid.json` and
  `NodeShuffle_CaveFloors.json` — the mod's **learned terrain map** (which
  100 m cells are land/water and where cavern floors are). Written as you play
  and merged with the prebuilt map shipped inside the mod; safe to delete (they
  simply re-learn). Not per-save — the terrain is the same across saves.

## How it works (design)

Relocated nodes are spawned **as their own resource-node class**, so modded
nodes keep their native behaviour and accept the right extractors; each is
dressed from an authored visual table, a look **captured from the original
node** (for modded resources the table doesn't cover, e.g. thorium), or a clean
quartz placeholder. Nodes **settle onto the terrain** — slope-fitting, avoiding
water and cliffs, and mapping cavern floors — and the layout is **rolled once
per save from a seed**. A **global terrain map** (water/land and cave floors) is
learned as you play and **shipped prebuilt** inside the mod. See
[DESIGN.md](DESIGN.md) for more detail.

### What needs you nearby, and what doesn't

Two different things get confused here, so plainly:

**Not gated on you at all.** Deciding *which resource goes where* and *hiding the original
rock* cover the **whole map, at load** — every one of the game's resource nodes is already
live the moment your save opens (measured: all 630 of them, in a single frame, across
biomes tens of kilometres apart). You do not have to explore for the shuffle to reach a
node, and exploring does not "unlock" more of it.

**Genuinely gated on you.** *Spawning a replacement rock* and *moving a resource well* both
need the ground itself to be loaded, because we place them with a downward trace and you
cannot trace terrain that isn't there. So a dealt node materialises when you come within
the Spawn-On-Discovery radius, and a well moves when you visit its destination.

**One real exception.** Nodes added by **other mods** are picked up when they appear — and
some mods spawn theirs minutes after the world loads, or only once you research something.
Those can be **added after the first roll has already happened**, so they stay vanilla where
they are until you re-roll. Re-rolling re-scans the live world and brings them in.

### Known behaviour: a relocated resource well is absent until you reach its new site

**This is a deliberate trade-off, not a bug.**

Resource-well relocation is *not* instant, and the
original is removed **as soon as the well is dealt a destination** — not after the
replacement exists. The replacement is only built once you travel to the new site
and its terrain streams in, so between those two moments the well is in
**neither** place: it is genuinely absent from the world. How long that lasts is
**not bounded** — destinations are drawn across the whole map, so a well dealt
somewhere you never visit stays absent for as long as you don't go there.

A well you have **built on** (a Resource Well Pressurizer on the core, or any
Extractor on a satellite) is never removed — the mod re-checks that continuously,
so the move happens by itself once you take the building down. Re-rolling also
re-considers wells that have already moved, dealing each a new destination like
any other node.

**There is no setting that turns wells off on their own.** Until 1.4.0 well relocation was an
opt-in toggle; wells are now a natural part of the shuffle, so this applies to every save the mod
is running on. Building on a well is the only way to pin one well in place — a Pressurizer on the
core (or any Extractor on a satellite) holds that whole well permanently.
Turning **Enabled** off stops the mod rolling, moving or maintaining anything further, but it does
not move anything back — nodes it has already placed stay where they are, and the build-gun and
scanner rules it installs at startup keep applying to them.

## Building

C++ source for the SML starter project (SatisfactoryModLoader). Drop this folder
into `<SatisfactoryModLoader>/Mods/NodeShuffle`, then build the `FactoryEditor`
target and package with Alpakit, or via UAT `PackagePlugin -DLCName=NodeShuffle`.

**Build prerequisites:** the optional `NodeShuffleVetoKBFL` module compiles
against a build-time KBFL header stub that must exist at
`<SatisfactoryModLoader>/Mods/KBFL`. If that folder is missing, copy this repo's
`ThirdParty/KBFL-stub/` there before building (it is the tracked source of
truth; the `Mods/KBFL` copy is deployed-only and gitignored by the engine
repo — see `ThirdParty/KBFL-stub/README-STUB.md`). The stub is never packaged
or shipped.

## Logging

Quiet by default (session summaries only). For troubleshooting, enable
**Enable Diagnostic Logging** in the in-game Mods → Node Shuffle panel (writes
verbose placement / node diagnostics to `FactoryGame.log`; toggles live, off by
default). For detailed per-node console output you can also run
`Log LogNodeShuffle Verbose`.

### Console commands

Open the console (`` ` ``) and run:

- `NodeShuffle.Here` — logs your exact position, the ground slope at your feet,
  and a census of every NodeShuffle node/original within 300 m. Handy when
  reporting a spot that looks off.
- `NodeShuffle.SeedHere` — while standing under a natural rock roof (cave, arch,
  overhang), marks that spot so the shuffle can place nodes in that cavern.

## License

MIT — see [LICENSE](LICENSE).
