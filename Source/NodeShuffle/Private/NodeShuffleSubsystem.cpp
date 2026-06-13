#include "NodeShuffleSubsystem.h"
#include "NodeShuffle.h"
#include "NodeShuffleConfig.h"
#include "NodeShuffleNodeAssets.h"

#include "EngineUtils.h"
#include "TimerManager.h"
#include "Algo/Count.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/StaticMesh.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/PlayerController.h"

#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceDeposit.h"
#include "Resources/FGResourceDescriptor.h"
#include "Equipment/FGResourceScanner.h"
#include "Buildables/FGBuildableResourceExtractorBase.h"
#include "FGPortableMiner.h"

namespace
{
    constexpr float TickIntervalSeconds = 5.0f;
    constexpr float SettleDistance = 25000.0f;      // 250 m: settle new nodes near players
    constexpr float ExtractorSnapDistance = 700.0f; // 7 m
    constexpr float PortableMinerSnapDistance = 1500.0f; // 15 m
    constexpr float MinNodeSpacing = 2500.0f;       // 25 m between pool locations
    constexpr int32 MinVanillaNodesForRoll = 50;    // world considered streamed in
}

ANodeShuffleSubsystem::ANodeShuffleSubsystem()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ANodeShuffleSubsystem::BeginPlay()
{
    Super::BeginPlay();
    if (!HasAuthority())
    {
        return;
    }
    GetWorldTimerManager().SetTimer(TickTimerHandle, this, &ANodeShuffleSubsystem::RefreshTick,
        TickIntervalSeconds, true, TickIntervalSeconds);
}

void ANodeShuffleSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(TickTimerHandle);
    Super::EndPlay(EndPlayReason);
}

void ANodeShuffleSubsystem::RefreshTick()
{
    const FNodeShuffleConfigStruct Config = FNodeShuffleConfigStruct::GetActiveConfig(this);
    if (!Config.Enabled)
    {
        if (!bLoggedDisabled)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("NodeShuffle is disabled in config; doing nothing."));
            bLoggedDisabled = true;
        }
        return;
    }
    bLoggedDisabled = false;

    if (!bLayoutGenerated)
    {
        if (!IsWorldReadyForRoll())
        {
            return;
        }
        const int32 Seed = Config.SeedOverride != 0 ? Config.SeedOverride
            : static_cast<int32>(FPlatformTime::Cycles());
        RollLayout(Seed, false);
    }
    else if (!bRerollCheckDone)
    {
        // Explicit re-roll: only with user opt-in AND a new non-zero seed.
        if (Config.AllowReroll && Config.SeedOverride != 0 && Config.SeedOverride != SavedSeed
            && IsWorldReadyForRoll())
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("Re-rolling layout: seed %d -> %d (AllowReroll on)"),
                SavedSeed, Config.SeedOverride);
            RollLayout(Config.SeedOverride, true);
        }
    }
    bRerollCheckDone = true;

    if (bLayoutGenerated)
    {
        ApplyLayout();
        if (!bDidInitialApply)
        {
            bDidInitialApply = true;
            LogLayoutSummary();
        }
    }
}

// ---------------------------------------------------------------- roll ----

bool ANodeShuffleSubsystem::IsWorldReadyForRoll() const
{
    int32 Count = 0;
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        if (IsEligibleVanillaNode(*It, false) && ++Count >= MinVanillaNodesForRoll)
        {
            return true;
        }
    }
    return false;
}

