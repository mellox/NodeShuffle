# Cold review (FULL, round 2) — ns-t7b-reroll: A3 (deal-first) + A4 (`RerollRelocatedWells`)

- **Repo / branch / base HEAD:** `C:\Claude\Projects\NodeShuffle`, `wip/h2-relocation`, `5ed3ac2`
- **Files reviewed (only these three):**
  - `C:\Claude\Projects\NodeShuffle\Source\NodeShuffle\Private\NodeShuffleWellRelocateRoll.cpp` (519 → 864)
  - `C:\Claude\Projects\NodeShuffle\Source\NodeShuffle\Private\NodeShuffleConfig.cpp`
  - `C:\Claude\Projects\NodeShuffle\Source\NodeShuffle\Public\NodeShuffleConfig.h`
- **Out of scope, confirmed dirty, not a finding:** `NodeShuffleWellSpawn.cpp`, `NodeShuffleWellRetype.cpp/.h`, `docs/TECH-DEBT.md` (packet `ns-t17-retype-spawned`).
- **Reviewer:** cold. Did not author. **No build, no commit, no edit** outside this file. `stash@{0}` not touched.
- **Change class:** structural review-response change to a path-replacing change → full re-review, per round 1's own closing instruction.
- **VERDICT: DO NOT SHIP.** One blocker (F-A), two high findings (F-B, F-C).

---

## 0. Executive summary — read these five lines

1. **A3 works.** I walked every state. The claim *"no field of the layout entry is written on the
   success path until a destination exists"* is **true**, and the deal-failure orphan (round 1's
   blocker F2) is **closed**. §1 F-0 and parity rows 8/13.
2. **The self-exclusion rule is sound.** Two wells cannot be dealt the same destination, and no well
   can be dealt the site of a well that then fails to move. The packet's rejection of the reviewer's
   suggestion was **correct**. §2 rows 14–15.
3. **BLOCKER F-A: round 1's F1 is reachable again by a different door.** Both safety layers — T16's
   roll-time pin *and* the new commit-time gate 1 — read the **same session-scoped
   `SpawnedWellCores`/`SpawnedWellSatellites` maps**, and both return "not in use" when the maps hold
   nothing for the entry. Gate 1 treats **0 handles resolved as a PASS** and logs it as
   `gate PASSED -- 0 of N member handle(s)`. Phase 1 proves the *vanilla original* site is streamed;
   **nothing anywhere proves the relocated destination is.** A well the player has built on 4 km away
   can be torn from the layout, its claim withdrawn, and a duplicate dealt elsewhere.
4. **F-B: "byte-for-byte with the toggle OFF" is FALSE.** `WellDestinations` is now built from
   pre-capture entry state, so a candidate's *stale* destination becomes a blocking site for every
   other well. It was cleared before this list was built at HEAD. Different destinations, different
   `DealRng` consumption, more `deal-failed` — with the toggle **off**.
5. **F-C: the tooltip ships a false factual claim** — "removed and rebuilt at its new spot **within a
   single frame**". `NodeShuffleWellRelocateApply.cpp:441` defers every spawn behind
   `IsLocationNearAnyPlayer(E.DestCoreLocation, SpawnRadiusCm)`. On the author's save, one keypress
   makes 17 wells **vanish until visited**, possibly forever. Third false claim in this panel today.

**Format strings: independently re-verified, packet's §4 confirmed.** My own parser over the final
file: 25 `UE_LOG` calls, **0 spec/arg mismatches**, verbosity a bare identifier in all 25,
`TEXT(`-count == literal-count in all 25 (so no two-argument `TEXT()`), 29/29 on the summary line.
Config key `RerollRelocatedWells` matches the struct field name exactly. §9.

---

## 1. Findings — hardest first

### F-0 — WHAT IS ACTUALLY FIXED (stated first, so the rest is not read as a rejection)

Round 1's **F2 (deal-failure orphans a well)** is closed, and I could not break it. Traced state by
state through `NodeShuffleWellRelocateRoll.cpp:451-472` (phase 1 writes only the local
`FNodeShuffleWellPendingCapture`), `:550-611` (phase 2 writes only `P.Dest`/`P.bDealt`), `:632-826`
(phase 3). Phase 1 contains **no write to `FNodeShuffleWellEntry` on any success path** — I checked
every statement; the only entry writes in phase 1 are inside refusal branches (`NoteRefusal`,
`E.bRelocate`, and the guarded `E.bOffsetsCaptured` at `:441`). The commit block `:753-808` is
straight-line with exactly two `continue`s (`:704`, `:737`), both **before** the first write to `E`.
Round 1's F5 (counter overlap), F6 (moved-again vs first-relocation), F4 (five refusals asserting
"left vanilla") are all genuinely fixed. **The phase-1 partition is provable, not merely asserted:**
I checked all ten exits and each increments exactly one bucket.

---

