// RWG P5 (2026-10-18) -- cave spots on RWG through RWG's own carved-cave queries (FPWCarvedCaves).
// User decision (binding): caves on RWG follow the VANILLA rule -- natural share capped at 25 %, random locations, vanilla untouched.
// P5-2 user decisions (2026-10-18, binding): the share's numerator is cave CAPACITY (JSON rooms), and cave candidates come from the
// per-world cave FILE (NodeShuffleMapModeCaveJson.cpp); random XY + AnyNear is the fallback when the file is absent or does not match.
// Every function here is reached only from RWG call sites (GenerateRwgLocations, TryRedealRwgEntry, RwgResolveAtSpawn in
// NodeShuffleMapModePlace.cpp), so the vanilla path never enters this file. Log lines report measured counts and the predicate
// that branched, never a cause. Catalogue: _team/rwg-export-catalog.md section B4.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"
#include "NodeShuffleConfig.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "Resources/FGResourceNodeBase.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
    // x64 MSVC member ABI, DISASSEMBLED on RWG 0.1.7 build 705 (dumpbin /DISASM, 2026-10-18; the HeightApi version gate is the precondition):
    //  CarvedCaves: `lea rax,[rcx+28h]; ret`            -> this RCX (density field), returns the embedded FPWCarvedCaves*.
    //  Num:         `mov eax,[rcx+8]; ret`              -> this RCX (caves).
    //  AnyNear:     this RCX, X XMM1, Y XMM2, R XMM3; grid cell lookup, false outside the grid / on an empty grid.
    //  FloorCmAt:   this RCX, X XMM1, Y XMM2, ZHint XMM3, double* Out on the STACK (5th slot, [rsp+28h] at entry). Per cave whose XY box
    //               covers the point, per room and tube covering XY, it computes the floor Z and keeps the one with the smallest
    //               |floor - ZHint| (andps abs mask at 14B201 / 14B3A6); writes *Out only when one was found; returns found.
    //  IsInsideAt:  this RCX, const FVector* RDX; `DistanceCm(P, DBL_MAX) < 0`.
    using FnCaves = const void* (*)(const void* Field);
    using FnNum = int32 (*)(const void* Caves);
    using FnAnyNear = bool (*)(const void* Caves, double X, double Y, double R);
    using FnFloor = bool (*)(const void* Caves, double X, double Y, double ZHint, double* OutFloor);
    using FnInside = bool (*)(const void* Caves, const FVector* P);
    const char* const SymCaves = "?CarvedCaves@FPWDensityField@@QEBAAEBVFPWCarvedCaves@@XZ";
    const char* const SymNum = "?Num@FPWCarvedCaves@@QEBAHXZ";
    const char* const SymAnyNear = "?AnyNear@FPWCarvedCaves@@QEBA_NNNN@Z";
    const char* const SymFloor = "?FloorCmAt@FPWCarvedCaves@@QEBA_NNNNAEAN@Z";
    const char* const SymInside = "?IsInsideAt@FPWCarvedCaves@@QEBA_NAEBU?$TVector@N@Math@UE@@@Z";

    struct FRwgCaveApi
    {
        bool bTried = false;
        FString Status = TEXT("notTried");
        FnCaves Caves = nullptr;
        FnNum Num = nullptr;
        FnAnyNear AnyNear = nullptr;
        FnFloor Floor = nullptr;
        FnInside Inside = nullptr;
        bool Bound() const { return Caves && Num && AnyNear && Floor && Inside; }
    };
    FRwgCaveApi& CaveApi() { static FRwgCaveApi A; return A; } // per process: the RWG DLL never unloads

    void TryBindCaves(FRwgCaveApi& A)
    {
        A.bTried = true;
#if PLATFORM_WINDOWS
        HMODULE H = static_cast<HMODULE>(NodeShuffleMapMode::RwgHeightApiBoundModule());
        if (!H) { A.Status = TEXT("heightApiNotBound"); return; } // version / required exports / required signatures did not pass
        // Leading bytes of the build-705 disassembly; -1 = wildcard (IsInsideAt's movsd RIP displacement and call rel32 only).
        // CarvedCaves pins the +0x28 field offset and Num the +8 count offset in full: a moved field fails closed.
        static const int16 SigCaves[] = { 0x48,0x8D,0x41,0x28,0xC3 };
        static const int16 SigNum[] = { 0x8B,0x41,0x08,0xC3 };
        static const int16 SigAnyNear[] = { 0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x57,0x48,0x83,0xEC,0x70,0x8B,0x59,0x48,0x48,0x8B,0xF1 };
        static const int16 SigFloor[] = { 0x4C,0x8B,0xDC,0x53,0x41,0x55,0x48,0x81,0xEC,0xF8,0x00,0x00,0x00,0x8B,0x59,0x48,0x4C,0x8B,0xE9 };
        static const int16 SigInside[] = { 0x48,0x83,0xEC,0x28,0xF2,0x0F,0x10,0x15,-1,-1,-1,-1,0xE8,-1,-1,-1,-1,0x0F,0x57,0xC9,0x66,0x0F,0x2F,0xC8,0x0F,0x97,0xC0 };
        FString Missing, SigBad;
        const auto Bind = [&](const char* Sym, const int16* Sig, int32 N, const TCHAR* Name) -> void*
        {
            const uint8* B = reinterpret_cast<const uint8*>(::GetProcAddress(H, Sym));
            if (!B) { Missing += FString(Missing.IsEmpty() ? TEXT("") : TEXT(" ")) + Name; return nullptr; }
            for (int32 I = 0; I < N; ++I) { if (Sig[I] >= 0 && B[I] != static_cast<uint8>(Sig[I])) { SigBad += FString(SigBad.IsEmpty() ? TEXT("") : TEXT(" ")) + Name; return nullptr; } }
            return const_cast<uint8*>(B);
        };
        A.Caves = reinterpret_cast<FnCaves>(Bind(SymCaves, SigCaves, UE_ARRAY_COUNT(SigCaves), TEXT("CarvedCaves")));
        A.Num = reinterpret_cast<FnNum>(Bind(SymNum, SigNum, UE_ARRAY_COUNT(SigNum), TEXT("Num")));
        A.AnyNear = reinterpret_cast<FnAnyNear>(Bind(SymAnyNear, SigAnyNear, UE_ARRAY_COUNT(SigAnyNear), TEXT("AnyNear")));
        A.Floor = reinterpret_cast<FnFloor>(Bind(SymFloor, SigFloor, UE_ARRAY_COUNT(SigFloor), TEXT("FloorCmAt")));
        A.Inside = reinterpret_cast<FnInside>(Bind(SymInside, SigInside, UE_ARRAY_COUNT(SigInside), TEXT("IsInsideAt")));
        A.Status = !Missing.IsEmpty() ? TEXT("exportMissing") : (!SigBad.IsEmpty() ? TEXT("signatureMismatch") : TEXT("bound"));
        if (!A.Bound()) { A.Caves = nullptr; A.Num = nullptr; A.AnyNear = nullptr; A.Floor = nullptr; A.Inside = nullptr; } // all-or-nothing
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGCAVEBIND: status=%s missing='%s' signatureMismatched='%s' optional=1; the cave share counts only after RWGCAVECHECK grades it"),
            *A.Status, *Missing, *SigBad);
        if (!A.Bound()) { UE_LOG(LogNodeShuffle, Warning, TEXT("RWGCAVEBIND-OFF: status=%s; the cave share is 0 this session (every spot is a surface spot)"), *A.Status); }
