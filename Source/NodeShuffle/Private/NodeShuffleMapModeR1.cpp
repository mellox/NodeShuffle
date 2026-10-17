// RWG P1 (2026-10-14) -- the R1-only measurement lines (spec §G): RWGCOLLISION, RWGWATER, RWGROCK.
// Reached only from RwgObserveTick (NodeShuffleMapMode.cpp), i.e. only off vanilla, only after the
// observe-only readiness predicate latched. Traces and logs; writes nothing, learns nothing, gates nothing.
// RWG P2a (2026-10-17, _team/nodeshuffle-rwg-p2-spec.md §1/§3): + the readiness-v2 helpers moved here to keep
// NodeShuffleMapMode.cpp under 500 lines, RWGCOLLISION hit Z/classes, RWGWATER2, RWGROCK2 (replaces the
// RWGROCK node sample, which could not see deposits' rocks or instanced meshes -- brief §0.7). The latch is
// now after RWG's progress stage reads Ready, so every probe here runs on a world RWG called Ready.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Resources/FGResourceNodeBase.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceDeposit.h"
#include "FGWaterVolume.h"

namespace
{
    // "Name:count; ..." of the Max largest entries (ties in map order).
    FString TopCounts(TMap<FString, int32> M, int32 Max)
    {
        M.ValueSort([](int32 L, int32 R) { return L > R; });
        FString Out;
        int32 Shown = 0;
        for (const TPair<FString, int32>& Pair : M)
        {
            if (Shown >= Max) { break; }
            Out += FString::Printf(TEXT("%s%s:%d"), Shown > 0 ? TEXT("; ") : TEXT(""), *Pair.Key, Pair.Value);
            ++Shown;
        }
        return Out;
    }
}

int32 ANodeShuffleSubsystem::RwgCountStrictNodes() const
{
    // Same predicate as IsWorldReadyForRoll (NodeShuffleSubsystem.cpp), not capped at 50: print-only.
    int32 Count = 0;
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        if (IsEligibleVanillaNode(*It, false, false)) { ++Count; }
    }
    return Count;
}

bool ANodeShuffleSubsystem::RwgAnyLoadedNodesAtFloor() const
{
    // Mirror of IsWorldReadyForReroll's count loop (NodeShuffleSubsystem.cpp, after its settle check), kept
    // equal by hand. NOT IsWorldReadyForReroll itself: off vanilla it returns false while !bRwgReady, so
    // calling it from the predicate that sets bRwgReady would deadlock (brief §1).
    int32 Count = 0;
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        if (IsValid(*It) && ++Count >= NodeShuffleMapMode::RwgMinNodes) { return true; }
    }
    return false;
}

void ANodeShuffleSubsystem::RwgReadContentFields(int32& OutContentSpawned, int32& OutFillPct) const
{
    // P2a diagnostic-only: bContentSpawned (bool) and FillContentPercent (byte, non-enum), type-checked by
    // name on the generator's class chain. -1 = absent or another type. Gates nothing.
    OutContentSpawned = OutFillPct = -1;
    UObject* Gen = RwgGenerator.Get();
    if (!IsValid(Gen)) { return; }
    for (TFieldIterator<FProperty> It(Gen->GetClass()); It; ++It)
    {
        const FProperty* P = *It;
        const void* Ptr = P->ContainerPtrToValuePtr<void>(Gen);
        if (P->GetName() == TEXT("bContentSpawned"))
        {
            if (const FBoolProperty* B = CastField<FBoolProperty>(P)) { OutContentSpawned = B->GetPropertyValue(Ptr) ? 1 : 0; }
        }
        else if (P->GetName() == TEXT("FillContentPercent"))
        {
            const FByteProperty* BP = CastField<FByteProperty>(P);
            if (BP && !BP->Enum) { OutFillPct = BP->GetPropertyValue(Ptr); }
        }
    }
}

