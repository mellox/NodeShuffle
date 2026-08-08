# Cold review — packet `ns-t7-split` (file split + T3/T4 state promotion + `IsHidden()` filter)

**Reviewer:** cold-reviewer (did not author the packet).
**Scope:** SCOPED review of the uncommitted working tree on `wip/h2-relocation`, base `935d94a`.
**Files reviewed:** `Source/NodeShuffle/Public/NodeShuffleSubsystem.h`,
`Source/NodeShuffle/Private/NodeShuffleSubsystem.cpp`,
`Source/NodeShuffle/Private/NodeShuffleWellVisuals.cpp`,
`Source/NodeShuffle/Private/NodeShuffleWellVisualsApply.cpp`,
`Source/NodeShuffle/Private/NodeShuffleWellMeshIndex.cpp` (new),
`Source/NodeShuffle/Private/NodeShuffleWellSnapBox.cpp` (new).

**Taken as settled, not re-litigated** (per dispatch): it compiles; the import table is set-identical
to baseline (816/766, 0 missing); the six named functions are byte-identical.

**VERDICT: SHIP WITH TESTS** — nothing here blocks, but **F-1 invalidates the packet's own primary
runtime acceptance criterion** and must be corrected *before* anyone tests, or step 2 reports a false
FAIL on the first load. F-2 is a real intermittent defeat of the new world reset.

---

## 0. Runtime test checklist — UP FRONT (details in §4)

1. **Build DLL import diff** vs `tools/imports-baseline.txt` — *already done, zero delta. Settled.*
2. **`grep "WELLH2B-INDEX pass"` and read ALL passes, not the first.** `hiddenOriginal ≈ 0` on pass 1
   is **EXPECTED** (F-1), not a failure. PASS = `hiddenOriginal` climbs across passes toward ~658 and
   `activeMineable + hiddenOriginal` stays ~constant. FAIL = `hiddenOriginal == 0` on **every** pass.
3. **`grep "WELLH2C-SNAPBOX"`** — every line carries `activeMineable=` and `hiddenOriginal=`; `age` is
   `0` or `1`, **never negative**. `activeMineable=0` with `pass >= 0` is INCONCLUSIVE, not a pass.
4. **`grep "WELLH2C-SNAPBOX RESET:"`** — exactly one line on the first world, and it must read
   `0 node location(s) taken on pass -1 and 0 deduped log key(s)` (proves the first-use branch, not a
   mid-session wipe).
5. **Second save load without restarting the game** — a second RESET line, then non-negative ages, and
   each member re-says `WELLH2C-SNAPBOX` once. **If NO second RESET line appears, F-2 has fired.**
6. **Fly to a relocated well** — Pressurizer snaps to core, Well Extractor to satellite, Miner Mk1 to
   an ordinary node nearby, well is standable, `WELLH2B-COLLISION` reports `BuildGun=2`.
7. **`grep "MESHHIDE-LATENCY hide-pass"`** — 17 fields, same values as `2026-08-08-t1t2-2` for an
   equivalent load (the T4 rename check).
8. **`grep "WELLH2B-CAPTURE" / "WELLH2B-ADOPT" / "WELLH2B-APPLY" / "WELLH2-SUPPRESS"`** — unchanged
   cadence.

---

## 1. Findings — ranked

### F-1 — HIGH. The T3 population is measured BEFORE this tick's suppression runs, so `hiddenOriginal` reads ~0 on the first pass — and the packet's own step-2 FAIL criterion fires spuriously.

**Where:** filter at `Source/NodeShuffle/Private/NodeShuffleWellMeshIndex.cpp:158`; the ordering that
breaks it is `Source/NodeShuffle/Private/NodeShuffleSubsystem.cpp:2224` (`ApplyWellRelocation`) vs
`:2244` (`SuppressOriginalNodes`).

**The defect.** `RebuildWellMeshIndex` — which computes `HiddenOriginalUseBoxNodes` — runs inside
`ApplyWellRelocation` (`NodeShuffleWellRelocateApply.cpp:284`, reached via `SuppressVanillaWellGroup`).
`ApplyLayout` calls `ApplyWellRelocation()` at `:2224` and `SuppressOriginalNodes()` at `:2244`. So the
index sweeps the world **20 lines before** the pass that hides originals.

**Concrete failure scenario.** Load the reference save. Hidden state is not persisted — that is exactly
why `SteadyHiddenOriginals.Empty()` runs at `NodeShuffleSubsystem.cpp:472` and `SuppressOriginalNodes`
re-hides every load. At the instant `RebuildWellMeshIndex` sweeps on ApplyLayout pass 1, none of the 658
originals has been hidden **this session**, so `N->IsHidden()` is false for all of them. The first
`WELLH2B-INDEX pass` line prints `activeMineable≈1227 hiddenOriginal≈0`.

A tester executing handoff §7 step 2 reads its stated FAIL criterion verbatim —
*"`H == 0` on a save with hidden originals ⇒ `IsHidden()` is not the predicate this world uses, and
item 5 must be re-examined before anyone trusts the T3 numbers"* — and declares the one intended
behaviour change broken. It is not broken; the checklist is wrong.

