// RWG P2f (2026-10-18) -- void + killbox predicate. Brief: the P2f packet; research _team/test-logs/2026-10-18-rwg-two-seas-research.md §3-§5.
// MEASURED (R7 new game on p2e-2): 24 % of uniform draws land in RWG's VOID -- a flat plateau where SurfaceHeightCm reports -3000
// (FPWBaseMap::SeabedM) and InSeaVolume answers false away from the sea; entries dealt there settle at killbox top - 5000, inside RWG's
// killbox (two AFGDamageOverTimeVolume actors, top Z -22463 in that world -- per rwg-export-catalog.md that value is a hard CEILING inside
// FPWDensityField::KillboxTopAboveSeaCm, min(derived, -22463): another seed can report a deeper top, never a higher one, so it is never
// used here as a constant; the measured damage-volume top is the runtime value). This file holds (1) predicate v3 = v2 OR the graded VoidAt
// term OR the killbox floor (bBelowKillbox: a playability rule, not a water rule), (2) RWGKILLBOX, the once-per-session killbox top read
// from the damage volumes AND from GetKillboxTop (both printed; the volume value wins when they disagree), (3) the RWGVOIDCHECK census
// that GRADES VoidAt before its term may count, and (4) RESETTLE-KILLBOX, which tears down placed entries whose actor stands below the
// killbox top and re-deals them. Every function is reached only from RWG call sites (RwgDealOneSpot, RwgResolveAtSpawn, RwgDealWellSite,
// RwgSeaRedealPass, RwgSpawnPass); vanilla never enters this file.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "UObject/UnrealType.h"
#include "Resources/FGResourceNodeBase.h"
#include "Resources/FGResourceNode.h"
#include <limits>

namespace
{
    // RWG P2f: the dealer's void term -- VoidAt at P and at +-RwgVoidRingCm on X and on Y (RWG NearVoid's 5-point cross, catalogue-measured).
    bool RwgVoidCross(const NodeShuffleMapMode::FRwgGroundQuery& Q, double X, double Y)
    {
        const double R = NodeShuffleMapMode::RwgVoidRingCm;
        return Q.VoidAt(X, Y) || Q.VoidAt(X + R, Y) || Q.VoidAt(X - R, Y) || Q.VoidAt(X, Y + R) || Q.VoidAt(X, Y - R);
    }
}

int32 ANodeShuffleSubsystem::RwgRejectV3(const NodeShuffleMapMode::FRwgGroundQuery& Q, const FVector& P)
{
    // P.Z is RWG's SurfaceHeightCm at P's XY (every caller passes the API surface). Bits 1/2/4 = water predicate v2, unchanged; 8 = VoidAt on the
    // 5-point cross (P and +-RwgVoidRingCm on X / Y), counted only while this session's RWGVOIDCHECK grade is usable; 16 = bBelowKillbox: the API surface lies below the measured killbox top
    // plus RwgKillboxMarginCm (only while the top is known). 0 = dealable.
    int32 Bits = RwgWetV2(Q, P);
    RwgEnsureKillboxTop(&Q);
    if (RwgEnsureVoidCheck(Q) && RwgVoidCross(Q, P.X, P.Y)) { Bits |= 8; }
    const bool bBelowKillbox = bRwgKillboxKnown && FMath::IsFinite(P.Z) && P.Z < RwgKillboxTopZ + NodeShuffleMapMode::RwgKillboxMarginCm;
    if (bBelowKillbox) { Bits |= 16; }
    return Bits;
}

