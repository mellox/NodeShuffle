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
#include "Materials/MaterialInterface.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/PlayerController.h"

#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Engine/GameInstance.h"

#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceNodeBase.h"
#include "Resources/FGResourceDeposit.h"
#include "Resources/FGResourceDescriptor.h"
#include "Resources/FGItemDescriptor.h"
#include "Equipment/FGResourceScanner.h"
#include "Buildables/FGBuildableResourceExtractorBase.h"
#include "FGPortableMiner.h"
#include "FGWaterVolume.h"

namespace
{
    constexpr float TickIntervalSeconds = 5.0f;
    constexpr float SettleDistance = 25000.0f;      // 250 m: settle new nodes near players
    constexpr float ExtractorSnapDistance = 700.0f; // 7 m
    constexpr float PortableMinerSnapDistance = 1500.0f; // 15 m
    constexpr float MinNodeSpacing = 2500.0f;       // 25 m between pool locations
    constexpr int32 MinVanillaNodesForRoll = 50;    // world considered streamed in
    constexpr float RockResweepCooldownSeconds = 10.0f; // min gap between rock re-sweeps
    constexpr float RockSearchRange = 30000.0f;         // 300 m: in-range rocks are streamed
    constexpr float DefaultSpawnRadiusCm = 60000.0f;    // 600 m: spawn-on-discovery default
    // FIX A (land-only): when a settle raycast lands in water, nudge the node and
    // re-raycast a handful of expanding offset candidates, spawning at the first
    // LAND hit. Capped so a node fully surrounded by water defers instead of
    // spinning. Offsets are a spiral of growing radius (cm).
    constexpr int32 LandRelocationTries = 16;           // max offset candidates per settle
    constexpr float LandRelocationStepCm = 4000.0f;     // 40 m per ring of the spiral
    constexpr float LandRelocationMaxRadiusCm = 30000.0f; // 300 m max nudge
    // EResourceForm numeric values (mirror of EResourceForm in FGItemDescriptor.h).
    constexpr uint8 FormSolid = 1;
    constexpr uint8 FormLiquid = 2;
    // FIX 4: modded "advanced" nodes (AlkaLib lithium) can report a purity outside
    // {Impure,Normal,Pure}. Eligibility now admits them; normalize that out-of-range
    // value to RP_Normal everywhere the roll captures purity so the node shuffles
    // with a sane, mineable purity (and never feeds a bogus value into the deck).
    FORCEINLINE EResourcePurity NormalizePurity(EResourcePurity P)
    {
        return (P == RP_Inpure || P == RP_Normal || P == RP_Pure) ? P : RP_Normal;
    }
    // Crude oil descriptor + visual assets for liquid (Phase 2) donor fallback.
    const TCHAR* OilDescriptorPath = TEXT("/Game/FactoryGame/Resource/RawResources/CrudeOil/Desc_LiquidOil.Desc_LiquidOil_C");
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
        bRerollCheckDone = true; // a layout just rolled this session needs no re-roll check
    }
    else if (!bRerollCheckDone && IsWorldReadyForReroll())
    {
        // Decide the explicit re-roll only once the world has streamed in. Uses
        // the SHUFFLE-AWARE gate (IsWorldReadyForReroll), not the strict pristine-
        // vanilla gate: a previously-shuffled save has most originally-vanilla
        // nodes retyped or destroyed, so the strict 50+ /Game/ count never passes
        // on reload — the historical reason the re-roll never fired.
        //
        // Two triggers, in priority order:
        //   1. "Re-roll Now" one-shot toggle (RerollNow). Reliable and user-visible:
        //      always re-rolls regardless of seed, then clears itself.
        //   2. AllowReroll + a SeedOverride that differs from the saved seed. The
        //      legacy mechanism, kept for compatibility but fragile (SavedSeed is
        //      invisible to users).
        const bool bRerollNow = Config.RerollNow;
        const bool bSeedReroll = Config.AllowReroll && Config.SeedOverride != 0
                                 && Config.SeedOverride != SavedSeed;
        const TCHAR* Decision = bRerollNow ? TEXT("RE-ROLLING (Re-roll Now toggle)")
            : bSeedReroll ? TEXT("RE-ROLLING (seed changed)")
            : TEXT("no change (Re-roll Now off; seed matches saved seed or AllowReroll off)");
        UE_LOG(LogNodeShuffle, Display,
            TEXT("Re-roll check: RerollNow=%d AllowReroll=%d SeedOverride=%d SavedSeed=%d -> %s"),
            bRerollNow ? 1 : 0, Config.AllowReroll ? 1 : 0, Config.SeedOverride, SavedSeed, Decision);

        if (bRerollNow || bSeedReroll)
        {
            // Restore originals first so the rescan sees the true vanilla pool,
            // not the already-shuffled resources.
            RestoreOriginalsForReroll();
            const int32 RerollSeed = Config.SeedOverride != 0 ? Config.SeedOverride
                : static_cast<int32>(FPlatformTime::Cycles());
            UE_LOG(LogNodeShuffle, Display, TEXT("Re-rolling layout: seed %d -> %d"),
                SavedSeed, RerollSeed);
            RollLayout(RerollSeed, true);

            // Clear the one-shot toggle so it never loops. Do this only for the
            // RerollNow path; the seed mechanism self-limits via SavedSeed equality.
            if (bRerollNow)
            {
                const bool bCleared = ClearRerollNowFlag();
                UE_LOG(LogNodeShuffle, Display, TEXT("Re-roll Now flag cleared in config: %s"),
                    bCleared ? TEXT("yes") : TEXT("NO (could not write config; turn it off manually)"));
            }
        }
        bRerollCheckDone = true;
    }

    if (bLayoutGenerated)
    {
        ApplyLayout();
        if (!bDidInitialApply)
        {
            bDidInitialApply = true;
            LogLayoutSummary();
        }
        DiagnoseRocksNearPlayers();
    }
}

// ---------------------------------------------------------------- roll ----

bool ANodeShuffleSubsystem::IsWorldReadyForRoll() const
{
    // Solid-only readiness check (no liquid): the strict gate just needs proof the
    // world has streamed in, and solid nodes are the overwhelming majority.
    int32 Count = 0;
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        if (IsEligibleVanillaNode(*It, false, false) && ++Count >= MinVanillaNodesForRoll)
        {
            return true;
        }
    }
    return false;
}

bool ANodeShuffleSubsystem::IsWorldReadyForReroll() const
{
    // A shuffled save's originally-vanilla nodes are mostly retyped (no longer
    // /Game/) or destroyed, so the pristine-vanilla count is unreliable here.
    // Gate instead on having streamed in enough loaded resource nodes of ANY
    // kind, after a short post-load settle so the level has time to stream.
    if (GetWorld() && GetWorld()->GetTimeSeconds() < 8.0f)
    {
        return false;
    }
    int32 Count = 0;
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        if (IsValid(*It) && ++Count >= MinVanillaNodesForRoll)
        {
            return true;
        }
    }
    return false;
}

void ANodeShuffleSubsystem::RestoreOriginalsForReroll()
{
    // Before the re-roll rescans the vanilla pool, put each live non-new node back
    // to the resource class/purity it had at the FIRST roll (stored on the entry).
    // Otherwise the rescan reads the previously-shuffled resources and builds a
    // corrupted pool. Destroyed nodes cannot return — counted and logged.
    int32 Restored = 0;
    int32 MissingDestroyed = 0;
    int32 Unhidden = 0;
    for (const FNodeShuffleEntry& Entry : Layout)
    {
        if (Entry.bIsNewNode)
        {
            continue;
        }
        AFGResourceNode* Node = FindVanillaNodeByPath(Entry.VanillaNodePath);
        if (!IsValid(Node))
        {
            // Either not streamed in yet, or permanently destroyed in a PRE-LOSSLESS
            // save (those destructions are baked in and cannot be undone — the
            // comprehensive respawn in ApplyLayout repopulates them). Lossless
            // deactivation never reaches here: hidden nodes stay valid in the world.
            MissingDestroyed++;
            continue;
        }
        // LOSSLESS: un-hide any node this layout had reversibly deactivated, so the
        // re-roll rescan/restore sees a live, visible, mineable vanilla node again
        // (the destruction-era code could never do this — the node was gone).
        if (DeactivatedNodePaths.Contains(Entry.VanillaNodePath))
        {
            if (Node->IsHidden() || !Node->GetActorEnableCollision())
            {
                Node->SetActorEnableCollision(true);
                Node->SetActorHiddenInGame(false);
                if (AFGNodeMeshActor* MeshActor = FindMeshActorForNode(Node))
                {
                    MeshActor->SetActorHiddenInGame(false);
                    MeshActor->SetActorEnableCollision(true);
                }
                Unhidden++;
            }
        }
        UClass* OriginalClass = LoadClassByPath(Entry.OriginalResourceClassPath);
        const bool bClassWrong = OriginalClass && Node->GetResourceClass() != OriginalClass;
        const bool bPurityWrong = Entry.OriginalPurity != RP_MAX
                                  && Node->GetResourcePurity() != Entry.OriginalPurity;
        if (bClassWrong && OriginalClass)
        {
            UClass* Current = Node->GetResourceClass();
            Node->SetResourceClassOverride(OriginalClass);
            Node->OnResourceClassOverrideReplication.Broadcast(Node, Current, OriginalClass);
        }
        if (bPurityWrong)
        {
            Node->SetResourcePurityOverride(Entry.OriginalPurity);
        }
        if (bClassWrong || bPurityWrong)
        {
            Node->InitRadioactivity();
            Node->UpdateRadioactivity();
            Restored++;
        }
    }
    // The new layout (RollLayout, called right after this) will repopulate
    // DeactivatedNodePaths from scratch. Clear it now so a node this re-roll makes
    // active is no longer considered deactivated and is never re-hidden by a stale
    // entry. (Hidden nodes were un-hidden above for the ones that streamed in;
    // ApplyLayout reactivates any that stream in later via ReactivateVanillaNode,
    // which also drops them from the set.)
    DeactivatedNodePaths.Reset();

    UE_LOG(LogNodeShuffle, Display,
        TEXT("Re-roll restore: reverted %d live nodes to originals, un-hid %d reversibly-deactivated nodes; %d entries had no live node (destroyed in a PRE-LOSSLESS save or not yet streamed — comprehensive respawn repopulates these)"),
        Restored, Unhidden, MissingDestroyed);
}

bool ANodeShuffleSubsystem::ClearRerollNowFlag()
{
    const FConfigId ConfigId{"NodeShuffle", ""};
    const UWorld* World = GetWorld();
    if (!World || !World->GetGameInstance())
    {
        return false;
    }
    UConfigManager* ConfigManager = World->GetGameInstance()->GetSubsystem<UConfigManager>();
    if (!ConfigManager)
    {
        return false;
    }
    UConfigPropertySection* Root = ConfigManager->GetConfigurationRootSection(ConfigId);
    if (!Root)
    {
        return false;
    }
    TObjectPtr<UConfigProperty>* Found = Root->SectionProperties.Find(TEXT("RerollNow"));
    UConfigPropertyBool* BoolProp = Found ? Cast<UConfigPropertyBool>(Found->Get()) : nullptr;
    if (!BoolProp)
    {
        return false;
    }
    if (BoolProp->Value)
    {
        BoolProp->Value = false;
        // MarkDirty walks the live property's Outer chain to the save handler
        // (the live tree from GetConfigurationRootSection is properly outer'd by
        // SML's deserialization, unlike the CDO tree built in PostInitProperties).
        BoolProp->MarkDirty();
    }
    ConfigManager->MarkConfigurationDirty(ConfigId);
    ConfigManager->FlushPendingSaves();
    return true;
}

