# Cold review — packet `ns-t2-meshtype` (T2: `mNodeMeshType` gate replaces `Contains("Frack")`)

- **Subject:** `C:\Claude\Projects\NodeShuffle\Source\NodeShuffle\Private\NodeShuffleWellVisuals.cpp`, uncommitted on `wip/h2-relocation`, base `731246d`. `+86 / −32`, one function (`ANodeShuffleSubsystem::RebuildWellMeshIndex`, route 3).
- **Reviewer:** cold (did not author). Nothing was built, committed, or edited.
- **Implementer handoff reviewed as claims:** `C:\Claude\Projects\_team\nodeshuffle-followups\T2-meshtype-handoff.md`.
- **Out of scope, not reviewed:** `NodeShuffleConfig.cpp/.h`, `NodeShuffleWellSpawn.cpp`, `docs/TECH-DEBT.md` (separate packet, concurrent reviewer).
- **Not re-litigated (settled by the orchestrator):** route-3 reachability of `AFGNodeMeshActor`. Confirmed independently; treated as given.

## VERDICT: SHIP WITH TESTS — conditional on two diagnostic additions (F-2, F-3)

The change compiles-as-read, needs no new access transformer, and is not the F-1 regression reproduced. But it **moves the entire load of the F-1 guarantee off a mesh-name family and onto a single level-authored `EditInstanceOnly` enum**, and it instruments the *safe* direction at `Display` while leaving the *dangerous* direction with **no counter at all**. Ship it with F-2 and F-3 folded in — they are ~20 lines and they turn the packet's central assumption from "test it in-game and hope you notice" into a number printed at boot on any save.

---

## 1. Findings — ranked

### F-1 (HIGH, risk reclassification, not a defect) — the class half of the new gate provides **zero** protection against an ordinary node's rock; 100% of F-1's protection is now one level-authored enum field
`NodeShuffleWellVisuals.cpp:259-266`

```cpp
const AFGNodeMeshActor* MeshOwner = Cast<AFGNodeMeshActor>(C->GetOwner());
const ENodeMeshType MeshType = MeshOwner ? MeshOwner->mNodeMeshType : ENodeMeshType::MT_Node;
const bool bTypeSaysWellPiece = MeshOwner != nullptr && MeshType != ENodeMeshType::MT_Node;
```

**The comment at `:193-195` ("an ordinary node's rock reports `MT_Node` and is rejected on the FIRST test") is misleading about which test does the work.** The *first* test is the `Cast`, and it rejects nothing: **ordinary node rocks in this world sit on `AFGNodeMeshActor` instances.** This is established by the mod's own code, not by inference:

- `FGResourceNodeBase.h:59` — *"A Static Mesh Actor that should be used for **all node meshes in the world**."*
- `NodeShuffleSubsystem.cpp:7269` — `for (TActorIterator<AFGNodeMeshActor> It(GetWorld()); It; ++It)`, the back-link sweep that pairs mesh actors to **ordinary** nodes (the forward sweep at `:7285` explicitly `continue`s on `IsFrackingActor(Node)`).
- `NodeShuffleSubsystem.cpp:2600-2606` — the "nearest unpaired mesh actor within 10 m" probe run for **ordinary** node capture.

So the discriminating power of the new gate against ordinary nodes is entirely `mNodeMeshType != MT_Node` — a `UPROPERTY(EditInstanceOnly)` (`FGResourceNodeBase.h:71`) with default `MT_Node`, per-instance, baked into cooked `.uasset`s. The old predicate discriminated on a **mesh-name family** that ordinary node rocks provably do not join (`ResourceNode*`, `CoalResource*`, `SulfurResource*`, `SAM_*`).

