#include "NodeShuffleSubsystem.h"
#include "NodeShuffle.h"
#include "NodeShuffleConfig.h"
#include "NodeShuffleNodeAssets.h"
#include "NodeShuffleResourceNode.h"

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
#include "Materials/MaterialInstance.h"
#include "Materials/Material.h"
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
#include "Resources/FGResourceNodeManager.h"
#include "Resources/FGResourceNodeFrackingCore.h"
#include "Resources/FGResourceNodeFrackingSatellite.h"
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
    // redesign-5: our spawned rock is now a RockMesh subobject OF ANodeShuffleResourceNode, so scans
    // exclude our rocks by OWNER TYPE (IsA<ANodeShuffleResourceNode>) — the old ManagedRockTag (a Tag
    // on a separate rock actor) is gone along with the separate-actor machinery.
    // redesign-3: spawned nodes are identified by TYPE (ANodeShuffleResourceNode) with a
    // UPROPERTY(SaveGame) EntryGuid — Tags don't survive reload, so the old SpawnedNodeTag was dropped.
    // FIX 4: modded "advanced" nodes (AlkaLib lithium) can report a purity outside
    // {Impure,Normal,Pure}. Eligibility now admits them; normalize that out-of-range
    // value to RP_Normal everywhere the roll captures purity so the node shuffles
    // with a sane, mineable purity (and never feeds a bogus value into the deck).
    FORCEINLINE EResourcePurity NormalizePurity(EResourcePurity P)
    {
        return (P == RP_Inpure || P == RP_Normal || P == RP_Pure) ? P : RP_Normal;
    }
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

    // redesign-1 STARTER NODES: capture the player's REAL spawn location as early as possible on a
    // BRAND-NEW game (before the layout is first rolled, before the player wanders). Works with
    // random-start mods because it reads the live pawn, not a hardcoded spawn. Captured once, saved.
    if (!bPlayerStartCaptured && !bLayoutGenerated)
    {
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            const APlayerController* Pc = It->Get();
            const APawn* Pawn = Pc ? Pc->GetPawn() : nullptr;
            if (Pawn)
            {
                PlayerStartLocation = Pawn->GetActorLocation();
                bPlayerStartCaptured = true;
                UE_LOG(LogNodeShuffle, Display, TEXT("Captured player start location %s for starter nodes"),
                    *PlayerStartLocation.ToCompactString());
                break;
            }
        }
    }

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
    // redesign-1: a re-roll restarts from a pristine world. Every original node that this layout
    // suppressed (hid whole-actor) is UN-HIDDEN so the new roll starts clean — the new roll then
    // re-hides per its own record. Nothing was ever retyped in place or destroyed (Hide & Replace
    // never touches an original's resource), so this is purely an un-hide. The reroll pool itself
    // is rebuilt from the saved Layout's stored ORIGINAL resources (streaming-independent), not a
    // live rescan, so we do not need the originals' live resource data here.
    // redesign-3b FOLD-IN 1: clear the scanner-deregister tracking so an un-hidden original can re-register
    // its scanner representation under the new roll (the new layout may keep this spot active). Harmless
    // now (a spot the new roll hides again is simply re-deregistered), but correct for re-roll cleanliness.
    ScannerDeregistered.Reset();

    int32 Unhidden = 0;
    for (const FNodeShuffleSuppressedOriginal& Rec : OriginalNodeRecord)
    {
        // redesign-6 FIX 2: Base-aware so esc_ (Base-only) originals are un-hidden on re-roll too.
        AFGResourceNodeBase* Node = FindOriginalBaseByPath(Rec.VanillaNodePath);
        if (!IsValid(Node)) { continue; } // not streamed in (or genuinely gone) — nothing to un-hide
        if (Node->IsHidden() || !Node->GetActorEnableCollision())
        {
            Node->SetActorEnableCollision(true);
            Node->SetActorHiddenInGame(false);
            // Re-register its scanner rep so an un-hidden original pings again until the new roll decides.
            Node->UpdateNodeRepresentation();
            if (AFGNodeMeshActor* MeshActor = FindMeshActorForNode(Node))
            {
                MeshActor->SetActorHiddenInGame(false);
                MeshActor->SetActorEnableCollision(true);
            }
            Unhidden++;
        }
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("Re-roll restore: un-hid %d suppressed original nodes (world reset to pristine before the new roll re-hides)"),
        Unhidden);
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
        // redesign-6 FIX 3 (REROLL COLLAPSE). In the Hide & Replace model every UNOCCUPIED original was
        // converted at the initial roll into a bIsNewNode SPAWNED entry (its VanillaNodePath emptied) —
        // only OCCUPIED/pinned originals remain as !bIsNewNode entries. So the old reroll loop, which
        // rebuilt from !bIsNewNode entries ONLY, saw just the few pinned ones and COLLAPSED the pool
        // (732 -> 349). The complete original pool = EVERY entry that carries an original resource:
        //   - pinned originals (kept as !bIsNewNode), AND
        //   - every spawned entry (bIsNewNode) — each one IS a relocated original carrying its
        //     OriginalResourceClassPath / OriginalPurity / ResourceForm.
        // Rebuild the FULL pool from BOTH so counts/floors/purity match the initial roll. Locations are
        // re-randomized below regardless, so we don't need the exact original location here.
        for (const FNodeShuffleEntry& Old : Layout)
        {
            // An entry contributes to the original pool if it carries an original resource. Pinned
            // (occupied) originals stay in place; everything else becomes a fresh original-pool member
            // that the deck below re-shuffles + relocates.
            if (Old.OriginalResourceClassPath.IsEmpty())
            {
                continue; // no original resource recorded (e.g. a dropped/inactive remnant) — skip
            }

            // Occupancy for pinned originals comes from the LIVE node when streamed in (Base-aware).
            AFGResourceNodeBase* Live = Old.VanillaNodePath.IsEmpty() ? nullptr : FindOriginalBaseByPath(Old.VanillaNodePath);
            AFGResourceNode* LiveNode = Cast<AFGResourceNode>(Live);
            const bool bOccupied = (IsValid(Live) && Live->IsOccupied())
                || (LiveNode && PortableMinerNodes.Contains(LiveNode));

            FNodeShuffleEntry E;
            E.EntryGuid = FGuid::NewGuid();
            E.bIsNewNode = false;                 // re-enters the pool as an original; conversion re-runs below
            E.VanillaNodePath = Old.VanillaNodePath; // empty for already-relocated originals (fine — they hide via record)
            E.Location = Old.Location;
            E.Rotation = Old.Rotation;
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
            VanillaResourceCounts.FindOrAdd(Old.OriginalResourceClassPath)++;
            FormByResource.FindOrAdd(Old.OriginalResourceClassPath) =
                Old.ResourceForm != 0 ? Old.ResourceForm : FormSolid;
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
        // A re-roll starts a clean record (the conversion below re-populates OriginalNodeRecord for the
        // unoccupied originals it re-detaches). Without this, stale records from the prior layout linger.
        OriginalNodeRecord.Reset();
        UE_LOG(LogNodeShuffle, Display,
            TEXT("Re-roll pool (redesign-6): rebuilt %d original entries from the FULL saved layout (pinned + all relocated), %d resource kinds — no collapse"),
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

        // MODDED-SOLID augment (RelocateModdedNodes). The reroll pool is rebuilt from the saved Layout,
        // which was captured with modded SOLID nodes (AllMinable esc_ item nodes) left IN PLACE — the
        // initial roll admits them but a reroll, rebuilding from the layout, never re-introduces them,
        // so they perpetually stay put. Mirror the experimental-form augment above, but for MODDED SOLID
        // originals: live-scan the BASE class (esc_ nodes are NOT AFGResourceNode), admit eligible modded
        // solids not already in the pool, and add them as real entries so the deal/convert/suppress path
        // relocates them like vanilla. Gated behind the experimental RelocateModdedNodes toggle (default
        // off) so stable behavior is byte-for-byte preserved. Liquid/gas modded nodes are handled by the
        // experimental-form augment above; this is SOLID-only.
        if (Config.RelocateModdedNodes && Config.IncludeModdedNodes)
        {
            int32 ModdedSolidAdded = 0;
            for (TActorIterator<AFGResourceNodeBase> It(GetWorld()); It; ++It)
            {
                AFGResourceNodeBase* BaseNode = *It;
                if (!IsValid(BaseNode) || IsFrackingActor(BaseNode)) { continue; }
                if (BaseNode->IsA<ANodeShuffleResourceNode>()) { continue; } // never our own spawned nodes
                const UClass* RC = BaseNode->GetResourceClass();
                if (!RC) { continue; }
                // MODDED only: resource class OR node class outside /Game/ (vanilla solids are already in the pool).
                const bool bModded = !RC->GetPathName().StartsWith(TEXT("/Game/"))
                    || !BaseNode->GetClass()->GetPathName().StartsWith(TEXT("/Game/"));
                if (!bModded) { continue; }
                // SOLID only here (liquid/gas modded handled above); plain Node type only (no fracking/geyser).
                const EResourceForm BF = BaseNode->GetResourceForm();
                if (BF == EResourceForm::RF_LIQUID || BF == EResourceForm::RF_GAS) { continue; }
                if (BaseNode->GetResourceNodeType() != EResourceNodeType::Node) { continue; }
                const FString Path = BaseNode->GetPathName();
                if (ExistingPaths.Contains(Path)) { continue; }
                ExistingPaths.Add(Path);

                AFGResourceNode* Node = Cast<AFGResourceNode>(BaseNode); // null for Base-only esc_ nodes
                const bool bOccupied = BaseNode->IsOccupied() || (Node && PortableMinerNodes.Contains(Node));
                const EResourcePurity NodePurity = Node ? Node->GetResourcePurity() : RP_Normal;

                FNodeShuffleEntry E;
                E.EntryGuid = FGuid::NewGuid();
                E.bIsNewNode = false;
                E.VanillaNodePath = Path;
                E.Location = BaseNode->GetActorLocation();
                E.Rotation = BaseNode->GetActorRotation();
                E.OriginalResourceClassPath = RC->GetPathName();
                E.OriginalPurity = NormalizePurity(NodePurity);
                E.AssignedResourceClassPath = E.OriginalResourceClassPath;
                E.AssignedPurity = E.OriginalPurity;
                E.ResourceForm = FormSolid;
                E.bPinned = bOccupied;
                E.bActive = true;
                E.NodeClassPath = BaseNode->GetClass()->GetPathName();
                NewLayout.Add(E);

                VanillaLocations.Add(E.Location);
                VanillaResourceCounts.FindOrAdd(E.OriginalResourceClassPath)++;
                FormByResource.FindOrAdd(E.OriginalResourceClassPath) = FormSolid;
                if (E.OriginalPurity != RP_MAX) { PurityDeckSource.Add(E.OriginalPurity); }
                if (SpawnableNodeClassPath.IsEmpty()) { SpawnableNodeClassPath = E.NodeClassPath; }
                VanillaCount++;
                ModdedSolidAdded++;
            }
            UE_LOG(LogNodeShuffle, Display,
                TEXT("Modded-solid augment (RelocateModdedNodes ON): added %d live modded SOLID nodes not in the saved pool to the reroll pool (they now relocate like vanilla; captured into the layout)"),
                ModdedSolidAdded);
        }
    }
    else
    {
        // INITIAL roll: live scan of the (fully streamed) vanilla pool.
        // Experimental forms (liquid oil AND gas, e.g. lithium) join only when the
        // experimental flag is on; the stable mod is solid-only.
        const bool bIncludeLiquid = Config.EnableExperimentalFeatures;

        // redesign-6 FIX 2: one-shot esc_/AllMinable class-hierarchy diagnostic (names their real type).
        DiagnoseEscClassHierarchy();

        // redesign-6 FIX 2: BROADEN the pool scan from TActorIterator<AFGResourceNode> to the BASE class
        // AFGResourceNodeBase — the analyst proved esc_ nodes are NOT AFGResourceNode (so the old iterator
        // never saw them). Fracking cores/satellites also derive from Base; they're excluded by node-type
        // in eligibility (and IsFrackingActor). A Base node that is NOT an AFGResourceNode (a candidate
        // esc_ node) is admitted via the modded path with sane defaults (purity Normal, form from Base).
        for (TActorIterator<AFGResourceNodeBase> It(GetWorld()); It; ++It)
        {
            AFGResourceNodeBase* BaseNode = *It;
            if (!IsValid(BaseNode) || IsFrackingActor(BaseNode)) { continue; }
            // Skip our own spawned nodes (they're AFGResourceNode subclass; eligibility also excludes them).
            if (BaseNode->IsA<ANodeShuffleResourceNode>()) { continue; }
            AFGResourceNode* Node = Cast<AFGResourceNode>(BaseNode); // may be null for Base-only esc_ nodes

            // redesign-5/6 UPSTREAM-SCAN: log once per distinct node-class|resource-class, EVERY base node
            // the iterator sees BEFORE eligibility — class paths, AFGResourceNode-or-not, form, node-type.
            const UClass* RC = BaseNode->GetResourceClass();
            const FString DiagKey = BaseNode->GetClass()->GetPathName()
                + TEXT("|") + (RC ? RC->GetPathName() : TEXT("<null>"));
            if (!UpstreamScanLogged.Contains(DiagKey))
            {
                UpstreamScanLogged.Add(DiagKey);
                const bool bResVanilla = RC && RC->GetPathName().StartsWith(TEXT("/Game/"));
                const bool bNodeClassVanilla = BaseNode->GetClass()->GetPathName().StartsWith(TEXT("/Game/"));
                const TCHAR* Branch = (bResVanilla && bNodeClassVanilla) ? TEXT("vanilla-strict") : TEXT("modded-gate");
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("UPSTREAM-SCAN: nodeClass='%s' resClass='%s' isResNode=%d isResDesc=%d form=%d nodeType=%d -> branch=%s"),
                    *BaseNode->GetClass()->GetPathName(), RC ? *RC->GetPathName() : TEXT("<null>"),
                    Node ? 1 : 0,
                    (RC && RC->IsChildOf(UFGResourceDescriptor::StaticClass())) ? 1 : 0,
                    (int32)BaseNode->GetResourceForm(), (int32)BaseNode->GetResourceNodeType(), Branch);
            }

            // ELIGIBILITY. AFGResourceNode -> full predicate. Base-only (esc_) -> Base-aware modded admit.
            if (Node)
            {
                if (!IsEligibleVanillaNode(Node, Config.IncludeModdedNodes, bIncludeLiquid)) { continue; }
            }
            else
            {
                // Base-only node (esc_ candidate). Admit only when modded is on, it's plain Node-type, has
                // a resource class, and is solid/unknown form (experimental gates liquid/gas). Keeps junk out.
                if (!Config.IncludeModdedNodes) { continue; }
                if (!RC) { continue; }
                if (BaseNode->GetResourceNodeType() != EResourceNodeType::Node) { continue; }
                const EResourceForm BF = BaseNode->GetResourceForm();
                const bool bExp = (BF == EResourceForm::RF_LIQUID) || (BF == EResourceForm::RF_GAS);
                if (bExp && !bIncludeLiquid) { continue; } // liquid/gas only under experimental
            }

            const bool bOccupied = BaseNode->IsOccupied() || (Node && PortableMinerNodes.Contains(Node));

            // redesign-6 FIX 2: build the entry off the BASE node (works for both AFGResourceNode and a
            // Base-only esc_ node). Purity is Node-only — default RP_Normal for Base-only nodes.
            const EResourcePurity NodePurity = Node ? Node->GetResourcePurity() : RP_Normal;

            FNodeShuffleEntry E;
            E.EntryGuid = FGuid::NewGuid();
            E.bIsNewNode = false;
            E.VanillaNodePath = BaseNode->GetPathName();
            E.Location = BaseNode->GetActorLocation();
            E.Rotation = BaseNode->GetActorRotation();
            E.OriginalResourceClassPath = BaseNode->GetResourceClass() ? BaseNode->GetResourceClass()->GetPathName() : FString();
            E.OriginalPurity = NormalizePurity(NodePurity);
            E.AssignedResourceClassPath = E.OriginalResourceClassPath;
            E.AssignedPurity = E.OriginalPurity;
            E.ResourceForm = (BaseNode->GetResourceForm() == EResourceForm::RF_LIQUID) ? FormLiquid : FormSolid;
            E.bPinned = bOccupied;
            E.bActive = true;
            E.NodeClassPath = BaseNode->GetClass()->GetPathName();
            NewLayout.Add(E);

            VanillaLocations.Add(BaseNode->GetActorLocation());
            if (const UClass* Res = BaseNode->GetResourceClass())
            {
                VanillaResourceCounts.FindOrAdd(Res->GetPathName())++;
                FormByResource.FindOrAdd(Res->GetPathName()) = E.ResourceForm;
            }
            PurityDeckSource.Add(E.OriginalPurity);
            if (E.ResourceForm == FormLiquid)
            {
                if (SpawnableLiquidNodeClassPath.IsEmpty()) { SpawnableLiquidNodeClassPath = BaseNode->GetClass()->GetPathName(); }
            }
            else if (SpawnableNodeClassPath.IsEmpty())
            {
                SpawnableNodeClassPath = BaseNode->GetClass()->GetPathName();
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

    // redesign-1 (Hide & Replace) CONVERSION. The deck above dealt resources onto the original
    // node SLOTS, preserving counts/floors/purity. Now we DETACH every unoccupied original from its
    // location: its resource lives on as one of OUR relocated spawned nodes, and the original node
    // itself is recorded for whole-actor hiding (SuppressOriginalNodes). Occupied/pinned originals
    // stay exactly where they are, 100% untouched (save-safety — built miners keep working).
    //
    // Build OriginalNodeRecord here (not in CaptureOriginalNodeRecord) because after this conversion
    // the unoccupied originals are no longer present in Layout as non-new entries.
    OriginalNodeRecord.Reset();
    {
        // Count the unoccupied originals that need a relocated home (active ones carry a resource;
        // inactive ones are simply hidden and contribute nothing to the spawned pool).
        int32 NeedRelocation = 0;
        for (const FNodeShuffleEntry& E : NewLayout)
        {
            if (!E.bIsNewNode && !E.bPinned && E.bActive) { NeedRelocation++; }
        }
        // Generate that many additional grounded, non-overlapping relocated locations.
        TArray<FVector> RelocSpots;
        if (NeedRelocation > 0)
        {
            // Avoid everything already placed: the map pool, occupied/kept, original-record (none yet),
            // and the new-node locations we already generated this roll.
            TArray<FVector> RelocAvoid = AvoidLocations;
            for (const FNodeShuffleEntry& E : NewLayout)
            {
                if (E.bIsNewNode) { RelocAvoid.Add(E.Location); }
            }
            GenerateNewLocations(Rng, VanillaLocations, RelocAvoid, NeedRelocation, RelocSpots);
        }

        int32 RelocCursor = 0;
        int32 Converted = 0, Dropped = 0, KeptOccupied = 0, Recorded = 0;
        for (FNodeShuffleEntry& E : NewLayout)
        {
            if (E.bIsNewNode) { continue; }
            if (E.bPinned)
            {
                // Occupied original: left in place, untouched. NOT recorded (never hidden).
                KeptOccupied++;
                continue;
            }
            // Record this unoccupied original for whole-actor hiding (vanilla AND modded).
            if (!E.VanillaNodePath.IsEmpty())
            {
                FNodeShuffleSuppressedOriginal Rec;
                Rec.VanillaNodePath = E.VanillaNodePath;
                Rec.Location = E.Location;
                Rec.bModdedOrigin = !E.OriginalResourceClassPath.StartsWith(TEXT("/Game/"));
                OriginalNodeRecord.Add(Rec);
                Recorded++;
            }
            if (E.bActive && RelocCursor < RelocSpots.Num())
            {
                // Convert to one of OUR relocated spawned nodes carrying the dealt resource.
                // redesign-6 FIX 3: KEEP VanillaNodePath (the original's identity) on the converted
                // spawned entry — emptying it lost the link needed to re-hide that original on a future
                // re-roll (which collapsed the reroll pool). A bIsNewNode entry never uses the path for
                // spawning (it keys on EntryGuid), so keeping it is harmless and preserves the pool.
                E.bIsNewNode = true;
                E.Location = RelocSpots[RelocCursor++];
                E.Rotation = FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f);
                E.bRayCasted = false; // re-settle onto terrain at the new spot
                E.OverlapNudges = 0;
                if (E.NodeClassPath.IsEmpty()) { E.NodeClassPath = SpawnableNodeClassPath; }
                Converted++;
            }
            else
            {
                // Inactive (or no relocation slot left): this original just gets hidden, no spawn.
                E.bActive = false;
                Dropped++;
            }
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("Hide & Replace conversion: %d unoccupied originals recorded for hiding, %d relocated to spawned nodes, %d left hidden-only, %d occupied originals kept in place"),
            Recorded, Converted, Dropped, KeptOccupied);
    }
    // Remove the now-detached inactive originals from the layout (they are pure hide records now —
    // keeping them as non-new inactive entries would do nothing and confuse the apply loop).
    NewLayout.RemoveAll([](const FNodeShuffleEntry& E){ return !E.bIsNewNode && !E.bPinned; });

    // redesign-1 STARTER NODES: on the FIRST roll of a BRAND-NEW game only, append a starter set
    // near the captured player-start so the early game is always playable. Never on a re-roll or an
    // existing save (bStarterNodesPlaced gates it; bIsReroll excludes re-rolls).
    if (!bIsReroll && Config.EnableStarterNodes && !bStarterNodesPlaced && bPlayerStartCaptured)
    {
        TArray<FVector> StarterAvoid = AvoidLocations;
        for (const FNodeShuffleEntry& E : NewLayout) { if (E.bIsNewNode) { StarterAvoid.Add(E.Location); } }
        AppendStarterNodes(NewLayout, Rng, StarterAvoid);
        bStarterNodesPlaced = true;
    }

    NewLayout.Append(CarriedEntries);
    Layout = MoveTemp(NewLayout);
    SavedSeed = Seed;
    bLayoutGenerated = true;
    bDidInitialApply = false;

    // redesign-1: OriginalNodeRecord was already built during the Hide & Replace conversion above
    // (it captures every unoccupied original — vanilla AND modded — before they were detached).

    // Live spawned actors from a previous layout die with the re-roll — EXCEPT occupied ones, which
    // were carried (pinned) above and MUST survive so the player's miner keeps its node (save-safety).
    if (bIsReroll)
    {
        // GUIDs the new layout still keeps as carried/pinned occupied spawned nodes.
        TSet<FGuid> KeptGuids;
        for (const FNodeShuffleEntry& E : CarriedEntries) { KeptGuids.Add(E.EntryGuid); }

        for (auto It = SpawnedNodes.CreateIterator(); It; ++It)
        {
            if (KeptGuids.Contains(It->Key)) { continue; } // keep the occupied node alive
            // redesign-5: the visual rock is a subobject OF the node now, so destroying the node destroys
            // its rock — no separate rock actor to clean up.
            if (IsValid(It->Value)) { It->Value->Destroy(); }
            It.RemoveCurrent();
        }
        MeshActorCache.Reset();
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
    // Push the diagnostics toggle to the module so the HOLOGRAMHOOK logging gate tracks the
    // config live (this pass runs on the discovery tick). OFF by default → clean user logs.
    FNodeShuffleModule::SetDiagnosticsEnabled(Config.EnableDiagnostics);
    // Spawn-on-discovery radius (config metres -> cm). Clamp to a sane floor so a
    // mis-set 0 never disables all spawning.
    const float SpawnRadiusCm = FMath::Max(10000.f, static_cast<float>(Config.SpawnRadiusMeters) * 100.f);

    // Refresh the vanilla path cache EVERY pass (not just when empty). The cache feeds
    // FindOriginalBaseByPath, which SuppressOriginalNodes uses to hide relocated originals. The old
    // "build once when empty" logic only captured originals STREAMED IN at the first pass, so any
    // original that streamed in later (as the player explored to it) was never cached → never hidden →
    // it lingered visible at its old spot next to its relocated copy (the "un-hidden duplicate" bug,
    // very visible after a big re-roll). Scan the BASE class (so esc_/Base-only originals are cached
    // too) and incrementally ADD any newly-streamed original; also REFRESH an entry whose weak ptr went
    // invalid (a node that unstreamed then re-streamed gets a new actor at the same path). Existing
    // valid entries are untouched. Cost is one actor iteration per discovery tick — negligible.
    int32 CacheAdded = 0;
    for (TActorIterator<AFGResourceNodeBase> It(GetWorld()); It; ++It)
    {
        if (It->IsA<ANodeShuffleResourceNode>()) { continue; } // our own spawned (relocated) nodes, not originals
        TWeakObjectPtr<AFGResourceNodeBase>& Slot = VanillaNodeCache.FindOrAdd(It->GetPathName());
        if (!Slot.IsValid()) { Slot = *It; CacheAdded++; }
    }
    if (FNodeShuffleModule::AreDiagnosticsEnabled() && CacheAdded > 0)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("CACHE-REFRESH: +%d newly-streamed originals cached (total %d) — now hidable"),
            CacheAdded, VanillaNodeCache.Num());
    }

    // redesign-2 FIX 1: ONE-TIME adopt-on-load — map GUID-tagged restored spawned nodes back into
    // SpawnedNodes so EnsureNewNodeSpawned never double-spawns a node the save already restored.
    if (!bAdoptedRestoredNodes)
    {
        AdoptRestoredSpawnedNodes();
        bAdoptedRestoredNodes = true;
    }

    // redesign-1: pair each original node to its own AFGNodeMeshActor so SuppressOriginalNodes can
    // hide the paired mesh actor when an unoccupied original streams in. Rebuilt each pass.
    RebuildMeshActorCache();

    // redesign-1: reset per-pass spawned-visual coverage counters (logged at pass end).
    SpawnedRockVanilla = 0;
    SpawnedRockQuartz = 0;
    SpawnedRockLiquid = 0;

    // redesign-1 (Hide & Replace) APPLY MODEL. The layout now has exactly two kinds of entry:
    //   - bIsNewNode == true  : one of OUR relocated/spawned nodes (the resource pool, re-homed).
    //                           Spawn it on discovery (raycast-settled, overlap-nudged, mineable +
    //                           scannable, with our own mesh actor at one transform we control).
    //   - bIsNewNode == false : an OCCUPIED/PINNED original (vanilla or modded) we leave 100%
    //                           UNTOUCHED (save-safety — the player's miners keep working). Every
    //                           UNOCCUPIED original was turned into a spawned entry at roll time and
    //                           is hidden whole-actor by SuppressOriginalNodes below.
    for (FNodeShuffleEntry& Entry : Layout)
    {
        if (Entry.bIsNewNode)
        {
            // redesign-3 BUG B (occupancy pin): if a player built a miner/extractor on this spawned
            // node, pin it so it is NEVER relocated again (settle/re-roll skip it). Persist the pin on
            // the node itself (bNodeShuffleOccupiedPinned) so it survives further reloads.
            if (AFGResourceNode* const* Live = SpawnedNodes.Find(Entry.EntryGuid))
            {
                if (IsValid(*Live) && !Entry.bPinned && IsNodeOccupiedAnyway(*Live))
                {
                    Entry.bPinned = true;
                    Entry.bRayCasted = true;
                    Entry.Location = (*Live)->GetActorLocation(); // lock the entry to where the miner is
                    if (ANodeShuffleResourceNode* Ours = Cast<ANodeShuffleResourceNode>(*Live))
                    {
                        Ours->bNodeShuffleOccupiedPinned = true;
                    }
                    UE_LOG(LogNodeShuffle, Verbose, TEXT("Pinned occupied spawned node %s (miner built — never relocate)"),
                        *Entry.EntryGuid.ToString());
                }
            }
            // SPAWN-ON-DISCOVERY: materialize an active relocated node once a player is within
            // SpawnRadius and the terrain has streamed in. Far nodes stay as data until explored.
            if (Entry.bActive && IsLocationNearAnyPlayer(Entry.Location, SpawnRadiusCm))
            {
                EnsureNewNodeSpawned(Entry, bChangedWorld);
            }
            continue;
        }

        // Non-new entry = an occupied/pinned original. Left untouched. (Defensive: if it somehow
        // became un-pinned and un-occupied it will be hidden by SuppressOriginalNodes, since its
        // location is in OriginalNodeRecord whenever it is not occupied.)
    }

    SettleNewNodesNearPlayers();
    ReassociateOrphanedExtractors();

    // redesign-9 SNAPDIAG: one-shot collision/component comparison of a spawned node vs a vanilla node,
    // so the log names the exact delta the extractor hologram detects (Mk1 works on vanilla, not ours).
    DiagnoseSnapState();

    // redesign-12 VALIDDIAG: one-shot OURS-vs-VANILLA on the extractor hologram's VALIDATION-gate
    // props/methods (collision + registration already ruled out; the gate is acceptance/validation).
    DiagnoseValidationGate();

    // redesign-1 CORE: hide EVERY unoccupied original node (vanilla AND modded) + its rock whenever
    // it streams in. This is what removes the world's original nodes so only our relocated nodes and
    // the untouched occupied originals remain. Reliable across sessions via the persistent record.
    SuppressOriginalNodes();

    OrphanRockCleanup();

    // Once new nodes materialize we KEEP them for the session (far-node despawn was removed long ago).

    if (SpawnedRockVanilla + SpawnedRockQuartz + SpawnedRockLiquid > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("Spawned-node visuals (this pass): %d vanilla rock + %d quartz placeholder (modded) + %d oil decal"),
            SpawnedRockVanilla, SpawnedRockQuartz, SpawnedRockLiquid);
    }

    if (bChangedWorld)
    {
        RefreshScannersAndRadarTowers();
    }
}