void ANodeShuffleSubsystem::RwgEnsureKillboxTop(const NodeShuffleMapMode::FRwgGroundQuery* Q)
{
    if (bRwgKillboxDone) { return; }
    const double Now = FPlatformTime::Seconds();
    if (Now < RwgKillboxRetryAt) { return; }
    RwgKillboxRetryAt = Now + 5.0;
    UWorld* World = GetWorld();
    if (!World) { return; }
    ++RwgKillboxTries;
    // Route 1 (engine level: what the player feels). RWG reuses the vanilla AFGDamageOverTimeVolume (FactoryGame/Public/FGDamageOverTimeVolume.h:11,
    // ": public AVolume") and repositions two of them under the void. Each one's damage comes from UFGDotComponent mDotComponent (same header,
    // :42-43), whose TSubclassOf<UFGDamageOverTime> mDotClass (FGDotComponent.h:45-46) names it (BP_DoTWorldBottom_C / BP_DoTWorldPerimeter_C in
    // the R7 log). Matched by class NAME and read by reflection, so nothing here adds an import. The top is the BOTTOM volume's bounds max Z; a
    // perimeter volume is printed and used only when no bottom volume exists. Plausibility: the top must lie below the lowest API surface
    // (GlobalMinSurfaceCm; 0 when that optional export was dropped) -- a top at or above it would reject real terrain.
    const double GMin = Q ? Q->GlobalMinZ() : std::numeric_limits<double>::quiet_NaN();
    const double CeilZ = FMath::IsFinite(GMin) ? GMin : 0.0;
    const auto Plausible = [CeilZ](double Z) { return FMath::IsFinite(Z) && Z < CeilZ && Z > -2000000.0; };
    static const FName DotVolumeClass(TEXT("FGDamageOverTimeVolume"));
    int32 DotN = 0, BottomN = 0, PerimN = 0;
    double BottomTop = TNumericLimits<double>::Max(), PerimTop = TNumericLimits<double>::Max();
    FString List;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* A = *It;
        if (!IsValid(A)) { continue; }
        bool bDot = false;
        for (const UClass* C = A->GetClass(); C && !bDot; C = C->GetSuperClass()) { bDot = C->GetFName() == DotVolumeClass; }
        if (!bDot) { continue; }
        ++DotN;
        FString DotClass = TEXT("<unread>");
        if (const FObjectPropertyBase* CP = CastField<FObjectPropertyBase>(A->GetClass()->FindPropertyByName(TEXT("mDotComponent"))))
        {
            if (const UObject* Comp = CP->GetObjectPropertyValue_InContainer(A))
            {
                if (const FObjectPropertyBase* DP = CastField<FObjectPropertyBase>(Comp->GetClass()->FindPropertyByName(TEXT("mDotClass"))))
                {
                    const UObject* DC = DP->GetObjectPropertyValue_InContainer(Comp);
                    DotClass = DC ? DC->GetName() : FString(TEXT("<none>"));
                }
            }
        }
        FVector O, E;
        A->GetActorBounds(false, O, E);
        const double MaxZ = O.Z + E.Z;
        const FString Cls = A->GetClass()->GetName();
        const bool bBottom = DotClass.Contains(TEXT("Bottom")) || Cls.Contains(TEXT("Bottom"));
        const bool bPerim = !bBottom && (DotClass.Contains(TEXT("Perimeter")) || Cls.Contains(TEXT("Perimeter")));
        if (bBottom) { ++BottomN; BottomTop = FMath::Min(BottomTop, MaxZ); }
        if (bPerim) { ++PerimN; PerimTop = FMath::Min(PerimTop, MaxZ); }
        if (DotN <= 8)
        {
            List += FString::Printf(TEXT("%s%s class=%s dot=%s role=%s maxZ=%.0f minZ=%.0f extentKm=%.1fx%.1f"), List.IsEmpty() ? TEXT("") : TEXT("; "),
                *A->GetName(), *Cls, *DotClass, bBottom ? TEXT("bottom") : (bPerim ? TEXT("perimeter") : TEXT("other")), MaxZ, O.Z - E.Z,
                E.X * 2.0 / 100000.0, E.Y * 2.0 / 100000.0);
        }
    }
    const TCHAR* ActorFrom = TEXT("none");
    if (BottomN > 0 && Plausible(BottomTop)) { RwgKillboxTopActorZ = BottomTop; bRwgKillboxActorKnown = true; ActorFrom = TEXT("bottomVolume"); }
    else if (PerimN > 0 && Plausible(PerimTop)) { RwgKillboxTopActorZ = PerimTop; bRwgKillboxActorKnown = true; ActorFrom = TEXT("perimeterVolume"); }
    // Route 3: the bound export (optional), compared in the census; never preferred over the volumes.
    double ApiZ = std::numeric_limits<double>::quiet_NaN();
    const bool bApiReturned = Q && Q->KillboxTopApi(ApiZ);
    if (bApiReturned && Plausible(ApiZ)) { RwgKillboxTopApiZ = ApiZ; bRwgKillboxApiKnown = true; }
    const bool bAgree = bRwgKillboxActorKnown && bRwgKillboxApiKnown && FMath::Abs(RwgKillboxTopActorZ - RwgKillboxTopApiZ) <= 100.0;
    const TCHAR* Source = TEXT("none");
    if (bRwgKillboxActorKnown) { RwgKillboxTopZ = RwgKillboxTopActorZ; bRwgKillboxKnown = true; Source = ActorFrom; }
    else if (bRwgKillboxApiKnown) { RwgKillboxTopZ = RwgKillboxTopApiZ; bRwgKillboxKnown = true; Source = TEXT("getKillboxTop"); }
    if (!bRwgKillboxKnown && RwgKillboxTries < 12)
    {
        if (RwgKillboxTries == 1)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("RWGKILLBOX: deferred; dotVolumes=%d bottomVolumes=%d perimeterVolumes=%d apiReturned=%d apiBind=%s; retried every 5 s (12 tries), the killbox floor is off until measured"),
                DotN, BottomN, PerimN, bApiReturned ? 1 : 0, *NodeShuffleMapMode::RwgKillboxBindStatus());
        }
        return;
    }
    bRwgKillboxDone = true;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGKILLBOX: known=%d killboxTopZ=%.0f source=%s killboxTopActorZ=%.0f actorKnown=%d killboxTopApiZ=%.0f apiKnown=%d apiReturned=%d apiBind=%s agree=%d plausibleBelowZ=%.0f dotVolumes=%d bottomVolumes=%d perimeterVolumes=%d tries=%d [%s]"),
        bRwgKillboxKnown ? 1 : 0, bRwgKillboxKnown ? RwgKillboxTopZ : std::numeric_limits<double>::quiet_NaN(), Source,
        bRwgKillboxActorKnown ? RwgKillboxTopActorZ : (BottomN > 0 ? BottomTop : std::numeric_limits<double>::quiet_NaN()),
        bRwgKillboxActorKnown ? 1 : 0, ApiZ, bRwgKillboxApiKnown ? 1 : 0, bApiReturned ? 1 : 0, *NodeShuffleMapMode::RwgKillboxBindStatus(),
        bAgree ? 1 : 0, CeilZ, DotN, BottomN, PerimN, RwgKillboxTries, *List);
    if (bRwgKillboxActorKnown && bRwgKillboxApiKnown && !bAgree)
    {
        UE_LOG(LogNodeShuffle, Warning, TEXT("RWGKILLBOX-DISAGREE: the volume top and GetKillboxTop differ by more than 100 cm; the volume top is used"));
    }
    if (!bRwgKillboxKnown) { UE_LOG(LogNodeShuffle, Warning, TEXT("RWGKILLBOX-OFF: no plausible killbox top after %d tries; the killbox floor and RESETTLE-KILLBOX are off this session"), RwgKillboxTries); }
}

