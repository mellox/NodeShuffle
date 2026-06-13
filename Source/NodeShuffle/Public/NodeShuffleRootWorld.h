#pragma once

#include "CoreMinimal.h"
#include "Module/GameWorldModule.h"
#include "NodeShuffleRootWorld.generated.h"

// Root game world module. Spawns the subsystem in every game world.
UCLASS()
class NODESHUFFLE_API URootGameWorld_NodeShuffle : public UGameWorldModule
{
    GENERATED_BODY()

public:
    URootGameWorld_NodeShuffle();
};