// correct-visual-11: PurgePhantomMeshes DELETED — it was dead code (zero call sites) and an
// unguarded modded-hide loaded gun (it iterated SweptRocks and hid by size with no modded guard).
// Phantom/shelf protection is unnecessary now: managed rocks copy the authentic native rock's exact
// world scale, so a size-blowout can't occur.

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

UStaticMesh* ANodeShuffleSubsystem::ResolveNodeMesh(UClass* OverrideClass)
{
    // correct-visual-1: the OVERRIDE resource's authored NODE-rock mesh (the big rock,
    // ResourceNode_<X>_01) from the NodeShuffleNodeAssets table. Cached. Null if uncovered —
    // do NOT fall back to GetDepositMesh (that's the small SM_Deposit_Ores outcrop).
    if (!OverrideClass)
    {
        return nullptr;
    }
    const FString ResPath = OverrideClass->GetPathName();
    if (const TWeakObjectPtr<UStaticMesh>* Cached = NodeMeshCache.Find(ResPath))
    {
        return Cached->Get();
    }
    UStaticMesh* Mesh = nullptr;
    const FName ShortName(*OverrideClass->GetName());
    if (const FNodeShuffleVisual* Visual = FNodeShuffleNodeAssets::FindVisual(ShortName))
    {
        if (Visual->MeshPath)
        {
            Mesh = LoadObject<UStaticMesh>(nullptr, Visual->MeshPath);
        }
    }
    NodeMeshCache.Add(ResPath, Mesh);
    return Mesh;
}

