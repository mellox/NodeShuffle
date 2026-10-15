// RWG P1 (2026-10-14) -- the R1-only measurement lines (spec §G): RWGCOLLISION, RWGWATER, RWGROCK.
// Reached only from RwgObserveTick (NodeShuffleMapMode.cpp), i.e. only off vanilla, only after the
// observe-only readiness predicate latched. Traces and logs; writes nothing, learns nothing, gates nothing.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Resources/FGResourceNodeBase.h"
#include "Resources/FGResourceNode.h"
#include "FGWaterVolume.h"

void ANodeShuffleSubsystem::RwgLogCollision(bool bEmitWater)
{
    UWorld* World = GetWorld();
    APawn* Pawn = World ? NodeShuffleMapMode::FirstPlayerPawn(World) : nullptr;
    if (!Pawn)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGCOLLISION: no player pawn this pass; probes skipped"));
        return;
    }
    // Two windows per probe: "wide" = originals' zMax+300 m down to zMin-300 m (the spec C2 candidate) and
    // "today" = the vanilla settle window around the originals' mean Z (+200 m / -400 m, RaycastGroundAt).
    // Direct traces, NOT RaycastGroundAt: that one teaches the water grid as a side effect.
    static const float BandMinM[5] = { 0.f, 250.f, 500.f, 1000.f, 2000.f };
    static const float BandMaxM[5] = { 250.f, 500.f, 1000.f, 2000.f, 4000.f };
    const FVector P = Pawn->GetActorLocation();
    const float WideTop = RwgZMax + 30000.f;
    const float WideBottom = RwgZMin - 30000.f;
    const float TodayTop = RwgMeanZ + 20000.f;
    const float TodayBottom = RwgMeanZ - 40000.f;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(NodeShuffleRwgProbe), true);
    Params.AddIgnoredActor(Pawn);
    FString Bands;
    int32 RingWide = 0, RingInVolume = 0, RingBelowZ0 = 0;
    for (int32 B = 0; B < 5; ++B)
    {
        int32 HitWide = 0, HitToday = 0;
        const float R = 0.5f * (BandMinM[B] + BandMaxM[B]) * 100.f;
        for (int32 K = 0; K < 16; ++K)
        {
            const float A = K * (2.f * PI / 16.f);
            const float X = P.X + FMath::Cos(A) * R;
            const float Y = P.Y + FMath::Sin(A) * R;
            FHitResult Hit;
            if (World->LineTraceSingleByChannel(Hit, FVector(X, Y, WideTop), FVector(X, Y, WideBottom), ECC_WorldStatic, Params))
            {
                ++HitWide;
                ++RingWide;
                if (IsPointInWater(Hit.ImpactPoint)) { ++RingInVolume; }
                if (Hit.ImpactPoint.Z < 0.f) { ++RingBelowZ0; }
            }
            FHitResult Hit2;
            if (World->LineTraceSingleByChannel(Hit2, FVector(X, Y, TodayTop), FVector(X, Y, TodayBottom), ECC_WorldStatic, Params))
            {
                ++HitToday;
            }
        }
        Bands += FString::Printf(TEXT("%s%.0f-%.0fm wideHits %d of 16, todayHits %d of 16"),
            B > 0 ? TEXT("; ") : TEXT(""), BandMinM[B], BandMaxM[B], HitWide, HitToday);
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGCOLLISION: pawn=(%.0f,%.0f,%.0f) wideWindowZ=%.0f..%.0f todayWindowZ=%.0f..%.0f probesPerBand=16 bands[%s]"),
        P.X, P.Y, P.Z, WideTop, WideBottom, TodayTop, TodayBottom, *Bands);
    if (!bEmitWater) { return; }

    int32 Volumes = 0;
    for (TActorIterator<AFGWaterVolume> It(World); It; ++It) { if (IsValid(*It)) { ++Volumes; } }
    TMap<FString, int32> WaterClasses;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        const FString Name = It->GetClass()->GetName();
        if (Name.Contains(TEXT("Water")) || Name.Contains(TEXT("Ocean"))) { WaterClasses.FindOrAdd(Name)++; }
    }
    WaterClasses.ValueSort([](int32 L, int32 R) { return L > R; });
    FString List;
    int32 Shown = 0;
    for (const TPair<FString, int32>& Pair : WaterClasses)
    {
        if (Shown >= 15) { break; }
        List += FString::Printf(TEXT("%s%s:%d"), Shown > 0 ? TEXT("; ") : TEXT(""), *Pair.Key, Pair.Value);
        ++Shown;
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGWATER: waterVolumeActors=%d waterOrOceanClassesDistinct=%d [%s] ringWideHits=%d ringHitsInWaterVolume=%d ringHitsBelowZ0=%d"),
        Volumes, WaterClasses.Num(), *List, RingWide, RingInVolume, RingBelowZ0);
}

