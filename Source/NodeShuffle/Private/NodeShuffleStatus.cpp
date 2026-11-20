// ns-status-1 (2026-11-15): first-shuffle status on Random World Generator worlds. See NodeShuffleStatus.h.
// DISPLAY ONLY. The stage is computed on the existing 5 s RefreshTick from values RwgObserveTick already computed (StatusNoteGate),
// the first-roll call site (StatusNoteFirstRoll) and the roll-first per-pass report (StatusNoteRollFirst); no actor scan is added.
// IsStartReady (RWG's readiness getter, otherwise log-only) decides ONLY when the S0 chat line and panel may first show; it feeds no
// roll, latch or apply clause (check_status_lint pins its one reader to this file).
// Log lines print what this run measured (stage, the predicates that chose it, counts), never a cause.
#include "NodeShuffleStatus.h"
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

const TCHAR* NodeShuffleStatus::StageName(EStage S)
{
    switch (S)
    {
    case EStage::S0: return TEXT("S0");
    case EStage::S0x: return TEXT("S0x");
    case EStage::S1: return TEXT("S1");
    case EStage::S1floor: return TEXT("S1floor");
    case EStage::Soon: return TEXT("Soon");
    case EStage::S3: return TEXT("S3");
    case EStage::S4: return TEXT("S4");
    case EStage::S5: return TEXT("S5");
    default: return TEXT("None");
    }
}

void NodeShuffleStatus::StageText(EStage S, int32 N, int32 A, int32 B, FString& OutBody, FString& OutDetail)
{
    // Copy: design Q4, verbatim; split at a sentence boundary into body + detail where it is two sentences.
    OutBody.Reset();
    OutDetail.Reset();
    switch (S)
    {
    case EStage::S0:
        OutBody = TEXT("Waiting for the world generator to report it is ready.");
        OutDetail = TEXT("Your nodes are shuffled after that.");
        break;
    case EStage::S0x:
        OutBody = TEXT("Can't read the world generator's progress, so nodes are not shuffled yet.");
        break;
    case EStage::S1:
        OutBody = FString::Printf(TEXT("World ready. Waiting until no new nodes appear for a minute (%d s so far)."), N);
        break;
    case EStage::S1floor:
        OutBody = TEXT("Waiting for more nodes to load.");
        break;
    case EStage::Soon:
        OutBody = TEXT("Shuffling in a few seconds.");
        OutDetail = TEXT("The game may pause briefly.");
        break;
    case EStage::S3:
        OutBody = FString::Printf(TEXT("Nodes shuffled: %d nodes."), N);
        OutDetail = TEXT("The full table is in chat.");
        break;
    case EStage::S4:
        OutBody = FString::Printf(TEXT("Placing the nodes nearest the players: %d still to place."), A);
        break;
    case EStage::S5:
        OutBody = TEXT("Done.");
        break;
    default:
        break;
    }
}

const TCHAR* NodeShuffleStatus::ChatLineS0()
{
    return TEXT("Node Shuffle: waiting for the world generator to report it is ready. Your nodes are shuffled after that.");
}

const TCHAR* NodeShuffleStatus::ChatLineS1()
{
    return TEXT("Node Shuffle: world ready. Waiting until no new nodes appear for a minute, then shuffling. The game may pause briefly when it shuffles.");
}

bool NodeShuffleStatus::ParseTestStage(const FString& Arg, EStage& OutStage, bool& bOutOff)
{
    const FString A = Arg.TrimStartAndEnd().ToLower();
    bOutOff = A == TEXT("off");
    if (bOutOff) { OutStage = EStage::None; return true; }
    const EStage All[] = { EStage::S0, EStage::S0x, EStage::S1, EStage::S1floor, EStage::Soon, EStage::S3, EStage::S4, EStage::S5 };
    for (const EStage S : All) { if (A == FString(StageName(S)).ToLower()) { OutStage = S; return true; } }
    return false;
}