void ANodeShuffleSubsystem::RwgNoteStage(const FString& Stage, int32 ContentSpawned, int32 FillPct, double T, double RealT)
{
    // One line per CHANGE of the stage string or of bContentSpawned, pre- and post-latch: the flip times and
    // the reload sequence (brief §0.10) at 5 s resolution instead of RWGWAIT's 30 s.
    const FString Shown = Stage.IsEmpty() ? FString(TEXT("<unreadable>")) : Stage;
    if (bRwgStageNoted && Shown == RwgLastStage && ContentSpawned == RwgLastContentSpawned) { return; }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGSTAGE: t=%.0fs realT=%.0fs observeTick=%d stage='%s' prevStage='%s' contentSpawned=%d prevContentSpawned=%d fillPct=%d latched=%d"),
        T, RealT, RwgObserveTicks, *Shown, bRwgStageNoted ? *RwgLastStage : TEXT("<first read>"),
        ContentSpawned, RwgLastContentSpawned, FillPct, bRwgReady ? 1 : 0);
    bRwgStageNoted = true;
    RwgLastStage = Shown;
    RwgLastContentSpawned = ContentSpawned;
}

void ANodeShuffleSubsystem::RwgLogReadyDiagnostics()
{
    if (!bRwgProbeLogged) { RwgLogProbe(); }
    RwgLogCollision(/*bEmitWater=*/true);
    RwgLogReprobes(/*bFull=*/true);
}

void ANodeShuffleSubsystem::RwgLogReprobes(bool bFull)
{
    // brief §3 + Addendum 2: per-node / per-point lines on the latch run only (bFull); summaries every run.
    RwgLogWater2();
    RwgLogRock(bFull);
    RwgLogHeightApi(bFull);
}

void ANodeShuffleSubsystem::RwgLogCollision(bool bEmitWater)
{
    UWorld* World = GetWorld();
    APawn* Pawn = World ? NodeShuffleMapMode::FirstPlayerPawn(World) : nullptr;
    if (!Pawn)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGCOLLISION: no player pawn this pass; probes skipped"));
        return;
    }
    // Two windows per probe: "wide" = strict originals' zMax+300 m down to zMin-300 m (the spec C2 candidate) and
    // "today" = the vanilla settle window around the strict originals' mean Z (+200 m / -400 m, RaycastGroundAt).
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
    TMap<FString, int32> HitClasses;
    int32 RingWide = 0, RingInVolume = 0, RingBelowZ0 = 0;
    for (int32 B = 0; B < 5; ++B)
    {
        int32 HitWide = 0, HitToday = 0;
        float ZLo = TNumericLimits<float>::Max(), ZHi = TNumericLimits<float>::Lowest();
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
                ZLo = FMath::Min(ZLo, static_cast<float>(Hit.ImpactPoint.Z));
                ZHi = FMath::Max(ZHi, static_cast<float>(Hit.ImpactPoint.Z));
                const AActor* HA = Hit.GetActor();
                HitClasses.FindOrAdd(HA ? HA->GetClass()->GetName() : FString(TEXT("<no actor>")))++;
                if (IsPointInWater(Hit.ImpactPoint)) { ++RingInVolume; }
                if (Hit.ImpactPoint.Z < 0.f) { ++RingBelowZ0; }
            }
            FHitResult Hit2;
            if (World->LineTraceSingleByChannel(Hit2, FVector(X, Y, TodayTop), FVector(X, Y, TodayBottom), ECC_WorldStatic, Params))
            {
                ++HitToday;
            }
        }
        Bands += FString::Printf(TEXT("%s%.0f-%.0fm wideHits %d of 16, todayHits %d of 16, wideHitZ %s"),
            B > 0 ? TEXT("; ") : TEXT(""), BandMinM[B], BandMaxM[B], HitWide, HitToday,
            HitWide > 0 ? *FString::Printf(TEXT("%.0f..%.0f"), ZLo, ZHi) : TEXT("none"));
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGCOLLISION: pawn=(%.0f,%.0f,%.0f) wideWindowZ=%.0f..%.0f todayWindowZ=%.0f..%.0f probesPerBand=16 bands[%s] wideHitClasses[%s]"),
        P.X, P.Y, P.Z, WideTop, WideBottom, TodayTop, TodayBottom, *Bands, *TopCounts(HitClasses, 5));
    if (!bEmitWater) { return; }

    int32 Volumes = 0;
    for (TActorIterator<AFGWaterVolume> It(World); It; ++It) { if (IsValid(*It)) { ++Volumes; } }
    TMap<FString, int32> WaterClasses;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        const FString Name = It->GetClass()->GetName();
        if (Name.Contains(TEXT("Water")) || Name.Contains(TEXT("Ocean"))) { WaterClasses.FindOrAdd(Name)++; }
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGWATER: waterVolumeActors=%d waterOrOceanClassesDistinct=%d [%s] ringWideHits=%d ringHitsInWaterVolume=%d ringHitsBelowZ0=%d"),
        Volumes, WaterClasses.Num(), *TopCounts(WaterClasses, 15), RingWide, RingInVolume, RingBelowZ0);
}