void ANodeShuffleSubsystem::RollLayout(int32 Seed, bool bIsReroll)
{
    const FNodeShuffleConfigStruct Config = FNodeShuffleConfigStruct::GetActiveConfig(this);
    FRandomStream Rng(Seed);

    // FIX 4: name exactly which modded node classes shuffle (and why the rest do
    // not). Once per class per session. This is how "lithium never shuffles" stops
    // being a guess.
    DiagnoseModdedNodeEligibility(Config.IncludeModdedNodes, Config.EnableExperimentalFeatures);

    // Carry occupied new-node entries through a re-roll: anything with a live
    // spawned actor that is occupied keeps its position/resource and is pinned.
    TArray<FNodeShuffleEntry> CarriedEntries;
    // LOSSLESS (build lossless-5): the old code tracked "destroyed" vanilla paths
    // here (inactive entries were Destroy()'d) and resurrected them as new-node
    // spawns on re-roll. With reversible deactivation nothing is destroyed — an
    // inactive entry is a HIDDEN, still-alive level node, so that resurrect would
    // DUPLICATE it. We removed it. The authoritative "is the live actor genuinely
    // missing?" test now lives in ApplyLayout (EnsureMissingVanillaRespawned),
    // which checks the live world and is overlap-guarded — it repopulates only
    // truly-gone nodes (pre-lossless saves) and never double-spawns a present one.
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

    // Portable-miner occupancy set, used by both the live scan and the reroll
    // per-node live occupancy re-check.
    TSet<const AFGResourceNode*> PortableMinerNodes;
    for (TActorIterator<AFGPortableMiner> It(GetWorld()); It; ++It)
    {
        if (It->mExtractResourceNode)
        {
            PortableMinerNodes.Add(It->mExtractResourceNode);
        }
    }

    // 1. Build the vanilla pool + the data that drives balance (per-resource
    //    counts, the purity multiset, the spawnable node class).
    //
    // CRITICAL (streaming determinism): on a RE-ROLL the world may be only
    // partially streamed in, so a live TActorIterator scan would miss rare
    // resources and produce a different, broken balance each time (a resource
    // could get ZERO active nodes despite its floor). The saved Layout holds
    // the COMPLETE original vanilla pool (resource/purity/location/path were
    // persisted at the initial roll), so on reroll we rebuild from it directly
    // and balance becomes identical every time, independent of streaming.
    //
    // The INITIAL roll has no Layout yet and IsWorldReadyForRoll already gated
    // full streaming, so it keeps the live scan.
    TArray<FVector> VanillaLocations;
    TMap<FString, int32> VanillaResourceCounts;     // descriptor path -> count
    TMap<FString, uint8> FormByResource;            // descriptor path -> EResourceForm value
    TArray<TEnumAsByte<EResourcePurity>> PurityDeckSource;
    FString SpawnableNodeClassPath;                 // a solid-node BP class (default)
    // Phase 2: the BP class of an actual liquid (oil) node, so spawned oil nodes
    // are the right node type for oil extractors. Empty when no oil node exists or
    // experimental features are off; liquid entries then fall back to the solid
    // class (still form-correct via the oil descriptor).
    FString SpawnableLiquidNodeClassPath;
    int32 VanillaCount = 0;

    TArray<FNodeShuffleEntry> NewLayout;

    if (bIsReroll)
    {
        // Rebuild the vanilla pool from the persisted entries (complete + stable),
        // not from a live scan that depends on what happened to stream in.
        for (const FNodeShuffleEntry& Old : Layout)
        {
            if (Old.bIsNewNode)
            {
                continue;
            }
            // Occupation/pinning still comes from the LIVE node when it is
            // streamed in; a node that is not currently streamed is treated as
            // not-occupied (default unpinned), exactly as directed.
            AFGResourceNode* Live = FindVanillaNodeByPath(Old.VanillaNodePath);
            const bool bOccupied = IsValid(Live)
                && (Live->IsOccupied() || PortableMinerNodes.Contains(Live));

            FNodeShuffleEntry E;
            E.EntryGuid = FGuid::NewGuid();
            E.bIsNewNode = false;
            E.VanillaNodePath = Old.VanillaNodePath;
            E.Location = Old.Location;
            E.Rotation = Old.Rotation;
            // Counts/floors/purity MUST come from the stored ORIGINAL values,
            // never from a live GetResourceClass() (which is the shuffled type
            // and may not even be streamed).
            E.OriginalResourceClassPath = Old.OriginalResourceClassPath;
            E.OriginalPurity = Old.OriginalPurity;
            E.AssignedResourceClassPath = Old.OriginalResourceClassPath;
            E.AssignedPurity = Old.OriginalPurity;
            E.ResourceForm = Old.ResourceForm;
            E.bPinned = bOccupied;
            E.bActive = true;
            E.NodeClassPath = Old.NodeClassPath;
            NewLayout.Add(E);

            VanillaLocations.Add(Old.Location);
            if (!Old.OriginalResourceClassPath.IsEmpty())
            {
                VanillaResourceCounts.FindOrAdd(Old.OriginalResourceClassPath)++;
                FormByResource.FindOrAdd(Old.OriginalResourceClassPath) =
                    Old.ResourceForm != 0 ? Old.ResourceForm : FormSolid;
            }
            if (Old.OriginalPurity != RP_MAX)
            {
                PurityDeckSource.Add(Old.OriginalPurity);
            }
            if (!Old.NodeClassPath.IsEmpty())
            {
                if (Old.ResourceForm == FormLiquid)
                {
                    if (SpawnableLiquidNodeClassPath.IsEmpty()) { SpawnableLiquidNodeClassPath = Old.NodeClassPath; }
                }
                else if (SpawnableNodeClassPath.IsEmpty())
                {
                    SpawnableNodeClassPath = Old.NodeClassPath;
                }
            }
            VanillaCount++;
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("Re-roll pool: rebuilt %d vanilla entries from saved layout (streaming-independent), %d resource kinds"),
            VanillaCount, VanillaResourceCounts.Num());

        // EXPERIMENTAL-FORM augment into the reroll (generic — covers oil AND gas):
        // the saved Layout was captured when experimental forms were excluded
        // (experimental off at the initial roll), so rebuilding the pool from it
        // NEVER includes liquid (oil) or gas (e.g. lithium) — a reroll with
        // experimental ON would still produce a solid-only layout. When experimental
        // is on, augment the pool with a LIVE scan of any eligible node whose form is
        // an experimental form (RF_LIQUID or RF_GAS) that the saved pool does not
        // already contain (path-deduplicated against the rebuilt pool). Each is added
        // as a real vanilla entry; because these entries are written into NewLayout
        // (which becomes the saved Layout), the NEXT reroll already finds them in the
        // saved pool — so the result stays deterministic AND keeps them from here on.
        //
        // Form byte: liquid stays FormLiquid (oil has no rock — decal path). Gas is
        // recorded as FormSolid: a gas node (lithium's BP_ResourdeNode_Alkali) carries
        // its OWN rock mesh, so it shuffles/visualizes exactly like a solid; only its
        // eligibility was gated behind experimental. Path-dedupe avoids re-adding any
        // experimental node that already has an entry from a prior augment.
        TSet<FString> ExistingPaths;
        for (const FNodeShuffleEntry& E : NewLayout)
        {
            if (!E.bIsNewNode && !E.VanillaNodePath.IsEmpty()) { ExistingPaths.Add(E.VanillaNodePath); }
        }
        if (Config.EnableExperimentalFeatures)
        {
            int32 ExperimentalAdded = 0;
            for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
            {
                AFGResourceNode* Node = *It;
                const EResourceForm LiveForm = Node->GetResourceForm();
                const bool bExpForm = (LiveForm == EResourceForm::RF_LIQUID) || (LiveForm == EResourceForm::RF_GAS);
                // Experimental-form-only live scan: solids are already in the rebuilt pool.
                if (!bExpForm || !IsEligibleVanillaNode(Node, Config.IncludeModdedNodes, true))
                {
                    continue;
                }
                const FString Path = Node->GetPathName();
                if (ExistingPaths.Contains(Path)) { continue; }
                ExistingPaths.Add(Path);
                const bool bOccupied = Node->IsOccupied() || PortableMinerNodes.Contains(Node);
                const uint8 EntryForm = (LiveForm == EResourceForm::RF_LIQUID) ? FormLiquid : FormSolid;

                FNodeShuffleEntry E;
                E.EntryGuid = FGuid::NewGuid();
                E.bIsNewNode = false;
                E.VanillaNodePath = Path;
                E.Location = Node->GetActorLocation();
                E.Rotation = Node->GetActorRotation();
                E.OriginalResourceClassPath = Node->GetResourceClass() ? Node->GetResourceClass()->GetPathName() : FString();
                E.OriginalPurity = NormalizePurity(Node->GetResourcePurity());
                E.AssignedResourceClassPath = E.OriginalResourceClassPath;
                E.AssignedPurity = E.OriginalPurity;
                E.ResourceForm = EntryForm;
                E.bPinned = bOccupied;
                E.bActive = true;
                E.NodeClassPath = Node->GetClass()->GetPathName();
                NewLayout.Add(E);

                VanillaLocations.Add(E.Location);
                if (!E.OriginalResourceClassPath.IsEmpty())
                {
                    VanillaResourceCounts.FindOrAdd(E.OriginalResourceClassPath)++;
                    FormByResource.FindOrAdd(E.OriginalResourceClassPath) = EntryForm;
                }
                if (E.OriginalPurity != RP_MAX) { PurityDeckSource.Add(E.OriginalPurity); }
                if (EntryForm == FormLiquid)
                {
                    if (SpawnableLiquidNodeClassPath.IsEmpty()) { SpawnableLiquidNodeClassPath = E.NodeClassPath; }
                }
                else if (SpawnableNodeClassPath.IsEmpty())
                {
                    SpawnableNodeClassPath = E.NodeClassPath;
                }
                VanillaCount++;
                ExperimentalAdded++;
            }
            UE_LOG(LogNodeShuffle, Display,
                TEXT("Experimental-form augment: experimental ON -> added %d live RF_LIQUID/RF_GAS nodes not in the saved pool to the reroll pool (captured into the layout for determinism)"),
                ExperimentalAdded);
        }
    }
    else
    {
        // INITIAL roll: live scan of the (fully streamed) vanilla pool.
        // Experimental forms (liquid oil AND gas, e.g. lithium) join only when the
        // experimental flag is on; the stable mod is solid-only.
        const bool bIncludeLiquid = Config.EnableExperimentalFeatures;
        for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
        {
            AFGResourceNode* Node = *It;
            if (!IsEligibleVanillaNode(Node, Config.IncludeModdedNodes, bIncludeLiquid))
            {
                continue;
            }
            const bool bOccupied = Node->IsOccupied() || PortableMinerNodes.Contains(Node);

            FNodeShuffleEntry E;
            E.EntryGuid = FGuid::NewGuid();
            E.bIsNewNode = false;
            E.VanillaNodePath = Node->GetPathName();
            E.Location = Node->GetActorLocation();
            E.Rotation = Node->GetActorRotation();
            E.OriginalResourceClassPath = Node->GetResourceClass() ? Node->GetResourceClass()->GetPathName() : FString();
            E.OriginalPurity = NormalizePurity(Node->GetResourcePurity());
            E.AssignedResourceClassPath = E.OriginalResourceClassPath;
            E.AssignedPurity = E.OriginalPurity;
            E.ResourceForm = (Node->GetResourceForm() == EResourceForm::RF_LIQUID) ? FormLiquid : FormSolid;
            E.bPinned = bOccupied;
            E.bActive = true;
            E.NodeClassPath = Node->GetClass()->GetPathName();
            NewLayout.Add(E);

            VanillaLocations.Add(Node->GetActorLocation());
            if (const UClass* Res = Node->GetResourceClass())
            {
                VanillaResourceCounts.FindOrAdd(Res->GetPathName())++;
                FormByResource.FindOrAdd(Res->GetPathName()) = E.ResourceForm;
            }
            PurityDeckSource.Add(E.OriginalPurity);
            if (E.ResourceForm == FormLiquid)
            {
                if (SpawnableLiquidNodeClassPath.IsEmpty()) { SpawnableLiquidNodeClassPath = Node->GetClass()->GetPathName(); }
            }
            else if (SpawnableNodeClassPath.IsEmpty())
            {
                SpawnableNodeClassPath = Node->GetClass()->GetPathName();
            }
            VanillaCount++;
        }
    }

    if (VanillaCount == 0 || VanillaResourceCounts.Num() == 0 || PurityDeckSource.Num() == 0)
    {
        UE_LOG(LogNodeShuffle, Warning, TEXT("Roll aborted: no eligible vanilla pool (count %d, kinds %d, purities %d)."),
            VanillaCount, VanillaResourceCounts.Num(), PurityDeckSource.Num());
        return;
    }

    // 2. New node locations: custom JSON wins, otherwise seeded generation.
    //
    // FIX 2 (overlap): build the COMPLETE set of locations a new node must NOT
    // land on. VanillaLocations covers the vanilla pool, but a new node could
    // still overlap (a) carried/pinned occupied nodes from a prior layout,
    // (b) any original-node-record spot, or (c) a live occupied/pinned node that
    // is not in the rebuilt pool. Union them all so generation spaces new nodes
    // away from every occupied/kept/original location, not just other new nodes.
    TArray<FVector> AvoidLocations = VanillaLocations;
    for (const FNodeShuffleEntry& E : CarriedEntries)
    {
        AvoidLocations.Add(E.Location);
    }
    for (const FNodeShuffleSuppressedOriginal& Rec : OriginalNodeRecord)
    {
        AvoidLocations.Add(Rec.Location);
    }
    // Live occupied/pinned resource nodes currently streamed in (covers nodes
    // mined by miners/extractors that may not be in the rebuilt saved pool).
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        AFGResourceNode* Live = *It;
        if (IsValid(Live) && (Live->IsOccupied() || PortableMinerNodes.Contains(Live)))
        {
            AvoidLocations.Add(Live->GetActorLocation());
        }
    }

    TArray<FVector> NewLocations;
    if (!ReadCustomLocationsJson(NewLocations))
    {
        GenerateNewLocations(Rng, VanillaLocations, AvoidLocations, FMath::Max(0, Config.NewNodeCount), NewLocations);
        WriteGeneratedLocationsJson(NewLocations);
    }

    // The vanilla entries are already in NewLayout (built above); append new-node
    // location entries to complete the pool.
    NewLayout.Reserve(NewLayout.Num() + NewLocations.Num() + CarriedEntries.Num());
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
    // Completability floor. Vanilla ores (/Game/) get MinPerResource; modded
    // resources get MinPerModded (default 2) so no shuffled type can collapse to
    // a single location, while the proportional shares still drive the rest. Set
    // MinNodesPerModdedResource=0 to restore the old "modded shuffles freely with
    // no minimum" behavior.
    const int32 MinPerModded = FMath::Max(0, Config.MinNodesPerModdedResource);
    const auto FloorFor = [&](const FString& K){ return K.StartsWith(TEXT("/Game/")) ? MinPerResource : MinPerModded; };
    const int32 VanillaKindCount = static_cast<int32>(Algo::CountIf(ResourceKinds,
        [](const FString& K){ return K.StartsWith(TEXT("/Game/")); }));
    const int32 ModdedKindCount = ResourceKinds.Num() - VanillaKindCount;
    int32 TargetActive = FMath::RoundToInt(PoolSize * FMath::Clamp(Config.ActivePercent, 1, 100) / 100.0f);
    // Budget enough active slots to satisfy every per-kind floor (vanilla + modded),
    // so the trim loop never has to fight the floors.
    TargetActive = FMath::Max3(TargetActive,
        MinPerResource * VanillaKindCount + MinPerModded * ModdedKindCount, PinnedCount);
    if (!Config.AllowVanillaDisappear)
    {
        TargetActive = FMath::Max(TargetActive, VanillaCount);
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
        const float Share = static_cast<float>(VanillaResourceCounts[Kind]) / VanillaCount;
        const int32 KindFloor = FloorFor(Kind);
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
            const int32 Floor = FMath::Max(FloorFor(Kind), PinnedPerResource.FindRef(Kind));
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
    //
    // FIX B (form-consistency): a card's resource FORM (solid vs liquid) must match
    // the SLOT it is dealt into, or we get nonsense like an oil descriptor on a
    // quartz rock (ROCKDIAG: orig=Desc_RawQuartz_C assigned=Desc_LiquidOil_C). Slots:
    //   - existing-solid vanilla node  -> SOLID cards only (its rock IS solid)
    //   - existing-liquid vanilla node -> LIQUID cards only (vanilla oil location)
    //   - NEW node                     -> either form (it spawns from the matching
    //                                     node class via the Phase-2 stamp below),
    //                                     but still vanilla-/Game/ only for visuals.
    // So we PARTITION the deck by form and deal each slot from the matching pile,
    // preserving the per-resource quotas/floors already computed above.
    const auto CardFormOf = [&](const FString& Card) -> uint8
    {
        const uint8 F = FormByResource.FindRef(Card);
        return F != 0 ? F : FormSolid;
    };

    TArray<FString> SolidDeck;
    TArray<FString> LiquidDeck;
    for (const FString& Kind : ResourceKinds)
    {
        const int32 DeckCount = Quota[Kind] - PinnedPerResource.FindRef(Kind);
        TArray<FString>& Pile = (CardFormOf(Kind) == FormLiquid) ? LiquidDeck : SolidDeck;
        for (int32 i = 0; i < DeckCount; i++) { Pile.Add(Kind); }
    }
    for (int32 i = SolidDeck.Num() - 1; i > 0; i--)  { SolidDeck.Swap(i, Rng.RandRange(0, i)); }
    for (int32 i = LiquidDeck.Num() - 1; i > 0; i--) { LiquidDeck.Swap(i, Rng.RandRange(0, i)); }

    // Purity deck: vanilla purity multiset, sized to the FULL card count (both piles).
    const int32 TotalCards = SolidDeck.Num() + LiquidDeck.Num();
    TArray<TEnumAsByte<EResourcePurity>> PurityDeck;
    for (int32 i = 0; i < TotalCards; i++)
    {
        PurityDeck.Add(PurityDeckSource[i % PurityDeckSource.Num()]);
    }
    for (int32 i = PurityDeck.Num() - 1; i > 0; i--)
    {
        PurityDeck.Swap(i, Rng.RandRange(0, i));
    }

    // Per-pile draw cursors + a shared purity cursor.
    int32 SolidIdx = 0, LiquidIdx = 0, PurityIdx = 0;
    const auto DrawSolid = [&]() -> FString
    {
        return SolidIdx < SolidDeck.Num() ? SolidDeck[SolidIdx] : FString();
    };
    const auto DrawLiquid = [&]() -> FString
    {
        return LiquidIdx < LiquidDeck.Num() ? LiquidDeck[LiquidIdx] : FString();
    };

    for (FNodeShuffleEntry& E : NewLayout)
    {
        if (!E.bActive || E.bPinned)
        {
            continue;
        }

        // Determine the SLOT's required form.
        //   existing vanilla node -> fixed by the node we are retyping (E.ResourceForm)
        //   new node              -> flexible (prefer liquid if liquid cards remain
        //                            AND it can host them, else solid)
        FString Card;
        if (!E.bIsNewNode)
        {
            const uint8 SlotForm = E.ResourceForm != 0 ? E.ResourceForm : FormSolid;
            if (SlotForm == FormLiquid)
            {
                Card = DrawLiquid();
                if (!Card.IsEmpty()) { LiquidIdx++; }
            }
            else
            {
                Card = DrawSolid();
                if (!Card.IsEmpty()) { SolidIdx++; }
            }
            if (Card.IsEmpty())
            {
                // No matching-form card left for this slot: keep its vanilla
                // assignment rather than force a form-mismatched card onto it.
                continue;
            }
        }
        else
        {
            // NEW node. It must carry a VANILLA (/Game/) resource (guaranteed donor
            // look; item/modded cards render nothing on a brand-new node). It can be
            // solid OR liquid — a liquid new node spawns from the liquid node class
            // (Phase-2 stamp below). Prefer a vanilla solid card (the common case);
            // fall back to a vanilla liquid card so oil can populate new nodes too.
            // A liquid card on a SOLID existing slot is what we are avoiding — never
            // happens here because existing slots are handled above.
            auto FindVanilla = [&](TArray<FString>& Pile, int32& Idx) -> FString
            {
                for (int32 j = Idx; j < Pile.Num(); j++)
                {
                    if (Pile[j].StartsWith(TEXT("/Game/")))
                    {
                        Pile.Swap(Idx, j);
                        return Pile[Idx];
                    }
                }
                return FString();
            };
            Card = FindVanilla(SolidDeck, SolidIdx);
            if (!Card.IsEmpty())
            {
                SolidIdx++;
            }
            else
            {
                Card = FindVanilla(LiquidDeck, LiquidIdx);
                if (!Card.IsEmpty()) { LiquidIdx++; }
            }
            if (Card.IsEmpty())
            {
                E.bActive = false; // no vanilla cards of any form left: skip this new node
                continue;
            }
        }

        E.AssignedResourceClassPath = Card;
        // Record the slot's resulting form on the entry. For existing nodes this
        // matches the node we retype; for new nodes it drives the node-class stamp.
        E.ResourceForm = CardFormOf(Card);
        if (Config.RandomizePurity || E.bIsNewNode)
        {
            E.AssignedPurity = PurityDeck[PurityIdx % PurityDeck.Num()];
        }
        PurityIdx++;
    }

    // FIX B verification summary: by construction NO liquid card ever lands on a
    // solid slot (existing-solid slots draw only from SolidDeck; existing-liquid
    // slots only from LiquidDeck; new nodes accept either but spawn the matching
    // node class). Log the form distribution so the log proves it.
    {
        int32 LiquidExisting = 0, LiquidNew = 0, SolidExisting = 0, SolidNew = 0;
        for (const FNodeShuffleEntry& E : NewLayout)
        {
            if (!E.bActive || E.bPinned) { continue; }
            const bool bLiq = E.ResourceForm == FormLiquid;
            if (E.bIsNewNode) { bLiq ? LiquidNew++ : SolidNew++; }
            else              { bLiq ? LiquidExisting++ : SolidExisting++; }
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("FIX B oil form: dealt solid[existing %d, new %d] liquid[existing %d, new %d]; solid-deck %d liquid-deck %d (no liquid card on a solid slot by construction)"),
            SolidExisting, SolidNew, LiquidExisting, LiquidNew, SolidDeck.Num(), LiquidDeck.Num());
    }

    // Phase 2: now that every active new node has its resource, stamp its form and
    // choose the correct node BP class to spawn from. A new oil (RF_LIQUID) node
    // must spawn from a liquid node class so oil extractors accept it; solid nodes
    // keep the solid class. Liquid only ever appears here when the experimental
    // flag was on at scan time (oil was eligible) — otherwise FormByResource has no
    // liquid entries and this is a no-op for the stable, solid-only shuffle.
    for (FNodeShuffleEntry& E : NewLayout)
    {
        if (!E.bIsNewNode || !E.bActive || E.bPinned || E.AssignedResourceClassPath.IsEmpty())
        {
            continue;
        }
        const uint8 Form = FormByResource.FindRef(E.AssignedResourceClassPath);
        E.ResourceForm = Form != 0 ? Form : FormSolid;
        if (E.ResourceForm == FormLiquid && !SpawnableLiquidNodeClassPath.IsEmpty())
        {
            E.NodeClassPath = SpawnableLiquidNodeClassPath;
        }
        else if (E.NodeClassPath.IsEmpty())
        {
            E.NodeClassPath = SpawnableNodeClassPath;
        }
    }

    // FIX 3 (old resurrect) REMOVED for build lossless-5: it converted active
    // entries whose path was "destroyed in a past roll" into new-node spawns. With
    // lossless deactivation no vanilla node is ever destroyed, so that signal is
    // gone and the conversion would duplicate a still-alive hidden node. The
    // genuinely-missing case (pre-lossless saves) is handled at apply time by
    // EnsureMissingVanillaRespawned, which tests the LIVE world and is overlap-
    // guarded — repopulating real holes without ever double-spawning a present node,
    // and keeping the entry's vanilla identity (so re-roll balance stays stable).

    NewLayout.Append(CarriedEntries);
    Layout = MoveTemp(NewLayout);
    SavedSeed = Seed;
    bLayoutGenerated = true;
    bDidInitialApply = false;

    // INCREMENTAL DONOR CAPTURE (visual-6): the assigned-resource set changed, so
    // rebuild the donor target set and re-open coverage on the next apply tick.
    // Donors already captured stay valid (same resources keep the same look).
    bDonorTargetsBuilt = false;
    bAllDonorsCovered = false;
    AssignedResourcesNeedingDonor.Reset();

    // Refresh the persistent original-node record from the new layout so
    // SuppressOriginalNodes (wipe-on-reroll) can hide every original spot that the
    // new layout no longer keeps, reliably on stream-in and across sessions.
    CaptureOriginalNodeRecord();

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
        Seed, PoolSize, VanillaCount, NewLocations.Num(), ActiveCount, PinnedCount);
}