void ANodeShuffleSubsystem::StatusNoteGate(bool bReadable, bool bStageReady, bool bFloor, const TCHAR* GatedBy, const FString& Stage)
{
    bStsGateNoted = true;
    bStsReadable = bReadable;
    bStsStageReady = bStageReady;
    bStsFloor = bFloor;
    StsGatedBy = GatedBy;
    // S0's "player has control" gate, latched once true: RWG's IsStartReady bound AND true, or the stage name LoadingTerrain. Read only on
    // a world with no layout yet (the first-shuffle case), and never once latched. The accessor never runs the getter bind itself.
    if (!bStsControlSeen && !bLayoutGenerated)
    {
        StsStartReady = NodeShuffleMapMode::RwgStartReadyForStatus(GetWorld());
        bStsControlSeen = StsStartReady == 1 || (bReadable && Stage == TEXT("LoadingTerrain"));
    }
}

void ANodeShuffleSubsystem::StatusNoteFirstRoll()
{
    // The FIRST-SHUFFLE SESSION FLAG: set here, right after the first RollLayout of this session, on RWG only. bLayoutGenerated alone
    // cannot gate the display (it turns true at this roll and S3/S4/S5 still show); a save loaded with a layout never reaches this call.
    if (bStsFirstRollThisSession || GetMapMode() != ENodeShuffleMapMode::Rwg) { return; }
    if (!bLayoutGenerated) { return; } // RollLayout returned without a layout (its "Roll aborted" branch): the flag waits for a roll that produced one
    bStsFirstRollThisSession = true; UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS-ROLLSEEN: observeTick=%d stageAtRoll=%s"), RwgObserveTicks, NodeShuffleStatus::StageName(static_cast<NodeShuffleStatus::EStage>(StsStageNow))); // ns-quietlog-1: always on, once per session
    StsFirstRollRealT = FPlatformTime::Seconds();
    int32 Active = 0;
    for (const FNodeShuffleEntry& E : Layout) { if (E.bActive) { ++Active; } } // the same population as the ROLLCHAT table's Total row
    StsShuffledTotal = Active;
}

void ANodeShuffleSubsystem::StatusNoteRollFirst(int32 AtK, int32 Left, int32 Resources)
{
    if (!bStsFirstRollThisSession || bStsDoneLatched) { return; }
    bStsRfSeen = true;
    StsRfAtK = AtK;
    StsRfLeft = Left;
    StsRfResources = Resources;
    // The S5 ONE-SHOT LATCH: the first Left == 0 of ANY generation. Never reset, so a later generation re-opening Left > 0 never re-opens it.
    if (Left == 0) { bStsDoneLatched = true; StsDoneRealT = FPlatformTime::Seconds(); }
}

// ns-quietlog-1: the word for the return StatusComputeStage took on its last call (printed as NSSTATUS branch=). For a return whose
// condition ORs operands, the word is written just before that test and names the FIRST operand that reads true (when the test is not
// taken, a later return overwrites it); the operands are read again, the condition is unchanged.
static const TCHAR* GStsBranch = TEXT("none");