void ANodeShuffleSubsystem::RollLayout(int32 Seed, bool bIsReroll)
{
    const FNodeShuffleConfigStruct Config = FNodeShuffleConfigStruct::GetActiveConfig(this);
    FRandomStream Rng(Seed);

    // Carry occupied new-node entries through a re-roll: anything with a live
    // spawned actor that is occupied keeps its position/resource and is pinned.
    TArray<FNodeShuffleEntry> CarriedEntries;
    if (bIsReroll)
    {
        for (FNodeShuffleEntry& Old : Layout)
        {
            if (!Old.bIsNewNode || !Old.bActive)
            {
                continue;
            }
            AFGResourceNode* const* Live = SpawnedNodes.Find(Old.EntryGuid);
            if (Live && IsValid(*Live) && IsNodeOccupiedAnyway(*Live))
            {
                FNodeShuffleEntry Kept = Old;
                Kept.bPinned = true;
                CarriedEntries.Add(Kept);
            }
        }
    }

    // 1. Scan the eligible vanilla pool.
    TSet<const AFGResourceNode*> PortableMinerNodes;
    for (TActorIterator<AFGPortableMiner> It(GetWorld()); It; ++It)
    {
        if (It->mExtractResourceNode)
        {
            PortableMinerNodes.Add(It->mExtractResourceNode);
        }
    }

    struct FVanillaScan
    {
        AFGResourceNode* Node;
        bool bOccupied;
    };
    TArray<FVanillaScan> Vanilla;
    TArray<FVector> VanillaLocations;
    TMap<FString, int32> VanillaResourceCounts;     // descriptor path -> count
    TArray<TEnumAsByte<EResourcePurity>> PurityDeckSource;
    FString SpawnableNodeClassPath;

    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        AFGResourceNode* Node = *It;
        if (!IsEligibleVanillaNode(Node, Config.IncludeModdedNodes))
        {
            continue;
        }
        const bool bOccupied = Node->IsOccupied() || PortableMinerNodes.Contains(Node);
        Vanilla.Add({Node, bOccupied});
        VanillaLocations.Add(Node->GetActorLocation());
        if (const UClass* Res = Node->GetResourceClass())
        {
            VanillaResourceCounts.FindOrAdd(Res->GetPathName())++;
        }
        PurityDeckSource.Add(Node->GetResourcePurity());
        if (SpawnableNodeClassPath.IsEmpty())
        {
            SpawnableNodeClassPath = Node->GetClass()->GetPathName();
        }
    }

    if (Vanilla.Num() == 0 || VanillaResourceCounts.Num() == 0)
    {
        UE_LOG(LogNodeShuffle, Warning, TEXT("Roll aborted: no eligible vanilla nodes found."));
        return;
    }

    // 2. New node locations: custom JSON wins, otherwise seeded generation.
    TArray<FVector> NewLocations;
    if (!ReadCustomLocationsJson(NewLocations))
    {
        GenerateNewLocations(Rng, VanillaLocations, FMath::Max(0, Config.NewNodeCount), NewLocations);
        WriteGeneratedLocationsJson(NewLocations);
    }

    // Build the pool.
    TArray<FNodeShuffleEntry> NewLayout;
    NewLayout.Reserve(Vanilla.Num() + NewLocations.Num() + CarriedEntries.Num());

    for (const FVanillaScan& Scan : Vanilla)
    {
        FNodeShuffleEntry E;
        E.EntryGuid = FGuid::NewGuid();
        E.bIsNewNode = false;
        E.VanillaNodePath = Scan.Node->GetPathName();
        E.Location = Scan.Node->GetActorLocation();
        E.Rotation = Scan.Node->GetActorRotation();
        E.OriginalResourceClassPath = Scan.Node->GetResourceClass() ? Scan.Node->GetResourceClass()->GetPathName() : FString();
        E.OriginalPurity = Scan.Node->GetResourcePurity();
        E.AssignedResourceClassPath = E.OriginalResourceClassPath;
        E.AssignedPurity = E.OriginalPurity;
        E.bPinned = Scan.bOccupied;
        E.bActive = true;
        E.NodeClassPath = SpawnableNodeClassPath;
        NewLayout.Add(E);
    }
    for (const FVector& Loc : NewLocations)
    {
        FNodeShuffleEntry E;
        E.EntryGuid = FGuid::NewGuid();
        E.bIsNewNode = true;
        E.Location = Loc;
        E.Rotation = FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f);
        E.bActive = false;
        E.NodeClassPath = SpawnableNodeClassPath;
        NewLayout.Add(E);
    }

    // 3. Decide how many nodes are active.
    const int32 PoolSize = NewLayout.Num() + CarriedEntries.Num();
    const int32 PinnedCount = CarriedEntries.Num() +
        static_cast<int32>(Algo::CountIf(NewLayout, [](const FNodeShuffleEntry& E){ return E.bPinned; }));

    TArray<FString> ResourceKinds;
    VanillaResourceCounts.GenerateKeyArray(ResourceKinds);
    ResourceKinds.Sort(); // deterministic order for the RNG

    const int32 MinPerResource = FMath::Max(0, Config.MinNodesPerResource);
    // The completability floor protects VANILLA ores only; modded/item
    // resources shuffle freely with no guaranteed minimum.
    const int32 VanillaKindCount = static_cast<int32>(Algo::CountIf(ResourceKinds,
        [](const FString& K){ return K.StartsWith(TEXT("/Game/")); }));
    int32 TargetActive = FMath::RoundToInt(PoolSize * FMath::Clamp(Config.ActivePercent, 1, 100) / 100.0f);
    TargetActive = FMath::Max3(TargetActive, MinPerResource * VanillaKindCount, PinnedCount);
    if (!Config.AllowVanillaDisappear)
    {
        TargetActive = FMath::Max(TargetActive, Vanilla.Num());
    }
    TargetActive = FMath::Min(TargetActive, PoolSize);

    // 4. Per-resource quotas: vanilla proportions scaled, floored at the
    //    completability minimum and at the pinned count per resource.
    TMap<FString, int32> PinnedPerResource;
    for (const FNodeShuffleEntry& E : NewLayout)
    {
        if (E.bPinned) { PinnedPerResource.FindOrAdd(E.AssignedResourceClassPath)++; }
    }
    for (const FNodeShuffleEntry& E : CarriedEntries)
    {
        PinnedPerResource.FindOrAdd(E.AssignedResourceClassPath)++;
    }

    TMap<FString, int32> Quota;
    int32 QuotaSum = 0;
    for (const FString& Kind : ResourceKinds)
    {
        const float Share = static_cast<float>(VanillaResourceCounts[Kind]) / Vanilla.Num();
        const int32 KindFloor = Kind.StartsWith(TEXT("/Game/")) ? MinPerResource : 0;
        int32 Q = FMath::RoundToInt(Share * TargetActive);
        Q = FMath::Max3(Q, KindFloor, PinnedPerResource.FindRef(Kind));
        Quota.Add(Kind, Q);
        QuotaSum += Q;
    }
    // Trim or pad to hit TargetActive exactly, never dropping below the floor.
    int32 GuardCounter = 100000;
    while (QuotaSum != TargetActive && GuardCounter-- > 0)
    {
        const FString& Kind = ResourceKinds[Rng.RandRange(0, ResourceKinds.Num() - 1)];
        int32& Q = Quota[Kind];
        if (QuotaSum > TargetActive)
        {
            const int32 KindFloor = Kind.StartsWith(TEXT("/Game/")) ? MinPerResource : 0;
            const int32 Floor = FMath::Max(KindFloor, PinnedPerResource.FindRef(Kind));
            if (Q > Floor) { Q--; QuotaSum--; }
        }
        else
        {
            Q++; QuotaSum++;
        }
    }

    // 5. Choose the active set: pinned always; all vanilla if disappearing is
    //    off; then random draws until TargetActive.
    TArray<int32> Candidates;
    int32 ActiveCount = 0;
    for (int32 i = 0; i < NewLayout.Num(); i++)
    {
        FNodeShuffleEntry& E = NewLayout[i];
        if (E.bPinned || (!E.bIsNewNode && !Config.AllowVanillaDisappear))
        {
            E.bActive = true;
            ActiveCount++;
        }
        else
        {
            E.bActive = false;
            Candidates.Add(i);
        }
    }
    ActiveCount += CarriedEntries.Num();
    // Fisher-Yates draw.
    for (int32 i = Candidates.Num() - 1; i > 0; i--)
    {
        Candidates.Swap(i, Rng.RandRange(0, i));
    }
    for (int32 i = 0; i < Candidates.Num() && ActiveCount < TargetActive; i++)
    {
        NewLayout[Candidates[i]].bActive = true;
        ActiveCount++;
    }

    // 6. Deal resources to the active, non-pinned nodes from the quota deck.
    TArray<FString> ResourceDeck;
    for (const FString& Kind : ResourceKinds)
    {
        const int32 DeckCount = Quota[Kind] - PinnedPerResource.FindRef(Kind);
        for (int32 i = 0; i < DeckCount; i++) { ResourceDeck.Add(Kind); }
    }
    for (int32 i = ResourceDeck.Num() - 1; i > 0; i--)
    {
        ResourceDeck.Swap(i, Rng.RandRange(0, i));
    }

    // Purity deck: vanilla purity multiset, resized to the resource deck.
    TArray<TEnumAsByte<EResourcePurity>> PurityDeck;
    for (int32 i = 0; i < ResourceDeck.Num(); i++)
    {
        PurityDeck.Add(PurityDeckSource[i % PurityDeckSource.Num()]);
    }
    for (int32 i = PurityDeck.Num() - 1; i > 0; i--)
    {
        PurityDeck.Swap(i, Rng.RandRange(0, i));
    }

    int32 DeckIndex = 0;
    for (FNodeShuffleEntry& E : NewLayout)
    {
        if (!E.bActive || E.bPinned)
        {
            continue;
        }
        if (DeckIndex >= ResourceDeck.Num())
        {
            // Deck exhausted (rounding edge): keep vanilla assignment / deactivate new.
            if (E.bIsNewNode) { E.bActive = false; }
            continue;
        }
        // NEW nodes always carry vanilla ores: they are new ore deposits, and
        // every vanilla ore has a guaranteed donor look — item/modded cards
        // on a brand-new node render nothing. Swap a vanilla card forward.
        if (E.bIsNewNode && !ResourceDeck[DeckIndex].StartsWith(TEXT("/Game/")))
        {
            for (int32 j = DeckIndex + 1; j < ResourceDeck.Num(); j++)
            {
                if (ResourceDeck[j].StartsWith(TEXT("/Game/")))
                {
                    ResourceDeck.Swap(DeckIndex, j);
                    break;
                }
            }
            if (!ResourceDeck[DeckIndex].StartsWith(TEXT("/Game/")))
            {
                E.bActive = false; // no vanilla cards left: skip this new node
                continue;
            }
        }
        E.AssignedResourceClassPath = ResourceDeck[DeckIndex];
        if (Config.RandomizePurity || E.bIsNewNode)
        {
            E.AssignedPurity = PurityDeck[DeckIndex];
        }
        DeckIndex++;
    }

    NewLayout.Append(CarriedEntries);
    Layout = MoveTemp(NewLayout);
    SavedSeed = Seed;
    bLayoutGenerated = true;
    bDidInitialApply = false;

    // Live spawned actors from a previous layout die with the re-roll.
    if (bIsReroll)
    {
        for (auto& Pair : SpawnedNodes) { if (IsValid(Pair.Value)) { Pair.Value->Destroy(); } }
        for (auto& Pair : SpawnedMeshActors) { if (IsValid(Pair.Value)) { Pair.Value->Destroy(); } }
        SpawnedNodes.Empty();
        SpawnedMeshActors.Empty();
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("Rolled layout: seed %d, pool %d (vanilla %d, new %d), active %d, pinned %d"),
        Seed, PoolSize, Vanilla.Num(), NewLocations.Num(), ActiveCount, PinnedCount);
}

