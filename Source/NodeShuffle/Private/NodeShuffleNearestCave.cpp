// RWG cave navigation (2026-10-18): `NodeShuffle.NearestCave` -- the nearest carved-cave MOUTH from RWG's per-world cave file and the
// nearest DEALT cave node (layout entries carrying bRwgCave), each with 2D distance, dZ and the "turn N deg and go" bearing that
// NodeShuffle.Here prints for the nearest relocated well (same expression: atan2 of the world delta minus the pawn's actor yaw).
// DECISION (user, 2026-10-18): cave navigation is a SEPARATE console command, not part of Here.
//
// LOG-ONLY. It reads the layout, the spawned-node map, the generator's WorldSeed and the cave file, and prints at most four lines.
// It traces nothing, spawns nothing, moves nothing and writes no member or layout field. Not gated behind EnableDiagnostics, same
// precedent as NodeShuffle.Here / .WhereCaveNodes: it exists for the author to report a location with.
//
// OFF RWG it prints one line and returns before any other read, so the vanilla path is untouched (Unknown / Unlatched maps take the
// same early return). Lines report what was measured and the predicate that branched, never a cause.
//
// WHY THE FILE IS RE-READ HERE: RwgEnsureCaveCheck (NodeShuffleMapModeCave.cpp) parses carved-caves-<seed>.json into locals and
// latches only the room generators, the capacity and the cave count -- no mouth point, headroom or cave index survives the check.
// So this command reads the same path again. The two helpers in the anonymous namespace below are COPIES of file-static helpers in
// NodeShuffleMapModeCave.cpp (ReadWorldSeed) and NodeShuffleMapModeCaveJson.cpp (CaveVec3), renamed so a unity build that merges
// those files into one translation unit cannot collide. TODO(2026-10-18, NearestCave): hoist both into NodeShuffleMapMode so the
// seed read and the vec3 read exist once (two copies of one question can come to disagree); the census line prints numAtCheck next
// to the file's cave count so a disagreement between the latch and this read is visible in the log.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "Resources/FGResourceNode.h"

namespace
{
    // COPY of ReadWorldSeed (NodeShuffleMapModeCave.cpp, anonymous namespace), unchanged in what it reads. -1 = unread.
    int64 NearestCaveReadWorldSeed(UObject* Gen, FString& OutSource)
    {
        OutSource = TEXT("unread");
        if (!IsValid(Gen)) { return -1; }
        for (TFieldIterator<FProperty> It(Gen->GetClass()); It; ++It)
        {
            const FNumericProperty* N = CastField<FNumericProperty>(*It);
            if (!N || N->IsFloatingPoint() || It->GetName() != TEXT("WorldSeed")) { continue; }
            OutSource = TEXT("WorldSeed");
            const int64 V = N->GetSignedIntPropertyValue(N->ContainerPtrToValuePtr<void>(Gen));
            return (V < 0 && N->ElementSize == 4) ? V + 4294967296LL : V; // the dump file names print the seed unsigned
        }
        return -1;
    }

    // COPY of CaveVec3 (NodeShuffleMapModeCaveJson.cpp): null-safe 3-vector read (a JSON null element is never dereferenced).
    bool NearestCaveVec3(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FVector& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if (!O.IsValid() || !O->TryGetArrayField(Key, A) || !A || A->Num() < 3 || !(*A)[0].IsValid() || !(*A)[1].IsValid() || !(*A)[2].IsValid()) { return false; }
        double V[3] = { 0.0, 0.0, 0.0 };
        for (int32 I = 0; I < 3; ++I) { if (!(*A)[I]->TryGetNumber(V[I])) { return false; } }
        Out = FVector(V[0], V[1], V[2]);
        return true;
    }

    FString NearestCaveShortName(const FString& Path)
    {
        int32 Dot = INDEX_NONE;
        return Path.FindLastChar(TEXT('.'), Dot) ? Path.Mid(Dot + 1) : Path;
    }

    constexpr int32 NearestCaveMaxCaves = 256; // bound on caves scanned per call (largest file seen on this machine: 7 caves)
}