uint8 ANodeShuffleSubsystem::StatusComputeStage(bool bEnabled)
{
    using namespace NodeShuffleStatus;
    const double Now = FPlatformTime::Seconds();
    GStsBranch = !bEnabled ? TEXT("off") : (bStsDisplayEnded ? TEXT("displayEnded") : TEXT("notRwg")); if (!bEnabled || bStsDisplayEnded || GetMapMode() != ENodeShuffleMapMode::Rwg) { return static_cast<uint8>(EStage::None); }
    if (bStsFirstRollThisSession)
    {
        if (bStsDoneLatched)
        {
            if (Now - StsDoneRealT < DoneHoldSeconds) { GStsBranch = TEXT("doneHold"); return static_cast<uint8>(EStage::S5); }
            bStsDisplayEnded = true; GStsBranch = TEXT("doneEnded");
            return static_cast<uint8>(EStage::None);
        }
        if (Now - StsFirstRollRealT >= DisplayCapSeconds) { bStsDisplayEnded = true; bStsCapReached = true; GStsBranch = TEXT("capReached"); return static_cast<uint8>(EStage::None); }
        GStsBranch = TEXT("postRoll"); return static_cast<uint8>((bStsRfSeen && StsRfLeft > 0) ? EStage::S4 : EStage::S3);
    }
    // Before the first roll: only a world with no layout, only before the readiness latch (the latch tick rolls in the same RefreshTick).
    GStsBranch = bLayoutGenerated ? TEXT("preRollLayoutExists") : (!bStsGateNoted ? TEXT("preRollGateNotNoted") : TEXT("preRollReadyLatched")); if (bLayoutGenerated || !bStsGateNoted || bRwgReady) { return static_cast<uint8>(EStage::None); }
    if (!bStsReadable) { GStsBranch = TEXT("unreadable"); return static_cast<uint8>(bRwgUnreadableWarned ? EStage::S0x : (bStsControlSeen ? EStage::S0 : EStage::None)); }
    if (!bStsStageReady) { GStsBranch = TEXT("stageNotReady"); return static_cast<uint8>(bStsControlSeen ? EStage::S0 : EStage::None); }
    if (StsGatedBy && FCString::Strcmp(StsGatedBy, TEXT("floor")) == 0) { GStsBranch = TEXT("floor"); return static_cast<uint8>(EStage::S1floor); }
    // Soon: one stable sample left. The next tick that reads Ready with an unchanged node count sets the count to the required value,
    // latches and rolls in that same tick; a changed count resets it to 0 (back to S1).
    if (bStsFloor && RwgStableSinceReady == NodeShuffleMapMode::RwgStableTicksRequired - 1) { GStsBranch = TEXT("soon"); return static_cast<uint8>(EStage::Soon); }
    GStsBranch = TEXT("s1"); return static_cast<uint8>(EStage::S1);
}

void ANodeShuffleSubsystem::StatusTick(bool bEnabled)
{
    using namespace NodeShuffleStatus;
    UWorld* World = GetWorld();
    if (!World) { return; }
    const EStage Prev = static_cast<EStage>(StsStageNow);
    const EStage Next = static_cast<EStage>(StatusComputeStage(bEnabled));
    StsStageNow = static_cast<uint8>(Next);
    if (Next == EStage::S0) { bStsChatS0Wanted = true; }
    if (Next == EStage::S1 || Next == EStage::S1floor || Next == EStage::Soon) { bStsChatS1Wanted = true; }
    if (bEnabled) { StatusChatStep(World); }
    StatusApplyPanel(StsStageNow); // the host panel: text refreshed from this 5 s tick only (NodeShuffleStatusPanel.cpp)
    if (Next == Prev && bStsStatusLogged) { return; }
    if (Next == EStage::None && !bStsStatusLogged) { return; } // nothing shown yet: no line for the initial None
    bStsStatusLogged = true;
    bool bLocalPc = false;
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* Pc = It->Get();
        if (Pc && Pc->IsLocalController()) { bLocalPc = true; break; }
    }
    // Legend (prose only, docs/DIAGNOSTICS.md): the stage and every predicate that chose it, as read this tick; startReady prints -1 when
    // the getter was not read or is unbound; the two chat fields say whether each chat line's broadcast call returned true; observeTick is
    // the RWG observe-tick counter; branch names the return the stage computation took (GStsBranch above).
    UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS: stage=%s prev=%s stageReadable=%d stageReady=%d startReady=%d s0Gate=%d stableSinceReady=%d/%d gatedBy=%s floorMet=%d unreadableWarned=%d readyLatched=%d layoutGenerated=%d firstRollThisSession=%d rfSeen=%d resourcesAtK=%d resources=%d rfLeft=%d doneLatched=%d capReached=%d chatS0Posted=%d chatS1Posted=%d netMode=%d localPC=%d widgetAdded=%d panelSetting=%d observeTick=%d branch=%s"),
        StageName(Next), StageName(Prev), bStsReadable ? 1 : 0, bStsStageReady ? 1 : 0, StsStartReady, bStsControlSeen ? 1 : 0,
        RwgStableSinceReady, NodeShuffleMapMode::RwgStableTicksRequired, StsGatedBy ? StsGatedBy : TEXT("none"), bStsFloor ? 1 : 0,
        bRwgUnreadableWarned ? 1 : 0, bRwgReady ? 1 : 0, bLayoutGenerated ? 1 : 0, bStsFirstRollThisSession ? 1 : 0, bStsRfSeen ? 1 : 0,
        StsRfAtK, StsRfResources, StsRfLeft, bStsDoneLatched ? 1 : 0, bStsCapReached ? 1 : 0, bStsChatS0Posted ? 1 : 0,
        bStsChatS1Posted ? 1 : 0, static_cast<int32>(World->GetNetMode()), bLocalPc ? 1 : 0, bStsWidgetAdded ? 1 : 0, StsPanelSetting, RwgObserveTicks, GStsBranch);
}

