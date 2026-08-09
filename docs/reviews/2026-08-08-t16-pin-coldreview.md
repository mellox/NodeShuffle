# Cold review — T16 pin predicate (`ns-t16-pin`), uncommitted, never compiled

**Reviewer:** cold-reviewer (did not author the change)
**Repo:** `C:\Claude\Projects\NodeShuffle` · branch `wip/h2-relocation` · base `b19f7fc`
**Under review:** working-tree diff over three files —
`Source/NodeShuffle/Private/NodeShuffleWellRetype.h`,
`Source/NodeShuffle/Private/NodeShuffleWellRetype.cpp`,
`Source/NodeShuffle/Private/NodeShuffleWellRoll.cpp`
**Not read / not popped:** `stash@{0}`. `NodeShuffleWellRelocateRoll.cpp` was read at **HEAD only**, and only
lines 125-165, to verify the handoff's §2.3 #1 claim.
**Nothing was built, committed or edited.** This file is the only artifact.

---

## VERDICT: **SHIP WITH TESTS**, and land one companion one-liner in the same build

The predicate is genuinely shared, the non-relocated population is byte-identical to before, the format
strings are correct, and there is no include cycle. Two things must land or be measured before this is
safe to leave in front of a player, and one of them is a change the packet deliberately did not make.

Runtime checklist, one line each (full text in §5):

1. First re-roll after load: every `groupPlaced=1` well must read `pinSrc=spawned(our relocated actors)` and `decisive=1`; `RelocatedFallback` in the summary must be 0.
2. Build a Pressurizer on a relocated well, re-roll: `pinned=1 managed=0 pinSrc=spawned … coreInUse=1(pressurizer-on-core) decisive=1`, then `WELLH1-SKIP … reason=PINNED`, and the well keeps producing what it produced.
3. Same well, **before** re-rolling and **after**: read `WELLH1-PIN … The core the pin was READ FROM ('X') now holds 'Y'` and compare `Y` to the well's `WELLH1-ROLL … -> 'Z'` line. **If Y ≠ Z, finding F2 is confirmed live.**
4. With SF+ installed: after step 2, try to build **another** Well Extractor on that well's satellites. A refusal is F1, not a build error.
5. Reload after step 2: the pinned relocated group must still be placed, linked and suppressed — no `WELLH2-STUCK`, no `*** ABANDONED IN PLACE ***`, no second `WELLH2-SPAWN`.
6. Dismantle everything, re-roll: same well must read `pinned=0 … decisive=1` and become a recipient again.
7. Relocation OFF save: a well with a pressurizer must read `pinned=1 … pinSrc=original(level actors)`; one without, `pinned=0`. Regression check on the untouched population.
8. `WELLH1-ROLL pool` lines must read `world N -> N` for every resource on every roll above.
9. `check_imports.ps1` / `dumpbin` on the built DLL against the H0 744/0 baseline.
10. Roll summary arithmetic: `managed + PINNED + no-authored + undealt` must equal the well total, and the three PIN-PROVENANCE counts must equal the number of `pinned=1` per-well lines.

---

## 0. THE FLAGGED EDIT — `AssignedResourceClassPath` re-sourcing (`NodeShuffleWellRetype.cpp:246-248`)

```cpp
const AFGResourceNodeFrackingCore* PinCore = Pin.ResolvedCore ? Pin.ResolvedCore : Core;
const UClass* LiveRes = PinCore->GetResourceClass().Get();
E.AssignedResourceClassPath = LiveRes ? LiveRes->GetPathName() : E.OriginalResourceClassPath;
```

**Is it correct?** Yes. **Is it necessary?** Yes — and more necessary than the packet argues, see F2.
**Can it change what an existing save's wells produce?** **No, not directly.** This branch `continue`s;
no actor is written on it. `RetypeWellMember` is never reached for a pinned well, and `E.bManaged=false`
keeps `ApplyWellRetype` out of the entry on every later pass. The only thing written is a layout string.

*Indirect* effects, both real and both already anticipated by the handoff:

- **The deck withdrawal takes a different card.** `NodeShuffleWellRoll.cpp:316` withdraws
  `Deck.RemoveSingle(E.AssignedResourceClassPath)`. Before, that string named the hidden original's
  class; now it names the spawned core's. When they differ, a *different* card leaves the deck and every
  other well's deal changes. That is the correct direction (the withdrawal must match what the world
  holds) but it is a real one-time re-shuffle of an existing save's wells on the roll after the first pin.
- **`UnmatchedTakes` is newly reachable.** If the spawned core holds a class that is not any well's
  authored resource, `RemoveSingle` misses, the `WELLH1-ROLL: a pinned well holds '%s'` warning fires and
  world totals drift by one card. Reachable via the adopt-late path (`NodeShuffleWellSpawn.cpp:125-137`,
  which adopts *any* runtime fracking core within `WellAdoptMatchRadiusCm` and never checks its resource)
  or another mod writing the override. Graded **assumed**; it is loud when it happens (checklist step 8).

