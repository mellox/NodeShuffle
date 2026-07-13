#include "NodeShuffleSubsystem.h"
#include "NodeShuffle.h"
#include "NodeShuffleConfig.h"
#include "NodeShuffleNodeAssets.h"
#include "NodeShuffleResourceNode.h"
#include "NodeShuffleNodeComponent.h"

#include "EngineUtils.h"
#include "TimerManager.h"
#include "Algo/Count.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NodeShuffleBakedData.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/Material.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DecalComponent.h"
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
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableFoundation.h"
#include "Engine/OverlapResult.h"
#include "FGPortableMiner.h"
#include "FGWaterVolume.h"
#include "FGAmbientVolume.h"
#include "FGRadioactivitySubsystem.h"
#include "FGGameState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "FGActorRepresentationManager.h"
#include "Representation/FGResourceNodeRepresentation.h"
#include "Buildables/FGBuildableRadarTower.h"

// real-class redesign: NodeShuffle now relocates each node AS ITS ORIGINAL CLASS and attaches a
// UNodeShuffleNodeComponent for identity + fallback visual. A node is "ours" (a relocated/spawned
// NodeShuffle node, never an original to shuffle) if it carries that component OR is a legacy old-save
// ANodeShuffleResourceNode (kept compiled so old saves still load; phased out by a re-roll). This
// replaces every former IsA<ANodeShuffleResourceNode> identity test.
static bool NodeShuffleIsOurNode(const AActor* Node)
{
    return IsValid(Node)
        && (UNodeShuffleNodeComponent::Find(Node) != nullptr || Node->IsA<ANodeShuffleResourceNode>());
}

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
    // playtest-fixes-1 DEPTH GUARD: the volume test (IsPointInWater) only sees STREAMED AFGWaterVolume
    // actors, and the deep/open ocean has none — so seabed hits passed as "dry" and ~20 nodes settled
    // on the east ocean floor (measured flat shelf at Z ≈ -10,500; deep outliers to -30,220). Any
    // settle hit below this Z is treated as water. Chosen from measured map data: legit node-bearing
    // land bottoms out around -2,000 (beaches) / rare ravines to ~-3,500; the confirmed seabed band
    // starts at ~-4,900. A rare legit deep-ravine spot rejected by this guard just redeals to normal
    // land — harmless; a seabed spot accepted without it is a permanently unreachable node.
    constexpr float DeepWaterFloorZ = -4500.0f;
    // slopefix-1 -> slopefit-1 (user: hills of ANY steepness are fair game — the game is full of
    // them; only true CLIFF FACES reject). The settle gate is a CLIFF gate: near-vertical hits redeal
    // via the water machinery; every walkable slope settles. There was NO gate at all before this
    // (steepfix-1's 22° version was reverted pre-deploy on the user's call).
    constexpr float CliffSlopeDeg = 60.0f;
    const float MinSettleNormalZ = FMath::Cos(FMath::DegreesToRadians(CliffSlopeDeg));
    // slopefit-1: the node ACTOR (interaction box + the transform the Miner hologram inherits) tilts
    // only up to this toward the smoothed ground normal — buildings get near-vanilla geometry. The
    // ROCK VISUAL takes the full slope alignment and sinks into the hill so steep placements look
    // bedded instead of skewered ("not at the slant of the hillside").
    constexpr float NodeTiltClampDeg = 12.0f;
    constexpr float SmoothNormalRingCm = 350.0f;    // radius of the 4-probe normal-smoothing ring
    constexpr float RockSlopeSinkMaxCm = 60.0f;     // extra rock sink, lerped over 10°..CliffSlopeDeg
    // playtest-fixes-3 WATER-LOCKED REDEAL (unanchored): random candidates drawn from the SAME deal
    // box the roll used, filtered by the learned water grid + spacing. No land anchors — anchoring to
    // streamed originals clustered relocations around original sites and defeated the randomization.
    constexpr int32 RedealTries = 10;
    constexpr int32 CensusRadiusCm = 30000;        // NodeShuffle.Here census: 300 m
    // playtest-fixes-3 LEARNED WATER GRID: 100 m cells; every ground probe teaches land/water; persisted
    // globally (the map is static) in Configs/NodeShuffle_WaterGrid.json across saves and re-rolls.
    constexpr float WaterGridCellCm = 10000.0f;    // 100 m
    constexpr int32 WaterGridFlushEvery = 500;     // safety flush after N new samples (also on save/quit)

    FORCEINLINE int64 NodeShuffleGridKey(const FVector& Loc, float CellCm)
    {
        const int64 CX = static_cast<int64>(FMath::FloorToDouble(Loc.X / CellCm));
        const int64 CY = static_cast<int64>(FMath::FloorToDouble(Loc.Y / CellCm));
        return (CX << 32) | (CY & 0xffffffffLL);
    }
    FORCEINLINE int64 NodeShuffleWaterCellKey(const FVector& Loc)
    {
        return NodeShuffleGridKey(Loc, WaterGridCellCm);
    }
    FORCEINLINE FString NodeShuffleWaterCellString(int64 Key)
    {
        const int32 CX = static_cast<int32>(Key >> 32);
        const int32 CY = static_cast<int32>(Key & 0xffffffffLL);
        return FString::Printf(TEXT("%d,%d"), CX, CY);
    }

    // cave-nodes-1: cavern discovery + placement tuning.
    constexpr float CaveCellCm = 800.0f;          // 8 m flood-fill cells
    constexpr float CaveHeadroomCm = 350.0f;      // min clearance above a floor cell (node + player)
    constexpr float CaveStepMaxCm = 250.0f;       // max floor step/slope between adjacent cells
    constexpr float CaveRoofProbeCm = 15000.0f;   // roofed = up-trace hits within 150 m
    constexpr int32 CaveExpandTracesPerPass = 120; // trace budget per 5 s pass (~40 neighbor tests)
    constexpr int32 CaveMaxCells = 25000;          // global cap (~1.6 km^2 of cave floor)
    // cave-nodes-2: nodes are only PLACED where a Miner building physically fits; lower passages stay
    // mapped for connectivity. -1 (unknown/legacy) ceilings are treated as tall.
    constexpr float CaveMinPlaceCeilingCm = 1200.0f; // 12 m
    // cave-nodes-2: natural cave share of any deal draw, capped so caves can never dominate a roll
    // even if the user seeds heavily.
    constexpr float CaveShareCap = 0.25f;
    constexpr float CaveExpandNearPlayerCm = 30000.0f; // expand only where collision is streamed
    FORCEINLINE FVector NodeShuffleCaveCellCenter(int64 Key, float FloorZ)
    {
        const int32 CX = static_cast<int32>(Key >> 32);
        const int32 CY = static_cast<int32>(Key & 0xffffffffLL);
        return FVector((CX + 0.5f) * CaveCellCm, (CY + 0.5f) * CaveCellCm, FloorZ);
    }
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
    FlushWaterGridIfDirty(); // playtest-fixes-3: persist learned water cells on session end
    FlushCaveStoreIfDirty(); // cave-nodes-1: persist discovered cavern floors on session end
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
        // A fresh FIRST roll supersedes any pending re-roll request (e.g. RerollNow left on for a new game).
        if (Config.RerollNow) { ClearRerollNowFlag(); }
    }
    // LIVE / LOAD re-roll. Edge-triggered: fires on each OFF->ON transition of the Re-roll toggle, so it behaves
    // identically whether the user set it and reloaded (load-time) OR toggled it mid-session (LIVE, no reload).
    // The world must have streamed in (the SHUFFLE-AWARE gate, not the strict pristine-vanilla one — a shuffled
    // save never passes the strict 50+ /Game/ count). Occupied/pinned nodes are carried; the new locations
    // reveal as the player explores near them (the same spawn-on-discovery behavior as a load-time re-roll).
    else if (Config.RerollNow && !bPrevRerollNow && IsWorldReadyForReroll())
    {
        RestoreOriginalsForReroll(); // restore originals first so the rescan sees the true vanilla pool
        const int32 RerollSeed = Config.SeedOverride != 0 ? Config.SeedOverride
            : static_cast<int32>(FPlatformTime::Cycles());
        UE_LOG(LogNodeShuffle, Display, TEXT("Re-roll triggered (Re-roll toggle): seed %d -> %d (LIVE if mid-session)"),
            SavedSeed, RerollSeed);
        RollLayout(RerollSeed, true);
        RefreshScannersAndRadarTowers(); // live re-roll: refresh handheld-scanner clusters + radar towers now
        const bool bCleared = ClearRerollNowFlag(); // one-shot: clear so it never loops
        UE_LOG(LogNodeShuffle, Display, TEXT("Re-roll done; Re-roll toggle cleared in config: %s"),
            bCleared ? TEXT("yes") : TEXT("NO (could not write config; turn it off manually)"));
    }
    bPrevRerollNow = Config.RerollNow; // track for the OFF->ON edge (after a clear the local copy is still true;
                                       // next tick re-reads the cleared value, so it can't loop)

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
            // playtest-fixes-1 (ghost radiation, restore side): hiding removed this original's radiation
            // emitter; un-hiding must give it back. InitRadioactivity re-reads the resource class and
            // re-registers under a stable UID (FindOrAddEmitter), so re-running it never duplicates.
            // Base-only esc_ originals have no InitRadioactivity — the new roll re-hides (and re-removes)
            // them anyway, so the brief pristine window is accepted.
            if (AFGResourceNode* AsNode = Cast<AFGResourceNode>(Node))
            {
                AsNode->InitRadioactivity();
                AsNode->UpdateRadioactivity(); // cold-review parity: always paired at the other two call sites
            }
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

void ANodeShuffleSubsystem::PostLoadGame_Implementation(int32 /*saveVersion*/, int32 /*gameVersion*/)
{
    // Layout-format version gate (cheap insurance, mirrors Resource Roulette's version-compare-on-load).
    // If a save's layout predates the current format, run migration (today a logged no-op) then re-stamp,
    // so a FUTURE FNodeShuffleEntry/layout change can migrate old saves deterministically instead of
    // silently misreading stored entries. LayoutVersion is loaded from SaveGame before this runs.
    if (bLayoutGenerated && LayoutVersion < CurrentLayoutVersion)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("Layout migration: saved layout v%d < current v%d. No structural migration is required for "
                 "this version (the entry format is unchanged); re-stamping to v%d."),
            LayoutVersion, CurrentLayoutVersion, CurrentLayoutVersion);
        // FUTURE: per-version migration steps go here, e.g.
        //   if (LayoutVersion < 3) { /* convert v2 entries -> v3 */ }
        LayoutVersion = CurrentLayoutVersion;
    }
}