void ANodeShuffleSubsystem::GenerateNewLocations(FRandomStream& Rng, const TArray<FVector>& VanillaLocations,
                                                 int32 Count, TArray<FVector>& OutLocations) const
{
    if (VanillaLocations.Num() == 0 || Count <= 0)
    {
        return;
    }
    int32 Attempts = Count * 30;
    while (OutLocations.Num() < Count && Attempts-- > 0)
    {
        const FVector& Anchor = VanillaLocations[Rng.RandRange(0, VanillaLocations.Num() - 1)];
        const float Angle = Rng.FRandRange(0.f, 2.f * PI);
        const float Dist = Rng.FRandRange(5000.f, 15000.f); // 50-150 m
        FVector Candidate = Anchor + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.f);

        bool bTooClose = false;
        for (const FVector& Existing : VanillaLocations)
        {
            if (FVector::DistSquared2D(Existing, Candidate) < FMath::Square(MinNodeSpacing)) { bTooClose = true; break; }
        }
        if (!bTooClose)
        {
            for (const FVector& Existing : OutLocations)
            {
                if (FVector::DistSquared2D(Existing, Candidate) < FMath::Square(MinNodeSpacing)) { bTooClose = true; break; }
            }
        }
        if (!bTooClose)
        {
            OutLocations.Add(Candidate);
        }
    }
}

bool ANodeShuffleSubsystem::ReadCustomLocationsJson(TArray<FVector>& OutLocations) const
{
    const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Configs"), TEXT("NodeShuffle_CustomNodes.json"));
    FString Content;
    if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToString(Content, *Path))
    {
        return false;
    }
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        UE_LOG(LogNodeShuffle, Warning, TEXT("NodeShuffle_CustomNodes.json exists but failed to parse; ignoring."));
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
    if (!Root->TryGetArrayField(TEXT("Nodes"), Nodes))
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Nodes)
    {
        const TSharedPtr<FJsonObject>* Obj = nullptr;
        if (Value->TryGetObject(Obj))
        {
            FVector Loc;
            Loc.X = (*Obj)->GetNumberField(TEXT("X"));
            Loc.Y = (*Obj)->GetNumberField(TEXT("Y"));
            Loc.Z = (*Obj)->GetNumberField(TEXT("Z"));
            OutLocations.Add(Loc);
        }
    }
    UE_LOG(LogNodeShuffle, Display, TEXT("Using %d custom node locations from NodeShuffle_CustomNodes.json"), OutLocations.Num());
    return OutLocations.Num() > 0;
}

void ANodeShuffleSubsystem::WriteGeneratedLocationsJson(const TArray<FVector>& Locations) const
{
    TArray<TSharedPtr<FJsonValue>> Nodes;
    for (const FVector& Loc : Locations)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetNumberField(TEXT("X"), Loc.X);
        Obj->SetNumberField(TEXT("Y"), Loc.Y);
        Obj->SetNumberField(TEXT("Z"), Loc.Z);
        Nodes.Add(MakeShared<FJsonValueObject>(Obj));
    }
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetArrayField(TEXT("Nodes"), Nodes);

    FString Out;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Configs"), TEXT("NodeShuffle_GeneratedNodes.json"));
    FFileHelper::SaveStringToFile(Out, *Path);
}

// --------------------------------------------------------------- apply ----

void ANodeShuffleSubsystem::ApplyLayout()
{
    bool bChangedWorld = false;

    // Rebuild the vanilla path cache when stale (level actors reload per session).
    bool bCacheStale = VanillaNodeCache.Num() == 0;
    for (const auto& Pair : VanillaNodeCache)
    {
        if (!Pair.Value.IsValid()) { /* destroyed inactive nodes are expected */ }
    }
    if (bCacheStale)
    {
        for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
        {
            VanillaNodeCache.Add(It->GetPathName(), *It);
        }
    }

    // Always capture each resource's authentic rock look (mesh+materials+world
    // scale) from the pre-retype world — cheap, read-only, and needed to dress
    // NEW nodes correctly in either mode. The experimental toggle only controls
    // whether EXISTING retyped nodes are also re-skinned.
    bSwapVisualsActive = FNodeShuffleConfigStruct::GetActiveConfig(this).SwapNodeRockVisuals;
    SweepRockComponents();
    CaptureDonorVisuals();

    for (FNodeShuffleEntry& Entry : Layout)
    {
        if (Entry.bIsNewNode)
        {
            if (Entry.bActive)
            {
                EnsureNewNodeSpawned(Entry, bChangedWorld);
            }
            continue;
        }

        AFGResourceNode* Node = FindVanillaNodeByPath(Entry.VanillaNodePath);
        if (!Node)
        {
            continue; // already destroyed this session (deactivated), or not streamed
        }

        // Safety rule re-check: occupation wins over the rolled layout.
        if (!Entry.bPinned && IsNodeOccupiedAnyway(Node))
        {
            Entry.bPinned = true;
            Entry.bActive = true;
            Entry.AssignedResourceClassPath = Node->GetResourceClass() ? Node->GetResourceClass()->GetPathName() : Entry.OriginalResourceClassPath;
            Entry.AssignedPurity = Node->GetResourcePurity();
            UE_LOG(LogNodeShuffle, Verbose, TEXT("Pinning %s (became occupied)"), *Entry.VanillaNodePath);
            continue;
        }
        if (Entry.bPinned)
        {
            continue;
        }

        if (!Entry.bActive)
        {
            ApplyVanillaDeactivate(Entry, Node, bChangedWorld);
        }
        else if (Entry.AssignedResourceClassPath != Entry.OriginalResourceClassPath
              || Entry.AssignedPurity != Entry.OriginalPurity)
        {
            ApplyVanillaRetype(Entry, Node, bChangedWorld);
        }
    }

    SettleNewNodesNearPlayers();
    ReassociateOrphanedExtractors();

    if (!bDepositSweepDone)
    {
        SweepMismatchedDeposits();
        bDepositSweepDone = true;
    }

    // Phantom cleanup (catch-all): no matter which path produced it, any mesh
    // NodeShuffle owns that is larger than a rock can be is scenery that got
    // mis-skinned — a walk-through "shelf". Hide it and name it so the exact
    // source (spawned mesh actor vs. swept rock) is identified in the log.
    if (bChangedWorld)
    {
        RefreshScannersAndRadarTowers();
    }
}

