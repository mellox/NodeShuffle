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

### ~~T1. The Relocate Resource Wells tooltip states the opposite of what the feature does~~ — FIXED 2026-08-08
The settings UI described a relocated well as *"functional but INVISIBLE"* and labelled the
toggle *"(INCOMPLETE - stage H2)"*. True before H2b; false since. Now reads EXPERIMENTAL, states
that a relocated well is dressed and buildable, and names the limits a player can actually hit:
presence-gated move ⇒ possible duplicate (D1); re-roll leaves an already-moved well where it is;
a late-loading satellite is lost from a moved well (T15); untested build-area overlap with nearby
ordinary nodes (T3); unverified desert meshes (T2).

> **THIS FIX TOOK THREE DRAFTS AND EACH ONE FAILED DIFFERENTLY. That is the entry.**
> 1. **Draft 1 under-disclosed.** It listed two limits and its own entry here claimed that was all of
>    them. Both listed were cosmetic or in the player's favour; both omitted were *functional* — a
>    disclosure asymmetry that reads as reassurance. The same review caught that the new
>    `(EXPERIMENTAL)` label contradicted the `EnableExperimentalFeatures` tooltip elsewhere in the
>    same panel. **A grep for the OLD claim cannot find a contradiction the NEW words create** — so
>    after fixing stale copy, re-read the whole panel as a player sees it, not just the diff.
> 2. **Draft 2 over-corrected into a NEW false claim** — it asserted a re-roll limitation that has no
>    code path (see T15), sourced from a review finding that was itself an unverified premise.
>    **Over-correcting a disclosure gap is the same defect class as the gap.** Verify a new limit
>    against the code before writing it, exactly the way you verify a removed one. Draft 2 also
>    asserted a *cause* for a Miner refusing to place — unmeasured, and it displaced a working remedy
>    the same panel already gives for that symptom. Player copy is bound by the
>    no-asserted-cause rule too ([[lessons-log-asserted-a-cause]]), for a sharper reason than a log
>    is: the player acts on the wrong remedy.
> 3. **Draft 3 is the reviewer's wording, applied verbatim.**
>
> **The pattern across all three rounds, which is why the workspace rule exists:** every edit a
> reviewer specified *verbatim* landed clean; **every edit authored in response to a finding
> introduced a defect.** Three for three, in a change that contains no logic at all.

> **The entry itself was wrong, and that is the reusable part.** It listed two sites; there were
> **three** — `NodeShuffleConfig.cpp:171` (UI), `NodeShuffleWellSpawn.cpp:393` (log), and
> `NodeShuffleConfig.h:120` (the header comment, which is where the other two were copied from).
> A stale claim propagates from the comment that justifies it, so **sweep by grepping the claim,
> not by working the list**. The log line's stale NOTE was deleted rather than re-worded: it was
> asserting a design fact, which is not what a diagnostic is for ([[lessons-log-asserted-a-cause]]).

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

