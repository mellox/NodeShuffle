// rwg-steep-1 (2026-10-19) -- spawn-time steep hit on RWG: a LOCAL redraw ring before the map-wide API redeal.
// User decision (binding, 2026-10-19, option c with b's rock test): when the RWG spawn settle ends unplaceable and its PRIMARY probe
// was a steep TERRAIN hit (smoothed ground normal below cos 60 deg, RaycastGroundAt's cliff gate), first try up to
// RwgSteepRingCandidates spots within the ring radius of the entry -- each through the deal's land test (RwgDensityLandSample:
// Outside -> RwgRejectV3 -> RwgApiSlopeTooSteep, the SAME functions), the map-wide redeal's spacing loops, then the same terrain-only
// trace (not steep, not a solid stop, not water, a terrain hit under any skipped canopy, not below the killbox top). The first pass
// wins and moves the entry with the map-wide redeal's bookkeeping; no pass -> the existing map-wide path runs unchanged.
// rwg-clifftop-1 (2026-10-30, user decision 2): a primary that stopped on a SOLID mesh (class 4) and was NOT accepted as a gentle top
// (NodeShuffleMapModeCliffTop.cpp) now runs the same ring (outcome occludedRing, or occludedMapwide when the ring finds nothing); the
// 2026-10-19 (b) exception that sent it straight map-wide is removed. Ring radius: console variable NodeShuffle.RwgRingRadiusM (default
// 300 m; the rwg-steep-1 value was 150 m), read once per ring run and printed.
// Every function here is reached only from RWG code (RwgResolveAtSpawn, TryRedealRwgEntry, RwgSpawnPass). Log lines report measured
// values and the predicate that branched, never a cause.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

namespace
{
    // rwg-clifftop-1: the ring's outer radius is a console variable (user decision 2026-10-30: configurable, default 300 m). A console
    // variable rather than a settings-panel row: see the rwg-clifftop-1 handoff.
    TAutoConsoleVariable<int32> CVarNodeShuffleRwgRingRadiusM(
        TEXT("NodeShuffle.RwgRingRadiusM"),
        300,
        TEXT("RWG maps only. Radius in metres of the local search tried before a new node spot is redrawn anywhere on the map, when the ")
        TEXT("spot it was dealt and the nearby spots tried around it are too steep, or rock or cliff it cannot stand on, and when a node being placed ahead of players, or settled as a player approaches it, meets solid geometry in every probed direction at a spot not dealt as a cave (not used for water, nor for any other spot refused after it settled). Default 300. Values are clamped to 50..1000. Read each time the search runs."),
        ECVF_Default);
    float RwgSteepRingRadiusCmNow() { return 100.0f * static_cast<float>(FMath::Clamp(CVarNodeShuffleRwgRingRadiusM.GetValueOnGameThread(), 50, 1000)); }
    constexpr int32 RwgSteepRingCandidates = 12;       // candidate cap per ring run
    constexpr double RwgSteepGoldenAngleRad = 2.39996323; // even areal coverage, as the settle spiral
    FString RwgSteepShortPath(const FString& P) { int32 Dot = INDEX_NONE; return P.FindLastChar(TEXT('.'), Dot) ? P.Mid(Dot + 1) : P; }
    // Horizontal over vertical component of a unit normal = rise/run of the surface it describes (tan of the slope angle).
    double RwgSteepRiseRunOf(const FVector& N) { return N.Z > 1e-6 ? FMath::Sqrt(N.X * N.X + N.Y * N.Y) / N.Z : 999.0; }
}

