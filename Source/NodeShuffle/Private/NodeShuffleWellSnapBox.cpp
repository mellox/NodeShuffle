// Packet H2b/H2c (branch wip/h2-relocation) -- COLLISION, WHICH IS THE ACCEPTANCE CRITERION.
// Split out of NodeShuffleWellVisualsApply.cpp by ns-t7-split (2026-08-08) at the seam the T1/T2
// instrumentation handoff proposed (_team/nodeshuffle-followups/T1T2-instrument-handoff.md §4), with
// ONE correction to that proposal, recorded here because a wrong seam is how a split breaks a build:
// the handoff also listed `DumpWellActorCollision` as moving. It does not, and cannot -- it is a
// file-static in an anonymous namespace whose ONLY caller is ApplyWellGroupVisuals, which stays. Moving
// it would have left an unreachable static in this file and an undefined symbol in that one.
//
// READ NodeShuffleWellVisuals.cpp's HEADER FIRST for the measured finding the whole packet exists for.
// THE ONE-LINE VERSION: appearance is the by-product. If a relocated well looks perfect and a Resource
// Well Pressurizer still will not snap to its core, this file has failed, and WELLH2B-COLLISION /
// WELLH2C-SNAPBOX in the log are where to look first.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"       // the "Resource" snap collider
#include "Resources/FGResourceNodeBase.h"  // mBoxComponent (friend-granted)

// ------------------------------------------------------------------------------------------------
// T3 (docs/TECH-DEBT.md) -- THE SNAPSHOT, HANDED ACROSS TWO TRANSLATION UNITS
// ------------------------------------------------------------------------------------------------
// WHY THIS EXISTS AT ALL. T3 says a well member's snap box (measured up to ~900 cm) plus an ordinary
// node's 650 cm EnsureNodeUseBox can provably intersect, and that the reason it had NEVER been observed
// is that NOTHING MEASURED IT. The measurement needs one number EnsureWellMemberSnapBox does not have:
// the distance to the nearest ACTIVE mineable resource node. RebuildWellMeshIndex
// (NodeShuffleWellMeshIndex.cpp) ALREADY sweeps the world for route 3's bystander contest, so it hands
// the narrower T3 subset over instead of adding a second world sweep on a per-member path.
//
// ns-t7-split: THESE WERE FUNCTION-LOCAL STATICS BEHIND ACCESSORS in `namespace
// NodeShuffleWellSnapBoxDiag`, which existed ONLY because that packet was barred from
// NodeShuffleSubsystem.h. They are now static data members of ANodeShuffleSubsystem (declared there,
// defined here). Static, not per-instance, is FORCED: the reader is EnsureWellMemberSnapBox, a static
// member function, which has no `this`. Storage duration is unchanged, so the promotion by itself
// changes nothing -- what DOES change is the world reset immediately below, which the namespace never
// had and its T4 sibling always did.
TArray<FVector> ANodeShuffleSubsystem::SnapBoxUseBoxNodes;
int32 ANodeShuffleSubsystem::SnapBoxHiddenOriginalNodes = 0;
int32 ANodeShuffleSubsystem::SnapBoxSnapshotPass = -1;      // -1 = never built in this process/world
int32 ANodeShuffleSubsystem::SnapBoxCurrentAuditPass = -1;
TWeakObjectPtr<const UWorld> ANodeShuffleSubsystem::SnapBoxDiagWorld;
TSet<FString> ANodeShuffleSubsystem::SnapBoxLogged;