void ANodeShuffleSubsystem::PurgePhantomMeshes()
{
    const auto CheckAndHide = [this](UStaticMeshComponent* C, const TCHAR* Source, const FGuid& Guid)
    {
        if (!IsValid(C) || !C->GetStaticMesh() || !C->IsVisible())
        {
            return;
        }
        // BoxExtent (true geometry half-size, pivot-independent) × 2 × scale =
        // real world size. SphereRadius was unreliable (pivot-inflated).
        const float R = C->GetStaticMesh()->GetBounds().BoxExtent.GetMax() * 2.0f * C->GetComponentScale().GetAbsMax();
        if (R > 1500.0f)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("PHANTOM purged: %s mesh '%s' radius %.0f at %s (guid %s)"),
                Source, *C->GetStaticMesh()->GetName(), R, *C->GetComponentLocation().ToCompactString(),
                *Guid.ToString());
            C->SetVisibility(false, true);
            C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    };

    for (const auto& Pair : SpawnedMeshActors)
    {
        if (AFGNodeMeshActor* MA = Cast<AFGNodeMeshActor>(Pair.Value))
        {
            CheckAndHide(MA->GetStaticMeshComponent(), TEXT("spawned-mesh-actor"), Pair.Key);
        }
    }
    for (const auto& Pair : SweptRocks)
    {
        CheckAndHide(Pair.Value.Get(), TEXT("swept-rock"), Pair.Key);
    }
}

void ANodeShuffleSubsystem::SweepMismatchedDeposits()
{
    // Resource deposits (the small one-off rocks) often sit ON nodes. After a
    // shuffle, a deposit can contradict its node (uranium deposit on a sulfur
    // node). Until deposits can be properly retyped with visuals, remove any
    // deposit whose resource differs from the shuffled node it sits on.
    // Free-standing deposits (no node nearby) are never touched.
    constexpr float DepositOnNodeDistance = 1200.0f; // 12 m
    int32 Removed = 0;
    for (TActorIterator<AFGResourceDeposit> It(GetWorld()); It; ++It)
    {
        AFGResourceDeposit* Deposit = *It;
        if (!IsValid(Deposit) || !Deposit->GetResourceClass())
        {
            continue;
        }
        const FVector Loc = Deposit->GetActorLocation();
        for (const FNodeShuffleEntry& Entry : Layout)
        {
            if (!Entry.bActive || Entry.bPinned)
            {
                continue;
            }
            if (FVector::DistSquared2D(Entry.Location, Loc) > FMath::Square(DepositOnNodeDistance))
            {
                continue;
            }
            if (Deposit->GetResourceClass()->GetPathName() != Entry.AssignedResourceClassPath)
            {
                UE_LOG(LogNodeShuffle, Verbose, TEXT("Removed mismatched deposit %s (%s) on shuffled node (%s)"),
                    *Deposit->GetName(), *Deposit->GetResourceClass()->GetName(),
                    *FPackageName::ObjectPathToObjectName(Entry.AssignedResourceClassPath));
                Deposit->Destroy();
                Removed++;
            }
            break; // nearest-enough entry found; matching deposits are kept
        }
    }
    if (Removed > 0)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("Deposit sweep: removed %d mismatched deposits"), Removed);
    }
}

void ANodeShuffleSubsystem::ApplyVanillaRetype(FNodeShuffleEntry& Entry, AFGResourceNode* Node, bool& bOutChangedWorld)
{
    UClass* AssignedClass = LoadClassByPath(Entry.AssignedResourceClassPath);
    UClass* OriginalClass = LoadClassByPath(Entry.OriginalResourceClassPath);
    if (!AssignedClass || !OriginalClass)
    {
        return;
    }

    const bool bClassRight = Node->GetResourceClass() == AssignedClass;
    const bool bPurityRight = Node->GetResourcePurity() == Entry.AssignedPurity;
    if (bClassRight && bPurityRight)
    {
        return; // override is SaveGame on the node: usually already right
    }

    if (!bClassRight)
    {
        // DATA: always apply the resource override so the node mines the new
        // resource. This is the rock-solid core and never depends on visuals.
        Node->SetResourceClassOverride(AssignedClass);
        Node->OnResourceClassOverrideReplication.Broadcast(Node, OriginalClass, AssignedClass);

        // VISUALS (experimental, opt-in): try to make the rock match. When
        // off, the node keeps its original rock look but mines the new resource.
        if (bSwapVisualsActive)
        {
            const bool bMeshSwapped = ApplyNodeOwnMesh(Node, AssignedClass, Entry);
            if (AFGNodeMeshActor* MeshActor = FindMeshActorForNode(Node))
            {
                MeshActor->OverrideMeshAndMaterials(Node, OriginalClass, AssignedClass);
            }
            UE_LOG(LogNodeShuffle, Verbose, TEXT("Retyped %s: %s -> %s (own mesh: %s)"),
                *Node->GetName(), *OriginalClass->GetName(), *AssignedClass->GetName(),
                bMeshSwapped ? TEXT("swapped") : TEXT("NO VISUAL"));
        }
    }
    if (!bPurityRight)
    {
        Node->SetResourcePurityOverride(Entry.AssignedPurity);
    }
    // Keep radiation truthful in both directions: nodes retyped TO uranium
    // start irradiating, nodes retyped AWAY from it stop. (Friend access.)
    Node->InitRadioactivity();
    Node->UpdateRadioactivity();
    bOutChangedWorld = true;
}