#else
        A.Status = TEXT("notWindows");
#endif
    }

    const FRwgCaveApi& BoundCaves() { FRwgCaveApi& A = CaveApi(); if (!A.bTried) { TryBindCaves(A); } return A; }

    // WorldSeed by reflection (int property on the generator; persistence research: a top-level save field). -1 = unread.
    int64 ReadWorldSeed(UObject* Gen, FString& OutSource)
    {
        OutSource = TEXT("unread");
        if (!IsValid(Gen)) { return -1; }
        for (TFieldIterator<FProperty> It(Gen->GetClass()); It; ++It)
        {
            const FNumericProperty* N = CastField<FNumericProperty>(*It);
            if (!N || N->IsFloatingPoint() || It->GetName() != TEXT("WorldSeed")) { continue; }
            OutSource = TEXT("WorldSeed");
            const int64 V = N->GetSignedIntPropertyValue(N->ContainerPtrToValuePtr<void>(Gen));
            return (V < 0 && N->ElementSize == 4) ? V + 4294967296LL : V; // the dump file names print the seed unsigned (ASSUMED for seeds >= 2^31; none seen)
        }
        return -1;
    }

    bool JsonVec(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FVector& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if (!O.IsValid() || !O->TryGetArrayField(Key, A) || !A || A->Num() < 3 || !(*A)[0].IsValid() || !(*A)[1].IsValid() || !(*A)[2].IsValid()) { return false; }
        Out = FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
        return true;
    }
}