const TArray<TWeakObjectPtr<UMaterialInterface>>* ANodeShuffleSubsystem::ResolveNodeMaterials(UClass* OverrideClass)
{
    // correct-visual-1: the OVERRIDE resource's correct PER-SLOT NODE materials from the
    // authored NodeShuffleNodeAssets table (outer shell ResourceNode_<X>_Inst + glowing core
    // ResourceNode_Middle_<X>_Inst), in slot order. Cached.
    //
    // POLICY (user goal): if the resource has NO table entry, return an EMPTY array — do NOT
    // tint with GetDepositMaterial. An uncovered modded crafted-item resource (esc_) then keeps
    // its existing CLEAN rock instead of the rejected "dirty" wrong-tint look.
    if (!OverrideClass)
    {
        return nullptr;
    }
    const FString ResPath = OverrideClass->GetPathName();
    if (const TArray<TWeakObjectPtr<UMaterialInterface>>* Cached = NodeMaterialCache.Find(ResPath))
    {
        return Cached;
    }
    TArray<TWeakObjectPtr<UMaterialInterface>> Mats;
    const FName ShortName(*OverrideClass->GetName());
    if (const FNodeShuffleVisual* Visual = FNodeShuffleNodeAssets::FindVisual(ShortName))
    {
        for (const TCHAR* MatPath : Visual->MaterialPaths)
        {
            Mats.Add(LoadObject<UMaterialInterface>(nullptr, MatPath)); // keep slot order
        }
    }
    return &NodeMaterialCache.Add(ResPath, MoveTemp(Mats));
}

UStaticMesh* ANodeShuffleSubsystem::GetQuartzPlaceholderMesh()
{
    // redesign-1: the quartz placeholder for any spawned node whose resource has no authored table
    // entry (modded resources). Resolved from the Desc_RawQuartz_C table row (ResourceNode_Quartz).
    static const FName QuartzKey(TEXT("Desc_RawQuartz_C"));
    if (const TWeakObjectPtr<UStaticMesh>* Cached = NodeMeshCache.Find(QuartzKey.ToString()))
    {
        return Cached->Get();
    }
    UStaticMesh* Mesh = nullptr;
    if (const FNodeShuffleVisual* Visual = FNodeShuffleNodeAssets::FindVisual(QuartzKey))
    {
        if (Visual->MeshPath) { Mesh = LoadObject<UStaticMesh>(nullptr, Visual->MeshPath); }
    }
    NodeMeshCache.Add(QuartzKey.ToString(), Mesh);
    return Mesh;
}

const TArray<TWeakObjectPtr<UMaterialInterface>>* ANodeShuffleSubsystem::GetQuartzPlaceholderMaterials()
{
    static const FName QuartzKey(TEXT("Desc_RawQuartz_C"));
    if (const TArray<TWeakObjectPtr<UMaterialInterface>>* Cached = NodeMaterialCache.Find(QuartzKey.ToString()))
    {
        return Cached;
    }
    TArray<TWeakObjectPtr<UMaterialInterface>> Mats;
    if (const FNodeShuffleVisual* Visual = FNodeShuffleNodeAssets::FindVisual(QuartzKey))
    {
        for (const TCHAR* MatPath : Visual->MaterialPaths)
        {
            Mats.Add(LoadObject<UMaterialInterface>(nullptr, MatPath));
        }
    }
    return &NodeMaterialCache.Add(QuartzKey.ToString(), MoveTemp(Mats));
}

void ANodeShuffleSubsystem::SpawnVisualRockForNode(AFGResourceNode* Node, UClass* ResourceClass, const FGuid& EntryGuid)
{
    // redesign-5 PRIMARY: dress the node's OWN RockMesh (a CONSTRUCTOR default subobject on
    // ANodeShuffleResourceNode). redesign-4's separate plain AActor + runtime NewObject<UStaticMeshComponent>
    // spawned + logged but never rendered (ROCKDIAG found NONE at node transforms). A constructor subobject is
    // guaranteed to register with the render scene and moves/persists with the node. So we no longer spawn a
    // standalone actor at all — we just set the mesh/materials/relative-scale/relative-offset on RockMesh.
    if (!IsValid(Node) || !ResourceClass)
    {
        return;
    }
    ANodeShuffleResourceNode* OurNode = Cast<ANodeShuffleResourceNode>(Node);
    if (!OurNode)
    {
        return; // only our own nodes carry the RockMesh subobject
    }

    UStaticMesh* Mesh = ResolveNodeMesh(ResourceClass);
    const TArray<TWeakObjectPtr<UMaterialInterface>>* Mats = ResolveNodeMaterials(ResourceClass);
    bool bQuartz = false;
    // The authored table row carries the correct per-mesh scale (ore ~2.0, coal/sulfur ~0.917, stone ~2.4)
    // and a vertical offset. redesign-5 restores per-mesh scale + Z offset (redesign-4 collapsed to a uniform
    // (0,0,-40)). We force the LATERAL offset to 0 (the table's (-400,..) X was the off-center bug) and keep
    // only the table's Z (clamped to a small sink) so each rock sits centered with its authored footprint.
    const FNodeShuffleVisual* Visual = FNodeShuffleNodeAssets::FindVisual(FName(*ResourceClass->GetName()));
    if (!Mesh)
    {
        // Modded resource with no authored entry -> quartz placeholder (clean, fully controlled).
        Mesh = GetQuartzPlaceholderMesh();
        Mats = GetQuartzPlaceholderMaterials();
        bQuartz = true;
        Visual = FNodeShuffleNodeAssets::FindVisual(FName(TEXT("Desc_RawQuartz_C"))); // quartz scale/offset
    }
    if (!Mesh)
    {
        UE_LOG(LogNodeShuffle, Warning, TEXT("SpawnVisualRockForNode: no mesh (not even quartz) for %s"),
            *ResourceClass->GetName());
        return;
    }
    // Per-mesh scale from the table; empirical 2.0 only if a mesh resolved with no table row.
    const FVector WantScale = Visual ? Visual->MeshScale : FVector(2.0f, 2.0f, 2.0f);
    // Centered laterally; keep the table's Z (a vertical sink), clamped so we never sink the rock absurdly.
    const float TableZ = Visual ? Visual->MeshOffset.Z : -40.0f;
    const FVector RelOffset(0.f, 0.f, FMath::Clamp(TableZ, -120.0f, 60.0f));

    // Build a plain material array for DressRock.
    TArray<UMaterialInterface*> MatPtrs;
    if (Mats)
    {
        for (const TWeakObjectPtr<UMaterialInterface>& M : *Mats) { MatPtrs.Add(M.Get()); }
    }

    OurNode->DressRock(Mesh, MatPtrs, WantScale, RelOffset);

    // DIAGNOSTIC (issue #1 snap off-center): the miner snaps to the node ORIGIN / GetPlacementLocation,
    // but the rock renders at its mesh bounds center. Log both so we can measure the lateral offset (the
    // rock's pivot is not its visual center). Gated behind EnableDiagnostics.
    if (FNodeShuffleModule::AreDiagnosticsEnabled() && IsValid(OurNode->RockMesh))
    {
        const FVector NodeOrigin = OurNode->GetActorLocation();
        const FVector RockCenter = OurNode->RockMesh->Bounds.Origin;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("ROCK-CENTER node='%s' resource='%s' nodeOrigin=%s rockBoundsCenter=%s offset(center-origin)=%s"),
            *OurNode->GetName(), *ResourceClass->GetName(),
            *NodeOrigin.ToCompactString(), *RockCenter.ToCompactString(),
            *(RockCenter - NodeOrigin).ToCompactString());
    }

    if (bQuartz) { SpawnedRockQuartz++; } else { SpawnedRockVanilla++; }
    UE_LOG(LogNodeShuffle, Verbose,
        TEXT("dressed %s rock (node subobject) for %s: mesh '%s' scale=(%.2f,%.2f,%.2f) relZ=%.0f %d mat(s)"),
        bQuartz ? TEXT("QUARTZ-placeholder") : TEXT("vanilla"),
        *ResourceClass->GetName(), *Mesh->GetName(),
        WantScale.X, WantScale.Y, WantScale.Z, RelOffset.Z, MatPtrs.Num());
}