`const AFGResourceNodeFrackingCore* PinCore` compiles: `GetResourceClass()` is already called through a
`const AFGResourceNodeFrackingCore*` at `NodeShuffleWellRetype.cpp:186` (`DescribeGroupState`), so
const-ness is proven by existing code in the same function.

**Verdict on the flagged edit: keep it.** It is the smallest change that makes the F2 pinned withdrawal
name the resource the player's well actually produces.

---

## 1. Findings, most severe first

### F1 — AUTO-ALLOW WITHDRAWAL: not proven to break a *running* machine, but it breaks *finishing the build*, and it is amplified from 3/20 wells to 20/20

`NodeShuffleSubsystem.cpp:6256` — `if (!Well.bManaged) { continue; }`.

**Traced concretely.** The chain is: pin ⇒ `bManaged=false` ⇒ the well emits neither its
`(SatelliteNodeClassPath | resource | form)` nor its `(CoreNodeClassPath | resource | form)` group ⇒ if no
other `bManaged` well supplies an equivalent key, the extractor that matched only on that evidence is not
written into `Mods/NodeShuffle/DataForge/NodeShuffleAutoAllow` ⇒ KDF applies the shrunken pack on the
**next boot** ⇒ SF+'s `mAllowedResourceExtractors` no longer contains it.

**Can an already-working extractor stop working? No — and here is the proof.** The SF+ allow-list is a
**placement** gate. Our own file says so twice, from measurement:
`NodeShuffleAutoAllowExtractors.cpp:440-444` — vanilla `Build_FrackingSmasher_C` is on SF+'s list and the
list "does not touch" the extractor's native node-type rule; and `NodeShuffleAutoAllowExtractors.cpp:445-449`
places the residual crash risk at *placement* time, in the hologram hooks. Nothing in the generator, in
KDF, or in the pack format removes or dismantles a built actor — the generator **only ever appends**
(`:444`, verbatim: "we only ever append, never remove"). A `AFGBuildableFrackingActivator` already in the
save is restored by the save system and never re-validated against the allow-list. **Not a blocker on that
axis.**

**What it does break is completing a well, and that is reachable in the ordinary build order.**
A player builds the Pressurizer on the core *first*. The pin fires on `pressurizer-on-core` at that
moment. The well is un-managed before a single satellite Well Extractor exists — and it is the satellite
group that the Well Extractor's evidence comes from. Next boot, the Well Extractor may no longer be
allow-listed and the player cannot finish the well they just pressurized. That is
`NodeShuffleWellRoll.cpp`'s own RT-6 sentence, verbatim: *"withdrawing the allow-list would leave the
player holding retyped wells they can no longer build on, which is strictly worse than either consistent
state."*

**Why it is worse after T16 than before.** The exclusion's stated rationale
(`NodeShuffleSubsystem.cpp:6243-6244`) is: *"a PINNED well — one a player has already built on, which
NodeShuffle leaves vanilla — is excluded for precisely the reason a pinned original is."* That sentence is
**factually false for a relocated well.** NodeShuffle spawned it, moved it, chose its coordinates and
chose its resource. The *only* reason a machine could ever be placed on it is evidence this census
emitted. The exclusion inherited a justification from a population it does not describe. Before T16 that
mismatch was unreachable (pinning never fired for relocated wells); after T16 it covers **15 of 20 wells
in the user's current save** (`WELLH2-ADOPT … 15/15 relocated well core(s) … re-matched`).

**How likely is the key to actually disappear?** A modded Well Extractor that restricts only to the
satellite node type and allows liquid+gas matches *any* managed well group, so it survives while ≥1 well
stays managed. The reachable case is a **resource-restricted** extractor: vanilla has only a handful of
nitrogen wells, so pinning those pins the key. Graded **assumed** — checklist step 4.

**Recommendation — land this companion one-liner in the same build.** Testing T16 in-game *requires*
building on a relocated well, which is exactly the act that triggers the withdrawal; shipping T16 without
it means the first test session is also the first exposure. Narrowest correct change, leaving every
non-relocated well byte-identical:

