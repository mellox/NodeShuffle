# Changelog

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
  wells missing again. Nothing turns this off; building on a well is the only way to pin it.
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
