# Cold review — T1 copy fix (relocated-well tooltip / header comment / log NOTE)

**Reviewed:** uncommitted working tree, `wip/h2-relocation`, base `731246d`, repo `C:\Claude\Projects\NodeShuffle`.
**Files in scope:** `Source/NodeShuffle/Private/NodeShuffleConfig.cpp`, `Source/NodeShuffle/Public/NodeShuffleConfig.h`,
`Source/NodeShuffle/Private/NodeShuffleWellSpawn.cpp`, `docs/TECH-DEBT.md`.
**Out of scope by instruction:** `NodeShuffleWellVisuals.cpp`, `NodeShuffleWellVisualsApply.cpp` (concurrent owner).
**Reviewer did not build, commit, or edit any file under review.**

**Verdict up front: SHIP WITH TESTS**, gated on three text edits (F1, F2, F3) that need no rebuild
decision. The new copy is strictly more truthful than what it replaces; it is two sentences short of
correct. Runtime checklist in §4.

---

## 0. The one fatal-if-wrong check, answered first

**`UE_LOG` format-string arity — PASS.** `NodeShuffleWellSpawn.cpp:392-397`.

| # | specifier | argument | type |
|---|---|---|---|
| 1 | `%s` | `*WellShort(E.CorePath)` | `FString` → `TCHAR*` |
| 2 | `%s` | `*WellShort(ResourceClass->GetPathName())` | `FString` → `TCHAR*` |
| 3 | `%.1f` | `E.GroupYawDeg` | `float` (`NodeShuffleSubsystem.h:368`), varargs-promoted to `double` |
| 4 | `%s` | `*E.VanillaCoreLocation.ToCompactString()` | `FString` → `TCHAR*` |
| 5 | `%s` | `*E.PlacedCoreLocation.ToCompactString()` | `FString` → `TCHAR*` |
| 6 | `%s` | `bComplete ? TEXT(...) : TEXT(...)` | `TCHAR*` |

Six specifiers, six arguments, order matches, no specifier lived in the removed text. The literal
concatenation still joins cleanly (`"...(group %s this "` + `"pass)."` → `"...(group %s this pass)."`).
Temporaries survive to the end of the full expression, as before. **This change cannot crash there.**

---

## 1. Findings — ranked

### F1 — HIGH. The KNOWN LIMITS list omits the one limit that makes a well the player already found disappear, and the same tooltip instructs them to trigger it.
`NodeShuffleConfig.cpp:171` (the `KNOWN LIMITS` paragraph, and the closing
*"applies at ROLL time — turn both on, then use 'Re-roll Layout'"*).

There is **no un-hide path for a suppressed well, ever.** `RestoreOriginalsForReroll`
(`NodeShuffleSubsystem.cpp:459-510`) iterates only `OriginalNodeRecord`, and
`SuppressVanillaWellGroup` deliberately never writes to it. What suppression does to a well member
(`NodeShuffleWellRelocateApply.cpp`, the `HideOne` lambda, ~`:287-330`): `SetActorEnableCollision(false)`,
`SetActorHiddenInGame(true)`, `HideWellMemberMeshes(...)`, `RemoveResourceNodeScan_Local()`,
`DeregisterNodeFromManager(...)`. Grepping `Private/`, the only `SetActorHiddenInGame(false)` /
`SetVisibility(true` call sites are the ordinary-node restore and our own spawned actors.

**Failure scenario.** Both toggles ON, player re-rolls, travels, well W relocates — origin hidden,
collision off, off the scanner and the node manager. Player re-rolls again (this tooltip's own
instruction, and the README's route for applying a new seed). `RollWellRelocation` reaches
`DespawnWellGroup(E, TEXT("re-enrolled by a new roll"))` (`NodeShuffleWellRelocateRoll.cpp:341`), which
destroys the relocated copy. **The origin stays hidden.** W now exists nowhere the player can see,
scan, or build on — at either location — until they travel to the *new* destination (which may defer
indefinitely; 14 groups sat deferred in the 2026-08-08 run) or **restart the game**.

The project already knows this and has already accepted it: the H2b test script's step 10 first half
is documented as *"EXPECTED TO FAIL (F-3, accepted limitation: a re-rolled-away origin stays invisible
until restart)"*, and `_team/nodeshuffle-followups/H2b-review.md` F-3 is the MAJOR finding that
recommended option **B1** (per-group hidden-piece ledger, un-hidden in the re-roll restore, capturing
the pre-hide `ECollisionEnabled` rather than assuming `QueryOnly`) and accepted **B3** (document it)
*for the test build only*.