void ANodeShuffleSubsystem::RwgLogRock()
{
    UWorld* World = GetWorld();
    if (!World) { return; }
    // 20 non-ours nodes nearest the pawn (the pawn's surroundings are what is loaded). Actor ROOT locations
    // within 8 m only: an instanced mesh owned by a far actor is not visible to this probe.
    APawn* Pawn = NodeShuffleMapMode::FirstPlayerPawn(World);
    const FVector From = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
    TArray<AFGResourceNodeBase*> Nodes;
    for (TActorIterator<AFGResourceNodeBase> It(World); It; ++It)
    {
        if (IsValid(*It) && !NodeShuffleMapMode::IsOursForRwg(*It)) { Nodes.Add(*It); }
    }
    Nodes.Sort([&From](const AFGResourceNodeBase& L, const AFGResourceNodeBase& R)
    {
        return FVector::DistSquared(L.GetActorLocation(), From) < FVector::DistSquared(R.GetActorLocation(), From);
    });
    if (Nodes.Num() > 20) { Nodes.SetNum(20); }
    TArray<FString> Near;
    TArray<int32> NearCount, MeshActorNear;
    Near.SetNum(Nodes.Num());
    NearCount.SetNumZeroed(Nodes.Num());
    MeshActorNear.SetNumZeroed(Nodes.Num());
    const float R2 = FMath::Square(800.f);
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* A = *It;
        if (!IsValid(A)) { continue; }
        const FVector L = A->GetActorLocation();
        for (int32 I = 0; I < Nodes.Num(); ++I)
        {
            if (A == Nodes[I] || FVector::DistSquared(L, Nodes[I]->GetActorLocation()) > R2) { continue; }
            ++NearCount[I];
            if (A->IsA<AFGNodeMeshActor>()) { ++MeshActorNear[I]; }
            if (NearCount[I] > 6) { continue; }
            const UStaticMeshComponent* Smc = A->FindComponentByClass<UStaticMeshComponent>();
            Near[I] += FString::Printf(TEXT("%s%s/%s"), NearCount[I] > 1 ? TEXT("; ") : TEXT(""),
                *A->GetClass()->GetName(), (Smc && Smc->GetStaticMesh()) ? *Smc->GetStaticMesh()->GetName() : TEXT("<no mesh>"));
        }
    }
    int32 WithMeshActorLink = 0, WithNodeMeshActorNear = 0;
    for (int32 I = 0; I < Nodes.Num(); ++I)
    {
        const AActor* Linked = Nodes[I]->GetMeshActor();
        if (Linked) { ++WithMeshActorLink; }
        if (MeshActorNear[I] > 0) { ++WithNodeMeshActorNear; }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RWGROCK node %d of %d: class='%s' resource='%s' distFromPawnM=%.0f linkedMeshActor='%s' nodeMeshActorsWithin8m=%d actorsWithin8m=%d [%s]"),
            I + 1, Nodes.Num(), *Nodes[I]->GetClass()->GetName(),
            Nodes[I]->GetResourceClass() ? *Nodes[I]->GetResourceClass()->GetName() : TEXT("<none>"),
            FVector::Dist(Nodes[I]->GetActorLocation(), From) / 100.0,
            Linked ? *Linked->GetClass()->GetName() : TEXT("<unresolved or unset>"),
            MeshActorNear[I], NearCount[I], *Near[I]);
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGROCK: sampled=%d withLinkedMeshActorResolved=%d withNodeMeshActorWithin8m=%d"),
        Nodes.Num(), WithMeshActorLink, WithNodeMeshActorNear);
}