void ANodeShuffleSubsystem::RollLayout(int32 Seed, bool bIsReroll)
{
    const FNodeShuffleConfigStruct Config = FNodeShuffleConfigStruct::GetActiveConfig(this);
    FRandomStream Rng(Seed);

    // real-class redesign: a new roll (initial OR re-roll) rebuilds the layout, so clear the once-per-roll
    // modded-visual guard — every modded node's native visual must be (re)built fresh for the new roll.
    ModdedVisualRebuilt.Empty();

    // FIX 4: name exactly which modded node classes shuffle (and why the rest do
    // not). Once per class per session. This is how "lithium never shuffles" stops
    // being a guess.
    DiagnoseModdedNodeEligibility(Config.IncludeModdedNodes, true);

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
        {
            // Liquid (oil) + gas (e.g. lithium) live scan — these forms join the re-roll pool. Solids are
            // already in the rebuilt pool; this captures liquid/gas the saved layout did not.
            int32 ExperimentalAdded = 0;
            for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
            {
                AFGResourceNode* Node = *It;
                const EResourceForm LiveForm = Node->GetResourceForm();
                const bool bExpForm = (LiveForm == EResourceForm::RF_LIQUID || LiveForm == EResourceForm::RF_GAS); // oil + gas (lithium)
                // Non-solid live scan (oil + gas): solids are already in the rebuilt pool.
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
                TEXT("Liquid-form augment: added %d live RF_LIQUID (oil) nodes not in the saved pool to the reroll pool (captured into the layout for determinism)"),
                ExperimentalAdded);
        }

        // FULL RE-SCAN augment. The reroll pool is rebuilt from the saved Layout (streaming-independent),
        // which is INCOMPLETE: the original roll only captured nodes that were LOADED at roll time, and a
        // reroll rebuilding from that layout never re-introduces the rest — so uncaptured originals stay
        // visible at their spots forever (proven by HIDEDIAG census: nonOurs uncapturedVisible BP_Resource
        // Node_C). Satisfactory uses a STATIC map with all resource-node actors effectively loaded (our
        // CACHE-REFRESH showed ~1012 nodes stable), and Resource Roulette likewise collects every node via
        // a plain TActorIterator — so a live re-scan HERE captures everything the saved layout missed.
        // SOLID-only (liquid/gas handled by the experimental-form augment above). Deduped by path against
        // the rebuilt pool. VANILLA solids: always captured (the core fix). MODDED solids (AllMinable esc_,
        // modded ores): captured when IncludeModdedNodes is on (they relocate like vanilla nodes).
        {
            int32 VanillaAdded = 0, ModdedAdded = 0;
            for (TActorIterator<AFGResourceNodeBase> It(GetWorld()); It; ++It)
            {
                AFGResourceNodeBase* BaseNode = *It;
                if (!IsValid(BaseNode) || IsFrackingActor(BaseNode)) { continue; }
                if (NodeShuffleIsOurNode(BaseNode)) { continue; } // never our own spawned nodes
                const UClass* RC = BaseNode->GetResourceClass();
                if (!RC) { continue; }
                // SOLID only; plain Node type only (no geyser/fracking).
                const EResourceForm BF = BaseNode->GetResourceForm();
                if (BF == EResourceForm::RF_LIQUID || BF == EResourceForm::RF_GAS) { continue; }
                if (BaseNode->GetResourceNodeType() != EResourceNodeType::Node) { continue; }

                const bool bModded = !RC->GetPathName().StartsWith(TEXT("/Game/"))
                    || !BaseNode->GetClass()->GetPathName().StartsWith(TEXT("/Game/"));
                AFGResourceNode* Node = Cast<AFGResourceNode>(BaseNode); // null for Base-only esc_ nodes

                // ELIGIBILITY + GATING. Vanilla: must be an AFGResourceNode passing the full predicate.
                // Modded: gated behind IncludeModdedNodes; a Base-only esc_ node (Node==null) is admitted
                // by the checks already done (RC set, Node type, solid form) like the initial scan does.
                if (bModded)
                {
                    if (!Config.IncludeModdedNodes) { continue; }
                    if (Node && !IsEligibleVanillaNode(Node, true, false)) { continue; }
                }
                else
                {
                    if (!Node || !IsEligibleVanillaNode(Node, Config.IncludeModdedNodes, false)) { continue; }
                }

                const FString Path = BaseNode->GetPathName();
                if (ExistingPaths.Contains(Path)) { continue; }
                ExistingPaths.Add(Path);

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
                bModded ? ModdedAdded++ : VanillaAdded++;
            }
            UE_LOG(LogNodeShuffle, Display,
                TEXT("Full re-scan augment: added %d uncaptured VANILLA + %d MODDED solid nodes to the reroll pool via live scan (modded gated by IncludeModdedNodes=%d). These were loaded but missing from the saved layout."),
                VanillaAdded, ModdedAdded, Config.IncludeModdedNodes ? 1 : 0);
        }
    }
    else
    {
        // INITIAL roll: live scan of the (fully streamed) vanilla pool.
        // Liquid (oil) AND gas (e.g. lithium) forms join the pool alongside solids.
        const bool bIncludeLiquid = true;

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
            if (NodeShuffleIsOurNode(BaseNode)) { continue; }
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
                // a resource class, and is a plain Node. bIncludeLiquid is true, so liquid/gas pass too.
                if (!Config.IncludeModdedNodes) { continue; }
                if (!RC) { continue; }
                if (BaseNode->GetResourceNodeType() != EResourceNodeType::Node) { continue; }
                const EResourceForm BF = BaseNode->GetResourceForm();
                // oil + gas (lithium etc.) shuffle when non-solid forms are on; the real-class spawn
                // relocates them as their own node class so their special extractor still works.
                if ((BF == EResourceForm::RF_LIQUID || BF == EResourceForm::RF_GAS) && !bIncludeLiquid) { continue; }
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
    TSet<int32> NewUnderground; // cave-nodes-2: indices of locations that are cave-floor picks
    if (!ReadCustomLocationsJson(NewLocations))
    {
        // cave-nodes-4: cave placement is a FIX for the cave-drain regression (the shuffle hid cave
        // originals and could never place anything back inside) — always on, like discovery.
        GenerateNewLocations(Rng, VanillaLocations, AvoidLocations, FMath::Max(0, Config.NewNodeCount), NewLocations,
                             &NewUnderground);
        WriteGeneratedLocationsJson(NewLocations);
    }

    // The vanilla entries are already in NewLayout (built above); append new-node
    // location entries to complete the pool.
    NewLayout.Reserve(NewLayout.Num() + NewLocations.Num() + CarriedEntries.Num());
    for (int32 LocIdx = 0; LocIdx < NewLocations.Num(); LocIdx++)
    {
        FNodeShuffleEntry E;
        E.EntryGuid = FGuid::NewGuid();
        E.bIsNewNode = true;
        E.Location = NewLocations[LocIdx];
        E.Rotation = FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f);
        E.bActive = false;
        E.NodeClassPath = SpawnableNodeClassPath;
        E.bUnderground = NewUnderground.Contains(LocIdx); // settles via the short in-cave trace
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

    // real-class redesign: GAS resources (e.g. lithium) are RELOCATE-ONLY — never retyped and never dealt
    // onto other slots — because their special modded extractor (AlkaLib's reactive ore extractor) is bound
    // to that one resource and outputs it regardless. A gas node relocates carrying its own resource + its
    // own modded node class. Identify gas by the descriptor's ACTUAL form so it holds across all capture
    // paths (initial roll, re-roll augment, carried entries).
    const auto IsGasResourcePath = [&](const FString& Path) -> bool
    {
        UClass* RC = LoadClassByPath(Path);
        return RC && UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(RC)) == EResourceForm::RF_GAS;
    };

    TArray<FString> SolidDeck;
    TArray<FString> LiquidDeck;
    for (const FString& Kind : ResourceKinds)
    {
        if (IsGasResourcePath(Kind)) { continue; } // gas: relocate-only, never enters a deal deck
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
        // GAS slots are relocate-only: keep their original (gas) resource + class, never retype. (New nodes
        // never carry gas — they only draw vanilla /Game/ cards below — so this only guards existing slots.)
        if (!E.bIsNewNode && IsGasResourcePath(E.AssignedResourceClassPath)) { continue; }

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
    // keep the solid class. Liquid/gas are part of the pool now, so this handles
    // them whenever FormByResource has such entries.
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
        TSet<int32> RelocUnderground; // cave-nodes-2: which relocation spots are cave floors
        if (NeedRelocation > 0)
        {
            // Avoid everything already placed: the map pool, occupied/kept, original-record (none yet),
            // and the new-node locations we already generated this roll.
            TArray<FVector> RelocAvoid = AvoidLocations;
            for (const FNodeShuffleEntry& E : NewLayout)
            {
                if (E.bIsNewNode) { RelocAvoid.Add(E.Location); }
            }
            GenerateNewLocations(Rng, VanillaLocations, RelocAvoid, NeedRelocation, RelocSpots,
                                 &RelocUnderground); // cave-nodes-4: always on (cave-drain fix)
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
                E.Location = RelocSpots[RelocCursor];
                E.bUnderground = RelocUnderground.Contains(RelocCursor); // cave-floor pick -> short-trace settle
                RelocCursor++;
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
    LayoutVersion = CurrentLayoutVersion; // stamp the format version so future loads can migrate
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
                                                 int32 Count, TArray<FVector>& OutLocations,
                                                 TSet<int32>* OutUndergroundIndices)
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
    // playtest-fixes-1 PERCENTILE BOUNDS: the raw min/max bounding box of the known-node set includes
    // the ocean bands along the map edges (and, post-re-roll, any stray previously-relocated offshore
    // location that leaked into the pool) — measured cost: 261 of 927 entries dealt over water/void,
    // permanently stuck retrying. Use the 2nd..98th percentile per axis instead: robust to outliers,
    // still spans the whole island, cuts the box's open-sea corners where most water rolls landed.
    TArray<float> Xs, Ys;
    Xs.Reserve(VanillaLocations.Num());
    Ys.Reserve(VanillaLocations.Num());
    for (const FVector& V : VanillaLocations)
    {
        Xs.Add(V.X);
        Ys.Add(V.Y);
    }
    Xs.Sort();
    Ys.Sort();
    const auto Percentile = [](const TArray<float>& Sorted, float P) -> float
    {
        const int32 Idx = FMath::Clamp(FMath::RoundToInt(P * (Sorted.Num() - 1)), 0, Sorted.Num() - 1);
        return Sorted[Idx];
    };
    FVector Min, Max;
    if (VanillaLocations.Num() >= 20)
    {
        Min = FVector(Percentile(Xs, 0.02f), Percentile(Ys, 0.02f), 0.f);
        Max = FVector(Percentile(Xs, 0.98f), Percentile(Ys, 0.98f), 0.f);
    }
    else
    {
        // Tiny pools (custom JSON lists): percentile is meaningless — keep the old min/max + inset.
        FBox Bounds(ForceInit);
        for (const FVector& V : VanillaLocations) { Bounds += V; }
        const float Inset = MinNodeSpacing; // 25 m
        Min = Bounds.Min + FVector(Inset, Inset, 0.f);
        Max = Bounds.Max - FVector(Inset, Inset, 0.f);
    }
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

    // playtest-fixes-3: persist the deal box + probe height so the water-locked redeal draws RANDOM
    // candidates from the same distribution later (no land-anchoring — randomization is the product).
    DealBoundsMin = Min;
    DealBoundsMax = Max;
    DealMeanZ = MeanZ;

    // cave-nodes-2: caves join the candidate space at their NATURAL share — the probability that
    // encodes vanilla's own cave-node density (seeds per pool slot), never a quota or a preference.
    float CaveChance = 0.0f;
    if (OutUndergroundIndices)
    {
        EnsureCaveStoreLoaded();
        CaveChance = FMath::Min(CaveShareCap,
            static_cast<float>(CaveSeedCount) / FMath::Max(1, VanillaLocations.Num() + Count));
    }

    int32 GridRejected = 0; // candidates dropped because the learned grid knows their cell is water
    int32 CavePicks = 0;
    int32 Attempts = Count * 60;
    while (OutLocations.Num() < Count && Attempts-- > 0)
    {
        bool bCavePick = false;
        FVector Candidate;
        if (CaveChance > 0.0f && Rng.FRand() < CaveChance && TryPickRawCaveCell(Rng, Candidate))
        {
            bCavePick = true; // spacing checked below like any candidate; the water grid is a
                              // surface concept and does not apply to a verified cave floor
        }
        else
        {
            Candidate = FVector(Rng.FRandRange(Min.X, Max.X), Rng.FRandRange(Min.Y, Max.Y), MeanZ);
            // playtest-fixes-3: excluded areas — skip cells the persistent water grid has already
            // proven to be water. Unknown/mixed cells still pass (the settle probe is ground truth).
            if (IsKnownWaterCell(Candidate))
            {
                GridRejected++;
                continue;
            }
        }

        // FIX 2: reject candidates within min-spacing of ANY occupied/kept/
        // original location (AvoidLocations = vanilla pool + carried/pinned +
        // original-node record + live occupied nodes), not just the vanilla pool.
        // cave-nodes-2: cave picks space in 3D — a surface location 2D-above a cavern (25 m+ higher)
        // must not veto the cell; nodes inside the same cavern still space out.
        const float SpacingSq = FMath::Square(MinNodeSpacing);
        const auto TooCloseTo = [&](const FVector& Existing) -> bool
        {
            return bCavePick
                ? FVector::DistSquared(Existing, Candidate) < SpacingSq
                : FVector::DistSquared2D(Existing, Candidate) < SpacingSq;
        };
        bool bTooClose = false;
        for (const FVector& Existing : AvoidLocations)
        {
            if (TooCloseTo(Existing)) { bTooClose = true; break; }
        }
        if (!bTooClose)
        {
            for (const FVector& Existing : OutLocations)
            {
                if (TooCloseTo(Existing)) { bTooClose = true; break; }
            }
        }
        if (!bTooClose)
        {
            if (bCavePick)
            {
                OutUndergroundIndices->Add(OutLocations.Num());
                CavePicks++;
            }
            OutLocations.Add(Candidate);
        }
    }
    if (CavePicks > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("Map-wide spread: %d of the locations are CAVE floors (natural share %.1f%%)"),
            CavePicks, CaveChance * 100.0f);
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("Map-wide spread: distributed %d/%d new locations across bounds X[%.0f..%.0f] Y[%.0f..%.0f] (%.1f x %.1f km); water-grid excluded %d candidates"),
        OutLocations.Num(), Count, Min.X, Max.X, Min.Y, Max.Y,
        (Max.X - Min.X) / 100000.0f, (Max.Y - Min.Y) / 100000.0f, GridRejected);
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
        if (NodeShuffleIsOurNode(*It)) { continue; } // our own spawned (relocated) nodes, not originals
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
    DeferredThisPass = 0; // playtest-fixes-1: per-pass deferral tally (summary logged at pass end)

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
                    // visfix-1: include location + resource so miner placements are analyzable from
                    // the log alone (was GUID-only — untraceable without a HERE census nearby).
                    UE_LOG(LogNodeShuffle, Display, TEXT("Pinned occupied spawned node %s (%s) at %s (miner built — never relocate)"),
                        *Entry.EntryGuid.ToString(), *Entry.AssignedResourceClassPath,
                        *(*Live)->GetActorLocation().ToCompactString());
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

    // cave-nodes-1/2: always-on cavern discovery (budgeted traces near players). Placement happens
    // only where locations are DEALT (roll/redeal draws) — never by actively re-homing entries.
    ExpandCaveFloorsBudgeted();

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

    // playtest-fixes-1 (defer-log backoff): ONE summary line when the deferral count changes, instead
    // of re-logging every stuck entry every 5 s (153k lines / 35 MB in one session). Per-entry detail
    // still logs once per session at Verbose (DeferLoggedThisSession).
    if (DeferredThisPass != LastDeferSummary)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("Deferral summary: %d entries deferred this pass (%d distinct water-locked so far this session)"),
            DeferredThisPass, WaterLockedThisSession.Num());
        LastDeferSummary = DeferredThisPass;
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

AFGRadioactivitySubsystem* ANodeShuffleSubsystem::GetRadSubsystem() const
{
    // Via the GameState's public inline getter — AFGRadioactivitySubsystem::Get is a static whose
    // dll-export is not trusted (AFGResourceNodeManager::Get LNK2019'd exactly this way).
    const AFGGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFGGameState>() : nullptr;
    return GS ? GS->GetRadioactivitySubsystem() : nullptr;
}

const FNodeShuffleCapturedVisual* ANodeShuffleSubsystem::FindCapturedVisual(const FString& ResourceClassName) const
{
    for (const FNodeShuffleCapturedVisual& Cap : CapturedVisuals)
    {
        if (Cap.ResourceClassName == ResourceClassName) { return &Cap; }
    }
    return nullptr;
}

const ANodeShuffleSubsystem::FNodeShuffleResolvedCapture* ANodeShuffleSubsystem::ResolveCapturedVisual(const FString& ResourceClassName)
{
    if (const FNodeShuffleResolvedCapture* Cached = ResolvedCaptureCache.Find(ResourceClassName))
    {
        return Cached;
    }
    const FNodeShuffleCapturedVisual* Cap = FindCapturedVisual(ResourceClassName);
    if (!Cap)
    {
        return nullptr; // no persisted capture — do NOT negative-cache (a capture may land later this session)
    }
    FNodeShuffleResolvedCapture Resolved;
    Resolved.Mesh = LoadObject<UStaticMesh>(nullptr, *Cap->MeshPath);
    for (const FString& MatPath : Cap->MaterialPaths)
    {
        Resolved.Materials.Add(LoadObject<UMaterialInterface>(nullptr, *MatPath)); // slot order; null-safe downstream
    }
    Resolved.Scale = Cap->MeshScale;
    if (!Resolved.Mesh.IsValid())
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("CAPTURE: persisted mesh path for %s failed to load ('%s') — falling back to quartz"),
            *ResourceClassName, *Cap->MeshPath);
    }
    return &ResolvedCaptureCache.Add(ResourceClassName, MoveTemp(Resolved));
}

void ANodeShuffleSubsystem::CaptureOriginalVisualIfNeeded(AFGResourceNodeBase* Node)
{
    // playtest-fixes-1 (modded-descriptor visuals). Capture gates, all cheap, most-selective first:
    // a real AFGResourceNode with a REAL resource descriptor (UFGResourceDescriptor — esc_ ITEM nodes
    // stay quartz by design), NO authored table entry, not already captured, and a PAIRED mesh actor
    // (MeshActorCache pairing — the node's own rock, never a neighbor's).
    AFGResourceNode* AsNode = Cast<AFGResourceNode>(Node);
    if (!AsNode) { return; }
    UClass* ResClass = AsNode->GetResourceClass().Get();
    if (!ResClass || !ResClass->IsChildOf(UFGResourceDescriptor::StaticClass())) { return; }
    const FString ShortName = ResClass->GetName();
    if (FNodeShuffleNodeAssets::FindVisual(FName(*ShortName)) != nullptr) { return; } // authored covers it
    if (FindCapturedVisual(ShortName) != nullptr) { return; }                          // already captured
    AFGNodeMeshActor* MeshActor = FindMeshActorForNode(Node);
    if (!MeshActor) { return; }
    UStaticMeshComponent* Smc = MeshActor->FindComponentByClass<UStaticMeshComponent>();
    if (!Smc || !Smc->GetStaticMesh()) { return; }

    FNodeShuffleCapturedVisual Cap;
    Cap.ResourceClassName = ShortName;
    Cap.MeshPath = Smc->GetStaticMesh()->GetPathName();
    for (int32 i = 0; i < Smc->GetNumMaterials(); i++)
    {
        UMaterialInterface* Mat = Smc->GetMaterial(i);
        // A dynamic instance's path is transient — persist its PARENT asset path instead.
        if (const UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Mat))
        {
            Mat = Mid->Parent;
        }
        Cap.MaterialPaths.Add(Mat ? Mat->GetPathName() : FString());
    }
    Cap.MeshScale = Smc->GetComponentScale();
    CapturedVisuals.Add(Cap);
    ResolvedCaptureCache.Remove(ShortName); // drop any stale (pre-capture) resolution
    UE_LOG(LogNodeShuffle, Display,
        TEXT("CAPTURE: %s visual from its own paired mesh actor — mesh '%s', %d mat(s), scale=(%.2f,%.2f,%.2f) (persisted)"),
        *ShortName, *Cap.MeshPath, Cap.MaterialPaths.Num(),
        Cap.MeshScale.X, Cap.MeshScale.Y, Cap.MeshScale.Z);
    RedressSpawnedOfResource(ShortName);
}

