# Cold re-review — packet `ns-t2-meshtype`, **review-response round 1**

- **Scope:** the code added SINCE `docs/reviews/2026-08-08-t2-meshtype-coldreview.md`, i.e. the packet's
  answers to F-1/F-2/F-3/F-5 and its new Alternative F census.
- **Subjects:**
  - `C:\Claude\Projects\NodeShuffle\Source\NodeShuffle\Private\NodeShuffleWellVisuals.cpp` (617 lines)
  - `C:\Claude\Projects\NodeShuffle\Source\NodeShuffle\Private\NodeShuffleSubsystem.cpp` (7 741 lines)
  - branch `wip/h2-relocation`, base `731246d`, uncommitted, **never compiled**.
- **Reviewer:** cold. Did not author the code, did not author the previous review, did not build,
  commit or edit anything but this file.
- **Out of scope, not findings:** `NodeShuffleConfig.cpp/.h`, `NodeShuffleWellSpawn.cpp` (concurrent
  copy-fix packet), `docs/TECH-DEBT.md`.
- **Settled, not re-litigated:** route-3 reachability of `AFGNodeMeshActor`; the type gate itself
  (`Cast != null && mNodeMeshType != MT_Node`); `mNodeMeshType` being public.

---

## VERDICT: **SHIP WITH TESTS** — no defect blocks a build, but three of the packet's own
## interpretive claims are over-stated and R-1's pass criterion is unsafe as written

The mechanical work is **clean**. I re-verified every format string independently and found **no
arity error**, no scope/lifetime problem, no new import candidate beyond what is already reached in
each TU, and the two counter/denominator pairs are genuine supersets that share the exact predicate
they measure — the F-3 fix is structurally correct and is **not** a T5 counter in disguise. That is a
better result than this workspace's prior for review-response fixes, and the packet's candour (§2's
"NOT attested — say so plainly") is the reason.

What is wrong is **interpretation, in the new census**. `MESHTYPE-CENSUS` measures a population it
does not name, over a coverage it does not print, and its comment claims a scope it does not have
("answers the question on ANY save, at load, with no travel" — it visits only *streamed-in* actors).
`pairedToOrdinaryNode: N MT_Node + 0 non-MT_Node` therefore reads as a PASS while the population that
could actually strand a node sits in the `unpaired` column, indistinguishable from legitimate well
crack geometry. **R-1 as currently written would record a PASS on evidence that does not cover the
failure mode.** That is the T5 defect one column over — the exact thing the census exists to avoid.
It is a ~6-line fix and the wording is in §5 below.

---

## 1. Findings — ranked

### FR-1 (HIGH, diagnostics-interpretation) — `MESHTYPE-CENSUS` has no **coverage** denominator, so its load-bearing zero is over-determined
`NodeShuffleSubsystem.cpp:7331-7357`

The census buckets every streamed `AFGNodeMeshActor` by `mNodeActor` into
{`pairedToOrdinaryNode`, `pairedToFracking`, `unpaired`}. Those **do** partition `Total` — I verified
that every valid actor takes exactly one branch, and `ENodeMeshType` has exactly seven enumerators
(`FGResourceNodeBase.h:37-46`), so `ByType[0..6]` is complete and the histogram sums to `Total`.
The partition is sound. **The problem is what the audited bucket covers.**

The load-bearing number is `pairedToOrdinaryNode … non-MT_Node`. An actor only reaches that bucket if
`mNodeActor` resolves. This function's own comment (`:7256-7259`) records that in this world the
engine back-link is *"unpopulated for ~98% of nodes (562 via-mesh-actor vs 27,773 no-mesh-actor)"*.
The forward sweep repairs it only where `Node->mMeshActor.Get()` resolves to an `AFGNodeMeshActor`.
**Nothing in the census prints what fraction of ordinary nodes ended up paired.**

**Concrete failure scenario.** Tester boots the save, greps `MESHTYPE-CENSUS`, reads:

```
meshActors=6200 | pairedToOrdinaryNode: 5000 MT_Node + 0 non-MT_Node | pairedToFracking: 0 MT_Node
+ 0 non-MT_Node | unpaired(mNodeActor unset): 1200, of which 210 non-MT_Node | histogram …
```

By the checklist this is a clean PASS: ordinary column is `0 non-MT_Node`, and it is not the
"inconclusive" case the log warns about (the ordinary columns are not both zero). But the 210
`unpaired` non-`MT_Node` actors are **exactly the population route 3's gate admits**, and the census
gives the reader **no way at all** to tell a well crack piece (never paired — the forward sweep
`continue`s on `IsFrackingActor`, `:7285`) from an ordinary node's mis-typed rock. The residual
exposure the census was built to bound is entirely inside the one bucket it cannot classify, and the
coverage number that would reveal this — how many of ~28 000 ordinary nodes are represented by those
5 000 pairings — is computed one loop above (`FromForwardLink`) but printed only at **`Verbose`**
(`:7307-7311`), where the tester will not see it.

**Same class as TECH-DEBT T5 verbatim:** *"A ratio read off lines whose numerator is structurally zero
is not evidence of anything."* Here the numerator is not structurally zero — the **denominator is
unprinted**, which is the same defect from the other side.

**Fix: §5 Fix A** (verbatim code). Adds `coverage: %d of %d streamed ordinary node(s) paired
(%d engine back-link, %d repaired by this pass)` and moves the two sweep counts to the `Display` line.

---

### FR-2 (HIGH, scope over-claim in a comment and a log line) — the census is a **streamed-in snapshot**, described as a world census
`NodeShuffleSubsystem.cpp:7320-7322` (comment) and `:7350` (`meshActors=%d`)

The comment states the histogram *"answers the question on ANY save, at load, with no well relocation
and no travel."* `TActorIterator<AFGNodeMeshActor>` visits only actors currently in the level's actor
list — i.e. streamed in. On a boot at the player's spawn, that is a small neighbourhood which in this
save may contain **no fracking well at all** and only local ordinary nodes.

**Concrete failure scenario.** Tester boots at a grassland base, sees
`meshActors=340 | pairedToOrdinaryNode: 300 MT_Node + 0 non-MT_Node | pairedToFracking: 0 + 0 |
unpaired: 40, of which 0 non-MT_Node`, and records R-1 PASS. Nothing near spawn is a well; the entire
question the census was built to answer was never in scope of the measurement, and the resulting
"census validates the T2 gate" is written into the test log as evidence. The one honesty guard in the
line — *"If pairedToOrdinaryNode is 0 in BOTH columns … inconclusive, not a pass"* — does **not** fire
here, because the ordinary column is healthily non-zero.

The dedup-on-the-measurement is what partially rescues this: as the player travels and `Total` moves,
each new distinct measurement prints, so a session produces a *series* rather than one line. That is
genuinely good design and I would keep it. But the log never tells the reader the number is a
snapshot, so a single grepped line reads as a world statement.

