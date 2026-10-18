#include "NodeShuffleSubsystem.h"
#include "NodeShuffleConfig.h"
#include "NodeShuffleNodeComponent.h"
#include "NodeShuffleResourceNode.h"
#include "NodeShuffleLateNodePolicy.h"
#include "EngineUtils.h"
#include "Misc/Crc.h"
#include "Resources/FGResourceDeposit.h"
#include "Resources/FGResourceDescriptor.h"

// RWG P2g (2026-10-18): the refresh timer polls this cheap schedule every pass. One bounded actor pass per
// ScanPeriodSeconds for the whole session (was two scans, t=0 and +60 s), until the session growth cap is reached.
void ANodeShuffleSubsystem::MaybeCaptureLateOriginalNodes()
{
    if (!HasAuthority() || !bLayoutGenerated || !bAdoptedRestoredNodes) { return; }
    if (NodeShuffleLateNodePolicy::GrowthCapReached(LateNodeTotalAddedSession, NodeShuffleLateNodePolicy::GrowthCapEntries))
    {
        if (!bLateNodeGrowthCapWarned)
        {
            bLateNodeGrowthCapWarned = true;
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("LATE-NODES growth cap reached: %d layout entries appended this session, cap %d; periodic late capture stopped until the next load (scans run: %d)"),
                LateNodeTotalAddedSession, NodeShuffleLateNodePolicy::GrowthCapEntries, LateNodeScansCompleted);
        }
        return;
    }
    const double Now = GetWorld()->GetTimeSeconds();
    if (!NodeShuffleLateNodePolicy::ScanDue(LateNodeScansCompleted, Now, LastLateNodeScanTime,
        NodeShuffleLateNodePolicy::ScanPeriodSeconds, LateNodeTotalAddedSession, NodeShuffleLateNodePolicy::GrowthCapEntries)) { return; }
    if (!FNodeShuffleConfigStruct::GetActiveConfig(this).IncludeModdedNodes) { return; }
    if (LateNodeScansCompleted == 0 && !IsWorldReadyForReroll()) { return; }

    const double Started = FPlatformTime::Seconds();
    const int32 LayoutBefore = Layout.Num();
    const int32 RecordsBefore = OriginalNodeRecord.Num();
    LateCaptureLinesThisScan = 0;
    LateCaptureUnlistedThisScan = 0;
    CaptureLateOriginalNodes();
    if (LateNodeScansCompleted == 0) { FirstLateNodeScanTime = Now; }
    LastLateNodeScanTime = Now; // every completed scan, empty or not, restarts the period
    ++LateNodeScansCompleted; // Count empty/deferred scans too: never create a faster-than-period retry loop.
    const int32 AddedEntries = Layout.Num() - LayoutBefore;
    LateNodeTotalAddedSession += AddedEntries;
    if (LateCaptureUnlistedThisScan > 0)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("LATE-CAPTURE: %d further captures this scan not listed (32 lines per scan); scan %d"),
            LateCaptureUnlistedThisScan, LateNodeScansCompleted);
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("LATE-NODES scan=%d addedEntries=%d addedHideRecords=%d totalAddedSession=%d layoutTotal=%d hideRecordsTotal=%d durationMs=%.1f elapsedSinceFirstS=%.0f"),
        LateNodeScansCompleted, AddedEntries, OriginalNodeRecord.Num() - RecordsBefore, LateNodeTotalAddedSession,
        Layout.Num(), OriginalNodeRecord.Num(), (FPlatformTime::Seconds() - Started) * 1000.0, Now - FirstLateNodeScanTime);
}

// Additional originals are dealt into new locations, without changing the existing draw.
// No mod names: each periodic scan admits any eligible resource node (vanilla late-placed nodes included, F3 KEEP).
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
                LogLateCapture(Node);
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
                                TEXT("LATE-NODES deferred: original='%s' resource='%s' has no compatible node representative; original retained (retried on every periodic scan; said once per class and resource)"),
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
            if (!LateDeferredLocationWarnings.Contains(ResourcePath)) // RWG P2g: latched; the scan repeats every period
            {
                LateDeferredLocationWarnings.Add(ResourcePath);
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("LATE-NODES deferred: resource='%s' unoccupied=%d requiredLocations=%d generated=%d; originals retained, retried on every periodic scan (said once per resource)"),
                    *ResourcePath, Free.Num(), Active, Locations.Num());
            }
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
            if (bHasOriginal) { OriginalNodeRecord.Add(Rec); LogLateCapture(Node); }
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

// RWG P2g: one line per captured original (pinned in place, or hidden + dealt). Bounded per scan; the remainder is counted.
void ANodeShuffleSubsystem::LogLateCapture(const AFGResourceNodeBase* Node)
{
    if (LateCaptureLinesThisScan >= 32) { ++LateCaptureUnlistedThisScan; return; }
    ++LateCaptureLinesThisScan;
    const FVector L = Node->GetActorLocation();
    UE_LOG(LogNodeShuffle, Display, TEXT("LATE-CAPTURE: class=%s path=%s at=(%.0f,%.0f,%.0f) scan=%d"),
        *Node->GetClass()->GetName(), *Node->GetPathName(), L.X, L.Y, L.Z, LateNodeScansCompleted + 1);
}