The copy names the **cosmetic** unknown (desert meshes) and withholds this one. That is the T1 defect
class surviving the T1 fix: the player is not told the thing that costs them something.

**Required edit:** one sentence in KNOWN LIMITS, e.g. *"Re-rolling after a well has already moved
leaves it missing until you travel to its new spot or restart the game — restarting always brings it
back."*

---

### F2 — HIGH. *"produce normally"* is contradicted by an explicit branch in the spawn path.
`NodeShuffleConfig.cpp:171` vs `NodeShuffleWellSpawn.cpp:283-295`.

```cpp
if (S.OriginalPurity != RP_MAX) { Sat->mPurityOverride = S.OriginalPurity; ++PurityWritten; }
else                            { ++PurityUnknown; }
```

When `OriginalPurity == RP_MAX`, **no purity is written and the relocated satellite inherits the CDO
purity.** The code's own comment on that branch calls the outcome *"the balance change H1 refused to
make"*. `OriginalPurity` is written at exactly one site — `NodeShuffleWellRoll.cpp:187`, from a **live**
satellite at roll time — so any satellite not resolvable at roll time keeps `RP_MAX`. The author counts
the case in the summary line (`:372`, *"purity reproduced on %d, unknown on %d"*), which is what a
reachable branch looks like, not a defensive one.

**Failure scenario.** A well whose satellites were not all live at the roll that enrolled them
relocates; the satellites spawn at CDO purity; the well's output rate no longer matches the well the
player mapped. The tooltip has told them it produces *normally*. The single in-game measurement
(2026-08-08, water well, pressurizer confirmed producing) attests **that it produces**, not that the
rate matches vanilla — production and rate parity are different claims.

**Required edit:** drop the word *normally*, or qualify it. *"…snap to it and produce"* is fully
supported by the measurement; *"produce normally"* is not.

---

### F3 — MEDIUM. "EXPERIMENTAL" now contradicts another toggle six lines below it in the same settings panel.
`NodeShuffleConfig.cpp:170` — `"Relocate Resource Wells (EXPERIMENTAL)"`
vs `NodeShuffleConfig.cpp:185-187` — `"Enable Experimental Features"` /
`"THIS VERSION HAS NO EXPERIMENTAL FEATURES, so this option currently does nothing — leave it off."`
(and `:150`, `"Enable Diagnostic Logging (Experimental)"`, a third usage).

**Failure scenario.** Player enables *Relocate Resource Wells (EXPERIMENTAL)*, nothing visibly happens
(they did not also enable *Shuffle Resource Wells*, or did not re-roll), reads two rows down that this
version has **no** experimental features and that the experimental toggle does nothing, and concludes
either that the mod's settings contradict themselves or that some other switch gates the feature. This
is precisely the T1 defect — settings copy that tells a player something false about the mod's own
state — reintroduced by the fix for T1.

**This is what the orchestrator's grep could not find**: the conflict is created by the *new* word, not
left behind by the old one. No stale-claim regex reaches it.

**Required edit (cheapest, no behaviour change):** amend the `EnableExperimentalFeatures` tooltip to
stop asserting that none exist, e.g. *"Reserved for in-development features that need their own opt-in.
Nothing in this version reads it — experimental features that exist today carry their own toggle."*
**Do not** instead re-gate the well toggle behind `EnableExperimentalFeatures`: that is a behaviour
change smuggled into a copy packet, and this project's precedent is that structural review-response
fixes introduce a fresh bug nearly every time.

---

### F4 — MEDIUM. *"its rocks and cracks are re-created at the new site"* is the least-evidenced sentence in the new copy and carries the most confidence.
`NodeShuffleConfig.cpp:171`; header claim at `NodeShuffleConfig.h:~122`.