// THE DEFECT THIS FIXES, quoted from the T1/T2 cold review §8: "NodeShuffleWellSnapBoxDiag has no
// world-change reset [where its sibling does], so on a second save load in one process the age can read
// NEGATIVE." That is exactly right and it is not only the age. WHAT THE RESET NOW DOES, field by field:
//   * SnapBoxUseBoxNodes  -- emptied. Otherwise the first WELLH2C-SNAPBOX line of the new world measures
//     the member's NEW world position against the OLD world's node locations and prints a distance that
//     is arithmetic on two different worlds. That is worse than printing nothing, because it looks
//     like a measurement.
//   * SnapBoxHiddenOriginalNodes -- zeroed with it, so the excluded-population count can never describe
//     a snapshot that no longer exists.
//   * SnapBoxSnapshotPass -- back to -1, which is this file's documented NOT MEASURED sentinel. Without
//     it, WellAuditPasses restarts at 0 on the new world while the stored pass stays large, and
//     `age = current - stored` prints NEGATIVE.
//   * SnapBoxCurrentAuditPass -- back to -1 for the same reason, so an age is never computed from one
//     world's counter and another's.
//   * SnapBoxLogged -- cleared, and this is the part that makes the rest of the reset OBSERVABLE. The
//     key is built from Actor->GetPathName(), which repeats across loads of the same map, so without
//     clearing it the new world's FIRST (and therefore most interesting) line for every member would be
//     deduped against the OLD world's line and never printed. A reset that silences its own evidence is
//     not a reset. Cost: each member says WELLH2C-SNAPBOX once more per world load.
// Idempotent (one pointer compare) so BOTH writers call it and whichever reaches a new world first does
// the work; calling it from only one would let that one wipe a snapshot the other just built.
void ANodeShuffleSubsystem::ResetWellSnapBoxDiagForWorld(const UWorld* World)
{
    // index+serial compare, NOT an address compare: a recycled UObject slot is not the same world.
    const TWeakObjectPtr<const UWorld> Incoming(World);
    if (SnapBoxDiagWorld.HasSameIndexAndSerialNumber(Incoming)) { return; }
    const int32 PrevNodes = SnapBoxUseBoxNodes.Num();
    const int32 PrevPass = SnapBoxSnapshotPass;
    const int32 PrevLogged = SnapBoxLogged.Num();
    SnapBoxDiagWorld = Incoming;
    SnapBoxUseBoxNodes.Reset();
    SnapBoxHiddenOriginalNodes = 0;
    SnapBoxSnapshotPass = -1;
    SnapBoxCurrentAuditPass = -1;
    SnapBoxLogged.Reset();
    // DELIBERATELY UNGATED, unlike the NARROWED/WIDENED/BYSTANDER lines: runtime step 5 greps for this
    // line to prove the world reset fired, and it emits at most once per UWorld change.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2C-SNAPBOX RESET: the T3 snapshot now belongs to a different UWorld ('%s'), so it has ")
        TEXT("been dropped -- it held %d node location(s) taken on pass %d and %d deduped log key(s). ")
        TEXT("Until RebuildWellMeshIndex runs again in this world, every WELLH2C-SNAPBOX line will read ")
        TEXT("nodes=0 / pass=-1, which means NOT MEASURED and never \"nothing nearby\". MEASURED here: ")
        TEXT("that the world pointer changed. NOT MEASURED: why it changed (a save load, a travel, or ")
        TEXT("first use in this process -- the first call of a session always takes this branch because ")
        TEXT("the stored pointer starts null). ns-t7-split added this; before it, a second save load in ")
        TEXT("one process printed a cross-world distance and a NEGATIVE snapshot age."),
        World ? *World->GetName() : TEXT("<null>"), PrevNodes, PrevPass, PrevLogged);
}

// ------------------------------------------------------------------------------------------------
// THE COLLISION RECIPE -- THE ACCEPTANCE CRITERION, IN ONE PLACE
// ------------------------------------------------------------------------------------------------
// Channel by channel, and every choice here is a previously-paid-for lesson rather than a preference:
//
//  * BASE PROFILE "ResourceNoCollision" -- objType=Resource, QueryOnly, ignores everything, overlaps
//    Hologram. The proven COSMETIC base: the building-clearance profile ignores the Resource object
//    type, so a rock on this profile can never block the footprint of the machine being built on it.
//    (redesign-8: a WorldStatic rock made the Miner Mk1 footprint reject its own node.)
//
//  * ECC_GameTraceChannel5 (BuildGun) = BLOCK -- THIS IS THE FIX. redesign-16 established the
//    mechanism by a perfect correlation in the r13/14/15 HOLOGRAMHOOK logs: every object the build-gun
//    trace hit had BuildGun=Block, every component of ours had BuildGun=Ignore and was never hit.
//    Blocking it makes hitResult.GetActor() our fracking core/satellite, which IS an
//    IFGExtractableResourceInterface, which is what AFGResourceExtractorHologram::
//    TrySnapToExtractableResource needs. r14 (Visibility/Camera/WorldStatic) and r15 (objType alone)
//    both missed because neither touched this channel.
//
//  * objType ECC_WorldDynamic + ECC_Pawn = BLOCK -- player solidity, so a relocated well reads as
//    ground you stand on like a vanilla one. DELIBERATELY NOT WorldStatic: WorldStatic is the one
//    object type the extractor encroachment test rejects (memory:nodeshuffle-rock-miner-polish), and
//    the Pawn preset IGNORES the Resource object type, which is why the profile alone leaves a rock
//    walk-through. This exact pair is what shipped for ordinary node rocks and has been in the
//    user's world since polish-2.
//
//  * EVERYTHING ELSE STAYS IGNORE -- Clearance included. We add a build-gun surface; we do not add an
//    obstacle. Overlap events off: nothing subscribes and they are not free.
//
// WHAT IS NOT PROVABLE HERE. That this makes a Resource Well Pressurizer and a Well Extractor actually
// snap runs through the hologram's closed-source placement path. It is graded ASSUMED and it is test
// step 1 of the runtime script, not a footnote.
void ANodeShuffleSubsystem::ConfigureWellMeshCollision(UStaticMeshComponent* Comp)
{
    if (!IsValid(Comp)) { return; }
    Comp->SetCollisionProfileName(TEXT("ResourceNoCollision"));
    Comp->SetCollisionResponseToChannel(ECC_GameTraceChannel5, ECR_Block); // BuildGun -- THE FIX
    Comp->SetCollisionObjectType(ECC_WorldDynamic);
    Comp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);              // standable, like vanilla
    Comp->SetGenerateOverlapEvents(false);
}

