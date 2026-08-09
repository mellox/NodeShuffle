// Packet H2 (ns-wells-h2, branch feature/extractor-automatch): THE FOOTPRINT TEST -- whether ONE member
// of a relocating well group can stand at a given XY. It is the smallest and most-called piece of H2,
// and it is the piece design
// ns-t27-fixes-review F-C (fourth site): the worst-case cost model that used to sit on the line above --
// "36 yaws x up to 10 satellites x 8 nudges x 3 redeals" -- described the RETIRED rigid yaw search and
// was still stated as current here, in the exact file the placement file's own cost comment sends
// readers to. Under ns-t27-corefirst the worst case is 6 layout attempts x up to 10 satellites x 24
// draws per satellite x 8 nudges x 3 redeals = 34,560, not 8,640: a 4x understatement, which is the same
// defect F1 corrected one file over. Numbers restated from the constants in NodeShuffleSubsystem.h
// (WellLayoutAttempts, WellSatPlacementTries, WellMaxGroupNudges, WellMaxGroupRedeals) and from H0's
// measured satellite maximum; nothing here is a runtime measurement.
// §4b explicitly handed to this packet: H0 measured the 800 cm resource-node half of the overlap
// question world-wide but deliberately scoped OUT "the 600 cm non-foundation AFGBuildable half", saying
// "H2's own footprint validation covers it". This file is that coverage.
//
// It is split out of NodeShuffleWellRelocateApply.cpp for the 500-line limit -- the same split H0 made
// (NodeShuffleWellCensus out of NodeShuffleWellDump) and H1 made (NodeShuffleWellRetype.h out of its
// two halves) -- and the seam is real rather than arbitrary: everything here answers "can a NODE stand
// HERE", with no knowledge of groups, yaws, budgets or the layout. The caller owns all of that.
//
// A MEMBER function rather than a free function, for the reason NodeShuffleWellRetype.h states for
// H1's: it reads the subsystem's spawned-well handles and calls RaycastGroundAt, and C++ friendship is
// class-to-class, not file-to-file.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleWellCensus.h"   // AFGResourceNodeFrackingSatellite (the self-exclusion scan)

#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceDeposit.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableFoundation.h"

namespace
{
    // Mirrors of NodeShuffleSubsystem.cpp's file-local placement constants (anonymous namespace there,
    // so not reachable here). Same values, named to the same things, so the two stay greppable, and
    // deliberately the SAME two radii the ordinary node spawn's OverlapsAt lambda uses -- a relocated
    // well member is an ordinary node as far as the terrain is concerned.
    constexpr float WellOverlapRejectRadiusCm = 800.0f;   // OverlapsAt's node footprint
    constexpr float WellBuildingRejectRadiusCm = 600.0f;  // OverlapsAt's non-foundation buildable radius

    // ns-t27-corefirst: the enclosure ray parameters, previously buried in EnsureNewNodeSpawned's
    // IsEnclosed lambda and now the single definition both paths use. 8 horizontal rays of 500 cm
    // from 200 cm above the settled point; enclosed at 7 or more blocked.
    constexpr float WellEnclosureEyeHeightCm = 200.0f;
    constexpr float WellEnclosureReachCm = 500.0f;
    constexpr int32 WellEnclosureRayCount = 8;
    constexpr int32 WellEnclosureBlockedThreshold = 7;
}

// ------------------------------------------------------------------------------------------------
// ns-t27-corefirst: THE ONE ENCLOSURE TEST -- shared by the node path and the well path
// ------------------------------------------------------------------------------------------------
// A spot is "enclosed" if boxed in by rock on nearly all sides at close range -- e.g. the bottom of a
// narrow vertical slot between rock columns: open ABOVE (the down-settle trace came through the shaft)
// but unreachable horizontally. Real caves, ledges and cliff-bases are NOT flagged; they are open on
// several sides. The ground trace takes the FIRST blocking hit from high above, so a spot open above
// and blocked horizontally passes every OTHER gate there is, which is exactly the T26 hole.
//
// It was a lambda private to EnsureNewNodeSpawned. That scoping IS T26: the well path could not reach
// it, so the well path did not have it, and a comment two files away claimed it did. This is now a
// member with ONE definition; EnsureNewNodeSpawned's lambda delegates here rather than keeping a
// second copy, because two copies of a gate that must agree is the same defect one indirection later.
bool ANodeShuffleSubsystem::IsSpotEnclosed(const FVector& At, int32& OutBlockedRays,
                                           int32& OutTotalRays) const
{
    OutBlockedRays = 0;
    OutTotalRays = WellEnclosureRayCount;
    UWorld* World = GetWorld();
    // No world means the test could not be RUN. Report not-enclosed and a zero blocked count -- the
    // caller's own no-world guard is the one that refuses; this must not manufacture a verdict.
    if (!World) { return false; }

    const FVector Eye(At.X, At.Y, At.Z + WellEnclosureEyeHeightCm);
    for (int32 d = 0; d < WellEnclosureRayCount; ++d)
    {
        const float Ang = (2.0f * PI * d) / WellEnclosureRayCount;
        const FVector To(Eye.X + WellEnclosureReachCm * FMath::Cos(Ang),
                         Eye.Y + WellEnclosureReachCm * FMath::Sin(Ang),
                         Eye.Z);
        FHitResult EncHit;
        FCollisionQueryParams EncParams(FName(TEXT("NodeShuffleEnclosure")), false);
        if (World->LineTraceSingleByChannel(EncHit, Eye, To, ECC_WorldStatic, EncParams))
        {
            ++OutBlockedRays;
        }
    }
    return OutBlockedRays >= WellEnclosureBlockedThreshold;
}

