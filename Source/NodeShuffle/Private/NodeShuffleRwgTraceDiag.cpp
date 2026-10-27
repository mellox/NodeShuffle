// rwg-trace-diag-1 (2026-10-27) -- DIAGNOSTICS ONLY; see NodeShuffleRwgTraceDiag.h for the contract. Every value printed is a count, a
// coordinate or a predicate evaluated in THIS session; no line states why a trace found nothing.
//
// RWGTRACEMISS legend (prose only, so no example can collide with a field):
//   pass, entry (first 8 GUID chars), res (resource class tail), x / y (the entry XY the decision trace ran at), apiSurfaceZ (the API Z the
//   window was built around). stuckTrackerPrior is RwgStuckTracker's count for the entry before this attempt; attemptOrdinal is that plus
//   one, which is the failedAttemptsAtLoc RWGSTUCK-ENTRY prints after this attempt when the entry stays unspawned at this XY. The line is
//   printed only when attemptOrdinal is on the schedule (RwgTraceMissPrintAt, then every RwgTraceMissPrintEvery).
//   pawnDist2DM / pawns: 2-D distance to the nearest player pawn of this pass and how many pawns the pass saw.
//   shortWindow is Entry.bUnderground (RaycastSettle's short-trace branch); settleReturned is the bool RaycastSettle returned for this settle
//   (Place.cpp bSettled, passed to the hook): a miss line with settleReturned 1 is a short-window settle the probe was never armed for. windowTopZ / windowBottomZ are the decision trace's Start / End
//   Z computed by the same expression RaycastGroundAt uses (mirrored constants below, lint-pinned against NodeShuffleSubsystem.cpp).
//   channel and traceComplex are the probe's own query (the decision trace's channel and complex flag, lint-pinned).
//   probesRun is 1 when this pass still had probe budget; probeEventsThisPass counts the probe events run so far this pass (this one included only when probesRun is 1).
//   wideTopZ / wideBottomZ: the probe window. down* is the first blocking hit of a single trace from wideTopZ to wideBottomZ at the same XY,
//   up* of a single trace from wideBottomZ to wideTopZ: actor class, actor name, component name, impact Z, and impact Z minus apiSurfaceZ in
//   metres. A trace that returned no blocking hit prints none for its names and nan for its numbers; notRun when probesRun is 0.
//   distinctHitComponents counts the distinct components among those two hits (0, 1 or 2 -- two traces, not a full column count); probeMs is
//   the wall time of those two traces (nan when probesRun is 0).
//   chunk*: actors of the class /Script/ProceduralWorld.PWTerrainChunk (the catalogue's residency route; -1 when the class does not
//   resolve): all of them, those whose all-components box lies within 500 m of the XY in 2-D, those whose box contains the XY in 2-D, and
//   those whose collision-enabled-components box (GetComponentsBoundingBox with bNonColliding false) contains it, with the Z span of the
//   UNION of those boxes; over those chunks, the registered primitive components whose collision is enabled, and of them the ones whose
//   BodyInstance is a valid body instance; chunkCensusMs is the time the census took. killboxTopZ is the session's measured killbox top (nan while unknown).
// RWGTRACECENSUS legend: attempted is PLACECENSUS attempted for this pass; firstTracesSeen counts RwgResolveAtSpawn primary settles that
//   reached the hook; firstTraceHits / firstTraceMisses split them on RwgProbeFirstClass (the spawnTraceMiss predicate); settledOnTerrain,
//   noTerrainDefer and deferredSettle are this pass's PLACECENSUS counters; firstHitKindsTop3 lists RwgHitClassOf labels of the hits as
//   label:count; absDz* are the min / lower median / max over this pass's hits of the absolute first-hit Z minus apiSurfaceZ, in metres;
//   missLines, probeEventsRun, probeEventsSkippedBudget and extraTraces count this pass's RWGTRACEMISS lines, probe events and traces.
// RWGTRACECTRL legend (positive control, at most once per pass on passes where pass mod 12 is 1, budget permitting): the same two wide
//   traces at the XY of a first trace that DID hit; firstHitKind / firstHitZ are that decision hit's RwgHitClassOf label and impact Z; down* /
//   up* as on RWGTRACEMISS; upSameComponentAsDown is 1 when both traces hit the same component. It shows what up* reads on a column known to hit.
#include "NodeShuffleRwgTraceDiag.h"
#include "NodeShuffle.h"
#include "NodeShuffleMapMode.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"