The cited measurement — `TrySnapToActor -> 1`, `hitComp='NodeShuffleWellMesh_0'`, `compFound=1`,
`bForceAccept=0`, pressurizer producing water — establishes **BUILDABLE**. It establishes that a
`NodeShuffleWellMesh_*` component existed with build-gun collision. It does **not** establish that the
piece renders as a rock or a crack to a player's eye. The project's own `docs/TECH-DEBT.md:251` (T5)
records that the `MT_Crack` capture counter is *structurally blind* (`CrackPieces` only increments
inside `Cast<AFGNodeMeshActor>(C->GetOwner())`), so the log cannot corroborate cracks either.
`H2c-review.md` F-4b records that `DressWellActor` early-returns on `Visuals.Num() == 0`, and those
members are counted by `ApplyWellGroupVisuals` as *"have NO visual at all"* — a reachable
buildable-but-bare state the copy has no hedge for.

Visibility *is* forced in code (`NodeShuffleWellVisualsApply.cpp:353` `SetVisibility(true, true)`,
`:357` un-hide the actor), which is why this is graded **assumed** rather than *unverifiable* — but
rendering is an engine-side outcome and this workspace's rule caps that at assumed with a measurement
step. One screenshot closes it (checklist step 4).

**Recommended edit (not required):** keep *BUILDABLE* (measured); either soften *DRESSED* or run
checklist step 4 first and then keep it as written. Do not carry the word into the cookbook until a
screenshot exists.

---

### F5 — MEDIUM-LOW. T3 is the only unverified limit that can break something the player already owns, and it is the one the copy omits.
`docs/TECH-DEBT.md:189-196`. `EnsureWellMemberSnapBox` reaches 900 cm; `EnsureNodeUseBox` gives ordinary
nodes 650 cm; H0 measured the nearest non-same-well node at 1400 cm. 900 + 650 = 1550 > 1400 — the boxes
provably intersect in the population H0 measured. **Only relocated well members get the 900 cm box**, so
enabling this toggle is what creates the overlap.

**Failure scenario.** A well relocates within ~15 m of an ordinary iron node. The player can no longer
place a Miner Mk1 on that iron node — the trace resolves to the well member's box. Nothing in the copy
warns them, and the README's stated remedy (*"leave `RelocateResourceWells` off"*) does **not** shrink
the box on an already-relocated well in the same session (`RollWellRelocation` returns early when the
toggle is off and explicitly leaves placed groups exactly where they are,
`NodeShuffleWellRelocateRoll.cpp:58-74`).

The disclosure asymmetry is the finding: the copy names the **cosmetic** unknown (T2, desert meshes)
and withholds the **functional** one (T3). If the standard is *"name the limits a player can actually
hit"*, T3 qualifies more strongly than T2 does.

**Recommended edit:** half a sentence in KNOWN LIMITS, e.g. *"A moved well can land close to an ordinary
node; if a miner will not place on a node right next to one, that is why."*

---

### F6 — LOW. `docs/TECH-DEBT.md`'s new T1 entry makes an enumeration claim that is false, in the file whose own preamble forbids exactly this.
`docs/TECH-DEBT.md:80-82` — *"names the two limits a player can actually hit (…D1…, …T2…)"*.

There are at least **four**: D1 (duplicate), T2 (desert), **T3** (snap-box, `:189`), and **F-3**
(re-roll invisibility until restart). And F-3 **is not an entry in this file at all** — the file bills
itself as the living list (the state file says 14 items), the H2b review graded F-3 MAJOR, and the H2b
test script still carries it as a documented expected failure.

The preamble at `:4-5` says: *"If you find yourself rediscovering an item on this list, the entry has
failed — fix the entry, not just the bug."* An entry that says "the copy now names the limits" when it
names half of them is how the next reader stops looking.

**Required edits:** (a) *"two of the limits"* or enumerate all four; (b) add F-3 as a numbered P1 entry
with its pre-scoped fix already sized (H2b-review B1: a per-group hidden-piece ledger un-hidden in
`RestoreOriginalsForReroll`, capturing the pre-hide `ECollisionEnabled` rather than assuming
`QueryOnly`). Everything else in the T1 entry is accurate — the three-sites correction and the
"grep the claim, not the list" lesson are both correct and are the reusable part.

---

### F7 — LOW. Deleting the log NOTE was right, and it took an instruction with it.
`NodeShuffleWellSpawn.cpp:394-395` (removed text).

