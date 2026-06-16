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

    // When true AND SeedOverride is non-zero AND differs from the seed stored
    // in the save, the layout is re-rolled at the next session start.
    UPROPERTY(BlueprintReadWrite)
    bool AllowReroll{false};

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

    // Shuffle mod-added nodes (AllMinable items, modded ores) too. The
    // completability floor never applies to non-vanilla resources.
    UPROPERTY(BlueprintReadWrite)
    bool IncludeModdedNodes{true};

    // Spawn-on-discovery radius (metres). A new node only materializes (spawns
    // its actor + visual) once a player is within this distance of it AND the
    // terrain there has streamed in. Far, undiscovered nodes stay as data until
    // you explore to them — this is what makes them settle correctly on the
    // ground instead of floating in unloaded regions.
    UPROPERTY(BlueprintReadWrite)
    int32 SpawnRadiusMeters{600};

    // Standard "experimental features" gate. Off by default. Experimental,
    // not-yet-stable features check this before activating; stable features
    // ignore it. In this version it enables oil/liquid node shuffling
    // (oil nodes join the scan/pool/spawn and can be mined with oil extractors).
    UPROPERTY(BlueprintReadWrite)
    bool EnableExperimentalFeatures{false};

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