void ANodeShuffleSubsystem::AppendStarterNodes(TArray<FNodeShuffleEntry>& NewLayout, FRandomStream& Rng,
                                               const TArray<FVector>& AvoidLocations)
{
    // redesign-1: place 2 Iron, 2 Limestone, 1 Copper (PURE) near the captured player-start. Drawn
    // FROM the relocated pool when possible (re-home an already-active node of that resource near
    // spawn, preserving counts); spawned additionally only if the pool can't supply that type.
    const FNodeShuffleConfigStruct Config = FNodeShuffleConfigStruct::GetActiveConfig(this);
    const float RadiusCm = FMath::Max(5000.f, static_cast<float>(Config.StarterNodeRadiusMeters) * 100.f);

    struct FStarter { const TCHAR* Path; int32 Count; };
    const FStarter Wanted[] = {
        { TEXT("/Game/FactoryGame/Resource/RawResources/OreIron/Desc_OreIron.Desc_OreIron_C"), 2 },
        { TEXT("/Game/FactoryGame/Resource/RawResources/Stone/Desc_Stone.Desc_Stone_C"), 2 },
        { TEXT("/Game/FactoryGame/Resource/RawResources/OreCopper/Desc_OreCopper.Desc_OreCopper_C"), 1 },
    };

    // A spawnable solid node class: reuse the first one already on a spawned/new entry.
    FString SolidNodeClassPath;
    for (const FNodeShuffleEntry& E : NewLayout)
    {
        if (E.bIsNewNode && !E.NodeClassPath.IsEmpty()) { SolidNodeClassPath = E.NodeClassPath; break; }
    }

    // Candidate placement points: a few random spots within the radius around the player start.
    auto PickStarterLocation = [&](const TArray<FVector>& AlreadyPlaced) -> FVector
    {
        constexpr float GoldenAngleRad = 2.39996323f;
        for (int32 i = 1; i <= 24; i++)
        {
            const float R = RadiusCm * FMath::Sqrt(Rng.FRand());
            const float A = GoldenAngleRad * i + Rng.FRandRange(0.f, GoldenAngleRad);
            const FVector Cand(PlayerStartLocation.X + R * FMath::Cos(A),
                               PlayerStartLocation.Y + R * FMath::Sin(A),
                               PlayerStartLocation.Z);
            bool bTooClose = false;
            for (const FVector& V : AvoidLocations)
            {
                if (FVector::DistSquared2D(V, Cand) < FMath::Square(MinNodeSpacing)) { bTooClose = true; break; }
            }
            if (!bTooClose)
            {
                for (const FVector& V : AlreadyPlaced)
                {
                    if (FVector::DistSquared2D(V, Cand) < FMath::Square(MinNodeSpacing)) { bTooClose = true; break; }
                }
            }
            if (!bTooClose) { return Cand; }
        }
        // Fall back to a small offset right at the start if everything is crowded.
        return PlayerStartLocation + FVector(MinNodeSpacing, 0.f, 0.f);
    };

    TArray<FVector> Placed;
    int32 Reused = 0, Added = 0;
    for (const FStarter& W : Wanted)
    {
        const FString WantPath(W.Path);
        for (int32 n = 0; n < W.Count; n++)
        {
            const FVector Loc = PickStarterLocation(Placed);
            Placed.Add(Loc);

            // Try to RE-HOME an existing active relocated node of this resource (preserve counts).
            FNodeShuffleEntry* Reuse = nullptr;
            for (FNodeShuffleEntry& E : NewLayout)
            {
                if (E.bIsNewNode && E.bActive && !E.bPinned
                    && E.AssignedResourceClassPath == WantPath)
                {
                    // Skip ones we already moved to a starter spot this call.
                    bool bAlready = false;
                    for (const FVector& P : Placed)
                    {
                        if (E.Location.Equals(P, 1.0f)) { bAlready = true; break; }
                    }
                    if (!bAlready) { Reuse = &E; break; }
                }
            }
            if (Reuse)
            {
                Reuse->Location = Loc;
                Reuse->Rotation = FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f);
                Reuse->AssignedPurity = RP_Pure;
                Reuse->bRayCasted = false;
                Reuse->OverlapNudges = 0;
                Reused++;
            }
            else
            {
                // Pool can't supply this type — spawn it additionally.
                FNodeShuffleEntry E;
                E.EntryGuid = FGuid::NewGuid();
                E.bIsNewNode = true;
                E.bActive = true;
                E.Location = Loc;
                E.Rotation = FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f);
                E.AssignedResourceClassPath = WantPath;
                E.OriginalResourceClassPath = WantPath;
                E.AssignedPurity = RP_Pure;
                E.OriginalPurity = RP_Pure;
                E.ResourceForm = FormSolid;
                E.NodeClassPath = SolidNodeClassPath;
                NewLayout.Add(E);
                Added++;
            }
        }
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("Starter nodes: placed 5 near %s (radius %.0f m) — %d re-homed from pool, %d spawned additionally"),
        *PlayerStartLocation.ToCompactString(), RadiusCm / 100.f, Reused, Added);
}

void ANodeShuffleSubsystem::AdoptRestoredSpawnedNodes()
{
    // redesign-3 BUG B: spawned nodes are ANodeShuffleResourceNode with a UPROPERTY(SaveGame) EntryGuid
    // that survives reload (AActor::Tags do NOT — that was the redesign-2 root cause: adopt matched 0 of
    // 732, so every node re-spawned and occupied nodes moved off their miners). Iterate the subclass,
    // read each one's saved EntryGuid, repopulate SpawnedNodes[guid] so EnsureNewNodeSpawned's
    // SpawnedNodes.Find guard short-circuits and we never re-spawn. ONE iteration, once per session.

    // VERIFY-FIRST (the make-or-break for redesign-3): count how many of OUR subclass nodes actually
    // exist in the world post-reload. ~N (the expected spawned count) => they persist (adopt them).
    // ~0/1 => a raw subclass still isn't save-collected and we must wrap as a buildable (report it).
    int32 SubclassInWorld = 0;
    int32 Adopted = 0;
    int32 PinnedOnLoad = 0;
    const int32 ExpectedSpawned = static_cast<int32>(Algo::CountIf(Layout,
        [](const FNodeShuffleEntry& E){ return E.bIsNewNode; }));

    // Map EntryGuid -> layout index so adoption can flip a saved pin / occupancy onto the entry.
    TMap<FGuid, int32> EntryByGuid;
    for (int32 i = 0; i < Layout.Num(); i++)
    {
        if (Layout[i].bIsNewNode) { EntryByGuid.Add(Layout[i].EntryGuid, i); }
    }

    for (TActorIterator<ANodeShuffleResourceNode> It(GetWorld()); It; ++It)
    {
        ANodeShuffleResourceNode* Node = *It;
        if (!IsValid(Node)) { continue; }
        SubclassInWorld++;
        if (!Node->EntryGuid.IsValid()) { continue; }

        AFGResourceNode* const* Existing = SpawnedNodes.Find(Node->EntryGuid);
        if (!Existing || !IsValid(*Existing))
        {
            SpawnedNodes.Add(Node->EntryGuid, Node);
            Adopted++;
        }

        // redesign-3b BLOCKER FIX: every adopted/restored node lost its (unserialized) "Resource" UseBox
        // on reload. Recreate it HERE so even far nodes (never reached by EnsureNewNodeSpawned this pass)
        // are interactable. Idempotent — no-op if the box is already present.
        EnsureNodeUseBox(Node);

        // redesign-21 (THE ACTUAL MK1 SNAP FIX): re-assert both placement gates on every adopted node. The CDO
        // ctor default now carries mCanPlaceResourceExtractor=true, but set it here too so an old save (written by
        // a pre-r21 class version) is corrected on load. This is the flag the Mk1 extractor hologram checks; the
        // r20 InitResource block below was chasing the wrong gate (resource-completeness, not the placement bool).
        Node->mCanPlaceResourceExtractor = true;
        Node->mCanPlacePortableMiner = true;

        // redesign-20 (THE MK1 SNAP FIX): on reload an adopted node is NOT resource-complete. mResourceClass
        // (EditAnywhere) AND mAmount (EditInstanceOnly) are NOT SaveGame, so after reload the node can report
        // HasAnyResources()=false / a stale class / 0 extraction-speed. r19 proved the gate: the Mk1 extractor's
        // CanOccupyResource=0 AND IsAllowedOnResource=0 reject our node even though the resource class+form look
        // valid — because those checks read the FULL extractable state (amount/has-resources), which a half-
        // restored adopted node lacks. A real level node always has it. FIX: fully re-run InitResource on adopt
        // (it sets mResourceClass + mAmount=Infinite + mResourcesLeft + radioactivity) from the persisted
        // override class, so an adopted node is byte-for-byte resource-complete like a fresh spawn. Then re-seat
        // the SaveGame override == the (now-restored) original and refresh the rep.
        if (Node->mResourceClassOverride && (!Node->GetResourceClassOriginal().Get() || !Node->HasAnyResources()))
        {
            const EResourcePurity Pur = Node->GetResourcePurity();
            const TSubclassOf<UFGResourceDescriptor> Cls = Node->mResourceClassOverride;
            Node->InitResource(Cls, EResourceAmount::RA_Infinite, Pur);
            Node->mResourceClassOverride = Cls;
            Node->InitRadioactivity();
            Node->UpdateRadioactivity();
            Node->OnRep_ResourceClassOverride();
        }

        // Occupancy pin: a restored spawned node that has a miner/extractor on it (or carries the saved
        // pin) must NEVER be relocated again. Mark its entry pinned so settle/re-roll skip it, and stamp
        // the node's saved pin so the state survives further reloads.
        if (int32* Idx = EntryByGuid.Find(Node->EntryGuid))
        {
            const bool bOccupied = IsNodeOccupiedAnyway(Node) || Node->bNodeShuffleOccupiedPinned;
            if (bOccupied)
            {
                Node->bNodeShuffleOccupiedPinned = true;
                Layout[*Idx].bPinned = true;
                Layout[*Idx].bRayCasted = true; // already settled where the miner expects it
                Layout[*Idx].Location = Node->GetActorLocation(); // lock the entry to the live node
                PinnedOnLoad++;
            }
        }
    }

    // FOLD-IN 2: the PASS signal is `adopted N` ≈ `live subclass count` (the actors that actually exist
    // post-reload), NOT ≈ ExpectedSpawned. ExpectedSpawned counts ALL bIsNewNode entries (~732), but only
    // streamed-in/active nodes ever spawned this/last session — so "60 vs expected 732" is NORMAL, not a
    // failure. The real failure mode is SubclassInWorld <= 1 (a raw subclass not save-collected at all).
    const TCHAR* Verdict = (SubclassInWorld <= 1 && ExpectedSpawned > 1)
        ? TEXT("<-- WARNING: ANodeShuffleResourceNode NOT save-collected (count <=1) — must wrap as buildable")
        : TEXT("(OK: subclass nodes persist & adopted)");
    UE_LOG(LogNodeShuffle, Display,
        TEXT("Adopt-on-load (redesign-3): adopted %d of %d live ANodeShuffleResourceNode actors in world (these persisted across reload); pinned %d occupied. [%d of %d total layout spawn-entries have streamed in so far — far/undiscovered entries spawn later, so a count below the total is normal.] %s"),
        Adopted, SubclassInWorld, PinnedOnLoad,
        SubclassInWorld, ExpectedSpawned,
        Verdict);
}

