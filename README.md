# Node Shuffle

A Satisfactory mod (requires SML 3.12.1 or later and game build 520140 or later) that
**randomizes the planet's resource nodes per save**. It **relocates nodes to new
locations** across the map and shuffles which are active, what resource each
carries, and their purity — for **solids, oil/liquids, and modded nodes** alike —
so every world is a fresh puzzle, while staying completable. It runs on the
standard map and on **Random World Generator** worlds (see below).

NodeShuffle 1.4.0 was built for an older game version; on game build 513885 it crashed a few seconds after a world loaded. Use 1.5.0 or later on the current game.

NodeShuffle 1.5.0 is built for Satisfactory 1.2.6 (game build 520140) and needs SML 3.12.1 or later. A mod with C++ code that was built for an older game build can crash on this one until its author updates it; if the game crashes when you look at or use something another mod adds, check that the mod has a build for 1.2.6.

## Compatibility — Satisfactory Plus and other overhauls

NodeShuffle is built to run alongside the Satisfactory Plus ecosystem: it writes the extractor
compatibility patches an overhaul needs while you play (restart the game once to pick them up), and
protection for other mods' nodes is on by default.

No overhaul combination has been re-tested on the current game version. The versions last confirmed
working with NodeShuffle, before the game's August 2026 update, were KBFL 2026.2.267, MkPlusSFPlus
1.3.0-alpha and KPatchwork 2026.3.26 (and earlier). That is a historical record, not a current pass.

## Random World Generator worlds

NodeShuffle recognises a Random World Generator world and places nodes using the generator's own
terrain data (height, water, void and caves) instead of the prebuilt map of the standard world.

- **Tested version:** Random World Generator 0.2.2 (build 1150), on one world. The mod's list of
  known versions also names 0.1.7, 0.1.9, 0.1.12, 0.2.0 and 0.2.1; those have not been played with
  this NodeShuffle version.
- **A version not on the list:** NodeShuffle checks the generator functions it uses before relying
  on them and posts a chat message saying what it decided. If the check fails, it places nodes with
  its own ground traces for that session instead. If nodes land in water or over void, include the
  Random World Generator version when you report it.
- **First shuffle of a new world:** it waits until the generator reports the world is ready and no
  new nodes have appeared for a minute, then shuffles. The game may pause briefly when it does.
- **Placement:** nodes are spawned across the whole map, not only near you, and settle onto the
  ground when you come near them.
- **Settings that only act on these worlds** carry "(Random World Generator)" in their name: the
  status panel, spreading nodes by land, and the vanilla counts for modded resources. The status
  panel is shown only on Random World Generator worlds, only for the first shuffle of a new world,
  and only on the host's screen.

## What it does

- **Relocates & shuffles nodes** — nodes move to new map-wide locations and may
  change resource and purity. This covers solid ore, **oil/liquid and gas nodes**
  (minable with the matching extractors), and **modded nodes** added by other
  mods. Originals are hidden, never destroyed. Uninstalling stops all relocation
  and no new nodes appear, but resource changes already written onto vanilla
  nodes stay in that save.
- **Adds new node locations** (100 by default, up to 300) — spawned across the
  map, settled onto the terrain, visible, solid, and mineable like any vanilla node.
- **Places nodes where they belong** — relocated nodes **conform to slopes and
  hillsides**, stay **out of water** and **off sheer cliff faces**, and can land
  on **cavern floors** the mod maps as you explore (so caves keep resources
  instead of being emptied by the shuffle). A node that lands somewhere a miner
  can't go re-deals itself to better ground.
- **Ships a prebuilt terrain map of the standard map** — a snapshot of discovered
  water and cave areas travels inside the mod, so a **fresh install places nodes
  well from the first launch** instead of learning the map from scratch; your own
  exploration keeps refining it. Random World Generator worlds use the
  generator's terrain data instead.
- **Fully buildable** — relocated and new nodes accept hand-mining, portable
  miners, **and Miner buildings (Mk.1 and up)**; placed miners keep their node
  through save/load and re-roll.
- **Starter nodes (optional)** — on a brand-new game, a small guaranteed set
  near spawn (Iron/Limestone/Copper) keeps the early game playable.
- **Per-save, deterministic** — the layout is rolled once per save from a seed
  and persists. With modded-node inclusion enabled, eligible additions are captured when the world is ready and then about every 60 game seconds for the rest of the session; once a session's scans have added 2048 or more layout entries, scanning stops until the next load. Existing entries are not re-rolled unless you opt in.
- **Balance guarantees** — every vanilla resource keeps a configurable minimum
  number of active nodes, so no playthrough becomes impossible.
- **Respects your factory** — any node with a miner or extractor on it is never
  changed, retyped, or removed (checked at roll time and continuously).
- **Radioactivity follows the shuffle** — a node retyped to uranium irradiates;
  one retyped away from it stops.
