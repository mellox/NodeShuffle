#include "NodeShuffleSubsystem.h"
#include "NodeShuffleConfig.h"
#include "NodeShuffleNodeComponent.h"
#include "NodeShuffleResourceNode.h"
#include "NodeShuffleLateNodePolicy.h"
#include "EngineUtils.h"
#include "Misc/Crc.h"
#include "Resources/FGResourceDeposit.h"
#include "Resources/FGResourceDescriptor.h"

// The existing refresh timer only polls this cheap schedule. Actor discovery stops after scan two.
void ANodeShuffleSubsystem::MaybeCaptureLateOriginalNodes()
{
    if (!HasAuthority() || !bLayoutGenerated || !bAdoptedRestoredNodes) { return; }
    const double Now = GetWorld()->GetTimeSeconds();
    if (!NodeShuffleLateNodePolicy::ScanDue(LateNodeScansCompleted, Now, FirstLateNodeScanTime)) { return; }
    if (!FNodeShuffleConfigStruct::GetActiveConfig(this).IncludeModdedNodes) { return; }
    if (LateNodeScansCompleted == 0 && !IsWorldReadyForReroll()) { return; }

    const double Started = FPlatformTime::Seconds();
    const int32 LayoutBefore = Layout.Num();
    const int32 RecordsBefore = OriginalNodeRecord.Num();
    CaptureLateOriginalNodes();
    if (LateNodeScansCompleted == 0) { FirstLateNodeScanTime = Now; }
    ++LateNodeScansCompleted; // Count empty/deferred scans too: never create an unbounded retry loop.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("LATE-NODES scan=%d/2 elapsedSinceFirst=%.1fs durationMs=%.2f addedEntries=%d addedHideRecords=%d layoutTotal=%d hideRecordsTotal=%d scheduleExhausted=%d"),
        LateNodeScansCompleted, Now - FirstLateNodeScanTime, (FPlatformTime::Seconds() - Started) * 1000.0,
        Layout.Num() - LayoutBefore, OriginalNodeRecord.Num() - RecordsBefore, Layout.Num(), OriginalNodeRecord.Num(),
        LateNodeScansCompleted == 2);
}

