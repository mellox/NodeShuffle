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
#include "FGUnlockSubsystem.h" // knowledge-1: UnlockScannableResource + FScannableResourcePair (+ geyser descriptor transitively)
#include "UObject/UnrealType.h" // knowledge-2: FMapProperty/FSetProperty + script helpers (KAPI reflection)
#include "UObject/Package.h" // knowledge-3: CreatePackage — package identity for provisioned MinerInfo clones
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
    // ns-truth-diagnostics: DO NOT change this value, and do not restore the old comment.
    // Old comment: "world considered streamed in". That was a CLAIM, never a derivation -- unchanged
    // since a787752 (v1.0.0 initial release), with no measurement or design note behind the 50.
    // MEASURED 2026-08-08 (three independent boots, user logs): level-placed vanilla resource nodes are
    // NOT streamed at all. All 630 are live in a SINGLE frame at load (8-11 ms), spanning biomes tens of
    // km apart (all 8 Uranium, all 23 Bauxite, in the same frame). So this gate has never delayed
    // anything and there is nothing for it to wait for; raising it was explicitly REJECTED by
    // _team/nodeshuffle-followups/node-enumeration-investigation.md Q3 because it treats a non-problem
    // and would cement the false model. What the first roll genuinely misses is nodes RUNTIME-SPAWNED BY
    // OTHER MODS, which have been observed arriving minutes after boot -- a count gate cannot fix that;
    // only a re-scan can (that is a separate, user-gated packet).
    // WHAT THIS CONSTANT ACTUALLY IS: a cheap "a world exists and has resource nodes in it" sanity floor.
    constexpr int32 MinVanillaNodesForRoll = 50;
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
    // coexist-veto-1 FIX A: restored-node identity match radius, SHARED by AdoptRestoredSpawnedNodes
    // (pass 2) and the BeginPlay veto pre-registration pass so the two can never drift apart.
    // Restored transforms are exact; 3 m just disambiguates.
    constexpr float AdoptMatchRadiusCm = 300.0f;

    // rehide-1: an actor's leaf object name with a trailing auto-numbered "_<digits>" suffix stripped
    // — lead_C_2147470535 -> lead_C. GetPathName() for a level/runtime actor is
    // "Package.Level:PersistentLevel.ActorName", so the leaf is everything after the LAST '.'. Spawner
    // mods re-create their nodes with a fresh auto-number every process boot; this recovers the stable
    // class-name token used as TryRematchStaleRecord's legacy fallback (design §3.1 point 2) when no
    // layout entry resolves to give a real NodeClassPath.
    FString RehideClassNameToken(const FString& ObjectPath)
    {
        int32 Dot = INDEX_NONE;
        const FString Leaf = ObjectPath.FindLastChar(TEXT('.'), Dot) ? ObjectPath.Mid(Dot + 1) : ObjectPath;
        int32 Underscore = INDEX_NONE;
        if (Leaf.FindLastChar(TEXT('_'), Underscore) && Underscore + 1 < Leaf.Len())
        {
            const FString Suffix = Leaf.Mid(Underscore + 1);
            bool bAllDigits = Suffix.Len() > 0;
            for (const TCHAR C : Suffix) { if (!FChar::IsDigit(C)) { bAllDigits = false; break; } }
            if (bAllDigits) { return Leaf.Left(Underscore); }
        }
        return Leaf;
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
    // coexist-veto-1: fresh world, fresh managed-node registry (it is module-static, so it outlives
    // worlds; stale FObjectKeys from a previous world can never match a new actor, but clearing keeps
    // it tight). Then arm the optional KBFL destroyer veto for THIS world. Timing matters: actor
    // BeginPlay runs inside GameMode->StartPlay() (World.cpp:5931), which precedes the
    // OnWorldBeginPlay.Broadcast() (World.cpp:5938) where KBFL's world CDO subsystem (re)builds each
    // overwrite asset's requirement-instance list from mRequirements — so a class we prepend here is
    // picked up naturally for this world's destroy passes. Authority-only, like all subsystem logic:
    // the registry only fills on the authority side, so a client-side arm would veto nothing anyway.
    FNodeShuffleModule::ResetManagedNodes();
    PreRegisterRestoredNodesForVeto(); // FIX A: fill the registry BEFORE arming (first-load sweep gap)
    FNodeShuffleModule::ArmDestroyerVetoIfEnabled(GetWorld());
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
    // scanregen-1 (P2 §4 touch-point 5): per-pass debounce reset, unconditional and BEFORE any
    // early-out below (Config.Enabled, world-not-ready, ...) — every RefreshTick invocation gets
    // exactly one "has this pass already refreshed scanners" answer, even a pass that returns early.
    bScannerRefreshedThisPass = false;
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
    // Gated by the SHUFFLE-AWARE readiness check, not the strict pristine-vanilla one — a shuffled save never
    // passes the strict 50+ /Game/ count. (ns-truth-diagnostics: this used to read "the world must have streamed
    // in". It does not mean that and never tested it — see MinVanillaNodesForRoll.) Occupied/pinned nodes are carried; the new locations
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
        // knowledge-1 item 1: once per load (and once more after a re-roll — RollLayout re-arms the
        // flag), after the layout is applied and resource classes resolve: register managed MODDED
        // resources with the game's scanner-unlock list. Post-apply so a re-roll's fresh deal is
        // what gets registered. knowledge-2: the pass latches only when it COMPLETED — it returns
        // false when KAPI is present but its data-asset scan hasn't populated yet (retry next tick).
        if (!bKnowledgeUnlockDone && UnlockModdedScannerKnowledge())
        {
            bKnowledgeUnlockDone = true;
        }
        // Packet G (ns-automatch): once per load / re-roll, after the layout (and therefore the managed-
        // node census) has been applied -- see the member declaration comment in NodeShuffleSubsystem.h
        // for why this sits alongside bKnowledgeUnlockDone and mirrors its exact retry idiom.
        if (!bAutoAllowExtractorsDone)
        {
            // ns-h1b-notice: the pass fills PendingFromPass only on a completed pass that actually wrote
            // documents; every degraded path returns before writing, so there is nothing to filter here.
            // The RETURN VALUE and therefore the latch/retry behaviour is untouched by the notice.
            //
            // ns-review-notice2 F-A (BLOCKING FIX): the hand-off MUST NOT sit inside the success branch.
            // A bWriteFailed entry exists only when FailedCount > 0, which forces the pass to return
            // FALSE (a partial write must not latch -- it retries). So with the hand-off inside the
            // `if`, PendingFromPass died on the stack unread and the ENTIRE failure half of the notice
            // was unreachable: AV blocks 2 of 7 documents and the player was told nothing at all --
            // neither the "may stop working" warning for the 2, nor the restart notice for the 5 that
            // DID write. Silence is the pre-packet state this packet exists to end.
            //
            // AND WHY THE OBVIOUS VERSION OF THIS FIX LIVELOCKS -- do not "simplify" it back. Under a
            // PERMANENT write failure the pass never latches, so it runs every tick. An unconditional
            // `PendingNoticeGateTicks = -1` here would re-arm the spawn gate every tick, the emitter
            // would set it to 0 and return, the next tick would reset it to -1, and the message would
            // never post. So the gate is re-armed ONLY when the pending set is genuinely DIFFERENT from
            // what is already queued. Repeat-set spam across passes is already handled by
            // AnnouncedPendingKeys (NewKeyCount == 0 -> SUPPRESSED(duplicate)); this compare exists
            // solely to stop an unchanged set from resetting the gate underneath the emitter.
            //
            // The pass's return contract is deliberately NOT changed to latch on failure: a transient
            // AV lock would then never self-heal.
            TArray<FNodeShufflePendingEntry> PendingFromPass;
            const bool bPassDone = FNodeShuffleModule::RunAutoAllowExtractorsIfEnabled(GetWorld(), &PendingFromPass);
            if (bPassDone) { bAutoAllowExtractorsDone = true; }
            if (PendingFromPass.Num() > 0)
            {
                TArray<FString> NewKeys, OldKeys;
                FNodeShuffleModule::BuildPendingNoticeKeys(PendingFromPass, NewKeys);
                FNodeShuffleModule::BuildPendingNoticeKeys(PendingNoticeQueue, OldKeys);
                NewKeys.Sort();
                OldKeys.Sort();
                if (NewKeys != OldKeys)
                {
                    // REPLACE, never append: the newest pass is the authoritative pending state, and
                    // appending would let a stale entry from an earlier pass ride into the message.
                    PendingNoticeQueue = MoveTemp(PendingFromPass);
                    PendingNoticeGateTicks = -1; // re-arm the spawn gate for this genuinely new set
                }
            }
        }
        EmitPendingNoticeIfReady(Config.ShowCompatibilityNotices);
        // scanregen-1 consume point (P2 §4 touch-point 3, §9 AMENDMENT — binding): do NOT clear
        // bScannerClusterRefreshPending on the SKIPPED branch. The flag clears ONLY when
        // RefreshScannersAndRadarTowers() actually runs from HERE (it sets bScannerRefreshedThisPass
        // itself). Why: on a re-roll tick the flow is Refresh #1 (the live-reroll branch above) ->
        // ApplyLayout (possible Refresh #2 via its bChangedWorld tail) -> the knowledge pass HERE,
        // where the unlock lands and sets pending — both refreshes already ran BEFORE the unlock, and
        // radar towers re-scan eagerly, so skip-and-clear would leave the tower/map surface stale for
        // a resource newly unlocked by that same re-roll. With the amendment the refresh runs on the
        // next pass where nothing else already refreshed (typically +5s); worst case one redundant,
        // idempotent refresh during a world-settling storm.
        if (bScannerClusterRefreshPending && !bScannerRefreshedThisPass)
        {
            // P5: SCANREGEN line #2 (P2 design §5) — gated; RefreshScannersAndRadarTowers() below is
            // the behavior and is UNCHANGED, always runs regardless of this log.
            if (FNodeShuffleModule::AreDiagnosticsEnabled())
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("SCANREGEN: consuming refresh (pending=1, alreadyRefreshedThisPass=0) -> invalidating"));
            }
            RefreshScannersAndRadarTowers();
            bScannerClusterRefreshPending = false;
        }
        else if (bScannerClusterRefreshPending)
        {
            if (FNodeShuffleModule::AreDiagnosticsEnabled())
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("SCANREGEN: consuming refresh (pending=1, alreadyRefreshedThisPass=1) -> SKIPPED (already refreshed this pass)"));
            }
        }
        DiagnoseRocksNearPlayers();
    }
}

// ---------------------------------------------------------------- roll ----

bool ANodeShuffleSubsystem::IsWorldReadyForRoll() const
{
    // Solid-only readiness check (no liquid): a cheap floor proving a populated world exists before the
    // one-time roll fires. Solid nodes are the overwhelming majority, so they are the cheapest probe.
    // ns-truth-diagnostics: this comment used to say "just needs proof the world has streamed in". It
    // does not prove that and never tried to — see MinVanillaNodesForRoll for the measurement that
    // falsified the streaming model. Do not raise the threshold; it would treat a non-problem.
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
    // Gate instead on enough LOADED resource nodes of ANY kind, after a short post-load settle.
    // (ns-truth-diagnostics: the settle is a cheap "let the world finish coming up" delay, not a
    // streaming wait — see MinVanillaNodesForRoll.)
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
    // coexist-1: the steady-hidden markers describe the OLD roll's hide state — drop them so the new
    // roll's funnel re-processes every record from scratch (same lifecycle as ScannerDeregistered).
    SteadyHiddenOriginals.Empty();
    // dirtdress-1 (cold review): same lifecycle for the capture-chain diag dedupe — the re-rolled
    // population re-runs capture from scratch, so it should re-emit its CAPTURE-CHAIN breadcrumbs.
    CaptureChainLogged.Empty();
    // rehide-1 (cold review): same lifecycle for the no-match REHIDE throttle — rebuilt records keep
    // their old path keys, so without this a still-stale record stays log-silent after a re-roll.
    RematchNoMatchLogged.Empty();

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

    // knowledge-2 EARLY PASS (crash-window closer): a SAVED Modular Miner on a provided ore
    // BeginPlays during world init — ~70 s BEFORE the first RefreshTick — and KLib's fgcheckf needs
    // its mMinerMapping entry to exist by then. PostLoadGame runs in the load flow with the Layout
    // already deserialized and KAPI's game-launch scan long done, so provisioning here lands before
    // gameplay BeginPlay ordering can bite. The function self-defers (returns false) when the
    // unlock subsystem or KAPI's map isn't ready yet — the RefreshTick pass retries and completes
    // the scanner-unlock half; provisioning itself is idempotent either way. Residual (stated
    // honestly): if an engine change ever BeginPlays restored buildables before save-interface
    // PostLoadGame, the window reopens — nothing mod-side can order around that.
    if (bLayoutGenerated && UnlockModdedScannerKnowledge())
    {
        bKnowledgeUnlockDone = true;
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
            // coexist-veto-1 FIX 1 (narrowed, review #2): trust bPinned ONLY when there is no live actor
            // to consult — a destroyed/tombstoned pinned entry is carried so a re-roll never orphans the
            // player's miner; with a live actor, LIVE occupancy decides — bPinned is never cleared, so a
            // pinned node whose miner was dismantled must free back into the pool as players expect.
            const bool bLiveValid = Live && IsValid(*Live);
            if ((Old.bPinned && !bLiveValid) || (bLiveValid && IsNodeOccupiedAnyway(*Live)))
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
    // CRITICAL (re-roll determinism): a live TActorIterator scan on a RE-ROLL sees whatever population
    // happens to exist at that instant, which is NOT the population the initial roll saw — our own
    // previous spawns have replaced originals, other mods' nodes may have arrived since, and the result
    // would be a different, broken balance each time (a resource could get ZERO active nodes despite its
    // floor). The saved Layout holds the COMPLETE original vanilla pool (resource/purity/location/path
    // were persisted at the initial roll), so on reroll we rebuild from it directly and balance becomes
    // identical every time.
    //
    // The INITIAL roll has no Layout yet, so it keeps the live scan.
    // ns-truth-diagnostics: this block used to justify itself with "the world may be only partially
    // streamed in" and "IsWorldReadyForRoll already gated full streaming". BOTH ARE FALSE. Measured
    // 2026-08-08: level-placed vanilla nodes are all live in one frame at load (see
    // MinVanillaNodesForRoll), and IsWorldReadyForRoll gates nothing of the kind. The rebuild-from-Layout
    // decision is still right, for the reason restated above — determinism against a CHANGING live
    // population — but it was never right for the reason previously written here.
    TArray<FVector> VanillaLocations;
    TMap<FString, int32> VanillaResourceCounts;     // descriptor path -> count
    TMap<FString, uint8> FormByResource;            // descriptor path -> EResourceForm value
    TArray<TEnumAsByte<EResourcePurity>> PurityDeckSource;
    FString SpawnableNodeClassPath;                 // a solid-node BP class; first-seen-ANY (pre-fix fallback tier)
    // Phase 2: the BP class of an actual liquid (oil) node, so spawned oil nodes
    // are the right node type for oil extractors. Empty when no oil node exists or
    // experimental features are off; liquid entries then fall back to the solid
    // class (still form-correct via the oil descriptor).
    FString SpawnableLiquidNodeClassPath;
    // FU1-v2 (D4-A, followups-plan-2 Packet B): companion vanilla-preferred tier for each
    // Spawnable*NodeClassPath above. New-location entries (bIsNewNode, no original) stamp their
    // NodeClassPath from these Spawnable* variables (:1071, :1356-1362, :1441) -- pre-fix, that was
    // whichever solid/liquid class happened to be scanned FIRST, vanilla or modded (the FicsitFarming
    // BP_DirtNode_C-on-lead contamination). Track the first /Game/ candidate per form separately;
    // PickRepresentative below then prefers it, falling back to the any-tier so a pool with no /Game/
    // candidate still latches something -- see the rep-class census log just before first use.
    FString SpawnableVanillaNodeClassPath;
    FString SpawnableVanillaLiquidNodeClassPath;
    // Shared two-tier latch, called from every capture site below (reroll rebuild, liquid/gas
    // augment, full re-scan augment, initial live scan) AFTER each site's own SpawnRefusedClassSubstitute
    // exclusion (P3/deckevict-1 -- kept at each call site, not folded in here, so that guard stays
    // visibly in force at every latch). AnyLatch keeps the exact pre-fix first-seen semantics.
    const auto LatchRepresentative = [](const FString& CandidatePath, FString& AnyLatch, FString& VanillaLatch)
    {
        if (AnyLatch.IsEmpty()) { AnyLatch = CandidatePath; }
        if (VanillaLatch.IsEmpty() && CandidatePath.StartsWith(TEXT("/Game/"))) { VanillaLatch = CandidatePath; }
    };
    // Resolves a (vanilla, any) pair to the single value consumers read: vanilla wins when present,
    // else the any-tier -- so the two-tier fallback can only ADD a preferred pick, never regress to
    // empty where the pre-fix any-only code found a class (design §8 invariant).
    const auto PickRepresentative = [](const FString& VanillaFirst, const FString& AnyFirst) -> FString
    {
        return !VanillaFirst.IsEmpty() ? VanillaFirst : AnyFirst;
    };
    // repclass-2 (PacketD): the form tiers above key a representative class by resource FORM, which
    // conflates unrelated resources that happen to share a form (solid coal and solid lithium/alkali
    // are both FormSolid) -- whichever is scanned first wins the class for BOTH, so a new-location
    // node stamped for one resource can come out as the other's class (the AlkaLib Reactive Ore
    // Extractor Mk.2 regression: a coal node stamped with the lithium/alkali node class). Key the
    // representative by the actual RESOURCE instead so each resource gets ITS OWN observed class --
    // this is populated at the same four sites that already call LatchRepresentative above (each one
    // has both the resource path and the node class in scope), guarded by the same P3
    // (SpawnRefusedClassSubstitute) exclusion. The form tiers (Spawnable*NodeClassPath) remain
    // untouched as the fallback for a resource this session never actually observed on a real node.
    TMap<FString, FString> NodeClassByResource;      // resource descriptor path -> node class path
    TSet<FString> NodeClassByResourceConflictLogged; // diagnostics-only: resources already logged for a class conflict
    const auto LatchByResource = [](const FString& ResourcePath, const FString& ClassPath,
        TMap<FString, FString>& Map, TSet<FString>& ConflictLogged)
    {
        if (ResourcePath.IsEmpty() || ClassPath.IsEmpty()) { return; }
        if (const FString* Existing = Map.Find(ResourcePath))
        {
            // Same resource, two different observed node classes across this roll's capture sites.
            // Keep the FIRST (matches LatchRepresentative's first-seen semantics) -- genuinely
            // interesting, not noise, so log it once per resource (diagnostics-gated; the map write
            // itself is unconditional behavior above/below this branch).
            if (*Existing != ClassPath && FNodeShuffleModule::AreDiagnosticsEnabled() && !ConflictLogged.Contains(ResourcePath))
            {
                ConflictLogged.Add(ResourcePath);
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("rep-class byResource CONFLICT: resource='%s' second class='%s' seen -- keeping first '%s'"),
                    *ResourcePath, *ClassPath, **Existing);
            }
            return;
        }
        Map.Add(ResourcePath, ClassPath);
    };
    int32 VanillaCount = 0;
    int32 ZombiesDropped = 0; // rehide-1: pre-fix un-anchorable runtime-foreign records dropped at rebuild (§5)

    TArray<FNodeShuffleEntry> NewLayout;

    if (bIsReroll)
    {
        // redesign-6 FIX 3 (REROLL COLLAPSE). In the Hide & Replace model every UNOCCUPIED original was
        // converted at the initial roll into a bIsNewNode SPAWNED entry. PRE-FIX-3, that conversion
        // EMPTIED its VanillaNodePath — only OCCUPIED/pinned originals remained as !bIsNewNode entries,
        // so the OLD reroll loop, which rebuilt from !bIsNewNode entries ONLY, saw just the few pinned
        // ones and COLLAPSED the pool (732 -> 349). P5 (addenda item 4): the emptying described above is
        // PRE-FIX-3 history, not current behavior — the conversion below (see "redesign-6 FIX 3: KEEP
        // VanillaNodePath") instead KEEPS the path, and P1's rehide-1 relies on that identity surviving
        // for path-based re-matching across process restarts. The complete original pool = EVERY entry
        // that carries an original resource:
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

            // rehide-1 ZOMBIE-GC (design §11 ruling 1, §5): a !bPinned entry whose path no longer
            // resolves, whose resource is modded (non-/Game/), and whose OriginalTrueLocation was never
            // stamped is a PRE-FIX runtime-foreign record that can never be re-anchored — every capture
            // site post-rehide-1 always stamps OriginalTrueLocation, so only a pre-fix zombie can ever
            // satisfy all three legs at once. Drop it instead of carrying it forward: it would otherwise
            // inflate the pool with an un-healable duplicate of a resource that already lives on
            // elsewhere in the layout. Never touches a pinned (occupied) entry.
            if (!Old.bPinned && !IsValid(Live) && !Old.OriginalResourceClassPath.StartsWith(TEXT("/Game/"))
                && Old.OriginalTrueLocation.IsNearlyZero())
            {
                ZombiesDropped++;
                // Ungated by design (rare: only at an explicit re-roll, only pre-fix zombies): name WHAT
                // was dropped so an unexpectedly large ZombiesDropped count is diagnosable from the log.
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("Re-roll ZOMBIE-GC: dropping un-anchorable pre-fix entry path='%s' res='%s' (unpinned, path dead, no TrueLocation)"),
                    *Old.VanillaNodePath, *Old.OriginalResourceClassPath);
                continue;
            }

            FNodeShuffleEntry E;
            E.EntryGuid = FGuid::NewGuid();
            E.bIsNewNode = false;                 // re-enters the pool as an original; conversion re-runs below
            // P5 (addenda item 4): PRE-FIX-3 this was empty for an already-relocated original (the old
            // conversion emptied it); post-FIX-3 (current) the conversion KEEPS the original's identity,
            // so Old.VanillaNodePath is populated here too, carried straight through — P1's rehide-1
            // depends on this surviving so a stale record can still resolve/re-match by path.
            E.VanillaNodePath = Old.VanillaNodePath;
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
            // rehide-1: carry the durable anchor through the rebuild. Opportunistically stamp it when
            // Old never got one but is STILL a raw (unconverted) original (!bIsNewNode) with a live,
            // path-resolvable node — Live above IS that node itself in this case, a true first-hand
            // location (this only applies to carried pinned/occupied originals; converted/relocated
            // entries were already stamped at their own capture site or the prior rebuild).
            E.OriginalTrueLocation = Old.OriginalTrueLocation;
            if (E.OriginalTrueLocation.IsNearlyZero() && !Old.bIsNewNode && IsValid(Live))
            {
                E.OriginalTrueLocation = Live->GetActorLocation();
            }
            NewLayout.Add(E);

            VanillaLocations.Add(Old.Location);
            VanillaResourceCounts.FindOrAdd(Old.OriginalResourceClassPath)++;
            FormByResource.FindOrAdd(Old.OriginalResourceClassPath) =
                Old.ResourceForm != 0 ? Old.ResourceForm : FormSolid;
            if (Old.OriginalPurity != RP_MAX)
            {
                PurityDeckSource.Add(Old.OriginalPurity);
            }
            // P3 (deckevict-1): never adopt a session-refused class as the representative — a re-roll
            // must not re-arm a class the mod stack already proved unspawnable this session.
            if (!Old.NodeClassPath.IsEmpty() && !SpawnRefusedClassSubstitute.Contains(Old.NodeClassPath))
            {
                if (Old.ResourceForm == FormLiquid)
                {
                    LatchRepresentative(Old.NodeClassPath, SpawnableLiquidNodeClassPath, SpawnableVanillaLiquidNodeClassPath);
                }
                else
                {
                    LatchRepresentative(Old.NodeClassPath, SpawnableNodeClassPath, SpawnableVanillaNodeClassPath);
                }
                LatchByResource(Old.OriginalResourceClassPath, Old.NodeClassPath, NodeClassByResource, NodeClassByResourceConflictLogged);
            }
            VanillaCount++;
        }
        // A re-roll starts a clean record (the conversion below re-populates OriginalNodeRecord for the
        // unoccupied originals it re-detaches). Without this, stale records from the prior layout linger.
        OriginalNodeRecord.Reset();
        UE_LOG(LogNodeShuffle, Display,
            TEXT("Re-roll pool (redesign-6): rebuilt %d original entries from the FULL saved layout (pinned + all relocated), %d resource kinds — no collapse (rehide-1: %d pre-fix un-anchorable zombie(s) dropped)"),
            VanillaCount, VanillaResourceCounts.Num(), ZombiesDropped);

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
            // ns-truth-diagnostics A3: counted PER FORM, because the summary line below used to call the
            // whole total "RF_LIQUID (oil)" while this loop admits RF_LIQUID *and* RF_GAS. Lithium (gas)
            // was reported to the user as oil on the strength of that label.
            int32 ExperimentalAdded = 0, LiquidAdded = 0, GasAdded = 0;
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
                E.OriginalTrueLocation = E.Location; // rehide-1: first-hand live capture — true by construction
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
                // P3 (deckevict-1): never adopt a session-refused class as the representative.
                if (!SpawnRefusedClassSubstitute.Contains(E.NodeClassPath))
                {
                    if (EntryForm == FormLiquid)
                    {
                        LatchRepresentative(E.NodeClassPath, SpawnableLiquidNodeClassPath, SpawnableVanillaLiquidNodeClassPath);
                    }
                    else
                    {
                        LatchRepresentative(E.NodeClassPath, SpawnableNodeClassPath, SpawnableVanillaNodeClassPath);
                    }
                    LatchByResource(E.OriginalResourceClassPath, E.NodeClassPath, NodeClassByResource, NodeClassByResourceConflictLogged);
                }
                VanillaCount++;
                ExperimentalAdded++;
                (LiveForm == EResourceForm::RF_LIQUID) ? ++LiquidAdded : ++GasAdded;
            }
            UE_LOG(LogNodeShuffle, Display,
                TEXT("Non-solid augment: added %d live non-solid nodes not in the saved pool to the reroll pool "
                     "(%d RF_LIQUID e.g. oil, %d RF_GAS e.g. lithium -- this loop admits BOTH forms; the old "
                     "line called the combined total 'RF_LIQUID (oil)' and a gas resource was reported as oil "
                     "because of it) (captured into the layout for determinism)"),
                ExperimentalAdded, LiquidAdded, GasAdded);
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
                E.OriginalTrueLocation = E.Location; // rehide-1: first-hand live capture — true by construction
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
                // P3 (deckevict-1): never adopt a session-refused class as the representative.
                if (!SpawnRefusedClassSubstitute.Contains(E.NodeClassPath))
                {
                    LatchRepresentative(E.NodeClassPath, SpawnableNodeClassPath, SpawnableVanillaNodeClassPath);
                    LatchByResource(E.OriginalResourceClassPath, E.NodeClassPath, NodeClassByResource, NodeClassByResourceConflictLogged);
                }
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
        // INITIAL roll: live scan of the vanilla pool. (ns-truth-diagnostics: "(fully streamed)" removed —
        // it was an assumption, not a measurement. What IS measured: level-placed vanilla nodes are all
        // live at load, so this scan does capture the whole map's vanilla set; nodes runtime-spawned by
        // OTHER mods can arrive after this point and are missed until a re-roll. See ROLLCENSUS.)
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
            E.OriginalTrueLocation = E.Location; // rehide-1: first-hand live capture — true by construction
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
            // P3 (deckevict-1): never adopt a session-refused class as the representative.
            if (!SpawnRefusedClassSubstitute.Contains(BaseNode->GetClass()->GetPathName()))
            {
                if (E.ResourceForm == FormLiquid)
                {
                    LatchRepresentative(BaseNode->GetClass()->GetPathName(), SpawnableLiquidNodeClassPath, SpawnableVanillaLiquidNodeClassPath);
                }
                else
                {
                    LatchRepresentative(BaseNode->GetClass()->GetPathName(), SpawnableNodeClassPath, SpawnableVanillaNodeClassPath);
                }
                LatchByResource(E.OriginalResourceClassPath, BaseNode->GetClass()->GetPathName(), NodeClassByResource, NodeClassByResourceConflictLogged);
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

    // FU1-v2 (D4-A): resolve the two-tier latch into the SAME variables every consumer below already
    // reads (:1071 new-location stamp, :1356-1362 Phase-2 form stamp, :1441 conversion fallback need no
    // changes) -- vanilla wins when a /Game/ candidate was seen, else the any-tier. This is BEHAVIOR
    // (runs unconditionally, not diagnostics-gated); the log line below is the diagnostics half and is
    // what makes a non-firing vanilla tier visible (vanilla=0 with a non-empty pick == the fallback
    // fired). The any-tier values are SNAPSHOTTED FIRST so that line stays truthful however the
    // statements below are later reordered or gated (P5 owns log hygiene): reading the live Spawnable*
    // variables after the overwrite would print the resolved value twice and silently delete the only
    // evidence the fallback was used.
    const FString AnyTierSolidRepresentative = SpawnableNodeClassPath;
    const FString AnyTierLiquidRepresentative = SpawnableLiquidNodeClassPath;
    const FString ResolvedSolidRepresentative = PickRepresentative(SpawnableVanillaNodeClassPath, AnyTierSolidRepresentative);
    const FString ResolvedLiquidRepresentative = PickRepresentative(SpawnableVanillaLiquidNodeClassPath, AnyTierLiquidRepresentative);
    SpawnableNodeClassPath = ResolvedSolidRepresentative;
    SpawnableLiquidNodeClassPath = ResolvedLiquidRepresentative;
    // repclass-2 (PacketD): the rep-class census line is emitted AFTER the Phase-2 stamp loop below
    // (not here) so it can report how many new-location entries were actually stamped from
    // NodeClassByResource vs the form fallback -- that count doesn't exist until stamping runs. The
    // any-tier/vanilla values it prints are still captured HERE (this exact point, right after
    // resolution, before anything downstream could touch them) into the consts above/below so the
    // "snapshotted before the overwrite" guarantee those fields depend on is unchanged; only the LOG
    // STATEMENT moved, not what it reads.

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
    //
    // repclass-2 (PacketD): this is the AUTHORITATIVE stamp point for new-location entries -- the
    // provisional stamp at new-location creation (above, section 2) only guarantees NodeClassPath is
    // never left empty; the resource isn't dealt until the deck loop just above THIS one, so this is
    // the first place a new-location entry's FINAL AssignedResourceClassPath can be looked up in
    // NodeClassByResource. A hit means this exact resource was actually observed on a real node this
    // roll, so its class is provably correct for THIS resource -- not merely a same-form guess like
    // the fallback below. A miss falls back to the existing form representative (Packet B, unchanged)
    // so a resource this session never observed on a real node still gets something (never regresses
    // to empty where the pre-fix code produced a class).
    int32 StampedByResource = 0;
    int32 StampedByFormFallback = 0;
    for (FNodeShuffleEntry& E : NewLayout)
    {
        if (!E.bIsNewNode || !E.bActive || E.bPinned || E.AssignedResourceClassPath.IsEmpty())
        {
            continue;
        }
        const uint8 Form = FormByResource.FindRef(E.AssignedResourceClassPath);
        E.ResourceForm = Form != 0 ? Form : FormSolid;
        if (const FString* ByResource = NodeClassByResource.Find(E.AssignedResourceClassPath))
        {
            E.NodeClassPath = *ByResource;
            StampedByResource++;
        }
        else
        {
            StampedByFormFallback++;
            if (E.ResourceForm == FormLiquid && !SpawnableLiquidNodeClassPath.IsEmpty())
            {
                E.NodeClassPath = SpawnableLiquidNodeClassPath;
            }
            else if (E.NodeClassPath.IsEmpty())
            {
                E.NodeClassPath = SpawnableNodeClassPath;
            }
        }
    }
    // rep-class census (repclass-2 extension): moved here (not at resolution above) so it can report
    // the resource-keyed map plus how many of THIS roll's new-location stamps actually came from it
    // vs the form fallback -- see the comment at the resolution point for why the log statement moved
    // while what it reads did not. Kept as the single greppable "rep-class:" line.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("rep-class: solid='%s' vanilla=%d liquid='%s' vanilla=%d (any-tier solid='%s' liquid='%s') byResource=%d stampedByResource=%d stampedByFallback=%d"),
        *ResolvedSolidRepresentative, SpawnableVanillaNodeClassPath.IsEmpty() ? 0 : 1,
        *ResolvedLiquidRepresentative, SpawnableVanillaLiquidNodeClassPath.IsEmpty() ? 0 : 1,
        *AnyTierSolidRepresentative, *AnyTierLiquidRepresentative,
        NodeClassByResource.Num(), StampedByResource, StampedByFormFallback);

    // redesign-1 (Hide & Replace) CONVERSION. The deck above dealt resources onto the original
    // node SLOTS, preserving counts/floors/purity. Now we DETACH every unoccupied original from its
    // location: its resource lives on as one of OUR relocated spawned nodes, and the original node
    // itself is recorded for whole-actor hiding (SuppressOriginalNodes). Occupied/pinned originals
    // stay exactly where they are, 100% untouched (save-safety — built miners keep working).
    //
    // Build OriginalNodeRecord HERE — the only place it is built (P5: the dead standalone
    // CaptureOriginalNodeRecord() this comment used to contrast against has been removed) — because
    // after this conversion the unoccupied originals are no longer present in Layout as non-new entries.
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
                // rehide-1: durable anchor for this record — prefer the entry's own first-hand capture
                // (TRUE at initial capture and every live-capture augment); fall back to E.Location
                // (correct here too, since we are BEFORE the relocation two lines below — see design §2
                // row 4 — but on a re-roll-rebuilt entry E.OriginalTrueLocation is the carried-forward
                // real anchor while E.Location is only the previous relocated dest, so the field wins
                // whenever it's set).
                Rec.TrueLocation = !E.OriginalTrueLocation.IsNearlyZero() ? E.OriginalTrueLocation : E.Location;
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
            // coexist-1 INVARIANT: this is NodeShuffle's ONLY self-destroy of a spawned node, and it
            // removes the SpawnedNodes slot in the same synchronous block — the external-destroy
            // detection in EnsureNewNodeSpawned relies on exactly that (a lingering invalid slot can
            // only mean an OUTSIDE destroyer). Keep destroy + RemoveCurrent together.
            // coexist-veto-1: unregister AFTER Destroy() — the OnActorDestroyed delegates fire inside
            // Destroy(), so a KBFL destroyed-event listener still sees the node as MANAGED (vetoed)
            // for our own intentional wipe; the FObjectKey stays computable (index/serial unchanged).
            if (IsValid(It->Value)) { It->Value->Destroy(); }
            FNodeShuffleModule::UnregisterManagedNode(It->Value);
            It.RemoveCurrent();
        }
        MeshActorCache.Reset();
    }

    // coexist-1: a fresh roll is a fresh start for the session-only backoff/steady state — new entries
    // (new guids) get their attempts, and carried pinned entries get a clean slate too. All transient.
    // LastDormantSummaryNum tracks the cleared set so the next pass doesn't emit a "0 dormant" line.
    ExternalDestroyCounts.Empty();
    DormantThisSession.Empty();
    SteadyAliveNodes.Empty();
    LastDormantSummaryNum = 0;
    // dirtdress-1 (cold review): the capture retry budget is per-roll-population too — a new roll may
    // admit resources/originals the old budget never saw, so terminal marks and counters start over.
    CapturePendingThisPass.Reset();
    CapturePendingPasses.Empty();
    CaptureTerminalThisSession.Empty();
    // knowledge-1 item 3: same lifecycle for the spawn-failure budget (new roll = new entries/spots).
    SpawnFailCounts.Empty();
    SpawnParkedThisSession.Empty();
    // P3 (deckevict-1) — DELIBERATE OMISSION, read before "fixing" it: SpawnRefusedClassSubstitute,
    // ProvenSpawnClassByForm and bEvictionCapLogged are SESSION-scoped, not ROLL-scoped, and are NOT
    // reset here even though their siblings above are. Spawn-gating is a property of the loaded MOD
    // STACK, not of the world layout — a re-roll re-randomizes locations/resources but does not un-gate
    // a class the stack refuses to spawn. Clearing these here would re-arm the exact bug P3 fixes on
    // every re-roll (design §2.7, §9 ruling R8). They reset only on a genuine new world/session (a new
    // subsystem instance default-constructs them empty).
    // knowledge-1 item 1: a re-roll can deal modded resources that weren't active before — let the
    // post-apply knowledge pass run again for this new population.
    bKnowledgeUnlockDone = false;
    KnowledgeDeferPasses = 0; // knowledge-2: fresh defer budget for the re-armed pass
    // Packet G: a re-roll can change which node types are actively MANAGED -- let the auto-allow pass
    // reconsider extractors against this new population (mirrors bKnowledgeUnlockDone immediately above).
    bAutoAllowExtractorsDone = false;

    // Packet H1 (ns-wells-h1): deal each resource well a resource, IN PLACE. Placed HERE, at the end of
    // the roll, for two reasons. (a) Wells must re-roll with the rest of the layout, and this is the one
    // function both the initial roll and the re-roll go through. (b) It must run AFTER every draw above,
    // because it deliberately uses its OWN salted stream (see RollWellLayout) rather than this Rng —
    // taking draws from Rng would shift every later draw and change the NODE layout for a given seed,
    // i.e. merely enabling well shuffling would silently re-shuffle the player's ordinary nodes too.
    // Self-gated: a no-op that leaves any existing well data untouched when the config toggle is off.
    RollWellLayout(Seed, bIsReroll);

    // Packet H2 (ns-wells-h2): RIGID RELOCATION of whole well groups, dealt immediately after the
    // retype and on the SAME roll. Strictly after RollWellLayout, never before or merged into it: the
    // relocation deal reads bManaged, which the retype's own pin/deck logic decides, and it must be
    // able to refuse a well the retype already stood down from. It also uses its OWN salted stream
    // ('WLR2'), for the same reason RollWellLayout uses 'WELL' -- so that WITHIN THIS ROLL, turning
    // relocation on shifts no draw of the node layout or of the well retype. (ns-review-h2 F15: that
    // isolation is per-roll, NOT across re-rolls -- H2's footprint probes teach the persistent learned
    // water grid, which gates the ordinary water-locked redeal later. See NodeShuffleWellRelocateRoll.cpp.)
    // Self-gated: a no-op that leaves already-relocated wells exactly where they are when the toggle is off.
    RollWellRelocation(Seed, bIsReroll, FNodeShuffleConfigStruct::GetActiveConfig(this).RelocateResourceWells);

    UE_LOG(LogNodeShuffle, Display,
        TEXT("Rolled layout: seed %d, pool %d (vanilla %d, new %d), active %d, pinned %d"),
        Seed, PoolSize, VanillaCount, NewLocations.Num(), ActiveCount, PinnedCount);

    EmitRollCensus(Seed, bIsReroll, PoolSize, TargetActive, VanillaCount, NewLocations.Num(),
                   VanillaResourceCounts);
}