### F-A — **BLOCKER** — the occupancy gate is FAIL-OPEN on unresolved handles, and both "independent" layers fail together
`NodeShuffleWellRelocateRoll.cpp:671-710` (gate 1) · `NodeShuffleWellRetype.h:209-236` +
`:127-132` (`EvaluateWellPin` / `IsDecisive`) · `NodeShuffleWellDespawn.cpp:147-170`
(`DespawnWellGroup`'s null-handle path) · `NodeShuffleWellRelocateApply.cpp:441`

**The defect.** Gate 1 resolves the group's live handles out of the two runtime maps:

```cpp
if (AFGResourceNodeFrackingCore* PlacedCore = SpawnedWellCores.FindRef(E.CorePath)) { ++Tested; ... }
if (InUseActor.IsEmpty()) { for (S : E.Satellites) { ...SpawnedWellSatellites.FindRef(...)... } }
if (!InUseActor.IsEmpty()) { ++AbortedInUse; ...continue; }
// falls through and logs "gate PASSED"
```

When **neither map holds anything for this entry**, `Tested == 0`, `InUseActor` is empty, and the gate
**passes**, printing `re-enrolment occupancy gate PASSED -- 0 of N member handle(s) were live and none
reported in use, so the teardown may run.` A vacuous negative is reported as a positive clearance —
the workspace's *"a zero without a denominator is not evidence"* shape, except here the denominator
*is* printed and nothing acts on it.

The roll-time pin does not save you, because it has the **same** dependency:
`EvaluateWellPin` (`NodeShuffleWellRetype.h:209`) only reaches the spawned satellites *if the spawned
core resolved*; otherwise it falls back to `OriginalWhileRelocated` and tests the **hidden, de-collided,
deregistered vanilla actors** — which is exactly the false negative T16 exists to prevent. The verdict
is then **not decisive** (`IsDecisive()`, `:127-132`, deliberately excludes that source), so `E.bPinned`
is neither set nor cleared and keeps its saved value. `E.bManaged` follows `E.bPinned`
(`NodeShuffleWellRoll.cpp:296-310, 358`). For a well built on **since** the last roll, the saved
`bPinned` is `false` → `bManaged == true` → **the `!E.bManaged` guard at `:257` does not fire.**

`DespawnWellGroup` then also finds nothing: `SpawnedWellCores.FindRef` null → `Remove` the slot;
each satellite `if (!Sat) { Remove; S.bPlaced = false; continue; }`. `Destroyed == 0`,
`RefusedOccupied == 0` → **`bAllClear = true`**, and the `WELLH2-DESPAWN` line is gated on
`Destroyed > 0 || RefusedOccupied > 0` (`NodeShuffleWellDespawn.cpp:172`) so **it prints nothing at all**.
The commit proceeds: capture written, `bGroupPlaced = false`, `ClearAbandonedWellPlacement` zeroes
`PlacedCoreLocation`, new destination written, `RE-ENROLLED (MOVED AGAIN) … claimWithdrawn=1`.

**Concrete failure scenario.**
1. Session N: `ShuffleResourceWells` + `RelocateResourceWells` + `RerollRelocatedWells` all ON. Well
   `BP_FrackingCore10` (vanilla site **V**) is relocated to **D**, ~4 km away. Player flies to D,
   builds a Resource Well Pressurizer on the core and three Extractors on satellites, runs a nitrogen
   line off it. **No re-roll happens while they are there.** `E.bPinned` is still `false` in the save.
   Save, quit.
2. Session N+1: player loads at their base near **V**. `AdoptRestoredWellGroups` is **single-shot at
   first apply** (`NodeShuffleWellLink.cpp:138`) and adopts only what is resident at that instant; the
   lazy re-adoption path runs inside `SpawnWellGroup` only. D is not visited.
3. Player presses **Re-roll Layout**.
   - `RollWellLayout` → `EvaluateWellPin` → `Source = OriginalWhileRelocated` → not pinned, **not
     decisive** → `bPinned` stays `false` → `bManaged = true`.
   - `RollWellRelocation` phase 1: the **vanilla** core at V *is* in the census, all satellites live →
     capture succeeds → `CAPTURED as a RE-ENROLMENT (already relocated) candidate`.
   - Phase 2: dealt **D2**.
   - Phase 3 gate 1: `Tested = 0` → **`gate PASSED -- 0 of 7 member handle(s)`**.
   - `DespawnWellGroup`: nothing resolved → `bAllClear = true`, **no log line**.
   - Commit: claim withdrawn, `bGroupPlaced = false`, `DestCoreLocation = D2`.
4. **End state:** the player's pressurizer and three extractors are standing on NodeShuffle-spawned
   actors at D that **no layout entry names any more** — claim withdrawn, coordinates zeroed, handles
   dropped from both maps. A fresh group of the same resource will spawn at D2 the moment the player
   goes there. That is round 1's F1 outcome verbatim — *nitrogen duplication in a mod whose entire deck
   machinery exists to conserve well resources* — plus an orphaned build the sweep will now argue about.

**Reachability grade.** Whether `SpawnedWellCores` can be empty for a placed entry at roll time depends
on Satisfactory's actor residency for save-restored runtime actors — engine/game-internal, so per the
workspace grading cap I may **not** call it "provably reachable" or "provably unreachable" from here.
But the packet's own code documents the state as **expected, named and routine**:
`ENodeShuffleWellPinSource::OriginalWhileRelocated` is literally labelled
`"original-FALLBACK(relocated, no spawned handle)"` (`NodeShuffleWellRetype.h:91`), and
`NodeShuffleWellRetype.cpp:214-219` states the non-decisive verdict is **"GUARANTEED to occur on apply
pass 1 of every session"** for every relocated well. The mod believes in this state. T7b makes it
destructive.

**Why this is a blocker and not a "harden it later".** The direction is wrong on the one path in the
mod that destroys player-facing actors, the failure is silent (no `WELLH2-DESPAWN` line at all), and
the fix is six lines that cost nothing when the handles *do* resolve. Fix in §3 B1, verbatim.

---

### F-B — **HIGH** — A4's "byte-for-byte with the toggle OFF" is FALSE: `WellDestinations` is seeded from pre-capture state
`NodeShuffleWellRelocateRoll.cpp:536-543` vs `HEAD:432-437`

At HEAD the capture block ran **first** and set `E.bDestDealt = false; E.DestCoreLocation = ZeroVector;`
(`HEAD:346-347`) for every entry it captured. `WellDestinations` was built **after** that, so a
candidate's stale destination contributed **nothing** to the spacing list.

Under A3 the capture writes nothing, so when `WellDestinations` is built at `:538-543` every captured
candidate still carries its previous `bDestDealt = true` / `DestCoreLocation`. `SelfSiteIndex` excuses
the owner from its own stale site — but **every other candidate must now clear 2 × 6500 cm of a site
nobody is standing on and nobody will stand on.**

This is **not gated by `bRerollRelocated`**. It applies with the toggle OFF.

**Concrete failure scenario.** 20 wells; 3 were dealt destinations on an earlier roll and the player
never travelled there, so they sit `bDestDealt=1, bGroupPlaced=0` (the *normal* state — the apply
defers spawning until a player is near, `RelocateApply.cpp:441`). Player re-rolls with
`RerollRelocatedWells` **OFF**. At HEAD those three phantom sites are cleared before the spacing list
exists, and a fourth well is legitimately dealt into that area. Under T7b those three sites block it;
with a 130 m exclusion radius and `WellRedealTries = 24`, that well reports `deal-failed` and stays
vanilla. Every subsequent draw in the roll also shifts, because `DealRng` is consumed differently.

**Consequences:** (a) A4's stated safety property is void — the save the author's 17 wells depend on
*does* diverge with the toggle off; (b) the T8 determinism regression guard will show different
destinations for the same seed and will be blamed on the wrong thing; (c) it compounds — see F-F.

Handoff §6.7 ("the draw sequence should be unchanged for a save with the new toggle OFF") is
**disproved**. Verbatim fix in §3 B6.

---

### F-C — **HIGH** — the toggle's tooltip asserts a fact the code contradicts
`NodeShuffleConfig.cpp` (`RerollRelocatedWells` tooltip) vs `NodeShuffleWellRelocateApply.cpp:441`

> "Each one is **removed and rebuilt at its new spot within a single frame**; the log line 'WELLH2-ROLL:
> re-enrolment teardown' reports how many groups, how many actor removals and how many milliseconds
> that took…"

Only the **removal** happens in that frame. The rebuild is behind
`if (!IsLocationNearAnyPlayer(E.DestCoreLocation, SpawnRadiusCm)) { ++Deferred; continue; }` and cannot
run until the player physically travels to the new coordinate — the same limit the
`RelocateResourceWells` tooltip already states two panels up ("A well cannot actually move until you
travel to its destination and the terrain loads").

**Failure scenario, as a player experiences it.** Author enables the toggle on the 17-well save
exactly as the tooltip instructs and presses Re-roll Layout. All 17 relocated wells are **destroyed in
that frame**, their vanilla twins stay hidden/de-collided/deregistered, and the new groups exist only
as coordinates in the save. The map now has **zero visible resource wells** until the player flies to
17 separate destinations. Nothing warns them; the tooltip they just read told them it was already done.
Any well whose new destination they never visit is gone for the rest of the save.

Today's precedent is exactly this: a false claim shipped in this panel, then a second false claim
shipped in the fix for it. This is the third. Verbatim replacement wording in §3 B4.

Two smaller factual problems in the same string:
- *"it is never left with nowhere to be"* — an absolute **safety claim**, which handoff §2 says the
  tooltip deliberately avoids making. It happens to be true under A3, but it is the same class of
  sentence that failed twice today. Downgrade to what is measured.
- *"Each well gets **24** draws"* — `WellRedealTries` transcribed as a literal into a player-facing
  string, in the same packet whose F3 flags that constant for re-sizing. A borrowed constant with no
  single source; it will be wrong the day the cap moves. See F-H.

**Correct in the tooltip, and credited:** the "17 moved wells out of 20" figure is scoped as *"the save
this was developed against"* — presented as this-machine measurement, not as a universal. That is the
right form and should be kept.

---

### F-D — **MEDIUM** — `NoteRefusal` is applied to the `!E.bManaged` branch, which sits ABOVE the toggle gate, so it flips a T16-shipped state with the toggle OFF
`NodeShuffleWellRelocateRoll.cpp:210-214` (`NoteRefusal`) and `:257-271`, vs `HEAD:140-145`

- HEAD / `5ed3ac2`: `if (!E.bManaged) { ++RefusedPinned; E.bRelocate = false; continue; }`
- T7b: `NoteRefusal(E)` → `E.bRelocate = E.bGroupPlaced;`

For a **placed, pinned** well (the population T16 created hours ago) `bRelocate` goes **false → true**,
and this branch is **before** the `bRerollRelocated` gate at `:278`, so it happens with the toggle OFF.

I traced the consumers I can reach and **found no failure**, but I am grading it *assumed*, not proven:
- `ReconcileAbandonedWellClaims` (`NodeShuffleWellClaim.cpp:422`) `continue`s on `bGroupPlaced`
  **before** reading `bRelocate`. Safe.
- `ApplyWellRelocation` (`NodeShuffleWellRelocateApply.cpp:432-434`) — the `bRelocate` test is
  **inside** the `!E.bGroupPlaced` branch, so a placed entry takes the maintenance branch either way.
  Safe. (Round 1 graded this "unverifiable"; it is now **provably provided** for this reader.)
- `FinishWellRollTeardown` → the reconciliation above. Safe.
- The status line at `NodeShuffleSubsystem.cpp:6635` (`if (W.bRelocate) WellsRelocateFlagged++`) now
  **counts** placed pinned wells where it previously did not. Cosmetic, and arguably the fix F8 wanted.

Residual: ~37 files, I read the four that matter. `bRelocate` is `UPROPERTY(SaveGame)`, so this
polarity persists into the save and into any future reader. **If A4 is meant to be inert when OFF,
this branch must be inside the gate** — see §3 B5.

---

### F-E — **MEDIUM** — RT-6's headline number is derived, not measured, and it is the number that would have exposed F-A
`NodeShuffleWellRelocateRoll.cpp:723`, `:828-840`

`TeardownMembers += 1 + E.Satellites.Num();` sums the **layout's** group sizes. It is printed as
*"up to %d member Destroy() call(s) in this one frame"*. `DespawnWellGroup` computes the real numbers —
`Destroyed`, `AlreadyGone`, `RefusedOccupied` — and returns only `bAllClear`, so the actual count is
discarded (round 1's F7 was closed for the *boolean*, not for the counts).

This matters twice:
1. The packet deferred the destroy-cap decision on the grounds of *"measured, not capped"*. What ships
   is an **upper bound computed from the save**, not a measurement of the world. That is the workspace's
   *validated shape instead of content* shape: right-looking number, wrong quantity.
2. In F-A's scenario the line would print `17 groups torn down, up to 136 member Destroy() calls,
   0.4 ms` while **zero actors were destroyed**. The contradiction between "136 destroys" and "0.4 ms"
   is the single strongest tell that F-A fired, and nothing in the log connects them.

Fix: have `DespawnWellGroup` report its counts (out-params, no signature break for the other caller),
or at minimum print `TeardownMembers` explicitly as *expected group members, not destroys*. §3 B2.

---

### F-F — **MEDIUM** — a never-placed candidate whose deal fails now keeps a stale `bDestDealt` / `DestCoreLocation`
`NodeShuffleWellRelocateRoll.cpp:636-658` vs `HEAD:346-347, 492-501`

HEAD cleared `bDestDealt`/`DestCoreLocation` in the capture, so a deal failure ended
`bRelocate=false, bDestDealt=false, DestCoreLocation=0`. T7b's `!P.bDealt` path writes **only**
`E.bRelocate = P.bWasPlaced` (false here) and nothing else, so the entry retains a destination from a
previous roll that it is no longer enrolled for.

`ApplyWellRelocation:434` requires `bRelocate`, so nothing spawns there — but **next roll it is a
phantom site in `WellDestinations` again** (F-B), and a well that deal-fails repeatedly reserves an
ancient coordinate forever. `WellEntryNamesAnyPlacedCoordinate` (`NodeShuffleWellClaim.cpp:44-52`)
reads only `PlacedCoreLocation`/`PlacedLocation`, so this does **not** trip the claim-invariant repair —
no log spam, and no detector either.

Fix: on `!P.bDealt && !P.bWasPlaced`, restore HEAD's clear. §3 B6 covers both F-B and F-F.

---

### F-G — **MEDIUM** — the satellite-growth warning is silenced exactly when the feature is on
`NodeShuffleWellRelocateRoll.cpp:278-311`

The `ns-review-h2 F2` warning (*"already relocated but its satellite list has grown from X to Y — N
records have NO rigid-body capture"*) now lives **only** inside the `bAlreadyPlaced && !bRerollRelocated`
branch. With the toggle **ON**, a placed entry that is refused for pin (`:257`), streaming (`:313`,
`:387`), geometry (`:410`) or path mismatch (`:431`) keeps its stale, under-captured group and prints
**nothing** about it — the entry stays placed with `Satellites.Num() != CapturedSatelliteCount` and
uncaptured satellites that will never spawn. Under HEAD every placed entry got the warning every roll.

This is a real diagnostic regression on the population the feature targets, and the sentence the packet
appended to that warning (*"Turning ON 'Re-roll Wells That Have Already Moved' lets a later re-roll
re-capture the whole group"*) is a promise the player can only read while the toggle is **off**.

Fix: hoist the growth check above the `:278` gate so it runs for every `bAlreadyPlaced` entry, whatever
the toggle. §3 B3.

---

### F-H — **LOW** — a placeholder-class constant shipped into a player-facing string
`NodeShuffleConfig.cpp` (`"Each well gets 24 draws"`). `WellRedealTries` lives in
`NodeShuffleSubsystem.h`; the packet's own F3 says it was sized for 3–5 wells/roll and now faces ~20.
The tooltip transcribes the value the packet simultaneously flags as wrong-sized, with no dated TODO at
either surface. Workspace rule: *if a constant is a placeholder, it needs a dated TODO at every surface
that reads it.* Either drop the number from the tooltip or add the TODO at both sites.

### F-I — **LOW (style note, no failure scenario)** — file length and a tripwire that cannot fire
864 lines against a 500-line limit — 73% over, up from 4% over at HEAD. The deferral reason (the
extraction must be a member, and `NodeShuffleSubsystem.h` is not in the path set) is legitimate and
correctly recorded, but this is now the largest single file-size debt in the repo and it should be the
first thing the next packet that *does* own the header pays down.

Separately: the `*** COUNTER PARTITION BROKEN ***` assertion at `:478` is **provably unfireable** as
written (I checked all ten exits; each increments exactly one bucket). That is fine — it is a
regression tripwire, not a live check — but the comment should say so, otherwise a future reader will
read a green partition line as evidence the roll did something.

### F-J — **LOW** — `AbortedInUse` is reachable and correctly framed; `AbortedAfterTeardown` is not
Credit where due: gate 1 tests the spawned satellites **unconditionally**, whereas `EvaluateWellPin`
reaches them only if the spawned core resolved. So gate 1 is a genuine second layer for the
core-missing/satellites-present case, `AbortedInUse` **can** fire with `bManaged=1`, and the log line
saying *"the roll-time pin and this commit-time check disagreed"* is honest and actionable.
`AbortedAfterTeardown` requires the same predicate over the same key set to disagree with itself inside
one frame with no intervening code — I could not construct it. Keep the branch (it is free); do not
expect the line.

---

## 2. Differential / parity table

Invariants the pre-T7b path guaranteed, versus this packet. Rows 1–12 carry forward round 1's numbering
where they still apply; rows 13–18 are new to A3/A4.

| # | Invariant the OLD path guaranteed | How the NEW path provides it | Grade |
|---|---|---|---|
| 1 | A placed group's geography never churns across re-rolls | Deliberately dropped, and now behind `RerollRelocatedWells` (default OFF) | n/a (intentional) |
| 2 | A placed entry always ends the roll with `bRelocate = true` | Restored via `NoteRefusal` at every refusal below the guard, and `= true` on both aborts. But it also flips a state `5ed3ac2` shipped, **above** the toggle gate (F-D) | **assumed** — 4 consumers read, ~37 files exist. **RT-8** |
| 3 | A player's buildings on a relocated well are never destroyed | Provided *only when the spawned handles resolve*. Both layers read the same session-scoped maps; 0 resolved handles = PASS (**F-A**) | **provably ABSENT** for the unresolved-handle case (static); reachability of that case **assumed**. **RT-11** |
| 4 | A relocated well the player built on is never re-enrolled | T16 fixed the pin's *source*; gate 1 adds a second read of the same maps. Neither covers `bPinned=false` + no handle (**F-A**) | **provably ABSENT** in that state. **RT-1, RT-11** |
| 5 | A placed entry keeps `PlacedCoreLocation` / a live claim when it cannot re-capture | Every refusal `continue`s with `bGroupPlaced` untouched; `KeptOrVanilla` reports it from measured state | **provably provided** (static) — round 1 row 5 still holds |
| 6 | A late-appended satellite with no offset never reaches the spawn | Unchanged where it matters: `bCaptured` is still written in exactly one place (`:763`), and refusals happen before any capture write. **But the warning that names the condition is now toggle-gated (F-G)** | **provably provided** for the spawn; **diagnostic regression** |
| 7 | A distant, unstreamed relocated well is never destroyed by a re-roll | Count/path mismatch → `RefusedUnstreamed` → `continue` before phase 3. Protects against an unstreamed **vanilla origin**, not an unstreamed **destination** (F-A) | **provably provided** for the origin; **ABSENT** for the destination |
| 8 | A re-enrolled well always ends up *somewhere* | **NOW PROVIDED.** Deal precedes teardown; `!P.bDealt` touches nothing but `bRelocate`. Round 1's F2 is closed | **provably provided** (static) |
| 9 | Re-capture reads correct transforms from suppressed originals | Unchanged mechanism (`TActorIterator` over hidden, deregistered actors + `GetActorLocation`) | **assumed** — engine-internal. **RT-4** |
| 10 | `ByCorePath` resolves for an already-placed entry | `IsManagedSpawnedNode` filter retained at `:176`; log prints `skippedOurSpawned` | **provably provided** (static) |
| 11 | The roll's counters partition the layout | Ten exits, one bucket each; sum asserted at `:476-487` | **provably provided** (static). The assertion itself cannot fire (F-I) |
| 12 | A save written under the OLD rules stays stable on load | Load path untouched. **But the roll diverges with the toggle OFF** (F-B `WellDestinations`, F-D `bRelocate`, F-F stale dest) | **provably ABSENT** — the byte-for-byte claim is false. **RT-12** |
| 13 | *(new)* Nothing is written to the entry before a destination exists | Verified statement by statement across phase 1 and phase 2; the only entry writes are inside refusal branches | **provably provided** (static) |
| 14 | *(new)* Two wells cannot be dealt the same destination | A dealt `Cand` is appended to `WellDestinations`; `SelfSiteIndex` was assigned before the deal loop, so it can never alias a newly appended index. Later candidates always test it | **provably provided** (static) |
| 15 | *(new)* A well cannot be dealt the site of a well that then fails to move | All three non-move exits (`!P.bDealt`, gate-1 abort, post-teardown abort) leave the site occupied, and only its *owner* was ever allowed to skip it | **provably provided** (static). The packet's rejection of the reviewer's alternative was correct |
| 16 | *(new)* Gate 1 and `DespawnWellGroup` cannot disagree | Same predicate (`IsWellMemberInUse`), same key set, same frame; gate 1 omits `IsValid`, which is the *stricter* direction | **assumed** — occupancy is read through closed-source `AFGResourceNodeBase`. **RT-3** |
| 17 | *(new)* After a post-teardown abort the entry is recoverable | Entry keeps `bGroupPlaced` + claim + coordinates; the re-spawn that would repair it lives in `NodeShuffleWellSpawn.cpp`, which this packet does not own and did not read as current. **No code or log line claims recovery** | **unverifiable statically** — honestly graded by the packet. **RT-13** |
| 18 | *(new)* RT-6's teardown cost is measured | `TeardownMembers` is `Σ(1 + Satellites.Num())` from the layout, not `DespawnWellGroup`'s `Destroyed` (F-E) | **provably ABSENT** — it is a derived upper bound, not a measurement |

**Grading-cap note.** Rows 3 (reachability half), 9, 16 and 17 rest on engine/game-internal mechanisms —
`AFGResourceNodeBase` occupancy, `TActorIterator` over hidden actors, and Satisfactory's residency for
save-restored runtime actors. Per workspace rule none may be promoted above **assumed** from headers or
language reasoning; each carries a measurement step in §4.

---

## 3. Alternatives — with the code I would ship

### B1 (REQUIRED, fixes F-A) — make gate 1 fail CLOSED on unresolved handles
Insert immediately after the satellite loop in gate 1, replacing the unconditional
`gate PASSED` log at `NodeShuffleWellRelocateRoll.cpp:706-709`:

```cpp
            // ns-t7b-r2 F-A (BLOCKER): 0 RESOLVED HANDLES IS NOT A CLEARANCE.
            // Both safety layers for this population read the SAME session-scoped maps: T16's
            // EvaluateWellPin falls back to the HIDDEN vanilla actors when the spawned core does not
            // resolve (source "original-FALLBACK(relocated, no spawned handle)"), and that verdict is
            // deliberately NON-DECISIVE, so E.bPinned keeps its SAVED value -- false for a well built
            // on since the last roll. This gate then tested nothing and reported PASSED. The maps are
            // UPROPERTY() and NOT SaveGame (NodeShuffleSubsystem.h:1830-1831); AdoptRestoredWellGroups
            // is single-shot at first apply (NodeShuffleWellLink.cpp:138), so an entry whose
            // destination has not been visited THIS SESSION has no handle at all.
            // MEASURED, not reasoned: nothing in phase 1 proves the DESTINATION is streamed -- the
            // census gate proves the VANILLA ORIGIN is, and those are different places.
            // Refusing costs one re-roll taken near the well. Passing costs the player's factory.
            if (Tested == 0)
            {
                ++RefusedNoHandles;
                E.bRelocate = true;
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("WELLH2-ROLL core='%s': *** RE-ENROLMENT REFUSED -- NOTHING TO ASK *** 0 of %d ")
                    TEXT("member handle(s) for this relocated group resolved in SpawnedWellCores/")
                    TEXT("SpawnedWellSatellites, so NO occupancy question was answered for it. That is ")
                    TEXT("NOT a clearance: bPinned=%d is the SAVED value and the roll-time pin resolved ")
                    TEXT("against %s. The dealt destination %s is DISCARDED and the well keeps its ")
                    TEXT("placement at %s. Visit the well once this session, then re-roll."),
                    *WellShort(E.CorePath), 1 + E.Satellites.Num(), E.bPinned ? 1 : 0,
                    TEXT("actors that may not be resident"), *P.Dest.ToCompactString(),
                    *E.PlacedCoreLocation.ToCompactString());
                continue;
            }
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH2-ROLL core='%s': re-enrolment occupancy gate PASSED -- %d of %d member ")
                TEXT("handle(s) were live and none reported in use, so the teardown may run."),
                *WellShort(E.CorePath), Tested, 1 + E.Satellites.Num());
```

Declare `int32 RefusedNoHandles = 0;` beside `AbortedInUse` at `:630` and add it to the summary at
`:847` (both a spec and an arg — the count is currently 29/29, it becomes 30/30).

**Trade-off:** a relocated well is only re-rollable while its destination is resident this session. That
is a real usability cost and it should be in the tooltip (B4 covers it). It is also exactly the rule the
apply pass already lives by (`RelocateApply.cpp:441`), so it is not a new concept for the player.

**Stricter variant (B1′), if you prefer symmetry over minimum diff:** require
`Tested == 1 + E.Satellites.Num()` rather than `Tested > 0`. Catches the partially-resident group as
well. I recommend B1 first — B1′ can be a one-word change once RT-11 tells you how often partial
residency actually happens.

### B2 (fixes F-E) — return the real teardown counts
Add out-params to `DespawnWellGroup` (`bool DespawnWellGroup(FNodeShuffleWellEntry&, const TCHAR*, int32*
OutDestroyed = nullptr, int32* OutAlreadyGone = nullptr)`) — default-null keeps the escalation-ladder
call site untouched — and print the summed `OutDestroyed` in the RT-6 line beside the expected total.
**Not in this path set** (`NodeShuffleWellDespawn.cpp`). If it cannot land now, the minimum honest fix
is inside this file: relabel the number as *"%d member records expected in those groups (this is the
layout's count, NOT a count of Destroy() calls — DespawnWellGroup does not return one)"*.

### B3 (fixes F-G) — hoist the satellite-growth warning above the toggle gate
Move the `if (E.Satellites.Num() != E.CapturedSatelliteCount) { … }` block from inside the
`bAlreadyPlaced && !bRerollRelocated` branch to just after `const bool bAlreadyPlaced = E.bGroupPlaced;`,
guarded by `if (bAlreadyPlaced && E.Satellites.Num() != E.CapturedSatelliteCount)`. Drop the trailing
"Turning ON 'Re-roll Wells…'" sentence when the toggle is already on.

### B4 (fixes F-C) — tooltip replacement, verbatim
Replace the `WHAT TO EXPECT ON AN EXISTING SAVE` paragraph with:

> WHAT TO EXPECT ON AN EXISTING SAVE: every well that has already moved is re-considered on the FIRST
> re-roll after you turn this on - not a few of them. The save this was developed against held 17 moved
> wells out of 20.
>
> A RE-ROLLED WELL DISAPPEARS UNTIL YOU GO AND FIND IT. The old well is removed the moment you press
> Re-roll Layout, but the new one is not built until you travel to its new spot and the terrain loads -
> exactly like a well moving for the first time. Turn this on with many moved wells and most of your
> wells will be gone from the map until you visit each new location. The log line 'WELLH2-ROLL:
> re-enrolment teardown' reports how many groups were removed and how long that frame took.

and replace the `never left with nowhere to be` bullet with:

> - Each well gets a limited number of draws to find a destination that clears water and spacing (the
>   log's 'deal-failed' count says how often a well ran out). A well that runs out keeps the spot it
>   already has: nothing is removed for it that roll.

and add a new bullet under LIMITS YOU CAN HIT (paired with B1):

> - A moved well is only re-considered while the place it currently stands has been loaded this session.
>   If it has not, the log says '0 of N member handle(s)' and the well keeps its spot.

Keep the `WHAT STILL DOES NOT MOVE` paragraph as written **once B1 lands** — with B1 it is true; without
B1 it is the false claim.

### B5 (fixes F-D) — put the `!E.bManaged` polarity change inside the toggle
If A4 is to be inert when OFF, the `!E.bManaged` branch must not change what `5ed3ac2` shipped:

```cpp
        if (!E.bManaged)
        {
            ++RefusedPinned;
            // ns-t7b-r2 F-D: NoteRefusal writes bRelocate = bGroupPlaced, which flips a state T16
            // shipped at 5ed3ac2 (a placed pinned well got false). This branch sits ABOVE the A4 gate,
            // so applying it unconditionally makes a default-OFF toggle change behaviour. Gated.
            if (bRerollRelocated) { NoteRefusal(E); } else { E.bRelocate = false; }
            ...
```
**Trade-off:** it keeps two polarities alive for one field, which is the drift shape this packet keeps
being bitten by. The alternative — accept the flip and call F8 a deliberate fix that applies always — is
defensible *if you re-audit `bRelocate`'s readers properly* and say so in the handoff. **My
recommendation is the gate**, because "default OFF changes nothing" is the property the author's 17-well
save is relying on and it should be literally true.

### B6 (fixes F-B and F-F) — restore the pre-capture clear at the seeding site
```cpp
    TArray<FVector> WellDestinations;
    TMap<int32, int32> SiteIndexByEntry;
    TSet<int32> PendingEntry;
    for (const FNodeShuffleWellPendingCapture& P : Pending) { PendingEntry.Add(P.EntryIndex); }
    for (int32 i = 0; i < WellLayout.Num(); ++i)
    {
        const FNodeShuffleWellEntry& E = WellLayout[i];
        if (E.bGroupPlaced) { SiteIndexByEntry.Add(i, WellDestinations.Add(E.PlacedCoreLocation)); }
        // ns-t7b-r2 F-B: a DEALT-BUT-UNPLACED site has NO actors on it, and pre-T7b the capture
        // cleared bDestDealt BEFORE this list was built -- so a candidate's stale destination was
        // never a blocking site for anybody. A3 moved the clear into phase 3, which silently made it
        // one. Excluded here so the toggle-OFF deal really is the pre-T7b deal.
        else if (E.bDestDealt && !PendingEntry.Contains(i))
        {
            SiteIndexByEntry.Add(i, WellDestinations.Add(E.DestCoreLocation));
        }
    }
```
and in phase 3's `!P.bDealt` branch, for the never-placed case only:
```cpp
            else
            {
                // F-F: HEAD cleared these in the capture; A3 must clear them here or a dead
                // destination survives into the next roll's spacing list.
                E.bDestDealt = false;
                E.DestCoreLocation = FVector::ZeroVector;
                UE_LOG(... "left vanilla this roll. The next roll draws again." ...);
            }
```

### B7 — the alternatives NOT taken, named so the fallback exists
- **Persist the spawned-handle presence** (make `SpawnedWellCores` keys `SaveGame`, or add
  `bSpawnHandlesResolvedThisSession`). Would let the pin distinguish "not built on" from "cannot tell"
  across sessions. **Rejected:** a new serialization surface on a save that already has A3-5 in flight,
  for a problem B1 solves by refusing.
- **Gate the whole feature on destination residency at roll time** —
  `if (P.bWasPlaced && !IsLocationNearAnyPlayer(E.PlacedCoreLocation, SpawnRadiusCm)) { refuse; }`.
  Strictly stronger than B1 and reuses the apply's own rule, but it refuses wells whose handles *are*
  resolvable and would make the feature feel broken on a big base. **B1 is the better first cut**; keep
  this as the fallback if RT-11 shows B1 still lets something through.
- **Ship as-is with the toggle OFF and call ON "experimental".** **Rejected:** the author's stated intent
  is to turn it ON on a 17-well save, F-B bites with it OFF anyway, and "experimental" has never yet
  stopped this workspace from shipping a false tooltip.

### Recommendation
**B1 + B6 + B4 are the ship gate.** B1 closes the blocker, B6 makes A4's default-OFF promise true, B4
stops a false claim reaching the player. **B5 and B3 next** (both one-block edits, both restore a
property the packet claims). **B2 after** — it needs a file this packet does not own, and its value is
diagnostic. F-H and F-I can ride to the next packet with dated TODOs.

Note for whoever applies these: **B1, B5 and B6 are specified verbatim above.** Per the workspace rule,
applying a reviewer's verbatim spec is a lower risk class and does **not** owe a further full re-review.
Anything you write differently from the text above does, and it should go to a reviewer who is not me
and not the author of round 2.

---

## 4. Runtime test checklist

Every row graded **assumed** or **unverifiable statically**, plus a reproduction for the blocker.
**TEST save only** — F-A destroys a build. Run after B1/B6/B4 land.

1. **RT-11 (NEW, the blocker — must FAIL before B1, PASS after).** Relocate a well, fly to it, build a
   Resource Well Pressurizer on the core and one Extractor on a satellite. **Do not re-roll.** Save,
   quit, relaunch, load, and — without going anywhere near that well — press Re-roll Layout with
   `RerollRelocatedWells` ON.
   *PASS (post-B1):* `*** RE-ENROLMENT REFUSED -- NOTHING TO ASK *** 0 of N member handle(s)`, no
   `RE-ENROLLED (MOVED AGAIN)` for that core, the well is still at the same coordinate and producing.
   *FAIL (expected today):* `gate PASSED -- 0 of N member handle(s)` followed by
   `RE-ENROLLED (MOVED AGAIN) … claimWithdrawn=1`, no `WELLH2-DESPAWN` line at all, and the pressurizer
   still standing on an actor no entry names.
2. **RT-12 (NEW, F-B/F-F — determinism with the toggle OFF).** On a save with at least one well in
   `bDestDealt=1, bGroupPlaced=0`, re-roll with the toggle OFF at a fixed seed on the HEAD build and on
   this build; diff every `DEALT … -> ⟨coord⟩` line and the `deal-failed` count. *PASS:* identical.
   *FAIL (expected today):* different coordinates and/or a higher `deal-failed`.
3. **RT-0 — the toggle is really default OFF and really inert.** Fresh install, do not touch the option,
   re-roll. *PASS:* `rerollRelocatedWells=0 -- N of M well(s) … ALREADY relocated`,
   `kept-as-placed without re-enrolment` == N, no `RE-ENROLLED (MOVED AGAIN)` anywhere.
4. **RT-1 — build on a relocated well, re-roll in the SAME session (handles resolvable).**
   *PASS:* `NOT re-enrolled -- bManaged=0, bPinned=1`, or the second layer
   `*** RE-ENROLMENT ABORTED -- A MEMBER IS IN USE ***`. **If the second line appears with `bManaged=1`,
   report it — that is a T16 defect, not a T7b one.** *FAIL:* `*** ABANDONED IN PLACE ***` then a fresh
   `WELLH2-PLACED` elsewhere.
5. **RT-2 — force a deal failure (row 8).** Set `WellRedealTries` to 1 in a scratch build, or re-roll
   until `deal-failed` is non-zero for a well whose `CAPTURED as a RE-ENROLMENT` line named it.
   *PASS:* `It KEEPS the relocated placement it already has at ⟨coord⟩`, **no** `WELLH2-DESPAWN` and
   **no** `WELLH2-ABANDON` for that core in the same roll; the well is still standing afterwards.
6. **RT-3 — the occupancy predicate through closed-source `AFGResourceNodeBase` (rows 3, 16).** On the
   built-on well, confirm the refusal names `pressurizer-on-core` / `extractor-on-satellite`, **not**
   `IsOccupied`. Do it **twice**: right after building, and after a full save → quit → load.
   `mIsOccupied` is non-SaveGame; if only `IsOccupied` fires after a reload, the gate is one reload from
   failing open.
7. **RT-4 — re-capture off suppressed originals (row 9).** Re-enrol a previously placed, **unbuilt**
   well. The `RE-ENROLLED (MOVED AGAIN)` line's *rigid body captured about the core at ⟨coord⟩* must
   equal that well's **original vanilla** coordinate (cross-check the earlier
   `WELLH2-PLACED core=… vanilla ⟨coord⟩ ->` line), and `minIntraGroup` must be ≥ 1800 cm — not 0, not
   the destination. This is the load-bearing engine assumption and it has **never** been verified.
8. **RT-5 — no satellite at world origin (row 6).** After two consecutive re-rolls with wells moving,
   `NodeShuffle.DumpWells`: zero fracking satellites within 300 cm of (0,0,0).
9. **RT-6 — mass-churn cost (row 18), and read it against RT-11.** On the 17-well save, first re-roll
   with the toggle ON: record N groups, M "member Destroy() calls", X ms. **If M is large and X is near
   zero, F-A fired** — cross-check that a `WELLH2-DESPAWN` line exists for each of the N cores.
10. **RT-7 — counter honesty.** No `*** COUNTER PARTITION BROKEN ***` on any re-roll where a placed well
    is refused.
11. **RT-8 — `bRelocate` on a placed-but-refused entry (row 2, F-D).** Re-roll from far away so a placed
    well is refused as not-streamed. The well must still be spawned, linked and producing on the next
    visit, and the status line's `WellsRelocateFlagged` must match the placed-well count.
12. **RT-13 (NEW, row 17) — the post-teardown abort path.** Believed unreachable; instrument rather than
    force. Grep a full session for `*** RE-ENROLMENT ABORTED AFTER THE TEARDOWN ***`. If it ever appears,
    stop and inspect that well's group in-world before saving — nothing in this packet claims it recovers.
13. **RT-9 — a re-enrolled well can come back a different size.** Re-enrol a well whose satellite list
    grew; the `RE-ENROLLED` line must report the new count and the spawned group must have that many.
14. **RT-10 — a re-enrolled well is a normal well.** Walk to the new coordinate: the group dresses, a
    Pressurizer snaps and produces, and the audit does not print `*** GROUP SCATTERED ***`.

---

## Verdict

**DO NOT SHIP.**

A3 is correct and closes round 1's blocker — but the safety property both the code comments and the
player-facing tooltip rest on ("a well you have built on is not moved") is defeated whenever the
relocated group's spawned handles are not resident, because T16's pin and the new commit-time gate read
the *same* session-scoped maps and both report a vacuous "not in use" as a clearance; and the tooltip
additionally tells the player the wells are rebuilt in the same frame when the spawn is in fact deferred
until they travel there.

**If it ships anyway** (it should not), the runtime checklist above is the minimum, RT-11 first.

**Post-fix review scope.** B1, B5 and B6 are specified verbatim here and, applied verbatim, are a
lower risk class that does not owe another full pass — but B2, B3 and any deviation from the text above
do, and that review must come from someone who authored neither round 2 nor the fixes.
