// ---- ns-rollchat-1 (2026-11-14) -----------------------------------------------------------------
// THE POST-SHUFFLE CHAT BREAKDOWN. The user's request: "After a shuffle, let's put in the game's chat
// the breakdown of nodes and count."
//
// POPULATION: the LAYOUT the roll produced, not spawned actors. On RWG most dealt spots have no actor
// yet when the roll returns, so an actor count would under-report the deal. Counted: every Layout entry
// with bActive, keyed on AssignedResourceClassPath (the same predicate and key EmitRollCensus uses), and
// every WellLayout entry keyed on its AssignedResourceClassPath (the effective resource, pinned wells
// included -- the same key the WELLH1-ROLL pool lines use for their "after" column).
//
// ONE POINT PER ROLL: RollChatQueueBreakdown is the last statement of RollLayout, after both node deal
// passes (GenerateNewLocations at the new-location draw and at the relocation draw), RollWellLayout,
// RollWellRelocation and EmitRollCensus. RollLayout's callers are the first roll and the re-roll in
// RefreshTick and the legacy-layout re-roll in RwgTryApplySavedLayout; ApplyLayout never calls it.
//
// RECONCILIATION: the node total (named + no-resource-yet) is compared with the ROLLCENSUS `active`
// figure, which EmitRollCensus stashes in RollChatCensusActiveEntries in the same roll; the well total
// (named + no-resource) with WellLayout.Num(), the first number of the "WELLH1-ROLL: ... complete" line.
//
// LIFETIME: the queue is plain members of this actor, never UPROPERTY, so it is never saved and dies with
// the world. Same emit gates as the other two notices: no local pawn -> hold; one more tick -> hold;
// then post, with a bounded retry while the chat manager returns null.

#include "NodeShuffleSubsystem.h"
#include "NodeShuffle.h"

#include "Resources/FGItemDescriptor.h"
#include "Resources/FGResourceDescriptor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace
{
    // Resources per chat message. 25 node resources -> 1 header + 5 node messages, plus one wells message per
    // 6 well resources (a typical well set of 3-4 resources adds 1): 7 in all.
    constexpr int32 RollChatNamesPerMessage = 6;
    // Same budget as the other notices: ~12 RefreshTicks at ~5 s is about a minute of trying.
    constexpr int32 RollChatMaxEmitAttempts = 12;

    struct FRollChatRow
    {
        FString ClassPath;
        FString Name;
        int32 Count = 0;
    };

    // Display name via UFGItemDescriptor::GetItemName (the call NodeShufflePendingNotice.cpp already makes).
    // FindObject only: no load is triggered from here. FALLBACK when the class is not in memory or the name
    // is empty: the class's object name (the text after the last '.' of the path, e.g. Desc_OreIron_C), so
    // a row is never blank. bOutFallback counts how often that happened, for the log.
    FString RollChatName(const FString& ClassPath, bool& bOutFallback)
    {
        bOutFallback = false;
        if (UClass* Cls = FindObject<UClass>(nullptr, *ClassPath))
        {
            // UFGResourceDescriptor::StaticClass, not UFGItemDescriptor's: the subsystem already calls this one
            // (NodeShuffleSubsystem.cpp IsChildOf sites), so the guard adds no FactoryGame symbol. Every dealt
            // node and well resource is a resource descriptor; anything else takes the fallback below.
            if (Cls->IsChildOf(UFGResourceDescriptor::StaticClass()))
            {
                const FString Name = UFGItemDescriptor::GetItemName(TSubclassOf<UFGItemDescriptor>(Cls)).ToString();
                if (!Name.TrimStartAndEnd().IsEmpty()) { return Name; }
            }
        }
        bOutFallback = true;
        int32 Dot = INDEX_NONE;
        return ClassPath.FindLastChar(TEXT('.'), Dot) ? ClassPath.Mid(Dot + 1) : ClassPath;
    }

    // Builds sorted rows. ORDER (user ruling 2026-11-14): ALPHABETICAL by display name, case-insensitive,
    // tie-broken by the class path compared case-sensitively, so two resources sharing a display name
    // still order the same way after every shuffle. The count plays no part in the order.
    TArray<FRollChatRow> RollChatRows(const TMap<FString, int32>& Counts, int32& OutFallbacks)
    {
        TArray<FRollChatRow> Rows;
        for (const TPair<FString, int32>& P : Counts)
        {
            bool bFallback = false;
            FRollChatRow& R = Rows.AddDefaulted_GetRef();
            R.ClassPath = P.Key;
            R.Name = RollChatName(P.Key, bFallback);
            R.Count = P.Value;
            if (bFallback) { ++OutFallbacks; }
        }
        Rows.StableSort([](const FRollChatRow& A, const FRollChatRow& B)
        {
            const int32 ByName = A.Name.Compare(B.Name, ESearchCase::IgnoreCase);
            if (ByName != 0) { return ByName < 0; }
            return A.ClassPath.Compare(B.ClassPath, ESearchCase::CaseSensitive) < 0;
        });
        return Rows;
    }

    // "Label 1/3: Name 12, Name 7, ..." in groups of RollChatNamesPerMessage. Numbered because the chat
    // manager sorts by its own ServerTimeStamp and every group is posted in the same tick: if chat ever
    // shows them out of order, the numbers still let a reader put them back together.
    void RollChatAppendGroups(const TCHAR* Label, const TArray<FRollChatRow>& Rows, TArray<FString>& Out)
    {
        const int32 Groups = (Rows.Num() + RollChatNamesPerMessage - 1) / RollChatNamesPerMessage;
        for (int32 i = 0; i < Rows.Num(); i += RollChatNamesPerMessage)
        {
            FString Body = FString::Printf(TEXT("%s %d/%d: "), Label, i / RollChatNamesPerMessage + 1, Groups);
            for (int32 k = i; k < Rows.Num() && k < i + RollChatNamesPerMessage; ++k)
            {
                Body += FString::Printf(TEXT("%s%s %d"), k == i ? TEXT("") : TEXT(", "), *Rows[k].Name, Rows[k].Count);
            }
            Out.Add(Body);
        }
    }
}

