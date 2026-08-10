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
#include "NodeShuffleWellStage0.h"   // ns-t35-gatereach: the gate enum the reached array is indexed by

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
// ns-t35-gatereach: OutRays RECORDS, IT DOES NOT DECIDE. It exists so NodeShuffle.Here can print this
// predicate's per-ray working while calling THIS function -- the one the placement paths call -- rather
// than a second copy that could drift from it, which is T26's defect in a new place. Everything that
// produces the verdict is unchanged: the same 8 bearings, the same 500 cm reach from At.Z+200, the same
// ECC_WorldStatic trace with the same query params, the same 7-of-8 threshold, the same return. The
// recorder writes only inside `if (OutRays)`, and every placement caller passes nullptr.
// ns-t36-probefix AMENDS ONE CLAUSE OF THAT SENTENCE: the query params are the same for every caller
// that passes no IgnoreActor, which is both placement call sites. A caller that passes one gets that
// one actor on the ignore list and nothing else changed. See the paragraph on the function itself.
// ns-t36-probefix: IgnoreActor ADDS ONE ACTOR TO THE TRACE'S IGNORE LIST AND CHANGES NOTHING ELSE.
// docs/TECH-DEBT.md T36: a character blocks ECC_WorldStatic, and this query previously named no ignored
// actor at all -- so NodeShuffle.Here, whose whole job is to probe the spot the player is standing on,
// cast 8 rays that terminated inside the player's own capsule at 0 cm and concluded ENCLOSED. That is a
// reading with no terrain in it, and it was unobtainable by construction: the command has to stand on
// the spot to test it. The parameter defaults to nullptr, both placement call sites pass nothing, and
// the placement behaviour is therefore bit-identical -- ignoring a pawn during PLACEMENT would be a
// change to a gate the ordinary node path shares and is deliberately not done here.
bool ANodeShuffleSubsystem::IsSpotEnclosed(const FVector& At, int32& OutBlockedRays,
                                           int32& OutTotalRays,
                                           TArray<FNodeShuffleEnclosureRay>* OutRays,
                                           int32* OutBlockedThreshold,
                                           const AActor* IgnoreActor) const
{
    OutBlockedRays = 0;
    OutTotalRays = WellEnclosureRayCount;
    if (OutRays) { OutRays->Reset(); }
    // Written from the same named constant this function's return statement compares the blocked count
    // against -- never from a number a caller typed into a log string.
    if (OutBlockedThreshold) { *OutBlockedThreshold = WellEnclosureBlockedThreshold; }
    UWorld* World = GetWorld();
    // No world means the test could not be RUN. Report not-enclosed and a zero blocked count -- the
    // caller's own no-world guard is the one that refuses; this must not manufacture a verdict.
    // With a recorder attached the array is left EMPTY, which is how a caller tells "no rays were cast"
    // from "8 rays were cast and none hit".
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
        // ns-t36-probefix: the ONLY new statement in this function. With IgnoreActor null -- which is
        // what every placement caller passes -- EncParams is byte-for-byte what it was before.
        if (IgnoreActor) { EncParams.AddIgnoredActor(IgnoreActor); }
        const bool bHit = World->LineTraceSingleByChannel(EncHit, Eye, To, ECC_WorldStatic, EncParams);
        if (bHit)
        {
            ++OutBlockedRays;
        }
        if (OutRays)
        {
            FNodeShuffleEnclosureRay Rec;
            Rec.BearingDeg = FMath::RadiansToDegrees(static_cast<double>(Ang));
            Rec.bBlocked = bHit;
            // The distance is taken from the hit this ray reported, not from the reach constant: a
            // blocking hit can be anywhere along the segment and the constant is only its far end.
            Rec.HitDistanceCm = bHit ? FVector::Dist(Eye, EncHit.ImpactPoint) : -1.0;
            const AActor* HitActor = bHit ? EncHit.GetActor() : nullptr;
            Rec.HitActor = HitActor ? HitActor->GetName()
                                    : (bHit ? FString(TEXT("<hit-with-no-actor>")) : FString(TEXT("<no-hit>")));
            OutRays->Add(Rec);
        }
    }
    return OutBlockedRays >= WellEnclosureBlockedThreshold;
}

