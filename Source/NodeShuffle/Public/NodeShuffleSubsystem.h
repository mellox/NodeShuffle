#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ModSubsystem.h"
#include "FGSaveInterface.h"
#include "Resources/FGResourceNode.h"
#include "NodeShuffle.h" // ns-h1b-notice: FNodeShufflePendingEntry, held by value in a member below
#include "NodeShuffleSubsystem.generated.h"

class AFGNodeMeshActor;
class AFGResourceScanner;
// Packet H2 (ns-wells-h2): the relocated well group's runtime handles are UPROPERTY TMaps of these,
// so the pointer types must at least be declared here. The full headers are deliberately NOT pulled
// into this Public header -- the three H2 .cpp files include NodeShuffleWellCensus.h for them, exactly
// as H1's files do, so a change to the fracking headers does not rebuild the world.
class AFGResourceNodeFrackingCore;
class AFGResourceNodeFrackingSatellite;
class UFGResourceDescriptor; // scanregen-1: TSubclassOf<> member below only needs the forward decl

// T4 (docs/TECH-DEBT.md) -- ONE WATCHED ORIGINAL: a record whose AFGNodeMeshActor was NOT resolvable
// at the instant its node was hidden, i.e. exactly the set whose rock can go dark LATER.
// Deliberately a PLAIN struct, not a USTRUCT: it holds no UObject, is never replicated, never saved,
// and lives only in a transient TMap. Declared at file scope (not nested in the UCLASS) so UHT never
// has to parse it as class content.
//
// ns-t7-split: this and the members that use it were module-statics in NodeShuffleSubsystem.cpp
// (`namespace NodeShuffleMeshHideLatency`) ONLY because the packet that wrote them was barred from
// this header. Promoted verbatim -- same fields, same initial values, and the world-change reset that
// guarded them is KEPT AS-IS at its original call site, so the promotion cannot change behaviour
// whether or not the subsystem instance itself survives a world change.
struct FNodeShuffleMeshHideWatch
{
    FVector NodeLoc = FVector::ZeroVector;
    float HideTimeSeconds = 0.0f;
    int32 HidePass = 0;
};

// One node-pool entry of the per-save layout. The layout is rolled exactly
// once per save (seeded) and afterwards only ever *applied*; it is the single
// source of truth for which nodes exist, are active, and what they carry.
USTRUCT()
struct FNodeShuffleEntry
{
    GENERATED_BODY()

    // Stable identity; spawned actors are tagged with it so live actors can be
    // matched back to entries across the session.
    UPROPERTY(SaveGame) FGuid EntryGuid;

    // True for mod-added node locations, false for vanilla level nodes.
    UPROPERTY(SaveGame) bool bIsNewNode = false;

    // GetPathName() of the vanilla level actor (empty for new nodes). Level
    // actor paths are stable across loads.
    UPROPERTY(SaveGame) FString VanillaNodePath;

    UPROPERTY(SaveGame) FVector Location = FVector::ZeroVector;
    UPROPERTY(SaveGame) FRotator Rotation = FRotator::ZeroRotator;

    // Vanilla state at roll time (empty/RP_MAX for new nodes).
    UPROPERTY(SaveGame) FString OriginalResourceClassPath;
    UPROPERTY(SaveGame) TEnumAsByte<EResourcePurity> OriginalPurity = RP_MAX;

    // Rolled state.
    UPROPERTY(SaveGame) FString AssignedResourceClassPath;
    UPROPERTY(SaveGame) TEnumAsByte<EResourcePurity> AssignedPurity = RP_Normal;

    // Inactive vanilla nodes are deactivated each session; inactive new nodes
    // are simply never spawned.
    UPROPERTY(SaveGame) bool bActive = true;

    // Had a miner/extractor at roll time: never retyped, never deactivated.
    UPROPERTY(SaveGame) bool bPinned = false;

    // New node has been settled onto the terrain (raycast done near a player).
    UPROPERTY(SaveGame) bool bRayCasted = false;

    // The vanilla node Blueprint class to spawn new nodes from.
    UPROPERTY(SaveGame) FString NodeClassPath;

    // FIX C (overlap guard): how many times this new node's location has been
    // spiral-nudged off an overlap. Persisted so retries don't restart each load.
    // When it exceeds the cap the entry is resolved (deactivated) instead of being
    // re-checked — and re-logged — every single tick forever.
    UPROPERTY(SaveGame) uint8 OverlapNudges = 0;

    // Resource form of the assigned resource at roll time: 1 = solid, 2 = liquid
    // (matches EResourceForm). Lets the apply path treat liquid (oil) nodes
    // correctly without re-deriving the form from a possibly-unloaded class.
    // 0 = unknown/legacy (treated as solid). Phase 2.
    UPROPERTY(SaveGame) uint8 ResourceForm = 0;

    // cave-nodes-1/2: this entry's location is a discovered CAVERN FLOOR cell. Its settle (and any
    // nudge probes) use a SHORT local trace instead of the 200 m top-down ray — the long ray would
    // hit the cave ROOF and strand the node on the surface. Set when a deal draw (roll, relocation,
    // water-locked redeal) randomly picks a cave cell at the natural share; never by a quota fill.
    UPROPERTY(SaveGame) bool bUnderground = false;

    // rehide-1: the TRUE live location this entry's original was captured at (before any relocation),
    // stamped at every live-capture site (initial roll, re-scan augment, experimental augment) and
    // carried through re-roll rebuilds (opportunistically backfilled there too). ZeroVector = unset
    // (pre-rehide-1 saves). Feeds Rec.TrueLocation at the Hide & Replace conversion so a runtime-
    // foreign twin can be re-matched by location once its record's VanillaNodePath goes stale (spawner
    // mods re-create their nodes with a fresh auto-numbered id every process boot).
    UPROPERTY(SaveGame) FVector OriginalTrueLocation = FVector::ZeroVector;
};

// One original vanilla node location to suppress on stream-in after a wipe-on-
// reroll: the node itself plus its associated separate rock are hidden whenever
// they stream in, unless the spot is occupied or reused by the new layout. This
// record is persistent so suppression is reliable across sessions.
USTRUCT()
struct FNodeShuffleSuppressedOriginal
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) FString VanillaNodePath;
    UPROPERTY(SaveGame) FVector Location = FVector::ZeroVector;
    // rehide-1: the durable cross-session anchor for location-based re-matching once VanillaNodePath
    // goes stale (runtime-foreign spawner nodes get a fresh auto-numbered id every process boot, so
    // path resolution alone dies at every restart). ZeroVector = unset (legacy sentinel) — falls back
    // to Location, which is TRUE at initial capture and STALE (the previous relocated dest) after a
    // re-roll rebuild; see SuppressOriginalNodes/TryRematchStaleRecord in NodeShuffleSubsystem.cpp.
    UPROPERTY(SaveGame) FVector TrueLocation = FVector::ZeroVector;
    // correct-visual-6: true when this record's node is MODDED-origin (original resource class path
    // not under /Game/). Such nodes are LEFT NATIVE and must NEVER be suppressed/hidden — set inside
    // RollLayout's Hide & Replace conversion (NodeShuffleSubsystem.cpp, the ONLY place OriginalNodeRecord
    // is built; P5: the dead CaptureOriginalNodeRecord() this comment used to name has been removed) so
    // SuppressOriginalNodes can skip them in BOTH hide loops.
    UPROPERTY(SaveGame) bool bModdedOrigin = false;
};

// (2026-06-15-engine-reskin-1) The old FNodeShuffleDonorRecord + PersistedDonors
// SaveGame store was part of the deleted donor-CAPTURE system. It is gone; any old
// saved donor data is simply ignored on load (no migration needed — the property no
// longer exists, and visuals are now reapplied from authored data every session).

// playtest-fixes-1 (modded-descriptor visuals): one visual captured from an ORIGINAL node's own
// look — NOT from neighbors (the deleted neighbor-proximity capture mispaired). Sources, in order:
//   1. the node's PAIRED AFGNodeMeshActor (MeshActorCache engine-link pairing) — vanilla-class
//      modded-resource nodes (RefinedPower thorium, bamrenew lead) whose look lives on the level's
//      mesh actor;
//   2. dirtdress-1: a static-mesh component OWNED BY (or attached to) the node actor itself —
//      self-rendering modded node BPs (FicsitFarming dirt mounds, KLib crystals) that have no
//      engine mesh-actor links, so source 1 never fires for them (the 98-quartz-dirt-nodes bug).
//      An original's own mesh IS its look by definition, so no rock-name pattern gate applies.
// Keyed by resource class SHORT name; only real UFGResourceDescriptor resources are captured, and
// an own-mesh candidate identical to the quartz placeholder is never stored (esc_ item nodes keep
// their dirty-quartz identity by design). Persisted so visuals resolve by path next session even
// before any original streams in.
USTRUCT()
struct FNodeShuffleCapturedVisual
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) FString ResourceClassName;      // short class name, e.g. Desc_RP_Thorium_C
    UPROPERTY(SaveGame) FString MeshPath;
    UPROPERTY(SaveGame) TArray<FString> MaterialPaths;  // per-slot, slot order
    UPROPERTY(SaveGame) FVector MeshScale = FVector(1.0f, 1.0f, 1.0f);
};

// ns-review-g G1 (Packet G, CRITICAL fix): one (node class, resource class, form) group derived from
// the ROLLED LAYOUT, not from live/spawned actors -- see BuildManagedNodeGroupsFromLayout's own comment
// in NodeShuffleSubsystem.cpp for the full "why". Plain (not reflected -- this never crosses SaveGame or
// Blueprint) so it can be returned across the public/private header boundary without pulling in
// NodeShuffleExtractorDiscovery.h (a Private header) from this Public one.
struct FNodeShuffleManagedGroup
{
    UClass* NodeClass = nullptr;
    UClass* ResourceClass = nullptr;
    int32 Form = -1;
    int32 Count = 0; // number of ACTIVE layout entries in this group -- loaded or not
};

// Packet H2b (ns-wells-h2b): one captured mesh piece of a well member's look. See below the struct
// for FNodeShuffleWellSatellite, which owns an array of these.
USTRUCT()
struct FNodeShuffleWellVisual
{
    GENERATED_BODY()

    // Packet H2b: ONE captured static-mesh piece of a vanilla well member's look, stored by ASSET PATH
    // rather than by pointer so it survives a save round trip AND so a relocated well can be dressed
    // when its ORIGINAL site is not streamed. That second property is the reason this is SaveGame at
    // all: relocation is spawn-on-discovery at the DESTINATION, and the origin can be kilometres away
    // and never loaded again in that session. A capture held only in RAM would leave exactly the wells
    // a tester flies to invisible.
    //
    // Design §2.4 named the paired AFGNodeMeshActor as capture "source 1". H2's measured reality is
    // that the engine link is set for roughly one well group in six (see NodeShuffleWellVisuals.cpp),
    // so this is populated from whichever route found the component; RouteTag records WHICH, so the
    // log can say why a member captured nothing without anyone having to guess.
    UPROPERTY(SaveGame) FString MeshPath;

    // Per-slot materials AS THEY WERE ON THE LIVE COMPONENT (a slot the component did not override
    // records an empty string, and the mesh's own default is used at apply time -- never back-filled
    // with the previous slot's material, which is the flat-colour bug memory:nodeshuffle-rock-miner-polish
    // records against DressRock).
    UPROPERTY(SaveGame) TArray<FString> MaterialPaths;

    // The piece's transform RELATIVE TO ITS MEMBER ACTOR. Relative, not world, is the whole trick:
    // re-applying it on the relocated actor reproduces the look already rotated by the group yaw and
    // already tilted by the destination's own terrain settle, with no separate rotation maths and no
    // way for the two to disagree.
    UPROPERTY(SaveGame) FVector RelLocation = FVector::ZeroVector;
    UPROPERTY(SaveGame) FRotator RelRotation = FRotator::ZeroRotator;
    UPROPERTY(SaveGame) FVector RelScale = FVector::OneVector;

    // ENodeMeshType as reported by a paired AFGNodeMeshActor (255 = unknown / not a node mesh actor).
    // Recorded for diagnostics only -- nothing branches on it. MT_Crack is the piece design §2.4 warns
    // is most likely to be forgotten, so the apply log names how many cracks it re-applied.
    UPROPERTY(SaveGame) uint8 MeshType = 255;

    // "own" / "link" / "spatial" -- which of the three pairing routes produced this piece.
    UPROPERTY(SaveGame) FString RouteTag;
};

// Packet H1 (ns-wells-h1): ONE SATELLITE of a managed resource well. Purity is recorded and NEVER
// written back -- H0 measured that vanilla wells MIX purities across their satellites (design §Q2),
// so there is no shared purity to normalise and normalising one would be a silent balance change.
// The field exists purely so the log (and H2) can state what the vanilla purity vector was without
// re-reading a possibly-unstreamed actor.
USTRUCT()
struct FNodeShuffleWellSatellite
{
    GENERATED_BODY()

    // GetPathName() of the LEVEL satellite actor. Wells are never spawned or moved by H1, so this is
    // a level-actor path and is stable across loads -- the same identity idiom FNodeShuffleEntry uses
    // for a vanilla original.
    UPROPERTY(SaveGame) FString SatellitePath;

    // RECORD ONLY under H1 -- read at roll time, never written back to a LEVEL satellite.
    //
    // Packet H2 (ns-wells-h2) CHANGES WHAT THIS IS FOR, and the distinction matters. H2 relocates a
    // well by SPAWNING a fresh satellite actor, and a fresh actor has the CDO's purity, not this
    // satellite's. So the relocation path DOES write mPurityOverride -- from this field. That is not
    // "changing purity" (H1's forbidden act); it is REPRODUCING the vanilla purity on the actor that
    // replaces this one. A relocated well that silently normalised its purities would be the balance
    // change H1 refused to make, arriving by a different road.
    UPROPERTY(SaveGame) TEnumAsByte<EResourcePurity> OriginalPurity = RP_MAX;

    // ---- Packet H2: rigid-body relocation state ----
    // Offset of this satellite from its CORE, in the core's UNROTATED frame, captured from the live
    // vanilla actors (design §Q3 point 2). XY is rigid and is what the group yaw rotates; Z is a
    // capture-time record only -- every relocated member re-settles its own Z on the new terrain
    // (H0 measured four wells with >12 m vertical spread, so per-node Z settle is mandatory).
    UPROPERTY(SaveGame) FVector LocalOffset = FVector::ZeroVector;

    // This satellite's OWN actor yaw relative to the core's yaw at capture. Re-applied (plus the group
    // yaw) to the spawned satellite so the rocks turn WITH the group -- design §Q3a: without it a
    // rotated well's meshes all face the original direction.
    UPROPERTY(SaveGame) float LocalYawDeg = 0.0f;

    // Where this satellite ACTUALLY ended up once the group's footprint validated, including its
    // settled Z. Also the cross-session identity anchor: a spawned satellite carries no SaveGame
    // field of ours, so it is re-matched on load by location + class, exactly as
    // AdoptRestoredSpawnedNodes re-matches a real-class relocated node.
    UPROPERTY(SaveGame) FVector PlacedLocation = FVector::ZeroVector;
    UPROPERTY(SaveGame) FRotator PlacedRotation = FRotator::ZeroRotator;
    UPROPERTY(SaveGame) bool bPlaced = false;

    // ns-review-h2 F2 (CRITICAL): THIS SATELLITE'S RIGID-BODY OFFSET WAS ACTUALLY CAPTURED.
    //
    // Without it the spawn path could not tell a genuine offset from an absent one, and the review
    // found the exact path that produces an absent one: RollWellRelocation skips re-capture for a
    // well that is already bGroupPlaced, but RollWellLayout's merge loop still APPENDS newly-streamed
    // satellites to that same entry. Those records arrive with LocalOffset and PlacedLocation both
    // ZeroVector -- and (0,0,0) IS FINITE, so the original IsFiniteVector guard did not fire. The
    // result was live, extractor-snappable satellite actors spawned AT WORLD ORIGIN, inflating the
    // well's rate, while the acceptance line printed satellites=7/7/7 OK.
    //
    // An explicit flag rather than a geometric heuristic: "did we measure this?" is a fact about our
    // own bookkeeping, and inferring it from coordinates is how the (0,0,0) hole existed in the first
    // place. An UNCAPTURED satellite is never spawned, but it is still SUPPRESSED with the rest of
    // the vanilla group -- otherwise it would be left visible at the abandoned original site.
    UPROPERTY(SaveGame) bool bCaptured = false;

    // ---- Packet H2b: this satellite's captured LOOK (see FNodeShuffleWellVisual) ----
    // Separate from bCaptured on purpose. bCaptured is about the RIGID BODY and gates whether the
    // satellite may be SPAWNED AT ALL; this is about appearance and gates nothing -- a well with no
    // captured visual is still a working well, it is only an ugly one, and refusing to spawn it would
    // trade a real feature for a cosmetic one.
    UPROPERTY(SaveGame) TArray<FNodeShuffleWellVisual> Visuals;
    UPROPERTY(SaveGame) bool bVisualsCaptured = false;
};

// Packet H1: one resource well (fracking core + its satellites) as a unit of the per-save layout.
// Deliberately a SEPARATE array from Layout rather than an FNodeShuffleEntry: a well is one thing
// with N members, and FNodeShuffleEntry is one-node-shaped (design §2.2).
//
// H1 scope, stated so the next reader does not look for the missing half: the well is RETYPED IN
// PLACE. There is no location, no rotation, no group yaw, no offsets and no satellite spawn state
// here, because H1 never moves a well -- all of that arrives with H2.
USTRUCT()
struct FNodeShuffleWellEntry
{
    GENERATED_BODY()

