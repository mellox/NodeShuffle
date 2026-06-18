# NodeShuffle — Diagnostics Catalog (troubleshooting reference)

All diagnostics are **gated behind the `EnableDiagnostics` config toggle** (Mods → Node Shuffle →
"Enable Diagnostic Logging", default OFF). Turn it on, reproduce the issue, then read the game log:
`%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log` and grep for the marker below. The mod's
actual fixes are always active regardless of this toggle.

This catalog maps **symptom → which diagnostic to enable/restore**. Some heavy probes are removed from
the shipping build to keep it lean; for those, the "Restore from" column gives the git commit on branch
`feature/shuffle-modded-nodes` whose version of the file you can cherry-pick the logging back from.

> When ADDING a new diagnostic during troubleshooting, append a row here. When REMOVING one, move its row
> to the "Removed (restore on demand)" section with the commit hash — never delete the knowledge.

---

## Symptom → Diagnostic

| Symptom / situation | Marker (grep) | File | What it tells you |
|---|---|---|---|
| **Miner Mk.1 won't place/snap on a relocated node** | `HOLOGRAMHOOK` (incl. `IsValidHitResult`, `TrySnapToActor`, `SNAP-RES`, `ACCEPTANCE`, `ACCEPT-NODE`, `ACCEPT-EXT`) | NodeShuffle.cpp | Full placement funnel: does the build trace hit our node, is the hit valid, does the snap succeed, and **which acceptance sub-condition rejects** (incl. the Miner's `mRestrictToNodeType`/`mAllowedResourceForms` and node-side `canPlaceExtractor`/form/occupied). This is the probe that cracked the Mk1 saga. |
| Mk1 snap: node not in the manager's registry | `REGDIAG` | NodeShuffleSubsystem.cpp | `mResourceNodes` before/after, whether OUR nodes are in the list the hologram queries. |
| Mk1 snap: node validation (can-place/occupied/purity/class) | `VALIDDIAG` | NodeShuffleSubsystem.cpp | Per-node `CanPlaceExtractor`/`CanBecomeOccupied`/`IsOccupied`/form/purity/`mCanPlaceResourceExtractor`/resClass. |
| **Shuffled rock is a flat/uniform color** | `ROCKDIAG` | NodeShuffleResourceNode.cpp / NodeShuffleSubsystem.cpp | Per-node mesh, mesh slot count vs table material count, and the material applied to each slot + its source (table vs back-fill vs mesh default). Reveals the missing "Middle"/core material. |
| **Miner snaps off-center on the rock** | `ROCK-CENTER` (and `SNAP-CENTER` if present) | NodeShuffleSubsystem.cpp / NodeShuffle.cpp | Node origin vs rendered rock bounds-center vs the resolved snap location — names the mesh-pivot offset. |
| **Collision box "rises up" / player bumps it / must jump** | `USEBOX-RISE` | NodeShuffleSubsystem.cpp | UseBox scaled extent, box top Z vs node Z vs terrain Z (how far it protrudes), and the Pawn/WorldDynamic/WorldStatic responses (the real blocking channel). |
| Spawned node collision/structure (Mk1 geometry era) | `COLLDIAG`, `SNAPDIAG` | NodeShuffleSubsystem.cpp | Full collision dump of the node's components (profile, objType, per-channel responses) and the box/root/mBoxComponent wiring vs a vanilla node. |
| **Modded / "dirty" (AllMinable esc_) nodes not entering the shuffle** | `Modded-node eligibility`, `UPSTREAM-SCAN`, `ESC-HIERARCHY` | NodeShuffleSubsystem.cpp | Per-node-class admit/reject + gate reason; the raw pre-eligibility scan; and the esc_ class super-chain (esc_ nodes are AFGResourceNodeBase but NOT AFGResourceNode). |
| Re-roll didn't re-capture loaded nodes | `Full re-scan augment`, `Re-roll pool`, `Re-roll restore` | NodeShuffleSubsystem.cpp | Counts of vanilla/modded solids re-added on re-roll, pool rebuild size, originals un-hidden. |
| **Original nodes linger visible at old locations after relocate/re-roll** | `HIDEDIAG funnel`, `HIDEDIAG census`, `Hide originals` | NodeShuffleSubsystem.cpp | THE hide probe. Funnel: of records near a player, found-by-path / already-hidden / occupied / missed. Census: categorizes every node near the player (ours vis/hidden, uncapturedVisible, shouldHideButVisible). **Caveat: the census's "uncapturedVisible" uses `Rec.Location` and mislabels correctly-captured nodes after a re-roll — trust `Hide originals: hid N` climbing, not the census.** Also: `BP_ResourceDeposit_C` deposits are never shuffled and always show as census "uncaptured". |
| Node cache not finding originals to hide | `CACHE-REFRESH` | NodeShuffleSubsystem.cpp | Newly-streamed originals added to `VanillaNodeCache` (the path→node lookup). NOTE the count is cumulative (cache never evicts) — not a live "loaded now" count. |
| "Are uncaptured nodes real or already captured?" | `CAPTUREDIAG` | NodeShuffleSubsystem.cpp | Per-pass capture count + **rejection funnel** (type/ineligible/inKnownPath/nearKnownLoc) with a sample. The `inKnownPath` vs `ineligible` split is what proved nodes were already captured (just unhidden), not uncaptured. |

## Removed (restore on demand)

These were removed in `cleanup-1` to keep the build lean. To bring one back, `git show <commit>:<path>`
on branch `feature/shuffle-modded-nodes` and copy the block.

| Diagnostic | Restore from commit | Why removed | What it did |
|---|---|---|---|
| `HIDEDIAG census` (per-pass scene census: oursVisible/uncapturedVisible/shouldHideButVisible) | `72bd1e7` (hidefix-1) | Heavy (O(actors×records) per pass) AND **mislabeled** captured nodes as "uncaptured" because it read the same stale `Rec.Location` as the bug. It chased a phantom for several builds. | Categorized every node near the player. Only re-add if you need to know what *class* of node is visible near the player and you account for the stale-location caveat. |
| `CAPTUREDIAG` (capture-on-discovery rejection funnel) | `72bd1e7` (hidefix-1) | Belonged to the capture-on-discovery feature, which was removed (proven unnecessary — 0 truly-uncaptured nodes). | Per-pass capture count + rejection funnel (type/ineligible/inKnownPath/nearKnownLoc). Its `inKnownPath` vs `ineligible` split is what proved the nodes were already captured. Genuinely useful if a future "node not entering pool" bug appears — re-add the funnel pattern. |
| `CaptureUndiscoveredNodes` feature + toggle | `72bd1e7` (hidefix-1) | Built on the false premise that nodes were uncaptured; the real bug was the hide proximity gate (see [[nodeshuffle-stale-record-hide]]). 0 nodes were ever actually uncaptured. | Discovery-tick capture of streamed-in originals. Only resurrect if a future diagnostic proves genuinely-uncaptured (not just unhidden) nodes exist. |

## Key lessons baked into these probes
- **Instrument the exact failing sub-condition, not a proxy.** The `ACCEPT-EXT` and `CAPTUREDIAG`
  rejection funnels each ended a multi-build guessing saga in one log line.
- **A diagnostic that reads the same buggy field as the bug will lie.** The `HIDEDIAG census` used the
  stale `Rec.Location` and reported phantom "uncaptured" nodes — chasing it wasted builds. Always sanity
  the metric's own inputs.
- Resource nodes DO stream (the cumulative cache count looked "stable" but wasn't a live count); deposits
  are intentionally never shuffled.
