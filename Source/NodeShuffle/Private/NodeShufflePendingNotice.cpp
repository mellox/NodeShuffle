// ns-h1b-notice: the "restart required" player notice.
//
// THE DEFECT THIS EXISTS TO REMOVE. NodeShuffle's AUTOALLOW pass writes KDataForge documents that
// append extractor classes to SF+'s allow-list. KDataForge reads packs at GAME-INSTANCE INIT; we write
// them ~19-50 s after the world is up. So a document written at boot N only takes effect at boot N+1,
// and until then the player gets a red hologram / "Invalid aim location!" with NO in-game explanation
// at all -- everything the game and the mod know about it lives in FactoryGame.log. A player had to
// come and ask. That is the whole reason this file exists.
//
// AND IT IS NOW THE NORMAL POST-BUILD STATE, NOT AN EDGE CASE. The deployed DataForge/ tree holds
// exactly ONE pack -- the self-generating one -- since the hand-written pack was retired (1ab8868), and
// DataForge/ is in no build mirror list, so every build wipes the generated pack and resets that clock.
// The FIRST boot after any build therefore has ZERO NodeShuffle allow-list documents applied and is
// EXPECTED to be pending. The copy below is written for that reader: this is a routine "one more
// restart" notice, not an error report.
//
// THE PENDING PREDICATE, and why it cannot nag:
//     PENDING = { documents actually WRITTEN this pass } \ { classes SF+ already allows }
// Both halves already existed in the pass; the second is the bAlreadyInPdaArray bool it was already
// logging as sfPlusAlreadyAllows=%d. Crucially this is a STATE test, not a "did we just write
// something" test: at boot N+1 every one of those classes reads sfPlusAlreadyAllows=1, PENDING is
// empty, and the notice is silent WITHOUT any flag, latch or saved field having to remember anything.
// That self-clearing property is the design; do not replace it with persistence.
//
// WHAT IT WILL SAY THAT NOTHING ELSE COULD (H1b, 2026-07-31): fracking machines -- the Resource Well
// Pressurizer and Well Extractor -- can now reach ADD through H1b's fail-closed pairing rule. Those are
// exactly the buildings the user could not place on their retyped wells, so they are the highest-value
// thing this notice will ever name. That is also why the resource-naming pass below re-uses
// ClassifyFrackingPairing rather than the generic acceptance predicate alone: naming a Pressurizer as
// accepting "Coal" because some non-fracking group satisfied the generic test would be worse than
// saying nothing.
//
// SCOPE, stated so a later reader does not look for the missing half: this notice does NOT fix the
// remote-client gap (a client never generates a pack because the subsystem is authority-gated). That is
// a pre-existing Packet G limitation, deliberately left open. The dedicated-server copy therefore tells
// players it is the SERVER that must restart, and never promises a client that restarting helps them.

#include "NodeShuffle.h"
#include "NodeShuffleSubsystem.h" // FNodeShuffleManagedGroup

#include "FGChatManager.h"
#include "Buildables/FGBuildable.h" // AFGBuildable::mDisplayName (public UPROPERTY -- a field read, 0 imports)
#include "Buildables/FGBuildableResourceExtractorBase.h"
#include "Buildables/FGBuildableFrackingActivator.h"
#include "Buildables/FGBuildableFrackingExtractor.h"
#include "Resources/FGResourceNodeFrackingCore.h"
#include "Resources/FGResourceNodeFrackingSatellite.h"
#include "Resources/FGItemDescriptor.h"
#include "Engine/World.h"

namespace
{
    // Caps (anti-nag rule 8). A 20-line chat wall is its own kind of nag.
    constexpr int32 MaxNamedBuildings = 4;
    constexpr int32 MaxNamedResourcesPerBuilding = 3;
    // (The emit-retry budget lives with the emitter, in NodeShufflePendingNoticeEmit.cpp.)

    const TCHAR* NameSourceLabel(int32 NameSource)
    {
        switch (NameSource)
        {
        case 0:  return TEXT("mDisplayName");
        case 1:  return TEXT("GetExtractorTypeName");
        default: return TEXT("class-name(NOT-PLAYER-READABLE)");
        }
    }

