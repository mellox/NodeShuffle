# Cold review — ns-t7b: delete the `bGroupPlaced` guard in `RollWellRelocation`

- **Repo / branch / HEAD:** `C:\Claude\Projects\NodeShuffle`, `wip/h2-relocation`, `2ccb7af`
- **File under review:** `C:\Claude\Projects\NodeShuffle\Source\NodeShuffle\Private\NodeShuffleWellRelocateRoll.cpp` (uncommitted)
- **Change class:** PATH-REPLACING. Not a new path — an existing ~50-line path made reachable for the first time.
- **Reviewer:** cold (did not author). No build, no edit, no commit performed.
- **Verdict: DO NOT SHIP.** Two blockers, both save-visible, both reachable by the exact sequence the feature's own tooltip instructs.

---

## 0. Executive summary

The change is correct about *one* thing and wrong about the load-bearing thing.

**Correct:** the F2 world-origin satellite catastrophe cannot reoccur by this route (§2, F2 row). The
orchestrator's rationale for deleting the satellite-append warning holds — I traced it and could not
break it.

**Wrong, and it is the sentence the whole change rests on.** The new comment at
`NodeShuffleWellRelocateRoll.cpp:148-150` says:

> *"Pinning is already enforced above via `!E.bManaged` — the same rule ordinary nodes use — so nothing
> a player has built on can be moved out from under them."*

A **relocated well is structurally unpinnable.** `E.bPinned` is computed in `RollWellLayout` from the
**vanilla original core**, which relocation has hidden, collision-disabled and deregistered from the
node manager — an actor a player is physically prevented from building on. The actor they *can* build
on (our spawned core) is explicitly excluded from that computation by the `ns-review-h2 F1` filter at
`NodeShuffleWellRoll.cpp:133`. So `bPinned` is false forever, `bManaged` is true forever, and the
`!E.bManaged` guard the change relies on never fires for the population the change newly makes
reachable.

**This is measured, not reasoned.** The live log
(`C:\Users\mello\AppData\Local\FactoryGame\Saved\Logs\FactoryGame.log`) prints, in the same roll:

```
:93715  WELLH1-ROLL: skippedOurSpawned=17 -- 17 live fracking core(s) in the census are wells H2 RELOCATED ...
:93740  WELLH1-ROLL: re-roll complete -- 20 wells (20 managed, 0 pinned/unresolved), ...
```

17 of 20 wells are already relocated and **all 20 report `managed=1 pinned=0`.** Every relocated well
in that save is, today, eligible for re-enrolment under the new code.

The change is also correct that it makes first-run code live: the same log confirms **zero**
`WELLH2-ABANDON` events with reason `re-enrolled by a new roll` across the whole session.

---

## 1. Findings — hardest first

### F1 — BLOCKER — a relocated well can never be pinned, so the change's safety argument is void
`NodeShuffleWellRelocateRoll.cpp:140` (the guard being relied on) ·
`NodeShuffleWellRoll.cpp:133, 153, 190` (where the pin is actually decided) ·
`NodeShuffleWellRetype.cpp:152, 202` (the apply-time re-check, same defect)

`RollWellLayout` skips `IsManagedSpawnedNode(Core)` and then does an **unconditional**
`E.bPinned = bPinned;` at `:190`, where `bPinned` came from `Core->GetActivator().IsValid() ||
Core->IsOccupied()` on the *vanilla original*. `ApplyWellRetype`'s live re-check has the identical
defect — it resolves the core via `FindOriginalBaseByPath(E.CorePath)` (`:152`), the original again.
Nothing anywhere in the packet reads occupancy off `SpawnedWellCores.FindRef(E.CorePath)` for pin
purposes.

**Failure scenario (concrete).** Save with `ShuffleResourceWells` + `RelocateResourceWells` on. Well
`BP_FrackingCore10` relocates to (X₁,Y₁). Player walks there, builds a Resource Well Pressurizer on
the spawned core and three Resource Well Extractors on its satellites, and runs a factory off it.
Player later hits **Re-roll Layout** for unrelated reasons (ordinary nodes).

