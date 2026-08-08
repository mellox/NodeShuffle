# Cold RE-review — the four self-authored edits made to close the T1 copy review

**Reviewed:** uncommitted working tree, `wip/h2-relocation`, base `731246d`, repo `C:\Claude\Projects\NodeShuffle`.
**Scope:** the four review-response edits only — `Source/NodeShuffle/Private/NodeShuffleConfig.cpp` (two tooltips),
`docs/TECH-DEBT.md` (T1 rewrite + new T15), `C:\Claude\Projects\_team\NodeShuffle-state.md`.
**Out of scope by instruction and NOT reviewed:** `NodeShuffleWellVisuals.cpp` / `NodeShuffleWellVisualsApply.cpp`
(concurrent owner, dirty as expected); the `UE_LOG` arity in `NodeShuffleWellSpawn.cpp:392-397`; the stale-claim sweep.
**Reviewer did not build, commit, or edit any file under review.**

**VERDICT UP FRONT: DO NOT SHIP.** One of the two newly-added bullets is a **false statement about the mod's
behaviour**, and it is false in the direction the brief predicted: the fix authored to close "you omitted a real
limit" invented a limit that does not exist. It is player-facing, in the settings UI, and it has already been
promoted into a P1 TECH-DEBT entry (T15) as though it were established.

---

## 0. The answer to the brief's headline question, first

> *"Are the two newly-added bullets ACCURATE, or did I over-correct into a new false claim?"*

**Bullet 2 (re-roll ⇒ vanish): FALSE.** Over-corrected. See F1.
**Bullet 3 (build-area ⇒ likely cause): a cause you have no measurement for, and it collides with the
panel's own top-level Description.** See F2.
**The `•` encoding risk: settled by measurement, not luck** — `/utf-8` is in the actual compiler response
file. See F5. The remaining risk is font glyph coverage, not the toolchain.

---

## 1. Findings — ranked

### F1 — BLOCKER. The re-roll bullet describes a state the code cannot produce. An already-relocated well is *deliberately* preserved across re-rolls.

**Sites:** `Source/NodeShuffle/Private/NodeShuffleConfig.cpp:171`, KNOWN LIMITS bullet 2 —

> *"RE-ROLLING the layout makes an ALREADY-relocated well vanish from the world — from both the old site and the new one — until you restart the game. Restarting brings it back."*