// ------------------------------------------------------------------------------------------------
// H2b-identity -- MAKE THE OFF-CENTRE HIT RESOLVE TO THE NODE
// ------------------------------------------------------------------------------------------------
// THE MEASUREMENT THIS EXISTS FOR (2026-08-08, marker 2026-08-08-h2b-2):
//     HOLOGRAMHOOK IsValidHitResult -> 1 ... hitComp='NodeShuffleWellMesh_2' ... BuildGun=Block
//     HOLOGRAMHOOK TrySnapToActor  -> 0 ... hitComp='NodeShuffleWellMesh_2' compFound=0
// H2b's collision half works: the trace HITS us. The snap one step later does not.
//
// THIS IS NOT A NEW DIAGNOSIS. It is the SAME failure, on a class the existing fix could not reach.
// NodeShuffleSubsystem.cpp's EnsureNodeUseBox already records it, MEASURED, for ordinary nodes:
//     "acceptance ALL passes (CanOccupy=1, IsAllowed=1, nodeIsA=1, hasAnyResources=1) yet
//      TrySnapToActor->0 -- because the build-gun trace lands on our laterally-offset rock
//      (NodeShuffleRockMesh_Rt) and the game's snap can't resolve that off-center hit to the node
//      without a large 'Resource' collider (mBoxComponent) covering it."
// Swap "NodeShuffleRockMesh_Rt" for "NodeShuffleWellMesh_2" and that paragraph is this bug verbatim.
// A well member's pieces are offset FURTHER than an ordinary rock ever is -- NodeShuffleWellVisuals.cpp
// records a crack graphic sitting 940 cm from its core -- so it needs the fix MORE, not less.
//
// WHY IT NEVER GOT IT: `void ANodeShuffleSubsystem::EnsureNodeUseBox(AFGResourceNode* Node)`. A fracking
// CORE is an AFGResourceNodeFrackingCore : AFGResourceNodeBase and is NOT an AFGResourceNode, so that call
// does not compile for one and no well path ever made it. Its non-legacy branch needs only
// AFGResourceNodeBase. The typed-signature blind spot, again, in a second function.
//
// SIZED, NOT HARD-CODED, AND CLAMPED AGAINST A MEASURED NUMBER. EnsureNodeUseBox uses a flat 650 cm,
// which does not cover a 940 cm crack piece. This measures the actual lateral reach of the member's own
// pieces and clamps it to [650, 900]. The 900 ceiling is half of H0's MEASURED minimum inter-satellite
// spacing (1818.8 cm over 401 pairs), so two members' boxes can never overlap and steal each other's
// snap. When a piece reaches past the ceiling the log says clamped=1 and prints the raw reach -- the
// case is then visible rather than silently mis-sized.
//
// PARITY, AND WHY THIS IS A NO-OP WHEN IT IS NOT NEEDED. Re-pointing mBoxComponent replaces what the
// node's USE (E-key) bounds and its native highlight resolve against. So this only acts when the
// member's EXISTING box does NOT already cover the pieces; when it does, nothing is created, nothing is
// re-pointed, and the log says why. It also never SHRINKS a box: the wanted extent is max'd against
// whatever the member already had.
//
// NOT PROVABLE HERE, AND GRADED ACCORDINGLY: that a covering mBoxComponent is what the Pressurizer's
// snap actually resolves against runs entirely through closed-source hologram code. ASSUMED. It is test
// step 1 of the runtime script, not a footnote -- and the identity half is what makes the log
// answer the question either way, because HOLOGRAMHOOK ACCEPTANCE / ACCEPT-NODE / ACCEPT-EXT only print
// when compFound=1.
void ANodeShuffleSubsystem::EnsureWellMemberSnapBox(AActor* Actor)
{
    if (!IsValid(Actor)) { return; }
    AFGResourceNodeBase* Base = Cast<AFGResourceNodeBase>(Actor);
    USceneComponent* Root = Actor->GetRootComponent();
    if (!Base || !Root) { return; }

    // Lateral/vertical reach of OUR pieces, measured from the actor origin in world-axis terms. World
    // bounds (not relative location) so a piece's own mesh size counts, which is the whole point -- a
    // hit lands on the far EDGE of a piece, not on its pivot.
    const FVector ActorLoc = Actor->GetActorLocation();
    // DOUBLE, not float, deliberately: FVector is double-precision in UE5 and FMath::Max/Clamp deduce a
    // SINGLE template type -- mixing a float accumulator with a double FVector component is a compile
    // error, not a silent narrowing. Every scalar in this function is double for that one reason.
    double RawXY = 0.0;
    double RawZ = 0.0;
    int32 Pieces = 0;
    TInlineComponentArray<UStaticMeshComponent*> Meshes(Actor);
    for (UStaticMeshComponent* M : Meshes)
    {
        if (!IsValid(M) || !M->GetName().StartsWith(TEXT("NodeShuffleWellMesh"))) { continue; }
        ++Pieces;
        const FBoxSphereBounds B = M->Bounds;
        const FVector D = B.Origin - ActorLoc;
        RawXY = FMath::Max(RawXY, FMath::Max(FMath::Abs(D.X) + B.BoxExtent.X, FMath::Abs(D.Y) + B.BoxExtent.Y));
        RawZ = FMath::Max(RawZ, FMath::Abs(D.Z) + B.BoxExtent.Z);
    }
    if (Pieces == 0) { return; } // undressed member: nothing to cover, and nothing to snap to either

    static constexpr double SnapBoxMinXY = 650.0; // EnsureNodeUseBox's proven ordinary-node extent
    static constexpr double SnapBoxMaxXY = 900.0; // half the MEASURED 1818.8 cm min inter-satellite spacing
    static constexpr double SnapBoxMinZ = 180.0;  // EnsureNodeUseBox's proven ordinary-node half-height
    static constexpr double SnapBoxMaxZ = 600.0;
    // What WE want, before the never-shrink floor: the piece reach, clamped to the ceiling.
    const double DesiredXY = FMath::Clamp(RawXY, SnapBoxMinXY, SnapBoxMaxXY);
    const double DesiredZ  = FMath::Clamp(RawZ,  SnapBoxMinZ,  SnapBoxMaxZ);

    // Does the member's EXISTING collider already cover the pieces? If so this whole fix is a no-op.
    UBoxComponent* Existing = Base->mBoxComponent;
    const FVector ExistingExtent = IsValid(Existing) ? Existing->GetUnscaledBoxExtent() : FVector::ZeroVector;
    const bool bExistingCovers = IsValid(Existing)
        && ExistingExtent.X >= RawXY && ExistingExtent.Y >= RawXY && ExistingExtent.Z >= RawZ;

    // Never SHRINK: whatever the member already had is a floor, so no native bound gets smaller.
    // PER-AXIS, deliberately (ns-review-h2c F-1). The previous form was
    //   WantXY = Max(WantXY, Max(ExistingExtent.X, ExistingExtent.Y))
    // which fed ONE value to both axes, so an anisotropic native box of (1200,400,200) came out
    // 1200x1200 -- Y grew 3x for no reason, and the growth came from the OTHER axis, not from any
    // piece. Growing an axis is only ever justified by that axis's own floor or our own reach.
    const double WantX = FMath::Max(DesiredXY, ExistingExtent.X);
    const double WantY = FMath::Max(DesiredXY, ExistingExtent.Y);
    const double WantZ = FMath::Max(DesiredZ,  ExistingExtent.Z);

    // bClamped is computed from the FINAL extent, not from RawXY (ns-review-h2c F-1). The old form
    // latched `RawXY > SnapBoxMaxXY` BEFORE the never-shrink max, so a box pushed past the ceiling by
    // an existing native bound printed clamped=0 -- the one field the test script tells the tester to
    // watch, reporting SAFE in exactly the unsafe case. It now means what it says: this box is not
    // fully within the 900 cm ceiling, whatever put it there.
    const bool bClamped = (RawXY > SnapBoxMaxXY) || (WantX > SnapBoxMaxXY) || (WantY > SnapBoxMaxXY);

    // ---- T3: SNAP-BOX OVERLAP, MEASURED INSTEAD OF HUNTED (docs/TECH-DEBT.md T3) ----
    // The ordinary-node box this is compared against is EnsureNodeUseBox's, which sets
    // FVector(650, 650, 180) at NodeShuffleSubsystem.cpp:3312 and :3331. Named here rather than
    // borrowed from a previous run's log.
    static constexpr double OrdinaryUseBoxXY = 650.0;
    static constexpr double OrdinaryUseBoxZ = 180.0;
    // ns-t7-split: this array is now ACTIVE (non-hidden) mineable nodes only; the hidden originals it
    // used to contain are counted in SnapBoxHiddenOriginalNodes and printed beside it, never dropped
    // silently. See the T3 CORRECTION comment in NodeShuffleWellMeshIndex.cpp for why.
    const TArray<FVector>& ByLocs = SnapBoxUseBoxNodes;
    const int32 BystanderCount = ByLocs.Num();          // THE DENOMINATOR: nodes actually considered
    const int32 HiddenExcluded = SnapBoxHiddenOriginalNodes; // the population REMOVED, printed with it
    const int32 BystanderFromPass = SnapBoxSnapshotPass;

    // The snapshot's PRESENCE is in the key (not its contents) so that a member first dressed before
    // RebuildWellMeshIndex ever ran -- which prints "not measured" -- says it again ONCE the snapshot
    // exists. Without that bit the only line a reader ever gets could be the unmeasured one. The
    // contents are deliberately NOT in the key: they change as the world streams, and this line is not
    // a stream monitor. ns-t7-split: SnapBoxLogged is now a member and is CLEARED on a world change,
    // so a second save load in one process gets its own first line instead of being deduped against
    // the previous world's.
    const FString LogKey = FString::Printf(TEXT("%s|%d|%d|%d|%d|%d"), *Actor->GetPathName(), Pieces,
                                           FMath::RoundToInt(WantX), FMath::RoundToInt(WantY),
                                           bExistingCovers ? 1 : 0, BystanderCount > 0 ? 1 : 0);
    const bool bSayIt = !SnapBoxLogged.Contains(LogKey);
    if (bSayIt) { SnapBoxLogged.Add(LogKey); }

    // The FINAL extent this member ends up with, which is what an overlap test has to use: the existing
    // native box in the no-op branch, ours in the re-point branch. Computed for both so the two log
    // lines below report the same quantity.
    const FVector FinalExtent = bExistingCovers ? ExistingExtent : FVector(WantX, WantY, WantZ);
    double NearestNonWellCm = -1.0;                     // -1 = NOT MEASURED, never "nothing nearby"
    double NearestDX = -1.0, NearestDY = -1.0, NearestDZ = -1.0;
    int32 bProvableOverlap = 0;
    // Cost: the O(bystanders) scan runs ONLY on a pass that will actually print -- once per member per
    // distinct (pieces, extent, snapshot-present) tuple for the whole session, not once per apply pass.
    if (bSayIt && BystanderCount > 0)
    {
        // Seeded from element 0 rather than from a sentinel maximum: the branch is already guarded on
        // BystanderCount > 0, and this needs no numeric-limits header in this translation unit.
        double BestSq = FVector::DistSquared(ByLocs[0], ActorLoc);
        FVector NearestLoc = ByLocs[0];
        for (int32 i = 1; i < BystanderCount; i++)
        {
            const double DSq = FVector::DistSquared(ByLocs[i], ActorLoc);
            if (DSq < BestSq) { BestSq = DSq; NearestLoc = ByLocs[i]; }
        }
        NearestNonWellCm = FMath::Sqrt(BestSq);
        NearestDX = FMath::Abs(NearestLoc.X - ActorLoc.X);
        NearestDY = FMath::Abs(NearestLoc.Y - ActorLoc.Y);
        NearestDZ = FMath::Abs(NearestLoc.Z - ActorLoc.Z);
        // Per-axis AABB test, not the scalar "extent + 650 > distance" shorthand: two axis-aligned
        // boxes intersect only if they overlap on ALL THREE axes, and the scalar form over-reports.
        bProvableOverlap = (NearestDX < FinalExtent.X + OrdinaryUseBoxXY
                            && NearestDY < FinalExtent.Y + OrdinaryUseBoxXY
                            && NearestDZ < FinalExtent.Z + OrdinaryUseBoxZ) ? 1 : 0;
    }

    if (bExistingCovers)
    {
        if (bSayIt)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH2C-SNAPBOX actor='%s' class='%s' pieces=%d reach=(xy=%.0f z=%.0f): NO-OP -- the ")
                TEXT("member's own mBoxComponent '%s' extent=%s ALREADY covers every piece, so nothing was ")
                TEXT("created and mBoxComponent was NOT re-pointed. If the snap still fails, the collider is ")
                TEXT("not the gate -- read HOLOGRAMHOOK ACCEPTANCE / ACCEPT-NODE / ACCEPT-EXT for this actor. ")
                TEXT("|| T3 OVERLAP: finalExtent=%s; nearest ordinary MINEABLE node (AFGResourceNode; ")
                TEXT("DEPOSITS EXCLUDED -- only this class ever carries EnsureNodeUseBox's 650 cm box) ")
                TEXT("is %.0f cm away, per-axis |dx|/|dy|/|dz| = %.0f/%.0f/%.0f cm, measured against a ")
                TEXT("snapshot of activeMineable=%d such node(s) with a further hiddenOriginal=%d ")
                TEXT("EXCLUDED (ns-t7-split: a hidden original cannot be built on regardless of any box, ")
                TEXT("so an overlap against one was a FALSE POSITIVE -- both counts are printed so a ")
                TEXT("smaller activeMineable on this build is readable as THIS change, not as a world ")
                TEXT("that lost nodes), taken on WELLH2B-INDEX pass %d, and it is now pass %d ")
                TEXT("(age = %d pass(es); a snapshot older than 0 passes may MISS nodes that streamed in ")
                TEXT("since, which reports the distance TOO LARGE and can print a FALSE ")
                TEXT("provableOverlap=0); provableOverlap=%d. A -1 distance with activeMineable=0 / ")
                TEXT("pass=-1 means ")
                TEXT("NOT MEASURED (RebuildWellMeshIndex had not run in this process/world yet). A -1 ")
                TEXT("distance with activeMineable=0 and a pass >= 0 means the snapshot WAS built and ")
                TEXT("contained no candidate ")
                TEXT("at all -- also not a measurement of \"nothing nearby\", just an empty population. ")
                TEXT("Neither means nothing is nearby. provableOverlap=1 means the two ")
                TEXT("axis-aligned boxes overlap on ALL THREE axes IF that node carries EnsureNodeUseBox's ")
                TEXT("standard 650/650/180 cm box. NOT MEASURED here: whether that node actually has that ")
                TEXT("box, and whether a Miner then refuses to place on it -- both are runtime tests."),
                *Actor->GetName(), *Actor->GetClass()->GetName(), Pieces, RawXY, RawZ,
                *Existing->GetName(), *ExistingExtent.ToCompactString(),
                *FinalExtent.ToCompactString(), NearestNonWellCm, NearestDX, NearestDY, NearestDZ,
                BystanderCount, HiddenExcluded, BystanderFromPass, SnapBoxCurrentAuditPass,
                SnapBoxCurrentAuditPass - BystanderFromPass, bProvableOverlap);
        }
        return;
    }

    UBoxComponent* Box = nullptr;
    TInlineComponentArray<UBoxComponent*> Boxes(Actor);
    for (UBoxComponent* B : Boxes)
    {
        if (IsValid(B) && B->GetFName() == FName(TEXT("NodeShuffleWellUseBox_Rt"))) { Box = B; break; }
    }
    const bool bCreated = (Box == nullptr);
    if (!Box)
    {
        Box = NewObject<UBoxComponent>(Actor, TEXT("NodeShuffleWellUseBox_Rt"));
        if (!Box)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH2C-SNAPBOX actor='%s': NewObject FAILED -- the off-centre build-gun hit on this ")
                TEXT("member cannot resolve to the node and the snap will keep returning 0."),
                *Actor->GetName());
            return;
        }
        Box->SetupAttachment(Root);
        Box->RegisterComponent();
        if (Box->GetAttachParent() != Root)
        {
            Box->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
        }
    }
    // The NAMED profile, with no per-channel overrides -- EnsureNodeUseBox's own comment records that an
    // added BuildGun override flipped the profile to 'Custom' and broke the very snap it was meant to fix.
    if (Box->GetCollisionProfileName() != FName(TEXT("Resource")))
    {
        Box->SetCollisionProfileName(TEXT("Resource"));
    }
    const FVector WantExtent(WantX, WantY, WantZ);
    if (!Box->GetUnscaledBoxExtent().Equals(WantExtent, 1.0f)) { Box->SetBoxExtent(WantExtent); }
    const FString PrevName = IsValid(Existing) ? Existing->GetName() : FString(TEXT("<null>"));
    Base->mBoxComponent = Box; // the resource collider the snap resolves against

    if (bSayIt)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2C-SNAPBOX actor='%s' class='%s' pieces=%d reach=(xy=%.0f z=%.0f) clamped=%d: ")
            TEXT("%s 'NodeShuffleWellUseBox_Rt' profile='Resource' extent=%s; mBoxComponent re-pointed ")
            TEXT("'%s' extent=%s -> ours. This is what an off-centre hit on a NodeShuffleWellMesh_* piece ")
            TEXT("now resolves against. clamped=1 means a piece reaches past the 900 cm ceiling (half the ")
            TEXT("measured 1818.8 cm min inter-satellite spacing) and is NOT fully covered. ")
            TEXT("|| T3 OVERLAP: finalExtent=%s; nearest ordinary MINEABLE node (AFGResourceNode; ")
            TEXT("DEPOSITS EXCLUDED -- only this class ever carries EnsureNodeUseBox's 650 cm box) ")
            TEXT("is %.0f cm away, per-axis |dx|/|dy|/|dz| = %.0f/%.0f/%.0f cm, measured against a ")
            TEXT("snapshot of activeMineable=%d such node(s) with a further hiddenOriginal=%d ")
            TEXT("EXCLUDED (ns-t7-split: a hidden original cannot be built on regardless of any box, ")
            TEXT("so an overlap against one was a FALSE POSITIVE -- both counts are printed so a ")
            TEXT("smaller activeMineable on this build is readable as THIS change, not as a world ")
            TEXT("that lost nodes), taken on WELLH2B-INDEX pass %d, and it is now pass %d ")
            TEXT("(age = %d pass(es); a snapshot older than 0 passes may MISS nodes that streamed in ")
            TEXT("since, which reports the distance TOO LARGE and can print a FALSE ")
            TEXT("provableOverlap=0); provableOverlap=%d. A -1 distance with activeMineable=0 / ")
            TEXT("pass=-1 means ")
            TEXT("NOT MEASURED (RebuildWellMeshIndex had not run in this process/world yet). A -1 ")
            TEXT("distance with activeMineable=0 and a pass >= 0 means the snapshot WAS built and ")
            TEXT("contained no candidate ")
            TEXT("at all -- also not a measurement of \"nothing nearby\", just an empty population. ")
            TEXT("Neither means nothing is nearby. provableOverlap=1 means the two ")
            TEXT("axis-aligned boxes overlap on ALL THREE axes IF that node carries EnsureNodeUseBox's ")
            TEXT("standard 650/650/180 cm box. NOT MEASURED here: whether that node actually has that ")
            TEXT("box, and whether a Miner then refuses to place on it -- both are runtime tests."),
            *Actor->GetName(), *Actor->GetClass()->GetName(), Pieces, RawXY, RawZ, bClamped ? 1 : 0,
            bCreated ? TEXT("created") : TEXT("re-asserted"), *WantExtent.ToCompactString(),
            *PrevName, *ExistingExtent.ToCompactString(),
            *FinalExtent.ToCompactString(), NearestNonWellCm, NearestDX, NearestDY, NearestDZ,
            BystanderCount, HiddenExcluded, BystanderFromPass, SnapBoxCurrentAuditPass,
            SnapBoxCurrentAuditPass - BystanderFromPass, bProvableOverlap);
    }
}
