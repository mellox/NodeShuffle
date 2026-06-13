#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "NodeShuffleRootInstance.generated.h"

// Root game instance module. SML discovers it automatically because
// bRootModule is true. Registers the mod configuration.
UCLASS()
class NODESHUFFLE_API URootInstance_NodeShuffle : public UGameInstanceModule
{
    GENERATED_BODY()

public:
    URootInstance_NodeShuffle();
};