void ANodeShuffleSubsystem::RwgSteepNoteUnplaceable(const NodeShuffleMapMode::FRwgGroundQuery& Q, const FNodeShuffleEntry& Entry)
{
    // Called by RwgResolveAtSpawn only when RaycastSettle returned false WITH the unplaceable flag (primary water/steep and the spiral
    // found nothing). RaycastSettle discards which predicate fired, so the PRIMARY probe is re-run once here, at the same XY and the
    // same window origin (Entry.Location is still (X0, Y0, ApiZ0): RaycastSettle writes it only on success), terrain-only, with the
    // normal and the hit requested. bRwgLastProbeSolid is read straight after this call, so it describes THIS probe, not a spiral one.
    bRwgSteepArmed = false;
    FVector Loc;
    FRotator Rot = Entry.Rotation;
    bool bWater = false, bSteep = false;
    FVector Normal = FVector::UpVector;
    FHitResult Hit;
    const bool bHit = RaycastGroundAt(Entry.Location, Entry.Location.Z, nullptr, nullptr, Loc, Rot, bWater, false, &bSteep, &Normal, &Hit,
                                      /*bTerrainOnly=*/true);
    if (bHit && bWater) { ++RwgSteepSessReprobeWater; return; } // the water label: map-wide redeal, unchanged, no ring
    if (!bHit || !bSteep) { ++RwgSteepSessReprobeOther; return; } // re-probe no longer steep (or no hit): map-wide, unchanged
    bRwgSteepArmed = true;
    bRwgSteepOccluded = bRwgLastProbeSolid;
    RwgSteepGuid = Entry.EntryGuid;
    RwgSteepLoc = Entry.Location;
    // The value the cliff gate tested: rise/run of the SMOOTHED normal (gate: normal Z < cos 60 deg, i.e. rise/run > 1.73). A solid
    // stop sets the steep flag without measuring a ground normal, so -1 is printed for it.
    RwgSteepNormalRiseRun = bRwgSteepOccluded ? -1.0 : RwgSteepRiseRunOf(Normal);
    RwgSteepApiRiseRun = 0.0;
    RwgApiSlopeTooSteep(Q, Entry.Location.X, Entry.Location.Y, RwgSteepApiRiseRun);
    const UStaticMeshComponent* Smc = Cast<UStaticMeshComponent>(Hit.GetComponent());
    const UStaticMesh* Mesh = Smc ? Smc->GetStaticMesh() : nullptr;
    RwgSteepHitMesh = Mesh ? Mesh->GetName() : (Hit.GetComponent() ? Hit.GetComponent()->GetName() : FString(TEXT("<none>")));
}

void ANodeShuffleSubsystem::RwgSteepArmForInside(const FNodeShuffleEntry& Entry)
{
    // rwg-farspawn-1f OPTION B (2026-10-31): the second arm site. RwgProvisionalInsideRedeal (NodeShuffleMapModeProvisionalMove.cpp) calls this
    // just before its RwgRedealLiveEntry, so an all-solid inside-gate reading tries the local ring (RwgSteepTryRing, read-and-clear, guid-checked)
    // before the map-wide draw, as an occluded settle does (rwg-clifftop-1). No re-probe: the inside gate's reading is the trigger. It arms as a
    // SOLID stop (occluded, rise/run -1), so the RWGSTEEP line reads occludedRing or occludedMapwide with hitMesh naming the inside gate, and the
    // PLACECENSUS / RWGSTEEPSUM occluded counts INCLUDE these runs (PROVCENSUS insideRedealSession / insideRedealRingSession count them apart);
    // the ring run is charged to RwgPcSteepRingRuns, i.e. against the near-player attempt budget of the pass it runs in.
    bRwgSteepArmed = true;
    bRwgSteepOccluded = true;
    RwgSteepGuid = Entry.EntryGuid;
    RwgSteepLoc = Entry.Location;
    RwgSteepNormalRiseRun = -1.0;
    RwgSteepApiRiseRun = 0.0;
    RwgSteepHitMesh = TEXT("<inside gate all-solid: no probe hit>");
}