// ------------------------------------------------------------------------------------------------
// ns-t27-perf: THE ONE WORLD SCAN PER GROUP PER PASS
// ------------------------------------------------------------------------------------------------
// The node-overlap gate below used to walk ULevel::Actors with TActorIterator<AFGResourceNode> ON EVERY
// PROBE. The cold review ESTIMATED that as the dominant cost of a placement pass (T27-fixes-review.md
// section 5, under a heading that says "The estimate"): an order-of-magnitude argument from an assumed
// actor count and an assumed per-actor cost. NOTHING WAS TIMED -- there was no clock on this path until
// F-A added one in the same packet, so no millisecond figure for the OLD binary can exist. The probe
// COUNT is measured; the milliseconds are not, and runtime step 1 is what turns the estimate into a
// number. What is STRUCTURAL rather than estimated is the property that makes it dangerous: the
// iterator walks the level's actor list, so it scales with the number of ACTORS IN THE WORLD -- every
// conveyor segment, wall, foundation and machine the player has built -- not with the number of
// resource nodes. So it costs more the longer a save has been played, and a well relocating near a
// large factory is exactly where it costs the most.
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
// The scan is therefore a superset of every actor that could REJECT any candidate this call will
// probe. It is NOT a superset of the per-probe walk it replaces -- that walk reached every
// AFGResourceNode in the level and this set is a strict SUBSET of it, bounded to
// WellSatMaxRadiusCm + WellOverlapRejectRadiusCm (7300 cm as those two constants stand today) in XY
// about the settled core. That distinction is load-bearing: this cache is safe for THIS gate at THIS
// reject radius and for nothing else. A second consumer with a different radius, or a draw that
// reaches further than WellSatMaxRadiusCm, is outside the proof above and will silently under-scan.
// It is deliberately measured in XY and not in 3-D: a 3-D radius would be a SMALLER set (3-D distance
// is the larger number), and would silently drop a node beside a satellite that settled far below its
// core -- the exact shape of a population bug that reads as a clean optimisation.
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
                                                   FRotator& OutRot, FString& OutReason,
                                                   int32* OutGatesReached) const
{
    // ns-t35-gatereach: the caller hands in a bare int32* because this header may not include the
    // private census header. That makes the array WIDTH an unchecked contract, so it is checked here --
    // the one place that knows both sides -- rather than left to a future edit of the enum.
    static_assert(FNodeShuffleWellProbeCensus::Gate_Count == 9,
                  "ns-t35-gatereach: the gate enum changed width; every OutGatesReached caller passes a "
                  "fixed-size array indexed by it and must be revisited before this assert is relaxed.");
    // Increments the reached slot for gate G, and does nothing at all when no recorder was supplied.
    // Called immediately BEFORE the gate it names, so it counts chances to fire and never outcomes.
    const auto Reach = [OutGatesReached](int32 G) { if (OutGatesReached) { ++OutGatesReached[G]; } };

    OutReason.Reset();
    // The unclassified slot's denominator is every probe that got this far -- see the field's own
    // comment in NodeShuffleWellStage0.h. It is counted before the no-world guard on purpose: the
    // no-world refusal classifies as unclassified and must be inside its own denominator.
    Reach(FNodeShuffleWellProbeCensus::Gate_Unclassified);
    UWorld* World = GetWorld();
    if (!World) { OutReason = TEXT("no-world"); return false; }

    bool bWater = false, bSteep = false;
    // Long top-down trace. Wells are surface features; there is no cave path for them, so the short
    // trace (and its whole cave-floor ruleset) deliberately does not apply.
    Reach(FNodeShuffleWellProbeCensus::Gate_Void);
    if (!RaycastGroundAt(ProbeXY, StartZ, nullptr, nullptr, OutLoc, OutRot, bWater, /*bShortTrace=*/false,
                         &bSteep))
    {
        // TRUE VOID -- no terrain under the probe. This is NOT a rejection of the spot: it means the
        // region has not streamed in. The caller must DEFER the whole group rather than counting a
        // nudge, or an unstreamed edge of the footprint would burn the entire budget on nothing.
        OutReason = TEXT("void(unstreamed)");
        return false;
    }
    // Both flags were written by the single RaycastGroundAt call above, so each gate below is reached
    // exactly when the one before it did not return -- the count is of the TEST, not of the trace.
    Reach(FNodeShuffleWellProbeCensus::Gate_Water);
    if (bWater) { OutReason = TEXT("water"); return false; }
    Reach(FNodeShuffleWellProbeCensus::Gate_Cliff);
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
    // Counted ONCE per probe, not once per candidate actor: the gate is the whole scan, and the thing
    // with a denominator is how many probes the scan was run for.
    Reach(FNodeShuffleWellProbeCensus::Gate_NodeOverlap);
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
        Reach(FNodeShuffleWellProbeCensus::Gate_Buildable);
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
        // INSIDE the flag test, so the reached count answers the question T35 asks: how often this gate
        // actually ran, not how often a probe got as far as the flag that decides whether it runs.
        // ns-t36-probefix (T35 cold review F1) CORRECTS WHAT THIS COMMENT USED TO CLAIM. It said a zero
        // here beside a non-zero BUILDABLE reached count showed the flag was false for every probe. It
        // does not: a probe REJECTED by the buildable gate returns above and never reaches this counter,
        // so that pair is equally consistent with every such probe having been rejected there. Nothing
        // stands between the buildable gate and this line but the flag, so the comparison that does
        // settle it needs BOTH buildable halves -- buildable reached minus buildable rejected is the
        // number of probes that cleared it, and if that is non-zero while this counter is zero, the flag
        // was false for all of them. The shipped census legend states the rule correctly; only this
        // comment was wrong, and it was wrong in the direction that reads as a finding.
        Reach(FNodeShuffleWellProbeCensus::Gate_Enclosed);
        int32 Blocked = 0, Total = 0;
        if (IsSpotEnclosed(OutLoc, Blocked, Total))
        {
            OutReason = FString::Printf(TEXT("enclosed(blockedRays %d of %d)"), Blocked, Total);
            return false;
        }
    }
    return true;
}