// ---- FRwgGroundQuery cave methods (declared in NodeShuffleMapMode.h). The cave object is CarvedCaves(F), taken per call. ----
bool NodeShuffleMapMode::FRwgGroundQuery::HasCaves() const { return IsUsable() && BoundCaves().Bound(); }
int32 NodeShuffleMapMode::FRwgGroundQuery::CaveNum() const { return HasCaves() ? CaveApi().Num(CaveApi().Caves(F)) : -1; }
bool NodeShuffleMapMode::FRwgGroundQuery::CaveAnyNear(double X, double Y, double R) const { return HasCaves() && CaveApi().AnyNear(CaveApi().Caves(F), X, Y, R); }
bool NodeShuffleMapMode::FRwgGroundQuery::CaveFloorAt(double X, double Y, double ZHint, double& OutFloorZ) const
{
    double Z = 0.0;
    if (!HasCaves() || !CaveApi().Floor(CaveApi().Caves(F), X, Y, ZHint, &Z) || !FMath::IsFinite(Z)) { return false; }
    OutFloorZ = Z;
    return true;
}
bool NodeShuffleMapMode::FRwgGroundQuery::CaveInsideAt(const FVector& P) const { return HasCaves() && CaveApi().Inside(CaveApi().Caves(F), &P); }

FString NodeShuffleMapMode::RwgCaveBindStatus() { return CaveApi().bTried ? CaveApi().Status : FString(TEXT("notTried")); }