void ANodeShuffleSubsystem::RedressSpawnedOfResource(const FString& ResourceClassName)
{
    // A capture landed mid-session: swap every already-spawned node of that resource from the quartz
    // placeholder to the captured look now (DressRock is idempotent), instead of waiting for a reload.
    const FString Suffix = TEXT(".") + ResourceClassName;
    int32 Redressed = 0;
    for (const FNodeShuffleEntry& Entry : Layout)
    {
        if (!Entry.bIsNewNode || !Entry.AssignedResourceClassPath.EndsWith(Suffix)) { continue; }
        AFGResourceNode* const* Live = SpawnedNodes.Find(Entry.EntryGuid);
        if (!Live || !IsValid(*Live)) { continue; }
        if (UClass* ResClass = LoadClassByPath(Entry.AssignedResourceClassPath))
        {
            // visfix-1: a node that was showing its NATIVE mesh pre-capture must hide it now that the
            // captured look exists — otherwise the new rock double-renders against it. Legacy-aware
            // rock exclusion (cold review: a legacy node's own RockMesh must not be swept as "native").
            UNodeShuffleNodeComponent* LiveComp = UNodeShuffleNodeComponent::Find(*Live);
            ANodeShuffleResourceNode* LiveLegacy = LiveComp ? nullptr : Cast<ANodeShuffleResourceNode>(*Live);
            HideNativeNodeMesh(*Live, LiveComp ? LiveComp->RockMesh : (LiveLegacy ? LiveLegacy->RockMesh : nullptr));
            SpawnVisualRockForNode(*Live, ResClass, Entry.EntryGuid);
            Redressed++;
        }
    }
    if (Redressed > 0)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("CAPTURE: re-dressed %d live %s node(s) quartz -> captured visual"),
            Redressed, *ResourceClassName);
    }
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
    // real-class redesign: the fallback rock lives on our companion component now (new real-class nodes);
    // legacy old-save nodes still carry it as an ANodeShuffleResourceNode subobject. Route to whichever
    // this node has. (Modded-origin real nodes never reach here — the spawn path uses their native visual.)
    UNodeShuffleNodeComponent* Comp = UNodeShuffleNodeComponent::Find(Node);
    ANodeShuffleResourceNode* Legacy = Comp ? nullptr : Cast<ANodeShuffleResourceNode>(Node);
    if (!Comp && !Legacy)
    {
        return; // only our own spawned nodes carry the fallback rock
    }

    UStaticMesh* Mesh = ResolveNodeMesh(ResourceClass);
    const TArray<TWeakObjectPtr<UMaterialInterface>>* Mats = ResolveNodeMaterials(ResourceClass);
    bool bQuartz = false;
    bool bCaptured = false;
    // The authored table row carries the correct per-mesh scale (ore ~2.0, coal/sulfur ~0.917, stone ~2.4)
    // and a vertical offset. redesign-5 restores per-mesh scale + Z offset (redesign-4 collapsed to a uniform
    // (0,0,-40)). We derive the LATERAL offset from the mesh's own bounds (below) — the table's authored X was
    // wrong — and keep only the table's Z (clamped to a small sink) so each rock sits centered + grounded.
    const FNodeShuffleVisual* Visual = FNodeShuffleNodeAssets::FindVisual(FName(*ResourceClass->GetName()));
    FVector CapturedScale = FVector::ZeroVector; // non-zero when a captured visual supplies the scale
    if (!Mesh)
    {
        // playtest-fixes-1: no authored entry — try a visual CAPTURED from this resource's own hidden
        // original (RP thorium / bamrenew lead ship vanilla-class nodes + modded descriptor, so their
        // real look only exists on the original's paired mesh actor). Falls through to quartz when
        // no capture exists (yet) — a later capture re-dresses via RedressSpawnedOfResource.
        if (const FNodeShuffleResolvedCapture* Cap = ResolveCapturedVisual(ResourceClass->GetName()))
        {
            if (UStaticMesh* CapMesh = Cap->Mesh.Get())
            {
                Mesh = CapMesh;
                Mats = &Cap->Materials;
                CapturedScale = Cap->Scale;
                bCaptured = true;
            }
        }
    }
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
    // Per-mesh scale: captured world scale for a captured visual (the original's authored scale);
    // else the table row; empirical 2.0 only if a mesh resolved with no table row.
    const FVector WantScale = bCaptured && !CapturedScale.IsNearlyZero() ? CapturedScale
                            : Visual ? Visual->MeshScale : FVector(2.0f, 2.0f, 2.0f);
    // Centered laterally; keep the table's Z (a vertical sink), clamped so we never sink the rock absurdly.
    const float TableZ = Visual ? Visual->MeshOffset.Z : -40.0f;
    // issue #1 (snap off-center): the donor mesh's geometric center is offset from its pivot (ROCK-CENTER showed
    // the quartz placeholder ~350uu laterally), so with the actor's random yaw the rock renders to the side of the
    // node. Counter the mesh's LOCAL bounds-center in XY (scaled) so the rock's visual center lands on the node
    // origin regardless of yaw (RockMesh's relative rotation is identity; actor yaw spins the centered rock about
    // the origin). Z keeps the authored sink so the rock stays PROMINENT and grounded (NOT buried — polish-3 tried
    // sinking it and the user rightly rejected that). NOTE: miners place at the node origin (vanilla snap), so a
    // prominent centered rock pokes up where the miner sits — rock-vs-miner fit is still an open visual question.
    const FVector MeshLocalCenter = Mesh->GetBoundingBox().GetCenter();

    // slopefit-1: the ROCK takes the FULL smoothed slope alignment as a RELATIVE rotation (the actor
    // itself is tilt-clamped for the hologram's sake), and steep slopes sink it further so the
    // downhill edge doesn't float. Underground entries skip this (cave floors are fill-checked flat).
    FQuat RockRelQuat = FQuat::Identity;
    float SlopeDeg = 0.0f;
    bool bUndergroundEntry = false;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (E.EntryGuid == EntryGuid) { bUndergroundEntry = E.bUnderground; break; }
    }
    if (!bUndergroundEntry)
    {
        FVector ProbeLoc;
        FRotator ProbeRot = FRotator::ZeroRotator;
        bool bProbeWater = false;
        FVector GroundN = FVector::UpVector;
        const FVector NodeLoc = Node->GetActorLocation();
        if (RaycastGroundAt(NodeLoc, NodeLoc.Z, Node, nullptr, ProbeLoc, ProbeRot, bProbeWater,
                            /*bShortTrace=*/false, /*bOutTooSteep=*/nullptr, &GroundN))
        {
            SlopeDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(GroundN.Z), -1.0f, 1.0f)));
            // Cold review (BLOCKER fix): the desired WORLD rotation must keep the actor's random YAW —
            // FullAlign alone has none, and ActorInv*FullAlign cancels the actor's yaw exactly, making
            // every rock face identically. Compose yaw under the slope tilt (same pattern as the
            // actor's own settle rotation in RaycastGroundAt).
            const FQuat FullAlign = FQuat::FindBetweenNormals(FVector::UpVector, GroundN);
            const FQuat YawQuat(FRotator(0.f, Node->GetActorRotation().Yaw, 0.f));
            RockRelQuat = Node->GetActorQuat().Inverse() * (FullAlign * YawQuat);
        }
    }

    // Centering counters the mesh pivot->center offset in the ROTATED frame; slope sink beds the rock.
    // Cold review #2: counter the FULL rotated offset incl. its Z-term — a tilted lateral offset
    // (quartz ~350 uu) otherwise raises/sinks the rock by sin(slope)*offset, dwarfing the sink budget.
    const FVector ScaledCenter(WantScale.X * MeshLocalCenter.X, WantScale.Y * MeshLocalCenter.Y, 0.0f);
    const FVector RotatedCenter = RockRelQuat.RotateVector(ScaledCenter);
    const float SlopeSink = FMath::GetMappedRangeValueClamped(
        FVector2D(10.0f, CliffSlopeDeg), FVector2D(0.0f, RockSlopeSinkMaxCm), SlopeDeg);
    const FVector RelOffset(
        -RotatedCenter.X,
        -RotatedCenter.Y,
        FMath::Clamp(TableZ, -120.0f, 60.0f) - SlopeSink - RotatedCenter.Z);

    // Build a plain material array for DressRock.
    TArray<UMaterialInterface*> MatPtrs;
    if (Mats)
    {
        for (const TWeakObjectPtr<UMaterialInterface>& M : *Mats) { MatPtrs.Add(M.Get()); }
    }

    if (Comp) { Comp->DressRock(Mesh, MatPtrs, WantScale, RelOffset, RockRelQuat.Rotator()); }
    else { Legacy->DressRock(Mesh, MatPtrs, WantScale, RelOffset, RockRelQuat.Rotator()); }

    UStaticMeshComponent* RockMC = Comp ? Comp->RockMesh : Legacy->RockMesh;
    // DIAGNOSTIC (issue #1 snap off-center): the miner snaps to the node ORIGIN / GetPlacementLocation,
    // but the rock renders at its mesh bounds center. Log both so we can measure the lateral offset (the
    // rock's pivot is not its visual center). Gated behind EnableDiagnostics.
    if (FNodeShuffleModule::AreDiagnosticsEnabled() && IsValid(RockMC))
    {
        const FVector NodeOrigin = Node->GetActorLocation();
        const FVector RockCenter = RockMC->Bounds.Origin;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("ROCK-CENTER node='%s' resource='%s' nodeOrigin=%s rockBoundsCenter=%s offset(center-origin)=%s"),
            *Node->GetName(), *ResourceClass->GetName(),
            *NodeOrigin.ToCompactString(), *RockCenter.ToCompactString(),
            *(RockCenter - NodeOrigin).ToCompactString());
    }

    if (bQuartz) { SpawnedRockQuartz++; } else { SpawnedRockVanilla++; }
    UE_LOG(LogNodeShuffle, Verbose,
        TEXT("dressed %s rock (node subobject) for %s: mesh '%s' scale=(%.2f,%.2f,%.2f) relZ=%.0f %d mat(s)"),
        bQuartz ? TEXT("QUARTZ-placeholder") : bCaptured ? TEXT("CAPTURED") : TEXT("vanilla"),
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
                Reuse->bUnderground = false; // cave-nodes-2: starter re-home is a surface spot near spawn
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
    // real-class redesign: relocated nodes are now spawned AS THEIR ORIGINAL class, so identity no longer
    // lives on the actor — it lives in the SaveGame Layout. On reload FG restores our runtime-spawned node
    // actors at their saved transforms; we match each back to its layout entry and repopulate SpawnedNodes
    // so EnsureNewNodeSpawned never re-spawns (which would move occupied nodes off their miners). Two passes:
    //   1. LEGACY old-save nodes: ANodeShuffleResourceNode carries a SaveGame EntryGuid — adopt by guid.
    //   2. REAL-class nodes: no on-actor identity — match restored RUNTIME nodes (!IsNetStartupActor, which
    //      excludes level-placed originals) to active new-node entries by LOCATION (+ resource as a guard).
    // Runs once per session.

    int32 Adopted = 0, LegacyByGuid = 0, RealByLocation = 0, PinnedOnLoad = 0, RuntimeNodesSeen = 0;
    const int32 ExpectedSpawned = static_cast<int32>(Algo::CountIf(Layout,
        [](const FNodeShuffleEntry& E){ return E.bIsNewNode; }));

    // Map EntryGuid -> layout index.
    TMap<FGuid, int32> EntryByGuid;
    for (int32 i = 0; i < Layout.Num(); i++)
    {
        if (Layout[i].bIsNewNode) { EntryByGuid.Add(Layout[i].EntryGuid, i); }
    }

    // Per-node finalize is FinalizeAdoptedNode() (a member function, so it has the subsystem's Friend access
    // to AFGResourceNode internals) — it resource-completes, re-asserts gates, re-registers, attaches our
    // component, and pins occupied entries; it returns true when it newly pinned an entry.

    // PASS 1: legacy old-save subclass nodes — adopt by SaveGame guid.
    for (TActorIterator<ANodeShuffleResourceNode> It(GetWorld()); It; ++It)
    {
        ANodeShuffleResourceNode* Node = *It;
        if (!IsValid(Node) || !Node->EntryGuid.IsValid()) { continue; }
        int32* Idx = EntryByGuid.Find(Node->EntryGuid);
        if (!Idx) { continue; }
        AFGResourceNode* const* Existing = SpawnedNodes.Find(Node->EntryGuid);
        if (!Existing || !IsValid(*Existing))
        {
            SpawnedNodes.Add(Node->EntryGuid, Node);
            Adopted++; LegacyByGuid++;
        }
        if (FinalizeAdoptedNode(Node, *Idx)) { PinnedOnLoad++; }
    }

    // PASS 2: real-class restored nodes — match by location to an active new-node entry not already adopted.
    constexpr float AdoptMatchRadius = 300.0f; // 3 m: restored transforms are exact; this just disambiguates
    const float MatchSq = FMath::Square(AdoptMatchRadius);
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        AFGResourceNode* Node = *It;
        if (!IsValid(Node)) { continue; }
        if (Node->IsA<ANodeShuffleResourceNode>()) { continue; }  // handled in pass 1
        if (Node->IsNetStartupActor()) { continue; }              // level-placed original, not ours
        if (UNodeShuffleNodeComponent::Find(Node)) { continue; }  // already ours this session
        RuntimeNodesSeen++;

        const FVector NodeLoc = Node->GetActorLocation();
        const FString NodeRes = Node->GetResourceClass() ? Node->GetResourceClass()->GetPathName() : FString();

        int32 BestIdx = INDEX_NONE;
        float BestSq = MatchSq;
        for (const TPair<FGuid, int32>& Pair : EntryByGuid)
        {
            const FNodeShuffleEntry& E = Layout[Pair.Value];
            if (!E.bActive) { continue; }
            if (SpawnedNodes.Contains(E.EntryGuid)) { continue; } // entry already has a live node
            const float DSq = FVector::DistSquared(NodeLoc, E.Location);
            if (DSq >= BestSq) { continue; }
            // Resource match (when both known) guards against adopting some OTHER mod's runtime node.
            if (!NodeRes.IsEmpty() && !E.AssignedResourceClassPath.IsEmpty()
                && NodeRes != E.AssignedResourceClassPath) { continue; }
            BestSq = DSq; BestIdx = Pair.Value;
        }
        if (BestIdx != INDEX_NONE)
        {
            SpawnedNodes.Add(Layout[BestIdx].EntryGuid, Node);
            Adopted++; RealByLocation++;
            if (FinalizeAdoptedNode(Node, BestIdx)) { PinnedOnLoad++; }
        }
    }

    const bool bFail = (Adopted <= 1 && ExpectedSpawned > 1 && RuntimeNodesSeen <= 1);
    UE_LOG(LogNodeShuffle, Display,
        TEXT("Adopt-on-load (real-class): adopted %d (%d legacy-by-guid, %d real-by-location); pinned %d occupied; %d candidate runtime nodes seen. [%d of %d layout spawn-entries streamed in — a count below total is normal.]%s"),
        Adopted, LegacyByGuid, RealByLocation, PinnedOnLoad, RuntimeNodesSeen,
        Adopted, ExpectedSpawned,
        bFail ? TEXT(" <-- WARNING: almost nothing adopted — spawned nodes may not be save-collected") : TEXT(" (OK)"));
}

bool ANodeShuffleSubsystem::FinalizeAdoptedNode(AFGResourceNode* Node, int32 EntryIdx)
{
    // Shared adopt finalize. A member function (not a lambda) so it has the subsystem's AccessTransformers
    // Friend access to AFGResourceNode internals. Resource-completes the node, re-asserts placement gates for
    // vanilla-origin nodes (modded keep native rules), re-registers it with the node manager, attaches our
    // runtime component (real nodes only — legacy carries its own subobjects), and pins occupied entries.
    // Returns true when it NEWLY pinned this entry as occupied (so the caller can count it).
    if (!IsValid(Node) || !Layout.IsValidIndex(EntryIdx))
    {
        return false;
    }
    FNodeShuffleEntry& E = Layout[EntryIdx];
    const bool bVanillaOrigin = E.NodeClassPath.StartsWith(TEXT("/Game/"));
    ANodeShuffleResourceNode* Legacy = Cast<ANodeShuffleResourceNode>(Node);

    // Restored nodes lost the (unserialized) interaction box — re-assert it (no-op for real native boxes).
    EnsureNodeUseBox(Node);

    // Vanilla-origin: re-assert placement gates so the Mk1 hologram snaps. Modded: keep native rules
    // (e.g. the Alkali node's mCanPlacePortableMiner=false that rejects normal miners).
    if (bVanillaOrigin)
    {
        Node->mCanPlaceResourceExtractor = true;
        Node->mCanPlacePortableMiner = true;
    }

    // Resource-complete on adopt: mAmount (not SaveGame) is missing after reload, so acceptance checks can
    // read HasAnyResources()=false. Re-run InitResource from the SaveGame override (mResourceClassOverride is
    // restored before BeginPlay) so the node is byte-for-byte complete like a fresh spawn.
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

    // redesign-11: the manager's mResourceNodes list is runtime — re-register so Mk1 can snap again.
    RegisterNodeWithManager(Node);

    // Re-attach our runtime component (lost on reload) so the node is recognised as ours and can host the
    // fallback visual. Legacy subclass nodes keep their own subobjects — no component needed.
    if (!Legacy)
    {
        const bool bForceAccept = (Node->GetResourceForm() != EResourceForm::RF_GAS);
        UNodeShuffleNodeComponent::Attach(Node, E.EntryGuid, bVanillaOrigin, bForceAccept);
    }

    // Occupancy pin: a node with a miner/extractor must never be relocated again. The durable pin is
    // E.bPinned (SaveGame Layout); legacy nodes also stamp their own saved bNodeShuffleOccupiedPinned.
    const bool bOccupied = IsNodeOccupiedAnyway(Node) || E.bPinned
        || (Legacy && Legacy->bNodeShuffleOccupiedPinned);
    bool bNewlyPinned = false;
    if (bOccupied)
    {
        if (Legacy) { Legacy->bNodeShuffleOccupiedPinned = true; }
        if (!E.bPinned)
        {
            E.bPinned = true;
            E.bRayCasted = true;
            E.Location = Node->GetActorLocation(); // lock the entry to the live node
            bNewlyPinned = true;
        }
    }
    return bNewlyPinned;
}

bool ANodeShuffleSubsystem::NodeHasOwnVisual(AActor* Node, UStaticMeshComponent* ExcludeRock) const
{
    // real-class redesign: does the relocated node render its OWN visual, so we should NOT add our fallback
    // rock? VISDIAG confirmed the user's modded nodes all self-render (AllMinable item-nodes carry their own
    // 'StaticMesh' with ResourceNode_quartz; the lithium Alkali node has its own mesh) -> true -> keep native,
    // no double visual. The fallback rock remains a safety net for any (hypothetical) modded node that has no
    // mesh of its own and no live linked mesh actor.
    if (!IsValid(Node))
    {
        return false;
    }
    TInlineComponentArray<UStaticMeshComponent*> Meshes(Node);
    for (UStaticMeshComponent* MC : Meshes)
    {
        if (!IsValid(MC) || MC == ExcludeRock) { continue; }
        if (MC->GetStaticMesh() != nullptr) { return true; } // a real self-rendering mesh of the node's own
    }
    if (AFGResourceNodeBase* Base = Cast<AFGResourceNodeBase>(Node))
    {
        if (AActor* MA = Base->mMeshActor.Get())
        {
            if (IsValid(MA) && !MA->IsHidden()) { return true; }
        }
    }
    return false;
}

bool ANodeShuffleSubsystem::ResourceHasAuthoredLook(UClass* ResourceClass)
{
    if (!ResourceClass) { return false; }
    const FString Short = ResourceClass->GetName();
    return FNodeShuffleNodeAssets::FindVisual(FName(*Short)) != nullptr
        || FindCapturedVisual(Short) != nullptr;
}