**Second-order, and this one is about the code, not the doc.** Because pass 1's snapshot still contains
every node that is hidden moments later in the same ApplyLayout call, pass-1 `provableOverlap=1`
results can still be false positives against a hidden original — *the exact defect item 5 exists to
remove.* Item 5 only achieves its purpose from pass 2 onward. Nothing in the log says so.

This is the workspace's **"a pass that reads state its own later output writes"** signature, and the
number is now timing-dependent where it previously was not.

**Fix (ship this).** Two parts. First, make the log explain itself — append to the
`WELLH2B-INDEX pass` format string in `NodeShuffleWellMeshIndex.cpp`, immediately after
`TEXT("contest count: the contest also holds AFGResourceDeposit, which is neither), ")`:

```cpp
        TEXT("WHEN this was measured is load-bearing and is NOT an ignorable caveat: ")
        TEXT("RebuildWellMeshIndex runs inside ApplyWellRelocation, which ApplyLayout calls BEFORE ")
        TEXT("SuppressOriginalNodes (NodeShuffleSubsystem.cpp:2224 then :2244) -- so on the FIRST pass ")
        TEXT("of a session hiddenOriginal reads ~0 because nothing has been hidden YET this session ")
        TEXT("(hidden state is not persisted; that is why SuppressOriginalNodes re-runs on every load ")
        TEXT("and SteadyHiddenOriginals is emptied at :472). hiddenOriginal CLIMBING across passes is ")
        TEXT("correct and expected. hiddenOriginal==0 on the FIRST pass falsifies NOTHING. Only ")
        TEXT("hiddenOriginal==0 on EVERY pass of a session would mean IsHidden() is not the predicate ")
        TEXT("this world uses. MEASURED: the two counts at the instant of this sweep. NOT MEASURED: ")
        TEXT("what will be hidden later in this same ApplyLayout call. "),
```

Second, replace handoff §7 step 2 with:

> **2. Boot the existing save. `grep "WELLH2B-INDEX pass"` and read EVERY pass, not the first.**
> * **PASS:** `activeMineable + hiddenOriginal` stays roughly constant across passes (≈1227 in the
>   reference session) while `hiddenOriginal` **climbs** from ~0 on pass 1 toward ~658. The index runs
>   before this tick's suppression, so pass 1 legitimately sees almost nothing hidden.
> * **FAIL:** `hiddenOriginal == 0` on **every** pass of the session ⇒ `IsHidden()` is not the
>   predicate this world uses and item 5 is inert.
> * **ALSO NOTE:** any `provableOverlap=1` on **pass 1 only** is not yet trustworthy — the snapshot it
>   was measured against pre-dates that tick's hiding. Judge T3 from pass 2+.

---

### F-2 — MEDIUM-HIGH. `SnapBoxDiagWorld` is a raw address compare; a recycled `UWorld` slot silently defeats the new world reset and reinstates the negative age it was written to remove.

**Where:** `Source/NodeShuffle/Private/NodeShuffleWellSnapBox.cpp:42` (definition) and `:68`
(`if (SnapBoxDiagWorld == World) { return; }`); declared `Source/NodeShuffle/Public/NodeShuffleSubsystem.h:1809`.

**Concrete failure scenario.** Load save A (runs to ~40 apply passes). Quit to main menu, load save B in
the same process. UE frees world A's `UObject` and its `GUObjectArray` slot is returned to the free
list; world B is allocated and **can land on the same address** — slot recycling is the normal case in
UE, not an exotic one. `SnapBoxDiagWorld == World` then evaluates **true**, `ResetWellSnapBoxDiagForWorld`
returns early, and:

* `SnapBoxSnapshotPass` still holds world A's `40`, while world B's `WellAuditPasses` restarts at `0`
  → `age = 0 - 40 = **-40**`. That is verbatim the defect the T1/T2 review reported and this packet
  claims to fix.
* `SnapBoxUseBoxNodes` still holds world A's node locations → the first `WELLH2C-SNAPBOX` distance is
  arithmetic across two worlds, which the file's own comment calls *"worse than printing nothing,
  because it looks like a measurement."*
* `SnapBoxLogged` is not cleared → world B's first line for each member is deduped away, so **the
  evidence that anything went wrong is also suppressed.**

It is intermittent by construction: it works on most loads and fails on the one where the allocator
reuses the slot. That is the worst possible failure profile for a diagnostic.

**Grade:** the reset's efficacy is **assumed**, not *provably provided*. Runtime step 5 is the measurement.

**Fix (ship this).** Header, replacing line 1809:

```cpp
    // WEAK, NOT A RAW ADDRESS, AND THAT IS THE WHOLE POINT. A freed UWorld's GUObjectArray slot is
    // recycled, so the NEXT world can be allocated at the SAME address -- a raw pointer compare would
    // then report "same world", skip the reset, and reinstate the negative age this reset exists to
    // remove, WHILE ALSO suppressing the log line that would show it. TWeakObjectPtr compares
    // index+serial, so a recycled slot is detected. It still holds no strong reference: no GC interaction.
    static TWeakObjectPtr<const UWorld> SnapBoxDiagWorld;
```