bool ANodeShuffleSubsystem::RwgEnsureCaveCheck(const NodeShuffleMapMode::FRwgGroundQuery& Q)
{
    using namespace NodeShuffleMapMode;
    if (bRwgCaveCheckDone) { return bRwgCaveUsable; }
    if (!Q.HasCaves())
    {
        bRwgCaveCheckDone = true;
        bRwgCaveUsable = false;
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGCAVECHECK: not run; caveBind=%s queryUsable=%d; the cave share is 0 this session"), *RwgCaveBindStatus(), Q.IsUsable() ? 1 : 0);
        return false;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now < RwgCaveCheckRetryAt) { return false; }
    RwgCaveCheckRetryAt = Now + 5.0;
    UWorld* World = GetWorld();
    if (!World) { return false; }
    // RWG's own nodes (non-ours, hidden originals included): the natural-share numerator's population. An unadopted restored spawn of ours
    // is not caught by IsOursForRwg (P2g F2), so any node within 300 cm of an active new-node entry is excluded (mirror of the F2 guard):
    // otherwise a session that placed cave nodes would raise the next session's seed count (an output-derived input).
    TArray<FVector> OurSpots;
    for (const FNodeShuffleEntry& E : Layout) { if (E.bIsNewNode && E.bActive) { OurSpots.Add(E.Location); } }
    TArray<FVector> Nodes;
    int32 ExcludedOurs = 0;
    const FNodeShuffleConfigStruct Cfg = FNodeShuffleConfigStruct::GetActiveConfig(this); int32 ExcludedNotPool = 0;
    for (TActorIterator<AFGResourceNodeBase> It(World); It; ++It)
    {
        if (!IsValid(*It) || IsOursForRwg(*It)) { continue; }
        const AFGResourceNode* AsNode = Cast<AFGResourceNode>(*It); // RWG P5 review F1: the numerator uses the POOL's admission predicate (RollLayout eligibility)
        if (AsNode ? !IsEligibleVanillaNode(AsNode, Cfg.IncludeModdedNodes, /*bIncludeLiquid=*/true)
                   : !(Cfg.IncludeModdedNodes && It->GetResourceClass() && It->GetResourceNodeType() == EResourceNodeType::Node)) { ++ExcludedNotPool; continue; }
        const FVector L = It->GetActorLocation();
        bool bOurs = false;
        for (const FVector& O : OurSpots) { if (FVector::DistSquared(O, L) < FMath::Square(300.0)) { bOurs = true; break; } }
        if (bOurs) { ++ExcludedOurs; continue; }
        Nodes.Add(L);
    }
    if (Nodes.Num() < 8)
    {
        if (!bRwgCaveCheckDeferLogged) { bRwgCaveCheckDeferLogged = true; UE_LOG(LogNodeShuffle, Display, TEXT("RWGCAVECHECK: deferred; rwgNodesFound=%d (< 8); retried every 5 s, the cave share is 0 until it grades"), Nodes.Num()); }
        return false;
    }
    bRwgCaveCheckDone = true;
    const int32 Num = Q.CaveNum();
    RwgCaveNumAtCheck = Num;
    // Per-world JSON dump (Saved/PWMaps/carved-caves-<seed>.json): the cave count and each cave's stand point (positive controls).
    FString SeedSrc;
    const int64 Seed = ReadWorldSeed(RwgGenerator.Get(), SeedSrc);
    const FString JsonPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PWMaps"), FString::Printf(TEXT("carved-caves-%lld.json"), Seed));
    FString JsonStatus = Seed < 0 ? TEXT("seedUnread") : TEXT("missing");
    TArray<FVector> Stands;
    TArray<double> HeadroomCm;
    int32 JsonCaves = -1;
    int32 JsonRooms = 0, JsonRoomsDefaulted = 0; // P5-2 capacity: sum of each cave's `rooms` (a cave without the field counts RwgCaveDefaultRoomsPerCave)
    TArray<FRwgCaveCandidateGen> Gens;
    FString Text;
    if (Seed >= 0 && FFileHelper::LoadFileToString(Text, *JsonPath))
    {
        TArray<TSharedPtr<FJsonValue>> Arr;
        const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Text);
        if (!FJsonSerializer::Deserialize(R, Arr)) { JsonStatus = TEXT("parseFailed"); }
        else
        {
            JsonStatus = TEXT("read");
            JsonCaves = Arr.Num();
            for (const TSharedPtr<FJsonValue>& V : Arr)
            {
                const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
                FVector S;
                if (O.IsValid() && JsonVec(O, TEXT("stand"), S) && Stands.Num() < 64) { Stands.Add(S); double Hm = 0.0; O->TryGetNumberField(TEXT("headroom_m"), Hm); HeadroomCm.Add(Hm * 100.0); }
                int32 Rooms = 0;
                if (O.IsValid() && O->TryGetNumberField(TEXT("rooms"), Rooms) && Rooms >= 0) { JsonRooms += Rooms; } else { JsonRooms += RwgCaveDefaultRoomsPerCave; ++JsonRoomsDefaulted; }
                RwgCaveGensFromJson(O, Gens);
            }
        }
    }
    // Positive controls at the stand points: IsInsideAt at stand and at stand + RwgCaveInsideProbeCm; FloorCmAt (hint = stand Z) at the first 16.
    int32 StandInside = 0, StandInsideRaw = 0, FloorN = 0, FloorFound = 0, FloorWithin = 0;
    for (int32 I = 0; I < Stands.Num(); ++I)
    {
        const FVector& S = Stands[I];
        const bool bRaw = Q.CaveInsideAt(S);
        const bool bUp = Q.CaveInsideAt(S + FVector(0, 0, RwgCaveInsideProbeCm));
        StandInsideRaw += bRaw ? 1 : 0;
        StandInside += (bRaw || bUp) ? 1 : 0;
        double Fz = 0.0;
        bool bFound = false, bWithin = false;
        if (I < 16)
        {
            ++FloorN;
            bFound = Q.CaveFloorAt(S.X, S.Y, S.Z, Fz);
            bWithin = bFound && FMath::Abs(Fz - S.Z) <= RwgCaveFloorMatchCm;
            FloorFound += bFound ? 1 : 0;
            FloorWithin += bWithin ? 1 : 0;
        }
        if (I < 16)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("RWGCAVECHECK stand %d: at=(%.0f,%.0f,%.0f) insideAtStand=%d insideAtStandPlusProbe=%d floorFound=%d floorZ=%.0f floorMinusStandZ=%.0f headroomCm=%.0f"),
                I, S.X, S.Y, S.Z, bRaw ? 1 : 0, bUp ? 1 : 0, bFound ? 1 : 0, bFound ? Fz : RwgNaN(), bFound ? Fz - S.Z : RwgNaN(), HeadroomCm[I]);
        }
    }
    // Node positions: up to 32 sampled RWG nodes (negative-control expectation: false), and the full population for the share numerator.
    int32 SampleN = 0, SampleInside = 0, SampleFloor = 0, Seeds = 0;
    const int32 Stride = FMath::Max(1, Nodes.Num() / 32);
    for (int32 I = 0; I < Nodes.Num(); ++I)
    {
        const bool bIn = Q.CaveInsideAt(Nodes[I] + FVector(0, 0, RwgCaveInsideProbeCm));
        Seeds += bIn ? 1 : 0;
        if ((I % Stride) == 0 && SampleN < 32)
        {
            ++SampleN;
            SampleInside += bIn ? 1 : 0;
            double Fz = 0.0;
            SampleFloor += Q.CaveFloorAt(Nodes[I].X, Nodes[I].Y, Nodes[I].Z, Fz) ? 1 : 0;
        }
    }
    RwgCaveSeedCount = Seeds;
    RwgCaveSeedDen = Nodes.Num();
    // Grade. Num()==0 is usable with share 0. Otherwise: the JSON count must match when the file was read; every stand point must read
    // inside; every FloorCmAt at a stand point must find a floor within its headroom; and the node sample must not be ALL inside (a
    // stub answering true everywhere fails this; RWG cave nodes, if any, are expected to be a minority).
    const bool bNumOk = Num >= 0 && (JsonStatus != TEXT("read") || JsonCaves == Num);
    const bool bStandOk = Stands.Num() == 0 || StandInside == Stands.Num();
    const bool bFloorOk = FloorN == 0 || FloorWithin == FloorN;
    const bool bNotAllInside = SampleN == 0 || SampleInside < SampleN;
    bRwgCaveUsable = bNumOk && bStandOk && bFloorOk && bNotAllInside;
    // P5-2 user decision 1: capacity = sum of the JSON `rooms` when the file was read and matches Num(); otherwise Num() x the documented default.
    const bool bJsonMatches = JsonStatus == TEXT("read") && JsonCaves == Num;
    bRwgCaveCapacityFromJson = bJsonMatches;
    RwgCaveCapacity = bJsonMatches ? JsonRooms : FMath::Max(0, Num) * RwgCaveDefaultRoomsPerCave;
    // P5-2 user decision 2: candidates from the file when it matches (numMatchesJson=1) and yielded generators; else random XY + AnyNear.
    RwgCaveGens = bJsonMatches ? Gens : TArray<FRwgCaveCandidateGen>();
    RwgCaveGenWeight = 0.0;
    for (const FRwgCaveCandidateGen& G : RwgCaveGens) { RwgCaveGenWeight += G.Weight; }
    bRwgCaveJsonCandidates = bRwgCaveUsable && RwgCaveGens.Num() > 0 && RwgCaveGenWeight > 0.0;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGCAVECHECK: num=%d seed=%lld seedSource=%s json='%s' jsonStatus=%s jsonCaves=%d numMatchesJson=%d standPoints=%d insideAtStandOrProbe=%d insideAtStandRaw=%d floorChecked=%d floorFound=%d floorWithin300=%d nodeSample=%d nodeSampleInside=%d nodeSampleFloorFound=%d originalsTested=%d originalsInsideCave=%d seedsExcludedOurs=%d seedsExcludedNotPool=%d caveCapacity=%d caveCapacitySource=%s jsonRooms=%d jsonRoomsDefaulted=%d caveCandidateSource=%s caveCandidateGens=%d naturalShareCap=%.2f usable=%d"),
        Num, Seed, *SeedSrc, *JsonPath, *JsonStatus, JsonCaves, (JsonStatus == TEXT("read") && JsonCaves == Num) ? 1 : 0, Stands.Num(), StandInside, StandInsideRaw,
        FloorN, FloorFound, FloorWithin, SampleN, SampleInside, SampleFloor, RwgCaveSeedDen, RwgCaveSeedCount, ExcludedOurs, ExcludedNotPool, RwgCaveCapacity,
        bRwgCaveCapacityFromJson ? TEXT("json") : TEXT("default"), JsonRooms, JsonRoomsDefaulted, bRwgCaveJsonCandidates ? TEXT("json") : TEXT("random"), RwgCaveGens.Num(),
        RwgCaveShareCapMirror, bRwgCaveUsable ? 1 : 0);
    if (!bRwgCaveUsable) { UE_LOG(LogNodeShuffle, Warning, TEXT("RWGCAVECHECK-OFF: numOk=%d standOk=%d floorOk=%d nodeSampleNotAllInside=%d; the cave share is 0 this session"), bNumOk ? 1 : 0, bStandOk ? 1 : 0, bFloorOk ? 1 : 0, bNotAllInside ? 1 : 0); }
    else if (Num == 0) { UE_LOG(LogNodeShuffle, Display, TEXT("RWGCAVECHECK: this world holds no carved cave; usable, cave share 0 (every spot is a surface spot)")); }
    else if (Seeds == 0) { UE_LOG(LogNodeShuffle, Display, TEXT("RWGCAVECHECK: no tested pool node lies inside a cave (%d tested, %d caves); comparison only, the cave share uses the capacity"), RwgCaveSeedDen, Num); }
    return bRwgCaveUsable;
}

