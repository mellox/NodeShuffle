#pragma once

// Packet H2 (ns-wells-h2, branch feature/extractor-automatch): the shared, PURE geometry and
// determinism helpers of RIGID resource-well RELOCATION. Split out for the same two reasons H0 split
// NodeShuffleWellCensus.h out of NodeShuffleWellDump.cpp and H1 split NodeShuffleWellRetype.h out of
// its two halves: the implementation files stay under the 500-line limit, and the yaw permutation in
// particular must have EXACTLY ONE implementation -- it is consumed by the roll (for the log) and by
// the apply (for the search), and two copies that drifted would silently break determinism.
//
// WHAT LIVES WHERE, so the next reader does not hunt:
//   * NodeShuffleWellRelocateRoll.cpp  -- ANodeShuffleSubsystem::RollWellRelocation
//                                         (rigid-body capture from live actors + destination deal)
//   * NodeShuffleWellRelocateApply.cpp -- ApplyWellRelocation / TryPlaceWellGroup /
//                                         SuppressVanillaWellGroup  (WHERE to try, and does it fit)
//   * NodeShuffleWellFootprint.cpp     -- ValidateWellMemberSpot: "can ONE node stand HERE", with no
//                                         knowledge of groups, yaws or budgets. This is the 600 cm
//                                         AFGBuildable half of the overlap question that H0
//                                         deliberately scoped out and handed to H2 (design §4b).
//   * NodeShuffleWellEscalate.cpp      -- NoteWellVoidDefer / EscalateWellPlacement: WHAT HAPPENS WHEN
//                                         IT DOES NOT FIT. One ladder, called from both failure sites.
//                                         It is its own file because ns-review-h2 F3 was caused
//                                         precisely by that decision living in two places, only one of
//                                         which was complete.
//   * NodeShuffleWellLink.cpp          -- EnsureSatelliteLinked / the lazy-adoption helpers /
//                                         AdoptRestoredWellGroups  (the mCore lifecycle, design R1)
//   * NodeShuffleWellSpawn.cpp         -- SpawnWellGroup: the group-atomic materialisation. Adjacent to
//                                         the link BECAUSE the spawn IS the link -- mCore has to be
//                                         written between SpawnActorDeferred and FinishSpawning.
//   * NodeShuffleWellDespawn.cpp       -- DespawnWellGroup / DespawnStaleWellMembers: CALL-SITE-driven
//                                         teardown, the half the packet shipped twice without
//                                         (ns-review-h3 H1). Its own file because a missing half of a
//                                         lifecycle deserves a name, not a paragraph in the other half.
//   * NodeShuffleWellSweep.cpp         -- SweepOrphanedWellActors: STATE-driven reclamation. Pass A is
//                                         handle-driven (h4 F2), pass B is location-driven over the
//                                         actor iterators and catches an abandoned actor that holds no
//                                         handle at all (h5 F-1). Separate from the despawn because the
//                                         distinction is the whole lesson of two reviews: call-site
//                                         teardown covers routes someone remembered, a state scan
//                                         covers the ones nobody did.
//   * NodeShuffleWellAudit.cpp         -- AuditOneWellGroup / AuditWellGroupLinks: the acceptance gate,
//                                         i.e. the only part of the packet whose job is to disbelieve
//                                         the rest of it.
// All of those are MEMBERS of ANodeShuffleSubsystem merely DEFINED in those files: writing
// AFGResourceNodeBase::mResourceClassOverride and AFGResourceNodeFrackingSatellite::mCore both depend
// on this project's AccessTransformers Friend grants, and C++ friendship is class-to-class, not
// file-to-file. Same constraint, same solution, as H1.
//
// Every function in THIS header is pure and does not log: each caller owns its own prefix and cadence.

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Resources/FGResourceNodeFrackingSatellite.h" // brings AFGResourceNodeFrackingCore too
#include "Buildables/FGBuildableFrackingActivator.h"   // completeness for the by-value TWeakObjectPtr
#include "Buildables/FGBuildableFrackingExtractor.h"   // ditto -- includes only, no methods called

