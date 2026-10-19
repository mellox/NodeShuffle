// rwg-rockcache-1: ON AN RWG MAP, PAIR EACH SHUFFLED ORIGINAL WITH ITS ROCK SO THE ROCK HIDES IN THE SAME PASS
// AS THE NODE.
//
// MEASURED BEFORE THIS FILE (_team/test-logs/2026-10-19-nodeshuffle-r14-vanishing-nodes.md, FOLLOW-UP Q1/Q2):
// on RWG an original BP_ResourceNode_C has no own mesh component and an unset mMeshActor; its rock is a
// SEPARATE NodeMeshActor_C (IsA AFGNodeMeshActor) within 8 m holding one StaticMeshComponent and one
// BoxComponent, not instanced (RWGROCK2 census, NodeShuffleMapModeR1.cpp). RebuildMeshActorCache pairs only by
// engine link (mNodeActor back-link / mMeshActor forward link), both unset here, and it was gated to Vanilla
// anyway -- so MeshActorCache stayed EMPTY on RWG, the node-hide loop's paired-rock hide never fired, the
// T64 stream-in route never ran, and in all 11 RWG session logs the stray-rock backstop (300 m from a pawn,
// 30 s cadence) was the only route that hid a rock.
//
// WHAT THIS FILE DOES. It fills the SAME MeshActorCache, with the SAME key/value types, by 2-D proximity
// instead of by engine link, so every existing reader works unchanged:
//   * SuppressOriginalNodes' node-hide (FindMeshActorForNode -> hide the paired actor, same statement);
//   * the T64 stream-in route (TryHideStreamedRockForSteadyOriginal, called after each Add, as at the two
//     Add sites in RebuildMeshActorCache);
//   * RestoreOriginalsForReroll (un-hides the paired actor before a re-roll).
// It runs every ApplyLayout pass, exactly the cadence of RebuildMeshActorCache on Vanilla, so a rock that is
// resident later than the roll is paired on the pass it first becomes visible to the iterator.
//
// THE PAIRING PREDICATE (one rock per original, global nearest-wins, never a rock already claimed):
//   ORIGINALS = OriginalNodeRecord paths that resolve through FindOriginalBaseByPath (the same lookup the hide
//     loop uses; VanillaNodeCache already excludes our own copies), minus fracking cores/satellites, geysers,
//     deposits and anything IsOursForRwg -- deduplicated by actor. Every OTHER original in VanillaNodeCache (a kept,
//     unhidden neighbour) enters the same nearest-wins contest as a COMPETITOR, so a shuffled original can never claim
//     the rock of a node beside it that stays visible; competitors are never added to the cache.
//   ROCKS = AFGNodeMeshActor, not transient, owning EXACTLY ONE non-instanced StaticMeshComponent with a mesh
//     whose name passes IsNodeRockMeshName (the backstop's own rock-name test, no extra patterns).
//   WELL EXCLUSION (NodeMeshActor_C also carries fracking geometry; the fracking index has 642 members):
//     a rock is never claimed when (1) its mNodeMeshType is not MT_Node, (2) its mNodeActor back-link
//     resolves to a fracking actor, (3) its mesh name contains "Frack" (NodeShuffleWellMeshIndex.cpp's
//     piece-name test), or (4) a fracking actor is at least as close to it (2-D) as the original being
//     paired -- the contest rule, so a satellite's rock 7 m from an ordinary original stays the well's.
//   A rock whose mNodeActor back-link names a DIFFERENT live node is never paired to this original.
//   CANDIDATE = (original, rock) with DistSquared2D(original actor, rock's StaticMeshComponent) < 8 m squared,
//     the backstop's own test and constant (NodeShuffleRock::RockOwnRangeCm). All candidates are sorted by
//     distance and assigned greedily: an original takes its nearest UNCLAIMED rock; a claimed rock is never
//     re-assigned.
//
// WHAT THIS FILE WILL NOT DO. It never calls SetNodeActor (Vanilla's forward sweep repairs the engine
// back-link; a proximity guess must not write engine state other systems read), never hides a node actor,
// never hides a rock whose node is not hidden (TryHide's gate 1), and never touches a rock it did not pair.
// CaptureOriginalVisualIfNeeded skips these pairings as a capture source (see there) so capture on RWG is
// exactly what it was while this cache was empty.
//
// DIAGNOSTICS. RWGROCKCACHE (always-on Display): printed on the first ApplyLayout pass after every roll and
// every load (bDidInitialApply is false on exactly that pass), and re-printed when the paired count changes.
// ROCKPAIR / ROCKPAIR-UNPAIRED (EnableDiagnostics only, that first pass only): the first 20 pairs and every
// ambiguous or contended original, capped. Every count names what was counted, never why.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffleRwgRockCache.h"

