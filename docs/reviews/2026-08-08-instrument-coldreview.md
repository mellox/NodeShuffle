# Cold review — packet `ns-t1t2-instrument` (four measurements, never compiled)

**Reviewer:** cold-reviewer (did not write this code). **Date:** 2026-08-08.
**Subject:** uncommitted diff on `wip/h2-relocation` @ `2aef9ba`, files
`NodeShuffleWellVisualsApply.cpp` (+104), `NodeShuffleWellVisuals.cpp` (+17), `NodeShuffleSubsystem.cpp` (+285).
**Handoff under test:** `C:\Claude\Projects\_team\nodeshuffle-followups\T1T2-instrument-handoff.md`.
**Nothing was built, committed, or edited by this review.**

## VERDICT — DO NOT SHIP (as-is)

Two blockers, both of the exact class this packet was written to avoid: an always-on line that
prints a definitive "nothing was ever measured" in the very run where the thing *was* measured,
and a watch population that is ~91% inert on the user's own save. Both are cheap to fix, and
verbatim replacement code is given in §6. Items 1 (T3), 3 (cave) and 4 (nearest well) are close
to shippable with three smaller corrections.

**The live log settled the headline question — the packet's model of the world is wrong, and the
evidence was already on disk.** Sources: `C:\Users\mello\AppData\Local\FactoryGame\Saved\Logs\FactoryGame.log`.

---

## 0. HEADLINE — would the new counter have fired for the user's observation?

**Answer: it enrols the records, but it will NOT produce the number T4 exists to produce.**
The delay is real, it *is* measured, and it is routed exclusively through the one path that never
publishes it to the always-on line.

### The measured facts (user's own log, not reasoning)

| line | value |
|---|---|
| `Hide-originals funnel (first pass this load)` | `records=658 loaded=658 newlyHidden=658 alreadyHidden=0 occupied=0 pathUnresolved=0` |
| `Mesh-actor cache:` (200+ passes) | `74 paired (96 back-link, 0 forward)`, flat at 73–74 all session |
| `MESHTYPE-CENSUS` | `meshActors=96 STREAMED-IN` … `coverage: 67 of 1087 streamed ordinary node(s) paired` |
| `Hide originals: hid 0 original nodes and N stray original rocks` | **30 occurrences, N = 1..8** |

### What that implies for the new counters, before a single line of new code runs

1. **`MeshUnresolvedAtHide` ≈ 584 on the first pass** (658 hidden, ≤74 pairable). `Watch` ≈ 584.
2. **The watch sweep will essentially never fire.** `FindMeshActorForNode` reads `MeshActorCache`,
   which `RebuildMeshActorCache` rebuilds every pass from `TActorIterator` + `mMeshActor.Get()`.
   Coverage is *flat at 6%* across the whole logged session. Records that failed to pair keep
   failing. `ResolvedLaterTotal` stays **0**; `LastDelaySeconds` / `MaxDelaySeconds` stay **-1**.
3. **The rocks are going dark through the STRAY-ROCK BACKSTOP** — `hid 0 original nodes and 1..8
   stray original rocks`, thirty times, is *precisely* "a rock went dark on a later pass than its
   node". That is T4's phenomenon, already visible in the shipped log, on a path the packet's
   model does not name.
4. `RockBackstopCooldownSeconds = 30.0f` (`NodeShuffleSubsystem.cpp:4833`) gates that backstop when
   `NodesHidden == 0`. That is a standing, in-code, up-to-30-second latency floor for exactly this
   case. **The packet correctly kept the user's "30 seconds" estimate out of the code — and then
   did not print the mod's own 30 s constant, which is the leading candidate explanation.**

### So the log the user gets reads

```
MESHHIDE-LATENCY hide-pass 12: hid 0 original node(s) this pass; of those 0 had a resolvable
AFGNodeMeshActor ... and 0 did NOT. WATCH LIST ...: 584 still open. RESOLVED LATER, session
totals: 0 record(s), ... Delay last=-1.0 s worst=-1.0 s (-1 = no delay has ever been measured
this session). STRAY-ROCK BACKSTOP: 1 rock(s) hidden this pass within 800 cm of a watched record.
```

A reader takes `Delay worst=-1 … no delay has ever been measured this session` at face value. It is
false: a delay was measured ~30 s earlier, by the backstop, thirty lines up, in the same function.
**That is T5's defect living inside the fix for T4.** Blocker.

### The three sub-questions, answered directly

**(a) "If a mesh actor DOES resolve at hide time and is hidden, but the rock the player sees streams
in or re-renders later — is that case in the watched population?"**
**No.** `SteadyHiddenOriginals` skip at `:4647-4655` requires `Steady->Get() == Node && Node->IsHidden()`,
so a re-streamed **node** (new instance, weak-ptr mismatch) *does* re-enter the hide site and can be
re-enrolled — the packet is safe there. But a **mesh actor** that unstreams and re-streams *while its
node instance persists* is invisible to every counter: the node stays steady, the hide site never
re-runs, the record was never watched, and the fresh (visible) mesh actor is hidden only by the
backstop, whose watched-record loop finds nothing. Grade: **assumed reachable, unproven** — it needs
the two actors to be on different streaming grids. Rank it below (b), which is measured.

**(b) "Would the new counter have fired for the user's actual observation?"**
**Enrolment: yes. The number: no.** With 96 mesh actors for 1087 streamed ordinary nodes, the coal
originals were almost certainly in the else branch and are on the watch list — so `watch=584` fires.
But `resolvedLater` stays 0 and both delays stay -1, because the resolution never happens and the
route that *does* observe the rock going dark (backstop) contributes only a count. The user gets a
non-zero watch list, a permanent "INCONCLUSIVE", and `-1`. **This is a blocker, exactly as the brief
predicted.**

**(c) "Does the watch route inherit the `(first pass this load)` one-shot limitation?"**
**No — this one is clean.** `bLoadFunnelLogged` (`:4919`) gates *only that log line*. The record loop,
the hide site, the watch sweep (`:4791`) and the summary all run every `SuppressOriginalNodes` call,
which `ApplyLayout` invokes at `:2240` each ~5 s pass. The two early returns (`:4505` no records,
`:4524` no player pawn) both sit **above** the T4 bookkeeping block at `:4539`, so `MeshHidePass`
counts only passes that processed records — consistent with its own comment. No finding.

**(d) The "30 seconds" audit.** Confirmed clean. `grep` over the diff finds no `30`, no threshold, no
comparison against a stopwatch figure, in code or in log text. The packet's claim holds. ✔

---

## 1. Findings, most severe first

### F-1 — BLOCKER. The backstop route is a third observation path that bypasses the shared reporting lambda, so the one number T4 needs is never published
`NodeShuffleSubsystem.cpp:4876-4901`.
It increments `RocksHiddenNearWatched` and emits a `Verbose`+`bDiagHide` line, then `break`s. It does
**not** call `ReportMeshResolvedLater`, does **not** touch `LastDelaySeconds` / `MaxDelaySeconds` /
`ResolvedLaterTotal`, and does **not** remove the record from `Watch`. The handoff's claim that "one
`ReportMeshResolvedLater` lambda … so both observation routes produce identical numbers" is true of
the main loop and the watch sweep and silently omits the third route — which the live log shows is
the *only* one that will fire on this save.
**Failure scenario:** exactly the log block quoted in §0. `Delay worst=-1.0 s (-1 = no delay has ever
been measured this session)` printed at Display on a pass where the backstop measured ~30 s.
**Consequence:** a reader closes T4 as "nothing observable", which is the failure mode the governing
rule names verbatim. Fix in §6.1.