// ------------------------------------------------------------------------------------------------
// ns-t27-perf: THE ONE WORLD SCAN PER GROUP PER PASS
// ------------------------------------------------------------------------------------------------
// The node-overlap gate below used to walk ULevel::Actors with TActorIterator<AFGResourceNode> ON EVERY
// PROBE. The cold review measured that as the dominant cost of a placement pass, and named the property
// that makes it dangerous: it scales with the number of ACTORS IN THE WORLD -- every conveyor segment,
// wall, foundation and machine the player has built -- not with the number of resource nodes. So it
// costs more the longer a save has been played, and a well relocating near a large factory is exactly
// where it costs the most.
//
// WHY THIS IS NOT THE BROADPHASE CHANGE, WHICH IS STILL REFUSED. Replacing the iteration with an
// overlap query would change the POPULATION the gate sees (a broadphase reaches only collision-enabled
// actors), and that is this workspace's named burn class -- a right-looking predicate over a wrong set.
// This does not change the query at all. It runs THE SAME TActorIterator, with the SAME deposit
// exclusion, once instead of 25-240 times, and keeps every per-probe test where it was.
//
// THE POPULATION ARGUMENT, STATED SO IT CAN BE CHECKED RATHER THAN TRUSTED:
//   * the gate rejects on 3-D distance < WellOverlapRejectRadiusCm from the SETTLED candidate;
//   * 3-D distance >= XY distance, so anything inside that sphere is inside the same radius in XY;
//   * RaycastGroundAt traces straight down from (ProbeXY.X, ProbeXY.Y) and writes Hit.ImpactPoint, so a
//     settled candidate's XY IS its probe's XY -- verified at NodeShuffleSubsystem.cpp's
//     RaycastGroundAt, which never rewrites X or Y;
//   * every satellite probe is drawn at XY radius <= WellSatMaxRadiusCm from the scan centre;
//   * therefore XY distance from the scan centre to any node that could reject any candidate is under
//     WellSatMaxRadiusCm + WellOverlapRejectRadiusCm, which is this scan's radius.
// The scan is therefore a strict SUPERSET of the union of every per-probe walk it replaces. It is
// deliberately measured in XY and not in 3-D: a 3-D radius would be a SMALLER set (3-D distance is the
// larger number), and would silently drop a node beside a satellite that settled far below its core --
// the exact shape of a population bug that reads as a clean optimisation.
// The relative-Z gate does not help here and is not relied on: it runs AFTER this test, so at scan time
// a candidate's Z is unbounded.
//
// WHY THE CACHED ENTRIES ARE STILL RE-TESTED PER PROBE. The deposit exclusion, the radius test and the
// F7 self-exclusion all still run in ValidateWellMemberSpot, in the same order, over entries kept in
// TActorIterator order -- so the FIRST actor that rejects a candidate is the same actor it always was,
// and the `node-overlap(<name>)` reason string is unchanged. Nothing spawns a resource node inside one
// TryPlaceWellGroup call (SpawnWellGroup runs after it returns), so the set cannot go stale mid-call;
// TWeakObjectPtr is used anyway so a stale entry degrades to a skip rather than to a dangling read.
void ANodeShuffleSubsystem::BuildWellNodeScanCache(const FVector& ScanCentre,
                                                   TArray<TWeakObjectPtr<AFGResourceNode>>& OutNodes) const
{
    OutNodes.Reset();
    UWorld* World = GetWorld();
    if (!World) { return; }
    const double ScopeRadius = static_cast<double>(WellSatMaxRadiusCm)
                             + static_cast<double>(WellOverlapRejectRadiusCm);
    const double ScopeSq = ScopeRadius * ScopeRadius;
    int32 Walked = 0;
    for (TActorIterator<AFGResourceNode> It(World); It; ++It)
    {
        ++Walked;
        AFGResourceNode* Other = *It;
        if (!IsValid(Other) || Other->IsA<AFGResourceDeposit>()) { continue; }
        if (FVector::DistSquared2D(Other->GetActorLocation(), ScanCentre) >= ScopeSq) { continue; }
        OutNodes.Add(Other);
    }
    UE_LOG(LogNodeShuffle, Verbose,
        TEXT("WELLH2-NODESCAN centre=%s: walked %d AFGResourceNode actor(s) and kept %d within %.0f cm ")
        TEXT("in XY (the satellite draw's outer radius plus the node reject radius). This ran ONCE for ")
        TEXT("this group this pass; before ns-t27-perf the same walk ran inside every probe. Both ")
        TEXT("numbers were counted by this call; neither states why any node is or is not in the set."),
        *ScanCentre.ToCompactString(), Walked, OutNodes.Num(), ScopeRadius);
}

