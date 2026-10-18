// RWG P2e (2026-10-18) -- open-sea water predicate. Brief: _team/nodeshuffle-state.md "R6 NEW GAME on p2d-2 ... the SEA is the
// defect" + the P2e packet. Measured defect (R6): 1646 of 2049 dealt spots stood on RWG's sea floor; AFGWaterVolume::EncompassesPoint
// answers false for the sea volume although its box contains the point, so the volume test never saw the sea.
// This file holds (1) the water predicate v2 every RWG API-level water decision goes through (node deal + redeal, spawn resolve,
// well core + satellites, the sea re-deal), (2) the RWGSEACHECK validation census that GRADES the bound InSeaVolume export before
// its term may count, and (3) the one-time RWGSEAREDEAL pass for existing layouts. Every function is reached only from RWG call
// sites (RwgDealOneSpot, RwgResolveAtSpawn, RwgDealWellSite, RwgSpawnPass, RwgPipelineMayRun); vanilla never enters this file.
// User rule (binding, 2026-10-18): a point is water only if a water surface exists above it -- no rule here keys on the sign of Z.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"
#include "NodeShuffleWellRelocate.h"   // WellLayoutSeedFor
#include "NodeShuffleWellRetype.h"     // WellShort
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Resources/FGResourceNodeBase.h"
#include "Resources/FGResourceNode.h"

int32 ANodeShuffleSubsystem::RwgWetV2(const NodeShuffleMapMode::FRwgGroundQuery& Q, const FVector& P)
{
    // P.Z is RWG's SurfaceHeightCm at P's XY (every caller passes the API surface). Bits: 1 = InSeaVolume(X, Y, SurfaceZ), counted only
    // while this session's RWGSEACHECK grade is usable; 2 = the lake/river volume test (EncompassesPoint, bounds-prefiltered);
    // 4 = inland WaterLevelCmAt finite and above the surface by RwgWaterApiMarginCm. 0 = dry. IsFinite guards the call: a NaN Z
    // would fall through InSeaVolume's depth compare into its grid lookup.
    int32 Wet = 0;
    if (RwgEnsureSeaCheck(Q) && FMath::IsFinite(P.Z) && Q.InSea(P.X, P.Y, P.Z)) { Wet |= 1; }
    if (RwgIsPointInWaterCached(P)) { Wet |= 2; }
    const double Wl = Q.WaterLevelZ(P.X, P.Y);
    if (FMath::IsFinite(Wl) && Wl > P.Z + NodeShuffleMapMode::RwgWaterApiMarginCm) { Wet |= 4; }
    return Wet;
}