### F-2 — BLOCKER. The watch population is ~91% records that have no mesh actor at all, so `watch=N / resolvedLater=0` is inconclusive by construction and can never become conclusive
`NodeShuffleSubsystem.cpp:4716-4726`.
Enrolment predicate is "`FindMeshActorForNode` returned null". `MESHTYPE-CENSUS` measures 96 mesh
actors against 1087 streamed ordinary nodes. For ~991 of those the node's rock is not a separate
`AFGNodeMeshActor` at all — `SetActorHiddenInGame(true)` on the node handles it, and "the mesh actor
was unresolvable" means *absent*, not *lagging*. The packet conflates:
- **absent** (nothing can ever resolve; nothing can lag through this mechanism), with
- **assigned but not loaded / not paired** (the genuine lag candidate).
**Failure scenario:** watch list pins at ~584 forever, `resolvedLater` at 0, the summary re-prints its
1,250-character INCONCLUSIVE text on every travel pass, and the real lag population — if it is 3
records — is undetectable inside the noise. The instrument answers "inconclusive" on every possible
run. Fix in §6.2: split at enrolment on `Node->mMeshActor.IsNull()`.
*Access is available:* `Config/AccessTransformers.ini:7` grants
`Friend=(Class="AFGResourceNodeBase", FriendClass="ANodeShuffleSubsystem")`, and
`FGResourceNodeBase.h:311` declares `TSoftObjectPtr< class AActor > mMeshActor;` on the **Base**.
`SuppressOriginalNodes` is a member, so the read compiles. (The public `GetMeshActor()` at
`FGResourceNodeBase.h:225` returns `.Get()` and cannot distinguish the two cases — do not use it here.)

### F-3 — MAJOR. T3's "nearest ORDINARY (non-fracking) resource node" population includes RESOURCE DEPOSITS, which never carry a 650 cm use box
`NodeShuffleWellVisuals.cpp:172-176` — `Bystanders` is everything that fails `IsFrackingActor` over
`TActorIterator<AFGResourceNodeBase>`, and that iterator's own comment (`:164-166`) states it "visits
ordinary nodes, **deposits** AND fracking cores/satellites". Deposits are numerous, small, and are
explicitly excluded elsewhere in this mod as census noise (`NodeShuffleSubsystem.cpp:6571`,
`Cast<AFGResourceDeposit>` → skip).
**Failure scenario, and it is the masking direction:** a deposit sits 200 cm from a relocated well
member; a genuine `AFGResourceNode` sits 800 cm away and *also* overlaps. The line reports
`nearest ORDINARY … 200 cm … provableOverlap=1`. The tester flies out, finds a deposit, concludes
false alarm, and closes T3. The real overlapping node was never named and never will be — the scan
reports only the single nearest.
**Do not fix by filtering `Bystanders`** — that array is load-bearing for route 3's contest
(`:164-169` comment records the coal-node regression it exists to prevent). Publish a **second**
array from the same sweep. Fix in §6.3.

### F-4 — MAJOR. T3's staleness signal cannot be read from the line it is printed on, and the stale direction is the false-negative one
`NodeShuffleWellVisualsApply.cpp:239-241` prints `BystanderFromPass`, but not the *current* pass —
`EnsureWellMemberSnapBox` is `static` (`NodeShuffleSubsystem.h:1701`) and cannot read `WellAuditPasses`,
which is the whole reason the pass rides along. The reader is therefore told the snapshot's age
denominator only, never its numerator, and must hunt a matching `WELLH2B-INDEX` line elsewhere in a
110k-line log.
*Good news first:* the pass numbering **is** consistent — `WellAuditPasses` is bumped once per apply
pass at `NodeShuffleWellRelocateApply.cpp:426`, published at `NodeShuffleWellVisuals.cpp:187`, and
printed by the `WELLH2B-INDEX pass %d` summary at `:458/:479` from the same variable. No off-by-one.
(The `MESHTYPE-CENSUS` line at `Subsystem.cpp:7684` deliberately prints `WellAuditPasses,
WellAuditPasses + 1` and is a different, self-documented case.)
**Failure scenario:** `EnsureWellMeshIndex` is guarded `if (WellMeshIndexPass == WellAuditPasses) return;`
and is called from the suppression path (`NodeShuffleWellRelocateApply.cpp:284`). On a save/pass where
no relocated group is suppressed, the snapshot ages. The player travels; new ordinary nodes stream in;
the snapshot does not contain them; the nearest-node distance comes back **too large**;
`provableOverlap=0` prints next to a plausible-looking distance and a plausible-looking `nodes=1473`.
A false "no overlap" is the dangerous direction and is undetectable from the line.
**Verdict on the packet's claim that printing size+pass is sufficient:** it is sufficient to *explain*
a stale read after someone already suspects one; it is **not** sufficient to *detect* one. Fix in §6.4
(publish the current pass through the same namespace from `ApplyWellGroupVisuals`, which is a member —
`NodeShuffleWellVisualsApply.cpp:468`).

### F-5 — MODERATE. The cave-store zero can print a stated conclusion that is false
`NodeShuffleSubsystem.cpp:1841-1855`. The reading rule shipped in the line says *"0 over a 0-cell cave
store means no cave cell was ever available to draw."*
`EnsureCaveStoreLoaded()` is called from `GenerateNewLocations` at `:1954`, but that function
early-returns at `:1881` on `VanillaLocations.Num() == 0 || Count <= 0`, and the load itself is nested
inside `if (OutUndergroundIndices)` at `:1952`. With `Config.NewNodeCount == 0` the store is never
loaded on the roll path, `CaveFloors.Num()` is 0, and the census asserts a negative finding about data
that exists on disk (baked chunks + `Configs/NodeShuffle_CaveFloors.json`) and was simply not read.
**Failure scenario:** a user with new nodes disabled re-rolls, reads `cave store: 0 cell(s)`, and files
"cave placement is broken". *Numerator/denominator themselves are correct* — see §3.
Fix in §6.5 (wording, not a call — calling the loader here would be a behaviour change; see the
trade-off note).

### F-6 — MODERATE. The latency summary omits the constant most likely to explain the latency
`NodeShuffleSubsystem.cpp:4957-4981` prints `RockOwnRange` (800 cm) but not
`RockBackstopCooldownSeconds` (30 s, `:4833`) nor `NowSeconds - LastRockBackstopSeconds`. The standing
rule is that the log alone must explain what happened; here the mod's own 30-second gate is the
leading candidate cause of the observed wait and does not appear anywhere in the new diagnostics.
Not an asserted cause — printing the constant and the elapsed time is a measurement. Fix in §6.1
(folded into the same replacement line).