void ANodeShuffleSubsystem::ApplyVanillaDeactivate(FNodeShuffleEntry& Entry, AFGResourceNode* Node, bool& bOutChangedWorld)
{
    AFGNodeMeshActor* MeshActor = FindMeshActorForNode(Node);
    if (MeshActor)
    {
        MeshActor->SetActorHiddenInGame(true);
        MeshActor->SetActorEnableCollision(false);
    }
    // Ore-style nodes: also hide the separate rock actor's component, or the
    // rock remains standing as an unmineable ghost.
    if (const TWeakObjectPtr<UStaticMeshComponent>* Rock = SweptRocks.Find(Entry.EntryGuid))
    {
        if (UStaticMeshComponent* RockSmc = Rock->Get())
        {
            RockSmc->SetVisibility(false, true);
            RockSmc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    }
    VanillaNodeCache.Remove(Entry.VanillaNodePath);
    const bool bDestroyed = Node->Destroy();
    UE_LOG(LogNodeShuffle, Verbose, TEXT("Deactivated %s (destroyed: %s, mesh actor: %s)"),
        *Entry.VanillaNodePath, bDestroyed ? TEXT("yes") : TEXT("NO"),
        MeshActor ? TEXT("hidden") : TEXT("NOT FOUND"));
    bOutChangedWorld = true;
}

void ANodeShuffleSubsystem::EnsureNewNodeSpawned(FNodeShuffleEntry& Entry, bool& bOutChangedWorld)
{
    AFGResourceNode* const* Existing = SpawnedNodes.Find(Entry.EntryGuid);
    if (Existing && IsValid(*Existing))
    {
        return;
    }

    UClass* NodeClass = LoadClassByPath(Entry.NodeClassPath);
    UClass* ResourceClass = LoadClassByPath(Entry.AssignedResourceClassPath);
    if (!NodeClass || !ResourceClass)
    {
        return;
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Params.ObjectFlags = RF_Transient; // never serialized: the layout respawns it
    AFGResourceNode* Node = GetWorld()->SpawnActor<AFGResourceNode>(NodeClass, Entry.Location, Entry.Rotation, Params);
    if (!Node)
    {
        UE_LOG(LogNodeShuffle, Warning, TEXT("Failed to spawn new node at %s"), *Entry.Location.ToCompactString());
        return;
    }
    Node->InitResource(ResourceClass, RA_Infinite, Entry.AssignedPurity.GetValue());
    // Radiation is computed separately from the resource class: without this,
    // spawned uranium nodes look right but never irradiate. (Friend access.)
    Node->InitRadioactivity();
    Node->UpdateRadioactivity();
    Node->Tags.Add(FName(*Entry.EntryGuid.ToString()));

    // Spawned nodes need usable collision: the look-at prompt, build gun and
    // miner placement all trace against the Resource profile. Level-placed
    // nodes get this from per-instance level data, so the spawned BP has none.
    {
        UBoxComponent* UseBox = NewObject<UBoxComponent>(Node, TEXT("NodeShuffleUseBox"));
        if (Node->GetRootComponent())
        {
            UseBox->SetupAttachment(Node->GetRootComponent());
        }
        else
        {
            Node->SetRootComponent(UseBox);
            UseBox->SetWorldLocationAndRotation(Entry.Location, Entry.Rotation);
        }
        UseBox->SetBoxExtent(FVector(650.f, 650.f, 180.f));
        UseBox->SetCollisionProfileName(TEXT("Resource"));
        UseBox->RegisterComponent();
        // NOTE: deliberately NO extra blocking box. A solid invisible box
        // becomes a mid-air wall before the node settles to terrain — far
        // worse than the minor cosmetic of walking through the visual rock.
    }

    SpawnedNodes.Add(Entry.EntryGuid, Node);
    // Primary visual path: the node BP carries its own rock mesh — retarget it.
    if (!ApplyNodeOwnMesh(Node, ResourceClass, Entry))
    {
        DressSpawnedNode(Node, Entry, ResourceClass);
    }

    // Settle to terrain immediately if the ground is loaded here (it usually is
    // near the player). On success the node is grounded, so solid collision is
    // safe to add now. Distant nodes (no terrain loaded) defer to the proximity
    // pass — and stay collision-free while floating, so no mid-air walls.
    AActor* MeshActor = SpawnedMeshActors.FindRef(Entry.EntryGuid);
    if (!Entry.bRayCasted && RaycastSettle(Entry, Node, MeshActor))
    {
        Node->SetActorLocationAndRotation(Entry.Location, Entry.Rotation);
        if (MeshActor) { MeshActor->SetActorLocationAndRotation(Entry.Location, Entry.Rotation); }
        Entry.bRayCasted = true;
    }

    UE_LOG(LogNodeShuffle, Verbose, TEXT("Spawned new %s node at X=%.0f Y=%.0f Z=%.0f (settled: %s)"),
        *ResourceClass->GetName(), Entry.Location.X, Entry.Location.Y, Entry.Location.Z,
        Entry.bRayCasted ? TEXT("yes") : TEXT("deferred"));
    bOutChangedWorld = true;
}

void ANodeShuffleSubsystem::CaptureRockScales()
{
    if (bRockScalesCaptured)
    {
        return;
    }
    // Record the real world scale of each node-rock mesh as it sits in the
    // level. New nodes reuse these to match vanilla size exactly.
    for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
    {
        UStaticMeshComponent* Smc = *It;
        if (!IsValid(Smc) || Smc->GetWorld() != GetWorld() || !Smc->GetStaticMesh()
            || Smc->GetOwner() == nullptr || Smc->GetOwner()->HasAnyFlags(RF_Transient))
        {
            continue; // skip our own transient spawns
        }
        const FString Name = Smc->GetStaticMesh()->GetName();
        if (!RockScaleByMesh.Contains(Name))
        {
            RockScaleByMesh.Add(Name, Smc->GetComponentScale());
        }
    }
    bRockScalesCaptured = true;
}

void ANodeShuffleSubsystem::DressSpawnedNode(AFGResourceNode* Node, FNodeShuffleEntry& Entry, UClass* ResourceClass)
{
    // Spawn a mesh actor and stamp the donor look for the assigned resource —
    // the SAME faithful-copy approach as retypes (mesh + materials + world
    // scale from a real vanilla rock). No guessed scales, no size gating.
    AFGNodeMeshActor* MeshActor = GetWorld()->SpawnActorDeferred<AFGNodeMeshActor>(
        AFGNodeMeshActor::StaticClass(), FTransform(Entry.Rotation, Entry.Location),
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!MeshActor)
    {
        return;
    }
    MeshActor->SetFlags(RF_Transient);
    MeshActor->mNodeActor = Node;
    MeshActor->FinishSpawning(FTransform(Entry.Rotation, Entry.Location));
    SpawnedMeshActors.Add(Entry.EntryGuid, MeshActor);

    UStaticMeshComponent* Smc = MeshActor->GetStaticMeshComponent();
    if (!Smc)
    {
        return;
    }
    Smc->SetMobility(EComponentMobility::Movable);

    const FDonorVisual* Donor = DonorVisuals.Find(ResourceClass->GetPathName());
    if (Donor && Donor->Mesh.Get())
    {
        Smc->SetStaticMesh(Donor->Mesh.Get());
        for (int32 i = 0; i < Donor->Materials.Num(); i++)
        {
            if (UMaterialInterface* Mat = Donor->Materials[i].Get())
            {
                Smc->SetMaterial(i, Mat);
            }
        }
        MeshActor->SetActorScale3D(Donor->RelativeTransform.GetScale3D());
        MeshActor->SetActorLocation(Entry.Location);
    }

    // Solid collision so the player stands ON the spawned rock like a vanilla
    // node, instead of walking through a ghost.
    if (Smc->GetStaticMesh())
    {
        Smc->SetVisibility(true, true);
        Smc->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Smc->SetCollisionProfileName(TEXT("BlockAll"));
    }
}

void ANodeShuffleSubsystem::SettleNewNodesNearPlayers()
{
    TArray<FVector> PlayerLocations;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (const APlayerController* Pc = It->Get())
        {
            if (const APawn* Pawn = Pc->GetPawn())
            {
                PlayerLocations.Add(Pawn->GetActorLocation());
            }
        }
    }
    if (PlayerLocations.Num() == 0)
    {
        return;
    }

    for (FNodeShuffleEntry& Entry : Layout)
    {
        if (!Entry.bIsNewNode || !Entry.bActive || Entry.bRayCasted)
        {
            continue;
        }
        bool bNear = false;
        for (const FVector& P : PlayerLocations)
        {
            if (FVector::DistSquared2D(P, Entry.Location) < FMath::Square(SettleDistance)) { bNear = true; break; }
        }
        if (!bNear)
        {
            continue;
        }
        AFGResourceNode* const* Node = SpawnedNodes.Find(Entry.EntryGuid);
        AActor* const* Mesh = SpawnedMeshActors.Find(Entry.EntryGuid);
        if (Node && IsValid(*Node) && RaycastSettle(Entry, *Node, Mesh ? *Mesh : nullptr))
        {
            (*Node)->SetActorLocationAndRotation(Entry.Location, Entry.Rotation);
            if (Mesh && IsValid(*Mesh))
            {
                (*Mesh)->SetActorLocationAndRotation(Entry.Location, Entry.Rotation);
            }
            Entry.bRayCasted = true;
        }
    }
}

bool ANodeShuffleSubsystem::RaycastSettle(FNodeShuffleEntry& Entry, const AActor* IgnoreNode, const AActor* IgnoreMesh) const
{
    FCollisionQueryParams Params(SCENE_QUERY_STAT(NodeShuffleSettle), true);
    if (IgnoreNode) { Params.AddIgnoredActor(IgnoreNode); }
    if (IgnoreMesh) { Params.AddIgnoredActor(IgnoreMesh); }

    const FVector Start = Entry.Location + FVector(0, 0, 20000.f);
    const FVector End = Entry.Location - FVector(0, 0, 40000.f);
    FHitResult Hit;
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
    {
        return false;
    }
    Entry.Location = Hit.ImpactPoint;
    const FQuat AlignQuat = FQuat::FindBetweenNormals(FVector::UpVector, Hit.ImpactNormal);
    Entry.Rotation = (AlignQuat * FQuat(FRotator(0.f, Entry.Rotation.Yaw, 0.f))).Rotator();
    return true;
}

void ANodeShuffleSubsystem::ReassociateOrphanedExtractors()
{
    if (SpawnedNodes.Num() == 0)
    {
        return;
    }

    for (TActorIterator<AFGBuildableResourceExtractorBase> It(GetWorld()); It; ++It)
    {
        AFGBuildableResourceExtractorBase* Extractor = *It;
        if (Extractor->GetExtractableResource().GetObject() != nullptr)
        {
            continue;
        }
        const FVector Loc = Extractor->GetActorLocation();
        for (auto& Pair : SpawnedNodes)
        {
            AFGResourceNode* Node = Pair.Value;
            if (IsValid(Node) && FVector::DistSquared(Node->GetActorLocation(), Loc) < FMath::Square(ExtractorSnapDistance))
            {
                Extractor->SetExtractableResource(TScriptInterface<IFGExtractableResourceInterface>(Node));
                UE_LOG(LogNodeShuffle, Verbose, TEXT("Re-associated extractor %s with new node"), *Extractor->GetName());
                break;
            }
        }
    }

    for (TActorIterator<AFGPortableMiner> It(GetWorld()); It; ++It)
    {
        AFGPortableMiner* Miner = *It;
        if (Miner->mExtractResourceNode != nullptr)
        {
            continue;
        }
        const FVector Loc = Miner->GetActorLocation();
        for (auto& Pair : SpawnedNodes)
        {
            AFGResourceNode* Node = Pair.Value;
            if (IsValid(Node) && FVector::DistSquared(Node->GetActorLocation(), Loc) < FMath::Square(PortableMinerSnapDistance))
            {
                Miner->mExtractResourceNode = Node;
                break;
            }
        }
    }
}

void ANodeShuffleSubsystem::RefreshScannersAndRadarTowers()
{
    for (TActorIterator<AFGResourceScanner> It(GetWorld()); It; ++It)
    {
        // Friend access (AccessTransformers): force cluster rebuild on next use.
        It->mNodeClustersUpToDate = false;
    }
}

// -------------------------------------------------------------- helpers ----

bool ANodeShuffleSubsystem::IsEligibleVanillaNode(const AFGResourceNode* Node, bool bIncludeModded)
{
    if (!IsValid(Node) || Node->GetClass()->GetName().StartsWith(TEXT("SKEL_")))
    {
        return false;
    }
    // Mod-spawned nodes are transient: never part of the vanilla pool.
    if (Node->HasAnyFlags(RF_Transient))
    {
        return false;
    }
    // The resource class must be a genuine raw-resource descriptor. Mods like
    // AllMinable scatter invisible nodes carrying crafted-ITEM descriptors
    // (rotors, plates, ...) across the map; without this check they pollute
    // the pool AND the per-resource balance floor then guarantees active
    // nodes of every crafted item.
    // Nodes with a non-standard purity (shown as "Other Purity" in node
    // listings) are non-functional companion artifacts from mods like
    // AllMinable: no visual, no mining. Leave them out of the pool entirely
    // so they never count toward targets or receive/donate resources.
    const EResourcePurity Purity = Node->GetResourcePurity();
    if (Purity != RP_Inpure && Purity != RP_Normal && Purity != RP_Pure)
    {
        return false;
    }
    return Node->GetResourceNodeType() == EResourceNodeType::Node
        && Node->GetResourceForm() == EResourceForm::RF_SOLID
        && Node->GetResourceAmount() == EResourceAmount::RA_Infinite
        && Node->GetResourceClass() != nullptr
        && Node->GetResourceClass()->IsChildOf(UFGResourceDescriptor::StaticClass())
        // Mod-added descriptors (AllMinable's item nodes, modded ores) live
        // outside /Game/. They join the pool only when the user opts in;
        // the completability floor never applies to them either way.
        && (bIncludeModded || Node->GetResourceClass()->GetPathName().StartsWith(TEXT("/Game/")));
}

bool ANodeShuffleSubsystem::IsNodeOccupiedAnyway(const AFGResourceNode* Node) const
{
    if (Node->IsOccupied())
    {
        return true;
    }
    for (TActorIterator<AFGPortableMiner> It(GetWorld()); It; ++It)
    {
        if (It->mExtractResourceNode == Node)
        {
            return true;
        }
    }
    return false;
}

void ANodeShuffleSubsystem::SweepRockComponents()
{
    if (bRocksSwept)
    {
        return;
    }
    // User-extendable rock names: mods with vanilla-style separate rock
    // actors can be taught to the matcher without a rebuild. The unmatched-
    // name diagnostic in the log tells users exactly what to add here.
    // File: <game>/FactoryGame/Configs/NodeShuffle_RockPatterns.json
    //   { "Patterns": [ "SM_MyModRock", "CrystalNode_" ] }   (prefix match)
    TArray<FString> ExtraPatterns;
    {
        const FString PatternPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Configs"), TEXT("NodeShuffle_RockPatterns.json"));
        FString Content;
        TSharedPtr<FJsonObject> Root;
        if (FPaths::FileExists(PatternPath) && FFileHelper::LoadFileToString(Content, *PatternPath)
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Content), Root) && Root.IsValid())
        {
            const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
            if (Root->TryGetArrayField(TEXT("Patterns"), Arr))
            {
                for (const TSharedPtr<FJsonValue>& V : *Arr)
                {
                    FString S;
                    if (V->TryGetString(S) && !S.IsEmpty()) { ExtraPatterns.Add(S); }
                }
            }
            UE_LOG(LogNodeShuffle, Display, TEXT("Loaded %d extra rock patterns from NodeShuffle_RockPatterns.json"), ExtraPatterns.Num());
        }
    }

    // Candidate rocks: any static mesh component in this world whose mesh
    // name matches the game's node-rock naming (same names as the asset
    // table: ResourceNode_*, CoalResource_*, SulfurResource_*, Resource_*).
    TArray<UStaticMeshComponent*> Candidates;
    for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
    {
        UStaticMeshComponent* Smc = *It;
        if (!IsValid(Smc) || Smc->GetWorld() != GetWorld() || !Smc->GetStaticMesh())
        {
            continue;
        }
        const FString MeshName = Smc->GetStaticMesh()->GetName();
        if (MeshName.StartsWith(TEXT("ResourceNode")) || MeshName.StartsWith(TEXT("CoalResource"))
            || MeshName.StartsWith(TEXT("SulfurResource")) || MeshName.StartsWith(TEXT("Resource_"))
            || MeshName.StartsWith(TEXT("SAM_")) || MeshName.Contains(TEXT("ResourceNode"))
            // Modern naming family discovered via the unpaired-node diagnostic
            // (e.g. SM_SAM_Node_01): SM_ prefix + Node in the name.
            || (MeshName.StartsWith(TEXT("SM_")) && MeshName.Contains(TEXT("Node")))
            || ExtraPatterns.ContainsByPredicate([&MeshName](const FString& P){ return MeshName.StartsWith(P); }))
        {
            Candidates.Add(Smc);
        }
    }

    // Instanced components are shared across MANY rocks: hiding or re-meshing
    // one would change every instance in the world. Exclude them entirely.
    int32 SkippedInstanced = 0;
    Candidates.RemoveAll([&SkippedInstanced](UStaticMeshComponent* Smc)
    {
        if (Cast<UInstancedStaticMeshComponent>(Smc)) { SkippedInstanced++; return true; }
        return false;
    });

    // Exclusive greedy pairing by distance: each rock belongs to at most ONE
    // node, so a neighbor's deactivation can never hide this node's rock.
    constexpr float RockPairDistance = 800.0f; // rocks sit ON their node
    struct FPairCandidate { int32 EntryIdx; UStaticMeshComponent* Smc; float DistSq; };
    TArray<FPairCandidate> Pairs;
    for (int32 i = 0; i < Layout.Num(); i++)
    {
        if (Layout[i].bIsNewNode)
        {
            continue;
        }
        for (UStaticMeshComponent* Smc : Candidates)
        {
            const float DistSq = FVector::DistSquared2D(Smc->GetComponentLocation(), Layout[i].Location);
            if (DistSq < FMath::Square(RockPairDistance))
            {
                Pairs.Add({i, Smc, DistSq});
            }
        }
    }
    Pairs.Sort([](const FPairCandidate& A, const FPairCandidate& B){ return A.DistSq < B.DistSq; });
    TSet<UStaticMeshComponent*> ClaimedRocks;
    int32 Paired = 0;
    for (const FPairCandidate& P : Pairs)
    {
        if (SweptRocks.Contains(Layout[P.EntryIdx].EntryGuid) || ClaimedRocks.Contains(P.Smc))
        {
            continue;
        }
        SweptRocks.Add(Layout[P.EntryIdx].EntryGuid, P.Smc);
        ClaimedRocks.Add(P.Smc);
        Paired++;
    }
    bRocksSwept = true;
    UE_LOG(LogNodeShuffle, Display, TEXT("Rock sweep: %d candidate rock meshes (%d instanced skipped), %d nodes paired"),
        Candidates.Num(), SkippedInstanced, Paired);

    // Auto-learn pass: no name list can know every mod's rocks, but rock
    // meshes betray themselves statistically — their instances live almost
    // exclusively at nodes, while scenery (cliffs, water, foliage) is spread
    // across the whole map. Any mesh name whose instances are mostly at
    // unpaired nodes is adopted as a rock automatically.
    TArray<int32> UnpairedIdx;
    for (int32 i = 0; i < Layout.Num(); i++)
    {
        if (!Layout[i].bIsNewNode && !SweptRocks.Contains(Layout[i].EntryGuid))
        {
            UnpairedIdx.Add(i);
        }
    }
    if (UnpairedIdx.Num() > 0)
    {
        TMap<FString, int32> TotalByName;
        TMap<FString, TArray<FPairCandidate>> NearByName;
        for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
        {
            UStaticMeshComponent* Smc = *It;
            if (!IsValid(Smc) || Smc->GetWorld() != GetWorld() || !Smc->GetStaticMesh()
                || Cast<UInstancedStaticMeshComponent>(Smc) || ClaimedRocks.Contains(Smc))
            {
                continue;
            }
            const FString MeshName = Smc->GetStaticMesh()->GetName();
            TotalByName.FindOrAdd(MeshName)++;
            for (int32 Idx : UnpairedIdx)
            {
                const float DistSq = FVector::DistSquared2D(Smc->GetComponentLocation(), Layout[Idx].Location);
                if (DistSq < FMath::Square(RockPairDistance))
                {
                    NearByName.FindOrAdd(MeshName).Add({Idx, Smc, DistSq});
                    break;
                }
            }
        }
        int32 AutoLearned = 0;
        for (auto& Pair : NearByName)
        {
            const int32 Near = Pair.Value.Num();
            const int32 Total = TotalByName.FindRef(Pair.Key);
            // Adopt: at least 3 instances at nodes AND >=60% of all instances at nodes.
            if (Near >= 3 && Total > 0 && (Near * 100) / Total >= 60)
            {
                Pair.Value.Sort([](const FPairCandidate& A, const FPairCandidate& B){ return A.DistSq < B.DistSq; });
                int32 Adopted = 0;
                for (const FPairCandidate& P : Pair.Value)
                {
                    if (!SweptRocks.Contains(Layout[P.EntryIdx].EntryGuid) && !ClaimedRocks.Contains(P.Smc))
                    {
                        SweptRocks.Add(Layout[P.EntryIdx].EntryGuid, P.Smc);
                        ClaimedRocks.Add(P.Smc);
                        Adopted++;
                    }
                }
                AutoLearned += Adopted;
                UE_LOG(LogNodeShuffle, Display, TEXT("Auto-learned rock '%s': %d/%d instances at nodes, %d paired"),
                    *Pair.Key, Near, Total, Adopted);
            }
            else
            {
                UE_LOG(LogNodeShuffle, Verbose, TEXT("Rejected mesh '%s' near %d unpaired nodes (%d total instances)"),
                    *Pair.Key, Near, Total);
            }
        }
        UE_LOG(LogNodeShuffle, Display, TEXT("Auto-learn: %d additional nodes paired (%d still unpaired)"),
            AutoLearned, UnpairedIdx.Num() - AutoLearned);
    }
}

