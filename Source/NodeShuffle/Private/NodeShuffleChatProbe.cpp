// ns-rocks-1 D (2026-11-14): `NodeShuffle.ChatProbe` -- one labelled chat line per variant, ChatProbeGapSeconds apart, so one listen in
// game tells which variant (if any) posts without the chat sound. The user: our chat lines make a noise and there are many of them.
// FGChatManager.h has no sound flag; the sound is presumably played by the Blueprint chat widget, which we cannot read (graphify query of
// the FactoryGame API vault and the unified vault, 2026-11-14: no node describes the widget's reaction or a mod posting quietly).
// This file changes nothing about how the shuffle table or any notice is posted. Every line's text names only what that line does.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffle.h"
#include "FGChatManager.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "UObject/Class.h"

namespace
{
    constexpr float ChatProbeGapSeconds = 4.0f;
    constexpr int32 ChatProbeVariants = 6;
}

void ANodeShuffleSubsystem::RunChatProbe()
{
    UWorld* World = GetWorld();
    if (!World) { return; }
    if (bChatProbeRunning)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("CHATPROBE: already running (next variant %d of %d); this call ignored"), ChatProbeNext + 1, ChatProbeVariants);
        return;
    }
    AFGChatManager* Chat = AFGChatManager::Get(World);
    if (Chat && !bChatProbeLimitsLogged)
    {
        // Once per session: the two BlueprintPure limits, called by reflection (FindFunction + ProcessEvent, both in the import baseline)
        // so no direct call adds an import. -1 = the function was not found by name.
        bChatProbeLimitsLogged = true;
        int32 MaxHistory = -1;
        float VisibleSeconds = -1.0f;
        if (UFunction* FnMax = Chat->FindFunction(FName(TEXT("GetMaxNumMessagesInHistory"))))
        {
            struct { int32 ReturnValue = -1; } P;
            Chat->ProcessEvent(FnMax, &P);
            MaxHistory = P.ReturnValue;
        }
        if (UFunction* FnVis = Chat->FindFunction(FName(TEXT("GetMessageVisibleDuration"))))
        {
            struct { float ReturnValue = -1.0f; } P;
            Chat->ProcessEvent(FnVis, &P);
            VisibleSeconds = P.ReturnValue;
        }
        UE_LOG(LogNodeShuffle, Display, TEXT("CHATPROBE-LIMITS: maxMessagesInHistory=%d messageVisibleSeconds=%.1f"), MaxHistory, VisibleSeconds);
    }
    ChatProbeNext = 0;
    bChatProbeRunning = true;
    UE_LOG(LogNodeShuffle, Display, TEXT("CHATPROBE: starting %d variants, %.0f s apart"), ChatProbeVariants, ChatProbeGapSeconds);
    ChatProbeStep();
}

void ANodeShuffleSubsystem::ChatProbeStep()
{
    UWorld* World = GetWorld();
    AFGChatManager* Chat = World ? AFGChatManager::Get(World) : nullptr;
    if (!Chat || ChatProbeNext >= ChatProbeVariants)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("CHATPROBE: finished after %d of %d variants (chatManager=%d)"), ChatProbeNext, ChatProbeVariants, Chat ? 1 : 0);
        bChatProbeRunning = false;
        return;
    }
    const int32 V = ChatProbeNext + 1;
    APlayerController* LocalPc = nullptr;
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It) { LocalPc = It->Get(); break; } // first controller
    FChatMessageStruct Msg;
    Msg.MessageSender = FText::FromString(TEXT("NODE SHUFFLE"));
    Msg.MessageSenderColor = FLinearColor(1.0f, 0.55f, 0.10f);
    Msg.MessageType = EFGChatMessageType::CMT_CustomMessage;
    APlayerController* ProbeInstigator = nullptr;
    FString Body;
    const TCHAR* Variant = TEXT("?");
    switch (V)
    {
        case 1: Variant = TEXT("customNoInstigator"); Body = TEXT("NodeShuffle chat probe 1 of 6: custom message type, no instigator (the call NodeShuffle uses today)"); break;
        case 2: Variant = TEXT("customLocalInstigator"); ProbeInstigator = LocalPc; Body = TEXT("NodeShuffle chat probe 2 of 6: custom message type, your player controller passed as instigator"); break;
        case 3: Variant = TEXT("system"); Msg.MessageType = EFGChatMessageType::CMT_SystemMessage; Body = TEXT("NodeShuffle chat probe 3 of 6: system message type"); break;
        case 4: Variant = TEXT("ada"); Msg.MessageType = EFGChatMessageType::CMT_AdaMessage; Body = TEXT("NodeShuffle chat probe 4 of 6: ADA message type"); break;
        case 5: Variant = TEXT("player"); Msg.MessageType = EFGChatMessageType::CMT_PlayerMessage; Body = TEXT("NodeShuffle chat probe 5 of 6: player message type"); break;
        default: Variant = TEXT("lineBreak"); Body = FString(TEXT("NodeShuffle chat probe 6 of 6: one message with a line break. First line")) + TEXT("\n") + TEXT("Second line, same message"); break;
    }
    Msg.MessageText = FText::FromString(Body);
    Chat->BroadcastChatMessage(Msg, ProbeInstigator);
    UE_LOG(LogNodeShuffle, Display, TEXT("CHATPROBE[%d]: variant=%s type=%d instigatorPassed=%d bodyChars=%d"),
        V, Variant, static_cast<int32>(Msg.MessageType), ProbeInstigator ? 1 : 0, Body.Len());
    ++ChatProbeNext;
    World->GetTimerManager().SetTimer(ChatProbeTimer, this, &ANodeShuffleSubsystem::ChatProbeStep, ChatProbeGapSeconds, false);
}