### F-7 — MINOR. A third state exists that the T3 legend does not cover
`nodes=0 / pass=-1` is documented as NOT MEASURED. `nodes=0 / pass>=1` — snapshot built, genuinely
empty — is a real state (a pass where no non-fracking node was streamed) and the legend does not name
it. A reader sees `-1` with a valid pass number and has no rule to apply. One clause fixes it (§6.4).

### F-8 — MINOR. Always-on log volume
`Watch.Num() > 0` will be permanently true (F-2), so the summary's outer guard is permanently open,
and the delta array includes `NodesHidden` and `RocksHiddenNearWatched` — both of which change on most
passes while the player travels. Expect the ~1,250-character line on the majority of 5 s passes during
travel, settling only when the player is stationary. Acceptable *if* F-2 is fixed (which shrinks the
watch list to the real population and makes the line rare); unacceptable if F-2 ships.

### F-9 — STYLE NOTE (no failure scenario). `NodeShuffleWellVisualsApply.cpp` 432 → **524**, newly over the 500-line rule
Reported honestly by the packet; a new T7 entry is owed. **The proposed seam is sound**: moving
`DumpWellActorCollision` + `ConfigureWellMeshCollision` + `EnsureWellMemberSnapBox` (`:128`, `:179`)
into `NodeShuffleWellSnapBox.cpp` leaves `DressWellActor` + `ApplyWellGroupVisuals` (`:468`) behind
cleanly — verified against the actual definition boundaries. **This diff makes the seam slightly
harder in exactly one way**: the `NodeShuffleWellSnapBoxDiag` namespace must travel *with*
`EnsureWellMemberSnapBox` to the new file, and `NodeShuffleWellVisuals.cpp`'s forward declaration
(`:127-136`) must then be left pointing at the new TU. Add that sentence to the T7 entry. My §6.4 fix
adds one more accessor to the same namespace, so it moves as one unit.
**Is +285 lines proportionate for two diagnostics in `Subsystem.cpp`?** Roughly 60 lines are code and
~225 are comment + in-line log legend. That is this project's established (and, given its history,
justified) style, and the log legends are the reason the lines are self-explaining. Proportionate —
but F-1/F-2 mean ~40 of those 225 legend lines currently document behaviour the code does not have,
which is worse than terse.

---

## 2. Differential / parity table

Two established paths are touched. Grades follow the house cap: nothing whose mechanism lives in
engine/toolchain code is graded "provably provided".

### 2a. `EnsureWellMemberSnapBox` — measurement block inserted between the dedup key and the two branches

| Invariant the OLD path guaranteed | How the NEW path provides it | Grade |
|---|---|---|
| Early-outs (`!IsValid(Actor)`, `!Base \|\| !Root`, `Pieces == 0`) still return before any work | New block sits *after* all three (`:179-208`); read and confirmed | **provably provided** |
| The box actually created / re-pointed is unchanged | `FinalExtent` is a read-only derived local; no write to `Existing`, `WantX/Y/Z`, or `mBoxComponent` from the new code | **provably provided** |
| `bClamped` still computed from the final extent (the ns-review-h2c F-1 fix) | Untouched, computed at `:236` before the new block | **provably provided** |
| A member logs at most once per distinct state | `SnapBoxLogged` key gained a 6th field (`BystanderCount > 0`). Deliberate: a member first dressed before any snapshot re-fires exactly once. Contents excluded from the key — correct, they churn per stream event | **provably provided** |
| No new per-member world sweep | Scan is `O(bystanders)` and guarded on `bSayIt && BystanderCount > 0` — verified at `:245` | **provably provided** |
| The snapshot read is never garbage / never read before written | Function-local statics: zero-init `TArray`, `Pass = -1`; magic-static init is thread-safe; empty array ⇒ measurement skipped ⇒ prints NOT MEASURED | **provably provided** |
| The snapshot reflects the world at the time of the box decision | It does not — see **F-4**. Age is unbounded when `EnsureWellMeshIndex` does not run, and the stale direction produces a false "no overlap" | **assumed** → runtime step R-4 |
| The population compared against is nodes that carry a 650 cm use box | It is not — deposits are included, see **F-3** | **assumed** → runtime step R-3 |
| Writer and reader are both on the game thread | No `AsyncTask` / `ParallelFor` / `FRunnable` anywhere in `Source/NodeShuffle/Private/` (grepped; the single `ParallelFor` hit at `Subsystem.cpp:7075` is prose in a comment). Entry point is an engine-scheduled apply pass | **assumed** (engine schedules the caller) → runtime step R-5 |
| Unity-build linkage is legal in either concatenation order | Named namespace at global scope in both TUs (declaration at `WellVisuals.cpp:127-136` is *outside* the anonymous namespace closed on the preceding line — checked); definition-then-redeclaration and declaration-then-definition are both well-formed | **provably provided** |
| No new import surface | `FMath::Atan2` and `FRotator::NormalizeAxis` are **new to this module** (grepped: not previously used anywhere in `Private/`). Both are header-inline, but "inline ⇒ no import" has been falsified on this machine before | **assumed** → runtime step R-1 (`dumpbin`) |

### 2b. `SuppressOriginalNodes` — mesh-hide `if` rewritten as explicit `if/else`