    // GetPathName() of the LEVEL core actor -- this entry's identity. Also the deterministic SORT KEY
    // for dealing (see RollWellLayout): dealing in actor-iteration order would make the same seed
    // produce different worlds depending on what had streamed in, which is the exact class of bug
    // design §Q3a's "deterministic" rule exists to prevent.
    UPROPERTY(SaveGame) FString CorePath;

    // Captured from the LIVE actors at roll time -- never hardcoded /Game/FactoryGame/... well paths
    // (design §2.2). Consumed by BuildManagedNodeGroupsFromLayout for the SF+ auto-allow extension.
    UPROPERTY(SaveGame) FString CoreNodeClassPath;
    UPROPERTY(SaveGame) FString SatelliteNodeClassPath;

    // The AUTHORED resource (AFGResourceNodeBase::mResourceClass, via GetResourceClassOriginal()),
    // never the effective one -- on a re-roll the core already carries OUR override, and reading the
    // effective class would feed our own previous output back into the deck. That is the
    // "output-derived input is a loop" failure mode (memory: lessons-output-derived-input-is-a-loop).
    UPROPERTY(SaveGame) FString OriginalResourceClassPath;

    // What this well was dealt. Equal to OriginalResourceClassPath for a pinned well.
    UPROPERTY(SaveGame) FString AssignedResourceClassPath;

    UPROPERTY(SaveGame) TArray<FNodeShuffleWellSatellite> Satellites;

    // Someone has already built on this well (activator on the core, extractor on ANY satellite, or
    // either reporting IsOccupied()). Mirrors FNodeShuffleEntry::bPinned: a well a player has built on
    // is not ours to change. Set at roll time AND re-checked at apply time.
    UPROPERTY(SaveGame) bool bPinned = false;

    // True only when NodeShuffle actually owns this well's resource this save. False for a pinned
    // well and for one whose resource could not be resolved -- both fail SAFE to "left vanilla".
    // Also the gate on the SF+ auto-allow contribution: we only claim to manage what we retype.
    UPROPERTY(SaveGame) bool bManaged = false;

    // ======================= Packet H2 (ns-wells-h2): RIGID RELOCATION =======================
    // Everything below is inert unless the SEPARATE RelocateResourceWells toggle is on. H1's in-place
    // retype is untouched and still runs on its own toggle; relocation is strictly additive.
    //
    // FAIL-SAFE DIRECTION, stated once for every field here: at every exhaustion point the well is
    // LEFT AT ITS VANILLA LOCATION (design §Q3 point 4). There is no state in this struct that means
    // "half moved" -- either bGroupPlaced is true and the whole footprint validated, or the vanilla
    // well is still the only well.

    // This well is enrolled in the relocation program. Requires bManaged (a pinned well is never
    // moved -- moving a well someone built a pressurizer on would orphan the machine) AND a COMPLETE
    // offset capture.
    UPROPERTY(SaveGame) bool bRelocate = false;

    // The rigid body was captured from live actors: VanillaCoreLocation/Yaw plus every satellite's
    // LocalOffset/LocalYawDeg. Until this is true the well cannot be relocated at all, because moving
    // a well whose satellite set we only PARTLY know would permanently shrink it -- design §Q3's
    // "silently shrink" failure, which is invisible in the log precisely because each missing
    // satellite looks routine.
    UPROPERTY(SaveGame) bool bOffsetsCaptured = false;
    UPROPERTY(SaveGame) FVector VanillaCoreLocation = FVector::ZeroVector;
    UPROPERTY(SaveGame) float VanillaCoreYawDeg = 0.0f;

    // TODO (2026-07-31, ns-review-h3 H4 -- RETIRE THE h2-1 TEST SAVE, do not write a migration).
    // A save written by the h2-1 build has bCaptured=false on every satellite (the field did not
    // exist), so CapturedSatelliteCount is 0 and such a well now reports expected=0 and refuses to
    // spawn anything. That is LOUD and it fails in the safe direction -- the vanilla well is never
    // suppressed, because suppression requires a COMPLETE spawn -- so it is a test-hygiene item, not a
    // code one. Any h2-1 test save must be discarded rather than migrated: a migration would have to
    // invent a rigid body it does not have, which is exactly the fabricated-capture failure the flag
    // exists to prevent. Remove this note once no h2-1 save is in circulation.
    //
    // ns-review-h2 F2: how many satellites the rigid body was captured from. Compared against
    // Satellites.Num() on every roll: RollWellLayout's merge can APPEND satellites to an entry that
    // was already placed (a satellite that streamed in for the first time after the relocation), and
    // those records have no capture. A mismatch is a Warning naming the difference, never a silent
    // top-up -- they are refused at spawn and still suppressed at the vanilla site.
    UPROPERTY(SaveGame) int32 CapturedSatelliteCount = 0;

    // Dealt destination for the CORE, before settling. Re-dealt (up to WellMaxGroupRedeals) when the
    // yaw search and the nudge budget are both exhausted.
    UPROPERTY(SaveGame) FVector DestCoreLocation = FVector::ZeroVector;
    UPROPERTY(SaveGame) bool bDestDealt = false;

    // The committed placement. PlacedCoreLocation carries the settled Z.
    UPROPERTY(SaveGame) FVector PlacedCoreLocation = FVector::ZeroVector;
    UPROPERTY(SaveGame) FRotator PlacedCoreRotation = FRotator::ZeroRotator;

    // The group yaw (degrees) whose FULL satellite set validated, and how far through this group's
    // seeded yaw permutation the search has got. Both persist: design §Q3a requires that a group
    // deferred mid-search RESUMES rather than restarts, for the same reason OverlapNudges persists.
    // The permutation itself is never stored -- it is recomputed from WellYawSeedFor(), a pure
    // function of persisted state, so it is byte-identical every load. A yaw drawn from frame time or
    // actor-iteration order would pass every test on the H2 list except the determinism one (T8).
    UPROPERTY(SaveGame) float GroupYawDeg = 0.0f;
    UPROPERTY(SaveGame) int32 YawCursor = 0;
    UPROPERTY(SaveGame) uint8 GroupNudges = 0;
    UPROPERTY(SaveGame) uint8 GroupRedeals = 0;

    // The whole footprint validated and the group has been committed to these coordinates.
    UPROPERTY(SaveGame) bool bGroupPlaced = false;

    // ================================================================================================
    // A3 (ns-review-h2-r2 §5, ns-review-h2-r3 §6) -- THE PLACEMENT CLAIM, AS A STORED FACT.
    // ================================================================================================
    // TRUE means: PlacedCoreLocation (and every bCaptured satellite's PlacedLocation) name coordinates
    // THIS ENTRY CURRENTLY OWNS -- actors of ours may be standing there and nothing may reclaim them.
    // FALSE means: this entry names no coordinate at all.
    //
    // WHY THIS FIELD EXISTS AND WHY IT IS NOT A FIFTH LIFECYCLE FLAG. Seven cold-review rounds each
    // closed every prior finding and each found a NEW instance of ONE failure: "the entry has
    // abandoned its coordinate" was a DERIVED PREDICATE over four flags (bGroupPlaced, bRelocate,
    // bRelocationFailed, and Placed* being non-zero as a proxy), re-derived independently at every
    // reader, and every round found a new state tuple some reader mis-read:
    //   * h5 F2   -- the ten refusal `continue`s (flags said abandoned, Placed* said owned)
    //   * h2-6    -- the escalation ladder (destination rewritten, Placed* left behind)
    //   * h2-7 FA -- re-enrolment (bRelocate true AND bRelocationFailed false: a SEARCH-lifecycle
    //                state read as an ABANDONMENT state)
    //   * h2-7 F2 -- the withdrawal destroyed the very coordinate the measurement needed
    // The generator is the DERIVATION. This field turns it off: the claim is WRITTEN when it becomes
    // true and CLEARED when it becomes false, and readers ASK instead of inferring.
    //
    // THE COMPLETE WRITER CENSUS. Do not add another without editing this block AND both
    // *** CLAIM INVARIANT VIOLATED *** strings, which send the debugger straight here.
    // (ns-review-h2-r4 round 9 R-4: this block used to name ONE true-setter and the migration was the
    // unnamed second one, so every violation line pointed the reader AWAY from the writer that had most
    // likely produced the state. ns-review-h2-r4 round 9 R-5: it also still pointed the clearer at
    // NodeShuffleWellSweep.cpp, which is now READERS ONLY -- the clearer moved to
    // NodeShuffleWellClaim.cpp in h2-9 and this was the one surviving stale pointer, in the packet's
    // single most-read block.)
    //   set true  (1 of 2, THE STEADY-STATE SETTER) : NodeShuffleWellRelocateApply.cpp:180, beside
    //               `E.PlacedCoreLocation = CoreLoc;` -- the ONLY non-zero write of PlacedCoreLocation
    //               in the packet. This is the writer in every normal session.
    //   set true  (2 of 2, VERSION-GATED, AT MOST ONCE PER SAVE) : MigratePreA3PlacementClaimsOnce,
    //               NodeShuffleWellClaim.cpp:356. It asserts the claim back from a non-zero Placed*,
    //               i.e. it IS the pre-A3 derivation. It can only run while WellClaimMigrationVersion
    //               reads below WellClaimMigrationCurrentVersion. IF `WELLH2-CLAIM [backfill]` printed
    //               `version 0 -> 1` THIS SESSION, SUSPECT THIS ONE FIRST -- and if it prints that on
    //               every load, WellClaimMigrationVersion is not surviving the save round trip (P-15)
    //               and this migration, not the commit, is the writer producing the state you are
    //               looking at.
    //   set false (1 of 1) : ClearAbandonedWellPlacement (NodeShuffleWellClaim.cpp) -- the ONLY
    //               function that zeroes Placed*. All three abandonment events route through it.
    // INVARIANT A3 (enforced in the log, not just asserted in prose):
    //   bPlacementClaimLive == false  <=>  PlacedCoreLocation.IsNearlyZero()
    //   and, in the one direction that can actually arrive (ns-review-h2-r4 F-7):
    //   bPlacementClaimLive == false  =>   no bCaptured satellite names a PlacedLocation either.
    // ValidateWellClaimInvariant() checks BOTH every sweep and logs *** CLAIM INVARIANT VIOLATED *** if
    // a future edit breaks either. That line is the thing to grep before believing any sweep number.
    // The mirror ("claim live => every bCaptured satellite names one") is deliberately NOT enforced:
    // the only state that produced it was the roll zeroing satellites ahead of its commit-refusal
    // exit, removed in ns-review-h2-r4 F-5. Do not widen this definition without widening the check --
    // the two disagreeing IS the erosion mechanism A3 exists to stop.
    //
    // A LIVE CLAIM IS NOT ETERNAL (ns-review-h2-r4 D-2). An entry that holds a live claim while
    // mid-assembly and makes NO fresh footprint commit for WellClaimMidAssemblyMaxPasses consecutive
    // reconciliation passes has its claim withdrawn, loudly. Before that bound existed, disabling the
    // feature mid-spawn left the claim live for the life of the save.
    //
    // NOT the same idea as the SaveGame identity COMPONENT (ns-review-h2-r3 §5). This answers
    // "does this ENTRY still own the coordinate it names" (our save struct). The component would answer
    // "is this ACTOR ours, and whose" (the world). Neither subsumes the other; both are wanted.
    UPROPERTY(SaveGame) bool bPlacementClaimLive = false;

    // Every budget exhausted. The well stays exactly where the level author put it, for good, and the
    // roll never re-enrols it. Fail-safe to "untouched", never to "broken".
    UPROPERTY(SaveGame) bool bRelocationFailed = false;

    // ---- Packet H2b: the CORE's captured look, and the group-level capture verdict ----
    // The core is the member that carries MT_Core plus the MT_Crack ground graphic, and the crack is
    // both the piece design §2.4 says is most often forgotten and the one a player noticed left behind
    // at the abandoned original site.
    UPROPERTY(SaveGame) TArray<FNodeShuffleWellVisual> CoreVisuals;
    UPROPERTY(SaveGame) bool bCoreVisualsCaptured = false;

    // Every member (core + every CAPTURED satellite) has at least one visual piece recorded. Once true
    // the capture pass is skipped entirely, so a group whose original site never streams again keeps
    // whatever it already has instead of re-scanning the world every ~5 s forever.
    UPROPERTY(SaveGame) bool bGroupVisualsComplete = false;
};

// Server-side brain of NodeShuffle.
//
// Lifecycle per session:
//   BeginPlay -> repeating timer -> Tick():
//     1. (first tick) roll the layout if the save has none, or re-roll if the
//        user turned on the Re-roll Now toggle.
//     2. apply the layout idempotently: retype vanilla nodes via the game's
//        native SaveGame class/purity overrides, deactivate inactive vanilla
//        nodes, spawn + dress missing new nodes, settle new nodes near
//        players, re-associate extractors that lost their (respawned) node.
//
// Hard safety rule enforced at roll AND apply time: a node with a miner,
// extractor or portable miner on it is never changed in any way.
UCLASS()
class NODESHUFFLE_API ANodeShuffleSubsystem : public AModSubsystem, public IFGSaveInterface
{
    GENERATED_BODY()

public:
    ANodeShuffleSubsystem();

    // IFGSaveInterface
    virtual bool ShouldSave_Implementation() const override { return true; }
    virtual bool NeedTransform_Implementation() override { return false; }
    virtual void PreSaveGame_Implementation(int32 saveVersion, int32 gameVersion) override;
    virtual void PostSaveGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void PreLoadGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void PostLoadGame_Implementation(int32 saveVersion, int32 gameVersion) override;
    virtual void GatherDependencies_Implementation(TArray<UObject*>& out_dependentObjects) override {}

    // playtest-fixes-1: `NodeShuffle.Here` console command (registered by the module). Logs the
    // player's exact position plus a census of everything NodeShuffle-related within CensusRadius:
    // layout entries (state + distance), streamed originals (hidden? radioactive? emitter live?),
    // and the water/depth test at the player's feet. Log-only; safe anywhere.
    void LogHereCensus() const;

    // cave-nodes-1: `NodeShuffle.SeedHere` console command. Plants a manual cave seed at the player's
    // feet — for roofed spots vanilla never put a node under (rock bridges, shelves, side tunnels).
    // Same guarantees as automatic seeds: the player standing there proves reachability, the roof
    // check keeps surface spots out (a buildable roof is rejected), and the floor is re-sampled at
    // the cell center. Counts toward the underground placement quota.
    void SeedCaveCellAtPlayer();

    // ns-review-g G1 (Packet G, CRITICAL fix — mechanism A from the review). Builds the auto-allow
    // pass's managed-node census from the ROLLED LAYOUT rather than from live/spawned actors: NodeShuffle
    // spawns relocated nodes lazily, only within SpawnRadiusCm (~600 m) of a player ("far nodes stay as
    // data until explored" -- see EnsureNewNodeSpawned), so a live-actor census only sees whatever
    // happened to stream in near the load point -- non-deterministic, and unstable across sessions/
    // vantage points. Every active FNodeShuffleEntry is dealt at roll time regardless of streaming state,
    // so this is complete and deterministic: the result depends only on the rolled layout, never on
    // where the player is standing. Public so FNodeShuffleModule::RunAutoAllowExtractorsIfEnabled (a free
    // function in NodeShuffleAutoAllowExtractors.cpp) can call it via TActorIterator, the same idiom
    // NodeShuffle.Here already uses to reach the subsystem from a console-command-style entry point.
    // OutTotalActiveEntries / OutUnresolvedEntries: the residual, much-narrower honesty log the review
    // asked to keep (ns-review-g G1's "(C) honesty log" recommendation) -- an active entry whose
    // resource or node class fails to resolve THIS pass contributes no group and is not silently
    // dropped from the log, even though the census no longer depends on player position or streaming.
    void BuildManagedNodeGroupsFromLayout(TArray<FNodeShuffleManagedGroup>& OutGroups,
        int32& OutTotalActiveEntries, int32& OutUnresolvedEntries) const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    // ---- periodic driver ----
    // (Named to not hide AActor::Tick(float) — clang errors on that for the
    // Linux server target even though MSVC accepts it.)
    void RefreshTick();