void ANodeShuffleSubsystem::LogNearestCave() const
{
    // Off RWG: one line, nothing else read.
    const ENodeShuffleMapMode Mode = GetMapMode();
    if (Mode != ENodeShuffleMapMode::Rwg)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("NEARESTCAVE: no RWG cave data on this map (map mode is %s, not rwg); nothing else was read"),
            Mode == ENodeShuffleMapMode::Vanilla ? TEXT("vanilla") : (Mode == ENodeShuffleMapMode::Unknown ? TEXT("unknown") : TEXT("unlatched")));
        return;
    }
    const UWorld* World = GetWorld();
    const APlayerController* Pc = World ? World->GetFirstPlayerController() : nullptr;
    const APawn* Pawn = Pc ? Pc->GetPawn() : nullptr;
    if (!Pawn)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("NEARESTCAVE: no player pawn; nothing measured"));
        return;
    }
    const FVector P = Pawn->GetActorLocation();
    // Same bearing as NodeShuffle.Here's nearest-relocated-well line: turn from current facing, not a compass word.
    auto TurnFromFacing = [&P, Pawn](const FVector& Target) -> double
    {
        const FVector D = Target - P;
        return FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)) - Pawn->GetActorRotation().Yaw);
    };
    UE_LOG(LogNodeShuffle, Display, TEXT("NEARESTCAVE: player at X=%.0f Y=%.0f Z=%.0f yaw=%.0f (pawn actor location in cm, actor yaw in deg)"),
        P.X, P.Y, P.Z, Pawn->GetActorRotation().Yaw);

    // ---- Nearest cave MOUTH from the per-world cave file (the same path RWGCAVECHECK prints). ----
    FString SeedSrc;
    const int64 Seed = NearestCaveReadWorldSeed(RwgGenerator.Get(), SeedSrc);
    const FString JsonPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PWMaps"), FString::Printf(TEXT("carved-caves-%lld.json"), Seed));
    FString JsonStatus = Seed < 0 ? TEXT("seedUnread") : TEXT("missing");
    TArray<TSharedPtr<FJsonValue>> Arr;
    FString Text;
    if (Seed >= 0 && FFileHelper::LoadFileToString(Text, *JsonPath))
    {
        const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Text);
        JsonStatus = FJsonSerializer::Deserialize(R, Arr) ? TEXT("read") : TEXT("parseFailed");
    }
    int32 JsonCaves = -1;
    if (JsonStatus != TEXT("read"))
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("NEARESTCAVE: no cave JSON loaded -- jsonStatus=%s json='%s' seed=%lld seedSource=%s; RWGCAVECHECK latch: checkDone=%d usable=%d numAtCheck=%d capacityFromJson=%d"),
            *JsonStatus, *JsonPath, Seed, *SeedSrc, bRwgCaveCheckDone ? 1 : 0, bRwgCaveUsable ? 1 : 0, RwgCaveNumAtCheck, bRwgCaveCapacityFromJson ? 1 : 0);
    }
    else
    {
        JsonCaves = Arr.Num();
        int32 WithMouth = 0, BestIdx = -1, BestRooms = -1;
        double BestDistCm = TNumericLimits<double>::Max(), BestHeadroomM = -1.0;
        FVector BestMouth = FVector::ZeroVector;
        for (int32 I = 0; I < Arr.Num() && I < NearestCaveMaxCaves; ++I)
        {
            const TSharedPtr<FJsonObject> O = Arr[I].IsValid() ? Arr[I]->AsObject() : nullptr;
            FVector M;
            if (!NearestCaveVec3(O, TEXT("mouth"), M)) { continue; }
            ++WithMouth;
            const double DistCm = FVector::Dist2D(M, P);
            if (DistCm >= BestDistCm) { continue; }
            BestDistCm = DistCm;
            BestMouth = M;
            int32 Idx = I, Rooms = -1;
            double Hm = -1.0;
            O->TryGetNumberField(TEXT("cave"), Idx);   // the file's own index; the array position when absent
            O->TryGetNumberField(TEXT("rooms"), Rooms); // -1 when absent
            O->TryGetNumberField(TEXT("headroom_m"), Hm); // -1 when absent
            BestIdx = Idx; BestRooms = Rooms; BestHeadroomM = Hm;
        }
        if (WithMouth == 0)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("NEARESTCAVE: nearest cave mouth -- NONE; jsonCaves=%d cavesWithMouth=0 json='%s'"), JsonCaves, *JsonPath);
        }
        else
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("NEARESTCAVE: nearest cave mouth = cave %d (rooms=%d headroomM=%.1f) mouth at X=%.0f Y=%.0f Z=%.0f -- %.0f m away in 2D, dz=%+.0f m; from where you are standing and facing, turn %+.0f deg and go. jsonCaves=%d cavesWithMouth=%d"),
                BestIdx, BestRooms, BestHeadroomM, BestMouth.X, BestMouth.Y, BestMouth.Z, BestDistCm / 100.0, (BestMouth.Z - P.Z) / 100.0,
                TurnFromFacing(BestMouth), JsonCaves, WithMouth);
        }
    }

    // ---- Nearest DEALT cave node: active layout entries carrying bRwgCave; census over every bRwgCave entry. ----
    int32 Flagged = 0, Live = 0, RecordOnly = 0, Active = 0, Inactive = 0;
    const FNodeShuffleEntry* Best = nullptr;
    bool bBestLive = false;
    double BestEntryDistCm = TNumericLimits<double>::Max();
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bRwgCave) { continue; }
        ++Flagged;
        AFGResourceNode* const* Spawned = SpawnedNodes.Find(E.EntryGuid);
        const bool bLive = Spawned && IsValid(*Spawned);
        if (bLive) { ++Live; } else { ++RecordOnly; }
        if (!E.bActive) { ++Inactive; continue; }
        ++Active;
        const double DistCm = FVector::Dist2D(E.Location, P);
        if (DistCm < BestEntryDistCm) { BestEntryDistCm = DistCm; Best = &E; bBestLive = bLive; }
    }
    if (!Best)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("NEARESTCAVE: nearest dealt cave node -- NONE; %d layout entry(ies) carry the RWG cave flag, %d of them active"), Flagged, Active);
    }
    else
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("NEARESTCAVE: nearest dealt cave node = res=%s %s at X=%.0f Y=%.0f Z=%.0f -- %.0f m away in 2D, dz=%+.0f m; from where you are standing and facing, turn %+.0f deg and go. LIVE means the mod holds a spawned actor for that entry now; RECORD-ONLY does not tell a node never placed from one not spawned yet at this distance"),
            *NearestCaveShortName(Best->AssignedResourceClassPath), bBestLive ? TEXT("LIVE") : TEXT("RECORD-ONLY"),
            Best->Location.X, Best->Location.Y, Best->Location.Z, BestEntryDistCm / 100.0, (Best->Location.Z - P.Z) / 100.0, TurnFromFacing(Best->Location));
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("NEARESTCAVE: census caves=%d (the file's cave count, -1 when not loaded) numAtCheck=%d caveEntries=%d live=%d recordOnly=%d active=%d inactive=%d layout=%d"),
        JsonCaves, RwgCaveNumAtCheck, Flagged, Live, RecordOnly, Active, Inactive, Layout.Num());
}