// ------------------------------------------------------------------------------------------------
// ONE MEMBER'S TERRAIN TEST
// ------------------------------------------------------------------------------------------------
// The gates the ordinary node spawn applies, in the same order, because a relocated well member is an
// ordinary node as far as the terrain is concerned. What is NOT reused is EnsureNewNodeSpawned's
// OverlapsAt: that is a lambda local to a function that also mutates a layout entry, and it
// self-excludes by EntryGuid, which a well member does not have. Re-stating the two radii here (with
// the constants named after their originals) was judged safer than reshaping a 400-line function that
// the whole mod's node placement depends on.
//
// ns-t27-corefirst -- THE PARITY CLAIM, RESTATED HONESTLY. The previous version of this paragraph read
// "deliberately the SAME set of gates ... in the same order" and that was FALSE for a year of this
// file's life: the node path applies SIX gates and this function applied the first five, with no
// enclosure test at all. docs/TECH-DEBT.md T26 has the full account, including that this very comment
// is how the gap survived an audit -- someone checking the well path against the node path read the
// parity claim and stopped. So the claim is now conditional and says what the condition is:
//   * void / water / cliff / node-overlap 800 cm / buildable 600 cm -- ALWAYS, same as the node path.
//   * enclosure -- ONLY when bApplyEnclosureGate. The CORE always passes true (T27's whole point).
//     Satellites pass WellEnclosureGateOnSatellites, which is a named constant, not a hard-coded
//     false, precisely so this can never again be a silent omission.
// If you are auditing gate parity, the list above is the claim; go and count the node path's gates.
bool ANodeShuffleSubsystem::ValidateWellMemberSpot(const FVector& ProbeXY, float StartZ,
                                                   bool bApplyEnclosureGate,
                                                   const TArray<TWeakObjectPtr<AFGResourceNode>>* NodeScanCache,
                                                   FVector& OutLoc,
                                                   FRotator& OutRot, FString& OutReason) const
{
    OutReason.Reset();
    UWorld* World = GetWorld();
    if (!World) { OutReason = TEXT("no-world"); return false; }

    bool bWater = false, bSteep = false;
    // Long top-down trace. Wells are surface features; there is no cave path for them, so the short
    // trace (and its whole cave-floor ruleset) deliberately does not apply.
    if (!RaycastGroundAt(ProbeXY, StartZ, nullptr, nullptr, OutLoc, OutRot, bWater, /*bShortTrace=*/false,
                         &bSteep))
    {
        // TRUE VOID -- no terrain under the probe. This is NOT a rejection of the spot: it means the
        // region has not streamed in. The caller must DEFER the whole group rather than counting a
        // nudge, or an unstreamed edge of the footprint would burn the entire budget on nothing.
        OutReason = TEXT("void(unstreamed)");
        return false;
    }
    if (bWater) { OutReason = TEXT("water"); return false; }
    if (bSteep) { OutReason = TEXT("cliff"); return false; }

    // Resource-node overlap. Deposits are excluded for the same reason the node path excludes them
    // (tiny one-off rocks, not blocking nodes). Our OWN already-spawned group members are excluded so
    // a re-validation after a spawn cannot reject the group against itself -- note this is NOT the
    // same-group exemption design §4b ruled out: that was about satellites rejecting EACH OTHER at
    // validation time, which H0 measured cannot happen and the roll now asserts per well.
    const float RejSq = FMath::Square(WellOverlapRejectRadiusCm);
    // ns-t27-perf: ONE COPY OF THE PREDICATE, TWO SOURCES OF ACTORS. The cached path and the walking
    // path must never be able to drift apart in what they REJECT -- a gate with two copies is T26 one
    // indirection later -- so the test lives here once and the loops below only decide where the actors
    // come from. Returns true when Other rejects the spot, and writes OutReason when it does.
    const auto RejectsSpot = [&](AFGResourceNode* Other) -> bool
    {
        if (!IsValid(Other) || Other->IsA<AFGResourceDeposit>()) { return false; }
        if (FVector::DistSquared(Other->GetActorLocation(), OutLoc) >= RejSq) { return false; }
        bool bOurs = false;
        for (const TPair<FString, AFGResourceNodeFrackingSatellite*>& P : SpawnedWellSatellites)
        {
            if (P.Value == Other) { bOurs = true; break; }
        }
        // ns-review-h2 F7: OUR OWN SPAWNED CORES COUNT TOO. The exclusion listed satellites only, so a
        // group whose satellites all failed to spawn while its core succeeded would, on the next pass,
        // validate against the orphan core it had left behind -- and nudge the whole group away from
        // its own actor, chasing itself across the map. AFGResourceNodeFrackingCore is an
        // AFGResourceNodeBase, not an AFGResourceNode, so it does not appear in the iterator below;
        // this loop is here for the general case and for any future core class that does.
        if (!bOurs)
        {
            for (const TPair<FString, AFGResourceNodeFrackingCore*>& P : SpawnedWellCores)
            {
                if (static_cast<AActor*>(P.Value) == static_cast<AActor*>(Other)) { bOurs = true; break; }
            }
        }
        if (bOurs) { return false; }
        OutReason = FString::Printf(TEXT("node-overlap(%s)"), *Other->GetName());
        return true;
    };
    if (NodeScanCache)
    {
        // The hoisted set (BuildWellNodeScanCache above). Kept in TActorIterator order, so the first
        // rejecting actor -- and therefore the reason string -- is the same one the walk below produces.
        for (const TWeakObjectPtr<AFGResourceNode>& Weak : *NodeScanCache)
        {
            if (RejectsSpot(Weak.Get())) { return false; }
        }
    }
    else
    {
        // No cache supplied: walk the level, exactly as this gate always did. This is the CORE probe's
        // path (there is no settled centre to scan around until the core itself has settled) and the
        // path any future caller gets by default.
        for (TActorIterator<AFGResourceNode> It(World); It; ++It)
        {
            if (RejectsSpot(*It)) { return false; }
        }
    }

    // Player machinery. Foundations and ramps are EXEMPT -- the user's standing call is that a node on
    // a foundation is fine; a node inside a machine is not.
    {
        TArray<FOverlapResult> Hits;
        FCollisionObjectQueryParams ObjParams;
        ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
        ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);
        FCollisionQueryParams QParams(FName(TEXT("NodeShuffleWellFootprint")), false);
        if (World->OverlapMultiByObjectType(Hits, OutLoc, FQuat::Identity, ObjParams,
                FCollisionShape::MakeSphere(WellBuildingRejectRadiusCm), QParams))
        {
            for (const FOverlapResult& H : Hits)
            {
                AActor* HitActor = H.GetActor();
                if (HitActor && HitActor->IsA<AFGBuildable>() && !HitActor->IsA<AFGBuildableFoundation>())
                {
                    OutReason = FString::Printf(TEXT("buildable(%s)"), *HitActor->GetName());
                    return false;
                }
            }
        }
    }

    // ns-t27-corefirst: GATE SIX -- ENCLOSURE. Last, because that is where the node path applies it
    // (NodeShuffleSubsystem.cpp, EnsureNewNodeSpawned: OverlapsAt then IsEnclosed), and this function
    // claims to evaluate in the node path's order.
    //
    // The reason string reports the RAY COUNT THIS CALL MEASURED and nothing else. It does not say the
    // spot is in a slot, a crevice, a column or a cave -- the 8-ray predicate cannot tell those apart,
    // and naming one of them would be this repo's log-asserted-a-cause defect in its purest form.
    if (bApplyEnclosureGate)
    {
        int32 Blocked = 0, Total = 0;
        if (IsSpotEnclosed(OutLoc, Blocked, Total))
        {
            OutReason = FString::Printf(TEXT("enclosed(blockedRays %d of %d)"), Blocked, Total);
            return false;
        }
    }
    return true;
}