`NodeShuffleWellSnapBox.cpp`, replacing line 42 and the guard at 66-72:

```cpp
TWeakObjectPtr<const UWorld> ANodeShuffleSubsystem::SnapBoxDiagWorld;
...
void ANodeShuffleSubsystem::ResetWellSnapBoxDiagForWorld(const UWorld* World)
{
    // index+serial compare, NOT an address compare: a recycled UObject slot is not the same world.
    const TWeakObjectPtr<const UWorld> Incoming(World);
    if (SnapBoxDiagWorld.HasSameIndexAndSerialNumber(Incoming)) { return; }
    const int32 PrevNodes  = SnapBoxUseBoxNodes.Num();
    const int32 PrevPass   = SnapBoxSnapshotPass;
    const int32 PrevLogged = SnapBoxLogged.Num();
    SnapBoxDiagWorld = Incoming;
    ...
```

*This one edit needs a compile* — `TWeakObjectPtr<const UWorld>` and `HasSameIndexAndSerialNumber` are
standard `CoreUObject`, but I have not built. If it resists, the fallback is to keep the raw pointer
**and** store `World->GetUniqueID()`… which does **not** work, because `GetUniqueID()` returns the
recycled `InternalIndex` itself. The weak pointer's serial number is the only cheap correct
discriminator; do not substitute a raw ID for it.

**Related, and worth recording rather than fixing here:** the T4 sibling
(`const void* MeshHideLatencyWatchWorld`, `NodeShuffleSubsystem.h:1156`) has the *identical* pattern.
It is pre-existing and out of this packet's scope — but this packet has now copied that pattern into
new code and blessed it in a public header comment, which is how a latent defect becomes canon.

---

### F-3 — MEDIUM. A header comment states a rationale that is provably false: class static data members do **not** "reproduce the previous storage duration exactly."

**Where:** `Source/NodeShuffle/Public/NodeShuffleSubsystem.h:1792-1794`, and the same claim in
`NodeShuffleWellSnapBox.cpp:35-36` and handoff §3.

A **function-local** `static TSet<FString> SnapBoxLogged;` is initialized **lazily on first use**, with a
thread-safe guard, and is therefore *guaranteed initialized before any read*. A **class static data
member** of non-trivial type (`TArray<FVector>`, `TSet<FString>`) undergoes **dynamic initialization at
DLL load**, unordered with respect to other translation units. These are different storage lifetimes,
not the same one.

**Failure scenario (latent, not live today):** any future file-scope object in this module whose
constructor calls `EnsureWellMemberSnapBox` or `ResetWellSnapBoxDiagForWorld` — a self-registering
diagnostic, a static command registrar — would read `SnapBoxLogged` before its `TSet` constructor ran.
Under the old form that was safe by construction. Nothing does this today, so the *consequence* is
benign; the *comment* is the problem. It is a load-bearing-looking rationale in a public header, and
this workspace has been burned specifically by review-response comments carrying provably false
reasoning (a11caf6 precedent; `[[lessons-log-asserted-a-cause]]`).

**Fix (wording only).** Replace the "reproduce the previous storage duration exactly" sentence with:

```cpp
    // site -- out of scope for a behaviour-identity packet. STORAGE DURATION IS SIMILAR, NOT IDENTICAL,
    // and the difference is stated rather than glossed: the previous function-local statics were
    // initialised LAZILY on first use; these are dynamically initialised at DLL load, unordered against
    // other TUs. That is safe HERE only because nothing in this module's static initialisation touches
    // them -- every reader runs at gameplay time. If a file-scope object in this module ever calls into
    // EnsureWellMemberSnapBox or ResetWellSnapBoxDiagForWorld from its constructor, that stops being true.
```

---

### F-4 — MEDIUM. ~60 of `NodeShuffleWellSnapBox.cpp`'s 399 lines are a duplicated T3 legend the packet just had to edit twice, identically.

**Where:** `NodeShuffleWellSnapBox.cpp:299-318` and `:372-391` — the `|| T3 OVERLAP: …` tail, byte-identical
across the NO-OP and re-point branches, with identical 11-argument tails at `:321-323` and `:395-397`.

**Failure scenario.** The next packet adds a third count (or renames `hiddenOriginal`) and updates only
the branch it was reading. A NO-OP member and a re-pointed member then print **different field sets for
the same measurement**, and a `grep "activeMineable="`-based extraction silently returns two schemas. The
packet already demonstrated the hazard: it had to make the same three edits twice to land item 5, and
nothing enforces that they stayed in sync.

**Fix.** Build the clause once, `if (bSayIt)`, before the `bExistingCovers` branch, and end each
`UE_LOG` format with a single `TEXT("%s")` / `*T3Clause`:

```cpp
    // ONE legend, TWO printers. This tail was duplicated verbatim across the two UE_LOG calls below and
    // ns-t7-split had to edit both copies identically to add activeMineable/hiddenOriginal. The next
    // packet will not be so careful, and two branches printing different field sets for the SAME
    // measurement is unreadable. Built only on a pass that prints.
    FString T3Clause;
    if (bSayIt)
    {
        T3Clause = FString::Printf(
            TEXT("|| T3 OVERLAP: finalExtent=%s; nearest ordinary MINEABLE node (AFGResourceNode; ")
            TEXT("DEPOSITS EXCLUDED -- only this class ever carries EnsureNodeUseBox's 650 cm box) ")
            /* ...the existing legend text, unchanged... */,
            *FinalExtent.ToCompactString(), NearestNonWellCm, NearestDX, NearestDY, NearestDZ,
            BystanderCount, HiddenExcluded, BystanderFromPass, SnapBoxCurrentAuditPass,
            SnapBoxCurrentAuditPass - BystanderFromPass, bProvableOverlap);
    }
```

Cost: one `FString` per printed line (already rate-limited to once per member per key). Saves ~30 lines
and removes the divergence class entirely. **Style/robustness — no live failure scenario today**, so it
ranks below F-1/F-2.

---

### F-5 — LOW-MEDIUM. The T3 filter uses `IsHidden()` alone; the mod's own "is this original suppressed" predicate is two-part.

**Where:** `NodeShuffleWellMeshIndex.cpp:158` uses `N->IsHidden()`. The mod's restore path at
`NodeShuffleSubsystem.cpp:487` uses `Node->IsHidden() || !Node->GetActorEnableCollision()`.

They agree **today** because `SuppressOriginalNodes` sets both in one block
(`NodeShuffleSubsystem.cpp:4699-4700`), so no node is ever de-collided-but-visible. The handoff's claim
that "the two cannot disagree about what hidden means" is therefore true *of the current code*, but it
cites the wrong half of the predicate pair.

**Failure scenario (requires a future change):** any path that disables collision without hiding — an
occupancy guard, a partial restore, a third-party mod — leaves a node that T3 counts as
`activeMineable` and that cannot actually be built on, i.e. the false positive item 5 removed comes
back through the other door.

**Recommendation: do NOT change it now.** Widening the predicate would change what `hiddenOriginal`
*means* (from "hidden" to "suppressed") and the field name and log legend would have to follow — that is
new behaviour in a parked item. Record it instead, as a one-line comment at `:158`:

```cpp
                // PREDICATE PARITY: SuppressOriginalNodes sets hidden AND collision-off together
                // (:4699-4700) and the restore path tests BOTH (:487). This tests only the first half,
                // which is equivalent TODAY. If any path ever de-collides without hiding, widen this to
                // `IsHidden() || !GetActorEnableCollision()` AND rename the counter/log field to match.
```

---

### F-6 — LOW. The new `WELLH2C-SNAPBOX RESET:` line is always-on and prints the least useful world field.

**Where:** `NodeShuffleWellSnapBox.cpp:78-87`.

It is `UE_LOG(..., Display, ...)` with no `FNodeShuffleModule::AreDiagnosticsEnabled()` gate — a **new
always-on line in shipped builds**. It fires on the first call of every session, when nothing has
happened. And it prints only `"non-null"` / `"null"` for the world, which is the one field a reader
cannot act on. Handoff §9's claim "no new always-on per-actor line" is technically true (it is
per-world) but reads as "no new always-on line", which is false.

The two sibling `WELLH2C-SNAPBOX` lines are also ungated, so this is house style in this file — but
those describe a member the tester asked about, and this one describes a housekeeping event.

**Fix.** Print the map name so a real world change is distinguishable from a `GetWorld()` nullptr
thrash, and keep it ungated (a tester needs it in step 5) but say so:

```cpp
    // DELIBERATELY UNGATED, unlike the NARROWED/WIDENED/BYSTANDER lines: runtime step 5 greps for this
    // line to prove the world reset fired, and it emits at most once per UWorld change.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2C-SNAPBOX RESET: the T3 snapshot now belongs to a different UWorld ('%s'), so it ")
        /* ...unchanged... */,
        World ? *World->GetName() : TEXT("<null>"), PrevNodes, PrevPass, PrevLogged);
```

---

### F-7 — STYLE. Stale line citations in the handoff.

Handoff §4 cites `NodeShuffleSubsystem.cpp:4707` for `if (!Node->IsHidden()) { Node->SetActorHiddenInGame(true); }`.
The packet's own **−7 line** T4 delta moved it to **`:4700`**. Also §3 cites the namespace block at
`:4483-4502`; post-edit the region begins at `:4483` and the function at `:4497`. Cosmetic, but this is
the `[[stale figure drift]]` signature in a document a later agent will copy from.

### F-8 — STYLE / framing. "All four well `.cpp` files are now under the 500-line rule" reads as solved.