void ANodeShuffleSubsystem::RwgLogWater2()
{
    // brief §3 RWGWATER2. Every field is a count or a measured value. "largest" = the AFGWaterVolume with the
    // largest XY bounds area: the predicate is size, so the line says largest, not sea.
    UWorld* World = GetWorld();
    APawn* Pawn = World ? NodeShuffleMapMode::FirstPlayerPawn(World) : nullptr;
    if (!Pawn) { UE_LOG(LogNodeShuffle, Display, TEXT("RWGWATER2: no player pawn this pass; probes skipped")); return; }
    int32 Volumes = 0;
    const AFGWaterVolume* Largest = nullptr;
    double LargestArea = -1.0;
    FVector LOrigin = FVector::ZeroVector, LExtent = FVector::ZeroVector;
    for (TActorIterator<AFGWaterVolume> It(World); It; ++It)
    {
        if (!IsValid(*It)) { continue; }
        ++Volumes;
        FVector O, E;
        It->GetActorBounds(false, O, E);
        const double Area = 4.0 * E.X * E.Y;
        if (Area > LargestArea) { LargestArea = Area; Largest = *It; LOrigin = O; LExtent = E; }
    }
    const double LargestTop = LOrigin.Z + LExtent.Z;
    TMap<FString, int32> RwgWaterClasses;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        const UClass* C = It->GetClass();
        const FString Path = C->GetPathName();
        if ((Path.StartsWith(TEXT("/Script/ProceduralWorld.")) || Path.StartsWith(TEXT("/RandomWorldGenerator/")))
            && C->GetName().Contains(TEXT("Water")))
        {
            RwgWaterClasses.FindOrAdd(C->GetName())++;
        }
    }
    int32 RwgWaterActors = 0;
    for (const TPair<FString, int32>& Pair : RwgWaterClasses) { RwgWaterActors += Pair.Value; }
    // Ring over 0-500 m only (the resident disc R1 measured): 4 radii x 16 directions, the wide strict window.
    static const float RadiiM[4] = { 50.f, 150.f, 300.f, 450.f };
    const FVector P = Pawn->GetActorLocation();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(NodeShuffleRwgWater2), true);
    Params.AddIgnoredActor(Pawn);
    Params.bReturnPhysicalMaterial = true;
    int32 Probes = 0, Hits = 0, InVolume = 0, BelowTopNotIn = 0, WaterNamed = 0;
    float WZLo = TNumericLimits<float>::Max(), WZHi = TNumericLimits<float>::Lowest();
    TMap<FString, int32> WaterNamedLabels, PhysMats;
    double WaterTestSec = 0.0;
    for (int32 RI = 0; RI < 4; ++RI)
    {
        for (int32 K = 0; K < 16; ++K)
        {
            ++Probes;
            const float A = K * (2.f * PI / 16.f);
            const float X = P.X + FMath::Cos(A) * RadiiM[RI] * 100.f;
            const float Y = P.Y + FMath::Sin(A) * RadiiM[RI] * 100.f;
            FHitResult Hit;
            if (!World->LineTraceSingleByChannel(Hit, FVector(X, Y, RwgZMax + 30000.f), FVector(X, Y, RwgZMin - 30000.f), ECC_WorldStatic, Params)) { continue; }
            ++Hits;
            const double T0 = FPlatformTime::Seconds();
            const bool bIn = IsPointInWater(Hit.ImpactPoint);
            WaterTestSec += FPlatformTime::Seconds() - T0;
            if (bIn) { ++InVolume; }
            if (Largest && !bIn && Hit.ImpactPoint.Z < LargestTop) { ++BelowTopNotIn; }
            const AActor* HA = Hit.GetActor();
            const UPrimitiveComponent* HC = Hit.GetComponent();
            const FString ActorClass = HA ? HA->GetClass()->GetName() : FString(TEXT("<no actor>"));
            const FString CompLabel = HC ? HC->GetClass()->GetName() + TEXT(":") + HC->GetName() : FString(TEXT("<no component>"));
            if (ActorClass.Contains(TEXT("Water")) || CompLabel.Contains(TEXT("Water")))
            {
                ++WaterNamed;
                WZLo = FMath::Min(WZLo, static_cast<float>(Hit.ImpactPoint.Z));
                WZHi = FMath::Max(WZHi, static_cast<float>(Hit.ImpactPoint.Z));
                WaterNamedLabels.FindOrAdd(ActorClass + TEXT("/") + CompLabel)++;
            }
            const UPhysicalMaterial* PM = Hit.PhysMaterial.Get();
            PhysMats.FindOrAdd(PM ? PM->GetName() : FString(TEXT("<none>")))++;
        }
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGWATER2: pawn=(%.0f,%.0f,%.0f) volumes=%d largestVolume='%s' largestBoundsZ=%.0f..%.0f largestVolumeKm2=%.1f ringProbes=%d ringHits=%d inVolume=%d belowLargestTopNotInVolume=%d waterNamedHits=%d waterNamedHitZ=%s waterNamedHitLabels[%s] rwgWaterClassActors=%d rwgWaterClasses[%s] physMats[%s] waterTestUsPerProbe=%.2f"),
        P.X, P.Y, P.Z, Volumes, Largest ? *Largest->GetName() : TEXT("<none>"),
        LOrigin.Z - LExtent.Z, LargestTop, LargestArea > 0.0 ? LargestArea / 1.0e10 : 0.0,
        Probes, Hits, InVolume, BelowTopNotIn, WaterNamed,
        WaterNamed > 0 ? *FString::Printf(TEXT("%.0f..%.0f"), WZLo, WZHi) : TEXT("none"),
        *TopCounts(WaterNamedLabels, 3), RwgWaterActors, *TopCounts(RwgWaterClasses, 5), *TopCounts(PhysMats, 5),
        Hits > 0 ? WaterTestSec * 1.0e6 / Hits : 0.0);
}