void ANodeShuffleSubsystem::GenerateNewLocations(FRandomStream& Rng, const TArray<FVector>& VanillaLocations,
                                                 const TArray<FVector>& AvoidLocations,
                                                 int32 Count, TArray<FVector>& OutLocations) const
{
    if (VanillaLocations.Num() == 0 || Count <= 0)
    {
        return;
    }

    // MAP-WIDE SPREAD: the previous version anchored each new spot to a randomly
    // chosen vanilla node and offset it 50-150 m, so on the initial roll (only the
    // load-point cluster streamed in) every new node bunched around the player. The
    // saved vanilla set, by contrast, spans the whole playable map (the initial roll
    // is gated on full streaming, and on reroll the pool is rebuilt from saved
    // entries that cover the map). Distribute candidates uniformly across the
    // bounding box of that full known-node set instead — true map-wide spread,
    // independent of what happens to be streamed in.
    FBox Bounds(ForceInit);
    for (const FVector& V : VanillaLocations)
    {
        Bounds += V;
    }
    // Inset slightly from the extreme node positions so we don't push new nodes
    // past the edge of the explorable terrain (vanilla nodes already sit inside it).
    const float Inset = MinNodeSpacing; // 25 m
    const FVector Min = Bounds.Min + FVector(Inset, Inset, 0.f);
    const FVector Max = Bounds.Max - FVector(Inset, Inset, 0.f);
    // Z is irrelevant for placement (RaycastSettle drops each node onto terrain at
    // spawn time); use the mean vanilla Z as a sane starting height for the ray.
    double ZSum = 0.0;
    for (const FVector& V : VanillaLocations) { ZSum += V.Z; }
    const float MeanZ = static_cast<float>(ZSum / VanillaLocations.Num());

    if (Max.X <= Min.X || Max.Y <= Min.Y)
    {
        UE_LOG(LogNodeShuffle, Warning, TEXT("Map-wide spread: degenerate node bounds, cannot distribute new locations."));
        return;
    }

    int32 Attempts = Count * 60;
    while (OutLocations.Num() < Count && Attempts-- > 0)
    {
        FVector Candidate(Rng.FRandRange(Min.X, Max.X), Rng.FRandRange(Min.Y, Max.Y), MeanZ);

        // FIX 2: reject candidates within min-spacing of ANY occupied/kept/
        // original location (AvoidLocations = vanilla pool + carried/pinned +
        // original-node record + live occupied nodes), not just the vanilla pool.
        bool bTooClose = false;
        for (const FVector& Existing : AvoidLocations)
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

    UE_LOG(LogNodeShuffle, Display,
        TEXT("Map-wide spread: distributed %d/%d new locations across bounds X[%.0f..%.0f] Y[%.0f..%.0f] (%.1f x %.1f km)"),
        OutLocations.Num(), Count, Min.X, Max.X, Min.Y, Max.Y,
        (Max.X - Min.X) / 100000.0f, (Max.Y - Min.Y) / 100000.0f);
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

    const FNodeShuffleConfigStruct Config = FNodeShuffleConfigStruct::GetActiveConfig(this);
    // Spawn-on-discovery radius (config metres -> cm). Clamp to a sane floor so a
    // mis-set 0 never disables all spawning.
    const float SpawnRadiusCm = FMath::Max(10000.f, static_cast<float>(Config.SpawnRadiusMeters) * 100.f);

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

    // Capture each resource's authentic rock look (mesh+materials+world scale)
    // from the pre-retype world, used to re-skin retyped nodes AND dress new
    // nodes. Cheap, read-only, always on (this is now a stable feature).
    SweepRockComponents();
    // FIX 2: rebuild donors from the SaveGame store FIRST so a reloaded shuffled
    // save re-stamps the CORRECT rock (every live vanilla node is already retyped,
    // so a pristine live capture is impossible). Then CaptureDonorVisuals still
    // captures any not-yet-persisted resource live (first-ever roll).
    RehydrateDonorsFromPersisted();
    // DONOR-HYGIENE: before trusting rehydrated donors, enforce the one-mesh-one-
    // resource invariant on them. A SaveGame donor written by an older build may be
    // polluted (its mesh is really a shared placeholder); revalidation discards such
    // records and seeds RealDonorMeshOwner from the surviving (clean) donors so the
    // live capture below never lets a placeholder claim a mesh a real rock owns.
    RevalidatePersistedDonors();
    CaptureDonorVisuals();

    for (FNodeShuffleEntry& Entry : Layout)
    {
        if (Entry.bIsNewNode)
        {
            // SPAWN-ON-DISCOVERY: only attempt to materialize an active new node
            // when a player is within SpawnRadius of it. Far, undiscovered nodes
            // stay as pure data until you explore to them — at which point
            // EnsureNewNodeSpawned raycasts the (now-streamed) terrain and places
            // the node flush on the ground. This is what keeps new nodes from ever
            // floating/overlapping in unloaded regions.
            if (Entry.bActive && IsLocationNearAnyPlayer(Entry.Location, SpawnRadiusCm))
            {
                EnsureNewNodeSpawned(Entry, bChangedWorld);
            }
            continue;
        }

        // Inactive vanilla nodes: REVERSIBLY deactivated (hidden, not destroyed).
        // The hide is runtime-only — it reverts on every reload AND whenever the
        // node/rock streams back in — so re-apply it every tick. The live node is
        // passed through (now usually still valid, since lossless deactivation
        // keeps it in the world).
        if (!Entry.bActive && !Entry.bPinned)
        {
            ApplyVanillaDeactivate(Entry, FindVanillaNodeByPath(Entry.VanillaNodePath), bChangedWorld);
            continue;
        }

        AFGResourceNode* Node = FindVanillaNodeByPath(Entry.VanillaNodePath);

        // REACTIVATION (task #2): this entry is ACTIVE now, but its node may have
        // been deactivated (hidden) in a PRIOR layout — either it is still tracked
        // in DeactivatedNodePaths, or (same-session re-roll, where the set was
        // reset) the live actor is simply still hidden from the prior layout's
        // runtime hide. Either way, un-hide + re-enable it in place before retyping.
        // Lossless: the actor is alive because we never destroyed it.
        if (DeactivatedNodePaths.Contains(Entry.VanillaNodePath)
            || (IsValid(Node) && (Node->IsHidden() || !Node->GetActorEnableCollision())))
        {
            ReactivateVanillaNode(Entry, Node, bChangedWorld);
        }

        // COMPREHENSIVE RESPAWN (task #3): an ACTIVE entry whose live actor is
        // genuinely missing — never streams in (destroyed by a pre-lossless build,
        // or otherwise gone). Materialize a managed node from the recorded data so
        // even an already-degraded save repopulates. Gate on spawn-on-discovery so
        // we don't act on a node that is merely not-yet-streamed. If the real node
        // later streams in, the path lookup below matches it and this never fires
        // (the overlap guard inside also prevents a duplicate).
        if (!Node)
        {
            if (!Entry.bPinned && IsLocationNearAnyPlayer(Entry.Location, SpawnRadiusCm))
            {
                EnsureMissingVanillaRespawned(Entry, bChangedWorld);
            }
            continue; // no live level actor to retype this tick
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

        if (Entry.AssignedResourceClassPath != Entry.OriginalResourceClassPath
              || Entry.AssignedPurity != Entry.OriginalPurity)
        {
            ApplyVanillaRetype(Entry, Node, bChangedWorld);
        }
    }

    SettleNewNodesNearPlayers();
    ReassociateOrphanedExtractors();

    // WIPE-ON-REROLL: re-hide every ORIGINAL vanilla node + its rock whenever it
    // streams in, unless that spot is now occupied/pinned or reused by the active
    // new layout. Driven by the persistent OriginalNodeRecord so it is reliable
    // across sessions and stream-ins. This is the clean-slate guarantee and also
    // the catch-all for orphan-rock ghosts.
    SuppressOriginalNodes();

    OrphanRockCleanup();

    // FIX 1: the far-node despawn is DISABLED. Despawning materialized far nodes
    // "back to data" did not reliably respawn them — it caused DISAPPEARED nodes,
    // ONLY-VISUAL ghosts (mesh actor left behind), and vanished modded nodes.
    // Satisfactory handles thousands of nodes fine, so once a new node
    // materializes we KEEP it for the rest of the session (DespawnFarNewNodes
    // was removed entirely).

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

    // DATA (SaveGame override on the node — persists across reloads): apply only
    // when actually wrong, so reloads no-op here. This is the rock-solid core.
    if (!bClassRight)
    {
        Node->SetResourceClassOverride(AssignedClass);
        Node->OnResourceClassOverrideReplication.Broadcast(Node, OriginalClass, AssignedClass);
    }
    if (!bPurityRight)
    {
        Node->SetResourcePurityOverride(Entry.AssignedPurity);
    }
    if (!bClassRight || !bPurityRight)
    {
        // Keep radiation truthful in both directions: nodes retyped TO uranium
        // start irradiating, nodes retyped AWAY from it stop. (Friend access.)
        Node->InitRadioactivity();
        Node->UpdateRadioactivity();
        UE_LOG(LogNodeShuffle, Verbose, TEXT("Retyped %s: %s -> %s"),
            *Node->GetName(), *OriginalClass->GetName(), *AssignedClass->GetName());
        bOutChangedWorld = true;
    }

    // VISUAL (runtime mesh swap — does NOT persist, and reverts to the node's
    // original rock whenever the node streams back in): re-skin whenever the
    // rock is not already the donor mesh. Idempotent and self-healing across
    // reloads and streaming, where the early-return on data state used to skip
    // it entirely (leaving retyped nodes wearing their old rock after a reload).
    const FDonorVisual* Donor = DonorVisuals.Find(Entry.AssignedResourceClassPath);
    if (!Donor)
    {
        return; // no known look for this resource yet → leave the node's original
    }
    if (Donor->Kind == EDonorKind::Liquid)
    {
        // Distinguish a GENUINE liquid (oil) from a SOLID resource that only got a
        // "native rebuild" fallback because no mesh donor was ever found. Only a
        // genuine liquid may hide the node's existing rocks (it renders a decal
        // instead); for a solid fallback we MUST NOT hide the rocks — that is the
        // SAM "invisible but minable" bug (FIX 3). Form is read from the descriptor.
        const bool bGenuineLiquid =
            UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(AssignedClass)) == EResourceForm::RF_LIQUID;

        // Always rebuild the native visual from the (overridden) descriptor: for oil
        // this builds the puddle decal; for a solid fallback it lets the game draw
        // whatever the descriptor defines (a mesh, if the descriptor has one).
        RebuildNodeNativeVisual(Node);

        if (bGenuineLiquid)
        {
            // An ore node retyped to oil must lose BOTH possible solid rocks: its
            // swept separate rock AND any AFGNodeMeshActor. Oil has no mesh.
            if (const TWeakObjectPtr<UStaticMeshComponent>* Rock = SweptRocks.Find(Entry.EntryGuid))
            {
                if (UStaticMeshComponent* RockSmc = Rock->Get())
                {
                    if (RockSmc->IsVisible()) { RockSmc->SetVisibility(false, true); }
                }
            }
            if (AFGNodeMeshActor* MeshActor = FindMeshActorForNode(Node))
            {
                if (UStaticMeshComponent* MaSmc = MeshActor->GetStaticMeshComponent())
                {
                    if (MaSmc->IsVisible()) { MaSmc->SetVisibility(false, true); }
                }
            }
            UE_LOG(LogNodeShuffle, Verbose, TEXT("Re-skinned %s to LIQUID (oil decal) %s"),
                *Node->GetName(), *AssignedClass->GetName());
        }
        else
        {
            // FIX 3: solid resource, no mesh donor. Keep every existing visual VISIBLE
            // — the node must always show something even if it is the node's original
            // rock. Never hide here.
            UE_LOG(LogNodeShuffle, Verbose,
                TEXT("Native-rebuilt %s to solid %s (no mesh donor — kept existing visual, NOT hidden)"),
                *Node->GetName(), *AssignedClass->GetName());
        }
        bOutChangedWorld = true;
        return;
    }
    if (!Donor->Mesh.Get())
    {
        return; // donor recorded but mesh stale → leave original this tick
    }
    UStaticMeshComponent* Smc = Node->FindComponentByClass<UStaticMeshComponent>();
    if (!Smc)
    {
        const TWeakObjectPtr<UStaticMeshComponent>* Rock = SweptRocks.Find(Entry.EntryGuid);
        Smc = Rock ? Rock->Get() : nullptr;
    }
    if (Smc && Smc->GetStaticMesh() == Donor->Mesh.Get())
    {
        return; // already wearing the right rock
    }
    // FIX B (one visual per node — kill double rocks): re-skin via EXACTLY ONE
    // mechanism. ApplyNodeOwnMesh stamps the faithful donor copy onto the node's
    // single rock component (its own mesh, or — for ore-style nodes — its one
    // swept separate rock). The OLD code ALSO unconditionally called the game's
    // OverrideMeshAndMaterials on FindMeshActorForNode, which for ore nodes targets
    // a *second* visual (the AFGNodeMeshActor) and/or fights our stamp on the same
    // actor — producing the reported two-rocks-at-one-location bug. Now we only fall
    // back to OverrideMeshAndMaterials when the donor stamp could NOT be applied,
    // and we make sure the OTHER candidate visual is hidden so a node shows one rock.
    const bool bMeshSwapped = ApplyNodeOwnMesh(Node, AssignedClass, Entry);
    AFGNodeMeshActor* MeshActor = FindMeshActorForNode(Node);
    UStaticMeshComponent* StampedSmc = Node->FindComponentByClass<UStaticMeshComponent>();
    if (!StampedSmc)
    {
        const TWeakObjectPtr<UStaticMeshComponent>* Rock = SweptRocks.Find(Entry.EntryGuid);
        StampedSmc = Rock ? Rock->Get() : nullptr;
    }
    if (bMeshSwapped)
    {
        // FIX 3 (never leave a node invisible): only hide a SEPARATE mesh actor when
        // (a) it is a genuine duplicate of THIS node's visual (not the component we
        // just stamped) AND (b) the visual we applied is a REAL donor — a correct
        // rock. If we only had a FALLBACK donor (deposit mesh / native rebuild), we
        // keep the other mesh visible so the node never ends up with no rock at all
        // (the reported "visual removed but still minable" over-hide). A node must
        // always show something.
        if (MeshActor && !Donor->bIsFallback)
        {
            UStaticMeshComponent* MaSmc = MeshActor->GetStaticMeshComponent();
            if (MaSmc && MaSmc != StampedSmc && MaSmc->IsVisible())
            {
                MaSmc->SetVisibility(false, true);
                MaSmc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                UE_LOG(LogNodeShuffle, Verbose, TEXT("Hid duplicate mesh-actor rock on retyped %s"), *Node->GetName());
            }
        }
        UE_LOG(LogNodeShuffle, Verbose, TEXT("Re-skinned %s to %s%s"), *Node->GetName(), *AssignedClass->GetName(),
            Donor->bIsFallback ? TEXT(" (fallback look)") : TEXT(""));
        bOutChangedWorld = true;
    }
    else if (MeshActor)
    {
        // No donor mesh stamped (donor not yet captured this tick): fall back to the
        // game's own override so the node is at least not wearing its old resource.
        MeshActor->OverrideMeshAndMaterials(Node, OriginalClass, AssignedClass);
    }
}

void ANodeShuffleSubsystem::ApplyVanillaDeactivate(FNodeShuffleEntry& Entry, AFGResourceNode* Node, bool& bOutChangedWorld)
{
    // LOSSLESS DEACTIVATION (architectural change, build lossless-5):
    //
    // The OLD code called Node->Destroy() here. Satisfactory PERSISTS destruction
    // of persistent-level actors, so every re-roll permanently removed the inactive
    // vanilla nodes — over many re-rolls the world emptied out (the proven root
    // cause). We now make the node reversibly INACTIVE instead:
    //   - hidden in game + collision disabled (so it can't be seen, mined or
    //     placed-on), re-applied idempotently every tick because that runtime
    //     state reverts on reload AND whenever the actor streams back in;
    //   - tracked in DeactivatedNodePaths (SaveGame) so a later re-roll can fully
    //     reactivate it (ReactivateVanillaNode) — nothing is ever lost.
    //
    // SCANNER CONSISTENCY: a hidden + collision-disabled node is removed from the
    // Resource Manager because (a) ApplyLayout calls RefreshScannersAndRadarTowers
    // whenever the world changed, forcing every scanner to rebuild its node
    // clusters, and (b) the persistent SuppressOriginalNodes pass also keeps these
    // spots hidden on stream-in. We deliberately do NOT alter the node's resource
    // class/purity override here: leaving the data untouched is what makes
    // reactivation a clean, lossless restore.
    DeactivatedNodePaths.Add(Entry.VanillaNodePath);

    if (IsValid(Node))
    {
        bool bChanged = false;
        // Never hide an occupied node out from under a miner/extractor the player
        // built (occupancy wins over the rolled layout, same rule as elsewhere).
        if (!IsNodeOccupiedAnyway(Node))
        {
            if (Node->GetActorEnableCollision()) { Node->SetActorEnableCollision(false); bChanged = true; }
            if (!Node->IsHidden()) { Node->SetActorHiddenInGame(true); bChanged = true; }
            if (AFGNodeMeshActor* MeshActor = FindMeshActorForNode(Node))
            {
                MeshActor->SetActorHiddenInGame(true);
                MeshActor->SetActorEnableCollision(false);
            }
        }
        if (bChanged)
        {
            UE_LOG(LogNodeShuffle, Verbose, TEXT("Deactivated %s (hidden, reversible — NOT destroyed)"),
                *Entry.VanillaNodePath);
            bOutChangedWorld = true;
        }
    }

    // VISUAL (runtime-only — reverts on reload AND whenever the rock streams
    // back in): re-hide the paired separate ore-rock whenever it is present and
    // currently shown. Idempotent: once hidden this is a no-op. Without this the
    // rock returns as a ghost on every reload/stream-in.
    if (const TWeakObjectPtr<UStaticMeshComponent>* Rock = SweptRocks.Find(Entry.EntryGuid))
    {
        if (UStaticMeshComponent* RockSmc = Rock->Get())
        {
            if (RockSmc->IsVisible() || RockSmc->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
            {
                RockSmc->SetVisibility(false, true);
                RockSmc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                UE_LOG(LogNodeShuffle, Verbose, TEXT("Re-hid rock for inactive %s"), *Entry.VanillaNodePath);
                bOutChangedWorld = true;
            }
        }
    }
}

void ANodeShuffleSubsystem::ReactivateVanillaNode(FNodeShuffleEntry& Entry, AFGResourceNode* Node, bool& bOutChangedWorld)
{
    // Reverse of ApplyVanillaDeactivate: a node that was hidden in a PRIOR layout
    // is ACTIVE again in the current one. Because nothing was destroyed, the live
    // actor (and its rock) are still in the world — just un-hide + re-enable them.
    // The actual retype to the new resource is done by the normal retype path in
    // ApplyLayout after this returns; here we only undo the suppression so that
    // path (and the scanner) sees a live, visible node.
    DeactivatedNodePaths.Remove(Entry.VanillaNodePath);

    if (!IsValid(Node))
    {
        return; // not streamed in yet (or genuinely missing — respawn covers that)
    }

    bool bChanged = false;
    if (!Node->GetActorEnableCollision()) { Node->SetActorEnableCollision(true); bChanged = true; }
    if (Node->IsHidden()) { Node->SetActorHiddenInGame(false); bChanged = true; }
    if (AFGNodeMeshActor* MeshActor = FindMeshActorForNode(Node))
    {
        MeshActor->SetActorHiddenInGame(false);
        MeshActor->SetActorEnableCollision(true);
    }
    // Un-hide its separate ore-rock too (the deactivate path hid it).
    if (const TWeakObjectPtr<UStaticMeshComponent>* Rock = SweptRocks.Find(Entry.EntryGuid))
    {
        if (UStaticMeshComponent* RockSmc = Rock->Get())
        {
            if (!RockSmc->IsVisible())
            {
                RockSmc->SetVisibility(true, true);
                RockSmc->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
                bChanged = true;
            }
        }
    }
    if (bChanged)
    {
        UE_LOG(LogNodeShuffle, Verbose, TEXT("Reactivated %s (un-hidden in place — lossless restore)"),
            *Entry.VanillaNodePath);
        bOutChangedWorld = true;
    }
}

void ANodeShuffleSubsystem::EnsureMissingVanillaRespawned(FNodeShuffleEntry& Entry, bool& bOutChangedWorld)
{
    // COMPREHENSIVE RESPAWN (task #3): an ACTIVE non-new entry whose live actor is
    // genuinely missing — it never streams in. This happens for saves degraded by
    // the OLD destroy-based builds (those destructions are baked into the save and
    // cannot be undone) and for any node that is simply gone. Rather than leave a
    // hole in the world, materialize a managed node from the entry's recorded
    // location + assigned resource via the new-node spawn machinery (which gives it
    // collision, a rock mesh, radioactivity and terrain settling). Once spawned it
    // is tracked in SpawnedNodes by EntryGuid, so this fires at most once per entry.
    //
    // Only act once the player is near AND the level has had time to stream, so we
    // never spawn a duplicate for a node that is merely not-yet-streamed (a real
    // live actor that streams in later is matched by path and short-circuits the
    // caller before this is reached).
    if (AFGResourceNode* const* Existing = SpawnedNodes.Find(Entry.EntryGuid))
    {
        if (IsValid(*Existing)) { return; }
    }

    // CACHE-STALENESS SAFETY: VanillaNodeCache is built once per session, so a node
    // that streamed in AFTER it was built is absent from the cache and
    // FindVanillaNodeByPath wrongly reports it missing. Before spawning, do a
    // definitive live-world scan for ANY resource node at this entry's location.
    // If one is present, the level actor is merely slow to stream / cache-stale —
    // adopt it into the cache and abort the respawn (no duplicate). Only a spot
    // with genuinely no live node proceeds to respawn (a real, pre-lossless hole).
    {
        constexpr float PresentRadius = 1500.0f; // 15 m: same spot
        const float PresentSq = FMath::Square(PresentRadius);
        for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
        {
            AFGResourceNode* Live = *It;
            if (!IsValid(Live) || Live->IsA<AFGResourceDeposit>() || Live->HasAnyFlags(RF_Transient))
            {
                continue; // ignore deposits and our own transient spawns
            }
            if (FVector::DistSquared2D(Live->GetActorLocation(), Entry.Location) < PresentSq)
            {
                // The real level node is here after all — refresh the cache so the
                // normal retype/keep path picks it up, and don't respawn.
                VanillaNodeCache.Add(Entry.VanillaNodePath, Live);
                return;
            }
        }
    }

    // Drive the standard new-node spawn path. It already raycasts terrain, guards
    // overlap with any live node (so if the original streams in nearby we won't
    // double up), dresses the rock and adds collision. We do not flip bIsNewNode on
    // the persisted entry (it must stay a vanilla entry for pool/balance identity on
    // the next re-roll), so we ensure the spawn path has what it needs: a node class
    // and an assigned resource. Assigned == original for a respawned vanilla node
    // unless the layout retyped it.
    if (Entry.NodeClassPath.IsEmpty() || Entry.AssignedResourceClassPath.IsEmpty())
    {
        return; // cannot spawn without a class + resource; leave as data
    }
    EnsureNewNodeSpawned(Entry, bOutChangedWorld);
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

    // SPAWN-ON-DISCOVERY: raycast the ground FIRST and only materialize the node
    // if the ray HITS terrain (i.e. the region has actually streamed in). On a hit,
    // RaycastSettle snaps Entry.Location to the impact point and aligns Rotation to
    // the surface normal, so the node is born already grounded — never floating or
    // overlapping. On a miss, leave the entry as pure data and DEFER: a later tick
    // (as the player nears / terrain streams) retries. This is the core guarantee.
    if (!Entry.bRayCasted)
    {
        if (!RaycastSettle(Entry, nullptr, nullptr))
        {
            UE_LOG(LogNodeShuffle, Verbose,
                TEXT("Spawn-on-discovery: deferred %s node at %s (no terrain / out of range)"),
                *ResourceClass->GetName(), *Entry.Location.ToCompactString());
            return;
        }
        Entry.bRayCasted = true; // grounded: safe to spawn with solid collision now
    }

    // FIX C (overlap guard no longer spams + nudges instead of skip-forever):
    // even with spaced generation, the raycast may have settled this node's XY onto
    // a spot now occupied by a live vanilla/kept node that streamed in (or another
    // spawned new node). The OLD code logged "skipped (would overlap)" and returned
    // EVERY tick forever for the same spot — the node never spawned and the log
    // spammed. Now we SPIRAL-NUDGE the location to a nearby free, grounded, non-water
    // spot (like the water relocation) and PERSIST it; after a capped number of
    // failed nudges we RESOLVE the entry (deactivate it) so it stops re-attempting
    // and re-logging. A redundant node on an already-occupied spot is no real loss.
    {
        constexpr float OverlapRejectRadius = 800.0f; // 8 m: a node footprint
        constexpr uint8 MaxOverlapNudges = 8;
        const float RejSq = FMath::Square(OverlapRejectRadius);

        auto OverlapsAt = [&](const FVector& At) -> bool
        {
            for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
            {
                AFGResourceNode* Other = *It;
                if (!IsValid(Other) || Other->IsA<AFGResourceDeposit>())
                {
                    continue; // deposits are tiny one-off rocks, not blocking nodes
                }
                // Skip THIS entry's own already-spawned actor (if any) so we never
                // count ourselves as an overlap; other spawned new nodes (different
                // GUIDs) still block, preserving new-on-new spacing.
                if (AFGResourceNode* const* Mine = SpawnedNodes.Find(Entry.EntryGuid))
                {
                    if (*Mine == Other) { continue; }
                }
                if (FVector::DistSquared(Other->GetActorLocation(), At) < RejSq)
                {
                    return true;
                }
            }
            return false;
        };

        if (OverlapsAt(Entry.Location))
        {
            // Try one nudge per visit: an expanding golden-angle spiral, re-grounded
            // and water-checked, accepting the first free LAND spot. Persist it.
            constexpr float GoldenAngleRad = 2.39996323f;
            bool bRelocated = false;
            for (int32 i = 1; i <= LandRelocationTries && !bRelocated; i++)
            {
                const float Radius = FMath::Min(LandRelocationMaxRadiusCm,
                    LandRelocationStepCm * FMath::Sqrt(static_cast<float>(i)));
                const float Angle = GoldenAngleRad * i;
                const FVector Probe(Entry.Location.X + Radius * FMath::Cos(Angle),
                                    Entry.Location.Y + Radius * FMath::Sin(Angle),
                                    Entry.Location.Z);
                FVector TryLoc; FRotator TryRot = Entry.Rotation; bool bTryWater = false;
                if (RaycastGroundAt(Probe, Entry.Location.Z, nullptr, nullptr, TryLoc, TryRot, bTryWater)
                    && !bTryWater && !OverlapsAt(TryLoc))
                {
                    Entry.Location = TryLoc;
                    Entry.Rotation = TryRot;
                    bRelocated = true;
                    UE_LOG(LogNodeShuffle, Verbose,
                        TEXT("Overlap nudge: relocated %s node to %s (free land)"),
                        *ResourceClass->GetName(), *TryLoc.ToCompactString());
                }
            }
            if (!bRelocated)
            {
                Entry.OverlapNudges++;
                if (Entry.OverlapNudges >= MaxOverlapNudges)
                {
                    // Give up cleanly: resolve the entry so it never re-attempts or
                    // re-logs. (Logged once at Display, not spammed at Verbose.)
                    Entry.bActive = false;
                    Entry.bRayCasted = false;
                    UE_LOG(LogNodeShuffle, Display,
                        TEXT("Overlap guard: resolved %s node at %s after %d failed nudges (spot occupied; node dropped to stop retry spam)"),
                        *ResourceClass->GetName(), *Entry.Location.ToCompactString(), Entry.OverlapNudges);
                }
                return; // try again next tick (until the cap) at the new/old spot
            }
        }
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

    // The node was already settled onto terrain BEFORE the spawn (Entry.bRayCasted
    // was set true above), so it is grounded from birth — no floating, no deferred
    // settle pass needed, and solid collision is safe.
    UE_LOG(LogNodeShuffle, Verbose, TEXT("Spawn-on-discovery: materialized %s node at %s"),
        *ResourceClass->GetName(), *Entry.Location.ToCompactString());
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
    // LIQUID (oil): never spawn a rock mesh actor — rebuild the native decal. (The
    // spawn path normally handles this via ApplyNodeOwnMesh returning true, so this
    // is a defensive guard for any direct caller.)
    if (const FDonorVisual* LD = DonorVisuals.Find(ResourceClass->GetPathName()))
    {
        if (LD->Kind == EDonorKind::Liquid)
        {
            RebuildNodeNativeVisual(Node);
            return;
        }
    }
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

bool ANodeShuffleSubsystem::IsLocationNearAnyPlayer(const FVector& Loc, float RadiusCm) const
{
    const float RadiusSq = FMath::Square(RadiusCm);
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (const APlayerController* Pc = It->Get())
        {
            if (const APawn* Pawn = Pc->GetPawn())
            {
                if (FVector::DistSquared2D(Pawn->GetActorLocation(), Loc) < RadiusSq)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

// FIX 1: DespawnFarNewNodes was REMOVED. Materialized new nodes are now kept for
// the session once they spawn (the despawn-back-to-data path caused disappearing
// nodes, only-visual ghosts, and vanished modded nodes; actor count is not a real
// problem in Satisfactory).

void ANodeShuffleSubsystem::CaptureOriginalNodeRecord()
{
    // Snapshot every ORIGINAL vanilla node location from the freshly-rolled layout.
    // SuppressOriginalNodes uses this persistent record to hide originals (and their
    // rocks) on stream-in after a wipe — reliable across sessions because it does
    // not depend on a live scan. Rebuilt on every roll so it always matches the
    // current layout's notion of which spots were vanilla.
    OriginalNodeRecord.Reset();
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (E.bIsNewNode || E.VanillaNodePath.IsEmpty())
        {
            continue;
        }
        FNodeShuffleSuppressedOriginal Rec;
        Rec.VanillaNodePath = E.VanillaNodePath;
        Rec.Location = E.Location;
        OriginalNodeRecord.Add(Rec);
    }
    UE_LOG(LogNodeShuffle, Display, TEXT("Captured original-node record: %d vanilla locations"), OriginalNodeRecord.Num());
}

void ANodeShuffleSubsystem::SuppressOriginalNodes()
{
    if (OriginalNodeRecord.Num() == 0)
    {
        return;
    }

    // Build the set of vanilla paths that the CURRENT layout still uses as live
    // vanilla nodes (active, not destroyed) — those must NOT be suppressed. A path
    // is "reused/kept" when it appears as an active non-new entry. Inactive vanilla
    // entries are the ones we want gone; their nodes are destroyed by
    // ApplyVanillaDeactivate, but their separate rocks (and the node on a fresh
    // stream-in before deactivate runs) still need hiding.
    TSet<FString> KeptVanillaPaths;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bIsNewNode && E.bActive && !E.VanillaNodePath.IsEmpty())
        {
            KeptVanillaPaths.Add(E.VanillaNodePath);
        }
    }

    // Only act on streamed-in originals near a player (cheap, and matches the
    // streaming reality — far records can't have live actors anyway).
    TArray<FVector> Players;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (const APlayerController* Pc = It->Get())
        {
            if (const APawn* Pawn = Pc->GetPawn()) { Players.Add(Pawn->GetActorLocation()); }
        }
    }
    if (Players.Num() == 0)
    {
        return;
    }
    constexpr float SuppressPlayerRange = 30000.0f; // 300 m: only streamed-in originals
    constexpr float RockOwnRange = 800.0f;          // 8 m: a rock sits ON its node

    int32 NodesHidden = 0;
    for (const FNodeShuffleSuppressedOriginal& Rec : OriginalNodeRecord)
    {
        if (KeptVanillaPaths.Contains(Rec.VanillaNodePath))
        {
            continue; // this original is still a live, kept node — leave it alone
        }
        bool bNear = false;
        for (const FVector& P : Players)
        {
            if (FVector::DistSquared2D(P, Rec.Location) < FMath::Square(SuppressPlayerRange)) { bNear = true; break; }
        }
        if (!bNear)
        {
            continue;
        }

        // If the original node actor itself streamed back in (and is not occupied),
        // hide+disable it so it can't be mined as a ghost. Occupied nodes are never
        // touched (a miner could be on a node the player kept building on).
        if (AFGResourceNode* Node = FindVanillaNodeByPath(Rec.VanillaNodePath))
        {
            if (!IsNodeOccupiedAnyway(Node))
            {
                bool bChanged = false;
                if (Node->GetActorEnableCollision()) { Node->SetActorEnableCollision(false); bChanged = true; }
                if (!Node->IsHidden()) { Node->SetActorHiddenInGame(true); bChanged = true; }
                if (AFGNodeMeshActor* MeshActor = FindMeshActorForNode(Node))
                {
                    MeshActor->SetActorHiddenInGame(true);
                    MeshActor->SetActorEnableCollision(false);
                }
                if (bChanged) { NodesHidden++; }
            }
        }
    }

    // Hide any node-rock mesh sitting at a suppressed original's location (the
    // separate ore-rock that has no node behind it after a wipe). This is the
    // reliable orphan-rock kill driven by the persistent record. Instanced
    // components are world-shared — never touch them.
    int32 RocksHidden = 0;
    if (NodesHidden >= 0) // always run; cheap and self-healing on stream-in
    {
        for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
        {
            UStaticMeshComponent* Smc = *It;
            if (!IsValid(Smc) || Smc->GetWorld() != GetWorld() || !Smc->GetStaticMesh()
                || Cast<UInstancedStaticMeshComponent>(Smc) || !Smc->IsVisible())
            {
                continue;
            }
            // FIX 3: never hide a deposit's own visual mesh (child of the deposit
            // actor) — a deposit at a suppressed original spot must stay minable.
            if (Cast<AFGResourceDeposit>(Smc->GetOwner()))
            {
                continue;
            }
            if (!IsNodeRockMeshName(Smc->GetStaticMesh()->GetName(), TArray<FString>()))
            {
                continue;
            }
            const FVector Loc = Smc->GetComponentLocation();
            bool bNear = false;
            for (const FVector& P : Players)
            {
                if (FVector::DistSquared2D(P, Loc) < FMath::Square(SuppressPlayerRange)) { bNear = true; break; }
            }
            if (!bNear)
            {
                continue;
            }
            // Is this rock at a suppressed (not kept) original, and NOT at a kept one?
            bool bAtSuppressed = false, bAtKept = false;
            for (const FNodeShuffleSuppressedOriginal& Rec : OriginalNodeRecord)
            {
                if (FVector::DistSquared2D(Rec.Location, Loc) < FMath::Square(RockOwnRange))
                {
                    if (KeptVanillaPaths.Contains(Rec.VanillaNodePath)) { bAtKept = true; break; }
                    bAtSuppressed = true;
                }
            }
            if (bAtSuppressed && !bAtKept)
            {
                Smc->SetVisibility(false, true);
                Smc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                RocksHidden++;
            }
        }
    }

    if (NodesHidden > 0 || RocksHidden > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("Suppress originals: hid %d original nodes and %d original rocks (wipe-on-reroll clean slate)"),
            NodesHidden, RocksHidden);
    }
}

bool ANodeShuffleSubsystem::IsPointInWater(const FVector& Point) const
{
    // Definitive water test: is the point inside any streamed-in water volume?
    // The ocean covers ~half the map's XY bounds, so far candidates frequently
    // settle on the SEAFLOOR — the down-ray hits solid terrain (a valid hit),
    // but the impact point is underwater and the node is never seen. A water
    // volume's EncompassesPoint returns true for exactly those submerged points.
    // We iterate AFGWaterVolume directly (also catches volumes not yet listed in
    // AFGWorldSettings::mWaterVolumes). Only streamed-in volumes exist here, which
    // is fine: settle only runs where terrain (and thus its water) is loaded.
    if (!GetWorld())
    {
        return false;
    }
    for (TActorIterator<AFGWaterVolume> It(GetWorld()); It; ++It)
    {
        AFGWaterVolume* Water = *It;
        if (IsValid(Water) && Water->EncompassesPoint(Point))
        {
            return true;
        }
    }
    return false;
}

bool ANodeShuffleSubsystem::RaycastGroundAt(const FVector& ProbeXY, float StartZ, const AActor* IgnoreNode,
                                            const AActor* IgnoreMesh, FVector& OutLoc, FRotator& OutRot,
                                            bool& bOutWater) const
{
    bOutWater = false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(NodeShuffleSettle), true);
    if (IgnoreNode) { Params.AddIgnoredActor(IgnoreNode); }
    if (IgnoreMesh) { Params.AddIgnoredActor(IgnoreMesh); }

    const FVector Start(ProbeXY.X, ProbeXY.Y, StartZ + 20000.f);
    const FVector End(ProbeXY.X, ProbeXY.Y, StartZ - 40000.f);
    FHitResult Hit;
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
    {
        return false; // no terrain / out of range (true void)
    }
    OutLoc = Hit.ImpactPoint;
    const FQuat AlignQuat = FQuat::FindBetweenNormals(FVector::UpVector, Hit.ImpactNormal);
    OutRot = (AlignQuat * FQuat(FRotator(0.f, OutRot.Yaw, 0.f))).Rotator();
    // Cliffs/steep normals are FINE (reachable by ladder/jetpack); only WATER is
    // rejected. Test the grounded impact point for water containment.
    bOutWater = IsPointInWater(OutLoc);
    return true;
}

bool ANodeShuffleSubsystem::RaycastSettle(FNodeShuffleEntry& Entry, const AActor* IgnoreNode, const AActor* IgnoreMesh) const
{
    FVector Loc;
    FRotator Rot = Entry.Rotation; // preserve yaw through the align math
    bool bWater = false;

    // 1. Primary probe at the entry's own XY.
    if (RaycastGroundAt(Entry.Location, Entry.Location.Z, IgnoreNode, IgnoreMesh, Loc, Rot, bWater) && !bWater)
    {
        Entry.Location = Loc;
        Entry.Rotation = Rot;
        return true;
    }

    // Either a true void (no terrain — defer, terrain may not be streamed) or a
    // water hit (seafloor). For the void case we defer immediately; for water we
    // try to RELOCATE onto nearby land before giving up.
    const bool bPrimaryWasWater = bWater; // bWater only meaningful when a hit occurred

    // 2. FIX A relocation: nudge the candidate over an expanding spiral of offset
    //    points and re-raycast each; the FIRST land hit wins and is persisted to
    //    Entry.Location so the node stays put thereafter. If no land is found
    //    within the cap, defer (leave as data) — never spawn in water.
    if (bPrimaryWasWater)
    {
        const FVector OriginXY = Entry.Location; // current (water) XY/Z probe base
        // Golden-angle spiral: even areal coverage as the radius grows ring by ring.
        constexpr float GoldenAngleRad = 2.39996323f;
        for (int32 i = 1; i <= LandRelocationTries; i++)
        {
            const float Radius = FMath::Min(LandRelocationMaxRadiusCm,
                LandRelocationStepCm * FMath::Sqrt(static_cast<float>(i)));
            const float Angle = GoldenAngleRad * i;
            const FVector Probe(OriginXY.X + Radius * FMath::Cos(Angle),
                                OriginXY.Y + Radius * FMath::Sin(Angle),
                                OriginXY.Z);
            FRotator TryRot = Entry.Rotation;
            FVector TryLoc;
            bool bTryWater = false;
            if (RaycastGroundAt(Probe, OriginXY.Z, IgnoreNode, IgnoreMesh, TryLoc, TryRot, bTryWater)
                && !bTryWater)
            {
                UE_LOG(LogNodeShuffle, Verbose, TEXT("Land-check: relocated %s off water to %s"),
                    *Entry.EntryGuid.ToString(), *TryLoc.ToCompactString());
                Entry.Location = TryLoc; // persist the corrected location (stable thereafter)
                Entry.Rotation = TryRot;
                return true;
            }
        }
        UE_LOG(LogNodeShuffle, Verbose, TEXT("Land-check: deferred %s (no land found nearby)"),
            *Entry.EntryGuid.ToString());
    }

    // Void hit, or water with no nearby land: defer (leave entry as data, retry later).
    return false;
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

bool ANodeShuffleSubsystem::IsEligibleVanillaNode(const AFGResourceNode* Node, bool bIncludeModded, bool bIncludeLiquid)
{
    const TCHAR* Unused = nullptr;
    return IsEligibleVanillaNodeReason(Node, bIncludeModded, bIncludeLiquid, Unused);
}

bool ANodeShuffleSubsystem::IsEligibleVanillaNodeReason(const AFGResourceNode* Node, bool bIncludeModded,
                                                        bool bIncludeLiquid, const TCHAR*& OutReason)
{
    OutReason = TEXT("eligible");
    if (!IsValid(Node) || Node->GetClass()->GetName().StartsWith(TEXT("SKEL_")))
    {
        OutReason = TEXT("invalid/skeletal class");
        return false;
    }
    // Mod-spawned nodes are transient: never part of the vanilla pool.
    if (Node->HasAnyFlags(RF_Transient))
    {
        OutReason = TEXT("transient (mod-spawned)");
        return false;
    }
    // Resource deposits are the small one-off pickup rocks, never a shuffle node.
    if (Node->IsA<AFGResourceDeposit>())
    {
        OutReason = TEXT("AFGResourceDeposit (pickup, not a node)");
        return false;
    }

    // The resource class must be a genuine raw-resource descriptor. Mods like
    // AllMinable scatter invisible nodes carrying crafted-ITEM descriptors
    // (rotors, plates, ...) across the map; without this check they pollute
    // the pool AND the per-resource balance floor then guarantees active
    // nodes of every crafted item.
    const UClass* ResClass = Node->GetResourceClass();
    if (ResClass == nullptr || !ResClass->IsChildOf(UFGResourceDescriptor::StaticClass()))
    {
        OutReason = TEXT("resource class is not a UFGResourceDescriptor");
        return false;
    }
    const bool bVanillaResource = ResClass->GetPathName().StartsWith(TEXT("/Game/"));
    // Mod-added descriptors (AllMinable's item nodes, modded ores incl. lithium's
    // /Lithium/ alkali descriptor) live outside /Game/. They join the pool only
    // when the user opts in; the completability floor never applies to them either way.
    if (!bVanillaResource && !bIncludeModded)
    {
        OutReason = TEXT("modded resource and IncludeModdedNodes is off");
        return false;
    }

    // PURITY GATE.
    // Vanilla nodes: strict — a non-standard purity ("Other Purity" in node
    // listings) is a non-functional companion artifact and is excluded.
    // Modded nodes (FIX 4): AlkaLib's "advanced Resource Nodes" (e.g. lithium's
    // BP_ResourdeNode_Alkali_C) can report a purity OUTSIDE {Impure,Normal,Pure}
    // yet are fully functional, mineable nodes carrying their own rock mesh. So
    // for modded nodes we do NOT reject on purity here — the roll path clamps a
    // non-standard purity to RP_Normal so the node still shuffles correctly.
    const EResourcePurity Purity = Node->GetResourcePurity();
    const bool bStandardPurity = (Purity == RP_Inpure || Purity == RP_Normal || Purity == RP_Pure);
    if (bVanillaResource && !bStandardPurity)
    {
        OutReason = TEXT("vanilla node with non-standard purity (Other Purity artifact)");
        return false;
    }

    // NODE-TYPE GATE. Only plain Node type participates; geysers, fracking
    // cores/satellites and deposits are never shuffled (same for modded).
    if (Node->GetResourceNodeType() != EResourceNodeType::Node)
    {
        OutReason = TEXT("not EResourceNodeType::Node (geyser/fracking/deposit)");
        return false;
    }

    // RESOURCE-AMOUNT GATE.
    // Vanilla: must be infinite (poor/normal/rich finite veins do not exist as
    // shufflable infinite nodes). Modded (FIX 4): AlkaLib advanced nodes may
    // report a non-Infinite amount enum yet still mine forever in practice; do
    // not reject modded nodes on amount so lithium shuffles.
    if (bVanillaResource && Node->GetResourceAmount() != EResourceAmount::RA_Infinite)
    {
        OutReason = TEXT("vanilla node is not RA_Infinite");
        return false;
    }

    // RESOURCE FORM GATE (generic, property-driven — no name/class hardcoding).
    // Solid is ALWAYS allowed. Liquid (oil) AND gas (e.g. lithium, Desc_OreLithium
    // form=RF_GAS) join ONLY when the experimental flag is on: both need special
    // extractors (the same caveat that has always gated oil), so they share one gate
    // rather than a hardcoded per-form/per-name allow-list. RF_INVALID and any future
    // non-extractable form stay excluded as principled junk.
    const EResourceForm Form = Node->GetResourceForm();
    const bool bExperimentalForm = (Form == EResourceForm::RF_LIQUID) || (Form == EResourceForm::RF_GAS);
    const bool bFormAllowed = (Form == EResourceForm::RF_SOLID)
        || (bIncludeLiquid && bExperimentalForm);
    if (!bFormAllowed)
    {
        OutReason = bExperimentalForm
            ? TEXT("liquid/gas node and experimental features are off")
            : TEXT("resource form is not solid/liquid/gas");
        return false;
    }

    return true;
}

void ANodeShuffleSubsystem::DiagnoseModdedNodeEligibility(bool bIncludeModded, bool bIncludeLiquid) const
{
    // One line per distinct MODDED node class (keyed by node-class + resource-class
    // so a class carrying several resources is each reported once). Vanilla /Game/
    // nodes are not logged here — this targets the "why doesn't my modded ore
    // shuffle" question directly.
    auto* MutableThis = const_cast<ANodeShuffleSubsystem*>(this);
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        const AFGResourceNode* Node = *It;
        if (!IsValid(Node)) { continue; }
        const UClass* ResClass = Node->GetResourceClass();
        const bool bModdedResource = ResClass && !ResClass->GetPathName().StartsWith(TEXT("/Game/"));
        const bool bModdedNodeClass = !Node->GetClass()->GetPathName().StartsWith(TEXT("/Game/"));
        if (!bModdedResource && !bModdedNodeClass) { continue; } // vanilla — skip
        const FString Key = Node->GetClass()->GetName()
            + TEXT("|") + (ResClass ? ResClass->GetName() : TEXT("<null>"));
        if (ModdedEligibilityLogged.Contains(Key)) { continue; }
        MutableThis->ModdedEligibilityLogged.Add(Key);

        const TCHAR* Reason = nullptr;
        const bool bEligible = IsEligibleVanillaNodeReason(Node, bIncludeModded, bIncludeLiquid, Reason);
        UE_LOG(LogNodeShuffle, Display,
            TEXT("Modded-node eligibility: class '%s' resource '%s' purity=%d nodeType=%d amount=%d form=%d -> %s (%s)"),
            *Node->GetClass()->GetName(), ResClass ? *ResClass->GetName() : TEXT("<null>"),
            (int32)Node->GetResourcePurity(), (int32)Node->GetResourceNodeType(),
            (int32)Node->GetResourceAmount(), (int32)Node->GetResourceForm(),
            bEligible ? TEXT("SHUFFLED") : TEXT("excluded"), Reason);
    }
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
    // Drop pairings whose rock streamed out (weak ptr gone stale) so they can be
    // re-found when that area streams back in.
    for (auto It = SweptRocks.CreateIterator(); It; ++It)
    {
        if (!It->Value.IsValid()) { It.RemoveCurrent(); }
    }

    // Rocks stream in as the player explores, so a single up-front sweep can't
    // pair them all. Re-sweep (rate-limited) while a node that still needs its
    // separate rock sits near a player unpaired; stop once everything reachable
    // is paired or proven to have none (RockSearchExhausted), so we don't sweep
    // the whole component set every tick forever.
    const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    if (bRocksSwept)
    {
        if (Now - LastRockSweepSeconds < RockResweepCooldownSeconds || !HasNonNewEntryNeedingRock())
        {
            return;
        }
    }
    LastRockSweepSeconds = Now;

    // User-extendable rock names: mods with vanilla-style separate rock
    // actors can be taught to the matcher without a rebuild. The unmatched-
    // name diagnostic in the log tells users exactly what to add here.
    // File: <game>/FactoryGame/Configs/NodeShuffle_RockPatterns.json
    //   { "Patterns": [ "SM_MyModRock", "CrystalNode_" ] }   (prefix match)
    TArray<FString> ExtraPatterns;
    LoadExtraRockPatterns(ExtraPatterns);
    if (ExtraPatterns.Num() > 0)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("Loaded %d extra rock patterns from NodeShuffle_RockPatterns.json"), ExtraPatterns.Num());
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
        if (IsNodeRockMeshName(Smc->GetStaticMesh()->GetName(), ExtraPatterns))
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
    // Protect pairings made in earlier sweeps so a re-sweep never reassigns an
    // already-claimed rock to a different (closer) node.
    for (const auto& Pair : SweptRocks)
    {
        if (Pair.Value.IsValid()) { ClaimedRocks.Add(Pair.Value.Get()); }
    }
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

    // Give up on entries that are still unpaired even though a player is close
    // enough that any real rock would have streamed in — they are self-meshed
    // (their visual died with the destroyed node) or simply have no separate
    // rock. Marking them stops HasNonNewEntryNeedingRock from forcing a fresh
    // full sweep every cooldown for the rest of the session.
    {
        TArray<FVector> Players;
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            if (const APlayerController* Pc = It->Get())
            {
                if (const APawn* Pawn = Pc->GetPawn()) { Players.Add(Pawn->GetActorLocation()); }
            }
        }
        for (const FNodeShuffleEntry& E : Layout)
        {
            if (E.bIsNewNode || RockSearchExhausted.Contains(E.EntryGuid)) { continue; }
            const TWeakObjectPtr<UStaticMeshComponent>* Rock = SweptRocks.Find(E.EntryGuid);
            if (Rock && Rock->IsValid()) { continue; }
            for (const FVector& P : Players)
            {
                if (FVector::DistSquared2D(P, E.Location) < FMath::Square(RockSearchRange))
                {
                    RockSearchExhausted.Add(E.EntryGuid);
                    break;
                }
            }
        }
    }
}