bool ANodeShuffleSubsystem::RwgSteepTryRing(const NodeShuffleMapMode::FRwgGroundQuery& Q, FNodeShuffleEntry& Entry, float SpacingCm)
{
    // Read-and-clear: only the redeal that immediately follows the arming resolve can see it. Not armed (rock/killbox/resettle/sea
    // redeals, water label, vanilla fallback) -> false, and TryRedealRwgEntry continues byte-for-byte as at affc801.
    if (!bRwgSteepArmed) { return false; }
    bRwgSteepArmed = false;
    if (RwgSteepGuid != Entry.EntryGuid) { ++RwgSteepSessGuidMismatch; return false; }
    ++RwgSteepSessEvents;
    int32 Tried = 0, Passed = 0, RejLand = 0, RejSpacing = 0, RejTrace = 0;
    const float RadiusCm = RwgSteepRingRadiusCmNow();
    // rwg-clifftop-1: steep AND occluded entries both run the ring (the occluded -> map-wide exception is removed); the outcome splits by
    // the re-probe's solid flag and the ring result. A ring miss falls through to the map-wide draw in TryRedealRwgEntry.
    ++RwgPcSteepRingRuns; // charged against this pass's spawn-attempt budget by RwgSpawnPass
    const bool bMoved = RwgLocalRedraw(Q, Entry, SpacingCm, RadiusCm, Tried, Passed, RejLand, RejSpacing, RejTrace);
    const TCHAR* Outcome = TEXT("-");
    if (bRwgSteepOccluded)
    {
        if (bMoved) { ++RwgPcSteepOccludedRing; ++RwgSteepSessOccludedRing; Outcome = TEXT("occludedRing"); }
        else { ++RwgPcSteepOccluded; ++RwgSteepSessOccluded; Outcome = TEXT("occludedMapwide"); }
    }
    else
    {
        if (bMoved) { ++RwgPcSteepRing; ++RwgSteepSessRing; Outcome = TEXT("steepRing"); }
        else { ++RwgPcSteepMapwide; ++RwgSteepSessMapwide; Outcome = TEXT("steepMapwide"); }
    }
    RwgSteepSessRingTried += Tried;
    RwgSteepSessRingPassed += Passed;
    if (FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        const FString To = bMoved ? FString::Printf(TEXT("%.0f/%.0f/%.0f"), Entry.Location.X, Entry.Location.Y, Entry.Location.Z) : FString(TEXT("-"));
        // ringDistM: 2-D distance from the original spot (RwgSteepLoc) to the accepted candidate; a dash when the ring moved nothing.
        const FString DistM = bMoved ? FString::Printf(TEXT("%.0f"), FVector::Dist2D(RwgSteepLoc, Entry.Location) / 100.0) : FString(TEXT("-"));
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RWGSTEEP entry=%s res=%s loc=%.0f/%.0f/%.0f hitMesh=%s normalRiseRun=%.2f apiRiseRun=%.2f ringRadiusM=%.0f ringTried=%d ringPassed=%d ringRejLand=%d ringRejSpacing=%d ringRejTrace=%d ringTo=%s ringDistM=%s outcome=%s"),
            *Entry.EntryGuid.ToString().Left(8), *RwgSteepShortPath(Entry.AssignedResourceClassPath), RwgSteepLoc.X, RwgSteepLoc.Y, RwgSteepLoc.Z,
            *RwgSteepHitMesh, RwgSteepNormalRiseRun, RwgSteepApiRiseRun, RadiusCm / 100.0f, Tried, Passed, RejLand, RejSpacing, RejTrace,
            *To, *DistM, Outcome);
    }
    return bMoved;
}