// ns-truth-diagnostics B. ONE line per roll carrying the numbers that two separate log-archaeology
// passes had to reconstruct by hand on 2026-08-08 ("are vanilla nodes streamed?", "where did lithium
// go?"). Both questions are now a single grep for `ROLLCENSUS`.
//
// THE RULE FOR EVERY FIELD HERE, AND FOR ANY FIELD ADDED LATER: it reports a MEASUREMENT and it names
// HOW that measurement was taken. No field asserts a cause. The packet that created this function
// exists because `WELLH1-UNKNOWN` asserted "it had not streamed in when the roll ran" -- a cause it
// never tested -- and that sentence was then quoted to the user as fact. If you cannot name the test,
// the field does not go in the line.
//
// Diagnostics only: one extra read-only TActorIterator pass, once per roll (rolls are rare -- initial
// load and the edge-triggered re-roll). Nothing here writes to Layout or to any actor.
void ANodeShuffleSubsystem::EmitRollCensus(int32 Seed, bool bIsReroll, int32 PoolSize, int32 TargetActive,
                                           int32 OriginalsCaptured, int32 NewLocationCount,
                                           const TMap<FString, int32>& PoolCountsByResource) const
{
    // 1. LIVE PROVENANCE, measured right now. This is the block that would have answered
    //    "were 14 unknown well cores streamed in late, or were they ours?" without a log dig.
    int32 LiveTotal = 0, LiveOurs = 0, LiveLevelPlaced = 0, LiveRuntimeOther = 0, LiveFracking = 0;
    if (const UWorld* W = GetWorld())
    {
        for (TActorIterator<AFGResourceNodeBase> It(W); It; ++It)
        {
            AFGResourceNodeBase* N = *It;
            if (!IsValid(N)) { continue; }
            if (IsFrackingActor(N)) { ++LiveFracking; continue; } // wells have their own census (WELLH1-CENSUS)
            ++LiveTotal;
            // Test 1: our module-static managed-node registry OR the identity component we attach to
            // everything we spawn. Same test the roll scan itself uses to skip our own nodes (:1014).
            if (NodeShuffleIsOurNode(N) || FNodeShuffleModule::IsManagedSpawnedNode(N)) { ++LiveOurs; }
            // Test 2: AActor::IsNetStartupActor() -- true for an actor loaded from the level (world
            // partition included), false for anything SpawnActor'd at runtime. Already relied on at
            // :4627 for the same "is this a level actor" question.
            else if (N->IsNetStartupActor()) { ++LiveLevelPlaced; }
            else { ++LiveRuntimeOther; } // runtime-spawned and not ours => spawned by some other mod
        }
    }

    // 2. THE FINAL LAYOUT, after the active-set draw and after the deal.
    //    NOTE the asymmetry, stated in the line itself: inactive ORIGINALS are gone by now -- the Hide
    //    & Replace step removes them from the layout as hide-only records (see the RemoveAll at
    //    "Remove the now-detached inactive originals"). So `inactive` here is new-location entries only.
    TMap<FString, int32> ActiveByResource;
    int32 ActiveEntries = 0, InactiveEntries = 0, ActiveWithNoResource = 0;
    // CAVE PLACEMENT DENOMINATOR (item 3). Counted in this same loop, one iteration, so it can never
    // drift from the numerator's population: the numerator is CountUndergroundEntries(), whose predicate
    // is `bIsNewNode && bActive && bUnderground` -- this is the same predicate minus bUnderground, i.e.
    // a provable superset. Cave placement demonstrably works (the user's save reported 14 underground
    // entries, then 27 after a re-roll) but until now the ONLY way to see it was to type
    // NodeShuffle.Here in the console.
    int32 ActiveNewNodeEntries = 0;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bActive) { ++InactiveEntries; continue; }
        ++ActiveEntries;
        if (E.bIsNewNode) { ++ActiveNewNodeEntries; }
        if (E.AssignedResourceClassPath.IsEmpty()) { ++ActiveWithNoResource; continue; }
        ActiveByResource.FindOrAdd(E.AssignedResourceClassPath)++;
    }

    // Short display name: the trailing object name of a class path, e.g.
    // "/Game/.../Desc_OreIron.Desc_OreIron_C" -> "Desc_OreIron_C". Full paths go in the ZERO-ACTIVE
    // warning below, so nothing is only ever shown abbreviated.
    const auto ShortRes = [](const FString& Path) -> FString
    {
        int32 Dot = INDEX_NONE;
        return Path.FindLastChar(TEXT('.'), Dot) ? Path.Mid(Dot + 1) : Path;
    };

    TArray<FString> Kinds;
    PoolCountsByResource.GenerateKeyArray(Kinds);
    // Any resource that ended up active without being in the pool would be invisible otherwise; fold
    // it in rather than silently dropping it (that omission is exactly how "48 groups" hid a missing
    // resource for two sessions).
    for (const TPair<FString, int32>& P : ActiveByResource)
    {
        if (!PoolCountsByResource.Contains(P.Key)) { Kinds.Add(P.Key); }
    }
    Kinds.Sort();

    FString PerResource;
    TArray<FString> ZeroActive;
    for (const FString& K : Kinds)
    {
        const int32 A = ActiveByResource.FindRef(K);
        const int32 InPool = PoolCountsByResource.FindRef(K);
        PerResource += FString::Printf(TEXT("%s=%d/%d "), *ShortRes(K), A, InPool);
        if (A == 0) { ZeroActive.Add(K); }
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("ROLLCENSUS: seed=%d reroll=%d "
             "| LIVE NOW (one TActorIterator<AFGResourceNodeBase> at census time, fracking actors excluded=%d): "
             "total=%d nodeShuffleSpawned=%d levelPlaced=%d runtimeSpawnedByOtherMods=%d "
             "[provenance tests -- nodeShuffleSpawned: our managed-node registry / UNodeShuffleNodeComponent; "
             "levelPlaced: AActor::IsNetStartupActor(); runtimeSpawnedByOtherMods: neither of those. "
             "On a RE-ROLL nodeShuffleSpawned reads ~0 because the previous layout's spawns are destroyed "
             "before this point and the new ones do not exist yet -- that is the measurement, not a fault] "
             "| POOL THIS ROLL: originalsCaptured=%d resourceKinds=%d newLocations=%d poolSize=%d targetActive=%d "
             "| LAYOUT AFTER DRAW+DEAL: entries=%d active=%d inactive=%d activeWithNoResourceYet=%d "
             "[inactive ORIGINALS are NOT in these counts -- they were removed from the layout as hide-only "
             "records at the Hide & Replace step; read 'Hide & Replace conversion' for those] "
             "| CAVE PLACEMENT: undergroundActiveNewNodes=%d of %d active new-location entries "
             "(cave store as known at census time: %d cell(s), %d seed(s)) "
             "[numerator = CountUndergroundEntries(), i.e. bIsNewNode && bActive && bUnderground -- the "
             "SAME predicate NodeShuffle.Here prints; denominator counted in this function's own layout "
             "loop as that predicate minus bUnderground. READING A ZERO: 0 over a 0-cell cave store means "
             "no cave cell was LOADED at census time -- which is either \"none exist\" or "
             "\"EnsureCaveStoreLoaded had not run yet\", and this line CANNOT tell them apart "
             "(GenerateNewLocations early-returns before the load when NewNodeCount is 0). Type "
             "NodeShuffle.Here to force the load and read its cave-store line to separate them; "
             "0 over a NON-ZERO cave store means cells were "
             "known and none were drawn into an active entry on this roll. The flag is the value AS "
             "ROLLED -- the apply path can clear it later when a spot fails, and this line does not see "
             "that] "
             "| ACTIVE PER RESOURCE (active/originalsInPool): %s"),
        Seed, bIsReroll ? 1 : 0, LiveFracking,
        LiveTotal, LiveOurs, LiveLevelPlaced, LiveRuntimeOther,
        OriginalsCaptured, PoolCountsByResource.Num(), NewLocationCount, PoolSize, TargetActive,
        Layout.Num(), ActiveEntries, InactiveEntries, ActiveWithNoResource,
        CountUndergroundEntries(), ActiveNewNodeEntries, CaveFloors.Num(), CaveSeedCount,
        PerResource.IsEmpty() ? TEXT("<none>") : *PerResource);

    // 3. THE ZERO-ACTIVE ALARM. Both 2026-08-08 investigations independently asked for exactly this
    //    line: the roll reported "48 groups" and never said "Desc_OreLithium_C went to zero", so a
    //    resource being erased from the world cost two log-archaeology passes to notice.
    //    Still a measurement: it states what is true of the layout and explicitly does not test why.
    for (const FString& K : ZeroActive)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("ROLLCENSUS ZERO-ACTIVE: resource '%s' had %d original node(s) in this roll's pool and has "
                 "ZERO active entries in the rolled layout. MEASURED: for this layout that resource is ABSENT "
                 "from the world -- its originals are hidden and nothing active carries it. NOT MEASURED: why; "
                 "this line tests only the layout. WHERE TO LOOK (untested here): the 'Hide & Replace "
                 "conversion' line's 'left hidden-only' count for the active-set draw, and docs/TECH-DEBT.md "
                 "T12 for the gas-floor accounting. KNOWN CONSEQUENCE: an extractor restricted to this node "
                 "type has no managed group to be allow-listed against, so AUTOALLOW will report SKIP."),
            *K, PoolCountsByResource.FindRef(K));
    }
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
    // saved vanilla set, by contrast, spans the whole playable map (MEASURED 2026-08-08: every
    // level-placed vanilla node is live at load, so the initial roll's live scan already covers the whole
    // map; and on reroll the pool is rebuilt from saved entries that cover it too. ns-truth-diagnostics:
    // this used to read "the initial roll is gated on full streaming" — it is not, and never was; see
    // MinVanillaNodesForRoll). Distribute candidates uniformly across the
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
        // Our own spawned (relocated) nodes are never cached as originals. dirtdress-1 (cold review)
        // NOTE: this exclusion is one layer of the capture poison-guard, but the safety ultimately
        // rests on OriginalNodeRecord being built SOLELY from !bIsNewNode entries at roll time (the
        // Hide & Replace conversion in RollLayout — the ONLY place it is built) — so SuppressOriginalNodes
        // can only ever hand true originals to CaptureOriginalVisualIfNeeded. Any future refactor that
        // widens SuppressOriginalNodes' caller population must re-verify our-node exclusion end to end.
        // The transient first-pass cache gap (restored-not-yet-adopted spawned nodes, before
        // AdoptRestoredSpawnedNodes runs) is unreachable today for exactly that reason: their paths
        // are never in OriginalNodeRecord.
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
            // coexist-1 §1: a DORMANT entry (its actor was externally destroyed
            // ExternalDestroyTombstoneAt times this session) does nothing at all until the next world
            // load. Num()>0 keeps the no-destroyer common case at a single int compare.
            if (DormantThisSession.Num() > 0 && DormantThisSession.Contains(Entry.EntryGuid))
            {
                continue;
            }
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

    // Packet H1 (ns-wells-h1): apply the rolled well retype. Placed AFTER the VanillaNodeCache refresh
    // above, because that is what makes the path->live-actor lookup work — fracking cores and satellites
    // are AFGResourceNodeBase actors and the cache's own iterator already includes them (it excludes only
    // OUR spawned nodes). Idempotent and re-run every pass on purpose: a well that has not streamed in
    // yet is written on whichever later pass it appears, exactly like SuppressOriginalNodes' hide funnel.
    // NOTHING ELSE IN THIS PASS TOUCHES WELLS: they are excluded from the regular node population by
    // IsFrackingActor, so this call is the mod's entire well surface.
    ApplyWellRetype(Config.ShuffleResourceWells);

    // Packet H2 (ns-wells-h2): the relocation pass -- spawn-on-discovery placement of relocated well
    // GROUPS, plus the mCore re-link that every load depends on. Deliberately AFTER ApplyWellRetype, so
    // a group is only ever spawned with a resource the retype has already resolved and asserted.
    // NOT self-gated on the toggles in the same way the others are: its first act is the once-per-
    // session AdoptRestoredWellGroups, which must run even with relocation switched OFF -- a save that
    // already holds relocated wells must keep them linked whatever the config now says, because an
    // unlinked satellite is invisible in every way except the well producing nothing.
    ApplyWellRelocation(Config.ShuffleResourceWells, Config.RelocateResourceWells, SpawnRadiusCm);

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
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("Spawned-node visuals (this pass): %d vanilla rock + %d quartz placeholder (modded) + %d oil decal"),
            SpawnedRockVanilla, SpawnedRockQuartz, SpawnedRockLiquid);
    }

    // playtest-fixes-1 (defer-log backoff): ONE summary line when the deferral count changes, instead
    // of re-logging every stuck entry every 5 s (153k lines / 35 MB in one session). Per-entry detail
    // still logs once per session at Verbose (DeferLoggedThisSession).
    if (DeferredThisPass != LastDeferSummary)
    {
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("Deferral summary: %d entries deferred this pass (%d distinct water-locked so far this session)"),
            DeferredThisPass, WaterLockedThisSession.Num());
        LastDeferSummary = DeferredThisPass;
    }

    // ns-t35-gatereach: the solid-node gate census (NodeShuffleNodeGateCensus.cpp). Called every pass;
    // it emits only when one of its counters has moved, so a settled world adds no lines.
    EmitNodeGateCensus();

    // coexist-1 §1: THE ungated coexistence summary — one Display line per pass, only when the dormant
    // count changed (never repeated on a stable world; a bulk destroyer's mass-tombstone pass = 1 line).
    if (DormantThisSession.Num() != LastDormantSummaryNum)
    {
        LastDormantSummaryNum = DormantThisSession.Num();
        UE_LOG(LogNodeShuffle, Display,
            TEXT("coexistence: %d shuffled nodes dormant this session (externally removed %d times each); they retry next load."),
            LastDormantSummaryNum, ExternalDestroyTombstoneAt);
    }

    // coexist-1 §2: on a stable world the delta-driven pass should have touched nothing — say so
    // (gated) instead of re-logging identical funnel/dress/cache lines every 5 s.
    if (FNodeShuffleModule::AreDiagnosticsEnabled()
        && !bChangedWorld && SuppressChangesLastPass == 0
        && SpawnedRockVanilla + SpawnedRockQuartz + SpawnedRockLiquid == 0)
    {
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("pass: 0 changes (steady: %d live spawned, %d hidden originals, %d dormant, %d deferred)"),
            SteadyAliveNodes.Num(), SteadyHiddenOriginals.Num(), DormantThisSession.Num(), DeferredThisPass);
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

