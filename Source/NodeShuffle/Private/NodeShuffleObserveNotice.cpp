// ---- T61 (ns-t61-observe-always, 2026-08-10) ----------------------------------------------------
// THE "THIS RESOURCE IS NOT IN YOUR LIST YET" CHAT NOTICE, plus the ONE chat emitter both notices use.
//
// THE AUTHOR'S RULING THIS IMPLEMENTS, verbatim (2026-08-10): "default off, but we need detection if
// off or on to build our list and show in chat if not in our list and to show in config the list for
// allowing users to opt in." The veto hook now arms in either state (docs/TECH-DEBT.md T61); this file
// is the "show in chat if not in our list" half.
//
// WHY THE EMITTER LIVES HERE and not beside the pending notice it was extracted from: NodeShufflePending
// Notice.cpp is at ~470 lines and this project caps a source file at 500. The extraction itself is the
// point — there is now exactly ONE place that builds an FChatMessageStruct, so the two notices cannot
// drift into different renderings of the same mechanics.
//
// THE TRIGGER IS A ROW ADD, NOT A SIGHTING, and that is a deliberate honesty constraint rather than a
// convenience. The message tells the player the resource is now IN the settings list; queueing on the
// sighting would make that sentence a prediction (the population pass can be capped, or the config tree
// unreachable), while queueing on the successful add makes it a report of something already done. It
// also gives the dedup for free: a resource with a row on disk is never re-added, so it is never
// re-announced on a later load — the same self-clearing state test the pending notice is built on, with
// no persisted flag anywhere.
//
// WHAT IT DOES NOT DO: it never says the resource is protected unless this world is actually enforcing
// (IsForeignProtectionActingThisWorld), because the shipped default is observe-only and a notice that
// promised protection in that state would be a false claim to the player.

#include "NodeShuffle.h"

#include "FGChatManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace
{
    // Anti-nag caps, the same shape as the pending notice's (a chat wall is its own kind of nag).
    constexpr int32 MaxNamedResources = 6;
    // Same budget as MaxPendingNoticeEmitAttempts: ~12 RefreshTicks at ~5 s is about a minute of trying.
    constexpr int32 MaxNoticeEmitAttempts = 12;

    struct FNodeShuffleForeignNoticeItem
    {
        FString ResourceClassPath;  // identity, and the announced-set key
        FString DisplayName;        // the label the row carries; the descriptor class name, not an item name
    };

    // Session state. Game-thread only, like every other module static on this path, and cleared per world
    // init by ResetForeignNoticeState (which ResetSeenForeignResources calls, so the notice queue and the
    // sighting registry can never describe two different world sessions).
    TArray<FNodeShuffleForeignNoticeItem> GNodeShuffleForeignNoticeQueue;
    TSet<FString> GNodeShuffleForeignNoticeAnnounced;
    int32 GNodeShuffleForeignNoticeGateTicks = -1;
    int32 GNodeShuffleForeignNoticeEmitAttempts = 0;
}

