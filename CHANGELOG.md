# Changelog

## 1.5.0 — 2026-11-22

**Requires game build 520140 or later and SML 3.12.1 or later.** 1.4.0 was built for an older game version; on game build 513885 it crashed a few seconds after a world loaded, and it has not been tested on later builds. 1.5.0 is built for game build 520140.

**What has been tested.** This version has been played on the standard map (one new game, with 300
new node locations, 100% active and tiers unlocked: first shuffle, the chat summary, two extractors
placed) and on one Random World Generator 0.2.2 world, including one re-roll. Each item below was played in game at least once while it was being made. The status panel, map-wide spawning, Show in scanner, the vanilla counts, spreading by land and late-node pickup were last played on test builds before this one, on Random World Generator worlds. Not tested yet with this version: re-rolling, or saving and
reloading, on the standard map; the default settings; a new game without unlocked tiers; multiplayer
and dedicated servers; Satisfactory Plus or KBFL installed alongside it.

### New

- **Random World Generator worlds are supported.** NodeShuffle recognises a Random World Generator
  world and places nodes with the generator's own terrain data (height, water, void and caves). The
  version tested is Random World Generator 0.2.2 (build 1150). On a version that is not on the mod's
  list, it checks the generator functions it uses before relying on them and tells you in chat what it
  decided; if the check fails, it places nodes with its own ground traces instead. On these worlds the
  first shuffle of a new world waits until the generator reports it is ready, and nodes are spawned
  across the whole map, not only near you, then settle onto the ground as you come near them.
- **Status panel (Random World Generator worlds only).** While the first shuffle of a new world waits,
  and for up to ten minutes after it, a small panel on the host's screen says what NodeShuffle is
  doing. Setting: **Show Status Panel (Random World Generator)**, on by default.
- **A chat summary after every shuffle** lists each resource the layout dealt and how many nodes it got.
- **Show in scanner, per modded resource.** Each row of the **Protect Other Mods' Nodes (Per
  Resource)** list now has a **Show in scanner** switch that shows or hides that resource in the
  resource scanner wheel. It applies in a running game and writes nothing to the save. All Minable's resources start hidden; until you change it, every other row follows whether the game itself lists that resource in the scanner.
- **Modded resources on Random World Generator maps get vanilla-map node counts, on by default.** Random World
  Generator places modded ores from its own templates, so a resource can end up with far more nodes than the vanilla
  map has (lead: 104 on one tested world, against 6 on the vanilla map). A new table, **Modded Resource Vanilla
  Counts**, gives each listed modded resource its vanilla map node count, scaled by how many vanilla ore nodes the
  world has compared with the vanilla map, so it follows your Random World Generator amount settings the way the
  vanilla ores do. The shuffle then deals that many instead; your density and minimum-per-resource settings still
  apply. Pre-filled: Lead 6, Lithium 23, Thorium 20 (thorium is set lower than the 45 counted on one vanilla-map save). On a world with lead, lead nodes become far fewer.
- **A listed gas resource with no node on the world** (Lithium is the known case) gets new nodes of its own node
  class. If no usable node class is found, nothing is added for that resource.
- Both act only when a layout is rolled: a new Random World Generator world at its first roll, an existing one at
  its next **Re-roll Layout**. Loading a save changes nothing, and other maps are not affected. Turn off **Use
  Vanilla Counts For Modded Resources** before re-rolling to keep the counts Random World Generator planned.
- **Random World Generator maps: new node spots are spread by land, and nodes you have built on count first.** The
  map is divided into 1 km squares, and each square's share of the nodes is in proportion to how much of it the shuffle
  can place on (not sea, void or too steep), measured at 16 points per square. Nodes you have built on count toward
  their square's share before any fresh spot is dealt there, so a built-up base gets few or no fresh spots on top of its
  kept nodes, and a square that is mostly sea gets a small share. If the deal runs low on draws before every share is
  filled, the remaining spots are placed anywhere on placeable land, so the configured total is still dealt.
  Setting: **Spread Nodes Evenly By Land (Random World Generator)**, on by default; off places spots anywhere on
  placeable land with no per-square limit. Takes effect at the next roll or **Re-roll Layout**. The same Seed Override can give a different layout with this on than with it off.

### Changed

- **Purity is dealt per resource, on every map.** With **Randomize Purity** on, each dealt node's purity is drawn from the purities of that resource's original nodes on the map, instead of from one map-wide mix (a resource with no original purities still uses the map-wide mix). A fixed Seed Override therefore gives a different layout than it did in 1.4.0. Saved layouts keep their purities until the next roll.

### Fixed

- **Modded nodes that appear after the world loads are no longer left in place.** Some mods spawn
  their nodes a little after the world is ready (All Minable is one), and those used to stay where
  they were, unshuffled. With **Include Modded Nodes** on, NodeShuffle now looks for new nodes when the world is ready and then every 60 game seconds for the rest of the session, and adds them to the layout without re-rolling anything else. Once 2048 layout entries have been added this way in one session, it stops looking until the next load.

## 1.4.0 — 2026-08-13

### New

- **Resource wells shuffle too.** A well's resource is re-rolled (nitrogen may become water) with the
  overall mix preserved, and whole wells move as one rigid body — core and satellites together — to a
  new site that is dressed and buildable. A well you have built on is never touched; a new game shuffles
  wells from the start, an existing world from the first **Re-roll Layout**. Saving and loading
  never re-rolls anything.
