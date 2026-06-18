# Node Shuffle

A Satisfactory mod (SML 3.12+, game version ≥ 491125 / Satisfactory 1.1) that
**randomizes the planet's resource nodes per save**. It **relocates nodes to new
locations** across the map and shuffles which are active, what resource each
carries, and their purity — for **solids, oil/liquids, and modded nodes** alike —
so every world is a fresh puzzle, while staying completable.

## What it does

- **Relocates & shuffles nodes** — nodes move to new map-wide locations and may
  change resource and purity. This covers solid ore, **oil/liquid and gas nodes**
  (minable with the matching extractors), and **modded nodes** added by other
  mods. Originals are hidden, never destroyed, so it stays save-safe and
  reversible (disabling the mod restores the vanilla world).
- **Adds ~100+ new node locations** — spawned across the map, settled onto the
  terrain, visible, solid, and mineable like any vanilla node.
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
`<game>/FactoryGame/Configs/NodeShuffle.cfg`):

| Setting | Default | Meaning |
|---|---|---|
| Enabled | on | Master switch. Off = nothing spawns and no vanilla nodes deactivate (stored changes persist). |
| Seed Override | 0 | 0 = random seed at first roll; non-zero = fixed seed. To apply a new seed to an existing save, set it and use **Re-roll Layout**. |
| Re-roll Layout | off | Turn on to re-roll the whole layout **once** (using Seed Override, or a fresh random seed if 0), then it auto-turns itself off. Applies **live** within a few seconds if toggled in-game, or on the next load otherwise. New locations are map-wide and **reveal as you explore near them** (even previously-visited areas), so the world looks emptier right after. Miners are kept. |
| Active Percent Of Node Pool | 70 | % of all locations (vanilla + new) that are active. |
| New Node Locations | 100 | How many extra locations to generate (0–300). |
| Minimum Active Nodes Per Resource | 5 | Completability floor for vanilla resources. |
| Minimum Active Nodes Per Modded Resource | 2 | Same floor for resources added by other mods (0 = no floor). |
| Randomize Purity | on | Shuffle purities, dealt from the vanilla distribution (overall balance preserved). |
| Allow Vanilla Nodes To Disappear | on | Off = every vanilla node stays active; only new locations roll. |
| Include Modded Nodes | on | Shuffle nodes added by other mods too; their solid nodes also **relocate** on a re-roll, like vanilla nodes. |
| Spawn-On-Discovery Radius (m) | 600 | New nodes materialize once you come within this range and the terrain has streamed in. |
| Enable Diagnostic Logging | off | Verbose placement/node logging to `FactoryGame.log` for troubleshooting. The mod's fixes work whether this is on or off. |
| Starter Nodes Near Spawn | on | New game only: place a small starter set (2 Iron, 2 Limestone, 1 Copper, Pure) near spawn. |
| Starter Node Radius (m) | 200 | How far from spawn the starter nodes may sit. |

Generation-time settings (seed, counts, percentages, purity) affect a save only
at its first roll or an explicit re-roll. Toggles like Diagnostic Logging apply
live.

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

Quiet by default (session summaries only). For troubleshooting, enable
**Enable Diagnostic Logging** in the in-game Mods → Node Shuffle panel (writes
verbose placement / node diagnostics to `FactoryGame.log`; toggles live, off by
default). For detailed per-node console output you can also run
`Log LogNodeShuffle Verbose`.

## License

MIT — see [LICENSE](LICENSE).