    // ---- roll ----
    // Strict gate for the INITIAL roll: requires 50+ pristine /Game/ vanilla nodes to be
    // live before a first shuffle. That is a cheap floor proving a POPULATED WORLD EXISTS.
    // (truthdiag-fixes F1: this used to say "proving the world is fully loaded". It does not
    // test that, and the premise behind it is false — every level-placed vanilla node is
    // already live at load. See MinVanillaNodesForRoll and IsWorldReadyForRoll's own comment
    // in NodeShuffleSubsystem.cpp for the measurement. Do NOT raise the threshold from 50;
    // it is deliberate and would treat a non-problem.)
    bool IsWorldReadyForRoll() const;
    // Shuffle-aware gate for the RE-ROLL path. A shuffled save has most of its
    // originally-vanilla nodes retyped (no longer /Game/) or destroyed, so the
    // pristine-vanilla count never reaches 50 and the strict gate above can never
    // pass on reload. Instead require enough loaded resource nodes of ANY kind,
    // after a short post-load settle. (ns-truth-diagnostics: this used to say the
    // check proves "the world has streamed in". It does not test that, and the
    // premise is false — see MinVanillaNodesForRoll in NodeShuffleSubsystem.cpp.)
    bool IsWorldReadyForReroll() const;
    void RollLayout(int32 Seed, bool bIsReroll);
    // ns-truth-diagnostics B: the once-per-roll `ROLLCENSUS:` truth line + the ZERO-ACTIVE alarm.
    // DIAGNOSTICS ONLY -- reads Layout and does one read-only live actor pass; writes nothing.
    // Every field it prints is a measurement and names its own test; nothing in it asserts a cause.
    // See its definition in NodeShuffleSubsystem.cpp for the rule and why the rule exists.
    void EmitRollCensus(int32 Seed, bool bIsReroll, int32 PoolSize, int32 TargetActive,
                        int32 OriginalsCaptured, int32 NewLocationCount,
                        const TMap<FString, int32>& PoolCountsByResource) const;
    // redesign-1: before a re-roll, UN-HIDE every previously-suppressed original node (and its
    // mesh actor) that is streamed in, so the world returns to its pristine state before the new
    // layout re-hides per the new roll. Nothing was ever destroyed (whole-actor hide is reversible),
    // so this is a clean restore. The reroll pool is SEEDED from the saved Layout's stored ORIGINAL
    // resources ("Re-roll pool" log line) and is then AUGMENTED BY TWO LIVE SCANS of the world — the
    // "Non-solid augment" and "Full re-scan augment" lines — so it is NOT streaming-independent.
    // (truthdiag-fixes F1: this used to claim "(streaming-independent), not from a live rescan". Both
    // augments exist and both scan live actors; the seed step is the only streaming-independent half.)
    void RestoreOriginalsForReroll();
    // Map-wide spread: distribute new locations across the bounding box of the
    // full known-node set (whole playable map) rather than clustered around the
    // currently-loaded vanilla nodes, so the initial roll is not bunched at the
    // player's load point. The bounding box spans the whole map on BOTH paths: on the
    // initial roll because every level-placed vanilla node is already live at load
    // (MEASURED — see MinVanillaNodesForRoll), and on re-roll because the saved
    // locations span it. (truthdiag-fixes F1: this used to say "Streaming-independent
    // on reroll", which implied the initial roll was streaming-GATED. It never was.)
    // VanillaLocations defines the map bounding box; AvoidLocations is the full
    // union of occupied/kept/original/pinned locations new nodes must be spaced
    // away from (FIX 2 overlap guard).
    // playtest-fixes-3: non-const — it now stores the computed deal box (SaveGame) so the water-locked
    // redeal can draw RANDOM map-wide candidates from the same box later, and consults/updates the
    // learned water grid.
    // cave-nodes-2: when OutUndergroundIndices is non-null (experimental placement on), each accepted
    // location had a natural-share chance of being a CAVE cell instead of a surface box draw; the
    // indices of cave picks are reported so the caller stamps Entry.bUnderground.
    void GenerateNewLocations(FRandomStream& Rng, const TArray<FVector>& VanillaLocations,
                              const TArray<FVector>& AvoidLocations,
                              int32 Count, TArray<FVector>& OutLocations,
                              TSet<int32>* OutUndergroundIndices = nullptr);
    bool ReadCustomLocationsJson(TArray<FVector>& OutLocations) const;
    void WriteGeneratedLocationsJson(const TArray<FVector>& Locations) const;

    // ---- apply ----
    void ApplyLayout();
    // redesign-3 BUG B: ONE-TIME post-load reconciliation. Spawned nodes are now ANodeShuffleResourceNode
    // with a UPROPERTY(SaveGame) EntryGuid that survives reload (Tags do NOT). Iterate the subclass, read
    // each saved EntryGuid, repopulate SpawnedNodes[guid] so EnsureNewNodeSpawned's SpawnedNodes.Find
    // guard skips re-spawning. Also pins occupied restored nodes. Includes the VERIFY-FIRST count.
    // real-class redesign: modded-origin nodes whose native visual we've already rebuilt this session
    // (keyed by entry guid), so the per-node ProcessEvent rebuild fires once, not every tick (W2).
    TSet<FGuid> ModdedVisualRebuilt;
    void AdoptRestoredSpawnedNodes();
    // coexist-veto-1 FIX A: REGISTRY-ONLY pre-pass run in BeginPlay, before the veto arms. Registers
    // restored spawned-node actors into the module's managed-node registry using the SAME identity
    // predicates as AdoptRestoredSpawnedNodes (legacy guid / runtime+location+resource), so an armed
    // KBFL destroyer's initial sweep — which runs at OnWorldBeginPlay, ~5 s before the first
    // RefreshTick adopts — cannot destroy restored nodes through an empty registry. Touches NOTHING
    // but the registry (no SpawnedNodes writes, no adopt logic, no entry mutations).
    void PreRegisterRestoredNodesForVeto();
    // real-class redesign: shared per-node adopt finalize (resource-complete, gates, register, attach
    // component, pin). A member function so it keeps the subsystem's Friend access to AFGResourceNode
    // internals. Returns true when it newly pins the entry as occupied.
    bool FinalizeAdoptedNode(class AFGResourceNode* Node, int32 EntryIdx);
    // real-class redesign: true if the node renders its OWN visual (self-rendering mesh component or a live
    // linked engine mesh actor), so we should keep its native look instead of dressing our fallback rock.
    // Lithium's Alkali node -> true; AllMinable item-nodes -> false (they get the fallback rock).
    bool NodeHasOwnVisual(class AActor* Node, class UStaticMeshComponent* ExcludeRock) const;
    // visfix-1 (user report: "coal node with quartz visual"): the deal deck assigns resources across
    // ALL entries, so a modded-CLASS node (AllMinable Res_*2_C, whose native mesh is a quartz
    // look-alike) can carry a VANILLA resource. When the ASSIGNED resource has a look we can render
    // (authored table row or captured visual), that look must win: hide the native mesh and dress our
    // rock. Native visuals only win for resources we cannot dress (lithium, uncaptured modded ores,
    // esc_ item resources keeping their dirty-quartz identity).
    bool ResourceHasAuthoredLook(UClass* ResourceClass);
    void HideNativeNodeMesh(class AFGResourceNode* Node, class UStaticMeshComponent* ExcludeRock);
    bool bAdoptedRestoredNodes = false;
    // redesign-3 BUG C: originals already deregistered from the scanner this session (path set, so we
    // call RemoveResourceNodeScan_Local/UpdateNodeRepresentation once per original, not every pass) + a
    // running count for the log evidence (redesign-2's calls were silent).
    TSet<FString> ScannerDeregistered;
    int32 ScannerDeregisterCount = 0;
    // redesign-1: spawn one of OUR relocated nodes (the only kind of active node besides the
    // untouched occupied originals). Handles solid (authored rock or quartz placeholder) and oil.
    void EnsureNewNodeSpawned(FNodeShuffleEntry& Entry, bool& bOutChangedWorld);
    // redesign-3b BLOCKER FIX: the "Resource"-profile UseBox a spawned node needs for interaction
    // (look-at, build-gun, miner placement, hand-mine) is a runtime NewObject component and is NOT
    // serialized, so a restored/adopted node loses it on reload -> non-interactable. Idempotent helper
    // that creates the box if the node has none — called on BOTH spawn AND adopt/early-return paths.
    void EnsureNodeUseBox(AFGResourceNode* Node);
    // redesign-9 (MK1 RESOURCE-SNAP DETECTION). One-shot SNAPDIAG: when a spawned node AND a nearby
    // VANILLA node are both streamed in, log the FULL collision/component state of EACH so the log names
    // the EXACT vanilla-vs-ours delta the extractor hologram cares about. Friend access reads mBoxComponent.
    void DiagnoseSnapState();
    bool bSnapDiagLogged = false;
    // Helper: dump one node's components/collision for SNAPDIAG.
    void LogNodeSnapState(AFGResourceNodeBase* Node, const TCHAR* Label) const;
    // redesign-12 VALIDDIAG (diagnostics only): collision (r10) + manager registration (r11) are BOTH
    // ruled out — the Mk1 hologram FINDS our node but REJECTS it at VALIDATION. One-shot side-by-side log
    // of OURS vs a nearby VANILLA node on the validation-relevant props/methods the extractor hologram's
    // CanOccupyResource/IsAllowedOnResource path reads, so the next log names the exact differing gate.
    void DiagnoseValidationGate();
    bool bValidDiagLogged = false;
    void LogNodeValidationState(AFGResourceNode* Node, const TCHAR* Label) const;
    // redesign-11 (REGISTER NODES WITH THE RESOURCE-NODE MANAGER). The Miner Mk1 extractor hologram finds
    // the node to snap to via AFGResourceNodeManager::GetClosestNode over the manager's mResourceNodes
    // list. Our runtime-spawned nodes never auto-join it (it's built from level nodes at world init), so
    // Mk1 snap fails even with a vanilla collision byte-match. Register each spawned node into mResourceNodes
    // at spawn AND on adopt-after-reload (the list is runtime, not save-persisted). Friend access. Idempotent.
    // Resolve the live AFGResourceNodeManager instance by actor iteration (its static Get(UWorld*) is NOT
    // dll-exported — LNK2019 if called). One manager per world.
    class AFGResourceNodeManager* GetNodeManager() const;
    void RegisterNodeWithManager(AFGResourceNode* Node);
    // redesign-11 SECONDARY: when we hide an original (SuppressOriginalNodes), remove it from mResourceNodes
    // so the player can't place a Mk1 on an invisible ghost original. Only ever removes originals we hid.
    void DeregisterNodeFromManager(AFGResourceNodeBase* Node);
    // REGDIAG: one-shot — log the manager's mResourceNodes count + whether our node is Contains()'d, so the
    // next test confirms our nodes joined the manager (mirrors how SNAPDIAG confirmed the collision match).
    bool bRegDiagLogged = false;
    // Spawn-on-discovery: true if any player is within SpawnRadius of Loc.
    bool IsLocationNearAnyPlayer(const FVector& Loc, float RadiusCm) const;
    // redesign-1 (Hide & Replace): hide EVERY unoccupied original node (vanilla AND modded) + its
    // rock whenever it streams in. This IS the new core — all unoccupied originals are gone, their
    // resources live on as our spawned relocated nodes. Occupied/pinned originals are never touched.
    // Driven by the persistent OriginalNodeRecord so it works across sessions and stream-ins.
    void SuppressOriginalNodes();
    // rehide-1: called only from SuppressOriginalNodes' path-miss branch, when a stale record's
    // VanillaNodePath no longer resolves (a spawner mod re-created the node with a fresh id since last
    // process boot). Recovers identity from location (Rec.TrueLocation, or Rec.Location for legacy
    // unstamped records) + class + resource against CandidatePool (built once per pass by
    // BuildRematchCandidatePool). On a match: rebinds Rec's VanillaNodePath/TrueLocation AND the
    // originating layout entry's VanillaNodePath/OriginalTrueLocation (found by the OLD path), marks
    // the winning candidate in BoundCandidates (so a later record this pass can't claim it too), logs
    // once (REHIDE), and returns the live actor so the caller falls through the UNCHANGED hide funnel.
    // Returns null (zero mutation) when nothing matches within AdoptMatchRadiusCm — fail-closed: the
    // record stays stale and is retried next pass. Adds ZERO hide/suppress logic of its own.
    AFGResourceNodeBase* TryRematchStaleRecord(FNodeShuffleSuppressedOriginal& Rec,
        const TArray<AFGResourceNodeBase*>& CandidatePool, TSet<AFGResourceNodeBase*>& BoundCandidates);
    // rehide-1: candidate pool for TryRematchStaleRecord — VanillaNodeCache filtered down to the
    // actor-kind pre-gates that make a re-match safe (design §3.1/§3.4): valid, runtime-only
    // (!IsNetStartupActor — a level actor can NEVER be re-matched), non-transient (parity with capture
    // eligibility, :5822-5827), not one of our own nodes, not already managed/adopted, not fracking.
    // Built lazily by the caller: once per pass, only once a record's path misses.
    void BuildRematchCandidatePool(TArray<AFGResourceNodeBase*>& OutPool) const;
    // rehide-1: stale records already reported "no-match" this session — throttles the no-match REHIDE
    // log to once per record (the record itself still retries silently every pass, covering late/lazy
    // spawners; see TryRematchStaleRecord).
    TSet<FString> RematchNoMatchLogged;
    // P5 (addenda item 3): the standalone CaptureOriginalNodeRecord() declaration that used to sit
    // here was dead code (no callers) and has been removed. OriginalNodeRecord — the persistent record
    // of every unoccupied original node location (vanilla AND modded) that SuppressOriginalNodes hides
    // — is built inline inside RollLayout's Hide & Replace conversion instead.
    void SettleNewNodesNearPlayers();
    void ReassociateOrphanedExtractors();
    // knowledge-1 item 2: extractors whose resource binding this session already healed/refreshed
    // (bounded once-per-extractor logging + no re-heal churn). Weak keys — dead actors drop out.
    TSet<TWeakObjectPtr<const AActor>> ExtractorsHealed;
    // knowledge-2 item 3: extractors already truth-dumped this session (diag-gated once-per-extractor
    // ground-truth line: bound object validity, resolved resource, node-at-location match).
    TSet<TWeakObjectPtr<const AActor>> ExtractorsDumped;
    // knowledge-1 item 1: on authority, once per load after the layout is applied, register the
    // DISTINCT modded resources the shuffle actively manages with the game's scanner-unlock list
    // (AFGUnlockSubsystem::UnlockScannableResource — SaveGame + Replicated, so it persists). This is
    // what lets resource scanners AND mods that gate extractors on scanner knowledge (SF+'s Modular
    // Miner checks GetScannableResources().Contains via KLib's HasInformationAboutOre) recognize
    // shuffled modded resources whose own unlock schematics never ran in this save. Vanilla
    // resources are NEVER touched (their scanner unlocks are progression). Config-gated
    // (UnlockModdedKnowledge, default ON); idempotent per load (Contains gate).
    // knowledge-2 item 2 (CRASH-PROOF ORDERING): scanner knowledge now IMPLIES KAPI MinerInfo. When
    // KAPI is present, ProvideKAPIMinerInfo runs FIRST and only ores with an mMinerMapping entry
    // (pre-existing or freshly provided) are unlocked — KLib's AKLMMBuildableMiner::BeginPlay
    // fgcheckf's a valid description for any placeable ore, so unlocking without one ARMS a crash
    // (live: esc_CateriumIngot_C). Returns false only when the pass must be RETRIED next tick
    // (KAPI present but its scan hasn't populated yet); the caller latches bKnowledgeUnlockDone
    // only on true.
    bool UnlockModdedScannerKnowledge();
    bool bKnowledgeUnlockDone = false;
    // Packet G (ns-automatch): once per load (and once more after a re-roll -- re-armed alongside
    // bKnowledgeUnlockDone in RollLayout, same reasoning: a re-roll can change which node types are
    // actively MANAGED), run the auto-allow-extractors pass. Mirrors UnlockModdedScannerKnowledge()'s
    // own idiom exactly: FNodeShuffleModule::RunAutoAllowExtractorsIfEnabled() returns false only when a
    // dependency (the recipe manager) isn't ready yet, so RefreshTick retries; true means the pass
    // COMPLETED this tick (including "disabled" and "SF+ absent" -- there is nothing further to retry).
    bool bAutoAllowExtractorsDone = false;

    // ns-h1b-notice: the deferred emitter. Called every RefreshTick; does nothing at all unless a pass
    // queued something. Defined in NodeShufflePendingNoticeEmit.cpp (ns-review-notice2 F-B: it used to
    // say NodeShufflePendingNotice.cpp "next to the copy it delivers", which the file split inverted --
    // the copy now lives in the OTHER file). The seam between the two is WHEN to speak (this function,
    // in ...Emit.cpp) versus WHAT to say (the FNodeShuffleModule statics, in NodeShufflePendingNotice.cpp).
    // Neither lives in this file's already-7000-line .cpp.
    void EmitPendingNoticeIfReady(bool bNoticesEnabled);