- **Tells you what it dealt** — after every shuffle or re-roll, a chat message
  lists each resource in the new layout and how many nodes it got.
- **Multiplayer-aware** — all logic is server-side; overrides replicate. Not
  tested in multiplayer or on a dedicated server with this version.

> **Note on player-built nodes** (if you use a mod that lets you *build* resource
> nodes): with modded-node inclusion enabled, newly discovered eligible nodes can join at any of those scans. Occupied nodes are recorded in place; unoccupied nodes present at a scan can move. With Enable Diagnostic Logging on, each scan's duration and additions are logged.

## Configuration

All settings are in the in-game **Mods → Node Shuffle** panel (and persist to
`<game>/FactoryGame/Configs/NodeShuffle.cfg`), in three groups: **the shuffle**,
**other mods**, and **troubleshooting** — 18 settings plus two lists: the protected-resource list and the modded-resource vanilla-count table. There is no resource-wells group: since 1.4.0 wells are part of the shuffle
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
| Randomize Purity | on | Shuffle node purities too. Each dealt node's purity is drawn from the purities of that resource's original nodes on this map. Applied when the layout is rolled. |
| Starter Nodes Near Spawn | on | New game only: place a small starter set (2 Iron, 2 Limestone, 1 Copper, Pure) near spawn. |
| Starter Node Radius (m) | 200 | How far from spawn the starter nodes may sit. |
| Spawn-On-Discovery Radius (m) | 600 | New nodes materialize once you come within this range and the terrain has streamed in. On Random World Generator worlds nodes are spawned map-wide and settle onto the ground as you come near. |
| Spread Nodes Evenly By Land (Random World Generator) | on | Random World Generator maps only, when a layout is rolled or re-rolled: the map is divided into 1 km squares and each square's share of the nodes is in proportion to how much of it the shuffle can place on (not sea, void or too steep), measured at 16 points per square. Nodes you have built on count toward their square's share first, so a square already full of them gets few or no fresh spots. If the deal runs low on draws before every share is filled, the remaining spots are placed anywhere on placeable land, so the configured total is still dealt. Off: spots land anywhere on placeable land with no per-square limit, as before. Takes effect at the next roll or Re-roll Layout; other maps are not affected. |
| Show Status Panel (Random World Generator) | on | Random World Generator worlds only: while the first shuffle of a new world waits, and for up to ten minutes after it, a small panel on the host's screen says what Node Shuffle is doing. It is hidden while the mouse cursor is shown (menus). Other players do not see the panel. Off hides the panel; the chat lines still post. |

### Other mods