void ANodeShuffleSubsystem::EnsureNodeUseBox(AFGResourceNode* Node)
{
    // redesign-10 (MIRROR VANILLA NODE STRUCTURE FOR MK1 SNAP). The UseBox is now a CONSTRUCTOR default
    // subobject AND the actor root on ANodeShuffleResourceNode (mirroring a vanilla node whose root IS its
    // BoxComponent). So this helper no longer CREATES the box at runtime — it just (re)asserts the box's
    // named "Resource" profile + extent and wires mBoxComponent (which is not SaveGame, so it's null again
    // after a reload). CRITICAL: do NOT add per-channel overrides — SNAPDIAG showed the r9 BuildGun→Overlap
    // override flipped the named profile to 'Custom'; vanilla IGNORES BuildGun. Keep the NAMED profile.
    if (!IsValid(Node))
    {
        return;
    }
    ANodeShuffleResourceNode* OurNode = Cast<ANodeShuffleResourceNode>(Node);
    UBoxComponent* UseBox = OurNode ? OurNode->UseBox : nullptr;

    // Defensive fallback: if the ctor subobject is somehow missing, find/create a named box (legacy path).
    if (!IsValid(UseBox))
    {
        TInlineComponentArray<UBoxComponent*> Boxes(Node);
        for (UBoxComponent* Box : Boxes)
        {
            if (IsValid(Box) && Box->GetFName() == FName(TEXT("NodeShuffleUseBox"))) { UseBox = Box; break; }
        }
        if (!UseBox)
        {
            UseBox = NewObject<UBoxComponent>(Node, TEXT("NodeShuffleUseBox_Rt"));
            if (USceneComponent* Root = Node->GetRootComponent()) { UseBox->SetupAttachment(Root); }
            else { Node->SetRootComponent(UseBox); UseBox->SetWorldLocationAndRotation(Node->GetActorLocation(), Node->GetActorRotation()); }
            UseBox->SetBoxExtent(FVector(650.f, 650.f, 180.f));
            UseBox->RegisterComponent();
        }
    }

    // Re-assert the NAMED "Resource" profile + extent (no per-channel overrides → stays 'Resource', not
    // 'Custom'; BuildGun=Ignore exactly like vanilla). Idempotent.
    if (UseBox->GetCollisionProfileName() != FName(TEXT("Resource")))
    {
        UseBox->SetCollisionProfileName(TEXT("Resource"));
    }
    if (!UseBox->GetUnscaledBoxExtent().Equals(FVector(650.f, 650.f, 180.f), 1.0f))
    {
        UseBox->SetBoxExtent(FVector(650.f, 650.f, 180.f));
    }

    // DIAGNOSTIC (issue #2 collision box rising): log how far the box top sticks above the terrain, plus
    // the Pawn/World collision responses — to confirm whether the player-blocking channel is WorldDynamic
    // (profile drift) rather than Pawn, and how much the 180-half-height scales up. Gated behind EnableDiagnostics.
    if (FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        auto RStr = [](ECollisionResponse X){ return X==ECR_Block?TEXT("Block"):X==ECR_Overlap?TEXT("Overlap"):TEXT("Ignore"); };
        const FVector BoxCenter = UseBox->GetComponentLocation();
        const float BoxTopZ = BoxCenter.Z + UseBox->GetScaledBoxExtent().Z;
        const FVector NodeLoc = Node->GetActorLocation();
        float TerrainZ = NodeLoc.Z;
        FHitResult Hit;
        const FVector TraceTop = FVector(NodeLoc.X, NodeLoc.Y, NodeLoc.Z + 1000.f);
        const FVector TraceBot = FVector(NodeLoc.X, NodeLoc.Y, NodeLoc.Z - 1000.f);
        if (GetWorld()->LineTraceSingleByChannel(Hit, TraceTop, TraceBot, ECC_Visibility)) { TerrainZ = Hit.ImpactPoint.Z; }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("USEBOX-RISE node='%s' profile='%s' collEnabled=%d | scaledExtent=%s actorScale=%s | boxCenterZ=%.1f boxTopZ=%.1f nodeZ=%.1f terrainZ=%.1f topAboveTerrain=%.1f | Pawn=%s WorldDynamic=%s WorldStatic=%s"),
            *Node->GetName(), *UseBox->GetCollisionProfileName().ToString(), (int32)UseBox->GetCollisionEnabled(),
            *UseBox->GetScaledBoxExtent().ToCompactString(), *Node->GetActorScale3D().ToCompactString(),
            BoxCenter.Z, BoxTopZ, NodeLoc.Z, TerrainZ, BoxTopZ - TerrainZ,
            RStr(UseBox->GetCollisionResponseToChannel(ECC_Pawn)),
            RStr(UseBox->GetCollisionResponseToChannel(ECC_WorldDynamic)),
            RStr(UseBox->GetCollisionResponseToChannel(ECC_WorldStatic)));
    }

    // THE SNAP FIX (r9, kept): wire our box in as the node's engine mBoxComponent (the resource collider
    // the extractor hologram resolves). Not SaveGame → re-wire whenever it's null. Friend access.
    if (AFGResourceNodeBase* Base = Cast<AFGResourceNodeBase>(Node))
    {
        if (!IsValid(Base->mBoxComponent))
        {
            Base->mBoxComponent = UseBox;
        }
    }
}

void ANodeShuffleSubsystem::LogNodeSnapState(AFGResourceNodeBase* Node, const TCHAR* Label) const
{
    if (!IsValid(Node)) { return; }
    // Channel mapping (DefaultEngine.ini): Hologram=GTC2, Resource=GTC3, Clearance=GTC4, BuildGun=GTC5.
    const ECollisionChannel ChHologram = ECC_GameTraceChannel2;
    const ECollisionChannel ChResource = ECC_GameTraceChannel3;
    const ECollisionChannel ChClearance = ECC_GameTraceChannel4;
    const ECollisionChannel ChBuildGun = ECC_GameTraceChannel5;
    auto RespStr = [](ECollisionResponse R) -> const TCHAR*
    {
        return R == ECR_Block ? TEXT("Block") : R == ECR_Overlap ? TEXT("Overlap") : TEXT("Ignore");
    };

    UE_LOG(LogNodeShuffle, Display,
        TEXT("SNAPDIAG [%s] node='%s' class='%s' nodeType=%d occupied=%d mBoxComponent=%s root='%s'"),
        Label, *Node->GetName(), *Node->GetClass()->GetName(),
        (int32)Node->GetResourceNodeType(), Node->IsOccupied() ? 1 : 0,
        IsValid(Node->mBoxComponent) ? *Node->mBoxComponent->GetName() : TEXT("<NULL>"),
        Node->GetRootComponent() ? *Node->GetRootComponent()->GetName() : TEXT("<none>"));

    TInlineComponentArray<UPrimitiveComponent*> Prims(Node);
    for (UPrimitiveComponent* P : Prims)
    {
        if (!IsValid(P)) { continue; }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("SNAPDIAG [%s]   comp='%s' class='%s' collEnabled=%d profile='%s' objType=%d | Resource=%s BuildGun=%s Clearance=%s Hologram=%s"),
            Label, *P->GetName(), *P->GetClass()->GetName(),
            (int32)P->GetCollisionEnabled(),
            *P->GetCollisionProfileName().ToString(),
            (int32)P->GetCollisionObjectType(),
            RespStr(P->GetCollisionResponseToChannel(ChResource)),
            RespStr(P->GetCollisionResponseToChannel(ChBuildGun)),
            RespStr(P->GetCollisionResponseToChannel(ChClearance)),
            RespStr(P->GetCollisionResponseToChannel(ChHologram)));
    }
}

void ANodeShuffleSubsystem::DiagnoseSnapState()
{
    // redesign-9 SNAPDIAG (mandatory, diagnose-don't-guess). Run ONCE when BOTH a spawned node and a
    // streamed-in VANILLA node exist, dumping each one's components/collision so the log names the exact
    // delta the extractor hologram detects (vanilla works, ours didn't). Friend access reads mBoxComponent.
    if (bSnapDiagLogged) { return; }

    // Find one live spawned node (ours).
    ANodeShuffleResourceNode* OurNode = nullptr;
    for (const auto& Pair : SpawnedNodes)
    {
        if (ANodeShuffleResourceNode* N = Cast<ANodeShuffleResourceNode>(Pair.Value))
        {
            if (IsValid(N)) { OurNode = N; break; }
        }
    }
    if (!OurNode) { return; } // wait until at least one of ours is materialized

    // Find a streamed-in VANILLA node (a /Game/ AFGResourceNode that is NOT ours, plain Node type).
    AFGResourceNode* Vanilla = nullptr;
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        AFGResourceNode* N = *It;
        if (!IsValid(N) || N->IsA<ANodeShuffleResourceNode>()) { continue; }
        if (N->GetResourceNodeType() != EResourceNodeType::Node) { continue; }
        if (!N->GetClass()->GetPathName().StartsWith(TEXT("/Game/"))) { continue; }
        Vanilla = N;
        break;
    }

    bSnapDiagLogged = true;
    LogNodeSnapState(OurNode, TEXT("OURS"));
    if (Vanilla)
    {
        LogNodeSnapState(Vanilla, TEXT("VANILLA"));
    }
    else
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("SNAPDIAG: no streamed-in vanilla AFGResourceNode found to compare against this pass."));
    }
}

AFGResourceNodeManager* ANodeShuffleSubsystem::GetNodeManager() const
{
    // redesign-11: AFGResourceNodeManager::Get(UWorld*) is NOT dll-exported (LNK2019 if called), so resolve
    // the live manager instance by actor iteration instead (it's an AFGSubsystem actor, one per world).
    for (TActorIterator<AFGResourceNodeManager> It(GetWorld()); It; ++It)
    {
        if (IsValid(*It)) { return *It; }
    }
    return nullptr;
}

void ANodeShuffleSubsystem::RegisterNodeWithManager(AFGResourceNode* Node)
{
    // redesign-11 (THE MK1 SNAP FIX). The extractor hologram resolves the node to snap to via
    // AFGResourceNodeManager::GetClosestNode over the manager's mResourceNodes list. Vanilla level nodes
    // are added to it at world init; our runtime-spawned nodes never are -> "Must be placed on a Resource
    // Node!". Add our node to mResourceNodes (friend access). The list is runtime (no UPROPERTY -> not
    // save-persisted), so we re-add at spawn AND on adopt-after-reload. Idempotent (Contains guard).
    if (!IsValid(Node)) { return; }
    AFGResourceNodeManager* Mgr = GetNodeManager();
    if (!Mgr) { return; } // manager not up yet — retried next pass (adopt/spawn run every pass)

    const int32 Before = Mgr->mResourceNodes.Num();
    const bool bWasIn = Mgr->mResourceNodes.Contains(Node);
    if (!bWasIn)
    {
        Mgr->mResourceNodes.Add(Node);
    }

    // REGDIAG full-set (redesign-12): the r11 REGDIAG only sampled ONE node. Count ALL our
    // ANodeShuffleResourceNode entries actually present in mResourceNodes vs how many we've spawned, to
    // confirm the WHOLE set registers (not just the sample). One-shot, logged after the list has grown.
    if (!bRegDiagLogged)
    {
        bRegDiagLogged = true;
        int32 OursInList = 0;
        for (AFGResourceNode* N : Mgr->mResourceNodes)
        {
            if (IsValid(N) && N->IsA<ANodeShuffleResourceNode>()) { OursInList++; }
        }
        int32 OursSpawned = 0;
        for (const auto& Pair : SpawnedNodes)
        {
            if (Pair.Value && Pair.Value->IsA<ANodeShuffleResourceNode>()) { OursSpawned++; }
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("REGDIAG full-set: mResourceNodes was %d -> now %d; OUR nodes in list = %d (of %d spawned this session); last add: %s '%s' Contains=%d"),
            Before, Mgr->mResourceNodes.Num(), OursInList, OursSpawned,
            bWasIn ? TEXT("already had") : TEXT("ADDED"), *Node->GetName(),
            Mgr->mResourceNodes.Contains(Node) ? 1 : 0);
    }
}

