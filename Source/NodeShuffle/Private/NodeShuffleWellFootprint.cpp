// Packet H2 (ns-wells-h2, branch feature/extractor-automatch): THE FOOTPRINT TEST -- whether ONE member
// of a relocating well group can stand at a given XY. It is the smallest and most-called piece of H2
// (36 yaws x up to 10 satellites x 8 nudges x 3 redeals in the worst case), and it is the piece design
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
}

// ------------------------------------------------------------------------------------------------
// ONE MEMBER'S TERRAIN TEST
// ------------------------------------------------------------------------------------------------
// Deliberately the SAME set of gates the ordinary node spawn applies, in the same order, because a
// relocated well member is an ordinary node as far as the terrain is concerned. What is NOT reused is
// EnsureNewNodeSpawned's OverlapsAt: that is a lambda local to a function that also mutates a layout
// entry, and it self-excludes by EntryGuid, which a well member does not have. Re-stating the two
// radii here (with the constants named after their originals) was judged safer than reshaping a
// 400-line function that the whole mod's node placement depends on.
bool ANodeShuffleSubsystem::ValidateWellMemberSpot(const FVector& ProbeXY, float StartZ, FVector& OutLoc,
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
    for (TActorIterator<AFGResourceNode> It(World); It; ++It)
    {
        AFGResourceNode* Other = *It;
        if (!IsValid(Other) || Other->IsA<AFGResourceDeposit>()) { continue; }
        if (FVector::DistSquared(Other->GetActorLocation(), OutLoc) >= RejSq) { continue; }
        bool bOurs = false;
        for (const TPair<FString, AFGResourceNodeFrackingSatellite*>& P : SpawnedWellSatellites)
        {
            if (P.Value == Other) { bOurs = true; break; }
        }
        // ns-review-h2 F7: OUR OWN SPAWNED CORES COUNT TOO. The exclusion listed satellites only, so a
        // group whose satellites all failed to spawn while its core succeeded would, on the next pass,
        // validate against the orphan core it had left behind -- and nudge the whole group away from
        // its own actor, chasing itself across the map. AFGResourceNodeFrackingCore is an
        // AFGResourceNodeBase, not an AFGResourceNode, so it does not appear in the iterator above;
        // this loop is here for the general case and for any future core class that does.
        if (!bOurs)
        {
            for (const TPair<FString, AFGResourceNodeFrackingCore*>& P : SpawnedWellCores)
            {
                if (static_cast<AActor*>(P.Value) == static_cast<AActor*>(Other)) { bOurs = true; break; }
            }
        }
        if (bOurs) { continue; }
        OutReason = FString::Printf(TEXT("node-overlap(%s)"), *Other->GetName());
        return false;
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
    return true;
}