float ANodeShuffleSubsystem::RwgCaveShare(int32 Denominator) const
{
    // P5-2 user decision 1 (2026-10-18): the numerator is cave CAPACITY. Vanilla (NodeShuffleSubsystem.cpp:2043-2044 roll, :6983-6984 redeal):
    //     CaveChance = min(CaveShareCap, CaveSeedCount / max(1, pool + Count))      [the vanilla redeal divides by Layout.Num()]
    // RWG, the same shape with the seed count replaced by capacity, and capacity added to the denominator as the user wrote it:
    //     share = min(CaveShareCap = 0.25, RwgCaveCapacity / max(1, Denominator + RwgCaveCapacity))
    // Denominator = the caller's pool term (roll: VanillaLocations.Num() + Count; redeal: Layout.Num()). Capacity is latched at RWGCAVECHECK from
    // RWG's file (or Num() x RwgCaveDefaultRoomsPerCave), so it never counts our own spawns and the share cannot feed on itself.
    if (!bRwgCaveCheckDone || !bRwgCaveUsable || RwgCaveNumAtCheck <= 0 || RwgCaveCapacity <= 0) { return 0.0f; }
    return FMath::Min(static_cast<float>(NodeShuffleMapMode::RwgCaveShareCapMirror), static_cast<float>(RwgCaveCapacity) / FMath::Max(1, Denominator + RwgCaveCapacity));
}