The deleted text was **two** things: a design assertion (*"stage H2 ships NO VISUAL for a relocated
well (design §2.4)"*) and a tester instruction (*"fly to that coordinate to test it"*).

**Deleting the assertion is correct and is what the standing rule requires.** A `Display`-level line
that reaches every player's `FactoryGame.log` should not carry a design-document claim that a later
commit can falsify — and this one had already been falsified. Re-wording it would have re-created the
defect in a new tense. The deletion serves [[lessons-log-asserted-a-cause]] exactly.

**Deleting the instruction is a small, non-obvious loss.** The two comments immediately above the line
(`:378-382`) exist *because* the line's whole purpose is "fly to the logged coordinate" — that rationale
is now implicit in the comments while absent from the output. It is affordance loss, not information
loss: the coordinate is still printed.

**Recommendation: leave it deleted.** If anything goes back, add a *measured* fact only — e.g. the
dressed-piece count for the group, which `WELLH2B-APPLY` already computes — never prose.

---

### F8 — STYLE NOTE (no failure scenario). README's settings table never documented either well toggle.
`README.md:62-78` lists every config option except `ShuffleResourceWells`, `RelocateResourceWells` and
`ShowCompatibilityNotices`. Pre-existing, **not** introduced here. It becomes more visible now: the fix
promotes the toggle from *"INCOMPLETE, leave it off"* to *"EXPERIMENTAL, here is what it does"*, which
raises the chance a player goes looking in the README — where the feature is absent from the table
while a *"Known behaviour"* section further down references `RelocateResourceWells` by name.
`CHANGELOG.md` also has no well-relocation entry at all.

### F9 — STYLE NOTE. The rewritten header comment keeps a citation a repo reader cannot resolve.
`NodeShuffleConfig.h:~119` cites *"design §2.4"*, which lives in
`_team/nodeshuffle-followups/PacketH-design.md` — an untracked working-scratch tree outside this
(public) repo. Pre-existing; the rewrite carried it forward. **Checked:** §2.4's text (*"a relocated
well is invisible unless dressed"*) is conditional and is **not** stale, so there is nothing to sweep
there.

---

## 2. Sweep result (brief item 4) — what the grep missed

**No remaining site** in `Source/`, `README.md`, `docs/`, `CHANGELOG.md` or `DESIGN.md` still tells a
player or a reader that a relocated well is invisible / has no visual / is incomplete. Surviving hits
for `invisible` / `INCOMPLETE` / `NO VISUAL` are all about different subjects and are all correct:
hidden ordinary-node ghosts, an INCOMPLETE well **spawn** (a live runtime state, not a stage), the KDF
rollback message, quartz placeholders, and review-tag `F3` identifiers.

**What the grep could not have found — the two problems the NEW words create:**
1. **F3** — "EXPERIMENTAL" vs *"THIS VERSION HAS NO EXPERIMENTAL FEATURES"* in the same panel.
2. **F6** — `TECH-DEBT.md`'s new false enumeration ("the two limits").

**One more site, outside the repo and outside the brief's scope but worth the orchestrator's attention:**
`_team/NodeShuffle-state.md` still carries the stale claim as an **open action item** at lines ~120-122
(*"NEXT ACTIONS … 2. T1: the settings tooltip still says relocated wells are 'functional but
INVISIBLE'"*) and again at ~195-197 (*"TWO PLAYER-FACING STALE COMMENTS, unfixed"*). Harmless as
history — but the state file is the live handoff bus and is read as current, so the next session will
re-dispatch a fixed item. Recommend striking those two before the next handoff.

---

## 3. Differential / parity — what the OLD copy guaranteed vs what the NEW copy provides

The change replaces an established player-facing claim set. Table is over the claims, not the code
(one code line changed; its parity is §0).

| Invariant the OLD copy guaranteed | How the NEW copy provides it | Grade |
|---|---|---|
| The toggle defaults OFF | `AddBool(TEXT("RelocateResourceWells"), false, …)` unchanged; text still says "OFF by default" | **provably provided** |
| A blanket "do not use this for normal play" warning | Softened to *"leave it off if you want a quiet save"* — a deliberate polarity change, and the correct one now that the feature works. But the specific-warning list that replaces the blanket one is a **subset** (F1, F5) | **assumed — incomplete** |
| Requires `Shuffle Resource Wells` to also be ON | Retained verbatim | **provably provided** |
| Applies at ROLL time; turn both on then Re-roll | Retained verbatim | **provably provided** |
| All-or-nothing: if the footprint will not fit, the well stays put | Retained verbatim; unchanged claim, not re-verified this round | **assumed** |
| Rigid-body move, satellites keep spacing/pattern, group yaw-searched | Retained verbatim | **assumed** (unchanged) |
| A tester can find the well via the `WELLH2-PLACED` log line | Dropped from player copy. Correct if visuals work; a player filing a bug report no longer has the grep token | **assumed** (moot if F4 passes) |
| The player is told the site will be visually empty (sets the right expectation for a test) | Correctly inverted: the site is now claimed DRESSED. Evidence supports BUILDABLE; DRESSED rides on it (F4) | **assumed** → checklist 4, 5 |
| — *(new claim)* Pressurizer + Extractors snap and produce normally | Snap + production measured once (`bForceAccept=0`, water). "normally" (rate parity) is contradicted by the `PurityUnknown` branch (F2) | **assumed — overclaimed** → checklist 6, 7 |
| — *(new claim)* Desert wells may arrive undressed | Matches T2 exactly. Honest hedge, correctly included | **provably provided** (as a statement of a known unknown) |
| — *(new claim)* Building mid-move can yield two wells | Matches D1 and the README section it points to | **provably provided** |
| The log line prints a usable coordinate for the group | Coordinate still printed; only the NOTE was removed; arity verified | **provably provided** |
| The log line does not assert a design fact | Now satisfied — the assertion was the deleted text (F7) | **provably provided** |

**Nothing in this change is graded "provably provided" on the strength of engine-side behaviour.**
Rendering, snap resolution, production rate and tooltip layout all live in FactoryGame/UE and are
capped at *assumed* with a measurement step, per the workspace rule.

---

## 4. Runtime test checklist

Executable without reading the rest of this document. Steps 1-3 need no in-game travel.

1. **Tooltip renders whole.** Mod Settings → NodeShuffle → hover/expand *Relocate Resource Wells
   (EXPERIMENTAL)*. **PASS:** all four paragraphs readable, nothing clipped, and every dash renders as
   `—` (not `â€"`). *Why now:* the string grew 846 → 1121 bytes in this change, and the file is UTF-8
   **without** a BOM. **FAIL:** truncation or mojibake.
2. **Panel self-consistency.** In the same panel, read *Enable Experimental Features* (*"THIS VERSION
   HAS NO EXPERIMENTAL FEATURES"*). Record whether both are visible at once. Evidence for **F3**.
3. **Log line still formats.** Fresh save, both toggles ON, re-roll, travel to a destination.
   `grep WELLH2-PLACED FactoryGame.log`. **PASS:** one line per group ending `(group COMPLETE this
   pass).` with a plausible yaw and two distinct coordinates. **FAIL:** garbled text, a stray `%s`, or
   a crash at that call. *This is the only way a text-only change could be fatal.*
4. **Dressed — with eyes.** At the relocated well, screenshot the core and at least one satellite.
   **PASS:** cracked-ground graphic on the core, visible mesh on each satellite. **FAIL:** buildable but
   bare ground. Grades **F4**.
5. **No dark members.** `grep WELLH2B-APPLY` for `member(s) have NO visual at all`. **PASS:** zero.
   Non-zero names exactly the members the word "DRESSED" does not cover.
6. **Purity reproduced.** Same log: `grep "purity reproduced on"`. **PASS:** `unknown on 0`. Non-zero
   means *"produce normally"* is false for that well — record the count and the well. Grades **F2**.
7. **Output rate parity.** Run the pressurizer + extractors on the relocated well for 2 minutes and read
   the extractor's items/min against the vanilla purity mix recorded for that well at roll time.
   **PASS:** matches. Grades *"produce normally"* beyond *"it produces"*.
8. **T3 — the functional unknown.** Find a relocated well with an ordinary resource node within ~15 m
   (the 2026-08-08 test well was on a plateau with none, which is why this is still open). Aim a Miner
   Mk1 at that ordinary node from several angles. **PASS:** it snaps. **FAIL:** the hologram refuses or
   binds a well member — enabling this toggle broke an unrelated node. Grades **F5**.
9. **F-3 — the re-roll gap.** With step 3's well relocated and visible, set *Re-roll Layout* ON and let
   it roll. Return to that well's **original** (vanilla) location. **EXPECTED FAIL, known:** invisible,
   un-snappable, no scanner ping. Then **quit to desktop**, reload, and check the same spot. **PASS:**
   the origin well is back. Record both halves — this is the limit the tooltip omits (**F1**).
   *Use quit-to-desktop, never the main-menu reload — that path crashes this build
   (`sf-ontravelfinished-load-crash`).*
10. **T2 — desert.** Relocate a desert-biome well and check for a crack graphic. No graphic **and** no
    `spatial REJECT` naming it = the gap is real and the tooltip's desert hedge stays.

---

## 5. Alternatives — is "EXPERIMENTAL" the right label?

**My reasoning, not agreement.** `"INCOMPLETE - stage H2"` was a claim about the **mod's development
stage**, and it was false. `"EXPERIMENTAL"` is a claim about **confidence**, and confidence here is
genuinely low: one well, one biome, one session, on a plateau with no neighbouring nodes, with two named
unknowns of which one (T3) can break an unrelated node the player already owns. So the *register* is
right — EXPERIMENTAL is neither the old lie nor a "works" claim. **The label is not the defect; the
KNOWN LIMITS list under it being a subset is.**

| # | approach | trade-offs |
|---|---|---|
| **A** | Ship as written | Zero further work. Ships an overclaim (F2) and omits a well-vanishing limit (F1) into a public repo. **Reject.** |
| **B** | **Keep "EXPERIMENTAL"; complete KNOWN LIMITS with T3 + F-3; drop "normally"; fix the `EnableExperimentalFeatures` tooltip** | ~4 sentences of copy, no behaviour change, no new risk surface. Keeps the honest register and makes the disclosure complete. **RECOMMENDED.** |
| **C** | Revert to a hard warning label until T2/T3 close, e.g. *"(EXPERIMENTAL — two known gaps)"* | Most conservative. But it re-introduces the T1 shape in miniature: a label that under-describes a feature the author has measured working. Take this **only if step 8 FAILS** — an *observed* T3 should go back to a hard warning until fixed. |
| **D** | Drop the parenthetical; put status in the tooltip only | Cleanest label, but the settings list is scanned and tooltips are not — the one word is the only warning most players read. **Reject.** |
| **E** | Gate the toggle behind `EnableExperimentalFeatures` (which currently gates nothing) | Structurally resolves F3 and gives that dead toggle a purpose. **Reject for this packet:** it is a behaviour change inside a copy packet, it hides a working feature behind a second switch, and this project's precedent is that structural review-response fixes introduce a fresh bug nearly every time. Worth its own packet later. |

**Recommendation: B**, with **C** held as the pre-agreed fallback if checklist step 8 fails. On the
TECH-DEBT side, F-3 should become a numbered P1 entry with H2b-review's **B1** already sized — that is
the difference between an accepted limitation and a forgotten one.

---

## 6. Verdict

**SHIP WITH TESTS.** The new copy is strictly better than the copy it replaces — it no longer tells
players a working feature is broken, the log line no longer asserts a design fact, and the format
string is provably safe — but it ships one overclaim (*"produce normally"*, contradicted by the spawn
path's own `PurityUnknown` branch) and omits the two limits that can cost a player something (F-3
re-roll invisibility, T3 snap-box), while a new label contradicts another toggle in the same panel.
Those are four text edits, none of which needs a rebuild decision.

**Gating before the build that carries this copy:** F1, F2, F3 (and F6 in `docs/TECH-DEBT.md`).
**These edits are themselves new, self-authored copy and owe their own scoped pass** — they are text,
so that pass is cheap, but it is not optional.

**Runtime checklist, one line each:**
1. Tooltip renders whole, no clipping, no mojibake.
2. Record the EXPERIMENTAL-vs-"no experimental features" contradiction as seen in the panel.
3. `WELLH2-PLACED` formats cleanly, ends `(group COMPLETE this pass).`
4. Screenshot a relocated core + satellite — dressed, not bare ground.
5. `WELLH2B-APPLY` reports zero members with no visual at all.
6. `purity reproduced on N, unknown on 0`.
7. Relocated well's items/min matches its recorded vanilla purity mix.
8. A Miner Mk1 still snaps to an ordinary node within ~15 m of a relocated well member.
9. Re-roll, revisit the old well site (expected: still invisible), then quit-to-desktop and reload
   (expected: restored) — record both halves.
10. Relocate a desert well; check for a crack graphic and for a `spatial REJECT` naming it.