void ANodeShuffleSubsystem::RwgLogRock(bool bPerNode)
{
    // brief §3 RWGROCK2: 20 strict nodes + 10 deposits nearest the pawn. Per node: linked mesh actor, own
    // mesh components, and an 8 m WorldStatic+WorldDynamic OVERLAP (components, so instanced meshes owned by
    // a far actor are visible, unlike P1's actor-root scan). Per-node lines only when bPerNode.
    UWorld* World = GetWorld();
    if (!World) { return; }
    APawn* Pawn = NodeShuffleMapMode::FirstPlayerPawn(World);
    const FVector From = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
    TArray<AFGResourceNodeBase*> Strict, Deps;
    for (TActorIterator<AFGResourceNodeBase> It(World); It; ++It)
    {
        AFGResourceNodeBase* N = *It;
        if (!IsValid(N) || NodeShuffleMapMode::IsOursForRwg(N)) { continue; }
        if (N->IsA<AFGResourceDeposit>()) { Deps.Add(N); continue; }
        const AFGResourceNode* Rn = Cast<AFGResourceNode>(N);
        if (Rn && IsEligibleVanillaNode(Rn, false, false)) { Strict.Add(N); }
    }
    const auto ByDist = [&From](const AFGResourceNodeBase& L, const AFGResourceNodeBase& R)
    {
        return FVector::DistSquared(L.GetActorLocation(), From) < FVector::DistSquared(R.GetActorLocation(), From);
    };
    Strict.Sort(ByDist);
    Deps.Sort(ByDist);
    if (Strict.Num() > 20) { Strict.SetNum(20); }
    if (Deps.Num() > 10) { Deps.SetNum(10); }
    const int32 NStrict = Strict.Num();
    TArray<AFGResourceNodeBase*> Nodes = Strict;
    Nodes.Append(Deps);
    FCollisionObjectQueryParams ObjParams;
    ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
    ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);
    int32 Linked = 0, Own = 0, NearMeshActor = 0, NearInstRock = 0, None = 0;
    for (int32 I = 0; I < Nodes.Num(); ++I)
    {
        AFGResourceNodeBase* N = Nodes[I];
        const AActor* LinkedActor = N->GetMeshActor();
        int32 OwnMeshes = 0;
        TInlineComponentArray<UStaticMeshComponent*> Meshes(N);
        for (const UStaticMeshComponent* MC : Meshes) { if (IsValid(MC) && MC->GetStaticMesh()) { ++OwnMeshes; } }
        TArray<FOverlapResult> Over;
        FCollisionQueryParams QParams(FName(TEXT("NodeShuffleRwgRock2")), false);
        QParams.AddIgnoredActor(N);
        World->OverlapMultiByObjectType(Over, N->GetActorLocation(), FQuat::Identity, ObjParams, FCollisionShape::MakeSphere(800.f), QParams);
        int32 MeshActorHere = 0, InstRockHere = 0, Listed = 0;
        FString List;
        for (const FOverlapResult& O : Over)
        {
            const UPrimitiveComponent* C = O.GetComponent();
            if (!C) { continue; }
            const AActor* CompOwner = C->GetOwner();
            if (CompOwner && CompOwner->IsA<AFGNodeMeshActor>()) { ++MeshActorHere; }
            const UStaticMeshComponent* Smc = Cast<UStaticMeshComponent>(C);
            const FString MeshName = (Smc && Smc->GetStaticMesh()) ? Smc->GetStaticMesh()->GetName() : FString(TEXT("<no mesh>"));
            const bool bInst = C->GetClass()->GetName().Contains(TEXT("Instanced"));
            if (bInst && IsNodeRockMeshName(MeshName, TArray<FString>())) { ++InstRockHere; }
            if (bPerNode && Listed < 6)
            {
                List += FString::Printf(TEXT("%s%s/%s/%s/inst%d"), Listed > 0 ? TEXT("; ") : TEXT(""), *C->GetClass()->GetName(),
                    CompOwner ? *CompOwner->GetClass()->GetName() : TEXT("<no owner>"), *MeshName, bInst ? 1 : 0);
                ++Listed;
            }
        }
        if (LinkedActor) { ++Linked; }
        if (OwnMeshes > 0) { ++Own; }
        if (MeshActorHere > 0) { ++NearMeshActor; }
        if (InstRockHere > 0) { ++NearInstRock; }
        if (!LinkedActor && OwnMeshes == 0 && MeshActorHere == 0 && InstRockHere == 0) { ++None; }
        if (bPerNode)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("RWGROCK2 node %d of %d: kind=%s class='%s' distFromPawnM=%.0f linkedMeshActor='%s' ownMeshComponents=%d overlapsWithin8m=%d nodeMeshActorsWithin8m=%d instancedRockMeshWithin8m=%d [%s]"),
                I + 1, Nodes.Num(), I < NStrict ? TEXT("strict") : TEXT("deposit"), *N->GetClass()->GetName(),
                FVector::Dist(N->GetActorLocation(), From) / 100.0,
                LinkedActor ? *LinkedActor->GetClass()->GetName() : TEXT("<unresolved or unset>"),
                OwnMeshes, Over.Num(), MeshActorHere, InstRockHere, *List);
        }
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGROCK2: sampled=%d strictSampled=%d depositsSampled=%d linked=%d own=%d nearMeshActor=%d nearInstancedRockMesh=%d none=%d"),
        Nodes.Num(), NStrict, Nodes.Num() - NStrict, Linked, Own, NearMeshActor, NearInstRock, None);
}