bool ANodeShuffleSubsystem::CaptureOriginalVisualIfNeeded(AFGResourceNodeBase* Node)
{
    // playtest-fixes-1 (modded-descriptor visuals). Capture gates, all cheap, most-selective first:
    // a real AFGResourceNode with a REAL resource descriptor (UFGResourceDescriptor), NO authored
    // table entry, not already captured. Returns true only when capture is still PENDING (eligible
    // resource, nothing capturable on this node yet) so the caller keeps retrying next pass.
    //
    // dirtdress-1 (user report: 98 relocated FicsitFarming dirt nodes wearing the quartz
    // placeholder). The old single source — a PAIRED AFGNodeMeshActor (engine mNodeActor/mMeshActor
    // links) — is a lottery for KBFL-runtime-spawned originals: one session captured Dirt+Wet 200 ms
    // after the roll, the previous session went 0-for-98 across all three dirt variants, and
    // Fertilized never captured at all (log-verified 2026-07-21). Two fixes:
    //   1. NEW SOURCES: after the paired mesh actor, trust a static-mesh component OWNED BY the
    //      node actor itself, then one on an ATTACHED child actor — an original's own mesh IS its
    //      look by definition (no rock-name patterns, no proximity guessing, never a neighbor's).
    //   2. RETRY: report "eligible but empty-handed" to the caller, which then defers the
    //      steady-hidden mark so the original re-attempts every pass instead of losing its single
    //      per-session shot (the mesh-actor pairing can appear later in the session).
    AFGResourceNode* AsNode = Cast<AFGResourceNode>(Node);
    if (!AsNode) { return false; }
    // Poisoning guard (belt-and-braces — the record/cache paths already exclude our nodes): NEVER
    // capture from one of OUR spawned nodes; a quartz-dressed RockMesh captured as a resource's
    // "real look" would permanently wedge that resource on quartz.
    if (NodeShuffleIsOurNode(Node)) { return false; }
    UClass* ResClass = AsNode->GetResourceClass().Get();
    if (!ResClass || !ResClass->IsChildOf(UFGResourceDescriptor::StaticClass())) { return false; }
    const FString ShortName = ResClass->GetName();
    if (FNodeShuffleNodeAssets::FindVisual(FName(*ShortName)) != nullptr) { return false; } // authored covers it
    if (FindCapturedVisual(ShortName) != nullptr) { return false; }                          // already captured
    // dirtdress-1 (cold review): this resource burned its CaptureGiveUpPasses retry budget this
    // session with nothing capturable — terminal, not pending, so its originals steady-mark normally.
    if (CaptureTerminalThisSession.Contains(ShortName)) { return false; }

    const bool bDiagChain = FNodeShuffleModule::AreDiagnosticsEnabled()
        && !CaptureChainLogged.Contains(ShortName); // decision chain once per resource per session
    if (bDiagChain) { CaptureChainLogged.Add(ShortName); }

    // ---- Source 1 (existing, unchanged for thorium/lead): the node's paired AFGNodeMeshActor. ----
    UStaticMeshComponent* Smc = nullptr;
    const TCHAR* Source = TEXT("paired-mesh-actor");
    AFGNodeMeshActor* MeshActor = FindMeshActorForNode(Node);
    if (MeshActor)
    {
        UStaticMeshComponent* MaSmc = MeshActor->FindComponentByClass<UStaticMeshComponent>();
        if (MaSmc && MaSmc->GetStaticMesh()) { Smc = MaSmc; }
        else if (bDiagChain)
        {
            UE_LOG(LogNodeShuffle, Verbose, TEXT("CAPTURE-CHAIN %s: paired mesh actor '%s' rejected (%s)"),
                *ShortName, *MeshActor->GetName(), MaSmc ? TEXT("no static mesh set") : TEXT("no mesh component"));
        }
    }
    else if (bDiagChain)
    {
        UE_LOG(LogNodeShuffle, Verbose, TEXT("CAPTURE-CHAIN %s: node '%s' has NO paired mesh actor (engine links unset)"),
            *ShortName, *Node->GetName());
    }

    // ---- Source 2/3 (dirtdress-1): the node's OWN mesh components, then attached child actors. ----
    // SOLID resources only: gas keeps its native special node (lithium's Alkali look must stay the
    // native visual, never a reconstructed copy) and liquids draw a decal (a rock capture is dead
    // weight) — for those the paired-mesh-actor source above remains the only, ONE-SHOT capture
    // path exactly as before dirtdress-1 (they never report pending, so they never retry).
    const EResourceForm ResForm = UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(ResClass));
    bool bSawPlaceholderOnly = false; // saw candidates, but all identical to the quartz placeholder
    if (!Smc && ResForm == EResourceForm::RF_SOLID)
    {
        UStaticMesh* PlaceholderMesh = GetQuartzPlaceholderMesh();
        UStaticMeshComponent* Fallback = nullptr; // first valid but invisible candidate
        bool bSawAnyCandidate = false, bSawNonPlaceholder = false;

        // One flat candidate list: the node's own components first (most intrinsic), then components
        // of actors attached to the node (the "spawn mesh actor, attach, never set links" pattern).
        TArray<UStaticMeshComponent*, TInlineAllocator<16>> Candidates;
        TInlineComponentArray<UStaticMeshComponent*> OwnMeshes(AsNode);
        for (UStaticMeshComponent* MC : OwnMeshes) { Candidates.Add(MC); }
        const int32 NumOwn = Candidates.Num();
        TArray<AActor*> Attached;
        AsNode->GetAttachedActors(Attached);
        for (const AActor* Child : Attached)
        {
            if (!IsValid(Child) || NodeShuffleIsOurNode(Child)) { continue; }
            TInlineComponentArray<UStaticMeshComponent*> ChildMeshes(Child);
            for (UStaticMeshComponent* MC : ChildMeshes) { Candidates.Add(MC); }
        }

        for (int32 i = 0; i < Candidates.Num(); i++)
        {
            UStaticMeshComponent* MC = Candidates[i];
            const TCHAR* CandSource = i < NumOwn ? TEXT("own") : TEXT("attached");
            const TCHAR* Reject = nullptr;
            if (!IsValid(MC)) { continue; }
            else if (Cast<UInstancedStaticMeshComponent>(MC)) { Reject = TEXT("instanced (world-shared)"); }
            else if (MC->GetFName() == FName(TEXT("NodeShuffleRockMesh_Rt"))
                     || MC->GetFName() == FName(TEXT("RockMesh"))) { Reject = TEXT("our own rock subobject"); }
            else if (MC->GetStaticMesh() == nullptr) { Reject = TEXT("no static mesh set"); }
            else if (PlaceholderMesh && MC->GetStaticMesh() == PlaceholderMesh)
            {
                // Identical to the quartz placeholder: capturing it changes nothing visually and
                // would flip ResourceHasAuthoredLook for esc_/AllMinable nodes whose native mesh IS
                // the quartz look-alike — their dirty-quartz identity is by design. Terminal, not
                // pending (nothing better exists on this node).
                Reject = TEXT("identical to quartz placeholder (kept by design)");
            }
            if (bDiagChain)
            {
                UE_LOG(LogNodeShuffle, Verbose, TEXT("CAPTURE-CHAIN %s: candidate [%s] '%s' mesh='%s' visible=%d -> %s"),
                    *ShortName, CandSource, *MC->GetName(),
                    MC->GetStaticMesh() ? *MC->GetStaticMesh()->GetName() : TEXT("<none>"),
                    MC->IsVisible() ? 1 : 0, Reject ? Reject : TEXT("ACCEPT"));
            }
            if (Reject)
            {
                bSawAnyCandidate = bSawAnyCandidate || MC->GetStaticMesh() != nullptr;
                if (MC->GetStaticMesh() != nullptr && MC->GetStaticMesh() != PlaceholderMesh) { bSawNonPlaceholder = true; }
                continue;
            }
            bSawAnyCandidate = true;
            bSawNonPlaceholder = true;
            // Prefer a VISIBLE component (the rendered look); remember the first hidden one as a
            // fallback (a just-hidden original's components stay data-valid — hiding never nulls
            // the mesh — but a visible one is the stronger signal when both exist).
            if (MC->IsVisible()) { Smc = MC; Source = CandSource; break; }
            if (!Fallback) { Fallback = MC; Source = CandSource; }
        }
        if (!Smc && Fallback) { Smc = Fallback; }
        bSawPlaceholderOnly = bSawAnyCandidate && !bSawNonPlaceholder;
    }

    if (!Smc)
    {
        // Eligible but empty-handed. Non-solid resources and placeholder-identical-only nodes are
        // TERMINAL (not pending — steady proceeds, keeping lithium/esc_ retry cost exactly as
        // before); any other solid stays PENDING so the caller retries next pass (KBFL pairing or
        // late-set meshes can appear at any time during the session).
        if (bDiagChain)
        {
            // Name-the-culprit probe: is there an UNPAIRED mesh actor sitting right on this node
            // (engine links unset, so FindMeshActorForNode can't see it)? Diagnostic ONLY — we never
            // capture by proximity (neighbor capture mispairs; that rule stands). If dirt-likes
            // still fall through, this line names the follow-up fix in one diagnostics session.
            const FVector NodeLoc = Node->GetActorLocation();
            const AFGNodeMeshActor* NearMa = nullptr;
            double NearDistSq = FMath::Square(1000.0); // 10 m (FVector::DistSquared returns double)
            for (TActorIterator<AFGNodeMeshActor> It(GetWorld()); It; ++It)
            {
                const double DistSq = FVector::DistSquared(It->GetActorLocation(), NodeLoc);
                if (DistSq < NearDistSq) { NearDistSq = DistSq; NearMa = *It; }
            }
            const UStaticMeshComponent* NearSmc = NearMa ? NearMa->FindComponentByClass<UStaticMeshComponent>() : nullptr;
            UE_LOG(LogNodeShuffle, Display,
                TEXT("CAPTURE-CHAIN %s: no capturable source on '%s' -> %s. Nearest mesh actor within 10m: %s"),
                *ShortName, *Node->GetName(),
                ResForm != EResourceForm::RF_SOLID ? TEXT("terminal (non-solid: mesh-actor source only)")
                    : bSawPlaceholderOnly ? TEXT("terminal (placeholder-identical only)")
                    : TEXT("PENDING (retry next pass)"),
                NearMa ? *FString::Printf(TEXT("'%s' mesh='%s' dist=%.1fm (UNPAIRED — engine links unset)"),
                    *NearMa->GetName(),
                    NearSmc && NearSmc->GetStaticMesh() ? *NearSmc->GetStaticMesh()->GetName() : TEXT("<none>"),
                    FMath::Sqrt(NearDistSq) / 100.0f) : TEXT("none"));
        }
        const bool bPending = ResForm == EResourceForm::RF_SOLID && !bSawPlaceholderOnly;
        // dirtdress-1 (cold review): feed the per-pass retry-budget scratch — SuppressOriginalNodes
        // turns it into one consecutive-attempt count per RESOURCE at the end of the pass.
        if (bPending) { CapturePendingThisPass.Add(ShortName); }
        return bPending;
    }

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
    Cap.MeshScale = Smc->GetComponentScale(); // WORLD scale — what the mesh visually renders at
    CapturedVisuals.Add(Cap);
    ResolvedCaptureCache.Remove(ShortName); // drop any stale (pre-capture) resolution
    // Ungated by design: one line per resource per SAVE (the capture persists), the evidence that a
    // new resource's look was learned — e.g. dirt on first encounter of a dirt original.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("CAPTURE: stored visual for %s from '%s' (source=%s) — mesh '%s', %d mat(s), scale=(%.2f,%.2f,%.2f) (persisted)"),
        *ShortName, *Node->GetName(), Source, *Cap.MeshPath, Cap.MaterialPaths.Num(),
        Cap.MeshScale.X, Cap.MeshScale.Y, Cap.MeshScale.Z);
    // dirtdress-1 (cold review): success wipes the retry budget — including a pending report an
    // earlier original of this resource filed THIS pass (a later original delivered the mesh).
    CapturePendingPasses.Remove(ShortName);
    CapturePendingThisPass.Remove(ShortName);
    RedressSpawnedOfResource(ShortName);
    return false;
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
    bool bPinnedEntry = false;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (E.EntryGuid == EntryGuid) { bUndergroundEntry = E.bUnderground; bPinnedEntry = E.bPinned; break; }
    }
    // knowledge-1 item 5: a PINNED entry has a BUILDING on it (that is what pinning means), so the
    // slope probe below raycasts into the miner's mesh/foundation instead of terrain — a bogus steep
    // "ground" normal that tilted the rock and slope-sank it ~1 m below the node origin (ROCKDIAG at
    // the dirty-caterium miner: rock Z 3827.60 vs entry 3927.15). The entry is settled — trust its
    // stored Z and dress flat, exactly like a fresh spawn on flat open ground.
    if (!bUndergroundEntry && !bPinnedEntry)
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
    // coexist-1 §3: per-rock detail is diagnostics-only; the ungated evidence is the per-pass
    // "Spawned-node visuals" summary in ApplyLayout (change-driven).
    if (FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("dressed %s rock (node subobject) for %s: mesh '%s' scale=(%.2f,%.2f,%.2f) relZ=%.0f %d mat(s)"),
            bQuartz ? TEXT("QUARTZ-placeholder") : bCaptured ? TEXT("CAPTURED") : TEXT("vanilla"),
            *ResourceClass->GetName(), *Mesh->GetName(),
            WantScale.X, WantScale.Y, WantScale.Z, RelOffset.Z, MatPtrs.Num());
    }
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
    // P3 (deckevict-1): never adopt a session-refused class as the representative.
    FString SolidNodeClassPath;
    for (const FNodeShuffleEntry& E : NewLayout)
    {
        if (E.bIsNewNode && !E.NodeClassPath.IsEmpty() && !SpawnRefusedClassSubstitute.Contains(E.NodeClassPath))
        {
            SolidNodeClassPath = E.NodeClassPath;
            break;
        }
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

void ANodeShuffleSubsystem::PreRegisterRestoredNodesForVeto()
{
    // coexist-veto-1 FIX A (first-load registry gap). Timing facts this pass exists for: on a loaded
    // save our restored spawned-node actors ALREADY exist at BeginPlay (SaveGame data — Layout
    // included — deserializes during world load, before BeginPlay; the same invariant
    // FinalizeAdoptedNode relies on for mResourceClassOverride), and an armed KBFL destroyer runs its
    // initial existing-actors sweep synchronously at OnWorldBeginPlay — the SAME frame — while the
    // first RefreshTick (which runs AdoptRestoredSpawnedNodes, the normal registration point) is
    // ~5 s away. Without this pre-pass the veto registry is empty exactly when that sweep queries
    // it, and every restored node (miner-occupied ones included) eats one destroy/respawn round per
    // load. REGISTRY-ONLY by design: no SpawnedNodes writes, no adopt logic, no entry state changes —
    // AdoptRestoredSpawnedNodes formalizes adoption later exactly as today (RegisterManagedNode is a
    // set-add, so double registration is a no-op).
    // Identity = the SAME predicate family as the adopt pass: legacy nodes by SaveGame guid; real-
    // class nodes = runtime (!IsNetStartupActor) + within AdoptMatchRadiusCm of a bIsNewNode entry +
    // resource agreement when both sides are known. Over-registration of a coincidentally-located
    // foreign runtime node is acceptable: worst case we shield one extra node for one session —
    // vanilla originals at their own original locations are level actors (IsNetStartupActor) and can
    // never match. New game: Layout is empty -> the whole pass is a no-op.
    int32 PreRegistered = 0;
    if (Layout.Num() > 0)
    {
        TSet<FGuid> NewNodeGuids;                       // all spawned-entry guids (legacy match)
        TArray<const FNodeShuffleEntry*> ActiveEntries; // active spawned entries (location match)
        for (const FNodeShuffleEntry& E : Layout)
        {
            if (!E.bIsNewNode) { continue; }
            NewNodeGuids.Add(E.EntryGuid);
            if (E.bActive) { ActiveEntries.Add(&E); }
        }
        const float MatchSq = FMath::Square(AdoptMatchRadiusCm);
        for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
        {
            AFGResourceNode* Node = *It;
            if (!IsValid(Node)) { continue; }
            if (const ANodeShuffleResourceNode* Legacy = Cast<ANodeShuffleResourceNode>(Node))
            {
                if (Legacy->EntryGuid.IsValid() && NewNodeGuids.Contains(Legacy->EntryGuid))
                {
                    FNodeShuffleModule::RegisterManagedNode(Node);
                    PreRegistered++;
                }
                continue;
            }
            if (Node->IsNetStartupActor()) { continue; } // level-placed original — never ours
            const FVector NodeLoc = Node->GetActorLocation();
            const FString NodeRes =
                Node->GetResourceClass() ? Node->GetResourceClass()->GetPathName() : FString();
            for (const FNodeShuffleEntry* E : ActiveEntries)
            {
                if (FVector::DistSquared(NodeLoc, E->Location) >= MatchSq) { continue; }
                if (!NodeRes.IsEmpty() && !E->AssignedResourceClassPath.IsEmpty()
                    && NodeRes != E->AssignedResourceClassPath) { continue; }
                FNodeShuffleModule::RegisterManagedNode(Node);
                PreRegistered++;
                break;
            }
        }
    }
    if (PreRegistered > 0)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("veto: pre-registered %d restored nodes before arm"), PreRegistered);
    }
    else
    {
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("veto: pre-registered 0 restored nodes before arm (new game or nothing restored yet)"));
    }
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
    // coexist-veto-1 FIX 4b: this pass is safe against tombstone interaction ONLY because it runs exactly
    // once at first load (gated by bAdoptedRestoredNodes) — before any external destroy can have been
    // counted, so it can never re-adopt (and thereby resurrect) an entry the backoff later tombstones.

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
            FNodeShuffleModule::RegisterManagedNode(Node); // coexist-veto-1: adopted = managed (veto registry)
            Adopted++; LegacyByGuid++;
        }
        if (FinalizeAdoptedNode(Node, *Idx)) { PinnedOnLoad++; }
    }

    // PASS 2: real-class restored nodes — match by location to an active new-node entry not already
    // adopted. (Radius = AdoptMatchRadiusCm, shared with the FIX A veto pre-registration pass.)
    const float MatchSq = FMath::Square(AdoptMatchRadiusCm);
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        AFGResourceNode* Node = *It;
        if (!IsValid(Node)) { continue; }
        if (Node->IsA<ANodeShuffleResourceNode>()) { continue; }  // handled in pass 1
        if (Node->IsNetStartupActor()) { continue; }              // level-placed original, not ours
        // ns-review-h2 F9: NEVER adopt a fracking satellite into the ORDINARY node layout. Packet H2
        // spawns runtime AFGResourceNodeFrackingSatellite actors, which satisfy every predicate below
        // (runtime, no component of ours, resource matches a layout entry that happens to hold the same
        // resource). This pass also runs BEFORE ApplyWellRelocation, so it gets first refusal. Adopting
        // one would hand it to SettleNewNodesNearPlayers, which MOVES nodes -- silently breaking the
        // rigid body H2 spent 36 yaws validating, and orphaning the well's own record of where it is.
        // Wells are excluded from the regular node population everywhere else in this file by exactly
        // this predicate; this was the one place it was missing.
        if (IsFrackingActor(Node)) { continue; }
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
            FNodeShuffleModule::RegisterManagedNode(Node); // coexist-veto-1: adopted = managed (veto registry)
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
    // P3 (deckevict-1): resolve through the substitute redirect — same reason as EnsureNewNodeSpawned's
    // adopt branch (this is the load-time path): a substituted node must re-assert placement gates
    // based on the class it actually spawned from, not E.NodeClassPath's original (possibly refusing)
    // value.
    const bool bVanillaOrigin = ResolveSpawnNodeClassPath(E).StartsWith(TEXT("/Game/"));
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
            if (FNodeShuffleModule::AreDiagnosticsEnabled())
            {
                UE_LOG(LogNodeShuffle, Verbose, TEXT("visfix: hid native mesh '%s' on %s (assigned resource has an authored look)"),
                    *MC->GetName(), *Node->GetName());
            }
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
    // P5: the ONE-SHOT LATCH stays unconditional (bRegDiagLogged still flips on the very first call,
    // diagnostics on or off — identical trigger timing to before this pass); only the compute + log
    // inside now also require diagnostics, so an all-session-off run does the census work zero times
    // instead of once, and the line itself is silent.
    if (!bRegDiagLogged)
    {
        bRegDiagLogged = true;
        if (FNodeShuffleModule::AreDiagnosticsEnabled())
        {
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
    // coexist-1 §1: dormant entries sleep until the next world load (belt to the ApplyLayout skip —
    // this is the only spawn path, so the guard here makes the invariant local too).
    if (DormantThisSession.Num() > 0 && DormantThisSession.Contains(Entry.EntryGuid))
    {
        return;
    }

    AFGResourceNode* const* Existing = SpawnedNodes.Find(Entry.EntryGuid);

    // coexist-1 §1 (EXTERNAL-DESTROY BACKOFF): the slot holds a dead actor. NodeShuffle's only
    // self-destroy (the re-roll wipe in RollLayout) removes the slot in the same synchronous block, so
    // a lingering invalid slot is proof some OTHER mod destroyed our node (e.g. a KBFL actor-listener
    // removing FGResourceNodeBase actors). Count it; re-materialize below until the per-session cap,
    // then go dormant instead of fighting a destroy/respawn war. Session-only — never saved; the next
    // load retries once per node, so records survive and a relented destroyer sees the node stick.
    if (Existing && !IsValid(*Existing))
    {
        // coexist-veto-1: drop the dead actor from the veto registry BEFORE the slot goes away (the
        // UPROPERTY hard ref in SpawnedNodes is what keeps the destroyed object's memory addressable
        // for the key computation).
        FNodeShuffleModule::UnregisterManagedNode(*Existing);
        SpawnedNodes.Remove(Entry.EntryGuid);
        SteadyAliveNodes.Remove(Entry.EntryGuid);
        Existing = nullptr; // Remove() invalidated the slot pointer
        const int32 Count = ++ExternalDestroyCounts.FindOrAdd(Entry.EntryGuid);
        if (Entry.bPinned)
        {
            // coexist-veto-1 FIX 3 (policy A1): PINNED entries — a player's miner/extractor stands on
            // this node — are EXEMPT from tombstoning. The README promises an occupied node is never
            // changed ("checked continuously"); a session-long dormant occupied node would break that
            // promise and orphan the miner, so we keep respawning it every pass. The cost is a bounded
            // destroy/respawn residual on the (few) pinned nodes when a destroyer mod is active — the
            // lesser evil. Still counted in ExternalDestroyCounts for the log evidence. First destroy
            // per entry is an ungated Display so the user can see their occupied node is contested;
            // repeats are Verbose, diagnostics-gated.
            if (Count == 1)
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("coexistence: occupied node %s (%s) externally destroyed — keeping it respawned (pinned)"),
                    *Entry.EntryGuid.ToString(), *Entry.AssignedResourceClassPath);
            }
            else if (FNodeShuffleModule::AreDiagnosticsEnabled())
            {
                UE_LOG(LogNodeShuffle, Verbose,
                    TEXT("coexistence: occupied node %s (%s) externally destroyed again (#%d this session) — keeping it respawned (pinned)"),
                    *Entry.EntryGuid.ToString(), *Entry.AssignedResourceClassPath, Count);
            }
            // fall through to the normal respawn below — a pinned entry NEVER goes dormant
        }
        else
        {
            if (FNodeShuffleModule::AreDiagnosticsEnabled())
            {
                UE_LOG(LogNodeShuffle, Verbose,
                    TEXT("coexistence: spawned node %s (%s) at %s destroyed by another mod (#%d this session)"),
                    *Entry.EntryGuid.ToString(), *Entry.AssignedResourceClassPath,
                    *Entry.Location.ToCompactString(), Count);
            }
            if (Count >= ExternalDestroyTombstoneAt)
            {
                DormantThisSession.Add(Entry.EntryGuid);
                // coexist-veto-1 FIX 2: the tombstone transition means this node is now PERMANENTLY
                // gone for the session — a world change. Without this flag the early return below
                // skips the pass-tail RefreshScannersAndRadarTowers() gate, and the handheld
                // scanner's cached mNodeClusters keeps a cluster for the vanished node → phantom
                // ping on empty ground (this mod's historical scanner-5/6/7 bug class).
                bOutChangedWorld = true;
                // Per-record detail is gated; the ONE ungated coexistence summary is emitted from
                // ApplyLayout's tail, once per pass, only when the dormant count changed (a bulk
                // destroyer tombstones many entries in the same pass — one line covers them all).
                if (FNodeShuffleModule::AreDiagnosticsEnabled())
                {
                    UE_LOG(LogNodeShuffle, Verbose,
                        TEXT("coexistence: entry %s (%s) tombstoned for this session (dormant; retries next load)"),
                        *Entry.EntryGuid.ToString(), *Entry.AssignedResourceClassPath);
                }
                return;
            }
        }
    }

    if (Existing && IsValid(*Existing))
    {
        // coexist-1 §2 (IDEMPOTENT PASS): everything in this alive branch is an idempotent re-assert
        // that only matters ONCE PER LIVE INSTANCE — reload/adopt/respawn produce a NEW instance and
        // naturally fall through again (weak-ptr identity mismatch). While the SAME instance stays
        // valid and un-hidden, skip the whole chain: no LoadClass, no component scans, no re-dress.
        // The IsHidden re-check preserves the old every-pass un-hide guarantee with one flag read: if
        // anything external hides the actor, the next pass re-runs the full re-assert chain.
        if (const TWeakObjectPtr<AFGResourceNode>* Steady = SteadyAliveNodes.Find(Entry.EntryGuid))
        {
            if (Steady->Get() == *Existing && !(*Existing)->IsHidden())
            {
                return;
            }
        }

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
        // P3 (deckevict-1): resolve through the substitute redirect — a substituted node adopted after
        // reload must re-dress as vanilla-origin using the class it ACTUALLY spawned from, not the
        // entry's original (possibly refusing) NodeClassPath still recorded in the save.
        const bool bVanillaOrigin = ResolveSpawnNodeClassPath(Entry).StartsWith(TEXT("/Game/"));
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
                    if (FNodeShuffleModule::AreDiagnosticsEnabled())
                    {
                        UE_LOG(LogNodeShuffle, Verbose, TEXT("SLOPEFIT: refit rotation of %s at %s (actor tilt-clamped; rock re-dresses)"),
                            *Entry.EntryGuid.ToString(), *Entry.Location.ToCompactString());
                    }
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
        // coexist-1 §2: the full re-assert chain ran for THIS instance — steady until it dies, hides,
        // or is replaced (weak-ptr identity check at the top of this branch).
        SteadyAliveNodes.Add(Entry.EntryGuid, *Existing);
        return;
    }

    // knowledge-1 item 3: an entry whose class refused to spawn SpawnGiveUpAttempts times in a row is
    // parked for the session (it retries next load) — no per-pass class loads/raycasts/spawn attempts.
    if (SpawnParkedThisSession.Num() > 0 && SpawnParkedThisSession.Contains(Entry.EntryGuid))
    {
        return;
    }

    // P3 (deckevict-1): resolve through the session-only substitute redirect BEFORE loading — an entry
    // whose original class proved unspawnable this session transparently spawns from its substitute
    // instead. Entry.NodeClassPath itself is NEVER rewritten (it is UPROPERTY(SaveGame)) — the save
    // stays honest about what the world originally held; see ResolveSpawnNodeClassPath. Hoisted to a
    // local so bVanillaOrigin and the give-up block further below in THIS spawn path reuse the SAME
    // resolved value instead of calling the resolver again. (The EARLIER adopt/idempotent branch above
    // — which returns before reaching here — resolves separately, since this local isn't in scope yet.)
    const FString ResolvedNodeClassPath = ResolveSpawnNodeClassPath(Entry);
    UClass* NodeClass = LoadClassByPath(ResolvedNodeClassPath);
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
        // ns-t35-gatereach: the settle gate's denominator. Counted before the call, so it is a chance
        // to fire and not an outcome; the two refusal counters below split ONLY by the flag
        // RaycastSettle sets, because that is the only sub-reason it exposes.
        ++NodeGateSettleReached;
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
            // ns-t35-gatereach: the settle gate's rejection half, split by the one signal available.
            if (bWaterNoLand) { ++NodeGateSettleRejectedWater; } else { ++NodeGateSettleRejectedNoWater; }
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
                    UE_LOG(LogNodeShuffle, Verbose,
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
                if (FNodeShuffleModule::AreDiagnosticsEnabled())
                {
                    UE_LOG(LogNodeShuffle, Verbose,
                        TEXT("Spawn-on-discovery: deferred %s node at %s (no terrain / out of range; further retries silent)"),
                        *ResourceClass->GetName(), *Entry.Location.ToCompactString());
                }
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
            // ns-t35-gatereach: this lambda IS the node path's occupancy gate, so counting here counts
            // every call of it -- the dealt spot and every spiral-nudge probe. The counters are read by
            // nothing but the census emitter and no branch below tests them.
            ++NodeGateCallOccupiedReached;
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
                    ++NodeGateCallOccupiedRejected; // ns-t35-gatereach (resource-node half)
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
                            ++NodeGateCallOccupiedRejected; // ns-t35-gatereach (buildable half)
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
        // ns-t27-corefirst: THE BODY MOVED, THE BEHAVIOUR DID NOT. This was 15 lines of ray-casting
        // private to this function -- and that privacy is docs/TECH-DEBT.md T26: the WELL placement
        // path could not reach it, so wells were placed with five gates while nodes got six, and a
        // comment in NodeShuffleWellFootprint.cpp claimed parity that did not exist. The rays, the
        // reach, the eye height and the 7-of-8 threshold now live in ONE place
        // (ANodeShuffleSubsystem::IsSpotEnclosed, defined in NodeShuffleWellFootprint.cpp) and both
        // paths call it, so they cannot drift. Same inputs, same predicate, same verdict.
        auto IsEnclosed = [&](const FVector& At) -> bool
        {
            int32 Blocked = 0, Total = 0;
            // ns-t35-gatereach: THE COUNT T35 ASKS FOR ON THIS PATH. The verdict is still whatever the
            // shared predicate returns -- it is called once, its result is stored, returned unchanged,
            // and the counters are written from that stored result rather than from a second call.
            ++NodeGateCallEnclosedReached;
            const bool bEnclosed = IsSpotEnclosed(At, Blocked, Total);
            if (bEnclosed) { ++NodeGateCallEnclosedRejected; }
            return bEnclosed;
        };

        // ns-t35-gatereach: the PRIMARY-SPOT population, counted separately from the all-calls counters
        // (ns-t36-probefix relabelled it from "dealt spot": T35 cold review F3 -- Entry.Location here is
        // AFTER the settle step wrote it and after any nudge a previous pass persisted, so "dealt" names
        // the wrong population. What is counted is one test per entry per visit, wherever the entry
        // currently sits.)
        // inside the two lambdas because one nudging entry contributes many calls and exactly one dealt
        // spot. The enclosure line below is unchanged -- the short circuit still decides whether
        // IsEnclosed runs -- so the reached counter is incremented on the same condition the short
        // circuit tests, and never by evaluating that condition a second way.
        ++NodeGatePrimaryOccupiedReached;
        const bool bSpotOccupied = OverlapsAt(Entry.Location);
        if (bSpotOccupied) { ++NodeGatePrimaryOccupiedRejected; }
        else { ++NodeGatePrimaryEnclosedReached; }
        const bool bSpotEnclosed = !bSpotOccupied && IsEnclosed(Entry.Location);
        if (bSpotEnclosed) { ++NodeGatePrimaryEnclosedRejected; }
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
    // P3 (deckevict-1): bVanillaOrigin drives the placement-gate re-assert below and the visual branch
    // further down — it must reflect the class we're ACTUALLY spawning (ResolvedNodeClassPath, hoisted
    // above), not the entry's original (possibly refusing) NodeClassPath.
    const bool bVanillaOrigin = ResolvedNodeClassPath.StartsWith(TEXT("/Game/"));
    UClass* SpawnClass = NodeClass ? NodeClass : ANodeShuffleResourceNode::StaticClass();

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    // NOTE: intentionally NO RF_Transient — the node must be collected by the save system.
    // spawnrace-1: KBFL's OnActorSpawned delegate fires INSIDE this SpawnActor call — before it
    // returns, so before RegisterManagedNode below can run. The scope flags the spawn window so the
    // veto recognizes the newborn as ours at gate time (live evidence: 429 destroys/session of
    // respawned nodes that lost exactly this race). Kept TIGHT — just the SpawnActor call — so
    // nothing else (InitResource, registration, dressing) runs shielded.
    AFGResourceNode* Node = nullptr;
    {
        FNodeShuffleSpawningScope SpawnScope;
        Node = GetWorld()->SpawnActor<AFGResourceNode>(
            SpawnClass, Entry.Location, Entry.Rotation, Params);
    }
    if (!Node)
    {
        UE_LOG(LogNodeShuffle, Warning, TEXT("Failed to spawn new node (%s) at %s"),
            *SpawnClass->GetName(), *Entry.Location.ToCompactString());
        // knowledge-1 item 3: consecutive-failure budget (evidence: 675 identical warnings in ~4 min
        // for Node_BioWaterSF+_C — SpawnActor returns null every pass at the same coordinates).
        // First failure per CLASS also drops a diag-gated class-flags breadcrumb toward the real
        // cause (an abstract/deprecated class can never spawn).
        if (FNodeShuffleModule::AreDiagnosticsEnabled() && !SpawnFailFlagsLogged.Contains(SpawnClass->GetName()))
        {
            SpawnFailFlagsLogged.Add(SpawnClass->GetName());
            UE_LOG(LogNodeShuffle, Display,
                TEXT("spawn: class '%s' flags: abstract=%d deprecated=%d newerVersionExists=%d raw=0x%08x"),
                *SpawnClass->GetName(),
                SpawnClass->HasAnyClassFlags(CLASS_Abstract) ? 1 : 0,
                SpawnClass->HasAnyClassFlags(CLASS_Deprecated) ? 1 : 0,
                SpawnClass->HasAnyClassFlags(CLASS_NewerVersionExists) ? 1 : 0,
                static_cast<uint32>(SpawnClass->GetClassFlags()));
        }
        int32& Fails = SpawnFailCounts.FindOrAdd(Entry.EntryGuid);
        if (++Fails >= SpawnGiveUpAttempts)
        {
            SpawnFailCounts.Remove(Entry.EntryGuid);
            SpawnParkedThisSession.Add(Entry.EntryGuid);
            // Ungated by design: one line per parked entry per session — shipping-log evidence of a
            // class that refuses to spawn, without the per-pass warning firehose.
            UE_LOG(LogNodeShuffle, Display,
                TEXT("spawn: giving up on %s at %s for this session after %d attempts (SpawnActor returned null — class may be spawn-gated by its owning mod)"),
                *SpawnClass->GetName(), *Entry.Location.ToCompactString(), SpawnGiveUpAttempts);
            // P3 (deckevict-1): evict the CLASS (not just this entry) on its FIRST give-up. Must run
            // AFTER the park+log above — EvictSpawnRefusingClass un-parks whatever it successfully
            // rebinds, including this same entry if a substitute exists (order per design §2.3).
            EvictSpawnRefusingClass(ResolvedNodeClassPath, Entry);
        }
        return;
    }
    SpawnFailCounts.Remove(Entry.EntryGuid); // success resets the consecutive-failure budget
    // P3 (deckevict-1): SpawnForm hoisted from its original declaration further below (still used there,
    // for the visual-dress branch) so it's available here too, for the ProvenSpawnClassByForm record.
    const EResourceForm SpawnForm = UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(ResourceClass));
    // P3: remember a class we WATCHED spawn, per resource FORM — the substitution ladder's strongest
    // candidate (measured spawnable in THIS exact stack, not merely assumed). Only /Game/ classes count
    // (a vanilla-origin node takes our fallback rock, so a substitute drawn from here is always visible).
    const FString SpawnClassPath = SpawnClass->GetPathName();
    if (SpawnClassPath.StartsWith(TEXT("/Game/")))
    {
        const uint8 FormKey = (SpawnForm == EResourceForm::RF_LIQUID) ? FormLiquid : FormSolid;
        if (!ProvenSpawnClassByForm.Contains(FormKey)) { ProvenSpawnClassByForm.Add(FormKey, SpawnClassPath); }
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
    if (FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        UE_LOG(LogNodeShuffle, Verbose, TEXT("scanner: registered spawned node representation at %s (%s)"),
            *Entry.Location.ToCompactString(), *ResourceClass->GetName());
    }

    // Legacy fallback nodes (pure-C++ subclass) need their "Resource" UseBox re-asserted; real node classes
    // have a native box (EnsureNodeUseBox is a no-op for them).
    EnsureNodeUseBox(Node);

    // redesign-11 (THE MK1 SNAP FIX): register the node into the resource-node MANAGER's mResourceNodes list
    // — the registry the Mk1 hologram queries. Runtime list (not save-persisted), re-asserted on adopt too.
    RegisterNodeWithManager(Node);

    SpawnedNodes.Add(Entry.EntryGuid, Node);
    FNodeShuffleModule::RegisterManagedNode(Node); // coexist-veto-1: spawned = managed (veto registry)

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
    // P3 (deckevict-1): SpawnForm now hoisted above (near the ProvenSpawnClassByForm record) — reused
    // here unchanged.
    if (SpawnForm == EResourceForm::RF_LIQUID)
    {
        RebuildNodeNativeVisual(Node);
        if (Comp) { Comp->DressOilDecal(ResourceClass); }
        SpawnedRockLiquid++;
        if (FNodeShuffleModule::AreDiagnosticsEnabled())
        {
            UE_LOG(LogNodeShuffle, Verbose, TEXT("spawned LIQUID node %s (oil decal)"), *ResourceClass->GetName());
        }
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
        const bool bSpawnDiag = FNodeShuffleModule::AreDiagnosticsEnabled();
        if (ResourceHasAuthoredLook(ResourceClass))
        {
            HideNativeNodeMesh(Node, Comp ? Comp->RockMesh : nullptr);
            SpawnVisualRockForNode(Node, ResourceClass, Entry.EntryGuid);
            if (bSpawnDiag)
            {
                UE_LOG(LogNodeShuffle, Verbose, TEXT("spawned MODDED-CLASS node %s as real class %s (authored/captured look; native mesh hidden)"),
                    *ResourceClass->GetName(), *SpawnClass->GetName());
            }
        }
        else if (NodeHasOwnVisual(Node, Comp ? Comp->RockMesh : nullptr))
        {
            if (Comp) { Comp->ForceVisible(); }
            if (bSpawnDiag)
            {
                UE_LOG(LogNodeShuffle, Verbose, TEXT("spawned MODDED node %s as real class %s (native visual)"),
                    *ResourceClass->GetName(), *SpawnClass->GetName());
            }
        }
        else
        {
            SpawnVisualRockForNode(Node, ResourceClass, Entry.EntryGuid); // fallback rock (captured/quartz) -> visible
            if (bSpawnDiag)
            {
                UE_LOG(LogNodeShuffle, Verbose, TEXT("spawned MODDED node %s as real class %s (fallback rock — no native visual)"),
                    *ResourceClass->GetName(), *SpawnClass->GetName());
            }
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
    if (FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        UE_LOG(LogNodeShuffle, Verbose, TEXT("Spawn-on-discovery: materialized %s node at %s"),
            *ResourceClass->GetName(), *Entry.Location.ToCompactString());
    }
    // coexist-1 §2: a fresh spawn already ran the full dress/register/use-box chain — steady from birth
    // (the ungated per-pass visuals summary in ApplyLayout still evidences the spawn).
    SteadyAliveNodes.Add(Entry.EntryGuid, Node);
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
        // coexist-1 §1: dormant entries do no work (no settles, no redeals, no defer counts).
        if (DormantThisSession.Num() > 0 && DormantThisSession.Contains(Entry.EntryGuid))
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

// P5 (addenda item 3): CaptureOriginalNodeRecord() removed — dead code, no callers (P1 designer
// found; P1 review §B confirmed untouched). OriginalNodeRecord is built the ONE real way, inside
// RollLayout's Hide & Replace conversion (see the "Build OriginalNodeRecord here" comment there),
// which is also where Rec.bModdedOrigin is actually stamped.

// ------------------------------------------------------------------------------------------------
// T4 (docs/TECH-DEBT.md) -- MESH-HIDE LATENCY, MEASURED
// ------------------------------------------------------------------------------------------------
// WHAT IS UNOBSERVABLE TODAY. The user confirmed 2026-08-08 that after a shuffle a hidden original's
// ROCK stays visible on arrival -- long enough to fly to it and try two miners -- while the NODE hide is
// already correct (neither miner snapped, so the actor is out of mResourceNodes). Only the mesh actor
// lags. The one always-on funnel line covering the hide is emitted ONCE PER LOAD and reports nothing
// about mesh actors, so the duration is a stopwatch guess and nothing in the log can confirm or deny it.
//
// THE POPULATION THAT CAN LAG, AND WHY IT IS SMALL. The node hide (below) hides the paired
// AFGNodeMeshActor in the same statement -- but only if FindMeshActorForNode resolves at that instant.
// If it does not, the record is then marked SteadyHiddenOriginals and the loop skips it on every later
// pass, so nothing ever retries the pairing. So "records whose mesh actor was unresolvable at node-hide
// time" is exactly the set that can go dark later, and it is the only set this tracks. That is the
// cheaper design the packet asked for in place of a per-pass sweep over all 630 records.
//
// THIS PACKET HIDES NOTHING NEW. The watch sweep re-resolves and REPORTS; it does not hide the mesh
// actor it finds. Behaviour is byte-for-byte what it was.
//
// ns-t7-split (2026-08-08): THIS WAS `namespace NodeShuffleMeshHideLatency`, a block of module statics
// that existed ONLY because the packet that wrote it may not edit NodeShuffleSubsystem.h. The header is
// no longer barred, so the state is now MEMBERS (MeshHideLatency* in NodeShuffleSubsystem.h, with
// FNodeShuffleMeshHideWatch declared at file scope there). Same fields, same names after the prefix,
// same initial values, same read/write sites.
//
// THE WORLD-CHANGE RESET BELOW IS DELIBERATELY KEPT rather than "made redundant by member lifetime".
// GetTimeSeconds restarts with the world, so a second save load in one process must not inherit the
// previous world's timestamps -- and keeping the guard is what makes this promotion provably
// behaviour-identical: on a fresh subsystem instance MeshHideLatencyWatchWorld is nullptr and the guard
// fires exactly as it fired on fresh module statics, while on an instance that somehow outlives a world
// it still fires. Deleting it would have made correctness depend on an actor-lifetime assumption this
// packet cannot compile, let alone measure.

void ANodeShuffleSubsystem::SuppressOriginalNodes()
{
    SuppressChangesLastPass = 0; // coexist-1 §2: per-pass change tally (feeds "pass: 0 changes")
    if (OriginalNodeRecord.Num() == 0)
    {
        return;
    }
    // coexist-1 §2: deregistrations THIS pass = delta of the running counter (the old summary keyed on
    // the running TOTAL, so it re-fired "hid 0 ..." every pass forever once anything had deregistered).
    const int32 DeregBefore = ScannerDeregisterCount;

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
    // instability across save/reload (fix = match by location, not path). coexist-1: the counters are now
    // ALWAYS tallied (pure int increments — the once-per-load ungated funnel summary needs them); only
    // the per-pass LOGGING stays gated behind EnableDiagnostics, and delta-only at that.
    const bool bDiagHide = FNodeShuffleModule::AreDiagnosticsEnabled();
    int32 DbgNear = 0, DbgFoundPath = 0, DbgAlreadyHidden = 0, DbgOcc = 0, DbgMissedPath = 0;
    int32 DbgCapturePending = 0; // dirtdress-1: originals held out of steady, awaiting a capture source
    int32 DbgRematched = 0; // rehide-1: stale records resolved THIS pass by location+class+resource re-match

    // ---- T4 BOOKKEEPING (docs/TECH-DEBT.md T4) ----
    const UWorld* MeshHideWorld = GetWorld();
    if (MeshHideLatencyWatchWorld != MeshHideWorld)
    {
        MeshHideLatencyWatchWorld = MeshHideWorld;
        MeshHideLatencyWatch.Reset();
        MeshHideLatencyPassIndex = 0;
        MeshHideLatencyResolvedLaterTotal = 0;
        MeshHideLatencyStillVisibleWhenResolvedTotal = 0;
        MeshHideLatencyMaxDelaySeconds = -1.0f;
        MeshHideLatencyLastDelaySeconds = -1.0f;
        for (int32 i = 0; i < 8; i++) { MeshHideLatencyLastSummary[i] = -1; }
    }
    const int32 MeshHidePass = ++MeshHideLatencyPassIndex;
    const float MeshHideNow = MeshHideWorld ? MeshHideWorld->GetTimeSeconds() : 0.0f;
    // THE DENOMINATORS for the latency summary, both counted in the same loop iteration as the hide they
    // describe: of the nodes hidden this pass, how many had a resolvable mesh actor at that instant.
    int32 MeshResolvedAtHide = 0, MeshUnresolvedAtHide = 0, MeshActorNeverAssigned = 0;
    int32 ResolvedLaterThisPass = 0, StillVisibleThisPass = 0, RocksHiddenNearWatched = 0;
    // ONE reporting path for "this record's rock mesh actor became resolvable on a LATER pass than the
    // node hide", called from the two places that can observe it (the main loop, for a record not yet
    // steady; and the post-loop watch sweep, which is the only route open to one that already went
    // steady) so both produce the same numbers from the same code.
    // `Route`: "cache" = its AFGNodeMeshActor became resolvable (main loop or watch sweep);
    // "backstop" = the stray-rock backstop hid a rock at this record's node location. BOTH are
    // observations of "this rock went dark LATER than its node", which is the quantity T4 needs, so
    // both MUST feed the same delay fields -- a delay measured on one route and reported as -1 on the
    // other is the "zero with no denominator" defect this packet exists to avoid.
    const auto ReportMeshResolvedLater =
        [&](const FString& Path, const FNodeShuffleMeshHideWatch& W, bool bMeshVisibleNow,
            const TCHAR* Route) -> void
    {
        const float Delay = MeshHideNow - W.HideTimeSeconds;
        const int32 PassDelay = MeshHidePass - W.HidePass;
        ResolvedLaterThisPass++;
        MeshHideLatencyResolvedLaterTotal++;
        if (bMeshVisibleNow)
        {
            StillVisibleThisPass++;
            MeshHideLatencyStillVisibleWhenResolvedTotal++;
        }
        MeshHideLatencyLastDelaySeconds = Delay;
        MeshHideLatencyMaxDelaySeconds =
            FMath::Max(MeshHideLatencyMaxDelaySeconds, Delay);
        if (bDiagHide)
        {
            UE_LOG(LogNodeShuffle, Verbose,
                TEXT("[route=%s] ")
                TEXT("MESHHIDE-LATENCY record='%s': its AFGNodeMeshActor was NOT resolvable when the node ")
                TEXT("was hidden on hide-pass %d, and IS resolvable now on hide-pass %d -- %.1f s and %d ")
                TEXT("pass(es) later. meshVisibleAtThisMoment=%d (1 = the rock was still drawn this long ")
                TEXT("after the node stopped accepting a miner; 0 = something had already hidden it). ")
                TEXT("MEASURED: pairing resolvability, the mesh actor's hidden flag, and world time ")
                TEXT("between the two events. NOT MEASURED: why the pairing was missing, and what hid ")
                TEXT("the rock. Said once per record."),
                Route, *Path, W.HidePass, MeshHidePass, Delay, PassDelay, bMeshVisibleNow ? 1 : 0);
        }
    };

    int32 NodesHidden = 0;
    // Real locations of the originals we processed near the player this pass (resolved by path). The stray-
    // rock BACKSTOP below uses these instead of the stale Rec.Location (which is the relocated dest after a
    // re-roll) so a lingering separate rock next to a just-hidden node is still caught.
    TArray<FVector> NearOriginalLocs;
    NearOriginalLocs.Reserve(OriginalNodeRecord.Num());
    // rehide-1: candidate pool for stale-record re-matching — lazily built at most once per pass, only
    // if a record's path actually misses below (most passes never need it: once a record re-matches it
    // resolves by path from then on, and the common case is zero stale records at all). BoundCandidates
    // is per-pass too: a candidate already claimed by an earlier record this pass can't satisfy another
    // (design §3.7) — a fresh local set each call already scopes this correctly with no member state.
    TArray<AFGResourceNodeBase*> RematchPool;
    bool bRematchPoolBuilt = false;
    TSet<AFGResourceNodeBase*> RematchBoundThisPass;
    for (FNodeShuffleSuppressedOriginal& Rec : OriginalNodeRecord)
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
        if (!Node)
        {
            // rehide-1: the record's path is dead — most likely a spawner mod re-created this node with
            // a fresh auto-numbered id since the last process boot (exactly what the funnel comment
            // above predicted). Try to recover identity from location+class+resource before giving up;
            // this is the ONLY place TryRematchStaleRecord is called, and it mutates ONLY Rec + the
            // originating Layout entry on success — no hide/suppress decision is made here or in it.
            if (!bRematchPoolBuilt)
            {
                BuildRematchCandidatePool(RematchPool);
                bRematchPoolBuilt = true;
                if (bDiagHide)
                {
                    UE_LOG(LogNodeShuffle, Verbose,
                        TEXT("REHIDE: candidate pool built (%d eligible foreign runtime nodes) for this pass's stale-record re-match"),
                        RematchPool.Num());
                }
            }
            AFGResourceNodeBase* Rematched = TryRematchStaleRecord(Rec, RematchPool, RematchBoundThisPass);
            if (!Rematched) { DbgMissedPath++; continue; } // still not streamed in / no re-match this pass
            DbgRematched++;
            Node = Rematched; // rebind already done inside TryRematchStaleRecord — fall through unchanged below
        }
        // coexist-1 §2 (IDEMPOTENT PASS): the SAME resolved instance was fully processed (captured,
        // roof-classified, deregistered, hidden) on an earlier pass and is still hidden — nothing can
        // have changed (a hidden, collision-less node cannot become occupied), so skip the funnel work.
        // Its location still feeds the stray-rock backstop. An original that unstreams and re-streams
        // is a NEW instance at the same path -> weak-ptr mismatch -> full funnel runs again (re-hide).
        if (const TWeakObjectPtr<AFGResourceNodeBase>* Steady = SteadyHiddenOriginals.Find(Rec.VanillaNodePath))
        {
            if (Steady->Get() == Node && Node->IsHidden())
            {
                NearOriginalLocs.Add(Node->GetActorLocation());
                DbgNear++; DbgFoundPath++; DbgAlreadyHidden++;
                continue;
            }
        }
        // SCANNER FIX: hide EVERY loaded original, not just those within 300 m of a player. World partition
        // streams a region LARGER than the old 300 m hide gate, so a relocated original that was loaded but
        // >300 m away stayed visible AND registered with the resource scanner — the scanner pinged it, the
        // player walked toward it, and it vanished the instant they crossed 300 m (a real false lead the user
        // hit). Hiding ANY loaded (path-resolvable) original closes the gap: it's deregistered before a player
        // can scan-and-walk to it. Cheap — the hide is idempotent (skip-if-already-hidden) and the deregister
        // runs once per node (ScannerDeregistered), and only loaded actors ever reach this point.
        const FVector NodeLoc = Node->GetActorLocation();
        // rehide-1 backfill: heal a record's durable anchor the first time its path resolves this
        // session (covers records whose TrueLocation was never stamped — pre-rehide-1 saves, or a
        // record just rebound by TryRematchStaleRecord where it's already set and this is a no-op). By
        // the next restart every live record has a TRUE anchor, independent of the stamping sites above.
        if (Rec.TrueLocation.IsNearlyZero()) { Rec.TrueLocation = NodeLoc; }
        NearOriginalLocs.Add(NodeLoc); // hidden-original locations for the stray-rock backstop below
        DbgNear++;

        // playtest-fixes-1 (modded-descriptor visuals): every resolved original — occupied ones too —
        // may donate its visual (paired mesh actor, or dirtdress-1: its own/attached meshes) for
        // resources our table doesn't cover (RP thorium, bamrenew lead, FF dirt). One-time per
        // resource; all gates inside are cheap. Runs BEFORE the hide below by design, and a pending
        // capture defers the steady mark (further down) so this original retries every pass until
        // its resource captures — hiding never invalidates the mesh data, so retries stay correct.
        const bool bCapturePending = CaptureOriginalVisualIfNeeded(Node);
        if (bCapturePending) { DbgCapturePending++; }

        // cave-nodes-1: roof-classify each original once (persisted) — under-a-roof originals are the
        // proven-reachable seeds the cavern flood-fill grows from.
        ClassifyOriginalUnderground(Node, Rec.VanillaNodePath);

        // Hide the original node actor whole (this removes its rock, INCLUDING an instanced one). Never
        // touch an occupied node (a built miner). Occupancy checked on the Base + the Node-only portable check.
        {
            DbgFoundPath++; if (Node->IsHidden()) { DbgAlreadyHidden++; }
            AFGResourceNode* AsNode = Cast<AFGResourceNode>(Node);
            const bool bOcc = Node->IsOccupied() || (AsNode && IsNodeOccupiedAnyway(AsNode));
            if (bOcc) { DbgOcc++; }
            if (!bOcc)
            {
                bool bChanged = false;
                if (Node->GetActorEnableCollision()) { Node->SetActorEnableCollision(false); bChanged = true; }
                if (!Node->IsHidden()) { Node->SetActorHiddenInGame(true); bChanged = true; }
                // T4: SAME BEHAVIOUR, NOW MEASURED. The `if` below is the entire mesh-hide this mod
                // performs on the ordinary-node path; when FindMeshActorForNode returns null nothing
                // hides the rock here and the record is about to go steady, so this branch decides
                // whether the rock can lag at all. The counters record which branch ran; the watch map
                // records the records that took the null branch, and NOTHING here hides anything extra.
                AFGNodeMeshActor* MeshActor = FindMeshActorForNode(Node);
                if (MeshActor)
                {
                    const bool bMeshWasVisible = !MeshActor->IsHidden();
                    MeshActor->SetActorHiddenInGame(true);
                    MeshActor->SetActorEnableCollision(false);
                    MeshResolvedAtHide++;
                    if (const FNodeShuffleMeshHideWatch* W =
                            MeshHideLatencyWatch.Find(Rec.VanillaNodePath))
                    {
                        ReportMeshResolvedLater(Rec.VanillaNodePath, *W, bMeshWasVisible, TEXT("cache"));
                        MeshHideLatencyWatch.Remove(Rec.VanillaNodePath);
                    }
                }
                else
                {
                    // T4, THE POPULATION SPLIT. "FindMeshActorForNode returned null" is TWO different
                    // worlds and only one of them can lag:
                    //   mMeshActor.IsNull()  -> this node was never AUTHORED a separate mesh actor. Its
                    //      rock (if any) is not an AFGNodeMeshActor, hiding the node actor is the whole
                    //      story here, and nothing will EVER resolve. Enrolling these drowns the real
                    //      population ~9:1 on the live save (96 mesh actors vs 1087 streamed ordinary
                    //      nodes, MESHTYPE-CENSUS 2026-08-08) and pins the watch list open forever.
                    //   !IsNull() but unresolved -> a mesh actor IS assigned and is not loaded/paired
                    //      right now. THIS is the set whose rock can go dark later.
                    // Friend access to the private soft pointer: Config/AccessTransformers.ini grants
                    // ANodeShuffleSubsystem friendship on AFGResourceNodeBase. GetMeshActor() is NOT a
                    // substitute -- it returns .Get(), which is null in both worlds.
                    MeshUnresolvedAtHide++;
                    if (Node->mMeshActor.IsNull())
                    {
                        MeshActorNeverAssigned++;
                    }
                    else if (!MeshHideLatencyWatch.Contains(Rec.VanillaNodePath))
                    {
                        FNodeShuffleMeshHideWatch W;
                        W.NodeLoc = NodeLoc;
                        W.HideTimeSeconds = MeshHideNow;
                        W.HidePass = MeshHidePass;
                        MeshHideLatencyWatch.Add(Rec.VanillaNodePath, W);
                    }
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
                            if (bDiagHide)
                            {
                                UE_LOG(LogNodeShuffle, Verbose,
                                    TEXT("RADFIX: removed radiation emitter of hidden original %s at %s (running total %d)"),
                                    *Rec.VanillaNodePath, *NodeLoc.ToCompactString(), RadEmittersRemoved);
                            }
                        }
                    }
                    ScannerDeregistered.Add(Rec.VanillaNodePath);
                    ScannerDeregisterCount++;
                    if (bDiagHide)
                    {
                        UE_LOG(LogNodeShuffle, Verbose,
                            TEXT("scanner: DEREGISTERED hidden original %s (scan + representation + manager mResourceNodes)"),
                            *Rec.VanillaNodePath);
                    }
                }
                if (bChanged) { NodesHidden++; }
                // coexist-1 §2: fully processed AND hidden — steady for as long as this instance lives
                // hidden (skip checked at the top of the loop). Occupied originals are NEVER marked
                // steady: they stay live and must keep being re-tested (a removed miner frees them).
                // dirtdress-1: a capture-PENDING original is not steady either — it re-runs the funnel
                // (cheap: every step above is idempotent/set-gated) so its resource's capture retries
                // each pass instead of losing its one shot per session. It goes steady the pass its
                // resource captures (or proves terminal).
                if (!bCapturePending) { SteadyHiddenOriginals.Add(Rec.VanillaNodePath, Node); }
            }
        }
    }

    // T4: THE WATCH SWEEP. Only records whose mesh actor was UNRESOLVABLE at node-hide time are in this
    // map, so this is O(watched) two-cache-lookup work, not O(all records) -- and a record leaves the map
    // the first time it is reported. A record that already went steady never re-enters the loop above, so
    // this is the ONLY route by which its rock can be measured at all. IT HIDES NOTHING: measurement only.
    if (MeshHideLatencyWatch.Num() > 0)
    {
        TArray<FString> ReportedNow;
        for (const TPair<FString, FNodeShuffleMeshHideWatch>& Pair : MeshHideLatencyWatch)
        {
            AFGResourceNodeBase* WatchedNode = FindOriginalBaseByPath(Pair.Key);
            if (!WatchedNode) { continue; }
            AFGNodeMeshActor* WatchedMesh = FindMeshActorForNode(WatchedNode);
            if (!WatchedMesh) { continue; }
            ReportMeshResolvedLater(Pair.Key, Pair.Value, !WatchedMesh->IsHidden(), TEXT("cache"));
            ReportedNow.Add(Pair.Key);
        }
        for (const FString& K : ReportedNow) { MeshHideLatencyWatch.Remove(K); }
    }

    // dirtdress-1 (cold review): capture retry-budget bookkeeping. Each resource that reported
    // PENDING anywhere this pass consumes one attempt-pass; at CaptureGiveUpPasses (~3 min of real
    // attempts) it goes terminal for the session — its originals steady-mark from the next pass and
    // the placeholder stays. Same count-then-tombstone idiom as ExternalDestroyCounts/Dormant.
    for (const FString& Res : CapturePendingThisPass)
    {
        int32& Passes = CapturePendingPasses.FindOrAdd(Res);
        if (++Passes >= CaptureGiveUpPasses)
        {
            CaptureTerminalThisSession.Add(Res);
            CapturePendingPasses.Remove(Res);
            // Ungated by design: once per resource per session, the shipping-log evidence that the
            // capture ladder exhausted its sources (pair with CAPTURE-CHAIN under diagnostics).
            UE_LOG(LogNodeShuffle, Display,
                TEXT("CAPTURE: giving up on %s for this session after %d attempts (no capturable mesh found — placeholder stays)"),
                *Res, CaptureGiveUpPasses);
        }
    }
    CapturePendingThisPass.Reset();

    // BACKSTOP: hide any separate node-rock mesh sitting at a suppressed original's location with no
    // node actor behind it (a rare actor-independent rock). Never touch deposits, fracking, our own
    // spawned rocks, or instanced components (world-shared).
    // coexist-1 §2: the sweep walks EVERY static-mesh component in the world, which is real per-pass
    // cost on a stable world for a catch that near-always finds nothing. Run it whenever a node NEWLY
    // hid this pass (its rock may linger), else on a 30 s cooldown (catches a stray rock that streams
    // in long after its original was hidden — bounded latency instead of every 5 s).
    constexpr float RockBackstopCooldownSeconds = 30.0f;
    const float NowSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    const bool bRunBackstop = NodesHidden > 0
        || (NowSeconds - LastRockBackstopSeconds >= RockBackstopCooldownSeconds);
    int32 RocksHidden = 0;
    if (bRunBackstop)
    {
    LastRockBackstopSeconds = NowSeconds;
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
            // T4, THE SECOND OBSERVATION ROUTE. On this save it is the ONLY one that fires: the
            // mesh-actor cache pairs ~6% of streamed ordinary nodes, so a watched record's
            // AFGNodeMeshActor mostly never becomes resolvable and the watch sweep never reports.
            // The rock still goes dark -- here, via the backstop -- and that delay IS the number T4
            // has never had. It therefore feeds the SAME delay fields as the cache route.
            // MEASURED: both timestamps and the 2-D distance. NOT MEASURED / NOT CLAIMED: that this
            // rock belongs to that record -- the backstop matches on proximity alone, and so does this.
            FString BackstopHitKey;
            for (const TPair<FString, FNodeShuffleMeshHideWatch>& WPair : MeshHideLatencyWatch)
            {
                if (FVector::DistSquared2D(WPair.Value.NodeLoc, Loc) >= FMath::Square(RockOwnRange)) { continue; }
                RocksHiddenNearWatched++;
                ReportMeshResolvedLater(WPair.Key, WPair.Value, /*bMeshVisibleNow=*/true, TEXT("backstop"));
                BackstopHitKey = WPair.Key;
                break;
            }
            // Removed AFTER the range-for, never during it.
            if (!BackstopHitKey.IsEmpty()) { MeshHideLatencyWatch.Remove(BackstopHitKey); }
        }
    }
    } // if (bRunBackstop)

    // coexist-1 §2: the summary is CHANGE-driven now. The old condition keyed on the RUNNING
    // ScannerDeregisterCount total, so once anything had ever deregistered it re-logged
    // "hid 0 ... (running totals)" every 5 s forever. Fire only when THIS pass changed something.
    const int32 DeregThisPass = ScannerDeregisterCount - DeregBefore;
    SuppressChangesLastPass = NodesHidden + RocksHidden + DeregThisPass;
    if (SuppressChangesLastPass > 0)
    {
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("Hide originals: hid %d original nodes and %d stray original rocks; deregistered %d from scanner, removed %d radiation emitters (running totals) (Hide & Replace)"),
            NodesHidden, RocksHidden, ScannerDeregisterCount, RadEmittersRemoved);
    }
    // coexist-1 §3: ONE ungated funnel-totals line per LOAD (the first pass that processed records),
    // so a shipping log still proves the hide pipeline ran without any per-pass repetition.
    if (!bLoadFunnelLogged && (DbgNear > 0 || DbgMissedPath > 0))
    {
        bLoadFunnelLogged = true;
        // truthdiag-fixes: the sixth field was named `notStreamed` until 2026-08-08. NOTHING HERE TESTS
        // STREAMING. The counter is DbgMissedPath: FindOriginalBaseByPath returned null (a TWeakObjectPtr
        // cache lookup behind an IsValid() gate — it never loads anything) AND TryRematchStaleRecord
        // failed. So the honest name is `pathUnresolved`: "this record's path did not resolve to a live
        // actor this pass." The old name was a LABEL asserting a cause, which is worse than prose doing
        // it — a label is what makes a reader stop looking. Its complement (`loaded`, DbgNear) is still a
        // genuine residency measurement, which is why this is a rename and not a re-measurement.
        // Grepping logs from before this build needs the OLD token `notStreamed=` (same number, same slot).
        UE_LOG(LogNodeShuffle, Display,
            TEXT("Hide-originals funnel (first pass this load): records=%d loaded=%d newlyHidden=%d alreadyHidden=%d occupied=%d pathUnresolved=%d capturePending=%d rematched=%d"),
            OriginalNodeRecord.Num(), DbgNear, NodesHidden, DbgAlreadyHidden, DbgOcc, DbgMissedPath,
            DbgCapturePending, DbgRematched);
    }
    // T4 (docs/TECH-DEBT.md): MESH-HIDE LATENCY SUMMARY. Always-on and SUMMARY ONLY -- per-record detail
    // is the Verbose MESHHIDE-LATENCY lines above, behind the diagnostics flag. Delta-gated on its own
    // fields (same idiom as LastHideFunnel), so a settled world stops printing it entirely.
    {
        const int32 Summary[8] = {
            NodesHidden, MeshResolvedAtHide, MeshUnresolvedAtHide, MeshActorNeverAssigned,
            MeshHideLatencyWatch.Num(),
            MeshHideLatencyResolvedLaterTotal,
            MeshHideLatencyStillVisibleWhenResolvedTotal,
            RocksHiddenNearWatched };
        bool bLatencyChanged = false;
        for (int32 i = 0; i < 8; i++)
        {
            if (Summary[i] != MeshHideLatencyLastSummary[i])
            {
                bLatencyChanged = true;
                MeshHideLatencyLastSummary[i] = Summary[i];
            }
        }
        if (bLatencyChanged && (NodesHidden > 0 || MeshHideLatencyWatch.Num() > 0
                                || MeshHideLatencyResolvedLaterTotal > 0))
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("MESHHIDE-LATENCY hide-pass %d: hid %d original node(s) this pass; of those %d had a ")
                TEXT("resolvable AFGNodeMeshActor AT THE MOMENT OF THE HIDE (their rock went dark on the ")
                TEXT("SAME pass) and %d did NOT (this mod hid no rock for them on this pass). ")
                TEXT("Of the %d that did NOT, %d had NO mesh actor assigned at all (mMeshActor is null -- the node's ")
                TEXT("rock is not a separate AFGNodeMeshActor, nothing can lag through this mechanism, and these are ")
                TEXT("NOT watched); the remainder have one assigned but unresolved and ARE watched. ")
                TEXT("WATCH LIST ")
                TEXT("(records whose mesh actor was unresolvable at node-hide time -- the only population ")
                TEXT("that can lag): %d still open. RESOLVED LATER, session totals: %d record(s), of which ")
                TEXT("%d still had a VISIBLE mesh actor at the moment it became resolvable; %d of them ")
                TEXT("were reported this pass, %d still visible this pass. Delay last=%.1f s worst=%.1f s ")
                TEXT("(-1 = no delay has ever been measured this session). STRAY-ROCK BACKSTOP: %d rock(s) ")
                TEXT("hidden this pass within %.0f cm of a watched record. HOW TO READ A ZERO: 'resolved ")
                TEXT("later=0' WITH 'watch list=0' means every node hidden so far had its mesh actor ")
                TEXT("paired at hide time and nothing could lag; 'resolved later=0' with a NON-ZERO watch ")
                TEXT("list means the lag population exists and none of it has been observed going dark ")
                TEXT("yet -- that is not evidence the rocks vanished instantly. MEASURED: pairing ")
                TEXT("resolvability, the hidden flag, and world time. NOT MEASURED: why a pairing was ")
                TEXT("missing, what eventually hides a rock this line never reports, and anything about ")
                TEXT("render-state catch-up after SetActorHiddenInGame.")
                TEXT(" BACKSTOP CADENCE: the stray-rock backstop runs on any pass that newly hid a node, else on a ")
                TEXT("%.0f s cooldown; %.1f s have elapsed since it last ran. That cooldown is an UPPER BOUND this ")
                TEXT("mod itself imposes on how late a rock can go dark -- it is a measured constant from this run, ")
                TEXT("not an explanation of any particular delay above."),
                MeshHidePass, NodesHidden, MeshResolvedAtHide, MeshUnresolvedAtHide,
                MeshUnresolvedAtHide, MeshActorNeverAssigned,
                MeshHideLatencyWatch.Num(),
                MeshHideLatencyResolvedLaterTotal,
                MeshHideLatencyStillVisibleWhenResolvedTotal,
                ResolvedLaterThisPass, StillVisibleThisPass,
                MeshHideLatencyLastDelaySeconds,
                MeshHideLatencyMaxDelaySeconds,
                RocksHiddenNearWatched, RockOwnRange,
                RockBackstopCooldownSeconds, NowSeconds - LastRockBackstopSeconds);
        }
    }

    if (bDiagHide && (DbgNear > 0 || DbgMissedPath > 0))
    {
        // Hide funnel: of all records — how many resolved to a LOADED actor (scanner-1: every loaded original
        // hides, any distance), of those how many were already hidden / occupied (skipped), and how many were
        // path-missed (record whose node isn't streamed in this pass). (See docs/DIAGNOSTICS.md.)
        // coexist-1 §2: DELTA-ONLY — identical numbers are not re-logged every pass.
        // dirtdress-1: capturePending = originals deferred from steady awaiting a capture source; a stuck
        // non-zero value across passes names the resource-capture gap (pair with the CAPTURE-CHAIN lines).
        const int32 Funnel[8] = { OriginalNodeRecord.Num(), DbgNear, DbgFoundPath, DbgAlreadyHidden, DbgOcc, DbgMissedPath, DbgCapturePending, DbgRematched };
        bool bFunnelChanged = false;
        for (int32 i = 0; i < 8; i++) { if (Funnel[i] != LastHideFunnel[i]) { bFunnelChanged = true; LastHideFunnel[i] = Funnel[i]; } }
        if (bFunnelChanged)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("HIDEDIAG funnel: recordsTotal=%d loaded=%d foundByPath=%d alreadyHidden=%d occupied=%d pathMissed=%d capturePending=%d rematched=%d"),
                Funnel[0], Funnel[1], Funnel[2], Funnel[3], Funnel[4], Funnel[5], Funnel[6], Funnel[7]);
        }
    }
}

