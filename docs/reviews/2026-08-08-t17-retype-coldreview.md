# T17 cold review — the spawned well group carries the layout's resource

**Reviewed** 2026-08-08 · branch `wip/h2-relocation`, HEAD `5ed3ac2`, working tree uncommitted, **never compiled**.
**Scope** exactly three files: `NodeShuffleWellSpawn.cpp`, `NodeShuffleWellRetype.cpp`, `NodeShuffleWellRetype.h`.
`NodeShuffleWellRelocateRoll.cpp` / `NodeShuffleConfig.*` are held by a concurrent packet and were **not read**; where
call-site ordering mattered I read `NodeShuffleWellRelocateApply.cpp` (not in the concurrent set) and `git show HEAD:`.

**Verdict: SHIP WITH TESTS.** The write is correct, idempotent, cannot oscillate, and never touches purity. The pin
extraction is a genuine pure extraction. What is not settled is (a) one bookkeeping gap where a pin found here is never
recorded anywhere, (b) that the block is **completely silent on success**, so the first build cannot prove it ran, and
(c) an engine-internal rebuild that now runs on a spawned fracking actor for the first time.

---

## 0. THE RUNTIME CHECKLIST, UP FRONT

Every item below is graded *assumed* or *unverifiable statically* in §2 and must be executed in game.

1. **Prove the code path is live.** Load a save with relocated wells. Grep `WELLH1 core=` and confirm every relocated
   well now prints `groupPlaced=1`. If no line carries `groupPlaced=`, the build is stale — stop.
2. **Produce the defect, then watch it close.** Re-roll with a **different seed** (the reference log's two rolls shared
   seed `1223222528`, which is why the consequence has never been observed). Fly to a relocated well whose
   `res=` changed. Expect exactly one `WELLH2-RETYPE core=… %d of %d live spawned member(s) disagreed, N rewritten`,
   with **N equal to the "disagreed" count**. A smaller N is finding F4.
3. **Confirm the world actually changed** — the well's visible resource, and a Pressurizer's reported output, must be
   the NEW resource. Do not accept the log alone: `WELLH1`'s `res=` is the layout, not the world (§Q8).
4. **Undress check (F3).** Immediately after step 2, at that same well: is the core still visible, still dressed, and
   does a **Resource Well Pressurizer hologram still snap to it**? Then check for anything new on the ground — an oil
   puddle-style **decal** or an invisible box that the build gun now hits *instead of* our mesh. Grep
   `WELLH2B-COLLISION` for that actor; **expect it NOT to have re-fired** (its throttle is keyed on our piece count),
   which is exactly why this must be checked with eyes and the build gun, not with grep.
5. **Well Extractor check.** Same well, on a satellite that was rewritten: does a Well Extractor still snap?
6. **Purity unchanged.** Note two satellites' purity before step 2 and after. They must be identical and still MIXED
   across the group.
7. **Idempotence.** Stay at the well for ~60 s (≈12 apply passes). `WELLH2-RETYPE` must appear **once** and never again.
   A repeat every ~5 s is an oscillation and is a stop-ship.
8. **Pin behaviour.** Build a Pressurizer on a relocated core, then re-roll to a different resource. Expect one
   `WELLH2-RETYPE-PIN`, **no** resource change in the world, and no `WELLH1-MISMATCH`. Then check `WELLH1-SKIP` /
   `WELLH1-PIN` for that core — **if neither appears, F1 is confirmed** (the well is de facto pinned but recorded
   `bManaged=1 bPinned=0` for the rest of the session).
9. **Import table.** After the build, run `check_imports.ps1` / `dumpbin /imports` on the shipped DLL and diff against
   the H0 baseline (744 / 0 missing). Expect **zero** delta. Per `memory:ue-import-table-must-be-measured`, "no new
   entry point" is a prediction until this runs.

---

## 1. FINDINGS

### F1 — MEDIUM-HIGH · a pin found here is never recorded, and the owner it delegates to may be structurally unable to record it
`NodeShuffleWellSpawn.cpp:382-408` (the pin branch) delegates ownership of `E.bPinned` / `E.bManaged` to
`ApplyWellRetype` with the comment *"ApplyWellRetype owns E.bPinned/E.bManaged and re-evaluates the same predicate."*
It re-evaluates the same **predicate**, but not against the same **actor**.

`NodeShuffleWellRetype.cpp:230` — `const bool bAlreadyApplied = (Core->mResourceClassOverride.Get() == ResourceClass);`
— where `Core` is the **hidden vanilla original** from `FindOriginalBaseByPath`. The pin one line above
(`:209`, T16) resolves against the **spawned** core. The write gate is
`(Pin.bCoreInUse && !bAlreadyApplied) || Pin.bSatelliteInUse`, so a core-only pin is suppressed by a fact measured on a
different population.