// ---------------------------------------------------------------------------------------------
// IS THIS WELL MEMBER IN USE?  (ns-review-h4 F1, BLOCKING)
// ---------------------------------------------------------------------------------------------
// THE ONE predicate that gates every DESTRUCTIVE and every SUPPRESSIVE act on a well member.
//
// The despawn originally gated on IsOccupied() alone. That is a WEAKER test than this mod had already
// decided was insufficient for these exact classes: RollWellLayout's pin logic is two-part, and the
// `||` exists precisely because IsOccupied() was not trusted on its own --
//     core: GetActivator().IsValid() || IsOccupied()
//     sat:  GetExtractor().IsValid() || IsOccupied()
// Dropping the half that was added out of distrust, and dropping it specifically on the path that
// DESTROYS an actor, is the wrong direction to be wrong in.
//
// It is worse than a mismatch, because none of the three signals survives a save cleanly:
// AFGResourceNodeBase::mIsOccupied is NOT SaveGame, and mActivator / mExtractor are bare
// TWeakObjectPtrs -- all three are re-established by the restored BUILDING's BeginPlay. So there is a
// post-load window in which a well with a Pressurizer bolted to it reports false on every occupancy
// signal there is. This packet's own audit waits until pass 8 for exactly that reason. Reading ANY of
// these early is unsafe; reading only the weakest of them and then calling Destroy() is how you delete
// a player's machinery.
//
// So: one function, used at every site, and it names WHICH half fired so a refusal is diagnosable.
// Non-const because GetActivator()/GetExtractor() are non-const accessors returning by value.
inline bool IsWellMemberInUse(class AFGResourceNodeBase* Node, const TCHAR*& OutWhy)
{
    OutWhy = TEXT("");
    if (!IsValid(Node)) { return false; }

    if (AFGResourceNodeFrackingCore* Core = Cast<AFGResourceNodeFrackingCore>(Node))
    {
        if (Core->GetActivator().IsValid()) { OutWhy = TEXT("pressurizer-on-core"); return true; }
    }
    else if (AFGResourceNodeFrackingSatellite* Sat = Cast<AFGResourceNodeFrackingSatellite>(Node))
    {
        if (Sat->GetExtractor().IsValid()) { OutWhy = TEXT("extractor-on-satellite"); return true; }
    }
    if (Node->IsOccupied()) { OutWhy = TEXT("IsOccupied"); return true; }
    return false;
}

// ---------------------------------------------------------------------------------------------
// IDENTITY TOLERANCE  (ns-review-h3 H1)
// ---------------------------------------------------------------------------------------------
// "Is the actor at this coordinate the one this record means?" -- the same radius
// AdoptRestoredSpawnedNodes uses for ordinary relocated nodes (NodeShuffleSubsystem.cpp:
// AdoptMatchRadiusCm). A relocated well member is restored at exactly the transform we spawned it at,
// so this is a tolerance for float round-tripping through the save, not for drift.
//
// It lives here rather than in one .cpp because FOUR sites now depend on agreeing about it: lazy
// adoption, the cross-session re-match, the stale-handle guard before a reuse, and the *** GROUP
// SCATTERED *** verdict in the audit. A tolerance that decides actor identity in one file and reports
// a failure in another must have one value, not four copies that can drift apart -- which is the same
// mistake, in miniature, as the two-copies-of-the-escalation-ladder bug (ns-review-h2 F3).
constexpr float WellAdoptMatchRadiusCm = 300.0f;

// ---------------------------------------------------------------------------------------------
// DETERMINISM (design §Q3a, and test T8 is the regression guard on it)
// ---------------------------------------------------------------------------------------------
// NodeShuffle rolls once per save and only ever APPLIES afterwards, so the same seed must produce the
// same world on every load and from every vantage point. The yaw search is the one place in H2 where
// it would be easy and wrong to reach for something ambient: frame time, actor-iteration order, or a
// live RNG advanced by whatever else ran that pass would each pass every H2 test except T8.
//
// ns-review-h2 F15 -- WHAT IS AND IS NOT SEED-PURE, stated precisely, because the earlier wording
// implied more than the code delivers. Seed-pure: the CANDIDATE SEQUENCE, the redeal coordinates, the
// destination deal. NOT seed-pure: WHICH candidate wins, which nudge succeeds, and therefore where a
// well ends up -- those depend on TERRAIN QUERIES, and terrain queries depend on what has streamed in,
// so the SEARCH is deterministic but its OUTCOME is world-state-dependent. Two loads of the SAME SAVE
// reproduce the same result because the placement is persisted the moment it is committed and never
// re-searched (that is what test T8 checks). Two runs from a fresh save with the same seed but
// different exploration order need NOT place a well identically. Do not build anything on the
// stronger claim.
//
// ns-t27-corefirst: THE YAW PERMUTATION IS GONE and so are BuildWellYawOrder / WellYawDegForIndex.
// The rigid-body search rotated the whole captured cloud about the core and asked whether the WHOLE
// footprint fitted at that angle; satellites are now drawn INDEPENDENTLY around the settled core, so
// there is no group angle left to permute. What survives unchanged is the seeding discipline, because
// it is the thing test T8 actually guards: the candidate sequence is not stored and not carried in a
// live stream. It is RECOMPUTED, identically, from a seed that is a pure function of PERSISTED state:
//     save seed  ^  hash(this well's core path)  ^  redeal count  ^  nudge count
// Nudges and redeals are included on purpose: a re-dealt group should search its NEW location in a
// different order (searching the same order at a different place is not a different search), and both
// counters are UPROPERTY(SaveGame), so the result is still fully reproducible. TryPlaceWellGroup mixes
// the ATTEMPT index in on top of this, so a re-draw is a genuinely different draw.
//
// RotateWellOffsetXY below is NOT retired with the rest of the yaw machinery, and deleting it would
// break something with no obvious connection to placement -- see its own comment.
inline int32 WellLayoutSeedFor(int32 SavedSeed, const FString& CorePath, uint8 Redeals, uint8 Nudges)
{
    return SavedSeed
         ^ static_cast<int32>(GetTypeHash(CorePath))
         ^ (static_cast<int32>(Redeals) * 7919)
         ^ (static_cast<int32>(Nudges) * 104729);
}