**Fix: §5 Fix A** (adds `(STREAMED-IN ONLY)` to the payload) **+ Fix A-text** (replaces the
over-claiming comment sentence and adds one sentence to the log).

---

### FR-3 (MEDIUM, test-plan defect that will be executed) — the handoff's §7 R-0 still carries the borrowed-constant pass criterion the packet says it removed
`C:\Claude\Projects\_team\nodeshuffle-followups\T2-meshtype-handoff.md:205-212`

Round 1 §6.3 states: *"The other half of F-4 … is NOT done: it lives in `docs/`, which is barred to
me."* **That is factually wrong about the location.** R-0 lives in §7 of the handoff itself — the file
the packet was actively appending to in the same round. §7 R-0 still reads:

> `grep "WELLH2B-INDEX pass"` → the counts must read **`(20 own, 17 via engine link, 18 spatial)`**
> and **`0 narrowed-by-type`**. … FAIL (spatial < 18, or narrowed-by-type > 0): the gate dropped pieces.

**Concrete failure scenario.** The tester opens the handoff (the natural front door — it is the
packet's own test plan), runs R-0 on the current tree, and gets `(20, 17, 17)` because one
`SM_FrackingNode_Crack_*` actor streamed a fraction later, or because the concurrent copy-fix packet
perturbed a config read. R-0 says **FAIL: the gate dropped pieces**. The tester then greps
`spatial NARROWED`, finds nothing (because nothing was narrowed), and now has an unexplained FAIL on
the packet's own primary criterion — or, worse, follows the pre-approved contingency and adopts
hybrid B against evidence that does not exist. Symmetrically: a real regression that happens to land
on `(20, 17, 18)` reads as PASS.

I verified the source of those constants myself: `FactoryGame-backup-2026.08.08-15.53.04.log` pass 28
does read `(20 own, 17 via engine link, 18 spatial)`, 1 bystander reject, 55 pieces. They are real —
they are just from a different binary, and R-0 asserts them as a *must*.

**Fix: §5 Fix E** (verbatim replacement for §7 R-0).

---

### FR-4 (MEDIUM, dedup) — the census key includes numbers that churn and excludes nothing that matters
`NodeShuffleSubsystem.cpp:7358` — `const FString CensusKey = Census + TEXT("|meshtypecensus");`

Answering the question asked directly: **no, it cannot suppress a changed census** — a different
measurement is a different string is a different key, so any change prints. The packet got the
direction that matters right.

Two secondary problems, both real:

1. **It re-prints on irrelevant churn.** `Total`, `Unpaired` and all seven histogram bins are in the
   key, and every one of them moves whenever any node mesh actor streams in or out. `RebuildMeshActorCache`
   runs once per `ApplyLayout` pass (`NodeShuffleSubsystem.cpp:2124`); the reference session ran **28
   passes in ~2 minutes**. A moving player therefore gets a near-per-pass `Display` line of ~900
   characters, and every distinct value is retained forever in `WellVisualCaptureLogged`
   (`grep -rn` over `Source/` confirms it is **never** `Reset()`), so the set grows unbounded for the
   world's lifetime with ~250-char strings.
2. **A measurement that returns to a previously-seen value is suppressed.** If an alarming census
   (`OrdinaryNonNode = 1`) appears, then that actor streams out and back in, only the first occurrence
   prints. Acceptable — once is enough for an alarm — but it means the census cannot answer *"is it
   still bad?"* on demand. That is what the console-command alternative in §3 is for.

**Fix: §5 Fix C** (key on the decision-relevant subset), and §3 Alternative F3 (a console command).

---

### FR-5 (MEDIUM, missing discriminator) — a non-zero `WIDENED` line cannot be classified good-vs-bad from its own fields
`NodeShuffleWellVisuals.cpp:352-371`

The line prints mesh, actor, location, distance to winning well member, radius, `nodeMeshType`,
nearest-ordinary-node distance, `bystanderWins`. It does **not** print the one field that settles the
question instantly: the owning `AFGNodeMeshActor`'s **own** `mNodeActor` back-link.

Three world states produce a WIDENED line and they need opposite responses:
- **(good)** a well member's own core/satellite rock on a separate `AFGNodeMeshActor` whose mesh is not
  `Frack`-named → back-link resolves to a **fracking** node → correct new capture, T2 working.
- **(good)** an unpaired desert well piece → back-link **unset** → the packet's whole hypothesis, R-3.
- **(BAD)** an ordinary node's mis-typed rock → back-link resolves to an **ordinary** node → F-1
  recurrence in progress, stop-ship.

Today all three print the same shape and the reader must go stand at the world location to tell them
apart. The line's closing claim — *"the measured cost of T2's gate in the UNSAFE direction"* — is a
mild over-statement for cases 1 and 2, though it is hedged by printing `bystanderWins`, so it is not
an asserted-cause violation.

**Concrete failure scenario.** R-3 (desert well) produces four WIDENED lines. Three are desert crack
pieces (case 2) and one is a mis-typed ordinary node's rock 900 cm from a satellite that A2 happened
to lose (`bystanderWins=0`). All four look alike; the tester records "4 widened, desert wells now
dressed, T2 closed" and the fourth rock is hidden and un-minable in the save.

`mNodeActor` is **public** on `AFGNodeMeshActor` (`FGResourceNodeBase.h:70`, inside the `public:`
block that starts at `:64`), so no access transformer is needed, and `IsFrackingActor` is a static
member already reachable. **Fix: §5 Fix B** (verbatim, ~6 lines, arity 9 → 11).

---

### FR-6 (LOW) — the census's `wellAuditPasses` stamp is off by one against the `WELLH2B-INDEX pass` line a reader will correlate it with
`NodeShuffleSubsystem.cpp:7368`

`RebuildMeshActorCache()` is called at `NodeShuffleSubsystem.cpp:2124`; `++WellAuditPasses` happens
inside `ApplyWellRelocation`, called at `:2202` — **later in the same `ApplyLayout`**. So the census
printed with `wellAuditPasses=27` was taken at the start of the pass that logs `WELLH2B-INDEX pass 28`.
The wording *"at the time of this count"* is technically honest, but a reader correlating a census to
an index line will pair the wrong two. One-word fix in §5 Fix A-text.

---

### FR-7 (LOW, style note — no failure scenario) — the census key is the only entry in `WellVisualCaptureLogged` with its tag as a **suffix**
`:7358`. Every other family is prefixed (`narrowed|`, `widened|`, `bystander|`, `…|adopt`, `…|grp`).
No collision is possible today, but any future prefix-match over the set silently misses the census.
Fix C changes it to a prefix as a side effect.

---

### FR-8 (STYLE NOTE) — 500-line rule: `NodeShuffleWellVisuals.cpp` 617 (117 over). Not asked for now; see §6.

---

## 2. What I verified and found CLEAN — do not re-spend budget here

### 2a. Every counter, its fire condition, and whether its zero is interpretable

I checked whether each numerator and its denominator share a code path — the question that decides
whether this is a T5 counter. **They do, and tightly**: both members of each pair are computed from
the *same two booleans* (`bTypeSaysWellPiece`, `bNameSaysWellPiece`) and the *same* `bInRadius`,
inside the same loop iteration, at `NodeShuffleWellVisuals.cpp:283-308`.

| counter | numerator set | denominator printed with it | numerator ⊆ denominator? | zero interpretable? |
|---|---|---|---|---|
| `NarrowedByType` `:317` | `{name ∧ ¬type ∧ inRadius}` | `NameCandidates` `:283`, `NameCandidatesInRadius` `:298` | **yes, provably** — `:313` is reached only when `bName` held (`:285` `continue`d otherwise) | **YES** |
| `WidenedByType` `:308` | `{type ∧ ¬name ∧ inRadius}` | `TypeCandidates` `:284`, `TypeCandidatesInRadius` `:299` | **yes, provably** — same conjunction minus `¬name` | **YES** |

**The attested denominators — verified by me against the named log, not taken from the handoff.**
`C:\Users\mello\AppData\Local\FactoryGame\Saved\Logs\FactoryGame-backup-2026.08.08-15.53.04.log`:

- **53 distinct** `(mesh, actor)` pairs on `WELLH2B-INDEX spatial REJECT` lines + **18 distinct** on
  `spatial:` accepts = **≥71 distinct `Frack`-named route-3 candidates**, all with `Best != nullptr`.
  The handoff claims `NameCandidates ≥ 48`. **Verified and conservative** — the true figure is higher.
  (41 of the 53 rejected are on `StaticMeshActor_<N>`; the rest are on `SM_FrackingNode_Crack_<N>`.)
- `NameCandidatesInRadius ≥ 19`: the 18 accepts each required `bInRadius`, plus the 1 bystander reject
  (also `Frack`-named and in radius). **Verified.**
- `TypeCandidatesInRadius`: **stronger than the handoff claims.** Every `BySpatial` accept passes
  `bTypeSaysWellPiece` (`:313` `continue`) and `bOwned == bInRadius` (`:395`), so
  **`TypeCandidatesInRadius >= BySpatial` is a structural identity**, and both numbers are printed
  **in the same log line**. The reader gets a free in-line consistency check; if
  `TypeCandidatesInRadius < spatial` the counter is broken. On this save that pins the denominator at
  ≥18 without importing anything. Worth stating in the handoff — it is the packet's best argument and
  it did not make it.

**Conclusion on the packet's central argument: it holds.** `0 widened of N type-matching (M in radius)`
with `M > 0` does mean *every in-radius type-match was also a name-match*, i.e. the gate admitted
nothing new. That **is** evidence, and `M ≥ spatial` makes `M > 0` structural on any save where route
3 accepts anything. `0 narrowed of 71 (19 in radius)` is likewise evidence.

**Fire conditions, stated concretely as required:**

| counter | concrete world state that makes it non-zero | reachable in **this** game? |
|---|---|---|
| `NameCandidates` / `…InRadius` | any `Frack`-named route-3 component / one within 1200 cm of a member | **YES — attested ≥71 / ≥19, verified by me** |
| `TypeCandidates` / `…InRadius` | owner casts + `!= MT_Node`, at any distance / in radius | **YES — structurally ≥ `BySpatial`, attested ≥18** |
| `NarrowedByType` | a `Frack`-named piece in radius whose owner does not cast, **or** whose type is `MT_Node`. Concretely: a decoration `SM_FrackingNode_Crack_*` on a plain `AStaticMeshActor` that a relocated well is moved to within 1200 cm of | **NOT attested. Plausible** — 41 `StaticMeshActor_<N>` `Frack`-named candidates exist in this save; today all are 90–800 m out. Reading is `0 of 19 in radius` = real evidence |
| `WidenedByType` | (a) a desert well piece not `Frack`-named; (b) an ordinary node's rock in radius typed non-`MT_Node`; (c) *(neither the packet nor the previous review named this one)* **a well member's own core/satellite rock on a separate mesh actor whose mesh is not `Frack`-named** — a *benign* firing | **NOT attested, and (a) is unfalsifiable until R-3.** If desert well meshes turn out to be `Frack`-named, (a) is structurally 0 forever and T2 closes with widened permanently 0. Reading is `0 of ≥18 in radius` = real evidence. See FR-5 for why a non-zero must be classifiable |
| census `pairedToOrdinaryNode … MT_Node` | any mesh actor back-linked to a non-fracking node | **YES, by construction** — the forward sweep `SetNodeActor`s ordinary nodes at `:7296` immediately above. `FromForwardLink > 0 ⇒ bucket > 0`. **Verified.** |
| census `pairedToOrdinaryNode … non-MT_Node` | an ordinary node's mesh actor typed `MT_Core`/`MT_Crack`/`MT_Satellite`/`MT_Desert*` | **The falsifier; reachability unknown by design.** But see FR-1: its zero is bounded by unprinted coverage, and see FR-9 below for a false-positive path |
| census `unpaired` / `unpairedNonNode` | `mNodeActor` unset at count time | **YES** — every well crack piece lands here (the forward sweep skips fracking), which is precisely why it cannot discriminate |

### 2b. FR-9 (noted here, not ranked as a defect) — the census reads state its own sweep wrote seconds earlier

`RebuildMeshActorCache` **writes** `MA->SetNodeActor(Node)` at `:7296` and the census then **reads**
`MA->mNodeActor` at `:7339`. The write persists in engine state across passes, so pass 2's back-link
sweep finds links pass 1 created: `FromBackLink` rises and `FromForwardLink` falls while
`MeshActorCache.Num()` is unchanged. This is the [[lessons-output-derived-input-is-a-loop]] shape.

**It does not invalidate the audited number** — `mNodeMeshType` is never written by us, and our writes
only *add* to the audited bucket, which cannot manufacture a false zero. But it **can** manufacture a
false **non-zero**: if a level-authored `mMeshActor` forward link on an ordinary node points at a
`MT_Crack` actor, *we* create that pairing and the census then reports it as an ordinary/non-`MT_Node`
falsification. It is still a real signal about level data, but the census cannot say whether CSS
authored the pairing or we did seconds earlier. Fix A prints `FromBackLink`/`FromForwardLink` at
`Display`, which lets the reader see how much of the pairing is ours. Symptom to watch:
back-link/forward-link split shifting between pass 1 and pass 2 with a constant cache size.

### 2c. Compile-as-a-compiler — independently re-verified, **no error found**

I re-counted every touched and adjacent format string **twice**: once by hand, once with a
brace-matching script (`…\scratchpad\fmt.py`; its arg counter is systematically +1 because the
leading comma after the format string counts as a separator — corrected below, and two lines with
`TEXT()` inside their argument list were hand-counted).

| site | specifiers | args | |
|---|---|---|---|
| `WellVisuals.cpp:322` NARROWED (untouched) | 8 | 8 | OK |
| `:358` **WIDENED (new)** | **9** | **9** | OK |
| `:383` BYSTANDER (untouched) | 7 | 7 | OK |
| `:401` `spatial:` (reworded) | 8 | 8 | OK |
| `:415` **REJECT (reworked)** | **8** | **8** | OK |
| `:428` **pass summary (reworked)** | **15** | **15** | OK |
| `:475` ADOPT / `:502` CAPTURE / `:562` CAPTURE (untouched) | 7 / 4 / 6 | 7 / 4 / 6 | OK |
| `Subsystem.cpp:7350` **census `Printf` (new)** | **14** | **14** | OK |
| `:7367` **census `UE_LOG` (new)** | **2** | **2** | OK |

The packet's own counts match mine exactly. Other compiler-facing checks:

- **`ENodeMeshType` / `AFGNodeMeshActor` complete in both TUs.** `WellVisuals.cpp:78` and
  `Subsystem.cpp:34` both include `Resources/FGResourceNodeBase.h`. `ENodeMeshType` is a first use in
  `Subsystem.cpp` — the include is present. **OK.**
- **Access.** `mNodeMeshType` (`FGResourceNodeBase.h:71`) and `mNodeActor` (`:70`) are both in the
  `public:` block starting at `:64` (`private:` starts at `:82`). **No `AccessTransformers` entry is
  needed by anything in this round**, including my Fix B.
- **`const` chain.** `const AFGResourceNodeBase* Paired = MA->mNodeActor.Get();` →
  `IsFrackingActor(const AActor*)` (static, `NodeShuffleSubsystem.h:842`) by qualification conversion +
  public upcast; `GetResourceNodeType()` is called on a `const` pointer inside `IsFrackingActor`
  already. **OK.**
- **`TSoftObjectPtr::Get()` null-safety.** `mNodeActor` is `TSoftObjectPtr<AFGResourceNodeBase>`, so
  `.Get()` returns `nullptr` for unloaded *and* for garbage objects. The `!Paired` branch handles both.
  No `IsValid(Paired)` needed; matches the existing back-link sweep at `:7271`. **OK.**
- **`int32 ByType[7]`** — 7 initializers, 7 enumerators (`MT_Node`…`MT_DesertSatellite`), bounds-checked.
  Histogram sums to `Total` exactly. `TypeIdx >= 0` on a signed `int32` raises no MSVC C4296. **OK.**
- **Lifetime / scope in the new blocks.** `MeshOwner`, `MeshType`, `bWidenedByType`, `Best`, `ByStSq`
  all live in the same `for (UStaticMeshComponent* C : Comps)` body; `bWidenedByType` is declared at
  `:307` *before* its use at `:352`. `Best->Path` at `:367` is guarded — `bWidenedByType ⇒ bInRadius ⇒
  Best != nullptr`. `FromBackLink`/`FromForwardLink` are still in scope at the census. **No dangling
  reference, no lambda captures added.** **OK.**
- **Counters are per-pass, log lines are per-session.** On pass 5 the summary may report
  `3 widened-by-type` while the three `WIDENED` lines were emitted thousands of lines earlier at pass 1.
  Both lines say *"Said once per mesh"*, so this is correct — but a tester must grep the **whole** file,
  not the tail. Folded into R-2 below.
- **Import surface — graded `assumed`, per house rule.** Nothing new is predicted:
  `TActorIterator<AFGNodeMeshActor>` and `AFGNodeMeshActor::StaticClass` are already reached at
  `Subsystem.cpp:7269`; `mNodeMeshType`/`mNodeActor` are plain field reads (compile-time offsets, no
  symbol); `FString::Printf` and `IsFrackingActor` are already reached. In `WellVisuals.cpp`,
  `Cast<AFGNodeMeshActor>` and `mNodeMeshType` were already reached at `:524-527` **before** this arc.
  **This may not be graded higher than `assumed` from reading headers** — settle by `dumpbin` /
  `check_imports.ps1` diff against the baseline (R-0).

### 2d. Alternative F — census placement and partition, checked as asked

- **Ordering: correct.** The census sits after *both* sweeps (`:7331` vs `:7269` back-link, `:7282`
  forward). Every repair the forward sweep made is visible to it. There is **no** mid-population
  snapshot. **provably provided.**
- **Partition: correct.** Each valid actor takes exactly one of `!Paired` / `IsFrackingActor` / else.
  No double-count, no drop. `Total` is incremented once per valid actor before any branch.
  **provably provided.**
- **De-dup cannot suppress a changed census.** Different measurement ⇒ different string ⇒ different
  key ⇒ prints. **provably provided.** (Its two lesser problems are FR-4.)
- **"If both ordinary columns are 0 it is inconclusive, not a pass" — honest but insufficient.**
  It catches the *empty-bucket* case correctly. It does **not** catch the *unrepresentative-bucket*
  case (FR-1) or the *streamed-snapshot* case (FR-2), where the ordinary column is comfortably
  non-zero and still proves nothing about the population at risk.

### 2e. The declared disagreement (item 3) — **the removal was right; I would not restore the number**

The packet deleted `(5 of 6 groups hid 0 mesh actors)` from the always-on summary, beyond the review's
ask. **I checked the constant against the log it was supposedly measured from.** In
`FactoryGame-backup-2026.08.08-15.53.04.log` the string `hid 0 mesh actors` occurs **28 times and
every one of them is inside the `WELLH2B-INDEX` message itself** — there is no `WELLH2B-SUPPRESS` tag,
no per-group hide statistic, nothing that independently produces "5 of 6". The only tags present are
`WELLH2B-INDEX` (2094), `WELLH2B-COLLISION` (162), `WELLH2B-CAPTURE` (92), `WELLH2B-APPLY` (19),
`WELLH2B-ADOPT` (15). **The constant is not re-derivable from the log it was attributed to.**
It was also doing double duty as an asserted cause (*"…count next to a high 'spatial' count **IS** the
suppression bug"*). Removing it fixed two standing-rule violations, not one. **Judgement: correct call,
and going past the review's ask was right here.**

**But information the reader needed was lost:** the line no longer offers anything to compare
`17 link / 18 spatial` against. Recommended repair is *not* to restore the constant — it is to make
the comparison **self-contained from the same line's own fields**. Wording in §5 Fix D. If the
orchestrator wants a historical anchor anyway, the only figures I can vouch for are
`(20 own, 17 link, 18 spatial)` at pass 28, which I re-derived myself — and those belong in
`docs/TECH-DEBT.md`, not in a shipped log string.

### 2f. No asserted causes, no borrowed constants in the new/reworded strings — **clean**

- `spatial REJECT` `:415-423`: the *"type gate ACCEPTED and DISTANCE rejected"* claim is gone. It now
  prints `inRadius=%d` and says *"the fields above are what was measured, classify from them."* **This
  is the correct shape.** F-5 closed.
- Pass summary `:443-446`: *"ZERO is the expected value"* is gone, replaced by *"READ EACH COUNT
  AGAINST ITS CANDIDATE DENOMINATOR…"* — a reading rule, not a prediction. **Clean.**
- NARROWED and WIDENED both state measured fields only and explicitly decline to explain the mismatch.
  **Clean.**
- Borrowed constants in the two files: **none remain.** I grepped the new strings; the only numerals
  are radii and enum legends, both from `constexpr`/enum in the same TU.
- One residual hedge worth knowing about, **not a finding**: *"a low 'via engine link' count next to a
  high 'spatial' count is the shape of the suppression bug this index exists to make visible"* is still
  an interpretation, but it is stated as a shape rather than an identification. Acceptable.

---

## 3. Alternatives — with trade-offs and a recommendation

The census (Alternative F) is the new decision point, so the alternatives are about *how to measure the
gate's assumption*, not about the gate.

| | approach | closes FR-1 (coverage)? | closes FR-2 (streaming)? | cost | verdict |
|---|---|---|---|---|---|
| **F1** | **Pair-based census in `RebuildMeshActorCache` (shipped)** | no | no | one extra `TActorIterator` per apply pass, `bDiag`-gated | **keep** — cheapest possible, runs unattended, correct partition |
| **F1+** | ***(mine — the recommendation)*** **F1 + the coverage denominator + the streamed-only caveat** (§5 Fix A) | **yes** | **yes, honestly stated** | +4 lines, one `int32` in a loop already running | **ADOPT.** Turns the load-bearing zero from over-determined into readable. Without it R-1 is not a test |
| **F2** | Proximity census: for each **unpaired** mesh actor, distance to nearest ordinary node vs nearest fracking member — classify the bucket F1 cannot | yes, fully | no | O(unpaired × nodes); the reference save has 1 473 bystanders and thousands of mesh actors, **per apply pass** | **reject on the hot path.** Correct answer, wrong place. Viable only as F3 |
| **F3** | ***(mine)*** **`NodeShuffle.DumpMeshTypeCensus` console command** running F2's expensive classification once, on demand | yes | **yes — the tester runs it standing at a well** | ~25 lines, zero hot-path cost, no dedup problem, answers "is it still bad?" | **ADOPT as a companion to F1+, or as the fallback if F1+ is judged insufficient.** TECH-DEBT **T6 already proposes exactly this shape** (`NodeShuffle.DumpWellBackstop`) for the same reason, so it is a pattern this repo has already chosen once. It also removes FR-4's second problem outright |
| **F4** | Give the census its own cvar instead of sharing `bDiag` | no | no | small | **defer.** Real (a tester who wants R-1 must accept all `bDiag` output), but not blocking |
| **F5** | Do nothing; rely on R-5 (walk the world and look at nodes) | no | no | none in code, high in tester time | **reject** — this is what Alternative F was built to replace, and it does not scale past the nodes the tester happens to walk to |
| **D′** | *(carried from the previous review)* reject a route-3 candidate whose owner is in `MeshActorCache` mapped from a **non-fracking** node — a third structural lock alongside A2 | n/a (prevention, not measurement) | n/a | ~8 lines, no new engine surface | **still deferred, correctly.** Becomes **mandatory** if F1+ reports `pairedToOrdinaryNode … non-MT_Node > 0`. Note it is now *cheaper* than when first proposed: FR-9 shows the forward sweep populates the cache for ordinary nodes on every pass |

**Recommendation: F1+ (Fix A) now, F3 (console command) as the next packet.** Fix A is 4 lines and
converts R-1 from a test that can pass vacuously into one that cannot. F3 is the durable answer and
matches a pattern the repo already committed to in T6. Keep B (hybrid `type OR name`) as the
pre-approved contingency exactly as both prior rounds concluded — adopt only against a named NARROWED
line, never pre-emptively.

---

## 4. Differential / parity — what round 1 changed, against what round 0 was graded

Round 1 is diagnostics-only; it replaces no accept path. The gate's own parity table stands. This
table covers **only the invariants round 1 could have broken or claims to have fixed**.

| # | Invariant | How round 1 provides it | Grade |
|---|---|---|---|
| R-P1 | The accept predicate is unchanged by round 1 | Diff touches only counters, comments and log strings; `:285` `continue` and `:313`/`:346`/`:395` control flow are byte-identical in shape. `bInRadius` is computed once at `:295` and reused everywhere | **provably provided** |
| R-P2 | `NarrowedByType` still a superset of `Lost` | Unchanged predicate; the `if (bInRadius)` denominator block inserted at `:296` is above it and has no side effect on control flow | **provably provided** |
| R-P3 | `WidenedByType` is a superset of `Gained` | `{type ∧ ¬name ∧ inRadius}` omits the bystander test, exactly as NARROWED does | **provably provided** |
| R-P4 | Each counter has a denominator it is a subset of | §2a — same booleans, same iteration | **provably provided** |
| R-P5 | `TypeCandidatesInRadius ≥ BySpatial` (free in-line consistency check) | Accept requires `bType ∧ bInRadius`, both counted at `:299` | **provably provided** |
| R-P6 | The census partitions its printed total | §2d | **provably provided** |
| R-P7 | The census runs after the pairing it reports is known | After both sweeps in the same function | **provably provided** |
| R-P8 | The census's ordinary bucket **represents** the ordinary-node population | **NOT PROVIDED** — coverage unprinted (FR-1) and streamed-only (FR-2) | **not provided** → Fix A |
| R-P9 | A non-zero `WIDENED` is classifiable as safe or unsafe from the log | **NOT PROVIDED** — back-link not printed (FR-5) | **not provided** → Fix B |
| R-P10 | `pairedToOrdinaryNode` reflects level authoring, not our own writes | `SetNodeActor` at `:7296` is ours; census reads it at `:7339` (FR-9). Cannot create a false zero; **can** create a false non-zero | **assumed** → R-1b |
| R-P11 | Every `%`-specifier matches its argument | §2c, hand + script, twice | **assumed** — a format-string mismatch is a runtime crash, not a compile error, in `UE_LOG`'s varargs. Only a build + one printed line settles it → R-0/R-2 |
| R-P12 | No new DLL imports | Every symbol already reached in each TU | **assumed** — engine/toolchain-internal; `dumpbin` only → R-0 |
| R-P13 | `mNodeMeshType`/`mNodeActor` really carry the values the census reports | `EditInstanceOnly` cooked level data | **unverifiable statically** → R-1 |
| R-P14 | Desert well pieces sit on `AFGNodeMeshActor`s | Cooked level data; T2's founding hypothesis | **unverifiable statically** → R-3 |
| R-P15 | `WellVisualCaptureLogged` growth is bounded | Never `Reset()` in `Source/`; census keys are ~250-char strings added per distinct measurement | **assumed** (memory, not correctness) → R-6 |

---

## 5. Fixes — verbatim, applyable

### Fix A — census coverage denominator + streamed-only caveat (closes FR-1, FR-2, mitigates FR-9)

**A1.** In `NodeShuffleSubsystem.cpp`, forward-link sweep. Replace:

```cpp
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        AFGResourceNode* Node = *It;
        if (!IsValid(Node) || Node->HasAnyFlags(RF_Transient) || IsFrackingActor(Node))
        {
            continue;
        }
        if (MeshActorCache.Contains(Node))
```

with:

```cpp
    // MESHTYPE-CENSUS coverage denominator: how many ordinary nodes were streamed in at all, so the
    // census below can print "N of M paired" rather than a bare N whose population is unknown.
    int32 OrdinaryNodesSeen = 0;
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        AFGResourceNode* Node = *It;
        if (!IsValid(Node) || Node->HasAnyFlags(RF_Transient) || IsFrackingActor(Node))
        {
            continue;
        }
        ++OrdinaryNodesSeen;
        if (MeshActorCache.Contains(Node))
```

**A2.** Replace the `Census` payload (`:7349-7357`) with:

```cpp
        const FString Census = FString::Printf(
            TEXT("meshActors=%d STREAMED-IN (not world) | pairedToOrdinaryNode: %d MT_Node + %d ")
            TEXT("non-MT_Node | pairedToFracking: %d MT_Node + %d non-MT_Node | unpaired(mNodeActor ")
            TEXT("unset): %d, of which %d non-MT_Node | coverage: %d of %d streamed ordinary node(s) ")
            TEXT("paired (%d engine back-link, %d back-links repaired by THIS pass) | mNodeMeshType ")
            TEXT("histogram 0..6 = %d/%d/%d/%d/%d/%d/%d"),
            Total, OrdinaryMTNode, OrdinaryNonNode, FrackMTNode, FrackNonNode, Unpaired, UnpairedNonNode,
            OrdinaryMTNode + OrdinaryNonNode, OrdinaryNodesSeen, FromBackLink, FromForwardLink,
            ByType[0], ByType[1], ByType[2], ByType[3], ByType[4], ByType[5], ByType[6]);
```

*(18 specifiers / 18 arguments — counted. `FromBackLink`, `FromForwardLink` and `OrdinaryNodesSeen`
are all in scope at this point.)*

**A3.** Replace the last four sentences of the census `UE_LOG` text (from `"THE LOAD-BEARING NUMBER"`
to `"Re-printed only when the numbers change."`) with:

```cpp
                TEXT("THE LOAD-BEARING NUMBER IS 'pairedToOrdinaryNode ... non-MT_Node': T2's ")
                TEXT("well-piece gate in NodeShuffleWellVisuals.cpp is only correct while it is 0. ")
                TEXT("READ IT AGAINST 'coverage': a 0 over a small coverage fraction says nothing ")
                TEXT("about the nodes that were not paired. 'unpaired' is NOT a pass or a fail -- it ")
                TEXT("holds every fracking well's own crack geometry (never paired here) mixed with ")
                TEXT("any node whose mMeshActor link did not resolve, and this line cannot tell them ")
                TEXT("apart. Counts cover STREAMED-IN actors only, so this number changes as you ")
                TEXT("travel; read the SERIES of these lines across a session, not one line. ")
                TEXT("'repaired by THIS pass' is pairing THIS MOD created seconds ago, not level ")
                TEXT("authoring. Re-printed when the numbers change."),
```

**A4.** Fix the over-claiming comment. Replace `NodeShuffleSubsystem.cpp:7320-7322`:

```cpp
    // Previously it could only be checked by walking the map and looking at nodes. This sweep ALREADY
    // visits every AFGNodeMeshActor, so the histogram is nearly free and it answers the question on ANY
    // save, at load, with no well relocation and no travel. It runs AFTER the forward sweep above so the
    // back-links that sweep repaired are counted as paired.
```

with:

```cpp
    // Previously it could only be checked by walking the map and looking at nodes. This sweep ALREADY
    // visits every AFGNodeMeshActor, so the histogram is nearly free. SCOPE, STATED HONESTLY: a
    // TActorIterator sees only STREAMED-IN actors, so one line is a snapshot of the player's
    // neighbourhood, NOT a world census -- read the series across a session. It runs AFTER the forward
    // sweep above, so back-links that sweep repaired are counted as paired (and are reported separately,
    // because they are OUR write, not level authoring).
```

**A5.** FR-6 wording. In the census `UE_LOG` change `wellAuditPasses=%d at the time of this count` to
`wellAuditPasses=%d as of the START of this apply pass; the WELLH2B-INDEX line for the SAME tick reads
pass %d`, adding `WellAuditPasses + 1` as a second argument. *(2 → 3 specifiers / 3 args.)*

### Fix B — make a `WIDENED` line self-classifying (closes FR-5)

In `NodeShuffleWellVisuals.cpp`, inside `if (bDiag && !WellVisualCaptureLogged.Contains(WKey))`,
insert before the `UE_LOG` and extend the format:

```cpp
                    WellVisualCaptureLogged.Add(WKey);
                    // THE FIELD THAT CLASSIFIES THIS LINE. A widened piece back-linked to a FRACKING
                    // node is T2 working; back-linked to nothing is the desert hypothesis; back-linked
                    // to an ORDINARY node is an ns-review-h2b F-1 recurrence. Without this the reader
                    // must go stand at the world location to tell them apart. mNodeActor is public
                    // (FGResourceNodeBase.h:70) -- no access transformer, no new import.
                    const AFGResourceNodeBase* WPaired = MeshOwner->mNodeActor.Get();
                    UE_LOG(LogNodeShuffle, Display,
                        TEXT("WELLH2B-INDEX spatial WIDENED: '%s' on '%s' at %s is %.0f cm from well ")
                        TEXT("member '%s' (inside the %.0f cm radius); its name does NOT contain 'Frack' ")
                        TEXT("but meshActorOwner=AFGNodeMeshActor nodeMeshType=%d, so the mNodeMeshType ")
                        TEXT("gate admits it and the old name test did not. Nearest ordinary resource node ")
                        TEXT("is %.0f cm away; bystanderWins=%d (1 = A2 refused it, 0 = this pass may claim, ")
                        TEXT("capture and hide it). This mesh actor's own mNodeActor back-link is '%s' ")
                        TEXT("(frackingOwner=%d; 'NONE' = unset, which is the expected state for an ")
                        TEXT("unpaired well piece). A back-link to an ORDINARY node here is the ")
                        TEXT("stop-ship case. This line is T2's gate cost in the direction that can ")
                        TEXT("hide a node; NARROWED is the safe one. Said once per mesh."),
                        *MeshName, *SMA->GetName(), *Loc.ToCompactString(), FMath::Sqrt(BestSq),
                        *WellShort(Best->Path), WellMeshOwnerRadiusCm, static_cast<int32>(MeshType),
                        ByStSq < TNumericLimits<float>::Max() ? FMath::Sqrt(ByStSq) : -1.0f,
                        bBystanderWins ? 1 : 0,
                        WPaired ? *WPaired->GetName() : TEXT("NONE"),
                        (WPaired && IsFrackingActor(WPaired)) ? 1 : 0);
```

*(11 specifiers / 11 arguments — counted. `MeshOwner != nullptr` is implied by `bWidenedByType`.
`WPaired ? *WPaired->GetName() : TEXT("NONE")` mixes an `FString`-temporary deref with a string
literal; if the toolchain objects, hoist it: `const FString WPairedName = WPaired ?
WPaired->GetName() : FString(TEXT("NONE"));` and pass `*WPairedName`. **Prefer the hoisted form** —
it is unambiguous and the temporary's lifetime question disappears.)*

### Fix C — dedup on the decision-relevant subset (closes FR-4.1, FR-7)

Replace `NodeShuffleSubsystem.cpp:7358`:

```cpp
        const FString CensusKey = Census + TEXT("|meshtypecensus");
```

with:

```cpp
        // Key on the DECISION-RELEVANT subset, not the whole payload. Total, unpaired and all seven
        // histogram bins move whenever any node mesh actor streams in or out, so keying on the full
        // string re-prints a ~900-char Display line on nearly every apply pass while nothing the
        // reader is asked to act on has changed. The four numbers below are what R-1 reads.
        const FString CensusKey = FString::Printf(
            TEXT("meshtypecensus|%d|%d|%d|%d"),
            OrdinaryMTNode, OrdinaryNonNode, FrackNonNode, UnpairedNonNode);
```

*Honest trade-off: `OrdinaryMTNode` is kept in the key deliberately, so the line still re-prints as
coverage grows — that series is the point. It will therefore still print often while the player
travels; it will stop once coverage settles, which the current key never does.*

### Fix D — the removed constant: leave it removed, replace with a self-contained comparison (item 3)

In the pass summary, replace:

```cpp
        TEXT("a low 'via engine link' count next to a high 'spatial' count is the shape of the suppression ")
        TEXT("bug this index exists to make visible. 'bystander reject' is ns-review-h2b ")
```

with:

```cpp
        TEXT("a low 'via engine link' count next to a high 'spatial' count is the shape of the ")
        TEXT("suppression bug this index exists to make visible; compare the two numbers PRINTED ABOVE ")
        TEXT("ON THIS LINE against each other and against 'member(s) with at least one piece' -- no ")
        TEXT("constant from a previous build is cited here on purpose, because the one that used to be ")
        TEXT("was not re-derivable from the log it named. 'bystander reject' is ns-review-h2b ")
```

*(No arity change.)* **Do not restore `(5 of 6 groups hid 0 mesh actors)`** — §2e shows it is not
re-derivable from `FactoryGame-backup-2026.08.08-15.53.04.log`. If a historical anchor is wanted, the
only figures I can vouch for are `(20 own, 17 link, 18 spatial)` at pass 28, and they belong in
`docs/TECH-DEBT.md`, not in a shipped log string.

### Fix E — the stale test criterion (closes FR-3)

In `C:\Claude\Projects\_team\nodeshuffle-followups\T2-meshtype-handoff.md`, replace §7 **R-0** entirely:

```markdown
**R-0 — the gate cost nothing, measured with NO borrowed baseline.**
Load any save with wells and let >= 1 apply pass run. `grep "WELLH2B-INDEX pass"`.
- **PASS:** `0 narrowed-by-type` AND `0 widened-by-type`, **each against a non-zero candidate
  denominator on the same line** (e.g. `0 narrowed-by-type of 71 name-matching candidate(s) (19 of
  them in radius), 0 widened-by-type of 18 type-matching candidate(s) (18 of them in radius)`).
  Consistency check, free on the same line: `type-matching ... in radius` must be **>= the `spatial`
  count**; if it is lower the counter is broken, not the gate.
- **INCONCLUSIVE, NOT A PASS:** either count is 0 next to a **0** denominator -- nothing was ever in
  range to be dropped or added on this save.
- **FAIL (narrowed > 0):** `grep "spatial NARROWED"` (whole file -- the lines are said once per mesh
  and may sit thousands of lines above the summary). That output is the evidence for adopting hybrid B.
- **FAIL (widened > 0):** `grep "spatial WIDENED"`. Read each line's `mNodeActor` back-link field:
  fracking = T2 working, NONE = the desert hypothesis, an ORDINARY node = **stop and revert**.

**R-0b — SECONDARY, same-save-same-session only.** If and only if you are on the exact save used on
2026-08-08 with no intervening re-roll: `(20 own, 17 via engine link, 18 spatial)` should be
unchanged. **A difference is INCONCLUSIVE, not a regression** -- the working tree also carries an
unrelated copy-fix packet. R-0 is the authoritative signal.
```

Also correct round 1 §6.3: R-0 lives in **this file** (`_team/`), not in `docs/`.

---

## 6. The 500-line seam — still holds, one new coupling to flag

`NodeShuffleWellVisuals.cpp` is **617** (confirmed by `wc -l`), 117 over. **I am not asking for the
split now** — it would re-stale `file:line` references in three review reports.

The previously-proposed seam (move `WellMeshOwnerRadiusCm`, `EnsureWellMeshIndex`,
`RebuildWellMeshIndex` into `NodeShuffleWellMeshIndex.cpp`) **still holds and got easier**: every line
round 1 added is inside `RebuildWellMeshIndex`, i.e. entirely on the moving side. Post-split residual
≈ 287 lines (`SaveableMaterialPath`, `CaptureWellGroupVisuals`, `HideWellMemberMeshes`), comfortably
under 500. Moving side ≈ 330.

**One thing round 1 makes harder, flag now:** `WellVisualCaptureLogged` (`NodeShuffleSubsystem.h:1715`)
is now shared across **three** translation units with **six** key families — `narrowed|`, `widened|`,
`bystander|` (moving side), `…|adopt`, `…|grp`, bare `Path` (staying side), plus the census key in
`NodeShuffleSubsystem.cpp`. No collision is possible (the prefixes are disjoint and a bare path cannot
contain `|`), but after the split there will be no single place that documents the namespace. **Ask
the splitting packet to add a one-line key-family table to the member's declaration comment.**

`NodeShuffleSubsystem.cpp` at 7 741 is 15× over and was before this packet; +67 does not change its
class and splitting it is not this work.

---

## 7. Runtime test checklist

Every **assumed** / **unverifiable** / **not provided** item above, executable without reading this
review. Requires a build carrying these changes and NodeShuffle diagnostics **enabled** (every new
line is `bDiag`-gated); R-3/R-4 additionally need `Log LogNodeShuffle Verbose`.

**R-0 — measure the DLL.** After the build, before launching: `check_imports.ps1` / `dumpbin /imports`
on the built NodeShuffle DLL, diffed against the pre-change baseline.
**PASS:** zero new symbols. **FAIL:** any new import — name it and stop. *(R-P12.)*

**R-1 — the census, read against its coverage.** Boot the save, `grep "MESHTYPE-CENSUS"` (all
occurrences, not the first).
**PASS:** `pairedToOrdinaryNode … non-MT_Node = 0` **and** `coverage` shows a substantial majority of
streamed ordinary nodes paired.
**INCONCLUSIVE (record as such, never as a pass):** ordinary columns both 0; **or** coverage is a
small fraction; **or** only one census line was emitted (you never travelled).
**FAIL:** any non-zero in `pairedToOrdinaryNode … non-MT_Node` — **do not ship A alone**; adopt D′ or
fall back to hybrid B. *(R-P8, R-P13. Requires Fix A; without it this step cannot distinguish PASS
from INCONCLUSIVE.)*

**R-1b — is the falsifier ours or the level's?** If R-1 FAILs, read `repaired by THIS pass` on the
same line. A large value means the mod created most of the pairings and the mis-typed pairing may be
a level-authored `mMeshActor` link we merely followed. Report both readings; do not attribute.
*(R-P10, FR-9.)*

**R-2 — the gate cost nothing, no borrowed baseline.** `grep "WELLH2B-INDEX pass"`.
**PASS:** `0 narrowed-by-type` **and** `0 widened-by-type`, each against a **non-zero** denominator on
the same line, and `type-matching … in radius >= spatial`.
**INCONCLUSIVE:** either count 0 next to a 0 denominator.
**FAIL:** see R-0 in Fix E. Grep the **whole** log for `NARROWED`/`WIDENED` — those lines are said
once per mesh and sit far above the summary. *(R-P2, R-P3, R-P4, R-P5, R-P11.)*

**R-2b — every new format string actually printed.** Confirm at least one line each of
`MESHTYPE-CENSUS`, `WELLH2B-INDEX pass` (new fields populated), and `spatial REJECT` (carrying
`inRadius=`) appears **without** garbage or truncation. A `UE_LOG` varargs mismatch is a runtime
fault, not a compile error, and `WIDENED` may legitimately never print — so it cannot be cleared this
way. *(R-P11.)*

**R-3 — desert well, the point of the packet.** Relocate a desert-biome well; fly to the destination.
**PASS:** crack graphic present and `grep "WELLH2B-INDEX spatial:"` shows accepts for that group with
`nodeMeshType=4/5/6`. **PARTIAL:** accepts with `nodeMeshType=1/2/3` — desert wells reuse grassland
types; record and move on. **FAIL:** no graphic and no accept → read `NARROWED`/`REJECT`/`WIDENED`; if
none names anything at the desert origin, the desert piece is not on an `AStaticMeshActor` and T2's
pre-scoped fix was the wrong fix — escalate, do not widen the predicate. *(R-P14.)*

**R-4 — grassland well still dresses.** Relocate a grassland well, fly there,
`grep "WELLH2B-CAPTURE"` for that core. **PASS:** `(N of them MT_Crack)` with N > 0 and the graphic
visible.

**R-5 — STOP-SHIP GATE: F-1 has not been re-opened.** At every relocated well origin, and at every
ordinary node within ~15 m of any well member: **PASS:** every ordinary node still has its rock, still
highlights on scan, still accepts a miner. **FAIL:** any invisible or un-minable node — **stop and
revert this packet.** Then `grep "spatial BYSTANDER"`: 0 with a stranded node means A2 did not catch
it and the cause is the type gate.

**R-6 — census log volume and set growth.** After ~20 minutes of travel, count
`grep -c "MESHTYPE-CENSUS"`. **PASS:** the count settles (new lines stop once coverage stabilises).
**FAIL:** it grows roughly one per apply pass indefinitely — apply Fix C or gate the census behind its
own cvar. *(R-P15.)*

---

## VERDICT: **SHIP WITH TESTS**

The round-1 code is mechanically clean — I found no arity error, no lifetime or scope defect, no new
import candidate, and the two counter/denominator pairs are provably correct supersets sharing the
predicate they measure — but the new census reports a load-bearing zero over an **unprinted coverage**
across a **streamed-in-only** population, so R-1 as written can record a vacuous PASS; apply Fix A
(4 lines) before the build, Fix B and Fix E alongside it, and treat Fix C/D as optional.

1. **R-0** — `dumpbin`/`check_imports.ps1` on the built DLL: zero new imports.
2. **R-1** — `MESHTYPE-CENSUS`: `pairedToOrdinaryNode … non-MT_Node = 0` **against a substantial
   `coverage` fraction**; both-columns-zero or low coverage = INCONCLUSIVE, never a pass.
3. **R-1b** — if R-1 fails, read `repaired by THIS pass` before attributing the falsifier to level data.
4. **R-2** — `WELLH2B-INDEX pass`: `0 narrowed-by-type` and `0 widened-by-type`, each against a
   non-zero denominator, with `type-matching in radius >= spatial`.
5. **R-2b** — one clean printed line each of `MESHTYPE-CENSUS`, the new pass summary, and
   `spatial REJECT` — a varargs mismatch is a runtime fault, not a compile error.
6. **R-3** — relocate a **desert** well: crack graphic present, `spatial:` accepts with
   `nodeMeshType=4/5/6` (1/2/3 = PARTIAL).
7. **R-4** — relocate a **grassland** well: `WELLH2B-CAPTURE` reports `MT_Crack > 0`, graphic visible.
8. **R-5** — **stop-ship:** every ordinary node near any well still has its rock, highlights, and takes
   a miner.
9. **R-6** — `MESHTYPE-CENSUS` line count settles rather than growing once per apply pass.

### What must be true in the FIRST build log for the build to count as passing

1. **Compiles with zero errors and zero new warnings** in `NodeShuffleWellVisuals.cpp` and
   `NodeShuffleSubsystem.cpp`. Specifically watch for: `C4477` (format/argument mismatch — MSVC does
   *not* emit it for `UE_LOG`, so its **absence proves nothing**); an `IsFrackingActor` overload
   resolution error on the `const AFGResourceNodeBase*` argument; and, if Fix B is applied in its
   non-hoisted form, a temporary-lifetime or `TCHAR*`/`FString` conversion error on
   `WPaired ? *WPaired->GetName() : TEXT("NONE")`.
2. `check_imports.ps1` / `dumpbin /imports` on the built DLL shows **zero new symbols** versus the
   pre-change baseline — and cross-check one known-good symbol to prove you queried the right DLL path
   (`Satisfactory\Engine\Binaries\Win64` for Core/Engine).
3. `NodeShuffleWellVisuals.cpp` is still **617 lines** (or the Fix-A/B deltas above it) — i.e. nobody
   split the file mid-review and re-staled these `file:line` refs.
4. Nothing in `NodeShuffleConfig.cpp/.h` or `NodeShuffleWellSpawn.cpp` changed as a side effect of the
   build — those belong to the concurrent copy-fix packet.