void ANodeShuffleSubsystem::BuildRematchCandidatePool(TArray<AFGResourceNodeBase*>& OutPool) const
{
    // rehide-1: VanillaNodeCache already excludes our own nodes (built that way, :1622-1636) and holds
    // every live non-ours original loaded this pass — including nodes a HEALTHY record already resolves
    // by path (harmless to include: the identity key below still has to match a STALE record's own
    // class+resource+anchor, and MinNodeSpacing keeps distinct authored spots outside 300 cm of each
    // other — design §3.1). Filter down to the actor-kind pre-gates that make a re-match safe at all
    // (§3.1/§3.4): a level-placed actor can NEVER be re-matched (IsNetStartupActor is the same hard
    // guarantee the veto pre-registration relies on, :2491-2492), a mod-spawned transient is never part
    // of the vanilla/foreign pool (parity with IsEligibleVanillaNodeReason's capture-eligibility gate,
    // :5822-5827), our own nodes and anything already managed/adopted are never foreign, and fracking
    // wells are handled on their own dedicated path (never suppressed, never re-matched).
    OutPool.Reset();
    for (const TPair<FString, TWeakObjectPtr<AFGResourceNodeBase>>& Pair : VanillaNodeCache)
    {
        AFGResourceNodeBase* Candidate = Pair.Value.Get();
        if (!IsValid(Candidate)) { continue; }
        if (Candidate->IsNetStartupActor()) { continue; }       // level actor — never re-matchable
        if (Candidate->HasAnyFlags(RF_Transient)) { continue; } // parity with capture eligibility
        if (NodeShuffleIsOurNode(Candidate)) { continue; }
        if (FNodeShuffleModule::IsManagedSpawnedNode(Candidate)) { continue; }
        if (IsFrackingActor(Candidate)) { continue; }
        if (Cast<AFGResourceDeposit>(Candidate)) { continue; } // design §3.1: deposits are never re-match candidates
        OutPool.Add(Candidate);
    }
}