void ANodeShuffleSubsystem::LogNodeValidationState(AFGResourceNode* Node, const TCHAR* Label) const
{
    if (!IsValid(Node)) { return; }
    const UClass* ResClass = Node->GetResourceClass();
    const UClass* OrigClass = Node->GetResourceClassOriginal().Get(); // mResourceClass
    // CanPlaceResourceExtractor / CanBecomeOccupied are virtual engine-DLL bodies — these are exactly the
    // checks AFGResourceExtractorHologram::CanOccupyResource/IsAllowedOnResource read. One WILL differ.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("VALIDDIAG [%s] node='%s' | CanPlaceExtractor=%d CanBecomeOccupied=%d IsOccupied=%d nodeType=%d form=%d purity=%d mCanPlaceResourceExtractor=%d | resClass='%s'(null=%d) origClass(mResourceClass)='%s'(null=%d)"),
        Label, *Node->GetName(),
        Node->CanPlaceResourceExtractor() ? 1 : 0,
        Node->CanBecomeOccupied() ? 1 : 0,
        Node->IsOccupied() ? 1 : 0,
        (int32)Node->GetResourceNodeType(),
        (int32)Node->GetResourceForm(),
        (int32)Node->GetResourcePurity(),
        Node->mCanPlaceResourceExtractor ? 1 : 0,
        ResClass ? *ResClass->GetName() : TEXT("<null>"), ResClass ? 0 : 1,
        OrigClass ? *OrigClass->GetName() : TEXT("<null>"), OrigClass ? 0 : 1);
}

// redesign-13 (item 2): dump the FULL collision of EVERY primitive on a node — incl Visibility/Camera/
// WorldStatic/WorldDynamic (which SNAPDIAG never logged) + bounds/extent + world location — so we can see,
// OURS vs VANILLA, exactly which channels each collider answers and where it sits. The r12 hook proved the
// build trace never lands on our node; this names what our box/rock lacks vs a node the trace DOES hit.
static void LogNodeCollisionFull(AActor* Node, const TCHAR* Label)
{
    if (!IsValid(Node)) { return; }
    auto R = [](ECollisionResponse X) -> const TCHAR*
    { return X == ECR_Block ? TEXT("Block") : X == ECR_Overlap ? TEXT("Overlap") : TEXT("Ignore"); };
    TInlineComponentArray<UPrimitiveComponent*> Prims(Node);
    UE_LOG(LogNodeShuffle, Display, TEXT("COLLDIAG [%s] node='%s' root='%s' actorLoc=%s : %d primitive(s)"),
        Label, *Node->GetName(),
        Node->GetRootComponent() ? *Node->GetRootComponent()->GetName() : TEXT("<null>"),
        *Node->GetActorLocation().ToCompactString(), Prims.Num());
    for (UPrimitiveComponent* C : Prims)
    {
        if (!C) { continue; }
        const FBoxSphereBounds B = C->Bounds;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("COLLDIAG [%s]   comp='%s' class='%s' collEnabled=%d profile='%s' objType=%d boundsR=%.0f extent=%s worldLoc=%s | Visibility=%s Camera=%s WorldStatic=%s WorldDynamic=%s Resource=%s BuildGun=%s"),
            Label, *C->GetName(), *C->GetClass()->GetName(),
            (int32)C->GetCollisionEnabled(), *C->GetCollisionProfileName().ToString(),
            (int32)C->GetCollisionObjectType(),
            B.SphereRadius, *B.BoxExtent.ToCompactString(), *C->GetComponentLocation().ToCompactString(),
            R(C->GetCollisionResponseToChannel(ECC_Visibility)),
            R(C->GetCollisionResponseToChannel(ECC_Camera)),
            R(C->GetCollisionResponseToChannel(ECC_WorldStatic)),
            R(C->GetCollisionResponseToChannel(ECC_WorldDynamic)),
            R(C->GetCollisionResponseToChannel(ECC_GameTraceChannel3)),   // Resource
            R(C->GetCollisionResponseToChannel(ECC_GameTraceChannel5)));  // BuildGun
    }
}

void ANodeShuffleSubsystem::DiagnoseValidationGate()
{
    // redesign-12 VALIDDIAG (diagnostics only). Collision (r10 SNAPDIAG byte-match) + manager registration
    // (r11 REGDIAG Contains=1) are BOTH ruled out — the Mk1 hologram FINDS our node but REJECTS it at the
    // validation gate. Log OURS vs a nearby VANILLA node on the validation-relevant props/methods so the
    // next test names the exact differing gate. One-shot (like SNAPDIAG).
    if (bValidDiagLogged) { return; }

    ANodeShuffleResourceNode* OurNode = nullptr;
    for (const auto& Pair : SpawnedNodes)
    {
        if (ANodeShuffleResourceNode* N = Cast<ANodeShuffleResourceNode>(Pair.Value))
        {
            if (IsValid(N)) { OurNode = N; break; }
        }
    }
    if (!OurNode) { return; } // wait until at least one of ours is materialized

    AFGResourceNode* Vanilla = nullptr;
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        AFGResourceNode* N = *It;
        if (!IsValid(N) || N->IsA<ANodeShuffleResourceNode>()) { continue; }
        if (N->GetResourceNodeType() != EResourceNodeType::Node) { continue; }
        if (!N->GetClass()->GetPathName().StartsWith(TEXT("/Game/"))) { continue; }
        Vanilla = N;
        break;
    }

    bValidDiagLogged = true;
    LogNodeValidationState(OurNode, TEXT("OURS"));
    if (Vanilla)
    {
        LogNodeValidationState(Vanilla, TEXT("VANILLA"));
    }
    else
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("VALIDDIAG: no streamed-in vanilla AFGResourceNode found to compare against this pass."));
    }
    // redesign-13 (item 2): full per-component collision (incl Visibility/Camera) OURS vs VANILLA — the hook
    // can't capture our node (the trace never lands on it), so log it directly here.
    LogNodeCollisionFull(OurNode, TEXT("OURS"));
    if (Vanilla) { LogNodeCollisionFull(Vanilla, TEXT("VANILLA")); }
}

void ANodeShuffleSubsystem::DeregisterNodeFromManager(AFGResourceNodeBase* Node)
{
    // redesign-11 SECONDARY (correctness): when we HIDE an original node, remove it from the manager's
    // mResourceNodes so the player can't snap a Mk1 onto an invisible ghost original. Only called for
    // originals we hide (SuppressOriginalNodes) — never our own spawned nodes. mResourceNodes is
    // TArray<AFGResourceNode*>, so only the AFGResourceNode-typed entries are removable here.
    if (!IsValid(Node)) { return; }
    AFGResourceNode* AsNode = Cast<AFGResourceNode>(Node);
    if (!AsNode) { return; } // Base-only (esc_) originals aren't in mResourceNodes anyway
    if (AsNode->IsA<ANodeShuffleResourceNode>()) { return; } // never deregister OUR spawned nodes
    AFGResourceNodeManager* Mgr = GetNodeManager();
    if (Mgr)
    {
        Mgr->mResourceNodes.RemoveSingleSwap(AsNode);
    }
}

