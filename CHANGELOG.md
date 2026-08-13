# Changelog

## 1.4.0 — 2026-08-12

### New

- **Resource wells shuffle too, and there is nothing to switch on.** What each resource well yields is
  re-rolled — a nitrogen well may become a water well — and the overall mix is preserved, so a save is
  never short of a well-only resource such as Nitrogen Gas. Whole wells also **move**: core and every
  satellite travel to a new place as one rigid body, keeping the satellites' exact spacing and rotating
  the group to fit the terrain. A relocated well is dressed and buildable — its rocks and cracks are
  rebuilt at the new site, and a Pressurizer and its Extractors snap to it and produce. A well with a
  Pressurizer or any Well Extractor on it is never changed and never moved.
- **Read this before you start a save: a moving well is genuinely gone in the meantime.** The original
  is removed as soon as the well is dealt a destination, and the replacement is only built once you
  travel to the new spot, so the well is in **neither** place in between — and how long that lasts is
  **not bounded**, because destinations are drawn across the whole map. Re-rolling re-considers wells
  that have already moved, so a re-roll sends most of them missing again until you find each one. There
  is no setting that turns this off; building on a well is the only way to pin it. Two edges are still
  unverified: desert-biome wells may arrive without their rock graphics, and a relocated well's build
  area has not been tested against ordinary nodes closer than about 15 m.
- **Other mods' nodes are protected.** Some overhaul mods sweep the map early on a new game and remove
  resource nodes added by other mods — lead, lithium and similar types could be gone before you ever saw
  them. NodeShuffle now answers that check for the resources it manages, so they survive. **This is on by
  default**, and there is a per-resource list in the settings if you would rather let a particular mod's
  own progression decide.
- **Modded resources are registered with the scanner** (**Unlock Scanner Knowledge**, on by default), so
  scanners recognise them. Note that some overhaul mods also gate *miner placement* on scanner knowledge,
  so this can let you place miners on modded ores earlier than that mod intended — turn it off if you
  prefer their progression. Vanilla resources are never affected.
- **The mod now talks to you in chat** when it has written a compatibility document and the game needs a
  restart before another mod will accept it, naming the buildings and resources involved. Previously this
  showed up only as a red hologram with no explanation.

### Changed — read this before you load an existing save

**Settings.**

> **1.4.0 — settings cleanup.** The mod settings page is shorter and re-grouped: 15 settings in three
> groups, and the resource-wells group is gone entirely. Seven options were removed. Five had their
> behaviour fixed at the setting they shipped with; three changed on purpose. **Protecting other mods'
> nodes is now ON by default** (there is a new tick box for it — it used to need a console variable, and
> it was off unless you set one). **Resource wells are now part of the shuffle and cannot
> be turned off on their own**: wells are retyped and they move on every new save, and on an existing
> save from the first **Re-roll Layout** onwards; the two toggles that used to gate them are deleted. **Re-rolling the layout also re-rolls wells that had already moved**, so on a save with
> moved wells the first re-roll will move most of them again and each is gone from the map until you go
> and find it. If you had silenced the compatibility chat notices, they are back on. Existing settings
> files are upgraded automatically on first load.

*Coming from 1.3.0 specifically:* two settings are gone — **Enable Experimental Features** and **Allow
Vanilla Nodes To Disappear**. (The two well toggles never appeared in a public release either: wells
arrive already always-on.) Both are hard-wired to the value they shipped with, so nothing about your
world changes. "Allow Vanilla Nodes To Disappear" never did what its name suggested — with it off,
vanilla nodes still moved and still got retyped; it only forced them to stay active. The rest of the
paragraph above describes changes relative to development builds; the three other removed options never
appeared in a public release.

- **A node that another mod destroys twice now goes dormant for the rest of the session** instead of
  being respawned into a destroy-and-respawn loop. Nodes you have a miner on are never affected.
- **New node locations now take their type from the resource, not from its form.** Coal and lithium used
  to share one node type and whichever won was wrong for the other; each new node is now genuinely its
  own type, so other mods' placement rules pass or fail on the node's own merits.

### Improved

- **Compatibility with overhaul mods works by rule, not by a list.** Extractors are admitted to an
  overhaul's allowed-extractor list if they natively accept a node type NodeShuffle manages, instead of
  being named one by one. New mods, new extractor tiers and renamed classes now work with no update from
  us. A hand-maintained list that had to be re-installed by hand after every update is gone.
- **The settings page is shorter and grouped** — the shuffle, other mods, resource wells, and
  troubleshooting. The per-resource protection list shows each entry as `Mod: Resource` with a tick box,
  sorted by mod, and the full path in the tooltip.
- **Troubleshooting commands.** `NodeShuffle.AuditPlacements` reports how every placed node was decided,
  and `NodeShuffle.DumpExtractors` lists the extractors, nodes and compatibility state the mod can see —
  both useful to paste into a bug report alongside a log with **Enable Diagnostic Logging** on.

### Fixed

- **Crash when saving.** Saving with a modular miner built on a shuffled modded node could crash the
  game outright. Fixed.
- **Crash on load with modded ores.** The scanner-knowledge unlock could trip a hard assertion in another
  mod's miner for modded resources it had no entry for. The mod now provides those entries first and
  never unlocks a resource it could not provide one for.
- **Higher-tier well machines would not place** on a well the mod had retyped — the crash guard added in
  1.2.1 was skipping every well machine, not just the mismatched ones. It is now a precise rule: the
  machine must be the right kind for that part of the well.
- **Miners reading "Invalid" on older saves** now re-bind themselves at load instead of needing a rebuild.
- **Ghost rocks at old node locations.** A rock that streamed in *after* its node was hidden used to
  arrive visible and stay there; it is now hidden when it appears. The same fix covers nodes from other
  mods, whose records could not be matched across a restart.
- **The resource scanner could not find newly unlocked modded resources** until you saved and reloaded.
- **Alternating between two saves broke modded extractor placement for one boot, every time** — each save
  was overwriting the compatibility documents the other one needed. Each playthrough now keeps its own.
- **A periodic stutter** during play, from the mod's upkeep pass repeating work it had already done.
- **A flood of warnings** when a node type refused to spawn. Those nodes now fall back to a type that
  works — with the same resource and the same balance — instead of vanishing quietly.
- **Well fixes** (only reachable with the well settings on): a relocated well could not be detected as
  built-on, could be dealt a resource it cannot hold, could validate inside a crevice, or could appear
  wearing another resource's rocks. All fixed.

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