// Additional originals are dealt into new locations, without changing the existing draw.
// No mod names: the bounded startup/load scans admit any eligible resource node.
void ANodeShuffleSubsystem::CaptureLateOriginalNodes()
{
    if (!HasAuthority() || !bLayoutGenerated || !bAdoptedRestoredNodes) { return; }
    const FNodeShuffleConfigStruct Config = FNodeShuffleConfigStruct::GetActiveConfig(this);
    if (!Config.IncludeModdedNodes) { return; }

    TSet<FString> KnownPaths;
    TSet<FString> CountedPaths;
    TArray<FVector> MapLocations;
    TArray<FVector> AvoidLocations;
    TMap<FString, int32> OriginalCounts;
    TMap<FString, int32> ActiveCounts;
    for (const FNodeShuffleSuppressedOriginal& Rec : OriginalNodeRecord)
    {
        KnownPaths.Add(Rec.VanillaNodePath);
        const FVector Anchor = Rec.TrueLocation.IsNearlyZero() ? Rec.Location : Rec.TrueLocation;
        MapLocations.Add(Anchor);
        AvoidLocations.Add(Anchor);
        if (!Rec.OriginalResourceClassPath.IsEmpty())
        {
            OriginalCounts.FindOrAdd(Rec.OriginalResourceClassPath)++;
            CountedPaths.Add(Rec.VanillaNodePath);
        }
    }
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (E.bActive) { ActiveCounts.FindOrAdd(E.AssignedResourceClassPath)++; }
        AvoidLocations.Add(E.Location);
        if (E.VanillaNodePath.IsEmpty()) { continue; }
        // Older records have no resource field; recover their count from surviving entries.
        if (!CountedPaths.Contains(E.VanillaNodePath))
        {
            OriginalCounts.FindOrAdd(E.OriginalResourceClassPath)++;
            CountedPaths.Add(E.VanillaNodePath);
            MapLocations.Add(E.OriginalTrueLocation.IsNearlyZero() ? E.Location : E.OriginalTrueLocation);
        }
        KnownPaths.Add(E.VanillaNodePath);
    }

    TArray<AFGResourceNodeBase*> Candidates;
    int32 Seen = 0, Managed = 0, Known = 0, Ineligible = 0, OnOurDestination = 0, AnchorDeferred = 0;
    for (TActorIterator<AFGResourceNodeBase> It(GetWorld()); It; ++It)
    {
        AFGResourceNodeBase* Node = *It;
        if (!IsValid(Node)) { continue; }
        AvoidLocations.Add(Node->GetActorLocation());
        ++Seen;
        if (FNodeShuffleModule::IsManagedSpawnedNode(Node)
            || UNodeShuffleNodeComponent::Find(Node) || Node->IsA<ANodeShuffleResourceNode>())
        {
            ++Managed;
            continue;
        }
        if (KnownPaths.Contains(Node->GetPathName())) { ++Known; continue; }
        const UClass* Resource = Node->GetResourceClass();
        AFGResourceNode* SolidNode = Cast<AFGResourceNode>(Node);
        if (Node->HasAnyFlags(RF_Transient) || Node->IsA<AFGResourceDeposit>()
            || IsFrackingActor(Node) || Node->GetResourceNodeType() != EResourceNodeType::Node
            || !Resource || (SolidNode && !IsEligibleVanillaNode(SolidNode, true, true)))
        {
            ++Ineligible;
            continue;
        }
        // Mirror of AdoptRestoredSpawnedNodes' key (active new-node entry, same dealt resource, within
        // 300 cm of E.Location): an actor on one of OUR destinations is never treated as a fresh original.
        bool bOnOurDestination = false;
        for (const FNodeShuffleEntry& E : Layout)
        {
            if (!E.bIsNewNode || !E.bActive) { continue; }
            if (E.AssignedResourceClassPath == Resource->GetPathName()
                && FVector::DistSquared(E.Location, Node->GetActorLocation()) < FMath::Square(300.0f))
            {
                bOnOurDestination = true;
                break;
            }
        }
        if (bOnOurDestination) { ++OnOurDestination; continue; }

        // Spawner mods recreate actors with new names on reload. Never treat an actor at
        // a recorded original's anchor as a fresh discovery while re-matching is pending.
        // This conservative exclusion also covers legacy hide-only records lacking identity.
        bool bRecordedAnchor = false;
        for (const FNodeShuffleSuppressedOriginal& Rec : OriginalNodeRecord)
        {
            const FVector Anchor = Rec.TrueLocation.IsNearlyZero() ? Rec.Location : Rec.TrueLocation;
            const bool bIdentityMatches = Rec.OriginalNodeClassPath.IsEmpty()
                || (Rec.OriginalNodeClassPath == Node->GetClass()->GetPathName()
                    && Rec.OriginalResourceClassPath == Resource->GetPathName());
            if (bIdentityMatches && FVector::DistSquared(Anchor, Node->GetActorLocation()) < FMath::Square(300.0f))
            {
                bRecordedAnchor = true;
                break;
            }
        }
        for (const FNodeShuffleEntry& E : Layout)
        {
            if (bRecordedAnchor) { break; }
            if (E.VanillaNodePath.IsEmpty()) { continue; }
            const FVector Anchor = E.OriginalTrueLocation.IsNearlyZero() ? E.Location : E.OriginalTrueLocation;
            if (E.NodeClassPath == Node->GetClass()->GetPathName()
                && E.OriginalResourceClassPath == Resource->GetPathName()
                && FVector::DistSquared(Anchor, Node->GetActorLocation()) < FMath::Square(300.0f))
            {
                bRecordedAnchor = true;
            }
        }
        if (bRecordedAnchor) { ++AnchorDeferred; continue; }
        Candidates.Add(Node);
    }

    const FString Census = FString::Printf(
        TEXT("seen=%d managed=%d knownPath=%d ineligible=%d onOurDestination=%d recordedAnchor=%d candidates=%d"),
        Seen, Managed, Known, Ineligible, OnOurDestination, AnchorDeferred, Candidates.Num());
    if (Census != LastLateCaptureCensus)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("LATE-NODES census: %s"), *Census);
        LastLateCaptureCensus = Census;
    }
    if (Candidates.IsEmpty()) { return; }

    // Stable actor-independent ordering; saved records make subsequent visits idempotent.
    Candidates.Sort([](const AFGResourceNodeBase& A, const AFGResourceNodeBase& B)
    {
        const FString AR = A.GetResourceClass()->GetPathName();
        const FString BR = B.GetResourceClass()->GetPathName();
        if (AR != BR) { return AR < BR; }
        const FVector AL = A.GetActorLocation(), BL = B.GetActorLocation();
        if (AL.X != BL.X) { return AL.X < BL.X; }
        if (AL.Y != BL.Y) { return AL.Y < BL.Y; }
        return AL.Z < BL.Z;
    });
    for (AFGResourceNodeBase* Node : Candidates) { MapLocations.Add(Node->GetActorLocation()); }

    int32 Added = 0, Pinned = 0, Inactive = 0, Relocated = 0;
    for (int32 Begin = 0; Begin < Candidates.Num();)
    {
        const FString ResourcePath = Candidates[Begin]->GetResourceClass()->GetPathName();
        int32 End = Begin + 1;
        while (End < Candidates.Num() && Candidates[End]->GetResourceClass()->GetPathName() == ResourcePath) { ++End; }
        TArray<AFGResourceNodeBase*> Free;
        TMap<AFGResourceNodeBase*, FString> Representatives;
        TArray<TEnumAsByte<EResourcePurity>> Purities;
        int32 GroupPinned = 0;
        for (int32 I = Begin; I < End; ++I)
        {
            AFGResourceNodeBase* Node = Candidates[I];
            AFGResourceNode* SolidNode = Cast<AFGResourceNode>(Node);
            if (Node->IsOccupied() || (SolidNode && IsNodeOccupiedAnyway(SolidNode)))
            {
                // Occupied discoveries are recorded in place, never hidden or retyped.
                FNodeShuffleEntry E;
                E.EntryGuid = FGuid::NewGuid();
                E.VanillaNodePath = Node->GetPathName();
                E.Location = E.OriginalTrueLocation = Node->GetActorLocation();
                E.Rotation = Node->GetActorRotation();
                E.NodeClassPath = Node->GetClass()->GetPathName();
                E.OriginalResourceClassPath = E.AssignedResourceClassPath = ResourcePath;
                E.OriginalPurity = E.AssignedPurity = SolidNode ? SolidNode->GetResourcePurity() : RP_Normal;
                E.ResourceForm = Node->GetResourceForm() == EResourceForm::RF_LIQUID ? 2 : 1;
                E.bPinned = true;
                Layout.Add(E);
                ++Pinned; ++Added; ++GroupPinned;
            }
            else
            {
                if (!SolidNode)
                {
                    FNodeShuffleEntry Cause;
                    Cause.AssignedResourceClassPath = ResourcePath;
                    Cause.ResourceForm = Node->GetResourceForm() == EResourceForm::RF_LIQUID ? 2 : 1;
                    const FString Representative = PickSubstituteClass(Node->GetClass()->GetPathName(), Cause);
                    UClass* Class = LoadClassByPath(Representative);
                    if (!Class || !Class->IsChildOf(AFGResourceNode::StaticClass())
                        || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
                    {
                        const FString WarningKey = Node->GetClass()->GetPathName() + TEXT("|") + ResourcePath;
                        if (!LateDeferredClassWarnings.Contains(WarningKey))
                        {
                            LateDeferredClassWarnings.Add(WarningKey);
                            UE_LOG(LogNodeShuffle, Warning,
                                TEXT("LATE-NODES deferred: original='%s' resource='%s' has no compatible node representative; original retained (retry on remaining scheduled scan or next load)"),
                                *Node->GetPathName(), *ResourcePath);
                        }
                        continue;
                    }
                    Representatives.Add(Node, Representative);
                }
                Free.Add(Node);
                const EResourcePurity Purity = SolidNode ? SolidNode->GetResourcePurity() : RP_Normal;
                Purities.Add(Purity == RP_Inpure || Purity == RP_Normal || Purity == RP_Pure ? Purity : RP_Normal);
            }
        }

        const int32 TotalOriginals = OriginalCounts.FindRef(ResourcePath) + Free.Num() + GroupPinned;
        const int32 Minimum = ResourcePath.StartsWith(TEXT("/Game/"))
            ? Config.MinNodesPerResource : Config.MinNodesPerModdedResource;
        const int32 Target = FMath::Max(FMath::Max(0, Minimum),
            FMath::RoundToInt(TotalOriginals * FMath::Clamp(Config.ActivePercent, 1, 100) / 100.0f));
        // Extra slots can be necessary when a brand-new resource has fewer originals than
        // its minimum. Do not alter the existing layout to supply those slots.
        const int32 Active = NodeShuffleLateNodePolicy::AdditionalActive(TotalOriginals,
            ActiveCounts.FindRef(ResourcePath), GroupPinned, Config.ActivePercent, Minimum);
        if (Free.IsEmpty()) { Begin = End; continue; } // occupied-only discoveries remain untouched
        // Independent stream: discovering a batch consumes none of the original roll's draws.
        FRandomStream Rng(static_cast<int32>(FCrc::StrCrc32(*ResourcePath) ^ static_cast<uint32>(SavedSeed)
            ^ static_cast<uint32>(OriginalCounts.FindRef(ResourcePath)) ^ 0x4c415445u));
        for (int32 I = Free.Num() - 1; I > 0; --I) { Free.Swap(I, Rng.RandRange(0, I)); }
        if (Config.RandomizePurity)
        {
            for (int32 I = Purities.Num() - 1; I > 0; --I) { Purities.Swap(I, Rng.RandRange(0, I)); }
        }

        TArray<FVector> Locations;
        TSet<int32> Underground;
        // Generate against the full known map, not the late batch's local bounding box.
        // Restore the saved deal box: an append must not alter later recovery of old entries.
        const FVector OldMin = DealBoundsMin, OldMax = DealBoundsMax;
        const float OldZ = DealMeanZ;
        if (Active > 0) { GenerateNewLocations(Rng, MapLocations, AvoidLocations, Active, Locations, &Underground); }
        DealBoundsMin = OldMin; DealBoundsMax = OldMax; DealMeanZ = OldZ;
        if (Locations.Num() != Active)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("LATE-NODES deferred: resource='%s' unoccupied=%d requiredLocations=%d generated=%d; originals retained, retry on remaining scheduled scan or next load"),
                *ResourcePath, Free.Num(), Active, Locations.Num());
            Begin = End;
            continue;
        }
        for (int32 I = 0; I < FMath::Max(Free.Num(), Active); ++I)
        {
            const bool bHasOriginal = I < Free.Num();
            AFGResourceNodeBase* Node = Free[I % Free.Num()];
            AFGResourceNode* SolidNode = Cast<AFGResourceNode>(Node);
            FNodeShuffleEntry E;
            E.EntryGuid = FGuid::NewGuid();
            E.VanillaNodePath = bHasOriginal ? Node->GetPathName() : FString();
            E.OriginalTrueLocation = bHasOriginal ? Node->GetActorLocation() : FVector::ZeroVector;
            E.NodeClassPath = Node->GetClass()->GetPathName();
            E.SpawnNodeClassPath = Representatives.FindRef(Node);
            E.OriginalResourceClassPath = bHasOriginal ? ResourcePath : FString();
            E.AssignedResourceClassPath = ResourcePath;
            E.OriginalPurity = SolidNode ? SolidNode->GetResourcePurity() : RP_Normal;
            E.AssignedPurity = Config.RandomizePurity ? Purities[I % Purities.Num()] : E.OriginalPurity;
            E.ResourceForm = Node->GetResourceForm() == EResourceForm::RF_LIQUID ? 2 : 1;
            E.bActive = I < Active;
            E.bIsNewNode = true; // retain inactive identities too; no actor is spawned for them
            E.Location = E.bActive ? Locations[I] : E.OriginalTrueLocation;
            E.Rotation = FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f);
            E.bUnderground = E.bActive && Underground.Contains(I);
            FNodeShuffleSuppressedOriginal Rec;
            Rec.VanillaNodePath = E.VanillaNodePath;
            Rec.Location = Rec.TrueLocation = E.OriginalTrueLocation;
            Rec.OriginalNodeClassPath = E.NodeClassPath;
            Rec.OriginalResourceClassPath = ResourcePath;
            Rec.bModdedOrigin = !ResourcePath.StartsWith(TEXT("/Game/"));
            if (bHasOriginal) { OriginalNodeRecord.Add(Rec); }
            Layout.Add(E);
            AvoidLocations.Add(E.Location);
            E.bActive ? ++Relocated : ++Inactive;
            ++Added;
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("LATE-NODES captured: resource='%s' found=%d pinned=%d relocated=%d hiddenOnly=%d originalsAfter=%d activeBefore=%d target=%d; existing layout retained"),
            *ResourcePath, End - Begin, GroupPinned, Active, FMath::Max(0, Free.Num() - Active),
            TotalOriginals, ActiveCounts.FindRef(ResourcePath), Target);
        Begin = End;
    }
    if (Added > 0)
    {
        bKnowledgeUnlockDone = false;
        KnowledgeDeferPasses = 0;
        bAutoAllowExtractorsDone = false;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("LATE-NODES appended: added=%d pinned=%d relocated=%d hiddenOnly=%d layout=%d hideRecords=%d seed=%d"),
            Added, Pinned, Relocated, Inactive, Layout.Num(), OriginalNodeRecord.Num(), SavedSeed);
    }
}