// ---------------------------------------------------------------------------------------------
// THE ONE CHAT EMITTER (extracted verbatim from EmitPendingNotice's PostMessage lambda, T61).
// ---------------------------------------------------------------------------------------------
// COLD REVIEW F1: every line here names its CALLER. Both notices share these three lines now, so
// without the tag a log carrying both could attribute neither -- and a pre-T61 grep for
// `PENDINGNOTICE: EMITTED` finds nothing on this build, which is why the rename is called out in the
// pending notice's comment and in docs/DIAGNOSTICS.md rather than left for a reader to discover.
bool FNodeShuffleModule::PostChatNotice(UWorld* World, const FString& Body, const TCHAR* SourceTag)
{
    AFGChatManager* Chat = World ? AFGChatManager::Get(World) : nullptr;
    if (!Chat)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("CHATNOTICE[%s]: AFGChatManager::Get(World) returned null (world=%s) -- message NOT ")
            TEXT("delivered (bodyChars=%d). The caller decides whether to retry."),
            SourceTag, World ? TEXT("valid") : TEXT("null"), Body.Len());
        return false;
    }

    FChatMessageStruct Msg;
    Msg.MessageText = FText::FromString(Body);
    // ASSUMED (test item, not a claim): FGChatManager.h:38 says "System and ADA messages override the
    // sender", so CMT_SystemMessage/CMT_AdaMessage would discard our sender name. CMT_CustomMessage is
    // the type that plausibly honours it -- but the rendering lives in Blueprint UI we cannot read.
    // If the sender does not show, switch to CMT_SystemMessage and drop MessageSender.
    Msg.MessageType = EFGChatMessageType::CMT_CustomMessage;
    Msg.MessageSender = FText::FromString(TEXT("NODE SHUFFLE"));
    Msg.MessageSenderColor = FLinearColor(1.0f, 0.55f, 0.10f); // FICSIT orange
    // ServerTimeStamp / bIsLocalPlayerMessage are set by the chat manager -- do not touch.
    Chat->BroadcastChatMessage(Msg, nullptr);
    UE_LOG(LogNodeShuffle, Display,
        TEXT("CHATNOTICE[%s]: EMITTED via AFGChatManager::BroadcastChatMessage (type=%d sender='NODE SHUFFLE' bodyChars=%d)"),
        SourceTag, (int32)EFGChatMessageType::CMT_CustomMessage, Body.Len());
    // ASSUMED: a NetMulticast RPC executes locally in Standalone, so the host sees this without a
    // separate local echo. If in-game testing shows it does not, add AddChatMessageToReceived(Msg)
    // HERE -- this comment names the exact line so the fix needs no re-investigation.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("CHATNOTICE[%s] ROUTE: BroadcastChatMessage taken; local echo NOT separately added ")
        TEXT("(multicast is expected to execute locally -- if it does not, add AddChatMessageToReceived here)."),
        SourceTag);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Queue / reset.
// ---------------------------------------------------------------------------------------------
void FNodeShuffleModule::ResetForeignNoticeState()
{
    GNodeShuffleForeignNoticeQueue.Reset();
    GNodeShuffleForeignNoticeAnnounced.Empty();
    GNodeShuffleForeignNoticeGateTicks = -1;
    GNodeShuffleForeignNoticeEmitAttempts = 0;
}

void FNodeShuffleModule::NoteUnlistedForeignResourceForNotice(const FString& ResourceClassPath,
    const FString& DisplayName)
{
    if (ResourceClassPath.IsEmpty()) { return; }
    if (GNodeShuffleForeignNoticeAnnounced.Contains(ResourceClassPath)) { return; }
    for (const FNodeShuffleForeignNoticeItem& Item : GNodeShuffleForeignNoticeQueue)
    {
        if (Item.ResourceClassPath == ResourceClassPath) { return; }
    }
    FNodeShuffleForeignNoticeItem& New = GNodeShuffleForeignNoticeQueue.AddDefaulted_GetRef();
    New.ResourceClassPath = ResourceClassPath;
    New.DisplayName = DisplayName;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("T61NOTICE: queued resource '%s' (label '%s') -- it had no row in the protection list and one ")
        TEXT("was just added. Queue now holds %d item(s); %d already announced this session."),
        *ResourceClassPath, *DisplayName, GNodeShuffleForeignNoticeQueue.Num(),
        GNodeShuffleForeignNoticeAnnounced.Num());
}