| Setting | Default | Meaning |
|---|---|---|
| Include Modded Nodes | on | Shuffle nodes added by other mods too; their solid nodes also **relocate** on a re-roll, like vanilla nodes. |
| Unlock Scanner Knowledge For Shuffled Modded Resources | on | After a world loads, and again after a re-roll or when NodeShuffle finds new nodes, adds the modded resources it shuffles in that world to the game's list of researched scanner resources, unless a resource is already on it. When the KAPI library is loaded, a resource is added only if KAPI has miner data for it or NodeShuffle can supply that data. Added resources are stored in the save and NodeShuffle never removes them, so turning this off later does not take them back. Some overhaul mods also gate **miner placement** on this list, so this can allow placing miners on modded ores earlier than that overhaul's own research intended (crafting stays gated). Base-game resources are never added. To show or hide a resource in the scanner without writing to the save, use **Show in scanner** in the list below. |
| Protect Other Mods' Nodes | **on** | Some overhaul mods — Satisfactory Plus is the known case — remove other mods' resource nodes. When on, NodeShuffle **answers those removal checks** for the resources ticked in the list below (refusing the handler's condition — never a claim that it "stops removal" outright). Off, NodeShuffle answers nothing and each mod's own handler decides; the list still fills itself in either way. Takes effect the next time you load the save; doesn't bring back nodes already removed. |
| Use Vanilla Counts For Modded Resources (Random World Generator) | on | ON by default. Random World Generator maps only, at each layout roll or re-roll: each modded resource listed in the Modded Resource Vanilla Counts table gets its vanilla map node count, scaled by how many vanilla ore nodes this world has compared with the vanilla map, in place of the number of its nodes NodeShuffle found on this world, when deciding how many nodes of it the shuffle deals. A listed gas resource with no node on this world gets new nodes of its own node class (one NodeShuffle finds in that resource's mod, otherwise the class listed in the row); if neither is usable, none are added. Other maps are not affected. |
| Modded Resource Vanilla Counts (Random World Generator) | Lead 6, Lithium 23, Thorium 20 | Used on Random World Generator maps only, at each layout roll or re-roll, while 'Use Vanilla Counts For Modded Resources' is on. Each row names a modded resource, an optional node class, and a node count to scale (pre-filled with the vanilla map's count, except thorium, set lower). That number is scaled by how many vanilla ore nodes this world has compared with the vanilla map and replaces the resource's own node count in the shuffle. A gas resource with no node on this world gets new nodes of its own node class (one NodeShuffle finds in that resource's mod, otherwise the class in the row); if neither is usable, nothing is added. Pre-filled: Lead 6, Lithium 23, Thorium 20 (thorium is set lower than the 45 counted on one vanilla-map save). |

**Protect Other Mods' Nodes (Per Resource)** — the self-filling list under the checkbox above: a resource appears once another mod's node handler has checked nodes of that resource, or once NodeShuffle shuffles a modded resource or the scanner offers one, so it starts empty and grows as you play alongside other mods. Every row defaults to ticked
(protected); untick one to let that resource's own mod decide for itself. The console variable
`NodeShuffle.DestroyerVeto` still overrides the checkbox above whenever it has been explicitly set.

Each row also has a **Show in scanner** switch: on shows that resource in the resource scanner wheel
(adding it if the game's list lacks it), off hides it. It applies in a running game and writes nothing
to the save. A new row starts hidden for All Minable's resources; until you change it, every other row follows whether the game itself lists that resource in the scanner.

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
absent from the world for an **unbounded** time before its replacement
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
at its first roll or an explicit re-roll. After turning on Diagnostic Logging,
load your save again before reproducing a problem. Old settings files upgrade automatically — a setting removed in a newer
version is dropped from `NodeShuffle.cfg` the next time you load.

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
  re-learn). Not per-save — on the standard map the terrain is the same across
  saves. Random World Generator worlds do not use these files.

## How it works (design)

Relocated nodes are spawned **as their own resource-node class**, so modded
nodes keep their native behaviour and accept the right extractors; each is
dressed from an authored visual table, a look **captured from the original
node** (for modded resources the table doesn't cover, e.g. thorium), or a clean
quartz placeholder. Nodes **settle onto the terrain** — slope-fitting, avoiding
water and cliffs, and mapping cavern floors — and the layout is **rolled once
per save from a seed**. On the standard map, a **global terrain map** (water/land
and cave floors) is learned as you play and **shipped prebuilt** inside the mod;
Random World Generator worlds use the generator's own terrain data. See
[DESIGN.md](DESIGN.md) for more detail.

### What needs you nearby, and what doesn't

Two different things get confused here:

**Not gated on you at all.** Deciding *which resource goes where* and *hiding the original
rock* cover the **whole map, at load** — every one of the game's resource nodes is already
live the moment your save opens (measured once on the standard map: all 630 of them, in a
single frame, across biomes tens of kilometres apart). You do not have to explore for the shuffle to reach a
node, and exploring does not "unlock" more of it.

**Gated on you.** *Spawning a replacement rock* and *moving a resource well* both
need the ground itself to be loaded, because we place them with a downward trace and you
cannot trace terrain that isn't there. So on the standard map a dealt node materialises
when you come within the Spawn-On-Discovery radius, and a well moves when you visit its
destination. On Random World Generator worlds nodes are spawned map-wide and settle onto the
ground when you come near.

**One real exception.** Nodes added by **other mods** are picked up when they appear — and
some mods spawn theirs after the world loads, or only once you research something. With **Include Modded Nodes** on, NodeShuffle looks for new nodes when the world is ready and then every 60 game seconds for the rest of the session, and adds them to the layout without re-rolling anything else. Once 2048 layout entries have been added this way in one session, it stops looking until the next load.

### Known behaviour: a relocated resource well is absent until you reach its new site

**This is a deliberate trade-off, not a bug.**

Resource-well relocation is *not* instant, and the
original is removed **as soon as the well is dealt a destination** — not after the
replacement exists. The replacement is only built once you travel to the new site
and its terrain streams in, so between those two moments the well is in
**neither** place: it is absent from the world. How long that lasts is
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
verbose placement / node diagnostics to `FactoryGame.log`; off by default), then
load your save again before reproducing the problem. For detailed per-node console output you can also run
`Log LogNodeShuffle Verbose`.

### Console commands

Open the console (`` ` ``) and run:

- `NodeShuffle.Here` — logs your exact position, the ground slope at your feet,
  and a census of every NodeShuffle node/original within 300 m. Handy when
  reporting a spot that looks off.
- `NodeShuffle.SeedHere` — while standing under a natural rock roof (cave, arch,
  overhang), marks that spot so the shuffle can place nodes in that cavern.
- `NodeShuffle.AuditPlacements` — logs, over every node in the layout, counts of how many read as in water, below the terrain or enclosed.
- `NodeShuffle.DumpExtractors` — logs the extractors, nodes and compatibility
  state the mod can see.
- `NodeShuffle.NearestCave` — Random World Generator worlds: logs the nearest cave
  mouth and the nearest node dealt into a cave, with distance and bearing.

## License

MIT — see [LICENSE](LICENSE).