AFGResourceNodeBase* ANodeShuffleSubsystem::TryRematchStaleRecord(FNodeShuffleSuppressedOriginal& Rec,
    const TArray<AFGResourceNodeBase*>& CandidatePool, TSet<AFGResourceNodeBase*>& BoundCandidates)
{
    // rehide-1 (design §3.1/§3.2). Anchor: TrueLocation when stamped (the durable cross-session
    // anchor); legacy records (never stamped) fall back to Location, which is TRUE at initial capture
    // and STALE (the previous relocated dest) after a re-roll rebuild — the stale case NORMALLY fails
    // closed below (our own node sits at the dest and is pool-excluded). Cold-review P1 residual: a
    // same-class+resource FOREIGN node within 300 cm of the dest ring WOULD bind, and the rebind
    // persists (TrueLocation stamped at match). Accepted as §3.4.5's risk class; the ungated REHIDE
    // MATCHED line (anchor kind [legacy], distance, names) is the audit trail for exactly this case.
    const FVector Anchor = !Rec.TrueLocation.IsNearlyZero() ? Rec.TrueLocation : Rec.Location;
    const bool bTrueAnchor = !Rec.TrueLocation.IsNearlyZero();

    // Resolve the originating layout entry — it kept its VanillaNodePath through the Hide & Replace
    // conversion (redesign-6 FIX 3, :1287-1291) — for the class + resource identity legs, and so a
    // successful match can rebind it too (design §3.3).
    FNodeShuffleEntry* MatchEntry = nullptr;
    for (FNodeShuffleEntry& E : Layout)
    {
        if (E.VanillaNodePath == Rec.VanillaNodePath) { MatchEntry = &E; break; }
    }
    const FString ExpectedClassPath = MatchEntry ? MatchEntry->NodeClassPath : FString();
    const FString ExpectedResourcePath = MatchEntry ? MatchEntry->OriginalResourceClassPath : FString();
    const FString LegacyClassToken = RehideClassNameToken(Rec.VanillaNodePath);
    const FString ClassLabel = !ExpectedClassPath.IsEmpty()
        ? FPackageName::ObjectPathToObjectName(ExpectedClassPath) : LegacyClassToken;
    const FString ResLabel = !ExpectedResourcePath.IsEmpty()
        ? FPackageName::ObjectPathToObjectName(ExpectedResourcePath) : TEXT("<unknown>");

    const float MatchSq = FMath::Square(AdoptMatchRadiusCm);
    AFGResourceNodeBase* Best = nullptr;
    float BestSq = MatchSq;
    int32 CandidatesInRadius = 0;
    for (AFGResourceNodeBase* Candidate : CandidatePool)
    {
        if (!IsValid(Candidate) || BoundCandidates.Contains(Candidate)) { continue; }
        const float DSq = FVector::DistSquared(Candidate->GetActorLocation(), Anchor);
        if (DSq >= MatchSq) { continue; } // fixed radius gate, independent of the running best (BestSq)

        // Class leg: prefer the resolved entry's NodeClassPath (full path, exact); fall back to the
        // legacy name-token parse (design §3.1 point 2) only when no entry resolved.
        // P1 review finding 5 (INFO, carried into P5 per the reviewer's own recommendation: "leave
        // as-is"): this else-if is LABEL-ONLY as a matcher in practice, not dead in the sense of
        // unreachable code — when ExpectedClassPath is empty (no MatchEntry resolved),
        // ExpectedResourcePath is ALSO empty, and the resource leg below rejects every candidate that
        // HAS a resource class (P1 review's exact wording), so no candidate carrying a resource class
        // can pass both legs via this branch. NOT proven for the one residual corner: a candidate whose
        // GetResourceClass() is NULL leaves CandidateResPath empty too, so the resource leg's
        // both-sides-unknown escape applies and such a candidate COULD bind here on a class-name-token
        // match alone. Neither the cache build (:1796-1809) nor BuildRematchCandidatePool requires a
        // resource class, so the pool can contain one; treat this as an assumed-rare corner with no
        // static proof, not as an impossibility. Left in place rather than deleted: P5 is
        // log/diagnostics hygiene only, and this branch is control flow, not a log line.
        if (!ExpectedClassPath.IsEmpty())
        {
            if (Candidate->GetClass()->GetPathName() != ExpectedClassPath) { continue; }
        }
        else if (Candidate->GetClass()->GetName() != LegacyClassToken)
        {
            continue;
        }

        // Resource leg: skip the check only when BOTH sides are unknown (mirrors adopt's guard,
        // :2614-2616 / :2525-2526).
        const UClass* CandidateRes = Candidate->GetResourceClass();
        const FString CandidateResPath = CandidateRes ? CandidateRes->GetPathName() : FString();
        if (!(CandidateResPath.IsEmpty() && ExpectedResourcePath.IsEmpty())
            && CandidateResPath != ExpectedResourcePath)
        {
            continue;
        }

        CandidatesInRadius++;
        if (DSq < BestSq) { BestSq = DSq; Best = Candidate; }
    }

    if (!Best)
    {
        // FAIL-CLOSED (design §3.4 point 4): zero mutation, the record stays stale and is retried next
        // pass — covers late/lazy spawners. Only the LOG is throttled to once per record per session.
        if (!RematchNoMatchLogged.Contains(Rec.VanillaNodePath))
        {
            RematchNoMatchLogged.Add(Rec.VanillaNodePath);
            // P5: this whole block is diagnostic work, not just its log — the "nearest" scan below is a
            // SECOND full CandidatePool pass solely to enrich the message, so it is gated alongside the
            // log rather than left to run once-per-record regardless of diagnostics. The once-per-record
            // THROTTLE above (RematchNoMatchLogged.Add) is unaffected by this gate — it still marks the
            // record whether or not diagnostics is on, so behavior (never re-scanning this record again
            // this session) is unchanged.
            if (FNodeShuffleModule::AreDiagnosticsEnabled())
            {
                // Diagnostic-only second pass: nearest same-class candidate regardless of radius, so the
                // log itself proves which §5 timeline is real (small distance = a late spawner about to
                // heal on its own; huge distance = a stale/legacy anchor needing one re-roll, §11 ruling 2).
                AFGResourceNodeBase* Nearest = nullptr;
                float NearestSq = TNumericLimits<float>::Max();
                for (AFGResourceNodeBase* Candidate : CandidatePool)
                {
                    if (!IsValid(Candidate)) { continue; }
                    const bool bClassOk = !ExpectedClassPath.IsEmpty()
                        ? Candidate->GetClass()->GetPathName() == ExpectedClassPath
                        : Candidate->GetClass()->GetName() == LegacyClassToken;
                    if (!bClassOk) { continue; }
                    const float DSq = FVector::DistSquared(Candidate->GetActorLocation(), Anchor);
                    if (DSq < NearestSq) { NearestSq = DSq; Nearest = Candidate; }
                }
                if (Nearest)
                {
                    UE_LOG(LogNodeShuffle, Display,
                        TEXT("REHIDE: record='%s' anchor=%s[%s] res='%s' class='%s' -> no-match (cands=0 in %.0fcm; nearest same-class '%s' at %.0fcm) — twin stays visible this session"),
                        *Rec.VanillaNodePath, *Anchor.ToCompactString(), bTrueAnchor ? TEXT("true") : TEXT("legacy"),
                        *ResLabel, *ClassLabel, AdoptMatchRadiusCm, *Nearest->GetName(), FMath::Sqrt(NearestSq));
                }
                else
                {
                    UE_LOG(LogNodeShuffle, Display,
                        TEXT("REHIDE: record='%s' anchor=%s[%s] res='%s' class='%s' -> no-match (cands=0 in %.0fcm; no same-class candidate anywhere loaded) — twin stays visible this session"),
                        *Rec.VanillaNodePath, *Anchor.ToCompactString(), bTrueAnchor ? TEXT("true") : TEXT("legacy"),
                        *ResLabel, *ClassLabel, AdoptMatchRadiusCm);
                }
            }
        }
        else if (FNodeShuffleModule::AreDiagnosticsEnabled())
        {
            UE_LOG(LogNodeShuffle, Verbose,
                TEXT("REHIDE: record='%s' still no-match this pass (retry suppressed after first report)"),
                *Rec.VanillaNodePath);
        }
        return nullptr;
    }

    // MATCH. Identity is real (class + resource + 300 cm of our own recorded anchor, design §3.4) —
    // rebind unconditionally, even when the matched candidate turns out to be occupied: the funnel's
    // existing occupancy gate a few lines below the caller independently skips the hide for an occupied
    // node, same parity as an occupied vanilla original (design §3.6). Bind BEFORE any other record
    // this pass can claim the same actor (design §3.7).
    BoundCandidates.Add(Best);
    const float MatchedDistCm = FMath::Sqrt(BestSq);
    const FString OldPath = Rec.VanillaNodePath;
    Rec.VanillaNodePath = Best->GetPathName();
    Rec.TrueLocation = Best->GetActorLocation();
    if (MatchEntry)
    {
        MatchEntry->VanillaNodePath = Rec.VanillaNodePath;
        MatchEntry->OriginalTrueLocation = Rec.TrueLocation;
    }

    // P5: matched/occupied REHIDE lines (P1 decision ruling 4, revisit-in-P5) — gated. The occupancy
    // PEEK below is for log wording only (mirrors the funnel's own predicate; makes NO hide/skip
    // decision of its own — the funnel re-evaluates and enforces it independently moments later on the
    // same Node pointer, byte-identical to how it already treats a path-resolved original, reviewer
    // focus item (c)), so it is gated alongside the log rather than computed unconditionally. No new
    // throttle added here — a successful rematch is inherently rare (the record resolves by path
    // directly on every later pass unless it goes stale again), unlike the no-match branch above, which
    // keeps its existing once-per-record throttle unchanged.
    if (FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        AFGResourceNode* BestAsNode = Cast<AFGResourceNode>(Best);
        const bool bBestOccupied = Best->IsOccupied() || (BestAsNode && IsNodeOccupiedAnyway(BestAsNode));

        if (bBestOccupied)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("REHIDE: record='%s' anchor=%s[%s] res='%s' class='%s' -> candidate OCCUPIED (miner present) '%s' dist=%.0fcm (cands=%d) — rebound, hide skipped (occupied), will re-test"),
                *OldPath, *Anchor.ToCompactString(), bTrueAnchor ? TEXT("true") : TEXT("legacy"),
                *ResLabel, *ClassLabel, *Best->GetName(), MatchedDistCm, CandidatesInRadius);
        }
        else
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("REHIDE: record='%s' anchor=%s[%s] res='%s' class='%s' -> MATCHED '%s' dist=%.0fcm (cands=%d) — path rebound, entry rebound, falling through hide funnel"),
                *OldPath, *Anchor.ToCompactString(), bTrueAnchor ? TEXT("true") : TEXT("legacy"),
                *ResLabel, *ClassLabel, *Best->GetName(), MatchedDistCm, CandidatesInRadius);
        }
    }

    return Best;
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
    UE_LOG(LogNodeShuffle, Verbose,
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
        UE_LOG(LogNodeShuffle, Verbose,
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
                UE_LOG(LogNodeShuffle, Verbose,
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

        UE_LOG(LogNodeShuffle, Verbose,
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

FString ANodeShuffleSubsystem::ResolveSpawnNodeClassPath(const FNodeShuffleEntry& Entry) const
{
    // P3 (deckevict-1): the class an entry should ACTUALLY spawn from. Identical to Entry.NodeClassPath
    // unless that class was evicted this session -> nothing persists (design §2.2). Num()==0 fast path
    // mirrors the established idiom elsewhere in this file (e.g. VanillaNodeCache/SteadyHiddenOriginals
    // early-outs) — the common case (no eviction ever happened) is a single int compare.
    if (SpawnRefusedClassSubstitute.Num() == 0) { return Entry.NodeClassPath; }
    const FString* Sub = SpawnRefusedClassSubstitute.Find(Entry.NodeClassPath);
    return (Sub && !Sub->IsEmpty()) ? *Sub : Entry.NodeClassPath;
}

// ns-review-g G1 (Packet G, CRITICAL fix): see the declaration comment in NodeShuffleSubsystem.h for
// the full "why this replaces a live-actor census" argument. Walks EVERY active Layout entry (loaded or
// not -- Layout is the rolled, per-save source of truth, dealt once regardless of streaming), resolving
// each entry's resource->node-class pairing via the SAME functions the spawner itself uses
// (ResolveSpawnNodeClassPath -- Packet D's, 4320467, resource-keyed substitution logic -- and
// LoadClassByPath), so this never re-derives that keying, only reuses it. Form comes from
// UFGItemDescriptor::GetForm on the resolved resource class -- the same idiom already used throughout
// this file (e.g. PickSubstituteClass's gas check just below). An entry whose resource or node class
// cannot currently be resolved is skipped, not guessed -- it simply does not contribute a group this
// pass (no silent placeholder value).
void ANodeShuffleSubsystem::BuildManagedNodeGroupsFromLayout(TArray<FNodeShuffleManagedGroup>& OutGroups,
    int32& OutTotalActiveEntries, int32& OutUnresolvedEntries) const
{
    OutTotalActiveEntries = 0;
    OutUnresolvedEntries = 0;
    TMap<FString, int32> KeyToIndex;
    for (const FNodeShuffleEntry& Entry : Layout)
    {
        if (!Entry.bActive) { continue; }
        // ns-review-g2 F2: MANAGED means bIsNewNode. After a roll, Layout holds exactly two kinds of
        // entry (see ApplyLayout's contract comment): bIsNewNode entries, which are OUR relocated/spawned
        // nodes, and !bIsNewNode entries, which are OCCUPIED/PINNED originals ApplyLayout leaves "100%
        // UNTOUCHED" -- every UNOCCUPIED original was already converted to a bIsNewNode entry by the Hide
        // & Replace conversion, and the RemoveAll immediately after it drops the rest. Counting a pinned
        // original as a managed group would allow-list an extractor on the evidence of a node this mod
        // never touches, which is over-reach onto SF+'s own vanilla balance and nothing this pass claims
        // the right to change. Nothing is lost by excluding them: a pinned original already has an
        // extractor on it, so its placement question was settled before NodeShuffle ran.
        if (!Entry.bIsNewNode) { continue; }
        ++OutTotalActiveEntries;
        UClass* ResourceClass = LoadClassByPath(Entry.AssignedResourceClassPath);
        if (!ResourceClass) { ++OutUnresolvedEntries; continue; }
        UClass* NodeClass = LoadClassByPath(ResolveSpawnNodeClassPath(Entry));
        if (!NodeClass) { ++OutUnresolvedEntries; continue; }
        const int32 Form = (int32)UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(ResourceClass));
        const FString Key = NodeClass->GetPathName() + TEXT("|") + ResourceClass->GetPathName()
            + TEXT("|") + FString::FromInt(Form);
        if (const int32* ExistingIdx = KeyToIndex.Find(Key))
        {
            ++OutGroups[*ExistingIdx].Count;
        }
        else
        {
            KeyToIndex.Add(Key, OutGroups.Num());
            FNodeShuffleManagedGroup& G = OutGroups.AddDefaulted_GetRef();
            G.NodeClass = NodeClass;
            G.ResourceClass = ResourceClass;
            G.Form = Form;
            G.Count = 1;
        }
    }

    // ---- Packet H1 (ns-wells-h1): resource wells contribute managed groups too (design §2.6) ----
    //
    // *** READ THIS FIRST: SINCE H1b THESE GROUPS ARE LOAD-BEARING. THEY DECIDE ALLOW-LIST OUTCOMES. ***
    //
    // HISTORY, because the previous wording said the opposite and a reader who finds it in git history
    // needs to know which half changed. H1 emitted these groups as pure GROUNDWORK: ns-review-h1 F1
    // measured that design decisions 2 and 3 were mutually exclusive (2: managing wells auto-allow-lists
    // bamrenew's build_frqking_C / build_pressuresqtmk5_C "by the rule"; 3: leave the fracking crash guard
    // alone), because NodeShuffleAutoAllowExtractors.cpp skipped every AFGBuildableFrackingActivator /
    // AFGBuildableFrackingExtractor subclass BEFORE the group-matching loop was reached. Both bases are
    // UCLASS(Abstract), so every Pressurizer and Well Extractor is necessarily such a subclass. So the
    // groups were correct and unconsumable, and H1 said so.
    //
    // H1b (2026-07-31) RESOLVED THAT by narrowing the guard to a fail-closed PAIRING rule: a
    // fracking-derived machine is now allow-listed exactly when the matched group's node class IS the
    // fracking node type its kind requires AND its own mRestrictToNodeType is itself confined to that
    // hierarchy. The consequence for THIS function: the two groups emitted below are precisely the
    // evidence that rule consumes. Dropping either one now silently un-builds half a well — emit the
    // SATELLITE class and the Pressurizer has nothing to match; emit the CORE class and the Well
    // Extractor has nothing to match. See NodeShuffleAutoAllowExtractors.cpp's H1b block for the full
    // predicate and for why the two hologram hooks stay BLANKET regardless of what this census says.
    //
    // WHY THE EXTENSION IS STILL REQUIRED. The loop above requires bIsNewNode, and a well is never
    // spawned, so without this a managed well contributes NOTHING to the census — which would be wrong
    // independently of the guard. It feeds the SAME rule ("allow an extractor that natively accepts a
    // node type we manage") the same kind of evidence; nothing is hardcoded. The ns-review-g2 F2
    // exclusion above still holds exactly as written for its own population: only bManaged wells are
    // counted, and a PINNED well — one a player has already built on, which NodeShuffle leaves vanilla —
    // is excluded for precisely the reason a pinned original is.
    //
    // BOTH the satellite class AND the core class are emitted, deliberately, because they answer
    // different questions: a Resource Well Extractor restricts to the SATELLITE node type and a
    // Resource Well Pressurizer restricts to the CORE node type. Emitting only one would leave the
    // census half-right and produce a well nobody can finish building — a state that was merely LATENT
    // under H1's blanket guard and is REACHABLE now that H1b consumes these groups.
    //
    // The class paths come from the LIVE actors captured at roll time, never from hardcoded
    // /Game/FactoryGame/... paths, so a modded well class joins automatically.
    for (const FNodeShuffleWellEntry& Well : WellLayout)
    {
        // T16-followup (2026-08-08): A PINNED **RELOCATED** WELL STILL CONTRIBUTES ITS EVIDENCE.
        // The `!bManaged` skip that used to stand alone here inherited its rationale from the ordinary-
        // node population -- "a pinned well is one NodeShuffle leaves vanilla, so its placement question
        // was settled before we ran". That sentence is FALSE for a well H2 RELOCATED: we spawned it, we
        // moved it and we chose the resource it carries, so the ONLY reason a machine could be on it is
        // evidence THIS census emitted. Withdrawing it the moment a player pressurizes the core lands
        // BETWEEN the pressurizer and the satellite extractors -- i.e. mid-build -- which is
        // RollWellLayout's own RT-6 failure ("withdrawing the allow-list would leave the player holding
        // retyped wells they can no longer build on"). Unreachable before T16, because pinning never
        // fired for a relocated well at all.
        // NARROWEST POSSIBLE WIDENING: only bGroupPlaced entries. A pinned NON-relocated well is still
        // excluded, exactly as ns-review-g2 F2 decided, for exactly its own reason.
        const bool bContributesEvidence = Well.bManaged || (Well.bPinned && Well.bGroupPlaced);
        if (!bContributesEvidence) { continue; }
        // Keyed on what it ACTUALLY holds. The apply-time pin maintains AssignedResourceClassPath from
        // the core the pin was resolved against (T16), so for a pinned relocated well this names the
        // resource the player's well produces, not a layout value the world may have diverged from.
        if (Well.AssignedResourceClassPath.IsEmpty()) { continue; }
        ++OutTotalActiveEntries; // one well = one entry, not one per member
        UClass* WellResource = LoadClassByPath(Well.AssignedResourceClassPath);
        if (!WellResource) { ++OutUnresolvedEntries; continue; }
        const int32 WellForm = (int32)UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(WellResource));
        bool bAnyResolved = false;
        const FString WellNodeClassPaths[2] = { Well.SatelliteNodeClassPath, Well.CoreNodeClassPath };
        for (const FString& WellNodeClassPath : WellNodeClassPaths)
        {
            UClass* WellNodeClass = LoadClassByPath(WellNodeClassPath);
            if (!WellNodeClass) { continue; }
            bAnyResolved = true;
            const FString WellKey = WellNodeClass->GetPathName() + TEXT("|") + WellResource->GetPathName()
                + TEXT("|") + FString::FromInt(WellForm);
            if (const int32* ExistingIdx = KeyToIndex.Find(WellKey))
            {
                ++OutGroups[*ExistingIdx].Count;
            }
            else
            {
                KeyToIndex.Add(WellKey, OutGroups.Num());
                FNodeShuffleManagedGroup& G = OutGroups.AddDefaulted_GetRef();
                G.NodeClass = WellNodeClass;
                G.ResourceClass = WellResource;
                G.Form = WellForm;
                G.Count = 1;
            }
        }
        // Counted as unresolved only when NEITHER class resolved — a well that contributed at least one
        // real group is not an entry we silently dropped, which is what that honesty counter means.
        if (!bAnyResolved) { ++OutUnresolvedEntries; }
    }
}

FString ANodeShuffleSubsystem::PickSubstituteClass(const FString& RefusedPath, const FNodeShuffleEntry& Cause) const
{
    // deckevict-1 (design §2.4). Ordered ladder, first hit wins. Every candidate must (a) differ from
    // RefusedPath, (b) not itself already be a KEY in SpawnRefusedClassSubstitute (no A->B->A
    // ping-pong), (c) respect the MaxSubstituteChain depth cap (checked once, up front, below).
    const bool bDiag = FNodeShuffleModule::AreDiagnosticsEnabled();
    const FString RefusedLabel = FPackageName::ObjectPathToObjectName(RefusedPath);

    // Rung 1 (design §2.4.1 / R2 HARD GATE): GAS is relocate-only and bound to its own modded reactive
    // extractor (AlkaLib) — overriding its node class is exactly the exclude-gas-1 bug class. Tested via
    // the DESCRIPTOR form (same IsGasResourcePath idiom RollLayout's deck build uses at LoadClassByPath
    // + UFGItemDescriptor::GetForm), NEVER the ResourceForm byte — gas entries record ResourceForm as
    // FormSolid, so the byte would silently miss every gas entry and let one get substituted.
    UClass* AssignedRC = LoadClassByPath(Cause.AssignedResourceClassPath);
    const bool bIsGas = AssignedRC
        && UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(AssignedRC)) == EResourceForm::RF_GAS;
    if (bIsGas)
    {
        if (bDiag)
        {
            UE_LOG(LogNodeShuffle, Verbose,
                TEXT("deck-substitute: %s -> <none> (source=none; gas entry, never substituted)"), *RefusedLabel);
        }
        return FString();
    }

    // Chain-depth guard (design §2.4c / §2.8): walk backward from RefusedPath through EXISTING
    // substitution links. A class already MaxSubstituteChain links deep is the terminal end of an
    // already-exhausted chain (e.g. A->B, B->C: C is depth 2) — no further substitution, entries park.
    {
        int32 Depth = 0;
        FString Probe = RefusedPath;
        for (int32 i = 0; i < MaxSubstituteChain; i++)
        {
            const FString* UpstreamKey = SpawnRefusedClassSubstitute.FindKey(Probe);
            if (!UpstreamKey) { break; }
            Depth++;
            Probe = *UpstreamKey;
        }
        if (Depth >= MaxSubstituteChain)
        {
            if (bDiag)
            {
                UE_LOG(LogNodeShuffle, Verbose,
                    TEXT("deck-substitute: %s -> <none> (source=none; substitution chain exhausted at depth %d)"),
                    *RefusedLabel, Depth);
            }
            return FString();
        }
    }

    const uint8 Form = Cause.ResourceForm;
    const auto IsEligible = [&](const FString& Candidate) -> bool
    {
        return !Candidate.IsEmpty() && Candidate != RefusedPath && !SpawnRefusedClassSubstitute.Contains(Candidate);
    };

    // Rung 2: a /Game/ class we WATCHED spawn successfully this session — measured, not assumed.
    if (const FString* Proven = ProvenSpawnClassByForm.Find(Form))
    {
        if (IsEligible(*Proven))
        {
            if (bDiag)
            {
                UE_LOG(LogNodeShuffle, Verbose, TEXT("deck-substitute: %s -> %s (source=proven)"),
                    *RefusedLabel, *FPackageName::ObjectPathToObjectName(*Proven));
            }
            return *Proven;
        }
    }

    // Rung 3: scan the layout for any active bIsNewNode entry already on a /Game/ class of this form —
    // data-only fallback for when the refusing class gives up before anything of that form has spawned.
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bIsNewNode || !E.bActive || E.ResourceForm != Form) { continue; }
        if (!E.NodeClassPath.StartsWith(TEXT("/Game/"))) { continue; }
        if (!IsEligible(E.NodeClassPath)) { continue; }
        if (bDiag)
        {
            UE_LOG(LogNodeShuffle, Verbose, TEXT("deck-substitute: %s -> %s (source=layout-scan)"),
                *RefusedLabel, *FPackageName::ObjectPathToObjectName(E.NodeClassPath));
        }
        return E.NodeClassPath;
    }

    // Rung 4 (SOLID ONLY): our own C++ fallback class — always spawnable, ships its own UseBox +
    // RockMesh subobjects and mCanPlaceResourceExtractor=true in its ctor. Ranked LAST: its path is
    // /Script/NodeShuffle.NodeShuffleResourceNode, so bVanillaOrigin is false for it -> the placement-
    // gate re-assert is skipped. The component force-accept is still applied, so extractor snapping
    // should still work, but this is engine/hologram behaviour: graded ASSUMED, not provably provided
    // (design §2.4.4, R4 — the in-game check names this explicitly).
    if (Form != FormLiquid)
    {
        const FString Fallback = ANodeShuffleResourceNode::StaticClass()->GetPathName();
        if (IsEligible(Fallback))
        {
            if (bDiag)
            {
                UE_LOG(LogNodeShuffle, Verbose, TEXT("deck-substitute: %s -> %s (source=cpp-fallback)"),
                    *RefusedLabel, *Fallback);
            }
            return Fallback;
        }
    }

    // Rung 5: liquid with no proven/scanned liquid class — never hand a liquid resource a solid node
    // class (oil/liquid extractors bind to the node TYPE). Park instead of guessing.
    if (bDiag)
    {
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("deck-substitute: %s -> <none> (source=none; no eligible candidate for form %d)"),
            *RefusedLabel, Form);
    }
    return FString();
}