void ANodeShuffleSubsystem::StatusChatStep(UWorld* World)
{
    using namespace NodeShuffleStatus;
    // After the first roll an unposted line is stale: drop it (counted in the log, never posted later).
    if (bStsFirstRollThisSession)
    {
        if (bStsChatS0Wanted && !bStsChatS0Done) { bStsChatS0Done = true; UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS-CHAT: line=S0 result=dropped firstRollThisSession=1")); }
        if (bStsChatS1Wanted && !bStsChatS1Done) { bStsChatS1Done = true; UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS-CHAT: line=S1 result=dropped firstRollThisSession=1")); }
        return;
    }
    // S0 never posts once S1 has been entered (its "after that" is then stale).
    if (bStsChatS1Wanted && bStsChatS0Wanted && !bStsChatS0Done) { bStsChatS0Done = true; UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS-CHAT: line=S0 result=dropped s1Entered=1")); }
    const bool bS1 = bStsChatS1Wanted && !bStsChatS1Done;
    const bool bS0 = !bS1 && bStsChatS0Wanted && !bStsChatS0Done;
    if (!bS1 && !bS0) { return; }
    const TCHAR* Line = bS1 ? TEXT("S1") : TEXT("S0");
    // The same gates as the other notices: a player pawn, then one more tick (RwgGateNotice.cpp TickNotice).
    const APlayerController* PC = World->GetFirstPlayerController();
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!PC || !Pawn)
    {
        if (!bStsChatHeldLogged) { bStsChatHeldLogged = true; UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS-CHAT: line=%s result=held pc=%d pawn=%d"), Line, PC ? 1 : 0, Pawn ? 1 : 0); }
        return;
    }
    if (StsChatHold < 0) { StsChatHold = 0; UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS-CHAT: line=%s result=holdingOneTick"), Line); return; }
    ++StsChatAttempts;
    const bool bPosted = FNodeShuffleModule::PostChatNotice(World, bS1 ? ChatLineS1() : ChatLineS0(), TEXT("NSSTATUS"));
    if (bPosted || StsChatAttempts >= ChatMaxAttempts)
    {
        if (bS1) { bStsChatS1Done = true; bStsChatS1Posted = bPosted; } else { bStsChatS0Done = true; bStsChatS0Posted = bPosted; }
        UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS-CHAT: line=%s result=%s attempt=%d"), Line, bPosted ? TEXT("posted") : TEXT("abandoned"), StsChatAttempts);
        StsChatAttempts = 0;
        StsChatHold = -1;
        bStsChatHeldLogged = false;
    }
}
