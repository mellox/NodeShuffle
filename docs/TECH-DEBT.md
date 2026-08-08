# NodeShuffle — tech debt & known limitations

Living list. Nothing here blocks a release; everything here is a thing we know about,
have decided about, and have not done. **If you find yourself rediscovering an item on
this list, the entry has failed — fix the entry, not just the bug.**

Each item records what it is, how we know, and why it is not fixed. Items with a
**pre-scoped fix** have had the work sized already — start there, don't redesign.

Last updated 2026-08-08 (second revision: the *truth-diagnostics* pass).

> **Correction, 2026-08-08 — read before using anything below about node coverage.**
> An earlier revision of this file was written while the project believed vanilla resource
> nodes stream in progressively and the roll therefore sees only part of the map. **That is
> false and was measured false**
> (`_team/nodeshuffle-followups/node-enumeration-investigation.md`): every level-placed
> vanilla resource node — all 630 — is live in a **single 8–11 ms frame at load**, in three
> independent boots, spanning biomes tens of kilometres apart. Nothing about the ordinary
> node roll is discovery-gated. What *is* presence-gated is **spawning a replacement rock**
> and **probing terrain for a well destination** (both need a ground raycast, which needs
> resident terrain). The one real discovery gap is **nodes runtime-spawned by other mods**,
> which can arrive minutes after boot and are missed until a re-roll (T14).
>
> The false model reached this file through a diagnostic that asserted a cause it never
> tested. That defect class is T4's section heading, and it is now also fixed at source
> (`ROLLCENSUS:`, see docs/DIAGNOSTICS.md).

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

### D2. Third-party node-manager mods still list/ping the original node locations
**Added here 2026-08-08. Previously tracked only in GitHub issue #1, item 4** — which is
exactly the failure mode this file exists to prevent, so it lives here now.

NodeShuffle **hides** original nodes rather than destroying them, deliberately: the world
stays save-safe and fully restorable if the mod is disabled. Mods such as
`ResourceNodesManager` scan **every** resource-node actor, hidden ones included, so they keep
reporting the vanilla positions after a shuffle or re-roll (e.g. the three vanilla oil spots
still ping).

**The vanilla map, compass and handheld scanner are clean** — that path is fixed at the
`GenerateNodeClusters` source, not per-consumer, so nothing downstream of it sees a hidden
original.

> **Options if ever revisited (neither chosen):** a "fully remove originals" toggle that
> destroys instead of hides — which forfeits restore-on-disable — or a plain compatibility
> note in the README. Do **not** widen the scanner fix to cover third-party scanners; they do
> not go through `GenerateNodeClusters`.

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

### T12. A gas resource is budgeted a completability floor and then dropped from every deck — it can be erased from the map
**Measured 2026-08-08 with an arithmetic proof, from the user's own logs**
(`_team/nodeshuffle-followups/lithium-extractor-investigation.md` §2). **Not fixed — the fix
is a separate, user-gated packet. Do not fix it as a side effect of anything else.**

The quota builder (`NodeShuffleSubsystem.cpp:1223-1287`, re-derived against this commit's tree
2026-08-08 — earlier revisions of this entry cited parent-commit numbering) floors **every** resource kind at
`MinNodesPerResource` / `MinNodesPerModdedResource` and budgets `TargetActive` to cover every
one of those floors. The deck builder then does this:

```cpp
// NodeShuffleSubsystem.cpp:1352
if (IsGasResourcePath(Kind)) { continue; }   // gas: relocate-only, never enters a deal deck
```

So a gas kind gets a floor **and** a slice of the active budget, and then **zero cards are
ever dealt for it**. The floor buys it nothing; the budget is phantom.

**Arithmetic proof, from two rolls in one day:**

| roll | gas kind in pool? | SolidDeck + LiquidDeck | `active` (TargetActive) | shortfall |
|---|---|---|---|---|
| 16:35:38 | yes (lithium) | 724 + 105 = **829** | **837** | **8** |
| 10:36 | no | 731 + 105 = **836** | **836** | **0** |

The shortfall is exactly `MinNodesPerModdedResource` × (number of gas kinds), and it vanishes
with the gas kind.

**The damage:** with no floor actually protecting it, a gas resource's originals go into the
ordinary `AllowVanillaDisappear` active-set draw like anything else. On the measured profile
(`ActivePercent: 90`, 68 of 630 originals losing the draw) a **single-node** gas resource is
**hidden with no relocated replacement about 11% of rolls — i.e. deleted from the world**. The
observed consequence was AlkaLib's lithium vanishing, taking the Reactive Ore Extractor's
reason to exist with it (`AUTOALLOW … decision=SKIP`).

**Now detectable in one grep**: `ROLLCENSUS ZERO-ACTIVE:` names the resource on the roll it
happens. That is diagnostics, not a fix.