```cpp
    for (const FNodeShuffleWellEntry& Well : WellLayout)
    {
        // T16-followup (2026-08-08): A PINNED **RELOCATED** WELL STILL CONTRIBUTES ITS EVIDENCE.
        // The `!bManaged` skip that used to stand alone here inherited its rationale from the ordinary-
        // node population -- "a pinned well is one NodeShuffle leaves vanilla, so its placement question
        // was settled before we ran". That sentence is FALSE for a well H2 RELOCATED: we spawned it, we
        // moved it and we chose the resource it carries, so the ONLY reason a machine could be on it is
        // evidence THIS census emitted. Withdrawing it the moment a player pressurizes the core lands
        // BETWEEN the pressurizer and the satellite extractors -- i.e. mid-build -- which is
        // RollWellLayout's own RT-6 failure ("withdrawing the allow-list would leave the player holding
        // retyped wells they can no longer build on"). Unreachable before T16, because pinning never
        // fired for a relocated well at all.
        // NARROWEST POSSIBLE WIDENING: only bGroupPlaced entries. A pinned NON-relocated well is still
        // excluded, exactly as ns-review-g2 F2 decided, for exactly its own reason.
        const bool bContributesEvidence = Well.bManaged || (Well.bPinned && Well.bGroupPlaced);
        if (!bContributesEvidence) { continue; }
        // Keyed on what it ACTUALLY holds. The apply-time pin maintains AssignedResourceClassPath from
        // the core the pin was resolved against (T16), so for a pinned relocated well this names the
        // resource the player's well produces, not a layout value the world may have diverged from.
        if (Well.AssignedResourceClassPath.IsEmpty()) { continue; }
        ++OutTotalActiveEntries;
        UClass* WellResource = LoadClassByPath(Well.AssignedResourceClassPath);
```

(Everything below that line is unchanged.) This is a reviewer-specified verbatim fix — the only class of
review-response change this workspace has landed clean.

---

### F2 — NEW, out of the diff's scope, and it changes T16's own premise: a relocated well's SPAWNED actors are never re-typed by a re-roll

`NodeShuffleWellSpawn.cpp:139-185` and `:239-290`. `Core->mResourceClassOverride = ResourceClass` and
`Sat->mResourceClassOverride = ResourceClass` are **inside the `if (!IsValid(Core))` / `else` fresh-spawn
branches only**. On the reuse path and on the adopt-late path the resource is *not* re-written.
`ApplyWellRetype` resolves its members through `FindOriginalBaseByPath`, i.e. the **hidden originals**
(the VanillaNodeCache "excludes only OUR spawned nodes", `NodeShuffleSubsystem.cpp:2210-2211`), so it
never touches the spawned actors either. `RollWellRelocation`'s `bGroupPlaced` branch keeps the placement
and does not despawn.

**Failure scenario, concrete.** In the user's current log: 15 relocated groups were **adopted**
(`WELLH2-ADOPT … 15/15 … re-matched by location`, and there is not one `WELLH2-SPAWN … spawned … res='…'`
line in that session), and the re-roll reported `16 actually changed resource`. Those 16 assignments were
written to hidden originals. **The 15 visible, buildable, player-facing wells are still producing their
pre-re-roll resources while the layout, the roll log and the conservation lines all say otherwise.**

**Consequences for this review:**

- It makes §0's re-sourcing not merely correct but **the only way to state the truth** — reading `Core`
  would have recorded a resource no player can obtain from that well.
- It **contradicts `docs/TECH-DEBT.md` T16's stated consequence** ("a re-roll can retype a relocated well
  the player has built on … The player's chlorine setup silently becomes water"). The retype cannot reach
  the spawned actors at all. The real defect is the inverse and quieter: the layout lies about what the
  well produces. The handoff's §5 already declined to quote that mechanism forward; this review supplies
  the reason it should not be.
- T16's diagnostics **expose it**: `WELLH1-PIN … The core the pin was READ FROM ('X') now holds '%s'`
  compared against that well's `WELLH1-ROLL … -> 'Z'` line is a direct read-out of the divergence.

**Grade: code-read and log-corroborated, NOT observed in-world.** Do not quote it as measured. Checklist
step 3 settles it in one re-roll. **File it as a new tech-debt item; do not fix it in this packet** — the
fix (re-assert the resource on the maintenance path) writes live actors under a player's machinery and is
a path-replacing change of its own.

---

### F3 — The predicate is shared; the **discipline** is not. The apply treats a non-decisive verdict as decisive, and it is guaranteed non-decisive on the first apply pass of every session

`NodeShuffleWellRetype.cpp:229` reads `(Pin.bCoreInUse && !bAlreadyApplied) || Pin.bSatelliteInUse` and
never consults `Pin.IsDecisive()`. `NodeShuffleWellRoll.cpp:223-224` does. So the roll refuses to *clear*
a pin on a verdict it does not trust, while the apply proceeds to *write the retype* on the identical
verdict.

**Failure scenario, guaranteed-reachable by construction, not hypothetical.** `ApplyLayout` calls
`ApplyWellRetype` at `NodeShuffleSubsystem.cpp:2215` and `ApplyWellRelocation` at `:2224`.
`AdoptRestoredWellGroups` — the only thing that populates `SpawnedWellCores` at load — runs *inside*
`ApplyWellRelocation` (`NodeShuffleWellRelocateApply.cpp:401-405`). Therefore **on apply pass 1 of every
session, `SpawnedWellCores` is empty, every `bGroupPlaced` well takes `Source = OriginalWhileRelocated`,
and every relocated well's live pin re-check asks a hidden, de-collided, deregistered actor** — the exact
false negative T16 exists to remove. From pass 2 (~5 s later) it is correct.