void ANodeShuffleSubsystem::EvictSpawnRefusingClass(const FString& RefusedPath, const FNodeShuffleEntry& Cause)
{
    // deckevict-1 (design §2.3, §2.9). IDEMPOTENT: the Nth give-up of the same class is a single hash
    // lookup + return — the FIRST eviction already swept and revived every entry carrying this class,
    // including ones that gave up earlier in the session.
    if (SpawnRefusedClassSubstitute.Contains(RefusedPath))
    {
        return;
    }
    if (SpawnRefusedClassSubstitute.Num() >= MaxEvictedClassesPerSession)
    {
        if (!bEvictionCapLogged)
        {
            bEvictionCapLogged = true;
            // Line B (design §5): ungated, Display, once for the whole session.
            UE_LOG(LogNodeShuffle, Display,
                TEXT("deck: eviction cap %d reached — further spawn-refusing classes stay parked this session"),
                MaxEvictedClassesPerSession);
        }
        return;
    }

    const FString Substitute = PickSubstituteClass(RefusedPath, Cause);
    SpawnRefusedClassSubstitute.Add(RefusedPath, Substitute); // may be empty -> "evicted, no substitute"

    const bool bDiag = FNodeShuffleModule::AreDiagnosticsEnabled();
    const FString RefusedLabel = FPackageName::ObjectPathToObjectName(RefusedPath);
    int32 Redealt = 0, Parked = 0;
    for (FNodeShuffleEntry& E : Layout)
    {
        if (!E.bIsNewNode || !E.bActive || E.bPinned) { continue; }  // occupied originals + pinned: untouched
        if (E.NodeClassPath != RefusedPath) { continue; }
        if (DormantThisSession.Contains(E.EntryGuid)) { continue; } // dormant entries do no work
        if (SpawnedNodes.Contains(E.EntryGuid)) { continue; }       // defensive: cannot happen, class never spawned
        if (Substitute.IsEmpty())
        {
            Parked++;
            continue; // stays parked (today's terminal state) — still counted for the log
        }
        // THE REVIVE: un-park + reset the fail budget so the entry re-attempts on the substitute class
        // via the resolver, next pass, with a fresh SpawnGiveUpAttempts budget.
        SpawnParkedThisSession.Remove(E.EntryGuid);
        SpawnFailCounts.Remove(E.EntryGuid);
        Redealt++;
        // Line D (design §5): diagnostics-gated, Verbose, one per rebound entry — proves §2.6's
        // "location untouched" claim from the log alone (settled=1, same Location as before).
        if (bDiag)
        {
            UE_LOG(LogNodeShuffle, Verbose,
                TEXT("deck-rebind: entry %s (%s) %s -> %s at %s (settled=%d, purity kept, budget reset)"),
                *E.EntryGuid.ToString(), *E.AssignedResourceClassPath, *RefusedLabel,
                *FPackageName::ObjectPathToObjectName(Substitute), *E.Location.ToCompactString(),
                E.bRayCasted ? 1 : 0);
        }
    }

    // Line A (design §5): ungated, Display, once per evicted class — the acceptance line. Contains the
    // literal "deck: evicted <class> after give-up, redealt N" the acceptance criterion requires.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("deck: evicted %s after give-up, redealt %d (substitute=%s, form=%d; %d entries parked — no substitute)"),
        *RefusedLabel, Redealt, Substitute.IsEmpty() ? TEXT("<none>") : *FPackageName::ObjectPathToObjectName(Substitute),
        Cause.ResourceForm, Parked);

    // Line C (design §5): ungated, Display, only for the parked-terminal case (no substitute exists) —
    // extra detail (gas flag) beyond line A.
    if (Substitute.IsEmpty())
    {
        UClass* AssignedRC = LoadClassByPath(Cause.AssignedResourceClassPath);
        const bool bIsGas = AssignedRC
            && UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(AssignedRC)) == EResourceForm::RF_GAS;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("deck: no viable substitute for %s (form=%d, gas=%d) — %d entries stay parked this session"),
            *RefusedLabel, Cause.ResourceForm, bIsGas ? 1 : 0, Parked);
    }
}

