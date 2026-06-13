#include "NodeShuffleRootInstance.h"
#include "NodeShuffleConfig.h"

URootInstance_NodeShuffle::URootInstance_NodeShuffle()
{
    bRootModule = true;
    ModConfigurations.Add(UNodeShuffleConfig::StaticClass());
}