void ANodeShuffleSubsystem::EnsureNewNodeSpawned(FNodeShuffleEntry& Entry, bool& bOutChangedWorld)
{
    AFGResourceNode* const* Existing = SpawnedNodes.Find(Entry.EntryGuid);
    if (Existing && IsValid(*Existing))
    {
        // redesign-3b BLOCKER FIX: a restored/adopted node lost its (unserialized) "Resource" UseBox on
        // reload -> non-interactable. Recreate it (idempotent — no-op if already present). This is the
        // path adopted nodes flow through every pass, so it covers them generally, not just first sight.
        EnsureNodeUseBox(*Existing);

        // redesign-11 (THE MK1 SNAP FIX): the manager's mResourceNodes list is runtime (not save-persisted),
        // so a restored/adopted node is NOT in it after reload — re-register so Mk1 can snap to it again.
        RegisterNodeWithManager(*Existing);

        // redesign-5: the node is already live (spawned this session OR a save-restored node adopted on
        // load). The NODE persists, but RockMesh's static mesh/materials are NOT SaveGame, so after a
        // reload the RockMesh subobject is EMPTY — re-dress it. Cost-guarded (DressRock no-ops when set).
        ANodeShuffleResourceNode* OurNode = Cast<ANodeShuffleResourceNode>(*Existing);
        // redesign-7 FIX 2 (THE LOCATION BUG): guarantee RockMesh is a CHILD of the restored root (never
        // the root) BEFORE re-dressing, so the re-dress relative offset can't teleport the node to origin.
        if (OurNode) { OurNode->EnsureRockChildOfRoot(); }
        const bool bNeedsDress = OurNode && (!IsValid(OurNode->RockMesh) || OurNode->RockMesh->GetStaticMesh() == nullptr);
        if (bNeedsDress)
        {
            UClass* RC = LoadClassByPath(Entry.AssignedResourceClassPath);
            const EResourceForm F = RC
                ? UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(RC))
                : EResourceForm::RF_SOLID;
            if (RC && F != EResourceForm::RF_LIQUID)
            {
                SpawnVisualRockForNode(*Existing, RC, Entry.EntryGuid); // -> DressRock -> ForceVisible
            }
            else if (RC && F == EResourceForm::RF_LIQUID)
            {
                RebuildNodeNativeVisual(*Existing);
            }
        }
        // redesign-6 FIX 1: a restored node may be left actor-hidden after reload — force the whole chain
        // visible every pass (idempotent), and log the runtime state for the first N adopted nodes.
        if (OurNode)
        {
            OurNode->ForceVisible();
            if (RenderDiagAdoptLogged < RenderDiagMax)
            {
                OurNode->LogRenderState(TEXT("adopt"));
                RenderDiagAdoptLogged++;
            }
        }
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

    // redesign-2 FIX 1 (THE CRASH — save-correct spawned nodes). A Miner the player builds on a
    // spawned node is SAVED with a ref to the node. In redesign-1 the node was RF_Transient (never
    // written) and its resource class lived only in mResourceClass (NOT a SaveGame field), so on
    // reload the node was gone and the miner's BeginPlay hit check(GetResourceClass()) -> hard
    // assert, corrupt save. Fix: (a) DROP RF_Transient so the node is a persistent, save-collectable
    // actor (AFGResourceNode implements IFGSaveInterface with ShouldSave()==true), and (b) ALSO set
    // the SaveGame field mResourceClassOverride so GetResourceClass() is valid the instant the node
    // is restored, BEFORE any miner BeginPlay runs. (The visual AFGNodeMeshActor stays RF_Transient —
    // only the NODE must persist; the rock is re-dressed from the layout each session.)
    // redesign-3 BUG B (SAVED NODE IDENTITY): spawn OUR OWN ANodeShuffleResourceNode subclass, not the
    // raw BP NodeClass. The subclass carries UPROPERTY(SaveGame) EntryGuid, which (unlike AActor::Tags)
    // round-trips a save/reload — so AdoptRestoredSpawnedNodes can match restored nodes and we never
    // re-spawn (which moved occupied nodes off their miners). It inherits the base constructor defaults
    // (mCanPlaceResourceExtractor=true etc.), so mining/placement is identical to a vanilla node.
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    // NOTE: intentionally NO RF_Transient — the node must be collected by the save system.
    ANodeShuffleResourceNode* Node = GetWorld()->SpawnActor<ANodeShuffleResourceNode>(
        ANodeShuffleResourceNode::StaticClass(), Entry.Location, Entry.Rotation, Params);
    if (!Node)
    {
        UE_LOG(LogNodeShuffle, Warning, TEXT("Failed to spawn new node at %s"), *Entry.Location.ToCompactString());
        return;
    }
    Node->EntryGuid = Entry.EntryGuid; // SaveGame identity — survives reload (Tags do not)
    Node->InitResource(ResourceClass, RA_Infinite, Entry.AssignedPurity.GetValue());
    // PERSIST the resource class in the only SaveGame class field (mResourceClass is EditAnywhere,
    // NOT saved). mResourceClassOverride is UPROPERTY(SaveGame, ReplicatedUsing=OnRep_...). Friend
    // access (AccessTransformers grants a class-wide friend on AFGResourceNodeBase). After reload the
    // engine restores this BEFORE BeginPlay, so GetResourceClass() returns it -> no assert.
    Node->mResourceClassOverride = ResourceClass;
    // redesign-3 FOLD-IN (reviewer note 1): persist purity too. mPurity is EditInstanceOnly (NOT saved);
    // only mPurityOverride is UPROPERTY(SaveGame), so set it so a built miner's rate survives reload.
    Node->mPurityOverride = Entry.AssignedPurity;
    Node->OnRep_ResourceClassOverride(); // singleplayer-safe: refresh the live representation/visual
    // redesign-21: set BOTH placement flags at the instance level (in addition to the CDO ctor default).
    // mCanPlaceResourceExtractor (EditDefaultsOnly, BP-sourced — NOT the C++ ctor as previously assumed) is the
    // gate the Mk1 extractor hologram checks; without it the Mk1 snap was rejected while portable miners worked.
    // mCanPlacePortableMiner is the separate handheld-miner gate. Set both so every snap path accepts the node.
    Node->mCanPlaceResourceExtractor = true;
    Node->mCanPlacePortableMiner = true;
    // Radiation is computed separately from the resource class: without this,
    // spawned uranium nodes look right but never irradiate. (Friend access.)
    Node->InitRadioactivity();
    Node->UpdateRadioactivity();
    // redesign-3 BUG C: register this relocated node's scanner/map representation at the NEW spot so
    // it pings the resource scanner there. Logged so we can SEE it run (redesign-2's call was silent).
    Node->UpdateNodeRepresentation();
    UE_LOG(LogNodeShuffle, Verbose, TEXT("scanner: registered spawned node representation at %s (%s)"),
        *Entry.Location.ToCompactString(), *ResourceClass->GetName());

    // Spawned nodes need usable collision: the look-at prompt, build gun and miner placement all trace
    // against the Resource profile. Level-placed nodes get this from per-instance level data; ours need
    // a UseBox created in code. Factored into EnsureNodeUseBox so the adopt/early-return path can also
    // (re)create it after reload (the box is not serialized).
    EnsureNodeUseBox(Node);

    // redesign-11 (THE MK1 SNAP FIX): register the node into the resource-node MANAGER's mResourceNodes
    // list — the registry the Miner Mk1 hologram queries (collision is a confirmed byte-match; registration
    // was the real gap). Re-asserted on adopt too (the list is runtime, not save-persisted).
    RegisterNodeWithManager(Node);

    SpawnedNodes.Add(Entry.EntryGuid, Node);

    // redesign-6 FIX 1 (THE BLOCKER): AFGResourceNode actors are LOGICAL and may be left actor-hidden
    // (they normally render via a separate engine mesh actor / significance management). Un-hide the
    // node actor so our child RockMesh can render. (DressRock -> ForceVisible below also re-asserts this
    // AFTER OnRep_ResourceClassOverride, which may toggle the engine's own visual.)
    Node->SetActorHiddenInGame(false);

    // redesign-7 FIX 2 (THE LOCATION BUG): now the actor exists and has its real root — GUARANTEE RockMesh
    // is a CHILD of the root (never the root itself) BEFORE dressing. If RockMesh were the root, DressRock's
    // SetRelativeLocation((0,0,-40)) would teleport the whole node to the world origin (the redesign-6 bug).
    Node->EnsureRockChildOfRoot();

    // VISUAL (redesign-5/6): dress the node's OWN RockMesh subobject + force the whole chain visible.
    const EResourceForm SpawnForm =
        UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(ResourceClass));
    if (SpawnForm == EResourceForm::RF_LIQUID)
    {
        RebuildNodeNativeVisual(Node);
        SpawnedRockLiquid++;
        UE_LOG(LogNodeShuffle, Verbose, TEXT("spawned LIQUID node %s (oil decal, no mesh actor)"),
            *ResourceClass->GetName());
    }
    else
    {
        SpawnVisualRockForNode(Node, ResourceClass, Entry.EntryGuid); // -> DressRock -> ForceVisible
    }

    // redesign-6 FIX 1 RUNTIME-STATE DIAGNOSTIC (mandatory): log the actual render-state truth for the
    // first N spawned nodes right after dressing, so we KNOW whether the rock is registered+visible at
    // the right location (code review said "should render" 3x while it didn't).
    if (RenderDiagSpawnLogged < RenderDiagMax)
    {
        Node->LogRenderState(TEXT("spawn"));
        RenderDiagSpawnLogged++;
    }

    // The node was already settled onto terrain BEFORE the spawn (Entry.bRayCasted
    // was set true above), so it is grounded from birth — no floating, no deferred
    // settle pass needed, and solid collision is safe.
    UE_LOG(LogNodeShuffle, Verbose, TEXT("Spawn-on-discovery: materialized %s node at %s"),
        *ResourceClass->GetName(), *Entry.Location.ToCompactString());
    bOutChangedWorld = true;
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
        if (Node && IsValid(*Node) && RaycastSettle(Entry, *Node, nullptr))
        {
            // redesign-5: the rock is a subobject of the node, so moving the node moves the rock too.
            (*Node)->SetActorLocationAndRotation(Entry.Location, Entry.Rotation);
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
        // correct-visual-6: flag modded-origin records so SuppressOriginalNodes never hides them
        // (its node OR its native rock) — the second hide path that re-opened the modded-blank bug.
        Rec.bModdedOrigin = !E.OriginalResourceClassPath.StartsWith(TEXT("/Game/"));
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

    // redesign-1: every recorded original is an UNOCCUPIED node we want GONE (vanilla AND modded).
    // The only protection is live occupancy (a miner the player built) — checked per node.
    int32 NodesHidden = 0;
    for (const FNodeShuffleSuppressedOriginal& Rec : OriginalNodeRecord)
    {
        bool bNear = false;
        for (const FVector& P : Players)
        {
            if (FVector::DistSquared2D(P, Rec.Location) < FMath::Square(SuppressPlayerRange)) { bNear = true; break; }
        }
        if (!bNear)
        {
            continue;
        }

        // Hide the original node actor whole (this is what removes its rock, INCLUDING an instanced
        // one — the analyst-validated mechanism). Never touch an occupied node (a built miner).
        // redesign-6 FIX 2: use the BASE finder so esc_ (Base-only) originals are hidden too. Occupancy
        // is checked on the Base (IsOccupied is a Base method) plus the Node-only portable-miner check.
        if (AFGResourceNodeBase* Node = FindOriginalBaseByPath(Rec.VanillaNodePath))
        {
            AFGResourceNode* AsNode = Cast<AFGResourceNode>(Node);
            const bool bOcc = Node->IsOccupied() || (AsNode && IsNodeOccupiedAnyway(AsNode));
            if (!bOcc)
            {
                bool bChanged = false;
                if (Node->GetActorEnableCollision()) { Node->SetActorEnableCollision(false); bChanged = true; }
                if (!Node->IsHidden()) { Node->SetActorHiddenInGame(true); bChanged = true; }
                if (AFGNodeMeshActor* MeshActor = FindMeshActorForNode(Node))
                {
                    MeshActor->SetActorHiddenInGame(true);
                    MeshActor->SetActorEnableCollision(false);
                }
                // redesign-3 BUG C: SetActorHiddenInGame hides the rock but does NOT remove the node from
                // the resource-map / scanner registry, so emptied originals still PING the scanner.
                // redesign-2's calls produced ZERO log evidence — they were gated on bChanged (so a node
                // hidden in a prior pass was never deregistered) and silent. Now: deregister on the FIRST
                // time we see this original (tracked in ScannerDeregistered), independent of bChanged, and
                // LOG it so we can confirm it actually runs. RemoveResourceNodeScan_Local clears the local
                // map reveal; UpdateNodeRepresentation refreshes the map entry so the empty spot stops showing.
                if (!ScannerDeregistered.Contains(Rec.VanillaNodePath))
                {
                    Node->RemoveResourceNodeScan_Local();
                    Node->UpdateNodeRepresentation();
                    // redesign-11 SECONDARY: also remove this hidden original from the resource-node MANAGER's
                    // mResourceNodes so a Mk1 can't snap to an invisible ghost original (the registry the
                    // hologram queries). Only ever removes an original we just hid — never our spawned nodes.
                    DeregisterNodeFromManager(Node);
                    ScannerDeregistered.Add(Rec.VanillaNodePath);
                    ScannerDeregisterCount++;
                    UE_LOG(LogNodeShuffle, Verbose,
                        TEXT("scanner: DEREGISTERED hidden original %s (scan + representation + manager mResourceNodes)"),
                        *Rec.VanillaNodePath);
                }
                if (bChanged) { NodesHidden++; }
            }
        }
    }

    // BACKSTOP: hide any separate node-rock mesh sitting at a suppressed original's location with no
    // node actor behind it (a rare actor-independent rock). Never touch deposits, fracking, our own
    // spawned rocks, or instanced components (world-shared).
    int32 RocksHidden = 0;
    for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
    {
        UStaticMeshComponent* Smc = *It;
        if (!IsValid(Smc) || Smc->GetWorld() != GetWorld() || !Smc->GetStaticMesh()
            || Cast<UInstancedStaticMeshComponent>(Smc) || !Smc->IsVisible())
        {
            continue;
        }
        AActor* RockOwner = Smc->GetOwner();
        if (Cast<AFGResourceDeposit>(RockOwner) || IsFrackingActor(RockOwner)
            || (RockOwner && RockOwner->IsA<ANodeShuffleResourceNode>()))
        {
            continue; // deposit / fracking / our own spawned rock (RockMesh subobject) — never hide
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
        if (!bNear) { continue; }
        bool bAtSuppressed = false;
        for (const FNodeShuffleSuppressedOriginal& Rec : OriginalNodeRecord)
        {
            if (FVector::DistSquared2D(Rec.Location, Loc) < FMath::Square(RockOwnRange)) { bAtSuppressed = true; break; }
        }
        if (bAtSuppressed)
        {
            Smc->SetVisibility(false, true);
            Smc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            RocksHidden++;
        }
    }

    if (NodesHidden > 0 || RocksHidden > 0 || ScannerDeregisterCount > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("Hide originals: hid %d original nodes and %d stray original rocks; deregistered %d from scanner (running total) (Hide & Replace)"),
            NodesHidden, RocksHidden, ScannerDeregisterCount);
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
    // redesign-3: our OWN spawned relocated nodes now PERSIST (non-transient) and are our own subclass.
    // Identify them by TYPE (Tags don't survive reload). A re-roll must never treat a restored spawned
    // node as an original to shuffle again (it would compound relocations every roll).
    if (Node->IsA<ANodeShuffleResourceNode>())
    {
        OutReason = TEXT("ANodeShuffleResourceNode (our spawned node, not an original)");
        return false;
    }
    // Resource deposits are the small one-off pickup rocks, never a shuffle node.
    if (Node->IsA<AFGResourceDeposit>())
    {
        OutReason = TEXT("AFGResourceDeposit (pickup, not a node)");
        return false;
    }

    const UClass* ResClass = Node->GetResourceClass();
    if (ResClass == nullptr)
    {
        OutReason = TEXT("no resource class");
        return false;
    }

    // redesign-4 BUG 2 (THE SPINE — 3rd attempt, stop guessing). DECOUPLE pool-eligibility from
    // modded-classification. Eligibility now admits ANY real AFGResourceNode by NODE PROPERTIES alone
    // (resource class present, Node-type, eligible form) — the /Game/-vs-modded distinction is computed
    // ONLY to drive (a) the vanilla strictness on a genuine vanilla node and (b) the VISUAL later
    // (authored rock vs quartz). It NO LONGER gates whether the node enters the pool. This is what finally
    // admits AllMinable's esc_ SOLID nodes (esc_OreIron/Stone/Coal/...), which carry crafted-ITEM
    // descriptors and/or report a non-solid/RF_INVALID form — both previously rejected them.
    const bool bResourceVanilla = ResClass->GetPathName().StartsWith(TEXT("/Game/"));
    const bool bNodeClassVanilla = Node->GetClass()->GetPathName().StartsWith(TEXT("/Game/"));
    const bool bVanillaNode = bResourceVanilla && bNodeClassVanilla; // genuine vanilla = both vanilla

    // MODDED opt-in: a non-vanilla node only joins when IncludeModdedNodes is on.
    if (!bVanillaNode && !bIncludeModded)
    {
        OutReason = TEXT("modded node and IncludeModdedNodes is off");
        return false;
    }

    // VANILLA strictness (applies ONLY to genuine vanilla nodes — keeps vanilla non-resource junk and
    // companion artifacts out of the pool/balance floor). Modded nodes skip all of these.
    if (bVanillaNode)
    {
        if (!ResClass->IsChildOf(UFGResourceDescriptor::StaticClass()))
        {
            OutReason = TEXT("vanilla resource class is not a UFGResourceDescriptor");
            return false;
        }
        const EResourcePurity Purity = Node->GetResourcePurity();
        if (Purity != RP_Inpure && Purity != RP_Normal && Purity != RP_Pure)
        {
            OutReason = TEXT("vanilla node with non-standard purity (Other Purity artifact)");
            return false;
        }
        if (Node->GetResourceAmount() != EResourceAmount::RA_Infinite)
        {
            OutReason = TEXT("vanilla node is not RA_Infinite");
            return false;
        }
    }

    // NODE-TYPE GATE (vanilla AND modded). Only plain Node type participates; geysers, fracking
    // cores/satellites and deposits are never shuffled.
    if (Node->GetResourceNodeType() != EResourceNodeType::Node)
    {
        OutReason = TEXT("not EResourceNodeType::Node (geyser/fracking/deposit)");
        return false;
    }

    // RESOURCE FORM GATE. SOLID is always allowed. Liquid/gas (oil, gas-form modded) join ONLY with the
    // experimental flag (both need special extractors). redesign-4 BUG 2 relaxation: a MODDED node whose
    // descriptor reports RF_INVALID/unknown (common for AllMinable's crafted-ITEM esc_ descriptors) is
    // treated as SOLID so it still enters the pool — its mining is driven by the node, not the descriptor
    // form. RF_INVALID on a VANILLA node stays rejected as principled junk.
    const EResourceForm Form = Node->GetResourceForm();
    const bool bExperimentalForm = (Form == EResourceForm::RF_LIQUID) || (Form == EResourceForm::RF_GAS);
    const bool bModdedUnknownForm = (!bVanillaNode) && (Form != EResourceForm::RF_LIQUID)
        && (Form != EResourceForm::RF_GAS); // modded solid OR modded RF_INVALID -> treat as solid
    const bool bFormAllowed = (Form == EResourceForm::RF_SOLID)
        || bModdedUnknownForm
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

void ANodeShuffleSubsystem::DiagnoseEscClassHierarchy()
{
    // redesign-6 FIX 2: the analyst proved esc_ AllMinable nodes are NOT enumerated by
    // TActorIterator<AFGResourceNode> — a different actor class entirely. Find every distinct actor whose
    // class name/path mentions esc_ / AllMinable and print its FULL super-class chain, plus whether it is
    // an AFGResourceNodeBase / AFGResourceNode. This names their real type so we route them correctly.
    if (bEscClassHierarchyLogged)
    {
        return;
    }
    bEscClassHierarchyLogged = true;
    TSet<FString> SeenClasses;
    int32 Logged = 0;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        AActor* A = *It;
        if (!IsValid(A)) { continue; }
        const FString ClassPath = A->GetClass()->GetPathName();
        const FString ClassName = A->GetClass()->GetName();
        const bool bEsc = ClassPath.Contains(TEXT("esc_")) || ClassName.Contains(TEXT("esc_"))
            || ClassPath.Contains(TEXT("AllMinable")) || A->GetName().Contains(TEXT("esc_"));
        if (!bEsc) { continue; }
        if (SeenClasses.Contains(ClassName)) { continue; }
        SeenClasses.Add(ClassName);

        // Build the super-class chain.
        FString Chain;
        for (UClass* C = A->GetClass(); C; C = C->GetSuperClass())
        {
            Chain += C->GetName();
            if (C->GetSuperClass()) { Chain += TEXT(" <- "); }
        }
        const bool bIsBase = A->IsA<AFGResourceNodeBase>();
        const bool bIsNode = A->IsA<AFGResourceNode>();
        UE_LOG(LogNodeShuffle, Display,
            TEXT("ESC-HIERARCHY: actor='%s' class='%s' isResNodeBase=%d isResNode=%d chain=[%s]"),
            *A->GetName(), *ClassPath, bIsBase ? 1 : 0, bIsNode ? 1 : 0, *Chain);
        if (++Logged >= 12) { break; } // a handful of distinct classes is enough to identify the type
    }
    if (Logged == 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("ESC-HIERARCHY: no actor with 'esc_'/'AllMinable' in its class/name found in the streamed world."));
    }
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
        const bool bIsResDesc = ResClass && ResClass->IsChildOf(UFGResourceDescriptor::StaticClass());
        // redesign-4 BUG 2: full per-distinct-class diagnostic — node-class PATH, resource-class PATH,
        // whether it's a UFGResourceDescriptor, form, and the EXACT gate that decided. Once per class.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("Modded-node eligibility: nodeClass='%s' resClass='%s' isResDesc=%d purity=%d nodeType=%d amount=%d form=%d -> %s (gate: %s)"),
            *Node->GetClass()->GetPathName(), ResClass ? *ResClass->GetPathName() : TEXT("<null>"),
            bIsResDesc ? 1 : 0,
            (int32)Node->GetResourcePurity(), (int32)Node->GetResourceNodeType(),
            (int32)Node->GetResourceAmount(), (int32)Node->GetResourceForm(),
            bEligible ? TEXT("SHUFFLED") : TEXT("EXCLUDED"), Reason);
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