| Invariant the OLD path guaranteed | How the NEW path provides it | Grade |
|---|---|---|
| `FindMeshActorForNode` called exactly once per hidden node | `AFGNodeMeshActor* MeshActor = FindMeshActorForNode(Node); if (MeshActor)` — one call, same position | **provably provided** |
| `SetActorHiddenInGame(true)` then `SetActorEnableCollision(false)`, same order, only when resolved | Unchanged inside the `if` (`:4705-4707`) | **provably provided** |
| Nothing else is hidden | Watch sweep (`:4791-4804`) does `Find*` + `IsHidden()` reads only; backstop insert (`:4883-4901`) reads `Watch` and logs. No `SetActorHiddenInGame` / `SetVisibility` added anywhere | **provably provided** |
| No iterator invalidation | Main loop: `Find` → call → `Remove` (not iterating `Watch`). Sweep: collects keys, removes after the loop. Backstop: range-for + `break`, no mutation | **provably provided** |
| `SteadyHiddenOriginals` / `ScannerDeregistered` / `bChanged` / `NodesHidden` semantics | Untouched; the `else` adds only map bookkeeping | **provably provided** |
| Per-pass cost stays bounded | Watch sweep is `O(watched)` × 2 map lookups. **On the live save that is ~584 `FString`-keyed lookups + 584 pointer lookups every 5 s, and it will NOT shrink** (the handoff's "shrinks monotonically" prediction is contradicted by the flat 6% pairing coverage). Still cheap in absolute terms (~1,200 lookups / 5 s, dwarfed by the `TObjectIterator<UStaticMeshComponent>` backstop already in this function) | **assumed** → runtime step R-6 |
| The delay reported is the delay that happened | **No** — see F-1. The backstop route measures a delay and does not publish it | **NOT provided** — blocker |
| The watch list is "the population whose rock can lag" | **No** — see F-2. It is "records with no separate mesh actor" ∪ "records not paired this pass", ~9:1 in favour of the inert class | **NOT provided** — blocker |
| A record whose *node* re-streams is re-enrolled | `:4647-4655` requires `Steady->Get() == Node && Node->IsHidden()`; a new instance breaks the match and re-runs the funnel | **provably provided** |
| A record whose *mesh actor alone* re-streams is observed | **No** — structurally blind (§0(a)) | **assumed reachable, unproven** → runtime step R-7 |
| World change resets timestamps | `WatchWorld != MeshHideWorld` resets all seven statics + the delta array. `const void*` vs `const UWorld*` comparison and assignment are standard conversions | **provably provided** |

---

## 3. Can each of the four numbers fire, and is each zero interpretable?

| # | number | file:line | world state that makes it non-zero | denominator on the SAME line | verdict |
|---|---|---|---|---|---|
| 1 | `nearest ORDINARY … cm` | `WellVisualsApply.cpp:296/355` | any non-fracking `AFGResourceNodeBase` streamed when `RebuildWellMeshIndex` last ran (log attests 1473) | `snapshot of %d such node(s) … pass %d` | **fires; zero interpretable** but population is wrong (F-3) and staleness undetectable (F-4) |
| 2 | `provableOverlap=1` | same | a member's final box + 650/180 overlapping on all three axes | distance + per-axis deltas on the line | **can fire; a 0 is readable**. Per-axis AABB (not the scalar shorthand) is the right test — confirmed correct at `:257-259` |
| 3 | `%d did NOT [resolve]` | `Subsystem.cpp:4957` | a hidden original with no cache entry at hide time | `hid %d … of those %d had … %d did NOT`, same loop iteration | **fires, ~584** — but see F-2: it measures absence, not lag |
| 4 | `WATCH LIST %d still open` | same | ≥1 record took the else branch | is itself #5's denominator | **fires, ~584, and never falls** |
| 5 | `RESOLVED LATER … Delay last/worst` | same | a watched record's cache entry appears later | watch list + `%d still had a VISIBLE mesh actor` | **will read 0 / -1 permanently on the live save — F-1 + F-2. This is the number T4 exists for.** |
| 6 | `STRAY-ROCK BACKSTOP: %d` | same | backstop hides a rock within 800 cm of a watched record | `within %.0f cm of a watched record` + watch list | **fires** (30 backstop events already in the log) — but contributes **no delay** to the line (F-1) |
| 7 | `undergroundActiveNewNodes=%d of %d` | `Subsystem.cpp:1829` | roll draws ≥1 cave cell into an active entry | `of %d active new-location entries` + `cave store: %d cell(s), %d seed(s)` | **fires** — see the shared-loop check below |
| 8 | `HERE: nearest relocated well` | `Subsystem.cpp:6637` | ≥1 group with `bGroupPlaced && bPlacementClaimLive && !PlacedCoreLocation.IsNearlyZero()` | `%d of %d well group(s) … (%d flagged)`; zero placed prints a sentence | **fires; both zero shapes distinct** ✔ |

**Item 3's numerator/denominator share a predicate and a loop — VERIFIED, not taken on trust.**
`CountUndergroundEntries()` (`Subsystem.cpp:5824-5832`) is literally
`if (E.bIsNewNode && E.bActive && E.bUnderground) { N++; }` over `Layout`.
The denominator at `:1796` is `if (!E.bActive) {…continue;} ++ActiveEntries; if (E.bIsNewNode) { ++ActiveNewNodeEntries; }`
over the same `Layout`, in `EmitRollCensus`'s own loop. Same container, same two guards, denominator =
numerator's predicate minus `bUnderground`. **Provable superset. The claim holds.** Two loops, not one
— but over the same immutable-during-the-call container in a `const` method, so they cannot disagree.
The only defect is the cave-store *context* number (F-5), not the ratio.

**Is `-1 / nodes=0` genuinely distinguishable from "measured, nothing nearby"?** Yes for the two states
the legend names: the scan is guarded only on `bSayIt && BystanderCount > 0`, and when it runs it
*always* writes a real distance — there is no path where `nodes>0` leaves the distance at -1. The gap
is the third state (F-7).

---

## 4. Compile-as-a-compiler — independent re-verification

I re-ran the format audit with **my own parser** (`…\scratchpad\fmt_audit.py`), not the packet's:
brace/quote-aware `UE_LOG(...)` extraction, top-level comma split, `%`-specifier count with `%%`
stripped, plus a ternary-in-verbosity check.

**All 15 `UE_LOG` sites in the three files, and both orchestrator files, match. Independently confirmed:**

| site | specs/args | specifier string |
|---|---|---|
| `WellVisualsApply.cpp:296` (NO-OP + T3 tail) | 15/15 | `ssdffsssffffddd` |
| `WellVisualsApply.cpp:355` (re-point + T3 tail) | 18/18 | `ssdffdsssssffffddd` |
| `Subsystem.cpp:1829` (ROLLCENSUS + cave) | 21/21 | `dddddddddddddddddddds` |
| `Subsystem.cpp:4582` (`record=`) | 6/6 | `sddfdd` |
| `Subsystem.cpp:4889` (`backstop:`) | 6/6 | `sfdssf` |
| `Subsystem.cpp:4957` (`hide-pass` summary) | 13/13 | `dddddddddffdf` |
| `Subsystem.cpp:6621` (`— NONE`) | 3/3 | `ddd` |
| `Subsystem.cpp:6637` (nearest well) | 13/13 | `sssfffffddddd` |
| `Subsystem.cpp:4930` (untouched funnel control) | 8/8 | `dddddddd` |

I then **hand-walked the type order** for the four highest-arity sites (`:296`, `:355`, `:4957`,
`:6637`), argument by argument against the specifier sequence. All align. Notes:
- `RockOwnRange` is `constexpr float` (`:4527`) passed through varargs → promotes to `double`, `%.0f` ✔.
- `LastDelaySeconds` / `MaxDelaySeconds` / `Delay` are `float` → `double`, `%.1f` ✔.
- Every T3 scalar is `double` (`FVector::DistSquared`, `FMath::Sqrt` → `FVector::FReal` = double) ✔.
- `FVector::DistSquared2D` returns `double`; `NearestWellD2` is `double` ✔.
- `Turn`: `FMath::Atan2(double,double)` → `RadiansToDegrees` → minus `FRotator::Yaw` (double under LWC)
  → `FRotator::NormalizeAxis` → `double`, `%+.0f` ✔.
- `*FString(...)` temporaries live to the end of the full expression ✔ (established idiom in this file).

**No ternary in any verbosity argument** — every new call passes a bare `Display` / `Verbose`. ✔

**Other compile-relevant checks, all pass:**
- `bDiagHide` declared `:4537`, **before** the lambda at `:4560` — capture is valid.
- `ResolvedLaterThisPass`, `StillVisibleThisPass`, `MeshHideNow`, `MeshHidePass` all declared before the lambda.
- `NodeShuffleWellSnapBoxDiag` declaration in `WellVisuals.cpp:127-136` is at **global** scope (the
  preceding `}` closes the anonymous namespace) — the anonymous-namespace link error is avoided ✔.
- `CountUndergroundEntries()` is `const` (`NodeShuffleSubsystem.h:1049`); `EmitRollCensus` is `const`
  (`.h:547-549`); `CaveSeedCount` is `mutable int32` (`.h:1029`) — reads in a `const` method compile ✔.
- `LogHereCensus` is `const`; `P`, `Pawn` (null-checked with early return at `:6469`) and the
  `ShortName` lambda (`:6538`) are all in scope at `:6596` ✔.
- All eight `FNodeShuffleWellEntry` fields used exist: `CorePath` `.h:286`, `AssignedResourceClassPath`
  `.h:300`, `Satellites` `.h:302`, `bRelocate` `.h:326`, `CapturedSatelliteCount` `.h:351`,
  `PlacedCoreLocation` `.h:359`, `bGroupPlaced` `.h:374`, `bPlacementClaimLive` `.h:436` ✔.
- `mMeshActor` is on `AFGResourceNodeBase` (`FGResourceNodeBase.h:311`) and friendship is granted
  (`Config/AccessTransformers.ini:7`) — relevant to the §6.2 fix ✔.

**Orchestrator files (out of scope, checked for outright compile errors only):**
`NodeShuffleConfig.cpp` — clean. `NodeShuffleWellRetype.cpp` — my parser flags `:291` as 7 specs /
10 args; **this is a false positive in my tooling**, caused by three commas inside `//` comments
placed between arguments (`// n/a, never 1, when no satellite has streamed: … no verdict,`). Hand-count
is 7/7 and the nested ternary is fully parenthesised. **No compile error found in either file.**

**Import surface: UNVERIFIABLE STATICALLY — settle by `dumpbin`.** `FMath::Atan2` and
`FRotator::NormalizeAxis` are new to this module (not present anywhere in `Source/NodeShuffle/Private/`
before this diff). Both are header-inline in UE, and the CRT `atan2` is the only plausible new symbol —
but per the standing rule and the 2026-07-26 precedent, "inline ⇒ no import" is not a conclusion this
review is allowed to draw. R-1 below.

---

## 5. Performance on a 1000+ node world

| path | added cost | assessment |
|---|---|---|
| `RebuildWellMeshIndex` (1×/apply pass) | one `TArray<FVector>` copy | ~1,500 × 24 B ≈ 36 KB/pass, inside a `TActorIterator<AFGResourceNodeBase>` sweep that dwarfs it. **Fine.** |
| `EnsureWellMemberSnapBox` (per dressed member per pass) | `.Num()` + one extra `%d` in an existing `Printf`; the `O(bystanders)` scan **only when the line prints** | Gating **verified** at `:245`: `if (bSayIt && BystanderCount > 0)`. `bSayIt` is false after the first print for a given key. ~100 members × 1,500 `DistSquared` ≈ 150 k double ops **for the whole session**. **Claim holds.** |
| hide site | 2 `int32` increments + one `TMap::Contains`/`Find` per newly-hidden node | 658 on load pass 1, ~0 after. **Fine.** |
| **watch sweep** | 2 lookups per watched record **per pass, forever** | ~584 `FString`-hash lookups + 584 pointer lookups every 5 s. Handoff predicted this shrinks; **the log says it will not** (6% pairing coverage, flat). Absolute cost is still small next to the `TObjectIterator<UStaticMeshComponent>` in the same function. **Acceptable, but only because F-2's fix removes ~91% of it.** |
| backstop insert | `O(watched)` per rock actually hidden, `break` on first match | ~584 × 1–8 rocks on a backstop pass ≈ ≤4,700 `DistSquared2D` — again shrinks by ~10× with F-2. **Fine.** |
| `EmitRollCensus` | one `if` in an existing loop + one extra `Layout` pass | Rolls are rare. **Fine.** |
| `LogHereCensus` | one pass over ~22 entries | Console only. **Fine.** |
| log volume | see F-8 | **Only acceptable with F-2 fixed.** |

**Item 2 adds no per-frame work and no new world sweep.** Confirmed.

---

## 6. Verbatim fixes (reviewer-authored; apply as written)

Precedent in this session: verbatim reviewer specs land clean, self-authored responses to a finding
have introduced a fresh bug every time. These are written to be applied literally.

### 6.1 — F-1 + F-6: route the backstop through the shared lambda, and print the cooldown

**(a)** Change the lambda's signature so it can record where the observation came from. Replace
`NodeShuffleSubsystem.cpp:4560-4561`:

```cpp
    const auto ReportMeshResolvedLater =
        [&](const FString& Path, const NodeShuffleMeshHideLatency::FWatch& W, bool bMeshVisibleNow) -> void
```

with:

```cpp
    // `Route`: "cache" = its AFGNodeMeshActor became resolvable (main loop or watch sweep);
    // "backstop" = the stray-rock backstop hid a rock at this record's node location. BOTH are
    // observations of "this rock went dark LATER than its node", which is the quantity T4 needs, so
    // both MUST feed the same delay fields -- a delay measured on one route and reported as -1 on the
    // other is the "zero with no denominator" defect this packet exists to avoid.
    const auto ReportMeshResolvedLater =
        [&](const FString& Path, const NodeShuffleMeshHideLatency::FWatch& W, bool bMeshVisibleNow,
            const TCHAR* Route) -> void
```

and inside it, replace the `UE_LOG(...)` first two lines and its argument list so the route is printed
(add `TEXT("[route=%s] ")` at the START of the format string and `Route` as the FIRST argument — the
site is `:4576-4587`, currently `6/6`, becoming `7/7` with specifier string `ssddfdd`).

**(b)** Update the two existing call sites to pass the route:
- `:4712` → `ReportMeshResolvedLater(Rec.VanillaNodePath, *W, bMeshWasVisible, TEXT("cache"));`
- `:4800` → `ReportMeshResolvedLater(Pair.Key, Pair.Value, !WatchedMesh->IsHidden(), TEXT("cache"));`

**(c)** Replace the whole backstop insert (`:4876-4901`) with:

```cpp
            // T4, THE SECOND OBSERVATION ROUTE. On this save it is the ONLY one that fires: the
            // mesh-actor cache pairs ~6% of streamed ordinary nodes, so a watched record's
            // AFGNodeMeshActor mostly never becomes resolvable and the watch sweep never reports.
            // The rock still goes dark -- here, via the backstop -- and that delay IS the number T4
            // has never had. It therefore feeds the SAME delay fields as the cache route.
            // MEASURED: both timestamps and the 2-D distance. NOT MEASURED / NOT CLAIMED: that this
            // rock belongs to that record -- the backstop matches on proximity alone, and so does this.
            FString BackstopHitKey;
            for (const TPair<FString, NodeShuffleMeshHideLatency::FWatch>& WPair : NodeShuffleMeshHideLatency::Watch)
            {
                if (FVector::DistSquared2D(WPair.Value.NodeLoc, Loc) >= FMath::Square(RockOwnRange)) { continue; }
                RocksHiddenNearWatched++;
                ReportMeshResolvedLater(WPair.Key, WPair.Value, /*bMeshVisibleNow=*/true, TEXT("backstop"));
                BackstopHitKey = WPair.Key;
                break;
            }
            // Removed AFTER the range-for, never during it.
            if (!BackstopHitKey.IsEmpty()) { NodeShuffleMeshHideLatency::Watch.Remove(BackstopHitKey); }
```

*(`bMeshVisibleNow=true` is correct and not an assumption: the backstop's own gate at `:4845` is
`!Smc->IsVisible() → continue`, so this component was visibly drawn at this instant.)*

**(d)** In the summary at `:4957`, append two fields. Add to the end of the format string (before the
closing `"`):

```
TEXT(" BACKSTOP CADENCE: the stray-rock backstop runs on any pass that newly hid a node, else on a ")
TEXT("%.0f s cooldown; %.1f s have elapsed since it last ran. That cooldown is an UPPER BOUND this ")
TEXT("mod itself imposes on how late a rock can go dark -- it is a measured constant from this run, ")
TEXT("not an explanation of any particular delay above."),
```

and append the two arguments after `RockOwnRange`:

```cpp
                RockBackstopCooldownSeconds, NowSeconds - LastRockBackstopSeconds,
```

**This requires moving the summary block from `:4935` to AFTER `:4910`** (it must be below the backstop
so `NowSeconds`, `LastRockBackstopSeconds`, `RockBackstopCooldownSeconds` and this pass's
`RocksHiddenNearWatched` are all in scope and final). It is already below the backstop at `:4935` —
**verify `RockBackstopCooldownSeconds` and `NowSeconds` are still in scope there**; both are declared at
`:4833-4834` at function scope, so they are. No move needed. Final arity: `13 → 15`, specifier string
`dddddddddffdfff`.

### 6.2 — F-2: split the watch enrolment on whether a mesh actor was ever assigned

Replace `NodeShuffleSubsystem.cpp:4716-4727` (the `else` branch) with:

```cpp
                else
                {
                    // T4, THE POPULATION SPLIT. "FindMeshActorForNode returned null" is TWO different
                    // worlds and only one of them can lag:
                    //   mMeshActor.IsNull()  -> this node was never AUTHORED a separate mesh actor. Its
                    //      rock (if any) is not an AFGNodeMeshActor, hiding the node actor is the whole
                    //      story here, and nothing will EVER resolve. Enrolling these drowns the real
                    //      population ~9:1 on the live save (96 mesh actors vs 1087 streamed ordinary
                    //      nodes, MESHTYPE-CENSUS 2026-08-08) and pins the watch list open forever.
                    //   !IsNull() but unresolved -> a mesh actor IS assigned and is not loaded/paired
                    //      right now. THIS is the set whose rock can go dark later.
                    // Friend access to the private soft pointer: Config/AccessTransformers.ini grants
                    // ANodeShuffleSubsystem friendship on AFGResourceNodeBase. GetMeshActor() is NOT a
                    // substitute -- it returns .Get(), which is null in both worlds.
                    MeshUnresolvedAtHide++;
                    if (Node->mMeshActor.IsNull())
                    {
                        MeshActorNeverAssigned++;
                    }
                    else if (!NodeShuffleMeshHideLatency::Watch.Contains(Rec.VanillaNodePath))
                    {
                        NodeShuffleMeshHideLatency::FWatch W;
                        W.NodeLoc = NodeLoc;
                        W.HideTimeSeconds = MeshHideNow;
                        W.HidePass = MeshHidePass;
                        NodeShuffleMeshHideLatency::Watch.Add(Rec.VanillaNodePath, W);
                    }
                }
```

Declare `MeshActorNeverAssigned` next to the other per-pass counters at `:4553`, add it to the
`Summary[]` delta array (size `7 → 8`, and the two `for (int32 i = 0; i < 7; i++)` loops and the
`LastSummary[7]` declaration all become `8`), and add to the summary format string, immediately after
the `%d did NOT` clause:

```
TEXT("Of the %d that did NOT, %d had NO mesh actor assigned at all (mMeshActor is null -- the node's ")
TEXT("rock is not a separate AFGNodeMeshActor, nothing can lag through this mechanism, and these are ")
TEXT("NOT watched); the remainder have one assigned but unresolved and ARE watched. ")
```

with arguments `MeshUnresolvedAtHide, MeshActorNeverAssigned,` inserted in order.

**Blocker-clearing effect:** the watch list becomes the population the line claims it is, `watch=0`
becomes a real finding ("every node that could lag paired at hide time"), and F-8's log volume and the
watch sweep's cost both drop ~10×.

### 6.3 — F-3: a second, correctly-typed bystander array

In `NodeShuffleWellVisualsApply.cpp:87-92`, add a third accessor to the namespace:

```cpp
    // The T3 hazard population, NARROWER than Bystanders. Bystanders is route 3's contest set and
    // includes AFGResourceDeposit, which never carries EnsureNodeUseBox's 650 cm box -- a deposit
    // 200 cm away would report an overlap that no Miner can ever be blocked by, while a REAL node
    // 800 cm away that DOES overlap is never named (the scan reports only the nearest). Do not
    // filter Bystanders itself: it is load-bearing for the contest.
    TArray<FVector>& UseBoxNodeLocations();
```

Mirror the declaration in `NodeShuffleWellVisuals.cpp:127-136`, and in `RebuildWellMeshIndex` change
the sweep body (`:172-176`) to fill both, then publish both:

```cpp
        if (IsFrackingActor(N)) { Members.Add({ N->GetActorLocation(), N, WellPathOf(N) }); }
        else
        {
            Bystanders.Add(N->GetActorLocation());
            // AFGResourceNode is the type EnsureNodeUseBox takes; deposits are AFGResourceNodeBase
            // but never AFGResourceNode, so this Cast IS the "carries a 650 cm box" filter.
            if (Cast<AFGResourceNode>(N)) { UseBoxNodes.Add(N->GetActorLocation()); }
        }
```

Read `UseBoxNodeLocations()` (not `BystanderLocations()`) in `EnsureWellMemberSnapBox:242`, and change
the log wording from `nearest ORDINARY (non-fracking) resource node` to
`nearest ordinary MINEABLE node (AFGResourceNode; DEPOSITS EXCLUDED -- only this class ever carries
EnsureNodeUseBox's 650 cm box)`.

### 6.4 — F-4 + F-7: make staleness readable from the line itself

Add a fourth accessor to `NodeShuffleWellSnapBoxDiag`:

```cpp
    // The pass number AS OF THE CALL, so a reader can compute snapshot AGE from one line instead of
    // hunting a matching WELLH2B-INDEX line in a 110k-line log. Written by ApplyWellGroupVisuals,
    // which is a member and can see WellAuditPasses; EnsureWellMemberSnapBox is static and cannot.
    int32& CurrentAuditPass();
```

definition `{ static int32 Cur = -1; return Cur; }`, and write it as the first statement of
`ANodeShuffleSubsystem::ApplyWellGroupVisuals` (`NodeShuffleWellVisualsApply.cpp:468`):

```cpp
    NodeShuffleWellSnapBoxDiag::CurrentAuditPass() = WellAuditPasses;
```

Then in both T3 log tails, replace `taken on WELLH2B-INDEX pass %d` with
`taken on WELLH2B-INDEX pass %d, and it is now pass %d (age = %d pass(es); a snapshot older than 0
passes may MISS nodes that streamed in since, which reports the distance TOO LARGE and can print a
FALSE provableOverlap=0)`, adding `NodeShuffleWellSnapBoxDiag::CurrentAuditPass()` and the difference
as arguments. Arity `15 → 17` and `18 → 20`.

Same edit, F-7: extend the NOT MEASURED legend clause to
`A -1 distance with nodes=0 / pass=-1 means NOT MEASURED (RebuildWellMeshIndex had not run in this
process yet). A -1 distance with nodes=0 and a pass >= 0 means the snapshot WAS built and contained
no candidate at all -- also not a measurement of "nothing nearby", just an empty population. Neither
means nothing is nearby.`

### 6.5 — F-5: correct the cave-store reading rule (do not add the loader call)

In `NodeShuffleSubsystem.cpp:1846-1848`, replace

```
READING A ZERO: 0 over a 0-cell cave store means no cave cell was ever available to draw;
```

with

```
READING A ZERO: 0 over a 0-cell cave store means no cave cell was LOADED at census time -- which is
either "none exist" or "EnsureCaveStoreLoaded had not run yet", and this line CANNOT tell them
apart (GenerateNewLocations early-returns before the load when NewNodeCount is 0). Type
NodeShuffle.Here to force the load and read its cave-store line to separate them;
```

**Trade-off, stated because the obvious fix is the wrong one:** calling `EnsureCaveStoreLoaded()` here
would make the number a true measurement, and it compiles (`const`, and `CaveFloors`/`CaveSeedCount`
are `mutable`). **Do not do it in this packet.** It would populate `CaveFloors` on a code path where it
was previously empty, and `ExpandCaveFloorsBudgeted` (`:5596`) early-returns on `CaveFloors.Num() == 0`
— so the "no behaviour change" property of this packet would be lost, and the mod would start doing
line traces on a config where it previously did none. Wording now; the loader call as its own packet.

---

## 7. Alternatives, with trade-offs and a recommendation

### For T4 (item 2) — how to measure mesh-hide latency

| | approach | cost | verdict |
|---|---|---|---|
| **A** | **as built** — watch map keyed on "cache miss at hide time", reported by a later cache hit | O(watched)/pass | **Broken on the live save** (F-1, F-2). Salvageable, and cheapest, once §6.1+§6.2 land. |
| **B** | Instrument the **stray-rock backstop as the primary route**: it is empirically the mechanism that hides these rocks (30 log events), it already knows the rock, the location and the pass, and it needs no pairing to work | ~zero new — the backstop already runs | **This is what A becomes after §6.1.** The reframe matters: the honest headline is "the backstop is the rock-hiding path; how late is it?", not "when does the pairing repair?" |
| **C** | Per-pass sweep re-testing **every** steady record's rock visibility | O(658)/pass + a visibility query each | Rejected by the packet, correctly — but it is the only approach that catches the mesh-actor-alone-re-streams blind spot (§0(a)). Keep as the fallback **if R-7 shows that case is real.** |
| **D** | A `NodeShuffle.DumpMeshHideLatency` console command dumping the watch map on demand | zero hot-path | Good complement, needs a registration site outside the owned files. **Recommend as a follow-up packet**, matching T6's `DumpWellBackstop` shape. |
| **E** | Do nothing; ask the user to time it again with a stopwatch | zero | Rejected: it produces another estimate, which is what the ⚠ constraint exists to prevent. |

**Recommendation: A + B (i.e. apply §6.1 and §6.2 to the existing code) now; D as a follow-up; hold C
in reserve behind R-7.** A alone is not shippable; A with the backstop folded in is the cheapest thing
that can actually answer T4 on this save.

### For T3 (item 1) — how to get the nearest-node distance into a static member

The packet's own A/B/C/D table is sound and I reach the same recommendation, with one correction and
one addition:

| | approach | verdict |
|---|---|---|
| **A** (built) | cross-TU snapshot from the index sweep | Keep. Correct call given the barred header. **Corrected by §6.3 (wrong population) and §6.4 (unreadable staleness).** |
| **B** | `TActorIterator` per member per pass | Correctly rejected. |
| **C** | a `TArray<FVector>` member on the subsystem | Correct end state. **Do it when the header next opens** and delete the namespace — and note that C alone does **not** fix F-3 or F-4; those are population and freshness bugs that survive the refactor. |
| **D** | `NodeShuffle.DumpSnapBoxOverlap` console command | Recommend as a follow-up. |
| **E** *(new)* | Have `RebuildWellMeshIndex` compute the nearest-`AFGResourceNode` distance **for every indexed well member** during its existing sweep, and publish a `TMap<FString, double>` keyed by member path | Strictly fresher than A (computed in the same sweep, zero staleness window) and eliminates F-4 entirely; costs one O(members × useBoxNodes) pass per index rebuild (~100 × 1500 = 150k ops/pass — 5× the *session* cost of A, per pass). **Rejected on cost, but it is the right answer if F-4 ever bites.** |

**Recommendation: keep A, apply §6.3 + §6.4, do C at the next header opening, D as a follow-up, E only
if a stale-snapshot false negative is ever observed.**

### For items 3 and 4
No alternative is warranted. Item 4 (`HERE: nearest relocated well`) is the cleanest thing in this
diff — invariant A3 asserted rather than trusted, a sentence instead of a fake distance at zero, and
turn-from-facing instead of an unverifiable compass word. **Ship it as written.** Item 3 needs only the
§6.5 wording change.

---

## 8. The two module-static blocks — safe interim, or latent trap?

**Safe interim, with two conditions, and one of them is not currently met.**

- `NodeShuffleWellSnapBoxDiag` — *named* namespace, external linkage, function-local statics of plain
  value types, no `UObject`, no GC interaction, no static-init-order dependency (magic statics),
  defined once and declared once. Legal in either unity concatenation order. **Safe.**
- `NodeShuffleMeshHideLatency` — `static` members, internal linkage, single TU, distinctively prefixed
  so a unity blob cannot collide. Reset on world change. **Safe.**

**The trap is not linkage, it is lifetime across worlds.** `NodeShuffleWellSnapBoxDiag` has **no
world-change reset**, unlike its sibling. Load save A, travel, load save B in the same process: the
bystander array holds save A's locations and `BystanderPass()` holds save A's pass number until the
first `RebuildWellMeshIndex` of save B. `WellAuditPasses` is a member and resets with the subsystem, so
the stale pass would read *higher* than the new world's current pass — which, after §6.4, makes the age
negative and visibly wrong. **Without §6.4 it is silently wrong.** Add the same world guard, or accept
it as covered by §6.4's age field; I recommend the latter (cheaper, and the negative age is a louder
signal than a reset).

