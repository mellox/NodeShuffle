// rwg-trace-diag-2 (2026-10-28) -- DIAGNOSTICS ONLY. NodeShuffle.Here on RWG: where the nearest unspawned active entry lies from where the
// player stands, and how the unspawned active entries spread over player-distance bands. Reads Layout, SpawnedNodes, RwgStuckTracker,
// SpawnParkedThisSession and DormantThisSession; writes nothing. Called once from LogHereCensus (NodeShuffleSubsystem.cpp), next to LogHereDensity.
// Population of both lines: layout entries with bIsNewNode && bActive whose SpawnedNodes actor is absent or invalid, so deferred, stuck and never-attempted entries
// alike. A provisional actor (rwg-farspawn-1) HAS an actor and is not in it, while HERE-DENSITY counts it as not live (fix F3): the two lines are not complements. Every distance is 2-D from the HERE pawn.
// HERE-NEAREST-UNSPAWNED legend (prose only): entry is the first 8 GUID characters, res the resource class tail, distM the 2-D distance,
//   dzM the entry's stored Z minus the pawn Z (an unspawned entry stores its deal or API Z, not a settled ground Z), worldDeltaXM /
//   worldDeltaYM the world-axis delta from the pawn in metres. failedAttemptsAtLoc is RwgStuckTracker's count for the entry (the field of
//   the same name on RWGSTUCK-ENTRY), or untracked when the tracker holds no row for it. within1000mAttemptCap is 1 when the 2-D distance is strictly below NodeShuffleMapMode::RwgAttemptRadiusCm (1000 m), the cap on the attempt radius (HERE-DENSITY's first radius); a pass attempts only within the smaller of that cap and the configured spawn radius, which PLACECENSUS prints as attemptRadiusM. The nearest pick skips cave-dealt (bRwgCave), dormant and spawn-parked entries (counted map-wide as excludedCaveDormantParkedSettledMapWide); the bands count them.
//   rwg-trace-diag-2 review round: the same count and skip also cover already-settled entries (bRayCasted), which EnsureNewNodeSpawned
//   sends straight to the spawn without a first trace. excludedSettledMapWide is the part of that count excluded only by bRayCasted (settled, no live actor, not cave/dormant/parked); RWGSTUCK-ENTRY's settled field names such entries one by one.
//   pawnYawDeg is the pawn's actor yaw the turn is computed from. The turn is Here's
//   nearest-well bearing expression (pawn yaw and the world delta, both measured), not a compass word. With no such entry within 3000 m
//   the line says so and prints the map-wide count, the excluded count and the nearest non-excluded one's distance (nan when there is none).
// HERE-UNSPAWNED-BANDS legend: the same population counted by 2-D distance band; each band includes its lower bound and excludes its
//   upper one; the six band counts sum to unspawnedActiveMapWide. provisionalUnfittedMapWide (rwg-farspawn-1e F6) counts the active new
//   entries map-wide that HAVE a valid actor still flagged provisional (placed, not yet fitted to the ground); they are not in the bands.
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
    int32 ProvUnfitted = 0; // rwg-farspawn-1e F6: provisional actors not yet fitted to the ground (counted apart, not unspawned)
    int32 Excluded = 0, ExcludedSettled = 0;
    const FNodeShuffleEntry* Nearest = nullptr;
    double NearestD2 = TNumericLimits<double>::Max();
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bIsNewNode || !E.bActive) { continue; }
        AFGResourceNode* const* Live = SpawnedNodes.Find(E.EntryGuid);
        if (Live && IsValid(*Live)) { if (E.bRwgProvisional) { ++ProvUnfitted; } continue; }
        ++MapWide;
        const double D2 = FVector::DistSquared2D(E.Location, P);
        int32 B = 0;
        while (B < NumEdges && D2 >= FMath::Square(EdgeCm[B])) { ++B; }
        ++Band[B];
        if (E.bRwgCave || DormantThisSession.Contains(E.EntryGuid) || SpawnParkedThisSession.Contains(E.EntryGuid)) { ++Excluded; continue; } // RwgSpawnPass skips dormant/parked; cave entries settle without the first trace
        if (E.bRayCasted) { ++Excluded; ++ExcludedSettled; continue; } // review round, fourth set: EnsureNewNodeSpawned skips the settle (and the first trace) for an already-settled entry
        if (D2 < NearestD2) { NearestD2 = D2; Nearest = &E; }
    }
    if (Nearest && NearestD2 < FMath::Square(RadiusCm))
    {
        const FVector D = Nearest->Location - P;
        const TPair<int32, FString>* T = RwgStuckTracker.Find(Nearest->EntryGuid);
        const FString Failed = T ? FString::FromInt(T->Key) : FString(TEXT("untracked"));
        const bool bInsideAttempt = NearestD2 < FMath::Square(static_cast<double>(NodeShuffleMapMode::RwgAttemptRadiusCm));
        // TODO(2026-10-28, rwg-trace-diag-2): diagnostic build only -- gate or remove before release.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("HERE-NEAREST-UNSPAWNED: entry=%s res=%s distM=%.0f dzM=%+.0f worldDeltaXM=%+.0f worldDeltaYM=%+.0f pawnYawDeg=%.0f failedAttemptsAtLoc=%s within1000mAttemptCap=%d excludedCaveDormantParkedSettledMapWide=%d excludedSettledMapWide=%d; from where you are standing and facing, turn %+.0f deg and go"),
            *Nearest->EntryGuid.ToString().Left(8), *Tail(Nearest->AssignedResourceClassPath), FMath::Sqrt(NearestD2) / 100.0, D.Z / 100.0, D.X / 100.0, D.Y / 100.0, Pawn->GetActorRotation().Yaw, *Failed, bInsideAttempt ? 1 : 0, Excluded, ExcludedSettled, TurnFromFacing(Nearest->Location));
    }
    else
    {
        // TODO(2026-10-28, rwg-trace-diag-2): diagnostic build only -- gate or remove before release.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("HERE-NEAREST-UNSPAWNED: none within %.0f m outside the cave/dormant/parked/settled set; unspawnedActiveMapWide=%d excludedCaveDormantParkedSettledMapWide=%d excludedSettledMapWide=%d nearestNotExcludedDistM=%.0f"),
            RadiusCm / 100.0, MapWide, Excluded, ExcludedSettled, Nearest ? FMath::Sqrt(NearestD2) / 100.0 : NodeShuffleMapMode::RwgNaN());
    }
    // TODO(2026-10-28, rwg-trace-diag-2): diagnostic build only -- gate or remove before release.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("HERE-UNSPAWNED-BANDS: band0to250m=%d band250to500m=%d band500to750m=%d band750to1000m=%d band1000to3000m=%d beyond3000m=%d unspawnedActiveMapWide=%d provisionalUnfittedMapWide=%d"),
        Band[0], Band[1], Band[2], Band[3], Band[4], Band[5], MapWide, ProvUnfitted);
}