Handoff §1 and `docs/TECH-DEBT.md:534` both say so. Meanwhile `NodeShuffleSubsystem.cpp` is **8069**
lines (16× the rule) and `NodeShuffleSubsystem.h` — which **this packet grew by 96 lines** — is now
**1852** (3.7×). The packet moved prose *out of* `.cpp` file headers *into* the shared public header,
which is the direction that maximises rebuild coupling: every TU including `NodeShuffleSubsystem.h`
now recompiles when a T3 comment changes. Not a defect, and T7's scope explicitly excluded the big
file — but the sentence should read "the four well files are split; the module's two largest files are
untouched and remain the open T7 item."

---

## 2. Differential / parity table

The dispatch's specific questions are answered inline in §3; this is the invariant ledger.

| Invariant the OLD path guaranteed | How the NEW path provides it | Grade |
|---|---|---|
| Index rebuilt at most once per apply pass | `EnsureWellMeshIndex` moved byte-identical; gate is still `WellMeshIndexPass == WellAuditPasses`, and `WellMeshIndexPass` initialises to `-1` vs `WellAuditPasses = 0`, so pass 0 still rebuilds | **provably provided** (read both, `NodeShuffleSubsystem.h:1678,1746`) |
| `WellMeshOwnerRadiusCm == 1200.0f`, one definition, internal linkage, one reader | file-static moved with its only reader into `NodeShuffleWellMeshIndex.cpp`; no other TU references it (grep-verified) | **provably provided** |
| `SaveableMaterialPath` / `DumpWellActorCollision` reachable from exactly one caller each, no unreferenced static created | both stayed with their callers; `DumpWellActorCollision` has exactly one call site, `NodeShuffleWellVisualsApply.cpp:215` (grep-verified, whole `Source/`) | **provably provided** |
| T3 snapshot readable from a **static** member function | promoted to class `static` data members; reader `EnsureWellMemberSnapBox` is still static | **provably provided** |
| T3 snapshot state initialised before first read | **NOT identical** — lazy-on-first-use became dynamic-init-at-DLL-load (F-3). Safe today only because no static initialiser in this module reads them | **assumed** — step 4/5 |
| T4 latency state resets when the world changes | original guard kept verbatim at `NodeShuffleSubsystem.cpp:4541`; on a fresh instance `MeshHideLatencyWatchWorld == nullptr` fires it identically. `~40` references renamed, no statement added/removed/reordered (diff-verified) | **provably provided** statically; the *values* are step 7 |
| T4 state is process-global (module statics) | now **per-instance**. Strictly narrower. Equivalent iff the subsystem is per-world; if two instances ever coexist they no longer share the watch map | **assumed** — step 7 compares values against `2026-08-08-t1t2-2` |
| `Bystanders` (route 3's contest set) holds **every** non-fracking `AFGResourceNodeBase`, hidden ones included | `Bystanders.Add()` at `NodeShuffleWellMeshIndex.cpp:137` is **outside and above** the `Cast<AFGResourceNode>` block the filter lives in. The `IsHidden()` test cannot reach it | **provably provided** (see §3.2) |
| `activeMineable + hiddenOriginal` == the old `UseBoxNodes` population | the `if/else` at `:158-159` is total over the cast-passing set; exactly one branch runs per node | **provably provided** |
| `activeMineable` is a stable property of the world | **NO LONGER TRUE.** It is now a function of *when in the ApplyLayout sequence the index ran* (F-1) | **regressed, disclosed here** — step 2 |
| T3 snapshot dropped on world change (NEW; did not exist before) | `ResetWellSnapBoxDiagForWorld`, called from both writers, idempotent | **assumed** — defeated by `UWorld` address reuse (F-2) — step 5 |
| A member's `mBoxComponent` re-point, extent, `clamped=` | computation byte-identical (diff-verified) | **provably provided** statically; the *effect on the snap* runs through closed-source hologram code — **assumed**, step 6 |
| `WellVisualCaptureLogged` keys cannot collide | 7 families, all verified against source (§3.7); disjoint by prefix/`|` | **provably provided** |
| DLL imports unchanged | measured, 816/766, 0 delta | **provably provided by measurement** (not by reasoning) |
| Every `AFGResourceNodeFrackingCore*→AActor*` conversion has a complete type | explicit `#include "NodeShuffleWellCensus.h"` replaces unity-blob luck | **provably provided by the clean build** |

---

## 3. The dispatch's seven questions, answered

**3.1 — What moved that was NOT byte-identical, and did its semantics survive?**

The non-byte-identical set is exactly four things, and I found no fifth:

* `RebuildWellMeshIndex` — 4 disclosed edits (reset call, counter decl, `IsHidden()` filter, publish
  block + 2 new `%d`). Format arity verified by hand: **17 specifiers, 17 arguments** (`NodeShuffleWellMeshIndex.cpp:449-479`). ✅
* `EnsureWellMemberSnapBox` — promotion rename + 2 new `%d` in each of two logs. Arity verified:
  NO-OP branch **18/18** (`:294-323`), re-point branch **21/21** (`:366-397`). ✅
* `ApplyWellGroupVisuals` — first statement changed to `ResetWellSnapBoxDiagForWorld(GetWorld()); SnapBoxCurrentAuditPass = WellAuditPasses;`. Order is correct (reset **then** assign — reversed, the reset would immediately wipe the value it just set). ✅
* `SuppressOriginalNodes` — ~40 mechanical renames, diff-verified: no statement added, removed or reordered.

**Linkage:** nothing gained or lost `static` / anonymous-namespace linkage. Verified independently of
the handoff's claim: `WellMeshOwnerRadiusCm` appears only in `NodeShuffleWellMeshIndex.cpp`;
`SaveableMaterialPath` only in `NodeShuffleWellVisuals.cpp:74,155`; `DumpWellActorCollision` only in
`NodeShuffleWellVisualsApply.cpp:58,215`. No unreferenced static in either new file. ✅

**Lifetime/initialisation across the TU boundary:** the one real change is F-3 (lazy → DLL-load dynamic
init) and F-2 (world identity by raw address). The promotion does **not** change *when the state is
reset or read* relative to the module-statics it replaces — `RebuildWellMeshIndex` still writes once per
apply pass and `EnsureWellMemberSnapBox` still reads per dressed member — **except** that it now also
resets on world change, which is the intended new behaviour.

**3.2 — Is the `IsHidden()` filter isolated from `Bystanders`? Do the counts partition? Is a zero interpretable?**

**Isolation: YES, provably.** `NodeShuffleWellMeshIndex.cpp:137` runs `Bystanders.Add(N->GetActorLocation())`
unconditionally for every non-fracking node; the `Cast<AFGResourceNode>` block containing the
`IsHidden()` test opens at `:140`, *after and inside* it. There is no path by which the filter can
remove an element from `Bystanders`. Route 3's contest is untouched, and `BystanderLocations()`'s
deletion removed the only other consumer. **No node is claimed, captured, hidden or de-collided
differently.** ✅

**Partition: YES.** `if (N->IsHidden()) { ++Hidden…; } else { UseBoxNodes.Add(…); }` is total and
exclusive over the cast-passing set, so `activeMineable + hiddenOriginal` equals the old population
exactly. The log's claim that the two do *not* sum to the contest count (deposits are `AFGResourceNodeBase`
but not `AFGResourceNode`) is also correct. ✅