**Concrete failure scenario** (ordering from `NodeShuffleSubsystem.cpp:2215` before `:2224`):

| pass | `ApplyWellRetype` (originals) | `ApplyWellRelocation` → `SpawnWellGroup` (spawned) |
|---|---|---|
| 1 | `SpawnedWellCores` still empty ⇒ pin non-decisive ⇒ `WELLH1-PIN-UNMEASURED`, **writes NEW res to the hidden originals** | `AdoptRestoredWellGroups` fills the handles; T17 sees `StaleRes>0`, pin fires on the pressurizer ⇒ `WELLH2-RETYPE-PIN`, writes nothing |
| 2 | pin now resolves SPAWNED ⇒ `bCoreInUse=1`. But `bAlreadyApplied` is read from the **original**, which pass 1 already wrote ⇒ `true`. No satellite extractor ⇒ **gate is false ⇒ NO PIN.** `E.bPinned` stays 0, `E.bManaged` stays 1, `E.AssignedResourceClassPath` is **not** corrected back | pin fires again, throttled, silent |
| 3…∞ | steady: `WELLH1 … equal=1 groupPlaced=1` — looks healthy | `StaleRes>0` forever, silent |

Net: a well with a Pressurizer on it is recorded **managed and unpinned** for the whole session while the world
permanently refuses the assignment. It self-heals only at the *next* roll (T16's roll-time pin reads live state). The
single piece of evidence is one throttled `WELLH2-RETYPE-PIN` line.

**This is a pre-existing T16 defect that T17 makes consequential** — before T17 nothing ever retyped the spawned
group, so the mixed-population `bAlreadyApplied` had no visible effect.

**What I would ship, lowest risk first.**
*Option A (recommended for this packet — diagnostics only, zero behaviour change):* make the discrepancy legible so
step 8 can prove it. Add to the `WELLH2-RETYPE-PIN` line:

```cpp
    TEXT("LAYOUT BOOKKEEPING AT THIS INSTANT: bManaged=%d bPinned=%d -- if bPinned=0 here, ")
    TEXT("ApplyWellRetype has NOT recorded this pin and the layout will keep claiming this ")
    TEXT("assignment until the next roll re-evaluates from live state."),
    ...
    E.bManaged ? 1 : 0, E.bPinned ? 1 : 0);
```

*Option B (the real fix, but it changes T16 — needs its own review):* one line in `NodeShuffleWellRetype.cpp:230`,
measuring "already applied" on the actor the pin was resolved against:

```cpp
        const AFGResourceNodeFrackingCore* AppliedOn = Pin.ResolvedCore ? Pin.ResolvedCore : Core;
        const bool bAlreadyApplied = (AppliedOn->mResourceClassOverride.Get() == ResourceClass);
```

I recommend **A now, B as its own TECH-DEBT item (T18)**. B is a mode-selection-predicate change to a path that
landed hours ago; per this workspace's own rule that class of fix has introduced a fresh bug every time it has been
authored under review pressure.

---

### F2 — MEDIUM · the block is silent on success, so the first build cannot prove it executed
`NodeShuffleWellSpawn.cpp:357-430` prints **nothing** when `StaleRes == 0`, and **nothing** when
`!IsValid(Core) && StaleRes > 0`. Steady-state silence is correct and deliberate. The problem is the *first* build:
a tester who re-rolls, flies out, and sees no `WELLH2-RETYPE` line cannot distinguish

* "the block ran, the group was already converged" (healthy),
* "the block ran but `LiveSpawnedSats` was empty" (broken population),
* "the maintenance caller never reached this well" (proximity gate, §5), and
* "this is a stale DLL and the code is not in it" (the failure mode that wastes a whole test session).

The workspace rule is explicit: *every new code path logs its inputs, the decision taken, and the outcome.* The
"decision taken" here — *converged, nothing to do* — is unlogged.

**What I would ship** — a once-per-(core, resource) positive line, keyed like the pin throttle so it costs one hash
lookup per pass and never repeats:

```cpp
    else if (IsValid(Core))
    {
        // T17 F2: the CONVERGED verdict, said once per core per assigned resource. Without a positive
        // line, "no WELLH2-RETYPE in the log" is ambiguous between converged, empty-population and a
        // stale DLL -- and the last of those is the one that wastes a test session.
        const FString OkKey = E.CorePath + TEXT("|t17ok|") + ResourceClass->GetPathName();
        if (!WellStaleInUseLogged.Contains(OkKey))
        {
            WellStaleInUseLogged.Add(OkKey);
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH2-RETYPE-OK core='%s': MEASURED -- all %d live spawned member(s) (1 core + %d ")
                TEXT("of %d satellite record(s)) already hold the assigned '%s'. Nothing written. NOT ")
                TEXT("MEASURED: whether they are the members a player can see -- see WELLH2-AUDIT."),
                *WellShort(E.CorePath), LiveMembers, LiveSpawnedSats.Num(), E.Satellites.Num(),
                *WellShort(ResourceClass->GetPathName()));
        }
    }
```

Note this also removes the second silent hole: with the `else if (IsValid(Core))` shape, the only unlogged path left is
`!IsValid(Core)`, which is already covered by the early returns above. If you would rather keep it minimal, at
*minimum* fold a session counter (`t17Converged=%d t17Rewritten=%d`) into the existing throttled `WELLH2-PLACED` line.

---

### F3 — MEDIUM · `RebuildNodeNativeVisual` on a spawned fracking actor — it will not *undress*, but it can *add*, and the one diagnostic that would show it is throttled off
Answering the question plainly: **no, it cannot undress a working well.** The dressing is a set of
`NodeShuffleWellMesh_%d` `UStaticMeshComponent`s that this mod creates and owns
(`NodeShuffleWellVisualsApply.cpp:114-161`); the engine's `OnRep_ResourceClassOverride` path knows nothing about them
and cannot delete them. Everything it *could* plausibly take away is re-asserted one statement later:

* `NodeShuffleWellRelocateApply.cpp:505-513` — for an **already-placed** group, `ApplyWellGroupVisuals(E)` runs
  **unconditionally after** `SpawnWellGroup`, in the same pass, outside the proximity `if`.
* `DressWellActor` re-asserts, every pass: `ConfigureWellMeshCollision` (`:158`), visibility (`:159-160`),
  `SetActorHiddenInGame(false)` (`:163`), `SetActorEnableCollision(true)` (`:164`), the identity component and
  `EnsureWellMemberSnapBox` (`:177-178`). So the T2 snap box is rebuilt in the same tick.

The real exposure is the **opposite** direction, and it is not repaired. `AFGResourceNodeBase` carries
`mDecalComponent` and `mBoxComponent` — the latter documented in the engine header as *"If we have no static mesh but
a decal, then we use this for collision"* (`FGResourceNodeBase.h:258-263`). Well resources are exactly the
no-static-mesh case (Water / Oil / Nitrogen / Chlorine). `ConditionallySetupComponents(needRegister, decalMaterial)` is
the protected setup the OnRep path exists to drive. So the rebuild can plausibly attach a **decal and a box collider**
to our spawned core/satellite that were never there before. Satellites take a *different* implementation
(`AFGResourceNode::OnRep_ResourceClassOverride`, `FGResourceNode.h:160`) than cores (the base, `FGResourceNodeBase.h:242`),
so the two halves of a group are not one test.

**Concrete failure scenario:** a re-roll retypes a relocated well to a liquid. The rebuild attaches `mBoxComponent`
at the actor origin with a profile that blocks `ECC_GameTraceChannel5`. The Pressurizer hologram's trace now lands on
that box instead of on our `NodeShuffleWellMesh_*` piece; the hologram's own fracking acceptance test runs against a
surface the mod never configured, and the well silently stops being buildable — the exact failure H2b exists to
prevent — with `WELLH2B-APPLY` still printing "every piece blocks BuildGun".

**And the dump cannot see it.** `NodeShuffleWellVisualsApply.cpp:211` keys the collision dump on
`FString::Printf(TEXT("%s|%d"), *Actor->GetPathName(), MyPieces)`, where `MyPieces` counts only
`NodeShuffleWellMesh_*` components. An engine-added `mBoxComponent`/`mDecalComponent` does not change `MyPieces`, so
`WELLH2B-COLLISION` **will not re-fire** and the only per-primitive evidence stays throttled off.

**Mitigating precedent, and it is worth weighing:** this project already calls `RebuildNodeNativeVisual` on
**freshly spawned runtime nodes** (`NodeShuffleSubsystem.cpp:4299`, `:4317`), and the comment there records a
measurement — *"the engine leaves a runtime node's decal invisible"* — which is why the mod dresses its own decal.
That is empirical evidence the native rebuild is largely inert on a runtime-spawned node. It is evidence about
`AFGResourceNode`, not about `AFGResourceNodeFrackingCore`. **Grade: assumed.** Runtime steps 4 and 5.

**What I would ship (optional, cheap):** append `|box%d` to the dump key so the throttle notices a change in the
*total* primitive count, not just our pieces:

```cpp
        // T17 F3: the engine's OnRep rebuild can attach mDecalComponent/mBoxComponent, which do not
        // change MyPieces -- so keying on our piece count alone would leave the one per-primitive
        // dump throttled off in exactly the pass that gained a competing build-gun surface.
        TInlineComponentArray<UPrimitiveComponent*> AllPrims(Actor);
        const FString DumpKey = FString::Printf(TEXT("%s|%d|%d"), *Actor->GetPathName(), MyPieces, AllPrims.Num());
```

That file is outside this packet's set — file it as a follow-up rather than widening the diff.

---

### F4 — LOW-MEDIUM · `Wrote` ships without its denominator, and its disagreement with `StaleRes` is unasserted
`NodeShuffleWellSpawn.cpp:418-428` prints `%d rewritten this pass` with no denominator. `StaleRes` was measured with
the *same* predicate `RetypeWellMember` re-tests (`mResourceClassOverride.Get() != ResourceClass`), so
`Wrote == StaleRes` is an invariant, not a coincidence. Any divergence means something real: the same actor appeared
twice in `LiveSpawnedSats`, or an actor was invalidated between the measure and the write.

**Failure scenario:** two satellite records whose `PlacedLocation`s are within `WellAdoptMatchRadiusCm` (300 cm) both
late-adopt the *same* runtime actor. `LiveSpawnedSats` holds it twice, `StaleRes` counts 2, `RetypeWellMember` returns
false on the second call, and the line prints `2 of 8 disagreed, 1 rewritten` — which reads as a half-failed write and
is actually a duplicate handle. Nobody can tell which from the log.

**What I would ship:**

```cpp
            // T17 F4: Wrote is measured with the SAME predicate StaleRes was, so they MUST agree.
            // A divergence is a duplicate handle in LiveSpawnedSats or an actor invalidated mid-pass,
            // and it is the only thing this line cannot otherwise distinguish from a partial failure.
            if (Wrote != StaleRes)
            {
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("WELLH2-RETYPE core='%s': %d member(s) measured stale but %d were written. These are "
                         "the SAME predicate, so they cannot legitimately differ -- suspect a duplicate handle "
                         "in the spawned-satellite map or an actor invalidated between the measure and the write. "
                         "NOT MEASURED: which."),
                    *WellShort(E.CorePath), StaleRes, Wrote);
            }
```

and change the summary text to `%d of %d rewritten this pass`.

---

### F5 — LOW (latent, not reachable today) · the "spawned satellite population" is now derived by two different filters
`EvaluateWellPin` builds its candidates from `SpawnedWellSatellites.Find(S.SatellitePath)` over **every** record.
`SpawnWellGroup` builds `LiveSpawnedSats` from records that survive `E.bOffsetsCaptured && S.bCaptured &&
!zero-offset` (`:204-221`) plus a successful spawn/reuse. These are different rules producing the same set.

I traced whether they can disagree. They cannot, today, because a third invariant holds in three separate places:
`AdoptRestoredWellGroups` skips `!S.bCaptured` (`NodeShuffleWellLink.cpp:304`, `:329`), `SpawnWellGroup`'s late-adopt
is unreachable past the uncaptured `continue`, and `S.bCaptured` is only ever set **true**
(`NodeShuffleWellRelocateRoll.cpp:763`, "the ONLY place this is ever set true"). So no handle can exist for an
uncaptured record, and the two filters agree by construction.

That is precisely the shape the header block warns about — *"the 'one rule in two places' shape that caused T8 and
ns-review-h2 F3"* — reintroduced one layer down, in the *population selection* rather than the test. If a future
packet ever establishes a handle for an uncaptured record, `ApplyWellRetype` would pin the well and `SpawnWellGroup`
would retype it in the same pass.

**What I would ship** — a cheap self-check that names the invariant at the seam rather than only in prose, right
before the `StaleRes` loop:

```cpp
    // T17 F5: LiveSpawnedSats and EvaluateWellPin's candidate list are built by DIFFERENT filters and
    // agree only because no handle is ever established for an uncaptured record (WellLink.cpp:304/:329,
    // RelocateRoll.cpp:763). Assert it AT THE SEAM -- if that ever stops holding, this pass would retype
    // a well ApplyWellRetype pins, and nothing else would say so.
    if (LiveSpawnedSats.Num() != SpawnedHandlesForThisEntry) { /* Warning naming both counts */ }
```

Cheapest concrete form: count `SpawnedWellSatellites.Contains(S.SatellitePath)` over all of `E.Satellites` into a
local and compare. Low priority — file it if the diff should stay minimal.

---

### F6 — LOW (honesty) · the `WELLH1` parenthetical qualifies the wrong subset
`NodeShuffleWellRetype.cpp:340-342` now says `groupPlaced=%d (1 => written=/equal= describe the HIDDEN VANILLA
originals only …)`. That is a genuine improvement and the line is now honest about `written=`/`equal=`. But `res=` is
`E.AssignedResourceClassPath` — **the layout** — in *both* cases, and the parenthetical does not say so. On a
`groupPlaced=1` well after a re-roll but before convergence, `res=` describes neither population.

**Wording I would ship:**

```
"groupPlaced=%d (res= is ALWAYS THE LAYOUT, never a measurement; 1 => written=/equal= describe the "
"HIDDEN VANILLA originals only, and the relocated actors a player sees are reported by WELLH2-RETYPE)"
```

Verified against the reference log: existing lines read `res='Desc_Gas_Chlor_C' (was 'Desc_Water_C') …
written=0 equal=1` — i.e. `equal=1` on hidden originals for wells the player sees as something else entirely. The flag
is the right fix; the sentence just needs to cover `res=` too.

---

### F7 — STYLE NOTE (not a defect, but it constrains the test) · convergence is proximity-gated, suppression and the audit are not
`NodeShuffleWellRelocateApply.cpp:501` gates the maintenance `SpawnWellGroup` on
`IsLocationNearAnyPlayer(E.PlacedCoreLocation, SpawnRadiusCm)`. `SuppressVanillaWellGroup`, `ApplyWellGroupVisuals`
and `ApplyWellRetype` are **not** gated. So after a re-roll, a distant relocated well keeps the old resource in the
world (nothing observable there) while H1 converges the hidden originals and the audit prints the layout. Correct
behaviour; it means **step 2 of the checklist must fly to the well, and the log must be read after arrival, not from
the moment of the roll.** Stating it so nobody grades the fix failed from a log taken 5 km away.

### F8 — STYLE NOTE · duplicate handles inflate both sides of `%d of %d`
Two records adopting one actor (see F4) inflates `StaleRes` and `LiveMembers` together, so the ratio stays plausible
while both numbers are wrong. Pre-existing exposure shared with `EvaluateWellPin`; F4's assert is the cheapest detector.

---

## 1b. COMPILE-AS-A-COMPILER (never built — this is inspection, not a compiler)

Checked, and **clean**:

* **`EvaluateWellPinOnActors` inclusion / ODR.** `inline`, defined in `NodeShuffleWellRetype.h`, included by both
  `NodeShuffleWellRetype.cpp:28` and `NodeShuffleWellSpawn.cpp:36`. Defined *above* its only in-header user. No cycle:
  `NodeShuffleWellRelocate.h` and `NodeShuffleSubsystem.h` do not include this header.
* **Field access.** The helper touches only `IsWellMemberInUse` (a free `inline` in `NodeShuffleWellRelocate.h:79`,
  null-safe: `if (!IsValid(Node)) return false;`) and `AActor::GetName()`. **No private field is read from the free
  function**, so the class-to-class Friend grant is not needed there. The private `mResourceClassOverride` reads at
  `WellSpawn.cpp:359/362/381` are inside `ANodeShuffleSubsystem::SpawnWellGroup`, which already writes that field at
  `:169`/`:285`.
* **Overloads / conversions.** `AFGResourceNodeFrackingCore : AFGResourceNodeBase` and
  `AFGResourceNodeFrackingSatellite : AFGResourceNode : AFGResourceNodeBase` (verified in the real headers), so both
  bind to `RetypeWellMember(AFGResourceNodeBase*, …)`. `AFGResourceNode` completeness comes from
  `#include "Resources/FGResourceNode.h"` at `WellSpawn.cpp:38`. `WellPathOf(const UObject*)` accepts `const UClass*`.
* **`EvaluateWellPin`'s surviving call site binds.** `NodeShuffleWellRoll.cpp:220-221` still passes
  `(E, SpawnedWellCores, SpawnedWellSatellites, Core, OriginalSats)` — signature unchanged. The handoff asked a
  reviewer to confirm this; **confirmed.**
* **`UE_LOG` specifier/argument counts** — hand-counted, all three lines balance:
  * `WELLH1` (`WellRetype.cpp:339`): 8 specifiers `%s %s %s %d %d %d %s %d`, 8 args, order matches.
  * `WELLH2-RETYPE-PIN` (`WellSpawn.cpp:393`): 12 specifiers, 12 args, order matches.
  * `WELLH2-RETYPE` (`WellSpawn.cpp:418`): 8 specifiers, 8 args, order matches.
  * `Pin.CoreWhy` / `Pin.SatelliteWhy` / `WellPinSourceName(...)` are `const TCHAR*` and correctly passed **without**
    a `*`; `FString`s are correctly passed **with** one. The
    `Pin.FiredActorName.IsEmpty() ? TEXT("<none>") : *Pin.FiredActorName` ternary resolves to `const TCHAR*` and is the
    identical pattern already compiling at `WellRetype.cpp:283`.
* **The two `TEXT()` traps named in the brief.** Neither is hit. The new `WellSpawn.cpp` lines use
  `TEXT("a") TEXT("b")` adjacent-macro concatenation (established throughout this file, e.g. `:70-71`, `:211-215`); the
  new `WellRetype.cpp` line uses `TEXT("a" "b")` single-macro concatenation (established at `:74-76`, `:395-402`).
  Both are valid and both already compile in these exact files. **No ternary appears in a verbosity slot** — the two
  new `UE_LOG`s use bare `Warning` / `Display`.
* **Line endings.** `git` warns `NodeShuffleWellRetype.h` is LF in the working copy where the other two are CRLF.
  Cosmetic for MSVC; normalise if the repo cares.

**Import surface.** Every engine entry point on the new path is already reached from these translation units:
`GetResourceClass()` (`WellRetype.cpp:49`), `GetName()`, `GetPathName()`, `ProcessEvent` via `RebuildNodeNativeVisual`,
`GetActivator()` / `GetExtractor()` / `IsOccupied()` via `IsWellMemberInUse`. **Predicted delta: zero.** Per
`memory:ue-import-table-must-be-measured` that stays a prediction — checklist step 9.

---

## 2. DIFFERENTIAL / PARITY TABLE

### 2a. The pin extraction — `EvaluateWellPin` → `EvaluateWellPin` + `EvaluateWellPinOnActors`

| Invariant the OLD (T16) path guaranteed | How the NEW path provides it | Grade |
|---|---|---|
| The occupancy test is ONE body — core `IsWellMemberInUse` first, then satellites, first-hit-wins with `break`, `FiredActorName` only filled if empty | The body was **moved verbatim** into `EvaluateWellPinOnActors`; `EvaluateWellPin` now has no test of its own. Line-by-line diff of the moved block shows no edit | **provably provided** |
| `ResolvedCore` is only set from a *valid* pointer | Old code applied `IsValid` in the original branch and relied on a prior `IsValid` in the spawned branch. New code applies `if (IsValid(ResolvedCore))` once, in the helper, covering both. Identical outcome | **provably provided** |
| `CoresTested`/`SatellitesTested` derive from the resolved pointer and the candidate array, and `Source` flips to `Nothing` when both are 0 | Moved unchanged; `Source` is now passed in and then overwritten by the same `Nothing` branch | **provably provided** |
| `SatellitesExpected` is reported, never decisive | Passed as a parameter, still only stored | **provably provided** |
| `Source` is never left `NotEvaluated` | Both branches of `EvaluateWellPin` assign the local before the call; the T17 call site passes `Spawned` literally | **provably provided** |
| Both original consumers behave exactly as at T16 | `NodeShuffleWellRoll.cpp:221` and `NodeShuffleWellRetype.cpp:210` call the unchanged 5-arg `EvaluateWellPin`; verified both by reading them | **provably provided** |
| The population asked is the one a player can build on | At the T17 call site the population is the just-resolved spawned group, which is stronger than T16's map lookup (it works before `bGroupPlaced` is set). But it is derived by a **different filter** — see F5 | **assumed** (F5; step 8) |

**Answering the three questions in the brief directly.**
*Is it a genuine pure extraction?* **Yes** — no second occupancy rule was born; there is exactly one
`IsWellMemberInUse` loop in the project's pin path.
*Is the population right at that moment?* **Yes.** T16's selector is gated on `E.bGroupPlaced`, which the caller sets
only after `SpawnWellGroup` returns, so calling `EvaluateWellPin` here would have tested the **hidden originals** —
the exact T16 defect. Bypassing selection is correct, not a shortcut.
*Could a well be retyped here that T16 would have refused?* Only via F5's uncaptured-record case, which I traced and
found **unreachable in current code**. Could one be refused here that T16 would have retyped? No — the T17 gate is the
same expression, and `Pin.IsDecisive()` is structurally true inside `if (IsValid(Core) …)`, so no non-decisive path
exists to diverge on.

### 2b. The write path — fresh-spawn-only → "a live handle exists"

| Invariant the OLD path guaranteed | How the NEW path provides it | Grade |
|---|---|---|
| Purity is written **only** at a satellite's fresh spawn, and never normalised across a group | The new block calls only `RetypeWellMember`, whose body writes `mResourceClassOverride` and nothing else; `mPurityOverride` appears nowhere in `NodeShuffleWellRetype.cpp` or in the new block. `RebuildNodeNativeVisual` drives `OnRep_ResourceClassOverride`, and `mPurityOverride` is `Replicated` with **no** `ReplicatedUsing`, so that OnRep cannot reach it | **provably provided** (the field-write half) / **assumed** (that the engine rebuild does not touch purity — step 6) |
| The steady state is a pointer compare: no write, no visual rebuild, no log | `StaleRes` is computed by `mResourceClassOverride.Get() != ResourceClass` over ≤11 actors; the whole block is skipped at 0. `RetypeWellMember` re-tests the same compare at `:47`. Nothing in the project writes the spawned actors' override except this block, so it cannot oscillate | **provably provided** |
| The resource is correct the instant `BeginPlay` runs | Unchanged — the fresh-spawn pre-`FinishSpawning` writes at `:169`/`:285` are untouched, and `StaleRes` is 0 for a fresh spawn so the new block is a no-op on that path | **provably provided** |
| `mResourceClassOverride` is `SaveGame` and survives reload | Unchanged field, unchanged write mechanism | **provably provided** |
| Convergence precedes anything that observes the resource | First-placement site: `SpawnWellGroup` → `SuppressVanillaWellGroup` → `ApplyWellGroupVisuals` → `AuditOneWellGroup` (`RelocateApply.cpp:467-486`). Maintenance site: `SpawnWellGroup` → suppress → dress (`:503-513`). But the maintenance call is **proximity-gated** while suppression and the audit are not (F7) | **provably provided** for ordering; **assumed** for reach (step 2) |
| Both cores and satellites are covered on every branch | `LiveSpawnedSats.Add(Sat)` sits after `RegisterNodeWithManager` at the loop tail, past every `continue`; the core is handled separately. Fresh, reuse and both adopt-late paths all fall through to it | **provably provided** |
| A well someone built on is not retyped | Same expression as `ApplyWellRetype:248`, evaluated on the spawned population. But the *bookkeeping* consequence differs — F1 | **assumed** (F1; step 8) |
| The visual/collision state of a relocated well is preserved | `RebuildNodeNativeVisual` now runs on a spawned fracking actor for the first time. Cannot remove our components; may add engine ones (F3). `ApplyWellGroupVisuals` repairs our side in the same pass | **assumed** — engine-internal `ProcessEvent`, may never be graded higher (steps 4, 5) |
| The T2 snap box survives | `EnsureWellMemberSnapBox` is re-run inside `DressWellActor` on every maintenance pass, after the retype | **assumed** — its input is our piece bounds, which the rebuild cannot change, but the *contest* for the build-gun trace can (step 4) |
| No new DLL import surface | Every call already exists in these TUs | **assumed** — never gradeable from headers (step 9) |

---

## 3. ALTERNATIVES

**A. Converge inside `SpawnWellGroup`, keyed on a live handle (what was built).**
*Pro:* one site covers fresh / reuse / adopt-late / re-roll-under-placed; reuses the existing idempotent
`RetypeWellMember`; the population is already resolved there so nothing is re-derived (which is the T16 defect); the
`ResourceClass` argument the caller already passes every pass finally gets consumed. *Con:* it puts a *retype* in a
file whose stated seam is *materialisation*, and it inherits the proximity gate (F7).

**B. Write at each of the four handle-establishing sites** (fresh core, fresh sat, adopt-late core, adopt-late sat,
reuse). *Pro:* no new block, no pin evaluation at all. *Con:* **reject.** Five copies of one rule is exactly the T8 /
ns-review-h2 F3 shape, it still misses the re-roll-under-an-already-placed-group case (no handle is *established*
there, one already exists), and it puts a resource write inside a deferred-spawn window.

**C. Extend `ApplyWellRetype` to resolve the spawned actors too** — try `SpawnedWellCores.FindRef(E.CorePath)` before
`FindOriginalBaseByPath`. *Pro:* puts the retype in the retype file; picks up the proximity-free cadence, closing F7;
`ApplyWellRetype` already owns `bPinned`/`bManaged`, which dissolves **F1 entirely**. *Con:* it must then write *two*
populations per well (originals stay live and hidden and must keep their assignment for the un-relocate path), which
doubles the assert surface and the `equal=` semantics; and it needs the same `bAlreadyApplied` correction that F1
Option B describes. **This is the strongest alternative and it is not obviously worse.** It is a larger diff in a file
that landed hours ago.

**D. Do nothing until the consequence is observed.** The consequence is genuinely unobserved (`docs/TECH-DEBT.md`
T17's own retraction is correct and well-earned). *Con:* the defect is proven statically and decisively, and the
reference log shows the adopt path is the normal path — 15 of 15 groups adopted. Waiting means a user hits it first.

**Recommendation: ship A** (the current approach), plus F1 Option A's diagnostics and F2's positive line, and **file C
as the design target for a T18 packet.** A is correct, minimal, and reversible; C is where this should end up once
someone owns both retype files and can move `bPinned`/`bManaged` ownership with it. A beat C on blast radius, not on
architecture — say so in TECH-DEBT so the next packet does not have to rediscover it.

---

## 4. QUESTION-BY-QUESTION

**Q1 pin extraction** — genuine pure extraction; no second rule; correct population; no divergence reachable today
(F5 is the latent one). §2a.
**Q2 idempotence** — holds. `StaleRes` is a pointer compare, `RetypeWellMember` re-tests the same compare, and
**nothing else in the project writes the spawned actors' `mResourceClassOverride`** (`ApplyWellRetype` reaches only
`VanillaNodeCache`, which skips our nodes). No oscillation path exists. Steady state costs one compare per member per
pass and prints nothing. Confirm with step 7.
**Q3 `RebuildNodeNativeVisual`** — **it cannot undress a working well**; it can *add* a decal/box collider that
competes for the build-gun trace, and `WELLH2B-COLLISION`'s throttle will not re-fire to show it. Graded *assumed*.
Steps 4, 5. F3.
**Q4 purity** — untouched. `RetypeWellMember` writes one field; `mPurityOverride` is `Replicated` with no
`ReplicatedUsing`, so the OnRep this triggers cannot reach it; the only purity write in the packet remains the
fresh-satellite spawn at `WellSpawn.cpp:292`. Step 6 confirms in-world.
**Q5 ordering and reach** — convergence runs before suppression, dressing and the audit at both call sites. Coverage
of satellites is complete on every branch that leaves a handle. The one gap is reach, not order: the maintenance call
is proximity-gated (F7).
**Q6 diagnostics** — `groupPlaced=` makes the `WELLH1` line honest about `written=`/`equal=` but not about `res=`
(F6). Both new lines separate MEASURED from NOT MEASURED and assert no causes — good. `StaleRes` ships with a
denominator; **`Wrote` does not** (F4). The block is **silent on success**, which is the worst diagnostic gap here
(F2).
**Q7 compile** — see §1b. No blocking issue found by inspection. Not a compiler.
**Q8 `NodeShuffleWellAudit.cpp`** — **agree with the packet, and it is worse than "unmeasured".** `:123-127` prints
`res=` from `*WellShort(E.AssignedResourceClassPath)` — the layout — while `:53` already holds
`SpawnedWellCores.FindRef(E.CorePath)`, i.e. the actor whose `GetResourceClass()` would be the measurement. The
acceptance gate therefore **cannot fail for this defect class**: `bHealthy` is built from counts, positions and links,
and never from resource. It would have printed `-- OK` throughout the entire pre-T17 defect. Follow-up packet: add the
spawned core's measured `GetResourceClass()` to the `WELL [phase]` line and make it a `bHealthy` term. Correctly out
of this packet's file set.

---

## 5. VERDICT

**SHIP WITH TESTS** — the write is correct, idempotent, non-oscillating, purity-safe and ordered ahead of every
observer; what is unsettled is one unrecorded pin, a success path that prints nothing, and an engine rebuild that runs
on a spawned fracking actor for the first time.

Runtime checklist, one line each (full form in §0):

1. `WELLH1` prints `groupPlaced=1` for relocated wells — else the DLL is stale, stop.
2. Re-roll with a **new seed**, fly to a changed well, expect one `WELLH2-RETYPE` with `rewritten == disagreed`.
3. The well's visible resource and a Pressurizer's output are the NEW resource — eyes, not the log.
4. Same well still dressed, core still snappable, and no new decal/box competing for the build gun (F3).
5. A Well Extractor still snaps to a rewritten satellite.
6. Two satellites' purities unchanged and still mixed across the group.
7. Stay ~60 s: `WELLH2-RETYPE` appears **once**, never repeats.
8. Pressurizer + re-roll ⇒ one `WELLH2-RETYPE-PIN`, no world change, and check whether `WELLH1-PIN`/`-SKIP` follows — absent confirms F1.
9. `check_imports.ps1` / `dumpbin` on the built DLL: zero delta against the H0 baseline.