void ANodeShuffleSubsystem::HideNativeNodeMesh(AFGResourceNode* Node, UStaticMeshComponent* ExcludeRock)
{
    // visfix-1: hide the node's own self-rendered mesh(es) so the authored/captured rock we dress is
    // not double-rendered against them (mirrors NodeHasOwnVisual's search; our RockMesh excluded).
    // Idempotent; re-asserted on adopt each session (BP component visibility resets from the CDO).
    if (!IsValid(Node)) { return; }
    TInlineComponentArray<UStaticMeshComponent*> Meshes(Node);
    for (UStaticMeshComponent* MC : Meshes)
    {
        if (!IsValid(MC) || MC == ExcludeRock || MC->GetStaticMesh() == nullptr) { continue; }
        if (MC->IsVisible())
        {
            MC->SetVisibility(false, true);
            UE_LOG(LogNodeShuffle, Verbose, TEXT("visfix: hid native mesh '%s' on %s (assigned resource has an authored look)"),
                *MC->GetName(), *Node->GetName());
        }
    }
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
    // real-class redesign (THE MK-SNAP FIX for real nodes): a relocated node is now its ORIGINAL class, which
    // does NOT have the big "Resource" UseBox-root our legacy subclass carries. SNAPDIAG proved the symptom:
    // acceptance ALL passes (CanOccupy=1, IsAllowed=1, nodeIsA=1, hasAnyResources=1) yet TrySnapToActor->0 —
    // because the build-gun trace lands on our laterally-offset rock (NodeShuffleRockMesh_Rt) and the game's
    // snap can't resolve that off-center hit to the node without a large "Resource" collider (mBoxComponent)
    // covering it. The legacy node's 650cm UseBox does exactly that (and legacy snaps fine). So create/assert
    // OUR OWN 650cm "Resource" box at the node root and wire it as mBoxComponent — replicate the legacy
    // structure the snap resolves against. Friend access to mBoxComponent. Idempotent.
    ANodeShuffleResourceNode* OurNode = Cast<ANodeShuffleResourceNode>(Node);
    if (!OurNode)
    {
        AFGResourceNodeBase* Base = Cast<AFGResourceNodeBase>(Node);
        if (!Base) { return; }
        UBoxComponent* RtBox = nullptr;
        TInlineComponentArray<UBoxComponent*> Boxes(Node);
        for (UBoxComponent* B : Boxes)
        {
            if (IsValid(B) && B->GetFName() == FName(TEXT("NodeShuffleUseBox_Rt"))) { RtBox = B; break; }
        }
        if (!RtBox)
        {
            RtBox = NewObject<UBoxComponent>(Node, TEXT("NodeShuffleUseBox_Rt"));
            if (USceneComponent* Root = Node->GetRootComponent()) { RtBox->SetupAttachment(Root); }
            RtBox->RegisterComponent();
            if (USceneComponent* Root = Node->GetRootComponent())
            {
                RtBox->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
            }
        }
        if (RtBox->GetCollisionProfileName() != FName(TEXT("Resource"))) { RtBox->SetCollisionProfileName(TEXT("Resource")); }
        if (!RtBox->GetUnscaledBoxExtent().Equals(FVector(650.f, 650.f, 180.f), 1.0f)) { RtBox->SetBoxExtent(FVector(650.f, 650.f, 180.f)); }
        Base->mBoxComponent = RtBox; // the resource collider the snap resolves against
        return;
    }
    UBoxComponent* UseBox = OurNode->UseBox;

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
        // issue #2 (player bumps node / must jump): the box is Pawn=Ignore above, so it is NOT what blocks the
        // player. Probe the ROCK MESH's own collision — if its Pawn response is Block (or its profile is a
        // blocking one), the visible rock is the obstacle. This names the real culprit before we change it
        // (making the rock pawn-passthrough vs. standable-like-vanilla is a design call for the user).
        if (ANodeShuffleResourceNode* RockNode = Cast<ANodeShuffleResourceNode>(Node))
        {
            if (IsValid(RockNode->RockMesh))
            {
                UStaticMeshComponent* RM = RockNode->RockMesh;
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("USEBOX-RISE   rock profile='%s' collEnabled=%d scaledBoxExtent=%s | Pawn=%s Visibility=%s Camera=%s WorldStatic=%s WorldDynamic=%s"),
                    *RM->GetCollisionProfileName().ToString(), (int32)RM->GetCollisionEnabled(),
                    *RM->Bounds.BoxExtent.ToCompactString(),
                    RStr(RM->GetCollisionResponseToChannel(ECC_Pawn)),
                    RStr(RM->GetCollisionResponseToChannel(ECC_Visibility)),
                    RStr(RM->GetCollisionResponseToChannel(ECC_Camera)),
                    RStr(RM->GetCollisionResponseToChannel(ECC_WorldStatic)),
                    RStr(RM->GetCollisionResponseToChannel(ECC_WorldDynamic)));
            }
        }
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
    AFGResourceNode* OurNode = nullptr;
    for (const auto& Pair : SpawnedNodes)
    {
        if (IsValid(Pair.Value)) { OurNode = Pair.Value; break; } // any class — our spawned nodes are real classes now
    }
    if (!OurNode) { return; } // wait until at least one of ours is materialized

    // Find a streamed-in VANILLA node (a /Game/ AFGResourceNode that is NOT ours, plain Node type).
    AFGResourceNode* Vanilla = nullptr;
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        AFGResourceNode* N = *It;
        if (!IsValid(N) || NodeShuffleIsOurNode(N)) { continue; }
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
            if (IsValid(N) && NodeShuffleIsOurNode(N)) { OursInList++; }
        }
        int32 OursSpawned = 0;
        for (const auto& Pair : SpawnedNodes)
        {
            if (Pair.Value && NodeShuffleIsOurNode(Pair.Value)) { OursSpawned++; }
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

    AFGResourceNode* OurNode = nullptr;
    for (const auto& Pair : SpawnedNodes)
    {
        if (IsValid(Pair.Value)) { OurNode = Pair.Value; break; } // any class — our spawned nodes are real classes now
    }
    if (!OurNode) { return; } // wait until at least one of ours is materialized

    AFGResourceNode* Vanilla = nullptr;
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        AFGResourceNode* N = *It;
        if (!IsValid(N) || NodeShuffleIsOurNode(N)) { continue; }
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
    if (NodeShuffleIsOurNode(Node)) { return; } // never deregister OUR spawned nodes

    // scanner-2 (compass/map FIX): remove the node's ACTOR REPRESENTATION — the discovered-node icon on the
    // compass and map. Hiding the actor + clearing the scan + removing it from mResourceNodes does NOT remove
    // this representation, so a relocated/hidden original kept showing a phantom marker the player could chase
    // (user-reported limestone marker over empty ground). RemoveRepresentationOfActor works for any actor,
    // including Base-only esc_ nodes, so do it BEFORE the AFGResourceNode-only mResourceNodes removal below.
    if (AFGActorRepresentationManager* RepMgr = AFGActorRepresentationManager::Get(GetWorld()))
    {
        RepMgr->RemoveRepresentationOfActor(Node);
        // oil-rep FIX: a resource node's persistent compass/map marker is a UFGResourceNodeRepresentation that
        // RemoveRepresentationOfActor does NOT always clear — oil/liquid originals that got shuffled out kept
        // showing a phantom map/scanner marker with no node there to mine. Remove the node's representation
        // DIRECTLY by node (the proper API). Harmless for solids (already cleared); fixes the lingering oil marker.
        if (UFGResourceNodeRepresentation* NodeRep = RepMgr->FindResourceNodeRepresentation(Node))
        {
            RepMgr->RemoveRepresentation(NodeRep);
            if (FNodeShuffleModule::AreDiagnosticsEnabled())
            {
                UE_LOG(LogNodeShuffle, Display, TEXT("OIL-REP removed lingering node representation for '%s' form=%d loc=%s"),
                    *Node->GetName(), (int32)Node->GetResourceForm(), *Node->GetActorLocation().ToCompactString());
            }
        }
    }

    // mResourceNodes is the AFGResourceNode-only registry (the Mk1 snap list) — esc_ nodes aren't in it.
    // (The scanner's phantom-ping is NOT fixed here — the vanilla scanner ignores hidden/amount/collision;
    // it's suppressed at the cluster level via the AFGResourceScanner hook in NodeShuffle.cpp.)
    if (AFGResourceNode* AsNode = Cast<AFGResourceNode>(Node))
    {
        if (AFGResourceNodeManager* Mgr = GetNodeManager())
        {
            Mgr->mResourceNodes.RemoveSingleSwap(AsNode);
        }
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
        // real-class redesign: a restored node lost its runtime component — re-attach it. RockMesh's
        // mesh/materials are not SaveGame, so a vanilla node's FALLBACK rock is EMPTY after reload; re-dress
        // it once. Modded-origin nodes use their own native visual (re-trigger only if restored hidden).
        const bool bVanillaOrigin = Entry.NodeClassPath.StartsWith(TEXT("/Game/"));
        // Legacy old-save nodes (ANodeShuffleResourceNode) carry their OWN rock/decal subobjects — never
        // attach a component to them (it would add a SECOND, duplicate rock). Only real-class nodes get a
        // component (re-attached after reload).
        ANodeShuffleResourceNode* Legacy = Cast<ANodeShuffleResourceNode>(*Existing);
        UNodeShuffleNodeComponent* Comp = Legacy ? nullptr : UNodeShuffleNodeComponent::Find(*Existing);
        if (!Legacy && !Comp)
        {
            const bool bForceAccept = ((*Existing)->GetResourceForm() != EResourceForm::RF_GAS);
            Comp = UNodeShuffleNodeComponent::Attach(*Existing, Entry.EntryGuid, bVanillaOrigin, bForceAccept);
        }
        if (Comp) { Comp->EnsureAttachedToRoot(); }
        else if (Legacy) { Legacy->EnsureRockChildOfRoot(); }

        // slopefit-1 RETRO-FIT (once per node/session): nodes settled under the old FULL-tilt rule get
        // the new clamped-actor rotation, and the forced re-dress below gives their rock the full-slope
        // relative alignment. Rotation only — location untouched; occupied/pinned/underground skipped.
        bool bRotRefit = false;
        if (!Entry.bPinned && !Entry.bUnderground && !AdoptRotRefit.Contains(Entry.EntryGuid)
            && !IsNodeOccupiedAnyway(*Existing))
        {
            AdoptRotRefit.Add(Entry.EntryGuid);
            FVector RefitLoc;
            FRotator RefitRot = (*Existing)->GetActorRotation(); // yaw preserved through the align math
            bool bRefitWater = false;
            if (RaycastGroundAt(Entry.Location, Entry.Location.Z, *Existing, nullptr, RefitLoc, RefitRot,
                                bRefitWater))
            {
                if (!(*Existing)->GetActorRotation().Equals(RefitRot, 1.0f))
                {
                    (*Existing)->SetActorRotation(RefitRot);
                    Entry.Rotation = RefitRot;
                    bRotRefit = true;
                    UE_LOG(LogNodeShuffle, Verbose, TEXT("SLOPEFIT: refit rotation of %s at %s (actor tilt-clamped; rock re-dresses)"),
                        *Entry.EntryGuid.ToString(), *Entry.Location.ToCompactString());
                }
            }
        }

        UStaticMeshComponent* RockMC = Comp ? Comp->RockMesh : (Legacy ? Legacy->RockMesh : nullptr);
        UDecalComponent* DecalMC = Comp ? Comp->OilDecal : nullptr;
        UClass* RC = LoadClassByPath(Entry.AssignedResourceClassPath);
        const EResourceForm F = RC
            ? UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(RC))
            : EResourceForm::RF_SOLID;
        if (RC && F == EResourceForm::RF_LIQUID)
        {
            const bool bNeedsDecal = !IsValid(DecalMC) || DecalMC->GetDecalMaterial() == nullptr;
            if (bNeedsDecal)
            {
                RebuildNodeNativeVisual(*Existing);
                if (Comp) { Comp->DressOilDecal(RC); } // re-dress our oil decal on adopt
            }
        }
        else if (RC && bVanillaOrigin)
        {
            // slopefit-1: a rotation refit forces a re-dress so the rock's relative alignment updates.
            const bool bNeedsDress = bRotRefit || !IsValid(RockMC) || RockMC->GetStaticMesh() == nullptr;
            if (bNeedsDress)
            {
                SpawnVisualRockForNode(*Existing, RC, Entry.EntryGuid); // -> DressRock -> ForceVisible
            }
        }
        else if (RC)
        {
            // Modded-origin solid/gas: use the real class's native visual if it self-renders; otherwise dress
            // our fallback rock (AllMinable item-nodes have no node mesh). Rebuild native ONCE per node/session
            // (ProcessEvent is non-trivial — W2).
            if (!ModdedVisualRebuilt.Contains(Entry.EntryGuid))
            {
                RebuildNodeNativeVisual(*Existing);
                ModdedVisualRebuilt.Add(Entry.EntryGuid);
            }
            if (ResourceHasAuthoredLook(RC))
            {
                // visfix-1: assigned resource's look wins — hide the native mesh (re-asserted each
                // pass; BP component visibility resets from the CDO on reload) and dress if needed.
                HideNativeNodeMesh(*Existing, RockMC);
                const bool bNeedsDress = bRotRefit || !IsValid(RockMC) || RockMC->GetStaticMesh() == nullptr;
                if (bNeedsDress) { SpawnVisualRockForNode(*Existing, RC, Entry.EntryGuid); }
                if ((*Existing)->IsHidden()) { (*Existing)->SetActorHiddenInGame(false); }
            }
            else if (NodeHasOwnVisual(*Existing, RockMC))
            {
                if ((*Existing)->IsHidden()) { (*Existing)->SetActorHiddenInGame(false); }
            }
            else
            {
                const bool bNeedsDress = bRotRefit || !IsValid(RockMC) || RockMC->GetStaticMesh() == nullptr;
                if (bNeedsDress) { SpawnVisualRockForNode(*Existing, RC, Entry.EntryGuid); } // fallback rock
            }
        }
        // redesign-6 FIX 1: a restored node may be left actor-hidden after reload — force visible every pass
        // (idempotent), and log the runtime state for the first N adopted nodes. For modded-origin nodes the
        // owner un-hide already happened above; ForceVisible on the component is a cheap idempotent re-assert.
        if (Comp)
        {
            Comp->ForceVisible();
            if (RenderDiagAdoptLogged < RenderDiagMax)
            {
                Comp->LogRenderState(TEXT("adopt"));
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
        bool bWaterNoLand = false;
        bool bSettled = RaycastSettle(Entry, nullptr, nullptr, &bWaterNoLand);
        // playtest-fixes-1 / steepfix-1 (unplaceable redeal): a hit that is water OR too steep, with
        // no flat land within the 300 m spiral, is DEFINITIVE (the ground is there, it just can't
        // host a mineable node) — re-deal the entry instead of retrying the same spot every 5 s.
        if (!bSettled && bWaterNoLand)
        {
            WaterLockedThisSession.Add(Entry.EntryGuid);
            // fixes-3: the redeal MOVES the entry to a random deal-box spot (grid-filtered) but does
            // NOT settle it — it materializes via normal spawn-on-discovery at the new location.
            TryRedealWaterLockedEntry(Entry);
        }
        if (!bSettled)
        {
            // Cold review #1 safety net: an underground entry that keeps VOID-deferring (bad cell Z,
            // changed geometry) has no water signal to trigger a redeal and would retry silently
            // forever — a lost node. Count misses only while a player is CLOSE (cave collision
            // streamed, so a miss is meaningful); after 8, hand the entry back to the surface pool.
            if (Entry.bUnderground && !bWaterNoLand
                && IsLocationNearAnyPlayer(Entry.Location, CaveExpandNearPlayerCm))
            {
                int32& Misses = RedealAttempts.FindOrAdd(Entry.EntryGuid);
                Misses++;
                if (Misses >= 8)
                {
                    Entry.bUnderground = false;
                    RedealAttempts.Remove(Entry.EntryGuid);
                    UE_LOG(LogNodeShuffle, Display,
                        TEXT("CAVE-DEAL: entry %s (%s) escaped a bad cave cell after 8 settle misses — returned to the surface pool"),
                        *Entry.EntryGuid.ToString(), *Entry.AssignedResourceClassPath);
                }
            }
            DeferredThisPass++;
            // Defer-log backoff: full detail once per entry per session; repeats are silent (the
            // per-pass Deferral summary in ApplyLayout carries the ongoing count).
            if (!DeferLoggedThisSession.Contains(Entry.EntryGuid))
            {
                DeferLoggedThisSession.Add(Entry.EntryGuid);
                UE_LOG(LogNodeShuffle, Verbose,
                    TEXT("Spawn-on-discovery: deferred %s node at %s (no terrain / out of range; further retries silent)"),
                    *ResourceClass->GetName(), *Entry.Location.ToCompactString());
            }
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
            // Also reject spots occupied by player MACHINERY — a relocated node must NOT spawn on top of the
            // factory (user-reported: a node embedded in a machine). Foundations/ramps are EXEMPT (the user is
            // fine with a node sitting on a foundation). Physics overlap query (spatially indexed — only nearby
            // hits), filtered to AFGBuildable but NOT AFGBuildableFoundation (which covers foundations,
            // lightweight foundations, and ramps); terrain/landscape, vehicles, and resource nodes are ignored.
            {
                constexpr float BuildingRejectRadius = 600.0f;
                TArray<FOverlapResult> Hits;
                FCollisionObjectQueryParams ObjParams;
                ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
                ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);
                FCollisionQueryParams QParams(FName(TEXT("NodeShuffleBuildingOverlap")), false);
                if (GetWorld()->OverlapMultiByObjectType(Hits, At, FQuat::Identity, ObjParams,
                        FCollisionShape::MakeSphere(BuildingRejectRadius), QParams))
                {
                    for (const FOverlapResult& H : Hits)
                    {
                        AActor* HitActor = H.GetActor();
                        if (HitActor && HitActor->IsA<AFGBuildable>() && !HitActor->IsA<AFGBuildableFoundation>())
                        {
                            return true; // a machine/wall/belt — reject (foundations & ramps are allowed)
                        }
                    }
                }
            }
            return false;
        };

        // A spot is "enclosed" if boxed in by rock on nearly all sides at close range — e.g. the bottom of a
        // narrow vertical slot/crevice between rock columns: open ABOVE (the down-settle trace came through a
        // shaft) but unreachable horizontally. User-reported: a node "inside a column with no entrance". Real
        // caves and cliff-bases are NOT flagged (they're open on multiple sides). 8 horizontal rays at ~2 m
        // height; flag only when 7+ of 8 hit rock within 5 m (a true pocket, not a walkable cave/ravine).
        auto IsEnclosed = [&](const FVector& At) -> bool
        {
            const FVector Eye(At.X, At.Y, At.Z + 200.f);
            constexpr float Reach = 500.0f;
            constexpr int32 Dirs = 8;
            int32 Blocked = 0;
            for (int32 d = 0; d < Dirs; d++)
            {
                const float Ang = (2.0f * PI * d) / Dirs;
                const FVector To(Eye.X + Reach * FMath::Cos(Ang), Eye.Y + Reach * FMath::Sin(Ang), Eye.Z);
                FHitResult EncHit;
                FCollisionQueryParams EncParams(FName(TEXT("NodeShuffleEnclosure")), false);
                if (GetWorld()->LineTraceSingleByChannel(EncHit, Eye, To, ECC_WorldStatic, EncParams)) { ++Blocked; }
            }
            return Blocked >= 7;
        };

        const bool bSpotOccupied = OverlapsAt(Entry.Location);
        const bool bSpotEnclosed = !bSpotOccupied && IsEnclosed(Entry.Location);
        if (bSpotOccupied || bSpotEnclosed)
        {
            if (bSpotEnclosed && FNodeShuffleModule::AreDiagnosticsEnabled())
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("ENCLOSURE: %s node at %s is boxed in (narrow slot/column) — nudging to an open spot"),
                    *ResourceClass->GetName(), *Entry.Location.ToCompactString());
            }
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
                FVector TryLoc; FRotator TryRot = Entry.Rotation; bool bTryWater = false; bool bTrySteep = false;
                // cave-nodes-1: underground entries nudge with the SHORT trace so the probe stays on
                // the cavern floor instead of relocating the node onto the roof/surface above.
                // steepfix-1: nudge targets must also be flat enough to take a Miner.
                if (RaycastGroundAt(Probe, Entry.Location.Z, nullptr, nullptr, TryLoc, TryRot, bTryWater,
                                    Entry.bUnderground, &bTrySteep)
                    && !bTryWater && !bTrySteep && !OverlapsAt(TryLoc) && !IsEnclosed(TryLoc))
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
    // real-class redesign (SPAWN THE ORIGINAL CLASS). Relocate the node AS ITS ORIGINAL class — the vanilla
    // BP_ResourceNode*, or a modded node class like the Lithium/Alkali reactive-ore node — resolved from
    // Entry.NodeClassPath (loaded into NodeClass above), NOT a generic ANodeShuffleResourceNode. A modded
    // extractor casts/binds the node to its OWN class, so only the real class makes those succeed (the
    // exclude-gas-1 crash) and preserves the node's native accept rules + native visual. We still keep the
    // redesign-2 crash fix: the node is non-transient (save-collectable) and carries the SaveGame
    // mResourceClassOverride so GetResourceClass() is valid the instant a built miner's BeginPlay runs after
    // reload. Identity lives in the SaveGame Layout (FNodeShuffleEntry) + a runtime UNodeShuffleNodeComponent
    // attached below — never on the actor's class — so ANY node class can be one of ours. ANodeShuffleResource
    // Node remains only as a defensive fallback if the original class fails to resolve (and so old saves load).
    const bool bVanillaOrigin = Entry.NodeClassPath.StartsWith(TEXT("/Game/"));
    UClass* SpawnClass = NodeClass ? NodeClass : ANodeShuffleResourceNode::StaticClass();

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    // NOTE: intentionally NO RF_Transient — the node must be collected by the save system.
    AFGResourceNode* Node = GetWorld()->SpawnActor<AFGResourceNode>(
        SpawnClass, Entry.Location, Entry.Rotation, Params);
    if (!Node)
    {
        UE_LOG(LogNodeShuffle, Warning, TEXT("Failed to spawn new node (%s) at %s"),
            *SpawnClass->GetName(), *Entry.Location.ToCompactString());
        return;
    }
    // Resource + purity via the subsystem's Friend access to AFGResourceNode(Base) — works on ANY concrete
    // node class. mResourceClassOverride/mPurityOverride are the SaveGame fields the engine restores BEFORE
    // BeginPlay, so GetResourceClass()/rate are valid the instant a built miner restores -> no assert.
    Node->InitResource(ResourceClass, EResourceAmount::RA_Infinite, Entry.AssignedPurity.GetValue());
    Node->mResourceClassOverride = ResourceClass;
    Node->mPurityOverride = Entry.AssignedPurity;
    Node->OnRep_ResourceClassOverride(); // singleplayer-safe: refresh the live representation/visual
    // Placement gates: force-enable ONLY for vanilla-origin nodes, so the Mk1 hologram snaps to our runtime
    // node (a runtime vanilla node mirrors a level one). MODDED nodes keep their NATIVE flags — e.g. the
    // Alkali node sets mCanPlacePortableMiner=false to reject normal miners; overriding that was exactly the
    // exclude-gas-1 "lets me use a normal miner" bug, so we leave modded nodes' own rules intact.
    if (bVanillaOrigin)
    {
        Node->mCanPlaceResourceExtractor = true;
        Node->mCanPlacePortableMiner = true;
    }
    // Radiation is computed separately from the resource class (uranium etc.). Friend access.
    Node->InitRadioactivity();
    Node->UpdateRadioactivity();
    // Register this relocated node's scanner/map representation at the NEW spot so it pings the scanner there.
    Node->UpdateNodeRepresentation();
    UE_LOG(LogNodeShuffle, Verbose, TEXT("scanner: registered spawned node representation at %s (%s)"),
        *Entry.Location.ToCompactString(), *ResourceClass->GetName());

    // Legacy fallback nodes (pure-C++ subclass) need their "Resource" UseBox re-asserted; real node classes
    // have a native box (EnsureNodeUseBox is a no-op for them).
    EnsureNodeUseBox(Node);

    // redesign-11 (THE MK1 SNAP FIX): register the node into the resource-node MANAGER's mResourceNodes list
    // — the registry the Mk1 hologram queries. Runtime list (not save-persisted), re-asserted on adopt too.
    RegisterNodeWithManager(Node);

    SpawnedNodes.Add(Entry.EntryGuid, Node);

    // AFGResourceNode actors are LOGICAL and may be left actor-hidden (significance mgmt) — un-hide it.
    Node->SetActorHiddenInGame(false);

    // Attach our identity + fallback-visual component (idempotent). The durable identity is the SaveGame
    // Layout entry; this component is the runtime handle, re-stamped each session and re-attached on adopt.
    // Force-accept the Mk hologram for every relocated node EXCEPT special GAS nodes (lithium), which have
    // their own reactive extractor and must keep native accept rules so a normal miner is rejected. This
    // restores the old (unconditional) force-accept that made vanilla + AllMinable mineable; the earlier
    // mCanPlacePortableMiner discriminator wrongly excluded AllMinable (its nodes report false yet are meant
    // to be normal-mined — portable miners reach them via the dispenser path, not this flag).
    const bool bForceAccept = (Node->GetResourceForm() != EResourceForm::RF_GAS);
    UNodeShuffleNodeComponent* Comp = UNodeShuffleNodeComponent::Attach(Node, Entry.EntryGuid, bVanillaOrigin, bForceAccept);
    if (Comp) { Comp->EnsureAttachedToRoot(); }

    // VISUAL. Liquids (oil): native rebuild + OUR own decal (the engine leaves a runtime node's decal
    // invisible). Vanilla-origin solids: OUR fallback rock (a runtime vanilla node gets no engine mesh actor).
    // Modded-origin (solid OR gas): the REAL node class supplies its own visual — use it, NO quartz
    // placeholder. This is what finally gives lithium its real look instead of the quartz stand-in.
    const EResourceForm SpawnForm =
        UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(ResourceClass));
    if (SpawnForm == EResourceForm::RF_LIQUID)
    {
        RebuildNodeNativeVisual(Node);
        if (Comp) { Comp->DressOilDecal(ResourceClass); }
        SpawnedRockLiquid++;
        UE_LOG(LogNodeShuffle, Verbose, TEXT("spawned LIQUID node %s (oil decal)"), *ResourceClass->GetName());
    }
    else if (bVanillaOrigin)
    {
        SpawnVisualRockForNode(Node, ResourceClass, Entry.EntryGuid); // our fallback rock (-> DressRock -> ForceVisible)
    }
    else
    {
        // Modded-origin solid/gas. visfix-1: the ASSIGNED resource's look wins when we can render it
        // (authored table / captured) — a modded-class node dealt COAL must look like coal, not like
        // AllMinable's native quartz look-alike. Native visuals win only for resources we cannot
        // dress (lithium's Alkali node, uncaptured modded ores, esc_ item resources = dirty quartz).
        RebuildNodeNativeVisual(Node);
        if (ResourceHasAuthoredLook(ResourceClass))
        {
            HideNativeNodeMesh(Node, Comp ? Comp->RockMesh : nullptr);
            SpawnVisualRockForNode(Node, ResourceClass, Entry.EntryGuid);
            UE_LOG(LogNodeShuffle, Verbose, TEXT("spawned MODDED-CLASS node %s as real class %s (authored/captured look; native mesh hidden)"),
                *ResourceClass->GetName(), *SpawnClass->GetName());
        }
        else if (NodeHasOwnVisual(Node, Comp ? Comp->RockMesh : nullptr))
        {
            if (Comp) { Comp->ForceVisible(); }
            UE_LOG(LogNodeShuffle, Verbose, TEXT("spawned MODDED node %s as real class %s (native visual)"),
                *ResourceClass->GetName(), *SpawnClass->GetName());
        }
        else
        {
            SpawnVisualRockForNode(Node, ResourceClass, Entry.EntryGuid); // fallback rock (captured/quartz) -> visible
            UE_LOG(LogNodeShuffle, Verbose, TEXT("spawned MODDED node %s as real class %s (fallback rock — no native visual)"),
                *ResourceClass->GetName(), *SpawnClass->GetName());
        }
    }

    // RUNTIME-STATE DIAGNOSTIC: log the render-state for the first N spawned nodes.
    if (RenderDiagSpawnLogged < RenderDiagMax)
    {
        if (Comp) { Comp->LogRenderState(TEXT("spawn")); }
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
        bool bWaterNoLand = false;
        bool bSettled = Node && IsValid(*Node) && RaycastSettle(Entry, *Node, nullptr, &bWaterNoLand);
        if (!bSettled && bWaterNoLand)
        {
            // fixes-3: same unanchored redeal on the legacy settle-near-players path — the moved
            // entry settles (and its live actor relocates) when a player nears the NEW spot.
            WaterLockedThisSession.Add(Entry.EntryGuid);
            TryRedealWaterLockedEntry(Entry);
        }
        if (bSettled && Node && IsValid(*Node))
        {
            // redesign-5: the rock is a subobject of the node, so moving the node moves the rock too.
            (*Node)->SetActorLocationAndRotation(Entry.Location, Entry.Rotation);
            Entry.bRayCasted = true;
        }
        else if (Node && IsValid(*Node))
        {
            // cold-review nit: live-but-unsettled nodes on this legacy path were invisible to the
            // Deferral summary. Entries WITHOUT a live actor are counted by EnsureNewNodeSpawned
            // (which early-returns before its counter only when a live actor exists) — no double count.
            DeferredThisPass++;
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

    // The Players array is used ONLY by the stray-rock backstop below (rocks near a player) and the
    // no-player early-out — the main hide loop now acts on EVERY loaded original regardless of distance
    // (scanner-1 fix: world partition streams wider than 300 m, so far-but-loaded originals must hide too).
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
    constexpr float SuppressPlayerRange = 30000.0f; // 300 m: backstop's rock-near-player gate only
    constexpr float RockOwnRange = 800.0f;          // 8 m: a rock sits ON its node

    // redesign-1: every recorded original is an UNOCCUPIED node we want GONE (vanilla AND modded).
    // The only protection is live occupancy (a miner the player built) — checked per node.
    // DIAGNOSTIC FUNNEL (issue: originals not hiding on reload). Counts WHY each near record does/doesn't
    // hide, and — crucially — for path-lookup MISSES, whether a live node exists AT the record's LOCATION
    // (proximity-resolvable). If MissedByPath is high AND ProxResolvable ≈ MissedByPath, the bug is path
    // instability across save/reload (fix = match by location, not path). Gated behind EnableDiagnostics.
    const bool bDiagHide = FNodeShuffleModule::AreDiagnosticsEnabled();
    int32 DbgNear = 0, DbgFoundPath = 0, DbgAlreadyHidden = 0, DbgOcc = 0, DbgMissedPath = 0;

    int32 NodesHidden = 0;
    // Real locations of the originals we processed near the player this pass (resolved by path). The stray-
    // rock BACKSTOP below uses these instead of the stale Rec.Location (which is the relocated dest after a
    // re-roll) so a lingering separate rock next to a just-hidden node is still caught.
    TArray<FVector> NearOriginalLocs;
    NearOriginalLocs.Reserve(OriginalNodeRecord.Num());
    for (const FNodeShuffleSuppressedOriginal& Rec : OriginalNodeRecord)
    {
        // THE FIX (stale record location after re-roll). Resolve the live node BY PATH first, then test
        // proximity against its REAL location. On a re-roll the pool is rebuilt from the saved Layout,
        // whose entries store Location = the previous RELOCATED dest (the original's true spot was never
        // persisted), so Rec.Location is NOT where the original sits. The old proximity test on Rec.Location
        // therefore skipped records for nodes the player is standing right next to -> they never hid (the
        // whole "uncaptured nodes" saga was this: the nodes WERE captured, just never hidden). Path lookup
        // is an O(1)-ish cache hit, so resolving every record each pass is cheap. esc_ (Base-only) originals
        // resolve via the BASE finder too.
        AFGResourceNodeBase* Node = FindOriginalBaseByPath(Rec.VanillaNodePath);
        if (!Node) { if (bDiagHide) { DbgMissedPath++; } continue; } // not streamed in (or genuinely gone)
        // SCANNER FIX: hide EVERY loaded original, not just those within 300 m of a player. World partition
        // streams a region LARGER than the old 300 m hide gate, so a relocated original that was loaded but
        // >300 m away stayed visible AND registered with the resource scanner — the scanner pinged it, the
        // player walked toward it, and it vanished the instant they crossed 300 m (a real false lead the user
        // hit). Hiding ANY loaded (path-resolvable) original closes the gap: it's deregistered before a player
        // can scan-and-walk to it. Cheap — the hide is idempotent (skip-if-already-hidden) and the deregister
        // runs once per node (ScannerDeregistered), and only loaded actors ever reach this point.
        const FVector NodeLoc = Node->GetActorLocation();
        NearOriginalLocs.Add(NodeLoc); // hidden-original locations for the stray-rock backstop below
        if (bDiagHide) { DbgNear++; }

        // playtest-fixes-1 (modded-descriptor visuals): every resolved original — occupied ones too —
        // may donate its paired-mesh-actor visual for resources our table doesn't cover (RP thorium,
        // bamrenew lead). One-time per resource; all gates inside are cheap.
        CaptureOriginalVisualIfNeeded(Node);

        // cave-nodes-1: roof-classify each original once (persisted) — under-a-roof originals are the
        // proven-reachable seeds the cavern flood-fill grows from.
        ClassifyOriginalUnderground(Node, Rec.VanillaNodePath);

        // Hide the original node actor whole (this removes its rock, INCLUDING an instanced one). Never
        // touch an occupied node (a built miner). Occupancy checked on the Base + the Node-only portable check.
        {
            if (bDiagHide) { DbgFoundPath++; if (Node->IsHidden()) { DbgAlreadyHidden++; } }
            AFGResourceNode* AsNode = Cast<AFGResourceNode>(Node);
            const bool bOcc = Node->IsOccupied() || (AsNode && IsNodeOccupiedAnyway(AsNode));
            if (bDiagHide && bOcc) { DbgOcc++; }
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
                    // playtest-fixes-1 GHOST-RADIATION FIX: SetActorHiddenInGame does NOT unregister the
                    // node's radiation emitter (registered at BeginPlay with AFGRadioactivitySubsystem,
                    // keyed by owner object) — so every hidden thorium/uranium/pellet original kept
                    // radiating invisibly (user hit by radiation at an empty spot in the Dunes). Remove
                    // the hidden original's emitters; the re-roll restore re-runs InitRadioactivity.
                    // bHadEmitter (friend read of mSources) verifies in-log that the owner really is the
                    // node actor — if a radioactive original ever logs hadEmitter=0, that assumption broke.
                    if (AFGRadioactivitySubsystem* RadSub = GetRadSubsystem())
                    {
                        const bool bHadEmitter = RadSub->mSources.Contains(Node);
                        RadSub->RemoveEmitters(Node);
                        if (bHadEmitter)
                        {
                            RadEmittersRemoved++;
                            UE_LOG(LogNodeShuffle, Display,
                                TEXT("RADFIX: removed radiation emitter of hidden original %s at %s (running total %d)"),
                                *Rec.VanillaNodePath, *NodeLoc.ToCompactString(), RadEmittersRemoved);
                        }
                    }
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
            || (RockOwner && NodeShuffleIsOurNode(RockOwner)))
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
        for (const FVector& OrigLoc : NearOriginalLocs)
        {
            if (FVector::DistSquared2D(OrigLoc, Loc) < FMath::Square(RockOwnRange)) { bAtSuppressed = true; break; }
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
            TEXT("Hide originals: hid %d original nodes and %d stray original rocks; deregistered %d from scanner, removed %d radiation emitters (running totals) (Hide & Replace)"),
            NodesHidden, RocksHidden, ScannerDeregisterCount, RadEmittersRemoved);
    }
    if (bDiagHide && (DbgNear > 0 || DbgMissedPath > 0))
    {
        // Hide funnel: of all records — how many resolved to a LOADED actor (scanner-1: every loaded original
        // hides, any distance), of those how many were already hidden / occupied (skipped), and how many were
        // path-missed (record whose node isn't streamed in this pass). (See docs/DIAGNOSTICS.md.)
        UE_LOG(LogNodeShuffle, Display,
            TEXT("HIDEDIAG funnel: recordsTotal=%d loaded=%d foundByPath=%d alreadyHidden=%d occupied=%d pathMissed=%d"),
            OriginalNodeRecord.Num(), DbgNear, DbgFoundPath, DbgAlreadyHidden, DbgOcc, DbgMissedPath);
    }
}

int32 ANodeShuffleSubsystem::MergeWaterGridFromContent(const FString& Content, const TCHAR* SourceLabel,
                                                       bool bKeepExisting) const
{
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        UE_LOG(LogNodeShuffle, Warning, TEXT("WATERGRID: %s failed to parse; ignored."), SourceLabel);
        return -1;
    }
    int32 Added = 0;
    const auto LoadState = [&](const TCHAR* Field, uint8 State)
    {
        const TArray<TSharedPtr<FJsonValue>>* Cells = nullptr;
        if (!Root->TryGetArrayField(Field, Cells)) { return; }
        for (const TSharedPtr<FJsonValue>& V : *Cells)
        {
            FString S;
            if (!V->TryGetString(S)) { continue; }
            FString XStr, YStr;
            if (!S.Split(TEXT(","), &XStr, &YStr)) { continue; }
            const int64 Key = (static_cast<int64>(FCString::Atoi(*XStr)) << 32)
                            | (static_cast<int64>(FCString::Atoi(*YStr)) & 0xffffffffLL);
            if (bKeepExisting && WaterGrid.Contains(Key)) { continue; } // local knowledge wins
            WaterGrid.Add(Key, State);
            Added++;
        }
    };
    LoadState(TEXT("land"), 1);
    LoadState(TEXT("water"), 2);
    LoadState(TEXT("mixed"), 3);
    return Added;
}