namespace
{
    // Composes the body. THREE branches, because the three states genuinely differ in what is true of
    // the player's world, and one message covering all of them could only do it by saying less than it
    // knows or more than it measured. EVERY FACTUAL ASSERTION IS GRADED:
    //   * "not part of the base game's content" -- MEASURED: the classifier grades a node Foreign only
    //     when its actor class path or its resource class path is outside /Game/ (the same two-sided
    //     test the roll uses).
    //   * "another mod's node handler checked nodes of it this session" -- MEASURED: the sighting came
    //     through a KBFL actor listener/destroyer asset our arm pass prepended into. It deliberately
    //     does NOT say the other mod tried to REMOVE anything: this hook sees a requirement evaluation,
    //     not a destroy (the same distinction T58's log copy is held to).
    //   * "added to <list>, ticked" -- MEASURED: this notice is queued from the successful row add.
    //   * the protection sentences -- each branch states only the latch its own world is running under,
    //     and the enforcing branch's wording is the settings list's own graded wording, not a new claim.
    //   * NOT CLAIMED anywhere: that anything was saved, removed or restored; that unticking brings back
    //     nodes already gone; any timing.
    FString BuildForeignNoticeBody(const TArray<FNodeShuffleForeignNoticeItem*>& Items)
    {
        FString Body = FString::Printf(
            TEXT("NODE SHUFFLE - %d resource(s) from other mods detected\n\n")
            TEXT("These are not part of the base game's content, and another mod's node handler\n")
            TEXT("checked nodes of them this session:\n"),
            Items.Num());
        for (int32 i = 0; i < Items.Num() && i < MaxNamedResources; ++i)
        {
            Body += FString::Printf(TEXT("  * %s\n"), *Items[i]->DisplayName);
        }
        if (Items.Num() > MaxNamedResources)
        {
            Body += FString::Printf(TEXT("  ...and %d more.\n"), Items.Num() - MaxNamedResources);
        }
        Body += TEXT("\nEach one has been added, TICKED, to \"Protect Other Mods' Resources (Per\n")
                TEXT("Resource)\" in NodeShuffle's mod settings.\n");

        if (FNodeShuffleModule::IsVetoObservingOnlyThisWorld())
        {
            Body += TEXT("\nNodeShuffle is NOT protecting them right now: that protection is off by\n")
                    TEXT("default. To turn it on, set the console variable NodeShuffle.DestroyerVeto\n")
                    TEXT("to 1 and load the save again. Until then this is only a heads-up, and\n")
                    TEXT("NodeShuffle is not changing what any other mod does.");
        }
        else if (FNodeShuffleModule::IsForeignProtectionActingThisWorld())
        {
            Body += TEXT("\nWhile a row stays ticked, NodeShuffle steps in to stop that removal for\n")
                    TEXT("that resource. Untick a row and load the save again to hand that one\n")
                    TEXT("resource back to the other mod.");
        }
        else
        {
            Body += TEXT("\nNodeShuffle is NOT protecting them this session: NodeShuffle.ProtectForeignNodes\n")
                    TEXT("is 0. Set it to 1 and load the save again for the ticks in that list to do\n")
                    TEXT("anything.");
        }
        return Body;
    }
}

