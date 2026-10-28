// rwg-trace-diag-2 (2026-10-28) -- DIAGNOSTICS ONLY. NodeShuffle.Here on RWG: where the nearest unspawned active entry lies from where the
// player stands, and how the unspawned active entries spread over player-distance bands. Reads Layout, SpawnedNodes, RwgStuckTracker,
// SpawnParkedThisSession and DormantThisSession; writes nothing. Called once from LogHereCensus (NodeShuffleSubsystem.cpp), next to LogHereDensity.
// Population of both lines: layout entries with bIsNewNode && bActive whose SpawnedNodes actor is absent or invalid (HERE-DENSITY's live
// test, negated), so deferred, stuck and never-attempted entries alike. Every distance is 2-D from the HERE pawn.
// HERE-NEAREST-UNSPAWNED legend (prose only): entry is the first 8 GUID characters, res the resource class tail, distM the 2-D distance,
//   dzM the entry's stored Z minus the pawn Z (an unspawned entry stores its deal or API Z, not a settled ground Z), worldDeltaXM /
//   worldDeltaYM the world-axis delta from the pawn in metres. failedAttemptsAtLoc is RwgStuckTracker's count for the entry (the field of
//   the same name on RWGSTUCK-ENTRY), or untracked when the tracker holds no row for it. insideAttemptRadius is 1 when the 2-D distance is
//   strictly below NodeShuffleMapMode::RwgAttemptRadiusCm (RwgEmitStuckCensus's test; a pass attempts within the smaller of that and the
//   spawn radius). parkedThisSession / dormantThisSession as on RWGSTUCK-ENTRY. The turn is Here's nearest-well bearing expression (pawn
//   yaw and the world delta, both measured), not a compass word. With no such entry within 3000 m the line says so and prints the
//   map-wide count and the nearest one's distance (nan when the count is 0).
// HERE-UNSPAWNED-BANDS legend: the same population counted by 2-D distance band; each band includes its lower bound and excludes its
//   upper one; the six band counts sum to unspawnedActiveMapWide.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"
#include "GameFramework/Pawn.h"
#include "Resources/FGResourceNode.h"

void ANodeShuffleSubsystem::LogHereUnspawned(const FVector& P, const APawn* Pawn) const
{
    if (!Pawn)
    {
        // TODO(2026-10-28, rwg-trace-diag-2): diagnostic build only -- gate or remove before release.
        UE_LOG(LogNodeShuffle, Display, TEXT("HERE-NEAREST-UNSPAWNED: no player pawn"));
        return;
    }
    // Here's bearing (LogHereCensus, nearest relocated well), byte-identical; check_rwgtrace_lint.ps1 pins it against that reference.
    auto TurnFromFacing = [&P, Pawn](const FVector& Target) -> double
    {
        const FVector D = Target - P;
        return FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)) - Pawn->GetActorRotation().Yaw);
    };
    auto Tail = [](const FString& Path) -> FString { int32 Dot = INDEX_NONE; return Path.FindLastChar(TEXT('.'), Dot) ? Path.Mid(Dot + 1) : Path; };
    constexpr double RadiusCm = 300000.0; // 3000 m
    constexpr int32 NumEdges = 5;
    const double EdgeCm[NumEdges] = { 25000.0, 50000.0, 75000.0, 100000.0, RadiusCm }; // band upper bounds; past the last one = beyond3000m
    int32 Band[NumEdges + 1] = {};
    int32 MapWide = 0;
    const FNodeShuffleEntry* Nearest = nullptr;
    double NearestD2 = TNumericLimits<double>::Max();
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bIsNewNode || !E.bActive) { continue; }
        AFGResourceNode* const* Live = SpawnedNodes.Find(E.EntryGuid);
        if (Live && IsValid(*Live)) { continue; }
        ++MapWide;
        const double D2 = FVector::DistSquared2D(E.Location, P);
        if (D2 < NearestD2) { NearestD2 = D2; Nearest = &E; }
        int32 B = 0;
        while (B < NumEdges && D2 >= FMath::Square(EdgeCm[B])) { ++B; }
        ++Band[B];
    }
    if (Nearest && NearestD2 < FMath::Square(RadiusCm))
    {
        const FVector D = Nearest->Location - P;
        const TPair<int32, FString>* T = RwgStuckTracker.Find(Nearest->EntryGuid);
        const FString Failed = T ? FString::FromInt(T->Key) : FString(TEXT("untracked"));
        const bool bInsideAttempt = NearestD2 < FMath::Square(static_cast<double>(NodeShuffleMapMode::RwgAttemptRadiusCm));
        // TODO(2026-10-28, rwg-trace-diag-2): diagnostic build only -- gate or remove before release.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("HERE-NEAREST-UNSPAWNED: entry=%s res=%s distM=%.0f dzM=%+.0f worldDeltaXM=%+.0f worldDeltaYM=%+.0f failedAttemptsAtLoc=%s insideAttemptRadius=%d parkedThisSession=%d dormantThisSession=%d; from where you are standing and facing, turn %+.0f deg and go"),
            *Nearest->EntryGuid.ToString().Left(8), *Tail(Nearest->AssignedResourceClassPath), FMath::Sqrt(NearestD2) / 100.0, D.Z / 100.0, D.X / 100.0, D.Y / 100.0,
            *Failed, bInsideAttempt ? 1 : 0, SpawnParkedThisSession.Contains(Nearest->EntryGuid) ? 1 : 0, DormantThisSession.Contains(Nearest->EntryGuid) ? 1 : 0,
            TurnFromFacing(Nearest->Location));
    }
    else
    {
        // TODO(2026-10-28, rwg-trace-diag-2): diagnostic build only -- gate or remove before release.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("HERE-NEAREST-UNSPAWNED: none within %.0f m; unspawnedActiveMapWide=%d nearestUnspawnedDistM=%.0f"),
            RadiusCm / 100.0, MapWide, Nearest ? FMath::Sqrt(NearestD2) / 100.0 : NodeShuffleMapMode::RwgNaN());
    }
    // TODO(2026-10-28, rwg-trace-diag-2): diagnostic build only -- gate or remove before release.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("HERE-UNSPAWNED-BANDS: band0to250m=%d band250to500m=%d band500to750m=%d band750to1000m=%d band1000to3000m=%d beyond3000m=%d unspawnedActiveMapWide=%d"),
        Band[0], Band[1], Band[2], Band[3], Band[4], Band[5], MapWide);
}
