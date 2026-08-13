# NodeShuffle — tech debt & known limitations

Living list. Nothing here blocks a release; everything here is a thing we know about,
have decided about, and have not done. **If you find yourself rediscovering an item on
this list, the entry has failed — fix the entry, not just the bug.**

Each item records what it is, how we know, and why it is not fixed. Items with a
**pre-scoped fix** have had the work sized already — start there, don't redesign.

Last updated 2026-08-11 (**T64 filed in P3 and IMPLEMENTED — the ordinary-node rock-hide was one-shot, so
a rock whose `AFGNodeMeshActor` streamed in after its node was hidden arrived VISIBLE and nothing ever
retried; measured in `scratchpad/solid-hide-latency.md` at 212 of 220 rocks still drawn when their pairing
resolved, worst delay 1535 s. Fixed at the mesh-actor cache's two add sites, capture-before-hide, with the
forward-link iterator widened to `AFGResourceNodeBase` (T43 family). `NodeShuffle.AuditPlacements` added.
Packet ns-t64-rock-hide-and-audit, build `2026-08-11-t64-1` — built, NOT yet reviewed and NOT yet
in-game.**) Earlier: **T63 filed in P2, author LOW/possibly-never — nodes can be dealt into
dense foliage; trees don't block the gates' trace channel, second measured sighting of the
foliage-blindness family. T62 filed in P2, author-priority LOW — a third-party pointer mod (RNM)
beams at hidden dirty originals and misses late replacements; window hypothesis + discriminating
test recorded, do not build until it runs.** Earlier: **T61 filed and IMPLEMENTED — the KBFL hook now arms whether or not
`NodeShuffle.DestroyerVeto` is on, in OBSERVE-ONLY mode when it is off: it measures, fills the T60
opt-in list and posts one chat notice, and vetoes nothing at all. Author's ruling; it deliberately
REVERSES T58's R1 "off means blind" paragraph and closes the ship-default decision. Packet
ns-t61-observe-always, neither built nor reviewed.** Earlier: T59 DECIDED + IMPLEMENTED as a per-SESSION pack namespace, and a pre-scoped
DESIGN ITEM added to T14 for auto-enrolling newly-surviving foreign resources — packet
ns-t59-pack-namespace, neither built nor reviewed. Earlier: T55 CLOSED + T59 split out of it; T56 discriminated — packet ns-t55-t56-reload. Earlier: T58 filed in P1 after T55 — SF+ destroys third-party nodes on new games;
DECIDED the same day by the author and implemented as the default-ON protect veto, pending build,
review and in-game — see the T58 STATUS block; source
`_team/nodeshuffle-followups/veto-spawnwindow-regression.md`.
T54 filed at the top of P1 by author ruling; supersedes D1's coupling.
T51/T52/T53 filed at the end of P3 — they were cited across the code and the state file and had
never been defined here, which is exactly the failure the paragraph above describes).

> **Correction, 2026-08-08 — read before using anything below about node coverage.**
> An earlier revision of this file was written while the project believed vanilla resource
> nodes stream in progressively and the roll therefore sees only part of the map. **That is
> false and was measured false**
> (`_team/nodeshuffle-followups/node-enumeration-investigation.md`): every level-placed
> vanilla resource node — all 630 — is live in a **single 8–11 ms frame at load**, in three
> independent boots, spanning biomes tens of kilometres apart. Nothing about the ordinary
> node roll is discovery-gated. What *is* presence-gated is **spawning a replacement rock**
> and **probing terrain for a well destination** (both need a ground raycast, which needs
> resident terrain). The one real discovery gap is **nodes runtime-spawned by other mods**,
> which can arrive minutes after boot and are missed until a re-roll (T14).
>
> The false model reached this file through a diagnostic that asserted a cause it never
> tested. That defect class is T4's section heading, and it is now also fixed at source
> (`ROLLCENSUS:`, see docs/DIAGNOSTICS.md).

---

## Accepted trade-offs — decided, not defects

### ~~D1. Resource wells can duplicate if you build on one mid-move~~ — **SUPERSEDED BY T54, 2026-08-10**
**The decision below was made 2026-08-08 and REVERSED by the author on 2026-08-10.** The history is
kept because D1 is the reason the old coupling existed, and a reader who finds only the new behaviour
will re-derive the old one as an improvement.

**WHAT D1 DECIDED (2026-08-08, now history):** accept, document, do not fix. Relocation is
player-presence-gated — a well cannot move until its destination's terrain has streamed, which requires
a player to travel there — so we hid the original only **after** the replacement provably existed.
Hiding on the roll would remove a well from the save for an unbounded time (design §5.4,
*"all-or-nothing; fail-safe to the vanilla location"*). `bPinned` is computed once at roll time
(`NodeShuffleWellRoll.cpp:153,188`); `ApplyWellRelocation` was spawn-then-suppress. Building on a well
after the roll therefore yielded two wells, which **errs in the player's favour**.

**WHAT T54 CHANGED, AND WHY THE DUPLICATION SCENARIO MOSTLY DISSOLVES.** The author ruled the origin
must be hidden immediately (*"It is a shuffle… that includes ALL things we shuffle"*). A hidden,
de-collided, de-registered origin cannot take a Pressurizer, so the window in which a player could
build on a well that is about to move is now roughly one apply pass wide instead of unbounded. It is
**not zero**: an origin that has not streamed in yet is not hidden yet, and a player standing on it at
that moment can still build.

**THE OCCUPIED GUARD IS UNCHANGED AND STILL LOAD-BEARING — this is the part of D1 that did NOT expire.**
A save that already holds a Pressurizer or Extractor at a vanilla origin, from before the first hide
ever ran, must never have it taken away. Two mechanisms enforce it and both survive T54:
`IsWellMemberInUse` inside `SuppressVanillaWellGroup`'s `HideOne` (per member), and the group-scoped
occupancy gate, which T54 now asks **before the immediate hide as well as before placement**, through
one shared lookup (`EvaluateVanillaWellGroupOccupancy`). Hiding the rest of a group around an occupied
member is the measured T24 field defect and is refused on both paths.

> **The pre-scoped fix D1 carried — re-test occupancy immediately before spawning and abandon the
> relocation — was NOT built and is not needed for the duplication case it was written for.** The
> residual (a player builds on an origin in the window before it streams in and hides) is refused by the
> group gate on the next pass, which leaves the whole well vanilla. Keep the hedge that shipped with it:
> *"I could not find a check"* is weaker evidence than *"there is no check"*.

### D2. Third-party node-manager mods still list/ping the original node locations
**Added here 2026-08-08. Previously tracked only in GitHub issue #1, item 4** — which is
exactly the failure mode this file exists to prevent, so it lives here now.

NodeShuffle **hides** original nodes rather than destroying them, deliberately: the world
stays save-safe and fully restorable if the mod is disabled. Mods such as
`ResourceNodesManager` scan **every** resource-node actor, hidden ones included, so they keep
reporting the vanilla positions after a shuffle or re-roll (e.g. the three vanilla oil spots
still ping).

**The vanilla map, compass and handheld scanner are clean** — that path is fixed at the
`GenerateNodeClusters` source, not per-consumer, so nothing downstream of it sees a hidden
original.

> **Options if ever revisited (neither chosen):** a "fully remove originals" toggle that
> destroys instead of hides — which forfeits restore-on-disable — or a plain compatibility
> note in the README. Do **not** widen the scanner fix to cover third-party scanners; they do
> not go through `GenerateNodeClusters`.

---

## P1 — player-visible, fix before more people use the feature

### T54. Vanilla wells are NOT hidden at load — the origin stays live and usable until its replacement places. The author has now ruled that wrong. **TOP OF P1 (author, 2026-08-10).**
**The ask (author, 2026-08-10, on a fresh multi-mod save):** *"So our on load shuffle still doesn't
hide vanilla immediately like I asked?"* — said while standing at `BP_FrackingCore12` (water), fully
live with its six satellites, `hidden=0`, while `WELLH2-STRANDED pass 1` read **2 of 20 well entries
placed**. By the suppression invariant, every unplaced entry's vanilla group stays visible and usable
for an unbounded time — until the player happens to stream terrain near that entry's destination.