bool ANodeShuffleSubsystem::IsFrackingActor(const AActor* Actor)
{
    // FIX B: detect fracking wells GENERICALLY by node TYPE (not mesh name). Both the
    // core (AFGResourceNodeFrackingCore) and each satellite
    // (AFGResourceNodeFrackingSatellite) report their type via GetResourceNodeType().
    // Anything that is a fracking core/satellite is left exactly as vanilla.
    const AFGResourceNodeBase* Base = Cast<AFGResourceNodeBase>(Actor);
    if (!Base) { return false; }
    const EResourceNodeType T = Base->GetResourceNodeType();
    return T == EResourceNodeType::FrackingCore || T == EResourceNodeType::FrackingSatellite;
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


void ANodeShuffleSubsystem::RebuildMeshActorCache()
{
    // engine-reskin-3 ROOT-CAUSE FIX. Pair every node to its OWN AFGNodeMeshActor.
    //
    // -2 built this cache ONLY from the mesh-actor SIDE back-link
    // (AFGNodeMeshActor::mNodeActor). In this heavily-modded world that back-link is
    // unpopulated for ~98% of nodes, so the cache was nearly empty and EngineReskinNode
    // fell back to the (non-mesh) decal rebuild — proven by the log (562 via-mesh-actor
    // vs 27,773 no-mesh-actor). The authoritative link is the NODE side:
    // AFGResourceNodeBase::mMeshActor (a TSoftObjectPtr<AActor> set per level instance,
    // friend-accessible). We resolve THAT for every node; when it points at an
    // AFGNodeMeshActor we cache it AND repair the missing back-link via SetNodeActor so
    // OverrideMeshAndMaterials applies the OVERRIDE resource's AUTHORED visual.
    //
    // Rebuilt fresh each ApplyLayout pass (mirrors VanillaNodeCache): mesh actors stream
    // in/out, and a re-sweep drops stale/null weak keys for free.
    MeshActorCache.Reset();
    int32 FromBackLink = 0, FromForwardLink = 0;

    // 1. Back-link sweep (kept): mesh actors that DID get their mNodeActor set.
    for (TActorIterator<AFGNodeMeshActor> It(GetWorld()); It; ++It)
    {
        if (AFGResourceNodeBase* Paired = It->mNodeActor.Get())
        {
            MeshActorCache.Add(Paired, *It);
            FromBackLink++;
        }
    }

    // 2. Forward-link sweep (THE fix): each node's own mMeshActor link. Friend access lets
    //    us read the private soft pointer directly. Cast the target to AFGNodeMeshActor;
    //    only that class exposes OverrideMeshAndMaterials. Skip our own transient spawns
    //    (they are cached at spawn time) and fracking (left vanilla).
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        AFGResourceNode* Node = *It;
        if (!IsValid(Node) || Node->HasAnyFlags(RF_Transient) || IsFrackingActor(Node))
        {
            continue;
        }
        if (MeshActorCache.Contains(Node))
        {
            continue; // already paired via the back-link
        }
        // mMeshActor is private on AFGResourceNodeBase; friend access (AccessTransformers)
        // makes the soft pointer readable. .Get() resolves it if the actor is loaded.
        AActor* RockActor = Node->mMeshActor.Get();
        if (AFGNodeMeshActor* MA = Cast<AFGNodeMeshActor>(RockActor))
        {
            // Repair the engine's own back-link so the game (and our cache) agree.
            if (!MA->mNodeActor.Get())
            {
                MA->SetNodeActor(Node);
            }
            MeshActorCache.Add(Node, MA);
            FromForwardLink++;
        }
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("Mesh-actor cache: %d paired (%d via mesh-actor back-link, %d via node->mMeshActor forward link)"),
        MeshActorCache.Num(), FromBackLink, FromForwardLink);
}

AFGNodeMeshActor* ANodeShuffleSubsystem::FindMeshActorForNode(AFGResourceNodeBase* Node) const
{
    const TWeakObjectPtr<AFGNodeMeshActor>* Found = MeshActorCache.Find(Node);
    return Found && Found->IsValid() ? Found->Get() : nullptr;
}

AFGResourceNode* ANodeShuffleSubsystem::FindVanillaNodeByPath(const FString& Path) const
{
    // The cache is now keyed by BASE node; Cast to Node (null for Base-only esc_ originals).
    const TWeakObjectPtr<AFGResourceNodeBase>* Found = VanillaNodeCache.Find(Path);
    return (Found && Found->IsValid()) ? Cast<AFGResourceNode>(Found->Get()) : nullptr;
}

AFGResourceNodeBase* ANodeShuffleSubsystem::FindOriginalBaseByPath(const FString& Path) const
{
    const TWeakObjectPtr<AFGResourceNodeBase>* Found = VanillaNodeCache.Find(Path);
    return (Found && Found->IsValid()) ? Found->Get() : nullptr;
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

    // (engine-reskin-1) The donor-capture mesh set is gone. Orphan node-rocks are now
    // recognized by NAME alone — the same families SweepRockComponents matches — which is
    // sufficient: a free node-rock mesh with no live node/deposit within the owner radius
    // is an orphan ghost from a past layout regardless of how it was created.
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
    // FIX B: fracking cores derive from AFGResourceNodeBase (not AFGResourceNode), so
    // the iterator above misses them. Add every fracking core/satellite as an OWNER so
    // their rocks are always protected from orphan-hiding — fracking wells stay vanilla.
    for (TActorIterator<AFGResourceNodeBase> It(GetWorld()); It; ++It)
    {
        if (IsValid(*It) && IsFrackingActor(*It)) { Owners.Add({It->GetActorLocation(), *It}); }
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
        // FIX B: never hide a fracking core/satellite's own mesh — fracking wells are
        // left exactly as vanilla (the "well center disappeared" bug was this pass
        // hiding a name-matched fracking rock that had no AFGResourceNode within 12 m).
        if (IsFrackingActor(Smc->GetOwner()))
        {
            continue;
        }
        // redesign-5: never hide OUR OWN spawned node rock (now a RockMesh subobject OF our node, which
        // wears an authored ResourceNode_* mesh, so it would otherwise name-match and get hidden as orphan).
        if (Smc->GetOwner() && Smc->GetOwner()->IsA<ANodeShuffleResourceNode>())
        {
            continue;
        }
        const bool bNameMatch = IsNodeRockMeshName(Smc->GetStaticMesh()->GetName(), ExtraPatterns);
        if (!bNameMatch)
        {
            continue; // not a name-matched node rock — leave scenery alone
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
                    TEXT("ORPHANDIAG: '%s' at %s KEPT (name=%d) -> owned by live %s '%s' at %.1fm"),
                    *MeshName, *Loc.ToCompactString(), bNameMatch ? 1 : 0,
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
                TEXT("ORPHANDIAG: '%s' at %s HIDDEN (name=%d) -> nearest live node/deposit %.1fm away (> %.0fm owner radius)"),
                *MeshName, *Loc.ToCompactString(), bNameMatch ? 1 : 0,
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

    // redesign-5: the "ours" set is each spawned node's OWN RockMesh subobject (the rock now lives on the
    // node, not a separate actor). Used only to annotate the diagnostic line (is this rock one WE spawned?).
    TSet<UStaticMeshComponent*> Claimed;
    for (const auto& Pair : SpawnedNodes)
    {
        if (ANodeShuffleResourceNode* OurNode = Cast<ANodeShuffleResourceNode>(Pair.Value))
        {
            if (IsValid(OurNode->RockMesh)) { Claimed.Add(OurNode->RockMesh); }
        }
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
        // Only rock-like meshes: matches the sweep's name families.
        const bool bLooksLikeRock = IsNodeRockMeshName(MeshName, TArray<FString>());
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