    // ---- ns-h1b-notice: the deferred "restart required" player notice ----
    // The pass completes ~19-50 s after boot, when the world exists but the local player's chat widget
    // may not. So the pass QUEUES and RefreshTick emits, once the player has actually spawned.
    //
    // ALL THREE MEMBERS ARE TRANSIENT BY DESIGN -- no UPROPERTY(SaveGame) anywhere in this block, and
    // that is load-bearing, not an omission. PENDING is a STATE recomputed every pass (written documents
    // MINUS what SF+ already allows); it empties itself one boot after the documents land, which is the
    // only reason this notice cannot nag. Persisting any of this across boots would fight that mechanism
    // and could suppress a notice the player genuinely needs after a rebuild.
    TArray<FNodeShufflePendingEntry> PendingNoticeQueue;
    // LOG-ONLY BREADCRUMB -- NOT a decision input. ns-review-notice2 F-C: this used to claim it was what
    // stopped the measured double-pass from double-messaging. It WAS, until F4 moved that job to
    // AnnouncedPendingKeys below; since then it is written once and read only by log format strings.
    // Kept deliberately, because a human reading the log wants to see the previous set alongside the new
    // one when a decision line says EMIT or SUPPRESSED -- but a field documented as load-bearing while
    // nothing reads it is exactly the documentation falsehood this packet exists to stop, so it says so.
    FString LastNotifiedSignature;
    // ns-review-notice F4: the SET of pending keys already announced this session. The signature above is
    // a set IDENTITY, which makes any change look new -- including a re-roll that merely SHRINKS the
    // pending set, which would post a second "restart required" message naming a strict subset of what
    // was already said. This set turns the test into "does the new set contain anything unannounced?",
    // so a shrink is silent while a genuinely new entry still gets through. Transient, like everything
    // else in this block.
    TSet<FString> AnnouncedPendingKeys;
    // Two-stage emit gate: -1 = nothing armed; 0 = a valid local player+pawn was seen this tick, wait
    // one more tick (~5 s) so the chat widget is up before we post; 1 = emit now.
    int32 PendingNoticeGateTicks = -1;
    // ns-review-notice F3: bounded retries. If the chat manager never materialises after the player gate
    // has passed, an uncapped retry logs three lines every ~5 s for the rest of the session (~720/hour)
    // for a message that will never arrive. This workspace has already paid once for unbounded per-call
    // log output; give up loudly instead.
    int32 PendingNoticeEmitAttempts = 0;
    // scanregen-1 (P2 design §4 touch-point 1): knowledge-unlock -> scanner/radar-tower refresh
    // trigger. UnlockModdedScannerKnowledge() has TWO callers — RefreshTick (world-settled) and
    // PostLoadGame_Implementation (mid save-load, before actor settling) — so the producer only
    // RECORDS that an unlock landed; only RefreshTick ACTS on it (never PostLoadGame — see the
    // knowledge-3 save-crash ordering class this avoids repeating).
    bool bScannerClusterRefreshPending = false;
    // scanregen-1: per-pass collapse so a re-roll tick (whose OWN existing call sites already invoke
    // RefreshScannersAndRadarTowers up to twice) can't make the new consume point a third redundant
    // call in the same pass. Set INSIDE RefreshScannersAndRadarTowers itself; cleared at the top of
    // every RefreshTick, before any early-out (design §9 amendment relies on this surviving a SKIP).
    bool bScannerRefreshedThisPass = false;
    // scanregen-1: resource classes unlocked by the CURRENT pending batch — read once by the
    // diagnostics-gated pre-invalidate cluster census in RefreshScannersAndRadarTowers, guarded by
    // bScannerClusterRefreshPending so a stale leftover list from an already-consumed batch is never
    // read (the producer resets + repopulates this every pass it runs, whether or not it unlocks).
    TArray<TSubclassOf<UFGResourceDescriptor>> ScanRegenUnlockedClasses;
    // knowledge-2 item 1: runtime MinerInfo provisioning — PURE REFLECTION against KAPI's
    // UKAPIDataAssetSubsystem (a UGameInstanceSubsystem; no KAPI include/link/stub anywhere). For
    // each managed modded ore MISSING from mMinerMapping, template-clone an existing description
    // (preferring the Desc_Stone_C entry), rewire its key (mResourceClass) and every
    // FKAPIModuleItems.mProductionItem to the ore (the resource descriptor IS the item class —
    // UFGResourceDescriptor : UFGItemDescriptor), insert into mMinerMapping and
    // mAllowedScannableResources exactly as KAPI's own ScanForMinerAssets does. knowledge-3: each
    // clone lives in the /NodeShuffle/RuntimeMinerInfo runtime package under a deterministic
    // NSMinerInfo_<Ore> name (RF_Public|RF_Standalone + rooted) so KLib's SaveGame reference to it
    // (mExtractionInfo) serializes down FObjectReferenceDisc's ASSET branch (LevelName empty,
    // absolute path) — the GameInstance-outered knowledge-2 clone crashed the save writer's
    // level-resolution machinery. Returns false when KAPI is present but not yet scanned (defer).
    // When KAPI is absent: returns true with bOutFilterUnlocks=false (no filtering — no Modular
    // Miner exists to crash). Every reflection lookup is null-checked; any failure skips that ore
    // entirely (never partially-wired).
    bool ProvideKAPIMinerInfo(const TArray<UClass*>& ManagedModded, TSet<UClass*>& OutWithMinerInfo,
                              bool& bOutFilterUnlocks);
    // knowledge-2: bounded defer while KAPI's game-instance-init scan hasn't populated mMinerMapping
    // yet (in practice it has, ~70 s before our first pass; an installation with KAPI but zero miner
    // description assets would otherwise defer forever). After the cap: terminal withhold-all.
    int32 KnowledgeDeferPasses = 0;
    static constexpr int32 KnowledgeDeferMaxPasses = 24; // ~2 min at the 5 s tick
    void RefreshScannersAndRadarTowers();
    // Removes one-off resource deposits sitting on shuffled nodes when their
    // resource contradicts the node's assigned one (runs once per session).
    void SweepMismatchedDeposits();

    // ---- helpers ----
    // Property-based eligibility (no class-name/form-list hardcoding). Eligible =
    // a genuine AFGResourceNode with a UFGResourceDescriptor resource, plain Node
    // type (GetResourceNodeType() == EResourceNodeType::Node — no geyser/fracking/
    // deposit), not skeletal/transient. bIncludeModded admits non-/Game/ resource
    // descriptors (AllMinable etc.). bIncludeLiquid admits non-solid forms: solid is
    // always allowed; liquid (oil) AND gas (e.g. lithium, Desc_OreLithium form=RF_GAS)
    // join when it is true (callers now pass true). Nothing is special-cased by mod/class name.
    static bool IsEligibleVanillaNode(const AFGResourceNode* Node, bool bIncludeModded, bool bIncludeLiquid);
    // Same gate, but reports WHY a node was rejected (for the once-per-class
    // modded-eligibility diagnostic). Accepts genuinely-functional modded nodes
    // (lithium's BP_ResourdeNode_Alkali_C, AllMinable item-style nodes) by driving
    // entirely off node PROPERTIES: relaxes purity/amount for non-/Game/ resources
    // (purity normalized at roll time) while keeping the principled junk exclusions
    // (skeletal, transient, non-resource-descriptor, deposit, geyser/fracking).
    static bool IsEligibleVanillaNodeReason(const AFGResourceNode* Node, bool bIncludeModded,
                                            bool bIncludeLiquid, const TCHAR*& OutReason);
    // FIX 4: once-per-node-class diagnostic naming why each MODDED node class is or
    // isn't shuffled. Turns a silent "lithium never shuffles" into a log line that
    // states the exact gate. Runs at roll time.
    void DiagnoseModdedNodeEligibility(bool bIncludeModded, bool bIncludeLiquid) const;
    TSet<FString> ModdedEligibilityLogged;
    // redesign-5 SECONDARY: distinct node-class|resource-class keys already logged by the UPSTREAM-SCAN
    // diagnostic at the top of the initial-roll node loop (so each class is reported once, not per node).
    TSet<FString> UpstreamScanLogged;
    // redesign-6 FIX 2: one-shot diagnostic — walk the full class hierarchy of every distinct actor whose
    // class name/path contains "esc_" or "AllMinable", so we learn their REAL base type (the analyst
    // proved esc_ nodes are NOT AFGResourceNode; this names what they actually are). Runs once per roll.
    void DiagnoseEscClassHierarchy();
    // True if the node has an extractor or a portable miner on it.
    bool IsNodeOccupiedAnyway(const AFGResourceNode* Node) const;
    // FIX B: true if this actor is a fracking core or satellite (by node TYPE, not
    // mesh name) — those wells (core + satellites + activator) are left EXACTLY as
    // vanilla: never deactivated, retyped, relocated, re-skinned, or rock-hidden.
    static bool IsFrackingActor(const AActor* Actor);

    // redesign-1 (Hide & Replace). The 21-build reskin saga is GONE. We no longer touch any
    // existing node's rock: every UNOCCUPIED original node is hidden whole-actor and its resource
    // is re-spawned as OUR OWN node at a relocated location. This is the visual for those spawned
    // nodes — one mod-owned AFGNodeMeshActor at the node's OWN transform (no significance fight, no
    // instanced wall, no offset). Vanilla resource -> authored ResourceNode_<X>_01 mesh+materials;
    // MODDED resource (no authored entry) -> the quartz placeholder (Desc_RawQuartz_C visual).
    // Strong-ref'd in SpawnedMeshActors (decay-proof). Liquids draw a decal instead (no rock).
    void SpawnVisualRockForNode(AFGResourceNode* Node, UClass* ResourceClass, const FGuid& EntryGuid);
    // Resolve & cache a resource's authored node MESH (the big node rock ResourceNode_<X>_01, NOT
    // the deposit outcrop). Null if the resource has no table entry (-> caller uses the quartz
    // placeholder for modded resources).
    UStaticMesh* ResolveNodeMesh(UClass* ResourceClass);
    TMap<FString, TWeakObjectPtr<UStaticMesh>> NodeMeshCache;
    // Resolve & cache a resource's authored PER-SLOT node materials from the table. Empty when the
    // resource has no table entry.
    const TArray<TWeakObjectPtr<UMaterialInterface>>* ResolveNodeMaterials(UClass* ResourceClass);
    TMap<FString, TArray<TWeakObjectPtr<UMaterialInterface>>> NodeMaterialCache;
    // The quartz placeholder visual (mesh + materials) used for any spawned node whose resource has
    // no authored table entry (modded resources: esc_/lithium/etc.). Resolved from Desc_RawQuartz_C.
    UStaticMesh* GetQuartzPlaceholderMesh();
    const TArray<TWeakObjectPtr<UMaterialInterface>>* GetQuartzPlaceholderMaterials();
    // playtest-fixes-1 (modded-descriptor visuals): capture the visual of a hidden ORIGINAL whose
    // resource is a real UFGResourceDescriptor with NO authored table entry (RP thorium, bamrenew
    // lead, FF dirt). Sources in order: its OWN paired AFGNodeMeshActor (engine links), then
    // dirtdress-1: its OWN static-mesh components / attached mesh actors (self-rendering node BPs
    // that have no engine links — FF dirt was 0-for-98 on the pairing lottery). Called from
    // SuppressOriginalNodes (pairing is fresh each pass). On a NEW capture, already-spawned nodes
    // of that resource are re-dressed so they swap quartz -> the real look without a respawn.
    // Returns TRUE when the resource is capture-ELIGIBLE but nothing could be captured YET (no
    // pairing, no own mesh) — the caller then defers the steady-hidden mark so the original is
    // re-attempted next pass instead of losing its one shot per session (Fertilized-dirt bug).
    bool CaptureOriginalVisualIfNeeded(class AFGResourceNodeBase* Node);
    // dirtdress-1: resources whose full capture-decision chain was already logged this session
    // (diagnostics are once per resource per session, so retries can't spam the log).
    TSet<FString> CaptureChainLogged;
    // dirtdress-1 (cold review): terminal cap for the capture retry loop — same count-then-tombstone
    // idiom as ExternalDestroyCounts/DormantThisSession. A resource that reports capture-PENDING for
    // CaptureGiveUpPasses consecutive attempt-passes (~3 min at the 5 s tick) goes terminal for the
    // session: its originals steady-mark as normal and the placeholder stays (e.g. a modded node
    // whose only visual is an instanced-mesh component). All session-only; a successful capture
    // clears its resource's counter, and RollLayout's clear block resets all three with the sibling
    // sets. PendingThisPass is the per-pass scratch that turns per-ORIGINAL pending reports into one
    // per-RESOURCE count (processed + cleared at the end of each SuppressOriginalNodes pass).
    TSet<FString> CapturePendingThisPass;
    TMap<FString, int32> CapturePendingPasses;
    TSet<FString> CaptureTerminalThisSession;
    static constexpr int32 CaptureGiveUpPasses = 36;
    // knowledge-1 item 3: bounded give-up for spawns that fail every pass (evidence: 675 identical
    // "Failed to spawn new node (Node_BioWaterSF+_C)" warnings in ~4 min — SpawnActor returns null
    // each attempt, likely spawn-gated by the owning mod). Consecutive per-ENTRY failures; at
    // SpawnGiveUpAttempts the entry gives up and parks. P5 (addenda item 1 / P3 design §2.9 decision
    // Q6): this is NOT a one-time terminal event per entry — P3's deckevict-1 (EvictSpawnRefusingClass)
    // can REVIVE a parked entry onto a substitute class when one exists, resetting this budget, so the
    // SAME entry can give up again on the new class. The give-up therefore fires ONCE PER (entry,
    // class), bounded at 1 + MaxSubstituteChain (currently 3) total give-ups before the entry parks for
    // good with no further substitute to try — not once per entry. Same lifecycle as the capture retry
    // budget otherwise: success clears the counter, RollLayout's clear block resets all, and everything
    // is session-only (no SaveGame). FlagsLogged bounds the one-shot per-CLASS class-flag breadcrumb
    // (diagnostics-gated) that hints WHY the class refuses to spawn.
    TMap<FGuid, int32> SpawnFailCounts;
    TSet<FGuid> SpawnParkedThisSession;
    TSet<FString> SpawnFailFlagsLogged;
    static constexpr int32 SpawnGiveUpAttempts = 10;
    // P3 (deckevict-1): node classes that PROVED unspawnable this session (SpawnActor returned null
    // through the whole give-up budget) -> the class path we substitute for them. An EMPTY value means
    // "evicted, but no viable substitute exists" (entries stay parked). Keyed by CLASS PATH, not by
    // entry guid, so it survives a re-roll (which mints new EntryGuids) with zero re-seeding and is
    // inherently idempotent. SESSION-ONLY BY DESIGN: spawn-gating is a property of the loaded MOD
    // STACK, not of the world, so nothing here may reach the save. Deliberately NOT cleared in
    // RollLayout's reset block even though its siblings are -- a re-roll does not un-gate a class.
    TMap<FString, FString> SpawnRefusedClassSubstitute;
    // Classes we have WATCHED spawn successfully this session, by resource form (1 solid / 2 liquid).
    // Only /Game/ (vanilla) paths are recorded: a vanilla-origin node takes our fallback rock (see the
    // bVanillaOrigin dress branch), so a substitute drawn from here is guaranteed visible.
    TMap<uint8, FString> ProvenSpawnClassByForm;
    static constexpr int32 MaxEvictedClassesPerSession = 8;
    static constexpr int32 MaxSubstituteChain = 2;
    // P3: log-once latch for the eviction-cap Display line (design §5 line B) — the cap condition
    // itself (SpawnRefusedClassSubstitute.Num() vs MaxEvictedClassesPerSession) needs no counter.
    bool bEvictionCapLogged = false;
    void RedressSpawnedOfResource(const FString& ResourceClassName);
    // Find a persisted capture for a resource short name (null when none).
    const FNodeShuffleCapturedVisual* FindCapturedVisual(const FString& ResourceClassName) const;
    // Resolved-capture runtime cache: mesh/materials LoadObject'd once per session per resource.
    struct FNodeShuffleResolvedCapture
    {
        TWeakObjectPtr<UStaticMesh> Mesh;
        TArray<TWeakObjectPtr<UMaterialInterface>> Materials;
        FVector Scale = FVector(1.0f, 1.0f, 1.0f);
    };
    TMap<FString, FNodeShuffleResolvedCapture> ResolvedCaptureCache;
    const FNodeShuffleResolvedCapture* ResolveCapturedVisual(const FString& ResourceClassName);
    // redesign-1: place the starter node set (config) near the captured player-start on the FIRST
    // roll of a brand-new game. Drawn from the pool when possible. Spawned as normal new entries.
    void AppendStarterNodes(TArray<FNodeShuffleEntry>& NewLayout, FRandomStream& Rng,
                            const TArray<FVector>& AvoidLocations);
    // Re-applies the native visual (mesh-actor refresh or oil/gas decal) a node draws
    // for its CURRENT descriptor, by invoking the game's own OnRep_ResourceClassOverride via
    // ProcessEvent. Handles liquids (oil decal, no rock) and the no-mesh-actor case.
    // Packet H1: parameter WIDENED AFGResourceNode* -> AFGResourceNodeBase* so a fracking CORE can use
    // it. A core is an AFGResourceNodeBase but NOT an AFGResourceNode (design §2.1 -- the single biggest
    // asymmetry in Packet H), so the narrower signature could not reach it. The body is unchanged and
    // touches only UObject methods (IsValid / FindFunction / ProcessEvent), so this is a pure widening:
    // every pre-existing caller passes an AFGResourceNode* and binds exactly as before, and the function
    // still dispatches to whichever OnRep_ResourceClassOverride override the concrete class has
    // (AFGResourceNode's for nodes and satellites, AFGResourceNodeBase's for cores).
    void RebuildNodeNativeVisual(AFGResourceNodeBase* Node);
    // Per-pass spawned-visual coverage counters (reset + logged each ApplyLayout).
    int32 SpawnedRockVanilla = 0;
    int32 SpawnedRockQuartz = 0;
    int32 SpawnedRockLiquid = 0;
    // redesign-6 FIX 1: limit the per-node RENDERDIAG runtime-state log to the first N spawned + first N
    // adopted nodes (so we get the truth without spamming thousands of lines). Session counters.
    int32 RenderDiagSpawnLogged = 0;
    int32 RenderDiagAdoptLogged = 0;
    static constexpr int32 RenderDiagMax = 10;
    // redesign-6 FIX 2: one-shot esc_/AllMinable class-hierarchy diagnostic done flag.
    bool bEscClassHierarchyLogged = false;

