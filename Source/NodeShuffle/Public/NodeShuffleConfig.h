#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "Configuration/ConfigManager.h"
#include "NodeShuffleConfig.generated.h"

// Plain struct mirror of the configuration. Field names MUST match the
// section property keys registered in UNodeShuffleConfig exactly —
// UConfigManager::FillConfigurationStruct maps them by name.
USTRUCT(BlueprintType)
struct NODESHUFFLE_API FNodeShuffleConfigStruct
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    bool Enabled{true};

    // 0 = roll a random seed at first generation; non-zero = use this seed.
    UPROPERTY(BlueprintReadWrite)
    int32 SeedOverride{0};

    // User-facing reliable re-roll trigger. When true at load and the world is
    // ready, the layout re-rolls once regardless of seed equality, then the mod
    // clears this flag back to false and saves the config so it fires only once.
    UPROPERTY(BlueprintReadWrite)
    bool RerollNow{false};

    // Percent of the total node pool (vanilla + new) that is active.
    UPROPERTY(BlueprintReadWrite)
    int32 ActivePercent{70};

    // How many new candidate node locations to generate (generation time only).
    UPROPERTY(BlueprintReadWrite)
    int32 NewNodeCount{100};

    // Minimum active nodes per resource type — the completability floor.
    UPROPERTY(BlueprintReadWrite)
    int32 MinNodesPerResource{5};

    // Minimum active nodes per MODDED resource type (resources added by other
    // mods, i.e. outside /Game/). 0 = no minimum (the old behavior, where modded
    // types could collapse to a single location). Default 2 keeps the
    // proportional feel while preventing singletons.
    UPROPERTY(BlueprintReadWrite)
    int32 MinNodesPerModdedResource{2};

    UPROPERTY(BlueprintReadWrite)
    bool RandomizePurity{true};

    // When false, every vanilla node stays active (new nodes still add on top).
    UPROPERTY(BlueprintReadWrite)
    bool AllowVanillaDisappear{true};

    // Shuffle mod-added nodes (AllMinable items, modded ores) too. When on, modded
    // SOLID nodes also RELOCATE (move to new locations) on a re-roll, like vanilla
    // nodes, instead of shuffling in place. The completability floor never applies to
    // non-vanilla resources.
    UPROPERTY(BlueprintReadWrite)
    bool IncludeModdedNodes{true};

    // Spawn-on-discovery radius (metres). A new node only materializes (spawns
    // its actor + visual) once a player is within this distance of it AND the
    // terrain there has streamed in. Far, undiscovered nodes stay as data until
    // you explore to them — this is what makes them settle correctly on the
    // ground instead of floating in unloaded regions.
    UPROPERTY(BlueprintReadWrite)
    int32 SpawnRadiusMeters{600};

    // knowledge-1: register shuffled MODDED resources with the game's scanner-unlock list (SaveGame,
    // replicated) so scanners — and mods that gate extractors on scanner knowledge (SF+ Modular
    // Miner via KLib HasInformationAboutOre) — recognize them even when their own unlock schematics
    // never ran in this save. Vanilla resources are never touched.
    UPROPERTY(BlueprintReadWrite)
    bool UnlockModdedKnowledge{true};

    // Diagnostics gate. OFF by default so normal users get NO extra log output and
    // no overhead. When ON, the mod writes verbose HOLOGRAMHOOK / placement / node
    // diagnostics to FactoryGame.log for troubleshooting (e.g. Miner snap issues).
    // The actual fixes (e.g. the Mk1 accept-hook) are always active regardless.
    UPROPERTY(BlueprintReadWrite)
    bool EnableDiagnostics{false};

    // redesign-1 (Hide & Replace): on a BRAND-NEW game's first roll only, place a
    // starter set of nodes (2 Iron, 2 Limestone, 1 Copper, PURE) within
    // StarterNodeRadiusMeters of the player's real spawn point, so the early game is
    // playable no matter how the shuffle relocated the world's nodes. Drawn from the
    // relocated pool when possible (preserving counts), spawned additionally if the
    // pool can't supply a type. NEVER placed on an existing save.
    UPROPERTY(BlueprintReadWrite)
    bool EnableStarterNodes{true};

    // redesign-1: radius (metres) around the player's captured spawn location within
    // which the starter nodes are placed. Default 200 m.
    UPROPERTY(BlueprintReadWrite)
    int32 StarterNodeRadiusMeters{200};

    // Packet H1 (ns-wells-h1): shuffle which RESOURCE each resource well produces, IN PLACE. The well
    // never moves — only what it yields changes (a nitrogen well may become a water well). Wells with
    // a pressurizer or any fracking extractor already on them are never changed.
    //
    // DEFAULT OFF, and it is its OWN toggle rather than a use of EnableExperimentalFeatures: that flag
    // is documented in-UI as "THIS VERSION HAS NO EXPERIMENTAL FEATURES ... leave it off", and it is a
    // single switch, so hanging wells off it would (a) contradict its own tooltip and (b) mean any
    // future experimental feature could not be enabled independently of well shuffling. With this off,
    // WellLayout is never rolled and ApplyWellRetype returns immediately, so the mod's stable core
    // behaves identically to a build without Packet H1.
    UPROPERTY(BlueprintReadWrite)
    bool ShuffleResourceWells{false};

    // Packet H2 (ns-wells-h2): RIGID RELOCATION of whole well groups. Its OWN toggle, separate from
    // ShuffleResourceWells above and DEFAULT OFF, because retyping a well in place and physically
    // moving it are different promises. Relocation additionally REQUIRES ShuffleResourceWells (a
    // well NodeShuffle does not manage is not one it may move), so the two together are the gate.
    //
    // STAGE: H2 shipped the relocation ENGINE (placement, the yaw search, the group-atomic spawn and
    // the mCore lifecycle); H2b then shipped the group VISUALS, which the engine does not provide for
    // us (design §2.4: a runtime-spawned node gets no engine AFGNodeMeshActor, and wells have their
    // own MT_Core / MT_Crack / MT_Satellite mesh vocabulary, so we re-create them ourselves).
    //
    // MEASURED 2026-08-08, in game, on a fresh save: a relocated well is dressed and BUILDABLE --
    // TrySnapToActor -> 1 with bForceAccept=0 onto our own core, and the user confirmed the
    // pressurizer producing water. Still flagged EXPERIMENTAL in the tooltip because two edges are
    // unverified rather than known-good: desert-biome mesh names (docs/TECH-DEBT.md T2) and snap-box
    // overlap with ordinary nodes within ~15 m (T3). Do NOT re-word this into "invisible" -- that
    // claim was true only before H2b and outlived its truth in three places.
    UPROPERTY(BlueprintReadWrite)
    bool RelocateResourceWells{false};

    // T7b (ns-t7b-reroll): does a re-roll re-consider a well that has ALREADY been relocated?
    //
    // DEFAULT OFF, and the default is the whole point of the option. With it off, RollWellRelocation
    // behaves exactly as it did before T7b: a placed group keeps its geography across re-rolls and only
    // never-moved wells are dealt a destination. With it on, an already-relocated well is re-captured
    // from its (still standing, suppressed) vanilla actors, torn down and dealt a new destination like
    // any other well -- which on the author's own save means ~17 of 20 wells churn on the first
    // re-roll after the toggle is enabled. That is a deliberate act, not a surprise, which is why it
    // has its own switch rather than riding on RelocateResourceWells.
    //
    // A well someone has built on is never re-enrolled: bManaged is false for a pinned well, and since
    // T16 the pin for a relocated well is decided from the actors WE spawned (the ones a player can
    // actually reach) rather than from the hidden vanilla core.
    //
    // Inert unless BOTH ShuffleResourceWells and RelocateResourceWells are on -- this flag only
    // widens the population RollWellRelocation considers, it never enables relocation by itself.
    UPROPERTY(BlueprintReadWrite)
    bool RerollRelocatedWells{false};

    // T23 stage 3 (ns-t23-rollhide): WHEN is the vanilla well removed -- at the ROLL, or once the
    // replacement has actually been built at the destination?
    //
    // ns-t54-immediate-hide (2026-08-10) -- THE PARAGRAPH BELOW THIS ONE IS NOW HISTORY, NOT BEHAVIOUR,
    // AND IS KEPT BECAUSE IT RECORDS WHAT THE DEFAULT USED TO GUARANTEE. The author ruled that a shuffled
    // origin must disappear immediately whatever this toggle says, so ApplyWellRelocation now INITIATES
    // the suppression for every entry marked as moving, on every pass. What survives of this flag: the
    // roll path additionally attempts the hide at the instant of the roll, for entries whose look is
    // completely captured then. It is therefore close to a no-op -- it moves the disappearance earlier by
    // roughly one apply pass and takes on the roll's one-shot-capture fallback to do it.
    // NOT RETIRED HERE ON PURPOSE: retiring a shipped, save-visible toggle is the author's call, not this
    // packet's, and its roll-time path is still the only one that can hide before the first apply pass.
    // Recommendation is filed in docs/TECH-DEBT.md under T54.
    //
    // (HISTORY, true until 2026-08-10) DEFAULT OFF, and the default is the feature's own gate. With it
    // off, suppression happens exactly
    // where it always has (NodeShuffleWellRelocateApply.cpp, after a COMPLETE spawn), so the roll path
    // behaves identically to a build without this packet. With it on, a well whose look can be captured
    // at the roll is hidden at the roll, and the world holds it in NEITHER place until a player reaches
    // the destination -- an interval that is genuinely unbounded (destinations are dealt uniformly over
    // the whole map and the author rejected biasing them toward the player, T23 §6 stage 1).
    //
    // THE HALF THAT MAKES THIS SHIPPABLE IS THE UN-HIDE, not the hide. See
    // FNodeShuffleWellSuppressionRecord in NodeShuffleSubsystem.h: without a persisted, re-attempted
    // restore path, a well whose relocation terminally fails would be deleted from the save permanently.
    // Inert unless BOTH ShuffleResourceWells and RelocateResourceWells are on.
    UPROPERTY(BlueprintReadWrite)
    bool CommitWellsAtRoll{false};

    // ns-h1b-notice: post one chat message when an extractor has been cleared for use on shuffled nodes
    // but needs a game restart before SF+ will permit it. DEFAULT TRUE -- the entire point is that the
    // player did not know, and the notice is structurally unable to nag (it is a STATE test that empties
    // itself one boot after the patches land, so a steady-state profile never sees it at all).
    UPROPERTY(BlueprintReadWrite)
    bool ShowCompatibilityNotices{true};

    // The standard experimental gate (workspace convention: every mod keeps this flag, even when
    // inert). cave-nodes-4: cave placement GRADUATED to always-on — it fixes the cave-drain
    // regression, so it is not optional. NOTHING is currently gated by this flag.
    UPROPERTY(BlueprintReadWrite)
    bool EnableExperimentalFeatures{false};

    static FNodeShuffleConfigStruct GetActiveConfig(UObject* WorldContext);
};

// C++-defined SML mod configuration. Registered by the root game instance
// module; SML generates the in-game settings UI (on the mod's page in the
// Mods menu) and persists values to FactoryGame/Configs/NodeShuffle.cfg.
UCLASS()
class NODESHUFFLE_API UNodeShuffleConfig : public UModConfiguration
{
    GENERATED_BODY()

public:
    UNodeShuffleConfig();

    // The property tree is built here rather than in the constructor so each
    // property can be outer'd to the root section — SML's dirty/save chain
    // walks property Outers and silently never saves otherwise.
    virtual void PostInitProperties() override;
};