namespace
{
    // TODO(2026-10-27, rwg-trace-diag-1): diagnostic build only -- gate on FNodeShuffleModule::AreDiagnosticsEnabled() or remove before release.
    // RWGTRACEMISS schedule, per entry: printed when attemptOrdinal is one of these, then at every multiple of RwgTraceMissPrintEvery.
    constexpr int32 RwgTraceMissPrintAt[] = { 1, 12, 48 };
    constexpr int32 RwgTraceMissPrintEvery = 96;
    // TODO(2026-10-27, rwg-trace-diag-1): diagnostic build only -- gate on FNodeShuffleModule::AreDiagnosticsEnabled() or remove before release.
    // Extra-trace budget: a probe event is RwgTraceDiagTracesPerEvent traces; at most RwgTraceDiagMaxEventsPerPass events per pass, so a
    // pass spends at most 32 extra traces. An event past the budget still prints its line, with probesRun 0.
    constexpr int32 RwgTraceDiagTracesPerEvent = 2;
    constexpr int32 RwgTraceDiagMaxEventsPerPass = 16;
    static_assert(RwgTraceDiagTracesPerEvent * RwgTraceDiagMaxEventsPerPass <= 32, "rwg-trace-diag-1: at most 32 extra traces per pass");
    // Wide window: the RWGHEIGHTAPI plausibility bounds (NodeShuffleMapModeHeightApi.cpp, bRangeOk / bPawnOk: -200000 .. 360000 cm; the ceiling is 3x the .key ceiling 120000 cm, the floor lies below 3x the .key floor -60000 cm), i.e. every Z the height API is accepted to report.
    constexpr double RwgTraceDiagWideTopZ = 360000.0;
    constexpr double RwgTraceDiagWideBottomZ = -200000.0;
    // Mirror of RaycastGroundAt's window (NodeShuffleSubsystem.cpp: StartZ + 20000 / - 40000, short trace + 400 / - 800). Lint-pinned.
    constexpr double RwgDecisionUpCm = 20000.0;
    constexpr double RwgDecisionDownCm = 40000.0;
    constexpr double RwgDecisionShortUpCm = 400.0;
    constexpr double RwgDecisionShortDownCm = 800.0;
    constexpr double RwgTraceDiagChunkRadiusCm = 50000.0; // 500 m
    // RwgHitClassOf's return values (NodeShuffleMapModeResettle.cpp), in order 0..4; the last slot counts any other value.
    const TCHAR* const RwgFirstClassLabels[] = { TEXT("terrainChunk"), TEXT("canopyOrUnknownMesh"), TEXT("waterSheet"), TEXT("resourceNodeOrNonMesh"), TEXT("solidMesh"), TEXT("otherClassValue") };
    constexpr int32 RwgFirstClassSlots = UE_ARRAY_COUNT(RwgFirstClassLabels);

    struct FRwgTraceDiagPass
    {
        int32 Pass = 0;
        TArray<FVector> Pawns;
        int32 Seen = 0, Hits = 0, Misses = 0, MissLines = 0, EventsRun = 0, EventsSkipped = 0, ExtraTraces = 0;
        bool bCtrlDone = false;
        int32 ClassCount[RwgFirstClassSlots] = {};
        TArray<double> AbsDzCm;
    };
    FRwgTraceDiagPass GRwgTraceDiag;
    TWeakObjectPtr<UClass> GRwgChunkClass;