**The missing condition: the TODO is not at the surface that would be read.** The handoff records
`// TODO 2026-08-08 (ns-t1t2-instrument): promote to a member when the header is next open.` and then
states it was not added anywhere, because the header is barred. But the TODO belongs at the *namespace
definitions*, which are in files the packet **does** own — `NodeShuffleWellVisualsApply.cpp:74` and
`NodeShuffleSubsystem.cpp:4460`. **Add the dated TODO at both, verbatim as written above.** A note that
lives only in a `_team/` handoff is a note nobody opening the header will see, and "stopgap constants
ship" is this workspace's most-repeated lesson.

---

## 9. Standing-rule checks

| rule | result |
|---|---|
| No asserted causes in new log text | **PASS.** Every new line names MEASURED and NOT MEASURED explicitly. The `record=` line says "NOT MEASURED: why the pairing was missing"; the backstop line disclaims ownership; the T3 tail disclaims that the node carries the box. Best-in-class for this repo. |
| No borrowed constants | **PASS.** 650/650/180 named to `NodeShuffleSubsystem.cpp:3312/:3331`; 900 named to H0's measured 1818.8 cm; 1818.8 is re-stated, not re-derived. **The user's "30 seconds" appears nowhere** — verified by grep over the diff. |
| Stopgap constants get a dated TODO at every reading surface | **FAIL** — §8. Two module-static blocks are explicit placeholders with no in-source dated TODO. |
| Diagnostics on every new path, gated | **PASS with one gap** — every branch logs, alternatives are logged (which route ran, why a guard returned early). Gap: the backstop route's delay is Verbose-only (F-1). |
| Opposite-polarity test pair for split-owner work | **N/A, correctly.** Nothing here is half-applied across two owners; all four measurements are complete in the three owned files. The handoff's §8 is accurate. |
| Files under 500 lines | **FAIL** — `NodeShuffleWellVisualsApply.cpp` 524 (F-9). Reported honestly; T7 entry owed. |
| Input validated at boundaries | **PASS.** `IsValid` guards, null-pawn early return, empty-array guard, world-pointer guard. |
| No secrets / `.env` in diff | **PASS.** |
| Deployed binary must match committed | **N/A** — nothing built. |