#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceNodeBase.h"
#include "Resources/FGResourceDeposit.h"

namespace
{
    constexpr int32 RwgRockPairFirstLines = 20;   // ROCKPAIR: the first N pairs of a roll/load
    constexpr int32 RwgRockPairContendedCap = 200; // ROCKPAIR / ROCKPAIR-UNPAIRED: ambiguous/contended lines per roll/load

    FIntPoint RwgRockCell(const FVector& L)
    {
        return FIntPoint(FMath::FloorToInt(L.X / NodeShuffleRock::RockOwnRangeCm),
                         FMath::FloorToInt(L.Y / NodeShuffleRock::RockOwnRangeCm));
    }
}

void ANodeShuffleSubsystem::RebuildRwgRockCache()
{
    const double StartSeconds = FPlatformTime::Seconds();
    const bool bFirstPassAfterRollOrLoad = !bDidInitialApply;

    // Same per-pass reset RebuildMeshActorCache performs: the cache is a snapshot of THIS pass, and the T64
    // counters ROCKHIDE-CENSUS reads later in this pass describe THIS rebuild.
    MeshActorCache.Reset();
    RockCacheAddPairsSeenThisPass = 0;
    RockCacheAddNodeNotHiddenThisPass = 0;
    RockCacheAddNoSteadyRecordThisPass = 0;
    RockCacheAddAlreadyDarkThisPass = 0;
    RocksHiddenAtCacheAddThisPass = 0;
    RocksCaptureFirstThisPass = 0;
    RocksCapturePendingAtHideThisPass = 0;
    MeshCacheBaseOnlySeenThisPass = 0;   // not measured on RWG; zeroed so ROCKHIDE-CENSUS cannot print a stale value
    MeshCacheBaseOnlyPairedThisPass = 0;
    RockHiddenAtCacheAddPathsThisPass.Reset();

    UWorld* World = GetWorld();
    if (!World) { return; }
    const double RangeSq = FMath::Square(static_cast<double>(NodeShuffleRock::RockOwnRangeCm));

    // ---- 1. ROCKS ----
    struct FRwgRock { AFGNodeMeshActor* Actor; FVector Loc; const AFGResourceNodeBase* Back; double NearestFrackSq; FString Mesh; };
    TArray<FRwgRock> Rocks;
    TMap<FIntPoint, TArray<int32>> RockGrid;
    int32 MeshActorsIterated = 0, WellExcluded = 0, ShapeRejected = 0;
    for (TActorIterator<AFGNodeMeshActor> It(World); It; ++It)
    {
        AFGNodeMeshActor* MA = *It;
        if (!IsValid(MA) || MA->HasAnyFlags(RF_Transient)) { continue; }
        ++MeshActorsIterated;
        const AFGResourceNodeBase* Back = MA->mNodeActor.Get();
        // WELL EXCLUSION (1)+(2): engine-authored well geometry type, or a back-link into a fracking actor.
        if (MA->mNodeMeshType != ENodeMeshType::MT_Node || IsFrackingActor(Back)) { ++WellExcluded; continue; }
        TInlineComponentArray<UStaticMeshComponent*> Smcs(MA);
        int32 MeshCount = 0;
        bool bInstanced = false;
        UStaticMeshComponent* Only = nullptr;
        for (UStaticMeshComponent* C : Smcs)
        {
            if (!IsValid(C) || !C->GetStaticMesh()) { continue; }
            ++MeshCount;
            Only = C;
            if (Cast<UInstancedStaticMeshComponent>(C)) { bInstanced = true; }
        }
        if (MeshCount != 1 || bInstanced || !Only) { ++ShapeRejected; continue; }
        const FString MeshName = Only->GetStaticMesh()->GetName();
        // WELL EXCLUSION (3): a well piece by name.
        if (MeshName.Contains(TEXT("Frack"))) { ++WellExcluded; continue; }
        if (!IsNodeRockMeshName(MeshName, TArray<FString>())) { ++ShapeRejected; continue; }
        const int32 Idx = Rocks.Add({ MA, Only->GetComponentLocation(), Back, TNumericLimits<double>::Max(), MeshName });
        RockGrid.FindOrAdd(RwgRockCell(Rocks[Idx].Loc)).Add(Idx);
    }
    // `resident` on the census: rock-shaped actors the iterator found THIS pass (premise-4 measurement).
    const int32 Resident = Rocks.Num();

    // ---- 2. WELL EXCLUSION (4): nearest fracking actor per rock, within range (the contest rule) ----
    for (TActorIterator<AFGResourceNodeBase> It(World); It; ++It)
    {
        if (!IsValid(*It) || !IsFrackingActor(*It)) { continue; }
        const FVector F = It->GetActorLocation();
        const FIntPoint C = RwgRockCell(F);
        for (int32 Dx = -1; Dx <= 1; ++Dx)
        {
            for (int32 Dy = -1; Dy <= 1; ++Dy)
            {
                const TArray<int32>* Bucket = RockGrid.Find(FIntPoint(C.X + Dx, C.Y + Dy));
                if (!Bucket) { continue; }
                for (const int32 R : *Bucket)
                {
                    const double D = FVector::DistSquared2D(F, Rocks[R].Loc);
                    if (D < Rocks[R].NearestFrackSq) { Rocks[R].NearestFrackSq = D; }
                }
            }
        }
    }

    // ---- 3. ORIGINALS and CANDIDATES ----
    struct FRwgCand { int32 Orig; int32 Rock; double DistSq; };
    TArray<AFGResourceNodeBase*> Origs;
    TArray<int32> CandCount;
    TArray<double> NearestSq, SecondSq;
    TArray<bool> HadContested;
    TArray<FRwgCand> Cands;
    TSet<const AFGResourceNodeBase*> SeenOrig;
    int32 PathUnresolved = 0, Ineligible = 0, LinkedElsewhereSkips = 0;
    // Recorded originals first (the population the hide loop consumes and the census counts), then every other
    // original in VanillaNodeCache as a COMPETITOR: a kept neighbour takes part in nearest-wins so a shuffled original
    // can never claim the rock of an unhidden node beside it. Competitors are never added to the cache.
    TArray<AFGResourceNodeBase*> Claimants;
    for (const FNodeShuffleSuppressedOriginal& Rec : OriginalNodeRecord)
    {
        AFGResourceNodeBase* Node = FindOriginalBaseByPath(Rec.VanillaNodePath);
        if (!Node) { ++PathUnresolved; continue; }
        Claimants.Add(Node);
    }
    const int32 RecordedClaimants = Claimants.Num();
    for (const TPair<FString, TWeakObjectPtr<AFGResourceNodeBase>>& KV : VanillaNodeCache)
    {
        if (AFGResourceNodeBase* Node = KV.Value.Get()) { Claimants.Add(Node); }
    }
    int32 RecordedOrigs = 0;
    for (int32 Ci = 0; Ci < Claimants.Num(); ++Ci)
    {
        AFGResourceNodeBase* Node = Claimants[Ci];
        const bool bRecorded = Ci < RecordedClaimants;
        if (SeenOrig.Contains(Node)) { if (bRecorded) { ++Ineligible; } continue; } // a duplicate record, or a recorded node met again
        if (IsFrackingActor(Node) || Node->GetResourceNodeType() == EResourceNodeType::Geyser
            || Node->IsA<AFGResourceDeposit>() || NodeShuffleMapMode::IsOursForRwg(Node))
        {
            if (bRecorded) { ++Ineligible; }
            continue;
        }
        SeenOrig.Add(Node);
        const int32 O = Origs.Add(Node);
        if (bRecorded) { RecordedOrigs = Origs.Num(); }
        CandCount.Add(0);
        NearestSq.Add(-1.0);
        SecondSq.Add(-1.0);
        HadContested.Add(false);
        const FVector OLoc = Node->GetActorLocation();
        const FIntPoint C = RwgRockCell(OLoc);
        for (int32 Dx = -1; Dx <= 1; ++Dx)
        {
            for (int32 Dy = -1; Dy <= 1; ++Dy)
            {
                const TArray<int32>* Bucket = RockGrid.Find(FIntPoint(C.X + Dx, C.Y + Dy));
                if (!Bucket) { continue; }
                for (const int32 R : *Bucket)
                {
                    const double D = FVector::DistSquared2D(OLoc, Rocks[R].Loc);
                    if (D >= RangeSq) { continue; }
                    if (Rocks[R].Back && Rocks[R].Back != Node) { ++LinkedElsewhereSkips; continue; }
                    if (Rocks[R].NearestFrackSq <= D) { HadContested[O] = true; continue; } // WELL EXCLUSION (4)
                    Cands.Add({ O, R, D });
                    ++CandCount[O];
                    if (NearestSq[O] < 0.0 || D < NearestSq[O]) { SecondSq[O] = NearestSq[O]; NearestSq[O] = D; }
                    else if (SecondSq[O] < 0.0 || D < SecondSq[O]) { SecondSq[O] = D; }
                }
            }
        }
    }

    // ---- 4. GLOBAL NEAREST-WINS ASSIGNMENT ----
    Cands.Sort([](const FRwgCand& A, const FRwgCand& B)
    {
        if (A.DistSq != B.DistSq) { return A.DistSq < B.DistSq; }
        return A.Orig != B.Orig ? A.Orig < B.Orig : A.Rock < B.Rock; // deterministic tie-break
    });
    TArray<int32> OrigRock;
    OrigRock.Init(INDEX_NONE, Origs.Num());
    TArray<double> OrigDistSq;
    OrigDistSq.Init(0.0, Origs.Num());
    TArray<bool> RockClaimed;
    RockClaimed.Init(false, Rocks.Num());
    for (const FRwgCand& Cd : Cands)
    {
        if (OrigRock[Cd.Orig] != INDEX_NONE) { continue; } // one rock per original
        if (RockClaimed[Cd.Rock]) { continue; }            // never a rock already claimed
        OrigRock[Cd.Orig] = Cd.Rock;
        OrigDistSq[Cd.Orig] = Cd.DistSq;
        RockClaimed[Cd.Rock] = true;
    }

    // ---- 5. FILL THE CACHE (Add, then the T64 stream-in hide -- the same order as RebuildMeshActorCache) ----
    const bool bDiag = bFirstPassAfterRollOrLoad && FNodeShuffleModule::AreDiagnosticsEnabled();
    int32 Paired = 0, NoRock = 0, NoRockWellContested = 0, AlreadyClaimed = 0, Ambiguous = 0;
    int32 FirstLines = 0, ContendedLines = 0, ContendedSuppressed = 0;
    int32 CompetitorClaims = 0;
    for (int32 O = RecordedOrigs; O < Origs.Num(); ++O) { if (OrigRock[O] != INDEX_NONE) { ++CompetitorClaims; } }
    for (int32 O = 0; O < RecordedOrigs; ++O)
    {
        AFGResourceNodeBase* Node = Origs[O];
        const int32 R = OrigRock[O];
        if (R != INDEX_NONE)
        {
            AFGNodeMeshActor* MA = Rocks[R].Actor;
            MeshActorCache.Add(Node, MA);
            TryHideStreamedRockForSteadyOriginal(Node, MA);
            ++Paired;
            const bool bAmbiguous = CandCount[O] >= 2;
            if (bAmbiguous) { ++Ambiguous; }
            if (bDiag)
            {
                const bool bFirst = FirstLines < RwgRockPairFirstLines;
                const bool bContendedRoom = ContendedLines < RwgRockPairContendedCap;
                if (bFirst || (bAmbiguous && bContendedRoom))
                {
                    if (bFirst) { ++FirstLines; } else { ++ContendedLines; }
                    UE_LOG(LogNodeShuffle, Display,
                        TEXT("ROCKPAIR node=%s rock=%s dist2dM=%.1f candidates=%d secondDist2dM=%.1f mesh=%s"),
                        *Node->GetName(), *MA->GetName(), FMath::Sqrt(OrigDistSq[O]) / 100.0, CandCount[O],
                        SecondSq[O] < 0.0 ? -1.0 : FMath::Sqrt(SecondSq[O]) / 100.0, *Rocks[R].Mesh);
                }
                else if (bAmbiguous) { ++ContendedSuppressed; }
            }
        }
        else if (CandCount[O] == 0)
        {
            ++NoRock;
            if (HadContested[O]) { ++NoRockWellContested; }
        }
        else
        {
            ++AlreadyClaimed;
            if (bDiag)
            {
                if (ContendedLines < RwgRockPairContendedCap)
                {
                    ++ContendedLines;
                    UE_LOG(LogNodeShuffle, Display,
                        TEXT("ROCKPAIR-UNPAIRED node=%s reason=alreadyClaimed candidates=%d nearestDist2dM=%.1f"),
                        *Node->GetName(), CandCount[O], FMath::Sqrt(NearestSq[O]) / 100.0);
                }
                else { ++ContendedSuppressed; }
            }
        }
    }
    if (bDiag && ContendedSuppressed > 0)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("ROCKPAIR-CAP lines=%d suppressed=%d"), ContendedLines, ContendedSuppressed);
    }

    const double ElapsedMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
    if (bFirstPassAfterRollOrLoad || Paired != RwgRockCacheLastPaired)
    {
        RwgRockCacheLastPaired = Paired;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RWGROCKCACHE originals=%d paired=%d unpairedNoRockWithin8m=%d ambiguousMultiRock=%d alreadyClaimed=%d resident=%d ms=%.1f")
            TEXT(" records=%d pathUnresolved=%d ineligible=%d competitors=%d competitorClaims=%d meshActorsIterated=%d wellExcluded=%d shapeRejected=%d noRockWellContested=%d linkedElsewhereSkips=%d firstPassAfterRollOrLoad=%d"),
            RecordedOrigs, Paired, NoRock, Ambiguous, AlreadyClaimed, Resident, ElapsedMs,
            OriginalNodeRecord.Num(), PathUnresolved, Ineligible, Origs.Num() - RecordedOrigs, CompetitorClaims,
            MeshActorsIterated, WellExcluded, ShapeRejected,
            NoRockWellContested, LinkedElsewhereSkips, bFirstPassAfterRollOrLoad ? 1 : 0);
    }
}