bool ANodeShuffleSubsystem::RwgDrawCaveSpot(const NodeShuffleMapMode::FRwgGroundQuery& Q, FRandomStream& Rng, int32 AnyNearBudget, FVector& OutSpot, int32& OutAnyNearCalls)
{
    using namespace NodeShuffleMapMode;
    // Brief pipeline: random land XY -> AnyNear(X, Y, R) -> FloorCmAt (hint = the API surface Z: the floor nearest the surface) -> IsInsideAt
    // at floor + probe. AnyNear is evaluated first (the cheapest and rarest predicate); after a hit, a ring of 24 points within R is tried
    // (an AnyNear hit says "a cave within R", not "a cave under this XY"). Surface prefilters (sea / void / killbox / slope, as today) at the
    // spot's XY, then vanilla's headroom (350 cm) and Miner ceiling (1200 cm) by IsInsideAt, then the killbox at the FLOOR.
    OutAnyNearCalls = 0;
    if (bRwgCaveJsonCandidates) { return RwgDrawCaveSpotJson(Q, Rng, OutSpot); } // P5-2 user decision 2: the file's rooms first; AnyNear is the fallback
    const double H = Q.HalfExtent();
    for (int32 I = 0; I < AnyNearBudget; ++I)
    {
        const double X0 = Rng.FRandRange(-H, H), Y0 = Rng.FRandRange(-H, H);
        ++OutAnyNearCalls;
        if (!Q.CaveAnyNear(X0, Y0, RwgCaveNearRadiusCm)) { continue; }
        ++RwgRcCaveNearHits;
        for (int32 K = 0; K < 25; ++K)
        {
            const double Rr = K == 0 ? 0.0 : RwgCaveNearRadiusCm * ((K - 1) / 8 + 1) / 3.0;
            const double Ang = (K % 8) * (PI / 4.0);
            const double X = X0 + FMath::Cos(Ang) * Rr, Y = Y0 + FMath::Sin(Ang) * Rr;
            if (RwgVerifyCaveCandidate(Q, X, Y, /*ZHintOrSurface=*/nullptr, TEXT("random"), K, OutAnyNearCalls, OutSpot)) { return true; } // hint = API surface Z
        }
    }
    return false;
}