**Zero interpretability: PARTIALLY — and this is where F-1 bites.** `hiddenOriginal == 0` is currently
ambiguous between "the predicate is wrong" (a real failure) and "the index ran before suppression on
this pass" (expected). The log does not distinguish them. F-1's fix makes it self-explaining.
Separately, `activeMineable == 0` with `pass >= 0` is explained in the *log legend* but is **not** in
the handoff's step 3 PASS/FAIL grid, which only excludes `activeMineable=0 / pass=-1`. Checklist step 3
above closes that gap.

**3.3 — Judgement on clearing `SnapBoxLogged` on world reset: right call, or new per-load spam?**

**Right call, and the cost is smaller than the disclosure implies.** Bound it: `EnsureWellMemberSnapBox`
is called only from `DressWellActor`, i.e. only for **our relocated well members that have been dressed
this session** — not for all nodes, not for all vanilla well members. The reference session resolved
**5** members. So the cost is on the order of **5 extra `Display` lines per world load**, not per node
and not per pass.

The alternative is strictly worse and is the failure mode this project keeps hitting: the key is
`Actor->GetPathName()`, which is **stable across loads of the same map**, so *not* clearing it means the
new world's first — and most diagnostically valuable — line for every member is deduped against the
previous world's and never printed. That is a reset that erases its own evidence, and it would also
hide F-2 if F-2 ever fires. The packet's reasoning is correct and the trade is heavily in favour of
clearing. **No change recommended.** Worth adding the bound to the handoff so nobody reads "once more
per member per world load" as unbounded.

**3.4 — Is `BystanderLocations()` genuinely unread, and was deleting it inside a split packet right?**

**Genuinely unread: VERIFIED INDEPENDENTLY.** A scoped grep for
`BystanderLocations|NodeShuffleWellSnapBoxDiag|BystanderPass|UseBoxNodeLocations` across `Source/` and
`tools/` returns **only comments** — three prose mentions and one in the header. Zero call sites, zero
reads, in the whole module. The deletion is safe. ✅

**Scoping call: RIGHT, with one caveat.** Promoting provably-dead state into a *public header* is worse
than deleting it — it makes the dead state more visible, more permanent, and harder for the next reader
to prove dead. The packet had the header open, the state was already recorded as dead by the T1/T2
review, and it is a pure removal with no observer. Deleting was correct.

The caveat is honesty about the claim: the handoff says "one ~36 KB array copy per apply pass goes
away." That is a **performance change**, not nothing — it is the only performance delta in the packet
and it is in the good direction, but "nothing observed it, so nothing can observe its absence" is only
true of *behaviour*, not of timing. Immaterial here; flagged so the phrasing isn't reused where it
matters.