---

## 10. Runtime test checklist

Every item graded **assumed** or **unverifiable statically** above, as a numbered step. Build with
NodeShuffle diagnostics **enabled**; R-3/R-5/R-7 additionally want `Log LogNodeShuffle Verbose`.

- **R-1 — Import table.** `check_imports.ps1` / `dumpbin /imports` on the built `NodeShuffle` DLL,
  diffed against the pre-change baseline. Cross-check one known-good symbol to prove you queried the
  right DLL path (`Satisfactory\Engine\Binaries\Win64` for Core/Engine). **Watch specifically for
  `atan2`** — `FMath::Atan2` and `FRotator::NormalizeAxis` are new to this module. PASS = zero new
  symbols. *(Note: `NodeShuffleConfig.cpp` and `NodeShuffleWellRetype.cpp` are dirty from the
  orchestrator, so a non-zero diff is not attributable to this packet alone.)*
- **R-2 — Format strings at runtime.** MSVC does not emit `C4477` for `UE_LOG`; a clean build proves
  nothing. Boot once and read every new line for garbage/`%s`-on-an-int corruption. Grep tokens:
  `WELLH2C-SNAPBOX`, `MESHHIDE-LATENCY`, `CAVE PLACEMENT:`, `HERE: nearest relocated well`.