**Harm is bounded but non-zero:** the writes land on hidden originals only (F2), so nothing player-visible
changes; but the entry stays `bManaged` for that pass, `E.AssignedResourceClassPath` is not re-sourced,
and `RetypeWellMember` calls `RebuildNodeNativeVisual` on suppressed actors (re-hidden by
`SuppressVanillaWellGroup` on the same pass — **assumed**, engine-internal `ProcessEvent` path).

**Recommendation: log it, do not restructure.** Hoisting `AdoptRestoredWellGroups()` above
`ApplyWellRetype` would fix it properly but is a load-time reordering with seven review rounds behind the
current sequence — not a scoped fix's call. Ship this instead, immediately after the
`EvaluateWellPin(...)` call at `NodeShuffleWellRetype.cpp:209-210`:

```cpp
        // T16, THE DISCIPLINE GAP, NAMED RATHER THAN ARGUED AWAY. The roll refuses to CLEAR a pin on a
        // non-decisive verdict; this apply does NOT refuse to WRITE on one, and that asymmetry is
        // deliberate for now -- refusing here would stop the retype of a relocated well every pass on
        // which its spawned handle happens not to have resolved, which is a behaviour change no part of
        // this packet asked for. It is GUARANTEED to occur on apply pass 1 of every session: ApplyLayout
        // calls ApplyWellRetype (NodeShuffleSubsystem.cpp:2215) BEFORE ApplyWellRelocation (:2224), and
        // AdoptRestoredWellGroups -- the only thing that populates SpawnedWellCores at load -- runs
        // inside the latter. So the line below is expected once per session per relocated well and is
        // NOT expected afterwards; a repeat past pass 2 means an adopted handle went missing.
        if (!Pin.IsDecisive())
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH1-PIN-UNMEASURED core='%s' pass=%d resolvedAgainst=%s groupPlaced=%d "
                     "(tested %d core + %d/%d satellite actor(s)) -- MEASURED: no in-use signal on the "
                     "actors tested. NOT MEASURED: whether anything is built on this well, because the "
                     "population tested is not the one a player can build on. This pass writes anyway."),
                *WellShort(E.CorePath), WellApplyPasses, WellPinSourceName(Pin.Source),
                E.bGroupPlaced ? 1 : 0, Pin.CoresTested, Pin.SatellitesTested, Pin.SatellitesExpected);
        }
```

(Verbosity slot is a bare identifier; 7 specifiers, 7 args.)

---

### F4 — Diagnostic honesty: three wordings assert more than they measured

Each is small; together they are the exact defect class this packet claims to be removing.

1. **`NodeShuffleWellRoll.cpp:398-399`** — `PinnedViaSpawned` / else `PinnedViaOriginal`. A pin found on
   the **`OriginalWhileRelocated` fallback** is counted as `PinnedViaOriginal` and printed as *"pinned via
   the vanilla level actors"* — in the same log line that calls that population unmeasurable. Contradiction
   inside one sentence. (Near-unreachable in practice: a relocated well's original can only carry a
   building if it was pinned *before* relocation, which relocation refuses. Still wrong to print.)
   **Fix:** `else if (Pin->Source == ENodeShuffleWellPinSource::Original) { ++PinnedViaOriginal; }
   else { ++PinnedViaFallback; }` with a fourth counter and a fourth clause.
2. **`NodeShuffleWellRoll.cpp:477`** — `"%d PINNED (built on, and holding an authored resource)"` asserts a
   **cause**. `PinnedCarried` entries are pinned because a saved bool was never re-measured this roll, not
   because anything was observed. **Fix, verbatim:** `"%d PINNED (bPinned set -- see the per-well pinSrc
   for whether THIS roll measured it -- and holding an authored resource)"`.
3. **`NodeShuffleWellRetype.cpp:259-260`** — `reason=` still prints `"pressurizer-on-core (appeared before
   we retyped it)"` whenever `!bSatelliteInUse`, but `IsWellMemberInUse` may have fired on `IsOccupied`
   instead. Pre-existing; now *partly* self-correcting because `why=` carries the true signal on the same
   line. Leave, or change the literal to `"core-in-use"`.

**Arithmetic verified.** The four summary counts **do** sum: `Merged` is partitioned exactly once by
`NodeShuffleWellRoll.cpp:287/296/310` into `NoAuthoredResource` ∪ `PinnedTakes` ∪ `Recipients`, and
`Recipients = DealCount + (Recipients.Num() - DealCount)` regardless of which side `FMath::Min` takes.
`WellLayout.Num() == Merged.Num()` after the `MoveTemp`. ✓
**All four can fire**, and each of the four `pinSrc` values is reachable: `Spawned` (relocated, handle
adopted), `Original` (relocation off, or `bGroupPlaced=false`), `not-evaluated` (a well in the save whose
core did not stream in, or one of our own spawned cores hit by the F1 census filter at `:148`),
`OriginalWhileRelocated` (apply pass 1 — see F3 — and the narrow handle-lost routes).
`NOTHING-RESOLVED` is **unreachable from the roll** (`Core` is `IsValid`-checked at `:133`, so
`CoresTested` is always ≥1 there) and reachable from the apply only if `FindOriginalBaseByPath` returns
null, which `:153` already `continue`s on. Dead-but-harmless; it makes the enum total rather than partial.