    FString JoinResourceNames(const FNodeShufflePendingEntry& E)
    {
        FString Out;
        for (int32 i = 0; i < E.ResourceNames.Num(); ++i)
        {
            if (i > 0) { Out += TEXT(", "); }
            Out += E.ResourceNames[i].ToString();
        }
        if (E.ExtraResourceCount > 0)
        {
            Out += FString::Printf(TEXT(", +%d more"), E.ExtraResourceCount);
        }
        return Out.IsEmpty() ? FString(TEXT("shuffled nodes")) : Out;
    }
}

// ---------------------------------------------------------------------------------------------
// H1b's fail-closed fracking pairing rule, single-sourced (see the enum comment in NodeShuffle.h).
// ---------------------------------------------------------------------------------------------
ENodeShuffleFrackPair FNodeShuffleModule::ClassifyFrackingPairing(const UClass* ExtractorClass,
    const UClass* NodeClass, const UClass* RestrictClass)
{
    if (!ExtractorClass) { return ENodeShuffleFrackPair::RejectedUnclassifiableKind; } // fail closed

    const bool bActivator = ExtractorClass->IsChildOf(AFGBuildableFrackingActivator::StaticClass());
    const bool bExtractor = ExtractorClass->IsChildOf(AFGBuildableFrackingExtractor::StaticClass());
    if (!bActivator && !bExtractor) { return ENodeShuffleFrackPair::NotFrackingExtractor; }

    // Per-kind, not "any fracking node": a Pressurizer belongs on the CORE, a Well Extractor on a
    // SATELLITE. Deriving from both is impossible under single inheritance -- kept, and kept failing
    // closed, because a rule that guesses when surprised is not fail-closed.
    const UClass* RequiredNodeBase = nullptr;
    if (bActivator && !bExtractor) { RequiredNodeBase = AFGResourceNodeFrackingCore::StaticClass(); }
    else if (bExtractor && !bActivator) { RequiredNodeBase = AFGResourceNodeFrackingSatellite::StaticClass(); }
    if (!RequiredNodeBase) { return ENodeShuffleFrackPair::RejectedUnclassifiableKind; }

    // Axis (i): is the evidence coming from a fracking node of the required kind?
    if (!NodeClass || !NodeClass->IsChildOf(RequiredNodeBase))
    {
        return ENodeShuffleFrackPair::RejectedNodeNotFracking;
    }
    // Axis (ii): is this machine natively confined to fracking nodes EVERYWHERE, not merely compatible
    // with this one group? An unset or broader mRestrictToNodeType means it could be placed on an
    // ordinary node and reach the fatal checked cast, so allow-listing it would re-open the crash.
    if (!RestrictClass || !RestrictClass->IsChildOf(RequiredNodeBase))
    {
        return ENodeShuffleFrackPair::RejectedRestrictionNotConfined;
    }
    return ENodeShuffleFrackPair::Allowed;
}