- **R-3 — T3 population is the right one (F-3).** `grep "WELLH2C-SNAPBOX"`. For every
  `provableOverlap=1`, fly to the reported coordinates and identify what is actually there.
  **FAIL = it is a resource DEPOSIT** — confirms F-3 and means §6.3 was needed.
  **FINDING (what T3 exists for) = it is a mineable node; try a Miner Mk1 on it.**
  **INCONCLUSIVE, never a pass = `nodes=0 / pass=-1` on every line** (snapshot never built).
- **R-4 — T3 snapshot staleness (F-4).** After §6.4, read the `age = N pass(es)` field across a travel
  session. PASS = age stays 0–1. **FINDING = any age > 1 next to `provableOverlap=0`** — that verdict
  is unsafe and must be re-measured. Without §6.4 this step cannot be executed at all; that is the
  finding.
- **R-5 — Game-thread affinity.** Boot with `-stompmalloc` or a debug build if available and watch for
  any `check(IsInGameThread())` ensure around `RebuildWellMeshIndex` / `EnsureWellMemberSnapBox`. PASS
  = no ensures over a full load + 10 minutes of travel.
- **R-6 — Watch-sweep cost.** `stat unit` / `stat game` while stationary near a relocated well for 60 s.
  PASS = no measurable frame-time delta vs. the pre-change build. Report the watch-list size from the
  summary alongside.