// ---------------------------------------------------------------------------------------------
// RIGID BODY -- RETIRED FOR PLACEMENT, STILL LOAD-BEARING FOR THE BACKSTOP (design §Q3 point 2)
// ---------------------------------------------------------------------------------------------
// ns-t27-corefirst -- DO NOT DELETE THIS FUNCTION, AND DO NOT DELETE THE LocalOffset CAPTURE THAT
// FEEDS IT. Placement no longer rotates a rigid cloud, so it is tempting to read both as dead. They
// are not. NodeShuffleWellBackstop.cpp reconstructs the VANILLA site's satellite points as
// VanillaCoreLocation + RotateWellOffsetXY(LocalOffset, VanillaCoreYawDeg), and that is how the
// stranded-actor discriminator tells one of OUR abandoned actors from a genuine untouched vanilla
// well. Those offsets describe the AUTHORED well, which the placement change does not touch, so the
// reconstruction is as correct after T27 as before it. Remove either half and the backstop starts
// mistaking vanilla wells for our litter.
//
// YAW ONLY -- never pitch or roll. Tilting would push members into or out of the terrain; per-node Z
// settle already handles slope, and H0 measured four wells with >12 m of vertical spread, so the Z
// that comes back from the settle is the only Z that means anything. That is why this returns XY only
// and the caller supplies Z from its own trace.
inline FVector2D RotateWellOffsetXY(const FVector& LocalOffset, float YawDeg)
{
    const float Rad = FMath::DegreesToRadians(YawDeg);
    const float C = FMath::Cos(Rad);
    const float S = FMath::Sin(Rad);
    return FVector2D(LocalOffset.X * C - LocalOffset.Y * S,
                     LocalOffset.X * S + LocalOffset.Y * C);
}

// The minimum distance between any two points in the offset cloud (core included as the origin).
// H0 measured 1818.8 cm across 401 satellite pairs and 2076 cm core-to-satellite, against the 800 cm
// reject radius -- which is exactly WHY H2 needs no same-group overlap exemption. This function is how
// that measurement is ASSERTED per well at capture time instead of assumed forever: a group whose own
// members sit closer than the reject radius is refused here rather than assumed away. ns-t27-review F7:
// the sentence that used to sit here -- that such a group "could never validate its own footprint" and
// would "loop through 36 yaws x 8 nudges x 3 redeals" -- described the RETIRED rigid search, which
// replayed the captured cloud at the destination. ns-t27-corefirst never replays it: satellites are
// redrawn at >= WellSiblingMinSeparationCm from each other, so a tight vanilla well would place fine.
// The refusal is conservatism about design 4b's DERIVATION and about the backstop's vanilla-site
// reconstruction (which does still use LocalOffset), not a claim about placeability. Same correction as
// WellSelfOverlapFloorCm's comment and the WELLH2-ROLL REFUSED line.
//
// ns-review-h2 F13: measured in XY, NOT in 3-D. The placement rotates the cloud about Z and RE-SETTLES
// every member's Z on the new terrain, so the vertical separation the group had at its vanilla site
// tells us nothing about the separation it will have at the destination. Two satellites 300 cm apart
// in XY and 1500 cm apart in Z read as 1529 cm in 3-D -- comfortably past the 800 cm floor -- and then
// land on top of each other once the destination flattens them onto one slope. H0's own numbers are
// the reason this is not academic: four wells exceed 12 m of vertical spread.
inline double MinIntraGroupDistance(const TArray<FVector>& OffsetsIncludingCoreOrigin)
{
    double Best = TNumericLimits<double>::Max();
    const int32 N = OffsetsIncludingCoreOrigin.Num();
    for (int32 i = 0; i < N; ++i)
    {
        for (int32 j = i + 1; j < N; ++j)
        {
            const double D = FVector::Dist2D(OffsetsIncludingCoreOrigin[i], OffsetsIncludingCoreOrigin[j]);
            if (D < Best) { Best = D; }
        }
    }
    return (N >= 2) ? Best : 0.0;
}

// A location is usable as a rigid-body anchor only if every component is finite. H0's census header
// spells out why this matters more than it looks: NaN < Best is false, so a NaN arriving first is
// stored as a minimum and can never be displaced, and NaN < 800.0 is false, which routes a poisoned
// value straight into the PASSED branch of a verdict.
inline bool IsFiniteVector(const FVector& V)
{
    return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
}