// ---------------------------------------------------------------------------------------------
// Raw pending classes -> player-facing entries.
// ---------------------------------------------------------------------------------------------
void FNodeShuffleModule::BuildPendingNotice(const TArray<FNodeShufflePendingRaw>& Raw,
    const TArray<FNodeShuffleManagedGroup>& NodeGroups,
    TArray<FNodeShufflePendingEntry>& OutEntries)
{
    // ns-review-notice nit: APPEND-vs-RESET is not the caller's problem to remember. Today's only caller
    // passes a fresh local, so this costs nothing; the day one does not, a stale entry would ride into a
    // player-facing message, which is the expensive kind of bug to find.
    OutEntries.Reset();
    for (const FNodeShufflePendingRaw& R : Raw)
    {
        const UClass* Cls = R.ExtractorClass;
        const AFGBuildableResourceExtractorBase* Ext = Cls
            ? Cls->GetDefaultObject<AFGBuildableResourceExtractorBase>() : nullptr;

        FNodeShufflePendingEntry& E = OutEntries.AddDefaulted_GetRef();
        E.ExtractorPath = R.ExtractorPath;
        E.bWriteFailed = R.bWriteFailed;
        E.bAlreadyAllowed = R.bAlreadyAllowed;

        // ---- name ladder (§3.4). NOT GetExtractorTypeName() as the first choice: MEASURED, it returns
        // an FName and it is an extractor CATEGORY ("Miner"), not a per-tier building name. The CDO's
        // own mDisplayName is a public UPROPERTY on AFGBuildable -- a field read, zero new imports.
        // The ladder is not padding: build_oilmk4_C, pumpmk4_C and Build_BioWaterExtractor_SF+_C are
        // real classes from three third-party mods and nothing here controls whether they filled it in.
        if (Ext && !Ext->mDisplayName.IsEmpty())
        {
            E.BuildingName = Ext->mDisplayName;
            E.NameSource = 0;
        }
        else if (Ext)
        {
            const FName TypeName = Ext->GetExtractorTypeName(); // already called elsewhere -- no NEW symbol
            if (!TypeName.IsNone())
            {
                // ns-review-notice F2: GetExtractorTypeName returns a CATEGORY ("Miner"), not a building.
                // Two modded tiers with an empty mDisplayName and the same category would print two
                // identical "Miner" lines; the player checks their vanilla Miner, finds it builds fine,
                // and concludes the notice is broken. Appending the class name makes the line ugly but
                // TRUE and distinguishable -- the design doc named this hazard and then put the bare
                // category at this rung anyway.
                E.BuildingName = FText::FromString(FString::Printf(TEXT("%s [%s]"),
                    *TypeName.ToString(), Cls ? *Cls->GetName() : *R.ExtractorPath));
                E.NameSource = 1;
            }
        }
        if (E.BuildingName.IsEmpty())
        {
            E.BuildingName = FText::FromString(Cls ? Cls->GetName() : R.ExtractorPath);
            E.NameSource = 2;
        }
        // ---- resources (§3.6). A SECOND, SEPARATE collection loop, deliberately NOT the decision loop:
        // that one early-breaks on purpose and its determinism is the property that killed the
        // oscillation bug, so it is not touched. This one does not break, so it names EVERY resource the
        // extractor accepts rather than only the first match the decision happened to stop on.
        // It runs only for the handful of PENDING classes (0-8 on any realistic profile).
        TSet<const UClass*> SeenResources;
        for (const FNodeShuffleManagedGroup& G : NodeGroups)
        {
            if (!Ext || !G.ResourceClass || SeenResources.Contains(G.ResourceClass)) { continue; }
            const FNodeShuffleExtractorAcceptance A =
                EvaluateExtractorAcceptance(Ext, G.NodeClass, G.Form, G.ResourceClass);
            if (!A.AcceptsNatively() || !A.bDiscriminated) { continue; }
            // H1b's pairing rule applies here too, via the SAME function the decision uses. Without it a
            // Resource Well Pressurizer would be advertised as accepting whatever ordinary resource
            // passed the generic test -- a message that contradicts the decision that produced it.
            const ENodeShuffleFrackPair Pair = ClassifyFrackingPairing(Cls, G.NodeClass, A.RestrictClass);
            if (Pair != ENodeShuffleFrackPair::NotFrackingExtractor
                && Pair != ENodeShuffleFrackPair::Allowed)
            {
                continue; // spelled out rather than compared by enum order -- the order is not a contract
            }
            // ORDERING IS LOAD-BEARING -- do not "tidy" this Add() up to the top of the loop. It must
            // stay AFTER the acceptance and pairing checks: BuildManagedNodeGroupsFromLayout emits a
            // {satellite, core} pair per well, and marking a resource seen on a group this extractor was
            // REJECTED for would consume it before the group it is actually paired with is reached --
            // silently dropping the one resource the message most needed to name.
            SeenResources.Add(G.ResourceClass);
            if (E.ResourceNames.Num() < MaxNamedResourcesPerBuilding)
            {
                E.ResourceNames.Add(UFGItemDescriptor::GetItemName(
                    TSubclassOf<UFGItemDescriptor>(G.ResourceClass)));
            }
            else
            {
                ++E.ExtraResourceCount;
            }
        }

    }

    // ---- ns-review-notice2 F-A: CAP THE PER-EXTRACTOR DETAIL ----
    // F-A makes the failure path reachable, and a SUSTAINED write failure (read-only install,
    // restrictive ACL, AV holding the folder) never latches, so the pass re-runs every ~5 s and this
    // function runs with it. One Display line per extractor per tick is precisely the unbounded
    // per-tick output F3 was added to stop -- reintroduced by the fix that made the failure path work.
    // So the DETAIL prints when the key set CHANGES, and an unchanged set prints exactly one line.
    // Function-local static, the same idiom NodeShuffle.cpp's hologram hooks already use for LoggedHits;
    // it is log-suppression state only, so a value surviving into another world costs at most one
    // suppressed line that carries no information the pass-summary line does not already carry.
    static TSet<FString> LastLoggedKeys;
    TArray<FString> KeysNow;
    BuildPendingNoticeKeys(OutEntries, KeysNow);
    const TSet<FString> KeySetNow(KeysNow);
    const bool bDetail = (KeySetNow.Num() != LastLoggedKeys.Num()) || !KeySetNow.Includes(LastLoggedKeys);
    LastLoggedKeys = KeySetNow;

    if (!bDetail)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("PENDINGNOTICE: %d pending entr(ies), UNCHANGED since the last pass -- per-extractor detail ")
            TEXT("suppressed (a sustained write failure re-runs this pass every tick; the detail is in the ")
            TEXT("PENDINGNOTICE lines from the first pass that saw this set)."), OutEntries.Num());
        return;
    }

    for (const FNodeShufflePendingEntry& E : OutEntries)
    {
        if (E.NameSource != 0)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("PENDINGNOTICE: extractor='%s' has an EMPTY mDisplayName -- fell back to %s. The chat ")
                TEXT("message will show '%s', which may not be player-readable."),
                *E.ExtractorPath, NameSourceLabel(E.NameSource), *E.BuildingName.ToString());
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("PENDINGNOTICE: pending extractor='%s' name='%s' nameSource=%s resources=[%s] ")
            TEXT("extraResources=%d writeFailed=%d alreadyAllowed=%d"),
            *E.ExtractorPath, *E.BuildingName.ToString(), NameSourceLabel(E.NameSource),
            *JoinResourceNames(E), E.ExtraResourceCount, E.bWriteFailed ? 1 : 0, E.bAlreadyAllowed ? 1 : 0);
    }
}