int32 ANodeShuffleSubsystem::MergeWaterGridFromJson(const FString& Path, bool bKeepExisting) const
{
    FString Content;
    if (Path.IsEmpty() || !FPaths::FileExists(Path) || !FFileHelper::LoadFileToString(Content, *Path))
    {
        return -1; // absent
    }
    return MergeWaterGridFromContent(Content, *Path, bKeepExisting);
}

void ANodeShuffleSubsystem::EnsureWaterGridLoaded() const
{
    if (bWaterGridLoaded) { return; }
    bWaterGridLoaded = true;
    // 1. The user's LOCAL learned grid (authoritative).
    const FString LocalPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Configs"), TEXT("NodeShuffle_WaterGrid.json"));
    const int32 LocalCells = FMath::Max(0, MergeWaterGridFromJson(LocalPath, /*bKeepExisting=*/false));
    // 2. bakedmaps-2: the SHIPPED snapshot — EMBEDDED in the DLL (the packaging pipeline ships only
    //    Binaries + Paks; loose files never reach the zip, FilterPlugin.ini included — verified).
    //    Fresh installs start with the developer's discovered map; veterans gain only cells they
    //    lack. A merge dirties the store so the union persists into the local file once.
    const FString Baked = NodeShuffleBakedData::Assemble(
        NodeShuffleBakedData::WaterGridChunks, NodeShuffleBakedData::WaterGridChunkCount);
    const int32 BakedAdded = MergeWaterGridFromContent(Baked, TEXT("embedded baked water grid"), /*bKeepExisting=*/true);
    if (BakedAdded > 0) { bWaterGridDirty = true; }
    if (WaterGrid.Num() > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WATERGRID: %d cells ready (%d local + %d merged from the embedded baked map)"),
            WaterGrid.Num(), LocalCells, FMath::Max(0, BakedAdded));
    }
}