// ---------------------------------------------------------------------------------------------
// The deferred emitter. THE SAME FOUR GATES, IN THE SAME ORDER, as EmitPendingNoticeIfReady -- and for
// the same measured reasons (see that file's header): config off -> drop; nothing unannounced -> drop;
// no player spawned -> hold; one further tick -> hold once; then emit, with a bounded retry.
// ---------------------------------------------------------------------------------------------
void FNodeShuffleModule::TickForeignNoticeEmitter(UWorld* World, bool bNoticesEnabled)
{
    if (GNodeShuffleForeignNoticeQueue.Num() == 0) { return; } // the overwhelmingly common case
    if (!World) { return; }

    if (!bNoticesEnabled)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("T61NOTICE: decision=SUPPRESSED(config-off) -- 'Show Compatibility Notices In Chat' is ")
            TEXT("off; %d queued resource(s) dropped. The rows are still in the settings list and the ")
            TEXT("T60 log lines above still name them."),
            GNodeShuffleForeignNoticeQueue.Num());
        GNodeShuffleForeignNoticeQueue.Reset();
        GNodeShuffleForeignNoticeGateTicks = -1;
        GNodeShuffleForeignNoticeEmitAttempts = 0;
        return;
    }

    // DELTA-ONLY, the T55 rule: only resources not already announced THIS session can produce a
    // message. Across sessions the dedup is structural instead -- a resource with a row on disk never
    // reaches the queue at all.
    TArray<FNodeShuffleForeignNoticeItem*> Unannounced;
    for (FNodeShuffleForeignNoticeItem& Item : GNodeShuffleForeignNoticeQueue)
    {
        if (!GNodeShuffleForeignNoticeAnnounced.Contains(Item.ResourceClassPath))
        {
            Unannounced.Add(&Item);
        }
    }
    if (Unannounced.Num() == 0)
    {
        // COLD REVIEW F8: THIS BRANCH IS UNREACHABLE BY CONSTRUCTION IN THIS BUILD and is kept anyway,
        // deliberately. The queue can only hold unannounced items (the queue function filters on the
        // announced set, and that set is written only immediately before the queue is emptied), so a
        // run that prints this line is telling you one of those two invariants has been broken by a
        // later edit -- which is worth more than the four lines it costs. docs/DIAGNOSTICS.md says the
        // same, so a reader does not go looking for the run that produces it.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("T61NOTICE: decision=SUPPRESSED(duplicate) -- %d queued, 0 of them unannounced this ")
            TEXT("session. NOTE: unreachable by construction in this build; seeing this line means the ")
            TEXT("queue/announced-set invariant has been broken."), GNodeShuffleForeignNoticeQueue.Num());
        GNodeShuffleForeignNoticeQueue.Reset();
        GNodeShuffleForeignNoticeGateTicks = -1;
        GNodeShuffleForeignNoticeEmitAttempts = 0;
        return;
    }

    const APlayerController* PC = World->GetFirstPlayerController();
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!PC || !Pawn)
    {
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("T61NOTICE: decision=SUPPRESSED(no-local-player) pc=%s pawn=%s -- holding %d item(s)"),
            PC ? TEXT("valid") : TEXT("null"), Pawn ? TEXT("valid") : TEXT("null"),
            GNodeShuffleForeignNoticeQueue.Num());
        return;
    }

    if (GNodeShuffleForeignNoticeGateTicks < 0)
    {
        GNodeShuffleForeignNoticeGateTicks = 0;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("T61NOTICE: player spawned; holding %d item(s) for ONE more tick before posting to chat ")
            TEXT("(the message reaches chat history regardless -- this only protects the toast)."),
            GNodeShuffleForeignNoticeQueue.Num());
        return;
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("T61NOTICE: decision=EMIT (%d queued, %d unannounced) observing %d foreignProtectActing %d ")
        TEXT("attempt %d of %d"),
        GNodeShuffleForeignNoticeQueue.Num(), Unannounced.Num(),
        IsVetoObservingOnlyThisWorld() ? 1 : 0, IsForeignProtectionActingThisWorld() ? 1 : 0,
        GNodeShuffleForeignNoticeEmitAttempts + 1, MaxNoticeEmitAttempts);

    if (PostChatNotice(World, BuildForeignNoticeBody(Unannounced), TEXT("T61")))
    {
        // Announce AFTER a delivery actually succeeded -- marking them announced on a failed post would
        // suppress the retry and lose the notice entirely (the pending notice's F3 lesson, unchanged).
        for (const FNodeShuffleForeignNoticeItem* Item : Unannounced)
        {
            GNodeShuffleForeignNoticeAnnounced.Add(Item->ResourceClassPath);
        }
        GNodeShuffleForeignNoticeQueue.Reset();
        GNodeShuffleForeignNoticeGateTicks = -1;
        GNodeShuffleForeignNoticeEmitAttempts = 0;
        return;
    }

    ++GNodeShuffleForeignNoticeEmitAttempts;
    if (GNodeShuffleForeignNoticeEmitAttempts >= MaxNoticeEmitAttempts)
    {
        UE_LOG(LogNodeShuffle, Error,
            TEXT("T61NOTICE: decision=ABANDONED(chat-manager-never-available) after %d attempts -- %d ")
            TEXT("item(s) DROPPED to stop an unbounded retry log. The settings rows were still added and ")
            TEXT("the T60/T61 log lines above name every resource."),
            GNodeShuffleForeignNoticeEmitAttempts, GNodeShuffleForeignNoticeQueue.Num());
        GNodeShuffleForeignNoticeQueue.Reset();
        GNodeShuffleForeignNoticeGateTicks = -1;
        GNodeShuffleForeignNoticeEmitAttempts = 0;
    }
}