The clause *"an unpinned verdict on those is not a measurement of anything a player could build on"* is
**honest and correctly scoped.** Keep it.

---

### F5 — The pin can now stick, though not forever, and only through one narrow gate

`Pin.IsDecisive()` is false only for `OriginalWhileRelocated` and `Nothing`. At roll time the only
reachable one is `OriginalWhileRelocated` = `bGroupPlaced == true` **and** no valid handle in
`SpawnedWellCores`. Measured evidence says that gate is normally shut:
`WELLH2-ADOPT … 15/15 relocated well core(s) and 98/98 captured satellite(s) re-matched by location`,
logged *at load time* and explicitly "before almost anything has streamed" — so the save system restores
all placed groups and adoption catches all of them, independent of where the player is standing. A
user-triggered re-roll therefore sees `Source = Spawned` for every placed well, and **a pin set by T16
can always be cleared by dismantling and re-rolling.** No stuck-forever pin.

Residual routes into a stuck pin, all narrow, all named by `decisive=0` in the log:
`DespawnWellGroup` dropped the handle while leaving `bGroupPlaced` true; another mod destroyed the spawned
core; a roll issued before the first `ApplyWellRelocation` of a session. **Graded assumed** — checklist
step 6 is its measurement. The asymmetry's *direction* (may set, may not clear) is the protective one and
is right.

---

### F6 — `bRelocate=false` while `bGroupPlaced=true`: re-audited, no destructive reader, and it **cannot** be settled statically

Confirmed the route: `NodeShuffleWellRelocateRoll.cpp:139-145` (HEAD), `if (!E.bManaged) { ++RefusedPinned;
E.bRelocate = false; continue; }` — reached *before* the `bGroupPlaced` branch, so the entry keeps its
placement and loses the "satellite list grew but has no capture" warning that lives inside that branch.

Re-audited every reader of `bRelocate` (scoped grep over `Source/NodeShuffle`):

| Reader | Guard | Verdict |
|---|---|---|
| `NodeShuffleWellRelocateApply.cpp:434` | inside `if (!E.bGroupPlaced)` | unreachable for a placed entry ✓ |
| `NodeShuffleWellClaim.cpp:431` | preceded by `if (E.bGroupPlaced) { …Remove…; continue; }` at `:422` | unreachable ✓ |
| `NodeShuffleWellSweep.cpp:139/167/173` | mid-assembly predicate is `!bGroupPlaced && …` | unreachable ✓ |
| `NodeShuffleWellDespawn.cpp:239`, `WellClaim.cpp:129/201/265/529`, `WellSpawn.cpp:98`, `Subsystem.cpp:6635` | log/telemetry only | reporting drift only ✓ |

**Honest answer to "can this be settled statically at all": no.** Three of the four `continue`-guards are
verifiable by reading (they are literal `bGroupPlaced` tests), but the *destructive* half of the question
— whether the actors survive a save round-trip in this flag tuple — runs through the save system,
`AdoptRestoredWellGroups`' actor iteration and `SuppressVanillaWellGroup`'s visibility state, all of which
are engine-internal. This project has had **seven** review rounds each close every prior finding and each
find a *new* tuple some reader mis-read (`NodeShuffleSubsystem.h:400-412`). A static audit of this surface
is worth exactly one grade: **assumed**. Checklist step 5 is the only thing that settles it.

---

### F7 — Compile-as-a-compiler: no blocking defect found

- **Include cycle:** none. `NodeShuffleSubsystem.h` and `NodeShuffleWellRelocate.h` each mention
  `NodeShuffleWellRetype.h` exactly once, both in **comments** (`Subsystem.h:1263`, `Relocate.h:5`) —
  verified, the handoff's claim holds.
- **Linkage/ODR:** `WellPinSourceName` and `EvaluateWellPin` are `inline`; `FNodeShuffleWellPinCheck` and
  `ENodeShuffleWellPinSource` are plain C++ (no `USTRUCT`/`UENUM`), so UHT is not involved and the header
  needs no `.generated.h`. `EvaluateWellPin` is now instantiated in ~19 TUs; all definitions are identical
  by construction (single header). ✓
- **Field/type access:** `SpawnedWellCores` / `SpawnedWellSatellites` are private members; both call sites
  are `ANodeShuffleSubsystem` member functions and pass by `const&` — access is checked at the call site. ✓
  Map value types match exactly. `Find()` on a `const TMap&` yields `ValueType* const*`; the code declares
  `AFGResourceNodeFrackingCore* const* FoundSpawnedCore`. ✓ The helper touches no `Friend`-granted field.