void ANodeShuffleSubsystem::CaptureDonorVisuals()
{
    if (bDonorsCaptured)
    {
        return;
    }
    // Capture one authentic look per ORIGINAL resource from the pre-retype
    // world (meshes are still original here). One donor per resource type is
    // enough; every vanilla resource has nodes, so coverage is complete. The
    // stored scale is WORLD scale — the only scale that renders correctly when
    // stamped onto another node (a rock's own scale is meaningless for a mesh
    // authored at a different base size — the cross-family "shelf" cause).
    for (const FNodeShuffleEntry& Entry : Layout)
    {
        if (Entry.bIsNewNode || Entry.OriginalResourceClassPath.IsEmpty()
            || DonorVisuals.Contains(Entry.OriginalResourceClassPath))
        {
            continue;
        }
        AFGResourceNode* Node = FindVanillaNodeByPath(Entry.VanillaNodePath);
        UStaticMeshComponent* Smc = Node ? Node->FindComponentByClass<UStaticMeshComponent>() : nullptr;
        if (!Smc || !Smc->GetStaticMesh())
        {
            const TWeakObjectPtr<UStaticMeshComponent>* Rock = SweptRocks.Find(Entry.EntryGuid);
            Smc = Rock ? Rock->Get() : nullptr;
        }
        if (!Smc || !Smc->GetStaticMesh())
        {
            continue;
        }
        FDonorVisual Donor;
        Donor.Mesh = Smc->GetStaticMesh();
        for (int32 i = 0; i < Smc->GetNumMaterials(); i++)
        {
            Donor.Materials.Add(Smc->GetMaterial(i));
        }
        // Store world scale (in the transform's scale slot).
        Donor.RelativeTransform = FTransform(FQuat::Identity, FVector::ZeroVector, Smc->GetComponentScale());
        DonorVisuals.Add(Entry.OriginalResourceClassPath, Donor);
    }
    bDonorsCaptured = true;
    UE_LOG(LogNodeShuffle, Display, TEXT("Captured donor visuals for %d resource types"), DonorVisuals.Num());
}