bool ANodeShuffleSubsystem::RwgEnsureSeaCheck(const NodeShuffleMapMode::FRwgGroundQuery& Q)
{
    if (bRwgSeaCheckDone) { return bRwgSeaUsable; }
    if (!Q.HasSea())
    {
        bRwgSeaCheckDone = true;
        bRwgSeaUsable = false;
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGSEACHECK: not run; seaBind=%s queryUsable=%d; the sea term is off this session and the volume + inland-level tests decide"),
            *NodeShuffleMapMode::RwgSeaBindStatus(), Q.IsUsable() ? 1 : 0);
        return false;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now < RwgSeaCheckRetryAt) { return false; }
    RwgSeaCheckRetryAt = Now + 5.0;
    UWorld* World = GetWorld();
    APawn* Pawn = World ? NodeShuffleMapMode::FirstPlayerPawn(World) : nullptr;
    if (!Pawn)
    {
        if (!bRwgSeaCheckDeferLogged)
        {
            bRwgSeaCheckDeferLogged = true;
            UE_LOG(LogNodeShuffle, Display, TEXT("RWGSEACHECK: deferred; pawnFound=0; retried every 5 s, the sea term is off until it grades"));
        }
        return false;
    }
    if (RwgWaterVolumes.Num() == 0) { RwgBuildWaterVolumeCache(); }
    const double T0 = FPlatformTime::Seconds();
    // Draw phase: random XY inside the world, recording the raw readings. Nothing here is a water rule; the classes below are the
    // census populations the brief names.
    struct FDraw { double X, Y, Z, Wl; bool bVol; };
    TArray<FDraw> Draws;
    Draws.Reserve(NodeShuffleMapMode::RwgSeaCheckMaxDraws);
    FRandomStream Rng(0x53454131); // fixed: the census is reproducible on a given world
    const double H = Q.HalfExtent();
    int32 LakeSeen = 0;
    for (int32 I = 0; I < NodeShuffleMapMode::RwgSeaCheckMaxDraws; ++I)
    {
        const double X = Rng.FRandRange(-H, H), Y = Rng.FRandRange(-H, H);
        if (Q.Outside(X, Y)) { continue; }
        const double Z = Q.SurfaceZ(X, Y);
        const bool bVol = RwgIsPointInWaterCached(FVector(X, Y, Z));
        Draws.Add(FDraw{ X, Y, Z, Q.WaterLevelZ(X, Y), bVol });
        if (bVol && Z >= 0.0) { ++LakeSeen; }
        if (LakeSeen >= NodeShuffleMapMode::RwgSeaCheckPerClass && Draws.Num() >= 2048) { break; }
    }
    // Open-sea floor: GlobalMinSurfaceCm when bound; if fewer than 8 draws sit on it (trenches may go deeper), the most frequent
    // exact Z among this run's draws with no inland level (the measured plateau) -- the line prints which source was used.
    const double GMin = Q.GlobalMinZ();
    TMap<int64, int32> ZCount;
    int32 AtGMin = 0;
    for (const FDraw& D : Draws)
    {
        if (FMath::IsFinite(D.Wl)) { continue; }
        ZCount.FindOrAdd(FMath::RoundToInt64(D.Z))++;
        if (FMath::IsFinite(GMin) && FMath::Abs(D.Z - GMin) <= 1.0) { ++AtGMin; }
    }
    int64 ModeZ = 0; int32 ModeN = 0;
    for (const TPair<int64, int32>& KV : ZCount) { if (KV.Value > ModeN) { ModeN = KV.Value; ModeZ = KV.Key; } }
    const bool bUseGMin = AtGMin >= 8;
    const double FloorZ = bUseGMin ? GMin : static_cast<double>(ModeZ);
    const bool bHaveFloor = bUseGMin || ModeN >= 8;
    const TCHAR* FloorSource = bUseGMin ? TEXT("globalMinSurfaceCm") : (bHaveFloor ? TEXT("modeOfDraws") : TEXT("none"));
    // Class (a) lakes, (b) open sea, then (c) RWG's own nodes and (d) the pawn. InSeaVolume is called with Z = the API surface.
    int32 LakeN = 0, LakeT = 0, SeaN = 0, SeaT = 0, NodeN = 0, NodeT = 0, ZProbeN = 0, ZSurfT = 0, Z0T = 0;
    FString ZProbe;
    for (const FDraw& D : Draws)
    {
        if (D.bVol && D.Z >= 0.0 && LakeN < NodeShuffleMapMode::RwgSeaCheckPerClass)
        {
            ++LakeN; LakeT += Q.InSea(D.X, D.Y, D.Z) ? 1 : 0;
        }
        else if (bHaveFloor && !FMath::IsFinite(D.Wl) && FMath::Abs(D.Z - FloorZ) <= 1.0 && SeaN < NodeShuffleMapMode::RwgSeaCheckPerClass)
        {
            ++SeaN;
            const bool bAtSurf = Q.InSea(D.X, D.Y, D.Z);
            SeaT += bAtSurf ? 1 : 0;
            if (ZProbeN < 8)
            {
                // Z argument probe: the same XY at Z = SurfaceZ and at Z = 0, so the next reader knows which Z the function wants.
                const bool bAt0 = Q.InSea(D.X, D.Y, 0.0);
                ++ZProbeN; ZSurfT += bAtSurf ? 1 : 0; Z0T += bAt0 ? 1 : 0;
                ZProbe += FString::Printf(TEXT("%s%.0f,%.0f,%.0f:%d/%d"), ZProbe.IsEmpty() ? TEXT("") : TEXT("; "), D.X, D.Y, D.Z, bAtSurf ? 1 : 0, bAt0 ? 1 : 0);
            }
        }
    }
    TArray<FVector> RwgNodes;
    for (TActorIterator<AFGResourceNodeBase> It(World); It; ++It)
    {
        if (IsValid(*It) && !NodeShuffleMapMode::IsOursForRwg(*It)) { RwgNodes.Add(It->GetActorLocation()); }
    }
    const int32 Stride = FMath::Max(1, RwgNodes.Num() / NodeShuffleMapMode::RwgSeaCheckPerClass);
    for (int32 I = 0; I < RwgNodes.Num() && NodeN < NodeShuffleMapMode::RwgSeaCheckPerClass; I += Stride)
    {
        const FVector& L = RwgNodes[I];
        if (Q.Outside(L.X, L.Y)) { continue; }
        ++NodeN; NodeT += Q.InSea(L.X, L.Y, Q.SurfaceZ(L.X, L.Y)) ? 1 : 0;
    }
    const FVector PP = Pawn->GetActorLocation();
    const double PawnZ = Q.SurfaceZ(PP.X, PP.Y);
    const bool bPawnSea = !Q.Outside(PP.X, PP.Y) && FMath::IsFinite(PawnZ) && Q.InSea(PP.X, PP.Y, PawnZ);
    // Grade: open sea >= 95 % true AND RWG nodes >= 95 % false, each class needing >= 8 samples. The pawn is printed, not graded
    // (RWG P2e review R8: one sample latched for the whole session).
    const bool bSeaOk = SeaN >= 8 && SeaT * 100 >= SeaN * 95;
    const bool bNodeOk = NodeN >= 8 && (NodeN - NodeT) * 100 >= NodeN * 95;
    bRwgSeaUsable = bSeaOk && bNodeOk; // pawnInSea stays printed (measured only)
    bRwgSeaCheckDone = true;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGSEACHECK: graded usable=%d seaBind=%s draws=%d lakeN=%d lakeTrue=%d lakeFalse=%d openSeaN=%d openSeaTrue=%d openSeaFalse=%d rwgNodeN=%d rwgNodeTrue=%d rwgNodeFalse=%d rwgNodesFound=%d pawnInSea=%d pawn=(%.0f,%.0f) pawnSurfaceZ=%.0f openSeaPass=%d rwgNodePass=%d floorSource=%s floorZ=%.1f globalMinZ=%.1f drawsAtGlobalMin=%d modeZ=%lld modeCount=%d durationMs=%.0f"),
        bRwgSeaUsable ? 1 : 0, *NodeShuffleMapMode::RwgSeaBindStatus(), Draws.Num(), LakeN, LakeT, LakeN - LakeT, SeaN, SeaT, SeaN - SeaT,
        NodeN, NodeT, NodeN - NodeT, RwgNodes.Num(), bPawnSea ? 1 : 0, PP.X, PP.Y, PawnZ, bSeaOk ? 1 : 0, bNodeOk ? 1 : 0, FloorSource, FloorZ,
        GMin, AtGMin, static_cast<long long>(ModeZ), ModeN, (FPlatformTime::Seconds() - T0) * 1000.0);
    if (!bRwgSeaUsable) { UE_LOG(LogNodeShuffle, Warning, TEXT("RWGSEACHECK-FAIL: openSeaPass=%d rwgNodePass=%d pawnInSea=%d; the sea term is off this session"), bSeaOk ? 1 : 0, bNodeOk ? 1 : 0, bPawnSea ? 1 : 0); }
    UE_LOG(LogNodeShuffle, Display, TEXT("RWGSEACHECK zprobe: n=%d trueWithSurfaceZ=%d trueWithZ0=%d; per point x,y,surfaceZ then the two answers [%s]"),
        ZProbeN, ZSurfT, Z0T, *ZProbe);
    return bRwgSeaUsable;
}