// ---------------------------------------------------------------------------------------------
// De-dup signature. Derived ONLY from the pending set -- never from the generated pack (that is the
// feedback-loop shape that caused the oscillation bug). The write-failed flag is part of the identity
// so a set that flips from "pending" to "failed" is a genuinely new thing to say.
// ---------------------------------------------------------------------------------------------
void FNodeShuffleModule::BuildPendingNoticeKeys(const TArray<FNodeShufflePendingEntry>& Entries,
    TArray<FString>& OutKeys)
{
    OutKeys.Reset();
    OutKeys.Reserve(Entries.Num());
    for (const FNodeShufflePendingEntry& E : Entries)
    {
        // The write-failed flag is part of the identity: a class that flips from "pending" to "failed"
        // is a genuinely new thing to say, and the two get different copy.
        OutKeys.Add(FString::Printf(TEXT("%s|%d"), *E.ExtractorPath, E.bWriteFailed ? 1 : 0));
    }
}

FString FNodeShuffleModule::BuildPendingNoticeSignature(const TArray<FNodeShufflePendingEntry>& Entries)
{
    TArray<FString> Keys;
    BuildPendingNoticeKeys(Entries, Keys);
    Keys.Sort(); // order must not depend on iteration order, or a re-roll would look like a new set
    return FString::Join(Keys, TEXT(";"));
}

