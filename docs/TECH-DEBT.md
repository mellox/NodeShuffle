# NodeShuffle — tech debt & known limitations

Living list. Nothing here blocks a release; everything here is a thing we know about,
have decided about, and have not done. **If you find yourself rediscovering an item on
this list, the entry has failed — fix the entry, not just the bug.**

Each item records what it is, how we know, and why it is not fixed. Items with a
**pre-scoped fix** have had the work sized already — start there, don't redesign.

Last updated 2026-08-08.

---

## Accepted trade-offs — decided, not defects

### D1. Resource wells can duplicate if you build on one mid-move
**Decided 2026-08-08 by the mod author: accept, document, do not fix.**
Player-facing explanation is in [README.md](../README.md) under *"Known behaviour"*.

Relocation is player-presence-gated: a well cannot move until its destination's terrain
has streamed, which requires a player to travel there. We therefore hide the original
only **after** the replacement provably exists — hiding on the roll would remove a well
from the save for an unbounded time, possibly permanently (design §5.4, *"all-or-nothing;
fail-safe to the vanilla location"*).

`bPinned` is computed once at roll time (`NodeShuffleWellRoll.cpp:153,188`), and
`ApplyWellRelocation` is spawn-then-suppress (`NodeShuffleWellRelocateApply.cpp:467-473`).
Suppression correctly refuses to hide an occupied member (`:295`). So building on a well
after the roll yields two wells.

**Errs in the player's favour** — we never hide or delete a well someone has built on.

> **Pre-scoped fix if ever revisited:** re-test occupancy immediately before spawning and
> abandon the relocation, returning the dealt card to the deck so the resource assignment
> does not drift by one. Machinery exists (`ClearAbandonedWellPlacement`).
> ⚠ **Verify the absence first.** The "nothing re-checks" claim is five greps and one read,
> not a cold review. *"I could not find a check"* is weaker evidence than *"there is no check"*.

---

## P1 — player-visible, fix before more people use the feature

### T1. The Relocate Resource Wells tooltip states the opposite of what the feature does
`NodeShuffleConfig.cpp:171` still describes a relocated well as *"functional but INVISIBLE"*.
That was true before H2b; relocated wells are now dressed and visible. **This is in the
settings UI**, so it is the copy most likely to reach a player, and it currently tells them
a working feature is broken.

Twin: `NodeShuffleWellSpawn.cpp:393` carries the same stale claim in a log line (lower
priority — log text, not UI).

*Text-only. No rebuild logic, no risk. Deliberately not fixed during testing so the
deployed binary would not drift from the reviewed one.*

---

## P2 — real unknowns, cheap to close

### T2. Desert-biome well meshes are unverified
H2b's mesh pairing narrows on `Contains("Frack")`. That is corroborated **only** for
`SM_FrackingNode_Crack_01` / `_Mid_01` / `_Small_01`. `MT_Desert*` variants exist in the
enum and **no desert fracking mesh name appears anywhere in our evidence**. If desert
wells use differently-named meshes they will relocate undressed.

**Measurement:** relocate a desert well and check for a crack graphic. No graphic *and*
no `spatial REJECT` naming it = the gap is real.

> **Pre-scoped fix (round 13's recommendation):** replace the name filter with a class +
> `mNodeMeshType` gate — type-driven like `IsFrackingActor`, closing the unknown
> *statically* rather than by enumeration. The code already reads `mNodeMeshType`
> (`NodeShuffleWellVisuals.cpp:406`), so the access is proven.

### T3. Snap-box overlap with neighbouring nodes is proven geometrically, never observed
`EnsureWellMemberSnapBox` can reach 900 cm; `EnsureNodeUseBox` gives ordinary nodes 650 cm.
H0 measured the nearest non-same-well node at **1400 cm**. 900 + 650 = 1550 > 1400, so the
boxes provably intersect in the population H0 measured.

**Not observed** — the well used for the 2026-08-08 test sat on a plateau with no ordinary
nodes in range. **Measurement:** a Miner Mk1 on an ordinary node within ~15 m of a relocated
well member must still snap.

---

## P3 — diagnostics that cannot report what they exist to report

*This project's recurring failure class. Eight sightings during the H2 arc. A gate that
cannot fail is worse than no gate, because it is read as evidence.*

### T4. Ordinary-node mesh-hide latency is unmeasured
A hidden ordinary node's **rock stays visible for a while** after the node itself is hidden
— it will not highlight or mine, but you can still see it. Observed in the desert,
2026-08-08.

Node hiding is **not** the lag: `Hide-originals funnel (first pass this load): records=630
loaded=630 newlyHidden=630 … notStreamed=0 pathMissed=0` — all hidden on pass one. The mesh
actor is a separate object and **the funnel reports nothing about it**. So we cannot
currently distinguish "the mesh hides a pass or two late" from "the mesh hides immediately
and the render state catches up".

Pre-existing in the long-shipped ordinary-node path; **not** an H2b regression. Cosmetic
and self-resolving.

> **Pre-scoped fix:** instrument mesh-hide latency before attempting any behaviour change.
> There is nothing to fix until there is something to measure.

### T5. `MT_Crack` capture counter is structurally blind
`(0 of them MT_Crack)` on nearly every `WELLH2B-CAPTURE` line is a **counter** defect, not a
capture failure: `CrackPieces` only increments inside `Cast<AFGNodeMeshActor>(C->GetOwner())`,
so the "own" and "spatial" routes — nearly every capture — can never be counted. Only
route-2 groups ever report a non-zero.

Fix belongs in `NodeShuffleWellVisuals.cpp`, currently at 497 lines (see T7).

### T6. The spawn-time registry ships unvalidated
`VERDICT=OURS-PROVEN` is unreachable by any scripted test: pass B runs once per session at
~40 s, in the settled phase only. The registry is present and believed correct, but nothing
has exercised it.

> **Pre-scoped fix:** a `NodeShuffle.DumpWellBackstop` console command so the backstop can be
> run on demand instead of waiting for its one scheduled pass.

---

## P4 — structural

### T7. Seven source files breach the 500-line rule
Splits **proposed, not performed** — deferred deliberately during the H2 arc because
splitting mid-review re-stales every `file:line` reference a reviewer just verified.

Known: `NodeShuffleWellRelocateApply.cpp` 567 · `NodeShuffleWellVisuals.cpp` 497 (three from
breaching) · `NodeShuffleWellRelocateRoll.cpp` 519 · `NodeShuffleWellClaim.cpp` 579 ·
`RelocateApply.cpp` 504. Proposed seam for the largest: `SuppressVanillaWellGroup` →
`NodeShuffleWellSuppress.cpp`.

### T8. `EnsureNodeUseBox` is typed `AFGResourceNode*`
**This blind spot caused three separate bugs during the H2 arc** — the mesh-lookup failure,
the suppression failure, and the snap failure. `AFGResourceNodeFrackingCore` derives from
`AFGResourceNodeBase`, so anything typed `AFGResourceNode*` **silently excludes fracking
cores at compile time**. `AttachIdentityOnly` was widened to `AActor*` for exactly this
reason; `EnsureNodeUseBox` has not been.

> **Pre-scoped fix:** re-type to `AFGResourceNodeBase*`. Round 13 called this the right
> long-term move but out of scope for a fix packet.
> **Heuristic worth keeping: when a fracking bug looks impossible, check whether the API
> you are using excludes `…Base` before looking anywhere else.**

---

## Unmeasurable on this machine

### T9. `bPlacementClaimLive` save-round-trip (A3-5) is UNMEASURED
Whether the `UPROPERTY(SaveGame)` field survives cook/serialization has never been observed.
It cannot be tested here: it requires a save written by build h2-8 or later, and every
candidate save on this machine provably predates the field's existence (verified by
timestamp against `b888b2f`, 2026-07-31 20:25).

Not "assumed working" — **unmeasured**, which is different. Recipe for manufacturing a
suitable save is in `_team/nodeshuffle-followups/H2-blockers-handoff.md` §W2.

---

## Never run

### T10. Bookkeeping test steps T-2 and T-3
The D-2 expiry counter and the stranded-actor class were scripted, amended across three
review rounds, and then **deferred** so the buildability test could run first. They remain
unrun. They are log-only measurements — no build required, ~25 minutes in game.

### T11. SaveGame-GUID identity component
Still gated on RT-6, which has not run. Round 9 established that it and `bPlacementClaimLive`
are **different layers** — entry-owns-coordinate vs actor-identity — and that neither
subsumes the other, so A3 shipping does not retire this.