    float LastOrphanSweepSeconds = 0.f;        // last orphan-rock cleanup, for rate-limiting
    // redesign-1 backstop: hides any node-rock mesh left with no node behind it (a rare
    // actor-independent rock at a suppressed original spot). World-state based, rate-limited.
    void OrphanRockCleanup();
    // Rock components already explained by OrphanRockCleanup's per-rock reason
    // log, so the reason for each is stated at most once (hidden OR why-not).
    TSet<TWeakObjectPtr<UStaticMeshComponent>> OrphanReasonLogged;
    // Shared rock-mesh name predicate used by SweepRockComponents, OrphanRockCleanup
    // and the diagnostic. Matches the game's node-rock mesh naming families plus the
    // user's NodeShuffle_RockPatterns.json prefixes. ExtraPatterns may be empty.
    static bool IsNodeRockMeshName(const FString& MeshName, const TArray<FString>& ExtraPatterns);
    // Reads NodeShuffle_RockPatterns.json prefixes (best-effort; empty on miss).
    void LoadExtraRockPatterns(TArray<FString>& OutPatterns) const;

    AFGNodeMeshActor* FindMeshActorForNode(AFGResourceNodeBase* Node) const;
    // Returns the live original node for a path as AFGResourceNode (null for Base-only esc_ originals).
    AFGResourceNode* FindVanillaNodeByPath(const FString& Path) const;
    // redesign-6 FIX 2: the live original as AFGResourceNodeBase (covers esc_ Base-only originals too).
    AFGResourceNodeBase* FindOriginalBaseByPath(const FString& Path) const;
    static UClass* LoadClassByPath(const FString& Path);
    // P3 (deckevict-1): the class an entry should ACTUALLY spawn from. Identical to Entry.NodeClassPath
    // unless that class was evicted this session (SpawnActor proved it unspawnable). Never mutates the
    // entry -> nothing persists; the save stays honest about what the world originally held, and the
    // workaround evaporates when the stack changes.
    FString ResolveSpawnNodeClassPath(const FNodeShuffleEntry& Entry) const;
    // P3: called only from the give-up block, on the FIRST give-up for a class. Idempotent (a Contains
    // check is the first statement). Picks a substitute via PickSubstituteClass, records it (possibly
    // empty = no viable substitute), then sweeps Layout reviving every OTHER entry already parked or
    // mid-budget on the same refused class — the whole point of evicting class-wide instead of per-entry.
    void EvictSpawnRefusingClass(const FString& RefusedPath, const FNodeShuffleEntry& Cause);
    // P3: ordered substitute-selection ladder (design §2.4) — gas gate first (DESCRIPTOR form, never
    // the ResourceForm byte), then a class proven spawnable THIS session, then a data-only layout scan,
    // then our own always-spawnable C++ fallback (solid only), then park (liquid with nothing proven).
    // Returns empty when no viable candidate exists.
    FString PickSubstituteClass(const FString& RefusedPath, const FNodeShuffleEntry& Cause) const;
    // playtest-fixes-1 / steepfix-1: optional out-flag distinguishes the definitive UNPLACEABLE
    // failure (the probe HIT ground but it is underwater OR steeper than a Miner tolerates, and the
    // 300 m spiral found no flat land) from the ambiguous no-terrain-hit defer (unstreamed terrain or
    // true void). Unplaceable triggers an immediate redeal; void keeps defer-and-retry.
    // cave-nodes-1: underground entries settle via a SHORT local trace (RaycastSettle reads
    // Entry.bUnderground) so the probe stays inside the cavern instead of hitting the roof.
    bool RaycastSettle(FNodeShuffleEntry& Entry, const AActor* IgnoreNode, const AActor* IgnoreMesh,
                       bool* bOutWaterNoLand = nullptr) const;

    // ---- cave-nodes-1..4: cavern discovery + placement (both always-on; placement = drain fix) ----
    // The shuffle DRAINED caves: originals inside caverns are hidden and the 200 m top-down settle ray
    // can only reach the outermost surface, so replacements never land inside. Discovery maps cavern
    // floors from PROVEN seeds (hidden originals that sit under a roof — vanilla only put nodes where
    // players can go) via a budgeted trace flood-fill that follows the floor through long winding
    // passages (step <=2.5 m, headroom >=3.5 m, still-roofed, dry), stopping at cave mouths. Cells are
    // persisted GLOBALLY (Configs/NodeShuffle_CaveFloors.json — map-static, like the water grid).
    // PLACEMENT (always on — cave-nodes-4): the shuffle used to DRAIN caves (hid their originals,
    // never placed anything back), a regression this fixes. Caves are ADDITIONAL RANDOM AREAS —
    // every deal draw (roll, relocation spots, water-locked redeal) picks a cave cell with natural
    // probability CaveSeedCount/poolSize (capped 25%). No quota, no top-up, no preference.
    struct FNodeShuffleCaveCell
    {
        float FloorZ = 0.0f;
        uint8 State = 1; // 1=frontier (expandable), 2=expanded, 4=mouth (walkable; no expansion past)
        // cave-nodes-2: measured ceiling clearance (roof hit - floor). Placement requires enough for
        // a Miner building; low passages stay mapped for connectivity only. -1 = unknown (legacy
        // imports) = treated as tall (the user stood there and chose it).
        float CeilingCm = -1.0f;
    };
    mutable TMap<int64, FNodeShuffleCaveCell> CaveFloors;
    mutable TSet<FString> CaveSeedsDone; // original node paths already roof-classified (persisted)
    mutable int32 CaveSeedCount = 0;     // seeds that WERE under a roof (the placement quota)
    mutable bool bCaveStoreLoaded = false;
    mutable bool bCaveStoreDirty = false;
    mutable int32 CaveStoreNewRecords = 0;
    void EnsureCaveStoreLoaded() const;
    void FlushCaveStoreIfDirty() const;
    // Roof-classify one resolved original (once per path, persisted): an up-trace that hits within
    // 150 m means the node sits under a roof -> cave seed cell at its own floor.
    void ClassifyOriginalUnderground(class AFGResourceNodeBase* Node, const FString& Path);
    // Budgeted per-pass flood-fill from frontier cells near players (traces need streamed collision).
    void ExpandCaveFloorsBudgeted();
    // cave-nodes-2 (user call: caves are ADDITIONAL RANDOM AREAS, never a quota to fill): the forcing
    // machinery (CaveTopUpPass + cave-first redeal priority) is GONE. Instead, every location deal —
    // roll, relocation spots, water-locked redeal — draws a cave cell with natural probability
    // CaveSeedCount / poolSize (what vanilla's own cave-node density encodes), capped at 25%.
    // Pick a random placeable cave cell (fully roofed + ceiling tall enough for a Miner). Raw variant
    // does NO spacing (the deal loop applies its own avoid/spacing checks); the Layout variant spaces
    // MinNodeSpacing (3D) against the live layout for redeals.
    bool TryPickRawCaveCell(FRandomStream& Rng, FVector& OutLoc);
    bool TryPickCaveCell(FRandomStream& Rng, FVector& OutLoc);
    int32 CountUndergroundEntries() const; // census/diagnostic only

    // playtest-fixes-3 (water-locked redeal, UNANCHORED): an entry whose spot is confirmed water-locked
    // is RE-DEALT to a fresh RANDOM location in the SAME map-wide deal box the roll used — anchoring to
    // streamed originals (fixes-1) clustered relocations around original sites and defeated the mod's
    // randomization (user call). The new spot is filtered by the LEARNED WATER GRID + MinNodeSpacing,
    // then settles LAZILY like any dealt entry (spawn-on-discovery when a player nears it). A spot the
    // grid doesn't know yet may hit water again — that settle teaches the grid one more cell and hops
    // once more; convergence is exponential (~25% water share per blind draw) and fully random.
    // Returns true when the entry was moved (NOT settled — bRayCasted stays false).
    bool TryRedealWaterLockedEntry(FNodeShuffleEntry& Entry);
    // ---- playtest-fixes-3: learned water grid (the "excluded areas" store) ----
    // The game has NO complete pre-stream water data at runtime (AFGWorldSettings::mWaterVolumes is
    // transient, "currently streamed in"). So we LEARN it: every RaycastGroundAt hit records land/water
    // at a 100 m cell, persisted GLOBALLY (Configs/NodeShuffle_WaterGrid.json — the map is static, so
    // knowledge carries across saves and re-rolls). Rolls and redeals reject known-water cells up
    // front. Cell states: 1=land, 2=water, 3=mixed (coastline; NOT excluded — the settle probe decides).
    void RecordWaterGridSample(const FVector& Loc, bool bWater) const;
    bool IsKnownWaterCell(const FVector& Loc) const;
    void EnsureWaterGridLoaded() const;
    void FlushWaterGridIfDirty() const;
    // bakedmaps-2: the mod SHIPS snapshots of the learned water/cave knowledge EMBEDDED in the DLL
    // (NodeShuffleBakedData.h, regenerated by Scripts/bake_maps.ps1 — the packaging pipeline ships
    // only Binaries + Paks, so loose files can never reach users). Loaders merge baked AFTER local
    // with local-wins (veterans keep their own learning; a merge dirties the store so the union
    // persists into the user's local file once). Content variants parse a string; Json variants load
    // a file and delegate. Return cells added, -1 when absent/unparseable.
    int32 MergeWaterGridFromContent(const FString& Content, const TCHAR* SourceLabel, bool bKeepExisting) const;
    int32 MergeWaterGridFromJson(const FString& Path, bool bKeepExisting) const;
    int32 MergeCaveStoreFromContent(const FString& Content, const TCHAR* SourceLabel, bool bKeepExisting,
                                    int32* OutFileSeedCount = nullptr) const;
    int32 MergeCaveStoreFromJson(const FString& Path, bool bKeepExisting, int32* OutFileSeedCount = nullptr) const;
    mutable TMap<int64, uint8> WaterGrid;
    mutable bool bWaterGridLoaded = false;
    mutable bool bWaterGridDirty = false;
    mutable int32 WaterGridNewSamples = 0;
    // Deal box captured at roll time (SaveGame) so redeals draw from the same distribution; for saves
    // rolled before this build the box is derived lazily from the Layout's own locations (they span
    // the map). MeanZ seeds the redealt entry's probe height.
    FVector GetDealBoundsMin() const;
    FVector GetDealBoundsMax() const;
    void EnsureDealBoundsDerived() const;
    // Transient per-entry redeal attempt counter — salts the deterministic RNG so a failed redeal
    // tries different anchors/offsets next pass instead of repeating the same candidates forever.
    TMap<FGuid, int32> RedealAttempts;
    // Entries confirmed water-locked at least once this session (drives the deferral summary + census).
    mutable TSet<FGuid> WaterLockedThisSession;
    // playtest-fixes-1 (defer-log backoff): first-occurrence-only detail logs; repeats are silent.
    // 153k defer lines in one session came from re-logging every retry of every stuck entry.
    TSet<FGuid> DeferLoggedThisSession;
    mutable TSet<FGuid> WaterDeferLoggedThisSession;
    int32 DeferredThisPass = 0;
    int32 LastDeferSummary = -1;