bool ANodeShuffleSubsystem::HasNonNewEntryNeedingRock() const
{
    TArray<FVector> Players;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (const APlayerController* Pc = It->Get())
        {
            if (const APawn* Pawn = Pc->GetPawn()) { Players.Add(Pawn->GetActorLocation()); }
        }
    }
    if (Players.Num() == 0)
    {
        return false;
    }
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (E.bIsNewNode || RockSearchExhausted.Contains(E.EntryGuid))
        {
            continue;
        }
        // Only entries that actually consume a separate rock: inactive nodes
        // (hide it) or ore-style retypes (re-skin it). Active untouched nodes
        // and self-meshed retypes use the node's own component — no pairing.
        const bool bRetyped = E.bActive && (E.AssignedResourceClassPath != E.OriginalResourceClassPath
                                            || E.AssignedPurity != E.OriginalPurity);
        if (E.bActive && !bRetyped)
        {
            continue;
        }
        const TWeakObjectPtr<UStaticMeshComponent>* Rock = SweptRocks.Find(E.EntryGuid);
        if (Rock && Rock->IsValid())
        {
            continue; // already paired
        }
        for (const FVector& P : Players)
        {
            if (FVector::DistSquared2D(P, E.Location) < FMath::Square(RockSearchRange))
            {
                return true;
            }
        }
    }
    return false;
}