// RWG P2g visible-original census, part 1 (runs every pass BEFORE SuppressOriginalNodes; works every 12th pass).
// Walks up to 256 hide records round-robin. A record counts as visible again only when the SAME instance that
// SuppressOriginalNodes marked steady is resolved by path, passes the path-identity test, is unoccupied, and has its
// actor hidden flag cleared or its collision back on. Its steady mark is then removed, so the hide funnel in
// SuppressOriginalNodes (the existing hide path, nothing here hides) runs in full for it this same pass. Measured only.
void ANodeShuffleSubsystem::RehideVisibleOriginalsPre()
{
    ++RehideCensusPass;
    RehideSteadyUnhiddenLinesThisPass = 0;
    RehidePendingNodes.Reset();
    if ((RehideCensusPass % 12) != 1) { return; }
    RehideCensusChecked = RehideCensusUnresolved = RehideCensusIdentityReject = RehideCensusOccupied = 0;
    RehideCensusNotSteady = RehideCensusNewInstance = RehideCensusHiddenOk = 0;
    const int32 Records = OriginalNodeRecord.Num();
    if (Records == 0) { return; }
    constexpr int32 MaxChecksPerPass = 256;
    constexpr int32 MaxLinesPerPass = 32;
    constexpr float IdentityMatchCm = 300.0f; // mirrors AdoptMatchRadiusCm (Subsystem.cpp:191), as RWGHIDECHECK does
    if (RehideCensusCursor >= Records) { RehideCensusCursor = 0; }
    int32 Lines = 0, Unlisted = 0;
    const int32 ToCheck = FMath::Min(Records, MaxChecksPerPass);
    for (int32 K = 0; K < ToCheck; ++K)
    {
        const FNodeShuffleSuppressedOriginal& Rec = OriginalNodeRecord[RehideCensusCursor];
        RehideCensusCursor = (RehideCensusCursor + 1) % Records;
        ++RehideCensusChecked;
        AFGResourceNodeBase* Orig = FindOriginalBaseByPath(Rec.VanillaNodePath);
        if (!Orig) { ++RehideCensusUnresolved; continue; }
        if (!Rec.OriginalNodeClassPath.IsEmpty())
        {
            const UClass* Res = Orig->GetResourceClass();
            const FVector Anchor = Rec.TrueLocation.IsNearlyZero() ? Rec.Location : Rec.TrueLocation;
            if (Orig->GetClass()->GetPathName() != Rec.OriginalNodeClassPath || !Res || Res->GetPathName() != Rec.OriginalResourceClassPath
                || FVector::Dist(Orig->GetActorLocation(), Anchor) >= IdentityMatchCm) { ++RehideCensusIdentityReject; continue; }
        }
        const AFGResourceNode* AsNode = Cast<AFGResourceNode>(Orig);
        if (Orig->IsOccupied() || (AsNode && IsNodeOccupiedAnyway(AsNode))) { ++RehideCensusOccupied; continue; }
        const TWeakObjectPtr<AFGResourceNodeBase>* Steady = SteadyHiddenOriginals.Find(Rec.VanillaNodePath);
        if (!Steady) { ++RehideCensusNotSteady; continue; }
        if (Steady->Get() != Orig) { ++RehideCensusNewInstance; continue; } // a re-streamed actor: the funnel treats it as new
        const bool bActorHidden = Orig->IsHidden();
        const bool bCollision = Orig->GetActorEnableCollision();
        if (bActorHidden && !bCollision) { ++RehideCensusHiddenOk; continue; }
        SteadyHiddenOriginals.Remove(Rec.VanillaNodePath); // route it back through SuppressOriginalNodes' full hide funnel
        RehidePendingNodes.Add(Orig);
        if (Lines >= MaxLinesPerPass) { ++Unlisted; continue; }
        ++Lines;
        const int32* At = SteadyHiddenAtPass.Find(Rec.VanillaNodePath);
        const FVector L = Orig->GetActorLocation();
        UE_LOG(LogNodeShuffle, Display, TEXT("REHIDE-VISIBLE: class=%s path=%s at=(%.0f,%.0f,%.0f) passesSinceHidden=%d actorHidden=%d collisionOn=%d"),
            *Orig->GetClass()->GetName(), *Rec.VanillaNodePath, L.X, L.Y, L.Z, At ? RehideCensusPass - *At : -1,
            bActorHidden ? 1 : 0, bCollision ? 1 : 0);
    }
    if (Unlisted > 0)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("REHIDE-VISIBLE: %d further visible-again originals this pass not listed (32 lines per pass)"), Unlisted);
    }
}

// RWG P2g visible-original census, part 2 (runs every pass AFTER SuppressOriginalNodes; reports on census passes).
// rehidden = visible-again originals that now have the actor hidden flag set and collision off.
void ANodeShuffleSubsystem::RehideVisibleOriginalsPost()
{
    if ((RehideCensusPass % 12) != 1) { return; }
    int32 Rehidden = 0, Gone = 0;
    for (const TWeakObjectPtr<AFGResourceNodeBase>& W : RehidePendingNodes)
    {
        const AFGResourceNodeBase* N = W.Get();
        if (!N) { ++Gone; continue; }
        if (N->IsHidden() && !N->GetActorEnableCollision()) { ++Rehidden; }
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("REHIDE-CENSUS: pass=%d checked=%d visibleAgain=%d rehidden=%d unresolved=%d identityReject=%d occupied=%d notSteady=%d newInstance=%d hiddenOk=%d actorGone=%d records=%d steadyUnhiddenSinceCensus=%d"),
        RehideCensusPass, RehideCensusChecked, RehidePendingNodes.Num(), Rehidden, RehideCensusUnresolved, RehideCensusIdentityReject,
        RehideCensusOccupied, RehideCensusNotSteady, RehideCensusNewInstance, RehideCensusHiddenOk, Gone, OriginalNodeRecord.Num(),
        RehideSteadyUnhiddenSinceCensus);
    RehideSteadyUnhiddenSinceCensus = 0; // the field covers the passes since the previous census line
    RehidePendingNodes.Reset();
}