- **Upcasts:** `AFGResourceNodeFrackingCore*` and `AFGResourceNodeFrackingSatellite*` → `AFGResourceNodeBase*`
  need both types complete; `NodeShuffleWellRelocate.h:52` includes `Resources/FGResourceNodeFrackingSatellite.h`,
  which brings both. ✓
- **Lifetime:** `CoreWhy`/`SatelliteWhy` store the `TEXT("…")` literals `IsWellMemberInUse` writes through
  `OutWhy` — static storage duration, safe to keep. ✓ `*PinCore->GetName()` points into a temporary
  `FString` that lives to the end of the `UE_LOG` full-expression — the same idiom already at
  `NodeShuffleWellRetype.cpp:61` and `:372`. ✓
- **Format strings — independently recounted, all five agree with the handoff's §3 table:**
  roll per-well `UE_LOG` 9/9; `PinDetail` Printf 11/11; not-evaluated Printf 2/2; roll summary 14/14
  (1 `%s` + 13 `%d`); `WELLH1-PIN` 12/12. Every `%s` argument is a `const TCHAR*` (either
  `WellPinSourceName`, a `TEXT()` literal, a `CoreWhy`/`SatelliteWhy`, or a dereferenced `FString`);
  every `%d` argument is `int32` or an `int` ternary. Both `FiredActorName` ternaries have `const TCHAR*`
  on **both** arms — `Pin` is `const` at both sites, so `FString::operator*` selects the const overload. ✓
- **`UE_LOG` verbosity slot:** `Display`, a bare identifier, at every touched call. No ternary — the trap
  already documented at `NodeShuffleWellRetype.cpp:360-362`. ✓
- **`switch` on `ENodeShuffleWellPinSource`** has a `default:`, so no `-Wswitch` on the unlisted
  `NotEvaluated`. ✓
- **500-line limit:** `WellRoll.cpp` 488, `WellRetype.cpp` 411, `WellRetype.h` 208. ✓
- **Unused includes** (`FGBuildableFrackingActivator.h` / `…Extractor.h` in `WellRoll.cpp`) do not warn. ✓
- **Cost, not a defect:** `NodeShuffleWellRetype.h` now drags `NodeShuffleSubsystem.h` (and its
  `.generated.h`) into every TU that included it — 19 files. Compile time only.
- **Import surface:** every entry point reached is already reached by the shipped build
  (`GetActivator`/`GetExtractor`/`IsOccupied` via `IsWellMemberInUse`, `GetResourceClass`/`GetName` in this
  very file). **Prediction, not a claim — cap at `assumed`.** Settle with `check_imports.ps1` / `dumpbin`
  against the H0 744/0 baseline. Note that `IsWellMemberInUse`'s inline body is newly instantiated in
  `NodeShuffleWellRoll.cpp`; the DLL-level import set is what matters, and only the dump can say
  (`ue-import-table-must-be-measured`: "inline ⇒ no import" and "virtual ⇒ no import" have **both** been
  falsified here).

---

## 2. Differential / parity table

This change replaces an established predicate at two call sites. Grades are as defined: *provably
provided* = demonstrable statically from code read; *assumed* = plausible, unproven; *unverifiable
statically* = the mechanism is engine/toolchain-internal.

