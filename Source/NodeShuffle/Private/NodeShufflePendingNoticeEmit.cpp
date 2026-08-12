// ns-review-notice: the DEFERRED EMITTER half of the pending-allow-list notice.
//
// WHY THIS IS ITS OWN FILE. NodeShufflePendingNotice.cpp holds the FNodeShuffleModule statics -- the
// pairing rule, the entry builder, the signature, and the chat composition. This holds the one
// ANodeShuffleSubsystem member that drives them per RefreshTick. Splitting on that boundary (module
// statics vs subsystem member) kept both files under the 500-line limit without inventing a seam:
// everything here is about WHEN to speak, everything there is about WHAT to say.
//
// WHY DEFERRED AT ALL: the AUTOALLOW pass completes ~19-50 s after boot. The world exists by then, but
// the local player's chat widget may not, and a toast posted into nothing is a notice the player never
// sees. BroadcastChatMessage puts the message into the received-message history either way, so the
// message is not LOST -- only the toast is at risk. Deferring costs one tick and removes that risk.
//
// THE FOUR GATES, in the order they run and for the reason each exists:
//   1. config off        -> drop (the player asked for silence; the log still has everything)
//   2. nothing UNANNOUNCED -> drop (a re-roll re-runs the pass; a SHRINKING set is not news)
//   3. no player spawned -> hold (nobody to tell yet -- true on a dedicated server with nobody on)
//   4. one further tick  -> hold once (let the chat widget come up before posting)
// then emit, and on failure retry a BOUNDED number of times before giving up loudly.

#include "NodeShuffle.h"
#include "NodeShuffleSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace
{
    // ns-review-notice F3: ~12 RefreshTicks at ~5 s each is about a minute of trying before we stop.
    constexpr int32 MaxPendingNoticeEmitAttempts = 12;
}