void ANodeShuffleSubsystem::RebuildNodeNativeVisual(AFGResourceNode* Node)
{
    // The node draws its own visual (solid mesh OR oil decal) from its CURRENT
    // resource descriptor. After SetResourceClassOverride the override descriptor is
    // live, so triggering the game's OWN override-applied path rebuilds the node's
    // representation for the assigned resource — for oil this builds the puddle DECAL
    // (oil has no static mesh), for a modded ore with no donor it builds whatever the
    // descriptor defines.
    //
    // We invoke OnRep_ResourceClassOverride() (the engine's post-override rebuild)
    // via reflection/ProcessEvent rather than calling it directly: it is the
    // game's authoritative rebuild, and — critically — UpdateMeshFromDescriptor is
    // declared but NOT exported by the shipping FactoryGame DLL (LNK2019 if called
    // directly), whereas a UFUNCTION is always reachable through reflection without
    // an exported symbol.
    if (!IsValid(Node))
    {
        return;
    }
    static const FName RebuildFn(TEXT("OnRep_ResourceClassOverride"));
    if (UFunction* Fn = Node->FindFunction(RebuildFn))
    {
        Node->ProcessEvent(Fn, nullptr); // no params
    }
}

bool ANodeShuffleSubsystem::IsMeshClaimedByOtherResource(const FString& MeshPath, const FString& ForResourcePath) const
{
    // DONOR-HYGIENE: the one-mesh-one-resource invariant. A rock mesh that is
    // already the REAL donor of a DIFFERENT resource is a shared placeholder when a
    // second resource tries to claim it (the classic 'ResourceNode_quartz' reused by
    // dozens of modded nodes). Reject the second (and later) claimants generically —
    // no mesh-name or mod-class hardcoding; the collision itself is the signal.
    if (MeshPath.IsEmpty())
    {
        return false;
    }
    const FString* MeshOwner = RealDonorMeshOwner.Find(MeshPath);
    return MeshOwner && *MeshOwner != ForResourcePath;
}