bool ANodeShuffleSubsystem::RwgCountSeaEntries(int32& OutSea, int32& OutWet, int32& OutWellSea, int32& OutWellWet)
{
    // RWGLAYOUT on load: active new-node entries and dealt well cores whose spot is in the sea (raw InSeaVolume at the API surface,
    // a measurement printed regardless of the grade) and wet under v2 (which counts the sea term only when graded usable).
    OutSea = OutWet = OutWellSea = OutWellWet = 0;
    const NodeShuffleMapMode::FRwgGroundQuery Q(GetWorld(), RwgGenerator.Get());
    if (!Q.IsUsable()) { return false; }
    if (RwgWaterVolumes.Num() == 0) { RwgBuildWaterVolumeCache(); }
    const auto Measure = [&](double X, double Y, int32& Sea, int32& Wet)
    {
        if (Q.Outside(X, Y)) { return; }
        const double Z = Q.SurfaceZ(X, Y);
        Sea += (Q.HasSea() && FMath::IsFinite(Z) && Q.InSea(X, Y, Z)) ? 1 : 0;
        Wet += RwgWetV2(Q, FVector(X, Y, Z)) != 0 ? 1 : 0;
    };
    for (const FNodeShuffleEntry& E : Layout) { if (E.bIsNewNode && E.bActive) { Measure(E.Location.X, E.Location.Y, OutSea, OutWet); } }
    for (const FNodeShuffleWellEntry& W : WellLayout)
    {
        if (!W.bDestDealt) { continue; }
        const FVector& C = W.bGroupPlaced ? W.PlacedCoreLocation : W.DestCoreLocation;
        Measure(C.X, C.Y, OutWellSea, OutWellWet);
    }
    return true;
}