bool ANodeShuffleSubsystem::RwgEnsureVoidCheck(const NodeShuffleMapMode::FRwgGroundQuery& Q)
{
    if (bRwgVoidCheckDone) { return bRwgVoidUsable; }
    if (!Q.HasVoid())
    {
        bRwgVoidCheckDone = true;
        bRwgVoidUsable = false;
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGVOIDCHECK: not run; voidBind=%s queryUsable=%d; the void term is off this session (the killbox floor still applies when measured)"),
            *NodeShuffleMapMode::RwgVoidBindStatus(), Q.IsUsable() ? 1 : 0);
        return false;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now < RwgVoidCheckRetryAt) { return false; }
    RwgVoidCheckRetryAt = Now + 5.0;
    UWorld* World = GetWorld();
    if (!World) { return false; }
    TArray<FVector> RwgNodes;
    for (TActorIterator<AFGResourceNodeBase> It(World); It; ++It)
    {
        if (IsValid(*It) && !NodeShuffleMapMode::IsOursForRwg(*It)) { RwgNodes.Add(It->GetActorLocation()); }
    }
    if (RwgNodes.Num() < 8)
    {
        if (!bRwgVoidCheckDeferLogged)
        {
            bRwgVoidCheckDeferLogged = true;
            UE_LOG(LogNodeShuffle, Display, TEXT("RWGVOIDCHECK: deferred; rwgNodesFound=%d (< 8); retried every 5 s, the void term is off until it grades"), RwgNodes.Num());
        }
        return false;
    }
    const double T0 = FPlatformTime::Seconds();
    // Classes (brief P2f item 2): (a) draws whose API surface sits on the void plateau (RwgVoidPlateauCm +- 1 cm) with raw InSeaVolume false
    // -> expect VoidAt true; (b) RWG's own node positions -> expect false; (c) draws with API surface above 0 -> expect false; (d) the pawn,
    // printed only. Same fixed seed as RWGSEACHECK, so the two censuses draw the same points on a given world.
    FRandomStream Rng(0x53454131);
    const double H = Q.HalfExtent();
    const int32 PerClass = NodeShuffleMapMode::RwgVoidCheckPerClass;
    int32 Draws = 0, VoidTrueDraws = 0, AN = 0, AT = 0, CN = 0, CT = 0;
    TMap<int64, int32> ZCount;
    for (int32 I = 0; I < NodeShuffleMapMode::RwgSeaCheckMaxDraws; ++I)
    {
        const double X = Rng.FRandRange(-H, H), Y = Rng.FRandRange(-H, H);
        if (Q.Outside(X, Y)) { continue; }
        ++Draws;
        const double Z = Q.SurfaceZ(X, Y);
        const bool bVoid = Q.VoidAt(X, Y);
        const bool bSea = FMath::IsFinite(Z) && Q.InSea(X, Y, Z); // raw: the grade gates the TERM, not this measurement
        VoidTrueDraws += bVoid ? 1 : 0;
        if (!FMath::IsFinite(Q.WaterLevelZ(X, Y)) && FMath::IsFinite(Z)) { ZCount.FindOrAdd(FMath::RoundToInt64(Z))++; }
        if (FMath::Abs(Z - NodeShuffleMapMode::RwgVoidPlateauCm) <= 1.0 && !bSea && AN < PerClass) { ++AN; AT += bVoid ? 1 : 0; }
        else if (Z > 0.0 && CN < PerClass) { ++CN; CT += bVoid ? 1 : 0; }
        if (AN >= PerClass && CN >= PerClass && Draws >= 2048) { break; }
    }
    int64 M1Z = 0, M2Z = 0; int32 M1N = 0, M2N = 0;
    for (const TPair<int64, int32>& KV : ZCount)
    {
        if (KV.Value > M1N) { M2Z = M1Z; M2N = M1N; M1Z = KV.Key; M1N = KV.Value; }
        else if (KV.Value > M2N) { M2Z = KV.Key; M2N = KV.Value; }
    }
    int32 BN = 0, BT = 0;
    const int32 Stride = FMath::Max(1, RwgNodes.Num() / PerClass);
    for (int32 I = 0; I < RwgNodes.Num() && BN < PerClass; I += Stride)
    {
        const FVector& L = RwgNodes[I];
        if (Q.Outside(L.X, L.Y)) { continue; }
        ++BN; BT += Q.VoidAt(L.X, L.Y) ? 1 : 0;
    }
    APawn* Pawn = NodeShuffleMapMode::FirstPlayerPawn(World);
    const FVector PP = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
    const double PawnSurfaceZ = Pawn ? Q.SurfaceZ(PP.X, PP.Y) : 0.0;
    const bool bPawnVoid = Pawn && Q.VoidAt(PP.X, PP.Y);
    const bool bPawnSea = Pawn && !Q.Outside(PP.X, PP.Y) && FMath::IsFinite(PawnSurfaceZ) && Q.InSea(PP.X, PP.Y, PawnSurfaceZ);
    // Grade: (a) >= 95 % true (n >= 8) AND (b)+(c) pooled >= 95 % false (n >= 8). The pawn is printed, not graded.
    const bool bVoidOk = AN >= 8 && AT * 100 >= AN * 95;
    const int32 DryN = BN + CN, DryFalse = (BN - BT) + (CN - CT);
    const bool bDryOk = DryN >= 8 && DryFalse * 100 >= DryN * 95;
    bRwgVoidUsable = bVoidOk && bDryOk;
    bRwgVoidCheckDone = true;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGVOIDCHECK: graded usable=%d voidBind=%s draws=%d voidTrueDraws=%d plateauZ=%.0f plateauN=%d plateauTrue=%d rwgNodeN=%d rwgNodeFalse=%d landN=%d landFalse=%d voidPass=%d dryPass=%d rwgNodesFound=%d pawnFound=%d pawn=(%.0f,%.0f,%.0f) pawnSurfaceZ=%.0f pawnVoid=%d pawnInSea=%d topModeZ=%lld topModeCount=%d secondModeZ=%lld secondModeCount=%d killboxKnown=%d killboxTopZ=%.0f durationMs=%.0f"),
        bRwgVoidUsable ? 1 : 0, *NodeShuffleMapMode::RwgVoidBindStatus(), Draws, VoidTrueDraws, NodeShuffleMapMode::RwgVoidPlateauCm, AN, AT, BN, BN - BT, CN, CN - CT,
        bVoidOk ? 1 : 0, bDryOk ? 1 : 0, RwgNodes.Num(), Pawn ? 1 : 0, PP.X, PP.Y, PP.Z, PawnSurfaceZ, bPawnVoid ? 1 : 0, bPawnSea ? 1 : 0,
        static_cast<long long>(M1Z), M1N, static_cast<long long>(M2Z), M2N, bRwgKillboxKnown ? 1 : 0,
        bRwgKillboxKnown ? RwgKillboxTopZ : std::numeric_limits<double>::quiet_NaN(), (FPlatformTime::Seconds() - T0) * 1000.0);
    if (!bRwgVoidUsable)
    {
        UE_LOG(LogNodeShuffle, Warning, TEXT("RWGVOIDCHECK-FAIL: voidPass=%d dryPass=%d plateauN=%d; the void term is off this session"), bVoidOk ? 1 : 0, bDryOk ? 1 : 0, AN);
    }
    return bRwgVoidUsable;
}