bool ANodeShuffleSubsystem::RwgLocalRedraw(const NodeShuffleMapMode::FRwgGroundQuery& Q, FNodeShuffleEntry& Entry, float SpacingCm, float RadiusCm,
                                           int32& OutTried, int32& OutPassed, int32& OutRejLand, int32& OutRejSpacing, int32& OutRejTrace)
{
    // A golden-angle disk of RwgSteepRingCandidates points, radius RadiusCm * sqrt((K + 0.5) / cap) (always < the radius),
    // phase from a SEPARATE per-entry stream (never the map-wide redeal's stream: on a ring failure that path draws exactly the
    // candidates it drew before this packet). Salted by the entry's current redeal count so a retried ring samples new points.
    const FVector O = Entry.Location;
    const int32* SaltP = RedealAttempts.Find(Entry.EntryGuid);
    FRandomStream Rng(static_cast<int32>(GetTypeHash(Entry.EntryGuid)) ^ (SavedSeed * 131) ^ ((SaltP ? *SaltP : 0) * 104729) ^ 0x2C1B3C6D);
    const double Phase = Rng.FRandRange(0.f, 2.f * PI);
    const float SpacingSq = FMath::Square(SpacingCm);
    // rwg-clifftop-1: the candidate traces mirror the spawn settle, so they are armed for solid-top acceptance exactly as RwgResolveAtSpawn's
    // settle is (a gentle top the spawn would accept is not rejected here); disarmed on every return.
    RwgSolidTopArm(Entry, TEXT("ring"));
    ON_SCOPE_EXIT { bRwgSolidTopArmed = false; };
    for (int32 K = 0; K < RwgSteepRingCandidates; ++K)
    {
        ++OutTried;
        const double R = RadiusCm * FMath::Sqrt((K + 0.5) / static_cast<double>(RwgSteepRingCandidates));
        const double A = Phase + RwgSteepGoldenAngleRad * K;
        const double X = O.X + R * FMath::Cos(A), Y = O.Y + R * FMath::Sin(A);
        if (!RwgDensityLandSample(Q, X, Y)) { ++OutRejLand; continue; } // the deal's chain: Outside -> RwgRejectV3 -> RwgApiSlopeTooSteep
        const FVector C(X, Y, Q.SurfaceZ(X, Y));
        if (!FMath::IsFinite(C.Z)) { ++OutRejLand; continue; } // non-finite API surface: not a land sample
        // Spacing: the same two loops TryRedealRwgEntry runs (live layout, then the hidden originals' anchors).
        bool bTooClose = false;
        for (const FNodeShuffleEntry& Other : Layout)
        {
            if (&Other != &Entry && FVector::DistSquared2D(Other.Location, C) < SpacingSq) { bTooClose = true; break; }
        }
        if (!bTooClose) { for (const FNodeShuffleSuppressedOriginal& Rec : OriginalNodeRecord) { const FVector An = Rec.TrueLocation.IsNearlyZero() ? Rec.Location : Rec.TrueLocation; if (FVector::DistSquared2D(An, C) < SpacingSq) { bTooClose = true; break; } } }
        if (bTooClose) { ++OutRejSpacing; continue; }
        // The spawn settle's PRIMARY probe at the candidate (window origin = the API Z there, terrain-only, solid stops block).
        FVector Loc;
        FRotator Rot = Entry.Rotation;
        bool bWater = false, bSteep = false;
        if (!RaycastGroundAt(C, C.Z, nullptr, nullptr, Loc, Rot, bWater, false, &bSteep, nullptr, nullptr, /*bTerrainOnly=*/true)
            || bWater || bSteep || (bRwgKillboxKnown && Loc.Z < RwgKillboxTopZ))
        {
            ++OutRejTrace;
            continue;
        }
        // rwg-steep-2 discriminator (diagnostics, gates nothing): the same XY re-traced from the ORIGIN's API Z, the settle spiral's
        // window. RWGSTEEPSUM ringPassedFromOriginZ counts the ring passes that also pass from that window.
        { const bool bArmWas = bRwgSolidTopArmed; bRwgSolidTopArmed = false; FVector L0; FRotator R0 = Entry.Rotation; bool bW0 = false, bS0 = false; if (RaycastGroundAt(C, O.Z, nullptr, nullptr, L0, R0, bW0, false, &bS0, nullptr, nullptr, true) && !bW0 && !bS0) { ++RwgSteepSessPassedFromOriginZ; } bRwgSolidTopArmed = bArmWas; } // rwg-clifftop-1 review: the discriminator gates nothing, so it never offers a top
        ++OutPassed;
        // The map-wide redeal's move, field for field: XY + API Z, a fresh yaw, back to the surface pool, unsettled (the next spawn
        // attempt runs the full RwgResolveAtSpawn there: killbox, dZ, insideCave censuses as for any redealt entry).
        Entry.Location = C;
        Entry.Rotation = FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f);
        Entry.bUnderground = false;
        Entry.bRayCasted = false;
        return true;
    }
    return false;
}

void ANodeShuffleSubsystem::RwgSteepEmitSession()
{
    // Session totals, printed on the first pass (the zero denominator) and then on 12th passes only when they changed.
    const int32 Sig = RwgSteepSessEvents + RwgSteepSessReprobeWater + RwgSteepSessReprobeOther + RwgSteepSessGuidMismatch; if (RwgSteepSessLastPrinted == Sig) { return; } RwgSteepSessLastPrinted = Sig;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGSTEEPSUM: pass=%d steepEvents=%d ring=%d mapwide=%d occludedMapwide=%d ringCandidatesTried=%d ringCandidatesPassed=%d reprobeWater=%d reprobeNoHitOrNotSteep=%d guidMismatch=%d ringRadiusM=%.0f ringCap=%d ringPassedFromOriginZ=%d occludedRing=%d"),
        RwgPcPasses, RwgSteepSessEvents, RwgSteepSessRing, RwgSteepSessMapwide, RwgSteepSessOccluded, RwgSteepSessRingTried, RwgSteepSessRingPassed,
        RwgSteepSessReprobeWater, RwgSteepSessReprobeOther, RwgSteepSessGuidMismatch, RwgSteepRingRadiusCmNow() / 100.f, RwgSteepRingCandidates, RwgSteepSessPassedFromOriginZ,
        RwgSteepSessOccludedRing);
}