void ANodeShuffleSubsystem::FlushWaterGridIfDirty() const
{
    if (!bWaterGridDirty) { return; }
    TArray<TSharedPtr<FJsonValue>> Land, Water, Mixed;
    for (const auto& Pair : WaterGrid)
    {
        const FString Cell = NodeShuffleWaterCellString(Pair.Key);
        switch (Pair.Value)
        {
        case 1: Land.Add(MakeShared<FJsonValueString>(Cell)); break;
        case 2: Water.Add(MakeShared<FJsonValueString>(Cell)); break;
        default: Mixed.Add(MakeShared<FJsonValueString>(Cell)); break;
        }
    }
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("cellCm"), WaterGridCellCm);
    Root->SetArrayField(TEXT("land"), Land);
    Root->SetArrayField(TEXT("water"), Water);
    Root->SetArrayField(TEXT("mixed"), Mixed);
    FString Out;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    if (FJsonSerializer::Serialize(Root, Writer))
    {
        const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Configs"), TEXT("NodeShuffle_WaterGrid.json"));
        if (FFileHelper::SaveStringToFile(Out, *Path))
        {
            UE_LOG(LogNodeShuffle, Verbose, TEXT("WATERGRID: flushed %d cells to disk"), WaterGrid.Num());
            bWaterGridDirty = false; // success path ONLY (cold review: a failed write must keep the
            WaterGridNewSamples = 0; // batch dirty so the next trigger — save/quit/500 — retries it)
            return;
        }
    }
    UE_LOG(LogNodeShuffle, Warning,
        TEXT("WATERGRID: flush FAILED (Configs/ locked or unwritable?) — %d cells kept in memory, retrying on next trigger"),
        WaterGrid.Num());
    WaterGridNewSamples = 0; // back off the every-sample retry; dirty flag keeps save/quit retries armed
}

void ANodeShuffleSubsystem::RecordWaterGridSample(const FVector& Loc, bool bWater) const
{
    EnsureWaterGridLoaded();
    const int64 Key = NodeShuffleWaterCellKey(Loc);
    uint8& State = WaterGrid.FindOrAdd(Key, 0);
    const uint8 Sample = bWater ? 2 : 1;
    if (State == 0)
    {
        State = Sample;
        bWaterGridDirty = true;
        WaterGridNewSamples++;
    }
    else if (State != Sample && State != 3)
    {
        State = 3; // coastline cell — both land and water seen; never excluded
        bWaterGridDirty = true;
        WaterGridNewSamples++;
    }
    if (bWaterGridDirty && WaterGridNewSamples >= WaterGridFlushEvery)
    {
        FlushWaterGridIfDirty(); // safety flush; also flushed on save + EndPlay
    }
}

bool ANodeShuffleSubsystem::IsKnownWaterCell(const FVector& Loc) const
{
    EnsureWaterGridLoaded();
    const uint8* State = WaterGrid.Find(NodeShuffleWaterCellKey(Loc));
    return State && *State == 2;
}

void ANodeShuffleSubsystem::EnsureDealBoundsDerived() const
{
    // Pre-fixes-3 saves have no stored deal box: derive one from the Layout's own locations (they span
    // the map), same 2..98 percentile rule. Water-stuck outliers are exactly what the percentile trims.
    if (bDerivedBoundsReady) { return; }
    bDerivedBoundsReady = true;
    TArray<float> Xs, Ys;
    Xs.Reserve(Layout.Num());
    Ys.Reserve(Layout.Num());
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bIsNewNode) { continue; }
        Xs.Add(E.Location.X);
        Ys.Add(E.Location.Y);
    }
    if (Xs.Num() < 20) { return; } // degenerate layout — leave zero (redeal stays inert)
    Xs.Sort();
    Ys.Sort();
    const auto Percentile = [](const TArray<float>& Sorted, float P) -> float
    {
        const int32 Idx = FMath::Clamp(FMath::RoundToInt(P * (Sorted.Num() - 1)), 0, Sorted.Num() - 1);
        return Sorted[Idx];
    };
    DerivedBoundsMin = FVector(Percentile(Xs, 0.02f), Percentile(Ys, 0.02f), 0.f);
    DerivedBoundsMax = FVector(Percentile(Xs, 0.98f), Percentile(Ys, 0.98f), 0.f);
}

FVector ANodeShuffleSubsystem::GetDealBoundsMin() const
{
    if (!DealBoundsMin.IsNearlyZero() || !DealBoundsMax.IsNearlyZero()) { return DealBoundsMin; }
    EnsureDealBoundsDerived();
    return DerivedBoundsMin;
}

FVector ANodeShuffleSubsystem::GetDealBoundsMax() const
{
    if (!DealBoundsMin.IsNearlyZero() || !DealBoundsMax.IsNearlyZero()) { return DealBoundsMax; }
    EnsureDealBoundsDerived();
    return DerivedBoundsMax;
}

void ANodeShuffleSubsystem::PreSaveGame_Implementation(int32 saveVersion, int32 gameVersion)
{
    FlushWaterGridIfDirty(); // persist learned cells alongside every save
    FlushCaveStoreIfDirty();
}

int32 ANodeShuffleSubsystem::MergeCaveStoreFromContent(const FString& Content, const TCHAR* SourceLabel,
                                                       bool bKeepExisting, int32* OutFileSeedCount) const
{
    if (OutFileSeedCount) { *OutFileSeedCount = 0; }
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        UE_LOG(LogNodeShuffle, Warning, TEXT("CAVESTORE: %s failed to parse; ignored."), SourceLabel);
        return -1;
    }
    double SeedCountNum = 0.0;
    Root->TryGetNumberField(TEXT("seedCount"), SeedCountNum);
    if (OutFileSeedCount) { *OutFileSeedCount = FMath::Max(0, static_cast<int32>(SeedCountNum)); }
    const TArray<TSharedPtr<FJsonValue>>* Seeds = nullptr;
    if (Root->TryGetArrayField(TEXT("seeds"), Seeds))
    {
        for (const TSharedPtr<FJsonValue>& V : *Seeds)
        {
            FString S;
            if (V->TryGetString(S)) { CaveSeedsDone.Add(S); } // union — spares re-classification traces
        }
    }
    int32 Added = 0;
    const TArray<TSharedPtr<FJsonValue>>* Cells = nullptr;
    if (Root->TryGetArrayField(TEXT("cells"), Cells))
    {
        for (const TSharedPtr<FJsonValue>& V : *Cells)
        {
            FString S;
            if (!V->TryGetString(S)) { continue; }
            TArray<FString> Parts;
            S.ParseIntoArray(Parts, TEXT(","));
            if (Parts.Num() < 4) { continue; }
            const int64 Key = (static_cast<int64>(FCString::Atoi(*Parts[0])) << 32)
                            | (static_cast<int64>(FCString::Atoi(*Parts[1])) & 0xffffffffLL);
            if (bKeepExisting && CaveFloors.Contains(Key)) { continue; } // local knowledge wins
            FNodeShuffleCaveCell Cell;
            Cell.FloorZ = FCString::Atof(*Parts[2]);
            Cell.State = static_cast<uint8>(FCString::Atoi(*Parts[3]));
            // cave-nodes-2: optional 5th field = measured ceiling; absent (legacy/manual imports) = -1.
            Cell.CeilingCm = (Parts.Num() >= 5) ? FCString::Atof(*Parts[4]) : -1.0f;
            CaveFloors.Add(Key, Cell);
            Added++;
        }
    }
    return Added;
}

int32 ANodeShuffleSubsystem::MergeCaveStoreFromJson(const FString& Path, bool bKeepExisting, int32* OutFileSeedCount) const
{
    if (OutFileSeedCount) { *OutFileSeedCount = 0; }
    FString Content;
    if (Path.IsEmpty() || !FPaths::FileExists(Path) || !FFileHelper::LoadFileToString(Content, *Path))
    {
        return -1; // absent
    }
    return MergeCaveStoreFromContent(Content, *Path, bKeepExisting, OutFileSeedCount);
}

void ANodeShuffleSubsystem::EnsureCaveStoreLoaded() const
{
    if (bCaveStoreLoaded) { return; }
    bCaveStoreLoaded = true;
    // 1. The user's LOCAL learned store (authoritative).
    int32 LocalSeedCount = 0;
    const FString LocalPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Configs"), TEXT("NodeShuffle_CaveFloors.json"));
    const int32 LocalCells = FMath::Max(0, MergeCaveStoreFromJson(LocalPath, /*bKeepExisting=*/false, &LocalSeedCount));
    CaveSeedCount = LocalSeedCount;
    // 2. bakedmaps-2: the SHIPPED snapshot — EMBEDDED in the DLL (see EnsureWaterGridLoaded). Local
    //    wins per-cell; seed done-set unions; seed count takes the larger of the two (the map is
    //    static, so both counts describe the same world — max is the safe combination).
    int32 BakedSeedCount = 0;
    const FString Baked = NodeShuffleBakedData::Assemble(
        NodeShuffleBakedData::CaveFloorsChunks, NodeShuffleBakedData::CaveFloorsChunkCount);
    const int32 BakedAdded = MergeCaveStoreFromContent(Baked, TEXT("embedded baked cave atlas"),
                                                       /*bKeepExisting=*/true, &BakedSeedCount);
    CaveSeedCount = FMath::Max(CaveSeedCount, BakedSeedCount);
    if (BakedAdded > 0) { bCaveStoreDirty = true; }
    if (CaveFloors.Num() > 0 || CaveSeedsDone.Num() > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("CAVESTORE: %d cave-floor cells ready (%d local + %d merged from the embedded baked atlas); %d classified originals, %d roofed seeds"),
            CaveFloors.Num(), LocalCells, FMath::Max(0, BakedAdded), CaveSeedsDone.Num(), CaveSeedCount);
    }
}

void ANodeShuffleSubsystem::FlushCaveStoreIfDirty() const
{
    if (!bCaveStoreDirty) { return; }
    TArray<TSharedPtr<FJsonValue>> Seeds, Cells;
    for (const FString& S : CaveSeedsDone) { Seeds.Add(MakeShared<FJsonValueString>(S)); }
    for (const auto& Pair : CaveFloors)
    {
        const int32 CX = static_cast<int32>(Pair.Key >> 32);
        const int32 CY = static_cast<int32>(Pair.Key & 0xffffffffLL);
        Cells.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%d,%d,%.0f,%d,%.0f"),
            CX, CY, Pair.Value.FloorZ, static_cast<int32>(Pair.Value.State), Pair.Value.CeilingCm)));
    }
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("cellCm"), CaveCellCm);
    Root->SetNumberField(TEXT("seedCount"), CaveSeedCount);
    Root->SetArrayField(TEXT("seeds"), Seeds);
    Root->SetArrayField(TEXT("cells"), Cells);
    FString Out;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    if (FJsonSerializer::Serialize(Root, Writer))
    {
        const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Configs"), TEXT("NodeShuffle_CaveFloors.json"));
        if (FFileHelper::SaveStringToFile(Out, *Path))
        {
            UE_LOG(LogNodeShuffle, Verbose, TEXT("CAVESTORE: flushed %d cells / %d classified originals (%d roofed seeds) to disk"),
                CaveFloors.Num(), CaveSeedsDone.Num(), CaveSeedCount);
            bCaveStoreDirty = false;
            CaveStoreNewRecords = 0;
            return;
        }
    }
    UE_LOG(LogNodeShuffle, Warning,
        TEXT("CAVESTORE: flush FAILED (Configs/ locked or unwritable?) — kept in memory, retrying on next trigger"));
    CaveStoreNewRecords = 0; // back off the every-record retry; dirty flag keeps save/quit retries armed
}

void ANodeShuffleSubsystem::ClassifyOriginalUnderground(AFGResourceNodeBase* Node, const FString& Path)
{
    // cave-nodes-1: once per original (persisted done-set). An up-trace that hits within 150 m means
    // the node sits under a roof — a PROVEN-reachable cave spot (vanilla placed a node there), so it
    // seeds the flood-fill. The trace starts above headroom height so the node's own boulder or a low
    // outcrop doesn't count as a roof; its paired mesh actor is explicitly ignored. A natural-arch
    // false positive is acceptable — under an arch IS reachable.
    EnsureCaveStoreLoaded();
    if (CaveSeedsDone.Contains(Path)) { return; }
    CaveSeedsDone.Add(Path);
    bCaveStoreDirty = true;
    CaveStoreNewRecords++;
    if (!IsValid(Node) || !GetWorld()) { return; }
    const FVector Loc = Node->GetActorLocation();
    FCollisionQueryParams Params(FName(TEXT("NodeShuffleCaveRoof")), true);
    Params.AddIgnoredActor(Node);
    if (const AFGNodeMeshActor* MeshActor = FindMeshActorForNode(Node)) { Params.AddIgnoredActor(MeshActor); }
    FHitResult Hit;
    const FVector Up0(Loc.X, Loc.Y, Loc.Z + CaveHeadroomCm);
    const FVector Up1(Loc.X, Loc.Y, Loc.Z + CaveRoofProbeCm);
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Up0, Up1, ECC_WorldStatic, Params))
    {
        return; // open sky — a surface node
    }
    // Cold review #2: a player FOUNDATION/factory floor above the node is not a cave roof. Treat a
    // buildable hit as open sky (mirrors the water-grid teaching exclusion).
    if (const AActor* RoofActor = Hit.GetActor())
    {
        if (RoofActor->IsA<AFGBuildable>()) { return; }
    }
    CaveSeedCount++;
    const int64 Key = NodeShuffleGridKey(Loc, CaveCellCm);
    if (!CaveFloors.Contains(Key) && CaveFloors.Num() < CaveMaxCells)
    {
        // Cold review #1 (HIGH): the node sits ANYWHERE in its 8 m cell, but placement probes the
        // cell's geometric CENTER — a FloorZ sampled off-center can miss the settle window there and
        // soft-lock a cave-dealt entry. Re-sample the floor AT the center (same window the flood-fill
        // uses). Center hit -> self-consistent, placeable frontier cell. Center miss (ledge/boulder
        // between node and center) -> record as state 4: never placed on, never expanded past — the
        // seed still counts toward the quota and neighboring caves reach it from other seeds.
        FNodeShuffleCaveCell Cell;
        Cell.State = 4;
        Cell.FloorZ = static_cast<float>(Loc.Z);
        Cell.CeilingCm = static_cast<float>(Hit.ImpactPoint.Z - Loc.Z); // roof height at the node
        const float CenterX = (static_cast<int32>(Key >> 32) + 0.5f) * CaveCellCm;
        const float CenterY = (static_cast<int32>(Key & 0xffffffffLL) + 0.5f) * CaveCellCm;
        FHitResult CenterHit;
        if (GetWorld()->LineTraceSingleByChannel(CenterHit,
                FVector(CenterX, CenterY, Loc.Z + CaveHeadroomCm + CaveStepMaxCm),
                FVector(CenterX, CenterY, Loc.Z - 2.0f * CaveStepMaxCm),
                ECC_WorldStatic, Params))
        {
            Cell.FloorZ = static_cast<float>(CenterHit.ImpactPoint.Z);
            Cell.State = 1; // frontier — expansion walks the cavern from here
            // Cold review cave-nodes-2 #1: the ceiling gate must be measured where placement actually
            // happens — the cell CENTER — not at the node (a node-side boulder or open pocket up to
            // ~5.7 m away could otherwise mis-gate the Miner-fits check).
            FHitResult CenterRoof;
            Cell.CeilingCm = GetWorld()->LineTraceSingleByChannel(CenterRoof,
                    FVector(CenterX, CenterY, Cell.FloorZ + 50.0f),
                    FVector(CenterX, CenterY, Cell.FloorZ + CaveRoofProbeCm),
                    ECC_WorldStatic, Params)
                ? static_cast<float>(CenterRoof.ImpactPoint.Z) - Cell.FloorZ
                : CaveRoofProbeCm; // no roof straight up from the center = unbounded headroom
        }
        CaveFloors.Add(Key, Cell);
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("CAVEDISCOVER: seed at %s (%s) — roof %.0f m up; %d underground seeds known"),
        *Loc.ToCompactString(), *Path, (Hit.ImpactPoint.Z - Loc.Z) / 100.0f, CaveSeedCount);
}

