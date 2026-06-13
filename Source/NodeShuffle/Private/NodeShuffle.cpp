#include "NodeShuffle.h"

DEFINE_LOG_CATEGORY(LogNodeShuffle);

void FNodeShuffleModule::StartupModule()
{
    UE_LOG(LogNodeShuffle, Log, TEXT("NodeShuffle module loaded"));
}

IMPLEMENT_GAME_MODULE(FNodeShuffleModule, NodeShuffle);