| Invariant the OLD path guaranteed | How the NEW path provides it | Grade |
|---|---|---|
| Core pin = `GetActivator().IsValid() \|\| IsOccupied()` | `Pin.bCoreInUse` ⇒ `IsWellMemberInUse(core)` ⇒ `Core->GetActivator().IsValid()` then `Node->IsOccupied()` — textually identical | **provably provided** |
| Satellite pin = any sat with `GetExtractor().IsValid() \|\| IsOccupied()`, first hit wins, `break` | `Pin.bSatelliteInUse`, same predicate, same `break` | **provably provided** |
| Apply stand-down condition `(core && !bAlreadyApplied) \|\| sat` — the ns-review-h1 W1 asymmetry | `NodeShuffleWellRetype.cpp:229` byte-identical shape over the two separated fields | **provably provided** |
| Roll-time satellite population = every valid `W.Members` actor | `OriginalSats` accumulates exactly the same actors in the same loop | **provably provided** |
| Apply-time satellite population = `Live` (resolved by path) | passed straight through as `OriginalSats` | **provably provided** |
| Non-relocated wells behave exactly as before | `bGroupPlaced=false` ⇒ `Source=Original`, `CoresTested≥1` ⇒ `IsDecisive()` ⇒ the `else if` clears exactly where the old unconditional write did | **provably provided** |
| Roll summary counts partition the well set | proved by hand from `:287/:296/:310` + `FMath::Min` | **provably provided** |
| `bPinned` survives a session (the "carried" semantics the new non-decisive branch relies on) | `UPROPERTY(SaveGame)` at `NodeShuffleSubsystem.h:325` | **provably provided** |
| A pinned well is never MOVED | `RollWellRelocation` refuses on `!bManaged`; the roll sets `bManaged=false` for every pin | **provably provided** |
| A relocated well's spawned handles are resolvable when the roll runs, so `Spawned` (not the fallback) is the normal source | `AdoptRestoredWellGroups` walks `TActorIterator` at first apply; log shows `15/15` and `98/98` adopted — but restoration order and completeness are save-system behaviour | **assumed** (step 1) |
| A pin, once set, can still be CLEARED when the player dismantles | requires a decisive verdict, i.e. a live spawned handle at roll time — see above | **assumed** (step 6) |
| A newly-pinned `bGroupPlaced` entry is not despawned and its claim is not withdrawn | three `bGroupPlaced` guards read and confirmed, but survival across a save round-trip runs through the save system | **assumed** (step 5) |
| `E.AssignedResourceClassPath` after a pin names a resource that is in the deck | true if the spawned core holds a well resource; the adopt-late path adopts by position without checking resource | **assumed** (step 8) |
| A player can still BUILD on a well they have built on (auto-allow evidence) | withdrawn by `!bManaged` — F1 | **assumed**, and expected to FAIL for a resource-restricted extractor (step 4) |
| `RebuildNodeNativeVisual` on a suppressed original does not un-hide it | `ProcessEvent` on a reflected `OnRep_`, re-hidden by `SuppressVanillaWellGroup` the same pass | **unverifiable statically** (engine) |
| No new symbol enters the DLL import table | every entry point already reached by the shipped build | **unverifiable statically** — *must* be `dumpbin`'d (step 9) |
| SF+ does not re-validate an already-built machine against the allow-list | our own measured notes say the list is a placement gate and the generator only appends | **assumed** (step 4) |

---

## 3. Alternatives, with trade-offs and a recommendation

**A. Source selection (the fix as built): relocated ⇒ spawned, else ⇒ original, fallback marked non-decisive.**
*Recommended — keep it.*

**B. Test BOTH populations and OR the results.** Strictly more protective on paper; removes the whole
"decisive" concept and the fallback. Author rejected it for log readability, which would be a weak reason
on its own — but the stronger reason is that it buys **nothing**: a relocated well's original is hidden,
de-collided and deregistered, so no building can exist on it; and `mIsOccupied` is not `SaveGame` while
`mActivator`/`mExtractor` are bare weak pointers re-established by the building's `BeginPlay`
(`NodeShuffleWellRelocate.h:69-75`), so the original's signals are false after every load regardless.
ORing would add only in-session staleness. **Rejected on merit, not on style.**

**C. Patch both call sites independently.** Smaller diff, no new header edges, no 19-TU include cost.
Rejected: `docs/TECH-DEBT.md` records T8 and `ns-review-h2 F3` as this project's two "one rule in two
places, only one complete" failures. This would be the third. **Correctly rejected.**

**D. New `NodeShuffleWellPin.h` instead of extending `NodeShuffleWellRetype.h`.** Would keep
`NodeShuffleSubsystem.h` out of 18 unrelated TUs. Costs a fourth well header in a packet whose headers are
indexed by hand-maintained "what lives where" blocks. **Reasonable either way; the author's call is fine.
Revisit only if compile times bite.**

**E. Persist the *provenance* of the pin, not just the bool** — e.g. a `SaveGame uint8 PinMeasuredAgainst`
so a later non-decisive roll knows the pin came from a decisive measurement. Would let a stuck pin
self-diagnose across sessions. Rejected for now: `bPinned` being `SaveGame` already gives the carried
semantics, and F5 shows the stuck gate is narrow. **Park it; revisit if step 6 fails.**

**F. Fix the ordering instead of the predicate** — hoist `AdoptRestoredWellGroups()` above
`ApplyWellRetype` in `ApplyLayout`. Would close F3 properly. Rejected for *this* packet: it is a load-time
reordering of a sequence with seven review rounds behind it. **Recommend the F3 log-only guard now, and
this as a separate, fully-reviewed packet if step 1 or 3 shows the pass-1 window mattering.**

**G. On the auto-allow (F1): three ways out.**
(i) leave it withdrawn and document — rejected, it contradicts RT-6 in the same file;
(ii) make the *pin* not clear `bManaged` — rejected, `bManaged` is what stops the retype and the
relocation, and widening it re-opens both;
(iii) **decouple "we own this well's resource" from "this well is evidence"** — the one-liner in F1.
**Recommend (iii).**

---

## 4. What still needs runtime verification (everything graded assumed / unverifiable)

Every row of §2 graded below *provably provided* maps to a numbered step in §5. There are no assumed items
without a step.

---

## 5. Runtime test checklist

Needs a save with `ShuffleResourceWells` **and** `RelocateResourceWells` on, holding at least one relocated
well. Grep targets: `WELLH1-ROLL well #`, `WELLH1-ROLL: … complete`, `WELLH1-PIN`, `WELLH1-SKIP`,
`WELLH2-ADOPT`, `AUTOALLOW`.

