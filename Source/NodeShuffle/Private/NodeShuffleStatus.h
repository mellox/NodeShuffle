// ns-status-1 (2026-11-15): what NodeShuffle is doing while the FIRST shuffle of a Random World Generator world waits.
// ONE stage model and ONE stage -> text function feed the chat lines, the host panel, the NSSTATUS log line and the
// NodeShuffle.StatusPanelTest command. DISPLAY ONLY: no roll, readiness latch or apply clause reads anything declared here.
// Design + graded copy: _team/test-logs/2026-11-15-nodeshuffle-statuspanel-design.md (Q3 data per stage, Q4 copy).
#pragma once

#include "CoreMinimal.h"

namespace NodeShuffleStatus
{
    // None = nothing to show. S0 waiting for the generator, S0x its stage unreadable, S1 world ready and holding, S1floor holding on
    // the node floor, Soon one stable sample left, S3 shuffled, S4 placing the nearest nodes, S5 done.
    enum class EStage : uint8 { None, S0, S0x, S1, S1floor, Soon, S3, S4, S5 };

    const TCHAR* StageName(EStage S);

    // THE text function. N: S1 seconds so far, S3 node total; A/B: S4 resources ready / resources. Empty body = show nothing.
    // Never a cause for a pause, never an ETA, never a raw RWG stage name; the S1 seconds count up and reset, never down.
    void StageText(EStage S, int32 N, int32 A, int32 B, FString& OutBody, FString& OutDetail);

    // The two chat lines (host posts each at most once per session; never re-posted on a regress or a stability reset).
    const TCHAR* ChatLineS0();
    const TCHAR* ChatLineS1();

    constexpr double DisplayCapSeconds = 600.0; // after the first roll: the panel ends silently if S5 never comes
    constexpr double DoneHoldSeconds = 10.0;    // S5 shows "Done." this long, then the display ends for the session
    constexpr int32 ChatMaxAttempts = 12;       // a chat line whose chat manager never answers is abandoned after this many ticks
    constexpr int32 StatusTickSeconds = 5;      // RefreshTick's period (NodeShuffleSubsystem.cpp TickIntervalSeconds; check_status_lint T12 pins both)

    // The host panel (NodeShuffleStatusWidget / NodeShuffleStatusPanel.cpp). ONE named constant places it: moving it is a one-line change.
    // RWG's own corner card position is not known yet (design Q1), so the default stays off the corners.
    enum class EPanelPos : uint8 { TopLeft, TopRight, BottomLeft, BottomRight, MiddleLeft };
    constexpr EPanelPos PanelPosition = EPanelPos::MiddleLeft;
    constexpr float PanelMarginPx = 24.f;       // from the screen edge(s) the position names
    constexpr float PanelWidthPx = 620.f;       // fixed; no font measuring (a longer line runs past the right edge: game-only check)
    constexpr int32 PanelZOrder = 40;           // AddToViewport z-order (UrDead's reticle uses 50)

    // NodeShuffle.StatusPanelTest argument: s0, s0x, s1, s1floor, soon, s3, s4, s5 or off. False = not a known stage.
    bool ParseTestStage(const FString& Arg, EStage& OutStage, bool& bOutOff);
}
