# NodeShuffle — tech debt & known limitations

Living list. Nothing here blocks a release; everything here is a thing we know about,
have decided about, and have not done. **If you find yourself rediscovering an item on
this list, the entry has failed — fix the entry, not just the bug.**

Each item records what it is, how we know, and why it is not fixed. Items with a
**pre-scoped fix** have had the work sized already — start there, don't redesign.

Last updated 2026-08-10 (T54 filed at the top of P1 by author ruling; supersedes D1's coupling).

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

### T54. Vanilla wells are NOT hidden at load — the origin stays live and usable until its replacement places. The author has now ruled that wrong. **TOP OF P1 (author, 2026-08-10).**
**The ask (author, 2026-08-10, on a fresh multi-mod save):** *"So our on load shuffle still doesn't
hide vanilla immediately like I asked?"* — said while standing at `BP_FrackingCore12` (water), fully
live with its six satellites, `hidden=0`, while `WELLH2-STRANDED pass 1` read **2 of 20 well entries
placed**. By the suppression invariant, every unplaced entry's vanilla group stays visible and usable
for an unbounded time — until the player happens to stream terrain near that entry's destination.

**This entry supersedes a DECISION, not an oversight — D1 (2026-08-08) chose the current behaviour
deliberately**: suppression is coupled to placement (`SuppressVanillaWellGroup` has two call sites,
`NodeShuffleWellRelocateApply.cpp:473`/`:505`, both requiring `bGroupPlaced` — T15's invariant *"a
suppressed well origin always has a live, maintained relocated group"*), because placement is
presence-gated (the destination probe needs streamed terrain) and hiding on the roll removes a well
from the save for an unbounded time, possibly permanently (design §5.4, fail-safe-to-vanilla). The
author's 2026-08-10 ruling reverses that priority: a player seeing and using a well that is destined
to move is the worse defect. D1's text must be re-decided as part of this item, not left contradicting it.

**What an immediate-hide fix must re-decide or not break — named up front:**
1. **The well-less window becomes intended.** Hide-at-load plus presence-gated placement means the
   resource exists NOWHERE until the destination streams. The author must confirm that window is
   acceptable as-is, or this item waits on removing the presence gate (T21's baked-surface analysis
   is exactly that question). **This decision gates the build.**
2. **`WELLH2-STRANDED` flips polarity.** "Suppressed but not placed = 0" is today's health
   invariant; after this fix that state is intended-transient. The check must distinguish
   transient-awaiting-placement from stuck, or it becomes a vacuous pass (the exact defect class in
   [[lessons-checklist-predates-the-feature]]).
3. **D1's duplication scenario mostly dissolves** — nobody can build on a hidden well — a point in
   its favour. But the occupied-member refusal (`NodeShuffleWellRelocateApply.cpp:295`, *never hide
   a well someone has built on*) must keep protecting saves where extractors already exist at the
   origin before the first hide runs.
4. **Restore paths get a bigger population.** Reroll/disable restore has been exercised on placed
   groups; after this fix it must un-hide entries that never placed. T15's late-streaming-satellite
   gap (hidden at origin, refused at destination) gains blast radius: with immediate hide there may
   be NO destination group yet when the satellite streams in.

> **Pre-scoped starting point:** the hide today runs inside `ApplyWellRelocation`'s spawn-then-suppress
> (`NodeShuffleWellRelocateApply.cpp:467-473`); immediate-hide would run at load/stream-in for every
> entry the roll marked as moving. **Do not build until the author answers item 1** — that answer
> decides whether this is a one-gate change now or waits on T21.

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

### ~~T20~~ → **D3. ACCEPTED: nodes may sit inside a well's footprint. Author's call, do not "fix" it.**
**Decided 2026-08-08 by the mod author, after taking the measurement this entry asked for: ~6 active
nodes sit around the well in question, and both the Miner and the well work.**

> *"I don't think the solid near the well is a bad thing since the miner and wells both work."*

**Do not open a packet to make the clearance symmetric.** Inverting the roll order so node placement can
see well destinations is a real change to the hottest path in the mod, and it would buy an aesthetic
property the author does not want. The asymmetry stays.

**The ONE thing that would reopen this** — and it is a *satellite*-level question the core-distance
measurement above does not answer: **a Well Extractor refused because a Miner got to that spot first.**
The 45.9 m figure is to the well's CORE; satellites sit further out, so a node can be much closer to a
satellite than to the core. If a Well Extractor is ever refused next to a Miner, this entry becomes a
defect again and option (A) below is the pre-scoped fix.

*Mechanism retained below, because the asymmetry is real and the next person to find it should read the
decision rather than re-derive the bug.*

### ~~T20 (mechanism, retained)~~. Well-vs-node clearance is ONE-WAY
**Found in game 2026-08-08 by the author, from a screenshot, on build `2026-08-08-t17-t7b-1`.**
A solid Sulfur node appeared beside a relocated chlorine well and took a Miner. Measured from the log:
the node sits **45.9 m** from `BaseNode_FrackingCore_2`'s core.

**The clearance a well respects is 90 m** — `WellSatMaxRadiusCm (6500) + WellMinNodeSpacingCm (2500)`
(ns-t27-review 3 merged the roll's file-local `WellMaxBoundRadiusCm` into that header constant),
tested against every active layout node in `NodeShuffleWellRelocateRoll.cpp:644-652` (the
`for (const FNodeShuffleEntry& Node : Layout)` loop; the `Node.Location, Cand` test is line 647 —
re-measured 2026-08-09 by ns-t27-truth. The previous citation here, `641-651`, was applied verbatim
from a review that had it wrong at both ends: 641 closes the *previous* well-spacing loop). So the well would
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

### ~~T21. Well relocation is presence-gated only because we never baked SURFACE terrain~~ — **CLOSED 2026-08-08. THE PREMISE IS FALSE. Do not reopen without reading why.**

**Sized on request, and it never got as far as size.** Full working: `_team/nodeshuffle-followups/T21-surface-bake-sizing.md`.

**`ValidateWellMemberSpot` (`NodeShuffleWellFootprint.cpp:48-127`) applies FIVE gates. Three are terrain
and are bakeable. TWO are decided against LIVE ACTOR POPULATIONS and can never be:**

| gate | how it decides | bakeable? |
|---|---|---|
| void / no terrain | ground trace | yes |
| water | water-body test | yes |
| cliff / slope | 4-probe normal ring | yes |
| **node overlap, 800 cm** | `TActorIterator<AFGResourceNode>` (`:76`) | **NO** |
| **buildable overlap, 600 cm** | live physics overlap (`:112`) | **NO** |

The node-overlap gate reads **exactly T14's population** — the nodes other mods `SpawnActor` after boot,
which T14 measured at 28 and about which it concluded a bake *"loses exactly this population"*. So T21's
own argument — *"terrain can be baked precisely because nothing spawns it"* — is **true of three gates and
false of two, inside one function**. A bake is a **partial oracle**, and a partial oracle cannot license
hiding the original at roll time. The spawn-time backstop stays; **D1 is unchanged**.

**The sharpest reason, and the one to quote if this is ever revived:** the ground trace is `ECC_WorldStatic`
and **accepts buildable hits by design** (`NodeShuffleSubsystem.cpp:5971-5975`, *"nodes on foundations are
allowed"*). So wherever a player has built, **baked vanilla terrain is not even the Z the game settles to** —
the bake would be wrong precisely where the world is most developed.

**Sizing, recorded so nobody re-derives it.** Resolution is driven to **≤350 cm** by `SmoothNormalRingCm`
(`NodeShuffleSubsystem.cpp:119`) — the radius of the 4-probe ring the cliff normal is smoothed over; a
coarser grid cannot represent the sampling pattern at all. 175 cm to be Nyquist-faithful; **≈24 cm vertical**
to match the 60° cliff verdict within 1°. That is **4.6× finer than the cave atlas's 800 cm**. Over the
measured 720,000 × 630,000 cm probed extent: **3.70 M cells** — **170 MB** in the shipped string-literal
encoding (46 B/cell, measured), **~20 MB** as a packed int16 raster. **The entire deployed mod DLL is
1,666 KB.** The cave atlas is 282,278 B = **16.7% of the DLL for 2,660 cells**, and it is **sparse** (1.8%
fill); a surface atlas must be dense, so that encoding does not transfer.

> **TWO CORRECTIONS TO THIS ENTRY'S ORIGINAL TEXT, both from inferring rather than measuring.**
> 1. **"6656 cells" was never the shipped bake.** The DLL holds **2,660**. 6,656 is the author's *merged
>    runtime* store (baked + locally learned), so **~60% of the figure quoted as evidence exists only on
>    one machine**.
> 2. **`Scripts/bake_maps.ps1` is a SNAPSHOTTER, not a generator** — it re-emits JSON the mod learned
>    while playing. The real generator is `ExpandCaveFloorsBudgeted`: seeded 4-connected flood fill,
>    **player-proximity gated at 30 km**, 120 traces/pass, capped at 25,000 cells. A surface variant is a
>    **new offline tool with no precedent here** (~18.5 M traces, needing a headless commandlet against the
>    cooked CSS map that may not be runnable at all).
>
> Both came from reading a log line and a filename and inferring the rest — the same *observe a value,
> infer its meaning, state the inference as evidence* shape this file keeps recording.

**STILL UNMEASURED, and it would close this twice over:** the rejection-reason distribution. There are
**0 `WELLH2` lines across 15 MB of logs** — the feature post-dates every log in the repo. One diagnostics-on
roll counting `WELLH2-SEARCH … REJECTED … (<reason>)` by category would say how much of the problem is even
terrain-shaped. **Also never measured: how often D1 actually fires.** If it has never been observed, T21's
motivation was hypothetical from the start.

> **THE ALTERNATIVE THAT ACTUALLY SERVES THE STATED GOAL ("wells shuffle immediately"), and needs no oracle.**
> The delay was never missing terrain data — it is that the **destination** is far from the player
> (`NodeShuffleWellRelocateApply.cpp:441`) while the **origin** is provably resident. **Bias the 24-draw deal
> loop to prefer candidates already within `SpawnRadiusCm` of a live pawn, falling back to uniform.** The well
> then validates and spawns on the next apply pass. No DLL growth, no new tooling, every safety property
> intact. **Trade-off is the author's call, not an engineering one: it biases spatial distribution toward
> wherever the player stood at roll time, and randomisation is the product.** Put to the author 2026-08-08.
>
> **Seam flagged, graded `assumed`, not chased:** hiding the original at roll time would also have to re-home
> `CaptureWellGroupVisuals`, which today runs inside `SuppressVanillaWellGroup`
> (`NodeShuffleWellRelocateApply.cpp:284-285`) precisely because that is the one site provably reached with
> the vanilla member still standing. Read at the call site only, not across every consumer.

*Original entry retained below for the history.*

### T21 (original entry, premise since falsified). Well relocation is presence-gated only because we never baked SURFACE terrain — and we already bake CAVE terrain
**Raised by the mod author 2026-08-08, and it dissolves an assumption three explanations in this file
were built on.** Their question: *"If a solid node can properly land on terrain, and we check the
terrain is the right type, why are we not able to check that for the wells? The terrain is static."*

**First, a correction this entry exists to make.** Solid nodes do **not** validate terrain at roll time
either. They settle through the same trace, which returns the same failure — *"no terrain / out of range
(true void)"* (`NodeShuffleSubsystem.cpp:5904`). The solid/well asymmetry was never *"we check for one
and not the other"*; it is only **failure tolerance**: a solid that cannot settle is one node, and nodes
are *allowed* to disappear (`ActivePercent`), so nothing needs a fallback. A well that cannot place must
fall back to its original, so the original must still exist — which is why it cannot be hidden on the
roll. That is the real reason, and earlier answers in this file gave weaker ones.

> ## ⚠ THE `ActivePercent` CLAUSE ABOVE IS FALSIFIED — measured 2026-08-08, see **T22**.
> A solid that cannot settle on terrain is **deferred and retried forever**
> (`NodeShuffleSubsystem.cpp:3972-4005`), **not dropped**. `ActivePercent` (`:1238`) and
> `AllowVanillaDisappear` (`:1243-1246`) are **roll-time budget knobs** deciding how many pool slots start
> `bActive`; they are **never consulted on a placement failure**. The only executable drop is the *overlap*
> path after 8 failed visits (`:4140`).
>
> **The real asymmetry is in the HIDING, not the checking, and it is the opposite of tolerance.**
> `SuppressOriginalNodes` (`:4497`) hides a solid's original unconditionally, on a loop that never reads
> whether the replacement spawned, with its proximity gate deliberately removed (`:4509-4510`).
> `SuppressVanillaWellGroup` has exactly two call sites, **both after a complete group spawn succeeded**.
> **Solids do not tolerate failure — they do not DETECT it**, which is T22.
>
> Two further measurements that sharpen this entry rather than change its conclusion: **a solid applies SIX
> gates and a well applies five of the same six** (the well lacks the enclosure test) — so **zero of the
> well's gates lack an ordinary-node counterpart**, and the asymmetry is not in the gate set in either
> direction. And the well's real difficulty is **footprint**: ~11 actors across up to 65 m must fit
> *simultaneously*, searched over 36 yaws × 8 nudges × 3 redeals, all-or-nothing. **Footprint is why
> placement fails often; the fail-safe is why failure cannot be pre-committed.**
>
> *The paragraph's headline claim — that solids do not validate terrain at roll time either — is CONFIRMED.
> Both paths call the same `RaycastGroundAt`, and both spawns are gated by the same
> `IsLocationNearAnyPlayer` at the same radius. Only its stated mechanism was wrong.*

**The author's lever is the right one.** The blocker is not that terrain changes — it is that the only
way we ask about terrain is a physics trace, and a trace needs the landscape **resident**. Static
geometry we cannot query is still unqueryable.

**But this mod already solves that exact problem for caves.** `NodeShuffleBakedData::CaveFloorsChunks`
is a **baked cave-floor atlas embedded in the DLL**, merged at load by `EnsureCaveStoreLoaded` and usable
with **no streaming** (6656 cells on the author's save). The technique — *terrain is static, so bake it
once and query the bake* — is shipped, working, and pointed at caves only.

> **Design option, not scheduled: bake a SURFACE height atlas and validate well footprints at roll time.**
> Then a well's destination is *committed* like a solid node's, the original can be hidden immediately,
> and the all-or-nothing fallback stops needing the original to survive — because failure is known
> before the deal, not hours later when a player flies there.
> **Costs and caveats, stated honestly:** an atlas at enough resolution to judge a footprint up to ~65 m
> across (`WellMaxBoundRadiusCm`) is larger than the cave one; it must be generated offline in-editor;
> and it can only describe **vanilla** terrain. That last point is fine — terrain is static — but note
> the contrast with **T14**, where a bake is strictly *worse* than a live iterator because other mods
> spawn nodes at runtime. **Terrain can be baked precisely because nothing spawns it.**
>
> **Do not start this without sizing the atlas first.** The question that decides it is resolution: what
> cell size is needed to reject a footprint that a yaw search would have rejected? Measure against the
> existing placement code's own tolerances before writing anything.

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

### ~~T19. The well acceptance gate cannot fail for a resource mismatch~~ — **FIXED 2026-08-08** (`32e3f38` + `ead59f9`, marker `2026-08-08-t19-2`)

The gate now prints the layout's assignment (`res=`, meaning unchanged so committed review reports that
grep it still resolve) **and** what the spawned core holds (`worldHolds=`), and `bHealthy` gates on their
agreement **over the core AND every live spawned satellite** — a satellite left on the old resource is
exactly as wrong as a core left on it, and exactly as invisible. A **pin-explained** disagreement is not a
fault (T17 deliberately declines to retype an in-use group) and gets its own non-alarming token.
**Reporting-only: `bHealthy` gates no behaviour** — re-derived independently three times.

**F1, and read this before believing a `*** RESOURCE MISMATCH ***`:** the T17 retype is **proximity-gated**
(`NodeShuffleWellRelocateApply.cpp:501`) while the audit sweep walks **every** placed group. On the
`RerollRelocatedWells=OFF` default — what most users run — a shuffle re-deals every placed well and only the
1–2 nearby ones are retyped, so an unguarded gate would fire **~15 warnings every 5 minutes on a CORRECT
build**. Hence `retypeReachable=`. **Fifth sighting of one-rule-one-side** (T16, `ns-review-h2 F1`, T8, T20).

> ## ⚠ FOUR PROSE ITERATIONS OF ONE LOG LINE. THREE SHIPPED A FALSE CLAIM. That is the entry.
> 1. The cold review's **own F1 text** glossed `retypeReachable=0` as *"has not been VISITED since its
>    assignment changed"* — the predicate tests presence **now**, not history.
> 2. Its replacement asserted *"NO PLAYER IS WITHIN THAT RADIUS"* **unconditionally**, on a line that fires
>    independent of that flag, so a `retypeReachable=1` line contradicted its own `%d`.
> 3. The third still said *"the retype demonstrably reached and failed to fix"* — a **cause asserted from a
>    proximity boolean**. Provably wrong: the `just-placed` audit (`:486`) runs in the `!bGroupPlaced` branch
>    and never reaches the retype at `:501`, yet prints `retypeReachable=1`. And it **contained the literal
>    token `retypeReachable=1`**, so the grep its own commit message recommended matched **every** mismatch
>    line — the `provableOverlap=0` legend trap, second occurrence in this file family.
>
> **THE RISK CLASS IS NOT WHO TYPED IT — IT IS WHETHER THE AUTHOR WAS UNDER PRESSURE TO CLOSE A FINDING.**
> All three were authored in response to a finding, by three different agents, and the orchestrator waved
> #2 through reasoning *"an agent specified it verbatim, so it is low-risk."* That is not what the rule
> means. Ask for verbatim text to avoid **re-derivation** — then still **verify it against the predicate it
> describes**. The genuinely safe material is text that predates the finding.
>
> Also caught late, by the scoped review only: the `*** SHORT OR UNLINKED ***` arm had **no counter**, so
> `12 not OK … 0, 0, 0, 0, 0` was printable — five reassuring zeros beside a dozen wells the Pressurizer
> will under-report. And `%d fully linked` had quietly become **a label that lies**, since `bHealthy` now
> also requires not-scattered / not-short / no-mismatch.

### ~~T26. A WELL footprint has NO enclosure gate — an ordinary node has one.~~ — **FIXED (built, COLD-REVIEWED, UNTESTED IN GAME) 2026-08-09, marker `2026-08-09-t27-3`, packets ns-t27-corefirst + ns-t27-fixes + ns-t27-perf**

> **THE SHIPPED MARKER IS `-t27-3`, AND IT WAS NOT COLD-REVIEWED WHEN IT WAS BUILT.** This heading and
> the paragraph below originally named `-t27-2` and its review, which is one packet short of what is
> deployed: `-t27-3` additionally contains `ns-t27-fixes` (second review round,
> `_team/nodeshuffle-followups/T27-fixes-review.md`) **and `ns-t27-perf`, the node-scan hoist, which
> was UNREVIEWED at the moment that binary was built and deployed (16:35, 2026-08-09).** It became
> reviewed later the same day, by `_team/nodeshuffle-followups/T27-perf-review-2.md` (SHIP WITH TESTS,
> 13 findings + an addendum) — a review that lands **after** the build, not before it. Corrected here
> 2026-08-09 by ns-t27-truth; do not read the earlier wording as evidence that the shipped build was
> reviewed before it shipped, because it was not.

> **STATUS IS "BUILT", NOT "CLOSED".** The build is green and the import table is unchanged
> (817/767/0, zero symbols moved). A cold review has now run (SHIP WITH TESTS, 13 findings —
> `_team/nodeshuffle-followups/T27-review.md`) and its fixes are applied in marker
> `2026-08-09-t27-2`, but **nobody has stood at a relocated core in game.** Do not mark this closed
> on the strength of a compile plus a review; the review's own verdict is that its two decisive
> assumptions (F2, the enclosure predicate against the reported symptom; and parity row 14, Pressurizer /
> Extractor clearance at 2076 cm) are unreachable by any amount of static review. (`F14` was written
> here first and is wrong: the review has findings F1-F13 only, and 14 is a row of its PARITY TABLE at
> `T27-review.md:565`. A reader would have hunted a finding that does not exist.)
>
> **What landed.** The enclosure lambda that lived inside `EnsureNewNodeSpawned` is now
> `ANodeShuffleSubsystem::IsSpotEnclosed` (defined in `NodeShuffleWellFootprint.cpp`), and BOTH paths
> call it — the node path's lambda delegates rather than keeping a second copy, so the two cannot
> drift. `ValidateWellMemberSpot` gained a **required, undefaulted** `bApplyEnclosureGate` parameter;
> the core passes a literal `true`, satellites pass the named constant
> `WellEnclosureGateOnSatellites` (**compiled `true` as of marker `2026-08-09-t27-2`** — it shipped
> `false` in `-t27-1`; the cold review's F3, reinforced by its §9.2 under the author's 2026-08-09
> ruling, flipped it, because a satellite you can see but cannot build a Well Extractor on is the
> same permanent, log-invisible shrink T15 names, and under independent retry a rejection costs one
> of 24 draws). The undefaulted parameter is deliberate: a
> defaulted one would let a future call site opt out by saying nothing, which is exactly how this
> gap survived in the first place.
>
> **The false parity comment is gone**, replaced by a conditional claim that lists the six gates and
> names the condition on the sixth. `WELLH2-PROBECENSUS` gained an `enclosed` counter on both sides,
> and its prose warns that a zero in the SATELLITE enclosure bucket may mean the gate is off rather
> than that nothing was enclosed — read the constant, not the zero.
>
> **The measurement this entry asked for was never taken** (`NodeShuffle.Here` at the 2026-08-09
> Water core). The author's in-game report — flew there, circled a rock column, could not reach it —
> was taken as sufficient grounds by directive. So the *rate* at which the new gate rejects
> destinations is still UNMEASURED; the first log from this build is what measures it.
>
> This entry is retained in full below because its gate-by-gate source citation is still the record
> of what the two paths did, and because the "measure the rejection rate before choosing" advice is
> the advice for the SATELLITE half, which is still an open toggle.

### T26 (original entry, retained). A WELL footprint has NO enclosure gate — an ordinary node has one. A well can validate inside a slot or crevice.
**Found 2026-08-09 by the author asking, in game, *"I'm pinging a water 48m away — is this inside a cliff?"*
Measured from source, not inferred.**

An ordinary node's placement applies **six** gates (`NodeShuffleSubsystem.cpp`, `EnsureNewNodeSpawned`):
void `:3958-4006` · water `:3960-3971` · cliff `:5931-5936` · node-overlap 800 cm `:4024-4044` ·
buildable 600 cm `:4050-4069` · **`IsEnclosed` `:4078-4093`** — 8 horizontal rays at 500 cm from
`Z+200`, reject when **7 of 8 are blocked** — applied at `:4095` *and re-applied to every nudge target*
at `:4123`.

`ValidateWellMemberSpot` (`NodeShuffleWellFootprint.cpp:58-126`) applies **the same first five and stops**.
**There is no enclosure test on the well path.**

> ⚠ **AND THE FILE SAYS OTHERWISE.** `NodeShuffleWellFootprint.cpp:42-43` claims the gates are
> *"Deliberately the SAME set of gates the ordinary node spawn applies, in the same order."* **That
> comment is false**, and the mod's own instrumentation agrees with the code rather than the comment —
> `NodeShuffleWellStage0.h:46-55` enumerates exactly five (`Gate_Void … Gate_Buildable`, no enclosure
> member). A false comment asserting parity is how the gap survived: anyone auditing the well path
> against the node path reads that line and stops.

**Consequence, measured:** a well footprint can validate at the bottom of a narrow vertical slot, in a
crevice, or hard against a cliff face — geometry where an ordinary node is refused. The ground trace
takes the *first* blocking hit from `StartZ+20000` down, so a spot open ABOVE but blocked horizontally
passes every gate the well path has. The cliff gate does not help: **a flat cave floor, a flat ledge
under an overhang and the flat bottom of a slot all pass a 60° slope test perfectly.**

**Seventh sighting of this project's most-repeated defect class: one rule applied to one side of a
relationship.** (T16 · `ns-review-h2 F1` · T8 · T20 · T24's per-member occupancy · T24's four hide sites
· this.)

**Not observed to have produced an unreachable well yet.** The 2026-08-09 case that raised it is
*unresolved*: the log can rule out a low roof but cannot distinguish an open-topped slot from open
terrain, because **the mod records no terrain identity at settle time** — no hit actor, material or
component. Its 8 satellites spanned 20.7 m of relief over ~78 × 62 m and all settled dry and off-cliff,
which reads as open ground, but says nothing about the core's own 5 m ring.

> **Cheapest measurement, and it needs no build:** stand at the well core and run `NodeShuffle.Here`.
> `roofAbovePlayer` is an upward `ECC_WorldStatic` trace (`NodeShuffleSubsystem.cpp:6537-6540`) — `1` means
> under rock, `0` means open sky — and the same line prints the local slope, which no other log line
> carries. **Do this before writing any code.**
>
> **Pre-scoped fix, if the measurement justifies it:** give `ValidateWellMemberSpot` the same `IsEnclosed`
> test, applied per member. **Decide deliberately whether it runs on every member or only the core** — a
> satellite tucked against a rock face is far less harmful than a core that cannot take a Pressurizer, and
> requiring 9 members to each pass an 8-ray test will reject more destinations at a time when only 7 of 17
> wells are placing. **Measure the rejection rate before choosing** — Stage 0's `WELLH2-PROBECENSUS`
> already carries a per-gate breakdown and would show the cost immediately.
> **And fix the false parity comment in the same change**, whichever way the gate decision goes.

### T22. A solid node dealt into true void has its ORIGINAL hidden and its REPLACEMENT never spawns — and nothing counts it
**Found 2026-08-08 while answering *"we can do solid nodes, why can't we do wells"* — measured, not
inferred. This is the same shape as T19: a failure with no detector.**

`SuppressOriginalNodes` (`NodeShuffleSubsystem.cpp:4497`) hides **every** recorded original as soon as it
resolves and is unoccupied (`:4612-4660`). That loop reads **nothing** about whether the replacement ever
spawned — not `SpawnedNodes`, not `bActive`, not `bRayCasted` — and the proximity gate was **deliberately
removed** from it (`:4509-4510`). Meanwhile a replacement that cannot settle on terrain is **deferred and
retried forever** (`:3972-4005`), never dropped.

So a node dealt into true void is **hidden at its origin and absent at its destination, indefinitely** —
deleted from the world — and **no counter anywhere reports it.**

> **This corrects the standing explanation.** It was believed solids "tolerate" placement failure because
> nodes are *allowed* to disappear via `ActivePercent`. **Falsified:** `ActivePercent` (`:1238`) and
> `AllowVanillaDisappear` (`:1243-1246`) are **roll-time budget knobs** that decide how many pool slots
> start `bActive`; they are **never consulted on a placement failure**. The only executable drop is the
> *overlap* path after 8 failed visits (`:4140`). **Solids do not tolerate failure — they do not DETECT
> it.** The loss is real and unbudgeted, on top of whatever `ActivePercent` intended.
>
> **Unmeasured, and cheap to measure:** how often this fires. The instrument does not exist — count records
> that are `hidden && !spawned` for N consecutive passes and name them. **Instrument before fixing**
> (T4's doctrine); the fix shape is either to gate suppression on the replacement existing, as the well
> path already does, or to return an unplaceable entry to the pool. Do not assume it is common: a deal box
> that lands in true void may be rare, and **a zero here needs a denominator** like every other counter in
> this file.

### ~~T19 (original entry)~~. The well acceptance gate cannot fail for a resource mismatch
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

### T27 REGRESSION — `NodeShuffleWellRelocateApply.cpp` is 1201 lines (was 892 before ns-t27-corefirst, 1171 after ns-t27-perf)
**Not introduced by T27, but made worse by it, and stated rather than left for someone to notice.**
*(This figure has now been wrong twice: it was written as 1056 — true after `ns-t27-corefirst` — and
not re-measured when `ns-t27-fixes` added ~56 lines of comment, then not re-measured again when
`ns-t27-perf` added the cost-fix commentary. Now **1201**: `ns-t27-truth` added ~30 lines of comment
(the estimate/measured re-labelling and the two coupling TODOs), and re-ran `wc -l` rather than
leaving the figure for the next reader to find wrong a fourth time. A debt entry whose entire content is a file-size figure
is the worst possible place for [[stale figure drift]]. **Re-run `wc -l` before editing this line.**)*
*(**And take the figure from `wc -l`, NOT from `tools/arity.py`.** ns-t27-perf published this same file
as **1172** in its handoff (from arity.py) and **1171** here (from `wc -l`, and correct) — one file, one
packet, two numbers. Cause, measured 2026-08-09 by ns-t27-truth: `arity.py`'s `total_lines =
raw.count(chr(10)) + 1` counts one line too many for any file ending in a newline, i.e. every file here.
Confirmed on two other files by the cold review. That off-by-one is DEFERRED, not fixed — see the
KNOWN-BROKEN banner at the top of `tools/arity.py`.)*
The file breached the 500-line rule before this packet (892 lines) and the core-first/independent-
satellite rewrite added ~164 more: the per-satellite draw loop, two layout gates, and the reasoning
for why sibling clearance became ours. No split was attempted — a split during a placement-semantics
rewrite would have made the diff unreviewable, which is the opposite of what a packet handing work to
a cold reviewer should do.

The seam is the same one `NodeShuffleWellFootprint.cpp` was split along and is already obvious: the
**satellite draw** (draw a candidate, run the two layout gates, log it) knows nothing about attempts,
cursors, budgets, escalation or the commit. It is a `TryDrawSatelliteSpot`-shaped function and would
take ~180 lines with it. `SuppressVanillaWellGroup` is a second, cleaner cut — it already carries its
own banner comment and shares nothing with the search but the entry type.

**Do this as its own packet, after T27 has been reviewed and tested in game.** Splitting unreviewed
code moves the review target while the reviewer is reading it.

### T27-PERF — the superset proof is unenforced: the node-scan scope and the satellite DRAW radius are coupled only by convention
**Opened 2026-08-09 by ns-t27-truth, deferred from it (that packet was comment-only). Source:
`_team/nodeshuffle-followups/T27-perf-review-2.md` F2 — which carries the verbatim fix.**

`BuildWellNodeScanCache` (`NodeShuffleWellFootprint.cpp`) scopes its one-per-call node scan to
`WellSatMaxRadiusCm + WellOverlapRejectRadiusCm`. The node-overlap gate is correct **only while every
satellite probe stays inside that scope** — and the probes are drawn from `WellSatMaxRadiusCm` at a
second, independent read of the same constant in `NodeShuffleWellRelocateApply.cpp`'s satellite loop.
The two files agree today. **Nothing makes them agree.**

**Why this is worse than an ordinary latent bug: the break is SILENT and it PASSES rather than fails.**
A larger draw radius — e.g. a per-well `SatMaxRadius`, which the **short-wells packet is named as the
next owner of this code and is exactly the kind of change it wants** — leaves probes outside the
scanned set. The gate then tests a candidate against an incomplete population, **accepts** a spot beside
a real resource node, and no reason string changes, no rejection is logged, and no counter moves. The
player sees two node meshes intersecting; the log says the layout validated.

**The fix is an assertion, not prose.** The two TODO comments ns-t27-truth left at both sides of the
coupling (`TODO(ns-t27-truth 2026-08-09)`, in `NodeShuffleWellRelocateApply.cpp` at the scan call and at
the satellite draw) are a marker, **not** a fix — this workspace's own rule is to put the check AT the
seam, and prose is not a check. The reviewer's proposed shape: pass the scan centre and scope radius
into `ValidateWellMemberSpot`, and on the cached branch count and log any probe whose
`Dist2D(OutLoc, ScanCentre)` exceeds `ScopeRadius - WellOverlapRejectRadiusCm`. A compile-time coupling
(deriving one from the other in one place) is stronger still and should be considered first.

**Do this before, or as part of, the short-wells packet — not after it.**

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

### T31. The last-resort visual template is ONE GLOBAL PAIR, first-capture-wins — so a fallback well can wear ANOTHER RESOURCE'S ROCKS. The code comment calls this a biome problem; the biome half is nearly harmless and the resource half is not.
**Found 2026-08-09 while measuring whether T30 (invented core groups) could ship. Measured from the
pak index, 18,094 anchored runtime observations across 8 sessions, and the source — not inferred.
Triggered by the AUTHOR contradicting the existing comment from in-game observation:** *"I'm not sure
the look is different between biomes, it still looks like a piece of rock in all that I saw."*
**They were right about the mesh, and the investigation that confirmed them found something worse.**

**The mechanism.** `NodeShuffleWellVisuals.cpp:174-176` selects between exactly two session members —
`WellVisualTemplateCore` and `WellVisualTemplateSatellite` (`NodeShuffleSubsystem.h:2140-2141`) — and
fills each with `if (Template.Num() == 0) { Template = Out; }`. So:
* there is **one template per KIND for the whole session**, not one per resource and not one per biome;
* it is **first-capture-wins** — whatever well happened to be dressed first owns the look; and
* it is **never refreshed**, so a later, better-matched capture cannot replace it.

**Why that is worse than the comment says.** The comment points at *"the wrong biome's rock"*.
Measured: **the biome axis barely exists on the MESH.** The pak index holds exactly two resource
families of three pieces — `SM_FrackingNode_{Crack,Mid,Small}_01` and
`SM_Nitrogen_Node_{Crack,Mid,Small}` — and **no desert node mesh exists at all**; all 40 `SM_*Desert*`
assets in the game are foliage, boulders and coral. At runtime, 2 distinct mesh names over **18,094**
observations, and the *same* mesh appears under both `nodeMeshType=2` (MT_Crack) and `=5`
(MT_DesertCrack) — 1,958 desert-typed observations carrying the identical mesh as 13,605
grassland-typed ones. **The author's in-game read is confirmed on mesh.**

**The RESOURCE axis is the real one, and nobody had named it.** `SM_Nitrogen_Node_*` and
`SM_FrackingNode_*` are genuinely different mesh sets. Because the template is global and
first-capture-wins, **a session whose first captured well is nitrogen dresses every later fallback
well — water, oil — in nitrogen rocks.** This is not hypothetical: a companion measurement found
**7 of 19 relocated groups (37%) never capture their own origin in ANY observation across 8 sessions**
and ride the fallback permanently.

**Grades, honestly.**
* One global first-capture-wins template per kind — **provably provided** (source above, 0 other writers).
* Two mesh families exist and differ — **measured** (pak index + runtime census).
* No desert MESH variant exists — **measured**.
* A desert MATERIAL split DOES exist (5 `MI_*Desert*` well instances paired against non-desert twins,
  plus `TX_DesertRock_Cracked_01`) — **measured that the assets exist**; whether a desert-typed
  instance actually *resolves* to the desert MI is **UNMEASURED**.
* **Whether a player NOTICES a family mismatch — UNMEASURED, and it is an art judgment, not ours.**
  The scoped render that would settle the material half is `MI_FrackingNodes_Water_01` vs
  `..._Desert_Water_01` on the same mesh. Mesh comparison is closed by the asset list; do not re-run it.

**Do not "fix" this by making the template per-resource until someone decides it is worth fixing** —
the fallback exists precisely so a member is never left invisible-and-unbuildable, and a per-resource
template makes an empty-template outcome MORE likely for rare resources, which is the strictly worse
failure. That trade is the decision, and it belongs to the author.

**What this does to T30.** An invented group has no origin, ever, so it rides this template 100% of the
time. The cosmetic cost of T30 is therefore **not** "wrong biome tint" — it is **"may wear another
resource's rocks"**, and it is bounded by whatever this entry is worth fixing. Full working:
`_team/nodeshuffle-followups/T30-lookdiff.md`.

### T32. RELOCATED WELL GROUPS END SESSIONS UNDRESSED — 11 of 79 core-instances, in 5 of 8 sessions, were still fully no-visual at their last observation. This ships TODAY; it is not a T30 risk.
**Found 2026-08-09 by re-running an earlier measurement whose METHOD produced the answer.
This is the POPULATION lens, and it is the eleventh sighting in this workspace: the arithmetic was
right, the SET was wrong, and every gate passed.**

**THE NUMBERS, each with its denominator, from 10 rotated logs / 8 sessions with hits on 2026-08-09:**
* **11 of 79 core-instances still fully no-visual at their LAST observation, across 5 of 8 sessions**
  (13 of 81 if counted per placement rather than per core).
* **5 of those 11 were fully no-visual at EVERY observation in their session — never observed dressed
  at all**, the worst running **19 minutes / 203 apply passes**.
* Rate overall: **17 of 88 observations** carried ≥1 no-visual member; **133 of 672 member-chances**.
  Every one of the 17 was whole-group (`NoVisual == Members`, `Pieces=0`, `FromTemplate=0`) — groups
  fail to dress entirely, never partially.
* Cases that DID resolve in place (n=4) took **125 / 125 / 130 / 135 s — 21 to 27 apply passes.** Two
  apparent 1010–1030 s "resolves" were mid-session re-rolls to a new placement, not resolves, and are
  excluded.

**HOW THE PRIOR MEASUREMENT GOT 0.** `T30-killtest.md` reported **0 of 146 fatal** and was reproduced
exactly — but only by deduping on core NAME **across all 8 sessions**, merging different saves and
different rolls into one population. Per session the population is **79 group-instances / 602
member-chances**, and **the prior's own best-state aggregator applied WITHIN a session yields 85 of
602 no-visual, not 0 of 146.** **The cross-session merge alone produced the zero.** A group that was
undressed for a whole session scored as clean because a same-named core in a *different save* had
been dressed. Nothing about the arithmetic was wrong.

**THE UNDER-COUNT IS ON THE SAFE SIDE, WHICH MAKES IT WORSE.** The tag throttles on
`CorePath|Members|Pieces` and fires only on change, so absence of a later line means "no change
observed", and a 0 → N → 0 sequence would not re-emit the trailing 0. **§1 and §3 are therefore
lower bounds.**

**Grades, honestly.**
* The counts above — **measured**, and independently reproduced against the prior run's own figures.
* **WHY any group failed to dress — UNTESTED.** Streaming, distance, capture order and a bug are all
  live candidates and this log distinguishes none of them. **Do not write a cause into a fix.**
* **Whether "no visual" also means UNBUILDABLE — NOT MEASURED HERE.** `NodeShuffleWellVisualsApply.cpp`
  documents the empty-template outcome as invisible *and* unbuildable; that is the code's claim, and
  this measurement only establishes the invisible half. **If it holds, a player has wells they cannot
  build on, in 5 of 8 sessions.** Settling it costs one in-game attempt on an undressed core.
* The log **cannot distinguish "no visual" from "not yet dressed"** — `NoVisual` is a true "no mesh at
  this instant" and carries no information about whether one ever arrives.

**WHAT TO DO, IN ORDER.** (1) Confirm or refute the unbuildable half in game — one attempt, and it
decides whether this is cosmetic or a stranded-resource bug. (2) Instrument the capture path so a
group that never dresses SAYS SO with a denominator; today the only evidence is the absence of a
throttled line, which is exactly the [[lessons-zero-needs-a-denominator]] shape. (3) Only then fix.

**WHAT THIS DOES TO T30.** It reorders the queue. An invented group rides the fallback 100% of the
time, so T30 cannot be safer than this path is — **but T30 is no longer the reason to care.** Fix this
first; T30's dressing question then answers itself. See also [[T31]] (the template is one global
first-capture-wins pair). Full working: `_team/nodeshuffle-followups/T30-recount.md`.

### ⚠ CORRECTION TO T31 AND T32 — THE AUTHOR WAS RIGHT: THE LOG POPULATION IS STALE. Both entries' runtime figures describe builds that no longer exist.
**Filed 2026-08-09, within the hour, after the author said: *"I haven't played in a bit so I don't know
if the log is representative of changes made since then."* They were right, and it is worse than
"some staleness" — MEASURED by reading the boot marker out of each log:**

| build marker | anchored `WELLH2B-APPLY` lines |
|---|---|
| `2026-08-08-t7split-1` | 36 |
| `2026-08-08-t17-t7b-1` | 36 |
| `2026-08-09-t23s0-3` | 2 |
| `2026-08-09-t23hide-4` | 3 |
| `2026-08-09-t24-2` | 11 |
| **`2026-08-09-t27-2` / `-t27-3` (the deployed build)** | **0** |

* **NOT ONE of the 88 observations comes from the deployed binary.**
* **72 of 88 (82%) predate T23, T24, T26 and T27 entirely** — they are 2026-08-08 builds, before the
  enclosure gate, before core-first placement with independent satellites, and before the group-scoped
  occupancy gate.
* The live `FactoryGame.log` is on `-t24-2` and has **0** apply lines.

**WHAT THIS RETRACTS.** T32's headline — *"11 of 79 core-instances still no-visual at their last
observation ... this ships TODAY"* — **is not supported.** The counts are real for the builds that
produced them; the words "ships today" are not. T31's runtime frequency claim (*"7 of 19 groups never
capture their own origin"*) inherits the same contamination, since it comes from the same population.

**WHAT SURVIVES, and why.**
* **T31's MECHANISM stands** — one global first-capture-wins template per kind, never keyed by
  resource, never refreshed. That is read from CURRENT source (`NodeShuffleWellVisuals.cpp:174-176`,
  `NodeShuffleSubsystem.h:2140-2141`), not from logs.
* **T31's ASSET findings stand** — two mesh families, no desert node mesh anywhere. That is the pak
  index, which is the game's, not ours, and is build-independent.
* **T32's METHOD finding stands and is the durable part** — the prior zero came from deduping across
  sessions. That critique is about arithmetic on a population, not about which build produced it.

**THE ACTUAL LESSON, AND IT IS THE THIRD SIGHTING IN ONE HOUR.** POPULATION again. The first run merged
different SAVES and ROLLS; the recount fixed that and merged different BUILDS; **neither of us checked
the boot marker, which is one grep and is printed in every log for exactly this purpose.** Every gate
we have passed on both runs. The author caught it from memory of when they last played.
**RULE, and it goes in the brief for every future log measurement: SLICE BY BUILD MARKER FIRST, print
the per-marker denominators, and state explicitly how many observations come from the build under
discussion. A count that spans builds is not a count of anything.**

**WHAT WOULD ACTUALLY MEASURE THIS.** One session on `-t27-3` with diagnostics ON, flying past a few
relocated wells. Until then T31's frequency and all of T32 are **UNMEASURED ON THE CURRENT BUILD**, and
neither may be cited as a live defect rate. The unbuildable question is unaffected and still worth one
in-game attempt on any undressed core.

### T34. A relocated well core can land far enough inside a rock that normal placement refuses — and the snap-mode toggle (H) then lets you build inside the rock anyway.
**Reported in game by the author 2026-08-09 on build `2026-08-09-t27-3`, with screenshots. Author's
call: ACCEPTABLE FOR NOW — filed, not scheduled.**

Observed: at a water well group, the core sat far enough inside a rock formation that the Resource
Well Extractor hologram refused normal placement. Pressing **H** (snap mode) turned the hologram blue
and placement was allowed **inside the rock**. The author also found **only 3 wells in the group they
could place on**, and saw the game offer placement on a well that was itself inside a rock — *"that
won't help a player."*

**What is measured and what is not.**
* The behaviour is **observed in game**, twice, with screenshots. That is the evidence.
* **Whether the snap-mode path bypasses a check our relocation relies on is UNTESTED.** H is a vanilla
  build-gun affordance; it is not ours. Do not assume the mod caused it.
* **Whether a vanilla (un-relocated) well behaves identically is UNTESTED and is the FIRST thing to
  check** — if vanilla wells also allow this, it is not our defect and this entry closes.

**Why it is not merely cosmetic.** A well the player can only reach by defeating the placement refusal
is, for practical purposes, a well they cannot use — the same class as T15's permanent, log-invisible
shrink. The group above yielded 3 usable members out of its full count.

**Do not "fix" this by blocking the snap-mode path.** That is vanilla behaviour and blocking it would
take a legitimate affordance away from players. The fix, if one is wanted, belongs at PLACEMENT time —
do not deal a core where its members are unreachable — which is [[T35]]'s territory.

### T35. THE ENCLOSURE GATE HAS NEVER REJECTED ANYTHING — 0 rejects across 2,961 census lines, on both the core and satellite sides — while the author stands in front of the exact defect it was built to prevent.
**Measured 2026-08-09 from the live session on `2026-08-09-t27-3`, the first log this build ever
produced. This is a ZERO WITHOUT A DENOMINATOR, and it is the gate T26/T27 exist to provide.**

```
core side:      enclosed:0  x2961 census lines
satellite side: enclosed:0  x2961 census lines
```

**The zero is almost certainly NOT "nothing was enclosed".** In the same session **2,032 of 2,042
attempts ended `DEFERRED-void`**, terminated on the core side with `void:1` — and **void is the FIRST
gate while enclosure is the SIXTH**. So the overwhelming majority of probes die before the enclosure
test is ever reached. **The census reports how often the gate REJECTED and never how often it RAN**,
so today's `enclosed:0` is indistinguishable from a gate that never executed.

**THE INSTRUMENTATION THIS NEEDS, and it is the whole first step.** Add a *gate-reached* counter beside
each *gate-rejected* counter, so every rejection bucket prints `rejected of reached`. Without it no
future session can tell a working gate from an unreachable one, and this entry will recur verbatim.
`[[lessons-zero-needs-a-denominator]]`, fourth sighting in this file family.

**THE AUTHOR'S HYPOTHESIS, which this entry exists to test** — *"we may find that we are not doing the
same checks as we do for solids, or that solids are having the same issue."* Both halves are open:
* T26 made `IsSpotEnclosed` shared, and the node path DELEGATES to it, so the two paths cannot hold
  different *code*. **That is not the same as running it at the same rate**, and rate is exactly what
  is unmeasured. **SYMMETRY, and the measurement must cover BOTH sides** — count gate-reached on the
  solid path too, or this answers half the question.
* The predicate itself is **8 horizontal rays at 500 cm from Z+200, rejecting at 7 of 8 blocked**. By
  construction that **cannot detect a bowl wider than 5 m, a ledge under an overhang, or a spire top** —
  and a core sitting inside a large rock formation, which is what the author is looking at, may be
  exactly the shape it cannot see. **Whether the gate PASSED that core or never REACHED it is the
  question**, and the counter above is what answers it.

**Do not tune the predicate before the counter lands.** Widening the ray count or radius against an
unmeasured baseline is how a gate gets tuned to satisfy the last screenshot. See [[T34]] for the
in-game symptom and [[T15]] for the silent-shrink class this belongs to.

### T36. `IsSpotEnclosed` IGNORES NOTHING — a PAWN standing near a candidate counts as blocking rays, and the shared predicate is used by BOTH the well and solid paths.
**MEASURED in game 2026-08-09 on `2026-08-09-t35-1`. The author stood on relocated core
`BP_FrackingCore13` (1 m away) and ran `NodeShuffle.Here`:**
```
enclosure ray 1..8 of 8: BLOCKED at 0 cm by Char_Player_C_2147475525
verdict: 8 of 8 rays blocked ... ENCLOSED (a placement here would be refused by this gate)
```
**All eight rays were blocked by the AUTHOR'S OWN CHARACTER, at 0 cm, and the line asserted the spot
was enclosed.** The trace originates inside the player's capsule, hence the zero distance.

**The cause is in the predicate, not the diagnostic** (`NodeShuffleWellFootprint.cpp`, in
`ANodeShuffleSubsystem::IsSpotEnclosed`):
```
FCollisionQueryParams EncParams(FName(TEXT("NodeShuffleEnclosure")), false);   // ignores NOTHING
World->LineTraceSingleByChannel(EncHit, Eye, To, ECC_WorldStatic, EncParams);
```
No actor is ever added to the ignore list. A character blocks `ECC_WorldStatic`, so it is a hit.

**GRADES.**
* Eight rays blocked by the player, at that spot, on that build — **measured**.
* The query params ignore nothing and the channel is `ECC_WorldStatic` — **measured from source**.
* **That a pawn near a PLACEMENT candidate would likewise block rays — reasoned from those two, NOT
  observed**, because [[T35]] establishes the gate has never been reached in placement. It is the same
  function with the same params and no caller-supplied ignore list, so the inference is strong, but it
  is an inference and must be tested, not assumed.
* **Whether a creature (not the player) blocks `ECC_WorldStatic` — UNTESTED.** Do not generalise from
  one `Char_Player_C` sighting to all pawns without checking; that is the POPULATION error this
  workspace keeps repeating.

**TWO SEPARATE FIXES, and they must not be conflated.**
1. **The diagnostic is currently unusable for its only purpose.** `Here` tests where the player stands,
   so measuring the spot under a core REQUIRES standing on it, which guarantees 8/8 blocked. This is a
   catch-22 and the reading can never be obtained as shipped. **Fix: the Here probe must ignore the
   calling player.** Low risk, diagnostics-only.
2. **Whether PLACEMENT should ignore pawns is a BEHAVIOUR change to a gate** and owes a differential
   review. The argument for is strong — a lizard doe wandering past a candidate should not make a spot
   permanently "enclosed", and the gate refuses at 7 of 8 so two or three pawns could do it. The
   argument for caution is that this predicate is SHARED with the ordinary-node path (T26), so a change
   moves both populations at once. **SYMMETRY: whatever is decided applies to both, by construction.**

**WHY IT HAS NOT BITTEN YET.** [[T35]]: the enclosure gate has been reached **zero** times on either
path this session, so this defect is latent. **It becomes live the moment the void/settle gate stops
dominating** — which is exactly what fixing T35 is meant to achieve. **Fix T36 before T35's cause, or
the first thing the newly-reachable gate does is refuse spots because a creature walked past.**

**THE DIAGNOSTIC EARNED ITS KEEP.** This was found only because the ray line prints the HIT ACTOR
rather than a bare blocked count. A `blocked 8 of 8` line would have read as a correct, damning
measurement of the terrain. **Print what you hit, not just that you hit.**

### T37. THE ENCLOSURE GATE IS WORKING CORRECTLY AND IS ANSWERING THE WRONG QUESTION. It asks "am I in a pit?" (7 of 8 rays); the player's constraint is "does the extractor footprint fit?". A core in a nook against a cliff blocks 5, PASSES, and is unbuildable.
**MEASURED in game 2026-08-09 on `2026-08-09-t36-1`, standing on relocated core `BP_FrackingCore13`
(2 m), with the pawn excluded from the trace. This is the first VALID enclosure reading ever taken.**
```
ray 1 (0 deg):   BLOCKED at 499 cm by LandscapeStreamingProxy_...508_3_4_0
ray 2 (45 deg):  BLOCKED at 278 cm by FGCliffActor_1637
ray 3 (90 deg):  BLOCKED at 271 cm by FGCliffActor_1637
ray 4 (135 deg): BLOCKED at 268 cm by FGCliffActor_1637
ray 5 (180 deg): clear      ray 6 (225 deg): clear      ray 7 (270 deg): clear
ray 8 (315 deg): BLOCKED at 358 cm by LandscapeStreamingProxy_...508_3_4_0
verdict: 5 of 8 blocked; refuses at 7 or more; NOT ENCLOSED
```
**The reading passes both validity tests** the T36 review demanded: every distance is non-zero
(268–499 cm) and **two distinct actors** are named, so this is terrain and not a trace originating
inside a body. Ground slope 26.6 deg, and the cliff gate accepts to 60 deg, so that gate passes too.

**A PREDICTION WAS PUT ON RECORD BEFORE THE MEASUREMENT AND IT RESOLVED AGAINST THE FIRST BRANCH.**
Stated: *">=7 blocked ⇒ the gate would have refused and the bug is upstream (T35, it never ran);
3–5 blocked ⇒ the gate deliberately passed and we are testing the wrong property."* **Result: 5.**
The predicate is **not broken, not blind, and not mis-thresholded by accident** — it saw a cliff on
five sides and passed by design.

**SO THE DEFECT IS THE QUESTION, NOT THE ANSWER.** 7-of-8 detects near-total surround: a pit, a hole,
a crevice. The author's actual constraint is whether a **Resource Well Extractor's footprint** fits —
and a core pressed into a nook blocks five rays, passes, and still cannot be built on without the
vanilla snap-mode override ([[T34]]). The author reported **3 usable members** in that group.

**DO NOT FIX THIS BY LOWERING THE THRESHOLD.** 5-of-8 would reject any spot with a single wall behind
it, which is most of the map's interesting terrain, and it would move the ordinary-node path too —
`IsSpotEnclosed` is shared since T26, so **any threshold change is a SYMMETRY change to both
populations at once**. The missing test is **terrain clearance at the building's footprint radius**,
which no current gate performs: `buildableOverlap` tests BUILDINGS, `nodeOverlap` tests NODES, and
neither tests a cliff or landscape inside the footprint.

**GRADES.** The ray pattern, distances, actors, verdict and slope — **measured**. That the extractor
footprint is what refuses — **the author's in-game observation** ([[T34]]), not measured by this mod.
**The footprint radius the game actually requires is UNMEASURED**, and it is the number any fix needs
first. **Whether the ordinary-node path has the same gap — UNTESTED**, and [[T35]] shows its enclosure
gate has also never been reached, so nobody has evidence either way. **Ask that question before
building anything.**

### ★ AUTHOR RULING 2026-08-09 on T34 / T37 — PARTIAL EMBEDDING IS WANTED. The footprint-fit gate is REJECTED. The target is a member FULLY INSIDE a rock.
**The author, verbatim:** *"I like that nodes are partially in something, thinks it adds character. A
smart person uses H to place on it. However, it's the well inside that rock outcrop that we need to
figure out."*

**WHAT THIS SETTLES — do not re-propose any of it.**
1. **A member partly embedded in terrain is CORRECT BEHAVIOUR, not a defect.** The measured core
   `BP_FrackingCore13` at **5 of 8 rays blocked** ([[T37]]) is a spot the author WANTS. The gate passing
   it was right.
2. **T37's proposed footprint-fit gate is REJECTED.** Do not build it. Do not lower the 7-of-8
   threshold — [[T37]] already showed 5-of-8 would reject most interesting terrain, and the author has
   now confirmed those spots are desirable.
3. **The extractor's required footprint radius NO LONGER NEEDS MEASURING.** It was only needed to size
   the rejected gate. Question withdrawn — the author identified this themselves.
4. **T34 is downgraded, not closed.** Using vanilla snap-mode (H) to place on a partly-embedded member
   is the intended player experience, not a workaround. What remains open in T34 is only the count of
   members a player could not use at all.

**WHAT IS ACTUALLY OPEN, AND IT IS NARROWER AND CHEAPER.** A member **fully inside** a rock outcrop —
not touching it, not partly in it — is unusable by any means, including H. **That is the only case to
detect.** And it may need no new predicate at all: `IsSpotEnclosed`'s 7-of-8 threshold is a reasonable
test for *fully inside*, and [[T35]] shows the gate **has never once been reached** on either path. So
the plausible outcome is that **the existing gate is correct for the case the author cares about and
the entire fix is making it RUN** — which is [[T35]], already open, rather than new machinery.

**THE MEASUREMENT THAT DECIDES IT IS NOT YET POSSIBLE.** `NodeShuffle.Here` probes where the player
STANDS, and nobody can stand inside a rock. The buried member in the author's screenshots has never
been measured. **This is what `NodeShuffle.PointAtHere` is for** (author-requested, alongside `Here`,
not replacing it): aim at the spot, trace from the camera, run the same predicate at the hit point.
**If the buried member reads 7–8 of 8 by terrain actors, the predicate is already right and T35 is the
whole job. If it reads 5 or fewer, the predicate cannot see the case the author cares about and a new
one is justified.** Do not build any fix before that reading.

**A NOTE ON HOW THIS RULING AROSE, because it saved a packet.** The prior entry proposed a footprint
gate off a correct measurement and a correct inference. It was the AUTHOR's taste — *partial embedding
is character* — that made it wrong. **A measurement can establish what IS and never what is WANTED;
this workspace has now had the author overturn a well-evidenced direction twice in one session.**

### T40. `IsSpotEnclosed` COUNTS THE PLAYER'S OWN BUILDINGS AS ENCLOSURE. 24 of the 28 blocked rays in the first full group probe were `Build_FrackingExtractor_C` — 3 of 7 members read REFUSED purely because the author had built extractors on them.
**MEASURED in game 2026-08-09 on `2026-08-09-t39-1`, `NodeShuffle.WellProbe` over group
`BP_FrackingCore13`. 7 of 7 members probed, 7 resolved from live spawned actor transforms, 0
unresolved, 0 with zero rays cast. The saved record agreed with the live actor to 0–1 cm on every
member, so the probed points are not stale.**

| member | blocked | eye inside solid | blocked by |
|---|---|---|---|
| core `BP_FrackingCore13` | 1 of 8 | NO | landscape at 399 cm |
| sat 81 | 0 of 8 | NO | — |
| **sat 82** | **8 of 8** | **YES** | **`Build_FrackingExtractor_C`, all 8 at 0 cm** |
| sat 83 | 0 of 8 | NO | — |
| **sat 84** | **8 of 8** | **YES** | **`Build_FrackingExtractor_C`, all 8 at 0 cm** |
| **sat 85** | **8 of 8** | **YES** | **`Build_FrackingExtractor_C`, all 8 at 0 cm** |
| sat 86 | 3 of 8 | NO | landscape at 102 / 135 / 180 cm |

**Across the whole probe: 24 blocked rays were the author's own extractors and 4 were terrain.**

**WHY IT MATTERS.** `FCollisionQueryParams` in `IsSpotEnclosed` ignores nothing ([[T36]]), so a
**buildable** is enclosure to this predicate. A player who builds on their wells manufactures permanent
"enclosed" spots. **The mod already has a `buildableOverlap` gate — the correct mechanism, with the
correct label.** Enclosure double-counts buildings under a wrong name, and the census would attribute
the refusal to the wrong gate. **Latent only because [[T35]] shows the gate has never been reached.**

**WHAT IT SETTLES ABOUT THE PREDICATE — and this is the useful half.** The predicate **does** detect
"fully inside a solid": three members inside an extractor's collision produced eye-inside-solid YES and
8 of 8 at 0 cm. It correctly returned **3 of 8** for a partially-embedded member and **1 of 8** for a
core beside landscape — exactly the "adds character" case the author wants kept. **The predicate is
working. On this evidence the fix for a buried member is [[T35]] — make the gate RUN — not new
machinery.**

**GRADES.**
* Every figure above — **measured**, with the hit actor named on each ray.
* "The predicate detects fully-inside-a-solid" — **measured for BUILDINGS (primitive collision).**
* **"Therefore it would detect a member inside ROCK" — INFERRED, NOT MEASURED.** Landscape heightfields
  and complex-as-simple triangle meshes do not necessarily report containment the way a primitive does,
  and the eye boolean's NO is unproven over exactly those two types ([[T39]] review). **No member of
  this group was inside rock, so the case the author cares about STILL HAS NO DIRECT MEASUREMENT.**
* Whether these three members are the ones the author found unplaceable — **untested**; they are
  occupied by working extractors, which is the opposite of unplaceable.

**THE ORCHESTRATOR'S ERROR THIS CORRECTS, RECORDED BECAUSE IT IS THE THIRD OF ITS KIND TODAY.** Before
this run I told the author that eye-inside-solid YES at a member's own location "**is** the defect —
it literally means the member is inside a rock." **That was an assertion about what a measurement
MEANS, made without testing what the solid was.** It was a building. **The only reason this was caught
is that the ray line prints the HIT ACTOR** — a bare "8 of 8 blocked" would have been read as proof of
a buried member and sent the next packet at the wrong target. Same lesson as [[T36]]: **print what you
hit, not just that you hit.**

### T41. THE ENCLOSURE PREDICATE IS BLIND TO A MEMBER INSIDE A ROCK FACE — it casts 8 HORIZONTAL rays at ONE height and has NO vertical sampling at all. A member the extractor snaps to *inside a cliff* reads 0 of 8 blocked.
**MEASURED 2026-08-09/10 on `2026-08-09-t39-1`. This closes the question T35–T40 were circling, and it
resolves AGAINST the cheap outcome the orchestrator predicted.**

**The evidence, from three commands in one session.**
* The author photographed a Resource Well Extractor hologram **snapping to a well member inside a
  vertical rock face**, and the log shows `HOLOGRAMHOOK ... snapped=1 disq=[<none>]` at that moment.
* `NodeShuffle.PointAtHere`, aimed at that face: the aim ray hit `FGCliffActor_1637`, component
  `CliffMesh`, at 1030 cm. **The enclosure predicate at that impact point returned 0 of 8 rays blocked,
  eye-inside-solid NO.** The same line reports the ground slope there as **68.3 deg, which the cliff
  gate (60 deg) WOULD REJECT** — so a different gate sees the problem this one cannot.
* `NodeShuffle.WellProbe` over that group had already probed **`BP_FrackingSatellite81` at its own
  location** — `V(X=101626.38, Y=159434.02, Z=1863.36)` — and returned **0 of 8 blocked, eye NO, "not
  refused"**. That member is **~6.4 m from the aim impact**, and is the nearest probed member to it.

**THE MECHANISM, read from the predicate rather than inferred from the symptom.** `IsSpotEnclosed`
casts **8 rays on the horizontal plane** (bearings 0–315 deg) from `Z + 200`, each **500 cm**, and
refuses at 7 of 8. **There is no up-ray, no down-ray, and no vertical component of any kind.** A member
at the base of a face, under an overhang, or set into a wall therefore has open air in most horizontal
directions at eye height and reads CLEAR — while being visually and practically inside the rock. **This
is not a threshold problem and not a tuning problem; the sampling geometry cannot represent the case.**

**A SECOND CONTRIBUTOR, measured and not to be conflated with the first.** The aim trace runs
`ECC_Visibility` with **complex** collision; the enclosure rays run `ECC_WorldStatic` with **simple**.
A cliff's simple hull can differ substantially from the mesh the player sees and the hologram snaps to,
so even a horizontally-enclosed spot may read clear on the gate's channel. **Which of the two dominates
here is UNTESTED.**

**WHAT THIS OVERTURNS.** [[T40]] concluded from three members inside an extractor's collision that "the
predicate detects fully-inside-a-solid, therefore the fix is [[T35]] — make the gate run." **The first
half is still true and the inference is now FALSIFIED:** it detects being inside a **primitive-collision
building**, and does not detect being inside a **cliff**. The orchestrator's on-record prediction —
*">=7 blocked means the predicate already works and this is only plumbing"* — is **wrong**. Making the
gate run would NOT have fixed the author's case, and shipping that conclusion would have closed the
investigation on a defect that remains.

**WHAT IS STILL NOT MEASURED.** Whether adding vertical sampling would refuse the spots the author WANTS
kept — a partially embedded member reads 3 of 8 today ([[T40]] sat 86) and **the author has ruled those
must keep passing** ([[T34]]/[[T37]] ruling). **Any fix must be checked against that ruling before it
ships, and `WellProbe` is now the instrument that can do it.** Also unmeasured: whether the ordinary
node path has the same blindness — `IsSpotEnclosed` is shared, so **by construction it does**, but its
consequences there are untested.

### T43. `TActorIterator<AFGResourceNode>` CANNOT SEE FRACKING CORES — or any modded node class deriving directly from `AFGResourceNodeBase`. Confirmed from the engine headers, twice, independently.
**The `Foo*`-excludes-`FooBase` signature, which this workspace has shipped before. Found in passing by
`ns-t42-centreshadow` and CONFIRMED by an independent cold review from the headers:**
* `AFGResourceNodeFrackingCore : public AFGResourceNodeBase` (`FGResourceNodeFrackingCore.h:14`)
* `AFGResourceNode : public AFGResourceNodeBase` (`FGResourceNode.h:66`)
* `AFGResourceNodeFrackingSatellite : public AFGResourceNode` — **satellites DO appear; cores do not.**

`BuildWellNodeScanCache` and `ValidateWellMemberSpot` iterate `TActorIterator<AFGResourceNode>`, so the
node-overlap gate is **blind to every fracking core in the world**, and blind to any node class a mod
declares directly under `AFGResourceNodeBase`. An in-repo comment already stated this; nobody had
verified it or drawn the consequence.

**This is a POPULATION defect, not a predicate defect** — the gate's arithmetic is fine and it is
looking at the wrong set, which is the failure this workspace's own review rules single out because
every other gate passes while it happens.

**IT IS LOAD-BEARING FOR A STANDING AUTHOR CONSTRAINT.** The author (2026-08-09): *"We've already
worked on them [lithium, lead, chlorine] and have them going, I just don't want them excluded in new
things we do."* A modded resource whose node class derives from `AFGResourceNodeBase` rather than
`AFGResourceNode` is **silently outside this gate**, and no vanilla-only test session can reveal it —
tonight's log contains 13 descriptors and every one is vanilla. See
[[nodeshuffle-modded-nodes-in-scope]].

**It also plausibly explains an older finding** — `docs/TECH-DEBT.md` records a solid Sulfur node
appearing **45.9 m** from a relocated chlorine well core, with the note *"node placement does not know
wells exist"*. A core invisible to the iterator is a concrete mechanism for that. **PLAUSIBLE, NOT
MEASURED — do not write it up as the cause until someone tests it.**

**Before fixing:** widening the iterator to `AFGResourceNodeBase` changes the POPULATION of a live gate
and would move both the well and ordinary-node paths at once. It needs a differential review and a
before/after count, not a one-line type change. **Measure what the gate currently sees and what it
would then see, first.**

### T44. THE CENTRE-CONTAINMENT CANDIDATE DOES NOT SEPARATE THE CASES — it would refuse the member the author explicitly wants KEPT, and every one of its NOT-INSIDE readings is UNPROVEN. Prediction falsified; the rule is not retired, the implementation is.
**MEASURED 2026-08-09/10 on `2026-08-09-t42-1`, `NodeShuffle.WellProbe` over `BP_FrackingCore13`,
7 of 7 members probed. The shadow metric gated nothing, which is the only reason this cost one run.**

| member | shipped gate | candidate | control built? |
|---|---|---|---|
| core (1 of 8) | accept | **CENTRE INSIDE** → refuse | yes |
| sat 81 — the buried one (0 of 8) | accept | **CENTRE INSIDE** → refuse | yes |
| sat 82 (8 of 8, extractor on it) | refuse | NOT INSIDE → accept | **NO** |
| sat 83 (0 of 8) | accept | **CENTRE INSIDE** → refuse | yes |
| sat 84 (8 of 8, extractor) | refuse | NOT INSIDE → accept | **NO** |
| sat 85 (8 of 8, extractor) | refuse | NOT INSIDE → accept | **NO** |
| **sat 86 (3 of 8, partly embedded — AUTHOR WANTS IT KEPT)** | accept | **CENTRE INSIDE** → refuse | yes |

**Shipped gate refuses 3, candidate refuses 4, they agree on 0 of 7 and disagree on all 7.**

**THE PREDICTION, RECORDED BEFORE THE RUN, WAS WRONG ON 3 OF 6 NAMED MEMBERS.** Predicted: sat 81
INSIDE (**correct**); core and sat 86 NOT INSIDE (**both wrong — both read INSIDE**); 82/84/85 NOT
INSIDE once buildables excluded (**correct in value, but see below**).

**THE DECISIVE FAILURE: sat 86 reads CENTRE INSIDE.** That is the partially-embedded member the author
ruled must keep passing ([[T34]]/[[T37]] ruling). **Shipping this as a gate would have deleted exactly
the terrain the author said adds character** — the outcome the shadow-metric discipline existed to
prevent, caught for the price of one run and zero placement changes.

**AND ALL THREE "NOT INSIDE" READINGS ARE UNPROVEN.** Each of 82/84/85 reports the positive control as
**not built** — *"a walk from the sky start down to 20000 cm below the tested point found no counted
surface at all"*. By the metric's own trust rule a NOT INSIDE without a passing control is **unproven,
not open air**. So the candidate has **zero trustworthy accepts** in this run. That the 3 unproven ones
are exactly the 3 with extractors built on them is a **correlation, and its cause is UNTESTED.**

**WHAT IS NOT ESTABLISHED — do not conclude the author's rule is wrong.** The rule is about a node's
centre being inside solid; this implementation of it failed. Two candidate explanations, neither tested:
* the walk is wrong (it is known to **fail toward NOT INSIDE**, and the control failed on 3 of 7); or
* **the members really are below the terrain surface.** The same command's ground line reports a long
  downward trace landing **+2 m, +13 m, +13 m, +5.7 m, +5.7 m, +21.5 m** above the members. **If a
  member sits 13 m under the surface, "centre inside solid" is TRUE and correctly reported** — and the
  real defect is elsewhere entirely. **That Z discrepancy has been printed all evening and has never
  been explained. Explain it before building another predicate on verticality.**

**NEXT, IN ORDER, AND NOTHING ELSE UNTIL THE FIRST IS DONE.**
1. **Explain the ground-trace Z gap.** It is unexplained, reproducible, printed per member, and every
   vertical predicate depends on what it means.
2. Then re-examine the walk: the F2 fix ([[T42]]) — a second reading with `SubjectActor = nullptr`
   printed beside the first, ~5 lines — turns the one unmeasured judgement call into a measurement.
3. **Do not tune the candidate to make sat 86 pass.** That is fitting the predicate to the last
   screenshot, which this file already warns against twice.

### T48. THE CENTRE-CONTAINMENT WALK REPORTS "INSIDE" FOR A POINT IN OPEN AIR WHENEVER AN `FGCliffActor` IS ABOVE IT. Proven in game, with a photograph. [[T44]] IS OVERTURNED — the author's rule was never tested.
**MEASURED 2026-08-10 on `2026-08-09-t47-1`. The author stood on open ground under a rock overhang,
photographed the overhang above and their own feet below, aimed straight down and ran
`NodeShuffle.PointAtHere`.**
```
aim: pitch -89.9, direction (0,0,-1), hit at 162 cm
     LandscapeStreamingProxy / LandscapeHeightfieldCollisionComponent   <- the actual ground
probe eye inside solid geometry: NO                                      <- eye is in open air
ENCLOSURE GATE: 5 of 8 rays blocked -> not enclosed
SHADOW centre-containment: CENTRE INSIDE                                 <- FALSE
ground trace from that point: FGCliffActor_1628 / CliffMesh              <- the overhang above
```
**The point is the ground under the author's boots. The eye check says NO. The enclosure rays say 5 of
8. The photograph shows open air. The walk says INSIDE.**

**THE PATTERN, across every reading taken tonight — it is perfectly mechanical:**
* trace above the point hits an **`FGCliffActor`** → **CENTRE INSIDE** (well members 1, 2, 4, 7, and
  this open-air point)
* trace above hits **`LandscapeStreamingProxy`** → **NOT INSIDE** (member 5, after the author deleted
  the extractor above it — a clean single-variable control)
* trace above hits an **excluded buildable** → **NOT INSIDE**, with the positive control unbuilt
**The walk is not measuring containment. It is measuring "is there an un-excluded `FGCliffActor` above
me."** It enters the cliff mesh from the sky and never registers the exit from its underside;
landscape heightfields exit correctly.

**WHAT THIS OVERTURNS.**
1. **[[T44]] IS WRONG.** It concluded the author's centre rule "does not separate the cases" because
   the candidate would refuse satellite 86, the partially-embedded member the author wants kept. **86
   read INSIDE for this reason, not because its centre is buried.** The rule is **UNTESTED, not
   disproven** — and every conclusion drawn from T44 must be re-derived.
2. **THE AUTHOR'S CAVE CONCERN WAS EXACTLY RIGHT, AND THIS IS THE MECHANISM.** They asked *"that
   doesn't mean it's considered a cave right? We don't want to break our putting nodes in caves."* A
   cave floor has rock above it. **This predicate would have condemned every cave placement in the
   world**, and it would have looked like a correct measurement while doing it.
3. **The shadow-metric discipline is what saved it.** The candidate gated nothing, so a predicate that
   is wrong in the most dangerous possible direction cost two console commands and no regression.

**WHAT IS STILL TRUE AND MUST NOT BE RE-LITIGATED.** [[T41]] stands on its own evidence — the shipped
enclosure gate has no vertical sampling and reads 0 of 8 for a member inside a cliff face. [[T40]]
stands. The `RaycastGroundAt` behaviour is now understood: it starts high and stops at the FIRST
surface, so under an overhang it returns **the top of the overhang** — measured at **+23 m** above the
author. That is the whole of the unexplained +2 m to +21.5 m gap. **Mystery closed; it was never a
cave.**

**NEXT.** Fix the walk's exit detection against `FGCliffActor` static meshes, then re-run the SAME
readings — the author's rule gets its first real test only after that. **Do not tune the walk against
satellite 86.** The single best regression test now exists and is free: **a point on open ground under
an overhang must read NOT INSIDE**, and the author has the coordinates.

### T50. THE CONTAINMENT WALK SPENDS ITS ITERATION BUDGET ON HITS IT THEN DISCARDS — the player's own capsule consumed 28 of 32 inbound and 31 of 32 outbound hits, so the walk never reached the geometry it existed to count. Root cause of [[T48]].
**MEASURED 2026-08-10 on `2026-08-10-t49-1`, `PointAtHere` at the known-correct test point
`V(X=81329.41, Y=156623.09, Z=2112.49)` (open ground under an overhang; correct answer NOT INSIDE).
The per-crossing instrument printed the whole arithmetic:**
```
INBOUND : 32 blocking hits, 28 EXCLUDED -> 4 counted, 4 front, 0 back
OUTBOUND: 32 blocking hits, 31 EXCLUDED -> 1 counted, 1 front, 0 back
entries = 4 ; exits = max(inbound back 0, outbound front 1) = 1 ; net = 3 >= 1  => CENTRE INSIDE
"A walk used all of its 32-hit iteration budget."   <- BOTH did
```
**Nearly every discarded hit was `Char_Player_C` — the caller's own pawn, passed as the subject actor.**

**DEFECT 1, primary. An EXCLUDED hit still costs a step of the budget.** The walk hits, tests, discards,
steps 2 cm past, and hits the same actor again. A player capsule is roughly 180 cm tall, so at a 2 cm
step it can absorb ~90 hits on its own — the 32-hit budget is gone long before the walk reaches any
terrain. **The exclusions are applied AFTER the trace instead of inside it.** `FCollisionQueryParams`
already carries `AddIgnoredActor`, which `IsSpotEnclosed` itself uses for the pawn ([[T36]]) — an
ignored actor is never returned at all and cannot consume a hit. **That is the fix, and it is the same
mechanism the sibling predicate already uses.**

**DEFECT 2, and it is why nothing caught defect 1.** **Back faces were ZERO on both walks.** The
`max(inbound back faces, outbound front faces)` was written precisely because nobody could settle from
the headers whether this engine reports back faces; this is the first evidence, and it says **it does
not** — so the outbound-front-face term is the ONLY exit mechanism there has ever been. Budget-starved
to 1 counted crossing, it under-reports exits, and the walk **fails toward INSIDE** whenever an
excluded actor stands near the tested point. **Note this inverts what the T42 review predicted** (it
reasoned the `max()` would make the walk fail toward NOT INSIDE, because a spurious exit wins). The
review's logic was right and its premise — that back faces might be reported — was wrong.

**WHY IT LOOKED LIKE "cliff above ⇒ INSIDE".** [[T48]] found that correlation and it is real, but it is
a symptom: the reading is taken where the player stands, so the player is always near the tested point,
and whatever geometry survives the exhausted budget decides the verdict. **The cliff was never the
cause. Do not fix this by special-casing `FGCliffActor`.**

**WHAT THIS DOES NOT SETTLE.** Whether the same starvation explains the well members' INSIDE readings
is **UNTESTED** — there the subject is the member's own actor, not the pawn, and nobody has looked at
its hit count. **Re-run `WellProbe` after the fix and compare, do not assume.** And the author's centre
rule remains **untested** — [[T44]]'s failure is now attributed to two instrument defects, so the rule
gets its first honest test only after both are fixed.

**THE FIX, IN ORDER.** (1) Move all three exclusions into `AddIgnoredActor` on the query params so an
excluded actor never returns a hit. (2) Re-run the free regression test above — it must read NOT
INSIDE. (3) Only then re-run `WellProbe` and grade the members. **Do not tune anything against
satellite 86.**