bool ANodeShuffleSubsystem::RwgTryCaveRedeal(const NodeShuffleMapMode::FRwgGroundQuery& Q, FNodeShuffleEntry& Entry, FRandomStream& Rng, float SpacingCm)
{
    // Vanilla redeal rule (TryRedealWaterLockedEntry): a SURFACE entry being re-homed rolls the natural cave share (liquids excluded).
    // P5 brief: a CAVE entry whose settle met rock / no floor is re-dealt within the cave pool first (bounded), else to the surface.
    const bool bWasCave = Entry.bRwgCave;
    Entry.bRwgCave = false;
    if (!RwgEnsureCaveCheck(Q)) { if (bWasCave) { ++RwgPcCaveRedealSurface; } return false; }
    const float Share = RwgCaveShare(Layout.Num());
    if (!bWasCave && (Entry.ResourceForm == 2 || !(Share > 0.0f && Rng.FRand() < Share))) { return false; } // vanilla: CaveChance > 0 && FRand() < CaveChance
    const float SpacingSq = FMath::Square(SpacingCm);
    for (int32 Try = 0; Try < 4; ++Try)
    {
        FVector C;
        int32 Calls = 0;
        if (!RwgDrawCaveSpot(Q, Rng, NodeShuffleMapMode::RwgCaveAnyNearPerSpot / 4, C, Calls)) { continue; }
        bool bTooClose = false; // cave picks space in 3D (vanilla cave-nodes-2)
        for (const FNodeShuffleEntry& Other : Layout) { if (&Other != &Entry && FVector::DistSquared(Other.Location, C) < SpacingSq) { bTooClose = true; break; } }
        if (bTooClose) { continue; }
        Entry.Location = C;
        Entry.Rotation = FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f);
        Entry.bRayCasted = false;
        Entry.bRwgCave = true;
        ++RwgPcCaveRedealCave;
        UE_LOG(LogNodeShuffle, Verbose, TEXT("RWGCAVEREDEAL: entry %s wasCave=%d -> cave floor %s"), *Entry.EntryGuid.ToString(), bWasCave ? 1 : 0, *C.ToCompactString());
        return true;
    }
    if (bWasCave) { ++RwgPcCaveRedealSurface; }
    return false;
}