void ANodeShuffleSubsystem::RwgSeaRedealPass()
{
    // One-time sea re-deal for existing RWG layouts (idempotent via the SaveGame flag bRwgSeaChecked on each entry). Population:
    // active new-node entries and dealt well cores without the flag; at most RwgSeaRedealPerPass checked per pass. A placed
    // (live actor / bGroupPlaced), pinned, cave or failed-well entry is flagged and never moved. A dry spot is flagged. A wet spot
    // (v2) gets a fresh deal: nodes through TryRedealWaterLockedEntry (the API dealer, salted per entry), well cores through
    // RwgDealWellSite on a salted stream, keeping bDestDealt (the Escalate precedent); ApplyWellRelocation places it later.
    bool bAny = false;
    for (const FNodeShuffleEntry& E : Layout) { if (E.bIsNewNode && E.bActive && !E.bRwgSeaChecked && !RwgSeaDealFailed.Contains(E.EntryGuid)) { bAny = true; break; } }
    if (!bAny) { for (const FNodeShuffleWellEntry& W : WellLayout) { if (W.bDestDealt && !W.bRwgSeaChecked && !RwgSeaWellDealFailed.Contains(W.CorePath)) { bAny = true; break; } } }
    if (!bAny) { return; }
    const NodeShuffleMapMode::FRwgGroundQuery Q(GetWorld(), RwgGenerator.Get());
    if (!Q.IsUsable() || !RwgEnsureSeaCheck(Q))
    {
        if (!bRwgSeaPassSkipLogged && (bRwgSeaCheckDone || !Q.IsUsable()))
        {
            bRwgSeaPassSkipLogged = true;
            UE_LOG(LogNodeShuffle, Display, TEXT("RWGSEAREDEAL: skipped; queryUsable=%d seaUsable=%d seaBind=%s; no entry is flagged, so a session with a usable grade re-deals them"),
                Q.IsUsable() ? 1 : 0, bRwgSeaUsable ? 1 : 0, *NodeShuffleMapMode::RwgSeaBindStatus());
        }
        return;
    }
    if (RwgWaterVolumes.Num() == 0) { RwgBuildWaterVolumeCache(); }
    ++RwgSeaPasses;
    const double T0 = FPlatformTime::Seconds();
    int32 Left = NodeShuffleMapMode::RwgSeaRedealPerPass;
    int32 Checked = 0, Sea = 0, Redealt = 0, Kept = 0, DealFailed = 0, InSea = 0, SkipPlaced = 0, SkipPinned = 0, SkipFailedWell = 0, Outside = 0;
    int32 WellsChecked = 0, WellsRedealt = 0;
    int32 PlacedWet = 0;
    const bool bDiag = FNodeShuffleModule::AreDiagnosticsEnabled();
    for (int32 I = 0; I < Layout.Num() && Left > 0; ++I)
    {
        FNodeShuffleEntry& E = Layout[I];
        if (!E.bIsNewNode || !E.bActive || E.bRwgSeaChecked || RwgSeaDealFailed.Contains(E.EntryGuid)) { continue; }
        --Left; ++Checked;
        // Placed = a live actor, OR settled at any time (bRayCasted / bRwgSettledOnTerrain are SaveGame; SpawnedNodes may not have
        // re-adopted a far actor yet this session, and an unadopted actor could carry a miner). A sea spot never settles (no terrain).
        if (IsValid(SpawnedNodes.FindRef(E.EntryGuid)) || E.bRayCasted || E.bRwgSettledOnTerrain) { ++SkipPlaced; PlacedWet += (!Q.Outside(E.Location.X, E.Location.Y) && RwgWetV2(Q, FVector(E.Location.X, E.Location.Y, Q.SurfaceZ(E.Location.X, E.Location.Y))) != 0) ? 1 : 0; E.bRwgSeaChecked = true; continue; }
        if (E.bPinned || E.bUnderground) { ++SkipPinned; E.bRwgSeaChecked = true; continue; }
        if (Q.Outside(E.Location.X, E.Location.Y)) { ++Outside; E.bRwgSeaChecked = true; continue; }
        const FVector At(E.Location.X, E.Location.Y, Q.SurfaceZ(E.Location.X, E.Location.Y));
        const int32 Wet = RwgWetV2(Q, At);
        if (Wet == 0) { ++Kept; E.bRwgSeaChecked = true; continue; }
        ++Sea; InSea += (Wet & 1) != 0 ? 1 : 0;
        const FVector From = E.Location;
        E.bRwgSettledOnTerrain = false;
        E.bRwgRockChecked = false;
        const bool bMoved = TryRedealWaterLockedEntry(E);
        if (bMoved) { ++Redealt; E.bRwgSeaChecked = true; } else { ++DealFailed; RwgSeaDealFailed.Add(E.EntryGuid); }
        if (bDiag)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("RWGSEAREDEAL-ENTRY: entry=%s from=%s to=%s wetBits=%d moved=%d"),
                *E.EntryGuid.ToString(), *From.ToCompactString(), *E.Location.ToCompactString(), Wet, bMoved ? 1 : 0);
        }
    }
    for (int32 I = 0; I < WellLayout.Num() && Left > 0; ++I)
    {
        FNodeShuffleWellEntry& W = WellLayout[I];
        if (!W.bDestDealt || W.bRwgSeaChecked || RwgSeaWellDealFailed.Contains(W.CorePath)) { continue; }
        --Left; ++Checked; ++WellsChecked;
        if (W.bGroupPlaced) { ++SkipPlaced; W.bRwgSeaChecked = true; continue; }
        if (W.bPinned) { ++SkipPinned; W.bRwgSeaChecked = true; continue; }
        if (W.bRelocationFailed) { ++SkipFailedWell; W.bRwgSeaChecked = true; continue; } // terminal: left at its vanilla site (open decision)
        const double X = W.DestCoreLocation.X, Y = W.DestCoreLocation.Y;
        if (Q.Outside(X, Y)) { ++Outside; W.bRwgSeaChecked = true; continue; }
        const int32 Wet = RwgWetV2(Q, FVector(X, Y, Q.SurfaceZ(X, Y)));
        if (Wet == 0) { ++Kept; W.bRwgSeaChecked = true; continue; }
        ++Sea; InSea += (Wet & 1) != 0 ? 1 : 0;
        TArray<FVector> Offs;
        for (const FNodeShuffleWellSatellite& S : W.Satellites) { if (S.bCaptured) { Offs.Add(S.LocalOffset); } }
        const int32 Salt = ++RwgSeaWellSalt.FindOrAdd(W.CorePath);
        FRandomStream Rng(WellLayoutSeedFor(SavedSeed, W.CorePath, W.GroupRedeals, 0) ^ 0x53454131 ^ (Salt * 7919));
        const FVector From = W.DestCoreLocation;
        FVector Site(FVector::ZeroVector);
        bool bDealt = false;
        for (int32 D = 0; D < NodeShuffleMapMode::RwgSeaWellDealDraws && !bDealt; ++D) { bDealt = RwgDealWellSite(Q, Rng, Offs, Site); }
        if (bDealt)
        {
            W.DestCoreLocation = Site;
            W.YawCursor = 0;
            W.GroupNudges = 0;
            W.bRwgSeaChecked = true;
            ++Redealt; ++WellsRedealt;
        }
        else { ++DealFailed; RwgSeaWellDealFailed.Add(W.CorePath); }
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGSEAREDEAL-WELL: core='%s' from=%s dest=%s wetBits=%d dealtNewSite=%d"),
            *WellShort(W.CorePath), *From.ToCompactString(), *W.DestCoreLocation.ToCompactString(), Wet, bDealt ? 1 : 0);
    }
    int32 Remaining = 0;
    for (const FNodeShuffleEntry& E : Layout) { Remaining += (E.bIsNewNode && E.bActive && !E.bRwgSeaChecked && !RwgSeaDealFailed.Contains(E.EntryGuid)) ? 1 : 0; }
    for (const FNodeShuffleWellEntry& W : WellLayout) { Remaining += (W.bDestDealt && !W.bRwgSeaChecked && !RwgSeaWellDealFailed.Contains(W.CorePath)) ? 1 : 0; }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGSEAREDEAL: pass=%d checked=%d sea=%d redealt=%d kept=%d dealFailed=%d inSeaVolume=%d skippedPlaced=%d skippedPinnedOrCave=%d skippedFailedWell=%d outsideWorld=%d wellsChecked=%d wellsRedealt=%d remaining=%d failedThisSession=%d placedWet=%d perPassCap=%d durationMs=%.0f"),
        RwgSeaPasses, Checked, Sea, Redealt, Kept, DealFailed, InSea, SkipPlaced, SkipPinned, SkipFailedWell, Outside, WellsChecked, WellsRedealt,
        Remaining, RwgSeaDealFailed.Num() + RwgSeaWellDealFailed.Num(), PlacedWet, NodeShuffleMapMode::RwgSeaRedealPerPass, (FPlatformTime::Seconds() - T0) * 1000.0);
}