void ANodeShuffleSubsystem::ClaimRealDonorMesh(const FString& MeshPath, const FString& ResourcePath)
{
    if (MeshPath.IsEmpty() || ResourcePath.IsEmpty())
    {
        return;
    }
    RealDonorMeshOwner.Add(MeshPath, ResourcePath);
}

void ANodeShuffleSubsystem::RevalidatePersistedDonors()
{
    // DONOR-HYGIENE: do not blindly trust SaveGame donors. A donor persisted by an
    // older build (or this build before a placeholder was rejected) may itself be a
    // polluted record — a resource whose stored mesh is really a shared placeholder.
    // After rehydrate, enforce the one-mesh-one-resource invariant on the persisted
    // store: the FIRST resource to claim a mesh keeps it; any later resource sharing
    // that exact mesh is a placeholder collision and is DISCARDED (both from the in-
    // memory donor and from the SaveGame store) so a genuine native node can re-capture
    // it later. Liquid donors carry no mesh and are exempt.
    if (bPersistedDonorsRevalidated)
    {
        return;
    }
    bPersistedDonorsRevalidated = true;

    // Prefer to seed ownership from resources whose NATIVE (vanilla /Game/) class is
    // most likely the mesh's rightful owner: process vanilla resources first so a
    // vanilla rock (e.g. real quartz -> ResourceNode_quartz) claims the mesh before a
    // modded placeholder resource that merely reuses it. Within each group, first-come.
    TArray<FString> Order;
    DonorVisuals.GenerateKeyArray(Order);
    Order.Sort([](const FString& A, const FString& B)
    {
        const bool bAV = A.StartsWith(TEXT("/Game/"));
        const bool bBV = B.StartsWith(TEXT("/Game/"));
        if (bAV != bBV) { return bAV; } // vanilla resources first
        return A < B;                    // stable
    });

    int32 Discarded = 0;
    for (const FString& ResPath : Order)
    {
        FDonorVisual* D = DonorVisuals.Find(ResPath);
        if (!D || D->Kind == EDonorKind::Liquid || D->bIsFallback)
        {
            continue; // only REAL solid donors participate in the one-mesh invariant
        }
        UStaticMesh* Mesh = D->Mesh.Get();
        const FString MeshPath = Mesh ? Mesh->GetPathName() : FString();
        if (MeshPath.IsEmpty())
        {
            continue;
        }
        if (IsMeshClaimedByOtherResource(MeshPath, ResPath))
        {
            // Placeholder collision: a different resource already owns this mesh.
            // Discard this resource's polluted donor so a genuine native node can
            // re-capture the correct rock (it goes back into the needing-donor set).
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("Donor hygiene: DISCARDED persisted donor for %s — its mesh '%s' is a shared placeholder already owned by %s (will re-capture from a genuine node)"),
                *FPackageName::ObjectPathToObjectName(ResPath),
                *Mesh->GetName(),
                *FPackageName::ObjectPathToObjectName(*RealDonorMeshOwner.Find(MeshPath)));
            DonorVisuals.Remove(ResPath);
            PersistedDonors.RemoveAll([&](const FNodeShuffleDonorRecord& R){ return R.ResourceClassPath == ResPath; });
            Discarded++;
            continue;
        }
        ClaimRealDonorMesh(MeshPath, ResPath);
    }
    if (Discarded > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("Donor hygiene: revalidated persisted donors, discarded %d polluted (placeholder) record(s)"), Discarded);
    }
}