// ---------------------------------------------------------------------------------------------
// Compose + post to chat.
// ---------------------------------------------------------------------------------------------
bool FNodeShuffleModule::EmitPendingNotice(UWorld* World, const TArray<FNodeShufflePendingEntry>& Entries)
{
    if (!World || Entries.Num() == 0) { return false; }

    AFGChatManager* Chat = AFGChatManager::Get(World);
    // Route diagnostics: the log must say which route ran and why the others did not. The alternatives
    // considered were a UMG push-notification widget and an ADA/UFGMessage codex entry -- BOTH need a
    // cooked .uasset and NodeShuffle has no Content/ tree, so neither is queried at runtime at all.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("PENDINGNOTICE ROUTE: chatManager=%s netMode=%d entries=%d gameUI=<not-queried: needs a cooked ")
        TEXT("widget asset this mod has no Content/ tree for> -> chose CHAT (ladder: BroadcastChatMessage ")
        TEXT("-> AddChatMessageToReceived -> log-only)"),
        Chat ? TEXT("valid") : TEXT("NULL"), (int32)World->GetNetMode(), Entries.Num());

    if (!Chat)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("PENDINGNOTICE: AFGChatManager::Get(World) returned null -- notice NOT delivered. Queue ")
            TEXT("kept; will retry next tick. (Pending set unchanged: %d entr(ies).)"), Entries.Num());
        return false;
    }

    const bool bDedicated = (World->GetNetMode() == NM_DedicatedServer);
    const bool bListen = (World->GetNetMode() == NM_ListenServer);

    // Split the two situations. They are NOT the same message: pending clears itself on a restart;
    // a write failure will never clear until the filesystem problem is fixed, so promising a restart
    // would be exactly the confident falsehood this packet exists to stop.
    TArray<const FNodeShufflePendingEntry*> Pending;
    TArray<const FNodeShufflePendingEntry*> Failed;
    for (const FNodeShufflePendingEntry& E : Entries)
    {
        (E.bWriteFailed ? Failed : Pending).Add(&E);
    }

    const auto PostMessage = [Chat](const FString& Body)
    {
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
            TEXT("PENDINGNOTICE: EMITTED via AFGChatManager::BroadcastChatMessage (type=%d sender='NODE SHUFFLE' bodyChars=%d)"),
            (int32)EFGChatMessageType::CMT_CustomMessage, Body.Len());
        // ASSUMED: a NetMulticast RPC executes locally in Standalone, so the host sees this without a
        // separate local echo. If in-game testing shows it does not, add AddChatMessageToReceived(Msg)
        // HERE -- this comment names the exact line so the fix needs no re-investigation.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("PENDINGNOTICE ROUTE: BroadcastChatMessage taken; local echo NOT separately added ")
            TEXT("(multicast is expected to execute locally -- if it does not, add AddChatMessageToReceived here)."));
    };

    // ---- failure copy first: it is the louder, rarer, actionable one ----
    if (Failed.Num() > 0)
    {
        // ns-review-notice F1: SPLIT BY bAlreadyAllowed. A failed write for a class SF+ does not yet
        // permit means that building will not work on shuffled nodes, restart or not -- the original
        // sentence, and it is true. A failed write for a class SF+ ALREADY permits is a different fact:
        // it works right now. The honest warning there is that it MAY stop working after a restart,
        // because the pack directory was cleared earlier in this same pass, so if the class was only on
        // the list courtesy of OUR previous document, that document is now gone. bAlreadyAllowed cannot
        // tell "SF+ ships it" from "we put it there last boot" -- so the softer sentence says "may",
        // which is true under both readings. Stating a certainty we do not have is the failure mode this
        // whole packet exists to prevent, and it does not get a pass just because the copy is a warning.
        TArray<const FNodeShufflePendingEntry*> FailedNew;
        TArray<const FNodeShufflePendingEntry*> FailedAlreadyAllowed;
        for (const FNodeShufflePendingEntry* E : Failed)
        {
            (E->bAlreadyAllowed ? FailedAlreadyAllowed : FailedNew).Add(E);
        }

        const auto AppendNames = [](FString& Body, const TArray<const FNodeShufflePendingEntry*>& List)
        {
            for (int32 i = 0; i < List.Num() && i < MaxNamedBuildings; ++i)
            {
                Body += FString::Printf(TEXT("  * %s\n"), *List[i]->BuildingName.ToString());
            }
            if (List.Num() > MaxNamedBuildings)
            {
                Body += FString::Printf(TEXT("  ...and %d more.\n"), List.Num() - MaxNamedBuildings);
            }
        };

        if (FailedNew.Num() > 0)
        {
            FString Body = FString::Printf(
                TEXT("NODE SHUFFLE - could not write %d compatibility patch(es)\n\n")
                TEXT("The game folder could not be written to (permissions, a read-only install, or antivirus).\n")
                TEXT("These buildings will NOT be usable on shuffled nodes even after a restart:\n"),
                FailedNew.Num());
            AppendNames(Body, FailedNew);
            Body += TEXT("\nDetails are in FactoryGame.log (search for \"ADD-FAILED\").");
            PostMessage(Body);
        }
        if (FailedAlreadyAllowed.Num() > 0)
        {
            FString Body = FString::Printf(
                TEXT("NODE SHUFFLE - could not rewrite %d compatibility patch(es)\n\n")
                TEXT("The game folder could not be written to (permissions, a read-only install, or antivirus).\n")
                TEXT("Satisfactory Plus currently permits these buildings, so they work RIGHT NOW, but they\n")
                TEXT("may stop working on shuffled nodes after a restart:\n"),
                FailedAlreadyAllowed.Num());
            AppendNames(Body, FailedAlreadyAllowed);
            Body += TEXT("\nDetails are in FactoryGame.log (search for \"ADD-FAILED\").");
            PostMessage(Body);
        }
    }

    // ---- pending copy ----
    if (Pending.Num() > 0)
    {
        // ns-t55-copy (T55, author directive 2026-08-10): OPTION 2 -- the state stays transient and the
        // COPY carries the change. Two assertions were removed because they were measured false, not
        // because they read badly:
        //   * "%d NEW building(s)" -- the same two AlkaLib extractors were announced on the 17.13.38 boot
        //     and again two boots later. They were pending both times, and both notices were correct; the
        //     word "new" was the only false part. This says "still need", which is true on a repeat and
        //     equally true the first time.
        //   * "this is normal after a mod update or reinstall" -- a CAUSE the notice never tested. The
        //     measured cause of the repeat on the author's machine was neither: the generated pack is
        //     cleared and rebuilt from the loaded world's managed node groups every pass (see
        //     NodeShuffleAutoAllowExtractors.cpp's clear-and-rebuild and its PACKCHURN line), so loading
        //     a different save deletes documents that save does not need. The replacement sentence states
        //     THAT, and only for the net mode where the player can act on it.
        FString Body = FString::Printf(
            TEXT("NODE SHUFFLE - %d building(s) still need a restart\n\n")
            TEXT("Compatibility patches are written for extractors that Satisfactory Plus does not yet\n")
            TEXT("permit on shuffled nodes. They take effect the NEXT time you start the game.\n"),
            Pending.Num());
        for (int32 i = 0; i < Pending.Num() && i < MaxNamedBuildings; ++i)
        {
            Body += FString::Printf(TEXT("  * %s  ->  %s\n"),
                *Pending[i]->BuildingName.ToString(), *JoinResourceNames(*Pending[i]));
        }
        if (Pending.Num() > MaxNamedBuildings)
        {
            Body += FString::Printf(
                TEXT("  ...and %d more (full list in FactoryGame.log, search for \"AUTOALLOW\").\n"),
                Pending.Num() - MaxNamedBuildings);
        }
        Body += TEXT("\nUntil you restart, these buildings show a red hologram (\"Invalid aim location!\")\n");
        // The second paragraph is where single-player, listen server and dedicated server differ. A
        // dedicated server's clients cannot fix anything by restarting their own game, and telling them
        // to would be worse than saying nothing.
        if (bDedicated)
        {
            Body += TEXT("on those nodes. The SERVER must be restarted for these to take effect - restarting\n")
                    TEXT("your own game will not help. Ask the server admin.");
        }
        else if (bListen)
        {
            Body += TEXT("on those nodes. Nothing is broken. The HOST must restart the session once - a client\n")
                    TEXT("restarting on its own will not change anything.");
        }
        else
        {
            Body += TEXT("on those nodes. Nothing is broken and nothing needs fixing - restart the game once\n")
                    TEXT("and they work. You can see this again on a later load: the patch set is rebuilt for\n")
                    TEXT("whichever save you open, so loading a different save can re-create it.");
        }
        PostMessage(Body);
    }

    return true;
}
