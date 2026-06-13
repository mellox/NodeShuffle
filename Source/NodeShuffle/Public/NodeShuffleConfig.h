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

    // Percent of the total node pool (vanilla + new) that is active.
    UPROPERTY(BlueprintReadWrite)
    int32 ActivePercent{70};

    // How many new candidate node locations to generate (generation time only).
    UPROPERTY(BlueprintReadWrite)
    int32 NewNodeCount{100};

    // Minimum active nodes per resource type — the completability floor.
    UPROPERTY(BlueprintReadWrite)
    int32 MinNodesPerResource{5};

    UPROPERTY(BlueprintReadWrite)
    bool RandomizePurity{true};

    // When false, every vanilla node stays active (new nodes still add on top).
    UPROPERTY(BlueprintReadWrite)
    bool AllowVanillaDisappear{true};

    // Shuffle mod-added nodes (AllMinable items, modded ores) too. The
    // completability floor never applies to non-vanilla resources.
    UPROPERTY(BlueprintReadWrite)
    bool IncludeModdedNodes{true};

    // Re-skin retyped existing nodes' rocks to match the new resource (the rock
    // appearance is copied from a real node of that resource). On by default;
    // turn off if you prefer retyped nodes to keep their ORIGINAL rock look
    // while still mining the new resource. New spawned nodes always get visuals.
    UPROPERTY(BlueprintReadWrite)
    bool SwapNodeRockVisuals{true};

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