> **Pre-scoped fix options, sized, none applied.** (A) Honour the floor: pre-mark
> deck-excluded (gas) entries `bActive = true` before the draw, up to
> `min(available, Quota[Kind])` — smallest change that makes the floor mean what it says;
> touches the roll's hottest function, so it inherits the full regression pass.
> (B) Stop budgeting a floor the deck cannot deliver: exclude gas from `Quota`/`TargetActive`
> — makes the arithmetic honest but does **not** protect the resource. (C) A + B.
> (D) Treat a relocate-only resource as non-disappearable outright — one condition at
> **`:1296`**, the active-set predicate
> `if (E.bPinned || (!E.bIsNewNode && !Config.AllowVanillaDisappear))`; the smallest correct
> statement of intent, since `AllowVanillaDisappear` is a *shuffle* knob and gas is not
> shuffled. **The investigation's recommendation was D.**
> *(Line numbers in this entry were re-derived against the current tree 2026-08-08. `:1264` —
> cited by an earlier revision — is a `{` inside the quota loop, not a predicate. If they have
> drifted again, find (D) by the predicate text, not the number.)*
> **Explicitly not a fix: changing auto-allow.** When the resource really is absent, `SKIP`
> is the correct answer and must stay.

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

### T14. The first roll misses nodes that other mods spawn after boot
**Measured 2026-08-08 by set-diff of object paths, not by counts**
(`node-enumeration-investigation.md` Q1 evidence 3). Between the boot roll (17:19) and a
manual re-roll six minutes later, **28 nodes** appeared that were absent at boot — 22
`BP_ResourdeNode_Alkali_C` (AlkaLib lithium) and 6 modded lead — while **zero** level-placed
vanilla nodes appeared late and **zero** were lost.

This is the mod's only real discovery gap, and it is **not** streaming: those nodes are
`SpawnActor`'d by other mods during their own init / research gating, so nothing that exists
at our roll time can contain them. A cook-time manifest could never hold them either
(`AFGWorldScannableDataGenerator::CacheWorldScannableData` is `WITH_EDITOR`, baked into the
base map's cook), which is why the live `TActorIterator` remains the correct design — its only
defect is *when* it runs, not *what* it can see.

**Today's answer is manual: re-roll.** `ROLLCENSUS:`'s `runtimeSpawnedByOtherMods` field makes
the population visible per roll.

> **Pre-scoped fix, not applied and deliberately gated on the user:** re-run the live augment
> automatically once — N seconds after boot, or on a node-count-changed edge — instead of
> requiring a manual re-roll. Moderate: it touches `RollLayout`'s hot path and changes a
> save-visible layout, so it needs its own packet and its own full regression pass.
> **Explicitly rejected alternatives:** raising `MinVanillaNodesForRoll` (fixes nothing, and
> would cement the false streaming model by looking like a fix) and building a node manifest
> (strictly worse — it *loses* exactly this population).

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

Node hiding is **not** the lag. The evidence is the always-on `Hide-originals funnel (first pass
this load):` line (`NodeShuffleSubsystem.cpp:4742`), which on the 2026-08-08 load reported
**`records=630`, `loaded=630`, `newlyHidden=630`** and a zero in the path-resolution field.
*Not quoted verbatim here* — an earlier revision of this entry presented a **splice of two
different log lines** as a quotation (it included `pathMissed`, which the `Hide-originals funnel`
format string does not emit; that field belongs to the gated `HIDEDIAG funnel` at `:4760`). Named
fields and their values, above, are what is attested; grep the line yourself for the rest.

**Field rename, 2026-08-08 (truthdiag-fixes).** `notStreamed` in that line is now printed as
**`pathUnresolved`** — nothing in it ever tested streaming; the counter is `DbgMissedPath`
("this record's path did not resolve to a live actor this pass"). Logs from before that build
carry the old token; grep for both. The separate diagnostics-gated `HIDEDIAG funnel` line
prints the *same* counter under a *third* name, `pathMissed`.

The load-bearing number is **`newlyHidden=630` equalling `records=630`** — a direct count of
hides performed — i.e. all hidden on pass one. The mesh actor is a separate object and **the
funnel reports nothing about it**. So we cannot currently distinguish "the mesh hides a pass or
two late" from "the mesh hides immediately and the render state catches up".

**Framing corrected 2026-08-08, and corrected again the same day.** The first revision treated
this line as a local footnote about one funnel. The second over-corrected and credited it with
falsifying this project's streaming model — **which is circular**: `records` is
`OriginalNodeRecord.Num()`, the originals *the roll itself captured*. Had the roll missed 200
nodes, the funnel would read `records=430 loaded=430` and look exactly as clean. This line
proves only that everything the roll saw was still live at hide time.

**What actually falsified the streaming model is elsewhere and is cited here so no one has to
re-find it:** `_team/nodeshuffle-followups/node-enumeration-investigation.md` — the per-resource
whole-map totals matching known counts (line 69: Uranium 8, Bauxite 23, SAM 23, Quartz 23,
Coal 82, Copper 72) and the 6-minute set-diff showing **zero** level-placed nodes arriving late
(line 76). With *that* as the premise, the funnel line's honest reading follows: **the hide pass
is not partial**, so any explanation of a visible-rock symptom reaching for "it had not streamed
in yet" is contradicted. One open question remains here, and it is a *mesh* question, not a
coverage question.

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