1. **The source selection is answerable at all.** Load, wait ~60 s, `Re-roll Layout`. Every well with
   `groupPlaced=1` must read `pinSrc=spawned(our relocated actors)`, `tested=1core+N/Nsat`, `decisive=1`.
   The summary's *"%d relocated well(s) had to fall back to the HIDDEN original"* must read **0**. Any
   nonzero value means that many wells were not tested by this fix at all — record it and re-roll standing
   near one of them.
2. **A well the player HAS built on.** Walk to a relocated well; build a Resource Well Pressurizer on the
   core and ≥1 Resource Well Extractor on a satellite; note what it produces. Re-roll. Required:
   `pinned=1 managed=0 pinSrc=spawned(our relocated actors) … coreInUse=1(pressurizer-on-core)` and/or
   `satInUse=1(extractor-on-satellite)`, `firedOn='<spawned actor name>'`, `decisive=1`; summary
   `PIN PROVENANCE (T16): ≥1 pinned via OUR SPAWNED relocated actors`; within ~10 s
   `WELLH1-SKIP core='…' reason=PINNED`; **and in the world, the pressurizer keeps running and the well
   still produces the same resource.**
3. **F2 — does the layout lie?** For the same well, capture its `WELLH1-ROLL … -> 'Z'` line *before* step 2
   and the `WELLH1-PIN … The core the pin was READ FROM ('…') now holds 'Y'` line *after*. If **Y ≠ Z**, F2
   is confirmed: the visible well never took the re-rolled resource. Also compare Y against what the well
   physically produces in-game. Report either way — this is the step that settles a tech-debt entry.
4. **F1 — the auto-allow withdrawal.** With SF+ installed, after step 2 try to build **another** Resource
   Well Extractor on a satellite of the newly-pinned well, and reboot once and try again. A refusal
   ("Invalid aim location!") or a shrunken `AUTOALLOW … document(s) WRITTEN` count is F1, not a build
   error. Grep `AUTOALLOW` for the extractor class and for `managed node group(s) derived from the ROLLED
   LAYOUT` — the group count must not have dropped by 2 per newly-pinned well if the F1 one-liner landed.
5. **F6 — the placed-and-pinned entry survives.** After step 2, keep playing ≥2 min, then reload. The
   pinned relocated group must still be at its coordinates, linked and suppressed: no `WELLH2-STUCK`, no
   `*** ABANDONED IN PLACE ***`, no second `WELLH2-SPAWN` for that core, and its audit line must still
   report the full satellite count.
6. **F5 — the pin is live state, not a latch.** Dismantle the pressurizer and every extractor; re-roll.
   The same well must read `pinned=0 … decisive=1` and appear as a recipient again. If it reads
   `decisive=0`, record `pinSrc` — that is the stuck gate.
7. **Regression on the untouched population.** In a save with relocation OFF: a well with a pressurizer
   must read `pinned=1 … pinSrc=original(level actors)`; one without, `pinned=0 … pinSrc=original`.
8. **Conservation.** On every roll above, each `WELLH1-ROLL pool` line must read `world N -> N`. One
   `a pinned well holds '…' which no card in the deck matched` warning is the §0 `UnmatchedTakes` case —
   report it with the well's `pinSrc`, do not wave it through.
9. **Import table.** After the build, `check_imports.ps1` / `dumpbin` the DLL against the H0 744/0
   baseline. Any new symbol falsifies the packet's prediction and must be recorded before the user plays.
10. **Summary arithmetic.** On any roll: `managed + PINNED + no-authored + undealt` must equal the well
    total, and `PinnedViaSpawned + PinnedViaOriginal + PinnedCarried` must equal the number of per-well
    lines reading `pinned=1`. A mismatch means a pinned well with an empty authored resource — expected,
    but it must be understood rather than discovered later.

---

## 6. Summary of required changes before ship

| # | Change | Class | Blocking? |
|---|---|---|---|
| 1 | F1 companion one-liner in `BuildManagedNodeGroupsFromLayout` (verbatim above) | reviewer-specified | **land in the same build** |
| 2 | F3 `WELLH1-PIN-UNMEASURED` log guard (verbatim above) | reviewer-specified | strongly recommended |
| 3 | F4.1 fourth provenance counter; F4.2 summary wording | reviewer-specified | recommended |
| 4 | F2 filed as a new `docs/TECH-DEBT.md` entry; **not fixed here** | documentation | required before ship |
| 5 | `docs/TECH-DEBT.md` T16's stated consequence corrected per F2 | documentation | required before ship |

Nothing in the diff as written is a DO-NOT-SHIP defect. The reason this is **SHIP WITH TESTS** and not
SHIP is item 1 plus checklist steps 1, 3 and 9: the fix's effectiveness, its side effect on the SF+
allow-list, and its import surface are all unmeasured, and one roll of the new diagnostics settles all
three.