- **R-7 — The blind spot (§0(a)).** Fly to a hidden coal original, confirm the rock is gone, fly ~2 km
  away until the cell unloads, fly back. **If the rock is visible again at a node that still refuses a
  Miner, and `MESHHIDE-LATENCY` reports nothing new, the mesh-actor-alone-re-stream case is real** and
  alternative C in §7 is required. This is the one case no counter in this diff can see.
- **R-8 — Reproduce the user's observation with the instrument on.** After a re-roll, fly to a hidden
  coal original near a `NodeShuffle.Here` marker while its rock is still visible; note wall time until
  it disappears; then grep `MESHHIDE-LATENCY` for a delay in the same range **with `route=backstop`**.
  Agreement is the confirmation. **Do not compare against "30 s" as data** — compare against the
  printed `BACKSTOP CADENCE` elapsed field, which is measured from this run.
- **R-9 — Cave census.** Re-roll, `grep "ROLLCENSUS: seed"`, read `CAVE PLACEMENT:`. PASS = `N > 0`
  (the save previously reported 14, then 27, via console). **INCONCLUSIVE = `N=0` with `C=0`** — and per
  §6.5 that now correctly reads as "not loaded OR none exist", not as a negative finding.
- **R-10 — `NodeShuffle.Here`.** PASS = a `nearest relocated well` line naming a core, resource,
  distance, `dz` and a turn; walk the turn and distance and arrive at a relocated well. PASS (other
  shape) = `— NONE` with `0 actually PLACED` on a save with no relocation. **FAIL = a distance that
  does not lead to a well** ⇒ an A3 finding, not a diagnostics bug.
- **R-11 — Behaviour-identity regression, not optional.** Confirm a Resource Well Pressurizer still
  snaps to a relocated core, and a Miner still snaps to an ordinary node near a well. Item 1 inserted a
  block between the dedup key and the branches; item 2 rewrote the mesh-hide `if` into an if/else.
  Both are believed behaviour-identical; believed is not measured.

---

## 11. Verdict

**DO NOT SHIP** — F-1 makes the always-on line state "no delay has ever been measured this session"
in the run where the delay is measured, and F-2 makes the watch population ~91% inert on the user's
own save, so the instrument answers INCONCLUSIVE on every possible run. Both are the exact defect
class (T5) the packet was written to avoid, and both are settled by evidence already in
`FactoryGame.log`, not by prediction.

**Path to SHIP WITH TESTS:** apply §6.1 and §6.2 verbatim (blockers), §6.3, §6.4 and §6.5 (majors and
moderates), add the two dated TODOs from §8, then re-review the fix as new code per the standing rule —
these are structural, not local, so escalate to a **full** re-review of item 2 and a **scoped**
re-review of items 1/3. Then R-1 through R-11.

**Ship as-is today, without fixes:** items **3 (cave census)** and **4 (nearest relocated well)** are
independently sound and touch nothing item 1 or 2 touches; item 3 needs only the §6.5 wording. If the
orchestrator wants something in the user's hands tonight, those two are the safe subset.