### T15. A satellite that loads AFTER its well was relocated is hidden at the origin and refused at the destination
**This entry replaces a first draft that was FALSE, and the falsehood is more instructive than the
bug.** The first draft claimed re-rolling makes an already-relocated well vanish from the world
until restart, sourced from H2b-review's "F-3, accepted limitation". A re-review disproved it
statically: `SuppressVanillaWellGroup` has two call sites (`NodeShuffleWellRelocateApply.cpp:473`,
`:505`), both requiring `bGroupPlaced == true`; `bGroupPlaced` is cleared at two sites and **both
are unreachable for a placed group** — `NodeShuffleWellRelocateRoll.cpp:352` sits below the guard at
`:152` (whose comment reads *"a player who has walked to a relocated well should not find it
gone"*), and `NodeShuffleWellEscalate.cpp:235` is reachable only through `TryPlaceWellGroup`, called
solely inside `if (!E.bGroupPlaced)`. **Invariant: a suppressed well origin always has a live,
maintained relocated group.** F-3 was an argument from *absence of a restore path* — true — plus an
unverified premise that a re-roll un-relocates a placed well. No log records the vanish; the test
script's "step 10 EXPECTED TO FAIL" is a prediction that was never run.

**CONFIRMED AT RUNTIME 2026-08-08**, build `2026-08-08-t1t2-1`, on the user's live save: a re-roll was
performed (`ROLLCENSUS: seed=1223222528 reroll=1`), 22 wells placed, and **zero** abandonment events —
the only occurrence of `WELLH2-ABANDON` in the whole log is inside the sentence that *documents* the
grep. The invariant now has runtime backing, not only a static read, so the hedge below is satisfied
for the re-roll case specifically. *(Note the near-miss: a naive `grep -c "re-enrolled by a new roll"`
returns **1** and looks like a violation. The hit is the diagnostic's own explanatory text. Count the
EVENT tag, never the prose that describes it — the same trap as the `(5 of 6 groups…)` constant.)*

**The real, reachable gap it was standing in front of:** a well is enrolled with the satellites
loaded at that moment (`NodeShuffleWellRoll.cpp:176-177`). A satellite that streams in later is
appended to the entry by the merge, then **refused at the destination**
(`NodeShuffleWellSpawn.cpp:201-205`) while still being **hidden at the origin**
(`NodeShuffleWellRelocateApply.cpp:332-335`, which applies no `bCaptured` filter). It has no un-hide
path. The well is permanently smaller — and produces less — than its vanilla counterpart.

Player-facing as of the T1 copy fix, which now states this instead of the false claim.

> **Pre-scoped fix: H2b-review B1, a per-group hidden-piece ledger**, so suppression can be undone
> per piece rather than only per placed group. **Verify the absence before building it** — "there is
> no un-hide path" rests on a reviewer's read, and this project has been burned by *"I could not
> find a check"* being quoted forward as *"there is no check"*. That hedge was in the first draft
> too; the headline violated it anyway, which is why it is repeated here.
>
> **"until you reload the save" is still ASSUMED** — the recovery half has not been observed.

### T16. A RELOCATED well is structurally UNPINNABLE — pin detection resolves through the hidden original
**Found 2026-08-08 by the cold review of the re-roll-guard change, and MEASURED in the author's own
log. This is a PRE-EXISTING defect in the shipped build, not a consequence of that change** (which was
parked; see below). It is filed here because the same broken predicate is what made that change unsafe.

`E.bPinned` is derived in `NodeShuffleWellRoll.cpp:153` from the **vanilla original core**. Relocation
hides that core, disables its collision and deregisters it from the node manager — so a player
**physically cannot build on it**, and the pin can never become true. The actor they *can* build on is
our spawned core, which the `ns-review-h2 F1` filter at `:133` deliberately excludes from the census
map. `:190` then does an unconditional `E.bPinned = bPinned;`, so even a previously-true pin is reset.

**Measured, one roll, from `FactoryGame.log`:**
```
WELLH1-ROLL: skippedOurSpawned=17 -- ... wells H2 RELOCATED ... excluded
WELLH1-ROLL: re-roll complete -- 20 wells (20 managed, 0 pinned/unresolved)
```
17 of 20 wells relocated; **every one reports `managed=1 pinned=0`.**

**`ApplyWellRetype`'s "live pin re-check" (`NodeShuffleWellRetype.cpp:152, 202`) has the IDENTICAL
defect** — it also resolves via `FindOriginalBaseByPath`.

> ⚠ **CORRECTION 2026-08-08 — THIS ENTRY'S STATED CONSEQUENCE WAS WRONG.** It said a re-roll could
> "retype a relocated well the player has built on … their chlorine setup silently becomes water." The
> T16 cold review found that **the retype cannot reach the spawned actors at all** (see T17): the write
> goes through `FindOriginalBaseByPath`, i.e. the *hidden originals*, so nothing player-visible is
> retyped. The pin defect is real and worth fixing — the *harm* I attributed to it is not the harm it
> causes. Written into this file as fact after one code read; corrected after a review measured it.
> **FIXED 2026-08-08** by `EvaluateWellPin()` (`NodeShuffleWellRetype.h`), one shared predicate for both
> consumers, resolving against the actors a player can actually build on.

> **Pre-scoped fix: repair the pin AT ITS SOURCE so both consumers inherit it** — resolve occupancy
> against the actor the player can actually build on (our spawned core/satellites) when the entry is
> relocated, falling back to the original only when it is not. **Do not patch it at one call site:**
> this file already records two copies of one rule drifting apart, and there are two consumers here
> (`WellRoll` and `WellRetype`) that must not diverge again.

### T17. A relocated well's SPAWNED actors are never re-typed — the world and the layout disagree
**Found 2026-08-08 by the T16 cold review. Code-read and log-corroborated, NOT observed — do not quote
it as measured.** This is the defect T16's first draft mistook itself for.

`NodeShuffleWellSpawn.cpp:169` and `:282` write `mResourceClassOverride` **only inside the fresh-spawn
branches**. The **reuse** and **adopt-late** paths never re-write it. `ApplyWellRetype` resolves through
`FindOriginalBaseByPath` — the *hidden originals* — so it cannot reach a spawned actor either.

**Confirmed statically and decisively 2026-08-08.** `ApplyWellRetype` provably cannot cover for it: it
resolves through `FindOriginalBaseByPath` → `VanillaNodeCache`, which **skips our own nodes by
construction** (`NodeShuffleSubsystem.cpp:2126`). So no path ever moves an existing spawned well actor
onto a re-dealt resource. Log confirms this is the *normal* state, not an edge case: all 15 relocated
groups were **adopted** (`:37147`), and only 4 `WELLH2-SPAWN` lines exist in 111,496 — both "spawned"
ones are newly-placed wells. No relocated group ever re-spawns.

> ⚠ **RETRACTED, and this is the third time today the same mistake was made in this file.** The first
> draft cited *"16 actually changed resource"* against 15 adopted groups as evidence the world had
> diverged from the layout. **It is not evidence of that.** That counter is
> `Assigned != ORIGINAL` (`NodeShuffleWellRoll.cpp:359`) — not `!= previously-assigned`. And in that
> log the assignment never moved at all: the pre-roll apply at 00:07:19 already shows the values the
> 00:07:55 roll produced, because **both rolls shared seed `1223222528`**.
>
> So the **defect is proven; its consequence is UNOBSERVED.** A relocated well's resource cannot change
> — but nothing has yet been seen changing and failing to take effect, because no roll in evidence
> re-dealt one. Same [[lessons-log-asserted-a-cause]] shape as the two retractions above it: a counter
> was read as answering a question it does not ask. **Do not re-state the consequence as measured until
> a roll with a DIFFERENT seed re-deals a relocated well and the world is checked against it.**

**Why it matters beyond correctness:** every conservation guarantee this mod advertises is asserted
against the *layout*, and the layout is not what the player's wells produce. It also means T16's pin fix
is necessary but not sufficient — pinning now protects the right actors, but a *non*-pinned relocated
well still will not actually change resource on a re-roll.

> **Not fixed here, deliberately** — a scoped pin fix is not the place for it. The likely shape is to
> re-write `mResourceClassOverride` on the reuse/adopt paths too, but that touches the spawn lifecycle
> and owes its own packet and its own review. **First runtime step is cheap:** on a well you have
> pressurized, compare `The core the pin was READ FROM ('…') now holds 'Y'` against that well's earlier
> `-> 'Z'`. **Y ≠ Z confirms it live.**

### T20. Well-vs-node clearance is ONE-WAY — a node can be dealt inside a well's footprint
**Found in game 2026-08-08 by the author, from a screenshot, on build `2026-08-08-t17-t7b-1`.**
A solid Sulfur node appeared beside a relocated chlorine well and took a Miner. Measured from the log:
the node sits **45.9 m** from `BaseNode_FrackingCore_2`'s core.

**The clearance a well respects is 90 m** — `WellMaxBoundRadiusCm (6500) + WellMinNodeSpacingCm (2500)`,
tested against every active layout node in `NodeShuffleWellRelocateRoll.cpp:604-612`. So the well would
never have chosen that spot. **The node did, because node placement does not know wells exist:**
`WellDestinations` occurs **0 times** in `NodeShuffleSubsystem.cpp`, which is where ordinary node
locations are generated.

**The relationship is therefore asymmetric by construction:** wells avoid nodes; nodes do not avoid
wells. Across rolls — and now more often, with `RerollRelocatedWells` moving wells — a node dealt in a
later roll can land inside a well placed in an earlier one, and nothing re-checks.

**Same shape as three other defects in this file** (T16's two consumers of one pin, the `ns-review-h2 F1`
census filter, T8's `AFGResourceNode*` typing): **one rule, applied on one side of a relationship only.**
That is now this project's most-repeated defect class, ahead of even the zero-without-a-denominator one.

**Not observed to break anything.** The Miner placed and works. The plausible harms are unmeasured: a
node overlapping a satellite's snap box (T3's geometry, from the other direction), a Well Extractor
refused because a Miner got there first, or simply a solid node standing in the middle of a fracking
cluster.

> **Pre-scoped fix, and pick deliberately:** (A) give node generation the same 90 m test against
> `WellDestinations` — symmetric, but it needs the well destinations to exist *before* node placement,
> which inverts the current roll order; (B) test it at the *later* of the two whenever a re-roll moves
> either — cheaper, but leaves pre-existing saves unfixed; (C) accept and document, like D1.
> **Measure before choosing:** count how many active nodes currently sit within 90 m of a placed well
> core. If it is a handful, (C) is defensible; if it is dozens, it is a placement bug.

### T18. A pin found on a relocated well is never RECORDED — `bAlreadyApplied` measures the wrong actor
**Found 2026-08-08 by the T17 cold review. Pre-existing T16 defect that T17 makes consequential.**
Diagnostics for it shipped with T17; the fix did not.

`NodeShuffleWellRetype.cpp:230` computes `bAlreadyApplied` from the **hidden original**, while the pin
one line above resolves the **spawned** core (T16). Consequence: apply pass 1 writes the new resource to
the originals; from pass 2 a core-only pin is suppressed by `bAlreadyApplied == true`. So **a well with a
Pressurizer on it stays recorded `bManaged=1 bPinned=0` all session** while the world permanently refuses
the assignment — the layout keeps claiming an assignment the player's well will never hold.

**Now visible in one grep**, shipped with T17: `WELLH2-RETYPE-PIN … LAYOUT BOOKKEEPING AT THIS INSTANT:
bManaged=%d bPinned=%d`. **`bPinned=0` on that line is the defect firing.** That is diagnostics, not a fix.

> **Pre-scoped fix (review's Option B), one line** — measure "already applied" on the actor the pin was
> resolved against:
> ```cpp
> const AFGResourceNodeFrackingCore* AppliedOn = Pin.ResolvedCore ? Pin.ResolvedCore : Core;
> const bool bAlreadyApplied = (AppliedOn->mResourceClassOverride.Get() == ResourceClass);
> ```
> **It is a mode-selection-predicate change to code that landed hours earlier, so it owes a full review**,
> not a one-line drive-by. This project's own record: *"a mode-selection predicate let one stray pixel
> fail a whole image"* is one of the three structural fixes that each introduced a fresh bug.
>
> **The review's stronger rival, and the better long-term target:** extend `ApplyWellRetype` to resolve
> **spawned actors first**. That dissolves this entry entirely and closes the proximity gate, at the cost
> of writing two populations per well. Its own packet.

### T19. The well acceptance gate cannot fail for a resource mismatch
**Found 2026-08-08 by the T17 cold review, and it is why T17 went unnoticed.**
`NodeShuffleWellAudit.cpp:123-127` prints `res=` **from the layout**, while `:53` already holds the
spawned core. `bHealthy` is computed from counts, positions and links and **never from resource** — so
the acceptance gate printed `-- OK` throughout the entire pre-T17 defect and would do so again.

This is the P3 class in its purest form: not a gate that *did not* fail, but one **structurally unable
to** for this defect class, while reading as evidence that the well is well.

> **Pre-scoped fix:** print `res=` from the spawned core (the audit already holds it) and add resource
> agreement to `bHealthy`. Cheap; needs one decision first — whether a resource mismatch should make a
> group *unhealthy* (blocking suppression) or merely be reported, since blocking suppression on it
> changes what the gate gates.

### SHIPPED (DEFAULT OFF) — re-roll geography, `RerollRelocatedWells`, 2026-08-08
**Author's decision: a well should re-roll like any other node** — unpinned re-rolls its geography, pinned
never moves. **Took two attempts and two rejections.** The toggle defaults **OFF**; with it off the
pre-T7b path runs unchanged, which is what protects an existing save from churning ~17 wells on one
keypress.

**Attempt 1** (just deleting the guard) was rejected: the safety property it claimed was false because a
relocated well was structurally unpinnable (**T16**, fixed separately), and despawn ran *before* the deal
so a failed deal left the well **nowhere**. Parked at `stash@{0}`.

**Attempt 2** restructured `RollWellRelocation` into three phases with a file-local pending capture, and
the full review verified — statement by statement — that **no layout field is written on any success
path before a destination exists**. The orphan blocker is closed and graded *provably provided*. It also
**upheld the packet's disagreement with the previous reviewer** on destination self-exclusion: two wells
cannot be dealt the same site, and no well can be dealt the site of one that then fails to move.

**Attempt 2 was still rejected**, for three things worth remembering:
1. **The same bug through a different door.** Gate 1 treated **0 resolved handles as a PASS**. Not an
   independent layer: T16's pin falls back to the hidden vanilla actors, that verdict is deliberately
   non-decisive so `bPinned` keeps its *saved* value, `DespawnWellGroup` then finds nothing, returns
   all-clear and **prints nothing** (its log is gated on having destroyed or refused something). Phase 1
   proves the *origin* streamed; nothing proved the *destination* did. **Fixed: the gate now fails
   closed, with its own counter and line.**
2. **"Byte-for-byte with the toggle OFF" was FALSE.** `WellDestinations` was built from pre-capture
   state, so a candidate's stale destination blocked every other well *with the feature disabled*.
   Fixed.
3. **The tooltip claimed a well is "removed and rebuilt at its new spot within a single frame."** False —
   spawns defer behind `IsLocationNearAnyPlayer`, so one keypress makes every relocated well vanish until
   visited. **Third false claim shipped into this one config panel in a single day.**

> **Lesson worth more than the feature.** The teardown-cost line reported `Σ(1 + Satellites.Num())` from
> the **layout**, not `DespawnWellGroup`'s real destroyed count — a derived upper bound presented as a
> measurement. In blocker 1's own scenario it would have printed *"136 destroys, 0.4 ms"* while
> destroying **nothing**: the single number that would have exposed the bug, structurally unable to.
> [[lessons-zero-needs-a-denominator]] has a sibling: **a derived figure is not a measurement, and the
> more precise it looks the more it is trusted.**

### T7 REGRESSION — `NodeShuffleWellRelocateRoll.cpp` is 921 lines, the day T7 was closed
The three-phase restructure took it 519 → **921** (84% over the limit), hours after the four well files
were split under 500. It **cannot** be split from inside its own packet: the extraction needs a member
declared in `NodeShuffleSubsystem.h`, which that packet did not own.

Recorded rather than waved through, because the split-then-immediately-regrow pattern is how a limit
stops meaning anything. **Next split packet takes this file and owns the header.**

### ~~PARKED — attempt 1 of the re-roll-geography change~~ — SUPERSEDED, stash DROPPED 2026-08-08
Attempt 2 shipped (above), so attempt 1 is dead code. **The stash was deliberately dropped rather than
left lying around**: a `DO NOT SHIP` entry sitting in `stash@{0}` is a trap for a future session that
pops it looking for context. Its content was one deletion — the `bGroupPlaced` guard — and both its
blockers are recorded above in full. Nothing recoverable was lost. *Original entry retained below for
the history.*
**The author decided wells should re-roll like ordinary nodes** — unpinned re-rolls its geography,
pinned does not move. The guard at `NodeShuffleWellRelocateRoll.cpp:152` was deleted to enable it, and
the cold review returned **DO NOT SHIP on two blockers**. The work is stashed, not lost; the deployed
binary was rebuilt from the reviewed commit so nothing unsafe is in the DLL.

1. **T16 above makes the change's stated safety property false.** The comment claimed pinning protected
   built-on wells; nothing is ever pinned, so a re-roll could despawn a well out from under a
   pressurizer. Buildings survive (`DespawnWellGroup`'s occupancy gate holds) — but **its return value
   is discarded**, so execution continues into `bGroupPlaced=false`, claim withdrawal and a re-deal,
   leaving the player's machine on the old core and fresh live satellites kilometres away, retrying
   forever. In a mod whose deck machinery exists to conserve well resources, that duplicates them.
2. **A failed re-deal deletes the well from the world.** Despawn + `ClearAbandonedWellPlacement` run
   *before* the deal loop, and nothing ever un-suppresses a vanilla group. On deal failure the log says
   "left vanilla this roll" — false: nothing at the old site, nothing at a new one.

> **Pre-scoped fix, in the review's recommended order:** (A2) fix the pin at its source per T16;
> (A3) restructure to **deal-first, despawn-second** so a failed deal cannot orphan the well;
> (A4) gate it behind a `RerollRelocatedWells` toggle, default OFF, so an existing save with 17 placed
> wells does not churn on one keypress. **This is a structural review-response change to a
> path-replacing change — it earns a FULL re-review, from a different reviewer.**
> Also flagged: `WellRedealTries = 24` was sized for 3-5 wells per roll and would now be asked for ~20
> ([[feedback-own-caps-are-revisable]]); five refusal diagnostics would assert "left vanilla" for wells
> that are visibly relocated; and the summary counters would double-count, since `AlreadyCaptured` and
> `Enrolled` were disjoint only because of the guard.

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

### T3. Snap-box overlap — **PARKED 2026-08-08 by the mod author. Watch-only; do not schedule work.**

**DECISION (author, 2026-08-08):** *"I don't think we should worry about well overlap a node. We can log
as a possibility for tech debt to explore more or if I run across it."*

**Do not open a packet for this.** It is now a **watch item**: if a Miner is ever refused on a visible,
mineable node beside a relocated well in ordinary play, that observation reopens it — and the
instrumentation to diagnose it already shipped (`2026-08-08-t1t2-2`), so the evidence will be in the log
when it happens. Everything below is retained as the record of what was measured and what the numbers
are worth.

**Justification for parking, so it is not re-escalated on the raw numbers:** the 8 measured overlaps
resolve so far to **hidden originals, never an active node** (see the correction below), and a hidden
node cannot be built on regardless of any box. There is no confirmed hazard, only a confirmed
*instrument* defect.

> **The `IsHidden()` filter LANDED 2026-08-08**, riding the T7 split as intended — it was the only
> intended behaviour change in that packet, scoped to this diagnostic alone (`Bystanders` untouched, so
> no node is claimed, hidden or de-collided differently). Both counts now print: `activeMineable=` and
> `hiddenOriginal=`, rather than the population silently shrinking.
>
> ⚠ **THE "8 PROVABLE OVERLAPS" FIGURE BELOW IS NOW HISTORICAL AND MUST NOT BE RE-QUOTED.** It was
> measured against the unfiltered population. **No hazard count exists until a fresh load re-measures**
> against `activeMineable` only. If that number comes back 0 — which the evidence below predicts,
> since every overlap resolved so far was a hidden original — T3 closes as a non-issue.

### T3 (evidence as measured 2026-08-08 — hazard status: none confirmed)

> ## ⚠ CORRECTION, same day, hours after the entry below was written and committed.
> **The user asked: "are you sure the node is an active one and not a hidden vanilla?" It is not sure,
> and the instrument cannot tell.** `RebuildWellMeshIndex`'s sweep
> (`NodeShuffleWellVisuals.cpp`) filters only `!IsValid(N)` and `Ours.Contains(N)`:
> ```cpp
> if (Cast<AFGResourceNode>(N)) { UseBoxNodes.Add(N->GetActorLocation()); }
> ```
> **There is no `IsHidden()` test.** A hidden vanilla original is still an `AFGResourceNode`, still
> passes the cast, and still enters the population. **658 originals were hidden at load in the very
> session that produced these 8 results**, so hidden nodes plausibly dominate the 1227-node snapshot.
>
> **Why this matters:** T3's hazard is "a Miner is refused on a node the player could otherwise use."
> A hidden original cannot be built on regardless of any box, so an overlap against one is **harmless
> and should never have been counted**. The 8 numbers below are correct as geometry and **unproven as
> hazards**. They may all be hidden originals.
>
> **This is a false-POSITIVE generator — the opposite direction from the reviewer's F-4 concern about
> false negatives.** The packet did not catch it, the cold review did not catch it, and neither did I;
> the mod author did, from the domain and not from the code. Recorded because the review process has a
> demonstrated blind spot for *population* errors: every gate here checked the predicate's arithmetic,
> and none asked whether the set being measured was the right set. Cousin of
> [[lessons-test-subject-was-exempted]].
>
> **Pre-scoped fix:** add `IsHidden()` to the `UseBoxNodes` filter, and **print both counts**
> (`activeMineable=` / `hiddenOriginal=`) rather than silently shrinking the population — a denominator
> that quietly changes meaning is the defect this file keeps re-learning.
> **Do not re-state any hazard count until that lands and a fresh load re-measures.**
>
> **RESOLVED BY EVIDENCE, same session — no in-game trip needed.** The author noted that the only node
> visible near that well in their own screenshot was a Kerr crystal far too distant to matter. Back-
> solving the satellite's world position against the overlap's per-axis deltas identified the culprit:
>
> ```
> ORPHANDIAG: 'Resource_Stone_01' at V(X=-43336.21, Y=239568.84, Z=-3837.56)
>             already hidden -> no action
> ```
>
> A **hidden stone original**. Repeating for all five distinct members: **2 of 5 resolve to confirmed
> hidden originals** (`Resource_Stone_01`, `SM_LithiumNode`), 3 could not be resolved from the log, and
> **0 resolve to an active node.** So there is **no confirmed T3 hazard** — the measured overlaps are so
> far entirely the artefact this correction predicted.
>
> **The author's eyes beat the instrument**, and that is the durable point: a screenshot answered in one
> glance what 128 measurements got wrong, because the instrument was counting the wrong population and
> could not know it. When a measurement disagrees with direct observation, suspect the population before
> the arithmetic.

### T3 (geometry as measured 2026-08-08, hazard status pending the fix above)
**The instrumentation worked on its first run.** Build `2026-08-08-t1t2-2`, one save load, no travel and
no re-roll: **128 measurements, 8 provable overlaps**, against a snapshot of **1227** ordinary mineable
nodes. Every verdict was audited against its own printed per-axis numbers — **zero mismatches**, so the
predicate is correct, not merely firing.

| member | extent | nearest mineable node | dx/dy/dz (cm) |
|---|---|---|---|
| `BP_FrackingSatellite_C_2147460444` | 900.00 | **868 cm** | 751 / 434 / 24 |
| `BaseNode_FrackingSat_KLib_C_2147416684` | 860.56 | 933 cm | 879 / 80 / 302 |
| `BaseNode_FrackingSat_KLib_C_2147416681` | 863.23 | 1099 cm | 1077 / 202 / 80 |
| `BaseNode_FrackingSat_KLib_C_2147413508` | 807.99 | 1380 cm | 1072 / 869 / 10 |
| `BaseNode_FrackingCore_KLib_C_2147416687` | 900.00 | 1833 cm | 1245 / 1281 / 413 |

The first row is a satellite of the chlorine core the user built a pressurizer on, so it is **directly
reachable for a runtime test**. The `KLib` members are modded wells.

**Overlap is necessary, not sufficient** — it proves the boxes intersect, not that a Miner is refused.
The runtime test is still owed: place a Miner Mk1 on the ordinary node beside row 1.

> ⚠ **A LOG-DESIGN LESSON THAT COST THREE WRONG READINGS IN ONE SITTING.** This line's legend contains
> the literal text `provableOverlap=0); provableOverlap=%d`, so the **legend's example value appears in
> the log BEFORE the real field**. A naive `grep -c "provableOverlap=1"`, and even a "first occurrence"
> regex, reads the legend and not the measurement — it produced "136 overlaps", then "0 overlaps",
> before the correct answer of 8. Same family as the `(5 of 6 groups…)` constant and the
> `re-enrolled by a new roll` prose. **RULE: a legend must DESCRIBE its fields, never EXEMPLIFY them in
> `field=value` syntax that collides with the real field.** When counting any field in this project's
> logs, anchor on a delimiter the legend cannot contain (here: the trailing `.`), and sanity-check the
> match count against the line count — 384 matches over 128 lines was the tell. [[lessons-zero-needs-a-denominator]]

### ~~T3 (original). Snap-box overlap with neighbouring nodes is proven geometrically, never observed~~
`EnsureWellMemberSnapBox` can reach 900 cm; `EnsureNodeUseBox` gives ordinary nodes 650 cm.
H0 measured the nearest non-same-well node at **1400 cm**. 900 + 650 = 1550 > 1400, so the
boxes provably intersect in the population H0 measured.

**Not observed** — the well used for the 2026-08-08 test sat on a plateau with no ordinary
nodes in range. **Measurement:** a Miner Mk1 on an ordinary node within ~15 m of a relocated
well member must still snap.

> **Why it has never been observed, found 2026-08-08: nothing measures it.** The 1400 cm figure
> came from H0's separate analysis, not from a log line. Grepping the live `FactoryGame.log` for a
> snap-box/nearest-neighbour diagnostic returns **nothing** — there is no `WELLH2B-SNAPBOX` line and
> no "nearest non-well node" line anywhere. So this item cannot be closed by playing; it can only be
> closed by *stumbling onto* the geometry and noticing a miner that will not place. That is the
> P3 failure class one section down, in its purest form: **an item whose test is "get lucky".**
>
> **Pre-scoped fix — instrument before touching behaviour** (same doctrine as T4). In
> `EnsureWellMemberSnapBox` (`NodeShuffleWellVisualsApply.cpp:153`), at box-creation time, log the
> member, its final box extent, the distance to the nearest **non-same-well** resource node, and
> whether the two boxes provably intersect (`extent + 650 > distance`). The contest sweep in
> `NodeShuffleWellVisuals.cpp` already builds exactly the bystander-location array this needs, so
> the data is in hand. Then the **existing save answers the question on the next load** with no
> hunting. Report only the measurement — never a cause ([[lessons-log-asserted-a-cause]]).
>
> **The 14 relocated destinations in the user's current save**, extracted from `FactoryGame.log`
> 2026-08-08, so a manual check does not have to start by finding the wells:
>
> | resource | core | destination |
> |---|---|---|
> | Gas_Chlor | `BP_FrackingCore10` | `X=138900.53, Y=240105.70, Z=-3828.89` |
> | Gas_Chlor | `BP_FrackingCore17` | `X=-44403.44, Y=236471.64, Z=-3874.49` |
> | LiquidOil | `BP_FrackingCore15` | `X=-20280.56, Y=60895.37, Z=22579.53` |
> | LiquidOil | `BP_FrackingCore3` | `X=-144296.27, Y=-110723.30, Z=2164.50` |
> | LiquidOil | `BaseNode_FrackingCore2_1` | `X=-197300.99, Y=-108281.73, Z=597.63` |
> | NitrogenGas | `BP_FrackingCore11` | `X=102207.83, Y=160609.81, Z=1807.17` |
> | NitrogenGas | `BP_FrackingCore13` | `X=149586.85, Y=263255.37, Z=-767.00` |
> | NitrogenGas | `BP_FrackingCore2` | `X=-22410.94, Y=-86941.46, Z=3089.27` |
> | NitrogenGas | `BP_FrackingCore9` | `X=281997.30, Y=-31236.98, Z=9806.94` |
> | Water | `BP_FrackingCore12` | `X=-148481.47, Y=42995.02, Z=23669.52` |
> | Water | `BP_FrackingCore18` | `X=124256.32, Y=142012.11, Z=8656.73` |
> | Water | `BP_FrackingCore5` | `X=-6358.01, Y=-80721.03, Z=13624.39` |
> | Water | `BP_FrackingCore6_UAID_...1961476789` | `X=-159925.11, Y=113053.53, Z=7629.70` |
> | Water | `BaseNode_FrackingCore1_0` | `X=77127.81, Y=46452.60, Z=11494.30` |
>
> ⚠ These are **destinations dealt in that save's roll**, read from one log. A re-roll re-deals them.

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

**BEHAVIOURAL HALF CONFIRMED BY THE USER 2026-08-08**, build `2026-08-08-t1t2-1`, after a re-roll.
Flying to coal originals near a `NodeShuffle.Here` marker: the rocks were **visible**, and **neither a
Mk8 nor a Mk3-class miner would snap to them**; they disappeared after a wait. So the *node* hide is
correct — the actor is out of `mResourceNodes`, which is exactly the guard that stops a player snapping
a miner onto a ghost original — and **only the mesh actor lags**. This upgrades the entry from "we
cannot distinguish a late mesh hide from a render-state catch-up" to: *the node is functionally gone
immediately; the visual is what persists.*

⚠ **The duration was UNMEASURED and the user's "30 seconds" was explicitly an estimate.** Then the
instrumentation review found the likely reason that estimate was *good*:
**`RockBackstopCooldownSeconds = 30.0f`** (`NodeShuffleSubsystem.cpp:4856`). The rocks go dark when the
**stray-rock backstop** sweeps, and that backstop runs on any pass that newly hid a node, else on a
30-second cooldown. It had already fired **30 times** in the user's session log
(`hid 0 original nodes and N stray original rocks`, N = 1..8). So the cooldown is an **upper bound this
mod imposes on itself** for how late a rock can go dark, and the user was very likely reading it off
the screen.
*Stated as the strongest available explanation, NOT as proven cause* — nothing has yet correlated an
individual rock's hide to an individual backstop sweep. That is what the new instrumentation measures.

**Newly understood mechanism candidate, NOT confirmed:** the `Hide-originals funnel` line is emitted
**once per load** (`(first pass this load)` — exactly one such line in the whole session log) and
covers records resident at that moment. A rock that streams in later, when the player arrives, was
never in that pass's population. What eventually hides it is unidentified. *Stated as a hypothesis on
purpose; it fits the evidence and has not been tested.*

**Player-facing consequence, small but real:** a player sees a node, flies to it, fails to build on it,
and concludes the mod is broken. Harmless, but it reads as a bug.

> **Pre-scoped fix:** instrument mesh-hide latency before attempting any behaviour change.
> There is nothing to fix until there is something to measure. Concretely: count rocks hidden on a
> **later** pass than their node, and report the delay, so the duration above stops being a stopwatch
> guess. Additive and diagnostics-only — but note this project's record that even additive counters
> ship with a defect when the zero has no denominator ([[lessons-zero-needs-a-denominator]]).

### ~~T5. `MT_Crack` capture counter is structurally blind~~ — FALSIFIED 2026-08-08
**This entry was wrong, and it was wrong in a way that nearly cost a packet.** It claimed
`CrackPieces` "can never be counted" on the own and spatial routes, so only route-2 groups report a
non-zero. That claim was used as evidence that `mNodeMeshType` is unreachable on the spatial route,
which would have sunk T2's pre-scoped fix before it was tried.

**Measured against `FactoryGame-backup-2026.08.08-15.53.04.log`, and the arithmetic is decisive
without needing any per-group route split:**

| group | pieces | MT_Crack |
|---|---|---|
| `BP_FrackingCore15` | 18 | **10** |
| `BP_FrackingCore17` | 17 | **9** |
| `BaseNode_FrackingCore1_0` / `2_1` / `_2` | 8 + 5 + 7 = **20** | 0 |
| every other group | 0 | 0 |

Per-pass route totals are `(20 own, 17 via engine link, 18 spatial)`. The three `BaseNode_` groups
hold exactly 20 pieces and report 0 cracks — matching `own` exactly — so **all 19 observed MT_Crack
pieces belong to the two groups fed only by link + spatial**. The link route can supply at most
**17**. `19 > 17`, so **at least two MT_Crack pieces were counted on the SPATIAL route**, and
`CrackPieces` increments only where `Cast<AFGNodeMeshActor>(C->GetOwner())` is non-null. The spatial
route therefore *does* reach an `AFGNodeMeshActor`. Confirmed independently of the T2 packet, which
reached the same conclusion by a different route.

> **How the entry went wrong, which is the reusable part.** `(0 of them MT_Crack)` really did appear
> "on nearly every line" — but on lines where **`pieces=0`**. A group that captured nothing reports
> zero cracks for the obvious reason. The observation was real; the inference that it revealed a
> *counter* defect never controlled for the denominator. **A ratio read off lines whose numerator is
> structurally zero is not evidence of anything.** Cousin of [[lessons-log-asserted-a-cause]]: not a
> log asserting a cause, but a *reader* inferring one from a statistic the log never supported.
>
> **What remains genuinely open is narrower:** the *own* route has never been observed producing a
> non-zero MT_Crack — but all 20 of its pieces are `BaseNode_` wells, which may simply have no crack
> meshes. Route 1's countability is **unproven in both directions**, not blind. Do not restate it as
> a defect without an own-route group that has cracks.

### T6. The spawn-time registry ships unvalidated
`VERDICT=OURS-PROVEN` is unreachable by any scripted test: pass B runs once per session at
~40 s, in the settled phase only. The registry is present and believed correct, but nothing
has exercised it.

> **Pre-scoped fix:** a `NodeShuffle.DumpWellBackstop` console command so the backstop can be
> run on demand instead of waiting for its one scheduled pass.

---

## P4 — structural

### T7. Source files breaching the 500-line rule — **the four well files are SPLIT and under 500 (2026-08-08)**

**Done for the well subsystem.** The split landed once its deferral reason expired (six review reports
had been citing `file:line` in these files; those reviews are now committed and historical).

| file | before → after |
|---|---|
| `NodeShuffleWellVisuals.cpp` | 658 → **248** (capture + origin-side hide) |
| `NodeShuffleWellMeshIndex.cpp` *(new)* | — → **480** (radius constant, `EnsureWellMeshIndex`, `RebuildWellMeshIndex`) |
| `NodeShuffleWellVisualsApply.cpp` | 549 → **243** (dump + dress + apply) |
| `NodeShuffleWellSnapBox.cpp` *(new)* | — → **399** (collision recipe, snap box, promoted T3 state) |

Also landed: the two module-static accessor blocks were **promoted to members** (they only ever existed
because the header was barred to the packet that wrote them), `NodeShuffleWellSnapBoxDiag` gained the
world-change reset its sibling already had, and `WellVisualCaptureLogged` got its key-family table.

**Verified by measurement, not by eye:** each moved region was `git show`n from HEAD and `diff -u`'d
against its new home — six functions byte-identical. Build clean. **Import set SET-IDENTICAL** to the
pre-split baseline (816/766, 0 missing), which was the packet's prediction and is the check that
matters, since a split can shift inlining and therefore the import table.

**Still open:** `NodeShuffleSubsystem.cpp` remains **8069 lines** and was deliberately out of scope —
it is a much larger job than the well files and needs its own packet and its own seam analysis.

> **One deviation from the proposed seam, and it was right.** The prior handoff said
> `DumpWellActorCollision` should move to the snap-box file. It cannot: it is an anonymous-namespace
> static whose only caller (`ApplyWellGroupVisuals`) stays behind, so moving it yields an unreferenced
> static in one file and an undefined symbol in the other. The packet reported this instead of
> following the spec into a link error — the intended behaviour when a spec meets the tree and loses.

### ~~T7 (previous revision). Source files breaching the 500-line rule — nine files~~
**Updated 2026-08-08.** The instrumentation packet pushed two more files over, and reported it rather
than trimming diagnostics to buy headroom — the right call, recorded so it is not mistaken for drift:
`NodeShuffleWellVisuals.cpp` 497 → **658**, `NodeShuffleWellVisualsApply.cpp` 432 → **549**.
`NodeShuffleSubsystem.cpp` reached **8076**.

**The splits were deferred deliberately and the reason has now expired.** Through the H2/T1/T2 arc,
splitting mid-review would have re-staled every `file:line` a reviewer had just verified — six review
reports now cite these files. **That constraint is gone once the current round is committed**, so the
split is the natural next packet, and it should happen *before* any further additions.

Proposed seams, both confirmed sound by review: move `WellMeshOwnerRadiusCm` / `EnsureWellMeshIndex` /
`RebuildWellMeshIndex` out of `NodeShuffleWellVisuals.cpp` (residual ≈ 287); the `WellVisualsApply`
seam is in the instrumentation handoff. **Ask the splitting packet for two things:** a key-family table
on `WellVisualCaptureLogged`'s declaration (it now spans three TUs with six key families), and
promotion of the two module-static accessor blocks to members — they exist only because
`NodeShuffleSubsystem.h` was barred to avoid a collision, and one of them
(`NodeShuffleWellSnapBoxDiag`) has **no world-change reset** where its sibling does.

### ~~T7 (original entry). Seven source files breach the 500-line rule~~
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
