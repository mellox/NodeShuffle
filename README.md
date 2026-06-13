# Node Shuffle

A Satisfactory mod (SML 3.12+, game version ≥ 491125 / Satisfactory 1.1) that
**randomizes the planet's resource nodes per save**. It adds new node locations
across the map and shuffles which nodes are active, what resource each carries,
and their purity — so every world is a fresh puzzle, while staying completable.

## What it does

- **Shuffles existing nodes** — each node may become a different resource and
  purity, using the game's own native node-override system (nodes are never
  destroyed, so it's save-safe and reversible).
- **Adds ~100+ new node locations** — spawned across the map, settled onto the
  terrain, visible, solid, and mineable like any vanilla node.
- **Per-save, deterministic** — the layout is rolled once per save from a seed
  and persists; identical on every reload. Never re-rolled unless you opt in.
- **Balance guarantees** — every vanilla resource keeps a configurable minimum
  number of active nodes, so no playthrough becomes impossible.
- **Respects your factory** — any node with a miner or extractor on it is never
  changed, retyped, or removed (checked at roll time and continuously).
- **Radioactivity follows the shuffle** — a node retyped to uranium irradiates;
  one retyped away from it stops.
- **Multiplayer-aware** — all logic is server-side; overrides replicate.

## Configuration

All settings are in the in-game **Mods → Node Shuffle** panel (and persist to
`<game>/FactoryGame/Configs/NodeShuffle.cfg`):

| Setting | Default | Meaning |
|---|---|---|
| Enabled | on | master switch |
| Seed Override | 0 | 0 = random seed at first roll; non-zero = fixed seed |
| Allow Re-roll Of Existing Saves | off | let a new seed re-roll an existing save at next load |
| Active Percent Of Node Pool | 70 | % of the total pool (vanilla + new) that is active |
| New Node Locations | 100 | how many new locations to generate (at first roll) |
| Minimum Active Nodes Per Resource | 5 | completability floor (vanilla resources only) |
| Randomize Purity | on | shuffle purities from the vanilla distribution |
| Allow Vanilla Nodes To Disappear | on | when off, every vanilla node stays active |
| Include Modded Nodes | on | shuffle nodes added by other mods too |
| Swap Node Rock Visuals | on | retyped nodes look like their new resource (off = keep original rock look, still mine the new resource) |

Generation-time settings (seed, counts, percentages) affect a save only at its
first roll or an explicit re-roll.

### Advanced data files

- `<game>/FactoryGame/Configs/NodeShuffle_GeneratedNodes.json` — a report of the
  generated new-node locations for the current save.
- `<game>/FactoryGame/Configs/NodeShuffle_CustomNodes.json` — optional; provide
  your own `{ "Nodes": [ {"X":..,"Y":..,"Z":..} ] }` to hand-place new nodes
  (applied on a new save or re-roll).
- `<game>/FactoryGame/Configs/NodeShuffle_RockPatterns.json` — optional override
  of rock-mesh name patterns; rarely needed (the mod auto-learns them).

## How it works (design)

See [DESIGN.md](DESIGN.md) for the full architecture: the native 1.1 node
override machinery, the donor-stamp visual system (retyped rocks copy a real
rock of the new resource so size is always correct), the split node-visual
architectures, statistical rock-mesh auto-learning, terrain settling, and the
per-save persistence model.

## Building

C++ source for the SML starter project (SatisfactoryModLoader). Drop this folder
into `<SatisfactoryModLoader>/Mods/NodeShuffle`, then build the `FactoryEditor`
target and package with Alpakit, or via UAT `PackagePlugin -DLCName=NodeShuffle`.

## Logging

Quiet by default (session summaries only). For detailed per-node output, run in
the console: `Log LogNodeShuffle Verbose`.

## License

MIT — see [LICENSE](LICENSE).