    // ---- coexist-1: external-destroy backoff + idempotent maintenance pass (ALL transient) ----
    // Another installed mod can DESTROY resource-node actors outright (observed: a KBFL actor-listener
    // targeting FGResourceNodeBase). Re-materializing every pass against such a destroyer is a
    // destroy/respawn war: log firehose, scanner/radar refresh churn, multi-second stutters. Backoff:
    // count destroys of OUR spawned actors that NodeShuffle did NOT perform itself; after
    // ExternalDestroyTombstoneAt of them in one session the entry goes DORMANT (skipped entirely) until
    // the next world load. NOTHING here is SaveGame — every load resets the tombstones, so each session
    // makes one cheap attempt per node and records are never lost if the destroyer relents.
    // Self-vs-external discrimination: NodeShuffle's ONLY self-destroy of spawned nodes (the re-roll
    // wipe in RollLayout) removes the SpawnedNodes slot synchronously in the same block — so a slot
    // holding an invalid/null actor when the pass looks is proof of an EXTERNAL destroyer. Any future
    // self-destroy site MUST keep that invariant (destroy + remove the slot together).
    static constexpr int32 ExternalDestroyTombstoneAt = 2;
    TMap<FGuid, int32> ExternalDestroyCounts; // per-entry external destroys this session
    TSet<FGuid> DormantThisSession;           // tombstoned entries: no re-materialize until next load
    int32 LastDormantSummaryNum = 0;          // ungated coexistence summary fires only on count change
    // Idempotent-pass markers: work that only matters ONCE PER LIVE INSTANCE (dress, use-box, manager
    // registration, hide funnel) is skipped while the SAME instance stays valid — keyed by weak ptr so
    // a reload/respawn/re-stream (new instance) naturally falls through to the full path again.
    TMap<FGuid, TWeakObjectPtr<AFGResourceNode>> SteadyAliveNodes;          // spawned nodes fully asserted
    TMap<FString, TWeakObjectPtr<AFGResourceNodeBase>> SteadyHiddenOriginals; // originals fully hidden
    // One ungated hide-funnel totals line per LOAD (the per-pass HIDEDIAG stays diagnostics-gated and
    // now only logs when its numbers CHANGE); per-pass change tally feeds the gated "pass: 0 changes".
    bool bLoadFunnelLogged = false;
    int32 LastHideFunnel[8] = { -1, -1, -1, -1, -1, -1, -1, -1 }; // dirtdress-1: +capturePending slot; rehide-1: +rematched slot
    int32 SuppressChangesLastPass = 0;
    float LastRockBackstopSeconds = 0.f; // stray-rock backstop cooldown (forced when a node newly hides)
    // ---- T4 MESH-HIDE LATENCY (docs/TECH-DEBT.md T4) ----
    // Promoted from `namespace NodeShuffleMeshHideLatency` in NodeShuffleSubsystem.cpp by ns-t7-split.
    // Read and written ONLY by SuppressOriginalNodes; every field keeps its original name suffix and
    // its original initial value. THE WORLD-CHANGE RESET AT THE TOP OF SuppressOriginalNodes IS KEPT:
    // GetTimeSeconds restarts with the world, so a second save load in one process must not inherit
    // the previous world's timestamps -- and keeping the guard is what makes this promotion provably
    // behaviour-identical, because it fires on a fresh instance (MeshHideLatencyWatchWorld == nullptr)
    // exactly as it fired on fresh module statics.
    TMap<FString, FNodeShuffleMeshHideWatch> MeshHideLatencyWatch; // keyed by VanillaNodePath, same key as SteadyHiddenOriginals
    const void* MeshHideLatencyWatchWorld = nullptr;
    int32 MeshHideLatencyPassIndex = 0;          // passes of SuppressOriginalNodes that processed records
    int32 MeshHideLatencyResolvedLaterTotal = 0;
    int32 MeshHideLatencyStillVisibleWhenResolvedTotal = 0;
    float MeshHideLatencyMaxDelaySeconds = -1.0f; // -1 = no delay has ever been measured this session
    float MeshHideLatencyLastDelaySeconds = -1.0f;
    int32 MeshHideLatencyLastSummary[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    // playtest-fixes-1 (ghost radiation): resolve the radioactivity subsystem via the GameState's
    // public inline getter (AFGRadioactivitySubsystem::Get is a static whose export is not trusted —
    // same LNK2019 class of problem as AFGResourceNodeManager::Get).
    class AFGRadioactivitySubsystem* GetRadSubsystem() const;
    int32 RadEmittersRemoved = 0; // running total, logged in the hide summary
    // FIX A (land-only placement): true if a world point sits inside a streamed-in
    // water volume (ocean/lake/river). Used to reject settle hits on the seafloor:
    // the down-ray hits solid terrain, but if that impact point is underwater the
    // node is invisible to the player. Definitive (volume containment), not a
    // sea-level guess. Returns false when no water is streamed near the point.
    bool IsPointInWater(const FVector& Point) const;
    // FIX A: single raycast that also rejects water. Out params return the grounded
    // location/rotation/water-state. Helper that RaycastSettle's relocation loop
    // drives over offset candidates.
    // cave-nodes-1: bShortTrace probes only +-4/8 m around StartZ (underground entries — the long ray
    // would hit the cave roof); short traces also skip the deep-water floor reclassification (the cell
    // was depth/water-verified at discovery) but keep the volume test.
    // slopefit-1: bOutTooSteep reports a CLIFF-face hit (steeper than CliffSlopeDeg; long traces only
    // — cave floors were walkability-checked at discovery). Cliff hits are LAND for the water grid
    // but unplaceable: the spiral/redeal machinery moves the entry. OutRot is the node-actor rotation
    // with tilt CLAMPED to NodeTiltClampDeg toward the SMOOTHED ground normal (4-probe ring average);
    // OutGroundNormal returns that full smoothed normal so the rock visual can take the whole slope.
    bool RaycastGroundAt(const FVector& ProbeXY, float StartZ, const AActor* IgnoreNode,
                         const AActor* IgnoreMesh, FVector& OutLoc, FRotator& OutRot,
                         bool& bOutWater, bool bShortTrace = false, bool* bOutTooSteep = nullptr,
                         FVector* OutGroundNormal = nullptr) const;
    // slopefit-1 RETRO-FIT: nodes settled under the old full-tilt rule get the clamped-actor rotation
    // + full-slope rock alignment re-applied ONCE on adopt (rotation only; never occupied/pinned).
    TSet<FGuid> AdoptRotRefit;
    void LogLayoutSummary() const;
    // Writes RerollNow=false back to the live config and flushes it to disk so the
    // one-shot "Re-roll Now" toggle fires exactly once per enable. Returns true on
    // success (property found and marked dirty).
    bool ClearRerollNowFlag();
    // Diagnostic: names rock-like meshes near players and why they aren't paired
    // (distance to nearest entry, that entry's active/paired state). Once each.
    void DiagnoseRocksNearPlayers();
    TSet<TWeakObjectPtr<UStaticMeshComponent>> DiagnosedComponents;

    // ---- persisted state ----
    UPROPERTY(SaveGame) int32 SavedSeed = 0;
    UPROPERTY(SaveGame) bool bLayoutGenerated = false;
    // Current layout-FORMAT version. Bump this whenever FNodeShuffleEntry / the saved layout semantics
    // change, and add a migration case in PostLoadGame_Implementation. Saves stamp LayoutVersion with this
    // at roll time; on load, an older saved value triggers migration (see PostLoadGame_Implementation).
    static constexpr int32 CurrentLayoutVersion = 2;
    UPROPERTY(SaveGame) int32 LayoutVersion = 1;
    UPROPERTY(SaveGame) TArray<FNodeShuffleEntry> Layout;

    // redesign-1: persistent record of EVERY unoccupied original node location (vanilla AND
    // modded), used by SuppressOriginalNodes to hide each original whole-actor whenever it streams
    // in. Reliable across sessions; the catch-all for orphan rocks. Rebuilt from the layout each roll.
    UPROPERTY(SaveGame) TArray<FNodeShuffleSuppressedOriginal> OriginalNodeRecord;

    // playtest-fixes-1: visuals captured from hidden originals' paired mesh actors (modded-descriptor
    // resources with no authored entry). Persisted: next session resolves by asset path immediately.
    UPROPERTY(SaveGame) TArray<FNodeShuffleCapturedVisual> CapturedVisuals;

    // playtest-fixes-3: the percentile deal box + mean probe height captured at roll time, reused by
    // the unanchored water-locked redeal. Zero when the save predates this build (derived lazily then).
    UPROPERTY(SaveGame) FVector DealBoundsMin = FVector::ZeroVector;
    UPROPERTY(SaveGame) FVector DealBoundsMax = FVector::ZeroVector;
    UPROPERTY(SaveGame) float DealMeanZ = 0.0f;
    // Lazily-derived fallback box for pre-fixes-3 saves (transient).
    mutable FVector DerivedBoundsMin = FVector::ZeroVector;
    mutable FVector DerivedBoundsMax = FVector::ZeroVector;
    mutable bool bDerivedBoundsReady = false;

    // redesign-1: STARTER NODES. Captured player-start world location (the player pawn's position at
    // the earliest tick it exists on a brand-new game) and whether starters have already been placed.
    // Both SaveGame so starters are placed exactly once per save and never on an existing save.
    UPROPERTY(SaveGame) bool bStarterNodesPlaced = false;
    UPROPERTY(SaveGame) bool bPlayerStartCaptured = false;
    UPROPERTY(SaveGame) FVector PlayerStartLocation = FVector::ZeroVector;

    // ---- session state ----
    FTimerHandle TickTimerHandle;
    // Previous-tick value of the Re-roll toggle, for EDGE-TRIGGERED re-roll: the re-roll fires on each OFF->ON
    // transition of Config.RerollNow — so it works the same whether the user set it and reloaded (load-time) or
    // toggled it mid-session (LIVE, no reload). Edge-triggering also prevents a loop if the config clear is slow.
    bool bPrevRerollNow = false;
    bool bLoggedDisabled = false;
    bool bDidInitialApply = false;
    bool bDepositSweepDone = false;

    // Live nodes for new-node entries (spawned this session + adopted-on-load). redesign-5: the visual
    // rock is now a RockMesh subobject OF each node (no separate rock actors — that machinery is gone).
    UPROPERTY() TMap<FGuid, AFGResourceNode*> SpawnedNodes;

    // Cache: original-node path -> live BASE node, rebuilt when stale. redesign-6 FIX 2: broadened from
    // AFGResourceNode to AFGResourceNodeBase so esc_ (Base-only) originals can be found + hidden too.
    TMap<FString, TWeakObjectPtr<AFGResourceNodeBase>> VanillaNodeCache;
    // Node -> its own AFGNodeMeshActor (engine back/forward link), rebuilt every ApplyLayout pass.
    // Used by SuppressOriginalNodes to hide an original node's paired mesh actor on stream-in.
    TMap<TWeakObjectPtr<AFGResourceNodeBase>, TWeakObjectPtr<AFGNodeMeshActor>> MeshActorCache;
    void RebuildMeshActorCache();

    // ---- Packet H1 (ns-wells-h1): IN-PLACE RESOURCE-WELL RETYPE ----
    // Everything below is DEFINED IN NodeShuffleWellRoll.cpp (the deal) and NodeShuffleWellRetype.cpp
    // (the write), not in NodeShuffleSubsystem.cpp; NodeShuffleWellRetype.h holds the shared pure
    // helpers and states which file owns what. They must be MEMBERS (not free functions in those files)
    // because writing AFGResourceNodeBase's PRIVATE mResourceClassOverride needs this class's
    // AccessTransformers Friend grant, and C++ friendship is class-to-class -- exactly the constraint
    // FNodeShuffleModule::DbgLogAcceptance documents for the module side. Defining members across extra
    // translation units keeps the code out of an already 8000-line file without giving up that access.

    // Deals a resource to every non-pinned well. Called from RollLayout (initial roll AND re-roll) so
    // wells re-roll with the rest of the layout. No-op (and leaves any existing assignment untouched)
    // when the ShuffleResourceWells config toggle is off.
    void RollWellLayout(int32 Seed, bool bIsReroll);

    // Idempotent per-pass apply, called from ApplyLayout. Writes the dealt resource to the core AND
    // every satellite, asserts they agree afterwards, and NEVER touches purity. bWellShuffleEnabled is
    // passed in rather than re-read so ApplyLayout's single config fill stays the only one per pass.
    void ApplyWellRetype(bool bWellShuffleEnabled);

    // Writes ResourceClass onto ONE well member (core or satellite) and rebuilds its native visual.
    // Returns true when it actually changed something (so the caller's per-pass counters only count
    // real writes, keeping the steady-state log silent). MemberRole/CoreName are for the log line only.
    bool RetypeWellMember(AFGResourceNodeBase* Member, UClass* ResourceClass,
                          const TCHAR* MemberRole, const TCHAR* CoreName);

    // Packet H1: a well is one thing with N members, so it gets its own SaveGame array rather than
    // being squeezed into Layout. Empty on every save where ShuffleResourceWells was never turned on,
    // which is what makes the mod's stable core byte-identical with the feature off.
    UPROPERTY(SaveGame) TArray<FNodeShuffleWellEntry> WellLayout;
    UPROPERTY(SaveGame) bool bWellLayoutRolled = false;

    // Session-scoped log throttles -- the apply pass runs every ~5 s over every well, so any line that
    // is not delta-driven would be a firehose (the 153k-line / 35 MB precedent this file already
    // documents for the deferral log). None of these are persisted.
    TSet<FString> WellAppliedLogged;   // core paths whose successful retype has been announced
    TSet<FString> WellSkipLogged;      // core paths whose skip reason has been announced
    int32 WellMembersWrittenThisSession = 0;
    bool bWellDisabledLogged = false;  // "feature off but this save has well data" -- said once
    // "feature ON but this save has no roll yet" -- said once. Its own latch, not shared with the one
    // above: the two states are opposites and a user can move between them mid-session by toggling, so
    // one shared flag would silently suppress the second message.
    bool bWellNoRollLogged = false;
    // Apply-pass counter, used ONLY to fire the once-per-session live-vs-layout well census on a
    // settled world. Apply runs every ~5 s, so pass 6 is roughly 30 s after the layout starts applying
    // -- late enough that streaming has caught up, early enough to be in the log before the player
    // reaches a well. A boolean latch would have fired on the first pass, mid-load, and reported a
    // half-streamed world as the answer.
    int32 WellApplyPasses = 0;
    static constexpr int32 WellCensusScanPass = 6;

    // ===================== Packet H2 (ns-wells-h2): RIGID WELL RELOCATION =====================
    // Defined in NodeShuffleWellRelocateRoll.cpp (capture + deal), NodeShuffleWellRelocateApply.cpp
    // (the yaw search, footprint validation and group-atomic spawn) and NodeShuffleWellLink.cpp (the
    // mCore lifecycle). Members for the same reason H1's are: writing AFGResourceNodeBase's private
    // mResourceClassOverride/mPurityOverride AND AFGResourceNodeFrackingSatellite's private mCore all
    // depend on this class's AccessTransformers Friend grants, and friendship is class-to-class.

    // Called from RollWellLayout's tail, on the SAME roll. Captures each eligible well's rigid body
    // from the live actors and deals it a destination. Never captures a partially-streamed well.
    void RollWellRelocation(int32 Seed, bool bIsReroll, bool bRelocationEnabled);

    // Per-pass driver, called from ApplyLayout right after ApplyWellRetype. Spawn-on-discovery: a
    // group is only searched/placed once a player is within SpawnRadiusCm of its dealt destination.
    void ApplyWellRelocation(bool bWellShuffleEnabled, bool bRelocationEnabled, float SpawnRadiusCm);

    // The §Q3a search for ONE group: settle the core, then walk the seeded yaw permutation from
    // YawCursor (budgeted per pass), validating the FULL satellite footprint each time. Returns true
    // only when the whole footprint validated and E's Placed* fields were committed.
    bool TryPlaceWellGroup(FNodeShuffleWellEntry& E, UClass* ResourceClass);

    // ns-review-h2 F3: the ONE escalation ladder -- nudge, then re-deal, then leave the well vanilla
    // for good. It exists as a function because it used to exist as two inline copies and only one of
    // them was complete: the core-rejection copy nudged, hit the cap, and then re-probed the identical
    // rejected spot forever, with GroupNudges (a SaveGame uint8) wrapping at 256 to a zero-radius
    // nudge. A well dealt into a lake was trapped permanently and the trap was saved.
    // bHaveSettledCore: true when AnchorLoc is a genuinely settled point to spiral out from; false
    // when it is only the last (rejected) probe, in which case the dealt destination is used.
    void EscalateWellPlacement(FNodeShuffleWellEntry& E, const FVector& AnchorLoc, bool bHaveSettledCore,
                               const TCHAR* Why);

    // ns-review-h2 F8: a void (unstreamed) probe is a legitimate defer that spends no budget -- but an
    // UNBOUNDED, Verbose-only defer is indistinguishable from a working feature nobody has walked to.
    // Counted per group, named once at WellVoidDeferNoticeAt, and escalated at WellVoidDeferEscalateAt.
    void NoteWellVoidDefer(FNodeShuffleWellEntry& E, const TCHAR* Which, const FVector& Probe,
                           const TCHAR* Who, bool bDiag);
    TMap<FString, int32> WellVoidDefers;             // core path -> consecutive void probes (session only)
    static constexpr int32 WellVoidDeferNoticeAt = 20;
    static constexpr int32 WellVoidDeferEscalateAt = 60;

    // One member's terrain test at a candidate XY: settle, water, slope, resource-node overlap and
    // buildable overlap. OutReason is filled with a short token for the rejection log.
    bool ValidateWellMemberSpot(const FVector& ProbeXY, float StartZ, FVector& OutLoc, FRotator& OutRot,
                                FString& OutReason) const;

    // Group-atomic spawn (design Q1's decision): core deferred-spawned first, then EVERY satellite
    // deferred-spawned with mCore pre-set, then all finished -- so a core can never exist without its
    // satellites, and BeginPlay does the registration.
    //
    // ns-review-h2 F7 (HIGH): returns true ONLY when the group is COMPLETE (every captured satellite
    // live, none failed). It used to return "did anything spawn at all", so one satellite out of ten
    // flipped bGroupPlaced and SuppressVanillaWellGroup then hid the entire vanilla group -- a short
    // well with no visible original, which is precisely the partial-well state this packet claims not
    // to have. The claim was true of VALIDATION and false of SPAWN; now it is true of both.
    bool SpawnWellGroup(FNodeShuffleWellEntry& E, UClass* ResourceClass);

    // ns-review-h3 H1 (BLOCKING): the counterpart the packet was missing entirely. Nothing in H2 ever
    // destroyed a spawned well actor -- the two runtime maps were only ever Add/FindRef/Contains -- so
    // every path that MOVED a group (a re-search after a partial spawn, a re-roll re-enrolment, the
    // give-up branch) left the previous actors alive at the abandoned coordinates. Called at all three
    // of those sites, BEFORE any destination is rewritten.
    //
    // DESPAWN-ON-MOVE, deliberately NOT SetActorLocationAndRotation: a teleported AFGResourceNode may
    // not update its paired mesh actor, its scanner representation or its manager entry, and all three
    // of those paths are closed-source, so the move-in-place version could only be hoped correct.
    //
    // Occupied members are NEVER destroyed -- destroying an actor under a player's machine would be
    // worse than the duplicate it prevents. Such a group is abandoned in place, its vanilla twin stays
    // suppressed, and it is logged loudly. Returns false when anything was refused for occupancy.
    bool DespawnWellGroup(FNodeShuffleWellEntry& E, const TCHAR* Why);

    // Drops any runtime handle whose actor is no longer at the coordinate the entry now names.
    // ns-review-h4 F3: RETURNS the count it could NOT clear (the member is in use), and SpawnWellGroup
    // requires that to be zero before calling a group COMPLETE. That moves the drift check from
    // DETECTION to PREVENTION -- suppression of the vanilla well is the irreversible half, and it must
    // not happen while a counted member is somewhere else entirely.
    int32 DespawnStaleWellMembers(FNodeShuffleWellEntry& E);

    // ns-review-h4 F2 (BLOCKING): the teardown driven by the HANDLE SET rather than by call sites.
    // RollWellRelocation's despawn is reached only after an entry survives six earlier `continue`s,
    // each of which clears bRelocate and touches neither map -- so an entry that becomes pinned (or
    // unstreamed, or non-finite, or count-mismatched, or geometry-refused) after an INCOMPLETE spawn
    // strands its actors permanently, in the maps and invisible to an audit that walks bGroupPlaced
    // only. Anything in either map with no corresponding placed group is an orphan REGARDLESS of the
    // route that produced it, which is what makes this structurally immune to the next new route as
    // well as to these six.
    // ns-review-h5 F-1 (HIGH, silent): pass A is handle-driven and is therefore blind to an abandoned
    // ACTOR THAT HOLDS NO HANDLE -- reachable by saving inside an INCOMPLETE spawn window, because our
    // well actors are save-collected but AdoptRestoredWellGroups skips non-placed entries, so both maps
    // come back empty for that group and nothing in the packet can see its actors again. Pass B is a
    // LOCATION-driven backstop over the actor iterators, run at the SETTLED phase only.
    // ns-review-h5 F-2: an entry also owns a handle whose actor is at its CURRENTLY COMMITTED
    // coordinate, so a group mid-assembly is not torn down by the sweep that runs later in the same pass.
    //
    // ns-review-h5 F1 (BLOCKING, the finding that PARKED this packet) -- bRelocationOn IS NOT OPTIONAL.
    // ApplyWellRelocation computes bOn = bWellShuffle && bRelocation and its `!bOn` block deliberately
    // does NOT return (already-relocated wells in an existing save must keep being spawned, linked and
    // suppressed whatever the config now says). The sweep therefore used to run with the feature OFF --
    // and with an empty WellLayout both AccountedFor and Held are empty, so pass B classified EVERY
    // runtime fracking core/satellite in the world as unaccounted and destroyed it at apply pass 8,
    // ~40 s into every session, in saves where the player never enabled relocation. That would have made
    // NodeShuffle the node-destroyer it already ships a two-layer defence against (cookbook §20), with
    // no equivalent opt-out. The gate is now a PARAMETER rather than a read of config inside here, so a
    // caller cannot forget it and cannot get a different answer than the pass it belongs to.
    //
    // ns-review-h2-r2 F-B (BLOCKING) -- BOTH GATES BELONG TO PASS B ONLY, AND USED TO GATE EVERYTHING.
    // The two gates above exist for pass B's WORLD SCAN: with an empty accounted-for set a world scan
    // calls the whole vanilla well population orphaned. Pass A is a different animal -- it only ever
    // destroys actors whose handles WE created, which is true whatever the config says and whatever
    // `placedGroups` is -- so gating it gave up the h4-F2 reclamation for nothing, in EXACTLY the window
    // that produces abandoned handles: every entry enrolled and none placed yet means placedGroups == 0,
    // so a re-roll that refuses an INCOMPLETE-spawned group left our actors standing while the log
    // printed "SWEEP SKIPPED ... placedGroups=0", which reads as healthy. Same over-reach with the
    // toggles OFF over a save that still holds handles. Gates now wrap the backstop call alone; the
    // reconciliation and pass A always run, and the skip line names WHICH pass was skipped.
    void SweepOrphanedWellActors(const TCHAR* Phase, bool bRelocationOn);
    static constexpr int32 WellOrphanSweepCadence = 12; // ~1 min at one apply pass per ~5 s
    bool bWellOrphanInUseLogged = false;                // F-4: permanent state, said once per roll

    // ns-review-h2-r2 F-A: ONE copy of the "withdraw the claim of every entry that no longer owns it"
    // rule. It was written twice -- once at the roll tail, once inside the sweep -- which is the
    // two-copies-of-a-rule drift shape this packet has already been bitten by three times. Returns the
    // number of entries whose claim it actually withdrew. NEVER gated: it destroys nothing, and gating
    // it would leave stale claims sitting in the save whenever the sweep's pass-B gates are shut.
    int32 ReconcileAbandonedWellClaims(const TCHAR* Why);

    // The roll's TEARDOWN TAIL (withdraw every claim the roll abandoned -> compute the post-roll sweep
    // gate -> run the sweep). Moved out of RollWellRelocation into NodeShuffleWellSweep.cpp because that
    // file is where the sweep it drives lives, and because NodeShuffleWellRelocateRoll.cpp was at exactly
    // the 500-line limit when the F-A fix landed. Takes RelocateResourceWells as the caller received it;
    // it computes the real gate (wellShuffle && relocate) itself.
    void FinishWellRollTeardown(bool bRelocationEnabled);

    // PASS B, the location backstop -- split into NodeShuffleWellBackstop.cpp for the 500-line rule.
    // LOG-ONLY in this build (see that file's banner). Reports its two headline numbers to the sweep's
    // summary line; everything else it measures it logs itself.
    void RunWellLocationBackstop(const TCHAR* Phase, int32& OutExamined, int32& OutUnaccounted);

    // ns-review-h5 F1: a DESTROY CAP on pass A. Pass A is provably ours (it only ever destroys actors
    // whose handles WE put in SpawnedWellCores/SpawnedWellSatellites), so it stays destructive -- but a
    // wrong ownership index should cost one group, not the world. 16 = one maximal well (1 core + H0's
    // measured max of 10 satellites) plus headroom. Anything past the cap is deferred to the next sweep
    // and said loudly.
    static constexpr int32 WellSweepMaxDestroysPerPass = 16;
    bool bWellSweepGatedLogged = false;      // "PASS B skipped, and why" -- once per session (F-B)
    bool bWellSweepCapLogged = false;        // cap tripped -- once per session
    bool bWellBackstopLogOnlyLogged = false; // pass B is LOG-ONLY -- once per session

    // ns-review-h5 F2 (the ownership predicate's other half). E.PlacedCoreLocation / S.PlacedLocation
    // were never cleared when an entry was ABANDONED, so IsAtTarget() kept protecting pinned, failed and
    // refused entries forever and the sweep reported their stranded actors as "mid-assembly: OWNED, not
    // orphaned" -- a confident falsehood at the exact spot built to prevent them. This is the ROOT-CAUSE
    // half: the coordinates are dropped the moment the entry stops owning them. NEVER touches a placed
    // group (bGroupPlaced true means the entry owns those coordinates and the spawn/adopt/link/suppress
    // paths all read them).
    // Returns true when it actually dropped something, so callers can count and report.
    //
    // A3: this is now the SINGLE CLEARER of FNodeShuffleWellEntry::bPlacementClaimLive. It no longer
    // decides ANYTHING from the four lifecycle flags except the one safety gate below.
    // ns-review-h2-r4 F-2: it also REPAIRS the dead-claim-on-a-live-coordinate desync, LOUDLY -- the
    // pre-A3 self-heal A3 removed without a parity row. Lives in NodeShuffleWellClaim.cpp since the
    // ns-review-h2-r4 split (the CLAIM, separated from the SWEEP THAT READS IT).
    bool ClearAbandonedWellPlacement(FNodeShuffleWellEntry& E, const TCHAR* Why);

    // A3, THE ENFORCEMENT. INVARIANT A3 is `bPlacementClaimLive == false <=> PlacedCoreLocation is
    // zero`, plus (ns-review-h2-r4 F-7) `claim dead => no bCaptured satellite names a coordinate`.
    // Prose invariants in this packet have been silently eroded three times (h2-6 added three
    // ZeroVector writers to one that "held by construction"; h2-7 added a fourth). This walks the
    // layout every sweep -- ~20 entries, free -- and logs loudly on either direction of violation.
    // SILENT ON PASS except one "checked N, 0 violations" line per session, so the log proves it ran.
    // It LOGS ONLY; the repair lives in the single clearer (ns-review-h2-r4 F-2). Returns violations.
    // ns-review-h2-r4 F-1: it must be called BEFORE any repair at EVERY site that repairs -- which
    // includes FinishWellRollTeardown, where the reconciliation used to run first and erase the
    // violation before the check ever looked at it.
    int32 ValidateWellClaimInvariant(const TCHAR* Where);
    bool bWellClaimInvariantOkLogged = false;

    // A3 migration -- and ns-review-h2-r4 D-1 calls it what it is: the OLD `Placed*-non-zero-means-
    // ownership` DERIVATION, kept only because a save written before bPlacementClaimLive existed
    // deserialises it false while Placed* still names a real, built-on, producing well. h2-8 ran it
    // unconditionally on every load forever with no version gate; it is now one-shot PER SAVE via
    // WellClaimMigrationVersion, logs on BOTH branches (F-3), and is named for what it is. Returns
    // entries backfilled. Called once per session from AdoptRestoredWellGroups, before any reader.
    int32 MigratePreA3PlacementClaimsOnce();
    // The save's claim-migration version. 0 = written before A3 (or by h2-8, which had no gate).
    // FAILS OPEN: if this int does not survive the round trip either, it reads 0 and the migration
    // simply runs again -- exactly h2-8's behaviour, never worse.
    UPROPERTY(SaveGame) int32 WellClaimMigrationVersion = 0;
    static constexpr int32 WellClaimMigrationCurrentVersion = 1;

    // A3 / ns-review-h2-r3 F-2. Session-scoped, NOT saved: the coordinates ClearAbandonedWellPlacement
    // has zeroed this session. The withdrawal deletes the ONLY field that could identify a stranded
    // actor as ours, at the exact instant the actor becomes strandable -- so pass B's "is this actor
    // ours or a mis-classified vanilla well" discriminator measured the distance to an unrelated
    // destination and could never fire. Remembering the coordinate costs 12 bytes and makes it fire.
    // ns-review-h2-r4 F-5: SATELLITES ARE RECORDED TOO. H0 measured a minimum core->satellite distance
    // of 2076 cm against a 300 cm adopt radius and satellites outnumber cores ~4-8:1, so a core-only
    // memory left the discriminator dead for the more numerous class.
    TArray<FVector> AbandonedWellClaimCoords;

    // ================================================================================================
    // ns-review-h2-r4 ROUND 9 §3b-B -- THE SPAWN-TIME REGISTRY. THE ONE PART OF RT-6's ANSWER THAT
    // RESTS ON NOTHING BUT OUR OWN CODE.
    // ================================================================================================
    // Three review rounds have produced a stranded-actor discriminator that can FALSIFY "this actor is
    // ours" soundly and CANNOT AUTHORISE it: `persistentLevel=1 && standing on a live claim of ours`
    // still rests on "Satisfactory authors every fracking well into a streaming sublevel", which is a
    // fact about somebody else's map and is unverifiable from our headers. That asymmetry is why RT-6
    // cannot currently produce the positive evidence the deferred SaveGame identity component is gated
    // on -- a circle: the component waits on RT-6, and RT-6 waits on the component's premise.
    //
    // THIS BREAKS THE CIRCLE FOR HALF THE CLASS, at zero engine premise. Every actor SpawnWellGroup
    // creates is recorded here, and nothing ever removes it. So:
    //   * registry KNOWS the actor  => WE SPAWNED IT. Provable, from our own SpawnActorDeferred call.
    //     No level premise, no distance threshold, no serialization surface.
    //   * registry does NOT know it => it was NOT spawned in THIS session. Either vanilla, or ours
    //     from a PREVIOUS session (the cross-reload half, which only the identity component can settle).
    // It is deliberately NOT saved and NOT a UPROPERTY: a weak pointer is meaningless across a reload,
    // and A3-5 already has one unproven serialization surface in flight. Session-scoped is the whole
    // claim -- see the `spawnedThisSession=` field on the RT-6 verdict line.
    //
    // TWeakObjectPtr, never a raw pointer: entries go stale when actors are destroyed and a stale weak
    // pointer compares unequal to a fresh actor, so a recycled address can never be mis-claimed as ours.
    // NOTHING DESTRUCTIVE READS THIS. It is evidence for a human reading a log, exactly like `level=`.
    // TODO (2026-08-07, WIP): unbounded in principle -- one entry per well member ever spawned this
    // session, ~20 groups x ~8 members. If a real session ever makes this large, cap it the way
    // AbandonedWellClaimCoords is capped and say so in the log, LOUDLY (F-6's lesson).
    TSet<TWeakObjectPtr<AActor>> WellActorsSpawnedThisSession;
    static constexpr int32 WellAbandonedClaimCoordCap = 512; // pathological-loop guard, not a budget
    bool bWellClaimCoordCapLogged = false; // F-6: saturation was SILENT, and silently un-fixes F-2

    // ns-review-h2-r4 D-2 -- THE BOUND ON MID-ASSEMBLY, i.e. round 8's predicted round-9 finding,
    // fixed in advance. `(placed=false, relocate=true, failed=false, claim=live)` was skipped by the
    // reconciliation as "mid-search" with NO EXPIRY: spawn INCOMPLETE and then disable the feature (or
    // simply never return to the destination) and pass A protected those actors forever, pass B
    // reported them OURS-STRANDED forever, and the claim persisted in the save forever.
    // Core path -> consecutive reconciliation passes seen mid-assembly WITHOUT a fresh footprint
    // commit. The commit (NodeShuffleWellRelocateApply.cpp, beside the one setter) RESETS it, so a
    // group that is genuinely retrying can never expire and this cannot become the h5 F-2
    // destroy/respawn cycle. DELIBERATELY NOT a UPROPERTY(SaveGame): round 8 advised against a second
    // unproven serialization surface while A3-5 is still ungraded, so the bound is per-session --
    // which still converts "forever" into "bounded", the property being bought.
    TMap<FString, int32> WellClaimMidAssemblyPasses;
    // ~20 reconciliations. The sweep's cadence is one per WellOrphanSweepCadence apply passes (~60 s),
    // so this is ~20 minutes of NO PROGRESS, plus one tick per re-roll. Generous on purpose: expiring
    // a group a player is actively assembling would cost a destroy/respawn round-trip.
    static constexpr int32 WellClaimMidAssemblyMaxPasses = 20;

    // ================================================================================================
    // ns-review-h2-r4 ROUND 9 R-1 -- THE COUNTER ABOVE WAS PRINTED NOWHERE EXCEPT AT EXPIRY, WHICH MADE
    // RT-11'S NEGATIVE HALF A STEP THAT COULD NOT FAIL.
    // ================================================================================================
    // The claim "an actively-assembling group can never expire, because the footprint commit resets the
    // counter" is the single most load-bearing claim in D-2. The only observation that could falsify it
    // was `*** MID-ASSEMBLY CLAIM EXPIRED ***` -- which needs 20 reconciliation passes (~20 minutes) to
    // appear, while every step that was supposed to watch for it runs for minutes. So the step reported
    // GREEN under ANY behaviour of the counter: a vacuous pass, the class round 9 named
    // ("THE ACCEPTANCE GATE CANNOT FAIL").
    //
    // The fix is diagnostics, not a redesign: print the counter EVERY pass it is non-empty, name the
    // entry holding the maximum, and say WHICH of the three conditions caused the tick. The tester then
    // stands next to an assembling group and watches the max pin at 0-1, then walks away and watches it
    // climb -- a number that MOVES, in one minute, instead of a 20-minute absence.
    //
    // The two fields below are the cheapest way to make the third question answerable from inside the
    // reconciliation, which has no idea why the apply pass did not reach the commit. They are written
    // ONCE per ApplyWellRelocation pass and READ ONLY BY DIAGNOSTICS -- nothing behavioural reads them,
    // and no decision changes if they are stale. At the roll tail they may be one pass old (the roll
    // tail's reconciliation can run before the first apply pass of the session, in which case the
    // radius is still -1 and the reason is reported as UNKNOWN rather than guessed).
    bool  bWellLastApplyRelocationOn   = false; // the `bOn` the last apply pass computed
    float WellLastApplySpawnRadiusCm   = -1.0f; // <0 => ApplyWellRelocation has not run yet this session

    // ns-review-h5 judgement call (2): a group that VALIDATES a footprint and then fails to ASSEMBLE
    // retries the same placement forever -- TryPlaceWellGroup neither advances YawCursor nor spends
    // nudge/redeal budget on that path -- and it was the one repeating condition here with no bounded,
    // audible counter. DIAGNOSIS ONLY: it never stops the retry, because incomplete spawns are usually
    // transient under spawn-on-discovery and a latch would retire wells for a mistimed flyby.
    void NoteWellIncompleteSpawn(const FNodeShuffleWellEntry& E);
    TMap<FString, int32> WellIncompleteSpawnCounts;      // core path -> consecutive failures (session)
    static constexpr int32 WellIncompleteSpawnNoticeAt = 20;
    static constexpr int32 WellIncompleteSpawnWarnAt = 60;

    // ns-review-h4 F1 + F4: the ONE gate and the ONE teardown sequence for a single well member,
    // shared by every despawn path. The two paths previously used different sequences (only one was
    // complete) and a weaker occupancy test than this mod's own pin logic. Returns true when the actor
    // is gone; on a refusal OutWhy names WHICH occupancy signal fired. A member function because
    // DeregisterNodeFromManager is private and rides this class's Friend grant.
    bool DestroyWellMemberIfUnused(class AFGResourceNodeBase* Member, const TCHAR*& OutWhy);

    // ns-review-h4 F7: stale-but-in-use records already warned about. That state never self-clears
    // (the player's machine stays built), so an unthrottled warning is a line every ~5 s forever.
    TSet<FString> WellStaleInUseLogged;
    // ns-review-h4 F1: vanilla members skipped by the suppression because they are in use -- also a
    // non-self-clearing state, so also throttled per record per session.
    TSet<FString> WellSuppressSkipLogged;

    // THE mCore LIFECYCLE (design R1 -- the packet's highest risk). Sets mCore if it is not already
    // this core, then registers the satellite ONLY IF the core's mSatellites does not already contain
    // it. The Contains() guard is what makes calling this safe on BOTH paths: after a spawn (where
    // BeginPlay already registered) it is a no-op that MEASURES the design's assumption, and after a
    // reload (where BeginPlay ran with mCore null and registered NOTHING) it is the repair. Returns
    // an FNodeShuffleWellLinkResult so the caller can log what actually happened.
    struct FWellLinkOutcome
    {
        // ns-review-h2 F11: DID THE FUNNEL ACTUALLY RUN? It early-returns on !IsValid(Core/Sat), and
        // the caller counted every such early return as "registered by BeginPlay, as designed" -- i.e.
        // it reported THE PACKET'S CENTRAL ASSUMPTION AS MEASURED on the exact path where nothing was
        // measured at all. Anything that observes a link must first be able to say it looked.
        bool bRan = false;
        bool bCoreWasAlreadySet = false;
        bool bCoreWritten = false;
        bool bWasAlreadyRegistered = false;
        bool bRegisteredNow = false;
        int32 RegistrationsBefore = 0;   // occurrences of THIS satellite in mSatellites before we acted
        int32 StaleWeakEntries = 0;      // dead weak pointers seen in mSatellites (diagnostic only)
        int32 ArraySizeAfter = 0;
    };
    void EnsureSatelliteLinked(class AFGResourceNodeFrackingCore* Core,
                              class AFGResourceNodeFrackingSatellite* Sat,
                              const TCHAR* Phase, FWellLinkOutcome& Out);

    // Once per session, at first apply: re-match our spawned well actors (which carry NO SaveGame
    // identity of ours -- they are stock BP_FrackingCore_C / BP_FrackingSatellite_C) back to their
    // layout entries BY LOCATION, exactly as AdoptRestoredSpawnedNodes re-matches a real-class node,
    // then re-establish every mCore link. THIS IS THE FUNCTION THAT STOPS A RELOCATED WELL DYING
    // SILENTLY ON RELOAD: mCore is EditInstanceOnly, not SaveGame and not replicated, so a restored
    // satellite comes back unlinked and the pressurizer reports zero satellites with no crash and no
    // error of any kind.
    void AdoptRestoredWellGroups();
    bool bAdoptedRestoredWells = false;

    // ns-review-h2 fix B / F5: LAZY adoption, swept immediately before every spawn. AdoptRestoredWellGroups
    // is single-shot at first apply, so a group whose actors had not streamed by then was never adopted
    // and the spawn would put a DUPLICATE inside the existing actor. Both helpers refuse level actors
    // and refuse anything another entry has already claimed.
    class AFGResourceNodeFrackingCore* FindExistingRuntimeWellCoreAt(const FVector& At);
    class AFGResourceNodeFrackingSatellite* FindExistingRuntimeWellSatelliteAt(const FVector& At);

    // ns-review-h2 F2/F7: satellites this group actually relocated (i.e. bCaptured). Records appended
    // to an already-placed well have no rigid body, are never spawned, and must not count as missing --
    // otherwise a correct group reports broken forever and the acceptance gate degenerates into noise.
    static int32 ExpectedRelocatedSatelliteCount(const FNodeShuffleWellEntry& E);

    // Satellites already reported as uncaptured-and-refused, so the warning is once per record, not
    // once per ~5 s pass.
    TSet<FString> WellUncapturedLogged;

    // Emitted for every placed group, every session, at both ends of the lifecycle. The line carries,
    // in order: the phase; the entry's core path and the live core actor's name; the LAYOUT's assigned
    // resource; what the spawned core actually HOLDS; how many live spawned members disagree out of how
    // many were tested, and one of them by name; the group yaw; expected/spawned/registered satellite
    // counts; the core's live/raw/stale array sizes; the uncaptured record count; the largest member
    // drift and which member; the bPlaced-vs-live flag mismatch count; the placed coordinate; and the
    // verdict. THE FORMAT STRING IN NodeShuffleWellAudit.cpp IS THE ONLY AUTHORITY FOR TOKEN NAMES --
    // this comment describes the fields and must never restate them as field=value (T19 review F7).
    // A shrunk, unlinked or wrongly-resourced well must be impossible to miss in one log read
    // (design §Q3 point 5, T19).
    void AuditWellGroupLinks(const TCHAR* Phase);
    // ns-review-h2 F12: one group, audited on demand -- called the instant a group is placed. The
    // fixed-pass sweep fires ~40 s after load, but relocation is spawn-on-discovery, so the wells a
    // tester actually flies to are placed long afterwards and were never audited by it.
    //
    // It RETURNS its verdict so the sweep can total without re-deriving the health rule. The first
    // version had the sweep recompute everything, which is two copies of the one rule that decides
    // whether this packet's silent failure is visible -- the same shape as the two-ladders bug.
    struct FWellAuditVerdict
    {
        bool bHealthy = false;
        bool bRateInflated = false;
        bool bScattered = false;
        bool bShortByDesign = false;
        bool bNoCore = false;
        // T19 review F4: the SHORT-OR-UNLINKED arm had no counter, so the sweep's breakdown could sum
        // to LESS than the not-OK count and print five reassuring zeros beside a dozen broken wells.
        // Set by the verdict chain itself, so the sweep still totals and derives nothing.
        bool bShortOrUnlinked = false;

        // ---- T19 (2026-08-08): THE RESOURCE TERM. docs/TECH-DEBT.md T19. ----
        // The gate had no resource term at all, so it printed OK throughout the entire pre-T17 defect
        // and was structurally unable to fail for that class while reading as evidence that the well
        // was well. These three are the per-group answers the sweep totals; the sweep still derives
        // nothing of its own.
        //
        // bResourceMismatch -- at least one LIVE SPAWNED member holds a resource other than the
        //   layout's assignment, and the in-use pin predicate is FALSE for this group. UNHEALTHY.
        // bResourcePinned   -- the same disagreement with the in-use pin predicate TRUE. The DISAGREEMENT
        //   is not a fault (the group may still be unhealthy for an unrelated reason), and it is
        //   deliberately NOT an alarm: NodeShuffleWellSpawn.cpp's T17 block declines to retype an
        //   in-use spawned group, so such a well legitimately holds the old resource, permanently. A
        //   false alarm on this file's loudest token teaches the reader to ignore it.
        // bResourceUnknown  -- the layout's assignment did not resolve to a class, so NOTHING was
        //   compared. Reported on its own so a zero disagreement count is never read as agreement.
        //   It does NOT feed bHealthy: that would change what the gate gates for a different defect
        //   class than the one T19 scopes.
        bool bResourceMismatch = false;
        bool bResourcePinned = false;
        bool bResourceUnknown = false;
    };
    FWellAuditVerdict AuditOneWellGroup(const FNodeShuffleWellEntry& E, const TCHAR* Phase);
    int32 WellAuditPasses = 0;
    static constexpr int32 WellLinkAuditPass = 8;
    // Slow repeating sweep after the settled one, so a group placed at minute 40 is still covered.
    static constexpr int32 WellLinkAuditCadence = 60; // ~5 min at one apply pass per ~5 s

    // Hide the VANILLA core, its satellites and their paired mesh actors once the group has been
    // relocated -- otherwise the world holds the well twice. Narrow and self-contained: it never
    // touches OriginalNodeRecord or SuppressOriginalNodes' machinery.
    void SuppressVanillaWellGroup(FNodeShuffleWellEntry& E);

    // ======================= Packet H2b: GROUP VISUALS + COLLISION =======================
    // ns-t7-split (2026-08-08): these live in FOUR files now, split along the seams the reviews named.
    //   NodeShuffleWellMeshIndex.cpp   -- EnsureWellMeshIndex / RebuildWellMeshIndex (which vanilla
    //                                     mesh pieces ARE a given well member: the three pairing routes)
    //   NodeShuffleWellVisuals.cpp     -- CaptureWellGroupVisuals / HideWellMemberMeshes (ORIGIN side)
    //   NodeShuffleWellVisualsApply.cpp-- DressWellActor / ApplyWellGroupVisuals (DESTINATION side)
    //   NodeShuffleWellSnapBox.cpp     -- ConfigureWellMeshCollision / EnsureWellMemberSnapBox (the
    //                                     collision recipe + the "Resource" collider the snap resolves against)
    // The file headers there carry the full rationale; what matters at the declaration is WHY this
    // exists as a separate mechanism at all:
    //
    // A runtime-spawned fracking core/satellite has NO engine AFGNodeMeshActor and no rock of its own,
    // so a relocated well was not merely invisible -- it had NOTHING FOR THE BUILD GUN TRACE TO HIT.
    // Measured in-game 2026-08-07 from our own hologram hook on two independent relocated wells:
    //     HOLOGRAMHOOK IsValidHitResult -> 1 | hitActor='LandscapeStreamingProxy_...'
    //     HOLOGRAMHOOK TrySnapToActor  -> 0 | hitActor='LandscapeStreamingProxy_...'
    // The trace went straight through our spawned node actors and landed on terrain. So COLLISION, not
    // appearance, is this packet's acceptance criterion, and the mesh that carries the look is the same
    // mesh that carries the collision -- one object, so the two can never drift apart.

    // Rebuild WellMeshIndex (vanilla well member path -> the static-mesh components that ARE its look).
    // At most once per apply pass; call EnsureWellMeshIndex rather than this.
    void RebuildWellMeshIndex();
    void EnsureWellMeshIndex();

    // Capture the group's look from the LIVE VANILLA actors, before anything is hidden. Returns true
    // when the group is now completely captured. Idempotent and skipped once bGroupVisualsComplete.
    bool CaptureWellGroupVisuals(FNodeShuffleWellEntry& E);

    // Hide every static-mesh piece paired to this vanilla member. THE ORIGIN-SIDE HALF of the same
    // pairing problem: SuppressVanillaWellGroup used to call FindMeshActorForNode, whose cache is built
    // by a sweep that skips fracking actors outright, so five well groups out of six hid ZERO mesh
    // actors while reporting nothing pending. Returns the number of pieces newly hidden.
    int32 HideWellMemberMeshes(class AFGResourceNodeBase* Node, int32& OutAlreadyHidden);

    // Dress the RELOCATED actors: re-apply the captured pieces as static-mesh components on our spawned
    // core/satellites, with the collision recipe that makes them build-gun surfaces. Idempotent.
    void ApplyWellGroupVisuals(FNodeShuffleWellEntry& E);

    // The proven cosmetic-rock collision recipe, in ONE place so a well mesh and an ordinary node rock
    // cannot drift apart. See the definition for the channel-by-channel reasoning.
    static void ConfigureWellMeshCollision(class UStaticMeshComponent* Comp);

    // Re-create/refresh one relocated member's mesh pieces. Returns how many components it had to
    // create this pass; OutPieces accumulates how many are now dressed.
    static int32 DressWellActor(AActor* Actor, const TArray<FNodeShuffleWellVisual>& Visuals,
                                int32& OutPieces);

    // H2b-identity (2026-08-08): give a relocated well member a "Resource" collider that COVERS its own
    // NodeShuffleWellMesh_* pieces, and wire it as AFGResourceNodeBase::mBoxComponent -- the SAME recipe
    // EnsureNodeUseBox already applies to ordinary relocated nodes, which could never reach a fracking core
    // because that function is typed AFGResourceNode*. Member (not free) function because it writes
    // mBoxComponent, which needs this class's AccessTransformers Friend grant on AFGResourceNodeBase.
    // NO-OP when the member's existing box already covers the pieces -- see the definition.
    static void EnsureWellMemberSnapBox(AActor* Actor);

    // vanilla member path -> its look. Transient; rebuilt at most once per apply pass.
    TMap<FString, TArray<TWeakObjectPtr<class UStaticMeshComponent>>> WellMeshIndex;
    int32 WellMeshIndexPass = -1;
    // Session-wide LAST-RESORT templates, filled opportunistically from any well member we did manage
    // to capture. Used only when a group's own origin never streamed. Deliberately a fallback and
    // deliberately logged as one: all vanilla wells share a mesh vocabulary, but the DESERT variants
    // (ENodeMeshType::MT_DesertCore / MT_DesertCrack / MT_DesertSatellite) do not match the grassland
    // ones, so a template can dress a well in the wrong biome's rock. An ugly well beats an invisible,
    // unbuildable one; a SILENT ugly well does not, hence the log line.
    TArray<FNodeShuffleWellVisual> WellVisualTemplateCore;
    TArray<FNodeShuffleWellVisual> WellVisualTemplateSatellite;
    TSet<FString> WellVisualLogged;        // per-group apply summary, said once
    // ONE session-wide log-throttle set, SHARED BY THREE TRANSLATION UNITS AND SEVEN KEY FAMILIES.
    // ns-t7-split asked for this table because the split scattered the writers: before it, a reader
    // could see every key by scrolling one file; now nothing but this comment documents the namespace.
    // NO COLLISION IS POSSIBLE TODAY and the table exists so that stays checkable, not because a
    // collision was found: the three prefixed families are disjoint by prefix, the two suffixed
    // families end in a literal that a bare path cannot end in, and a bare path can never contain '|'
    // (UObject path names use '/' , '.' and ':' -- never '|'), which is why the suffixed and prefixed
    // families can never be confused with it. ADD A KEY FAMILY -> ADD A ROW, and keep '|' out of any
    // bare-path key.
    //
    //   key family                                  | written by                        | said once per
    //   --------------------------------------------+-----------------------------------+---------------------------
    //   "narrowed|<smaPath>|<meshName>"              | NodeShuffleWellMeshIndex.cpp      | mesh the type gate DROPPED
    //   "widened|<smaPath>|<meshName>"               | NodeShuffleWellMeshIndex.cpp      | mesh the type gate ADDED
    //   "bystander|<smaPath>|<meshName>"             | NodeShuffleWellMeshIndex.cpp      | mesh A2's contest refused
    //   "<corePath>|adopt"                           | NodeShuffleWellVisuals.cpp        | well group, at first look
    //   "<corePath>|grp"                             | NodeShuffleWellVisuals.cpp        | well group capture summary
    //   "<memberPath>"           (BARE, no suffix)   | NodeShuffleWellVisuals.cpp        | member that captured nothing
    //   "meshtypecensus|%d|%d|%d|%d"                 | NodeShuffleSubsystem.cpp          | distinct MESHTYPE-CENSUS tuple
    //
    // NOT in this set (separate members, listed so nobody adds them here by mistake):
    // WellVisualLogged (apply summary), WellVisualCompDumped (component dump), SnapBoxLogged (T3).
    TSet<FString> WellVisualCaptureLogged; // per-member capture failure / adopt state / bystander reject
    // ns-review-h2b F-2: keyed "<actorPath>|<pieceCount>", NOT the actor path alone. A path-only key
    // froze the dump at the first pass, which for an unstreamed origin is an actor with zero pieces.
    TSet<FString> WellVisualCompDumped;    // per-actor component dump, re-fires when the piece count changes
    int32 WellMeshIndexMembers = 0;        // diagnostics: members indexed on the last rebuild
    int32 WellMeshIndexPieces = 0;         // diagnostics: pieces indexed on the last rebuild

    // ---- T3 SNAP-BOX OVERLAP DIAGNOSTIC (docs/TECH-DEBT.md T3, PARKED/watch-only) ----
    // Promoted by ns-t7-split from `namespace NodeShuffleWellSnapBoxDiag` (function-local statics
    // behind accessors in NodeShuffleWellVisualsApply.cpp), which existed ONLY because the packet that
    // wrote it was barred from this header. Defined in NodeShuffleWellSnapBox.cpp.
    //
    // STATIC, NOT PER-INSTANCE, AND THAT IS FORCED: the reader is EnsureWellMemberSnapBox, a STATIC
    // member function (it is static because DressWellActor is), so it has no `this` to read an
    // instance field through. Making it non-static would change two public signatures and every call
    // site -- out of scope for a behaviour-identity packet. STORAGE DURATION IS SIMILAR, NOT IDENTICAL,
    // and the difference is stated rather than glossed: the previous function-local statics were
    // initialised LAZILY on first use; these are dynamically initialised at DLL load, unordered against
    // other TUs. That is safe HERE only because nothing in this module's static initialisation touches
    // them -- every reader runs at gameplay time. If a file-scope object in this module ever calls into
    // EnsureWellMemberSnapBox or ResetWellSnapBoxDiagForWorld from its constructor, that stops being true.
    //
    // WRITER  = RebuildWellMeshIndex (NodeShuffleWellMeshIndex.cpp), once per apply pass.
    // READER  = EnsureWellMemberSnapBox (NodeShuffleWellSnapBox.cpp), per dressed member.
    // Game thread only; both run inside ApplyLayout. No UObject is held, so no GC interaction.
    //
    // THE DEFECT THIS PROMOTION FIXES (recorded in the T1/T2 cold review §8): the namespace had NO
    // world-change reset where its T4 sibling did, so on a SECOND save load in one process the
    // snapshot, its pass number and the log-throttle set all survived while WellAuditPasses restarted
    // at 0 -- printing a stale distance and a NEGATIVE snapshot age. ResetWellSnapBoxDiagForWorld()
    // below is the fix; see its definition for exactly what it clears.
    static TArray<FVector> SnapBoxUseBoxNodes;   // ACTIVE (non-hidden) mineable AFGResourceNode locations
    static int32 SnapBoxHiddenOriginalNodes;     // T3 correction: hidden originals EXCLUDED from the array, counted here
    static int32 SnapBoxSnapshotPass;            // -1 = never built in this process/world
    static int32 SnapBoxCurrentAuditPass;        // WellAuditPasses as of the current apply pass; -1 = unset
    // WEAK, NOT A RAW ADDRESS, AND THAT IS THE WHOLE POINT. A freed UWorld's GUObjectArray slot is
    // recycled, so the NEXT world can be allocated at the SAME address -- a raw pointer compare would
    // then report "same world", skip the reset, and reinstate the negative age this reset exists to
    // remove, WHILE ALSO suppressing the log line that would show it. TWeakObjectPtr compares
    // index+serial, so a recycled slot is detected. It still holds no strong reference: no GC interaction.
    static TWeakObjectPtr<const UWorld> SnapBoxDiagWorld;
    static TSet<FString> SnapBoxLogged;          // T3 per-member log throttle (NOT WellVisualCaptureLogged)

    // Drop the T3 snapshot when the UWorld changes. Idempotent and cheap (one pointer compare), so it
    // is called at the top of BOTH writers -- whichever runs first in a new world resets, the other
    // then sees a matching pointer and does nothing. Calling it from only one of them would let that
    // one wipe a snapshot the other had just built in the same new world.
    static void ResetWellSnapBoxDiagForWorld(const class UWorld* World);

    // Runtime handles for the relocated group, rebuilt each session (spawn or adopt). Keyed by the
    // layout entry's CorePath. UPROPERTY so the spawned actors are strongly referenced, mirroring
    // SpawnedNodes.
    UPROPERTY() TMap<FString, class AFGResourceNodeFrackingCore*> SpawnedWellCores;
    UPROPERTY() TMap<FString, class AFGResourceNodeFrackingSatellite*> SpawnedWellSatellites; // key: SatellitePath
    // Session log throttles -- the relocation pass runs every ~5 s like every other apply.
    TSet<FString> WellRelocLogged;
    TSet<FString> WellRelocFailLogged;
    TSet<FString> WellSuppressLogged;
    bool bWellRelocDisabledLogged = false;
    int32 WellGroupsPlacedThisSession = 0;

    // §Q3a: K IS MEASURED, NOT CHOSEN. H0's run gives a mean bounding radius of 4587 cm; a yaw step
    // that moves an outer satellite less than the 800 cm reject radius needs 800/4587 ~= 10 deg, and
    // the smallest measured angular gap between neighbouring satellites is 9.6 deg. Two independent
    // routes to ~10 deg is the confirmation. K=12 (30 deg) moves an outer satellite ~20 m per attempt
    // and would skip valid pockets wholesale.
    static constexpr int32 WellYawSteps = 36;
    // The footprint test early-outs on the FIRST failing satellite, so a bad yaw usually dies after
    // one or two traces. Still budgeted per pass and resumed via YawCursor rather than burning the
    // whole 36-yaw search in one frame.
    static constexpr int32 WellYawAttemptsPerPass = 6;
    static constexpr uint8 WellMaxGroupNudges = 8;
    static constexpr uint8 WellMaxGroupRedeals = 3;
    static constexpr int32 WellRedealTries = 24;
    // H0 measured satsPerWell min 4 / mean 6.75 / max 10 over 20 wells. A well reporting fewer than
    // this at capture time is almost certainly PARTIALLY STREAMED, and relocating it would shrink it
    // permanently. Refuse, log, and retry on a later roll.
    static constexpr int32 WellMinSatellitesForRelocation = 4;
    // H0 measured minimum inter-satellite distance 1818.8 cm over 401 pairs, against the 800 cm
    // reject radius -- which is WHY H2 needs no same-group overlap exemption. That measurement is
    // ASSERTED at capture, not assumed: a well whose own members sit closer than this could never
    // validate its own footprint, so it is refused with a loud line rather than looping forever.
    static constexpr float WellSelfOverlapFloorCm = 800.0f;
};