void ANodeShuffleSubsystem::RollChatQueueBreakdown(bool bIsReroll, int32 RolledLineActive)
{
    const TCHAR* Trigger = bIsReroll ? TEXT("re-roll") : TEXT("first-roll");

    // A breakdown from an earlier roll of THIS session that never fully posted is superseded: the newest
    // roll is the layout in the world, so the old numbers must not reach chat after it.
    if (RollChatPendingBodies.Num() > 0)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("ROLLCHAT: superseded -- %d of %d composed message(s) from the previous roll were still unposted and are dropped (%d accepted before this roll). Previous counts: %s"),
            RollChatPendingBodies.Num(), RollChatComposed, RollChatAccepted, *RollChatSummary);
    }

    // Nodes: the layout's active entries, same predicate and key as EmitRollCensus.
    TMap<FString, int32> NodeCounts;
    int32 NodesNamed = 0, NodesNoResource = 0;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bActive) { continue; }
        if (E.AssignedResourceClassPath.IsEmpty()) { ++NodesNoResource; continue; }
        NodeCounts.FindOrAdd(E.AssignedResourceClassPath)++;
        ++NodesNamed;
    }
    // Wells: one per WellLayout entry, keyed on the effective resource.
    TMap<FString, int32> WellCounts;
    int32 WellsNamed = 0, WellsNoResource = 0;
    for (const FNodeShuffleWellEntry& W : WellLayout)
    {
        if (W.AssignedResourceClassPath.IsEmpty()) { ++WellsNoResource; continue; }
        WellCounts.FindOrAdd(W.AssignedResourceClassPath)++;
        ++WellsNamed;
    }

    const int32 CensusActive = RollChatCensusActiveEntries;
    RollChatCensusActiveEntries = -1; // read once: a later roll must stash its own figure
    const bool bNodesMatch = CensusActive >= 0 && NodesNamed + NodesNoResource == CensusActive;
    const bool bWellsMatch = WellsNamed + WellsNoResource == WellLayout.Num();

    int32 NameFallbacks = 0;
    const TArray<FRollChatRow> NodeRows = RollChatRows(NodeCounts, NameFallbacks);
    const TArray<FRollChatRow> WellRows = RollChatRows(WellCounts, NameFallbacks);

    TArray<FString> Bodies;
    // Counts are of the LAYOUT (carried and pinned entries included), so the copy says "in the new layout"
    // and never "dealt" or "spawned".
    FString Header = FString::Printf(TEXT("New layout (%s): %d resource nodes, %d resources."),
        bIsReroll ? TEXT("re-roll") : TEXT("first shuffle"), NodesNamed, NodeRows.Num());
    if (NodesNoResource > 0)
    {
        Header += FString::Printf(TEXT(" %d more have no resource assigned yet."), NodesNoResource);
    }
    if (WellsNamed > 0)
    {
        Header += FString::Printf(TEXT(" Resource wells in the layout: %d."), WellsNamed);
    }
    Bodies.Add(Header);
    RollChatAppendGroups(TEXT("Nodes"), NodeRows, Bodies);
    RollChatAppendGroups(TEXT("Wells"), WellRows, Bodies);

    RollChatPendingBodies = Bodies;
    RollChatComposed = Bodies.Num();
    RollChatAccepted = 0;
    RollChatGateTicks = -1;
    RollChatAttempts = 0;
    bRollChatTotalsMatch = bNodesMatch && bWellsMatch;
    // Field legend (prose): nodeTotal counts active layout entries with a resource; nodesNoResource counts
    // active entries without one; censusActive is the ROLLCENSUS active figure of this roll (-1 = no census
    // ran); wellTotal and wellsNoResource split WellLayout the same way; wellLayoutEntries is its length.
    // censusActive is a SAME-POPULATION recount (same array, same predicate, nothing writes Layout between
    // the two walks), so nodesReconciled can only read 0 if a later edit breaks that. rolledLineActive is the
    // `active` of the "Rolled layout:" line, a counter RollLayout kept while choosing the active set BEFORE
    // the relocation pass; it is printed as an independent figure and is NOT required to be equal.
    RollChatSummary = FString::Printf(
        TEXT("trigger %s | nodeResources %d nodeTotal %d nodesNoResource %d censusActive %d nodesReconciled %d rolledLineActive %d | wellResources %d wellTotal %d wellsNoResource %d wellLayoutEntries %d wellsReconciled %d | nameFallbacks %d"),
        Trigger, NodeRows.Num(), NodesNamed, NodesNoResource, CensusActive, bNodesMatch ? 1 : 0, RolledLineActive,
        WellRows.Num(), WellsNamed, WellsNoResource, WellLayout.Num(), bWellsMatch ? 1 : 0, NameFallbacks);
    UE_LOG(LogNodeShuffle, Display, TEXT("ROLLCHAT-QUEUED: %d message(s) composed | %s"), RollChatComposed, *RollChatSummary);
}