bool ANodeShuffleSubsystem::ApplyNodeOwnMesh(AFGResourceNode* Node, UClass* ResourceClass, const FNodeShuffleEntry& Entry)
{
    // Self-meshed nodes (coal/sulfur/stone/modded) carry their own component;
    // ore-style nodes use the swept separate rock actor's component.
    UStaticMeshComponent* Smc = Node->FindComponentByClass<UStaticMeshComponent>();
    if (!Smc)
    {
        const TWeakObjectPtr<UStaticMeshComponent>* Rock = SweptRocks.Find(Entry.EntryGuid);
        Smc = Rock ? Rock->Get() : nullptr;
    }
    bool bCreatedComponent = false;
    if (!Smc && DonorVisuals.Contains(ResourceClass->GetPathName()))
    {
        // Spawned NEW nodes (never swept) get their own visual component,
        // dressed from donor visuals below.
        UStaticMeshComponent* Created = NewObject<UStaticMeshComponent>(Node, TEXT("NodeShuffleVisual"));
        if (Node->GetRootComponent())
        {
            Created->SetupAttachment(Node->GetRootComponent());
        }
        Created->RegisterComponent();
        Created->SetWorldLocationAndRotation(Node->GetActorLocation(), Node->GetActorRotation());
        Smc = Created;
        bCreatedComponent = true;
    }
    if (!Smc)
    {
        return false;
    }

    // Stamp a faithful copy of a REAL rock of the assigned resource: the donor
    // captured its mesh, materials AND world scale from an actual vanilla node.
    // Applying all three (never the target's own scale) renders the rock
    // exactly as it looks in vanilla — no size metric, no guessing, no shelves.
    const FDonorVisual* Donor = DonorVisuals.Find(ResourceClass->GetPathName());
    if (!Donor || !Donor->Mesh.Get())
    {
        return false; // no known look for this resource → leave node's original
    }
    Smc->SetMobility(EComponentMobility::Movable);
    Smc->SetStaticMesh(Donor->Mesh.Get());
    for (int32 i = 0; i < Donor->Materials.Num(); i++)
    {
        if (UMaterialInterface* Mat = Donor->Materials[i].Get())
        {
            Smc->SetMaterial(i, Mat);
        }
    }
    Smc->SetWorldScale3D(Donor->RelativeTransform.GetScale3D());
    Smc->SetWorldLocation(Node->GetActorLocation());

    // Our own created visual (new nodes) must be made solid using the ROCK
    // MESH's own collision — it matches the irregular, slanted rock shape
    // exactly, so the player stands on the actual rock (a box never could).
    // Existing vanilla rocks already have their own collision; leave them.
    if (bCreatedComponent)
    {
        Smc->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Smc->SetCollisionProfileName(TEXT("BlockAll"));
        Smc->RecreatePhysicsState(); // rebuild collision for the swapped mesh
    }
    return true;
}