**3.5 — Was refusing to move `DumpWellActorCollision` correct?**

**Correct, and confirmed from source, not from the handoff.** It is declared inside the anonymous
namespace at `NodeShuffleWellVisualsApply.cpp:58` and has exactly **one** call site in the entire
`Source/` tree: `:215`, inside the `DressOne` lambda in `ApplyWellGroupVisuals`, which stays. Moving it
to `NodeShuffleWellSnapBox.cpp` would produce an unreferenced internal-linkage function there
(`-Wunused-function` / MSVC C4505 class) and an **undefined identifier** at `:215`. The proposed seam
was wrong; refusing it was right; recording the correction in **both** file headers so it is not
re-proposed is exactly the right disposition. ✅

Placement is also right on responsibility grounds: `DumpWellActorCollision` dumps the **dressed
destination actor's** component collision after `DressOne`, which is the destination file's job. It is
not part of the collision *recipe*.

**3.6 — Are the seams conceptual or arbitrary?**

**Conceptual.** The four files map onto four distinct answers, not four line ranges:

| file | lines | the question it answers |
|---|---:|---|
| `NodeShuffleWellMeshIndex.cpp` | 480 | *Which vanilla mesh pieces ARE this well member?* (3 pairing routes, bystander contest, type gate) |
| `NodeShuffleWellVisuals.cpp` | 248 | *ORIGIN: capture that look, then hide it* |
| `NodeShuffleWellVisualsApply.cpp` | 243 | *DESTINATION: put it back on the actor we spawned* |
| `NodeShuffleWellSnapBox.cpp` | 399 | *COLLISION: what the build-gun trace hits and what the snap resolves against* |

The test I applied: **can each file's contents be named without reference to the others?** Yes for all
four, and the file map repeated in `NodeShuffleSubsystem.h:1689-1695` and in each file's header states
those names. The header prose was also split by *what it describes* rather than moved wholesale — the
routes/pairing-bug half went with `RebuildWellMeshIndex`, and each file's import-discipline prediction
was narrowed to the surface **that file** actually reaches. That is the opposite of an arbitrary cut.

Two reservations: `NodeShuffleWellMeshIndex.cpp` at 480 has ~20 lines of headroom before it re-breaches
(the type-gate rationale is ~40 lines of comment and will grow first), and F-4/F-8 above.

**Net: the split leaves the code more maintainable. It is not a line-count cut wearing a
responsibility label.**

**3.7 — Is the key-family table accurate and complete?**

**Accurate and complete — verified against every write site in the module**, not against the handoff:

| documented family | actual construction site | matches |
|---|---|---|
| `narrowed\|<smaPath>\|<meshName>` | `NodeShuffleWellMeshIndex.cpp:326` `FString::Printf(TEXT("narrowed\|%s\|%s"), *SMA->GetPathName(), *MeshName)` | ✅ |
| `widened\|<smaPath>\|<meshName>` | `NodeShuffleWellMeshIndex.cpp:362` | ✅ |
| `bystander\|<smaPath>\|<meshName>` | `NodeShuffleWellMeshIndex.cpp:399` | ✅ |
| `<corePath>\|adopt` | `NodeShuffleWellVisuals.cpp:97` `E.CorePath + TEXT("\|adopt")` | ✅ |
| `<corePath>\|grp` | `NodeShuffleWellVisuals.cpp:191,193` | ✅ |
| `<memberPath>` (bare) | `NodeShuffleWellVisuals.cpp:131,133` — `Path`, no suffix | ✅ |
| `meshtypecensus\|%d\|%d\|%d\|%d` | `NodeShuffleSubsystem.cpp:7683` | ✅ |

**Seven writers, seven rows, no eighth.** A grep for `WellVisualCaptureLogged` across `Source/` returns
exactly these seven sites (plus the header). The stated file attributions are correct post-split
(narrowed/widened/bystander did move to `…MeshIndex.cpp`). The non-collision argument holds: the three
prefixed families begin with a literal a UObject path cannot begin with; the two suffixed families
contain `|`, which a bare path never does; `meshtypecensus|…` is prefixed. The "NOT in this set" list
(`WellVisualLogged`, `WellVisualCompDumped`, `SnapBoxLogged`) is also correct and is the most useful
part of the table. **No change recommended.**

---

## 4. Alternatives — with trade-offs and a recommendation

**A. Ship as-is.** Zero further risk of a fresh review-response bug; the build and import measurement
already hold against this exact tree. Cost: F-1 ships a diagnostic that mis-explains itself and a
checklist that reports a false FAIL; F-2 ships an intermittently-defeated reset.

**B. Ship with F-1 + F-2 + F-3 + F-6 only** (leave F-4/F-5 as recorded notes). F-1 and F-6 are
comment/format-string edits, F-3 is a comment edit — three of the four cannot change behaviour at all.
F-2 is the one real code edit and it is 4 lines, mechanical, and independently testable by step 5.
Cost: one rebuild, and F-2 needs a compile it has not had.

