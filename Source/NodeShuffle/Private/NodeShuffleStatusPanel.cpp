// ns-status-1 (2026-11-15): the host status panel's lifetime (create, text, remove) and the NodeShuffle.StatusPanelTest command.
// Route mirrored from UrDead (UrDeadWeapon_Hud.cpp:145-159 create, :310 remove): the FIRST LOCAL player controller owns the widget,
// CreateWidget<T>(PC, T::StaticClass()) + AddToViewport, RemoveFromParent on teardown. A dedicated server has no local player controller:
// no widget, one NSSTATUS-PANEL line. The widget is held by the subsystem's UPROPERTY (StatusWidget), so it can neither be collected
// while shown nor dangle: EndPlay and every end of the display remove it and clear the pointer. DISPLAY ONLY.
#include "NodeShuffleStatus.h"
#include "NodeShuffleStatusWidget.h"
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleConfig.h"
#include "NodeShuffle.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

void ANodeShuffleSubsystem::StatusEnsurePanel()
{
    if (IsValid(StatusWidget)) { return; }
    UWorld* World = GetWorld();
    if (!World) { return; }
    APlayerController* LocalPc = nullptr;
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* Pc = It->Get();
        if (Pc && Pc->IsLocalController()) { LocalPc = Pc; break; }
    }
    if (!LocalPc)
    {
        if (!bStsNoLocalPcLogged)
        {
            bStsNoLocalPcLogged = true;
            UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS-PANEL: shown=0 localPC=0 netMode=%d; no widget is created without a local player controller"), static_cast<int32>(World->GetNetMode()));
        }
        return;
    }
    StatusWidget = CreateWidget<UNodeShuffleStatusWidget>(LocalPc, UNodeShuffleStatusWidget::StaticClass());
    if (StatusWidget)
    {
        StatusWidget->AddToViewport(NodeShuffleStatus::PanelZOrder);
        bStsWidgetAdded = true;
    }
    UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS-PANEL: created=%d addToViewportCalled=%d zOrder=%d"), StatusWidget ? 1 : 0, bStsWidgetAdded ? 1 : 0, NodeShuffleStatus::PanelZOrder);
}

void ANodeShuffleSubsystem::StatusRemovePanel(const TCHAR* Why)
{
    if (!IsValid(StatusWidget)) { StatusWidget = nullptr; bStsWidgetAdded = false; return; }
    StatusWidget->RemoveFromParent();
    StatusWidget = nullptr;
    bStsWidgetAdded = false;
    UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS-PANEL: removed reason=%s"), Why);
}

void ANodeShuffleSubsystem::StatusApplyPanel(uint8 StageU8)
{
    using namespace NodeShuffleStatus;
    if (bStsTestActive) { return; } // NodeShuffle.StatusPanelTest owns the widget until 'off'
    const EStage S = static_cast<EStage>(StageU8);
    if (S == EStage::None)
    {
        StatusRemovePanel(bStsDisplayEnded ? TEXT("displayEnded") : TEXT("stageNone"));
        return;
    }
    const FNodeShuffleConfigStruct Config = FNodeShuffleConfigStruct::GetActiveConfig(this);
    StsPanelSetting = Config.ShowStatusPanel ? 1 : 0;
    if (!Config.ShowStatusPanel) { StatusRemovePanel(TEXT("settingOff")); return; }
    FString Body, Detail;
    const int32 N = (S == EStage::S1) ? RwgStableSinceReady * StatusTickSeconds : StsShuffledTotal;
    StageText(S, N, StsRfLeft, 0, Body, Detail); // S4: the measured picks-left count (review F2 option a)
    StatusEnsurePanel();
    if (!IsValid(StatusWidget)) { StatusWidget = nullptr; bStsWidgetAdded = false; return; }
    StatusWidget->Title = TEXT("Node Shuffle");
    StatusWidget->Body = Body;
    StatusWidget->Detail = Detail;
}

void ANodeShuffleSubsystem::StatusRunPanelTest(const FString& Arg)
{
    // DISPLAY ONLY: fake numbers, a "(test)" title, no chat, no stage, latch, layout, store or save field. It writes only the panel
    // members (the widget pointer and the test-active flag that pauses the real panel's updates until 'off').
    using namespace NodeShuffleStatus;
    EStage S = EStage::None;
    bool bOff = false;
    if (!ParseTestStage(Arg, S, bOff))
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS-TEST: unknown stage '%s'; use s0, s0x, s1, s1floor, soon, s3, s4, s5 or off"), *Arg);
        return;
    }
    if (bOff)
    {
        bStsTestActive = false;
        StatusRemovePanel(TEXT("testOff"));
        UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS-TEST: off; the real panel resumes on the next status tick"));
        return;
    }
    FString Body, Detail;
    StageText(S, S == EStage::S1 ? 35 : 1032, 4, 0, Body, Detail);
    bStsTestActive = true;
    StatusEnsurePanel();
    if (StatusWidget)
    {
        StatusWidget->Title = TEXT("Node Shuffle (test)");
        StatusWidget->Body = Body;
        StatusWidget->Detail = Detail;
    }
    UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS-TEST: stage=%s widget=%d"), StageName(S), StatusWidget ? 1 : 0);
}

// ns-status-1: registered the same way as NodeShuffle.DumpLayout (NodeShuffleLayoutDump.cpp): a console command with world and args,
// first subsystem in the world. Works on any map and save, including vanilla and a disabled mod (it never reads the map mode).
static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleStatusPanelTestCmd(
    TEXT("NodeShuffle.StatusPanelTest"),
    TEXT("Display only. Shows the first-shuffle status panel for one stage with fake numbers, titled 'Node Shuffle (test)': ")
    TEXT("s0, s0x, s1, s1floor, soon, s3, s4, s5; 'off' removes it. Posts no chat and writes no layout, store or save field."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS-TEST: no world; nothing shown."));
            return;
        }
        for (TActorIterator<ANodeShuffleSubsystem> It(World); It; ++It)
        {
            It->StatusRunPanelTest(Args.Num() > 0 ? Args[0] : FString());
            return;
        }
        UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS-TEST: NodeShuffle subsystem not found in this world; nothing shown."));
    }));