bool ANodeShuffleSubsystem::RwgResolveCaveAtSpawn(const NodeShuffleMapMode::FRwgGroundQuery& Q, FNodeShuffleEntry& Entry, bool& bOutWaterNoLand)
{
    using namespace NodeShuffleMapMode;
    // Cave settle: ONE downward trace from floor + headroom (never from above the surface: that would stop on the ground over the cave) to
    // RwgCaveFloorMatchCm + 300 cm below the floor. Terrain-mesh hit within RwgCaveFloorMatchCm of the dealt floor -> settle; no hit -> defer
    // (chunk not resident); solid / water / far terrain -> bOutWaterNoLand (the caller redeals: cave pool first, then the surface).
    bOutWaterNoLand = false;
    ++RwgPcCaveSettleTried;
    UWorld* World = GetWorld();
    if (!World) { return false; }
    const bool bCaveOk = Q.HasCaves() && RwgEnsureCaveCheck(Q);
    if (!bCaveOk && (!Q.HasCaves() || bRwgCaveCheckDone))
    {
        bOutWaterNoLand = true; // the cave group is unbound or graded unusable this session: the caller's redeal sends it to the surface (counted there)
        return false;
    }
    if (!bCaveOk) { ++RwgPcCaveDeferred; return false; } // RWGCAVECHECK still deferred (< 8 RWG nodes found): defer, never judge the floor unchecked
    const double FloorZ = Entry.Location.Z;
    const FVector S(Entry.Location.X, Entry.Location.Y, FloorZ + RwgCaveHeadroomCmMirror);
    const FVector En(Entry.Location.X, Entry.Location.Y, FloorZ - RwgCaveFloorMatchCm - 300.0);
    FCollisionQueryParams P(SCENE_QUERY_STAT(NodeShuffleRwgCaveSettle), true);
    int32 Cls = -1, Skipped = 0;
    FHitResult Hit;
    for (int32 K = 0; K <= RwgCaveSettleSkipMax; ++K)
    {
        if (!World->LineTraceSingleByChannel(Hit, S, En, ECC_WorldStatic, P)) { Cls = -1; break; }
        const TCHAR* Rule = TEXT("");
        Cls = RwgHitClassOf(Hit, &Rule);
        if (Cls != 0) { RwgNoteHitClass(Hit, Cls, Rule); }
        if (Cls == 0 || Cls == 2 || Cls == 4) { break; }
        ++Skipped;
        P.AddIgnoredComponent(Hit.GetComponent());
        Cls = -1;
    }
    const TCHAR* Outcome = TEXT("settled");
    bool bSettled = false;
    if (Cls < 0)
    {
        Outcome = TEXT("noHitDefer");
        ++RwgPcCaveDeferred;
        if (IsLocationNearAnyPlayer(Entry.Location, 30000.f)) // mirror of the vanilla escape (CaveExpandNearPlayerCm = 300 m, 8 misses)
        {
            int32& Misses = RwgCaveSettleMisses.FindOrAdd(Entry.EntryGuid);
            if (++Misses >= RwgCaveSettleMissEscape) { RwgCaveSettleMisses.Remove(Entry.EntryGuid); ++RwgPcCaveEscaped; bOutWaterNoLand = true; Outcome = TEXT("missEscapeRedeal"); }
        }
    }
    else if (Cls == 4) { ++RwgPcCaveSolid; bOutWaterNoLand = true; Outcome = TEXT("solidRedeal"); }
    else if (Cls == 2) { ++RwgPcCaveWater; bOutWaterNoLand = true; Outcome = TEXT("waterRedeal"); }
    else if (FMath::Abs(Hit.ImpactPoint.Z - FloorZ) > RwgCaveFloorMatchCm) { ++RwgPcCaveFar; bOutWaterNoLand = true; Outcome = TEXT("terrainFarRedeal"); }
    else if (bRwgKillboxKnown && Hit.ImpactPoint.Z < RwgKillboxTopZ) { ++RwgKbSettleRefused; bOutWaterNoLand = true; Outcome = TEXT("belowKillboxRedeal"); }
    else
    {
        bSettled = true;
        Entry.Location.Z = Hit.ImpactPoint.Z;
        Entry.bRwgSettledOnTerrain = true; // a terrain-mesh hit (the resettle pass also skips bRwgCave)
        RwgCaveSettleMisses.Remove(Entry.EntryGuid);
        ++RwgPcCaveSettled;
        RwgNoteSettledInsideCave(Q, Entry);
    }
    if (RwgCaveSettleSamples < 8)
    {
        ++RwgCaveSettleSamples;
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGCAVESETTLE: sample=%d entry=%s floorZ=%.0f traceFromZ=%.0f traceToZ=%.0f hitClass=%d hitZ=%.0f skipped=%d outcome=%s"),
            RwgCaveSettleSamples, *Entry.EntryGuid.ToString(), FloorZ, S.Z, En.Z, Cls, Cls >= 0 ? Hit.ImpactPoint.Z : RwgNaN(), Skipped, Outcome);
    }
    return bSettled;
}

void ANodeShuffleSubsystem::RwgNoteSettledInsideCave(const NodeShuffleMapMode::FRwgGroundQuery& Q, const FNodeShuffleEntry& Entry)
{
    // settledInsideCave census (every RWG settle, cave and surface entries): IsInsideAt at the settled point + RwgCaveInsideProbeCm.
    // Measured only; gates nothing. Counted only while the cave group is graded usable (otherwise the query is not trusted).
    if (!bRwgCaveCheckDone || !bRwgCaveUsable || !Q.HasCaves()) { return; }
    ++RwgPcInsideCaveChecked;
    if (!Q.CaveInsideAt(Entry.Location + FVector(0, 0, NodeShuffleMapMode::RwgCaveInsideProbeCm))) { return; }
    ++RwgPcSettledInsideCave;
    if (!Entry.bRwgCave) { ++RwgPcSettledInsideCaveSurfaceEntry; }
}