1. `RollWellLayout` census sees the spawned core → `IsManagedSpawnedNode` → skipped. Sees the hidden
   original → `GetActivator()` null, `IsOccupied()` false → `E.bPinned = false` → recipient →
   `E.bManaged = true`.
2. `RollWellRelocation`: `E.bRelocationFailed` false, `!E.bManaged` false → **both guards pass.**
3. Guard deleted → re-captures from the hidden originals → `DespawnWellGroup(E, "re-enrolled by a new roll")`.
4. The occupancy gate in `DespawnWellGroup` saves the buildings — it refuses the occupied members and
   prints `*** ABANDONED IN PLACE ***`. **But `DespawnWellGroup`'s return value is discarded at
   `NodeShuffleWellRelocateRoll.cpp:328`.** Execution continues into `bGroupPlaced = false`,
   `bDestDealt = false`, `ClearAbandonedWellPlacement(...)`, `++Enrolled`.
5. The entry is dealt a fresh destination (X₂,Y₂). On the next apply pass `TryPlaceWellGroup` commits
   there, `SpawnWellGroup` runs `DespawnStaleWellMembers` → the occupied core at (X₁,Y₁) is not at
   target → refuse → `StaleInUse = 1`. Then `NodeShuffleWellSpawn.cpp:123` `FindRef` returns that
   **same occupied core, still in the map**, and reuses it as the group's core — while the satellite
   loop **spawns fresh live satellites at (X₂,Y₂)**.
6. `bComplete` is false (`StaleInUse != 0`, `NodeShuffleWellSpawn.cpp:341`), so `bGroupPlaced` never
   returns to true and nothing new is suppressed. `NoteWellIncompleteSpawn` explicitly **does not
   escalate or give up** ("*it will retry this same placement indefinitely*",
   `NodeShuffleWellEscalate.cpp:148`).