void ANodeShuffleSubsystem::CaptureDonorVisuals()
{
    // INCREMENTAL CAPTURE (visual-6): the old one-shot ran against a partially
    // streamed world and captured only the resources whose nodes happened to be
    // loaded (the log's "10 of 12" — oil + the modded lead ore were missing and
    // rendered a wrong rock). We now capture missing donors continuously as live
    // nodes stream in, until every ASSIGNED resource in the layout is covered.
    //
    // Donors are keyed by the ASSIGNED resource path (the thing we must reproduce),
    // captured from ANY live node currently carrying that resource as its ORIGINAL
    // type (its rock is still vanilla-correct). Three coverage cases:
    //   - SOLID vanilla/modded: stamp mesh + materials + WORLD scale from a real node.
    //   - LIQUID (oil): no static mesh; mark Liquid -> rebuild the node's native decal.
    //   - modded with no live donor mesh: fall back to the descriptor's deposit mesh,
    //     else a Liquid-style native rebuild — never a wrong ore rock.

    // 1. Build the target set once: every distinct resource the layout assigns.
    if (!bDonorTargetsBuilt)
    {
        AssignedResourcesNeedingDonor.Reset();
        for (const FNodeShuffleEntry& E : Layout)
        {
            if (E.AssignedResourceClassPath.IsEmpty()) { continue; }
            AssignedResourcesNeedingDonor.Add(E.AssignedResourceClassPath);
            // Originals also need a donor so a re-roll that restores them re-skins
            // correctly even if that resource is no longer assigned anywhere.
            if (!E.bIsNewNode && !E.OriginalResourceClassPath.IsEmpty())
            {
                AssignedResourcesNeedingDonor.Add(E.OriginalResourceClassPath);
            }
        }
        bDonorTargetsBuilt = true;
    }
    if (bAllDonorsCovered)
    {
        return;
    }

    // 2. Map each still-needed resource to a live node carrying it (by ORIGINAL
    //    type, whose visual is still authentic). One pass over the live world.
    TMap<FString, AFGResourceNode*> LiveByResource;
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        AFGResourceNode* Node = *It;
        if (!IsValid(Node) || Node->IsA<AFGResourceDeposit>() || Node->HasAnyFlags(RF_Transient))
        {
            continue; // ignore deposits and our own transient spawns
        }
        const UClass* Orig = Node->GetResourceClassOriginal().Get();
        if (!Orig) { continue; }
        // Only nodes whose visual is STILL their original look are valid donors: a
        // node we have already re-skinned (override active and different from its
        // original) now wears the WRONG rock, so capturing from it would propagate
        // that wrong look. Accept only un-overridden nodes (override null or == original).
        const UClass* Override = Node->GetResourceClassOverride().Get();
        if (Override && Override != Orig) { continue; }
        const FString Path = Orig->GetPathName();
        // FIX 2: a resource that only has a FALLBACK donor (deposit mesh / rehydrated
        // fallback) is still a real-donor target — a pristine live node lets us
        // upgrade it to the correct rock. Treat only a REAL donor as "covered".
        const FDonorVisual* HaveDonor = DonorVisuals.Find(Path);
        const bool bHaveReal = HaveDonor && !HaveDonor->bIsFallback;
        if (AssignedResourcesNeedingDonor.Contains(Path) && !bHaveReal)
        {
            LiveByResource.FindOrAdd(Path) = Node; // first live un-retyped node of this resource
        }
    }

    const auto LogCoverage = [this](const FString& Path, const TCHAR* How)
    {
        if (!DonorCoverageLogged.Contains(Path))
        {
            DonorCoverageLogged.Add(Path);
            UE_LOG(LogNodeShuffle, Display, TEXT("Donor coverage: %s -> %s"),
                *FPackageName::ObjectPathToObjectName(Path), How);
        }
    };

    // 3. Capture a donor for each still-needed resource we can now reach.
    // DONOR-HYGIENE: process VANILLA (/Game/) resources first so a genuine vanilla
    // rock claims a shared mesh before any modded placeholder resource that merely
    // reuses it (the one-mesh-one-resource guard then rejects the placeholder). The
    // TSet's own order is unspecified, so iterate a sorted snapshot and remove from
    // the set explicitly when a resource is finished.
    TArray<FString> NeedOrder = AssignedResourcesNeedingDonor.Array();
    NeedOrder.Sort([](const FString& A, const FString& B)
    {
        const bool bAV = A.StartsWith(TEXT("/Game/"));
        const bool bBV = B.StartsWith(TEXT("/Game/"));
        if (bAV != bBV) { return bAV; } // vanilla first
        return A < B;                    // stable
    });
    for (const FString& Path : NeedOrder)
    {
        const auto RemoveNeed = [&]() { AssignedResourcesNeedingDonor.Remove(Path); };
        // FIX 2: only a REAL donor finishes a resource. A fallback (deposit mesh /
        // rehydrated fallback) stays in the needing set so a pristine live node can
        // still upgrade it to the correct rock — but we DON'T re-log it as needing.
        const FDonorVisual* HaveDonor = DonorVisuals.Find(Path);
        if (HaveDonor && !HaveDonor->bIsFallback)
        {
            RemoveNeed();
            continue;
        }
        const bool bHaveFallback = (HaveDonor != nullptr); // fallback present, try to upgrade

        UClass* ResClass = LoadClassByPath(Path);
        const bool bLiquid = ResClass
            && UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(ResClass)) == EResourceForm::RF_LIQUID;

        // LIQUID (oil): there is no rock mesh to copy — the node renders a ground
        // decal from its descriptor. Record a Liquid donor (the apply path rebuilds
        // the native decal). Captured immediately, no live node required.
        if (bLiquid)
        {
            FDonorVisual Donor;
            Donor.Kind = EDonorKind::Liquid;
            DonorVisuals.Add(Path, Donor);
            PersistDonor(Path, Donor); // FIX 2: liquid look survives reload/reroll
            LogCoverage(Path, TEXT("REAL (liquid decal from descriptor)"));
            RemoveNeed();
            continue;
        }

        // SOLID: capture mesh + materials + world scale from a live node of this
        // resource (its own component, or its swept separate rock).
        AFGResourceNode* Live = LiveByResource.FindRef(Path);
        UStaticMeshComponent* Smc = Live ? Live->FindComponentByClass<UStaticMeshComponent>() : nullptr;
        if ((!Smc || !Smc->GetStaticMesh()) && Live)
        {
            // ore-style nodes carry no mesh: try the swept rock paired to its entry.
            for (const FNodeShuffleEntry& E : Layout)
            {
                if (!E.bIsNewNode && E.VanillaNodePath == Live->GetPathName())
                {
                    const TWeakObjectPtr<UStaticMeshComponent>* Rock = SweptRocks.Find(E.EntryGuid);
                    if (Rock && Rock->Get() && Rock->Get()->GetStaticMesh()) { Smc = Rock->Get(); }
                    break;
                }
            }
        }
        if (Smc && Smc->GetStaticMesh())
        {
            // DONOR-HYGIENE: enforce one-mesh-one-resource. If this candidate mesh is
            // already the REAL donor of a DIFFERENT resource, it is a shared
            // placeholder (e.g. a modded node's 'ResourceNode_quartz' that several
            // resources reuse, or a placeholder rock sitting on an ore node and grabbed
            // by proximity). Capturing it here is exactly the pollution that turns
            // iron's rock to quartz. Reject it: DON'T capture, and leave this resource
            // in the needing set so a genuine native node (or the deposit-mesh
            // fallback) supplies the correct rock instead. No mesh-name/mod hardcoding;
            // the cross-resource mesh collision is the generic placeholder signal.
            const FString CandMeshPath = Smc->GetStaticMesh()->GetPathName();
            if (IsMeshClaimedByOtherResource(CandMeshPath, Path))
            {
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("Donor hygiene: REJECTED placeholder donor for %s — candidate mesh '%s' is already the real rock of %s (one mesh serves many resources). Will use a genuine node or deposit-mesh fallback."),
                    *FPackageName::ObjectPathToObjectName(Path),
                    *Smc->GetStaticMesh()->GetName(),
                    *FPackageName::ObjectPathToObjectName(*RealDonorMeshOwner.Find(CandMeshPath)));
                // Leave Path in AssignedResourcesNeedingDonor; do not RemoveCurrent.
                continue;
            }

            FDonorVisual Donor;
            Donor.Kind = EDonorKind::SolidMesh;
            Donor.Mesh = Smc->GetStaticMesh();
            for (int32 i = 0; i < Smc->GetNumMaterials(); i++)
            {
                Donor.Materials.Add(Smc->GetMaterial(i));
            }
            Donor.RelativeTransform = FTransform(FQuat::Identity, FVector::ZeroVector, Smc->GetComponentScale());
            DonorVisuals.Add(Path, Donor);
            ClaimRealDonorMesh(CandMeshPath, Path); // this mesh now belongs to this resource
            PersistDonor(Path, Donor); // FIX 2: real rock look survives reload/reroll
            UE_LOG(LogNodeShuffle, Display, TEXT("donor[%s] = mesh '%s'"),
                *FPackageName::ObjectPathToObjectName(Path), *Smc->GetStaticMesh()->GetName());
            LogCoverage(Path, TEXT("REAL (live-node rock mesh)"));
            RemoveNeed();
            continue;
        }

        // No live donor node yet: keep this resource in the needing-donor set so a
        // later tick (more streamed in) captures a REAL donor. Do NOT install a
        // fallback yet — only once below, after streaming has had ample time.
    }

    // 4. FALLBACK only when streaming has clearly settled but a resource still has
    //    no real donor (a modded ore with no live node anywhere this session).
    //    Use the descriptor's own deposit mesh; if it has none, mark Liquid-style
    //    native rebuild. Never leave an obviously-wrong ore rock.
    const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    if (Now > 60.f && AssignedResourcesNeedingDonor.Num() > 0)
    {
        for (auto It = AssignedResourcesNeedingDonor.CreateIterator(); It; ++It)
        {
            const FString Path = *It;
            if (DonorVisuals.Contains(Path)) { It.RemoveCurrent(); continue; }
            UClass* ResClass = LoadClassByPath(Path);
            UStaticMesh* DepositMesh = ResClass
                ? UFGResourceDescriptor::GetDepositMesh(TSubclassOf<UFGResourceDescriptor>(ResClass)) : nullptr;
            FDonorVisual Donor;
            if (DepositMesh)
            {
                Donor.Kind = EDonorKind::SolidMesh;
                Donor.Mesh = DepositMesh;
                Donor.RelativeTransform = FTransform(FQuat::Identity, FVector::ZeroVector, FVector(1.f));
                Donor.bIsFallback = true;
                DonorVisuals.Add(Path, Donor);
                PersistDonor(Path, Donor); // persisted as fallback; upgraded if a real donor appears
                LogCoverage(Path, TEXT("FALLBACK (descriptor deposit mesh)"));
            }
            else
            {
                Donor.Kind = EDonorKind::Liquid; // native rebuild from descriptor
                Donor.bIsFallback = true;
                DonorVisuals.Add(Path, Donor);
                PersistDonor(Path, Donor);
                LogCoverage(Path, TEXT("FALLBACK (native descriptor rebuild — no mesh)"));
            }
            It.RemoveCurrent();
        }
    }

    if (AssignedResourcesNeedingDonor.Num() == 0)
    {
        if (!bAllDonorsCovered)
        {
            bAllDonorsCovered = true;
            UE_LOG(LogNodeShuffle, Display,
                TEXT("Donor coverage COMPLETE: %d resource types (all assigned + original resources covered)"),
                DonorVisuals.Num());
        }
    }
}

void ANodeShuffleSubsystem::PersistDonor(const FString& ResourcePath, const FDonorVisual& Donor)
{
    // FIX 2: mirror a captured donor into the SaveGame store as soft paths. A REAL
    // capture (bIsFallback=false) upgrades any prior fallback record; a fallback
    // never downgrades an existing real record.
    FNodeShuffleDonorRecord* Existing = PersistedDonors.FindByPredicate(
        [&](const FNodeShuffleDonorRecord& R){ return R.ResourceClassPath == ResourcePath; });
    if (Existing && !Existing->bIsFallback && Donor.bIsFallback)
    {
        return; // keep the better (real) record we already have
    }

    FNodeShuffleDonorRecord Rec;
    Rec.ResourceClassPath = ResourcePath;
    Rec.Kind = (Donor.Kind == EDonorKind::Liquid) ? 1 : 0;
    Rec.bIsFallback = Donor.bIsFallback;
    Rec.Scale = Donor.RelativeTransform.GetScale3D();
    if (Donor.Kind == EDonorKind::SolidMesh && Donor.Mesh.Get())
    {
        Rec.MeshPath = Donor.Mesh.Get()->GetPathName();
        for (const TWeakObjectPtr<UMaterialInterface>& M : Donor.Materials)
        {
            Rec.MaterialPaths.Add(M.Get() ? M.Get()->GetPathName() : FString());
        }
    }

    if (Existing)
    {
        *Existing = Rec;
    }
    else
    {
        PersistedDonors.Add(Rec);
    }
}

void ANodeShuffleSubsystem::RehydrateDonorsFromPersisted()
{
    // FIX 2: rebuild in-memory DonorVisuals from the persisted store at session
    // start. This is the fix for the reload case: every live vanilla node is already
    // retyped (SaveGame override), so CaptureDonorVisuals could not find a pristine
    // donor and fell back to the wrong deposit mesh. Resolving the persisted real
    // mesh/materials/scale gives the correct rock with no live donor required.
    if (bDonorsRehydrated)
    {
        return;
    }
    bDonorsRehydrated = true;

    int32 Real = 0, Fallback = 0;
    for (const FNodeShuffleDonorRecord& Rec : PersistedDonors)
    {
        if (Rec.ResourceClassPath.IsEmpty() || DonorVisuals.Contains(Rec.ResourceClassPath))
        {
            continue;
        }
        FDonorVisual Donor;
        Donor.bIsFallback = Rec.bIsFallback;
        if (Rec.Kind == 1)
        {
            Donor.Kind = EDonorKind::Liquid;
            DonorVisuals.Add(Rec.ResourceClassPath, Donor);
            Rec.bIsFallback ? Fallback++ : Real++;
            continue;
        }
        // Solid: resolve the rock mesh + materials from their soft paths.
        Donor.Kind = EDonorKind::SolidMesh;
        UStaticMesh* Mesh = Rec.MeshPath.IsEmpty()
            ? nullptr : LoadObject<UStaticMesh>(nullptr, *Rec.MeshPath);
        if (!Mesh)
        {
            // Mesh asset unresolved (mod removed / path stale) — skip; a live capture
            // or the deposit-mesh fallback will cover this resource later.
            continue;
        }
        Donor.Mesh = Mesh;
        for (const FString& MatPath : Rec.MaterialPaths)
        {
            Donor.Materials.Add(MatPath.IsEmpty() ? nullptr : LoadObject<UMaterialInterface>(nullptr, *MatPath));
        }
        Donor.RelativeTransform = FTransform(FQuat::Identity, FVector::ZeroVector, Rec.Scale);
        DonorVisuals.Add(Rec.ResourceClassPath, Donor);
        Rec.bIsFallback ? Fallback++ : Real++;
    }
    if (Real + Fallback > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("Rehydrated %d persisted donors (%d real, %d fallback) from save — no deposit-mesh guess needed on reload"),
            Real + Fallback, Real, Fallback);
    }
}