and its source claim, `docs/TECH-DEBT.md` T15 headline (*"Re-rolling makes an already-relocated well vanish
from the world until the game restarts"*).

**The invariant that refutes it: a suppressed well origin always has a live, maintained relocated group.**
Established statically, from code read this pass:

1. **Suppression happens only while `bGroupPlaced` is true.** `SuppressVanillaWellGroup` has exactly two call
   sites: `NodeShuffleWellRelocateApply.cpp:473` (immediately after `E.bGroupPlaced = true` at `:469`) and
   `:505` (inside the `already placed → maintain it every pass` branch).
2. **`bGroupPlaced` is written `false` at exactly two sites** (`grep "bGroupPlaced *="`):
   `NodeShuffleWellRelocateRoll.cpp:352` and `NodeShuffleWellEscalate.cpp:235`.
3. **`:352` is unreachable for a placed group.** `RollWellRelocation` guards it at
   `NodeShuffleWellRelocateRoll.cpp:152-185`:
   ```cpp
   if (E.bGroupPlaced) { ++AlreadyCaptured; E.bRelocate = true; /* …warning only… */ continue; }
   ```
   with the intent written out at `:147-151`: *"A group already placed keeps its placement across re-rolls…
   the GEOGRAPHY does not churn, because a player who has walked to a relocated well should not find it gone
   after a re-roll they ran for other reasons."*
4. **`:235` is unreachable for a placed group.** `EscalateWellPlacement`'s only callers are `TryPlaceWellGroup`
   (`NodeShuffleWellRelocateApply.cpp:106`, `:252`) and `NoteWellVoidDefer` (`NodeShuffleWellEscalate.cpp:70`,
   itself called only from `TryPlaceWellGroup` at `:91` / `:226`). `TryPlaceWellGroup` has **one** call site —
   `NodeShuffleWellRelocateApply.cpp:458`, inside `if (!E.bGroupPlaced)`.
5. **`DespawnWellGroup` has exactly two call sites** (`NodeShuffleWellEscalate.cpp:170`,
   `NodeShuffleWellRelocateRoll.cpp:341`), both downstream of the same `!bGroupPlaced` gates.
6. **The orphan sweep skips placed groups** — `NodeShuffleWellSweep.cpp:198` (`if (E && E->bGroupPlaced) { continue; }`)
   and `:213`.
7. **The re-roll's merge preserves the flag.** `NodeShuffleWellRoll.cpp:110` — `TArray<FNodeShuffleWellEntry>
   Merged = WellLayout;` — a copy of the saved array, never a rebuild; entries are added, never removed
   (`:155-161`, *"MERGE, NEVER REPLACE"*). `WellLayout = MoveTemp(Merged)` at `:389`.

So the bullet's premise — that a re-roll un-places a relocated well — has no code path. **A re-roll leaves a
relocated well exactly where it is, still spawned, still linked, still suppressed at the origin, still
producing.**

**Failure scenario (of the copy).** Player has both toggles on and a relocated water well they have piped.
They re-roll for an unrelated reason (a new node seed). The tooltip has told them their well is now gone from
both locations until they restart. It is not: it is where it was, producing. Three ways this costs them:
(a) they restart for nothing; (b) they read the bullet as a reason not to re-roll at all, so the copy
suppresses the mod's headline feature on a false premise; (c) they see the well still standing, conclude the
settings text is unreliable, and discount the other three bullets — **including bullet 1, which is true and is
the one that can actually cost them a duplicate well.**

**Where the false claim came from, because it matters.** `_team/nodeshuffle-followups/H2b-review.md` F-3 is an
argument from **absence**: *"Grepping the whole `Private/` tree, the only `SetActorHiddenInGame(false)` /
`SetVisibility(true` call sites are the ordinary-node restore and our own spawned actors — nothing un-hides a
well mesh piece, ever."* That part is **true and remains true**. What was never established is the second
premise — that a re-roll produces an un-relocated-but-still-hidden well. The absence of a restore path is not
evidence that a removal occurs. T15's own closing paragraph names this exact error class (*"this project has
been burned by 'I could not find a check' being quoted forward as 'there is no check'"*) — and then commits it
in its own headline. The H2b test script's step 10 inherits the same broken premise: it instructs the tester to
*"fly to a well origin that was suppressed under the previous roll and is now a plain vanilla well again"* — a
state items 1-7 above say cannot exist.

**There is no recorded observation of the vanish.** `_team/test-logs/` holds no NodeShuffle run; step 10's
"EXPECTED TO FAIL" is a prediction derived from F-3, never a result.

---

### F1b — the TRUE limit hiding underneath F1, which the bullet should have been about

While disproving the whole-well vanish I found a **real, reachable, functional vanish at satellite granularity**,
and it is triggered by exactly the action the tooltip names:

* `RollWellLayout`'s merge **appends** satellite records to an already-placed entry when a satellite streams in
  for the first time (`NodeShuffleWellRoll.cpp:176-177`; the entry-level warning is
  `NodeShuffleWellRelocateRoll.cpp:174-183`). Those arrive `bCaptured == false`, with zero offsets.
* `SpawnWellGroup` **refuses to spawn them** — `NodeShuffleWellSpawn.cpp:201-205` — correctly, because a zero
  offset would put a live node at world origin.
* `SuppressVanillaWellGroup` **hides every satellite in `E.Satellites` with no `bCaptured` filter** —
  `NodeShuffleWellRelocateApply.cpp:332-335` → `HideOne`.
* Nothing un-hides it (H2b F-3's true half).

⇒ **That satellite is hidden at the origin, never spawned at the destination, and has no restore path.** It
exists nowhere until the world is reloaded. `NodeShuffleWellSpawn.cpp:200` states the outcome in a comment
(*"Refused here, and still suppressed at the vanilla site"*), and the mod already logs it at `Warning`. The
well is permanently short of satellites for that session — a *rate* loss, i.e. functional.

This is almost certainly the kernel of the folklore. It is worth a bullet; the whole-well vanish is not.

---

### F2 — HIGH. The build-area bullet asserts a cause with no measurement, and it competes with the panel's own top-level Description on the same symptom, with a different remedy.

**Sites:** `NodeShuffleConfig.cpp:171` bullet 3 (*"If a Miner refuses to place on a nearby ordinary node, this
is the likely cause"*) vs **`NodeShuffleConfig.cpp:31`**, the mod Description shown at the top of the very same
panel:

> *"If a new extractor won't build on a shuffled node, restart the game once — compatibility patches are written during play and read at startup."*

**Direct answer to the brief.** Yes, this is an assertion of cause you have no measurement for, and yes,
player-facing copy is bound by a version of the same rule — for a *different* reason than a log line is. A log
line's danger is that the next agent quotes the reason forward as fact; a tooltip's danger is that the player
**acts on the wrong remedy**. `docs/TECH-DEBT.md` T3 says so itself in the same diff: *"nothing measures it…
there is no `WELLH2B-SNAPBOX` line… it can only be closed by stumbling onto the geometry."* An entry that says
"nothing measures this" cannot support "this is the likely cause" two files away. And "likely" is doing real
work here: T3 has **never been observed**, while the compatibility case is the mod's documented common one.

**Failure scenario.** A Miner will not place on a shuffled ordinary node 40 m from the nearest well — the
allow-list/compat case the Description covers. The player reads the well tooltip, turns `RelocateResourceWells`
off, does not restart, and the miner still will not place. Worse, turning the toggle off does **not** shrink the
snap box on an already-relocated well in that session (`NodeShuffleWellRelocateRoll.cpp:58-74` returns early and
leaves placed groups untouched), so even if T3 *were* the cause, the action the bullet implies is not a remedy.

The first sentence of the bullet ("has not been tested against… closer than about 15 m") is accurate and should
stay. The second sentence is the defect.

---

### F3 — MEDIUM. Edit 2 removed one contradiction and introduced a dependency on a UI ordering the mod does not control.

`NodeShuffleConfig.cpp:182` — *"A feature marked EXPERIMENTAL **above** carries its own toggle…"*

`Root->SectionProperties` is a `TMap<FString, TObjectPtr<UConfigProperty>>`
(`SatisfactoryModLoader\Mods\SML\Source\SML\Public\Configuration\Properties\ConfigPropertySection.h:16`), and
render order belongs to SML's `BP_ConfigPropertySection` widget, not to this file. Insertion order is not a
container guarantee, and if the widget sorts by key, `EnableExperimentalFeatures` sorts **before**
`RelocateResourceWells` — i.e. the word "above" points the player upward at nothing. The tooltip this replaced
made no positional claim; the fix introduced one.

**Failure scenario.** Player reads "marked EXPERIMENTAL above", scrolls up, finds nothing marked EXPERIMENTAL
(it is below them), and concludes the tooltip is describing a feature that does not exist in their build —
which is the *original* T1 defect shape.

Two smaller notes on the same string:
* *"another option's name"* (singular) — there are **two** options carrying the word: `Relocate Resource Wells
  (EXPERIMENTAL)` at `:170` and `Enable Diagnostic Logging (Experimental)` at `:150`. No contradiction (both do
  carry their own toggle), but say *"other options"*.
* **Verified accurate:** *"Nothing in this version is gated by this switch"* is TRUE today — nothing reads
  `EnableExperimentalFeatures`; the only hits are two `TODO(pre-release)` comments planning to move
  auto-allow-extractors behind it (`NodeShuffleAutoAllowExtractors.cpp:100`, `:111`, `:169`;
  `NodeShuffle.h:251`). **It goes false the moment either TODO lands** — per the workspace's stopgap rule this
  sentence needs a dated TODO beside it, or the first person to land that gate ships a lie.

---

### F4 — MEDIUM. `docs/TECH-DEBT.md`'s rewritten T1 makes an enumeration claim that is wrong again — in the entry whose entire lesson is that its enumeration was wrong.

`docs/TECH-DEBT.md` T1 — *"names the **four** limits a player can actually hit"*. Given F1, one of the four
(T15) is not a limit; given F2, another (T3) is stated as a diagnosis rather than an untested case. Two of four
do not survive.

Also in the same entry: *"the `EnableExperimentalFeatures` tooltip **four entries below** it"*. It is **two**
(`:169` `RelocateResourceWells` → `:174` `ShowCompatibilityNotices` → `:180` `EnableExperimentalFeatures`) — and
per F3 even that count is not a UI fact.

The entry's genuinely reusable halves — the three-sites correction, and *"grep the claim, not the list"* — are
correct and should survive the rewrite. The new *"a grep for the OLD claim cannot find a contradiction the NEW
words create"* paragraph is also correct and is the best line in the diff; F2 and F3 are two more instances of it.

---

### F5 — RESOLVED BY MEASUREMENT. The `•` (U+2022) is safe through the compiler. What is not established is the font.

The file is UTF-8 **without BOM** (verified: first bytes `#inclu`), valid UTF-8, containing exactly two
non-ASCII code points: `—` U+2014 ×11 and `•` U+2022 ×4.

**Measured, not reasoned:** `/utf-8` is present in the actual UBT-generated compiler response file for the
translation unit that contains this source —
`C:\Claude\Projects\SatisfactoryModLoader\Mods\NodeShuffle\Intermediate\Build\Win64\x64\FactoryGameEGS\Shipping\NodeShuffle\Module.NodeShuffle.cpp.obj.rsp`
— and in the `FactoryGameSteam` and `FactoryServer` equivalents. The unity file
`Module.NodeShuffle.cpp` `#include`s `…/Private/NodeShuffleConfig.cpp` directly, so that rsp is this file's rsp.
With `/utf-8`, MSVC reads the source as UTF-8 and a `TEXT()` wide literal receives U+2022 correctly. **So the
em-dash surviving is not luck — the toolchain is configured for it, and the bullet inherits the same guarantee.**

Two honest caveats:
* The rsp on disk is from **2026-07-21/25**; the flag comes from UBT's toolchain config, not from anything this
  diff touches, but the next build regenerates it. Grade: **measured on the last build's artifacts** — one-line
  re-check below.
* **The font is a separate question and is unverifiable statically.** U+2014 rendering in Satisfactory's UI font
  is not evidence for U+2022. A missing glyph shows as a box or blank; the text after it still reads, so this is
  cosmetic, not information loss. Cheapest de-risking is to not need the glyph at all (see Alternatives).

---

### F6 — LOW. Single `\n` before each bullet is a new usage in this file.

Every prior tooltip uses only paragraph breaks (`\n\n`). The four bullets are separated by single `\n`. If SML's
tooltip widget collapses single newlines, all four render as one run-on paragraph joined by `• `. Not fatal,
but it is the difference between a scannable list and a wall — and the list structure is the whole point of the
restructure. Covered by checklist step 1.

---

### F7 — LOW, but act on it. The previous review's own F1 and its checklist step 9 inherit F1's false premise.

`docs/reviews/2026-08-08-t1-copy-coldreview.md` F1 and runtime step 9 (*"Re-roll… return to the well's original
location. **EXPECTED FAIL, known:** invisible, un-snappable, no scanner ping"*). Under F1's invariant the origin
**stays** suppressed and the relocated well **stays** live — which is correct behaviour. A tester running step 9
as written will record the correct outcome as an unexpected PASS, or worse, hunt for a bug that is a feature.
**Step 9 must be rewritten before anyone runs the script** (replacement in §4).

---

### F8 — STYLE NOTE (no failure scenario). `_team\NodeShuffle-state.md` edits are accurate.

Both stale NEXT-ACTIONS sites the previous review flagged (~line 30 and ~line 125) are struck through and dated,
and neither now reads as an open item. This edit is clean. **It will need one more pass after F1 lands**, since
it currently records "F-3 now has a debt entry at last — T15" as a completed win.

---

## 2. Differential / parity — the claim set the fix replaced

The four edits replace an established, already-reviewed claim set (the first-draft KNOWN LIMITS). Table is over
the claims; no executable path changed in these four edits.

| Invariant the PREVIOUS (first-draft) copy guaranteed | How the NEW copy provides it | Grade |
|---|---|---|
| Toggle defaults OFF; "leave it off" | Unchanged (`AddBool(..., false, ...)`, text retained) | **provably provided** |
| Requires `Shuffle Resource Wells`; applies at ROLL time | Retained verbatim | **provably provided** |
| Rigid-body move, all-or-nothing, yaw-searched | Retained verbatim, word for word | **provably provided** (unchanged claim, not re-verified this round) |
| D1 duplicate-if-you-build-mid-move, pointing at the README | Bullet 1. README section exists and matches (`README.md:130-150`, *"We will never hide or delete a well you have built on"*) | **provably provided** |
| Desert wells may arrive undressed | Bullet 4; matches T2 | **provably provided** (as a statement of a known unknown) |
| *(previously)* "produce **normally**" — the overclaim F2 of the last round flagged | "…snap to it and **produce**" — the qualifier is gone | **provably provided** — the required edit landed correctly |
| *(new)* Re-roll makes a relocated well vanish from both sites | **No code path produces this state.** Refuted from `bGroupPlaced`'s two writers and `SuppressVanillaWellGroup`'s two callers | **FALSE — not provided** |
| *(new)* "Restarting brings it back" | A promise about save/streaming behaviour. Even if the premise were true, hidden-actor state persisting or not is engine/save-internal and may never be graded above assumed. **No measurement exists.** | **unverifiable statically** (moot if the bullet is deleted) |
| *(new)* A Miner refusing on a nearby node is *likely* the snap box | T3 is proven **geometrically** and never observed; T3's own entry says nothing measures it | **assumed — and it contradicts `NodeShuffleConfig.cpp:31`** |
| *(new)* `EnableExperimentalFeatures` gates nothing in this version | Grep-verified: no reader, two `TODO(pre-release)` comments only | **provably provided today; expires when either TODO lands** |
| *(new)* The EXPERIMENTAL-marked feature is "above" | Order belongs to a `TMap` + an SML BP widget | **unverifiable statically** |
| Non-ASCII in a `TEXT()` literal survives compilation | `/utf-8` present in the module's actual `.obj.rsp`, all three Win64 targets | **measured** (on the last build's artifacts) |
| Non-ASCII renders in the settings widget | Engine/UMG font path | **unverifiable statically** |
| The list structure is readable | Single `\n` separators, new in this file | **assumed** |

**Nothing here is graded "provably provided" on the strength of engine-side behaviour.** Rendering, font
coverage, save persistence of hidden state, and panel ordering are all capped at assumed/unverifiable with a
measurement step, per the workspace rule.

---

## 3. Alternatives — including the wording I would ship

### 3a. Bullet 2 (the re-roll bullet)

| # | approach | trade-offs |
|---|---|---|
| **A** | Ship as written | Ships a false statement about the mod's behaviour into the settings UI. **Reject.** |
| **B** | **Delete bullet 2. Replace it with the TRUE re-roll fact + the satellite limit (F1b).** | Two accurate sentences, both provable from code read this pass. Removes the false claim and *adds* disclosure rather than reducing it. **RECOMMENDED.** |
| **C** | Delete bullet 2, add nothing | Safest, one edit, no research. But it drops the only functional limit in the vicinity that is real (F1b), which is what the last round asked for. Take this if you want the smallest possible diff before the commit. |
| **D** | Keep a hedged version ("re-rolling *may* leave a well missing…") | **Reject.** Hedging a claim you have disproved is worse than either extreme: it survives greps, reads as knowledge, and it is now known-false rather than merely unmeasured. |
| **E** | Build the restore path (H2b-review B1: per-group hidden-piece ledger) so the gap cannot bite | The right long-term answer for F1b, and T15 should keep its pre-scoped sizing. **Not this packet** — a behaviour change in a copy packet is the precedent this project keeps getting burned by. |

**Wording I would ship for B** (replaces bullet 2 entirely):

> • Re-rolling does NOT move a well that has already moved — it keeps its new spot. Only wells that have not moved yet are dealt a new destination.
> • A well moves with the satellites that had loaded when it was enrolled. If more of its satellites load later, they are left out of the moved well permanently — the well is smaller, and produces less, until you reload the save.

Both are provable: sentence 1 from `NodeShuffleWellRelocateRoll.cpp:147-185`; sentence 2 from
`NodeShuffleWellRoll.cpp:176-177` + `NodeShuffleWellSpawn.cpp:201-205` +
`NodeShuffleWellRelocateApply.cpp:332-335`. "until you reload the save" is the one part still **assumed** —
checklist step 4.

### 3b. Bullet 3 (the build-area bullet)

| # | approach | trade-offs |
|---|---|---|
| **A** | Ship as written | Asserts an unmeasured cause and displaces the Description's working remedy. **Reject.** |
| **B** | **Keep sentence 1, replace sentence 2 with a report request** | One-line edit, keeps the honest disclosure, asserts nothing. **RECOMMENDED.** |
| **C** | Delete the bullet | Loses a genuine untested case that the last round specifically asked to have named. **Reject.** |
| **D** | Land T3's pre-scoped `WELLH2B-SNAPBOX` instrumentation first, then say what it measures | Correct order of operations and already pre-scoped in `docs/TECH-DEBT.md` T3. But it is a code change, and this is a copy packet. **Do it next**, not now. |

**Wording I would ship for B:**

> • A relocated well claims a large build area, and that has not been tested against ordinary resource nodes closer than about 15 m. If a Miner will not place on an ordinary node right beside a relocated well, please report it — that case is untested. (For a Miner that will not place anywhere near a well, see the note at the top of this panel.)

### 3c. Edit 2 (`EnableExperimentalFeatures`)

Replace *"marked EXPERIMENTAL **above**"* with *"marked EXPERIMENTAL **elsewhere in this list**"*, and
*"another option's name"* with *"other options' names"*. Add a dated TODO at the tooltip:
`// TODO(2026-08-08, pre-release): this tooltip says nothing is gated by this flag. Both TODO(pre-release)
sites in NodeShuffleAutoAllowExtractors.cpp plan to gate on it — update this string in the same commit.`

### 3d. The `•` glyph

| # | approach | trade-offs |
|---|---|---|
| **A** | Keep `•` | Compiler-safe (measured). Font coverage unverified. Nicest looking. |
| **B** | **Use `- ` (ASCII hyphen)** | Zero encoding surface, zero font risk, identical scannability in a plain-text tooltip. **RECOMMENDED** unless checklist step 1 has already been run and passed. |
| **C** | Keep `•` and run step 1 before the commit | Fine too — the check is 60 seconds and you must run step 1 anyway. Prefer this over B **if** you are launching the game regardless. |

### 3e. TECH-DEBT

* **T15 must be rewritten, not deleted.** Its true content is F1b: *an uncaptured satellite appended to an
  already-placed entry is hidden at the origin, refused at the destination, and has no un-hide path.* Keep the
  pre-scoped fix (H2b-review B1) — it is the right fix for the real gap. Retitle; drop the re-roll/whole-well
  framing; keep the closing "verify the absence before building either" hedge, which is correct and which the
  headline violated.
* **T1** should say *"names the limits a player can actually hit"* with the list enumerated inline rather than
  counted, or state a count only after F1/F2 land. Fix "four entries below" → "elsewhere in the same panel".
* Add a line to T1's lesson block: *"The second draft asserted a limitation that does not exist. Over-correcting
  a disclosure gap is the same defect class as the gap — verify a new limit against the code before writing it,
  the same way you verify a removed claim."*

---

## 4. Runtime test checklist

Executable without reading the rest of this document. **Steps 1-2 need no in-game travel.** Steps 3-5 replace
the previous review's step 9, which is invalid (F7).

1. **Tooltip renders whole, and the bullets are on their own lines.** Mods → Node Shuffle → hover *Relocate
   Resource Wells (EXPERIMENTAL)*. **PASS:** every paragraph readable, each `•` on its own line, every `•` and
   `—` a real glyph (not a box, not `â€"`). **FAIL:** mojibake (→ the `/utf-8` measurement in F5 no longer
   holds, report immediately), missing glyph (→ take Alternative 3d/B), or all four bullets run together
   (→ F6, use `\n\n`).
2. **Panel self-consistency, read top to bottom.** Confirm *Enable Experimental Features* appears **below**
   *Relocate Resource Wells* in the rendered panel. **FAIL:** it appears above → F3's "above"/"elsewhere" edit
   is required, not optional.
3. **F1 — the disproof, in game.** Fresh save, both toggles ON, re-roll, travel until at least one well
   relocates (`grep WELLH2-PLACED`). Then toggle *Re-roll Layout* again and let it roll. **Return to the
   relocated well's NEW site. EXPECTED PASS: the well is still there, still dressed, still snappable.** Also
   `grep "WELLH2-DESPAWN.*re-enrolled by a new roll"` — **EXPECTED: no line naming that core.** This is the step
   that confirms F1 and retires the previous review's step 9.
4. **F1b — the satellite gap, and whether a reload restores it.** In the same save, `grep "satellite list has
   grown"`. If a line appears: count the well's satellites in world, compare to `CapturedSatelliteCount` in the
   line, then **quit to desktop**, reload, and re-count at the ORIGIN. **PASS:** the missing satellites are
   visible at the original site after reload. **FAIL:** still invisible after a reload → something is persisting
   to the save, which is a much worse bug — report immediately. *Quit-to-desktop, never main-menu reload — that
   path crashes this build (`sf-ontravelfinished-load-crash`).* If no line appears, record **"not exercisable"**,
   never PASS.
5. **F2 — the snap box, still open.** Find a relocated well with an ordinary resource node within ~15 m. Aim a
   Miner Mk1 at that ordinary node from several angles. **PASS:** it snaps. **FAIL:** enabling this toggle broke
   an unrelated node → T3 is observed, and the tooltip should go to a hard warning. Still gated on luck until
   T3's pre-scoped `WELLH2B-SNAPBOX` instrumentation lands.
6. **`/utf-8` still present on the build that ships this.** After the next build, one line:
   `Select-String "/utf-8" "C:\Claude\Projects\SatisfactoryModLoader\Mods\NodeShuffle\Intermediate\Build\Win64\x64\FactoryGameEGS\Shipping\NodeShuffle\Module.NodeShuffle.cpp.obj.rsp"`.
   **PASS:** one hit. Re-grades F5 from "measured on the previous build" to "measured on this build".
7. **Carried forward, unchanged, from the previous review (still owed):** dressed-with-eyes screenshot of a
   relocated core + satellite; `WELLH2B-APPLY` reports zero members with `NO visual at all`;
   `purity reproduced on N, unknown on 0`; relocated well's items/min vs its recorded vanilla purity mix;
   desert-biome relocation shows a crack graphic.

---

## 5. Verdict

**DO NOT SHIP.**

The one local edit that the previous review specified verbatim — dropping *"normally"* — landed correctly and
introduced nothing. Every **structural** edit in this round introduced a defect, and the largest one is the
predicted failure: closing *"you omitted a real limit"* produced a limit that does not exist, stated in the
settings UI as fact and promoted to a P1 debt entry. That matches this project's recorded precedent exactly
(three of three structural review-response fixes, each failing in the opposite direction from the bug it fixed).

**Gating before the commit — all text, no rebuild decision:**
1. **F1** — delete the re-roll/vanish bullet; replace with the two provable sentences in §3a/B.
2. **F2** — replace the "likely cause" sentence with §3b/B.
3. **F3** — "above" → "elsewhere in this list"; add the dated TODO.
4. **F4 / §3e** — rewrite T15 around F1b; de-count T1's enumeration; fix "four entries below".
5. **F7** — rewrite the previous review's checklist step 9 as steps 3-4 above, before anyone runs the script.

**These edits will themselves be new, self-authored copy and owe their own pass.** They are text, so that pass
is cheap — but the last two rounds both said this, and this round is why.

**Runtime checklist, one line each:**
1. Tooltip renders whole; every `•` and `—` a real glyph, each bullet on its own line.
2. *Enable Experimental Features* really is below *Relocate Resource Wells* in the rendered panel.
3. Re-roll twice: the relocated well is STILL at its new site, and no `WELLH2-DESPAWN … re-enrolled` line names it.
4. If `satellite list has grown` appears: count satellites, quit to desktop, reload, confirm the missing ones are back at the origin.
5. A Miner Mk1 still snaps to an ordinary node within ~15 m of a relocated well member.
6. `/utf-8` still present in `Module.NodeShuffle.cpp.obj.rsp` after the next build.
7. Previous round's still-owed steps: dressed screenshot, zero `NO visual at all`, `unknown on 0`, rate parity, desert crack graphic.