void ANodeShuffleSubsystem::RwgKillboxResettlePass()
{
    // Placed entries whose LIVE actor stands below the killbox top are unreachable (inside RWG's damage volume): the same teardown as the
    // rock pass (RwgRedealLiveEntry: Mk1 snap list, scan reveal, representation, Destroy, unregister, slot removal, then the API re-deal,
    // which goes through predicate v3), and bRwgVoidChecked is cleared so the re-deal pass re-checks the new spot. Settled entries with no
    // live actor and placed well cores below the top are COUNTED only (not acted on; handoff UNEXAMINED).
    if (!bRwgKillboxDone)
    {
        const NodeShuffleMapMode::FRwgGroundQuery Q(GetWorld(), RwgGenerator.Get());
        RwgEnsureKillboxTop(Q.IsUsable() ? &Q : nullptr);
    }
    if (!bRwgKillboxKnown) { return; }
    const double Top = RwgKillboxTopZ;
    ++RwgKbPasses;
    int32 Left = NodeShuffleMapMode::RwgKillboxResettlePerPass;
    int32 Live = 0, Below = 0, Redealt = 0, Refused = 0, RefusedEarlier = 0, Capped = 0, NoActorBelow = 0, WellCoresBelow = 0;
    for (FNodeShuffleEntry& E : Layout)
    {
        if (!E.bIsNewNode || !E.bActive) { continue; }
        AFGResourceNode* Node = SpawnedNodes.FindRef(E.EntryGuid);
        if (!IsValid(Node))
        {
            NoActorBelow += ((E.bRwgSettledOnTerrain || E.bRayCasted) && E.Location.Z < Top) ? 1 : 0;
            continue;
        }
        ++Live;
        const FVector From = Node->GetActorLocation();
        if (From.Z >= Top) { continue; }
        ++Below;
        if (RwgKillboxRefused.Contains(E.EntryGuid)) { ++RefusedEarlier; continue; }
        if (Left <= 0) { ++Capped; continue; }
        --Left;
        if (RwgRedealLiveEntry(E, TEXT("actor below the killbox top")))
        {
            ++Redealt;
            E.bRwgVoidChecked = false; // flag for re-deal: the re-deal pass re-checks the new spot under v3
            UE_LOG(LogNodeShuffle, Display, TEXT("RESETTLE-KILLBOX-ENTRY: entry=%s actorAt=%s to=%s killboxTopZ=%.0f"),
                *E.EntryGuid.ToString(), *From.ToCompactString(), *E.Location.ToCompactString(), Top);
        }
        else { ++Refused; RwgKillboxRefused.Add(E.EntryGuid); }
    }
    for (const FNodeShuffleWellEntry& W : WellLayout) { WellCoresBelow += (W.bGroupPlaced && W.PlacedCoreLocation.Z < Top) ? 1 : 0; }
    if (Below > 0 || RwgKbPasses == 1 || (RwgKbPasses % 60) == 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RESETTLE-KILLBOX: pass=%d killboxTopZ=%.0f liveChecked=%d below=%d redealt=%d refusedBuiltOn=%d refusedEarlier=%d cappedThisPass=%d settledNoActorBelow=%d placedWellCoresBelow=%d settleRefusedSession=%d perPassCap=%d"),
            RwgKbPasses, Top, Live, Below, Redealt, Refused, RefusedEarlier, Capped, NoActorBelow, WellCoresBelow, RwgKbSettleRefused, NodeShuffleMapMode::RwgKillboxResettlePerPass);
    }
}