**C. Ship as-is, fix the checklist only** (F-1's second half, no code change). Cheapest correct-testing
option: it removes the false FAIL without touching the tree that already built and measured clean.
Cost: F-2 stays; the `WELLH2B-INDEX` line still doesn't explain its own timing to a future reader who
doesn't have this review.

**D. Go further and de-static the T3 state** (make `EnsureWellMemberSnapBox` and `DressWellActor`
instance methods, T3 state instance members). Benefit: F-2 **evaporates** — instance state dies with the
world, so no world-identity compare is needed at all, and the process-global state the split just
canonized in a public header disappears. Cost: two signature changes and three call sites; this stops
being a behaviour-identity packet, and it is precisely the class of "structural review-response fix"
that has introduced a fresh bug ~every time in this workspace.

**E. Split differently** — fold `SnapBox` back into `VisualsApply` and split the 480-line index into
pairing-routes + type-gate. Rejected: it would cut one algorithm in half, and the collision recipe is
the packet's stated acceptance criterion, which earns its own file.

### Recommendation: **B**, and explicitly **not D**.

Take F-1, F-2, F-3, F-6 now. Three of the four are text-only and cannot regress; F-2 is a 4-line
mechanical change whose *entire* purpose is to make step 5 meaningful, and shipping the reset without it
means step 5 can pass by luck. Record F-4, F-5, F-7, F-8 in `docs/TECH-DEBT.md` under T7 as follow-ups.

**Do not take D in this packet.** The T3 state's static-ness is *forced* by a signature this packet
correctly declined to change, and D is exactly the shape of change that has broken things here before.
Put it on the record as the right eventual shape and let it be its own reviewed packet.

If budget is tight and only one thing can land: **the F-1 checklist correction**, because without it
the first test run reports a false FAIL and the packet gets reverted for a defect it does not have.

---

## 5. Diagnostics audit (house rule: every new path logs inputs, decision, outcome)

| new path | inputs logged | decision logged | outcome logged | verdict |
|---|---|---|---|---|
| `ResetWellSnapBoxDiagForWorld` | partial — only `null`/`non-null`, not which world (F-6) | yes (the branch is the line's existence) | yes — nodes, pass, key count dropped | **pass with F-6** |
| `IsHidden()` filter | yes — contest count, both partitions | yes — which branch each node took, in aggregate | yes — the new snapshot population | **pass, but does not log its own timing (F-1)** |
| T4 promotion | unchanged (17 fields) | unchanged | unchanged | **pass** |
| T3 static promotion | n/a (no branch) | n/a | n/a | **pass** |

The rule about logging *why a guard returned early*: `ResetWellSnapBoxDiagForWorld`'s early return
(`SnapBoxDiagWorld == World`) is silent — correct, since it fires on every call after the first and a
line there would be per-group-per-pass spam. No finding.

**One thing the log will not explain today:** if F-2 fires, the *absence* of a second RESET line is the
only evidence, and absence is exactly what a tester skims past. Step 5 above is written to look for it
explicitly.

---

## 6. Verdict

**SHIP WITH TESTS** — the split is well-seamed, the byte-identity and isolation claims I re-verified
independently all hold, and `Bystanders` is provably untouched; but the packet's primary runtime
acceptance criterion is wrong (F-1) and the new world reset can be silently defeated by `UWorld` address
reuse (F-2), so nothing here should be tested against the handoff's checklist as written.

**Runtime checklist, one line each:**
1. Import diff vs `tools/imports-baseline.txt` — **already measured, zero delta, settled.**
2. `grep "WELLH2B-INDEX pass"` — read **all** passes; `hiddenOriginal` must **climb** toward ~658 while `activeMineable + hiddenOriginal` stays ~1227; `hiddenOriginal == 0` on pass 1 is expected, on **every** pass is a FAIL.
3. `grep "WELLH2C-SNAPBOX"` — every line carries `activeMineable=` and `hiddenOriginal=`; `age` is 0 or 1, never negative; `activeMineable=0` with `pass >= 0` is INCONCLUSIVE, never a pass.
4. `grep "WELLH2C-SNAPBOX RESET:"` — exactly one on the first world, reading `0 node location(s) taken on pass -1 and 0 deduped log key(s)`.
5. Load a second save without restarting — a **second** RESET line must appear, ages stay non-negative, each member re-says its line once; **no second RESET line = F-2 has fired.**
6. Fly to a relocated well — Pressurizer snaps to core, Well Extractor to satellite, Miner Mk1 to a nearby ordinary node, well is standable, `WELLH2B-COLLISION` shows `BuildGun=2` on `NodeShuffleWellMesh_*`.
7. `grep "MESHHIDE-LATENCY hide-pass"` — 17 fields, values matching `2026-08-08-t1t2-2` for an equivalent load.
8. `grep "WELLH2B-CAPTURE" / "WELLH2B-ADOPT" / "WELLH2B-APPLY" / "WELLH2-SUPPRESS"` — unchanged cadence.