// ns-t38-pointathere: read-back of the cliff gate's slope threshold, which is a file-local constant in
// this translation unit and therefore unreachable from NodeShufflePointAtHere.cpp. Read-only: it runs
// nothing and changes nothing, and exists so a diagnostic prints this build's constant rather than a
// number typed into a log string.
float ANodeShuffleSubsystem::GetCliffSlopeDegForDiag() const
{
    return CliffSlopeDeg;
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
        TEXT("HERE: player at X=%.0f Y=%.0f Z=%.0f | inWaterVolume=%d belowDepthFloor=%d | layout=%d spawn-entries, %d live spawned, %d water-locked this session, %d node classes evicted this session"),
        P.X, P.Y, P.Z,
        IsPointInWater(P) ? 1 : 0, (P.Z < DeepWaterFloorZ) ? 1 : 0,
        Layout.Num(), SpawnedNodes.Num(), WaterLockedThisSession.Num(), SpawnRefusedClassSubstitute.Num());
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
        // ns-t35-gatereach: the result is now held in a local so the enclosure block below can say
        // WHICH point it tested without tracing a second time. The call, its arguments and the branch
        // it feeds are unchanged.
        const bool bHaveSettled = RaycastGroundAt(P, P.Z, Pawn, nullptr, SlopeLoc, SlopeRot, bSlopeWater,
                                                  /*bShortTrace=*/false, &bSlopeCliff, &SlopeN);
        if (bHaveSettled)
        {
            const float SlopeHereDeg = FMath::RadiansToDegrees(
                FMath::Acos(FMath::Clamp(static_cast<float>(SlopeN.Z), -1.0f, 1.0f)));
            UE_LOG(LogNodeShuffle, Display,
                TEXT("HERE: ground slope at your feet = %.1f deg — cliff gate (%.0f deg) %s; water=%d"),
                SlopeHereDeg, CliffSlopeDeg,
                bSlopeCliff ? TEXT("WOULD REJECT settles here") : TEXT("accepts settles here"),
                bSlopeWater ? 1 : 0);
        }

        // ns-t35-gatereach: THE ENCLOSURE GATE, RUN WHERE YOU ARE STANDING, WITH ITS WORKING PRINTED.
        // docs/TECH-DEBT.md T35: this gate reported zero rejections across a whole session while the
        // author stood at a relocated core buried in a rock, and a reject count alone cannot say
        // whether the gate passed that spot or never reached it. This runs the SAME
        // ANodeShuffleSubsystem::IsSpotEnclosed that both placement paths call -- not a copy of it, so
        // there is nothing here that can disagree with the gate -- and prints every ray it cast.
        // WHICH POINT IS TESTED: the placement paths test the SETTLED location, so when the ground
        // trace above hit, this tests that impact point; when it missed, it tests the player's own
        // position instead. The line says which, every time, because they are different questions.
        {
            const FVector TestAt = bHaveSettled ? SlopeLoc : P;
            TArray<FNodeShuffleEnclosureRay> Rays;
            int32 Blocked = 0, Total = 0, Threshold = -1;
            // ns-t36-probefix: the pawn goes on the probe's ignore list. T36: a character blocks
            // ECC_WorldStatic and this call previously excluded nothing, so every ray died inside the
            // player's own capsule and the verdict measured the player rather than the world. Only THIS
            // caller passes it; the placement paths still pass nothing.
            const bool bEnclosed = IsSpotEnclosed(TestAt, Blocked, Total, &Rays, &Threshold, Pawn);
            // ns-t39-wellprobe (T38 cold review F1): THE PROBE-EYE READING, PRINTED BEFORE THE VERDICT.
            // Added to all three probe commands rather than only to the new one: a check present on one
            // probe and absent on its siblings is this project's most-repeated defect.
            FNodeShuffleProbeEyeReading Eye;
            const bool bEyeInside = IsProbeEyeInsideSolidForDiag(TestAt, Pawn, Eye);
            UE_LOG(LogNodeShuffle, Display,
                TEXT("HERE: probe eye inside solid geometry: %s. The eye sits at %s, which is where the ")
                TEXT("enclosure predicate below starts its rays. One sphere of radius %.0f cm was ")
                TEXT("overlapped there on the same channel those rays are cast on, with the same one ")
                TEXT("actor on the ignore list; it returned %d result(s), %d of which report blocking on ")
                TEXT("that channel: %s. WHY THIS COMES FIRST: an eye that starts inside a blocking body ")
                TEXT("makes every ray below terminate at once, and the verdict then reads as a confident ")
                TEXT("full refusal containing no terrain. A negative reading is a statement about this ")
                TEXT("channel at this radius and is not a claim that the eye stands in open air. This ")
                TEXT("line states no cause."),
                !Eye.bRan ? TEXT("UNMEASURED -- the overlap did not run, so neither answer is reported")
                          : (bEyeInside ? TEXT("YES") : TEXT("NO")),
                *Eye.Eye.ToCompactString(), Eye.ProbeRadiusCm,
                Eye.OverlapResults, Eye.BlockingOverlaps, *Eye.BlockingActors);
            UE_LOG(LogNodeShuffle, Display,
                TEXT("HERE: enclosure probe exclusions -- %d of the 1 actor this command has to offer ")
                TEXT("(the pawn it resolved for you, named here) was handed to the trace's ignore list ")
                TEXT("for the rays below: %s. This line reports what was passed IN to the predicate. ")
                TEXT("Whether any ray still reported a hit on that actor is a separate question and the ")
                TEXT("ray lines below answer it -- each names the actor its own hit belonged to. The ")
                TEXT("placement paths pass no exclusion at all, so a spot this command calls clear is ")
                TEXT("not thereby a spot they would call clear while a pawn stands on it."),
                (Pawn != nullptr) ? 1 : 0,
                (Pawn != nullptr) ? *Pawn->GetName() : TEXT("<none: no pawn resolved>"));
            for (int32 i = 0; i < Rays.Num(); ++i)
            {
                const FNodeShuffleEnclosureRay& R = Rays[i];
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("HERE: enclosure ray %d of %d, bearing %.0f deg: %s%s%s"),
                    i + 1, Rays.Num(), R.BearingDeg,
                    R.bBlocked ? TEXT("BLOCKED") : TEXT("clear"),
                    R.bBlocked ? *FString::Printf(TEXT(" at %.0f cm by "), R.HitDistanceCm) : TEXT(""),
                    R.bBlocked ? *R.HitActor : TEXT(""));
            }
            UE_LOG(LogNodeShuffle, Display,
                TEXT("HERE: ENCLOSURE GATE at %s (%s) — %d of %d rays blocked, and this build refuses a ")
                TEXT("spot at %d or more blocked, so the verdict here is %s. The rays above are the ")
                TEXT("ones this call cast: horizontal, evenly spaced in bearing, from a fixed height ")
                TEXT("above the tested point, each one reaching a fixed distance. The threshold and the ")
                TEXT("ray count printed here were read back from the predicate this run, not typed into ")
                TEXT("this line. WHAT THIS SHAPE OF TEST CANNOT SEE, by construction and not by ")
                TEXT("observation: anything that blocks beyond one ray's reach, anything above or below ")
                TEXT("the ray height, and any gap that falls between two bearings. The tested point is ")
                TEXT("%+.0f m in Z from your own feet; this line reports that difference and does not ")
                TEXT("explain it. WHICH PLACEMENT POINT THIS DOES AND DOES NOT CORRESPOND TO, since an ")
                TEXT("earlier build of this line simply called it the point a placement gate tests: the ")
                TEXT("WELL path settles its probes with the same long downward ground trace this command ")
                TEXT("just ran, but from the probe's own XY and start Z, not yours; the SOLID-NODE path ")
                TEXT("settles through RaycastSettle instead, which uses the short trace for an ")
                TEXT("underground entry and may spiral the entry to a different XY on the surface before ")
                TEXT("any gate is applied. So this is the point a placement gate would test only when ")
                TEXT("those inputs coincide with yours, and this command does not check that. This is ")
                TEXT("the same function both placement paths call, but ")
                TEXT("ns-t36-probefix means it is NOT called with the same arguments here: this command ")
                TEXT("hands it the pawn to ignore and the placement paths hand it nothing, so a spot ")
                TEXT("this line calls clear is a statement about the terrain and not a prediction of ")
                TEXT("what a placement probe would return while a character stands there. WHETHER a path runs ")
                TEXT("it at all is a different question and this line does not answer it -- the ")
                TEXT("gate-reached counters on the census lines are what do. It states no cause: each ")
                TEXT("ray reports the trace ")
                TEXT("it made and nothing about why the world is shaped that way. A ray count of zero ")
                TEXT("above means the predicate cast no rays at all and the verdict is not a ")
                TEXT("measurement of this spot."),
                *TestAt.ToCompactString(),
                // ns-t36-probefix (T35 cold review F2): the old text here claimed this WAS "the point a
                // placement gate tests", and that is not true of the solid-node path. What is true is
                // stated instead, per path, from the source: the well path settles with the same long
                // downward RaycastGroundAt this command just ran, but from the probe's own XY and start
                // Z; the node path settles with RaycastSettle, which uses the SHORT trace for an
                // underground entry and may spiral the entry to a different XY on the surface before any
                // gate is applied. So this point is a placement gate's point only when the placement
                // probe's XY and start Z happen to coincide with yours, which this command cannot check.
                bHaveSettled ? TEXT("the point a long downward ground trace from your position landed on")
                             : TEXT("your own position: that ground trace found nothing to settle on"),
                Blocked, Total, Threshold,
                bEnclosed ? TEXT("ENCLOSED (this predicate, called with this command's exclusion, ")
                            TEXT("refuses this point)")
                          : TEXT("not enclosed (this predicate, called with this command's exclusion, ")
                            TEXT("does not refuse this point)"),
                // ns-t36-probefix: measured here from the two positions this call already holds. The
                // 21 m gap seen on the previous build is UNEXPLAINED and this number does not explain
                // it; it only stops a reader having to compute it from two other lines.
                (TestAt.Z - P.Z) / 100.0);
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
        // coexist-veto-1 FIX 4a: a tombstoned (externally destroyed, dormant-this-session) entry
        // would otherwise read as a plain "no-actor" — indistinguishable from not-yet-streamed.
        if (DormantThisSession.Contains(E.EntryGuid)) { Flags += TEXT("|DORMANT"); }
        // P3 (deckevict-1): surface spawn-parked / class-substituted state so NodeShuffle.Here can
        // diagnose a dead spot without a second launch.
        if (SpawnParkedThisSession.Contains(E.EntryGuid)) { Flags += TEXT("|SPAWN-PARKED"); }
        if (ResolveSpawnNodeClassPath(E) != E.NodeClassPath) { Flags += TEXT("|CLASS-SUBBED"); }
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
    // NEAREST RELOCATED WELL (item 4). Finding one previously meant reading WELLH2-PLACED out of the log
    // and computing distances by hand. The well layout is right here and the player's position is
    // already known, so the command can answer it directly. Only a group that is actually PLACED is
    // reported: bGroupPlaced && bPlacementClaimLive (invariant A3 binds the second to a non-zero
    // PlacedCoreLocation, and the third test below is that invariant asserted rather than trusted). A
    // group merely flagged bRelocate has no destination a player can walk to, so printing a distance for
    // it would be a fiction.
    {
        // ns-t39-wellprobe: the loop that used to sit here is now ANodeShuffleSubsystem::
        // FindNearestPlacedWellForDiag (NodeShuffleWellProbe.cpp), because NodeShuffle.WellProbe needs the
        // same answer and two copies of one question that must agree is T26's defect one indirection
        // later. Lifted unchanged in what it tests, which group it picks and how it breaks a tie; this
        // line reads the same three counters the loop produced.
        int32 WellsTotal = 0, WellsRelocateFlagged = 0, WellsPlaced = 0;
        double NearestWellDistCm = 0.0;
        const FNodeShuffleWellEntry* NearestWell =
            FindNearestPlacedWellForDiag(P, WellsTotal, WellsRelocateFlagged, WellsPlaced, NearestWellDistCm);
        if (!NearestWell)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("HERE: nearest relocated well — NONE. %d well group(s) in the layout, %d flagged for ")
                TEXT("relocation, %d actually PLACED (bGroupPlaced && bPlacementClaimLive && a non-zero ")
                TEXT("PlacedCoreLocation). With 0 placed there is no relocated well ANYWHERE in this save, ")
                TEXT("which is a different statement from one being far away — that is why this prints a ")
                TEXT("sentence instead of a distance of 0."),
                WellsTotal, WellsRelocateFlagged, WellsPlaced);
        }
        else
        {
            const FVector D = NearestWell->PlacedCoreLocation - P;
            // Turn-from-current-facing rather than a compass heading: the map's north convention is not
            // something this code can verify, and a wrong compass word is worse than none. Yaw and the
            // world delta are both measured.
            const double Turn = FRotator::NormalizeAxis(
                FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)) - Pawn->GetActorRotation().Yaw);
            UE_LOG(LogNodeShuffle, Display,
                TEXT("HERE: nearest relocated well = '%s' res=%s at %s — %.0f m away, dz=%+.0f m, world ")
                TEXT("delta dX=%+.0f m dY=%+.0f m; from where you are standing and facing, turn %+.0f deg ")
                TEXT("and go. %d of %d well group(s) in the layout are PLACED (%d flagged for relocation); ")
                TEXT("this group has %d captured satellite(s) of %d. MEASURED: the saved ")
                TEXT("PlacedCoreLocation, your pawn's location and yaw. NOT MEASURED: whether that ")
                TEXT("group's actors are streamed in, dressed, or buildable right now — WELLH2B-APPLY and ")
                TEXT("WELLH2B-COLLISION are the lines for that."),
                *ShortName(NearestWell->CorePath), *ShortName(NearestWell->AssignedResourceClassPath),
                *NearestWell->PlacedCoreLocation.ToCompactString(),
                NearestWellDistCm / 100.0, D.Z / 100.0, D.X / 100.0, D.Y / 100.0, Turn,
                WellsPlaced, WellsTotal, WellsRelocateFlagged,
                NearestWell->CapturedSatelliteCount, NearestWell->Satellites.Num());
        }
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

    // knowledge-2 item 3: the truth dump needs player positions (150 m gate). Gathered only when
    // diagnostics are on — the heal logic itself needs none.
    const bool bExtractorDiag = FNodeShuffleModule::AreDiagnosticsEnabled();
    TArray<FVector> DiagPlayers;
    if (bExtractorDiag)
    {
        for (FConstPlayerControllerIterator PIt = GetWorld()->GetPlayerControllerIterator(); PIt; ++PIt)
        {
            if (const APlayerController* Pc = PIt->Get())
            {
                if (const APawn* Pawn = Pc->GetPawn()) { DiagPlayers.Add(Pawn->GetActorLocation()); }
            }
        }
    }

    for (TActorIterator<AFGBuildableResourceExtractorBase> It(GetWorld()); It; ++It)
    {
        AFGBuildableResourceExtractorBase* Extractor = *It;
        const FVector Loc = Extractor->GetActorLocation();

        // The live spawned node AT this extractor's location (the reassociation proximity rule).
        // Resolved lazily — healthy bound extractors never pay the SpawnedNodes scan.
        bool bLocationNodeResolved = false;
        AFGResourceNode* LocationNode = nullptr;
        auto ResolveLocationNode = [&]() -> AFGResourceNode*
        {
            if (!bLocationNodeResolved)
            {
                bLocationNodeResolved = true;
                for (auto& Pair : SpawnedNodes)
                {
                    AFGResourceNode* Node = Pair.Value;
                    if (IsValid(Node)
                        && FVector::DistSquared(Node->GetActorLocation(), Loc) < FMath::Square(ExtractorSnapDistance))
                    {
                        LocationNode = Node;
                        break;
                    }
                }
            }
            return LocationNode;
        };

        UObject* BoundObj = Extractor->GetExtractableResource().GetObject();
        IFGExtractableResourceInterface* BoundRes = Cast<IFGExtractableResourceInterface>(BoundObj);
        AFGResourceNode* BoundNode = Cast<AFGResourceNode>(BoundObj);
        // knowledge-2 item 3 (WIDENED detection — the res==null criterion alone missed the live
        // "Invalid"-output miner): broken states are (1) STALE — an object is bound but IsValid
        // fails on it (destroyed/pending-kill actor behind the interface); (2) RES-NULL — bound and
        // valid but the extract resource resolves null; (3) WRONG-NODE — bound to a valid node that
        // is nowhere near this extractor while OUR live spawned node sits under it (mid-war save
        // wrote a binding to a node instance that no longer stands here). A healthy extractor on an
        // untouched vanilla node hits none of these: its bound node is valid, resolves a resource,
        // and stands within snap range — we never steal a legitimate binding.
        const bool bStale = BoundObj != nullptr && !IsValid(BoundObj);
        const bool bResNull = BoundObj != nullptr && !bStale && BoundRes && BoundRes->GetResourceClass() == nullptr;
        const bool bBoundFar = !bStale && BoundNode != nullptr
            && FVector::DistSquared(BoundNode->GetActorLocation(), Loc) > FMath::Square(ExtractorSnapDistance);
        const bool bWrongNode = bBoundFar && ResolveLocationNode() != nullptr && BoundNode != LocationNode;

        // Truth dump: diag-gated, once per extractor per session, within 150 m of a player — ground
        // truth for the NEXT session even if the heal criteria still miss the real broken state.
        if (bExtractorDiag && !ExtractorsDumped.Contains(Extractor))
        {
            bool bNear = false;
            for (const FVector& P : DiagPlayers)
            {
                if (FVector::DistSquared(P, Loc) < FMath::Square(15000.0f)) { bNear = true; break; }
            }
            if (bNear)
            {
                ExtractorsDumped.Add(Extractor);
                UClass* DumpRes = (BoundRes && !bStale) ? BoundRes->GetResourceClass().Get() : nullptr;
                ResolveLocationNode();
                UE_LOG(LogNodeShuffle, Display, TEXT("extractor %s: bound=%s res=%s node-at-location=%s"),
                    *Extractor->GetName(),
                    BoundObj == nullptr ? TEXT("none") : bStale ? TEXT("INVALID") : *BoundObj->GetName(),
                    DumpRes ? *DumpRes->GetName() : TEXT("null"),
                    LocationNode == nullptr ? TEXT("none")
                        : LocationNode == BoundNode ? TEXT("match") : *FString::Printf(TEXT("mismatch(%s)"), *LocationNode->GetName()));
            }
        }

        if (BoundObj != nullptr)
        {
            // knowledge-1 item 2b + knowledge-2 widening: heal via the construct path's setter
            // (SetResourceNode — binds AND claims, re-running the OnExtractableResourceSet
            // derivation). Critical without a rebuild: SF+ removed vanilla miners from the build
            // menu, so existing ones are irreplaceable. Once per extractor per session.
            if ((bStale || bResNull || bWrongNode) && !ExtractorsHealed.Contains(Extractor))
            {
                // Prefer the live node at the extractor's location; a res-null binding with no
                // location node re-derives on its own bound node (the original knowledge-1 action).
                AFGResourceNode* HealTarget = ResolveLocationNode();
                if (!HealTarget && bResNull) { HealTarget = BoundNode; }
                if (HealTarget)
                {
                    ExtractorsHealed.Add(Extractor);
                    Extractor->SetResourceNode(HealTarget);
                    UClass* HealedClass = nullptr;
                    if (IFGExtractableResourceInterface* HealedRes = Cast<IFGExtractableResourceInterface>(HealTarget))
                    {
                        HealedClass = HealedRes->GetResourceClass().Get();
                    }
                    UE_LOG(LogNodeShuffle, Display, TEXT("relink: refreshed resource binding on %s -> %s (%s)"),
                        *Extractor->GetName(), HealedClass ? *HealedClass->GetName() : TEXT("<still null>"),
                        bStale ? TEXT("stale binding") : bResNull ? TEXT("null resource") : TEXT("wrong node"));
                }
            }
            continue;
        }
        if (AFGResourceNode* Orphan = ResolveLocationNode())
        {
            // knowledge-1 item 2a: re-link via SetResourceNode — the construct path's setter
            // ("set as our current, also claiming it"), which re-derives the extract-resource
            // binding the same way a freshly-built extractor does. The old bare
            // SetExtractableResource bound the interface but skipped the node-claim half.
            Extractor->SetResourceNode(Orphan);
            UE_LOG(LogNodeShuffle, Verbose, TEXT("Re-associated extractor %s with new node"), *Extractor->GetName());
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
    // scanregen-1 (P2 §4 touch-point 4): mark this pass as having refreshed — the debounce the new
    // knowledge-unlock consume point in RefreshTick reads (bScannerRefreshedThisPass). Existing
    // callers (the live-reroll branch, ApplyLayout's bChangedWorld tail) are unaffected: this is a
    // pure addition inside the function body, their call sites stay byte-identical.
    bScannerRefreshedThisPass = true;

    // scanregen-1 root-cause discriminator (P2 §5 point 4, diagnostics-gated, zero new imports): only
    // meaningful when THIS call is knowledge-triggered (a pending unlock batch is still unconsumed at
    // the moment we're called) — the re-roll/bChangedWorld callers are unrelated to any unlock, so the
    // gate keeps their calls silent on this line. K==0 confirms the cluster cache was the stale thing
    // (this fix's premise); K>0 would mean the real gate is the scanner's own selection list/UI, not
    // the cache (design alternative D) — discriminated in this SAME launch instead of a second one.
    const bool bRunCensus = FNodeShuffleModule::AreDiagnosticsEnabled()
        && bScannerClusterRefreshPending && ScanRegenUnlockedClasses.Num() > 0;
    int32 CensusTotal = 0, CensusAlreadyCarrying = 0;

    int32 ScannersInvalidated = 0;
    for (TActorIterator<AFGResourceScanner> It(GetWorld()); It; ++It)
    {
        if (bRunCensus)
        {
            // Friend read (AccessTransformers): walk the cluster list BEFORE flipping it stale below.
            for (const FNodeClusterData& Cluster : It->mNodeClusters)
            {
                CensusTotal++;
                if (ScanRegenUnlockedClasses.Contains(Cluster.ResourceDescriptor)) { CensusAlreadyCarrying++; }
            }
        }
        // Friend access (AccessTransformers): force cluster rebuild on next use.
        It->mNodeClustersUpToDate = false;
        ScannersInvalidated++;
    }
    if (bRunCensus)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("SCANREGEN: pre-invalidate cluster census total=%d, clusters already carrying newly-unlocked descriptor=%d"),
            CensusTotal, CensusAlreadyCarrying);
    }

    // Radar towers cache the resources they scanned; without a re-scan they keep showing the OLD
    // (pre-shuffle) resource set on the map until rebuilt. Force every built radar tower to re-scan so
    // the map reflects the relocated/retyped/deactivated nodes. (This function previously only touched
    // resource scanners despite its name — a built radar tower stayed stale. Mirrors Resource Roulette.)
    // COST NOTE: ScanForResources walks the world's resource nodes AND pushes a representation update per
    // found node (network-replicated). This function is gated by bChangedWorld in ApplyLayout, so it only
    // runs on actual world mutations (rolls / nodes settling), never in steady state — keep it that way;
    // do NOT call this from a per-tick hot path.
    int32 TowersRescanned = 0;
    for (TActorIterator<AFGBuildableRadarTower> It(GetWorld()); It; ++It)
    {
        AFGBuildableRadarTower* Tower = *It;
        if (!IsValid(Tower)) { continue; }
        Tower->ClearScannedResources();
        Tower->ScanForResources();
        TowersRescanned++;
    }

    // P5 (addenda item 10): SCANREGEN line #3 (P2 design §5) — gated. Phase C evidence showed this
    // fires on EVERY call to this function (the live-reroll branch at :297, RefreshTick's consume
    // point, AND ApplyLayout's bChangedWorld tail at :1945 — frequent during a world-settling storm),
    // not only the knowledge-unlock path this comment originally described. "N=0" is still the single
    // most valuable line under diagnostics: it means no AFGResourceScanner actor existed at this
    // moment, so the invalidation was a no-op and any staleness lives elsewhere (P2 design §5 point 3).
    if (FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("SCANREGEN: invalidated %d scanner cluster cache(s), re-scanned %d radar tower(s)"),
            ScannersInvalidated, TowersRescanned);
    }
}

bool ANodeShuffleSubsystem::UnlockModdedScannerKnowledge()
{
    // knowledge-1 item 1 (THE headline fix). SF+'s Modular Miner hologram (KLib) gates placement on
    // AKLUnlockSubsystem::HasInformationAboutOre == AFGUnlockSubsystem::GetScannableResources()
    // .Contains(Desc) — the STOCK scanner-unlock list. Shuffled modded resources whose own unlock
    // schematics never ran in this save (AllMinable's likely fell to SF+'s content remover) are
    // therefore machine-unminable even though the nodes work. Register every DISTINCT modded
    // resource the shuffle actively manages; the backing list is UPROPERTY(SaveGame, Replicated) so
    // the unlock persists and replicates. Vanilla resources are NEVER touched — their scanner
    // unlocks are progression. Idempotent (Contains gate); config-gated, default ON.
    //
    // knowledge-2 item 2 (CRASH-PROOF ORDERING — live crash: fgcheckf in AKLMMBuildableMiner::
    // BeginPlay, "No MinerInfo (DataAsset) found for esc_CateriumIngot_C"). KLib resolves a second
    // registry beyond scanner knowledge: KAPI's per-ore UKAPIModularMinerDescription in
    // UKAPIDataAssetSubsystem::mMinerMapping, and its BeginPlay hard-asserts on a miss. So when
    // KAPI is present, MinerInfo provisioning runs FIRST and only ores that now HAVE a map entry
    // (pre-existing or freshly provided) get the scanner unlock: knowledge implies MinerInfo, and
    // the assert is unreachable through us. Ores unlocked by knowledge-1 in existing saves are
    // covered the same way: their scanner-known state persists (SaveGame), and provisioning at
    // every load puts their map entry in place long before any placement can BeginPlay a miner.
    if (!HasAuthority()) { return true; }
    const FNodeShuffleConfigStruct Config = FNodeShuffleConfigStruct::GetActiveConfig(this);
    if (!Config.UnlockModdedKnowledge) { return true; }

    const bool bDiag = FNodeShuffleModule::AreDiagnosticsEnabled();

    // Collect ALL distinct managed modded descriptor classes first (active entries incl. pinned) —
    // including already-scanner-known ores: knowledge-2 provisioning must cover those too (the
    // user's save already knows esc_CateriumIngot_C from knowledge-1; only the map entry saves it).
    TSet<FString> SeenPaths;
    TArray<UClass*> Managed;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bActive) { continue; }
        const FString& Path = E.AssignedResourceClassPath;
        if (Path.IsEmpty() || Path.StartsWith(TEXT("/Game/"))) { continue; } // vanilla: never touched
        bool bSeen = false;
        SeenPaths.Add(Path, &bSeen);
        if (bSeen) { continue; }
        UClass* ResClass = LoadClassByPath(Path);
        if (ResClass && ResClass->IsChildOf(UFGResourceDescriptor::StaticClass())) { Managed.Add(ResClass); }
    }
    if (Managed.Num() == 0) { return true; }

    // knowledge-2 item 1: MinerInfo provisioning BEFORE any unlock — and BEFORE the unlock-subsystem
    // gate below, so the crash-critical map entries land even on the EARLY PostLoadGame pass where
    // the unlock subsystem may not exist yet. Defers the whole pass (return false -> caller retries)
    // while KAPI's data-asset scan hasn't populated yet. Idempotent on retries.
    TSet<UClass*> WithMinerInfo;
    bool bFilterUnlocks = false;
    if (!ProvideKAPIMinerInfo(Managed, WithMinerInfo, bFilterUnlocks))
    {
        return false;
    }

    const AFGGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFGGameState>() : nullptr;
    AFGUnlockSubsystem* Unlocks = GS ? GS->GetUnlockSubsystem() : nullptr;
    if (!Unlocks)
    {
        // Normal on the early PostLoadGame pass (the unlock subsystem restores in the same load
        // flow) — NOT latched: the RefreshTick pass completes the scanner-unlock half in ~70 s.
        if (bDiag)
        {
            UE_LOG(LogNodeShuffle, Verbose, TEXT("knowledge: unlock subsystem not ready — provisioning done, unlocks retry next pass"));
        }
        return false;
    }

    // knowledge-3 item c (DEDUP — the 42-resource unlock line re-fired every load and duplicate
    // pairs accumulated in the save): UnlockScannableResource writes the PAIRS array
    // (mScannableResourcesPairs) while GetScannableResources() reads a different view, so a
    // pairs-only unlock can look "unknown" forever. Check BOTH views and unlock only when the
    // descriptor is absent from both — the once-per-save Display line then goes quiet on later
    // loads as originally intended.
    const TArray<TSubclassOf<UFGResourceDescriptor>> Known = Unlocks->GetScannableResources();
    const TArray<FScannableResourcePair> KnownPairs = Unlocks->GetScannableResourcePairs();
    const auto IsScannerKnown = [&Known, &KnownPairs](UClass* Res)
    {
        if (Known.Contains(Res)) { return true; }
        for (const FScannableResourcePair& Pair : KnownPairs)
        {
            if (Pair.ResourceDescriptor.Get() == Res) { return true; }
        }
        return false;
    };
    int32 Unlocked = 0;
    FString UnlockedNames;
    // scanregen-1 (P2 review F1, verbatim fold): build the batch LOCALLY and commit to the member ONLY
    // when a batch actually lands (Unlocked>0), so a completed no-op pass (Unlocked==0, e.g. a second
    // re-roll toggle inside the pending window) can never wipe the list of a still-pending EARLIER
    // batch (§9 amendment: pending may survive several ticks before consume). The member therefore
    // always corresponds to the batch that set bScannerClusterRefreshPending -- it is only ever READ
    // while that flag is true.
    TArray<TSubclassOf<UFGResourceDescriptor>> NewlyUnlockedThisPass;
    for (UClass* ResClass : Managed)
    {
        if (bFilterUnlocks && !WithMinerInfo.Contains(ResClass))
        {
            // No KAPI MinerInfo could be provided for this ore -> unlocking it would arm the KLib
            // BeginPlay assert on first placement. Withheld (retries next load).
            if (bDiag)
            {
                UE_LOG(LogNodeShuffle, Verbose, TEXT("knowledge: %s withheld — no KAPI MinerInfo available/providable"),
                    *ResClass->GetName());
            }
            continue;
        }
        if (IsScannerKnown(ResClass))
        {
            if (bDiag)
            {
                UE_LOG(LogNodeShuffle, Verbose, TEXT("knowledge: %s already scanner-known — skipped"), *ResClass->GetName());
            }
            continue;
        }
        // Two-arg pair ctor on purpose: the one-arg ctor's geyser branch is dead code (it assigns
        // Geyser then unconditionally overwrites with Node) — mirror the MIGRATION INTENT instead:
        // geyser-descriptor subclasses register as Geyser-type scannables, everything else as Node.
        const EResourceNodeType PairType = ResClass->IsChildOf(UFGResourceDescriptorGeyser::StaticClass())
            ? EResourceNodeType::Geyser : EResourceNodeType::Node;
        Unlocks->UnlockScannableResource(FScannableResourcePair(
            TSubclassOf<UFGResourceDescriptor>(ResClass), PairType));
        Unlocked++;
        UnlockedNames += (UnlockedNames.IsEmpty() ? TEXT("") : TEXT(", "));
        UnlockedNames += ResClass->GetName();
        NewlyUnlockedThisPass.Add(TSubclassOf<UFGResourceDescriptor>(ResClass)); // scanregen-1
        if (bDiag)
        {
            UE_LOG(LogNodeShuffle, Verbose, TEXT("knowledge: scanner-unlocked %s (type=%s)"),
                *ResClass->GetName(), PairType == EResourceNodeType::Geyser ? TEXT("Geyser") : TEXT("Node"));
        }
    }
    if (Unlocked > 0)
    {
        // Ungated by design: one line per load (usually only the FIRST load changes anything — the
        // list is SaveGame, so later loads skip via Contains).
        UE_LOG(LogNodeShuffle, Display, TEXT("knowledge: scanner-unlocked %d modded resource(s): %s"),
            Unlocked, *UnlockedNames);
        // scanregen-1 (P2 §4 touch-point 2): this is the ONLY place that knows a batch actually
        // changed the scanner-unlock list (Unlocked counts past the IsScannerKnown dedup above, so a
        // reload of an already-unlocked save sets nothing here). Record-only — RefreshTick's consume
        // point acts, never here (this function's OTHER caller is PostLoadGame_Implementation, mid
        // save-load, before actor settling — see P2 design §2.1).
        // P2 review F1 (verbatim fold): commit the batch to the member HERE, alongside the flag that
        // marks it pending, so the member and the flag always describe the same batch (see the
        // declaration comment above).
        ScanRegenUnlockedClasses = MoveTemp(NewlyUnlockedThisPass);
        bScannerClusterRefreshPending = true;
        // P5: SCANREGEN line #1 (P2 design §5) — gated; the "knowledge:" line above stays ungated
        // (it is the one-per-load acceptance line for the unlock itself), this one is the internal
        // scanner-refresh trigger detail and is redundant with it outside diagnostics.
        if (FNodeShuffleModule::AreDiagnosticsEnabled())
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("SCANREGEN: knowledge unlocked %d resource(s) [%s] -> scanner cluster refresh PENDING"),
                Unlocked, *UnlockedNames);
        }
    }
    return true;
}