AFGNodeMeshActor* ANodeShuffleSubsystem::FindMeshActorForNode(AFGResourceNodeBase* Node) const
{
    auto* MutableThis = const_cast<ANodeShuffleSubsystem*>(this);
    if (!bMeshActorCacheBuilt)
    {
        for (TActorIterator<AFGNodeMeshActor> It(GetWorld()); It; ++It)
        {
            if (AFGResourceNodeBase* Paired = It->mNodeActor.Get())
            {
                MutableThis->MeshActorCache.Add(Paired, *It);
            }
        }
        MutableThis->bMeshActorCacheBuilt = true;
    }
    const TWeakObjectPtr<AFGNodeMeshActor>* Found = MeshActorCache.Find(Node);
    return Found && Found->IsValid() ? Found->Get() : nullptr;
}

AFGResourceNode* ANodeShuffleSubsystem::FindVanillaNodeByPath(const FString& Path) const
{
    const TWeakObjectPtr<AFGResourceNode>* Found = VanillaNodeCache.Find(Path);
    return Found && Found->IsValid() ? Found->Get() : nullptr;
}

UClass* ANodeShuffleSubsystem::LoadClassByPath(const FString& Path)
{
    return Path.IsEmpty() ? nullptr : LoadClass<UObject>(nullptr, *Path);
}

void ANodeShuffleSubsystem::LogLayoutSummary() const
{
    TMap<FString, int32> ActivePerResource;
    int32 ActiveVanilla = 0, InactiveVanilla = 0, ActiveNew = 0, Pinned = 0;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (E.bPinned) { Pinned++; }
        if (E.bActive)
        {
            FString Name = FPackageName::ObjectPathToObjectName(E.AssignedResourceClassPath);
            ActivePerResource.FindOrAdd(Name)++;
            if (E.bIsNewNode) { ActiveNew++; } else { ActiveVanilla++; }
        }
        else if (!E.bIsNewNode)
        {
            InactiveVanilla++;
        }
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("Layout active: %d vanilla kept, %d vanilla removed, %d new spawnable, %d pinned"),
        ActiveVanilla, InactiveVanilla, ActiveNew, Pinned);
    for (const auto& Pair : ActivePerResource)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("  %s: %d active nodes"), *Pair.Key, Pair.Value);
    }
}