- **Read this first: a moving well is gone in the meantime.** The original is removed the
  moment a destination is dealt and the replacement is only built when you travel there, so it is in
  **neither** place in between, for an **unbounded** time — and a re-roll sends most already-moved
  wells missing again. There is no setting that turns this off; building on a well is the only way to
  pin it.
  (Unverified edges: desert-biome wells may arrive without rock graphics, and a relocated well's build
  area is untested against ordinary nodes within ~15 m.)
- **Other mods' nodes are protected, on by default.** Some overhaul mods sweep the map on a new game
  and remove other mods' nodes — lead, lithium and similar could vanish before you ever saw them.
  NodeShuffle answers that check for the resources it manages, and a per-resource list lets you hand
  any of them back.
- **Modded resources are registered with the scanner** (**Unlock Scanner Knowledge**, on by default).
  Some overhauls also gate *miner placement* on scanner knowledge, so this can let you place miners on
  modded ores earlier than intended — turn it off if you prefer their progression.
- **The mod now tells you in chat** when it has written a compatibility document and the game needs a
  restart before another mod will accept it. Previously this was only an unexplained red hologram.

### Changed

**Back up your save before upgrading, and before any downgrade.** On an older NodeShuffle a 1.4.0
world loads, but every well 1.4.0 has moved is absent there; they return when you come back to
1.4.0. A returning well can appear as bare ground at first — see the known-issue note on the mod
page for the quick fix (fly near any fracking well, then reload).

**Settings.** The page is now **15 settings in three groups** — the shuffle, other mods, and
troubleshooting. **Enable Experimental Features** and **Allow Vanilla Nodes To Disappear** are gone,
both hard-wired to the value they shipped with, so nothing about your world changes (the second never
let vanilla nodes disappear — it only forced them to stay active). Old settings files upgrade
automatically on first load.

- **A node another mod destroys twice now goes dormant for the session** instead of looping through
  destroy-and-respawn. Nodes with a miner on them are unaffected.
- **New node locations take their type from the resource, not its form** — coal and lithium used to
  share one node type, which was wrong for one of them.

### Improved

- **Overhaul compatibility works by rule, not by a list.** An extractor is admitted if it natively
  accepts a node type NodeShuffle manages, so new mods, new tiers and renamed classes work with no
  update from us — the hand-installed list is gone.
- **The per-resource protection list reads at a glance** — `Mod: Resource` with a tick box, sorted by
  mod, full asset path in the tooltip.
- **Troubleshooting commands** — `NodeShuffle.AuditPlacements` (how every node was placed) and
  `NodeShuffle.DumpExtractors` (the extractors, nodes and compatibility state the mod can see).

### Fixed

- **Miners reading "Invalid" on older saves** now re-bind at load instead of needing a rebuild.
- **Ghost rocks at old node locations** — a rock that streamed in *after* its node was hidden used to
  arrive visible and stay; other mods' nodes included.
- **A re-rolled SAM node could wear quartz rocks for the rest of the session**, when its captured look
  had aged out of the mod's cache.
- **A periodic stutter** during play, from the upkeep pass repeating work it had already done.
- **A flood of warnings** when a node type refused to spawn — those nodes now fall back to a working
  type with the same resource and balance instead of vanishing quietly.

## 1.3.0

### New
- **Cave nodes.** The shuffle now maps cavern floors as you explore and can place
  relocated nodes inside caves — which it previously emptied, since it hides the
  originals. Nodes only land where a Miner actually fits.
- **Prebuilt terrain map.** A snapshot of discovered water and cave areas ships
  inside the mod, so a **fresh install places nodes well from the first launch**
  instead of learning the map from scratch. Your own exploration keeps refining
  it, and updates fold in more.
- **Console commands.** `NodeShuffle.Here` logs your position, the ground slope
  at your feet, and a census of nearby nodes; `NodeShuffle.SeedHere` marks a
  roofed spot (cave/arch/overhang) so the shuffle can place nodes there.

### Improved
- **Slope-fit placement.** Relocated nodes conform to hillsides and slopes, with
  the node kept upright enough for a Miner to place. Only sheer cliff faces are
  avoided now — the game is full of steep, usable ground.
- **Water avoidance.** Nodes no longer settle on the seabed or underwater; a
  learned land/water map keeps rolls and re-deals on dry, reachable ground — so
  far fewer nodes get stranded offshore and never appear.
- **Modded nodes spawn as their real type.** Nodes like lithium now use their
  native class — correct visual, and correct extractor rules (they reject a
  normal Miner and accept their intended extractor). A modded node dealt a
  vanilla resource now shows that resource's look, not its native placeholder.
- **Captured visuals** for modded resources the mod ships no art for (e.g.
  RefinedPower thorium, modded lead) — taken from the original node so they no
  longer appear as generic quartz.

### Fixed
- **Crash placing a modded extractor.** Building a modded reactive/"exotic"
  extractor (e.g. lithium's) on a shuffled node no longer crashes the game —
  relocated nodes now spawn as their real node type, so the extractor binds to
  them correctly. Modded nodes also stop accepting the wrong miner and stop
  showing a generic quartz rock.
- **Radiation truly follows the shuffle now.** A hidden uranium/thorium node
  used to keep irradiating its old spot even though its rock was gone; it no
  longer does, and relocating a radioactive node moves the radiation with it —
  no more invisible hot zones.
- Relocated nodes avoid steep cliffs, deep water, player buildings, and boxed-in
  rock pockets, and spread more evenly across the map.