    struct FRwgDiagHit
    {
        bool bHit = false;
        FString Cls, Actor, Comp;
        double Z = 0.0;
        const UPrimitiveComponent* C = nullptr;
    };

    bool RwgTraceMissOnSchedule(int32 Ordinal)
    {
        for (const int32 At : RwgTraceMissPrintAt) { if (Ordinal == At) { return true; } }
        return Ordinal > 0 && (Ordinal % RwgTraceMissPrintEvery) == 0;
    }

    FString RwgTraceDiagTail(const FString& P) { int32 Dot = INDEX_NONE; return P.FindLastChar(TEXT('.'), Dot) ? P.Mid(Dot + 1) : P; }

    FRwgDiagHit RwgTraceDiagTrace(UWorld* World, const FCollisionQueryParams& Fresh, double X, double Y, double FromZ, double ToZ, const TCHAR* Unset)
    {
        FRwgDiagHit R;
        R.Cls = R.Actor = R.Comp = Unset;
        R.Z = NodeShuffleMapMode::RwgNaN();
        FCollisionQueryParams P = Fresh; // a copy per trace: nothing is ever added to it
        FHitResult H;
        if (!World->LineTraceSingleByChannel(H, FVector(X, Y, FromZ), FVector(X, Y, ToZ), ECC_WorldStatic, P)) { return R; }
        const AActor* A = H.GetActor();
        R.bHit = true;
        R.C = H.GetComponent();
        R.Cls = A ? A->GetClass()->GetName() : FString(TEXT("noActor"));
        R.Actor = A ? A->GetName() : FString(TEXT("noActor"));
        R.Comp = R.C ? R.C->GetName() : FString(TEXT("noComponent"));
        R.Z = H.ImpactPoint.Z;
        return R;
    }

    struct FRwgChunkCensus
    {
        int32 Resolved = -1, Actors = -1, Within = -1, ContainAny = -1, ContainColliding = -1; // -1 = census not run
        double CollMinZ = 0.0, CollMaxZ = 0.0, Ms = 0.0;
        int32 PrimsCollisionEnabled = -1, PrimsWithBody = -1;
    };

    FRwgChunkCensus RwgTraceDiagChunks(UWorld* World, double X, double Y)
    {
        FRwgChunkCensus R;
        R.CollMinZ = R.CollMaxZ = NodeShuffleMapMode::RwgNaN();
        R.Resolved = 0;
        const double T0 = FPlatformTime::Seconds();
        UClass* Cls = GRwgChunkClass.Get();
        if (!Cls) { Cls = FindObject<UClass>(nullptr, TEXT("/Script/ProceduralWorld.PWTerrainChunk")); GRwgChunkClass = Cls; }
        if (Cls && Cls->IsChildOf(AActor::StaticClass()))
        {
            R.Resolved = 1;
            R.Actors = R.Within = R.ContainAny = R.ContainColliding = 0;
            R.PrimsCollisionEnabled = R.PrimsWithBody = 0;
            const double R2 = FMath::Square(RwgTraceDiagChunkRadiusCm);
            for (TActorIterator<AActor> It(World, Cls); It; ++It)
            {
                ++R.Actors;
                const FBox B = It->GetComponentsBoundingBox(true);
                if (!B.IsValid) { continue; }
                const double Dx = FMath::Max3(B.Min.X - X, 0.0, X - B.Max.X);
                const double Dy = FMath::Max3(B.Min.Y - Y, 0.0, Y - B.Max.Y);
                if (Dx * Dx + Dy * Dy > R2) { continue; }
                ++R.Within;
                if (Dx > 0.0 || Dy > 0.0) { continue; }
                ++R.ContainAny;
                const FBox BC = It->GetComponentsBoundingBox(false);
                if (!BC.IsValid || X < BC.Min.X || X > BC.Max.X || Y < BC.Min.Y || Y > BC.Max.Y) { continue; }
                ++R.ContainColliding;
                R.CollMinZ = R.ContainColliding == 1 ? BC.Min.Z : FMath::Min(R.CollMinZ, BC.Min.Z);
                R.CollMaxZ = R.ContainColliding == 1 ? BC.Max.Z : FMath::Max(R.CollMaxZ, BC.Max.Z);
                TInlineComponentArray<UPrimitiveComponent*> Prims(*It);
                for (const UPrimitiveComponent* P : Prims)
                {
                    if (!P || !P->IsRegistered() || P->GetCollisionEnabled() == ECollisionEnabled::NoCollision) { continue; }
                    ++R.PrimsCollisionEnabled;
                    if (P->BodyInstance.IsValidBodyInstance()) { ++R.PrimsWithBody; }
                }
            }
        }
        R.Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
        return R;
    }
}