void ANodeShuffleSubsystem::ExpandCaveFloorsBudgeted()
{
    // cave-nodes-1: budgeted per-pass flood-fill. Frontier cells near a player (streamed collision)
    // test their 4 neighbors with 3 traces each: connected floor (step <= 2.5 m), headroom (>= 3.5 m),
    // and roof (hit within 150 m => still inside; no hit => cave MOUTH — recorded walkable, never
    // expanded past, so the fill cannot leak onto the open surface). Wet cells are skipped.
    EnsureCaveStoreLoaded();
    if (CaveFloors.Num() == 0 || CaveFloors.Num() >= CaveMaxCells || !GetWorld()) { return; }
    TArray<FVector> Players;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (const APlayerController* Pc = It->Get())
        {
            if (const APawn* Pawn = Pc->GetPawn()) { Players.Add(Pawn->GetActorLocation()); }
        }
    }
    if (Players.Num() == 0) { return; }

    TArray<int64> Frontier;
    for (const auto& Pair : CaveFloors)
    {
        if (Pair.Value.State != 1) { continue; }
        const FVector Center = NodeShuffleCaveCellCenter(Pair.Key, Pair.Value.FloorZ);
        for (const FVector& P : Players)
        {
            if (FVector::DistSquared2D(P, Center) < FMath::Square(CaveExpandNearPlayerCm))
            {
                Frontier.Add(Pair.Key);
                break;
            }
        }
    }
    if (Frontier.Num() == 0) { return; }

    int32 TracesUsed = 0;
    int32 NewCells = 0;
    for (const int64 Key : Frontier)
    {
        if (TracesUsed >= CaveExpandTracesPerPass || CaveFloors.Num() >= CaveMaxCells) { break; }
        const FNodeShuffleCaveCell Cell = CaveFloors[Key];
        const int32 CX = static_cast<int32>(Key >> 32);
        const int32 CY = static_cast<int32>(Key & 0xffffffffLL);
        bool bAllNeighborsDone = true;
        static const int32 NX[4] = { 1, -1, 0, 0 };
        static const int32 NY[4] = { 0, 0, 1, -1 };
        for (int32 i = 0; i < 4; i++)
        {
            const int64 NKey = (static_cast<int64>(CX + NX[i]) << 32)
                             | (static_cast<int64>(CY + NY[i]) & 0xffffffffLL);
            if (CaveFloors.Contains(NKey)) { continue; }
            if (TracesUsed + 3 > CaveExpandTracesPerPass) { bAllNeighborsDone = false; break; }
            if (CaveFloors.Num() >= CaveMaxCells) { break; }
            const float NXc = (CX + NX[i] + 0.5f) * CaveCellCm;
            const float NYc = (CY + NY[i] + 0.5f) * CaveCellCm;
            FCollisionQueryParams QP(FName(TEXT("NodeShuffleCaveExpand")), true);
            // 1. connected floor
            FHitResult FloorHit;
            TracesUsed++;
            if (!GetWorld()->LineTraceSingleByChannel(FloorHit,
                    FVector(NXc, NYc, Cell.FloorZ + CaveHeadroomCm + CaveStepMaxCm),
                    FVector(NXc, NYc, Cell.FloorZ - 2.0f * CaveStepMaxCm),
                    ECC_WorldStatic, QP))
            {
                continue; // gap / ledge / unstreamed
            }
            const float FZ = static_cast<float>(FloorHit.ImpactPoint.Z);
            if (FMath::Abs(FZ - Cell.FloorZ) > CaveStepMaxCm) { continue; } // too steep to walk
            // 2. headroom
            FHitResult HeadHit;
            TracesUsed++;
            if (GetWorld()->LineTraceSingleByChannel(HeadHit,
                    FVector(NXc, NYc, FZ + 50.0f),
                    FVector(NXc, NYc, FZ + 50.0f + CaveHeadroomCm),
                    ECC_WorldStatic, QP))
            {
                continue; // ceiling too low here
            }
            // 3. dry
            if (IsPointInWater(FVector(NXc, NYc, FZ + 30.0f))) { continue; }
            // 4. roof (inside) vs sky (mouth)
            FHitResult RoofHit;
            TracesUsed++;
            const bool bRoofed = GetWorld()->LineTraceSingleByChannel(RoofHit,
                FVector(NXc, NYc, FZ + 50.0f + CaveHeadroomCm),
                FVector(NXc, NYc, FZ + CaveRoofProbeCm),
                ECC_WorldStatic, QP);
            FNodeShuffleCaveCell NewCell;
            NewCell.FloorZ = FZ;
            NewCell.State = bRoofed ? 1 : 4;
            // cave-nodes-2: ceiling clearance gates PLACEMENT (a Miner must fit); mouths count as tall.
            NewCell.CeilingCm = bRoofed ? static_cast<float>(RoofHit.ImpactPoint.Z) - FZ : CaveRoofProbeCm;
            CaveFloors.Add(NKey, NewCell);
            NewCells++;
            bCaveStoreDirty = true;
            CaveStoreNewRecords++;
        }
        if (bAllNeighborsDone)
        {
            CaveFloors[Key].State = 2; // fully expanded
            bCaveStoreDirty = true;
        }
    }
    if (NewCells > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("CAVEDISCOVER: mapped %d new cave-floor cells this pass (%d total, %d seeds)"),
            NewCells, CaveFloors.Num(), CaveSeedCount);
    }
    if (bCaveStoreDirty && CaveStoreNewRecords >= WaterGridFlushEvery)
    {
        FlushCaveStoreIfDirty();
    }
}

bool ANodeShuffleSubsystem::TryPickRawCaveCell(FRandomStream& Rng, FVector& OutLoc)
{
    // cave-nodes-2: a random PLACEABLE cell — fully roofed (state 1/2, never mouths) AND tall enough
    // for a Miner building (ceiling >= 12 m; -1 = legacy/manual seed, trusted as tall). NO spacing
    // here: the caller's own deal loop applies its avoid/spacing rules like for any candidate.
    EnsureCaveStoreLoaded();
    TArray<int64> Keys;
    Keys.Reserve(CaveFloors.Num());
    for (const auto& Pair : CaveFloors)
    {
        if ((Pair.Value.State == 1 || Pair.Value.State == 2)
            && (Pair.Value.CeilingCm < 0.0f || Pair.Value.CeilingCm >= CaveMinPlaceCeilingCm))
        {
            Keys.Add(Pair.Key);
        }
    }
    if (Keys.Num() == 0) { return false; }
    const int64 Key = Keys[Rng.RandRange(0, Keys.Num() - 1)];
    OutLoc = NodeShuffleCaveCellCenter(Key, CaveFloors[Key].FloorZ);
    return true;
}

bool ANodeShuffleSubsystem::TryPickCaveCell(FRandomStream& Rng, FVector& OutLoc)
{
    // Redeal variant: raw pick + MinNodeSpacing against the LIVE layout.
    for (int32 i = 0; i < 12; i++)
    {
        FVector Loc;
        if (!TryPickRawCaveCell(Rng, Loc)) { return false; }
        // 3D spacing on purpose: a cave cell sits UNDER the surface, so a surface node directly above
        // (2D-close but 25 m+ higher) must not veto it. Nodes inside the same cavern still space out.
        bool bTooClose = false;
        for (const FNodeShuffleEntry& Other : Layout)
        {
            if (FVector::DistSquared(Other.Location, Loc) < FMath::Square(MinNodeSpacing))
            {
                bTooClose = true;
                break;
            }
        }
        if (!bTooClose)
        {
            OutLoc = Loc;
            return true;
        }
    }
    return false;
}

void ANodeShuffleSubsystem::SeedCaveCellAtPlayer()
{
    // cave-nodes-1: manual seeding — the user tours roofed spots (bridges/shelves/tunnels) that have
    // no vanilla node to auto-seed them. Standing there IS the reachability proof.
    const UWorld* World = GetWorld();
    const APlayerController* Pc = World ? World->GetFirstPlayerController() : nullptr;
    const APawn* Pawn = Pc ? Pc->GetPawn() : nullptr;
    if (!Pawn)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("SEEDHERE: no player pawn"));
        return;
    }
    EnsureCaveStoreLoaded();
    const FVector P = Pawn->GetActorLocation();
    FCollisionQueryParams Params(FName(TEXT("NodeShuffleSeedHere")), true);
    Params.AddIgnoredActor(Pawn);
    FHitResult RoofHit;
    if (!GetWorld()->LineTraceSingleByChannel(RoofHit,
            P + FVector(0, 0, 250.0f), P + FVector(0, 0, CaveRoofProbeCm), ECC_WorldStatic, Params))
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("SEEDHERE: open sky above you — stand under the cave/bridge roof and re-run"));
        return;
    }
    if (const AActor* RoofActor = RoofHit.GetActor())
    {
        if (RoofActor->IsA<AFGBuildable>())
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("SEEDHERE: the roof above you is a player buildable (%s) — not a cave; no seed planted"),
                *RoofActor->GetClass()->GetName());
            return;
        }
    }
    const int64 Key = NodeShuffleGridKey(P, CaveCellCm);
    if (CaveFloors.Contains(Key))
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("SEEDHERE: this cell is already mapped (state %d) — nothing to do"),
            static_cast<int32>(CaveFloors[Key].State));
        return;
    }
    if (CaveFloors.Num() >= CaveMaxCells)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("SEEDHERE: cave store is at its %d-cell cap"), CaveMaxCells);
        return;
    }
    // Floor re-sampled at the CELL CENTER (same soft-lock guard as automatic seeds).
    FNodeShuffleCaveCell Cell;
    Cell.State = 4;
    Cell.FloorZ = static_cast<float>(P.Z);
    Cell.CeilingCm = static_cast<float>(RoofHit.ImpactPoint.Z - P.Z);
    const float CenterX = (static_cast<int32>(Key >> 32) + 0.5f) * CaveCellCm;
    const float CenterY = (static_cast<int32>(Key & 0xffffffffLL) + 0.5f) * CaveCellCm;
    FHitResult CenterHit;
    if (GetWorld()->LineTraceSingleByChannel(CenterHit,
            FVector(CenterX, CenterY, P.Z + CaveHeadroomCm + CaveStepMaxCm),
            FVector(CenterX, CenterY, P.Z - 2.0f * CaveStepMaxCm),
            ECC_WorldStatic, Params))
    {
        Cell.FloorZ = static_cast<float>(CenterHit.ImpactPoint.Z);
        Cell.State = 1; // frontier — the flood-fill walks the space from here
    }
    CaveFloors.Add(Key, Cell);
    CaveSeedCount++;
    bCaveStoreDirty = true;
    CaveStoreNewRecords++;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("CAVEDISCOVER: MANUAL seed at %s — roof %.0f m up, cell state %d; %d cave seeds known"),
        *P.ToCompactString(), (RoofHit.ImpactPoint.Z - P.Z) / 100.0f,
        static_cast<int32>(CaveFloors[Key].State), CaveSeedCount);
}

int32 ANodeShuffleSubsystem::CountUndergroundEntries() const
{
    int32 N = 0;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (E.bIsNewNode && E.bActive && E.bUnderground) { N++; }
    }
    return N;
}

// cave-nodes-2: CaveTopUpPass DELETED (user call — caves are additional random areas, not a quota to
// actively fill). Cave cells now enter the candidate space of every deal draw instead: see
// GenerateNewLocations (roll + relocation spots) and TryRedealWaterLockedEntry, each drawing a cave
// cell with natural probability CaveSeedCount/poolSize (capped CaveShareCap).

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
                                            bool& bOutWater, bool bShortTrace, bool* bOutTooSteep,
                                            FVector* OutGroundNormal) const
{
    bOutWater = false;
    if (bOutTooSteep) { *bOutTooSteep = false; }
    if (OutGroundNormal) { *OutGroundNormal = FVector::UpVector; }
    FCollisionQueryParams Params(SCENE_QUERY_STAT(NodeShuffleSettle), true);
    if (IgnoreNode) { Params.AddIgnoredActor(IgnoreNode); }
    if (IgnoreMesh) { Params.AddIgnoredActor(IgnoreMesh); }

    // cave-nodes-1: SHORT trace for underground entries — probe only a small window around the
    // recorded cave-floor Z. The standard 200 m top-down ray would hit the cave ROOF and settle the
    // node on the surface above the cavern.
    const FVector Start(ProbeXY.X, ProbeXY.Y, bShortTrace ? StartZ + 400.f : StartZ + 20000.f);
    const FVector End(ProbeXY.X, ProbeXY.Y, bShortTrace ? StartZ - 800.f : StartZ - 40000.f);
    FHitResult Hit;
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
    {
        return false; // no terrain / out of range (true void)
    }
    OutLoc = Hit.ImpactPoint;
    // slopefit-1: SMOOTH the ground normal over a small probe ring — one noisy collision triangle
    // made rocks sit "off the slant of the hillside". Long traces only (cave floors don't tilt).
    FVector GroundNormal = Hit.ImpactNormal;
    if (!bShortTrace)
    {
        FVector Acc = Hit.ImpactNormal;
        for (int32 i = 0; i < 4; i++)
        {
            const float Ang = PI * 0.5f * static_cast<float>(i);
            const float PX = OutLoc.X + SmoothNormalRingCm * FMath::Cos(Ang);
            const float PY = OutLoc.Y + SmoothNormalRingCm * FMath::Sin(Ang);
            FHitResult RingHit;
            if (GetWorld()->LineTraceSingleByChannel(RingHit,
                    FVector(PX, PY, OutLoc.Z + 400.0f), FVector(PX, PY, OutLoc.Z - 600.0f),
                    ECC_WorldStatic, Params))
            {
                Acc += RingHit.ImpactNormal;
            }
        }
        GroundNormal = Acc.GetSafeNormal(SMALL_NUMBER, FVector::UpVector);
    }
    if (OutGroundNormal) { *OutGroundNormal = GroundNormal; }
    // slopefit-1: CLIFF gate only (hills of any steepness settle — user call). Smoothed normal so a
    // single steep triangle can't reject a fair hillside.
    if (bOutTooSteep && !bShortTrace && GroundNormal.Z < MinSettleNormalZ)
    {
        *bOutTooSteep = true;
    }
    // slopefit-1: the ACTOR tilts only up to NodeTiltClampDeg toward the slope — the interaction box
    // and the Miner hologram inherit near-vanilla geometry; the rock visual takes the full tilt.
    FVector AlignN = GroundNormal;
    const float TiltRad = FMath::Acos(FMath::Clamp(static_cast<float>(GroundNormal.Z), -1.0f, 1.0f));
    if (TiltRad > FMath::DegreesToRadians(NodeTiltClampDeg))
    {
        const FVector Axis = FVector::CrossProduct(FVector::UpVector, GroundNormal)
            .GetSafeNormal(SMALL_NUMBER, FVector::XAxisVector);
        AlignN = FVector::UpVector.RotateAngleAxis(NodeTiltClampDeg, Axis);
    }
    const FQuat AlignQuat = FQuat::FindBetweenNormals(FVector::UpVector, AlignN);
    OutRot = (AlignQuat * FQuat(FRotator(0.f, OutRot.Yaw, 0.f))).Rotator();
    // Cliffs/steep normals are FINE (reachable by ladder/jetpack); only WATER is
    // rejected. Test the grounded impact point for water containment.
    // playtest-fixes-1 DEPTH GUARD: the volume test only sees STREAMED water volumes and the deep
    // ocean has none — seabed hits (measured shelf at Z≈-10,500) passed as dry. Any hit below the
    // floor counts as water regardless of volumes; legit land never sits that deep (see constant).
    // cave-nodes-1: short traces skip the deep-water floor reclassification — the cave cell was
    // depth/water-verified at discovery, and deep caves may legitimately sit below the ocean floor
    // constant. The volume containment test still applies (cave puddles/pools are real water).
    // Cold review #3: short traces also do NOT teach the surface water grid (a cave floor says
    // nothing about the surface column above it) — see the recording condition below.
    const bool bBelowDepthFloor = !bShortTrace && OutLoc.Z < DeepWaterFloorZ;
    bOutWater = bBelowDepthFloor || IsPointInWater(OutLoc);
    if (bBelowDepthFloor && FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        UE_LOG(LogNodeShuffle, Verbose, TEXT("DEPTHGUARD: hit at %s is below the deep-water floor (%.0f) — treated as water"),
            *OutLoc.ToCompactString(), DeepWaterFloorZ);
    }
    // playtest-fixes-3: every ground probe teaches the persistent water grid one cell — this is how
    // the roll/redeal "excluded areas" build themselves up across sessions. Cold review: skip hits on
    // player BUILDABLES (a foundation over open ocean would persist a bogus "land" cell into the
    // global cross-save grid) — only genuine terrain verdicts teach the grid. The settle itself still
    // accepts buildable hits (nodes on foundations are allowed by design); this gates learning only.
    const AActor* GridHitActor = Hit.GetActor();
    if (!bShortTrace && (!GridHitActor || !GridHitActor->IsA<AFGBuildable>()))
    {
        RecordWaterGridSample(OutLoc, bOutWater);
    }
    return true;
}

bool ANodeShuffleSubsystem::RaycastSettle(FNodeShuffleEntry& Entry, const AActor* IgnoreNode, const AActor* IgnoreMesh,
                                          bool* bOutWaterNoLand) const
{
    if (bOutWaterNoLand) { *bOutWaterNoLand = false; }
    FVector Loc;
    FRotator Rot = Entry.Rotation; // preserve yaw through the align math

    // cave-nodes-1: underground entries settle with the SHORT local trace only — no water spiral (a
    // cave pool has no "nearby land" in the surface sense). A water/void result reports water-no-land
    // so the caller's redeal machinery re-homes the entry (back to the surface pool).
    if (Entry.bUnderground)
    {
        bool bCaveWater = false;
        if (RaycastGroundAt(Entry.Location, Entry.Location.Z, IgnoreNode, IgnoreMesh, Loc, Rot, bCaveWater,
                            /*bShortTrace=*/true) && !bCaveWater)
        {
            Entry.Location = Loc;
            Entry.Rotation = Rot;
            return true;
        }
        if (bCaveWater && bOutWaterNoLand) { *bOutWaterNoLand = true; }
        return false; // void (unstreamed cave) -> normal defer; water -> caller redeals
    }

    bool bWater = false;
    bool bSteep = false;

    // 1. Primary probe at the entry's own XY. steepfix-1: a placeable spot is a hit that is neither
    //    water NOR steeper than the Miner hologram tolerates.
    if (RaycastGroundAt(Entry.Location, Entry.Location.Z, IgnoreNode, IgnoreMesh, Loc, Rot, bWater, false, &bSteep)
        && !bWater && !bSteep)
    {
        Entry.Location = Loc;
        Entry.Rotation = Rot;
        return true;
    }

    // Either a true void (no terrain — defer, terrain may not be streamed) or an UNPLACEABLE hit
    // (seafloor / too-steep hillside). Void defers immediately; unplaceable tries to RELOCATE onto
    // nearby flat land before giving up.
    const bool bPrimaryUnplaceable = bWater || bSteep; // only meaningful when a hit occurred

    // 2. FIX A relocation: nudge the candidate over an expanding spiral of offset
    //    points and re-raycast each; the FIRST placeable hit wins and is persisted to
    //    Entry.Location so the node stays put thereafter. If none is found within
    //    the cap, defer (leave as data) — never settle in water or on a cliffside.
    if (bPrimaryUnplaceable)
    {
        const FVector OriginXY = Entry.Location; // current (unplaceable) XY/Z probe base
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
            bool bTrySteep = false;
            if (RaycastGroundAt(Probe, OriginXY.Z, IgnoreNode, IgnoreMesh, TryLoc, TryRot, bTryWater, false, &bTrySteep)
                && !bTryWater && !bTrySteep)
            {
                UE_LOG(LogNodeShuffle, Verbose, TEXT("Land-check: relocated %s off %s to %s"),
                    *Entry.EntryGuid.ToString(), bWater ? TEXT("water") : TEXT("a cliff face"),
                    *TryLoc.ToCompactString());
                Entry.Location = TryLoc; // persist the corrected location (stable thereafter)
                Entry.Rotation = TryRot;
                return true;
            }
        }
        // playtest-fixes-1 / steepfix-1: definitive UNPLACEABLE spot (water or cliffside with no flat
        // land in the spiral) — report it so the caller redeals instead of retrying forever. Detail
        // log once per entry per session (was 22k lines).
        if (bOutWaterNoLand) { *bOutWaterNoLand = true; }
        if (!WaterDeferLoggedThisSession.Contains(Entry.EntryGuid))
        {
            WaterDeferLoggedThisSession.Add(Entry.EntryGuid);
            UE_LOG(LogNodeShuffle, Verbose, TEXT("Land-check: deferred %s (no placeable ground within %.0f m of the %s hit)"),
                *Entry.EntryGuid.ToString(), LandRelocationMaxRadiusCm / 100.0f,
                bWater ? TEXT("water") : TEXT("cliff"));
        }
    }

    // Void hit, or water with no nearby land: defer (leave entry as data, retry later).
    return false;
}