**This entry supersedes a DECISION, not an oversight — D1 (2026-08-08) chose the current behaviour
deliberately**: suppression is coupled to placement (`SuppressVanillaWellGroup` has two call sites,
`NodeShuffleWellRelocateApply.cpp:473`/`:505`, both requiring `bGroupPlaced` — T15's invariant *"a
suppressed well origin always has a live, maintained relocated group"*), because placement is
presence-gated (the destination probe needs streamed terrain) and hiding on the roll removes a well
from the save for an unbounded time, possibly permanently (design §5.4, fail-safe-to-vanilla). The
author's 2026-08-10 ruling reverses that priority: a player seeing and using a well that is destined
to move is the worse defect. D1's text must be re-decided as part of this item, not left contradicting it.

**What an immediate-hide fix must re-decide or not break — named up front:**
1. **The well-less window becomes intended.** Hide-at-load plus presence-gated placement means the
   resource exists NOWHERE until the destination streams. **DECIDED (author, 2026-08-10): accept the
   window.** Verbatim: *"It is a shuffle, I've repeatedly said I want things hidden immediately on a
   shuffle and that includes ALL things we shuffle."* Scope is therefore every shuffled population,
   not only wells — any origin that currently stays visible pending its replacement is in scope.
   T21's bake remains worth having (it shrinks the window) but no longer gates this item.
2. **`WELLH2-STRANDED` flips polarity.** "Suppressed but not placed = 0" is today's health
   invariant; after this fix that state is intended-transient. The check must distinguish
   transient-awaiting-placement from stuck, or it becomes a vacuous pass (the exact defect class in
   [[lessons-checklist-predates-the-feature]]).
3. **D1's duplication scenario mostly dissolves** — nobody can build on a hidden well — a point in
   its favour. But the occupied-member refusal (`NodeShuffleWellRelocateApply.cpp:295`, *never hide
   a well someone has built on*) must keep protecting saves where extractors already exist at the
   origin before the first hide runs.
4. **Restore paths get a bigger population.** Reroll/disable restore has been exercised on placed
   groups; after this fix it must un-hide entries that never placed. T15's late-streaming-satellite
   gap (hidden at origin, refused at destination) gains blast radius: with immediate hide there may
   be NO destination group yet when the satellite streams in.

> **Pre-scoped starting point:** the hide today runs inside `ApplyWellRelocation`'s spawn-then-suppress
> (`NodeShuffleWellRelocateApply.cpp:467-473`); immediate-hide would run at load/stream-in for every
> entry the roll marked as moving. **Do not build until the author answers item 1** — that answer
> decides whether this is a one-gate change now or waits on T21.

---

#### T54 STATUS: **BUILT AND SHIPPED** (packet `ns-t54-immediate-hide`, commit `ee01d38`, 2026-08-10, boot marker `2026-08-10-t54-1`). Cold-reviewed same day — differential FIX-FIRST ×11 → all applied → scoped re-review → 3 residuals applied → lint 0 (`_team/nodeshuffle-followups/t54-coldreview.md`). Verified in-game on a TEST save, 8/8 PASS (`_team/test-logs/2026-08-10-t54-1/run.md`); the REAL-save spot checks (core12 gone-check, `ProbeNearestNode` at the buried lead) were still marked OWED as of the last mention (`_team/NodeShuffle-state.md` 2026-08-11 ~10:3x block) and no later confirmation was found. (status per `_team/NodeShuffle-state.md` 2026-08-10 ~03:0x/~afternoon blocks + `t54-coldreview.md`, 2026-08-12 release-readiness audit)

**THE PRE-SCOPED STARTING POINT ABOVE WAS STALE AND IS THE FIRST THING TO CORRECT.** It describes the
tree as it was before `ns-t23-rollhide` landed. At `7bf99b4` there are **four** `SuppressVanillaWellGroup`
call sites, not two, and one of them — `NodeShuffleWellRelocateRoll.cpp` phase-3 commit — is **not**
gated on `bGroupPlaced`. The real coupling was narrower and worse than "two sites behind `bGroupPlaced`":

- the roll-time hide sits behind a **default-OFF** toggle (`CommitWellsAtRoll`), so on the author's save it never ran;
- a roll happens **once** and can only hide what is resident at that instant, so an origin that streams in later was never revisited;
- the apply pass's unplaced branch only ever **re-asserted** an existing suppression — it never **initiated** one.

**MEASURED PER POPULATION BEFORE ANY CODE WAS WRITTEN:**

| population | where the origin is hidden | coupled to the replacement? |
|---|---|---|
| ordinary/solid nodes | `NodeShuffleSubsystem.cpp:4531` `SuppressOriginalNodes`, over `OriginalNodeRecord` built at roll time at `:1601` | **No — already immediate.** Hides the moment the path resolves; nothing consults the replacement. |
| oil (liquid) | same record, same loop | **No — already immediate.** A liquid `Layout` entry is an ordinary record. |
| gas | not shuffled (left vanilla) | n/a |
| fracking wells | `NodeShuffleWellRelocateApply.cpp` (3 sites) + `NodeShuffleWellRelocateRoll.cpp` (1) | **Yes — the only population that was.** |

**WHAT LANDED.** A third arm in `ApplyWellRelocation`'s `!bGroupPlaced` branch, above every `continue`,
that **initiates** the suppression for every entry the roll marked as moving
(`bRelocate && bOffsetsCaptured && bDestDealt && !bRelocationFailed`), behind the group-scoped occupancy
gate. It runs every pass, so an origin that streams in later is caught — which the roll path structurally
cannot do. The roll path is **not** disabled. Plus: a disable-restore that arms the persisted un-hide
intent on the stranded census's own `NotWorked` predicate; the `WELLH2-STRANDED` polarity re-statement;
a `WELLH2-IMMEDIATE` per-pass census; and a T54-A/T54-B opposite-polarity pair alongside (never
replacing) T23-A/T23-B.

**OPEN, DELIBERATELY NOT DECIDED HERE — `CommitWellsAtRoll` is now close to a no-op.** It moves the
disappearance earlier by roughly one apply pass and pays the roll's one-shot-capture fallback to do it.
Retiring a shipped, save-visible toggle is the author's call, so it was left in place and its tooltip
and header comment were rewritten to say truthfully what it still does. **Decide it before release.**

**T15 GAINED BLAST RADIUS AND WAS COUNTED, NOT FIXED** (per the packet brief). An uncaptured satellite
record is hidden with its group and never spawned at the destination; immediate hide widens that
population because a satellite can now stream in and be hidden while no destination group exists yet.
The `WELLH2-IMMEDIATE` census reports uncaptured-hidden over total-satellite-records-hidden.

### T55. The SF+ allow-list chat notice RE-ANNOUNCES THE SAME PENDING EXTRACTORS ON EVERY LOAD **— MECHANISM CORRECTED 2026-08-10: the set is RE-CREATED, not re-announced; see the T55 STATUS block and [[T59]]. CLOSED as option 2.** — its dedup is session-scoped **by documented design**, so the player is told the same thing every time they load. The pre-scoped "persist the signature" fix is **forbidden by that same design comment** and must be re-decided, not applied.
**Measured 2026-08-10 across eight loads of build `2026-08-10-t54-1`** (the author's real multi-mod
save; evidence `lithium-probe-extract.md` §"4th follow-up: morning save-reload investigation" Q1, plus
a direct re-grep of `%LOCALAPPDATA%\FactoryGame\Saved\Logs\`). Every `decision=EMIT` line in every
load carries `lastSignature=''`:

| load | log | timestamp | what was announced |
|---|---|---|---|
| A | `…-07.37.43` | `07.29.43` | bam-renew pump set, `lastSignature=''` |
| A (2nd) | `…-07.37.43` | `07.34.06` | AlkaLib set — `lastSignature` POPULATED, emitted because the keys were genuinely new |
| D | `…-17.09.45` | `17.09.33` | MinerMk1/2/3, `lastSignature=''` |
| F | `…-17.13.38` | `17.13.21` | AlkaLib ReactiveOreExtractor Mk2/Mk3, `lastSignature=''` |
| H | live (opened 12:29:49) | `17.30.34` | AlkaLib ReactiveOreExtractor Mk2/Mk3 again, `lastSignature=''`, *"2 of them not previously announced"* |

B, C, E, G emitted nothing (`PENDING=0` — nothing to say, the gate working). The player-visible
defect is the last row: **the same two AlkaLib extractors announced on F were announced again on H
as "not previously announced".**

**THE EVIDENCE FILE'S ROOT CAUSE IS ONE FIELD OFF, AND THE DIFFERENCE DECIDES THE FIX.** It reads
`lastSignature=''` as *"the dedup memory never loads"*. `LastNotifiedSignature`
(`Source/NodeShuffle/Public/NodeShuffleSubsystem.h:1159`) is documented **LOG-ONLY — NOT a decision
input**; it is written once and read only by format strings. The actual gate is the unannounced-key
test at `Source/NodeShuffle/Private/NodeShufflePendingNoticeEmit.cpp:68-86` over
`AnnouncedPendingKeys` (`NodeShuffleSubsystem.h:1166`). Both are transient, so a blank
`lastSignature` on a first-of-session EMIT is the **expected** print, not the malfunction. The
malfunction is only that the *set* is session-scoped.

**AND THAT IS DELIBERATE.** `NodeShuffleSubsystem.h:1147-1151`, verbatim: *"ALL THREE MEMBERS ARE
TRANSIENT BY DESIGN — no UPROPERTY(SaveGame) anywhere in this block, and that is load-bearing, not an
omission… Persisting any of this across boots would fight that mechanism and could suppress a notice
the player genuinely needs after a rebuild."* So the pre-scoped fix (make the signature a SaveGame
UPROPERTY) is **a reversal of a written decision, in the [[T54]]/D1 class** — it must be re-decided in
this entry, not slipped in as a bug fix. The sibling pattern the fix would lean on *does* exist and is
not the issue: `ANodeShuffleSubsystem` carries plenty of top-level `UPROPERTY(SaveGame)` state
(`NodeShuffleSubsystem.h:1616`, `:1701`, `:2049`), so persisting is mechanically trivial. It is the
semantics that are contested.

**Options, with the trade-off each buys:**
1. **Persist `AnnouncedPendingKeys` (not the signature).** Silences the repeat. Directly contradicts
   the comment above; needs an invalidation rule so a player who rebuilds/reinstalls SF+ still gets
   told. Cheapest to write, most expensive to get *right*.
2. **Leave the state transient; make the MESSAGE non-alarming.** Reword to a status line ("N
   extractor(s) still need an SF+ restart") so a per-load restatement reads as status, not news. No
   design reversal. Player copy is a graded claim — see the T1 block below.
3. **Persist only a "player has been told about this exact set" hash, cleared whenever the written
   document set changes.** Splits the difference; most code.

**Not yet measured, and it bounds how bad this is:** whether the announced items ever clear on their
own. The design says PENDING *"empties itself one boot after the documents land"* — but AlkaLib Mk2/Mk3
were pending on F **and** on H (two loads apart), which is the first datum against that claim and the
cheapest next measurement. Do that before choosing an option: if PENDING never empties, this is not a
dedup defect at all but a stuck allow-list write, and every option above is the wrong fix.

#### T55 STATUS: **MEASURED, THEN DECIDED — OPTION 2. BUILT AND SHIPPED** (packet `ns-t55-t56-reload`, commit `84a23fa`, 2026-08-10, boot marker `2026-08-10-t5556-1`). Cold-reviewed same day, verdict SHIP WITH TESTS, F1/F2 applied before commit (`_team/nodeshuffle-followups/t5556-coldreview.md`). Ran through the full overnight real-save session on top of it (`t61-1`); no T55-specific chat-notice pass/fail line was found recorded in state.md. **The measurement above was taken and it CHANGED the answer.** (status per `t5556-coldreview.md` + `_team/NodeShuffle-state.md` 2026-08-10 ~19:0x block, 2026-08-12 release-readiness audit)

`grep "AUTOALLOW extractor='/AlkaLib" *.log` over the surviving logs, per boot:

| boot | AlkaLib ReactiveOreExtractor Mk2/Mk3 |
|---|---|
| `…-17.13.38` (F) | `sfPlusAlreadyAllows=0 decision=ADD` → WROTE, announced |
| `…-17.15.16` (G) | **`sfPlusAlreadyAllows=1`** `decision=SKIP reason=no-managed-node-type-natively-accepted` |
| `…-18.05.58` (the log this entry called "H") | `sfPlusAlreadyAllows=0 decision=ADD` → WROTE, announced again |

**PENDING DOES empty itself one boot later, exactly as the design comment claims — and then REFILLS.**
So the third option in the "not yet measured" paragraph (a stuck write) is falsified too, and so is this
entry's own headline mechanism. The set is not being *re-announced*; it is being *re-created*. The cause
is `NodeShuffleAutoAllowExtractors.cpp:759` (`if (FM.DirectoryExists(*PackDir) && !FM.DeleteDirectory(...))`)
— the pack directory is **cleared and rebuilt every completed pass** from `ToGenerate`, which is a pure function of *the loaded save's* managed node groups, while the
directory itself is per-INSTALL. G loaded a save whose layout has no lithium/alkali group, so AlkaLib
matched nothing, so its two documents were deleted; the next boot of the other save had to write them
again. Filed as its own entry, **[[T59]]**, because it is a real defect and this one is not its fix.
**(T59 was decided and implemented later the same day — the line numbers cited in this paragraph are the
PRE-FIX ones and are kept because they are what the measurement was taken against; the delete is now
scoped to the loaded session's own name space. See the T59 STATUS block.)**

**Therefore option 1 is REFUSED ON EVIDENCE, not on deference.** Persisting `AnnouncedPendingKeys` would
have suppressed the third row above — a notice that was CORRECT, for buildings the player genuinely could
not place until a restart. That is verbatim the failure the header comment predicted
(`NodeShuffleSubsystem.h`, "could suppress a notice the player genuinely needs"), now observed.

**Option 2 shipped.** No state change; the copy carries it. Two assertions in the chat body were measured
false and are gone: *"%d **new** building(s)"* (they were announced two boots earlier) and *"this is normal
after a mod update or reinstall"* (a cause the notice never tested; the measured cause was a save switch).
The header comment at `NodeShuffleSubsystem.h:1147` now records the measurement and forbids the reversal by
name. New diagnostic: `AUTOALLOW PACKCHURN:` — docs before/now, unchanged/added/removed, with the removed
filenames — so a repeat notice is explained by one line instead of a cross-log diff.

### T59. THE GENERATED SF+ ALLOW-LIST PACK IS PER-INSTALL BUT ITS CONTENT IS PER-SAVE, so loading save B deletes the compatibility documents save A needs — and the extractors A had working go back to "Invalid aim location!" for one boot, every time the player alternates saves. **Split out of [[T55]] 2026-08-10 when the measurement showed T55's symptom was this. DECIDED BY THE AUTHOR AND IMPLEMENTED THE SAME DAY — option 1 keyed per SESSION; see the T59 STATUS block below for the form, the migration and what it deliberately does not fix. The description below is the DEFECT AS MEASURED, kept in the past tense's place because the status block is what says where it stands.**
**MEASURED**, three consecutive boots, table in the T55 STATUS block above: written+announced → allowed →
deleted → not allowed → written+announced again. **THE MECHANISM IS TWO LINES, BOTH DELIBERATE:**
`NodeShuffleAutoAllowExtractors.cpp:759` deletes the whole pack directory on every completed pass, and
`ToGenerate` (same file, add-site `:679`, declared `:355`) is built from **this world's** managed node groups. Neither is a bug on
its own — the clear-and-rebuild exists to stop stale entries, and its own header comment explains at length
why reading the pack back would reintroduce an oscillation. The defect is that the two together make a
GLOBAL artifact a function of a PER-SAVE input, which nothing in that file's reasoning accounts for.

**Blast radius is wider than the chat notice.** Anything the pass writes is affected the same way, for any
player with more than one save — which is the ordinary case, and the author's own setup (`Test_02_*`,
`TestAllMinables`, `Reshuffle_01`).

**Options, unranked, none costed:**
1. **Namespace the pack by save** (subdirectory or filename prefix keyed on the save's identity). Needs KDF
   to read more than one pack dir, or a stable per-save path — unverified, and KDF is third-party.
2. **Union, don't replace** — keep documents from other saves and only add. Reintroduces exactly the stale
   entry the clear-and-rebuild was written to prevent; needs its own invalidation rule. **READ
   `NodeShuffleAutoAllowExtractors.cpp:63-75` BEFORE PICKING THIS.** Union requires reading the pack
   directory back, and that file records three measured boots (2026-07-30) where an input derived from the
   pass's own previous output oscillated the pack every other boot — closing *"AN INPUT DERIVED FROM THIS
   PASS'S OWN PREVIOUS OUTPUT IS A FEEDBACK LOOP, NOT A SHORT-CIRCUIT. Do not add one back."* Same class.
3. **Make the written set save-independent** — generate for every extractor×resource pairing the *installed
   mods* permit, not only what this save's layout currently manages. No churn, and the pending notice then
   fires once per install instead of once per save switch. **Its real cost is correctness, not size:** it
   widens SF+'s allow-list to nodes NodeShuffle never shuffled, i.e. a balance change to SF+ on unshuffled
   vanilla nodes. That is this option's strongest argument against it.
4. **Accept it and say so** — the copy shipped for T55 already tells the player a later load can re-create
   the state. Cheapest; leaves a real one-boot regression in place.

**Do not "fix" this by persisting the announce set** — see the T55 STATUS block for why that suppresses a
true notice.

#### T59 STATUS: **DECIDED AND IMPLEMENTED — OPTION 1, KEYED PER SESSION (PLAYTHROUGH), NOT PER SAVE. BUILT AND SHIPPED** (author decision 2026-08-10; packet `ns-t59-pack-namespace`, commit `5738c32`, marker `2026-08-10-t59-1`). Cold-reviewed same day, verdict SHIP WITH TESTS (`_team/nodeshuffle-followups/t59-coldreview.md`). Deployed and ran a full real-save overnight session on top of it (`t61-1`) with zero LogNodeShuffle errors; the review's own runtime checklist (R1-R9, incl. the `crossNamespaceRemoved=0` PACKCHURN measurement) has no confirmed pass recorded in state.md — still owed. (status per `t59-coldreview.md` + `_team/NodeShuffle-state.md` 2026-08-10 ~19:0x/~23:5x blocks, 2026-08-12 release-readiness audit)

**FORM: a filename prefix inside the SAME pack directory.** Documents are now
`s-<sessionSlug>--auto-allow-[<mod>-]<class>.cdo.yml`, alongside one shared `pack.yml`.
*KDF evidence, strongest first:* (a) **our own measurement** — `pack.yml` lists no documents and this
generator has always written two different filename shapes flat into this directory
(`auto-allow-<class>` and `auto-allow-<mod>-<class>`), and a written document read back
`sfPlusAlreadyAllows=1` on the next boot. **That inference is ASSUMED, NOT MEASURED — capped
deliberately.** What was MEASURED is narrower than the claim: two stems, BOTH beginning `auto-allow-`,
both applied. That is consistent with *"KDF enumerates every document file and ignores stems"* AND with
*"KDF matches a prefix or caches an index"*. Only the first makes this packet work. **The falsifier is
runtime test R3:** a renamed document must read back `sfPlusAlreadyAllows=1` on the next boot, on an
install with no legacy document for that class. (b) KPatchwork's `DataForge/README.md` states a pack may sit at `DataForge/**/<pack-name>/pack.yml`
(*any depth*) and its shipping packs nest documents two levels below their own `pack.yml`
(`DataForge/SatisfactoryPlus/MkPlusSFPlus/PDA/PDA-allowed-extractors.yml`) — so a per-session
subdirectory or a per-session sibling pack would very probably also work. They were **not** chosen this
round: (b) is another repo's layout rather than this generator's measured behaviour, and one shared
`pack.yml` states the `hasMod` gate exactly once. **THE PER-SESSION SUBDIRECTORY IS THE PRE-AGREED
RESERVE** (cold review 2026-08-10, alternative 1) — if R3 falsifies stem-agnostic discovery, swap the
prefix for a subdirectory component (`<PackDir>/s-<slug>/*.cdo.yml`) and the delete becomes a scoped
`DeleteDirectory` on a directory we own. The sibling-pack form stays rejected: it is the only option that
adds a directory under the discovered root. **The namespace stays inside the already-discovered pack
directory, so the set of places KDF reads is unchanged** — the `[[kdf-editor-exports-autoapply]]` hazard
(documents applying from an unexpected location) is not widened.

**IDENTITY: `AFGGameState::GetSessionName()`**, sanitised to `[A-Za-z0-9_]` (`-` excluded on purpose: the
prefix separator is `--`, and allowing `-` inside a slug would let one session's delete glob match
another's files) plus a CRC32 of the **original** name. **Collision, stated honestly:** two *different*
session names share a namespace only if they fold identically *and* collide on CRC32 — possible, not
impossible; the consequence is exactly the pre-T59 behaviour **for that one pair**, never a delete
outside this directory. A session **rename** starts a new namespace and orphans the old one's documents,
which keep applying and are never deleted.

**ACCEPTED RESIDUAL — A SESSION NAME IS NOT A PLAYTHROUGH ID** (cold review F4, 2026-08-10). The common
case is not a hash collision at all: **two separate playthroughs the player named identically share one
namespace by construction** — no fold, no checksum involved — and this entry's churn reproduces in full
for that pair. The game groups saves by session name too, so they are one session to Satisfactory as
well. **The author chose per-session deliberately** (per-save was considered and rejected: every autosave
and branch would become its own namespace, unbounded and un-GC'd, with a much staler union). This is the
known edge of the KEY, not a defect in the hashing, and the player-facing copy was corrected to say
"session" rather than "playthrough" so it does not over-claim.

**WHEN THE WRITER RUNS (verified, not assumed):** the only caller is `ANodeShuffleSubsystem::RefreshTick`
(`NodeShuffleSubsystem.cpp:372`), on an actor in the played world, behind `bLayoutGenerated` + `ApplyLayout()`
— there is no main-menu path. The identity gate is still mandatory and **retries rather than latching**:
an empty identity (a client before replication) writes nothing and deletes nothing.

**BOOT SEMANTICS ARE A UNION.** KDF applies everything present at launch, so a player with three
playthroughs boots with all three namespaces applied. Every document is an *append* to SF+'s
`mAllowedExtractors`, whose consumer is a `TSet` (measured from KAPI's PDB), so duplicates collapse.

**MIGRATION: LEAVE-AND-UNION — the pre-T59 un-namespaced documents are left in place and NEVER deleted.**
Chosen on the mechanism, not on caution: *adopting* them into the loaded session's namespace would make
this session's very next pass delete them, reproducing this entry's churn for whichever save booted
first. **What the player sees: nothing changes** — the legacy entries are additive, duplicates collapse,
and a one-line `AUTOALLOW: T59 migration` log says they were found, left alone, and how to remove them
(the `NodeShuffle.AutoAllowExtractors=0` rollback lever, which clears the whole directory *including every
session*, or deleting the files without an `s-` prefix by hand).
**THE MIGRATION BRANCH CANNOT FIRE ON A FRESHLY BUILT INSTALL** (cold review F3): `DataForge/` is in no
build mirror list, so every build wipes the pack dir and the legacy count is 0 for reasons that have
nothing to do with the feature. **It therefore ships UNTESTED unless a legacy document is planted** —
runtime step R5 does exactly that. The pass now logs the empty case explicitly ("*migration check ran and
found 0*") so "no legacy documents existed" cannot be mistaken for "the check never ran".

**STALENESS — WHAT PER-SESSION BOUNDS AND WHAT IT DELIBERATELY DOES NOT FIX.** Option 3's amendment above
names the correctness cost of widening SF+'s allow-list beyond what a save actually needs; the union has a
weaker form of it. A session's documents track **that session's** rolls, so a re-roll or a mod removal
inside a playthrough still cleans that playthrough's entries on the next pass — the population is bounded
by rolls the player actually made, not by every extractor×resource pairing the install permits.
**Not fixed:** documents belonging to *other* playthroughs still apply while you play this one, so an
extractor may be placeable on a node type your current session's layout never produced. That is a
strictly smaller widening than option 3 and a strictly larger one than a single-save pack. Deleting
another session's documents to fix it is the defect this entry exists to end; a garbage collector keyed
on "sessions that no longer exist on disk" is the shape a future fix would take, and is **not** in scope
here.

**DIAGNOSTIC:** `AUTOALLOW PACKCHURN:` is split — own-namespace counts (regeneration, *expected*) and
`crossNamespaceRemoved` **which must read zero**, measured by re-listing the directory after the writes,
with `otherNamespaceDocsBefore` on the same line as its denominator. A nonzero cross figure is a
regression to this entry.

### T61. THE HOOK NOW ARMS WHETHER OR NOT THE MASTER GATE IS ON — DEFAULT OFF MEANS *OBSERVE*, NOT *BLIND*. **Status: BUILT AND SHIPPED (packet `ns-t61-observe-always`, commit `5472648`, 2026-08-10, marker `2026-08-10-t61-1`). Cold-reviewed same day, verdict SHIP WITH TESTS, then a scoped re-review of 11 fix-applications (doc/string one-liners) — all confirmed correct (`_team/nodeshuffle-followups/t61-coldreview.md`). Verified in-game on the real save overnight: notice pipeline fired correctly (batched EMIT for 34 dirty resources; rows persisted 13→47), scanner-knowledge grew 8→35→42, zero LogNodeShuffle errors all night (`_team/NodeShuffle-state.md` 2026-08-10 ~19:0x block). (status per those sources, 2026-08-12 release-readiness audit)**

**THE AUTHOR'S RULING, verbatim (2026-08-10):** *"default off, but we need detection if off or on to
build our list and show in chat if not in our list and to show in config the list for allowing users to
opt in."*

**WHAT THIS REVERSES, deliberately.** [[T58]]'s R1 paragraph below documents the opposite behaviour:
with `NodeShuffle.DestroyerVeto 0` the veto module was either never armed or actively disarmed, so a
default install measured **nothing** — no census, no per-resource rows, no evidence at all. That was
correct for T58's question (does the veto work) and wrong for the author's (what is out there, before I
decide). The ship default is unchanged: **`NodeShuffle.DestroyerVeto` stays 0.** What changed is what 0
*does*.

**THE OBSERVE-ONLY INVARIANT — the load-bearing claim, and the only one worth reviewing hard.** While
the mode is observing, `IsRequirementMet` returns **true for every target class, managed nodes
included**, from a single guard placed above every veto return. Nothing on the path above that guard
mutates an actor, a component or the requirement chain — it counts, classifies, offers a config row and
queues a chat notice. So an armed-but-observing world differs from an un-hooked world **only** in log
lines, config rows and one chat message. `tools/check_t61_lint.ps1` pins the guard, its position
relative to every `return false`, the unconditional arm call, the `!bObserveOnly &&` protection term and
the mode field (6 mutants, all caught).

**THE SIX CELLS.** The four-cell matrix T58's review used is now six, because the master gate has two
meanings and two states can produce no measurement at all.

| # | `DestroyerVeto` | `ProtectForeignNodes` | KBFL | mode | managed nodes | foreign nodes | list rows | chat notice | census |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 0 (default) | 0 | present | observing | **not vetoed** | not protected | added, ticked | fires ("not protecting them; here is how to opt in") | yes, `mode=observing` |
| 2 | 0 (default) | 1 | present | observing | **not vetoed** | not protected — the CVar is **not latched** this world | added, ticked | same as 1 | yes, `protectLatched=0` while the CVar reads 1 |
| 3 | 1 | 0 | present | enforcing | vetoed | classified + counted, never protected | added, ticked | fires ("not protecting them; set `ProtectForeignNodes 1`") | yes, `mode=enforcing` |
| 4 | 1 | 1 | present | enforcing | vetoed | protected (broad-sweep assets only, F1) | added, ticked | fires ("ticked means NodeShuffle steps in") | yes |
| 5 | 1 | 1, row **unticked** | present | enforcing | vetoed | that resource allowed through ([[T60]]) | row kept, never re-ticked | not re-announced (it already has a row) | yes, `foreignAllowedPlayerUnticked` moves |
| 6 | either | either | **absent / ABI guard trip / module load fail** | **not armed** | not vetoed | nothing seen | none | none | **no line at all** |

Cell 6 is the remaining blind state and is blind for a structural reason: the hook exists only inside
KBFL's requirement chain. It is distinguishable in the log — an observing world prints a `VETOCENSUS`
line, a never-armed one prints none, and the arm entry point says which happened.

**WHAT OBSERVE MODE MEASURABLY DOES.** (a) one `VETOCENSUS` line per world at T+30 s (and T+300 s if
`foreignSeen` moved) carrying `mode`, the armed-asset denominator and the full partition; (b) a row per
foreign resource in the settings list, default ticked, through the same [[T60]] population pass as
enforcing mode; (c) one chat notice naming the resources that got a **new** row, pointing at the list,
and saying plainly that nothing is being protected; (d) one `Display` line the first time a managed node
passes through un-vetoed, so the invariant is visible in the log rather than inferred from an absence.

**R1 IS SUPERSEDED, NOT DROPPED, and the disarm entry point is DELETED.** T58's R1 hazard was a world
loading with the gate off and evaluating against the *previous* world's latch and protect set. The
observing arm pass's first two statements are exactly the two calls R1's disarm made
(`ResetSessionCounters` — which now also latches the mode — and `ResetForeignProtectAssets`), plus it
rebuilds the broad-sweep set and schedules the census. R1's own weakness (the disarm was a no-op when
the veto module had never loaded) cannot arise: no module, no prepend, no state. One path instead of two.

**FIELD RENAMES IN `VETOCENSUS`, both because the old names became untrue in the new mode:**
`protectCvar` → `protectLatched` (in observe mode the CVar can read 1 while the latch is 0), and
`managedVetoed` → `managedSeen` **plus a new `managedVetoed`** that only ever moves on a real
short-circuit. `partitionSum` uses `managedSeen`. A grep for an old name now finds nothing rather than
the wrong number.

**THE CHAT NOTICE IS QUEUED FROM THE ROW ADD, NOT THE SIGHTING** — so its sentence *"it is now in your
settings list"* is a report, not a prediction (the population pass can be capped or the config tree
unreachable). That also supplies the cross-load dedup for free: a resource that already has a row is
never re-added, so it is never re-announced, with **nothing persisted** — the same self-clearing state
test [[T55]] is built on. Within a session an announced-set makes it delta-only. It rides the existing
`ShowCompatibilityNotices` setting and posts through ONE extracted chat emitter shared with the pending
notice.

**CONSEQUENCE ACCEPTED: the hook now evaluates on every armed asset on every load, for every player, by
default.** T58's cost figures were conditional on the gate being on. **T61 adds NOTHING per evaluation —
that delta is zero — but on a default install the WHOLE cost is new, and it is more than "two path
compares" (cold review F7).** The foreign branch fills two `FString` out-params per node target, builds a
full object path with `GetPathNameSafe(From)` **per foreign evaluation**, and does **two `FString`
copy-assigns per evaluation** into `GNodeShuffleForeignByClass.FindOrAdd` (the resource path and the
last-asset path; the node-type field is an `int32`) **plus one key copy on insert only**, on top of the
`TMap<FString>` sighting write — so a world with N foreign nodes now does **O(N) path-string allocations
inside the KBFL sweep that begins ~0.9 s AFTER world init, on every KBFL install**, where it previously
did zero. (That figure is a latency; the sweep's own duration is unmeasured.) One sweep per world, not
per frame. **Unmeasured on this machine — grade the `evals` field against THAT model, not against the
per-evaluation delta.**

**IMPORTS BASELINE — THE NEXT RECAPTURE MUST EXPECT `2 REMOVED + 1 RENAMED` (cold review F6).**
`NodeShuffleVetoKBFL` loses `IsForeignNodeProtectionEnabled` and `SetKBFLVetoDisarmFunction`, and
`SetKBFLVetoArmFunction`'s parameter list changed, so its **decorated** name changed — a naive diff shows
1 removed + 1 added on top of the two deletions. Expect **2 removed, 1 renamed, 0 otherwise-new**; any
other new NodeShuffle import is a finding, and must not be absorbed into this explanation.

### T60. THE T58 PROTECTION IS ALL-OR-NOTHING, SO A PLAYER WHO WANTS SF+'s RESEARCH GATING BACK FOR **ONE** RESOURCE HAS TO GIVE IT UP FOR ALL OF THEM. **Status: BUILT AND SHIPPED (packet `ns-t60-protect-checkboxes`, commit `f2b8d67`, 2026-08-10, marker `2026-08-10-t60-1`). Cold-reviewed same day — round 1 DO-NOT-SHIP ×11 → all applied → scoped pass upheld both packet declines, R1+header fixed (`_team/nodeshuffle-followups/t60-coldreview.md`). Verified in-game across the T64-T67 follow-on work: fresh discovery `rowsAddedThisLoad 34` (2026-08-10 night), `T65ROUNDTRIP 49/49/0/0` + `T60CENSUS rowsLoadedFromDisk=49` (2026-08-11), untick round-trip DIRECTION 1 full pass persisted across quit/relaunch (2026-08-11 ~19:4x). Round-trip DIRECTION 2 (re-tick clearing back to 0) VERIFIED 2026-08-12: after the user re-ticked SteelIngot and loaded a world, `FactoryGame.log` printed `T60CENSUS latch: rowsInFileAtLatch 49 untickedRows 0 malformedRows 0 blankRows 0 optOutPathsLatched 0` and `T65ROUNDTRIP first-sync: ... diskRowsUnticked 0` (measured by the orchestrator session, log lines 2465/2590 of that boot). BOTH DIRECTIONS PROVEN. (status per `_team/NodeShuffle-state.md` 2026-08-10 night block + 2026-08-11 ~18:0x/~19:4x blocks, 2026-08-12 orchestrator log measurement, 2026-08-12 release-readiness audit)**

[[T58]] ships one CVar, `NodeShuffle.ProtectForeignNodes`, and it governs every foreign resource at
once. The author asked (2026-08-10) for a **dynamically-populated checkbox list in the mod-config UI**:
one row per foreign resource the veto has actually seen, so unticking a row hands **that resource
only** back to SF+'s cleanup. Built as **lane A** of
`_team/nodeshuffle-followups/T59-dynamic-config-research.md` (the file is named T59 but is the research
for THIS entry; T59 below is the unrelated pack-churn item).

**MECHANISM.** A `UConfigPropertyArray` in the C++-built schema
(`Source/NodeShuffle/Private/NodeShuffleConfig.cpp`) whose element template is a section of
`{Resource: String, Protected: Bool}`. Rows are added at runtime with SML's `AddNewElement()`. Section
`Serialize`/`Deserialize` iterate the **schema**, so an ad-hoc key in `NodeShuffle.cfg` would be dropped
— but array `Deserialize` empties `Values` and re-allocates **one element per JSON entry**
(`ConfigPropertyArray.cpp:117-137`), so rows added at runtime **do** round-trip. That is why the list
had to be an array and could not be per-resource top-level keys.

**POPULATION RULE.** A row is offered on the **first sighting of a resource this world session**, from
inside the veto's own requirement evaluation (`NoteForeignResourceSighting`). Rows are **keyed by the
resource descriptor class PATH** — not by node actor class, not by any display string — because that is
the only key that is stable across loads and unique across mods; the UI label is
"<MountRoot>: <DescriptorName>" derived from that path (T65), the row's text box holds the path
verbatim, and the row tooltip repeats it in full. Rows are **never removed and never reordered** by the mod, so a tick the player set is
never disturbed. Population runs even while protection is latched OFF, or the player could never untick
something they had not first been protected from.

**DEFAULT-PROTECTED POLICY (author's both-mods-work ruling).** A resource with no row, an untouched row,
or a row the latch could not read is **PROTECTED**. Only an explicitly unticked row opts out. The
empty-set case therefore behaves exactly as T58 shipped.

**WHEN AN EDIT APPLIES: THE NEXT WORLD LOAD.** The opt-out set is latched once in
`ANodeShuffleSubsystem::BeginPlay`, immediately before the veto arms, for the same reason T58 latches
its own policy — and because the sweep it governs runs ~0.9 s after world init, so a pause-menu edit
could not affect the session it was made in even under live re-reads. Consumption is one
`TSet<FString>` hash lookup per foreign sighting; **no config-tree walk ever happens at sweep time.**

**THE S1 EXCEPTION IS FILTERED OUT OF THE LIST.** A vanilla-class resource well NodeShuffle itself
retyped to a modded resource grades `Foreign` (see T58's S1 note) and is protected — correctly, because
that is protecting our own retype. It is **not** another mod's resource and must never be a checkbox
that switches off protection for our own work. Recognised by the ACTOR CLASS being under `/Game/`
(`ClassifyResourceNodeOrigin` now publishes that boolean) and **counted, not silently dropped**.

**RELATION TO [[T59]]: ADJACENT, NOT FIXED.** T59 is the SF+ allow-list **pack** being per-install while
its content is per-save. Nothing in T60 touches the pack, the allow-list writer, or the notice. A player
who unticks a resource here still gets T59's churn on the next save alternation. Do not close T59
against this entry.

**KNOWN WARTS, accepted rather than fixed.** (1) The stock array widget exposes Add / Remove / edit-string
on our list; a hand-added or edited row is inert (it matches no resource path) and a removed row returns
on the next sighting — the tooltip says both. Suppressing them needs a custom widget (lane B), which the
research costed at several times lane A for nothing this feature needs. (2) `DefaultValues` is empty, so
the in-game **Reset** button clears the list and the player's unticks with it; the rows come back, the
unticks do not.

**UNVERIFIABLE STATICALLY — the one thing a build cannot settle from headers:** whether
`AddNewElement()`'s `NewObject`-with-archetype instances the template's `Instanced SectionProperties`
map per element. If it does not, every row shares one tick box. The code **measures** it (pointer
identity against the live template) and logs `LogNodeShuffle Error` if they compare equal — but the
in-game check is still owed: untick ONE row of two and confirm the other stays ticked after a reload.

**COLD REVIEW 2026-08-10 — DO NOT SHIP → 11 findings applied, all in-packet.** Two were P1. **F1 was a real
SYMMETRY bug and the one that would have shipped damage:** the S1 exception was enforced on the population
side only, so a player unticking AlkaLib's lithium row would also have stripped protection from
NodeShuffle's own retyped wells yielding that resource — the exact outcome `NodeShuffle.h`'s S1 note says
must never happen. Consumption now carries the same `bNodeClassIsVanilla` term. **F2** was a false UI claim:
the tooltip said `NodeShuffle.DestroyerVeto` was on by default; `NodeShuffle.cpp:82` defaults it to **0**, so
on a default install the list would never fill and the panel said otherwise. Also applied: two ungraded
tooltip claims removed (mineability is untested — SF+ has a separate extractor allow-list; "as if not
installed" overclaimed), the n:1 resource↔node-class collapse now stated in the copy, the shared-subobject
guard widened to `Deserialize`-created rows and made non-writing, a 256-row cap, and
`bRequiresWorldReload=true` so an in-world Reset cannot wipe the list.

**TWO THINGS DELIBERATELY LEFT OPEN, both needing a runtime measurement before anyone "fixes" them.**
1. **Row labels (F7).** `HasHeader` was `false` while the design stamps a per-row `DisplayName` every sync —
   the two contradicted each other and **neither can be proven dead from C++** (`Widget_CP_Array` and the
   `CP_Section` widgets are Blueprint). They are now consistent (`HasHeader=true`, plus a template fallback
   label so `Deserialize`-created rows are never blank in the main-menu panel, where the sync never runs).
   **Runtime test step 3 decides which mechanism is inert; delete the loser then, not before.**
2. **Pruning (F9).** Nothing prunes, on purpose: a stale row for an uninstalled mod still carries the
   player's untick, and "the path no longer resolves" is **not** evidence of staleness — an unloaded mod's
   path does not resolve either. The cap bounds the growth; the pruning policy is unresolved.

**HELD IN RESERVE (reviewer's alternative D):** keying rows by NODE ACTOR CLASS instead of resource removes
F1 and F6 structurally (our retyped wells are a distinct actor class), at the cost of several rows per mod.
Not taken this round — rekeying unreviewed to answer a review is how the second bug gets in. Take it if the
F1 fix proves insufficient in runtime test 8.

**TWO ARTEFACTS ARE LOAD-BEARING *TOGETHER* — DO NOT REMOVE EITHER ALONE.** This is the condition on which
the scoped re-review accepted keying rows by RESOURCE rather than rekeying to node actor class:
1. the `bNodeClassIsVanilla ||` conjunct in the veto's consumption predicate
   (`NodeShuffleDestroyerVetoRequirement.cpp`, the T60 block), and
2. the array tooltip's sentence *"unticking a row can never affect one"* about NodeShuffle's own retyped
   wells (`NodeShuffleConfig.cpp`).
(1) is what makes (2) TRUE; (2) is what corrects the same tooltip's *"you hand back every node type that
yields that resource"* from an over-claim into an accurate statement. Delete (1) and the panel ships a false
claim — the F1 bug, back. Delete (2) and (1) becomes an undocumented surprise. Either deletion makes the
resource key indefensible and the fallback is alternative D above. Both code sites carry this note.

**RE-REVIEW ROUND 2 (FIX-FIRST, one item).** R1: the shared-subobject Error asserted *"any row added this
pass was withdrawn"* on a condition that includes row-to-row sharing, while only the TEMPLATE branch
withdraws — two shared DISK rows with a clean template would have made it contradict `addedThisPass` on the
next census line ([[lessons-log-asserted-a-cause]]). Withdrawal is now a counted field. Also applied:
`HeaderText` is seeded on the row template and stamped alongside `DisplayName` through one shared helper,
because `HasHeader=true` with `HeaderText` never set is a blank header bar — a third label surface. The
fallback text is `(resource)`, not `Resource`, so a stamp failure is distinguishable and does not collide
with the child field's own label. Runtime test 3 now decides among **three** surfaces, not two.

**DIAGNOSTICS.** `T60CENSUS latch` (once per world init: rows in the file, how many unticked, malformed,
blank) and `T60CENSUS sync` (rows seen / in config / loaded from disk / added / unticked-in-file /
opt-outs acting this world / whether the latch ran / S1 sightings suppressed). The veto's own
`VETOCENSUS` line gains `foreignAllowedPlayerUnticked`, a bucket **disjoint** from
`foreignAllowedAssetNotInBroadSet` — those two mean opposite things (we could not evaluate the asset
vs. the player asked us not to) and must never be conflated.
**`s1RetypedWellSightingsSuppressed` COUNTS REPEAT SIGHTINGS, NOT DISTINCT WELLS (measured 2026-08-11,
`_team/nodeshuffle-followups/s1-variance-2026-08-11.md`):** across three t64-2 sessions it read 4→45→44
while the whole dealt-well population was 23 — the value exceeds the population, so it is evaluation
traffic, not a well count; the 4→45 jump lands at the reroll→reload boundary. Benign accounting; do not
read it as "N wells were filtered", and slice it by session before quoting it anywhere.

### T66. THE PROBE COMMANDS' ENCLOSURE-GATE LEG SELF-HIT THE PROBED ENTRY'S OWN LIVE ACTOR — 8/8 rays blocked at 0 cm by the node itself — a false REFUSE the real placement path can never produce. **FIXED same day (packet ns-t66, marker t66-2); the suppression MECHANISM is ASSUMED until the in-game re-probe.**
**Measured 2026-08-11 (entry 526, `BP_ResourdeNode_Alkali_C_2147344490`, lithium):** every gate ray hit
the probed entry's own live actor at 0 cm because the probe replays the gate at a spot where the node
NOW stands, while real placement evaluates spots before any node exists — the containment instrument
already excluded its subject; the gate leg did not. **Fix:** `IsSpotEnclosed` gained an additive
optional `IgnoreActor2` (default null — all 5 non-probe callers byte-identical, lint-pinned); NODEPROBE,
WELLPROBE (gate leg + eye reading, review F2) and `AuditPlacements` reading 3 (review F1 — its
severity ranking was poisoned by the same self-hit) now pass the probed entry's IsValid-checked live
actor, and every verdict line prints `ignoredOwnActor=` read back from the pointer the call used.
**Graded ASSUMED, not proved:** that `AddIgnoredActor` suppresses the self-hit at runtime is Unreal's
closed-source collision layer — same class T36 needed an in-game log excerpt for. The re-probe at
entry 526 (`ignoredOwnActor=` names the actor, per-ray lines show terrain, DISAGREE resolves or
becomes genuine) is the gate for trusting the new readings; group tallies must move one-directionally
(ignoring more can only reduce blocked counts). Review: `t66-coldreview.md` SHIP WITH TESTS.

### T67. FOUR PLAYER-FACING SURFACES ASSERT A REMOVAL THE HOOK NEVER OBSERVES — "tries to remove" / "steps in to stop that removal" — where what is measured is a KBFL REQUIREMENT EVALUATION. Copy-only; filed from T65's park + cold-review L5 (2026-08-11).
The four: `NodeShuffleConfig.cpp:346`, `:349`, `:449` (tooltips) and `NodeShuffleObserveNotice.cpp:205`
(the notice's enforcing branch). T58's RefinedPower case measured that an asset can evaluate the
requirement on nodes it never destroys, so "tries to remove" asserts intent the hook cannot see —
the same claim-vs-measurement family the T61 notice fix closed. **Fix all four together in one pass**
(the T65 review's L5 failure scenario is a partial fix reproducing the defect); wording should say what
is measured: the other mod's handler *checked* the node / *its requirement was evaluated*. One packet,
copy + regrade of each sentence, no behavior change. Cold review L4's legend rewrite is the tone model.
Also queued for the same packet: `diskRowsUnticked %d` on `T65ROUNDTRIP` (review M2 fix b) and the E
alternative (carry the measured parse flag per notice item instead of re-deriving via `Contains(": ")`).

**T67 STATUS — IMPLEMENTED-PENDING-INGAME (build `t67-2`, 2026-08-11). The packet grew past copy: the
panel work the author asked for on 2026-08-11 was folded in, and the measurements it produced are
recorded here because a code comment cited this entry as their record.**

* **THE DOUBLED ROW LABEL (`X (X)`) — ROOT CAUSE MEASURED, NOT INFERRED.** SML's section widget builds
  its header from a `FormatText` node whose literal pattern is **`{HeaderText} ({DisplayName})`**. The
  string is present verbatim in
  `Mods/SML/Content/Interface/UI/Menu/Mods/ConfigProperties/Widgets/BaseClasses/Widget_CP_Section_Base.uasset`
  (byte-scan of the packaged asset, ASCII + UTF-16, 2026-08-11); it was the only `{…}` format string in
  that whole asset tree. T65 stamped ONE string into BOTH slots, which is the whole defect, in the
  stamped path and the template-fallback path alike. **This closes step 3's "which surface is inert"
  question and the dated TODO in `NodeShuffleConfig.cpp`: the answer is NEITHER — they are two slots
  of one format. The main-menu observation step 3 also asks for is still owed (T67 checklist steps
  1-2).** Neither slot may be emptied (the parentheses are literal), so they now
  carry different parts: `HeaderText` = resource name, `DisplayName` = mount label. A row reads
  `esc_Wire (AllMinable)`. **Consequence, deliberate: the panel row reads `Resource (Mod)`, not
  `Mod: Resource`** — the component order is dictated by SML's format, not chosen. The composed
  `Mod: Resource` form is unchanged in the chat notice and every log line. Whether the slot assignment
  should flip is DEFERRED until the author has seen both surfaces in game (T67 review A1).
* **MAIN-MENU LABEL STAMPING.** `StampForeignResourceRowLabelsFromConfig` — a label-only pass hooked from
  `URootInstance_NodeShuffle::DispatchLifecycleEvent` at `POST_INITIALIZATION`. Measured ordering:
  `UGameInstanceModule::DispatchLifecycleEvent` calls `RegisterDefaultContent()` at `INITIALIZATION`
  (`GameInstanceModule.cpp:30-37`) → `UConfigManager::RegisterModConfiguration`, which loads the `.cfg`
  inline (`ConfigManager.cpp:258-288`). The pass adds no row, writes no value, marks nothing dirty and
  saves nothing (lint-pinned). Rationale: SML greys a `bRequiresWorldReload` property out in the pause
  menu (SML's documented contract for the flag; the greying itself is Blueprint-side — graded ASSUMED,
  runtime step 11 settles it), so the main menu is the only editable surface, and the population pass
  is in-world/authority-only
  — the editable surface was the unlabelled one. **UNVERIFIABLE STATICALLY:** that the lifecycle is
  dispatched at all in the main-menu game instance. `T67MENUSTAMP` in the log is the decider.
* **ITEM D — `/Game` RENDERS AS `Satisfactory`** (user-approved 2026-08-11), one equality test in the
  label derivation so panel, notice and logs cannot disagree. The string that is TESTED anywhere is
  still the literal path. Graded **ASSUMED**, not measured: that `/Game/` is the base game's content root
  is the engine mount-root convention and the same assumption the Foreign classification rests on.
* **ITEM A (left-justify the label line, compact the row, kill the per-row scrollbar) — PARKED AT A
  BLUEPRINT WALL, and this is the measurement so nobody re-derives it.** `UCP_Section`
  (`SML/Public/Configuration/Properties/WidgetExtension/CP_Section.h`) exposes exactly `WidgetType`,
  `HasHeader`, `HeaderText`, `Collapsed`. **No SML config property exposes alignment, justification,
  slot size, fill or padding to C++ at all.** Row layout and header justification live in SML's own
  Blueprint widgets (`Widget_CP_Section_Base`, `SectionsWidgets/Widget_Section_HB`,
  `Container/Widget_CP_Container`, `ValueWidgets/Widget_CP_Section_Horizontal`) — SML's assets, not ours.
  Options, none started: accept the width reduction the doubling fix already gives; shorten our own child
  `DisplayName` strings (the only remaining C++ lever); ship an SML content override (real fix, and
  NodeShuffle then carries SML UI that drifts every SML update); or raise it upstream with SML.
* **OPEN, FILED BY THE T67 COLD REVIEW (F7) — NO CODE CHANGE THIS ROUND.** Item D maps two distinct
  mount roots onto one displayed string: `/Game/` and any plugin whose mount root is literally
  `Satisfactory`. Such a plugin's rows would render `Foo (Satisfactory)` in the panel and
  `Satisfactory: Foo` in the notice, indistinguishable from base-game rows, while the notice legend
  asserts *"Satisfactory is the base game's own"* — false for those rows. That is the same T62/T63
  ambiguity the mount segment exists to prevent, at low probability. The free fix is to render
  `Satisfactory (base game)`; it was deferred because the parenthetical adds width to the row item A
  is trying to narrow, and that trade-off is the author's.
  **Whoever takes it must update `check_t67_lint.ps1`'s item-D pin regex AND the legend pin at `:93`
  in the same edit.**

### T58. ON A NEW GAME, SF+ DESTROYS EVERY THIRD-PARTY RESOURCE NODE ~0.9 s AFTER WORLD INIT — before our roll can enumerate them — so lead, lithium/Alkali and AllMinable's `Res_*2_C` family are EXTINCT on new saves. **NOT a NodeShuffle regression. Status: DECIDED 2026-08-10 (author: protect veto, option 1, DEFAULT ON) → IMPLEMENTED-PENDING-BUILD-REVIEW-AND-INGAME — see the T58 STATUS block at the end of this entry.**
**AUTHORITATIVE SOURCE for every claim, quote and option below:
`_team/nodeshuffle-followups/veto-spawnwindow-regression.md` (read-only investigation, 2026-08-10, at
`f84930d`). Read it before acting on this entry; nothing here is re-derived.** The investigation was
opened as *"the veto flipped `lead_C` from spawn-window to not-ours"* — that framing is **wrong**, and
the entry is filed under what was actually measured.

**THE MECHANISM — our veto's predicate cannot see a level-placed foreign node, by construction.**
`Source/NodeShuffleVetoKBFL/Private/NodeShuffleDestroyerVetoRequirement.cpp:26-33`, verbatim:

```cpp
const bool bRegistered  = TargetActor && FNodeShuffleModule::IsManagedSpawnedNode(TargetActor);
const bool bSpawnWindow = !bRegistered && TargetActor && FNodeShuffleModule::IsSpawningManagedNode();
const bool bManaged     = bRegistered || bSpawnWindow;   // !bManaged -> return true (allow)
```

* `IsManagedSpawnedNode` = membership in `GNodeShuffleManagedNodes` (`NodeShuffle.cpp:114-117`).
* `IsSpawningManagedNode` = **a global game-thread depth counter**, `GNodeShuffleSpawningDepth > 0`
  (`NodeShuffle.cpp:119-130`), incremented by `FNodeShuffleSpawningScope` placed tightly around **our
  own** `SpawnActor` calls (`NodeShuffleSubsystem.cpp:4299`, `NodeShuffleWellSpawn.cpp:153,268`). It is
  **not a time window, not a class list, not per-actor** — do not read `VETO (spawn-window)` as "a
  window of leniency"; it means *we were inside our own SpawnActor for that actor.*

Both inputs are therefore **"did NodeShuffle create or adopt this actor"**. SF+'s
`WorldRequirement_ResearchNodeRemover` fires while the registry is empty and no spawn scope is open, so
the predicate returns `allow` — and is *correct* to.

**THE TWO-SAVE DISCRIMINATION (measured; `Destroy:` counts per log, all logs build `t54-1`, so build is
excluded — the flip has `t54-1` logs on BOTH sides):**

| save | logs | pre-registered at arm | lead destroyed | alkali destroyed |
|---|---|---|---|---|
| **TestWithMods** (established) | 6 (`07.37.43`, `07.45.49`, `17.08–17.15`) | 139 / >0 | **0** | **0** |
| **TestAllMinables** (new game) | `18.10.09` | **0** | **6** | **23** |
| TestAllMinables (reload) | `18.39.39`, `FactoryGame.log` | 70 / 54 | 6 | 23 |

The first flipped session, in one timeline:

```
18.07.31:549  veto: pre-registered 0 restored nodes before arm (new game)
18.07.31:549  veto: NodeShuffle.DestroyerVeto=1 at world init — arming    (armed on 2 assets)
18.07.32:461  [WorldRequirement_ResearchNodeRemover]: Destroy: lead_C_2147471175 | Resource: oreleaddesc_C
18.07.38:346  veto: IsRequirementMet ... class='BP_ResourceNode_C' -> VETO (spawn-window)   <- our FIRST spawn
```

**SF+ kills the foreign nodes 0.9 s after world init and ~5.9 s before NodeShuffle spawns anything.**
Arming is excluded as a candidate (it *precedes* the destroys and the requirement was in the chain);
config is excluded (`DestroyerVeto=1` and identical arm output in both eras); `VETO (managed node)`
fires **0 times in every log**. On the established save the same classes appear only as
`VETO (spawn-window)` at `07.34:01`/`17.30:29` — **minutes** after load, during our own spawn passes:
they were our re-spawns, protected because they were already ours.

**SOURCING CAVEAT, stated because the shape of the claim invites over-reading:** *no log contains*
`class='lead_C' -> allow (not ours)`. The trace is Verbose+diag-gated and starts ~5 s after load, i.e.
**after** the destroys. "Today they get allow (not ours)" is an *inference* — correct in substance
(the predicate has no other branch available) but **not a quoted log line**.

**WHY THIS IS NOT A REGRESSION, AND WHY THAT MATTERS FOR THE FIX.** `Source/NodeShuffleVetoKBFL/` has
**no commit since 2026-07-25** (entire history: `c0c53da`, `0dd69b2`, `288d416`); diffing
`a6c339b^..ee01d38` over the veto and both its inputs is **1 file, +1/-1 — the build-marker string
only** (`NodeShuffle.cpp:407`), and widening to `--since=2026-08-03` (48 commits) changes nothing. The
founding contract (`288d416`) never claimed this ground: it *"skips their destroy chain for nodes we
spawned/adopted (hidden originals never vetoed)"*, and the header states *"for every other actor this
returns true and the asset behaves exactly as if NodeShuffle were absent."* **Protecting third-party
nodes was a side effect of adoption, never an advertised capability** — so there is nothing to
"restore", and any fix is NEW capability that must be designed, not a repair.

**THE ESTABLISHED SAVE IS NOT SAFE BY DESIGN EITHER — it survives only because those actors were
already in the managed registry** (our own registered respawns are veto-protected). That is
save-contents luck, not a guarantee.

**RESHUFFLE DOES NOT RECOVER THEM (author-measured, 2026-08-10):** a manual reshuffle + save + reload
brings nothing back. The level **re-instantiates** the foreign nodes every load and SF+ **re-destroys**
them every load, still before we look — 6 lead + 23 alkali destroyed again in both later loads. There
is no in-game workaround from our side.

**OPEN — THE DISCRIMINATING MEASUREMENT, AND IT DECIDES WHETHER ANYTHING CHANGED AT ALL.** The author
had lead/lithium on low-tech **new** games about a week ago; the logs that would settle it have rotated
out (unprovable from evidence on disk, and nothing in NodeShuffle *can* protect a first-load new game —
the registry is 0 by construction). **Run: a new game with NodeShuffle DISABLED, SF+ on; grep
`ResearchNodeRemover]: Destroy:.*oreleaddesc`.** If they still die, NodeShuffle never protected new
games, SF+'s research gating of these resources changed in the last week, and there is no regression to
chase. **Do not choose a fix option before this runs.**

**THE STANDING AUTHOR RULING IS BROKEN ON NEW GAMES.** *"We've already worked on them [lithium, lead,
chlorine] and have them going, I just don't want them excluded in new things we do"*
([[nodeshuffle-modded-nodes-in-scope]], quoted in full at [[T43]]). With SF+ loaded, a new game loses
that whole population before the roll sees it, so the resources are not merely *excluded from a new
feature* — they are absent from the world.

**Options (from the source doc §5), with the trade-off each buys:**
1. **Non-vanilla protect veto — opt-in CVar, default OFF (INVESTIGATOR'S RECOMMENDATION).** Veto
   destroys of any `FGResourceNodeBase` whose resource/class is not `/Game/`. Fixes AllMinable **and
   every future resource mod at once** — the only option that satisfies "modded nodes permanently in
   scope" for *arbitrary* mods. Cost: it silently overrides SF+'s deliberate research gating — a
   balance change to another author's mod — so **opt-in only, and the settings copy must say so**
   (player-facing copy is a graded claim; see the T1 block below).
2. **KDF research-requirement patch (recommended alongside, for the specific SF+ pairing).** Patch the
   requirement for `oreleaddesc_C`/alkali so SF+ never gates them. No C++ risk, SF+-native, and honest
   — it changes SF+'s balance **in SF+'s own data** rather than vetoing its code. But it is
   per-resource, per-mod, and does **not** generalise to unseen mods.
3. **Adopt-early.** Claim the foreign population where we already `pre-register` restored nodes, before
   the arm. Smallest change, reuses the registry path, and the roll wants that population anyway. Same
   balance consequence as (1); and **a node we adopt and never deal is a new state to reason about.**
4. **Widen the spawn window — REJECTED OUTRIGHT.** It is a race-closer for our own `SpawnActor`.
   Widening it protects *whatever happens to be judged during our spawn bursts*: **non-deterministic
   and untestable.** Do not touch it. ("Restore the old classification" is likewise a non-option —
   nothing changed, so there is nothing to restore.)

**Cross-links.** [[T43]] is the same class-hierarchy seam seen from the other side: our
`TActorIterator<AFGResourceNode>` *under*-matches and cannot see `AFGResourceNodeBase` subclasses,
while SF+'s remover filter *over*-matches down that same base and sweeps up third-party nodes we never
intended it to reach — and option 1 would have us predicate on `FGResourceNodeBase` deliberately, so
T43's warning applies verbatim: **that changes a POPULATION, and needs a before/after count and a
differential review, not a one-line type change.** [[T54]]/[[T55]]/[[T56]]/[[T57]] are the same
2026-08-10 multi-mod-save investigation block; T55 shares this entry's shape — a documented design
decision, not a bug, that must be **re-decided** rather than patched.

#### T58 STATUS: **BUILT AND SHIPPED, DEFAULT ON** (packet `ns-t58-foreign-protect`, commit `4262b2b`, 2026-08-10, boot marker `2026-08-10-t58-1`). Cold-reviewed across 3 rounds — round 1 DO NOT SHIP (F1 confirmed default-ON regression) → fixed → round 2 FIX-FIRST → fixed → round 3 doc-only fixes, R1-R7 confirmed clean by code re-read (`_team/nodeshuffle-followups/t58-coldreview.md`). Field-confirmed in-game on the real save and a fresh new-game world: lead 6/6 + lithium 23/23 protected, user saw both in-world (`_team/NodeShuffle-state.md` 2026-08-10 ~15:4x and ~19:0x blocks). (status per those sources, 2026-08-12 release-readiness audit)

**THE AUTHOR'S DECISION (2026-08-10, verbatim): *"we are keeping shuffle mod active. we need both
active to work this situation."*** That selects **option 1, the non-vanilla protect veto — but DEFAULT
ON, not the investigator's recommended opt-in default OFF.** The balance consequence stands as
documented (SF+'s research gating of third-party resources is overridden while this is on) and is now
the shipped default. **`NodeShuffle.ProtectForeignNodes 0` restores the pre-T58 DECISION for every
evaluation, but it is NOT a clean revert for a save that has already run with it ON** (cold review F6):
layout entries persist `NodeClassPath`/`OriginalResourceClassPath`/`VanillaNodePath` as `SaveGame`
(`NodeShuffleSubsystem.h:198-234`), so foreign nodes enrolled while it was on stay in the layout, get
re-destroyed by SF+ once it is off, and go through the external-destroy tombstone into
`DormantThisSession` on every later load. Contained by coexist-1, but the entries do not disappear.
**REVIEWER'S DISSENT, RECORDED:** the cold review recommended shipping default OFF for one measured run
and flipping after. The author reaffirmed default ON. If the first measured run grades badly, default
OFF is the pre-agreed fallback and needs no new design.
**SHIP DEFAULT — DECIDED 2026-08-10 (author, decision 3 of the ship list; this CLOSES it) and implemented
by [[T61]]:** *"default off, but we need detection if off or on to build our list and show in chat if not
in our list and to show in config the list for allowing users to opt in."* So the shipped state is
**`NodeShuffle.DestroyerVeto 0`, `NodeShuffle.ProtectForeignNodes 1`** — cell 2 of T61's matrix: nothing
in the world is changed, the detection runs, the opt-in list fills, and the player is told once per new
resource. `ProtectForeignNodes`' own default stays 1 because it only has meaning once the master gate is
on. **The reviewer's dissent above is satisfied by this shape rather than overruled: the first measured
run now happens with the feature inert.**
**Option 2 (the KDF research patch) was NOT built** — it is per-resource and does not generalise;
**option 3 (adopt-early) was declined** — it invents an "adopted but never dealt" node state;
**option 4 remains rejected outright.** The **discriminating measurement (new game, NodeShuffle
disabled, SF+ on) was NOT run before implementing** — the author ruled on the outcome wanted rather
than on the provenance of the change, so this ships as new capability, which is what the entry above
already said any fix would be.

**WHAT LANDED.** The existing veto predicate gains a THIRD outcome at its existing interception point
only (KBFL `IsRequirementMet`); every other destroy path in the game is untouched.
`FNodeShuffleModule::ClassifyResourceNodeOrigin` (`Source/NodeShuffle/Private/NodeShuffle.cpp`) sorts
one target into `NotAResourceNode` / `VanillaOriginal` / `Foreign`, reusing the roll's own two-sided
`/Game/` test (`NodeShuffleSubsystem.cpp:7720-7722`) so the veto and the roll can never disagree about
what "vanilla" means. **PROTECTED SET: an `AFGResourceNodeBase`-derived target whose ACTOR class path
or RESOURCE class path is outside `/Game/`. Vanilla originals (both sides `/Game/`) are never
protected** — SF+ keeps managing those per its overhaul, exactly as the founding contract intends.

**S1 — THE ONE EXCEPTION TO "VANILLA IS NEVER AFFECTED", stated because that claim appears in the CVar
help, the arm log and this file (scoped re-review 3).** The predicate reads `GetResourceClass()`, which
consults `mResourceClassOverride` — and **NodeShuffle's own well retype WRITES that field** (SaveGame,
`NodeShuffleWellRetype.cpp:53`) on **LEVEL** wells that `NodeShuffleWellLink.cpp:275` never registers as
managed. So a stock `BP_FrackingCore*`/`BP_FrackingSatellite*` that we retyped to a **modded** resource
grades `Foreign` and IS protected. That is NodeShuffle protecting its own retype, not another mod's
node — the veto's *managed* path cannot see it because the retype never registered it. **Recognition in
the grading run: a census `[...]` entry whose class name is a stock `BP_Fracking*` but whose `res` path
is not `/Game/`.** Deliberately left as a documented exception: a null/registry check that re-graded
such a well as vanilla would be self-authored logic on the well-registration seam, written under
pressure to close a review finding — the exact thing this project refuses to ship unreviewed.
A node reporting a NULL resource class is judged on its actor class alone — a null resource is no
evidence of a foreign resource, so a `/Game/` actor class with a null resource class stays
`VanillaOriginal` and is never protected (R3; real `AFGResourceNodeBase` targets do report null — the
roll carries a dedicated rejection reason for it). Node TYPE is not filtered
(a foreign fracking core or geyser is protected too, and its type is printed in the census).

**BREADTH SCOPING (cold review F1, HIGH — applied).** Protection is offered ONLY on armed assets whose
own target list is a **broad node sweep** (they target `FGResourceNodeBase` itself or a SUPERCLASS of
it). An asset targeting a strict SUBCLASS is that mod's own handler for its own nodes: measured in this
modset, RefinedPower's `ActorListner_RPTurbine` targets `RPWaterTurbine`/`RPWaterTurbineNode`, is armed
because it overlaps `FGResourceNodeBase`, and its node class is not `/Game/` — so without this scoping a
**default-ON** flag would have short-circuited that listener's whole requirement chain for every water
turbine node and silently disabled another installed mod. The set is declared at arm time and consulted
per evaluation. **THE ACCEPTED SET IS EXACTLY ONE SHAPE: an asset with a target class that IS
`FGResourceNodeBase`** (scoped re-review 2, R2). A strict SUBCLASS target is that mod's own handler
(rejected — F1's measured collateral); a strict ANCESTOR target such as `AActor` is a generic actor
tracker that merely overlaps nodes and is rejected as TOO BROAD — the arm pass's own comment records
that assets "as broad as `AActor`" do get armed, and short-circuiting one of those for every foreign
node would be F1 all over again. An unrecognised FUTURE listener therefore joins ONLY on an exact
`FGResourceNodeBase` target, and is otherwise never foreign-protected. The veto for NodeShuffle's OWN
managed nodes is unchanged and still applies on every node-relevant asset. The arm log now prints `broadNodeTarget=` per asset and the
census counts `foreignAllowedAssetNotInBroadSet`, so a breadth mistake is visible in the log either way.

**MODDED WELLS ARE INSIDE THE PROTECTED POPULATION — stated because the packet's first handoff said the
opposite (cold review F5).** `BaseNode_FrackingCore_KLib_C` / `BaseNode_FrackingSat_KLib_C_*` (documented as modded wells in this
file's **T14** entry — cited by heading, not by line number, because this block's own insertion already
invalidated one line-number citation once) grade Foreign under the T58 predicate. That is intended —
the author wants modded wells alive — but it means T54's immediate-hide, the well occupancy gate and the
T14 late-discovery residue all meet the protected population, and must be re-checked under that
assumption rather than assumed absent.

**TWO POPULATION ESCAPES, documented not fixed (cold review F7).** (a) UNDER-COVERAGE: a mod that
patches vanilla assets in place (KDataForge — which this workspace itself uses) or ships node BPs under
`/Game/` grades `VanillaOriginal` and is never protected; the feature silently under-covers it.
(b) OVER-REACH: if an overhaul re-points a node's `GetResourceClass()` to its own descriptor, that node
grades `Foreign` and NodeShuffle would veto **the owning mod's own removal of its own node**. The F1
breadth set does not cure (b) — only reading the per-class `res` paths in the census does, which is why
the runtime checklist requires that read.

**PREDICTION TO CHECK IN GAME, stated before the test so it can fail:** existing new-game saves
(`TestAllMinables`) should **recover** their lead/Alkali population on the next load with this build,
because the level re-instantiates the foreign level-placed nodes every load (measured, above) — those
nodes were never removed from the level, only destroyed at runtime. A save whose foreign nodes were
*runtime-spawned* by their own mod is not covered by that argument and may not recover.

**THE OFF DIRECTION IS STATEFUL (R1). ⚠ SUPERSEDED BY [[T61]] 2026-08-10 — READ T61 FIRST; THE
PARAGRAPH BELOW DESCRIBES A BUILD THAT NO LONGER EXISTS AND IS KEPT ONLY FOR THE HAZARD IT NAMES.**
Our requirement stays prepended on the KBFL CDO for the life
of the process, so a world that loads with `NodeShuffle.DestroyerVeto 0` after the veto armed earlier
would otherwise keep vetoing on the previous world's latch and protect set, censusless. ~~The main module
now calls a registered DISARM entry point on that branch (latch false, protect set emptied) and logs
one `Display` line naming the cleared state (its predicate is "the veto MODULE has loaded this
session", not "the veto armed" — those diverge when a world aborts at the ABI guard, and the line says
loaded) — that line is the only foreign-protect evidence such a
world produces.~~
**AS OF T61 THAT DISARM ENTRY POINT IS DELETED and `DestroyerVeto 0` no longer means "no hook".** The
world arms in OBSERVE-ONLY mode: the requirement returns true for every target class including managed
nodes, and the arm pass's first two statements do everything the disarm did, plus rebuild the
broad-sweep set and schedule the census. So the R1 hazard is closed by strictly more work on ONE path,
and such a world now produces a full `VETOCENSUS` line (`mode=observing`) instead of a single cleared-state
line. **The claim in this entry that an OFF world measures nothing is therefore FALSE for the current
build — that reversal is deliberate and is the author's ruling quoted in T61.**

**DIAGNOSTICS.** One `VETOCENSUS` line per world session at T+30 s (and again at T+300 s only if
`foreignSeen` moved — that is ALL a second line reports; it does not identify a retry loop, and nothing
measures one), carrying the full partition with denominators —
`protectCvar / armedAssets / evals = managedVetoed + foreignSeen + vanillaAllowed + nonNodeAllowed`
(**[[T61]] RENAMED TWO OF THESE**: the line now reads `mode`, `protectLatched`, and `managedSeen` as the
partition member with a separate `managedVetoed` that only moves on a real short-circuit — grep for the
new names, the old ones are gone),
plus `foreignProtected`/`foreignAllowed` and a per-class breakdown. Plus one
`veto: FOREIGN-PROTECT first requirement evaluation vetoed` line per DISTINCT class, naming the asset
that was evaluating (29 nodes must not become 29 lines). **Every one of these texts says "requirement
evaluation", never "destroy" (cold review F2): this hook vetoes an EVALUATION, and F1 proves an armed
asset that never destroys anything.** Both are `Display` and deliberately **not** diagnostics-gated: the
sweep lands ~0.9 s after
world init and the diagnostics flag is pushed seconds later, so a gated line is silent exactly when it
matters — which is why no log in the investigation above could quote the decisive trace.

**THE ONE PREMISE THIS PACKET COULD NOT VERIFY, AND THE CENSUS EXISTS TO SETTLE IT.** That SF+'s 29
destroys pass through *our* `IsRequirementMet` at all is an **inference, not a measurement** — the
entry's own sourcing caveat says the decisive trace lines do not exist in any log. If the
`ResearchNodeRemover` lives in an asset the arm pass never armed, this change does **nothing** and the
census will show `armedAssets` > 0 with `foreignSeen=0` while the SF+ `Destroy:` lines still appear.
That is the first thing to check in the log, before judging the feature.

### ~~T1. The Relocate Resource Wells tooltip states the opposite of what the feature does~~ — FIXED 2026-08-08
The settings UI described a relocated well as *"functional but INVISIBLE"* and labelled the
toggle *"(INCOMPLETE - stage H2)"*. True before H2b; false since. Now reads EXPERIMENTAL, states
that a relocated well is dressed and buildable, and names the limits a player can actually hit:
presence-gated move ⇒ possible duplicate (D1); re-roll leaves an already-moved well where it is;
a late-loading satellite is lost from a moved well (T15); untested build-area overlap with nearby
ordinary nodes (T3); unverified desert meshes (T2).

> **THIS FIX TOOK THREE DRAFTS AND EACH ONE FAILED DIFFERENTLY. That is the entry.**
> 1. **Draft 1 under-disclosed.** It listed two limits and its own entry here claimed that was all of
>    them. Both listed were cosmetic or in the player's favour; both omitted were *functional* — a
>    disclosure asymmetry that reads as reassurance. The same review caught that the new
>    `(EXPERIMENTAL)` label contradicted the `EnableExperimentalFeatures` tooltip elsewhere in the
>    same panel. **A grep for the OLD claim cannot find a contradiction the NEW words create** — so
>    after fixing stale copy, re-read the whole panel as a player sees it, not just the diff.
> 2. **Draft 2 over-corrected into a NEW false claim** — it asserted a re-roll limitation that has no
>    code path (see T15), sourced from a review finding that was itself an unverified premise.
>    **Over-correcting a disclosure gap is the same defect class as the gap.** Verify a new limit
>    against the code before writing it, exactly the way you verify a removed one. Draft 2 also
>    asserted a *cause* for a Miner refusing to place — unmeasured, and it displaced a working remedy
>    the same panel already gives for that symptom. Player copy is bound by the
>    no-asserted-cause rule too ([[lessons-log-asserted-a-cause]]), for a sharper reason than a log
>    is: the player acts on the wrong remedy.
> 3. **Draft 3 is the reviewer's wording, applied verbatim.**
>
> **The pattern across all three rounds, which is why the workspace rule exists:** every edit a
> reviewer specified *verbatim* landed clean; **every edit authored in response to a finding
> introduced a defect.** Three for three, in a change that contains no logic at all.

> **The entry itself was wrong, and that is the reusable part.** It listed two sites; there were
> **three** — `NodeShuffleConfig.cpp:171` (UI), `NodeShuffleWellSpawn.cpp:393` (log), and
> `NodeShuffleConfig.h:120` (the header comment, which is where the other two were copied from).
> A stale claim propagates from the comment that justifies it, so **sweep by grepping the claim,
> not by working the list**. The log line's stale NOTE was deleted rather than re-worded: it was
> asserting a design fact, which is not what a diagnostic is for ([[lessons-log-asserted-a-cause]]).

### T12. A gas resource is budgeted a completability floor and then dropped from every deck — it can be erased from the map
**Measured 2026-08-08 with an arithmetic proof, from the user's own logs**
(`_team/nodeshuffle-followups/lithium-extractor-investigation.md` §2). **Not fixed — the fix
is a separate, user-gated packet. Do not fix it as a side effect of anything else.**

The quota builder (`NodeShuffleSubsystem.cpp:1223-1287`, re-derived against this commit's tree
2026-08-08 — earlier revisions of this entry cited parent-commit numbering) floors **every** resource kind at
`MinNodesPerResource` / `MinNodesPerModdedResource` and budgets `TargetActive` to cover every
one of those floors. The deck builder then does this:

```cpp
// NodeShuffleSubsystem.cpp:1352
if (IsGasResourcePath(Kind)) { continue; }   // gas: relocate-only, never enters a deal deck
```

So a gas kind gets a floor **and** a slice of the active budget, and then **zero cards are
ever dealt for it**. The floor buys it nothing; the budget is phantom.

**Arithmetic proof, from two rolls in one day:**

| roll | gas kind in pool? | SolidDeck + LiquidDeck | `active` (TargetActive) | shortfall |
|---|---|---|---|---|
| 16:35:38 | yes (lithium) | 724 + 105 = **829** | **837** | **8** |
| 10:36 | no | 731 + 105 = **836** | **836** | **0** |

The shortfall is exactly `MinNodesPerModdedResource` × (number of gas kinds), and it vanishes
with the gas kind.

**The damage:** with no floor actually protecting it, a gas resource's originals go into the
ordinary `AllowVanillaDisappear` active-set draw like anything else. On the measured profile
(`ActivePercent: 90`, 68 of 630 originals losing the draw) a **single-node** gas resource is
**hidden with no relocated replacement about 11% of rolls — i.e. deleted from the world**. The
observed consequence was AlkaLib's lithium vanishing, taking the Reactive Ore Extractor's
reason to exist with it (`AUTOALLOW … decision=SKIP`).

**Now detectable in one grep**: `ROLLCENSUS ZERO-ACTIVE:` names the resource on the roll it
happens. That is diagnostics, not a fix.

> **Pre-scoped fix options, sized, none applied.** (A) Honour the floor: pre-mark
> deck-excluded (gas) entries `bActive = true` before the draw, up to
> `min(available, Quota[Kind])` — smallest change that makes the floor mean what it says;
> touches the roll's hottest function, so it inherits the full regression pass.
> (B) Stop budgeting a floor the deck cannot deliver: exclude gas from `Quota`/`TargetActive`
> — makes the arithmetic honest but does **not** protect the resource. (C) A + B.
> (D) Treat a relocate-only resource as non-disappearable outright — one condition at
> **`:1296`**, the active-set predicate
> `if (E.bPinned || (!E.bIsNewNode && !Config.AllowVanillaDisappear))`; the smallest correct
> statement of intent, since `AllowVanillaDisappear` is a *shuffle* knob and gas is not
> shuffled. **The investigation's recommendation was D.**
> *(Line numbers in this entry were re-derived against the current tree 2026-08-08. `:1264` —
> cited by an earlier revision — is a `{` inside the quota loop, not a predicate. If they have
> drifted again, find (D) by the predicate text, not the number.)*
> **Explicitly not a fix: changing auto-allow.** When the resource really is absent, `SKIP`
> is the correct answer and must stay.

### T15. A satellite that loads AFTER its well was relocated is hidden at the origin and refused at the destination
**This entry replaces a first draft that was FALSE, and the falsehood is more instructive than the
bug.** The first draft claimed re-rolling makes an already-relocated well vanish from the world
until restart, sourced from H2b-review's "F-3, accepted limitation". A re-review disproved it
statically: `SuppressVanillaWellGroup` has two call sites (`NodeShuffleWellRelocateApply.cpp:473`,
`:505`), both requiring `bGroupPlaced == true`; `bGroupPlaced` is cleared at two sites and **both
are unreachable for a placed group** — `NodeShuffleWellRelocateRoll.cpp:352` sits below the guard at
`:152` (whose comment reads *"a player who has walked to a relocated well should not find it
gone"*), and `NodeShuffleWellEscalate.cpp:235` is reachable only through `TryPlaceWellGroup`, called
solely inside `if (!E.bGroupPlaced)`. **Invariant: a suppressed well origin always has a live,
maintained relocated group.** F-3 was an argument from *absence of a restore path* — true — plus an
unverified premise that a re-roll un-relocates a placed well. No log records the vanish; the test
script's "step 10 EXPECTED TO FAIL" is a prediction that was never run.

**CONFIRMED AT RUNTIME 2026-08-08**, build `2026-08-08-t1t2-1`, on the user's live save: a re-roll was
performed (`ROLLCENSUS: seed=1223222528 reroll=1`), 22 wells placed, and **zero** abandonment events —
the only occurrence of `WELLH2-ABANDON` in the whole log is inside the sentence that *documents* the
grep. The invariant now has runtime backing, not only a static read, so the hedge below is satisfied
for the re-roll case specifically. *(Note the near-miss: a naive `grep -c "re-enrolled by a new roll"`
returns **1** and looks like a violation. The hit is the diagnostic's own explanatory text. Count the
EVENT tag, never the prose that describes it — the same trap as the `(5 of 6 groups…)` constant.)*

**The real, reachable gap it was standing in front of:** a well is enrolled with the satellites
loaded at that moment (`NodeShuffleWellRoll.cpp:176-177`). A satellite that streams in later is
appended to the entry by the merge, then **refused at the destination**
(`NodeShuffleWellSpawn.cpp:201-205`) while still being **hidden at the origin**
(`NodeShuffleWellRelocateApply.cpp:332-335`, which applies no `bCaptured` filter). It has no un-hide
path. The well is permanently smaller — and produces less — than its vanilla counterpart.

Player-facing as of the T1 copy fix, which now states this instead of the false claim.

> **Pre-scoped fix: H2b-review B1, a per-group hidden-piece ledger**, so suppression can be undone
> per piece rather than only per placed group. **Verify the absence before building it** — "there is
> no un-hide path" rests on a reviewer's read, and this project has been burned by *"I could not
> find a check"* being quoted forward as *"there is no check"*. That hedge was in the first draft
> too; the headline violated it anyway, which is why it is repeated here.
>
> **"until you reload the save" is still ASSUMED** — the recovery half has not been observed.

### T16. A RELOCATED well is structurally UNPINNABLE — pin detection resolves through the hidden original
**Found 2026-08-08 by the cold review of the re-roll-guard change, and MEASURED in the author's own
log. This is a PRE-EXISTING defect in the shipped build, not a consequence of that change** (which was
parked; see below). It is filed here because the same broken predicate is what made that change unsafe.

`E.bPinned` is derived in `NodeShuffleWellRoll.cpp:153` from the **vanilla original core**. Relocation
hides that core, disables its collision and deregisters it from the node manager — so a player
**physically cannot build on it**, and the pin can never become true. The actor they *can* build on is
our spawned core, which the `ns-review-h2 F1` filter at `:133` deliberately excludes from the census
map. `:190` then does an unconditional `E.bPinned = bPinned;`, so even a previously-true pin is reset.

**Measured, one roll, from `FactoryGame.log`:**
```
WELLH1-ROLL: skippedOurSpawned=17 -- ... wells H2 RELOCATED ... excluded
WELLH1-ROLL: re-roll complete -- 20 wells (20 managed, 0 pinned/unresolved)
```
17 of 20 wells relocated; **every one reports `managed=1 pinned=0`.**

**`ApplyWellRetype`'s "live pin re-check" (`NodeShuffleWellRetype.cpp:152, 202`) has the IDENTICAL
defect** — it also resolves via `FindOriginalBaseByPath`.

> ⚠ **CORRECTION 2026-08-08 — THIS ENTRY'S STATED CONSEQUENCE WAS WRONG.** It said a re-roll could
> "retype a relocated well the player has built on … their chlorine setup silently becomes water." The
> T16 cold review found that **the retype cannot reach the spawned actors at all** (see T17): the write
> goes through `FindOriginalBaseByPath`, i.e. the *hidden originals*, so nothing player-visible is
> retyped. The pin defect is real and worth fixing — the *harm* I attributed to it is not the harm it
> causes. Written into this file as fact after one code read; corrected after a review measured it.
> **FIXED 2026-08-08** by `EvaluateWellPin()` (`NodeShuffleWellRetype.h`), one shared predicate for both
> consumers, resolving against the actors a player can actually build on.

> **Pre-scoped fix: repair the pin AT ITS SOURCE so both consumers inherit it** — resolve occupancy
> against the actor the player can actually build on (our spawned core/satellites) when the entry is
> relocated, falling back to the original only when it is not. **Do not patch it at one call site:**
> this file already records two copies of one rule drifting apart, and there are two consumers here
> (`WellRoll` and `WellRetype`) that must not diverge again.

### T17. A relocated well's SPAWNED actors are never re-typed — the world and the layout disagree
**Found 2026-08-08 by the T16 cold review. Code-read and log-corroborated, NOT observed — do not quote
it as measured.** This is the defect T16's first draft mistook itself for.

`NodeShuffleWellSpawn.cpp:169` and `:282` write `mResourceClassOverride` **only inside the fresh-spawn
branches**. The **reuse** and **adopt-late** paths never re-write it. `ApplyWellRetype` resolves through
`FindOriginalBaseByPath` — the *hidden originals* — so it cannot reach a spawned actor either.

**Confirmed statically and decisively 2026-08-08.** `ApplyWellRetype` provably cannot cover for it: it
resolves through `FindOriginalBaseByPath` → `VanillaNodeCache`, which **skips our own nodes by
construction** (`NodeShuffleSubsystem.cpp:2126`). So no path ever moves an existing spawned well actor
onto a re-dealt resource. Log confirms this is the *normal* state, not an edge case: all 15 relocated
groups were **adopted** (`:37147`), and only 4 `WELLH2-SPAWN` lines exist in 111,496 — both "spawned"
ones are newly-placed wells. No relocated group ever re-spawns.

> ⚠ **RETRACTED, and this is the third time today the same mistake was made in this file.** The first
> draft cited *"16 actually changed resource"* against 15 adopted groups as evidence the world had
> diverged from the layout. **It is not evidence of that.** That counter is
> `Assigned != ORIGINAL` (`NodeShuffleWellRoll.cpp:359`) — not `!= previously-assigned`. And in that
> log the assignment never moved at all: the pre-roll apply at 00:07:19 already shows the values the
> 00:07:55 roll produced, because **both rolls shared seed `1223222528`**.
>
> So the **defect is proven; its consequence is UNOBSERVED.** A relocated well's resource cannot change
> — but nothing has yet been seen changing and failing to take effect, because no roll in evidence
> re-dealt one. Same [[lessons-log-asserted-a-cause]] shape as the two retractions above it: a counter
> was read as answering a question it does not ask. **Do not re-state the consequence as measured until
> a roll with a DIFFERENT seed re-deals a relocated well and the world is checked against it.**

**Why it matters beyond correctness:** every conservation guarantee this mod advertises is asserted
against the *layout*, and the layout is not what the player's wells produce. It also means T16's pin fix
is necessary but not sufficient — pinning now protects the right actors, but a *non*-pinned relocated
well still will not actually change resource on a re-roll.

> **Not fixed here, deliberately** — a scoped pin fix is not the place for it. The likely shape is to
> re-write `mResourceClassOverride` on the reuse/adopt paths too, but that touches the spawn lifecycle
> and owes its own packet and its own review. **First runtime step is cheap:** on a well you have
> pressurized, compare `The core the pin was READ FROM ('…') now holds 'Y'` against that well's earlier
> `-> 'Z'`. **Y ≠ Z confirms it live.**

### ~~T20~~ → **D3. ACCEPTED: nodes may sit inside a well's footprint. Author's call, do not "fix" it.**
**Decided 2026-08-08 by the mod author, after taking the measurement this entry asked for: ~6 active
nodes sit around the well in question, and both the Miner and the well work.**

> *"I don't think the solid near the well is a bad thing since the miner and wells both work."*

**Do not open a packet to make the clearance symmetric.** Inverting the roll order so node placement can
see well destinations is a real change to the hottest path in the mod, and it would buy an aesthetic
property the author does not want. The asymmetry stays.

**The ONE thing that would reopen this** — and it is a *satellite*-level question the core-distance
measurement above does not answer: **a Well Extractor refused because a Miner got to that spot first.**
The 45.9 m figure is to the well's CORE; satellites sit further out, so a node can be much closer to a
satellite than to the core. If a Well Extractor is ever refused next to a Miner, this entry becomes a
defect again and option (A) below is the pre-scoped fix.

*Mechanism retained below, because the asymmetry is real and the next person to find it should read the
decision rather than re-derive the bug.*

### ~~T20 (mechanism, retained)~~. Well-vs-node clearance is ONE-WAY
**Found in game 2026-08-08 by the author, from a screenshot, on build `2026-08-08-t17-t7b-1`.**
A solid Sulfur node appeared beside a relocated chlorine well and took a Miner. Measured from the log:
the node sits **45.9 m** from `BaseNode_FrackingCore_2`'s core.

**The clearance a well respects is 90 m** — `WellSatMaxRadiusCm (6500) + WellMinNodeSpacingCm (2500)`
(ns-t27-review 3 merged the roll's file-local `WellMaxBoundRadiusCm` into that header constant),
tested against every active layout node in `NodeShuffleWellRelocateRoll.cpp:644-652` (the
`for (const FNodeShuffleEntry& Node : Layout)` loop; the `Node.Location, Cand` test is line 647 —
re-measured 2026-08-09 by ns-t27-truth. The previous citation here, `641-651`, was applied verbatim
from a review that had it wrong at both ends: 641 closes the *previous* well-spacing loop). So the well would
never have chosen that spot. **The node did, because node placement does not know wells exist:**
`WellDestinations` occurs **0 times** in `NodeShuffleSubsystem.cpp`, which is where ordinary node
locations are generated.

**The relationship is therefore asymmetric by construction:** wells avoid nodes; nodes do not avoid
wells. Across rolls — and now more often, with `RerollRelocatedWells` moving wells — a node dealt in a
later roll can land inside a well placed in an earlier one, and nothing re-checks.

**Same shape as three other defects in this file** (T16's two consumers of one pin, the `ns-review-h2 F1`
census filter, T8's `AFGResourceNode*` typing): **one rule, applied on one side of a relationship only.**
That is now this project's most-repeated defect class, ahead of even the zero-without-a-denominator one.

**Not observed to break anything.** The Miner placed and works. The plausible harms are unmeasured: a
node overlapping a satellite's snap box (T3's geometry, from the other direction), a Well Extractor
refused because a Miner got there first, or simply a solid node standing in the middle of a fracking
cluster.

> **Pre-scoped fix, and pick deliberately:** (A) give node generation the same 90 m test against
> `WellDestinations` — symmetric, but it needs the well destinations to exist *before* node placement,
> which inverts the current roll order; (B) test it at the *later* of the two whenever a re-roll moves
> either — cheaper, but leaves pre-existing saves unfixed; (C) accept and document, like D1.
> **Measure before choosing:** count how many active nodes currently sit within 90 m of a placed well
> core. If it is a handful, (C) is defensible; if it is dozens, it is a placement bug.

### ~~T21. Well relocation is presence-gated only because we never baked SURFACE terrain~~ — **CLOSED 2026-08-08. THE PREMISE IS FALSE. Do not reopen without reading why.**

**Sized on request, and it never got as far as size.** Full working: `_team/nodeshuffle-followups/T21-surface-bake-sizing.md`.

**`ValidateWellMemberSpot` (`NodeShuffleWellFootprint.cpp:48-127`) applies FIVE gates. Three are terrain
and are bakeable. TWO are decided against LIVE ACTOR POPULATIONS and can never be:**

| gate | how it decides | bakeable? |
|---|---|---|
| void / no terrain | ground trace | yes |
| water | water-body test | yes |
| cliff / slope | 4-probe normal ring | yes |
| **node overlap, 800 cm** | `TActorIterator<AFGResourceNode>` (`:76`) | **NO** |
| **buildable overlap, 600 cm** | live physics overlap (`:112`) | **NO** |

The node-overlap gate reads **exactly T14's population** — the nodes other mods `SpawnActor` after boot,
which T14 measured at 28 and about which it concluded a bake *"loses exactly this population"*. So T21's
own argument — *"terrain can be baked precisely because nothing spawns it"* — is **true of three gates and
false of two, inside one function**. A bake is a **partial oracle**, and a partial oracle cannot license
hiding the original at roll time. The spawn-time backstop stays; **D1 is unchanged**.

**The sharpest reason, and the one to quote if this is ever revived:** the ground trace is `ECC_WorldStatic`
and **accepts buildable hits by design** (`NodeShuffleSubsystem.cpp:5971-5975`, *"nodes on foundations are
allowed"*). So wherever a player has built, **baked vanilla terrain is not even the Z the game settles to** —
the bake would be wrong precisely where the world is most developed.

**Sizing, recorded so nobody re-derives it.** Resolution is driven to **≤350 cm** by `SmoothNormalRingCm`
(`NodeShuffleSubsystem.cpp:119`) — the radius of the 4-probe ring the cliff normal is smoothed over; a
coarser grid cannot represent the sampling pattern at all. 175 cm to be Nyquist-faithful; **≈24 cm vertical**
to match the 60° cliff verdict within 1°. That is **4.6× finer than the cave atlas's 800 cm**. Over the
measured 720,000 × 630,000 cm probed extent: **3.70 M cells** — **170 MB** in the shipped string-literal
encoding (46 B/cell, measured), **~20 MB** as a packed int16 raster. **The entire deployed mod DLL is
1,666 KB.** The cave atlas is 282,278 B = **16.7% of the DLL for 2,660 cells**, and it is **sparse** (1.8%
fill); a surface atlas must be dense, so that encoding does not transfer.

> **TWO CORRECTIONS TO THIS ENTRY'S ORIGINAL TEXT, both from inferring rather than measuring.**
> 1. **"6656 cells" was never the shipped bake.** The DLL holds **2,660**. 6,656 is the author's *merged
>    runtime* store (baked + locally learned), so **~60% of the figure quoted as evidence exists only on
>    one machine**.
> 2. **`Scripts/bake_maps.ps1` is a SNAPSHOTTER, not a generator** — it re-emits JSON the mod learned
>    while playing. The real generator is `ExpandCaveFloorsBudgeted`: seeded 4-connected flood fill,
>    **player-proximity gated at 30 km**, 120 traces/pass, capped at 25,000 cells. A surface variant is a
>    **new offline tool with no precedent here** (~18.5 M traces, needing a headless commandlet against the
>    cooked CSS map that may not be runnable at all).
>
> Both came from reading a log line and a filename and inferring the rest — the same *observe a value,
> infer its meaning, state the inference as evidence* shape this file keeps recording.

**STILL UNMEASURED, and it would close this twice over:** the rejection-reason distribution. There are
**0 `WELLH2` lines across 15 MB of logs** — the feature post-dates every log in the repo. One diagnostics-on
roll counting `WELLH2-SEARCH … REJECTED … (<reason>)` by category would say how much of the problem is even
terrain-shaped. **Also never measured: how often D1 actually fires.** If it has never been observed, T21's
motivation was hypothetical from the start.

> **THE ALTERNATIVE THAT ACTUALLY SERVES THE STATED GOAL ("wells shuffle immediately"), and needs no oracle.**
> The delay was never missing terrain data — it is that the **destination** is far from the player
> (`NodeShuffleWellRelocateApply.cpp:441`) while the **origin** is provably resident. **Bias the 24-draw deal
> loop to prefer candidates already within `SpawnRadiusCm` of a live pawn, falling back to uniform.** The well
> then validates and spawns on the next apply pass. No DLL growth, no new tooling, every safety property
> intact. **Trade-off is the author's call, not an engineering one: it biases spatial distribution toward
> wherever the player stood at roll time, and randomisation is the product.** Put to the author 2026-08-08.
>
> **Seam flagged, graded `assumed`, not chased:** hiding the original at roll time would also have to re-home
> `CaptureWellGroupVisuals`, which today runs inside `SuppressVanillaWellGroup`
> (`NodeShuffleWellRelocateApply.cpp:284-285`) precisely because that is the one site provably reached with
> the vanilla member still standing. Read at the call site only, not across every consumer.

*Original entry retained below for the history.*

### T21 (original entry, premise since falsified). Well relocation is presence-gated only because we never baked SURFACE terrain — and we already bake CAVE terrain
**Raised by the mod author 2026-08-08, and it dissolves an assumption three explanations in this file
were built on.** Their question: *"If a solid node can properly land on terrain, and we check the
terrain is the right type, why are we not able to check that for the wells? The terrain is static."*

**First, a correction this entry exists to make.** Solid nodes do **not** validate terrain at roll time
either. They settle through the same trace, which returns the same failure — *"no terrain / out of range
(true void)"* (`NodeShuffleSubsystem.cpp:5904`). The solid/well asymmetry was never *"we check for one
and not the other"*; it is only **failure tolerance**: a solid that cannot settle is one node, and nodes
are *allowed* to disappear (`ActivePercent`), so nothing needs a fallback. A well that cannot place must
fall back to its original, so the original must still exist — which is why it cannot be hidden on the
roll. That is the real reason, and earlier answers in this file gave weaker ones.

> ## ⚠ THE `ActivePercent` CLAUSE ABOVE IS FALSIFIED — measured 2026-08-08, see **T22**.
> A solid that cannot settle on terrain is **deferred and retried forever**
> (`NodeShuffleSubsystem.cpp:3972-4005`), **not dropped**. `ActivePercent` (`:1238`) and
> `AllowVanillaDisappear` (`:1243-1246`) are **roll-time budget knobs** deciding how many pool slots start
> `bActive`; they are **never consulted on a placement failure**. The only executable drop is the *overlap*
> path after 8 failed visits (`:4140`).
>
> **The real asymmetry is in the HIDING, not the checking, and it is the opposite of tolerance.**
> `SuppressOriginalNodes` (`:4497`) hides a solid's original unconditionally, on a loop that never reads
> whether the replacement spawned, with its proximity gate deliberately removed (`:4509-4510`).
> `SuppressVanillaWellGroup` has exactly two call sites, **both after a complete group spawn succeeded**.
> **Solids do not tolerate failure — they do not DETECT it**, which is T22.
>
> Two further measurements that sharpen this entry rather than change its conclusion: **a solid applies SIX
> gates and a well applies five of the same six** (the well lacks the enclosure test) — so **zero of the
> well's gates lack an ordinary-node counterpart**, and the asymmetry is not in the gate set in either
> direction. And the well's real difficulty is **footprint**: ~11 actors across up to 65 m must fit
> *simultaneously*, searched over 36 yaws × 8 nudges × 3 redeals, all-or-nothing. **Footprint is why
> placement fails often; the fail-safe is why failure cannot be pre-committed.**
>
> *The paragraph's headline claim — that solids do not validate terrain at roll time either — is CONFIRMED.
> Both paths call the same `RaycastGroundAt`, and both spawns are gated by the same
> `IsLocationNearAnyPlayer` at the same radius. Only its stated mechanism was wrong.*

**The author's lever is the right one.** The blocker is not that terrain changes — it is that the only
way we ask about terrain is a physics trace, and a trace needs the landscape **resident**. Static
geometry we cannot query is still unqueryable.

**But this mod already solves that exact problem for caves.** `NodeShuffleBakedData::CaveFloorsChunks`
is a **baked cave-floor atlas embedded in the DLL**, merged at load by `EnsureCaveStoreLoaded` and usable
with **no streaming** (6656 cells on the author's save). The technique — *terrain is static, so bake it
once and query the bake* — is shipped, working, and pointed at caves only.

> **Design option, not scheduled: bake a SURFACE height atlas and validate well footprints at roll time.**
> Then a well's destination is *committed* like a solid node's, the original can be hidden immediately,
> and the all-or-nothing fallback stops needing the original to survive — because failure is known
> before the deal, not hours later when a player flies there.
> **Costs and caveats, stated honestly:** an atlas at enough resolution to judge a footprint up to ~65 m
> across (`WellMaxBoundRadiusCm`) is larger than the cave one; it must be generated offline in-editor;
> and it can only describe **vanilla** terrain. That last point is fine — terrain is static — but note
> the contrast with **T14**, where a bake is strictly *worse* than a live iterator because other mods
> spawn nodes at runtime. **Terrain can be baked precisely because nothing spawns it.**
>
> **Do not start this without sizing the atlas first.** The question that decides it is resolution: what
> cell size is needed to reject a footprint that a yaw search would have rejected? Measure against the
> existing placement code's own tolerances before writing anything.

### T18. A pin found on a relocated well is never RECORDED — `bAlreadyApplied` measures the wrong actor
**Found 2026-08-08 by the T17 cold review. Pre-existing T16 defect that T17 makes consequential.**
Diagnostics for it shipped with T17; the fix did not.

`NodeShuffleWellRetype.cpp:230` computes `bAlreadyApplied` from the **hidden original**, while the pin
one line above resolves the **spawned** core (T16). Consequence: apply pass 1 writes the new resource to
the originals; from pass 2 a core-only pin is suppressed by `bAlreadyApplied == true`. So **a well with a
Pressurizer on it stays recorded `bManaged=1 bPinned=0` all session** while the world permanently refuses
the assignment — the layout keeps claiming an assignment the player's well will never hold.

**Now visible in one grep**, shipped with T17: `WELLH2-RETYPE-PIN … LAYOUT BOOKKEEPING AT THIS INSTANT:
bManaged=%d bPinned=%d`. **`bPinned=0` on that line is the defect firing.** That is diagnostics, not a fix.

> **Pre-scoped fix (review's Option B), one line** — measure "already applied" on the actor the pin was
> resolved against:
> ```cpp
> const AFGResourceNodeFrackingCore* AppliedOn = Pin.ResolvedCore ? Pin.ResolvedCore : Core;
> const bool bAlreadyApplied = (AppliedOn->mResourceClassOverride.Get() == ResourceClass);
> ```
> **It is a mode-selection-predicate change to code that landed hours earlier, so it owes a full review**,
> not a one-line drive-by. This project's own record: *"a mode-selection predicate let one stray pixel
> fail a whole image"* is one of the three structural fixes that each introduced a fresh bug.
>
> **The review's stronger rival, and the better long-term target:** extend `ApplyWellRetype` to resolve
> **spawned actors first**. That dissolves this entry entirely and closes the proximity gate, at the cost
> of writing two populations per well. Its own packet.

### ~~T19. The well acceptance gate cannot fail for a resource mismatch~~ — **FIXED 2026-08-08** (`32e3f38` + `ead59f9`, marker `2026-08-08-t19-2`)

The gate now prints the layout's assignment (`res=`, meaning unchanged so committed review reports that
grep it still resolve) **and** what the spawned core holds (`worldHolds=`), and `bHealthy` gates on their
agreement **over the core AND every live spawned satellite** — a satellite left on the old resource is
exactly as wrong as a core left on it, and exactly as invisible. A **pin-explained** disagreement is not a
fault (T17 deliberately declines to retype an in-use group) and gets its own non-alarming token.
**Reporting-only: `bHealthy` gates no behaviour** — re-derived independently three times.

**F1, and read this before believing a `*** RESOURCE MISMATCH ***`:** the T17 retype is **proximity-gated**
(`NodeShuffleWellRelocateApply.cpp:501`) while the audit sweep walks **every** placed group. On the
`RerollRelocatedWells=OFF` default — what most users run — a shuffle re-deals every placed well and only the
1–2 nearby ones are retyped, so an unguarded gate would fire **~15 warnings every 5 minutes on a CORRECT
build**. Hence `retypeReachable=`. **Fifth sighting of one-rule-one-side** (T16, `ns-review-h2 F1`, T8, T20).

> ## ⚠ FOUR PROSE ITERATIONS OF ONE LOG LINE. THREE SHIPPED A FALSE CLAIM. That is the entry.
> 1. The cold review's **own F1 text** glossed `retypeReachable=0` as *"has not been VISITED since its
>    assignment changed"* — the predicate tests presence **now**, not history.
> 2. Its replacement asserted *"NO PLAYER IS WITHIN THAT RADIUS"* **unconditionally**, on a line that fires
>    independent of that flag, so a `retypeReachable=1` line contradicted its own `%d`.
> 3. The third still said *"the retype demonstrably reached and failed to fix"* — a **cause asserted from a
>    proximity boolean**. Provably wrong: the `just-placed` audit (`:486`) runs in the `!bGroupPlaced` branch
>    and never reaches the retype at `:501`, yet prints `retypeReachable=1`. And it **contained the literal
>    token `retypeReachable=1`**, so the grep its own commit message recommended matched **every** mismatch
>    line — the `provableOverlap=0` legend trap, second occurrence in this file family.
>
> **THE RISK CLASS IS NOT WHO TYPED IT — IT IS WHETHER THE AUTHOR WAS UNDER PRESSURE TO CLOSE A FINDING.**
> All three were authored in response to a finding, by three different agents, and the orchestrator waved
> #2 through reasoning *"an agent specified it verbatim, so it is low-risk."* That is not what the rule
> means. Ask for verbatim text to avoid **re-derivation** — then still **verify it against the predicate it
> describes**. The genuinely safe material is text that predates the finding.
>
> Also caught late, by the scoped review only: the `*** SHORT OR UNLINKED ***` arm had **no counter**, so
> `12 not OK … 0, 0, 0, 0, 0` was printable — five reassuring zeros beside a dozen wells the Pressurizer
> will under-report. And `%d fully linked` had quietly become **a label that lies**, since `bHealthy` now
> also requires not-scattered / not-short / no-mismatch.

### ~~T26. A WELL footprint has NO enclosure gate — an ordinary node has one.~~ — **FIXED (built, COLD-REVIEWED, UNTESTED IN GAME) 2026-08-09, marker `2026-08-09-t27-3`, packets ns-t27-corefirst + ns-t27-fixes + ns-t27-perf**

> **THE SHIPPED MARKER IS `-t27-3`, AND IT WAS NOT COLD-REVIEWED WHEN IT WAS BUILT.** This heading and
> the paragraph below originally named `-t27-2` and its review, which is one packet short of what is
> deployed: `-t27-3` additionally contains `ns-t27-fixes` (second review round,
> `_team/nodeshuffle-followups/T27-fixes-review.md`) **and `ns-t27-perf`, the node-scan hoist, which
> was UNREVIEWED at the moment that binary was built and deployed (16:35, 2026-08-09).** It became
> reviewed later the same day, by `_team/nodeshuffle-followups/T27-perf-review-2.md` (SHIP WITH TESTS,
> 13 findings + an addendum) — a review that lands **after** the build, not before it. Corrected here
> 2026-08-09 by ns-t27-truth; do not read the earlier wording as evidence that the shipped build was
> reviewed before it shipped, because it was not.

> **STATUS IS "BUILT", NOT "CLOSED".** The build is green and the import table is unchanged
> (817/767/0, zero symbols moved). A cold review has now run (SHIP WITH TESTS, 13 findings —
> `_team/nodeshuffle-followups/T27-review.md`) and its fixes are applied in marker
> `2026-08-09-t27-2`, but **nobody has stood at a relocated core in game.** Do not mark this closed
> on the strength of a compile plus a review; the review's own verdict is that its two decisive
> assumptions (F2, the enclosure predicate against the reported symptom; and parity row 14, Pressurizer /
> Extractor clearance at 2076 cm) are unreachable by any amount of static review. (`F14` was written
> here first and is wrong: the review has findings F1-F13 only, and 14 is a row of its PARITY TABLE at
> `T27-review.md:565`. A reader would have hunted a finding that does not exist.)
>
> **What landed.** The enclosure lambda that lived inside `EnsureNewNodeSpawned` is now
> `ANodeShuffleSubsystem::IsSpotEnclosed` (defined in `NodeShuffleWellFootprint.cpp`), and BOTH paths
> call it — the node path's lambda delegates rather than keeping a second copy, so the two cannot
> drift. `ValidateWellMemberSpot` gained a **required, undefaulted** `bApplyEnclosureGate` parameter;
> the core passes a literal `true`, satellites pass the named constant
> `WellEnclosureGateOnSatellites` (**compiled `true` as of marker `2026-08-09-t27-2`** — it shipped
> `false` in `-t27-1`; the cold review's F3, reinforced by its §9.2 under the author's 2026-08-09
> ruling, flipped it, because a satellite you can see but cannot build a Well Extractor on is the
> same permanent, log-invisible shrink T15 names, and under independent retry a rejection costs one
> of 24 draws). The undefaulted parameter is deliberate: a
> defaulted one would let a future call site opt out by saying nothing, which is exactly how this
> gap survived in the first place.
>
> **The false parity comment is gone**, replaced by a conditional claim that lists the six gates and
> names the condition on the sixth. `WELLH2-PROBECENSUS` gained an `enclosed` counter on both sides,
> and its prose warns that a zero in the SATELLITE enclosure bucket may mean the gate is off rather
> than that nothing was enclosed — read the constant, not the zero.
>
> **The measurement this entry asked for was never taken** (`NodeShuffle.Here` at the 2026-08-09
> Water core). The author's in-game report — flew there, circled a rock column, could not reach it —
> was taken as sufficient grounds by directive. So the *rate* at which the new gate rejects
> destinations is still UNMEASURED; the first log from this build is what measures it.
>
> This entry is retained in full below because its gate-by-gate source citation is still the record
> of what the two paths did, and because the "measure the rejection rate before choosing" advice is
> the advice for the SATELLITE half, which is still an open toggle.

### T26 (original entry, retained). A WELL footprint has NO enclosure gate — an ordinary node has one. A well can validate inside a slot or crevice.
**Found 2026-08-09 by the author asking, in game, *"I'm pinging a water 48m away — is this inside a cliff?"*
Measured from source, not inferred.**

An ordinary node's placement applies **six** gates (`NodeShuffleSubsystem.cpp`, `EnsureNewNodeSpawned`):
void `:3958-4006` · water `:3960-3971` · cliff `:5931-5936` · node-overlap 800 cm `:4024-4044` ·
buildable 600 cm `:4050-4069` · **`IsEnclosed` `:4078-4093`** — 8 horizontal rays at 500 cm from
`Z+200`, reject when **7 of 8 are blocked** — applied at `:4095` *and re-applied to every nudge target*
at `:4123`.

`ValidateWellMemberSpot` (`NodeShuffleWellFootprint.cpp:58-126`) applies **the same first five and stops**.
**There is no enclosure test on the well path.**

> ⚠ **AND THE FILE SAYS OTHERWISE.** `NodeShuffleWellFootprint.cpp:42-43` claims the gates are
> *"Deliberately the SAME set of gates the ordinary node spawn applies, in the same order."* **That
> comment is false**, and the mod's own instrumentation agrees with the code rather than the comment —
> `NodeShuffleWellStage0.h:46-55` enumerates exactly five (`Gate_Void … Gate_Buildable`, no enclosure
> member). A false comment asserting parity is how the gap survived: anyone auditing the well path
> against the node path reads that line and stops.

**Consequence, measured:** a well footprint can validate at the bottom of a narrow vertical slot, in a
crevice, or hard against a cliff face — geometry where an ordinary node is refused. The ground trace
takes the *first* blocking hit from `StartZ+20000` down, so a spot open ABOVE but blocked horizontally
passes every gate the well path has. The cliff gate does not help: **a flat cave floor, a flat ledge
under an overhang and the flat bottom of a slot all pass a 60° slope test perfectly.**

**Seventh sighting of this project's most-repeated defect class: one rule applied to one side of a
relationship.** (T16 · `ns-review-h2 F1` · T8 · T20 · T24's per-member occupancy · T24's four hide sites
· this.)

**Not observed to have produced an unreachable well yet.** The 2026-08-09 case that raised it is
*unresolved*: the log can rule out a low roof but cannot distinguish an open-topped slot from open
terrain, because **the mod records no terrain identity at settle time** — no hit actor, material or
component. Its 8 satellites spanned 20.7 m of relief over ~78 × 62 m and all settled dry and off-cliff,
which reads as open ground, but says nothing about the core's own 5 m ring.

> **Cheapest measurement, and it needs no build:** stand at the well core and run `NodeShuffle.Here`.
> `roofAbovePlayer` is an upward `ECC_WorldStatic` trace (`NodeShuffleSubsystem.cpp:6537-6540`) — `1` means
> under rock, `0` means open sky — and the same line prints the local slope, which no other log line
> carries. **Do this before writing any code.**
>
> **Pre-scoped fix, if the measurement justifies it:** give `ValidateWellMemberSpot` the same `IsEnclosed`
> test, applied per member. **Decide deliberately whether it runs on every member or only the core** — a
> satellite tucked against a rock face is far less harmful than a core that cannot take a Pressurizer, and
> requiring 9 members to each pass an 8-ray test will reject more destinations at a time when only 7 of 17
> wells are placing. **Measure the rejection rate before choosing** — Stage 0's `WELLH2-PROBECENSUS`
> already carries a per-gate breakdown and would show the cost immediately.
> **And fix the false parity comment in the same change**, whichever way the gate decision goes.

### T22. A solid node dealt into true void has its ORIGINAL hidden and its REPLACEMENT never spawns — and nothing counts it
**Found 2026-08-08 while answering *"we can do solid nodes, why can't we do wells"* — measured, not
inferred. This is the same shape as T19: a failure with no detector.**

`SuppressOriginalNodes` (`NodeShuffleSubsystem.cpp:4497`) hides **every** recorded original as soon as it
resolves and is unoccupied (`:4612-4660`). That loop reads **nothing** about whether the replacement ever
spawned — not `SpawnedNodes`, not `bActive`, not `bRayCasted` — and the proximity gate was **deliberately
removed** from it (`:4509-4510`). Meanwhile a replacement that cannot settle on terrain is **deferred and
retried forever** (`:3972-4005`), never dropped.

So a node dealt into true void is **hidden at its origin and absent at its destination, indefinitely** —
deleted from the world — and **no counter anywhere reports it.**

> **This corrects the standing explanation.** It was believed solids "tolerate" placement failure because
> nodes are *allowed* to disappear via `ActivePercent`. **Falsified:** `ActivePercent` (`:1238`) and
> `AllowVanillaDisappear` (`:1243-1246`) are **roll-time budget knobs** that decide how many pool slots
> start `bActive`; they are **never consulted on a placement failure**. The only executable drop is the
> *overlap* path after 8 failed visits (`:4140`). **Solids do not tolerate failure — they do not DETECT
> it.** The loss is real and unbudgeted, on top of whatever `ActivePercent` intended.
>
> **Unmeasured, and cheap to measure:** how often this fires. The instrument does not exist — count records
> that are `hidden && !spawned` for N consecutive passes and name them. **Instrument before fixing**
> (T4's doctrine); the fix shape is either to gate suppression on the replacement existing, as the well
> path already does, or to return an unplaceable entry to the pool. Do not assume it is common: a deal box
> that lands in true void may be rare, and **a zero here needs a denominator** like every other counter in
> this file.

### ~~T19 (original entry)~~. The well acceptance gate cannot fail for a resource mismatch
**Found 2026-08-08 by the T17 cold review, and it is why T17 went unnoticed.**
`NodeShuffleWellAudit.cpp:123-127` prints `res=` **from the layout**, while `:53` already holds the
spawned core. `bHealthy` is computed from counts, positions and links and **never from resource** — so
the acceptance gate printed `-- OK` throughout the entire pre-T17 defect and would do so again.

This is the P3 class in its purest form: not a gate that *did not* fail, but one **structurally unable
to** for this defect class, while reading as evidence that the well is well.

> **Pre-scoped fix:** print `res=` from the spawned core (the audit already holds it) and add resource
> agreement to `bHealthy`. Cheap; needs one decision first — whether a resource mismatch should make a
> group *unhealthy* (blocking suppression) or merely be reported, since blocking suppression on it
> changes what the gate gates.

### SHIPPED (DEFAULT OFF) — re-roll geography, `RerollRelocatedWells`, 2026-08-08
**Author's decision: a well should re-roll like any other node** — unpinned re-rolls its geography, pinned
never moves. **Took two attempts and two rejections.** The toggle defaults **OFF**; with it off the
pre-T7b path runs unchanged, which is what protects an existing save from churning ~17 wells on one
keypress.

**Attempt 1** (just deleting the guard) was rejected: the safety property it claimed was false because a
relocated well was structurally unpinnable (**T16**, fixed separately), and despawn ran *before* the deal
so a failed deal left the well **nowhere**. Parked at `stash@{0}`.

**Attempt 2** restructured `RollWellRelocation` into three phases with a file-local pending capture, and
the full review verified — statement by statement — that **no layout field is written on any success
path before a destination exists**. The orphan blocker is closed and graded *provably provided*. It also
**upheld the packet's disagreement with the previous reviewer** on destination self-exclusion: two wells
cannot be dealt the same site, and no well can be dealt the site of one that then fails to move.

**Attempt 2 was still rejected**, for three things worth remembering:
1. **The same bug through a different door.** Gate 1 treated **0 resolved handles as a PASS**. Not an
   independent layer: T16's pin falls back to the hidden vanilla actors, that verdict is deliberately
   non-decisive so `bPinned` keeps its *saved* value, `DespawnWellGroup` then finds nothing, returns
   all-clear and **prints nothing** (its log is gated on having destroyed or refused something). Phase 1
   proves the *origin* streamed; nothing proved the *destination* did. **Fixed: the gate now fails
   closed, with its own counter and line.**
2. **"Byte-for-byte with the toggle OFF" was FALSE.** `WellDestinations` was built from pre-capture
   state, so a candidate's stale destination blocked every other well *with the feature disabled*.
   Fixed.
3. **The tooltip claimed a well is "removed and rebuilt at its new spot within a single frame."** False —
   spawns defer behind `IsLocationNearAnyPlayer`, so one keypress makes every relocated well vanish until
   visited. **Third false claim shipped into this one config panel in a single day.**

> **Lesson worth more than the feature.** The teardown-cost line reported `Σ(1 + Satellites.Num())` from
> the **layout**, not `DespawnWellGroup`'s real destroyed count — a derived upper bound presented as a
> measurement. In blocker 1's own scenario it would have printed *"136 destroys, 0.4 ms"* while
> destroying **nothing**: the single number that would have exposed the bug, structurally unable to.
> [[lessons-zero-needs-a-denominator]] has a sibling: **a derived figure is not a measurement, and the
> more precise it looks the more it is trusted.**

### T27 REGRESSION — `NodeShuffleWellRelocateApply.cpp` is 1201 lines (was 892 before ns-t27-corefirst, 1171 after ns-t27-perf)
**Not introduced by T27, but made worse by it, and stated rather than left for someone to notice.**
*(This figure has now been wrong twice: it was written as 1056 — true after `ns-t27-corefirst` — and
not re-measured when `ns-t27-fixes` added ~56 lines of comment, then not re-measured again when
`ns-t27-perf` added the cost-fix commentary. Now **1201**: `ns-t27-truth` added ~30 lines of comment
(the estimate/measured re-labelling and the two coupling TODOs), and re-ran `wc -l` rather than
leaving the figure for the next reader to find wrong a fourth time. A debt entry whose entire content is a file-size figure
is the worst possible place for [[stale figure drift]]. **Re-run `wc -l` before editing this line.**)*
*(**And take the figure from `wc -l`, NOT from `tools/arity.py`.** ns-t27-perf published this same file
as **1172** in its handoff (from arity.py) and **1171** here (from `wc -l`, and correct) — one file, one
packet, two numbers. Cause, measured 2026-08-09 by ns-t27-truth: `arity.py`'s `total_lines =
raw.count(chr(10)) + 1` counts one line too many for any file ending in a newline, i.e. every file here.
Confirmed on two other files by the cold review. That off-by-one is DEFERRED, not fixed — see the
KNOWN-BROKEN banner at the top of `tools/arity.py`.)*
The file breached the 500-line rule before this packet (892 lines) and the core-first/independent-
satellite rewrite added ~164 more: the per-satellite draw loop, two layout gates, and the reasoning
for why sibling clearance became ours. No split was attempted — a split during a placement-semantics
rewrite would have made the diff unreviewable, which is the opposite of what a packet handing work to
a cold reviewer should do.

The seam is the same one `NodeShuffleWellFootprint.cpp` was split along and is already obvious: the
**satellite draw** (draw a candidate, run the two layout gates, log it) knows nothing about attempts,
cursors, budgets, escalation or the commit. It is a `TryDrawSatelliteSpot`-shaped function and would
take ~180 lines with it. `SuppressVanillaWellGroup` is a second, cleaner cut — it already carries its
own banner comment and shares nothing with the search but the entry type.

**Do this as its own packet, after T27 has been reviewed and tested in game.** Splitting unreviewed
code moves the review target while the reviewer is reading it.

### T27-PERF — the superset proof is unenforced: the node-scan scope and the satellite DRAW radius are coupled only by convention
**Opened 2026-08-09 by ns-t27-truth, deferred from it (that packet was comment-only). Source:
`_team/nodeshuffle-followups/T27-perf-review-2.md` F2 — which carries the verbatim fix.**

`BuildWellNodeScanCache` (`NodeShuffleWellFootprint.cpp`) scopes its one-per-call node scan to
`WellSatMaxRadiusCm + WellOverlapRejectRadiusCm`. The node-overlap gate is correct **only while every
satellite probe stays inside that scope** — and the probes are drawn from `WellSatMaxRadiusCm` at a
second, independent read of the same constant in `NodeShuffleWellRelocateApply.cpp`'s satellite loop.
The two files agree today. **Nothing makes them agree.**

**Why this is worse than an ordinary latent bug: the break is SILENT and it PASSES rather than fails.**
A larger draw radius — e.g. a per-well `SatMaxRadius`, which the **short-wells packet is named as the
next owner of this code and is exactly the kind of change it wants** — leaves probes outside the
scanned set. The gate then tests a candidate against an incomplete population, **accepts** a spot beside
a real resource node, and no reason string changes, no rejection is logged, and no counter moves. The
player sees two node meshes intersecting; the log says the layout validated.

**The fix is an assertion, not prose.** The two TODO comments ns-t27-truth left at both sides of the
coupling (`TODO(ns-t27-truth 2026-08-09)`, in `NodeShuffleWellRelocateApply.cpp` at the scan call and at
the satellite draw) are a marker, **not** a fix — this workspace's own rule is to put the check AT the
seam, and prose is not a check. The reviewer's proposed shape: pass the scan centre and scope radius
into `ValidateWellMemberSpot`, and on the cached branch count and log any probe whose
`Dist2D(OutLoc, ScanCentre)` exceeds `ScopeRadius - WellOverlapRejectRadiusCm`. A compile-time coupling
(deriving one from the other in one place) is stronger still and should be considered first.

**Do this before, or as part of, the short-wells packet — not after it.**

### T7 REGRESSION — `NodeShuffleWellRelocateRoll.cpp` is 921 lines, the day T7 was closed
The three-phase restructure took it 519 → **921** (84% over the limit), hours after the four well files
were split under 500. It **cannot** be split from inside its own packet: the extraction needs a member
declared in `NodeShuffleSubsystem.h`, which that packet did not own.

Recorded rather than waved through, because the split-then-immediately-regrow pattern is how a limit
stops meaning anything. **Next split packet takes this file and owns the header.**

### ~~PARKED — attempt 1 of the re-roll-geography change~~ — SUPERSEDED, stash DROPPED 2026-08-08
Attempt 2 shipped (above), so attempt 1 is dead code. **The stash was deliberately dropped rather than
left lying around**: a `DO NOT SHIP` entry sitting in `stash@{0}` is a trap for a future session that
pops it looking for context. Its content was one deletion — the `bGroupPlaced` guard — and both its
blockers are recorded above in full. Nothing recoverable was lost. *Original entry retained below for
the history.*
**The author decided wells should re-roll like ordinary nodes** — unpinned re-rolls its geography,
pinned does not move. The guard at `NodeShuffleWellRelocateRoll.cpp:152` was deleted to enable it, and
the cold review returned **DO NOT SHIP on two blockers**. The work is stashed, not lost; the deployed
binary was rebuilt from the reviewed commit so nothing unsafe is in the DLL.

1. **T16 above makes the change's stated safety property false.** The comment claimed pinning protected
   built-on wells; nothing is ever pinned, so a re-roll could despawn a well out from under a
   pressurizer. Buildings survive (`DespawnWellGroup`'s occupancy gate holds) — but **its return value
   is discarded**, so execution continues into `bGroupPlaced=false`, claim withdrawal and a re-deal,
   leaving the player's machine on the old core and fresh live satellites kilometres away, retrying
   forever. In a mod whose deck machinery exists to conserve well resources, that duplicates them.
2. **A failed re-deal deletes the well from the world.** Despawn + `ClearAbandonedWellPlacement` run
   *before* the deal loop, and nothing ever un-suppresses a vanilla group. On deal failure the log says
   "left vanilla this roll" — false: nothing at the old site, nothing at a new one.

> **Pre-scoped fix, in the review's recommended order:** (A2) fix the pin at its source per T16;
> (A3) restructure to **deal-first, despawn-second** so a failed deal cannot orphan the well;
> (A4) gate it behind a `RerollRelocatedWells` toggle, default OFF, so an existing save with 17 placed
> wells does not churn on one keypress. **This is a structural review-response change to a
> path-replacing change — it earns a FULL re-review, from a different reviewer.**
> Also flagged: `WellRedealTries = 24` was sized for 3-5 wells per roll and would now be asked for ~20
> ([[feedback-own-caps-are-revisable]]); five refusal diagnostics would assert "left vanilla" for wells
> that are visibly relocated; and the summary counters would double-count, since `AlreadyCaptured` and
> `Enrolled` were disjoint only because of the guard.

---

## P2 — real unknowns, cheap to close

### T56. TEN well groups read `groupComplete=0 coreCaptured=0 corePieces=0` at load — the SAME ten, on every one of eight loads, in BOTH save files. **— "the same ten" is FALSIFIED by a third save; see the T56 STATUS block. Resolved to capture COVERAGE, not persistence.** The code's own comment says that state "must NOT" occur on a reload of a dressed well. **[[T54]]'s hide is NOT implicated — do not conflate them.**
**Measured 2026-08-10, build `2026-08-10-t54-1`** (evidence `lithium-probe-extract.md` §"4th
follow-up" Q2, extended by a direct re-grep of all eight `FactoryGame*.log` files). The reading comes
from `WELLH2B-ADOPT`, `Source/NodeShuffle/Private/NodeShuffleWellVisuals.cpp:107-113`, whose own text
is the invariant being violated: *"on a fresh roll it is all zeros, on a reload of an already-dressed
well it must NOT be"* (comment at `:89-96`).

**The ten are identical, by name, on every load** — `BP_FrackingCore3/4/5/7/9/10/11/12/18` and
`BP_FrackingCore6_UAID_40B076DF2F79D3DF01_1961476789`. Verified by set-diff, not by count, across
`…-07.37.43`, `…-17.13.38`, `…-17.15.16` and the live log (opened 12:29:49). Count per load: 10 on
seven of the eight; **13 on load A** — A additionally had the three `BaseNode_FrackingCore*` entries
zeroed on what was their first-ever session (see [[T57]]).

**THE ROUND TRIP IS NOT BROKEN IN GENERAL, WHICH IS WHY THIS IS A REAL UNKNOWN AND NOT A ONE-LINE
FIX.** In the same load lines, seven other groups read back **non-zero** (`BP_FrackingCore13`
`corePieces=2`, `14` `=5`, `15` `=3`, `17` `=3`, `2` `=1`, `6` `=5`). The fields are all
`UPROPERTY(SaveGame)` and reachable — `bGroupVisualsComplete` `NodeShuffleSubsystem.h:760`,
`bCoreVisualsCaptured`/`CoreVisuals` `:726-727`, inside `WellLayout` `:1701`. So persistence works;
these ten specifically do not carry it.

**Two candidate mechanisms, NEITHER measured. They demand different fixes, so measure before writing
code:**
1. **Lost on the way out** — captured in-session, absent from the save. `BP_FrackingCore10` does log
   `WELLH2B-CAPTURE` and `WELLH2B-APPLY` in the live session, so it *is* captured at some point after
   the zero reading.
2. **Never captured at all** — the zeros are TRUTHFUL and the comment's premise ("already-dressed")
   is false for these ten, i.e. this is a capture-coverage gap, not a persistence gap. **Nothing in
   the evidence establishes those ten were ever dressed at the moment either save file was written**,
   and no save was observed being written during these short sessions.

**Cheapest discriminator:** load, confirm the ten read zero, walk to one of them until
`WELLH2B-CAPTURE` reports it complete, **save manually**, reload, and read that core's `WELLH2B-ADOPT`
line. Non-zero ⇒ mechanism 2 (coverage). Zero ⇒ mechanism 1 (round trip), and it is then a P1.

#### T56 STATUS: **DISCRIMINATED — MECHANISM 2 (CAPTURE COVERAGE). Mechanism 1 (lost on the way out) is FALSIFIED. No round-trip fix is needed or shipped; the INVARIANT TEXT was the defect and is corrected.** (packet `ns-t55-t56-reload`, shipped in commit `84a23fa`, 2026-08-10, marker `2026-08-10-t5556-1`; cold-reviewed same day, verdict SHIP WITH TESTS.) (status per `_team/nodeshuffle-followups/t5556-coldreview.md`, 2026-08-12 release-readiness audit)

The discriminating in-game walk was **not needed** — a session the author started at 15:07 on a fourth save
(`loadgame=Reshuffle_01`, build `2026-08-10-t58-1`, live `FactoryGame.log`) answers it two ways:

1. **The zero set is NOT a fixed ten. It is per-save, and it moves in BOTH directions.** In `Reshuffle_01`
   the zero cores are `BP_FrackingCore2/3/6/7/9/10/11/12/13/14/_8` — **13 and 14 are in it**, and those two
   are among the seven this entry cites as *proof the round trip works* (`13 corePieces=2`, `14 =5`) in the
   other save files. Meanwhile `4`, `5` and `18` — three of "the ten" — read back **non-zero** here. A
   per-core persistence fault cannot flip in both directions across save files.
2. **Nothing was captured that a save could have dropped.** Every core reading zero in that load's
   `WELLH2B-ADOPT` block also logs, IN THE SAME SESSION, a `WELLH2B-CAPTURE` line reporting **`0 member(s)
   captured, 7–11 still empty -> group INCOMPLETE`**. The correspondence is exact and one-to-one: every
   zero-ADOPT core appears in that incomplete list, and no non-zero-ADOPT core does.

**This entry's supporting datum for mechanism 1 was a misread.** *"`BP_FrackingCore10` does log
`WELLH2B-CAPTURE` … so it IS captured at some point"* — that core's CAPTURE line reads `0 member(s)
captured, 8 still empty`. **A `WELLH2B-CAPTURE` line is emitted for FAILURE as well as success**; its
presence is not evidence of capture. (Filed as a specimen: the log line said what it measured, and the
reader supplied the meaning.)

**SO THE ZEROS ARE TRUTHFUL AND THE COMMENT WAS WRONG.** `NodeShuffleWellVisuals.cpp:89-96` already said a
well can be dressed from the session template with the persisted fields empty — and then its own log line
said a dressed well's fields *"must NOT"* be zero. The two halves contradicted each other and T56 was filed
against the wrong half. **Shipped:** the `WELLH2B-ADOPT` text now states what zero and non-zero mean without
claiming an invariant it cannot hold, and carries the measurement above.

**WHAT REMAINS OPEN, and it is a coverage question, not a persistence one:** why those members never
resolve. `CaptureMember` needs BOTH `FindOriginalBaseByPath` to return a live node AND a `WellMeshIndex`
entry with pieces; the single `MembersMissing` counter conflated three different absences, so no log said
which input was missing. **Shipped:** `WELLH2B-CAPTURE`'s group summary now splits it — how many members had
no node from `FindOriginalBaseByPath`, how many had a live node but no indexed mesh piece, and how many had
indexed pieces that yielded no visual. **One reading of that line on a next boot decides whether this is a
streaming question or a mesh-index question.** Also corrected there: the per-member Verbose line printed
*"is NOT streamed"* for `!IsValid(Node)`, which tests path resolution and never tested streaming — the
[[lessons-log-asserted-a-cause]] class, second sighting in this file family.

**WHAT THIS ENTRY IS NOT.** [[T54]]'s immediate-hide **holds across all eight loads**: the
`WELLH2-IMMEDIATE` (*"ORIGIN HIDDEN NOW"*) count is A=3, B=18, C-H=**0** — no first-touch re-hides
after load B — and the T54-A/T54-B opposite-polarity pair passes on every load (evidence §Q2). B's 18
is consistent with those groups being treated as new once, on the second-ever load, before settling.
A capture-state defect and a hide defect are separate populations; T54 is not re-opened by this.

### T57. The "well ledger oscillates 17↔20 and never converges" finding is **FALSIFIED — the ±3 is WHICH SAVE FILE WAS LOADED.** It is deterministic, 8 loads for 8. What survives is a smaller, real question: three modded-looking well cores enrol ~28 apply passes into a session.
**This entry exists because the measurement was right and the POPULATION was wrong** — the defect
class in `CLAUDE.md`'s POPULATION lens, caught here by re-grepping rather than by anyone's arithmetic.

**The original reading** (evidence `lithium-probe-extract.md` §"4th follow-up" Q3): `WELLH2-DEFERCENSUS
… layout holds N` (`Source/NodeShuffle/Private/NodeShuffleWellStage0.cpp:283-284`) gave A 17→20,
B 20, C 20, D 17, E 17, F 20, G 17 — read as *"the same ±3 entries appear and vanish, never
converging."*

**What the loads actually opened** (`loadgame=` in each log's travel URL, plus the well-core names in
each load's `WELLH2B-ADOPT` block):

| load | log | save file | ledger | `BaseNode_*` cores present |
|---|---|---|---|---|
| A | `…-07.37.43` | `Test_02_autosave_0` | 17 → **20 at pass 28** | enrolled mid-session (log lines 42226-42265) |
| B | `…-07.45.49` | `Test_02_100826` | 20 | 3 |
| C | `…-17.08.47` | `Test_02_100826` | 20 | 3 |
| D | `…-17.09.45` | `Test_02_autosave_0` | 17 | 0 |
| E | `…-17.12.31` | `Test_02_autosave_2` | 17 | 0 |
| F | `…-17.13.38` | `Test_02_100826` | 20 | 3 |
| G | `…-17.15.16` | `Test_02_autosave_0` | 17 | 0 |
| H | live, opened 12:29:49 | `Test_02_100826` | 20 | 3 |

`Test_02_100826` holds 20 every time; the autosaves hold 17 every time. **Zero exceptions in eight
loads.** The ±3 is always exactly `BaseNode_FrackingCore1_0`, `BaseNode_FrackingCore2_1`,
`BaseNode_FrackingCore_2` — a naming family disjoint from the vanilla `BP_FrackingCore*` set. The
ledger is stable per file and there is nothing to converge.

**The residue is real and is the whole item.** On load A those three were enrolled **at runtime, at
apply pass 28** — the ledger printed 17 through pass 27 and 20 at pass 28 — and that growth was never
saved back, which is why `autosave_0` still loads at 17 on D, G. Sessions that ended earlier never saw
them: E printed its last census at pass 23, still 17. So a well population exists in the world that the
mod does not know about for roughly the first two minutes of every session, and permanently in any
session shorter than that.

**HYPOTHESIS, NOT FACT — the [[T14]] class.** T14 measured that other mods `SpawnActor` their nodes
during their own init/research gating, after our roll, and that this is the mod's only real discovery
gap. `BaseNode_*` cores enrolling at pass 28 *looks* like the same mechanism. **It has not been
tested.** Two alternatives are equally live and cheaper to check first: (a) ordinary level streaming —
those three sit somewhere the author only reaches late; (b) they are in the ledger from the start and
only their `WELLH2B-ADOPT`/census *print* is late. **Measure which, before treating this as T14's
recurrence.** The discriminator is whether the three cores' actors exist in the world before pass 28 —
`ROLLCENSUS:`'s `runtimeSpawnedByOtherMods` field (T14's own instrument) answers it directly.

**Cross-links, stated as hypothesis:**
- **[[T56]]** — the ten zero-capture cores are the SAME ten in both save files, so T56 is **not**
  explained by this. The only overlap is load A's 13 (10 + these 3, on their first session).
- **[[T55]]** — the "same 3 miners announced on D and again on G" that made the chat repeat look
  worse is partly this: D and G are the same autosave loaded twice. T55's defect stands on its own
  (F→H announced the same AlkaLib pair across two *different* saves), but its severity narrative
  should not lean on the D/G pair.
- **The evidence file's own STRANDED sequence** (8→18→18→0→18→7→8→18→7) was read as tracking a
  17↔20 oscillation. It tracks the save file instead. Any conclusion drawn from that sequence needs
  re-deriving per save file before it is used.

**RULE, PAID FOR TWICE NOW:** before calling a cross-load reading unstable, **verify every load opened
the same save.** One `grep -o "loadgame=[A-Za-z0-9_]*"` per log would have retired this finding before
it was written up as the deepest of the three.

### T2. Desert-biome well meshes are unverified
> **SCOPE CHANGE, T71 (2026-08-12): THIS IS NOW ALWAYS IN SCOPE.** Until T71 this gap could only be
> reached by a player who had ticked the opt-in **Relocate Resource Wells** box, and that box's own
> tooltip carried the warning. Both well toggles are deleted and hard-wired ON, so **every** save
> relocates wells and every desert well is exposed. It is no longer a risk taken by people who opted in.
> The warning moved to README.md's Resource wells section, the CHANGELOG 1.4.0 entry and the mod
> Description. Nothing below is re-measured by T71 — only the population it applies to changed.
H2b's mesh pairing narrows on `Contains("Frack")`. That is corroborated **only** for
`SM_FrackingNode_Crack_01` / `_Mid_01` / `_Small_01`. `MT_Desert*` variants exist in the
enum and **no desert fracking mesh name appears anywhere in our evidence**. If desert
wells use differently-named meshes they will relocate undressed.

**Measurement:** relocate a desert well and check for a crack graphic. No graphic *and*
no `spatial REJECT` naming it = the gap is real.

> **Pre-scoped fix (round 13's recommendation):** replace the name filter with a class +
> `mNodeMeshType` gate — type-driven like `IsFrackingActor`, closing the unknown
> *statically* rather than by enumeration. The code already reads `mNodeMeshType`
> (`NodeShuffleWellVisuals.cpp:406`), so the access is proven.

### T14. The first roll misses nodes that other mods spawn after boot
**Measured 2026-08-08 by set-diff of object paths, not by counts**
(`node-enumeration-investigation.md` Q1 evidence 3). Between the boot roll (17:19) and a
manual re-roll six minutes later, **28 nodes** appeared that were absent at boot — 22
`BP_ResourdeNode_Alkali_C` (AlkaLib lithium) and 6 modded lead — while **zero** level-placed
vanilla nodes appeared late and **zero** were lost.

This is the mod's only real discovery gap, and it is **not** streaming: those nodes are
`SpawnActor`'d by other mods during their own init / research gating, so nothing that exists
at our roll time can contain them. A cook-time manifest could never hold them either
(`AFGWorldScannableDataGenerator::CacheWorldScannableData` is `WITH_EDITOR`, baked into the
base map's cook), which is why the live `TActorIterator` remains the correct design — its only
defect is *when* it runs, not *what* it can see.

**Today's answer is manual: re-roll.** `ROLLCENSUS:`'s `runtimeSpawnedByOtherMods` field makes
the population visible per roll.

> **Pre-scoped fix, not applied and deliberately gated on the user:** re-run the live augment
> automatically once — N seconds after boot, or on a node-count-changed edge — instead of
> requiring a manual re-roll. Moderate: it touches `RollLayout`'s hot path and changes a
> save-visible layout, so it needs its own packet and its own full regression pass.
> **Explicitly rejected alternatives:** raising `MinVanillaNodesForRoll` (fixes nothing, and
> would cement the false streaming model by looking like a fix) and building a node manifest
> (strictly worse — it *loses* exactly this population).

#### T14 DESIGN ITEM (author: yes, 2026-08-10) — **AUTO-ENROLL NEWLY-SURVIVING FOREIGN RESOURCES WITHOUT A FULL RESHUFFLE. NOT IMPLEMENTED, NOT SCOPED TO A PACKET, NOT DESIGNED — this block is the goal and the open questions, nothing more.** (Recorded by packet `ns-t59-pack-namespace`; that packet implemented **only** T59 and wrote no code for this.)

**GOAL.** When a foreign (other-mod) resource node exists in the world but NodeShuffle is not managing
it, bring it under management **incrementally** — without the player having to trigger a full re-roll
that re-deals the whole map.

**WHY NOW, and why this is newer than T14's own pre-scoped fix.** [[T58]]'s protect veto makes
third-party nodes **survive** SF+'s cleanup on new games — that was the fix, and it worked at the
survival layer only. A surviving foreign node is not thereby *enrolled*: our layout was dealt without it,
so it stays outside the shuffle until a **manual** reshuffle re-scans the world and picks it up. So the
population T14 has always described — nodes present but absent from the layout — now has a second, more
common source than late `SpawnActor` timing, and the only remedy on offer is the heaviest one the mod
has. T14's existing pre-scoped fix (re-run the live augment automatically once, N seconds after boot) is
**adjacent, not the same thing**: it is about *when* the augment runs, this is about *enrolling a node
the augment has already seen survive*, at any point in a session.

**OPEN QUESTIONS — none of these are decided, and the first two change the shape of the packet.**
1. **What triggers enrollment?** Candidates, uncosted: the veto's own first-sighting hook
   (`NoteForeignResourceSighting`, which T60 already runs per foreign resource per session — it is the
   one place that provably observes exactly this population); a periodic node-count-changed edge (T14's
   pre-scoped trigger); an explicit player action (console command / config toggle); or on the next load
   only. A hook that fires **per sighting** and a sweep that fires **per interval** have different
   failure modes, and the population rule (T60's POPULATION RULE) is the thing to reuse either way.
2. **Does enrollment MOVE the node, or adopt it in place?** *Adopt in place* = the node joins the managed
   set at its current location, keeps its own resource, and only becomes eligible for future rolls —
   cheap, invisible, and arguably not "shuffled" at all. *Move it* = deal it a destination now, which is
   what a reshuffle would have done, and which changes the world mid-session in front of a player who may
   already have built on or around it. **These are different features and the answer is not obvious;
   do not let an implementation pick one by accident.**
3. **Save impact.** The layout is save-visible state. Enrolling mid-session mutates it outside
   `RollLayout`, which every existing invariant about "the layout changes only at roll time" is written
   against — including the auto-allow pass's own latch/re-arm argument
   (`NodeShuffleAutoAllowExtractors.cpp` header: *"Layout only CHANGES at RollLayout"*), which would
   become false. **Whoever scopes this must enumerate the consumers of that invariant first**, not
   discover them afterwards.
4. Secondary, but real: what happens on the load AFTER enrollment if the foreign mod is removed; and
   whether an enrolled-in-place node is distinguishable in the layout from one this mod relocated (the
   S1/`Foreign` classification in [[T58]]/[[T60]] is the existing vocabulary for that question).

### T3. Snap-box overlap — **PARKED 2026-08-08 by the mod author. Watch-only; do not schedule work.**
> **SCOPE CHANGE, T71 (2026-08-12): THIS IS NOW ALWAYS IN SCOPE.** The park decision below was taken
> while relocation was an opt-in toggle, so the exposed population was "players who ticked the box".
> T71 deletes that toggle and hard-wires relocation ON, so every save with a well near an ordinary node
> is exposed. **The park still stands** — the author's ruling was about whether to open a packet, not
> about who was exposed, and nothing here has been re-measured. What changes is the watch: a Miner
> refused beside a relocated well is now something an ordinary player can hit without having opted in,
> so the "please report it" ask now lives in README.md's Resource wells section rather than in a tooltip
> only opt-in players ever saw.

**DECISION (author, 2026-08-08):** *"I don't think we should worry about well overlap a node. We can log
as a possibility for tech debt to explore more or if I run across it."*

**Do not open a packet for this.** It is now a **watch item**: if a Miner is ever refused on a visible,
mineable node beside a relocated well in ordinary play, that observation reopens it — and the
instrumentation to diagnose it already shipped (`2026-08-08-t1t2-2`), so the evidence will be in the log
when it happens. Everything below is retained as the record of what was measured and what the numbers
are worth.

**Justification for parking, so it is not re-escalated on the raw numbers:** the 8 measured overlaps
resolve so far to **hidden originals, never an active node** (see the correction below), and a hidden
node cannot be built on regardless of any box. There is no confirmed hazard, only a confirmed
*instrument* defect.

> **The `IsHidden()` filter LANDED 2026-08-08**, riding the T7 split as intended — it was the only
> intended behaviour change in that packet, scoped to this diagnostic alone (`Bystanders` untouched, so
> no node is claimed, hidden or de-collided differently). Both counts now print: `activeMineable=` and
> `hiddenOriginal=`, rather than the population silently shrinking.
>
> ⚠ **THE "8 PROVABLE OVERLAPS" FIGURE BELOW IS NOW HISTORICAL AND MUST NOT BE RE-QUOTED.** It was
> measured against the unfiltered population. **No hazard count exists until a fresh load re-measures**
> against `activeMineable` only. If that number comes back 0 — which the evidence below predicts,
> since every overlap resolved so far was a hidden original — T3 closes as a non-issue.

### T3 (evidence as measured 2026-08-08 — hazard status: none confirmed)

> ## ⚠ CORRECTION, same day, hours after the entry below was written and committed.
> **The user asked: "are you sure the node is an active one and not a hidden vanilla?" It is not sure,
> and the instrument cannot tell.** `RebuildWellMeshIndex`'s sweep
> (`NodeShuffleWellVisuals.cpp`) filters only `!IsValid(N)` and `Ours.Contains(N)`:
> ```cpp
> if (Cast<AFGResourceNode>(N)) { UseBoxNodes.Add(N->GetActorLocation()); }
> ```
> **There is no `IsHidden()` test.** A hidden vanilla original is still an `AFGResourceNode`, still
> passes the cast, and still enters the population. **658 originals were hidden at load in the very
> session that produced these 8 results**, so hidden nodes plausibly dominate the 1227-node snapshot.
>
> **Why this matters:** T3's hazard is "a Miner is refused on a node the player could otherwise use."
> A hidden original cannot be built on regardless of any box, so an overlap against one is **harmless
> and should never have been counted**. The 8 numbers below are correct as geometry and **unproven as
> hazards**. They may all be hidden originals.
>
> **This is a false-POSITIVE generator — the opposite direction from the reviewer's F-4 concern about
> false negatives.** The packet did not catch it, the cold review did not catch it, and neither did I;
> the mod author did, from the domain and not from the code. Recorded because the review process has a
> demonstrated blind spot for *population* errors: every gate here checked the predicate's arithmetic,
> and none asked whether the set being measured was the right set. Cousin of
> [[lessons-test-subject-was-exempted]].
>
> **Pre-scoped fix:** add `IsHidden()` to the `UseBoxNodes` filter, and **print both counts**
> (`activeMineable=` / `hiddenOriginal=`) rather than silently shrinking the population — a denominator
> that quietly changes meaning is the defect this file keeps re-learning.
> **Do not re-state any hazard count until that lands and a fresh load re-measures.**
>
> **RESOLVED BY EVIDENCE, same session — no in-game trip needed.** The author noted that the only node
> visible near that well in their own screenshot was a Kerr crystal far too distant to matter. Back-
> solving the satellite's world position against the overlap's per-axis deltas identified the culprit:
>
> ```
> ORPHANDIAG: 'Resource_Stone_01' at V(X=-43336.21, Y=239568.84, Z=-3837.56)
>             already hidden -> no action
> ```
>
> A **hidden stone original**. Repeating for all five distinct members: **2 of 5 resolve to confirmed
> hidden originals** (`Resource_Stone_01`, `SM_LithiumNode`), 3 could not be resolved from the log, and
> **0 resolve to an active node.** So there is **no confirmed T3 hazard** — the measured overlaps are so
> far entirely the artefact this correction predicted.
>
> **The author's eyes beat the instrument**, and that is the durable point: a screenshot answered in one
> glance what 128 measurements got wrong, because the instrument was counting the wrong population and
> could not know it. When a measurement disagrees with direct observation, suspect the population before
> the arithmetic.

### T3 (geometry as measured 2026-08-08, hazard status pending the fix above)
**The instrumentation worked on its first run.** Build `2026-08-08-t1t2-2`, one save load, no travel and
no re-roll: **128 measurements, 8 provable overlaps**, against a snapshot of **1227** ordinary mineable
nodes. Every verdict was audited against its own printed per-axis numbers — **zero mismatches**, so the
predicate is correct, not merely firing.

| member | extent | nearest mineable node | dx/dy/dz (cm) |
|---|---|---|---|
| `BP_FrackingSatellite_C_2147460444` | 900.00 | **868 cm** | 751 / 434 / 24 |
| `BaseNode_FrackingSat_KLib_C_2147416684` | 860.56 | 933 cm | 879 / 80 / 302 |
| `BaseNode_FrackingSat_KLib_C_2147416681` | 863.23 | 1099 cm | 1077 / 202 / 80 |
| `BaseNode_FrackingSat_KLib_C_2147413508` | 807.99 | 1380 cm | 1072 / 869 / 10 |
| `BaseNode_FrackingCore_KLib_C_2147416687` | 900.00 | 1833 cm | 1245 / 1281 / 413 |

The first row is a satellite of the chlorine core the user built a pressurizer on, so it is **directly
reachable for a runtime test**. The `KLib` members are modded wells.

**Overlap is necessary, not sufficient** — it proves the boxes intersect, not that a Miner is refused.
The runtime test is still owed: place a Miner Mk1 on the ordinary node beside row 1.

> ⚠ **A LOG-DESIGN LESSON THAT COST THREE WRONG READINGS IN ONE SITTING.** This line's legend contains
> the literal text `provableOverlap=0); provableOverlap=%d`, so the **legend's example value appears in
> the log BEFORE the real field**. A naive `grep -c "provableOverlap=1"`, and even a "first occurrence"
> regex, reads the legend and not the measurement — it produced "136 overlaps", then "0 overlaps",
> before the correct answer of 8. Same family as the `(5 of 6 groups…)` constant and the
> `re-enrolled by a new roll` prose. **RULE: a legend must DESCRIBE its fields, never EXEMPLIFY them in
> `field=value` syntax that collides with the real field.** When counting any field in this project's
> logs, anchor on a delimiter the legend cannot contain (here: the trailing `.`), and sanity-check the
> match count against the line count — 384 matches over 128 lines was the tell. [[lessons-zero-needs-a-denominator]]

### ~~T3 (original). Snap-box overlap with neighbouring nodes is proven geometrically, never observed~~
`EnsureWellMemberSnapBox` can reach 900 cm; `EnsureNodeUseBox` gives ordinary nodes 650 cm.
H0 measured the nearest non-same-well node at **1400 cm**. 900 + 650 = 1550 > 1400, so the
boxes provably intersect in the population H0 measured.

**Not observed** — the well used for the 2026-08-08 test sat on a plateau with no ordinary
nodes in range. **Measurement:** a Miner Mk1 on an ordinary node within ~15 m of a relocated
well member must still snap.

> **Why it has never been observed, found 2026-08-08: nothing measures it.** The 1400 cm figure
> came from H0's separate analysis, not from a log line. Grepping the live `FactoryGame.log` for a
> snap-box/nearest-neighbour diagnostic returns **nothing** — there is no `WELLH2B-SNAPBOX` line and
> no "nearest non-well node" line anywhere. So this item cannot be closed by playing; it can only be
> closed by *stumbling onto* the geometry and noticing a miner that will not place. That is the
> P3 failure class one section down, in its purest form: **an item whose test is "get lucky".**
>
> **Pre-scoped fix — instrument before touching behaviour** (same doctrine as T4). In
> `EnsureWellMemberSnapBox` (`NodeShuffleWellVisualsApply.cpp:153`), at box-creation time, log the
> member, its final box extent, the distance to the nearest **non-same-well** resource node, and
> whether the two boxes provably intersect (`extent + 650 > distance`). The contest sweep in
> `NodeShuffleWellVisuals.cpp` already builds exactly the bystander-location array this needs, so
> the data is in hand. Then the **existing save answers the question on the next load** with no
> hunting. Report only the measurement — never a cause ([[lessons-log-asserted-a-cause]]).
>
> **The 14 relocated destinations in the user's current save**, extracted from `FactoryGame.log`
> 2026-08-08, so a manual check does not have to start by finding the wells:
>
> | resource | core | destination |
> |---|---|---|
> | Gas_Chlor | `BP_FrackingCore10` | `X=138900.53, Y=240105.70, Z=-3828.89` |
> | Gas_Chlor | `BP_FrackingCore17` | `X=-44403.44, Y=236471.64, Z=-3874.49` |
> | LiquidOil | `BP_FrackingCore15` | `X=-20280.56, Y=60895.37, Z=22579.53` |
> | LiquidOil | `BP_FrackingCore3` | `X=-144296.27, Y=-110723.30, Z=2164.50` |
> | LiquidOil | `BaseNode_FrackingCore2_1` | `X=-197300.99, Y=-108281.73, Z=597.63` |
> | NitrogenGas | `BP_FrackingCore11` | `X=102207.83, Y=160609.81, Z=1807.17` |
> | NitrogenGas | `BP_FrackingCore13` | `X=149586.85, Y=263255.37, Z=-767.00` |
> | NitrogenGas | `BP_FrackingCore2` | `X=-22410.94, Y=-86941.46, Z=3089.27` |
> | NitrogenGas | `BP_FrackingCore9` | `X=281997.30, Y=-31236.98, Z=9806.94` |
> | Water | `BP_FrackingCore12` | `X=-148481.47, Y=42995.02, Z=23669.52` |
> | Water | `BP_FrackingCore18` | `X=124256.32, Y=142012.11, Z=8656.73` |
> | Water | `BP_FrackingCore5` | `X=-6358.01, Y=-80721.03, Z=13624.39` |
> | Water | `BP_FrackingCore6_UAID_...1961476789` | `X=-159925.11, Y=113053.53, Z=7629.70` |
> | Water | `BaseNode_FrackingCore1_0` | `X=77127.81, Y=46452.60, Z=11494.30` |
>
> ⚠ These are **destinations dealt in that save's roll**, read from one log. A re-roll re-deals them.

### T62. A third-party node-pointer mod (RNM) beams at our HIDDEN ORIGINALS and never finds late-materializing replacements — dirty classes only. **Author priority: LOW (2026-08-11) — do not schedule ahead of P1 work.**

**What it is.** Resource Nodes Manager (RNM, third-party) draws pointer lines to node centres from
its own list. After a manual reshuffle on the new-game test save (build `t61-1`), RNM tracked the
relocations of vanilla, `lead_C` and Alkali/lithium nodes correctly, but its entry for AllMinable's
Dirty Black Powder kept the ORIGINAL location — listed at 4 m, beam standing on empty ground
(author's screenshots, 2026-08-11 ~evening) — while the replacement **exists and is minable**
(a mod miner snapped to it; `materialized esc_BlackPowder_C node` in the roll session's log).
Known for weeks per the author ("we had this issue with rm then too") — **NOT a regression of the
2026-08-10 builds.** RNM also shows no pointers at all for water/nitrogen wells; presumed RNM's own
scope (fracking cores/satellites are not solid-node actors) and not chased.

**What is measured healthy on OUR side, same resource, same sessions** (evidence:
`nodeshuffle-t61-log-extract.md` §POST-REROLL, scratchpad copy of 2026-08-11): enrollment
(`ROLLCENSUS` `esc_BlackPowder_C=7/4`, AUTOALLOW groups 73→286), spawn success (zero dirty-class
spawn failures), originals capture-terminal **by design** then **DEREGISTERED**, cluster regen runs
in both sessions with dirty classes inside its population, scanner-knowledge 8→35→42 persisted
across the reload. The vanilla scanner — the one consumer we patched at the
`GenerateNodeClusters` source — is correct.

**Mechanism — HYPOTHESIS, not measured (RNM internals unread: actor iteration vs registry vs
persistent cache, unknown).** The per-session transition window. Dirty originals are the chronic
capture-terminal class (instanced-mesh visuals; `CaptureGiveUpPasses = 36` ≈ 3 min at the 5 s
tick), so each session they sit hidden-but-REGISTERED for up to ~3 minutes before deregistration,
while dirty replacements materialize through the same deferred passes. A consumer that snapshots
early each load captures exactly the wrong set — originals in, replacements out. Vanilla and
lead/lithium originals capture and deregister fast, so an early snapshot is already correct for
them. One window explains the per-class asymmetry, the reload persistence, and the weeks-old
history. It is still a hypothesis until the test below runs.

**Discriminating test (first step for whoever picks this up; ~5 min in game).** More than ~4 min
into a session, force an RNM rescan (reopen/reconfigure the terminal near the live replacement).
(1) Ghost drops and the live node appears → window hypothesis stands; fix is a window-shrink
and/or a compat note. (2) Rescan still shows the ghost or still misses a node the player can
mine → RNM enumerates something we do not clean; measure WHAT before designing anything.

**Pre-scoped fix directions — do not build until the test discriminates.** (a) Deregister
originals AT suppression time instead of after capture-terminal — touches the restore-on-disable
contract; restore is believed to read our own records rather than the registry, but that is an
ASSUMPTION to verify, not a given. (b) Compat note only ("rescan RNM after a reshuffle").
Cross-ref: this is the blast-radius lens's first recorded third-party sighting — RNM is a consumer
of node registration nobody enumerated.

**Diagnostics sub-note (P3-flavored, kept here to keep one entry):** ORPHANDIAG covered ZERO
dirty/AllMinable classes across both sessions (13 + 38 lines, all fracking chatter) — the
diagnostic that would have named the stale actor cannot see this population. If branch (2) wins,
extend ORPHANDIAG's population first.

### T63. Nodes can be dealt INTO DENSE FOLIAGE — a tree cluster is invisible to every placement gate. **Author priority: LOW, possibly never (2026-08-11: "I think it's hilarious").**

**What it is.** On the new-game test save, a Dirty Rubber (Pure) replacement landed inside a
dense tree cluster (author's screenshot, 2026-08-11). **Fully functional** — hand-mine prompt
appears, mod miner places. The defect, to the extent it is one, is aesthetic: a node can sit
where trees visually swallow it.

**Mechanism — measured, two sightings.** Foliage is `InstancedFoliageActor` instanced meshes,
which do not block the trace channel the placement/containment stack queries — and the
`TOTALLYINSIDE` legend's own caveat applies: *"geometry that does not block this channel is
invisible to all four of its instruments."* Prior sighting, same family: the 2026-08-10 WellProbe
at Core7 measured rays STARTING inside foliage firing already-penetrating without flipping any
verdict (state-file note at the time: "candidate future exclusion, not filed as debt" — this entry
is that filing). No gate in the stack has ever considered trees; nothing regressed.

**Why LOW/never.** Harmless to play, arguably thematic (author: "rubber does come from trees!").
File exists so the next person who sees a node in a bush knows it is KNOWN and understood, not a
mystery — the header rule of this file.

**Pre-scoped direction IF ever wanted.** A foliage-density check at the SETTLE/deal stage (query
`InstancedFoliageActor` instances in a small radius; prefer sparser candidates rather than veto) —
NOT in the enclosure predicate, which answers a different question (T37). Prototype in the solid
path first; wells have their own probe stack.

---

## P3 — diagnostics that cannot report what they exist to report

*This project's recurring failure class. Eight sightings during the H2 arc. A gate that
cannot fail is worse than no gate, because it is read as evidence.*

### T64. The ordinary-node rock-hide was ONE-SHOT: a rock whose mesh actor streamed in AFTER its node was hidden arrived VISIBLE and nothing retried. **A = FIXED-this-packet · B = TOOL-ADDED (packet ns-t64-rock-hide-and-audit, build `2026-08-11-t64-1`).**

**THE MECHANISM IS MEASURED, AND THE TRACE IS THE AUTHORITY** —
`scratchpad/solid-hide-latency.md` (2026-08-11, taken on build `2026-08-10-t61-1`, log session
`NodeShuffle 1.3.0 LOADED (2026-08-10-t61-1)`). Do not re-derive it; read it.

* All **874** original NODE actors resolved and were hidden on hide-pass 1, ~25 s after mod load
  (`records=874 loaded=874 newlyHidden=874 pathUnresolved=0`). Nothing was ever re-hidden: every one of
  the 20 `Hide originals:` lines after pass 1 reads `hid 0 original nodes`.
* Of those 874, only **37** had a resolvable `AFGNodeMeshActor` at that instant. **837 did not** — 271
  because no separate mesh actor is authored at all, **566** because the assigned one had not streamed in.
* The record was then marked `SteadyHiddenOriginals` on the **node** result alone (the steady mark's only
  other condition is the capture flag, never the mesh result), and the main hide loop `continue`s past a
  steady record for the rest of that instance's life. **So nothing ever retried the pairing.**
* When world partition finally loaded the rock, it loaded **visible**: of the 220 rocks that became
  pairable that session, **212 were still drawn at that moment** (`stillVisibleWhenResolved=212`), worst
  delay **1535 s**. The T4 watch sweep observes exactly this and, by design, only prints it.
* The one route left that could darken such a rock was the **stray-rock backstop** — the only
  player-distance gate in the whole hide path (300 m) on a 30 s cooldown. That bounds the visible window
  at 0–30 s after stream-in, which is where the ~10 s the author counted sits. **That last attribution
  step was HYPOTHESIS, not measurement**, because `route=backstop` was structurally unable to fire: the
  watch sweep removed a record in the same pass its rock became findable, before the backstop's
  attribution loop ran.

**A — WHAT THIS PACKET CHANGED (`NodeShuffleRockStreamInHide.cpp`, `NodeShuffleSubsystem.cpp`).**
`RebuildMeshActorCache` is the function that FIRST observes a newly-streamed mesh actor, and ApplyLayout
already calls it every pass **before** `SuppressOriginalNodes`. Both of its `MeshActorCache.Add` sites now
call `TryHideStreamedRockForSteadyOriginal`, which hides the rock when — and only when — the node is
hidden **and** a `SteadyHiddenOriginals` record resolves to that same instance. Order is
**capture → hide**, the author's ruling verbatim ("check if captured, capture if needed, then hide"); the
older in-file claim that hiding leaves the mesh data readable is an unmeasured comment and this packet
deliberately does not rest on it. *(Cold review F7 scoped the capture benefit: a late-streaming-donor
resource "can now capture at all" ONLY when it is non-solid or placeholder-only-terminal — an ordinary
solid with no donor never goes steady, so it was already re-funnelled every pass and still is.)* The forward-link sweep's iterator was widened from
`TActorIterator<AFGResourceNode>` to `AFGResourceNodeBase` (**the T43 family**) so originals deriving
directly from the base are pairable at all; fracking is still excluded by the same `IsFrackingActor` test,
and `AFGResourceDeposit` derives from `AFGResourceNode` so it was already in the old population.
This closes the window from 0–30 s to **≤1 pass tick**, adds no engine surface, and does not touch the
steady-set semantics. Pinned by `tools/check_t64_lint.ps1` (10 mutants, all caught).

**WHAT IT DOES NOT COVER, WITH ITS DENOMINATOR RATHER THAN A SILENCE.** A node-rock drawn by an
`UInstancedStaticMeshComponent` owned by something other than the hidden actor is outside **both** routes
— the backstop has always skipped instanced components (they are world-shared) and this route hides an
actor. They are now **counted**, not hidden: `ROCKHIDE-CENSUS` reports how many visible instanced
rock-named components near a player have an origin within 8 m of a processed original, and states that an
instanced component's origin is the component's transform, so **per-instance geometry is not measured**.

**B — `NodeShuffle.AuditPlacements` (`NodeShuffleAuditPlacements.cpp`), the population sibling of the
four nearest-one probe commands.** Walks every layout entry, including the ones it cannot judge (own
buckets, never dropped), and reports four readings per recorded centre as **separate** fields because
they are known to disagree (T41): the settle gate's own water signal, a below-terrain comparison against
that same probe's ground hit, the shipped 8-ray enclosure gate, and the deep TOTALLYINSIDE instrument.
Log-only and time-sliced. Its water probes are prevented from teaching the persistent water grid while it
runs (`bWaterGridLearningSuppressed`), so an audit cannot change what a later roll believes.

**STILL OPEN AFTER THIS PACKET:** the instanced residual above; whether the ≤1-pass-tick window is
short enough in play — the ROCKHIDE-CENSUS and `hiddenByStreamInRouteThisPass` fields exist to answer
that from a log rather than a stopwatch; and (cold review F6) the iterator widening also lets
`ClassifyOriginalUnderground` resolve a base-only original's own rock into its cave-roof ignore list,
where pre-T64 it was null — a **fix in direction**, but `CaveSeedsDone` is persisted and one-shot per
original, so an existing save now holds cave seeds derived under two different rules. If the author
wants that population re-derived under the new rule, bump the `CaveSeedsDone` version; left as-is, the
mixed population is accepted and this line is its record.

### T4. Ordinary-node mesh-hide latency is unmeasured
A hidden ordinary node's **rock stays visible for a while** after the node itself is hidden
— it will not highlight or mine, but you can still see it. Observed in the desert,
2026-08-08.

Node hiding is **not** the lag. The evidence is the always-on `Hide-originals funnel (first pass
this load):` line (`NodeShuffleSubsystem.cpp:4742`), which on the 2026-08-08 load reported
**`records=630`, `loaded=630`, `newlyHidden=630`** and a zero in the path-resolution field.
*Not quoted verbatim here* — an earlier revision of this entry presented a **splice of two
different log lines** as a quotation (it included `pathMissed`, which the `Hide-originals funnel`
format string does not emit; that field belongs to the gated `HIDEDIAG funnel` at `:4760`). Named
fields and their values, above, are what is attested; grep the line yourself for the rest.

**Field rename, 2026-08-08 (truthdiag-fixes).** `notStreamed` in that line is now printed as
**`pathUnresolved`** — nothing in it ever tested streaming; the counter is `DbgMissedPath`
("this record's path did not resolve to a live actor this pass"). Logs from before that build
carry the old token; grep for both. The separate diagnostics-gated `HIDEDIAG funnel` line
prints the *same* counter under a *third* name, `pathMissed`.

The load-bearing number is **`newlyHidden=630` equalling `records=630`** — a direct count of
hides performed — i.e. all hidden on pass one. The mesh actor is a separate object and **the
funnel reports nothing about it**. So we cannot currently distinguish "the mesh hides a pass or
two late" from "the mesh hides immediately and the render state catches up".

**Framing corrected 2026-08-08, and corrected again the same day.** The first revision treated
this line as a local footnote about one funnel. The second over-corrected and credited it with
falsifying this project's streaming model — **which is circular**: `records` is
`OriginalNodeRecord.Num()`, the originals *the roll itself captured*. Had the roll missed 200
nodes, the funnel would read `records=430 loaded=430` and look exactly as clean. This line
proves only that everything the roll saw was still live at hide time.

**What actually falsified the streaming model is elsewhere and is cited here so no one has to
re-find it:** `_team/nodeshuffle-followups/node-enumeration-investigation.md` — the per-resource
whole-map totals matching known counts (line 69: Uranium 8, Bauxite 23, SAM 23, Quartz 23,
Coal 82, Copper 72) and the 6-minute set-diff showing **zero** level-placed nodes arriving late
(line 76). With *that* as the premise, the funnel line's honest reading follows: **the hide pass
is not partial**, so any explanation of a visible-rock symptom reaching for "it had not streamed
in yet" is contradicted. One open question remains here, and it is a *mesh* question, not a
coverage question.

Pre-existing in the long-shipped ordinary-node path; **not** an H2b regression. Cosmetic
and self-resolving.

**BEHAVIOURAL HALF CONFIRMED BY THE USER 2026-08-08**, build `2026-08-08-t1t2-1`, after a re-roll.
Flying to coal originals near a `NodeShuffle.Here` marker: the rocks were **visible**, and **neither a
Mk8 nor a Mk3-class miner would snap to them**; they disappeared after a wait. So the *node* hide is
correct — the actor is out of `mResourceNodes`, which is exactly the guard that stops a player snapping
a miner onto a ghost original — and **only the mesh actor lags**. This upgrades the entry from "we
cannot distinguish a late mesh hide from a render-state catch-up" to: *the node is functionally gone
immediately; the visual is what persists.*

⚠ **The duration was UNMEASURED and the user's "30 seconds" was explicitly an estimate.** Then the
instrumentation review found the likely reason that estimate was *good*:
**`RockBackstopCooldownSeconds = 30.0f`** (`NodeShuffleSubsystem.cpp:4856`). The rocks go dark when the
**stray-rock backstop** sweeps, and that backstop runs on any pass that newly hid a node, else on a
30-second cooldown. It had already fired **30 times** in the user's session log
(`hid 0 original nodes and N stray original rocks`, N = 1..8). So the cooldown is an **upper bound this
mod imposes on itself** for how late a rock can go dark, and the user was very likely reading it off
the screen.
*Stated as the strongest available explanation, NOT as proven cause* — nothing has yet correlated an
individual rock's hide to an individual backstop sweep. That is what the new instrumentation measures.

**Newly understood mechanism candidate, NOT confirmed:** the `Hide-originals funnel` line is emitted
**once per load** (`(first pass this load)` — exactly one such line in the whole session log) and
covers records resident at that moment. A rock that streams in later, when the player arrives, was
never in that pass's population. What eventually hides it is unidentified. *Stated as a hypothesis on
purpose; it fits the evidence and has not been tested.*

**Player-facing consequence, small but real:** a player sees a node, flies to it, fails to build on it,
and concludes the mod is broken. Harmless, but it reads as a bug.

> **Pre-scoped fix:** instrument mesh-hide latency before attempting any behaviour change.
> There is nothing to fix until there is something to measure. Concretely: count rocks hidden on a
> **later** pass than their node, and report the delay, so the duration above stops being a stopwatch
> guess. Additive and diagnostics-only — but note this project's record that even additive counters
> ship with a defect when the zero has no denominator ([[lessons-zero-needs-a-denominator]]).

### ~~T5. `MT_Crack` capture counter is structurally blind~~ — FALSIFIED 2026-08-08
**This entry was wrong, and it was wrong in a way that nearly cost a packet.** It claimed
`CrackPieces` "can never be counted" on the own and spatial routes, so only route-2 groups report a
non-zero. That claim was used as evidence that `mNodeMeshType` is unreachable on the spatial route,
which would have sunk T2's pre-scoped fix before it was tried.

**Measured against `FactoryGame-backup-2026.08.08-15.53.04.log`, and the arithmetic is decisive
without needing any per-group route split:**

| group | pieces | MT_Crack |
|---|---|---|
| `BP_FrackingCore15` | 18 | **10** |
| `BP_FrackingCore17` | 17 | **9** |
| `BaseNode_FrackingCore1_0` / `2_1` / `_2` | 8 + 5 + 7 = **20** | 0 |
| every other group | 0 | 0 |

Per-pass route totals are `(20 own, 17 via engine link, 18 spatial)`. The three `BaseNode_` groups
hold exactly 20 pieces and report 0 cracks — matching `own` exactly — so **all 19 observed MT_Crack
pieces belong to the two groups fed only by link + spatial**. The link route can supply at most
**17**. `19 > 17`, so **at least two MT_Crack pieces were counted on the SPATIAL route**, and
`CrackPieces` increments only where `Cast<AFGNodeMeshActor>(C->GetOwner())` is non-null. The spatial
route therefore *does* reach an `AFGNodeMeshActor`. Confirmed independently of the T2 packet, which
reached the same conclusion by a different route.

> **How the entry went wrong, which is the reusable part.** `(0 of them MT_Crack)` really did appear
> "on nearly every line" — but on lines where **`pieces=0`**. A group that captured nothing reports
> zero cracks for the obvious reason. The observation was real; the inference that it revealed a
> *counter* defect never controlled for the denominator. **A ratio read off lines whose numerator is
> structurally zero is not evidence of anything.** Cousin of [[lessons-log-asserted-a-cause]]: not a
> log asserting a cause, but a *reader* inferring one from a statistic the log never supported.
>
> **What remains genuinely open is narrower:** the *own* route has never been observed producing a
> non-zero MT_Crack — but all 20 of its pieces are `BaseNode_` wells, which may simply have no crack
> meshes. Route 1's countability is **unproven in both directions**, not blind. Do not restate it as
> a defect without an own-route group that has cracks.

### T6. The spawn-time registry ships unvalidated
`VERDICT=OURS-PROVEN` is unreachable by any scripted test: pass B runs once per session at
~40 s, in the settled phase only. The registry is present and believed correct, but nothing
has exercised it.

> **Pre-scoped fix:** a `NodeShuffle.DumpWellBackstop` console command so the backstop can be
> run on demand instead of waiting for its one scheduled pass.

### T51. ~~The containment walk filtered its exclusions AFTER the trace, so an ignored actor ate the iteration budget~~ — **FIXED 2026-08-10, `6cf1c24`, marker `2026-08-10-t51-1`. This is the fix for [[T50]]; it did NOT fix the member readings — see [[T52]].**
**WHAT IT WAS.** [[T50]] measured the mechanism: the walk hit an excluded actor, discarded it, stepped
2 cm past, and hit the same actor again — 28 of 32 inbound and 31 of 32 outbound hits were the caller's
own `Char_Player_C`, both walks exhausted the 32-hit budget, and the verdict came out `CENTRE INSIDE`
for a point in open air.

**WHAT LANDED (`6cf1c24`).** The first hit on an excluded actor is handed to
`Params.AddIgnoredActor` — **one `FCollisionQueryParams` for the whole walk**
(`NodeShuffleCentreContainment.cpp:190`) — and the walk **re-traces from the SAME cursor without
stepping** (`:221`, `:341`). Cost is one budget iteration per **distinct** excluded actor per walk; the
commit message states a capsule drops from ~90 iterations to 1. The 32 budget now bounds *counted
crossings plus distinct excluded actors* rather than raw hits (`:500`). This is the mechanism
`IsSpotEnclosed` already used for the pawn ([[T36]]), not a new one.

**WHAT THE PACKET DELIBERATELY DID NOT DO, and it is the reason the next reading was interpretable**
(all from `6cf1c24`): the subject actor is **not** pre-ignored before the first trace, because that
would make `ExcludedSubject` structurally zero — and the exclusion counts *with their denominators* are
the instrument that found T50 ([[lessons-zero-needs-a-denominator]]); no `FGCliffActor` special case
(T50 established that correlation as a **symptom**); no change to the back-face rule; nothing tuned
against satellite 86. An anomaly guard was added — a hit on an already-ignored actor increments
`InboundIgnoredRehits`/`OutboundIgnoredRehits` (`NodeShuffleSubsystem.h:173`,
`NodeShuffleCentreContainment.cpp:152-158`), steps past as before, and is **printed with an expected
value of 0**, so the walk cannot stand still.

**THE VERDICT RULE IS UNCHANGED** — `6cf1c24` records a filtered diff over the containment file
returning nothing that touches entries, exits, the `FMath::Max`, the `>= 1` threshold, the
front/back classification, the counted-crossing step, the sky start, the cap, the channel, or the
exclusion predicates and their order; `IsSpotEnclosed`'s file is not in the modified set.

**A SECOND BEHAVIOUR CHANGE, disclosed by the packet rather than found later** (`6cf1c24`): the old
walk stepped 2 cm past every excluded hit and could therefore skip a surface lying within those 2 cm;
the new one cannot. **Counts may differ for that reason too, not only because of the budget** — do not
attribute a moved number to the budget alone.

**HOW WE KNOW IT WORKED, AND HOW FAR.** Measured on `2026-08-10-t51-1` over `BP_FrackingCore13`
(reported in `96a4cf3`): the [[T48]] overhang regression point reads **4 crossings in / 4 out, net 0,
NOT INSIDE, with a healthy budget**. **The member verdicts did not move** — which is [[T52]], and it
retires T50's "root cause of T48" claim.

### T52. The per-crossing detail was printed everywhere EXCEPT where the mechanism lives — and once it was moved there, budget starvation was ruled OUT as the members' cause. [[T50]]'s "root cause of [[T48]]" is TOO STRONG. **A second mechanism is live and UNMEASURED.**
**WHAT IT WAS (`96a4cf3`, marker `2026-08-10-t52-1`, diagnostics only).** T49 added the per-crossing
`SHADOWCROSS` detail and gated it to `Here` and `PointAtHere` to stop 7 members × 2 walks flooding the
screen. That was right for the question then and **became exactly backwards once [[T51]] landed**:
after the ignore-list fix, **the only points still reading `CENTRE INSIDE` are well-member locations**
(`NodeShuffleCentreShadow.h:31-40`, `NodeShuffleCentreShadowLog.cpp:113`) — the one command where the
detail was switched off. The instrument that would explain the remaining mechanism was disabled
precisely where the mechanism lives.

**WHAT LANDED.** The two meanings of one switch are now separate: the reading's own
`bCrossingDetailRequested` decides whether the walks **build** the list (`WellProbe` now asks for it on
every member) and a policy decides which built lists are **printed**
(`ECentreShadowDetailPolicy`, `NodeShuffleCentreShadow.h:39`, applied at
`NodeShuffleCentreShadowLog.cpp:120-123`). Shipped rule: a member prints its crossing list when the
candidate **would refuse it** OR **its positive control did not read inside** — through **one
predicate, `CentreShadowDetailSelected` (`NodeShuffleCentreShadow.h:54`,
`NodeShuffleCentreShadowLog.cpp:13`), read by both the emitter and `WellProbe`'s tally, so the group
count cannot disagree with the number of lists printed** (`96a4cf3`). The walk
itself is provably untouched: `96a4cf3` records `NodeShuffleCentreContainment.cpp` as **not appearing
in `git status` at all**.

**THE PACKET CONTRADICTED ITS OWN BRIEF AND WAS RIGHT TO** (`96a4cf3`): the brief predicted the rule
would select 4 members; **both limbs select all 7** — 1/2/4/7 read INSIDE and 3/5/6 have unproven
controls. Neither limb was narrowed; the group line prints the run's actual split.

**WHAT IT MEASURED — the finding.** On `2026-08-10-t51-1` over `BP_FrackingCore13`: the overhang
regression point is fixed (see [[T51]]), **but the member verdicts are unchanged — cliff above still
gives INSIDE for members 1, 2, 4 and 7.** Therefore **budget starvation was NOT the members' cause**,
and `96a4cf3` states T50's filing as "root cause of T48" is too strong: **it was the cause at the
player-standing point, where the pawn was in the trace path**, and nowhere else that has been measured.

**WHY IT IS NOT FIXED.** The state file's 2026-08-10 ~00:0x block, item 7, rules the whole approach
out rather than the instrument: *"T48/T50/T52 — the crossing-parity walk cannot work here: **zero back
faces have ever been reported**"*, so a node under one slab enters and exits through the **same**
surface and the exit is invisible (`a92c61b` states the same reason as the design premise for
[[T53]]). The project's answer was therefore to build a different instrument ([[T53]]), not to repair
this one. **The back-face question is filed as its own packet and is still open** (state file
2026-08-10 ~00:0x STILL OPEN, carried forward by the ~00:5x block). `96a4cf3` also declares what it
deliberately did **not** do: no fix to the second mechanism, no `FGCliffActor` special case, no change
to the back-face question, nothing tuned to satellite 86.

> **The second mechanism remains the open item here.** The crossing-parity walk is superseded in
> practice, not diagnosed. Anyone reopening it inherits an unmeasured cause for four INSIDE readings —
> **do not restate T50's root-cause line, and do not assume starvation.**

### T53. The `TOTALLY-INSIDE` detector — built as a trustworthy POSITIVE after three attempts at proving a negative failed. It over-fired on open ground, the 25 cm epsilon fixed that in game, and it still has **ZERO graded TRUE POSITIVES**. The first `ProbeNearestNode` at the lead node decides.
**WHY THIS SHAPE** (`a92c61b`, marker `2026-08-10-t53-1`; design rationale in
`NodeShuffleTotallyInside.h:1-41`). Three methods failed the same night and all three tried to prove a
**negative** — that a centre is *not* inside anything: crossing parity needs back faces and zero have
ever been reported here ([[T50]]/[[T52]]); a 1 cm sphere overlap cannot report containment in a
landscape heightfield; the shipped 8-ray gate has no vertical sampling ([[T41]]). The author's ruling
makes the asymmetry the right one — *"I'm ok with working on the center being on the edge later if we
can stop them being totally inside for now"* — so a **false accept is cheap and a false reject would
condemn terrain and cave placements the author asked to keep**. 14 directions from the point's own
location, each probed 500 cm by **four** instruments (outward line, inward line, inward sphere sweep
with its start-penetration flag, outer-point overlap); **all 14 must report solid** or the verdict is
withheld; which instrument carried each ray is printed. **It gates nothing** — `a92c61b` verifies
`NodeShuffleWellFootprint.cpp` and `NodeShuffleCentreContainment.cpp` are absent from its diff.

**THE OVER-FIRE DEFECT, MEASURED NOT INFERRED** (state file 2026-08-10 ~00:0x, §START HERE): the probe
origin **sits exactly ON a surface**, so all 14 rays — including straight up — registered a hit at
**0 cm**, naming `LandscapeHeightfieldCollisionComponent`. `RAY SOLID` **140 times**, `RAY CLEAR`
**zero**; plain open ground with sky above read `TOTALLY INSIDE`, and all 10 probed points came back
positive. *(The inverse of the `+200` eye-offset error, which was too large and lifted the probe out of
the rock; this one was zero and never left the ground.)*

**THE FIX AND ITS CORRECTED BASIS** (`a6c339b`, marker `2026-08-10-t53-2`). Every ray's line/sweep
segments now begin **`TIRayStartEpsilonCm` = 25 cm** from the tested point **along that ray's own
direction**; outer point (500 cm), outer overlap, centre overlap, the all-14 rule, the exclusions and
the retrace budget are unchanged, and every distance is still measured from the tested point. **The
constant is taken from the run's printed distances, as corrected by the cold review — use these
figures, not the earlier 74 cm one:** 226 of 250 landscape readings in the `t53-1` log lay **under
25 cm**; **7 lie at 28–73 cm and are NOT removed**; every non-landscape reading was `CliffMesh` at
**87 cm or more**. **The 74 cm figure in the pre-review draft was itself a landscape reading — the
reviewer falsified it.** `a6c339b` is explicit that 25 cm is **not a proven artefact boundary** — the
log cannot say which landscape reading came from the surface the point sat on — **it is where the bulk
of them stop.** The review also made line queries record their own start-penetration and print
`ALREADY PENETRATING`, because the down-family rays from a surface point start below it *by
construction* and such a hit would otherwise print as cover at exactly 25 cm.

**IN-GAME VERIFICATION** (state file 2026-08-10 ~00:5x, 17 verdicts read from the live log on marker
`t53-2`, predictions recorded before the run):
* **Open ground (`Here`): an escape was found, straight-up ray CLEAR, 10 of 14 clear.** The over-fire
  is gone.
* **Overhang `PointAtHere`: negative and NOT vacuously** — 12 of 14 solid through all four instrument
  classes; two clear diagonals withheld the verdict. The instrument is demonstrably alive.
* **All 10 of the previous night's positive points flipped negative, sat 81 included** — sat 81
  (member 2) now reads 10 of 14 CLEAR, so **its `TOTALLY INSIDE` was entirely the origin-on-surface
  artifact**. The state file records the author-facing reframing: **sat 81 is the DEFERRED
  centre-on-edge case, not the totally-inside case** — which is ruling 2 of the 2026-08-10 settled
  rulings, not a failed fix.
* Epsilon printed `begins 25 cm` on all 17 readings; zero unflagged `met solid 25 cm out` lines; no
  distance under 20 cm anywhere.
* **UNMET — the positive control never fired.** `TOTALLY INSIDE` printed nowhere. The author aimed at a
  thick cliff, but `PointAtHere` probes the aim trace's **impact** point, which is by construction the
  cliff **surface**, so the command **structurally cannot probe inside solid**. "It can still say
  TOTALLY INSIDE" is therefore **evidenced but not proven**.

**THE TWO FIELD FINDS THAT MOTIVATED THE NEXT COMMAND** (`NodeShuffleNodeProbe.cpp:4-10`, stated there
as what was measured rather than as theory): **two of our placed nodes — lithium (`Desc_OreLithium_C`)
and lead (`oreleaddesc_C`, actor `BP_ResourceNode_C_2147425393`) — sit almost entirely inside rock**;
and **no command could probe a solid node's CENTRE** — `NodeShuffle.Here` probes where the player
stands, `NodeShuffle.PointAtHere` probes wherever the aim trace lands, which on the author's own
attempt was a cliff 5.4 m away and then a rock mesh. **The centre is the point that decides the defect
— it is where an extractor snaps — and it had never been probed on this population.**

**CURRENT STATUS — CONSUMED, NOT GRADED.** `ee01d38` wires the instrument into two callers:
`NodeShuffle.ProbeNearestNode` (`NodeShuffleNodeProbe.cpp`, the solid-node sibling of `WellProbe`, no
resource-form filter so lithium stays in population) and the **shadow centre gate** at both final
accepted placement spots (`NodeShuffleInsideGate.cpp`) — `WOULD-REFUSE` logging always, refusal behind
`NodeShuffle.InsideGateRefuse`, **default 0**. `NodeShuffleInsideGate.cpp:9-15` states the grading
plainly: **one-sided — NEGATIVE on everything the author wants kept (caves, satellite 81, open ground)
and with ZERO graded TRUE POSITIVES** — and that an instrument with no graded positive cannot be
allowed to refuse a placement, because the first thing it refused would be unfalsifiable.

> **THE NEXT READING IS THE ONE THAT DECIDES IT** (`_team/nodeshuffle-followups/T54-implementation-handoff.md:59`,
> restated as step 6 of the cold review's checklist, `t54-coldreview.md:11`): stand near the lead node
> (`oreleaddesc_C` / `BP_ResourceNode_C_2147425393`) and run `NodeShuffle.ProbeNearestNode`, then
> repeat at the lithium node. **That reading is either this instrument's first genuine true positive or
> evidence against the 25 cm epsilon.** Held fallbacks, from the ~00:5x block: a per-instrument epsilon
> (outward line back to the point) if one instrument class goes silent everywhere; 40–60 cm if the 7
> landscape readings at 28–73 cm keep carrying rays. **Do not flip `InsideGateRefuse` before that
> positive exists.**

---

## P4 — structural

### T7. Source files breaching the 500-line rule — **the four well files are SPLIT and under 500 (2026-08-08)**

**Done for the well subsystem.** The split landed once its deferral reason expired (six review reports
had been citing `file:line` in these files; those reviews are now committed and historical).

| file | before → after |
|---|---|
| `NodeShuffleWellVisuals.cpp` | 658 → **248** (capture + origin-side hide) |
| `NodeShuffleWellMeshIndex.cpp` *(new)* | — → **480** (radius constant, `EnsureWellMeshIndex`, `RebuildWellMeshIndex`) |
| `NodeShuffleWellVisualsApply.cpp` | 549 → **243** (dump + dress + apply) |
| `NodeShuffleWellSnapBox.cpp` *(new)* | — → **399** (collision recipe, snap box, promoted T3 state) |

Also landed: the two module-static accessor blocks were **promoted to members** (they only ever existed
because the header was barred to the packet that wrote them), `NodeShuffleWellSnapBoxDiag` gained the
world-change reset its sibling already had, and `WellVisualCaptureLogged` got its key-family table.

**Verified by measurement, not by eye:** each moved region was `git show`n from HEAD and `diff -u`'d
against its new home — six functions byte-identical. Build clean. **Import set SET-IDENTICAL** to the
pre-split baseline (816/766, 0 missing), which was the packet's prediction and is the check that
matters, since a split can shift inlining and therefore the import table.

**Still open:** `NodeShuffleSubsystem.cpp` remains **8069 lines** and was deliberately out of scope —
it is a much larger job than the well files and needs its own packet and its own seam analysis.

> **One deviation from the proposed seam, and it was right.** The prior handoff said
> `DumpWellActorCollision` should move to the snap-box file. It cannot: it is an anonymous-namespace
> static whose only caller (`ApplyWellGroupVisuals`) stays behind, so moving it yields an unreferenced
> static in one file and an undefined symbol in the other. The packet reported this instead of
> following the spec into a link error — the intended behaviour when a spec meets the tree and loses.

### ~~T7 (previous revision). Source files breaching the 500-line rule — nine files~~
**Updated 2026-08-08.** The instrumentation packet pushed two more files over, and reported it rather
than trimming diagnostics to buy headroom — the right call, recorded so it is not mistaken for drift:
`NodeShuffleWellVisuals.cpp` 497 → **658**, `NodeShuffleWellVisualsApply.cpp` 432 → **549**.
`NodeShuffleSubsystem.cpp` reached **8076**.

**The splits were deferred deliberately and the reason has now expired.** Through the H2/T1/T2 arc,
splitting mid-review would have re-staled every `file:line` a reviewer had just verified — six review
reports now cite these files. **That constraint is gone once the current round is committed**, so the
split is the natural next packet, and it should happen *before* any further additions.

Proposed seams, both confirmed sound by review: move `WellMeshOwnerRadiusCm` / `EnsureWellMeshIndex` /
`RebuildWellMeshIndex` out of `NodeShuffleWellVisuals.cpp` (residual ≈ 287); the `WellVisualsApply`
seam is in the instrumentation handoff. **Ask the splitting packet for two things:** a key-family table
on `WellVisualCaptureLogged`'s declaration (it now spans three TUs with six key families), and
promotion of the two module-static accessor blocks to members — they exist only because
`NodeShuffleSubsystem.h` was barred to avoid a collision, and one of them
(`NodeShuffleWellSnapBoxDiag`) has **no world-change reset** where its sibling does.

### ~~T7 (original entry). Seven source files breach the 500-line rule~~
Splits **proposed, not performed** — deferred deliberately during the H2 arc because
splitting mid-review re-stales every `file:line` reference a reviewer just verified.

Known: `NodeShuffleWellRelocateApply.cpp` 567 · `NodeShuffleWellVisuals.cpp` 497 (three from
breaching) · `NodeShuffleWellRelocateRoll.cpp` 519 · `NodeShuffleWellClaim.cpp` 579 ·
`RelocateApply.cpp` 504. Proposed seam for the largest: `SuppressVanillaWellGroup` →
`NodeShuffleWellSuppress.cpp`.

### T8. `EnsureNodeUseBox` is typed `AFGResourceNode*`
**This blind spot caused three separate bugs during the H2 arc** — the mesh-lookup failure,
the suppression failure, and the snap failure. `AFGResourceNodeFrackingCore` derives from
`AFGResourceNodeBase`, so anything typed `AFGResourceNode*` **silently excludes fracking
cores at compile time**. `AttachIdentityOnly` was widened to `AActor*` for exactly this
reason; `EnsureNodeUseBox` has not been.

> **Pre-scoped fix:** re-type to `AFGResourceNodeBase*`. Round 13 called this the right
> long-term move but out of scope for a fix packet.
> **Heuristic worth keeping: when a fracking bug looks impossible, check whether the API
> you are using excludes `…Base` before looking anywhere else.**

---

## Unmeasurable on this machine

### T9. `bPlacementClaimLive` save-round-trip (A3-5) is UNMEASURED
Whether the `UPROPERTY(SaveGame)` field survives cook/serialization has never been observed.
It cannot be tested here: it requires a save written by build h2-8 or later, and every
candidate save on this machine provably predates the field's existence (verified by
timestamp against `b888b2f`, 2026-07-31 20:25).

Not "assumed working" — **unmeasured**, which is different. Recipe for manufacturing a
suitable save is in `_team/nodeshuffle-followups/H2-blockers-handoff.md` §W2.

---

## Never run

### T10. Bookkeeping test steps T-2 and T-3
The D-2 expiry counter and the stranded-actor class were scripted, amended across three
review rounds, and then **deferred** so the buildability test could run first. They remain
unrun. They are log-only measurements — no build required, ~25 minutes in game.

### T11. SaveGame-GUID identity component
Still gated on RT-6, which has not run. Round 9 established that it and `bPlacementClaimLive`
are **different layers** — entry-owns-coordinate vs actor-identity — and that neither
subsumes the other, so A3 shipping does not retire this.

### T31. The last-resort visual template is ONE GLOBAL PAIR, first-capture-wins — so a fallback well can wear ANOTHER RESOURCE'S ROCKS. The code comment calls this a biome problem; the biome half is nearly harmless and the resource half is not.
**Found 2026-08-09 while measuring whether T30 (invented core groups) could ship. Measured from the
pak index, 18,094 anchored runtime observations across 8 sessions, and the source — not inferred.
Triggered by the AUTHOR contradicting the existing comment from in-game observation:** *"I'm not sure
the look is different between biomes, it still looks like a piece of rock in all that I saw."*
**They were right about the mesh, and the investigation that confirmed them found something worse.**

**The mechanism.** `NodeShuffleWellVisuals.cpp:174-176` selects between exactly two session members —
`WellVisualTemplateCore` and `WellVisualTemplateSatellite` (`NodeShuffleSubsystem.h:2140-2141`) — and
fills each with `if (Template.Num() == 0) { Template = Out; }`. So:
* there is **one template per KIND for the whole session**, not one per resource and not one per biome;
* it is **first-capture-wins** — whatever well happened to be dressed first owns the look; and
* it is **never refreshed**, so a later, better-matched capture cannot replace it.

**Why that is worse than the comment says.** The comment points at *"the wrong biome's rock"*.
Measured: **the biome axis barely exists on the MESH.** The pak index holds exactly two resource
families of three pieces — `SM_FrackingNode_{Crack,Mid,Small}_01` and
`SM_Nitrogen_Node_{Crack,Mid,Small}` — and **no desert node mesh exists at all**; all 40 `SM_*Desert*`
assets in the game are foliage, boulders and coral. At runtime, 2 distinct mesh names over **18,094**
observations, and the *same* mesh appears under both `nodeMeshType=2` (MT_Crack) and `=5`
(MT_DesertCrack) — 1,958 desert-typed observations carrying the identical mesh as 13,605
grassland-typed ones. **The author's in-game read is confirmed on mesh.**

**The RESOURCE axis is the real one, and nobody had named it.** `SM_Nitrogen_Node_*` and
`SM_FrackingNode_*` are genuinely different mesh sets. Because the template is global and
first-capture-wins, **a session whose first captured well is nitrogen dresses every later fallback
well — water, oil — in nitrogen rocks.** This is not hypothetical: a companion measurement found
**7 of 19 relocated groups (37%) never capture their own origin in ANY observation across 8 sessions**
and ride the fallback permanently.

**Grades, honestly.**
* One global first-capture-wins template per kind — **provably provided** (source above, 0 other writers).
* Two mesh families exist and differ — **measured** (pak index + runtime census).
* No desert MESH variant exists — **measured**.
* A desert MATERIAL split DOES exist (5 `MI_*Desert*` well instances paired against non-desert twins,
  plus `TX_DesertRock_Cracked_01`) — **measured that the assets exist**; whether a desert-typed
  instance actually *resolves* to the desert MI is **UNMEASURED**.
* **Whether a player NOTICES a family mismatch — UNMEASURED, and it is an art judgment, not ours.**
  The scoped render that would settle the material half is `MI_FrackingNodes_Water_01` vs
  `..._Desert_Water_01` on the same mesh. Mesh comparison is closed by the asset list; do not re-run it.

**Do not "fix" this by making the template per-resource until someone decides it is worth fixing** —
the fallback exists precisely so a member is never left invisible-and-unbuildable, and a per-resource
template makes an empty-template outcome MORE likely for rare resources, which is the strictly worse
failure. That trade is the decision, and it belongs to the author.

**What this does to T30.** An invented group has no origin, ever, so it rides this template 100% of the
time. The cosmetic cost of T30 is therefore **not** "wrong biome tint" — it is **"may wear another
resource's rocks"**, and it is bounded by whatever this entry is worth fixing. Full working:
`_team/nodeshuffle-followups/T30-lookdiff.md`.

### T32. RELOCATED WELL GROUPS END SESSIONS UNDRESSED — 11 of 79 core-instances, in 5 of 8 sessions, were still fully no-visual at their last observation. This ships TODAY; it is not a T30 risk.
**Found 2026-08-09 by re-running an earlier measurement whose METHOD produced the answer.
This is the POPULATION lens, and it is the eleventh sighting in this workspace: the arithmetic was
right, the SET was wrong, and every gate passed.**

**THE NUMBERS, each with its denominator, from 10 rotated logs / 8 sessions with hits on 2026-08-09:**
* **11 of 79 core-instances still fully no-visual at their LAST observation, across 5 of 8 sessions**
  (13 of 81 if counted per placement rather than per core).
* **5 of those 11 were fully no-visual at EVERY observation in their session — never observed dressed
  at all**, the worst running **19 minutes / 203 apply passes**.
* Rate overall: **17 of 88 observations** carried ≥1 no-visual member; **133 of 672 member-chances**.
  Every one of the 17 was whole-group (`NoVisual == Members`, `Pieces=0`, `FromTemplate=0`) — groups
  fail to dress entirely, never partially.
* Cases that DID resolve in place (n=4) took **125 / 125 / 130 / 135 s — 21 to 27 apply passes.** Two
  apparent 1010–1030 s "resolves" were mid-session re-rolls to a new placement, not resolves, and are
  excluded.

**HOW THE PRIOR MEASUREMENT GOT 0.** `T30-killtest.md` reported **0 of 146 fatal** and was reproduced
exactly — but only by deduping on core NAME **across all 8 sessions**, merging different saves and
different rolls into one population. Per session the population is **79 group-instances / 602
member-chances**, and **the prior's own best-state aggregator applied WITHIN a session yields 85 of
602 no-visual, not 0 of 146.** **The cross-session merge alone produced the zero.** A group that was
undressed for a whole session scored as clean because a same-named core in a *different save* had
been dressed. Nothing about the arithmetic was wrong.

**THE UNDER-COUNT IS ON THE SAFE SIDE, WHICH MAKES IT WORSE.** The tag throttles on
`CorePath|Members|Pieces` and fires only on change, so absence of a later line means "no change
observed", and a 0 → N → 0 sequence would not re-emit the trailing 0. **§1 and §3 are therefore
lower bounds.**

**Grades, honestly.**
* The counts above — **measured**, and independently reproduced against the prior run's own figures.
* **WHY any group failed to dress — UNTESTED.** Streaming, distance, capture order and a bug are all
  live candidates and this log distinguishes none of them. **Do not write a cause into a fix.**
* **Whether "no visual" also means UNBUILDABLE — NOT MEASURED HERE.** `NodeShuffleWellVisualsApply.cpp`
  documents the empty-template outcome as invisible *and* unbuildable; that is the code's claim, and
  this measurement only establishes the invisible half. **If it holds, a player has wells they cannot
  build on, in 5 of 8 sessions.** Settling it costs one in-game attempt on an undressed core.
* The log **cannot distinguish "no visual" from "not yet dressed"** — `NoVisual` is a true "no mesh at
  this instant" and carries no information about whether one ever arrives.

**WHAT TO DO, IN ORDER.** (1) Confirm or refute the unbuildable half in game — one attempt, and it
decides whether this is cosmetic or a stranded-resource bug. (2) Instrument the capture path so a
group that never dresses SAYS SO with a denominator; today the only evidence is the absence of a
throttled line, which is exactly the [[lessons-zero-needs-a-denominator]] shape. (3) Only then fix.

**WHAT THIS DOES TO T30.** It reorders the queue. An invented group rides the fallback 100% of the
time, so T30 cannot be safer than this path is — **but T30 is no longer the reason to care.** Fix this
first; T30's dressing question then answers itself. See also [[T31]] (the template is one global
first-capture-wins pair). Full working: `_team/nodeshuffle-followups/T30-recount.md`.

### ⚠ CORRECTION TO T31 AND T32 — THE AUTHOR WAS RIGHT: THE LOG POPULATION IS STALE. Both entries' runtime figures describe builds that no longer exist.
**Filed 2026-08-09, within the hour, after the author said: *"I haven't played in a bit so I don't know
if the log is representative of changes made since then."* They were right, and it is worse than
"some staleness" — MEASURED by reading the boot marker out of each log:**

| build marker | anchored `WELLH2B-APPLY` lines |
|---|---|
| `2026-08-08-t7split-1` | 36 |
| `2026-08-08-t17-t7b-1` | 36 |
| `2026-08-09-t23s0-3` | 2 |
| `2026-08-09-t23hide-4` | 3 |
| `2026-08-09-t24-2` | 11 |
| **`2026-08-09-t27-2` / `-t27-3` (the deployed build)** | **0** |

* **NOT ONE of the 88 observations comes from the deployed binary.**
* **72 of 88 (82%) predate T23, T24, T26 and T27 entirely** — they are 2026-08-08 builds, before the
  enclosure gate, before core-first placement with independent satellites, and before the group-scoped
  occupancy gate.
* The live `FactoryGame.log` is on `-t24-2` and has **0** apply lines.

**WHAT THIS RETRACTS.** T32's headline — *"11 of 79 core-instances still no-visual at their last
observation ... this ships TODAY"* — **is not supported.** The counts are real for the builds that
produced them; the words "ships today" are not. T31's runtime frequency claim (*"7 of 19 groups never
capture their own origin"*) inherits the same contamination, since it comes from the same population.

**WHAT SURVIVES, and why.**
* **T31's MECHANISM stands** — one global first-capture-wins template per kind, never keyed by
  resource, never refreshed. That is read from CURRENT source (`NodeShuffleWellVisuals.cpp:174-176`,
  `NodeShuffleSubsystem.h:2140-2141`), not from logs.
* **T31's ASSET findings stand** — two mesh families, no desert node mesh anywhere. That is the pak
  index, which is the game's, not ours, and is build-independent.
* **T32's METHOD finding stands and is the durable part** — the prior zero came from deduping across
  sessions. That critique is about arithmetic on a population, not about which build produced it.

**THE ACTUAL LESSON, AND IT IS THE THIRD SIGHTING IN ONE HOUR.** POPULATION again. The first run merged
different SAVES and ROLLS; the recount fixed that and merged different BUILDS; **neither of us checked
the boot marker, which is one grep and is printed in every log for exactly this purpose.** Every gate
we have passed on both runs. The author caught it from memory of when they last played.
**RULE, and it goes in the brief for every future log measurement: SLICE BY BUILD MARKER FIRST, print
the per-marker denominators, and state explicitly how many observations come from the build under
discussion. A count that spans builds is not a count of anything.**

**WHAT WOULD ACTUALLY MEASURE THIS.** One session on `-t27-3` with diagnostics ON, flying past a few
relocated wells. Until then T31's frequency and all of T32 are **UNMEASURED ON THE CURRENT BUILD**, and
neither may be cited as a live defect rate. The unbuildable question is unaffected and still worth one
in-game attempt on any undressed core.

### T34. A relocated well core can land far enough inside a rock that normal placement refuses — and the snap-mode toggle (H) then lets you build inside the rock anyway.
**Reported in game by the author 2026-08-09 on build `2026-08-09-t27-3`, with screenshots. Author's
call: ACCEPTABLE FOR NOW — filed, not scheduled.**

Observed: at a water well group, the core sat far enough inside a rock formation that the Resource
Well Extractor hologram refused normal placement. Pressing **H** (snap mode) turned the hologram blue
and placement was allowed **inside the rock**. The author also found **only 3 wells in the group they
could place on**, and saw the game offer placement on a well that was itself inside a rock — *"that
won't help a player."*

**What is measured and what is not.**
* The behaviour is **observed in game**, twice, with screenshots. That is the evidence.
* **Whether the snap-mode path bypasses a check our relocation relies on is UNTESTED.** H is a vanilla
  build-gun affordance; it is not ours. Do not assume the mod caused it.
* **Whether a vanilla (un-relocated) well behaves identically is UNTESTED and is the FIRST thing to
  check** — if vanilla wells also allow this, it is not our defect and this entry closes.

**Why it is not merely cosmetic.** A well the player can only reach by defeating the placement refusal
is, for practical purposes, a well they cannot use — the same class as T15's permanent, log-invisible
shrink. The group above yielded 3 usable members out of its full count.

**Do not "fix" this by blocking the snap-mode path.** That is vanilla behaviour and blocking it would
take a legitimate affordance away from players. The fix, if one is wanted, belongs at PLACEMENT time —
do not deal a core where its members are unreachable — which is [[T35]]'s territory.

### T35. THE ENCLOSURE GATE HAS NEVER REJECTED ANYTHING — 0 rejects across 2,961 census lines, on both the core and satellite sides — while the author stands in front of the exact defect it was built to prevent.
**Measured 2026-08-09 from the live session on `2026-08-09-t27-3`, the first log this build ever
produced. This is a ZERO WITHOUT A DENOMINATOR, and it is the gate T26/T27 exist to provide.**

```
core side:      enclosed:0  x2961 census lines
satellite side: enclosed:0  x2961 census lines
```

**The zero is almost certainly NOT "nothing was enclosed".** In the same session **2,032 of 2,042
attempts ended `DEFERRED-void`**, terminated on the core side with `void:1` — and **void is the FIRST
gate while enclosure is the SIXTH**. So the overwhelming majority of probes die before the enclosure
test is ever reached. **The census reports how often the gate REJECTED and never how often it RAN**,
so today's `enclosed:0` is indistinguishable from a gate that never executed.

**THE INSTRUMENTATION THIS NEEDS, and it is the whole first step.** Add a *gate-reached* counter beside
each *gate-rejected* counter, so every rejection bucket prints `rejected of reached`. Without it no
future session can tell a working gate from an unreachable one, and this entry will recur verbatim.
`[[lessons-zero-needs-a-denominator]]`, fourth sighting in this file family.

**THE AUTHOR'S HYPOTHESIS, which this entry exists to test** — *"we may find that we are not doing the
same checks as we do for solids, or that solids are having the same issue."* Both halves are open:
* T26 made `IsSpotEnclosed` shared, and the node path DELEGATES to it, so the two paths cannot hold
  different *code*. **That is not the same as running it at the same rate**, and rate is exactly what
  is unmeasured. **SYMMETRY, and the measurement must cover BOTH sides** — count gate-reached on the
  solid path too, or this answers half the question.
* The predicate itself is **8 horizontal rays at 500 cm from Z+200, rejecting at 7 of 8 blocked**. By
  construction that **cannot detect a bowl wider than 5 m, a ledge under an overhang, or a spire top** —
  and a core sitting inside a large rock formation, which is what the author is looking at, may be
  exactly the shape it cannot see. **Whether the gate PASSED that core or never REACHED it is the
  question**, and the counter above is what answers it.

**Do not tune the predicate before the counter lands.** Widening the ray count or radius against an
unmeasured baseline is how a gate gets tuned to satisfy the last screenshot. See [[T34]] for the
in-game symptom and [[T15]] for the silent-shrink class this belongs to.

### T36. `IsSpotEnclosed` IGNORES NOTHING — a PAWN standing near a candidate counts as blocking rays, and the shared predicate is used by BOTH the well and solid paths.
**MEASURED in game 2026-08-09 on `2026-08-09-t35-1`. The author stood on relocated core
`BP_FrackingCore13` (1 m away) and ran `NodeShuffle.Here`:**
```
enclosure ray 1..8 of 8: BLOCKED at 0 cm by Char_Player_C_2147475525
verdict: 8 of 8 rays blocked ... ENCLOSED (a placement here would be refused by this gate)
```
**All eight rays were blocked by the AUTHOR'S OWN CHARACTER, at 0 cm, and the line asserted the spot
was enclosed.** The trace originates inside the player's capsule, hence the zero distance.

**The cause is in the predicate, not the diagnostic** (`NodeShuffleWellFootprint.cpp`, in
`ANodeShuffleSubsystem::IsSpotEnclosed`):
```
FCollisionQueryParams EncParams(FName(TEXT("NodeShuffleEnclosure")), false);   // ignores NOTHING
World->LineTraceSingleByChannel(EncHit, Eye, To, ECC_WorldStatic, EncParams);
```
No actor is ever added to the ignore list. A character blocks `ECC_WorldStatic`, so it is a hit.

**GRADES.**
* Eight rays blocked by the player, at that spot, on that build — **measured**.
* The query params ignore nothing and the channel is `ECC_WorldStatic` — **measured from source**.
* **That a pawn near a PLACEMENT candidate would likewise block rays — reasoned from those two, NOT
  observed**, because [[T35]] establishes the gate has never been reached in placement. It is the same
  function with the same params and no caller-supplied ignore list, so the inference is strong, but it
  is an inference and must be tested, not assumed.
* **Whether a creature (not the player) blocks `ECC_WorldStatic` — UNTESTED.** Do not generalise from
  one `Char_Player_C` sighting to all pawns without checking; that is the POPULATION error this
  workspace keeps repeating.

**TWO SEPARATE FIXES, and they must not be conflated.**
1. **The diagnostic is currently unusable for its only purpose.** `Here` tests where the player stands,
   so measuring the spot under a core REQUIRES standing on it, which guarantees 8/8 blocked. This is a
   catch-22 and the reading can never be obtained as shipped. **Fix: the Here probe must ignore the
   calling player.** Low risk, diagnostics-only.
2. **Whether PLACEMENT should ignore pawns is a BEHAVIOUR change to a gate** and owes a differential
   review. The argument for is strong — a lizard doe wandering past a candidate should not make a spot
   permanently "enclosed", and the gate refuses at 7 of 8 so two or three pawns could do it. The
   argument for caution is that this predicate is SHARED with the ordinary-node path (T26), so a change
   moves both populations at once. **SYMMETRY: whatever is decided applies to both, by construction.**

**WHY IT HAS NOT BITTEN YET.** [[T35]]: the enclosure gate has been reached **zero** times on either
path this session, so this defect is latent. **It becomes live the moment the void/settle gate stops
dominating** — which is exactly what fixing T35 is meant to achieve. **Fix T36 before T35's cause, or
the first thing the newly-reachable gate does is refuse spots because a creature walked past.**

**THE DIAGNOSTIC EARNED ITS KEEP.** This was found only because the ray line prints the HIT ACTOR
rather than a bare blocked count. A `blocked 8 of 8` line would have read as a correct, damning
measurement of the terrain. **Print what you hit, not just that you hit.**

### T37. THE ENCLOSURE GATE IS WORKING CORRECTLY AND IS ANSWERING THE WRONG QUESTION. It asks "am I in a pit?" (7 of 8 rays); the player's constraint is "does the extractor footprint fit?". A core in a nook against a cliff blocks 5, PASSES, and is unbuildable.
**MEASURED in game 2026-08-09 on `2026-08-09-t36-1`, standing on relocated core `BP_FrackingCore13`
(2 m), with the pawn excluded from the trace. This is the first VALID enclosure reading ever taken.**
```
ray 1 (0 deg):   BLOCKED at 499 cm by LandscapeStreamingProxy_...508_3_4_0
ray 2 (45 deg):  BLOCKED at 278 cm by FGCliffActor_1637
ray 3 (90 deg):  BLOCKED at 271 cm by FGCliffActor_1637
ray 4 (135 deg): BLOCKED at 268 cm by FGCliffActor_1637
ray 5 (180 deg): clear      ray 6 (225 deg): clear      ray 7 (270 deg): clear
ray 8 (315 deg): BLOCKED at 358 cm by LandscapeStreamingProxy_...508_3_4_0
verdict: 5 of 8 blocked; refuses at 7 or more; NOT ENCLOSED
```
**The reading passes both validity tests** the T36 review demanded: every distance is non-zero
(268–499 cm) and **two distinct actors** are named, so this is terrain and not a trace originating
inside a body. Ground slope 26.6 deg, and the cliff gate accepts to 60 deg, so that gate passes too.

**A PREDICTION WAS PUT ON RECORD BEFORE THE MEASUREMENT AND IT RESOLVED AGAINST THE FIRST BRANCH.**
Stated: *">=7 blocked ⇒ the gate would have refused and the bug is upstream (T35, it never ran);
3–5 blocked ⇒ the gate deliberately passed and we are testing the wrong property."* **Result: 5.**
The predicate is **not broken, not blind, and not mis-thresholded by accident** — it saw a cliff on
five sides and passed by design.

**SO THE DEFECT IS THE QUESTION, NOT THE ANSWER.** 7-of-8 detects near-total surround: a pit, a hole,
a crevice. The author's actual constraint is whether a **Resource Well Extractor's footprint** fits —
and a core pressed into a nook blocks five rays, passes, and still cannot be built on without the
vanilla snap-mode override ([[T34]]). The author reported **3 usable members** in that group.

**DO NOT FIX THIS BY LOWERING THE THRESHOLD.** 5-of-8 would reject any spot with a single wall behind
it, which is most of the map's interesting terrain, and it would move the ordinary-node path too —
`IsSpotEnclosed` is shared since T26, so **any threshold change is a SYMMETRY change to both
populations at once**. The missing test is **terrain clearance at the building's footprint radius**,
which no current gate performs: `buildableOverlap` tests BUILDINGS, `nodeOverlap` tests NODES, and
neither tests a cliff or landscape inside the footprint.

**GRADES.** The ray pattern, distances, actors, verdict and slope — **measured**. That the extractor
footprint is what refuses — **the author's in-game observation** ([[T34]]), not measured by this mod.
**The footprint radius the game actually requires is UNMEASURED**, and it is the number any fix needs
first. **Whether the ordinary-node path has the same gap — UNTESTED**, and [[T35]] shows its enclosure
gate has also never been reached, so nobody has evidence either way. **Ask that question before
building anything.**

### ★ AUTHOR RULING 2026-08-09 on T34 / T37 — PARTIAL EMBEDDING IS WANTED. The footprint-fit gate is REJECTED. The target is a member FULLY INSIDE a rock.
**The author, verbatim:** *"I like that nodes are partially in something, thinks it adds character. A
smart person uses H to place on it. However, it's the well inside that rock outcrop that we need to
figure out."*

**WHAT THIS SETTLES — do not re-propose any of it.**
1. **A member partly embedded in terrain is CORRECT BEHAVIOUR, not a defect.** The measured core
   `BP_FrackingCore13` at **5 of 8 rays blocked** ([[T37]]) is a spot the author WANTS. The gate passing
   it was right.
2. **T37's proposed footprint-fit gate is REJECTED.** Do not build it. Do not lower the 7-of-8
   threshold — [[T37]] already showed 5-of-8 would reject most interesting terrain, and the author has
   now confirmed those spots are desirable.
3. **The extractor's required footprint radius NO LONGER NEEDS MEASURING.** It was only needed to size
   the rejected gate. Question withdrawn — the author identified this themselves.
4. **T34 is downgraded, not closed.** Using vanilla snap-mode (H) to place on a partly-embedded member
   is the intended player experience, not a workaround. What remains open in T34 is only the count of
   members a player could not use at all.

**WHAT IS ACTUALLY OPEN, AND IT IS NARROWER AND CHEAPER.** A member **fully inside** a rock outcrop —
not touching it, not partly in it — is unusable by any means, including H. **That is the only case to
detect.** And it may need no new predicate at all: `IsSpotEnclosed`'s 7-of-8 threshold is a reasonable
test for *fully inside*, and [[T35]] shows the gate **has never once been reached** on either path. So
the plausible outcome is that **the existing gate is correct for the case the author cares about and
the entire fix is making it RUN** — which is [[T35]], already open, rather than new machinery.

**THE MEASUREMENT THAT DECIDES IT IS NOT YET POSSIBLE.** `NodeShuffle.Here` probes where the player
STANDS, and nobody can stand inside a rock. The buried member in the author's screenshots has never
been measured. **This is what `NodeShuffle.PointAtHere` is for** (author-requested, alongside `Here`,
not replacing it): aim at the spot, trace from the camera, run the same predicate at the hit point.
**If the buried member reads 7–8 of 8 by terrain actors, the predicate is already right and T35 is the
whole job. If it reads 5 or fewer, the predicate cannot see the case the author cares about and a new
one is justified.** Do not build any fix before that reading.

**A NOTE ON HOW THIS RULING AROSE, because it saved a packet.** The prior entry proposed a footprint
gate off a correct measurement and a correct inference. It was the AUTHOR's taste — *partial embedding
is character* — that made it wrong. **A measurement can establish what IS and never what is WANTED;
this workspace has now had the author overturn a well-evidenced direction twice in one session.**

### T40. `IsSpotEnclosed` COUNTS THE PLAYER'S OWN BUILDINGS AS ENCLOSURE. 24 of the 28 blocked rays in the first full group probe were `Build_FrackingExtractor_C` — 3 of 7 members read REFUSED purely because the author had built extractors on them.
**MEASURED in game 2026-08-09 on `2026-08-09-t39-1`, `NodeShuffle.WellProbe` over group
`BP_FrackingCore13`. 7 of 7 members probed, 7 resolved from live spawned actor transforms, 0
unresolved, 0 with zero rays cast. The saved record agreed with the live actor to 0–1 cm on every
member, so the probed points are not stale.**

| member | blocked | eye inside solid | blocked by |
|---|---|---|---|
| core `BP_FrackingCore13` | 1 of 8 | NO | landscape at 399 cm |
| sat 81 | 0 of 8 | NO | — |
| **sat 82** | **8 of 8** | **YES** | **`Build_FrackingExtractor_C`, all 8 at 0 cm** |
| sat 83 | 0 of 8 | NO | — |
| **sat 84** | **8 of 8** | **YES** | **`Build_FrackingExtractor_C`, all 8 at 0 cm** |
| **sat 85** | **8 of 8** | **YES** | **`Build_FrackingExtractor_C`, all 8 at 0 cm** |
| sat 86 | 3 of 8 | NO | landscape at 102 / 135 / 180 cm |

**Across the whole probe: 24 blocked rays were the author's own extractors and 4 were terrain.**

**WHY IT MATTERS.** `FCollisionQueryParams` in `IsSpotEnclosed` ignores nothing ([[T36]]), so a
**buildable** is enclosure to this predicate. A player who builds on their wells manufactures permanent
"enclosed" spots. **The mod already has a `buildableOverlap` gate — the correct mechanism, with the
correct label.** Enclosure double-counts buildings under a wrong name, and the census would attribute
the refusal to the wrong gate. **Latent only because [[T35]] shows the gate has never been reached.**

**WHAT IT SETTLES ABOUT THE PREDICATE — and this is the useful half.** The predicate **does** detect
"fully inside a solid": three members inside an extractor's collision produced eye-inside-solid YES and
8 of 8 at 0 cm. It correctly returned **3 of 8** for a partially-embedded member and **1 of 8** for a
core beside landscape — exactly the "adds character" case the author wants kept. **The predicate is
working. On this evidence the fix for a buried member is [[T35]] — make the gate RUN — not new
machinery.**

**GRADES.**
* Every figure above — **measured**, with the hit actor named on each ray.
* "The predicate detects fully-inside-a-solid" — **measured for BUILDINGS (primitive collision).**
* **"Therefore it would detect a member inside ROCK" — INFERRED, NOT MEASURED.** Landscape heightfields
  and complex-as-simple triangle meshes do not necessarily report containment the way a primitive does,
  and the eye boolean's NO is unproven over exactly those two types ([[T39]] review). **No member of
  this group was inside rock, so the case the author cares about STILL HAS NO DIRECT MEASUREMENT.**
* Whether these three members are the ones the author found unplaceable — **untested**; they are
  occupied by working extractors, which is the opposite of unplaceable.

**THE ORCHESTRATOR'S ERROR THIS CORRECTS, RECORDED BECAUSE IT IS THE THIRD OF ITS KIND TODAY.** Before
this run I told the author that eye-inside-solid YES at a member's own location "**is** the defect —
it literally means the member is inside a rock." **That was an assertion about what a measurement
MEANS, made without testing what the solid was.** It was a building. **The only reason this was caught
is that the ray line prints the HIT ACTOR** — a bare "8 of 8 blocked" would have been read as proof of
a buried member and sent the next packet at the wrong target. Same lesson as [[T36]]: **print what you
hit, not just that you hit.**

### T41. THE ENCLOSURE PREDICATE IS BLIND TO A MEMBER INSIDE A ROCK FACE — it casts 8 HORIZONTAL rays at ONE height and has NO vertical sampling at all. A member the extractor snaps to *inside a cliff* reads 0 of 8 blocked.
**MEASURED 2026-08-09/10 on `2026-08-09-t39-1`. This closes the question T35–T40 were circling, and it
resolves AGAINST the cheap outcome the orchestrator predicted.**

**The evidence, from three commands in one session.**
* The author photographed a Resource Well Extractor hologram **snapping to a well member inside a
  vertical rock face**, and the log shows `HOLOGRAMHOOK ... snapped=1 disq=[<none>]` at that moment.
* `NodeShuffle.PointAtHere`, aimed at that face: the aim ray hit `FGCliffActor_1637`, component
  `CliffMesh`, at 1030 cm. **The enclosure predicate at that impact point returned 0 of 8 rays blocked,
  eye-inside-solid NO.** The same line reports the ground slope there as **68.3 deg, which the cliff
  gate (60 deg) WOULD REJECT** — so a different gate sees the problem this one cannot.
* `NodeShuffle.WellProbe` over that group had already probed **`BP_FrackingSatellite81` at its own
  location** — `V(X=101626.38, Y=159434.02, Z=1863.36)` — and returned **0 of 8 blocked, eye NO, "not
  refused"**. That member is **~6.4 m from the aim impact**, and is the nearest probed member to it.

**THE MECHANISM, read from the predicate rather than inferred from the symptom.** `IsSpotEnclosed`
casts **8 rays on the horizontal plane** (bearings 0–315 deg) from `Z + 200`, each **500 cm**, and
refuses at 7 of 8. **There is no up-ray, no down-ray, and no vertical component of any kind.** A member
at the base of a face, under an overhang, or set into a wall therefore has open air in most horizontal
directions at eye height and reads CLEAR — while being visually and practically inside the rock. **This
is not a threshold problem and not a tuning problem; the sampling geometry cannot represent the case.**

**A SECOND CONTRIBUTOR, measured and not to be conflated with the first.** The aim trace runs
`ECC_Visibility` with **complex** collision; the enclosure rays run `ECC_WorldStatic` with **simple**.
A cliff's simple hull can differ substantially from the mesh the player sees and the hologram snaps to,
so even a horizontally-enclosed spot may read clear on the gate's channel. **Which of the two dominates
here is UNTESTED.**

**WHAT THIS OVERTURNS.** [[T40]] concluded from three members inside an extractor's collision that "the
predicate detects fully-inside-a-solid, therefore the fix is [[T35]] — make the gate run." **The first
half is still true and the inference is now FALSIFIED:** it detects being inside a **primitive-collision
building**, and does not detect being inside a **cliff**. The orchestrator's on-record prediction —
*">=7 blocked means the predicate already works and this is only plumbing"* — is **wrong**. Making the
gate run would NOT have fixed the author's case, and shipping that conclusion would have closed the
investigation on a defect that remains.

**WHAT IS STILL NOT MEASURED.** Whether adding vertical sampling would refuse the spots the author WANTS
kept — a partially embedded member reads 3 of 8 today ([[T40]] sat 86) and **the author has ruled those
must keep passing** ([[T34]]/[[T37]] ruling). **Any fix must be checked against that ruling before it
ships, and `WellProbe` is now the instrument that can do it.** Also unmeasured: whether the ordinary
node path has the same blindness — `IsSpotEnclosed` is shared, so **by construction it does**, but its
consequences there are untested.

### T43. `TActorIterator<AFGResourceNode>` CANNOT SEE FRACKING CORES — or any modded node class deriving directly from `AFGResourceNodeBase`. Confirmed from the engine headers, twice, independently.
**The `Foo*`-excludes-`FooBase` signature, which this workspace has shipped before. Found in passing by
`ns-t42-centreshadow` and CONFIRMED by an independent cold review from the headers:**
* `AFGResourceNodeFrackingCore : public AFGResourceNodeBase` (`FGResourceNodeFrackingCore.h:14`)
* `AFGResourceNode : public AFGResourceNodeBase` (`FGResourceNode.h:66`)
* `AFGResourceNodeFrackingSatellite : public AFGResourceNode` — **satellites DO appear; cores do not.**

`BuildWellNodeScanCache` and `ValidateWellMemberSpot` iterate `TActorIterator<AFGResourceNode>`, so the
node-overlap gate is **blind to every fracking core in the world**, and blind to any node class a mod
declares directly under `AFGResourceNodeBase`. An in-repo comment already stated this; nobody had
verified it or drawn the consequence.

**This is a POPULATION defect, not a predicate defect** — the gate's arithmetic is fine and it is
looking at the wrong set, which is the failure this workspace's own review rules single out because
every other gate passes while it happens.

**IT IS LOAD-BEARING FOR A STANDING AUTHOR CONSTRAINT.** The author (2026-08-09): *"We've already
worked on them [lithium, lead, chlorine] and have them going, I just don't want them excluded in new
things we do."* A modded resource whose node class derives from `AFGResourceNodeBase` rather than
`AFGResourceNode` is **silently outside this gate**, and no vanilla-only test session can reveal it —
tonight's log contains 13 descriptors and every one is vanilla. See
[[nodeshuffle-modded-nodes-in-scope]].

**It also plausibly explains an older finding** — `docs/TECH-DEBT.md` records a solid Sulfur node
appearing **45.9 m** from a relocated chlorine well core, with the note *"node placement does not know
wells exist"*. A core invisible to the iterator is a concrete mechanism for that. **PLAUSIBLE, NOT
MEASURED — do not write it up as the cause until someone tests it.**

**Before fixing:** widening the iterator to `AFGResourceNodeBase` changes the POPULATION of a live gate
and would move both the well and ordinary-node paths at once. It needs a differential review and a
before/after count, not a one-line type change. **Measure what the gate currently sees and what it
would then see, first.**

### T44. THE CENTRE-CONTAINMENT CANDIDATE DOES NOT SEPARATE THE CASES — it would refuse the member the author explicitly wants KEPT, and every one of its NOT-INSIDE readings is UNPROVEN. Prediction falsified; the rule is not retired, the implementation is.
**MEASURED 2026-08-09/10 on `2026-08-09-t42-1`, `NodeShuffle.WellProbe` over `BP_FrackingCore13`,
7 of 7 members probed. The shadow metric gated nothing, which is the only reason this cost one run.**

| member | shipped gate | candidate | control built? |
|---|---|---|---|
| core (1 of 8) | accept | **CENTRE INSIDE** → refuse | yes |
| sat 81 — the buried one (0 of 8) | accept | **CENTRE INSIDE** → refuse | yes |
| sat 82 (8 of 8, extractor on it) | refuse | NOT INSIDE → accept | **NO** |
| sat 83 (0 of 8) | accept | **CENTRE INSIDE** → refuse | yes |
| sat 84 (8 of 8, extractor) | refuse | NOT INSIDE → accept | **NO** |
| sat 85 (8 of 8, extractor) | refuse | NOT INSIDE → accept | **NO** |
| **sat 86 (3 of 8, partly embedded — AUTHOR WANTS IT KEPT)** | accept | **CENTRE INSIDE** → refuse | yes |

**Shipped gate refuses 3, candidate refuses 4, they agree on 0 of 7 and disagree on all 7.**

**THE PREDICTION, RECORDED BEFORE THE RUN, WAS WRONG ON 3 OF 6 NAMED MEMBERS.** Predicted: sat 81
INSIDE (**correct**); core and sat 86 NOT INSIDE (**both wrong — both read INSIDE**); 82/84/85 NOT
INSIDE once buildables excluded (**correct in value, but see below**).

**THE DECISIVE FAILURE: sat 86 reads CENTRE INSIDE.** That is the partially-embedded member the author
ruled must keep passing ([[T34]]/[[T37]] ruling). **Shipping this as a gate would have deleted exactly
the terrain the author said adds character** — the outcome the shadow-metric discipline existed to
prevent, caught for the price of one run and zero placement changes.

**AND ALL THREE "NOT INSIDE" READINGS ARE UNPROVEN.** Each of 82/84/85 reports the positive control as
**not built** — *"a walk from the sky start down to 20000 cm below the tested point found no counted
surface at all"*. By the metric's own trust rule a NOT INSIDE without a passing control is **unproven,
not open air**. So the candidate has **zero trustworthy accepts** in this run. That the 3 unproven ones
are exactly the 3 with extractors built on them is a **correlation, and its cause is UNTESTED.**

**WHAT IS NOT ESTABLISHED — do not conclude the author's rule is wrong.** The rule is about a node's
centre being inside solid; this implementation of it failed. Two candidate explanations, neither tested:
* the walk is wrong (it is known to **fail toward NOT INSIDE**, and the control failed on 3 of 7); or
* **the members really are below the terrain surface.** The same command's ground line reports a long
  downward trace landing **+2 m, +13 m, +13 m, +5.7 m, +5.7 m, +21.5 m** above the members. **If a
  member sits 13 m under the surface, "centre inside solid" is TRUE and correctly reported** — and the
  real defect is elsewhere entirely. **That Z discrepancy has been printed all evening and has never
  been explained. Explain it before building another predicate on verticality.**

**NEXT, IN ORDER, AND NOTHING ELSE UNTIL THE FIRST IS DONE.**
1. **Explain the ground-trace Z gap.** It is unexplained, reproducible, printed per member, and every
   vertical predicate depends on what it means.
2. Then re-examine the walk: the F2 fix ([[T42]]) — a second reading with `SubjectActor = nullptr`
   printed beside the first, ~5 lines — turns the one unmeasured judgement call into a measurement.
3. **Do not tune the candidate to make sat 86 pass.** That is fitting the predicate to the last
   screenshot, which this file already warns against twice.

### T48. THE CENTRE-CONTAINMENT WALK REPORTS "INSIDE" FOR A POINT IN OPEN AIR WHENEVER AN `FGCliffActor` IS ABOVE IT. Proven in game, with a photograph. [[T44]] IS OVERTURNED — the author's rule was never tested.
**MEASURED 2026-08-10 on `2026-08-09-t47-1`. The author stood on open ground under a rock overhang,
photographed the overhang above and their own feet below, aimed straight down and ran
`NodeShuffle.PointAtHere`.**
```
aim: pitch -89.9, direction (0,0,-1), hit at 162 cm
     LandscapeStreamingProxy / LandscapeHeightfieldCollisionComponent   <- the actual ground
probe eye inside solid geometry: NO                                      <- eye is in open air
ENCLOSURE GATE: 5 of 8 rays blocked -> not enclosed
SHADOW centre-containment: CENTRE INSIDE                                 <- FALSE
ground trace from that point: FGCliffActor_1628 / CliffMesh              <- the overhang above
```
**The point is the ground under the author's boots. The eye check says NO. The enclosure rays say 5 of
8. The photograph shows open air. The walk says INSIDE.**

**THE PATTERN, across every reading taken tonight — it is perfectly mechanical:**
* trace above the point hits an **`FGCliffActor`** → **CENTRE INSIDE** (well members 1, 2, 4, 7, and
  this open-air point)
* trace above hits **`LandscapeStreamingProxy`** → **NOT INSIDE** (member 5, after the author deleted
  the extractor above it — a clean single-variable control)
* trace above hits an **excluded buildable** → **NOT INSIDE**, with the positive control unbuilt
**The walk is not measuring containment. It is measuring "is there an un-excluded `FGCliffActor` above
me."** It enters the cliff mesh from the sky and never registers the exit from its underside;
landscape heightfields exit correctly.

**WHAT THIS OVERTURNS.**
1. **[[T44]] IS WRONG.** It concluded the author's centre rule "does not separate the cases" because
   the candidate would refuse satellite 86, the partially-embedded member the author wants kept. **86
   read INSIDE for this reason, not because its centre is buried.** The rule is **UNTESTED, not
   disproven** — and every conclusion drawn from T44 must be re-derived.
2. **THE AUTHOR'S CAVE CONCERN WAS EXACTLY RIGHT, AND THIS IS THE MECHANISM.** They asked *"that
   doesn't mean it's considered a cave right? We don't want to break our putting nodes in caves."* A
   cave floor has rock above it. **This predicate would have condemned every cave placement in the
   world**, and it would have looked like a correct measurement while doing it.
3. **The shadow-metric discipline is what saved it.** The candidate gated nothing, so a predicate that
   is wrong in the most dangerous possible direction cost two console commands and no regression.

**WHAT IS STILL TRUE AND MUST NOT BE RE-LITIGATED.** [[T41]] stands on its own evidence — the shipped
enclosure gate has no vertical sampling and reads 0 of 8 for a member inside a cliff face. [[T40]]
stands. The `RaycastGroundAt` behaviour is now understood: it starts high and stops at the FIRST
surface, so under an overhang it returns **the top of the overhang** — measured at **+23 m** above the
author. That is the whole of the unexplained +2 m to +21.5 m gap. **Mystery closed; it was never a
cave.**

**NEXT.** Fix the walk's exit detection against `FGCliffActor` static meshes, then re-run the SAME
readings — the author's rule gets its first real test only after that. **Do not tune the walk against
satellite 86.** The single best regression test now exists and is free: **a point on open ground under
an overhang must read NOT INSIDE**, and the author has the coordinates.

### T50. THE CONTAINMENT WALK SPENDS ITS ITERATION BUDGET ON HITS IT THEN DISCARDS — the player's own capsule consumed 28 of 32 inbound and 31 of 32 outbound hits, so the walk never reached the geometry it existed to count. Root cause of [[T48]].
**MEASURED 2026-08-10 on `2026-08-10-t49-1`, `PointAtHere` at the known-correct test point
`V(X=81329.41, Y=156623.09, Z=2112.49)` (open ground under an overhang; correct answer NOT INSIDE).
The per-crossing instrument printed the whole arithmetic:**
```
INBOUND : 32 blocking hits, 28 EXCLUDED -> 4 counted, 4 front, 0 back
OUTBOUND: 32 blocking hits, 31 EXCLUDED -> 1 counted, 1 front, 0 back
entries = 4 ; exits = max(inbound back 0, outbound front 1) = 1 ; net = 3 >= 1  => CENTRE INSIDE
"A walk used all of its 32-hit iteration budget."   <- BOTH did
```
**Nearly every discarded hit was `Char_Player_C` — the caller's own pawn, passed as the subject actor.**

**DEFECT 1, primary. An EXCLUDED hit still costs a step of the budget.** The walk hits, tests, discards,
steps 2 cm past, and hits the same actor again. A player capsule is roughly 180 cm tall, so at a 2 cm
step it can absorb ~90 hits on its own — the 32-hit budget is gone long before the walk reaches any
terrain. **The exclusions are applied AFTER the trace instead of inside it.** `FCollisionQueryParams`
already carries `AddIgnoredActor`, which `IsSpotEnclosed` itself uses for the pawn ([[T36]]) — an
ignored actor is never returned at all and cannot consume a hit. **That is the fix, and it is the same
mechanism the sibling predicate already uses.**

**DEFECT 2, and it is why nothing caught defect 1.** **Back faces were ZERO on both walks.** The
`max(inbound back faces, outbound front faces)` was written precisely because nobody could settle from
the headers whether this engine reports back faces; this is the first evidence, and it says **it does
not** — so the outbound-front-face term is the ONLY exit mechanism there has ever been. Budget-starved
to 1 counted crossing, it under-reports exits, and the walk **fails toward INSIDE** whenever an
excluded actor stands near the tested point. **Note this inverts what the T42 review predicted** (it
reasoned the `max()` would make the walk fail toward NOT INSIDE, because a spurious exit wins). The
review's logic was right and its premise — that back faces might be reported — was wrong.

**WHY IT LOOKED LIKE "cliff above ⇒ INSIDE".** [[T48]] found that correlation and it is real, but it is
a symptom: the reading is taken where the player stands, so the player is always near the tested point,
and whatever geometry survives the exhausted budget decides the verdict. **The cliff was never the
cause. Do not fix this by special-casing `FGCliffActor`.**

**WHAT THIS DOES NOT SETTLE.** Whether the same starvation explains the well members' INSIDE readings
is **UNTESTED** — there the subject is the member's own actor, not the pawn, and nobody has looked at
its hit count. **Re-run `WellProbe` after the fix and compare, do not assume.** And the author's centre
rule remains **untested** — [[T44]]'s failure is now attributed to two instrument defects, so the rule
gets its first honest test only after both are fixed.

**THE FIX, IN ORDER.** (1) Move all three exclusions into `AddIgnoredActor` on the query params so an
excluded actor never returns a hit. (2) Re-run the free regression test above — it must read NOT
INSIDE. (3) Only then re-run `WellProbe` and grade the members. **Do not tune anything against
satellite 86.**

---

## T68 — ficsit-release config cleanup: seven switches deleted, one promoted, rows sorted (2026-08-11)

**Status (updated 2026-08-12): COMPLETE — COMMITTED `5caea5a` (main packet, post scoped re-passes) +
`d4e9501` (F4 label round), DEPLOYED `t68-3` (byte-scan 8/8: t68-3 in, t68-2/t67-2 out, old label
absent, controls present). All authored rounds passed their scoped reviews. OWED: the in-game
checklist (t68-coldreview.md steps, incl. the F1/F2 honesty repros) and changelog sign-off; the
`ArchivedPlugins` zips hold t68-3 but were written pre-signoff — REPACKAGE from the reviewed commit
for any release.** Packet `ns-t68-release-config`, markers `t68-1..3`.
Spec: `_team/nodeshuffle-followups/ficsit-release-config-audit.md`; decisions taken by the author
2026-08-11. Handoff: `_team/nodeshuffle-followups/T68-implementation-handoff.md`.

**What changed.** Five panel properties deleted and hard-wired: `EnableExperimentalFeatures` (dead,
zero consumers), `CommitWellsAtRoll` (feature removed outright), `ShowCompatibilityNotices` (both call
sites pass the literal `true`), `AllowVanillaDisappear` (hard-wired to its shipped default ON — both
consumers deleted, not defaulted), `RerollRelocatedWells` (hard-wired **ON**: a re-roll now re-considers
an already-moved well). Two CVars deleted: `NodeShuffle.AutoAllowExtractors` (the KDF allow-list
generator always runs) and `NodeShuffle.ProtectForeignNodes` (no reachable state of its own). One
property added: **`ProtectOtherModsNodes`, DEFAULT ON**, a panel checkbox driving the latch
`NodeShuffle.DestroyerVeto` used to gate alone. Rows re-ordered into the audit's §3 grouping. The
protection list is now sorted by mount label, then resource name, at both populators. Version bumped
1.3.0 → 1.4.0 so SML rewrites every `NodeShuffle.cfg` and drops the orphaned keys on first load
(measured at SML `ConfigManager.cpp:103-117`).

**BEHAVIOUR CHANGES A PLAYER CAN SEE — none of these are silent, and none should be softened:**
1. **Protection is ON by default.** It shipped OFF (the CVar defaulted to 0). Every existing install
   that has KBFL + another mod's nodes starts answering that mod's node-handler checks on the next load.
2. **A re-roll now re-rolls already-moved wells.** On a save with relocated wells, the first re-roll
   after this build churns most of them, and each is absent until visited. (Author's standing ruling:
   a shuffle hides ALL the things we shuffle.)
3. **Anyone who had silenced the compatibility notices is un-silenced.**
4. Anyone who had turned `AllowVanillaDisappear` OFF gets the default on their **next roll or re-roll**
   only — this is generation-time code; an already-rolled layout is not re-rolled by the upgrade.

**Checkbox ↔ CVar precedence (decided here, one resolver).** The checkbox is the persisted source of
truth. `NodeShuffle.DestroyerVeto` is a **session-scoped console override** that wins only when it has
actually been set — decided by `SetBy` priority, never by comparing its value to the default, so a
player who deliberately types `NodeShuffle.DestroyerVeto 0` is obeyed. One resolver
(`NodeShuffleResolveDestroyerVetoRequested`, `NodeShuffle.cpp`) so the latch's two readers cannot
disagree. Pinned by `tools/check_t61_lint.ps1` (repointed) and `tools/check_t68_lint.ps1` (new).

**T23's opposite-polarity pair is RETIRED, with `tools/check_t23_writers.ps1`.** Its question was "what
does the roll-time removal toggle do"; the toggle and its arm are gone, so the pair had no subject and
would have read VACUOUS forever. **Retiring a pair because its FEATURE was removed is the one
legitimate way a red/green pair dies** — it was not retired to turn a red half green.

**PARKED, NOT DEAD (dated TODOs at every reading surface):**
* `FNodeShuffleWellSuppressionRecord::bSuppressedAtRoll` — a `SaveGame` field with no writer left. Kept
  because saves written by ≤1.3.0 carry Roll-stamped records this build must still deserialize and
  un-hide. `EWellSuppressPhase::Roll` and `SuppressVanillaWellGroup`'s `!bRollPhase` branch are kept for
  the same reason. Delete only when that back-compat window is closed.
* The `!bNoticesEnabled` suppression arms in `NodeShufflePendingNoticeEmit.cpp` and
  `NodeShuffleObserveNotice.cpp` — unreachable now that every caller passes `true`. Kept as the seam a
  future opt-out would re-enter at. Delete by 1.5.0 if no opt-out is wanted.

**WHAT WAS LOST, recorded because deleting a lever is a real loss:** `NodeShuffle.AutoAllowExtractors=0`
was the only path that DELETED the generated KDataForge pack. The per-load pass still clears and
rebuilds the documents it owns, so nothing stale survives a load; what is gone is "remove the pack
entirely and never write it again". Uninstalling the mod removes the directory. `check_t59_lint.ps1`'s
half 2 was rewritten from "exactly one tree delete, in the rollback lever" to **zero tree deletes** —
the invariant is unchanged and now stronger; its exception retired with its feature.

**Row order is semantics-free — proved, not assumed.** Consumers enumerated before the sort was
written: SML array serialization (positional, but every element is a keyed object and every reader keys
on the path); the opt-out latch (a `TSet`); the sync pass's `ExistingByPath` (a `TMap`); the add path's
`RemoveElementAtIndex(Num()-1)` withdrawal (**which is why the sort runs strictly after the add loop**,
pinned by the T68 lint); the T65LABEL listing's `[i]` indices (a listing, not a key). No lint pins a row
index. Nothing reads `FNodeShuffleConfigStruct::ProtectedForeignResources`.

**Not verifiable from C++, so it is a runtime step, not a claim:** that the panel RENDERS in
`SectionProperties` order, and that the sorted rows render sorted. `Widget_CP_Section` is Blueprint —
the same limit T65 and T67 recorded for the row field order and the row labels.

**Build note (2026-08-11).** The first build failed with ten `error C2001: newline in constant` in
`NodeShuffleObserveNotice.cpp` - an editing script wrote real newlines into two `TEXT()` literals.
**No lint and no `arity.py` run could have caught it: a malformed string literal is a compiler-only
signal.** Repairing it then collided with `check_t67_lint.ps1`, which pins a phrase that must stay
contiguous on one line - the naive re-wrap would have left that pin green on a mutant. Both are
recorded because the shape recurs: *a packet that has never been compiled carries an unmeasured
class of defect, and re-wrapping pinned copy is a silent lint break.*

**Cold review + fix round (2026-08-11, `_team/nodeshuffle-followups/t68-coldreview.md`).** Verdict SHIP
WITH TESTS. Two player-facing FALSE CLAIMS were caught, both of the `lessons-log-asserted-a-cause`
family: the chat notice asserted *the checkbox is unticked* on a branch whose predicate is the resolved
MODE (a player with `NodeShuffle.DestroyerVeto=0` and the box ticked was told to tick it), and the
tooltip's *"for the rest of the session"* was false for the exact population the CVar was kept for -- an
`Engine.ini [ConsoleVariables]` entry re-applies at EVERY launch, so the checkbox is inert forever, not
for one session. Both replacements were applied VERBATIM from the review and then re-verified against
their predicates. The review also found **a third `TODO(pre-release)` surviving in the PUBLIC header**
plus four other comments naming deleted switches as live -- **none reachable by any pin**, because every
tombstone was in the `.cpp` and the next author reads the `.h`
([[lessons-file-the-rule-where-the-author-works]]). `check_t68_lint.ps1` gained **H1c**, which scans four
files including headers, comments in scope, exempting only a 6-line window around a dated T68 marker;
it immediately found a SIXTH stale mention nobody had read. 13/13 mutants.

**Scoped re-pass (2026-08-11, same file, section 2): DO NOT COMMIT AS-IS -- 4 blockers, all comment/lint.**
The finding that matters: **two of the six AUTHORED comment repoints from the fix round asserted
something NEW and FALSE** -- a header claiming the KDF pass is "NOT GATED BY ANYTHING" (it is gated three
ways in its caller, and the claim contradicted another file in the same packet), and an apply-path
comment claiming "THIS ARM IS NOW THE ONLY PATH THAT HIDES AN ORIGIN" (four live call sites; two can be
the first hide). **Every VERBATIM item from that round was clean; both false claims were AUTHORED.**
That is the review-response rule paying for itself twice in one packet. Also caught: `check_t68_lint`'s
H1c **did not scan `Public/NodeShuffle.h`** -- the file whose defect created the pin -- and adding it
immediately failed on a SEVENTH stale mention at `:338`; and the mutation grader re-implemented the
pin's exemption instead of calling it, so a mutant could be graded CAUGHT while the real pin skipped it.
All fixed; 8/8 suites, 13/13 mutants. **No rebuild: comments and lint only, proven by comparing the
comment-stripped sources against the robocopy /MIR mirror that produced `t68-2` (md5 identical for all
three touched files), so `t68-2` remains the build of record and the deployed DLL is still `t67-2`.**

**F4 decided by the author (2026-08-11), build `t68-3`.** The cold review's one open finding was the
checkbox's DISPLAY NAME: `"Protect Other Mods' Nodes From Removal"` asserted the outcome T67 spent a
packet establishing this feature may not assert -- in the most-read string on the page, and the lint
could not catch it because it bans three literal phrases, not the claim. The author chose **a generic
label plus the concrete case in the tooltip**: the label is now `Protect Other Mods' Nodes`, and the
tooltip opens *"Some overhaul mods -- Satisfactory Plus is the known case -- remove other mods' resource
nodes. When this is on, NodeShuffle answers those removal checks..."*. Naming SF+ is MEASURED (T58's
~0.9 s ResearchNodeRemover sweep) and "the known case" scopes it to what we measured rather than
claiming SF+ is the only such mod. **SYMMETRY: the label was quoted at four other sites (the CVar help,
the protection-list tooltip, the chat notice, the veto module's log line); all four were updated in the
same commit and a repo-wide grep for the old label returns ZERO hits.** New lint half H8 pins the label,
the SF+ opening, and the absence of the old label from every `TEXT()` literal; M14/M15 mutate the label
and the SYMMETRY. 8/8 suites, 15/15 mutants. Deployed DLL remains `t67-2`; `t68-3` is parked.

## T69 — Per-row "Show In Scanner" checkbox on the protection list: modded resources clutter the object scanner (2026-08-12)

**Status: FILED (user-requested 2026-08-12). Not designed, not started.**

**What the user hit.** With the FF dirt variants (and the rest of the foreign-resource population)
all live, the object scanner's resource list is difficult to work with — many entries a player never
scans for. The protection list already enumerates exactly this population, one row per foreign
resource.

**The ask.** Add a second per-row checkbox to the protection list: **"Show In Scanner", default
SHOW (true)** — unticking hides that resource from the scanner's selectable list. Orthogonal to
the Protected tick (a resource can be protected and scanner-hidden, or vice versa).

**Design questions for the packet (do not assume any):**
- WHERE the scanner's selectable-resource population is built, and whether a mod can filter it
  per-descriptor without hooking every consumer — prior art: the scanner phantom-ping fix landed at
  the `GenerateNodeClusters` SOURCE, not per-consumer (`nodeshuffle-scanner-phantom-ping` memory);
  start from there. Also check what the installed `MapResourceNodeFilters` mod does — same problem
  space, possible conflict AND possible pattern to learn from.
- Whether a second bool fits SML's array-row schema alongside `Protected` + `Resource` (the row is
  an SML config struct; T65/T67 established what we can and cannot stamp on its widgets).
- Population: hide-in-scanner must not affect protection, notices, or the audit/census populations
  — every consumer of the row struct gets enumerated (BLAST RADIUS).
- Copy: the checkbox claims a scanner outcome — grade it; only ship wording for what is measured.

## T70 — Should the protection list allow manual ADD/REMOVE at all? The +/- affordances have murky semantics on an auto-populated list (2026-08-12)

**Status: FILED (user-raised 2026-08-12). INVESTIGATE before the ficsit release if cheap; otherwise park.**
2026-08-12 release-readiness audit: not a blocker; +/- affordances unchanged for 1.4.0.

**The smell.** The list is auto-populated on discovery (T61/T65). SML's array widget still renders
`+` / `-` on every row and a `+` on the header. Both affordances have unclear semantics here:
- **Manual ADD** creates a row with a blank/hand-typed path. Blank rows are already counted as a
  defect population (`diskRowsBlankPath`, expected 0); a typo'd path is a row that matches nothing.
  No user story needs manual add — discovery adds every real resource.
- **Manual REMOVE** deletes a row, but the protection policy is default-protected: a path absent
  from the latched opt-out set is PROTECTED (NodeShuffle.h:331-334). So removing a row does NOT
  unprotect the resource — it just removes the visible opt-out control until (or unless) discovery
  re-adds it. Whether the seen-set allows re-add after manual removal is UNMEASURED — measure it
  before writing any copy about `-`.

**Questions for the packet:** (a) Can the +/- affordances be hidden/disabled from C++ for this one
array property, or is that Blueprint-side like the T67 row layout (measure the exact boundary —
`UCP_Section`-style — before promising anything)? (b) If they cannot be hidden: is the cheaper fix
a tooltip on the list explaining that rows manage themselves, plus making manual removal provably
harmless (re-add on next discovery)? (c) Interaction with T69: if a second checkbox lands, the row
becomes more obviously "managed", which strengthens the case for hiding `+`/`-`.


---

## T71 — Resource wells become a natural part of the shuffle: both well toggles deleted and hard-wired ON (2026-08-12)

**STATUS: CODE COMPLETE, BUILD PENDING (game was running at implementation time).** Marker `t71-1`.

**Author's decision (2026-08-12):** *"make them a natural part of the shuffle."* This OVERRIDES the T68
release-config audit's recommendation #7, which argued for keeping both well toggles on the grounds that
relocation was experimental and carried an unbounded-absence risk a player should opt into. The risk is
unchanged; the ruling is that it is the mod's behaviour rather than an option.

### What was deleted

| Deleted | Was | Now |
|---|---|---|
| `ShuffleResourceWells` (panel row *Shuffle Resource Wells (In Place)*) | bool, default OFF | `FNodeShuffleConfigStruct::bWellShuffleHardWiredOn` — `static constexpr bool = true` |
| `RelocateResourceWells` (panel row *Relocate Resource Wells (EXPERIMENTAL)*) | bool, default OFF | `FNodeShuffleConfigStruct::bWellRelocationHardWiredOn` — `static constexpr bool = true` |

The panel goes **17 rows -> 15** (plus the `ProtectedForeignResources` array, which is not a row). The
**RESOURCE WELLS group is gone, not emptied**: those two rows were its entire content, so there is no
remaining well content for a header to head. Group order is now THE SHUFFLE -> OTHER MODS ->
TROUBLESHOOTING.

### Parity — what each OFF path guaranteed, and where the guarantee went

| OFF path | What it guaranteed | Where it went |
|---|---|---|
| `ShuffleResourceWells` OFF | No well ever changes what it yields; `WellLayout` empty on such a save; `RollWellLayout` and `ApplyWellRetype` both returned immediately | **GONE.** Every unpinned, unbuilt-on well is retyped at roll time on every save. |
| `ShuffleResourceWells` OFF (RT-6 sub-clause) | An already-retyped save KEPT its `WellLayout`, so `BuildManagedNodeGroupsFromLayout` kept emitting groups and fracking machines stayed allow-listed even while we were no longer touching wells | **RETIRED, not lost.** The divergence it described required a state where we hold well data but do not act on it. That state no longer exists. The allow-list itself is unchanged. |
| `RelocateResourceWells` OFF | **Wells never move.** This is the big one: its tooltip carried the UNBOUNDED-ABSENCE warning — the original is removed the moment a well is dealt a destination and only rebuilt when you travel there, so it is in neither place for as long as you do not go | **GONE as a guarantee; the WARNING SURVIVES and moved up a level** to `NodeShuffle.uplugin`'s `Description`, `README.md` (Resource wells + the Known-behaviour section), and the CHANGELOG 1.4.0 entry — because it now describes the mod rather than an option. |
| Either OFF | Two unverified edges were only reachable by opting in: desert-biome well mesh names (T2) and relocated-well snap-box overlap within ~15 m of an ordinary node (T3) | **Both are now ALWAYS IN SCOPE.** Scope-change banners added to T2 and T3 above. Neither is re-measured by this packet. |
| Both OFF | Surviving guarantee, UNCHANGED: a well with a Resource Well Pressurizer on its core or any Resource Well Extractor on a satellite is never retyped and never moved, re-checked every pass | **Unchanged.** This is now the ONLY lever a player has over a specific well. Stated as such in README.md. |

### Blast radius — every consumer of the two flags, before vs after

| Consumer | Before | After | Intended? |
|---|---|---|---|
| `RollWellLayout` (`NodeShuffleWellRoll.cpp`) | early return + `WELLH1-ROLL: SKIPPED ... is OFF` line | branch and its `GetActiveConfig()` read deleted; `static_assert` on the constant left in place of the gate | Yes |
| `ApplyWellRetype` (`NodeShuffleWellRetype.cpp`) | `bool` parameter + OFF branch + `bWellDisabledLogged` latch | parameter, branch and latch deleted; `bWellLayoutRolled` skip retained and its log line de-toggled | Yes |
| `RollWellRelocation` (`NodeShuffleWellRelocateRoll.cpp`) | `bool` parameter + OFF branch + `WELLH2-ROLL: SKIPPED` line | parameter and branch deleted; `static_assert` on the constant | Yes |
| `ApplyWellRelocation` (`NodeShuffleWellRelocateApply.cpp`) | two `bool` parameters; `bOn = shuffle && relocate`; `!bOn` log block (fell through) | parameters deleted; `bOn` now `constexpr` from the two named constants; `!bOn` block and `bWellRelocDisabledLogged` deleted. **Fall-through behaviour unchanged** — the function always continued into maintenance | Yes |
| `FinishWellRollTeardown` (`NodeShuffleWellSweep.cpp`) | `bool` parameter; `bSweepOn = GetActiveConfig().ShuffleResourceWells && param` | parameter deleted; `bSweepOn` `constexpr` from both constants, still written as a two-term conjunction so the h5-F1 finding survives | Yes |
| `SweepOrphanedWellActors(Phase, bRelocationOn)` | gate 1 could be false | **signature untouched**; both callers now pass a compile-time true, so gate 1 can no longer refuse. **GATE 2 (`PlacedGroups > 0`) is now the only gate that can refuse pass B.** Pass A and the claim reconciliation were already unconditional | Yes — gate 2 was always the save-fact gate and is unaffected |
| `bWellLastApplyRelocationOn` (diagnostics; read by `ReconcileAbandonedWellClaims` and `NodeShuffleWellUnhide.cpp`) | could be false -> `TickOff` / `NotWorked` | now always true after the first apply pass. `TickOff`'s legend REWRITTEN: it no longer claims "relocation is disabled" (a config state that does not exist) and instead reports the predicate and flags itself as unexpected. `NodeShuffleWellUnhide.cpp:442`'s `!bWellLastApplyRelocationOn OR !E.bRelocate` now decides on `!E.bRelocate` alone | Yes |
| `WellClaim.cpp` mid-assembly expiry route (a) "turn the feature off" | one of two live routes to an unbounded claim | **route (a) CLOSED**; route (b) "never walk back to that destination" is now the only live route. Comment updated | Yes — strictly fewer ways to reach the bad state |
| `NodeShuffle.cfg` on disk | held both keys | keys become orphans: ignored on load, dropped on next save (SML `ConfigPropertySection.cpp:18-42`) | Yes |
| Save game | **no consumer.** Nothing in the save mirrors a config value; `WellLayout` is `UPROPERTY(SaveGame)` and is not a config mirror | unchanged | Yes |

**UNEXAMINED consumers** (named so a reviewer's blast-radius pass starts here, not from zero):
`NodeShuffleWellUnhide.cpp`'s `NotWorked` counter denominator — the tally is still emitted but one of its
two disjuncts is now dead; nobody re-derived whether the resulting number still means what its log line
says. And `NodeShuffleWellSweep.cpp`'s `bWellSweepGatedLogged` throttle, which throttled a GATE line that
can now only be produced by gate 2.

### Version bump — FINDING: no further bump needed, stay at 1.4.0

`SemVersion` stays **1.4.0**. Verified at `SatisfactoryModLoader/.../Configuration/ConfigManager.cpp`
(the `LoadConfigurationInternal` tail): the rewrite predicate is
`if (bSaveOnSchemaChange && FileVersion != ModVersion) { SaveConfigurationInternal(ConfigId); }` — it
compares the file's recorded mod version against the loaded mod version and **knows nothing about which
keys exist**. T68 already moved 1.3.0 -> 1.4.0, and 1.4.0 is **unreleased**, so no player has a `.cfg`
stamped 1.4.0. Every existing file is stamped 1.3.0 or earlier, still differs from 1.4.0, and is still
rewritten on first load — dropping these two orphan keys along with T68's five. A second bump would
change nothing.

### Deliberately NOT done

- `SweepOrphanedWellActors`'s `bRelocationOn` parameter is **kept** even though both callers pass a
  constant. Removing it would delete the ns-review-h5 F1 / h2-r2 F-B two-gate essay's subject, and the
  parameter still documents which pass the gate belongs to.
- Historical narrative comments that describe what the toggles USED to do are left in place where they
  are clearly past-tense. Only comments asserting a CURRENT reachable state were rewritten.