void NodeShuffleRwgTraceDiag::BeginPass(int32 Pass, const TArray<FVector>& Pawns)
{
    GRwgTraceDiag = FRwgTraceDiagPass();
    GRwgTraceDiag.Pass = Pass;
    GRwgTraceDiag.Pawns = Pawns;
}

void NodeShuffleRwgTraceDiag::NoteFirstTrace(UWorld* World, const FGuid& Guid, const FString& ResPath, double X, double Y, double ApiZ, bool bShortWindow,
                                             int32 FirstClass, double FirstZ, bool bSettleReturned, const TPair<int32, FString>* Tracker, double KillboxTopZ)
{
    FRwgTraceDiagPass& S = GRwgTraceDiag;
    ++S.Seen;
    const FCollisionQueryParams Fresh(SCENE_QUERY_STAT(NodeShuffleRwgTraceDiag), true); // RaycastGroundAt's complex flag; no ignored actors (RwgResolveAtSpawn passes none)
    if (FirstClass >= 0)
    {
        ++S.Hits;
        ++S.ClassCount[FirstClass < RwgFirstClassSlots - 1 ? FirstClass : RwgFirstClassSlots - 1];
        S.AbsDzCm.Add(FMath::Abs(FirstZ - ApiZ));
        if (World && !S.bCtrlDone && (S.Pass % 12) == 1 && S.EventsRun < RwgTraceDiagMaxEventsPerPass)
        {
            S.bCtrlDone = true; ++S.EventsRun; S.ExtraTraces += RwgTraceDiagTracesPerEvent;
            const FRwgDiagHit Down = RwgTraceDiagTrace(World, Fresh, X, Y, RwgTraceDiagWideTopZ, RwgTraceDiagWideBottomZ, TEXT("none"));
            const FRwgDiagHit Up = RwgTraceDiagTrace(World, Fresh, X, Y, RwgTraceDiagWideBottomZ, RwgTraceDiagWideTopZ, TEXT("none"));
            UE_LOG(LogNodeShuffle, Display, TEXT("RWGTRACECTRL: pass=%d entry=%s x=%.0f y=%.0f apiSurfaceZ=%.0f firstHitKind=%s firstHitZ=%.0f downHitClass=%s downHitComponent=%s downHitZ=%.0f upHitClass=%s upHitComponent=%s upHitZ=%.0f upSameComponentAsDown=%d"),
                S.Pass, *Guid.ToString().Left(8), X, Y, ApiZ, RwgFirstClassLabels[FirstClass < RwgFirstClassSlots - 1 ? FirstClass : RwgFirstClassSlots - 1], FirstZ,
                *Down.Cls, *Down.Comp, Down.Z, *Up.Cls, *Up.Comp, Up.Z, (Down.bHit && Up.bHit && Up.C == Down.C) ? 1 : 0);
        }
        return;
    }
    ++S.Misses;
    const int32 Prior = Tracker ? Tracker->Key : 0;
    const int32 Ordinal = Prior + 1;
    if (!World || !RwgTraceMissOnSchedule(Ordinal)) { return; }
    ++S.MissLines;
    double Best = TNumericLimits<double>::Max();
    for (const FVector& P : S.Pawns) { Best = FMath::Min(Best, FVector::DistSquared2D(P, FVector(X, Y, 0.0))); }
    const double PawnDistM = S.Pawns.Num() > 0 ? FMath::Sqrt(Best) / 100.0 : NodeShuffleMapMode::RwgNaN();
    const double WinTop = ApiZ + (bShortWindow ? RwgDecisionShortUpCm : RwgDecisionUpCm);
    const double WinBottom = ApiZ - (bShortWindow ? RwgDecisionShortDownCm : RwgDecisionDownCm);
    const bool bRun = S.EventsRun < RwgTraceDiagMaxEventsPerPass;
    const TCHAR* const Unset = bRun ? TEXT("none") : TEXT("notRun");
    FRwgDiagHit Down, Up;
    Down.Cls = Down.Actor = Down.Comp = Up.Cls = Up.Actor = Up.Comp = Unset;
    Down.Z = Up.Z = NodeShuffleMapMode::RwgNaN();
    double ProbeMs = NodeShuffleMapMode::RwgNaN();
    FRwgChunkCensus Ch;
    Ch.CollMinZ = Ch.CollMaxZ = NodeShuffleMapMode::RwgNaN();
    if (bRun)
    {
        ++S.EventsRun;
        S.ExtraTraces += RwgTraceDiagTracesPerEvent;
        const double TProbe = FPlatformTime::Seconds();
        Down = RwgTraceDiagTrace(World, Fresh, X, Y, RwgTraceDiagWideTopZ, RwgTraceDiagWideBottomZ, Unset);
        Up = RwgTraceDiagTrace(World, Fresh, X, Y, RwgTraceDiagWideBottomZ, RwgTraceDiagWideTopZ, Unset);
        ProbeMs = (FPlatformTime::Seconds() - TProbe) * 1000.0;
        Ch = RwgTraceDiagChunks(World, X, Y);
    }
    else
    {
        ++S.EventsSkipped;
    }
    const int32 Distinct = (Down.bHit ? 1 : 0) + ((Up.bHit && (!Down.bHit || Up.C != Down.C)) ? 1 : 0);
    const double DownDzM = Down.bHit ? (Down.Z - ApiZ) / 100.0 : NodeShuffleMapMode::RwgNaN();
    const double UpDzM = Up.bHit ? (Up.Z - ApiZ) / 100.0 : NodeShuffleMapMode::RwgNaN();
    const FString Entry8 = Guid.ToString().Left(8);
    const FString Res = RwgTraceDiagTail(ResPath);
    // TODO(2026-10-27, rwg-trace-diag-1): diagnostic build only -- gate on FNodeShuffleModule::AreDiagnosticsEnabled() or remove before release.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGTRACEMISS: pass=%d entry=%s res=%s x=%.0f y=%.0f apiSurfaceZ=%.0f stuckTrackerPrior=%d attemptOrdinal=%d pawnDist2DM=%.0f pawns=%d shortWindow=%d settleReturned=%d windowTopZ=%.0f windowBottomZ=%.0f channel=%s traceComplex=%d probesRun=%d probeEventsThisPass=%d probeMs=%.2f wideTopZ=%.0f wideBottomZ=%.0f downHitClass=%s downHitActor=%s downHitComponent=%s downHitZ=%.0f downHitMinusApiM=%.1f upHitClass=%s upHitActor=%s upHitComponent=%s upHitZ=%.0f upHitMinusApiM=%.1f distinctHitComponents=%d chunkClassResolved=%d chunkActors=%d chunkActorsBoxWithin500m=%d chunkActorsBoxContainXY=%d chunkActorsCollisionEnabledBoxContainXY=%d containingCollisionEnabledBoxUnionMinZ=%.0f containingCollisionEnabledBoxUnionMaxZ=%.0f containingPrimsCollisionEnabled=%d containingPrimsWithPhysicsBody=%d chunkCensusMs=%.2f killboxTopZ=%.0f"),
        S.Pass, *Entry8, *Res, X, Y, ApiZ, Prior, Ordinal, PawnDistM, S.Pawns.Num(), bShortWindow ? 1 : 0, bSettleReturned ? 1 : 0, WinTop, WinBottom,
        TEXT("WorldStatic"), Fresh.bTraceComplex ? 1 : 0, bRun ? 1 : 0, S.EventsRun, ProbeMs, RwgTraceDiagWideTopZ, RwgTraceDiagWideBottomZ,
        *Down.Cls, *Down.Actor, *Down.Comp, Down.Z, DownDzM, *Up.Cls, *Up.Actor, *Up.Comp, Up.Z, UpDzM, Distinct,
        Ch.Resolved, Ch.Actors, Ch.Within, Ch.ContainAny, Ch.ContainColliding, Ch.CollMinZ, Ch.CollMaxZ, Ch.PrimsCollisionEnabled, Ch.PrimsWithBody, Ch.Ms, KillboxTopZ);
}

