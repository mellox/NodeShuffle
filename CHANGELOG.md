# Changelog

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
  learned land/water map keeps rolls and re-deals on dry, reachable ground.
- **Modded nodes spawn as their real type.** Nodes like lithium now use their
  native class — correct visual, and correct extractor rules (they reject a
  normal Miner and accept their intended extractor). A modded node dealt a
  vanilla resource now shows that resource's look, not its native placeholder.
- **Captured visuals** for modded resources the mod ships no art for (e.g.
  RefinedPower thorium, modded lead) — taken from the original node so they no
  longer appear as generic quartz.

### Fixed
- **Radiation follows the shuffle again.** A hidden uranium/thorium node no
  longer keeps irradiating its old spot — relocating it moves the radiation with
  it, so there are no more invisible hot zones.
- Relocated nodes avoid steep cliffs, deep water, player buildings, and boxed-in
  rock pockets, and spread more evenly across the map.