bool ANodeShuffleSubsystem::TryRedealWaterLockedEntry(FNodeShuffleEntry& Entry)
{
    // playtest-fixes-3 (UNANCHORED redeal): draw fresh RANDOM candidates from the SAME map-wide deal
    // box the roll used — the fixes-1 version anchored to streamed originals, which clustered
    // relocations around original node sites and defeated the mod's randomization (user call).
    // Candidates are filtered by the learned water grid + MinNodeSpacing only; the new spot settles
    // LAZILY via normal spawn-on-discovery (bRayCasted stays false). A grid-unknown spot may hit
    // water again — that settle teaches the grid one more cell and the entry hops once more; each
    // blind draw has the map's land share (~75%+) of succeeding, and the grid only improves.
    const FVector BoxMin = GetDealBoundsMin();
    const FVector BoxMax = GetDealBoundsMax();
    if (BoxMax.X <= BoxMin.X || BoxMax.Y <= BoxMin.Y)
    {
        return false; // no usable deal box (degenerate layout) — stay deferred
    }

    // Deterministic per-entry RNG, salted by attempt so each redeal draws different candidates.
    int32& Salt = RedealAttempts.FindOrAdd(Entry.EntryGuid);
    Salt++;
    FRandomStream Rng(static_cast<int32>(GetTypeHash(Entry.EntryGuid)) ^ (SavedSeed * 31) ^ (Salt * 7919));
    const float ProbeZ = (DealMeanZ != 0.0f) ? DealMeanZ : static_cast<float>(Entry.Location.Z);

    // cave-nodes-2: a cave-dealt entry whose cell turned out WET goes back to the surface pool. A
    // surface entry being re-homed rolls the same NATURAL cave share as any deal draw — caves are
    // additional random areas, never a quota (no priority, no fill target).
    const bool bWasUnderground = Entry.bUnderground;
    Entry.bUnderground = false;
    if (!bWasUnderground && Entry.ResourceForm != 2 /* liquid */) // cave-nodes-4: always on (drain fix)
    {
        EnsureCaveStoreLoaded();
        const float CaveChance = FMath::Min(CaveShareCap,
            static_cast<float>(CaveSeedCount) / FMath::Max(1, Layout.Num()));
        if (CaveChance > 0.0f && Rng.FRand() < CaveChance)
        {
            FVector CaveLoc;
            if (TryPickCaveCell(Rng, CaveLoc))
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WATER-REDEAL: entry %s (%s) re-dealt off water %s -> CAVE floor %s (natural share %.1f%%)"),
                    *Entry.EntryGuid.ToString(), *Entry.AssignedResourceClassPath,
                    *Entry.Location.ToCompactString(), *CaveLoc.ToCompactString(), CaveChance * 100.0f);
                Entry.Location = CaveLoc;
                Entry.Rotation = FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f);
                Entry.bUnderground = true;
                Entry.bRayCasted = false;
                return true;
            }
        }
    }

    for (int32 i = 0; i < RedealTries; i++)
    {
        const FVector Candidate(Rng.FRandRange(BoxMin.X, BoxMax.X),
                                Rng.FRandRange(BoxMin.Y, BoxMax.Y),
                                ProbeZ);
        // Excluded areas: cells the persistent grid has proven to be water.
        if (IsKnownWaterCell(Candidate)) { continue; }
        // Same spacing rule the roll enforces: MinNodeSpacing 2D from every other layout entry.
        // (Ground truth, overlap and enclosure are all handled by the normal settle at spawn time.)
        bool bTooClose = false;
        for (const FNodeShuffleEntry& Other : Layout)
        {
            if (&Other == &Entry) { continue; }
            if (FVector::DistSquared2D(Other.Location, Candidate) < FMath::Square(MinNodeSpacing))
            {
                bTooClose = true;
                break;
            }
        }
        if (bTooClose) { continue; }

        UE_LOG(LogNodeShuffle, Display,
            TEXT("WATER-REDEAL: entry %s (%s) re-dealt off water %s -> %s (random in deal box, grid-filtered; settles on discovery; attempt %d)"),
            *Entry.EntryGuid.ToString(), *Entry.AssignedResourceClassPath,
            *Entry.Location.ToCompactString(), *Candidate.ToCompactString(), Salt);
        Entry.Location = Candidate; // persisted (SaveGame)
        Entry.Rotation = FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f);
        Entry.bRayCasted = false;   // explicit: lazy settle at the new spot, like any dealt entry
        return true; // moved (NOT settled) — caller must still defer this tick
    }
    return false; // all candidates grid-water or too close — retry with fresh salt next pass
}

void ANodeShuffleSubsystem::LogHereCensus() const
{
    // playtest-fixes-1: `NodeShuffle.Here` — one command turns "something is odd at this spot" into a
    // log the whole team can act on: exact player position + every NodeShuffle-relevant thing nearby.
    const UWorld* World = GetWorld();
    const APlayerController* Pc = World ? World->GetFirstPlayerController() : nullptr;
    const APawn* Pawn = Pc ? Pc->GetPawn() : nullptr;
    if (!Pawn)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("HERE: no player pawn — census unavailable"));
        return;
    }
    const FVector P = Pawn->GetActorLocation();
    UE_LOG(LogNodeShuffle, Display,
        TEXT("HERE: player at X=%.0f Y=%.0f Z=%.0f | inWaterVolume=%d belowDepthFloor=%d | layout=%d spawn-entries, %d live spawned, %d water-locked this session"),
        P.X, P.Y, P.Z,
        IsPointInWater(P) ? 1 : 0, (P.Z < DeepWaterFloorZ) ? 1 : 0,
        Layout.Num(), SpawnedNodes.Num(), WaterLockedThisSession.Num());
    // playtest-fixes-3: learned water-grid knowledge (the roll/redeal excluded-areas store).
    EnsureWaterGridLoaded();
    int32 GridLand = 0, GridWater = 0, GridMixed = 0;
    for (const auto& Cell : WaterGrid)
    {
        switch (Cell.Value) { case 1: GridLand++; break; case 2: GridWater++; break; default: GridMixed++; break; }
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("HERE: water-grid knows %d cells (100 m): %d land / %d water / %d mixed (Configs/NodeShuffle_WaterGrid.json)"),
        WaterGrid.Num(), GridLand, GridWater, GridMixed);
    // cave-nodes-1: cavern knowledge + is-the-player-under-a-roof + ambient volumes here (the ambient
    // list evaluates whether the game's audio volumes could ever serve as an authored cave dataset).
    EnsureCaveStoreLoaded();
    int32 CaveFrontier = 0, CaveMouth = 0;
    for (const auto& Cell : CaveFloors)
    {
        if (Cell.Value.State == 1) { CaveFrontier++; }
        else if (Cell.Value.State == 4) { CaveMouth++; }
    }
    FHitResult RoofHit;
    FCollisionQueryParams RoofParams(FName(TEXT("NodeShuffleHereRoof")), true);
    const bool bPlayerRoofed = GetWorld()->LineTraceSingleByChannel(RoofHit,
        P + FVector(0, 0, 250.0f), P + FVector(0, 0, CaveRoofProbeCm), ECC_WorldStatic, RoofParams);
    FString AmbientNames;
    int32 AmbientCount = 0;
    for (TActorIterator<AFGAmbientVolume> It(GetWorld()); It; ++It)
    {
        if (IsValid(*It) && It->EncompassesPoint(P))
        {
            AmbientCount++;
            if (AmbientCount <= 3) { AmbientNames += It->GetName() + TEXT(" "); }
        }
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("HERE: cave-store %d cells (%d frontier, %d mouth), %d underground seeds, %d underground entries | roofAbovePlayer=%d%s | ambientVolumes(%d): %s"),
        CaveFloors.Num(), CaveFrontier, CaveMouth, CaveSeedCount, CountUndergroundEntries(),
        bPlayerRoofed ? 1 : 0,
        bPlayerRoofed ? *FString::Printf(TEXT(" (%.0fm up)"), (RoofHit.ImpactPoint.Z - P.Z) / 100.0f) : TEXT(""),
        AmbientCount, AmbientCount > 0 ? *AmbientNames : TEXT("<none>"));

    // slopefit-1 diagnostics: slope at the player's feet + the cliff-gate verdict — answers "would
    // nodes settle on this hillside?" in one line (user hit this exact question on a re-rolled hill).
    {
        FVector SlopeLoc;
        FRotator SlopeRot = FRotator::ZeroRotator;
        bool bSlopeWater = false;
        bool bSlopeCliff = false;
        FVector SlopeN = FVector::UpVector;
        if (RaycastGroundAt(P, P.Z, Pawn, nullptr, SlopeLoc, SlopeRot, bSlopeWater,
                            /*bShortTrace=*/false, &bSlopeCliff, &SlopeN))
        {
            const float SlopeHereDeg = FMath::RadiansToDegrees(
                FMath::Acos(FMath::Clamp(static_cast<float>(SlopeN.Z), -1.0f, 1.0f)));
            UE_LOG(LogNodeShuffle, Display,
                TEXT("HERE: ground slope at your feet = %.1f deg — cliff gate (%.0f deg) %s; water=%d"),
                SlopeHereDeg, CliffSlopeDeg,
                bSlopeCliff ? TEXT("WOULD REJECT settles here") : TEXT("accepts settles here"),
                bSlopeWater ? 1 : 0);
        }
    }

    auto ShortName = [](const FString& Path) -> FString
    {
        int32 Dot = INDEX_NONE;
        return Path.FindLastChar(TEXT('.'), Dot) ? Path.Mid(Dot + 1) : Path;
    };

    const float R2 = FMath::Square(static_cast<float>(CensusRadiusCm));
    int32 Shown = 0;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bIsNewNode) { continue; }
        const float D2 = FVector::DistSquared2D(E.Location, P);
        if (D2 > R2) { continue; }
        AFGResourceNode* const* Live = SpawnedNodes.Find(E.EntryGuid);
        const bool bLive = Live && IsValid(*Live);
        FString Flags;
        if (!E.bActive) { Flags += TEXT("inactive|"); }
        if (E.bPinned) { Flags += TEXT("pinned|"); }
        Flags += E.bRayCasted ? TEXT("settled|") : TEXT("unsettled|");
        Flags += bLive ? TEXT("LIVE") : TEXT("no-actor");
        if (WaterLockedThisSession.Contains(E.EntryGuid)) { Flags += TEXT("|WATER-LOCKED"); }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("HERE: entry %s dist=%.0fm dz=%+.0fm [%s] at %s"),
            *ShortName(E.AssignedResourceClassPath), FMath::Sqrt(D2) / 100.0f,
            (E.Location.Z - P.Z) / 100.0f, *Flags, *E.Location.ToCompactString());
        Shown++;
    }

    int32 OrigShown = 0;
    AFGRadioactivitySubsystem* RadSub = GetRadSubsystem();
    for (const auto& Pair : VanillaNodeCache)
    {
        AFGResourceNodeBase* Orig = Pair.Value.Get();
        if (!IsValid(Orig)) { continue; }
        // visfix-1: deposits (small one-off pickups) are never shuffled and flooded the census with
        // noise (40 lines, mostly deposits, in one report) — skip them.
        if (Cast<AFGResourceDeposit>(Orig)) { continue; }
        const FVector Loc = Orig->GetActorLocation();
        const float D2 = FVector::DistSquared2D(Loc, P);
        if (D2 > R2) { continue; }
        const AFGResourceNode* AsNode = Cast<AFGResourceNode>(Orig);
        const TSubclassOf<UFGResourceDescriptor> ResCls = AsNode ? AsNode->GetResourceClass() : nullptr;
        const float Decay = ResCls ? UFGItemDescriptor::GetRadioactiveDecay(ResCls) : 0.0f;
        // Friend read of mSources: is a radiation emitter still registered for this original?
        const bool bEmitter = RadSub && RadSub->mSources.Contains(Orig);
        UE_LOG(LogNodeShuffle, Display,
            TEXT("HERE: original %s dist=%.0fm dz=%+.0fm hidden=%d res=%s radioactiveDecay=%.3f emitterLive=%d"),
            *ShortName(Pair.Key), FMath::Sqrt(D2) / 100.0f, (Loc.Z - P.Z) / 100.0f,
            Orig->IsHidden() ? 1 : 0,
            ResCls ? *ResCls->GetName() : TEXT("<base-only>"), Decay, bEmitter ? 1 : 0);
        OrigShown++;
    }
    UE_LOG(LogNodeShuffle, Display, TEXT("HERE: census done — %d layout entries + %d streamed originals within %dm"),
        Shown, OrigShown, CensusRadiusCm / 100);
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

    // polish-9: portable-miner burial is fixed at the SOURCE — a hook on AFGPortableMinerDispenser::
    // Server_SpawnPortableMiner (in NodeShuffle.cpp) raises the spawn point onto the rock surface where the
    // player aimed, so each portable miner sits ON the rock (visible + interactable), any number of them, and
    // coexisting with a Mk1. No rock hiding here (a node is a SHARED marker — multiple portable miners + a Mk1
    // can all be on it), so the prominent solid rock simply stays.
}

void ANodeShuffleSubsystem::RefreshScannersAndRadarTowers()
{
    for (TActorIterator<AFGResourceScanner> It(GetWorld()); It; ++It)
    {
        // Friend access (AccessTransformers): force cluster rebuild on next use.
        It->mNodeClustersUpToDate = false;
    }
    // Radar towers cache the resources they scanned; without a re-scan they keep showing the OLD
    // (pre-shuffle) resource set on the map until rebuilt. Force every built radar tower to re-scan so
    // the map reflects the relocated/retyped/deactivated nodes. (This function previously only touched
    // resource scanners despite its name — a built radar tower stayed stale. Mirrors Resource Roulette.)
    // COST NOTE: ScanForResources walks the world's resource nodes AND pushes a representation update per
    // found node (network-replicated). This function is gated by bChangedWorld in ApplyLayout, so it only
    // runs on actual world mutations (rolls / nodes settling), never in steady state — keep it that way;
    // do NOT call this from a per-tick hot path.
    for (TActorIterator<AFGBuildableRadarTower> It(GetWorld()); It; ++It)
    {
        AFGBuildableRadarTower* Tower = *It;
        if (!IsValid(Tower)) { continue; }
        Tower->ClearScannedResources();
        Tower->ScanForResources();
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
    if (NodeShuffleIsOurNode(Node))
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

    // RESOURCE FORM GATE. SOLID is always allowed; LIQUID (oil) AND GAS (lithium / Desc_OreLithium) join when
    // non-solid shuffling is on. The real-class redesign relocates each node AS ITS ORIGINAL CLASS, so a gas
    // node comes back as its own modded node class (e.g. the Alkali reactive-ore node) — its special extractor
    // casts/binds succeed, it keeps its native accept rules (rejects a normal miner) and its real visual. That
    // is what makes gas safe to shuffle now (the three exclude-gas-1 breakages all came from spawning a generic
    // node in place of the modded class).
    // redesign-4 BUG 2 relaxation: a MODDED node whose descriptor reports RF_INVALID/unknown (common for
    // AllMinable's crafted-ITEM esc_ descriptors) is treated as SOLID so it still enters the pool — its mining
    // is driven by the node, not the descriptor form. RF_INVALID on a VANILLA node stays rejected as junk.
    const EResourceForm Form = Node->GetResourceForm();
    const bool bGasForm = (Form == EResourceForm::RF_GAS);
    const bool bLiquidForm = (Form == EResourceForm::RF_LIQUID);
    const bool bModdedUnknownForm = (!bVanillaNode) && (Form != EResourceForm::RF_LIQUID)
        && (Form != EResourceForm::RF_GAS); // modded solid OR modded RF_INVALID -> treat as solid
    const bool bFormAllowed = (Form == EResourceForm::RF_SOLID)
        || bModdedUnknownForm
        || (bIncludeLiquid && (bLiquidForm || bGasForm));
    if (!bFormAllowed)
    {
        OutReason = (bLiquidForm || bGasForm)
            ? TEXT("non-solid form (oil/gas) and non-solid shuffling is off")
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
        if (Smc->GetOwner() && NodeShuffleIsOurNode(Smc->GetOwner()))
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
        if (UNodeShuffleNodeComponent* Comp = UNodeShuffleNodeComponent::Find(Pair.Value))
        {
            if (IsValid(Comp->RockMesh)) { Claimed.Add(Comp->RockMesh); }
        }
        else if (ANodeShuffleResourceNode* OurNode = Cast<ANodeShuffleResourceNode>(Pair.Value))
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
