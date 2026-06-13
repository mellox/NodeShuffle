#include "NodeShuffleRootWorld.h"
#include "NodeShuffleSubsystem.h"

URootGameWorld_NodeShuffle::URootGameWorld_NodeShuffle()
{
    bRootModule = true;
    ModSubsystems.Add(ANodeShuffleSubsystem::StaticClass());
}
