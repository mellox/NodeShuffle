#include "NodeShuffle.h"

DEFINE_LOG_CATEGORY(LogNodeShuffle);

void FNodeShuffleModule::StartupModule()
{
    UE_LOG(LogNodeShuffle, Log, TEXT("NodeShuffle module loaded"));
    UE_LOG(LogNodeShuffle, Display, TEXT("===== NodeShuffle BUILD 2026-06-14-lossless-5 LOADED ====="));
}

IMPLEMENT_GAME_MODULE(FNodeShuffleModule, NodeShuffle);