void NodeShuffleRwgTraceDiag::EmitPassCensus(int32 Pass, int32 Attempted, int32 SettledOnTerrain, int32 NoTerrainDefer, int32 DeferredSettle)
{
    FRwgTraceDiagPass& S = GRwgTraceDiag;
    if (Attempted >= 1)
    {
        TArray<int32> Order;
        for (int32 I = 0; I < RwgFirstClassSlots; ++I) { if (S.ClassCount[I] > 0) { Order.Add(I); } }
        Order.Sort([&S](int32 A, int32 B) { return S.ClassCount[A] != S.ClassCount[B] ? S.ClassCount[A] > S.ClassCount[B] : A < B; });
        FString Top3;
        for (int32 K = 0; K < Order.Num() && K < 3; ++K) { Top3 += FString::Printf(TEXT("%s%s:%d"), K == 0 ? TEXT("") : TEXT(","), RwgFirstClassLabels[Order[K]], S.ClassCount[Order[K]]); }
        if (Top3.IsEmpty()) { Top3 = TEXT("none"); }
        S.AbsDzCm.Sort();
        const int32 N = S.AbsDzCm.Num();
        const double MinM = N > 0 ? S.AbsDzCm[0] / 100.0 : NodeShuffleMapMode::RwgNaN();
        const double MedM = N > 0 ? S.AbsDzCm[(N - 1) / 2] / 100.0 : NodeShuffleMapMode::RwgNaN();
        const double MaxM = N > 0 ? S.AbsDzCm.Last() / 100.0 : NodeShuffleMapMode::RwgNaN();
        // TODO(2026-10-27, rwg-trace-diag-1): diagnostic build only -- gate on FNodeShuffleModule::AreDiagnosticsEnabled() or remove before release.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RWGTRACECENSUS: pass=%d attempted=%d firstTracesSeen=%d firstTraceHits=%d firstTraceMisses=%d settledOnTerrain=%d noTerrainDefer=%d deferredSettle=%d firstHitKindsTop3=%s absDzMinM=%.1f absDzLowerMedianM=%.1f absDzMaxM=%.1f missLines=%d probeEventsRun=%d probeEventsSkippedBudget=%d extraTraces=%d channel=%s"),
            Pass, Attempted, S.Seen, S.Hits, S.Misses, SettledOnTerrain, NoTerrainDefer, DeferredSettle, *Top3, MinM, MedM, MaxM,
            S.MissLines, S.EventsRun, S.EventsSkipped, S.ExtraTraces, TEXT("WorldStatic"));
    }
    GRwgTraceDiag = FRwgTraceDiagPass();
}