**Concrete failure scenario.** A resource node — a level instance mis-tagged by CSS, a geyser (`EResourceNodeType::Geyser` has *no* `ENodeMeshType` value of its own, so its mesh actor's tag is unconstrained by the enum's design), or a node spawned by another mod in this heavily-modded world (`BP_ResourdeNode_Alkali_C`, modded lead — 28 such nodes appear post-boot per `TECH-DEBT` T14) — whose mesh actor carries `MT_Core`/`MT_Crack`/`MT_Satellite`. Its rock sits within 1200 cm of a fracking satellite **and** is nearer to that satellite than to its own node actor (the F-1 incident geometry was a coal rock at 1100 cm from a satellite; the mod's own probe allows a mesh actor to sit up to 10 m from its node), **or** its node actor is not in `TActorIterator<AFGResourceNodeBase>` at index-rebuild time while its mesh actor is. Then `ByStSq` does not win the A2 contest, the rock is claimed → captured → hidden with collision disabled. **Result: an invisible, un-minable node in the player's live save** — F-1 verbatim.

**What genuinely holds (and I verified it):**
- A2 (`:303-337`) is untouched and is the real lock. For the common case the rock is at ~0 cm from its own node, so `ByStSq ≈ 0` and the bystander always wins. Strong.
- **The mod cannot strand its own nodes this way.** `grep "mNodeMeshType *=" Source/` → **zero writes** anywhere in NodeShuffle; and `SpawnVisualRockForNode` (`NodeShuffleSubsystem.cpp:2686`) attaches a `RockMesh` **component to the `AFGResourceNode` itself** rather than spawning a separate `AStaticMeshActor`, so mod-spawned ordinary rocks are not visited by route 3 at all. **Provably provided.**

**This is not a blocker and not a reason to reject the packet** — the risk is *narrow* and A2 covers the realistic geometry. It is a reason to **measure the assumption instead of asserting it**, which is F-2/F-3 and Alternative F below.

**Required comment correction (no logic change):** `:193-195` should say F-1's protection now rests on the enum value, not on the cast, and that A2 is the *only* structural lock — the current wording invites a future reader to believe the class test filters ordinary nodes.

---

### F-2 (HIGH, diagnostics) — the safe direction is instrumented at `Display`; the dangerous direction has **no counter**
`:281-302` (NARROWED, `Display`) vs `:344-354` (`spatial:` accept, **`Verbose`**)

Define, over route-3 candidates:
- `Lost  = {name ∧ ¬type ∧ inRadius ∧ ¬bystanderWins}` — pieces the old path took and the new one drops.
- `Gained = {type ∧ ¬name ∧ inRadius ∧ ¬bystanderWins}` — pieces the new path takes and the old one refused. **This is the set that can strand a player's node.**

`NarrowedByType` (`:285`) counts a *superset* of `Lost` (it omits the bystander test) — so `narrowed-by-type == 0 ⇒ Lost = ∅`. That is sound and conservative. Good.

**`Gained` has no counter.** Its only trace is `nodeMeshType=%d` on the `spatial:` accept line, which is `Verbose` — and this file's own F-4 note (`:319-321`) records that *"a Verbose line is invisible at the runtime default verbosity"*. **The one direction that reproduces the mod's worst shipped bug is the one a tester cannot see without a console command.**

The handoff's R-0 does pin `Gained = 0`, but only by importing `|S_old| = 18` from a previous build's log — see F-3. On any other save, or after a re-roll, or after a well relocation changes `Members`, `Gained` is unmeasurable.

**Failure scenario:** tester runs R-0 on a *different* save (or after the concurrent copy-fix packet perturbs state), reads `18 spatial, 0 narrowed-by-type`, records PASS. Three newly-admitted non-`Frack` pieces were claimed and hidden; nothing in the log names them; the report says the gate cost nothing.

**Fix (required):** add the symmetric counter and line.
```
WidenedByType  = count of { bTypeSaysWellPiece && !bNameSaysWellPiece && bInRadius }
```
plus a once-per-`(actorPath|mesh)` `Display` `WELLH2B-INDEX spatial WIDENED:` line printing mesh, actor, location, distance, `nodeMeshType`, and the bystander distance — the same shape as NARROWED. Then `narrowed==0 && widened==0` is a **self-contained** pass criterion needing no baseline from any prior build.

---

### F-3 (MEDIUM, diagnostics) — `narrowed-by-type` has no denominator; `0` is over-determined
`:285`, summary at `:369-383`

`narrowed-by-type == 0` cannot distinguish *"the gate dropped nothing"* from *"no `Frack`-named candidate was ever inside 1200 cm to be dropped."* From the reference log the ~30 `SM_FrackingNode_Crack_01` decoration components on `StaticMeshActor_0…30` sit at **8 962–80 924 cm** — all far outside the radius. So on the reference save this counter reads 0 **because its input set is empty**, and the summary text (`:379-381`) instructs the reader that *"ZERO is the expected value"*, converting an empty denominator into a green light.

This is precisely the defect this repo post-mortemed **the same day**, in `docs/TECH-DEBT.md` T5: *"A ratio read off lines whose numerator is structurally zero is not evidence of anything."* The packet re-created the denominator-free counter in the same file.

**Fix (required):** also count and print `NameCandidatesSeen` = every component where `bNameSaysWellPiece` was true (in radius or not). A summary reading `0 narrowed-by-type of 31 name-matching candidate(s) (2 in radius)` is evidence; `0 narrowed-by-type` alone is not. Same treatment for the new `widened` counter.

**Also:** `"ZERO is the expected value"` is an unverified prediction printed in an always-on `Display` line. On a desert save it may legitimately be non-zero. Reword to state the measurement, not the expectation (e.g. *"a non-zero count means the type gate dropped an in-radius name-matching piece; the NARROWED lines name each one"*) and drop "expected".

---

### F-4 (MEDIUM, test design) — R-0's pass criterion `(20 own, 17 link, 18 spatial)` is a constant borrowed from a different binary
`T2-meshtype-handoff.md` §7 R-0

Those numbers come from `FactoryGame-backup-2026.08.08-15.53.04.log`, produced by the **pre-change** build. Since then: the concurrently-reviewed copy-fix packet has modified `NodeShuffleConfig.cpp/.h` and `NodeShuffleWellSpawn.cpp` in the same working tree, and any re-roll, relocation, or streaming difference moves `own`/`link`/`spatial`. A tester will read a legitimate difference as a T2 regression, or an actual regression as save drift.

Note also this is not new to the packet: the summary line already carries `"5 of 6 groups hid 0 mesh actors"` — a historical constant from an earlier build, asserted in an always-on log line. Pre-existing; inherited, not introduced.

**Fix:** restate R-0's pass criterion as `narrowed-by-type == 0 AND widened-by-type == 0` (self-contained, no baseline), and demote the `(20/17/18)` comparison to a *secondary* signal explicitly labelled "same save, same session, previous build only." **F-2 and F-4 are one fix.**

---

### F-5 (LOW) — the rewritten `spatial REJECT` line asserts a cause it did not measure
`:358-364`: *"This is a piece the type gate ACCEPTED and DISTANCE rejected."*

That branch is `else if (bDiag && Best != nullptr)` on `if (bOwned && AddPiece(Best->Path, C))`. It is reached when `!bInRadius` **or when `AddPiece` returned false**. `AddPiece` (`:203-210`) fails on `!IsValid(C) || Claimed.Contains(C)` — both already excluded at `:255` with nothing adding to `Claimed` in between, so today the claim is true. **But it is true by a non-local invariant across 85 lines.** Add one `Claimed.Add` or a second `AddPiece` call in this loop and the line begins lying, in the exact class this project shipped before ([[lessons-log-asserted-a-cause]]).

**Fix:** print the measured fields — `inRadius=%d` — and let the reader classify, or restructure as `if (!bInRadius) { …REJECT… } else if (!AddPiece(...)) { …CLAIMED-ALREADY… }`.

---

### F-6 (LOW) — two `UPROPERTY(SaveGame)` diagnostic fields silently change meaning across builds
`:465` `RouteTag` and `:460` `MeshType`

With the type gate, every accepted route-3 piece has a non-null `Cast<AFGNodeMeshActor>(C->GetOwner())`. Therefore:
- `RouteTag`'s bare `"spatial"` value becomes **unreachable**; all route-3 pieces are now tagged `"link-or-spatial"`.
- `MeshType`'s `255` sentinel ("unknown / not a node mesh actor", `NodeShuffleSubsystem.h:191`) becomes unreachable for route-3 pieces.

Both are persisted to the save. Nothing branches on them (`NodeShuffleSubsystem.h:192`), so this is not a functional defect — but a future reader diffing `RouteTag` across pre- and post-change saves will conclude the routes changed. The implementer flagged this as "not a defect I introduced"; it is nonetheless *caused* by this packet, since the previous build could emit `"spatial"`.

**Fix:** either set `RouteTag` from the route that actually produced the piece (the index knows it) or add a dated TODO at `NodeShuffleSubsystem.h:197`: `// TODO 2026-08-08 (ns-t2-meshtype): "spatial" unreachable since the type gate; do not read as route provenance.`

---

### F-7 (STYLE NOTE, no failure scenario) — 500-line rule breached
`wc -l` confirms **551**. Reported by the implementer, not hidden. Split deferred correctly. See §4.

---

## 2. Compiler review (never compiled — checked as a compiler would)

| check | result |
|---|---|
| `AFGNodeMeshActor` complete at use site | **YES.** `#include "Resources/FGResourceNodeBase.h"` at `:78`; class defined at `FGResourceNodeBase.h:61`. Not a forward decl. |
| `ENodeMeshType` in scope | **YES.** Namespace-scope `enum class : uint8` at `FGResourceNodeBase.h:37-46`, same header. |
| `mNodeMeshType` accessible from mod code | **YES — it is `public`.** `FGResourceNodeBase.h:64 public:` covers `:67-81`; `private:` starts at `:83`. **No `AccessTransformers.ini` entry is required or added.** The existing `Friend=(Class="AFGNodeMeshActor", FriendClass="ANodeShuffleSubsystem")` grant covers `mNodeActor`/`SetNodeActor` and is untouched. The implementer's claim holds. |
| const-correctness | `Cast<AFGNodeMeshActor>(AActor*)` → `AFGNodeMeshActor*`, binds to `const AFGNodeMeshActor*`. Reading a non-mutable member through a const pointer: fine. |
| `static_cast<int32>(MeshType)` on `enum class : uint8` → `%d` | valid |
| duplicate `bInRadius` declaration | none — hoisted to `:276`, the old site removed. Verified single declaration. |
| `MeshType` / `MeshOwner` scope at all four log sites | all inside the same `for (UStaticMeshComponent* C : Comps)` body. Valid. |
| null deref of `Best` in NARROWED (`:297`) | guarded — `bInRadius` is `Best != nullptr && …`. Safe. |
| unused-variable warning on `bNameSaysWellPiece` | used at `:266`. No warning. |
| **format-string arity (4 touched `UE_LOG`s)** | **NARROWED `:290-298`: 8 specifiers / 8 args ✓ · `spatial:` `:344-353`: 8/8 ✓ · `REJECT` `:358-364`: 7/7 ✓ · pass summary `:369-383`: 10/10 ✓.** Escaped `\"` inside `TEXT()` at `:378` is valid C++. `%.0f` fed `float`s (varargs-promoted to `double`) and `static constexpr float WellMeshOwnerRadiusCm` — identical to the pre-existing usage at `:333`. |

**No compile error found.** This is a static read, not a build; a build is still required.

**Import surface.** `Cast<AFGNodeMeshActor>` resolves to `AFGNodeMeshActor::GetPrivateStaticClass` (`FACTORYGAME_API`), already referenced in **this same translation unit** at `:458` and `:465`, and in `NodeShuffleSubsystem.cpp` at `:2602`, `:7269`, `:7296`. `mNodeMeshType` is a plain field read — a compile-time offset, no symbol. The identical `Cast<>` instantiation already exists, so no new template instantiation either. **Prediction: no new imports.** Per the house rule and [[ue-import-table-must-be-measured]] this is graded **assumed** and can only be settled by `check_imports.ps1` / `dumpbin` diff against the baseline on the built DLL. *(Precedent: "virtual ⇒ no import" and "inline ⇒ no import" have both been falsified on this machine.)*

---

## 3. Differential / parity table

Old accept: `{ Contains("Frack") ∧ inRadius ∧ ¬bystanderWins }`. New accept: `{ Cast<AFGNodeMeshActor> ∧ type != MT_Node ∧ inRadius ∧ ¬bystanderWins }`. Neither is a subset of the other.

| # | Invariant the OLD path guaranteed | How the NEW path provides it | Grade |
|---|---|---|---|
| P-1 | Route 3 never offers an **ordinary node's rock** to the contest (F-1's founding reason) | Ordinary rocks **do** pass the `Cast` (they are on `AFGNodeMeshActor`s — proven from `Subsystem.cpp:7269`/`:2600` + `FGResourceNodeBase.h:59`). Rejection depends solely on `mNodeMeshType == MT_Node`, an `EditInstanceOnly` field in cooked assets. A2 is the only structural lock. | **assumed** — F-1. *(Implementer graded this "provably provided for the predicate"; that grade is about the predicate's text, not the invariant. Downgraded.)* |
| P-2 | Routes 1 and 2 never consulted the predicate | `grep IsWellPieceMeshName Source/` → **empty**; neither loop body contains a name or type test | **provably provided** |
| P-3 | A grassland well's crack graphic is captured | Those pieces are on actors the orchestrator's arithmetic shows cast non-null and report `MT_Crack`; proven for ≥2, corroborated for 18 | **assumed** → R-2 |
| P-4 | Failure direction is SAFE (under-dressed, never strands a bystander) | Holds for `Lost`. **Does not hold for `Gained`** — a newly-admitted piece is *claimed*, not dropped, and claiming is the unsafe direction. A2 is untouched and is the guard. | **assumed** (was "provably provided" in the handoff; the handoff's grade covers only the `Lost` half) |
| P-5 | *(new capability)* Desert well pieces captured regardless of mesh name | `MT_Desert*` all satisfy `!= MT_Node` — **iff** the desert piece sits on an `AFGNodeMeshActor`. Cooked level data. | **unverifiable statically** → R-3 |
| P-6 | Nothing outside `AStaticMeshActor` visible to route 3 | Iterator unchanged (`:248`) | **provably provided** |
| P-7 | ISM / mesh-less / mod-owned well components excluded | `IsUsableMesh` (`:211-215`) untouched | **provably provided** |
| P-8 | **The mod cannot claim its own relocated ordinary node's rock** | `grep "mNodeMeshType *="` → zero writes in NodeShuffle; `SpawnVisualRockForNode` (`Subsystem.cpp:2686`) attaches a `RockMesh` **component to the node actor**, which is not an `AStaticMeshActor` and is therefore invisible to route 3 | **provably provided** |
| P-9 | `Lost` is observable | `NarrowedByType` + `Display` NARROWED lines — a superset of `Lost`, so `0 ⇒ Lost = ∅`. Sound. Weakened by having no denominator (F-3). | **provably provided** (the counter is correct); its *interpretation* is **assumed** → F-3 |
| P-10 | `Gained` is observable | **NOT PROVIDED.** No counter; `Verbose` only. | **not provided** → F-2 |
| P-11 | No new DLL import surface | Symbol already referenced in the same TU | **assumed** — `dumpbin` / `check_imports.ps1` after build |
| P-12 | Persisted `RouteTag` / `MeshType` keep their meaning | `"spatial"` and `255` become unreachable for route-3 pieces | **changed, diagnostics-only** → F-6 |

---

## 4. The 500-line seam (confirmed, not requested now)

Proposed seam — move `WellMeshOwnerRadiusCm`, `EnsureWellMeshIndex`, `RebuildWellMeshIndex` into `NodeShuffleWellMeshIndex.cpp` — is **sound**:

- `WellMeshOwnerRadiusCm` (`:110`) is used **only** inside `RebuildWellMeshIndex` (`:276, :297, :333, :353, :364`) plus two comments. It moves cleanly.
- `SaveableMaterialPath` (anon namespace, `:117`) is used only by `CaptureWellGroupVisuals` — stays.
- Everything else the index touches is a member of `ANodeShuffleSubsystem`, accessible from any TU defining its member functions. The `M.Node->mMeshActor` friend read (`:234`) works from any TU because the grant is `FriendClass="ANodeShuffleSubsystem"`, class-scoped not file-scoped.
- No new `AccessTransformers.ini` entry needed.

**Two things that make the split harder later — flag now:**
1. The 70-line file header (`:1-70`) documents the **index** (the three-routes taxonomy at `:43-53`, the import-discipline block at `:65-70`) as much as the capture. Splitting must carry those halves to the new file or the rationale is orphaned — this repo has an explicit record of being burned by lost rationale.
2. This packet added ~28 lines of comment at `:175-201` that are *index* rationale, and F-2/F-3 will add ~25 more lines of diagnostics. Both land on the moving side, so the split gets **better**, not worse — but the post-split residual grows from ~260 to ~285. Still comfortable.

---

## 5. Alternatives (with trade-offs and a recommendation)

I reviewed the implementer's A–E table and largely agree. Two corrections and one addition.

| | approach | closes desert? | F-1 exposure | can lose a working capture? | verdict |
|---|---|---|---|---|---|
| **A** | Pure type gate (shipped) | yes, if the piece is on an `AFGNodeMeshActor` | **higher than the handoff claims** (F-1): the cast filters nothing among node rocks | yes, in principle; made visible by NARROWED | acceptable, but **not sufficient alone** |
| **B** | Hybrid `type OR name` | yes | strictly the widest of the three | no, by construction | **agree with the implementer: do NOT adopt pre-emptively.** Adding a correction I want on record: B is the *union*, so it inherits **all** of A's `Gained` exposure **plus** the old name-set — it is strictly worse than A for F-1, not equal. And B **permanently pins `narrowed-by-type` to 0**, destroying the very measurement that would justify it. Correct as a contingency, wrong as a default. |
| **C** | Enumerate desert mesh names | only if named that way — unknowable | `"Desert"` is a terrible substring | no | **agree: reject.** Re-creates F-1's failure shape. |
| **D** | Reject if the owner's `mNodeActor` points at a non-well node | no | very low | no | **agree: near-inert** (`mNodeActor` unset for ~98% of level nodes). |
| **D′** | *(mine, stronger than D)* Reject if the owner is in `MeshActorCache` mapped **from a non-fracking node** | no | very low | no | **worth adding as a cheap third lock.** `MeshActorCache` (`Subsystem.h:1224`) is built from **both** links (`Subsystem.cpp:7269-7300`), not just `mNodeActor`, so it is materially less inert than D. Requires a reverse lookup or a pre-built `TSet` of claimed mesh actors — ~8 lines, no new engine surface. |
| **E** | Do nothing | no | none | no | **agree: reject.** |
| **F** | ***(mine — the recommendation)*** **A + a world-wide `mNodeMeshType` census at boot** | yes | **A's exposure, measured instead of assumed** | no | **ADOPT WITH A.** `RebuildMeshActorCache` (`Subsystem.cpp:7269`) *already* iterates every `AFGNodeMeshActor` in the world. ~12 lines there produce a histogram of `mNodeMeshType` split three ways: paired-to-fracking / paired-to-ordinary / unpaired. That single log line **directly measures the one assumption the entire packet rests on** — "ordinary nodes carry `MT_Node`" — on **any** save, at **boot**, with **no** well relocation and **no** desert travel. No new import (iterator + field read both already present), no new access transformer. It converts P-1 from **assumed** to **measured** in one launch, and it is strictly cheaper than R-4's "walk around and look at nodes." |

**Recommendation: ship A + F + F-2's `widened` counter + F-3's denominator.** Keep B as pre-approved contingency exactly as the implementer proposed — I agree it should be adopted only against a named NARROWED line, never pre-emptively. D′ is optional hardening; defer unless the census (F) shows any non-`MT_Node` value on an ordinary node, in which case D′ becomes **mandatory** and A alone must not ship.

---

## 6. On the handoff's §13 (what it admits it did not verify)

| # | its claim | my assessment |
|---|---|---|
| 1 | never compiled | **Real, and unavoidable here.** My §2 pass found no error, including all four format strings. Build required. |
| 2 | import table not measured | **Real but low.** Symbol already in this TU. Still requires `check_imports.ps1` — the rule exists because reasoning has been falsified twice. |
| 3 | not every route-3 piece proven to cast | **Real, MEDIUM.** This is `Lost`, and `narrowed-by-type` covers it correctly. Adequately handled. |
| 4 | desert pieces may not be on `AFGNodeMeshActor`s | **Real, and it is the packet's whole point being unresolved.** Only R-3 answers it. Correctly stated. |
| 5 | ordinary nodes may not carry `MT_Node` | **The most important one, and it is under-weighted.** The handoff treats it as a tail risk covered by A2; per F-1 it is the *sole* discriminator, and it is exactly the assumption Alternative F measures for ~12 lines. **Escalate.** |
| 6 | runtime class of `StaticMeshActor_0…30` unknown | **Acceptable.** Rejected by radius today at 90–800 m. But it is precisely why `narrowed-by-type` reads 0 for the wrong reason (F-3). |
| 7 | no in-game test run | **Real.** Everything below. |
| 8 | `WELLH2B-CAPTURE` numbers not re-derived for all logs | **Acceptable.** Orchestrator independently re-derived the load-bearing arithmetic. |

---

## 7. Runtime test checklist

Every **assumed** / **unverifiable** invariant above, as a step executable without reading this review. Requires a build carrying this change, NodeShuffle diagnostics **enabled**, and for R-4/R-5 the log verbosity raised (`Log LogNodeShuffle Verbose`).

**R-0 — measure the DLL, not the reasoning.** After the build, before launching: run `check_imports.ps1` / `dumpbin /imports` on the built NodeShuffle DLL and diff the import table against the pre-change baseline. **PASS:** zero new symbols. **FAIL:** any new import — name it and stop; the shipping-export trap has bitten this mod before. *(Covers P-11.)*

**R-1 — the `mNodeMeshType` census (do this first; it is the cheapest and highest-value signal).** With Alternative F landed, boot any save and `grep "MESHTYPE-CENSUS"`. **PASS:** every mesh actor paired to a **non-fracking** node reports `MT_Node` (count of non-`MT_Node` among ordinary = **0**). **FAIL:** any non-zero — **do not ship A alone**; add D′ or fall back to B. *(Covers P-1, handoff §13.5. Without Alternative F this step is impossible and P-1 stays assumed through the whole test round.)*

**R-2 — the gate cost nothing, measured without a borrowed baseline.** Load a save with wells, let ≥1 apply pass run, `grep "WELLH2B-INDEX pass"`. **PASS:** `0 narrowed-by-type` **and** `0 widened-by-type`, **against a non-zero name-candidate denominator** (F-3). **FAIL (narrowed > 0):** `grep "spatial NARROWED"` — each line names the mesh, actor, distance and owner. That output is the evidence for adopting hybrid B. **FAIL (widened > 0):** `grep "spatial WIDENED"` — each named piece is a rock this build claims and the previous build did not; verify at that world location that it belongs to a well and not to a node. *(Covers P-4, P-9, P-10, I-1.)*

**R-3 — desert well, the point of the packet.** Relocate a **desert-biome** well; fly to the destination. **PASS:** crack graphic present and `grep "WELLH2B-INDEX spatial:"` shows accepts for that group with `nodeMeshType=4/5/6`. **PARTIAL:** accepts with `nodeMeshType=1/2/3` — desert wells reuse grassland mesh types; T2 closed, desert enum values unexercised; record and move on. **FAIL:** no crack graphic and no `spatial:` accept — then read `spatial REJECT` (type passed, distance failed) and `spatial NARROWED` (type failed). If **neither** names anything at the desert origin, the desert piece is not on an `AStaticMeshActor` at all: route 3 cannot see it, T2's pre-scoped fix was the wrong fix — escalate, do not widen the predicate. *(Covers P-5, I-4.)*

**R-4 — grassland well still dresses.** Relocate a grassland well, fly to the destination, `grep "WELLH2B-CAPTURE"` for that core. **PASS:** `(N of them MT_Crack)` with **N > 0** and the crack graphic visible. **FAIL:** N = 0 or graphic missing. *(Covers P-3, I-2.)*

**R-5 — F-1 has NOT been re-opened.** This is the stop-ship gate. At every relocated well origin, **and** at every ordinary resource node within ~15 m of any well member (use the `WELLH2B-INDEX spatial` lines to find them): **PASS:** every ordinary node still has its rock, still highlights on scan, still accepts a miner. **FAIL:** any ordinary node invisible or un-minable — **stop and revert this packet.** Then `grep "spatial BYSTANDER"`: if the count is 0 and a node was stranded anyway, A2 did not catch it and the cause is the type gate. *(Covers P-1, P-4, I-3.)*

**R-6 — secondary, same-save-same-session only.** If and only if you are on the exact save used on 2026-08-08 with no intervening re-roll: `(20 own, 17 via engine link, 18 spatial)` should be unchanged. **Treat a difference as inconclusive, not as a regression** — the tree also carries an unrelated copy-fix packet. R-2 is the authoritative signal. *(F-4.)*

---

## VERDICT: **SHIP WITH TESTS**

The gate is correct as written and needs no access transformer, but it silently relocates the entire F-1 guarantee onto one unread level-authored field while instrumenting only the harmless direction — fold in F-2 (`widened` counter), F-3 (denominator), and Alternative F (boot-time `mNodeMeshType` census) before the user tests, then run:

1. **R-0** — `dumpbin`/`check_imports.ps1` on the built DLL: zero new imports.
2. **R-1** — boot any save, `MESHTYPE-CENSUS`: zero non-`MT_Node` among mesh actors paired to ordinary nodes.
3. **R-2** — `WELLH2B-INDEX pass`: `0 narrowed-by-type` **and** `0 widened-by-type` against a non-zero candidate denominator.
4. **R-3** — relocate a **desert** well: crack graphic present, `spatial:` accepts with `nodeMeshType=4/5/6` (1/2/3 = PARTIAL, record and move on).
5. **R-4** — relocate a **grassland** well: `WELLH2B-CAPTURE` reports `MT_Crack > 0` and the graphic is visible.
6. **R-5** — **stop-ship gate:** every ordinary node near any well still has its rock, still highlights, still takes a miner.
7. **R-6** — secondary only: `(20 own, 17 link, 18 spatial)` on the identical save/session; a difference is inconclusive, not a regression.