void ANodeShuffleSubsystem::TickRollChatEmitter()
{
    if (RollChatPendingBodies.Num() == 0) { return; } // the common case
    UWorld* World = GetWorld();
    if (!World) { return; }

    const APlayerController* PC = World->GetFirstPlayerController();
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!PC || !Pawn)
    {
        UE_LOG(LogNodeShuffle, Verbose, TEXT("ROLLCHAT: holding %d message(s); pc valid %d, pawn valid %d"),
            RollChatPendingBodies.Num(), PC ? 1 : 0, Pawn ? 1 : 0);
        return;
    }
    if (RollChatGateTicks < 0)
    {
        RollChatGateTicks = 0;
        UE_LOG(LogNodeShuffle, Display, TEXT("ROLLCHAT: player pawn present; holding %d message(s) one more tick"),
            RollChatPendingBodies.Num());
        return;
    }

    ++RollChatAttempts;
    int32 PostedThisTick = 0;
    while (RollChatPendingBodies.Num() > 0)
    {
        if (!FNodeShuffleModule::PostChatNotice(World, RollChatPendingBodies[0], TEXT("ROLLCHAT"))) { break; }
        UE_LOG(LogNodeShuffle, Display, TEXT("ROLLCHAT-BODY %d/%d: %s"),
            RollChatAccepted + 1, RollChatComposed, *RollChatPendingBodies[0]);
        RollChatPendingBodies.RemoveAt(0);
        ++RollChatAccepted;
        ++PostedThisTick;
    }

    if (RollChatPendingBodies.Num() == 0)
    {
        if (bRollChatTotalsMatch)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("ROLLCHAT: composed %d, emitter accepted %d, attempts %d | %s"),
                RollChatComposed, RollChatAccepted, RollChatAttempts, *RollChatSummary);
        }
        else
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("ROLLCHAT: TOTALS DIFFER -- composed %d, emitter accepted %d, attempts %d | %s"),
                RollChatComposed, RollChatAccepted, RollChatAttempts, *RollChatSummary);
        }
        return;
    }
    if (RollChatAttempts >= RollChatMaxEmitAttempts)
    {
        UE_LOG(LogNodeShuffle, Warning, TEXT("ROLLCHAT: EMITTER REFUSED -- composed %d, emitter accepted %d, %d dropped after %d attempts | %s"),
            RollChatComposed, RollChatAccepted, RollChatPendingBodies.Num(), RollChatAttempts, *RollChatSummary);
        RollChatPendingBodies.Reset();
        return;
    }
    UE_LOG(LogNodeShuffle, Verbose, TEXT("ROLLCHAT: attempt %d of %d posted %d this tick; %d message(s) still queued"),
        RollChatAttempts, RollChatMaxEmitAttempts, PostedThisTick, RollChatPendingBodies.Num());
}

void ANodeShuffleSubsystem::RollChatNoteFirstApply() const
{
    // Reached once per session from RefreshTick's first-apply block. A plain load never calls RollLayout,
    // so the queue is empty there and nothing is posted.
    UE_LOG(LogNodeShuffle, Verbose, TEXT("ROLLCHAT: first layout apply this session; %d message(s) queued"),
        RollChatPendingBodies.Num());
}