**End state, permanent:** core (with the player's pressurizer) at (X₁,Y₁); a fresh set of live,
extractor-snappable satellites of the same resource at (X₂,Y₂) kilometres away, mCore-linked to that
distant core; the vanilla group still suppressed at the level author's site; `WELLH2-STUCK` dripping
forever. That is *nitrogen duplication* in a mod whose entire deck machinery exists to conserve
well resources.

### F2 — BLOCKER — a re-enrolled well whose re-deal fails is deleted from the world outright
`NodeShuffleWellRelocateRoll.cpp:328-371` (despawn + claim withdrawal) vs `:479-488` (deal failure)

The re-enrolment block despawns the old group, clears `bGroupPlaced`, and withdraws the claim
**before** the deal loop runs. If the deal loop then cannot find a destination it does
`E.bRelocate = false; ++DealFailed;` and logs *"left vanilla this roll."*

**That sentence is false, and the falsehood is the bug.** Nothing in the packet ever un-suppresses a
vanilla group — I grepped every `SetActorHiddenInGame` / `SetActorEnableCollision` site; the only
un-hide is `Core->SetActorHiddenInGame(false)` on our own spawn (`NodeShuffleWellSpawn.cpp:172`), and
`ScannerDeregistered` (`NodeShuffleWellRelocateApply.cpp:321-327`) is a permanent set, so the original
is never re-registered with the node manager either.

**Failure scenario.** Save with 17 relocated wells (the user's current save). First re-roll under the
new rules re-enrols all 17 simultaneously. The deal loop must now place ~20 wells in one pass, each
needing `2 × 6500 cm` clearance from every other destination and `6500 + 2500 cm` from every active
ordinary node, within `WellRedealTries = 24` draws each. Wells that fail: **actors destroyed at the
old destination, no new destination, vanilla originals still hidden with collision off and
deregistered from the scanner.** The player loses those wells entirely — nothing at the old site,
nothing at the new site, and an invisible non-buildable ghost at the level author's site.

Before this change no route existed by which a *suppressed* vanilla group could lose its relocated
replacement. `EscalateWellPlacement`'s give-up branch (`NodeShuffleWellEscalate.cpp:228-242`) is only
reachable from `TryPlaceWellGroup` inside the `!E.bGroupPlaced` branch
(`NodeShuffleWellRelocateApply.cpp:432-458`), i.e. only for groups that were never placed and
therefore never suppressed. **This change creates that route.**

### F3 — HIGH — `WellRedealTries = 24` is our own cap, sized for a per-roll demand this change quadruples
`NodeShuffleSubsystem.h:1851` · consumed at `NodeShuffleWellRelocateRoll.cpp:446`

The observed per-roll enrolment is 3–5 wells. After this change it is *every unpinned well in the
save* — 20 in the test save. The cap and the `2 × WellMaxBoundRadiusCm` spacing rule were written when
the deal placed a handful of wells among an already-relocated field; they are now being asked to place
the whole field at once, against a `Layout` of ordinary nodes that also just moved. Cap provenance was
not re-derived against the new demand. This is a design input, not physics — and F2 is what a failed
draw costs now.

### F4 — HIGH — five refusal diagnostics now assert a state that is false for the newly-reachable population
`NodeShuffleWellRelocateRoll.cpp:178-182, 205-208, 230-235, 242-249, 265-272`

Every one of the roll's refusal branches was written when only *never-placed* entries could reach it,
so each says some form of *"the well stays exactly vanilla"* / *"Left vanilla."* An **already-placed**
entry can now reach all five. For it the statement is simply untrue: the well is standing at
`PlacedCoreLocation`, its vanilla twin is hidden, and the correct message is *"keeps its existing
relocated placement at ⟨coord⟩; not re-enrolled this roll."*

`:242-249` is the worst: *"a later re-roll with the well loaded will enrol it"* — for a placed entry
that reads as a promise that the well will move, printed about a well that already moved.

Standing rule violated: a diagnostic may report what it MEASURED, never assert a state it did not check.

### F5 — HIGH — the summary line double-counts, and the new wording is wrong in the case it was rewritten for
`NodeShuffleWellRelocateRoll.cpp:172` and `:496-507`

- `++AlreadyCaptured` at `:172` fires at the *top* of the loop, before six later `continue`s. An entry
  that is placed **and then refused** is counted in `AlreadyCaptured` *and* in `RefusedUnstreamed` /
  `RefusedTooFew` / `RefusedGeometry` / `RefusedNonFinite`. The counters no longer partition.
- `++Enrolled` at `:373` also fires for re-enrolled entries, so a placed well that successfully
  re-enrols is counted in **both** `AlreadyCaptured` and `Enrolled`. With 20 placed wells all
  re-capturing, the line reads *"20 newly enrolled … 20 were ALREADY PLACED and have been
  RE-ENROLLED"* — 40 outcomes for 20 wells. Under the old guard the two sets were disjoint **by
  construction** (the guard `continue`d), which is why this was never wrong before.
- The new text asserts *"this number is NOT 'kept in place'"* — but for every entry that took a
  refusal branch, "kept in place" is exactly what happened. The rewritten wording is wrong in the same
  direction the old wording was, for a different subset.

### F6 — MEDIUM — no diagnostic distinguishes "moved again" from "relocated for the first time"
`NodeShuffleWellRelocateRoll.cpp:375-379`

The `ENROLLED` line is byte-identical for a first enrolment and a re-enrolment. The only distinguishing
evidence is a `WELLH2-DESPAWN … (re-enrolled by a new roll)` line, and that is gated on
`Destroyed > 0 || RefusedOccupied > 0` (`NodeShuffleWellDespawn.cpp:172`) — so a placed entry whose
handles were already dropped (pass-A reclamation, a save round-trip, `AlreadyGone`) re-enrols with
**nothing in the log saying so.** The workspace rule is that the log alone must explain what happened;
for the single behaviour this change exists to introduce, it does not.

### F7 — MEDIUM — `DespawnWellGroup`'s return value is discarded on the one path where it now matters
`NodeShuffleWellRelocateRoll.cpp:328`

`DespawnWellGroup` returns `bAllClear` precisely to say "at least one member was occupied and left
standing". Both call sites ignore it. That was defensible while this site was unreachable and the
escalation site only handled never-placed groups. It is the mechanism of F1: the one signal that could
have aborted the re-enrolment is computed, logged, returned — and thrown away.

### F8 — MEDIUM — invariant silently dropped: a placed entry no longer always carries `bRelocate = true`
`NodeShuffleWellRelocateRoll.cpp` (deleted `E.bRelocate = true;`) vs `:181, 204, 229, 241, 264, 307`

The deleted guard guaranteed `bGroupPlaced ⇒ bRelocate` after every roll. Now a placed entry refused by
any of the six branches ends the roll `bGroupPlaced=true, bRelocate=false`. I traced the consumers I
could reach: `ReconcileAbandonedWellClaims` (`NodeShuffleWellClaim.cpp:422`) and sweep pass A
(`NodeShuffleWellSweep.cpp:198, 213`) both `continue` on `bGroupPlaced` *before* reading `bRelocate`,
and `ApplyWellRelocation` (`:432`) takes the maintenance branch — so the placement is safe. The reader
that does change behaviour is the status line at `NodeShuffleSubsystem.cpp:6635`
(`if (W.bRelocate) WellsRelocateFlagged++`), which will now under-report. Graded **assumed**, not
proven — 37 files, and I read the six that matter most.

### F9 — LOW (style note, not a failure scenario) — the file is still over the 500-line limit
508 lines (was 519 at HEAD). Improved by the change but not compliant. `RollWellRelocation` is now a
~450-line single function with twelve exit paths; the re-enrolment block is a natural extraction point
(`ReEnrolPlacedWellGroup(E)`).

### F10 — LOW (style note) — a review-response comment asserts a cause it did not measure
`NodeShuffleWellRelocateRoll.cpp:164-167` states the deleted warning "existed because a placed entry
was never re-captured … They are now re-captured with the rest of the group." The *conclusion* is
correct (I verified it, §2), but the comment states it as fact where the packet's own convention
(`NodeShuffleWellCensus.cpp:15-20`, `NodeShuffleWellSpawn.cpp:322-331`) is to mark unmeasured
reasoning as such. The measurement here is static and available; say so, or say it is reasoned.

---

## 2. Differential / parity table

Invariants the OLD guard (`if (E.bGroupPlaced) { …; continue; }`) guaranteed, versus the new path.

| # | Invariant the OLD path guaranteed | How the NEW path provides it | Grade |
|---|---|---|---|
| 1 | A placed group's geography never churns across re-rolls | **Deliberately dropped** — this is the intended change | n/a (intentional) |
| 2 | A placed entry always ends the roll with `bRelocate = true` | Not provided. Six refusal branches now set it false on a placed entry (F8) | **unverifiable statically** — placement itself is safe via `bGroupPlaced` short-circuits I read; other consumers unaudited |
| 3 | A player's buildings on a relocated well are never destroyed | Provided *by `DespawnWellGroup`'s occupancy gate*, **not** by the `!E.bManaged` guard the comment credits. The gate is `IsWellMemberInUse` on the *spawned* handle (`NodeShuffleWellDespawn.cpp:108`) and it is correct | **assumed** — the gate reads `GetActivator()`/`GetExtractor()`/`IsOccupied()` on closed-source `AFGResourceNodeBase`; `mIsOccupied` is documented non-SaveGame in `NodeShuffleWellRelocate.h:70`. Never exercised on this path. **RT-1** |
| 4 | A relocated well the player built on is never re-enrolled | **NOT PROVIDED — F1.** `bPinned` is read off the hidden original; log shows `20 managed, 0 pinned` with 17 relocated | **provably ABSENT** (measured in the live log) |
| 5 | An already-placed entry keeps `PlacedCoreLocation` / a live claim when it cannot re-capture | Provided. `bGroupPlaced` stays true through every refusal `continue`; `ReconcileAbandonedWellClaims` skips placed (`Claim.cpp:422`), sweep pass A skips placed (`Sweep.cpp:198,213`), `DespawnStaleWellMembers` needs a live claim it still has | **provably provided** (static, from code read) — the orchestrator's claim #3 **holds** |
| 6 | A late-appended satellite with no offset never reaches the spawn | Provided by two independent layers: the re-capture writes `bCaptured` for all satellites when counts agree, and when they do **not** agree the entry is refused before any capture, leaving `bCaptured=false` records that `NodeShuffleWellSpawn.cpp:201` still refuses. **The F2 catastrophe cannot recur by this route** — the orchestrator's rationale is correct | **provably provided** (static) |
| 7 | A distant, unstreamed relocated well is never destroyed by a re-roll | Provided — count mismatch → `RefusedUnstreamed` → `continue` before any despawn | **provably provided** (static) |
| 8 | A re-enrolled well always ends up *somewhere* (old place or new place) | **NOT PROVIDED — F2.** Despawn + claim withdrawal happen before the deal; a deal failure strands it nowhere, vanilla still suppressed | **provably ABSENT** (static) |
| 9 | Re-capture reads correct transforms from suppressed originals | Suppression is hide + collision-off + scanner/manager deregistration (`RelocateApply.cpp:287-328`) — the actor and its transform survive, and `TActorIterator` still returns hidden actors | **assumed** — `TActorIterator` returning hidden-but-live actors and `GetActorLocation()` on a deregistered node are engine behaviour, not statically provable from our headers. **RT-2** |
| 10 | `ByCorePath` resolves for an already-placed entry | Original core is not `IsManagedSpawnedNode`, so it is in the map; spawned cores are correctly excluded, so F1's "doubling the actor count" cannot recur by this route | **provably provided** (static) — log confirms `skippedOurSpawned=17` |
| 11 | The roll's counters partition the well population | **NOT PROVIDED — F5.** `AlreadyCaptured` now overlaps `Enrolled` and all four refusal counters | **provably ABSENT** (static) |
| 12 | A save written under the OLD rules stays stable on load | Load path is untouched; the divergence begins at the first re-roll. But that first re-roll makes ~20 entries eligible at once (F2, F3) | **assumed** — mass-churn cost (≈220 actor destroys in one frame, no cap on `DespawnWellGroup`) is a frame-time question only a run can answer. **RT-6** |

**Grading-cap note.** Rows 3, 9 and 12 involve engine-internal mechanisms (closed-source
`AFGResourceNodeBase` occupancy, `TActorIterator` semantics over hidden actors, actor-destroy frame
cost). Per workspace rule these are capped at **assumed** and carry measurement steps; none may be
promoted to "provably provided" from headers or language reasoning.

---

## 3. Alternatives

The user's decision — *a well re-rolls like any other node* — is sound and is not what I am arguing
with. The question is how to implement it. Five options; the current one is **A0**.

**A0 — as written (delete the guard, rely on `!E.bManaged`).**
Cost: F1 + F2. Rejected — the pin it relies on cannot see the actor the player builds on.

**A1 — minimal, surgical: pin the re-enrolment on the SPAWNED core, at the relocation roll only.**
Insert before the fall-through, after the `!E.bManaged` guard:

```cpp
// ns-t7b F1: !E.bManaged CANNOT see this. E.bPinned is derived in RollWellLayout from the VANILLA
// core, which relocation has hidden, collision-disabled and deregistered -- a player cannot build on
// it, so it reports unoccupied forever. The actor they CAN build on is our SPAWNED core, which the
// ns-review-h2 F1 filter deliberately excludes from that census at both sites. MEASURED 2026-08-08:
// FactoryGame.log printed "20 wells (20 managed, 0 pinned/unresolved)" with 17 of them relocated.
if (E.bGroupPlaced)
{
    const TCHAR* InUseWhy = TEXT("");
    bool bRelocatedGroupInUse = false;
    if (AFGResourceNodeFrackingCore* Placed = SpawnedWellCores.FindRef(E.CorePath))
    {
        if (IsWellMemberInUse(Placed, InUseWhy)) { bRelocatedGroupInUse = true; }
    }
    if (!bRelocatedGroupInUse)
    {
        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            AFGResourceNodeFrackingSatellite* PS = SpawnedWellSatellites.FindRef(S.SatellitePath);
            if (PS && IsWellMemberInUse(PS, InUseWhy)) { bRelocatedGroupInUse = true; break; }
        }
    }
    if (bRelocatedGroupInUse)
    {
        ++RefusedPinned;
        E.bRelocate = true;   // parity row 2: a placed entry keeps this true, as the old guard did
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-ROLL core='%s': PINNED AT ITS RELOCATED SITE (%s) -- a player has built on the ")
            TEXT("group we placed at %s, so it is not re-enrolled. E.bPinned=%d is derived from the ")
            TEXT("HIDDEN vanilla core and cannot see this (ns-t7b F1); this test reads the SPAWNED ")
            TEXT("handles. Keeps its placement."),
            *WellShort(E.CorePath), InUseWhy, *E.PlacedCoreLocation.ToCompactString(), E.bPinned ? 1 : 0);
        continue;
    }
    ++AlreadyCaptured;
}
```

Uses the packet's single shared predicate (`IsWellMemberInUse`, `NodeShuffleWellRelocate.h:78`) — no
new rule, no new engine symbol. Trade-off: leaves `E.bPinned`/`E.bManaged` still lying for a relocated
well, so H1's retype deck can still deal a new *resource* under a live pressurizer at the relocated
site. Fixes F1 only.

**A2 — fix the pin at its source, so both consumers get it right.**
In `RollWellLayout` (`:153`) and `ApplyWellRetype` (`:202`), OR the vanilla-core signal with the
spawned handles for a `bGroupPlaced` entry, so `E.bPinned` becomes true for a built-on relocated well.
Then `!E.bManaged` becomes the honest guard the new comment already claims it is, and the H1 deck
correctly withdraws that well's card. Trade-off: touches H1's conservation arithmetic, which has its
own review history (F2/F3 in `ns-review-h1`) — a wider blast radius that needs its own regression pass.
Strictly more correct than A1.

**A3 — deal FIRST, despawn SECOND (fixes F2 independently of F1).**
Restructure re-enrolment into two phases: phase 1 captures and marks candidates; the deal loop runs;
phase 2 despawns + withdraws the claim **only for entries that actually got a destination**. An entry
whose deal failed keeps `bGroupPlaced`, its claim, and its actors. Trade-off: the deal's
`WellDestinations` seeding must then treat a re-enrolment candidate's *old* `PlacedCoreLocation` as
occupied-but-vacating (exclude it, or you cannot re-deal near it) — a real ordering subtlety, but a
contained one. **Required regardless of which pin fix is chosen**; A1/A2 shrink the population that can
hit F2, they do not close it (an unpinned relocated well can still fail its re-deal).

**A4 — put the behaviour behind its own config toggle (`RerollRelocatedWells`, default OFF).**
Existing saves keep today's semantics until the player opts in; the ~20-wells-at-once first re-roll
(F3, parity row 12) becomes a deliberate act rather than a surprise. Trade-off: a third well toggle on
top of two, and the mod already has toggle-interaction complexity (`ApplyWellRelocation`'s
`bWellShuffle && bRelocation`). Cheap, and it is the only option that protects a save that predates
the change.

**A5 — keep the guard; make the *resource* re-roll and leave geography alone.**
The status quo. Rejected by the user; recorded only so the fallback is named. Zero risk, and it is
what the code did for its entire life.

### Recommendation

**A2 + A3 + A4, in that priority.**
- **A2** because F1's root cause is a *pin computed from the wrong actor*, and A1 patches the symptom
  at one of the two sites that has it — this packet's own history (`ns-review-h4 F1`, `F4`, the
  two-ladders bug) is that two copies of one rule drift apart. Fix the rule once.
- **A3** because F2 is independent of F1 and is the worse outcome for a player (silent total loss of a
  well) even when nobody has built anything.
- **A4** because the user's current save has 17 relocated wells that all become eligible on one keypress,
  and neither A2 nor A3 makes that first re-roll *small*.

If only one change can land: **A2**, then re-review. If speed matters more than blast radius, **A1 + A3**
is the smaller-diff pair that closes both blockers, at the cost of leaving `bPinned` dishonest for the
retype deck.

---

## 4. Runtime test checklist

Every item graded **assumed** or **unverifiable statically** above, plus a reproduction for each
blocker. Written to be executable without reading this review. Run these **after** a fix lands, on a
**TEST save** — F1 and F2 both damage a save.

**RT-1 — reproduce F1 (the blocker). Must FAIL before a fix, PASS after.**
Both well toggles ON. Re-roll. Fly to a `WELLH2-PLACED` coordinate. Build a Resource Well Pressurizer
on the relocated core and one Resource Well Extractor on a satellite. Save. Re-roll again.
*PASS:* log shows `RefusedPinned` incremented and a line naming this core as pinned at its relocated
site; **no** `WELLH2-DESPAWN … (re-enrolled by a new roll)` for it; the well is still at the same
coordinate and still producing.
*FAIL (today's expected result):* `WELLH2-DESPAWN … *** ABANDONED IN PLACE ***` for that core, followed
by a fresh `WELLH2-PLACED` at a different coordinate and a repeating `WELLH2-STUCK`.

**RT-2 — reproduce F2. Must FAIL before a fix, PASS after.**
Same save. Force deal failure: re-roll repeatedly until the summary reports a non-zero `deal-failed`
for a well that was `ALREADY PLACED` on entry (or temporarily shrink `WellRedealTries` in a scratch
build to force it).
*PASS:* that well is either still at its old relocated coordinate, or standing un-hidden at its vanilla
coordinate and buildable.
*FAIL:* nothing at the old destination, nothing at a new one, and the vanilla site shows no well
(check with `NodeShuffle.DumpWells` — the original should report hidden + deregistered).

**RT-3 — parity row 3: the occupancy gate on the spawned handle (engine-internal, never exercised).**
On the built-on relocated well from RT-1, confirm `WELLH2-DESPAWN` names the refusal reason
(`pressurizer-on-core` / `extractor-on-satellite`), **not** `IsOccupied`. `mIsOccupied` is non-SaveGame;
if the reason after a save/reload is `IsOccupied` only, the gate is one reload away from failing open.

**RT-4 — parity row 9: re-capture off suppressed originals.**
On a re-roll that re-enrols a *previously placed, unbuilt* well, confirm the `WELLH2-ROLL core=… ENROLLED`
line prints a `rigid body captured about the core at ⟨coord⟩` equal to the well's **original vanilla**
coordinate (cross-check against the `WELLH2-PLACED core=… vanilla ⟨coord⟩ ->` line from the earlier
roll), and `minIntraGroup` is a plausible ≥1800 cm — not 0, not the destination coordinate.

**RT-5 — parity row 6: no satellite at world origin.**
After two consecutive re-rolls with wells relocating, run `NodeShuffle.DumpWells` and confirm zero
fracking satellites within 300 cm of (0,0,0), and that any `refused (no capture)` count in
`WELLH2-SPAWN` is matched by a satellite that is *absent*, not one that exists somewhere odd.

**RT-6 — parity row 12: mass-churn cost on the existing save.**
On the 17-relocated-wells save, take the first re-roll under the new rules and record the frame hitch
(`stat unit` or the log timestamp delta across the `WELLH2-ROLL` block). ~220 `Destroy()` calls run
uncapped in one frame — `WellSweepMaxDestroysPerPass = 16` does **not** apply to `DespawnWellGroup`.
Record the number; if it is a visible stall, `DespawnWellGroup` needs its own cap.

**RT-7 — F5: counter honesty.**
On any re-roll where at least one placed well is refused, check the summary line's counters sum to
`WellLayout.Num()`. Today they will exceed it (an entry appears in both `AlreadyCaptured` and either
`Enrolled` or a refusal bucket).

**RT-8 — F8: `bRelocate` on a placed-but-refused entry.**
Re-roll from far away so a placed well is refused as not-streamed. Confirm the well is still spawned,
linked and producing on the next visit, and that the status line's `WellsRelocateFlagged` count matches
the number of placed wells (it will under-report today).

---

## Verdict

**DO NOT SHIP.**

The change's stated safety property — *"nothing a player has built on can be moved out from under
them"* — is false by construction for the only population the change affects: a relocated well's pin is
read from the hidden vanilla original, which no player can build on, and the live log measures
`20 managed, 0 pinned` across a save holding 17 relocated wells. A second, independent blocker
(despawn-before-deal) can delete a well from the world entirely while leaving its vanilla twin
suppressed. Both are reachable by the sequence the feature's own tooltip instructs, and neither has
ever run — the session log contains zero `re-enrolled by a new roll` events.

Fix per §3 (**A2 + A3 + A4**), then re-review. The fix will be a *structural* review-response change to
a path-replacing change, so it earns a **full** re-review, not a scoped one — and this reviewer must not
be the author of it.