bool ANodeShuffleSubsystem::ProvideKAPIMinerInfo(const TArray<UClass*>& ManagedModded,
                                                 TSet<UClass*>& OutWithMinerInfo, bool& bOutFilterUnlocks)
{
    // knowledge-2 item 1: runtime MinerInfo provisioning, PURE REFLECTION — the main module keeps
    // ZERO KAPI includes/links/stubs. Ground truth (KMods public source, verified 2026-07-21):
    //   - UKAPIDataAssetSubsystem is a UGameInstanceSubsystem; Initialize() runs
    //     StartScanForDataAssets once per game instance (deferred to AssetRegistry OnFilesLoaded),
    //     so by our first pass (~70 s into a world) the scan has run — an EMPTY map means either
    //     "not yet" (early) or "no description assets installed at all"; we defer a bounded number
    //     of passes then treat it as terminal.
    //   - ScanForMinerAssets keys mMinerMapping by each description's mResourceClass and also adds
    //     that class to mAllowedScannableResources — we mirror BOTH for provided ores.
    //   - AKLMMBuildableMiner::BeginPlay -> Miner_GetForKey (plain Contains+Find) -> fgcheckf.
    // The clone: template-initialized copy of an existing description (prefer the Desc_Stone_C
    // entry, else Desc_OreIron_C, else the first solid-resource entry), then rewire mResourceClass
    // and EVERY FKAPIModuleItems.mProductionItem in mModuleInformation to the ore (the resource
    // descriptor IS the item class: UFGResourceDescriptor : UFGItemDescriptor). mTrashItem/tier/UI
    // fields stay template (there is no per-ore name field on the description; the screenshot/
    // rarity text are cosmetic flavor).
    //
    // knowledge-3 (SAVE-CRASH FIX — EXCEPTION_ACCESS_VIOLATION in UFGSaveSession::SaveLevelState:
    // FObjectReferenceDisc::Set -> ULevel::GetWorldPartitionRuntimeCell -> FWeakObjectPtr::Get).
    // KLib's miner serializes mExtractionInfo as UPROPERTY(SaveGame) (KLMMBuildableMiner.h:122), so
    // the save writes an FObjectReferenceDisc for OUR clone. Its contract (FGObjectReference.h:
    // 26-27): "Name of the level we reside in, if empty, PathName is a absolute path" — i.e. the
    // SAFE branch for non-level objects is the ASSET branch, which requires an outermost that is a
    // plain content UPackage (that is how every vanilla descriptor reference serializes). The
    // knowledge-2 clone was outered to the KAPI subsystem -> GameInstance -> engine-transient chain
    // — no level AND no content package, an identity Set() was never built for; its level-name
    // machinery dereferenced garbage in a ParallelFor save worker. FIX: every clone now lives in a
    // dedicated runtime content package (/NodeShuffle/RuntimeMinerInfo) under a DETERMINISTIC
    // per-ore name (NSMinerInfo_<Ore>), so Set() takes the string-only asset branch. Determinism
    // matters for the reference a save captures: same path resolves to the same object next session
    // once provisioning has run. If a load resolves the reference BEFORE provisioning (actor
    // property deserialization precedes our PostLoadGame pass), StaticFindOrLoad misses and the
    // field restores null — harmless BY KLIB's OWN DESIGN: AKLMMBuildableMiner::BeginPlay
    // unconditionally re-fetches ("GetAssetSubsystem()->Miner_GetForKey(GetResourceClass(),
    // TempExtractionInfo); SetExtractionInfo(TempExtractionInfo);" — KLMMBuildableMiner.cpp:202-204)
    // before its fgcheckf, and our provisioning runs before BeginPlay (PostLoadGame early pass).
    // Flags: RF_Public (referenced from outside its package — the save reference is external) |
    // RF_Standalone (lives with its package even while nothing references it) + AddToRoot (belt
    // against a mid-session KAPI rescan emptying the map). Reuse-or-create keeps the name unique:
    // a same-session re-provision after a rescan FINDS the existing rooted clone instead of
    // colliding with it.
    bOutFilterUnlocks = false;
    UClass* SubsysClass = FindObject<UClass>(nullptr, TEXT("/Script/KAPI.KAPIDataAssetSubsystem"));
    if (!SubsysClass)
    {
        return true; // KAPI absent: no Modular Miner exists to assert — no filtering, unlock freely
    }
    bOutFilterUnlocks = true; // KAPI present: from here on, knowledge must imply MinerInfo

    const bool bDiag = FNodeShuffleModule::AreDiagnosticsEnabled();
    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    UGameInstanceSubsystem* Subsys = GI ? GI->GetSubsystemBase(SubsysClass) : nullptr;
    FMapProperty* MinerMapProp = Subsys ? FindFProperty<FMapProperty>(SubsysClass, TEXT("mMinerMapping")) : nullptr;
    FSetProperty* AllowedSetProp = Subsys ? FindFProperty<FSetProperty>(SubsysClass, TEXT("mAllowedScannableResources")) : nullptr;
    FObjectPropertyBase* MapKeyProp = MinerMapProp ? CastField<FObjectPropertyBase>(MinerMapProp->KeyProp) : nullptr;
    FObjectPropertyBase* MapValProp = MinerMapProp ? CastField<FObjectPropertyBase>(MinerMapProp->ValueProp) : nullptr;
    FObjectPropertyBase* SetElemProp = AllowedSetProp ? CastField<FObjectPropertyBase>(AllowedSetProp->ElementProp) : nullptr;
    if (!Subsys || !MapKeyProp || !MapValProp || !SetElemProp)
    {
        // KAPI is installed but its reflection surface moved (version drift) or the subsystem is
        // unreachable — provisioning is impossible, so item 2 withholds ALL modded unlocks
        // (crash-proof beats feature-complete). Terminal: no retry spin.
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("minerinfo: KAPI present but mMinerMapping/mAllowedScannableResources not reachable via reflection — withholding modded scanner unlocks this session"));
        OutWithMinerInfo.Reset();
        return true;
    }

    FScriptMapHelper MapHelper(MinerMapProp, MinerMapProp->ContainerPtrToValuePtr<void>(Subsys));
    if (MapHelper.Num() == 0)
    {
        // Scan not run yet (or no description assets exist at all). Defer a bounded number of
        // passes, then terminal-withhold: unlocking without MinerInfo would arm the KLib assert.
        if (++KnowledgeDeferPasses <= KnowledgeDeferMaxPasses)
        {
            if (bDiag)
            {
                UE_LOG(LogNodeShuffle, Verbose, TEXT("minerinfo: KAPI mMinerMapping empty — deferring knowledge pass (%d/%d)"),
                    KnowledgeDeferPasses, KnowledgeDeferMaxPasses);
            }
            return false;
        }
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("minerinfo: KAPI mMinerMapping stayed empty after %d passes (no miner description assets?) — withholding modded scanner unlocks this session"),
            KnowledgeDeferMaxPasses);
        OutWithMinerInfo.Reset();
        return true;
    }

    // ---- template pick: Desc_Stone_C > Desc_OreIron_C > first SOLID-resource entry. ----
    UObject* Template = nullptr;
    FString TemplateKey;
    UObject* IronPick = nullptr;
    UObject* SolidPick = nullptr;
    FString IronKey, SolidKey;
    for (int32 i = 0; i < MapHelper.GetMaxIndex(); ++i)
    {
        if (!MapHelper.IsValidIndex(i)) { continue; }
        UClass* Key = Cast<UClass>(MapKeyProp->GetObjectPropertyValue(MapHelper.GetKeyPtr(i)));
        UObject* Val = MapValProp->GetObjectPropertyValue(MapHelper.GetValuePtr(i));
        if (!Key || !IsValid(Val)) { continue; }
        const FString KeyName = Key->GetName();
        if (KeyName == TEXT("Desc_Stone_C")) { Template = Val; TemplateKey = KeyName; break; }
        if (!IronPick && KeyName == TEXT("Desc_OreIron_C")) { IronPick = Val; IronKey = KeyName; }
        if (!SolidPick && Key->IsChildOf(UFGResourceDescriptor::StaticClass())
            && UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(Key)) == EResourceForm::RF_SOLID)
        {
            SolidPick = Val;
            SolidKey = KeyName;
        }
    }
    if (!Template) { Template = IronPick; TemplateKey = IronKey; }
    if (!Template) { Template = SolidPick; TemplateKey = SolidKey; }
    if (!Template)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("minerinfo: no usable template entry in KAPI mMinerMapping (%d entries, none solid/valid) — withholding modded scanner unlocks this session"),
            MapHelper.Num());
        OutWithMinerInfo.Reset();
        return true;
    }

    // ---- clone-wiring properties, resolved ONCE on the template's class; any miss = withhold all
    // (a description with a wrong/unwired production item is worse than none). ----
    UClass* DescClass = Template->GetClass();
    FObjectPropertyBase* ResClassProp = CastField<FObjectPropertyBase>(DescClass->FindPropertyByName(TEXT("mResourceClass")));
    FMapProperty* ModulesProp = CastField<FMapProperty>(DescClass->FindPropertyByName(TEXT("mModuleInformation")));
    FStructProperty* ModulesValStruct = ModulesProp ? CastField<FStructProperty>(ModulesProp->ValueProp) : nullptr;
    FObjectPropertyBase* ProdItemProp = ModulesValStruct
        ? CastField<FObjectPropertyBase>(ModulesValStruct->Struct->FindPropertyByName(TEXT("mProductionItem"))) : nullptr;
    if (!ResClassProp || !ProdItemProp)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("minerinfo: KAPI description reflection surface changed (mResourceClass/mModuleInformation.mProductionItem) — withholding modded scanner unlocks this session"));
        OutWithMinerInfo.Reset();
        return true;
    }

    // knowledge-3: the runtime content package all provisioned clones live in. CreatePackage is
    // find-or-create (idempotent); rooted + marked fully loaded so nothing ever tries to "finish
    // loading" a package that has no disk backing (the ContentLib-style runtime-content idiom).
    UPackage* RuntimePackage = CreatePackage(TEXT("/NodeShuffle/RuntimeMinerInfo"));
    if (!RuntimePackage)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("minerinfo: could not create the /NodeShuffle/RuntimeMinerInfo package — withholding modded scanner unlocks this session"));
        OutWithMinerInfo.Reset();
        return true;
    }
    if (!RuntimePackage->IsRooted()) { RuntimePackage->AddToRoot(); }
    RuntimePackage->MarkAsFullyLoaded();

    FScriptSetHelper SetHelper(AllowedSetProp, AllowedSetProp->ContainerPtrToValuePtr<void>(Subsys));
    int32 Provided = 0;
    FString ProvidedNames;
    for (UClass* Ore : ManagedModded)
    {
        UClass* KeyVal = Ore; // object-property storage: a plain UClass* location (shipping layout)
        if (MapHelper.FindValueFromHash(&KeyVal) != nullptr)
        {
            OutWithMinerInfo.Add(Ore); // real (or previously provided) description exists — safe
            if (bDiag)
            {
                UE_LOG(LogNodeShuffle, Verbose, TEXT("minerinfo: %s already has a KAPI miner description"), *Ore->GetName());
            }
            continue;
        }
        if (!Ore->IsChildOf(UFGItemDescriptor::StaticClass()))
        {
            // Cannot wire a production item for this ore — leave it without MinerInfo (and therefore
            // without a scanner unlock) rather than produce a wrong item.
            UE_LOG(LogNodeShuffle, Warning, TEXT("minerinfo: %s is not an item descriptor — skipped (stays scanner-locked)"),
                *Ore->GetName());
            continue;
        }
        // Deterministic per-ore identity inside the runtime package (see knowledge-3 block above):
        // reuse an existing clone (same-session re-provision after a KAPI rescan) or template-init a
        // new one. NewObject-with-template copies all property values, same as DuplicateObject for
        // this asset shape (no Instanced subobjects on the description).
        const FString CloneName = FString::Printf(TEXT("NSMinerInfo_%s"), *Ore->GetName());
        UObject* Clone = FindObject<UObject>(RuntimePackage, *CloneName);
        if (!Clone)
        {
            Clone = NewObject<UObject>(RuntimePackage, DescClass, FName(*CloneName),
                RF_Public | RF_Standalone, Template);
        }
        if (!Clone)
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("minerinfo: clone creation failed for %s — skipped"), *Ore->GetName());
            continue;
        }
        Clone->AddToRoot();
        ResClassProp->SetObjectPropertyValue(ResClassProp->ContainerPtrToValuePtr<void>(Clone), Ore);
        if (ModulesProp) // rewire every module's production item to the ore (trash item stays template)
        {
            FScriptMapHelper ModHelper(ModulesProp, ModulesProp->ContainerPtrToValuePtr<void>(Clone));
            for (int32 mi = 0; mi < ModHelper.GetMaxIndex(); ++mi)
            {
                if (!ModHelper.IsValidIndex(mi)) { continue; }
                void* ModuleItemsPtr = ModHelper.GetValuePtr(mi);
                ProdItemProp->SetObjectPropertyValue(ProdItemProp->ContainerPtrToValuePtr<void>(ModuleItemsPtr), Ore);
            }
        }
        UObject* ValVal = Clone;
        MapHelper.AddPair(&KeyVal, &ValVal);     // mirrors ScanForMinerAssets: mMinerMapping.Add(...)
        SetHelper.AddElement(&KeyVal);           // ...and mAllowedScannableResources.Add(...)
        OutWithMinerInfo.Add(Ore);
        Provided++;
        ProvidedNames += (ProvidedNames.IsEmpty() ? TEXT("") : TEXT(", "));
        ProvidedNames += Ore->GetName();
        if (bDiag)
        {
            UE_LOG(LogNodeShuffle, Verbose, TEXT("minerinfo: provided description for %s (clone of %s entry)"),
                *Ore->GetName(), *TemplateKey);
        }
    }
    if (Provided > 0)
    {
        // Ungated one-shot: the shipping-log evidence the second registry was satisfied.
        UE_LOG(LogNodeShuffle, Display, TEXT("minerinfo: provided %d KAPI miner description(s) (template=%s): %s"),
            Provided, *TemplateKey, *ProvidedNames);
    }
    return true;
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

// Packet H1: parameter widened to AFGResourceNodeBase* so a fracking CORE (an AFGResourceNodeBase but
// NOT an AFGResourceNode) can be rebuilt too. Body unchanged — it only ever used UObject methods.
void ANodeShuffleSubsystem::RebuildNodeNativeVisual(AFGResourceNodeBase* Node)
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
    // MESHTYPE-CENSUS coverage denominator: how many ordinary nodes were streamed in at all, so the
    // census below can print "N of M paired" rather than a bare N whose population is unknown.
    int32 OrdinaryNodesSeen = 0;
    for (TActorIterator<AFGResourceNode> It(GetWorld()); It; ++It)
    {
        AFGResourceNode* Node = *It;
        if (!IsValid(Node) || Node->HasAnyFlags(RF_Transient) || IsFrackingActor(Node))
        {
            continue;
        }
        ++OrdinaryNodesSeen;
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

    if (FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("Mesh-actor cache: %d paired (%d via mesh-actor back-link, %d via node->mMeshActor forward link)"),
            MeshActorCache.Num(), FromBackLink, FromForwardLink);
    }

    // MESHTYPE-CENSUS (cold review 2026-08-08, Alternative F). THE BOOT-TIME MEASUREMENT OF THE ONE
    // ASSUMPTION T2's WELL-PIECE GATE RESTS ON. That gate (NodeShuffleWellVisuals.cpp, route 3) treats
    // "owner casts to AFGNodeMeshActor AND mNodeMeshType != MT_Node" as "this is well geometry, not an
    // ordinary node's rock". The cast half protects nothing -- AFGNodeMeshActor carries ALL node meshes
    // (FGResourceNodeBase.h:59), ordinary ones included -- so the whole guard is the enum value, and that
    // value is EditInstanceOnly level data baked into cooked assets that no static read can reach.
    // Previously it could only be checked by walking the map and looking at nodes. This sweep ALREADY
    // visits every AFGNodeMeshActor, so the histogram is nearly free. SCOPE, STATED HONESTLY: a
    // TActorIterator sees only STREAMED-IN actors, so one line is a snapshot of the player's
    // neighbourhood, NOT a world census -- read the series across a session. It runs AFTER the forward
    // sweep above, so back-links that sweep repaired are counted as paired (and are reported separately,
    // because they are OUR write, not level authoring).
    // Reported, never explained: this says what was counted, not why any value is what it is.
    if (FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        int32 Total = 0, OrdinaryMTNode = 0, OrdinaryNonNode = 0, FrackMTNode = 0, FrackNonNode = 0;
        int32 Unpaired = 0, UnpairedNonNode = 0;
        int32 ByType[7] = { 0, 0, 0, 0, 0, 0, 0 };
        for (TActorIterator<AFGNodeMeshActor> It(GetWorld()); It; ++It)
        {
            AFGNodeMeshActor* MA = *It;
            if (!IsValid(MA)) { continue; }
            ++Total;
            const int32 TypeIdx = static_cast<int32>(MA->mNodeMeshType);
            if (TypeIdx >= 0 && TypeIdx < 7) { ++ByType[TypeIdx]; }
            const bool bNonNode = MA->mNodeMeshType != ENodeMeshType::MT_Node;
            const AFGResourceNodeBase* Paired = MA->mNodeActor.Get();
            if (!Paired)
            {
                ++Unpaired;
                if (bNonNode) { ++UnpairedNonNode; }
            }
            else if (IsFrackingActor(Paired))
            {
                if (bNonNode) { ++FrackNonNode; } else { ++FrackMTNode; }
            }
            else
            {
                if (bNonNode) { ++OrdinaryNonNode; } else { ++OrdinaryMTNode; }
            }
        }
        // The three buckets partition the measured total, so every count here carries its own
        // denominator and no zero can be read without one (TECH-DEBT T5). Re-emitted only when the
        // measurement itself changes, so a repeating apply pass does not spam an unchanged census.
        const FString Census = FString::Printf(
            TEXT("meshActors=%d STREAMED-IN (not world) | pairedToOrdinaryNode: %d MT_Node + %d ")
            TEXT("non-MT_Node | pairedToFracking: %d MT_Node + %d non-MT_Node | unpaired(mNodeActor ")
            TEXT("unset): %d, of which %d non-MT_Node | coverage: %d of %d streamed ordinary node(s) ")
            TEXT("paired (%d engine back-link, %d back-links repaired by THIS pass) | mNodeMeshType ")
            TEXT("histogram 0..6 = %d/%d/%d/%d/%d/%d/%d"),
            Total, OrdinaryMTNode, OrdinaryNonNode, FrackMTNode, FrackNonNode, Unpaired, UnpairedNonNode,
            OrdinaryMTNode + OrdinaryNonNode, OrdinaryNodesSeen, FromBackLink, FromForwardLink,
            ByType[0], ByType[1], ByType[2], ByType[3], ByType[4], ByType[5], ByType[6]);
        // Key on the DECISION-RELEVANT subset, not the whole payload. Total, unpaired and all seven
        // histogram bins move whenever any node mesh actor streams in or out, so keying on the full
        // string re-prints a ~900-char Display line on nearly every apply pass while nothing the
        // reader is asked to act on has changed. The four numbers below are what R-1 reads.
        const FString CensusKey = FString::Printf(
            TEXT("meshtypecensus|%d|%d|%d|%d"),
            OrdinaryMTNode, OrdinaryNonNode, FrackNonNode, UnpairedNonNode);
        if (!WellVisualCaptureLogged.Contains(CensusKey))
        {
            WellVisualCaptureLogged.Add(CensusKey);
            UE_LOG(LogNodeShuffle, Display,
                TEXT("MESHTYPE-CENSUS (wellAuditPasses=%d as of the START of this apply pass; the ")
                TEXT("WELLH2B-INDEX line for the SAME tick reads pass %d): %s. ENodeMeshType: ")
                TEXT("0=MT_Node 1=MT_Core 2=MT_Crack ")
                TEXT("3=MT_Satellite 4=MT_DesertCore 5=MT_DesertCrack 6=MT_DesertSatellite. 'pairedTo*' ")
                TEXT("reads this actor's mNodeActor back-link after this pass repaired it from the node ")
                TEXT("side; 'unpaired' means that link was still unset when counted, NOT that the actor ")
                TEXT("belongs to nothing. THE LOAD-BEARING NUMBER IS 'pairedToOrdinaryNode ... ")
                TEXT("non-MT_Node': T2's well-piece gate in NodeShuffleWellVisuals.cpp is only correct ")
                TEXT("while it is 0. READ IT AGAINST 'coverage': a 0 over a small coverage fraction ")
                TEXT("says nothing about the nodes that were not paired. 'unpaired' is NOT a pass or a ")
                TEXT("fail -- it holds every fracking well's own crack geometry (never paired here) ")
                TEXT("mixed with any node whose mMeshActor link did not resolve, and this line cannot ")
                TEXT("tell them apart. Counts cover STREAMED-IN actors only, so this number changes as ")
                TEXT("you travel; read the SERIES of these lines across a session, not one line. ")
                TEXT("'repaired by THIS pass' is pairing THIS MOD created seconds ago, not level ")
                TEXT("authoring. Re-printed when the numbers change."),
                WellAuditPasses, WellAuditPasses + 1, *Census);
        }
    }
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
        // coexist-1 §3: diagnostics-gated. With the gate off the once-set stays empty, so enabling
        // diagnostics later still reports every rock once from that point on.
        const bool bReport = FNodeShuffleModule::AreDiagnosticsEnabled() && !OrphanReasonLogged.Contains(Smc);
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
    // coexist-1 §2+§3: log-only world sweep — skip it entirely unless diagnostics are on.
    if (!FNodeShuffleModule::AreDiagnosticsEnabled())
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

    constexpr float DiagRange = 1500.0f; // 15 m around the player

    // redesign-5: the "ours" set is each spawned node's OWN RockMesh subobject (the rock now lives on the
    // node, not a separate actor). Used only to annotate the diagnostic line (is this rock one WE spawned?).
    TSet<UStaticMeshComponent*> Claimed;
    for (const auto& Pair : SpawnedNodes)
    {
        if (!IsValid(Pair.Value)) { continue; }
        // knowledge-1 item 5: count EVERY static-mesh component owned by a managed spawned node as
        // paired, not just our RockMesh subobject. A real-class node rendering its NATIVE slab (esc_
        // AllMinable) has an EMPTY RockMesh, so the old RockMesh-only set reported paired-to-this=0
        // for a perfectly-owned native slab (false alarm at the dirty-caterium miner). RockMesh is
        // among the owned components, so both old cases stay covered. The stray-rock backstop
        // already protects owned meshes via its NodeShuffleIsOurNode owner check — this only makes
        // the DIAGNOSTIC agree with it.
        TInlineComponentArray<UStaticMeshComponent*> OwnedMeshes(Pair.Value);
        for (UStaticMeshComponent* MC : OwnedMeshes)
        {
            if (IsValid(MC)) { Claimed.Add(MC); }
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