bool ANodeShuffleSubsystem::ApplyNodeOwnMesh(AFGResourceNode* Node, UClass* ResourceClass, const FNodeShuffleEntry& Entry)
{
    // LIQUID (oil): no rock to stamp — rebuild the node's native decal from its
    // descriptor. The caller (spawn path) has already InitResource'd the node to
    // the liquid resource, so the descriptor is live. Return true (handled).
    if (const FDonorVisual* LD = DonorVisuals.Find(ResourceClass->GetPathName()))
    {
        if (LD->Kind == EDonorKind::Liquid)
        {
            RebuildNodeNativeVisual(Node);
            return true;
        }
    }
    // FIX 1 (no double visual on self-meshed nodes): a node may own MORE THAN ONE
    // static mesh component. AllMinable/AlkaLib modded nodes (Res_Plastic2_C, the
    // lithium BP_ResourdeNode_Alkali, ...) carry their OWN base rock mesh component
    // (the shared ResourceNode_quartz). The old code stamped the donor onto whatever
    // FindComponentByClass returned FIRST and left every other node-owned mesh
    // VISIBLE — so the node showed two rocks (the correct donor + the leftover
    // quartz). Proof from the orphan diag: 'ResourceNode_quartz' KEPT -> owned by
    // live Res_Plastic2_C at 0.0m.
    //
    // The donor must REPLACE the node's visual, not add a second one. So: collect
    // every node-OWNED static mesh component, stamp the donor onto ONE of them
    // (preferring one that already carries a node-rock mesh — that IS the base rock),
    // and HIDE/clear every OTHER node-owned rock component so exactly ONE correct
    // rock shows. Instanced components are shared across many rocks and are never
    // touched. Ore-style nodes (no own mesh) fall back to the swept separate rock;
    // brand-new spawned nodes get a fresh NodeShuffleVisual.
    TArray<FString> ExtraRockPatterns;
    LoadExtraRockPatterns(ExtraRockPatterns);
    TArray<UStaticMeshComponent*> OwnedMeshes;
    {
        TArray<UStaticMeshComponent*> All;
        Node->GetComponents<UStaticMeshComponent>(All);
        for (UStaticMeshComponent* Candidate : All)
        {
            if (!IsValid(Candidate) || Cast<UInstancedStaticMeshComponent>(Candidate))
            {
                continue; // never re-mesh/hide a shared instanced component
            }
            OwnedMeshes.Add(Candidate);
        }
        // Stamp target preference: a component already wearing a recognized node-rock
        // mesh (the base rock we must REPLACE) wins; otherwise the first owned mesh.
        OwnedMeshes.StableSort([&](const UStaticMeshComponent& A, const UStaticMeshComponent& B)
        {
            const bool bARock = A.GetStaticMesh() && IsNodeRockMeshName(A.GetStaticMesh()->GetName(), ExtraRockPatterns);
            const bool bBRock = B.GetStaticMesh() && IsNodeRockMeshName(B.GetStaticMesh()->GetName(), ExtraRockPatterns);
            return bARock && !bBRock; // rock-mesh components first
        });
    }

    UStaticMeshComponent* Smc = OwnedMeshes.Num() > 0 ? OwnedMeshes[0] : nullptr;
    if (!Smc)
    {
        const TWeakObjectPtr<UStaticMeshComponent>* Rock = SweptRocks.Find(Entry.EntryGuid);
        Smc = Rock ? Rock->Get() : nullptr;
    }
    bool bCreatedComponent = false;
    if (!Smc && DonorVisuals.Contains(ResourceClass->GetPathName()))
    {
        // Spawned NEW nodes (never swept, no own mesh) get their own visual
        // component, dressed from donor visuals below.
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
    // Replace existing material slots and clear any that the donor mesh does not
    // override, so no leftover material from the base mesh bleeds through.
    for (int32 i = 0; i < Donor->Materials.Num(); i++)
    {
        if (UMaterialInterface* Mat = Donor->Materials[i].Get())
        {
            Smc->SetMaterial(i, Mat);
        }
    }
    Smc->SetWorldScale3D(Donor->RelativeTransform.GetScale3D());
    Smc->SetWorldLocation(Node->GetActorLocation());

    // FIX 1: now that ONE component wears the correct donor rock, HIDE every OTHER
    // node-owned mesh component that still shows a node-rock mesh (the leftover
    // quartz base mesh on self-meshed modded nodes). This is what kills the second
    // visual. We only hide recognized node-rock meshes — never some unrelated
    // child decoration the node might legitimately own. The stamped component (Smc)
    // is skipped. Idempotent: a re-skin re-runs this and the already-hidden leftover
    // stays hidden.
    {
        for (UStaticMeshComponent* Other : OwnedMeshes)
        {
            if (Other == Smc || !IsValid(Other) || !Other->GetStaticMesh())
            {
                continue;
            }
            // Only hide a leftover that is itself a recognized node rock AND is not
            // already the donor mesh (would be the correct rock, e.g. coincidental).
            if (Other->GetStaticMesh() == Donor->Mesh.Get())
            {
                continue;
            }
            if (IsNodeRockMeshName(Other->GetStaticMesh()->GetName(), ExtraRockPatterns) && Other->IsVisible())
            {
                Other->SetVisibility(false, true);
                Other->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                UE_LOG(LogNodeShuffle, Verbose,
                    TEXT("FIX 1: hid leftover base rock '%s' on self-meshed node %s (donor stamped on a separate component)"),
                    *Other->GetStaticMesh()->GetName(), *Node->GetName());
            }
        }
    }

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
        TEXT("Layout active: %d vanilla kept, %d vanilla hidden (reversibly deactivated, NOT destroyed), %d new spawnable, %d pinned"),
        ActiveVanilla, InactiveVanilla, ActiveNew, Pinned);
    for (const auto& Pair : ActivePerResource)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("  %s: %d active nodes"), *Pair.Key, Pair.Value);
    }
}

bool ANodeShuffleSubsystem::IsNodeRockMeshName(const FString& MeshName, const TArray<FString>& ExtraPatterns)
{
    // Same families SweepRockComponents uses to recognize node rocks.
    if (MeshName.StartsWith(TEXT("ResourceNode")) || MeshName.Contains(TEXT("ResourceNode"))
        || MeshName.StartsWith(TEXT("CoalResource")) || MeshName.StartsWith(TEXT("SulfurResource"))
        || MeshName.StartsWith(TEXT("Resource_")) || MeshName.StartsWith(TEXT("SAM_"))
        || (MeshName.StartsWith(TEXT("SM_")) && MeshName.Contains(TEXT("Node"))))
    {
        return true;
    }
    return ExtraPatterns.ContainsByPredicate([&MeshName](const FString& P){ return MeshName.StartsWith(P); });
}

void ANodeShuffleSubsystem::LoadExtraRockPatterns(TArray<FString>& OutPatterns) const
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
                if (V->TryGetString(S) && !S.IsEmpty()) { OutPatterns.Add(S); }
            }
        }
    }
}

void ANodeShuffleSubsystem::OrphanRockCleanup()
{
    // Re-rolling rebuilds the layout from a live node scan. A vanilla node that
    // was already destroyed (removed in an earlier layout — destruction is
    // saved) is not in that scan, so its separate ore-rock loses its entry and
    // no per-entry re-hide can ever reach it again: a permanent unmineable
    // ghost. Catch these by world state instead of entries — a mesh that is a
    // PROVEN node rock (captured from a live node of some resource) sitting with
    // NO live resource node and NO deposit nearby is an orphan. Active nodes,
    // new spawns and retypes always keep their node right at the rock, so this
    // never touches a rock that still has something behind it.
    const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    if (Now - LastOrphanSweepSeconds < RockResweepCooldownSeconds)
    {
        return;
    }

    TArray<FVector> Players;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (const APlayerController* Pc = It->Get())
        {
            if (const APawn* Pawn = Pc->GetPawn()) { Players.Add(Pawn->GetActorLocation()); }
        }
    }
    if (Players.Num() == 0)
    {
        return;
    }
    LastOrphanSweepSeconds = Now;

    // Meshes proven to be real node rocks (captured from live nodes at roll).
    TSet<UStaticMesh*> NodeRockMeshes;
    for (const auto& Pair : DonorVisuals)
    {
        if (UStaticMesh* M = Pair.Value.Mesh.Get()) { NodeRockMeshes.Add(M); }
    }
    // A mesh whose original node was destroyed in a PAST roll was never captured
    // as a donor (no live node carried it at this session's roll), so the donor
    // set above misses it — the reported orphan-rock ghost (e.g. Resource_Stone_01).
    // Also recognize rocks by NAME, the same families SweepRockComponents matches,
    // so name-matched orphans are hidden even without a donor capture.
    TArray<FString> ExtraPatterns;
    LoadExtraRockPatterns(ExtraPatterns);

    // Live nodes: a rock this close is owned, never an orphan. Keep the OWNING
    // ACTOR alongside its location so the per-rock reason log names exactly what
    // is protecting a rock that stays visible.
    //
    // FIX 3: a resource DEPOSIT (AFGResourceDeposit, which derives from
    // AFGResourceNode) must NOT protect a separate orphaned node-rock from being
    // hidden — that was the cause of lingering ghost rocks (e.g. Resource_Stone_01
    // "owned by live BP_ResourceDeposit_C at 1.3m"). So deposits are EXCLUDED from
    // the owner set: only actual resource NODES count as owners. A deposit's OWN
    // visual mesh is a child component of the deposit actor (mDepositMeshComponent),
    // so it is protected separately below by skipping deposit-owned meshes — we
    // never hide a deposit's own rock, but we do hide a free orphan rock near one.
    struct FOwner { FVector Loc; const AActor* Actor; };
    TArray<FOwner> Owners;
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        if (IsValid(*It) && !It->IsA<AFGResourceDeposit>()) { Owners.Add({It->GetActorLocation(), *It}); }
    }

    constexpr float OrphanOwnerRadius = 1200.0f;    // 12 m: a real node owns the rock
    constexpr float OrphanPlayerRange = 30000.0f;   // 300 m: only act on streamed-in rocks

    int32 Hidden = 0;
    for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
    {
        UStaticMeshComponent* Smc = *It;
        if (!IsValid(Smc) || Smc->GetWorld() != GetWorld() || !Smc->GetStaticMesh()
            || Cast<UInstancedStaticMeshComponent>(Smc))
        {
            continue;
        }
        // FIX 3: never hide a deposit's OWN visual mesh (it is a child component of
        // the deposit actor). Deposits no longer protect SEPARATE orphan rocks
        // (they were removed from the owner set above), but a deposit's own rock
        // must stay visible/minable — so skip any mesh owned by a deposit actor.
        if (Cast<AFGResourceDeposit>(Smc->GetOwner()))
        {
            continue;
        }
        const bool bProvenDonor = NodeRockMeshes.Contains(Smc->GetStaticMesh());
        const bool bNameMatch = IsNodeRockMeshName(Smc->GetStaticMesh()->GetName(), ExtraPatterns);
        if (!bProvenDonor && !bNameMatch)
        {
            continue; // neither a captured donor mesh nor a name-matched node rock — leave scenery alone
        }
        const FVector Loc = Smc->GetComponentLocation();
        bool bNearPlayer = false;
        for (const FVector& P : Players)
        {
            if (FVector::DistSquared2D(P, Loc) < FMath::Square(OrphanPlayerRange)) { bNearPlayer = true; break; }
        }
        if (!bNearPlayer)
        {
            continue; // too far to be a streamed-in concern; don't even log it
        }

        // Per-rock reason logging (FIX 2): for every rock-like mesh near a player,
        // state once exactly why it was hidden or left alone, so the next log
        // pinpoints any remaining ghost's blocking gate.
        const bool bReport = !OrphanReasonLogged.Contains(Smc);
        const FString MeshName = Smc->GetStaticMesh()->GetName();

        if (!Smc->IsVisible())
        {
            if (bReport)
            {
                OrphanReasonLogged.Add(Smc);
                UE_LOG(LogNodeShuffle, Verbose,
                    TEXT("ORPHANDIAG: '%s' at %s already hidden -> no action"), *MeshName, *Loc.ToCompactString());
            }
            continue;
        }

        const AActor* OwnerActor = nullptr;
        float OwnerDistSq = TNumericLimits<float>::Max();
        for (const FOwner& O : Owners)
        {
            const float D = FVector::DistSquared2D(O.Loc, Loc);
            if (D < OwnerDistSq) { OwnerDistSq = D; OwnerActor = O.Actor; }
        }
        const bool bOwned = OwnerActor != nullptr && OwnerDistSq < FMath::Square(OrphanOwnerRadius);

        if (bOwned)
        {
            if (bReport)
            {
                OrphanReasonLogged.Add(Smc);
                UE_LOG(LogNodeShuffle, Verbose,
                    TEXT("ORPHANDIAG: '%s' at %s KEPT (donor=%d name=%d) -> owned by live %s '%s' at %.1fm"),
                    *MeshName, *Loc.ToCompactString(), bProvenDonor ? 1 : 0, bNameMatch ? 1 : 0,
                    OwnerActor ? *OwnerActor->GetClass()->GetName() : TEXT("?"),
                    OwnerActor ? *OwnerActor->GetName() : TEXT("?"),
                    FMath::Sqrt(OwnerDistSq) / 100.0f);
            }
            continue; // a live node or deposit is right on this rock — not an orphan
        }

        Smc->SetVisibility(false, true);
        Smc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if (bReport)
        {
            OrphanReasonLogged.Add(Smc);
            UE_LOG(LogNodeShuffle, Verbose,
                TEXT("ORPHANDIAG: '%s' at %s HIDDEN (donor=%d name=%d) -> nearest live node/deposit %.1fm away (> %.0fm owner radius)"),
                *MeshName, *Loc.ToCompactString(), bProvenDonor ? 1 : 0, bNameMatch ? 1 : 0,
                OwnerActor ? FMath::Sqrt(OwnerDistSq) / 100.0f : -1.0f, OrphanOwnerRadius / 100.0f);
        }
        Hidden++;
    }
    if (Hidden > 0)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("Orphan cleanup: hid %d node-rock meshes with no node behind them"), Hidden);
    }
}

void ANodeShuffleSubsystem::DiagnoseRocksNearPlayers()
{
    // Catch-all culprit-namer (per the world-modification playbook): for every
    // rock-like mesh near a player, log its name + visibility and the nearest
    // layout entry's distance/state/pairing. Walk onto a ghost and the line
    // tells us exactly why it wasn't hidden (too far to pair? claimed? active?).
    // Each component is reported once per session.
    TArray<FVector> Players;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (const APlayerController* Pc = It->Get())
        {
            if (const APawn* Pawn = Pc->GetPawn()) { Players.Add(Pawn->GetActorLocation()); }
        }
    }
    if (Players.Num() == 0)
    {
        return;
    }

    constexpr float DiagRange = 1500.0f; // 15 m around the player

    // Build the set of currently-claimed rock components for the paired check.
    TSet<UStaticMeshComponent*> Claimed;
    for (const auto& Pair : SweptRocks)
    {
        if (Pair.Value.IsValid()) { Claimed.Add(Pair.Value.Get()); }
    }

    for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
    {
        UStaticMeshComponent* Smc = *It;
        if (!IsValid(Smc) || Smc->GetWorld() != GetWorld() || !Smc->GetStaticMesh()
            || Cast<UInstancedStaticMeshComponent>(Smc))
        {
            continue;
        }
        const FVector Loc = Smc->GetComponentLocation();
        bool bNear = false;
        for (const FVector& P : Players)
        {
            if (FVector::DistSquared(P, Loc) < FMath::Square(DiagRange)) { bNear = true; break; }
        }
        if (!bNear || DiagnosedComponents.Contains(Smc))
        {
            continue;
        }

        const FString MeshName = Smc->GetStaticMesh()->GetName();
        // Only rock-like meshes: matches the sweep's name families, OR its mesh
        // is one of our captured donor rocks (a rock we re-skinned ourselves).
        bool bLooksLikeRock = IsNodeRockMeshName(MeshName, TArray<FString>());
        if (!bLooksLikeRock)
        {
            for (const auto& Pair : DonorVisuals)
            {
                if (Pair.Value.Mesh.Get() == Smc->GetStaticMesh()) { bLooksLikeRock = true; break; }
            }
        }
        if (!bLooksLikeRock)
        {
            continue;
        }

        // Nearest layout entry to this rock.
        int32 BestIdx = INDEX_NONE;
        float BestDistSq = TNumericLimits<float>::Max();
        for (int32 i = 0; i < Layout.Num(); i++)
        {
            const float D = FVector::DistSquared2D(Layout[i].Location, Loc);
            if (D < BestDistSq) { BestDistSq = D; BestIdx = i; }
        }

        DiagnosedComponents.Add(Smc);
        if (BestIdx == INDEX_NONE)
        {
            UE_LOG(LogNodeShuffle, Verbose, TEXT("ROCKDIAG: mesh '%s' vis=%d at %s | NO layout entries"),
                *MeshName, Smc->IsVisible() ? 1 : 0, *Loc.ToCompactString());
            continue;
        }
        const FNodeShuffleEntry& E = Layout[BestIdx];
        const bool bPaired = Claimed.Contains(Smc);
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("ROCKDIAG: mesh '%s' vis=%d at %s | nearest entry %.1fm: active=%d new=%d pinned=%d paired-to-this=%d assigned=%s orig=%s"),
            *MeshName, Smc->IsVisible() ? 1 : 0, *Loc.ToCompactString(),
            FMath::Sqrt(BestDistSq) / 100.0f, E.bActive ? 1 : 0, E.bIsNewNode ? 1 : 0, E.bPinned ? 1 : 0,
            bPaired ? 1 : 0,
            *FPackageName::ObjectPathToObjectName(E.AssignedResourceClassPath),
            *FPackageName::ObjectPathToObjectName(E.OriginalResourceClassPath));
    }
}