// The deferred emitter, called from ANodeShuffleSubsystem::RefreshTick. (ns-review-notice2 F-D: the
// "WHY DEFERRED AT ALL" paragraph that used to be repeated verbatim here is stated once, in the file
// header above -- it was a paste artifact of the split.)
// ---------------------------------------------------------------------------------------------
void ANodeShuffleSubsystem::EmitPendingNoticeIfReady(bool bNoticesEnabled)
{
    if (PendingNoticeQueue.Num() == 0) { return; } // the overwhelmingly common case: cost is one compare

    UWorld* World = GetWorld();
    if (!World) { return; }

    const FString Signature = FNodeShuffleModule::BuildPendingNoticeSignature(PendingNoticeQueue);

    // TODO(2026-08-11, T68) PARKED, NOT LIVE: 'Show Compatibility Notices In Chat' was deleted and every
    // caller now passes true, so this arm cannot run in a shipped build. Kept as the seam a future
    // opt-out would re-enter at, and so the suppression stays counted if one ever does.
    if (!bNoticesEnabled)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("PENDINGNOTICE: signature='%s' lastSignature='%s' -> decision=SUPPRESSED(config-off) ")
            TEXT("('Show Compatibility Notices In Chat' is off; %d entr(ies) dropped, log above still has them)"),
            *Signature, *LastNotifiedSignature, PendingNoticeQueue.Num());
        PendingNoticeQueue.Empty();
        PendingNoticeGateTicks = -1;
        PendingNoticeEmitAttempts = 0;
        return;
    }

    // Duplicate check BEFORE the spawn gate: a set we have already announced should be dropped
    // immediately rather than held across ticks waiting for a player who would then be told twice.
    //
    // ns-review-notice F4: this is a SUBSET test, not an identity test. Identity made any change look
    // new, so a re-roll that merely SHRANK the pending set (the last node accepting resource B gets
    // dealt away, pending goes {A,B} -> {A}) produced a second "restart required" message ~40 s later
    // naming a strict subset of what was already announced. Nothing new happened, so nothing should be
    // said. A genuinely NEW entry -- a schematic unlocked mid-session -- still contains an unannounced
    // key and still gets through, which is the behaviour that must be preserved.
    TArray<FString> Keys;
    FNodeShuffleModule::BuildPendingNoticeKeys(PendingNoticeQueue, Keys);
    int32 NewKeyCount = 0;
    for (const FString& K : Keys)
    {
        if (!AnnouncedPendingKeys.Contains(K)) { ++NewKeyCount; }
    }
    if (NewKeyCount == 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("PENDINGNOTICE: signature='%s' lastSignature='%s' -> decision=SUPPRESSED(duplicate) ")
            TEXT("(%d entr(ies), 0 of them unannounced -- already covered by a message this session; a ")
            TEXT("re-roll re-runs the pass and a SHRINKING set is not news)"),
            *Signature, *LastNotifiedSignature, Keys.Num());
        PendingNoticeQueue.Empty();
        PendingNoticeGateTicks = -1;
        PendingNoticeEmitAttempts = 0;
        return;
    }

    // Readiness gate. NOTE this works on a DEDICATED SERVER too: GetFirstPlayerController() on the
    // server returns the first player controller in the world -- remote players included -- so "a player
    // has spawned" is the same test in every net mode, and a server with nobody connected correctly
    // waits rather than multicasting to an empty room.
    const APlayerController* PC = World->GetFirstPlayerController();
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!PC || !Pawn)
    {
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("PENDINGNOTICE: decision=SUPPRESSED(no-local-player) pc=%s pawn=%s -- holding %d entr(ies), retry next tick"),
            PC ? TEXT("valid") : TEXT("null"), Pawn ? TEXT("valid") : TEXT("null"), PendingNoticeQueue.Num());
        return;
    }

    // One further tick after the player exists, so the chat widget has had a beat to come up.
    if (PendingNoticeGateTicks < 0)
    {
        PendingNoticeGateTicks = 0;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("PENDINGNOTICE: player spawned; holding %d entr(ies) for ONE more tick before posting to chat ")
            TEXT("(the message is added to chat history regardless -- this only protects the toast)."),
            PendingNoticeQueue.Num());
        return;
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("PENDINGNOTICE: signature='%s' lastSignature='%s' -> decision=EMIT (%d entr(ies), %d of them ")
        TEXT("not previously announced) attempt=%d/%d"),
        *Signature, *LastNotifiedSignature, PendingNoticeQueue.Num(), NewKeyCount,
        PendingNoticeEmitAttempts + 1, MaxPendingNoticeEmitAttempts);

    if (FNodeShuffleModule::EmitPendingNotice(World, PendingNoticeQueue))
    {
        LastNotifiedSignature = Signature;
        // Union AFTER a delivery actually succeeded -- marking keys announced on a failed post would
        // suppress the retry and lose the notice entirely.
        AnnouncedPendingKeys.Append(TSet<FString>(Keys));
        PendingNoticeQueue.Empty();
        PendingNoticeGateTicks = -1;
        PendingNoticeEmitAttempts = 0;
        return;
    }

    // ns-review-notice F3: EmitPendingNotice already logged WHY (chat manager not up). Retrying is right,
    // retrying FOREVER is not: three log lines every ~5 s for the rest of the session is the unbounded
    // per-call output this workspace has already paid for once. Give up loudly after ~1 minute.
    ++PendingNoticeEmitAttempts;
    if (PendingNoticeEmitAttempts >= MaxPendingNoticeEmitAttempts)
    {
        UE_LOG(LogNodeShuffle, Error,
            TEXT("PENDINGNOTICE: decision=ABANDONED(chat-manager-never-available) after %d attempts over ")
            TEXT("~%d ticks -- AFGChatManager::Get(World) never returned an object, so the notice cannot be ")
            TEXT("delivered this session and %d entr(ies) are being DROPPED to stop an unbounded retry log. ")
            TEXT("The pending set itself is unaffected and the next boot's pass will recompute it; the ")
            TEXT("per-extractor detail is in the PENDINGNOTICE lines above."),
            PendingNoticeEmitAttempts, MaxPendingNoticeEmitAttempts, PendingNoticeQueue.Num());
        PendingNoticeQueue.Empty();
        PendingNoticeGateTicks = -1;
        PendingNoticeEmitAttempts = 0;
    }
}
