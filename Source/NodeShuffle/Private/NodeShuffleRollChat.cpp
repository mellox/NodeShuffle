// ---- ns-rollchat-1 (2026-11-14) -----------------------------------------------------------------
// THE POST-SHUFFLE CHAT BREAKDOWN. The user's request: "After a shuffle, let's put in the game's chat
// the breakdown of nodes and count." Round 2 (user): "it should be in a table format so it is consistent
// and easy to read", sent row by row by a paced loop so the rows arrive in order; then (user, verbatim):
// "Just do the resource and the count. you may need to have fixed length lines so the show up in actual
// columns visually under the header". So: two columns, NODES only (wells are not in chat). Round 4: the chat
// font was measured PROPORTIONAL, so the layout is COUNT FIRST, then the name (see RollChatCountRow).
//
// POPULATION: the LAYOUT the roll produced, not spawned actors. On RWG most dealt spots have no actor
// yet when the roll returns, so an actor count would under-report the deal. Counted: every Layout entry
// with bActive, keyed on AssignedResourceClassPath (the same predicate and key EmitRollCensus uses). Wells
// are not in the chat table (user ruling); WellLayout.Num() is printed in ROLLCHAT-QUEUED only. Taken at
// the end of RollLayout,
// so nodes found later (late capture) are not in it -- the opening line says "counted at the shuffle".
//
// ONE POINT PER ROLL: RollChatQueueBreakdown is the last statement of RollLayout, after both node deal
// passes, RollWellLayout, RollWellRelocation and EmitRollCensus. RollLayout's callers are the first roll
// and the re-roll in RefreshTick and the legacy-layout re-deal in RwgPipelineMayRun; ApplyLayout never
// calls it.
//
// DRAIN STATE MACHINE (all state is plain members of this actor; nothing is UPROPERTY, nothing is saved):
//   Idle      RollChatNext >= RollChatRows.Num().
//   Waiting   rows queued, drain timer not active. TickRollChatEmitter (every RefreshTick, ~5 s) holds
//             until a local pawn exists, holds one more tick before the FIRST row, then starts Draining.
//   Draining  RollChatDrainTimer (one-shot, re-armed after each accepted row) fires RollChatRowGapSeconds
//             apart; each fire posts row RollChatNext and advances it only when PostChatNotice accepted it.
//             Last row accepted -> ROLLCHAT-DONE, Idle.
//   Stalled   a post was refused: the timer stops, RollChatAttempts++, and the next RefreshTick restarts
//             Draining at the SAME row. RollChatMaxEmitAttempts refusals -> the rest is dropped (Warning).
//   A new roll (RollChatQueueBreakdown) or world teardown (RollChatClear from EndPlay) stops the timer and
//   drops any unsent rows, logging how many.

#include "NodeShuffleSubsystem.h"
#include "NodeShuffle.h"

#include "Resources/FGItemDescriptor.h"
#include "Resources/FGResourceDescriptor.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace
{
    // THE ROW GAP. FGChatManager.h: ServerTimeStamp is "Used for message sorting. Automatically set by the
    // Chat Manager" -- a float in seconds whose clock is closed code (ASSUMED: a world/server clock).
    // Rows posted in one tick could share a stamp, so rows are one timer fire apart (game time; timers stop
    // while paused). Two values 0.3 s apart cannot round to the same float while the clock is below 2^22 s
    // (~48 days; the float step there is 0.25 s). Above that the step is 0.5 s and they can.
    constexpr float RollChatRowGapSeconds = 0.3f;
    // Same budget as the other notices' retry (each refusal waits for the next ~5 s RefreshTick).
    constexpr int32 RollChatMaxEmitAttempts = 12;

    struct FRollChatRow
    {
        FString ClassPath;
        FString Name;
        int32 Nodes = 0;
    };

    // Display name via UFGItemDescriptor::GetItemName (the call NodeShufflePendingNotice.cpp already makes).
    // FindObject only: no load is triggered from here. FALLBACK when the class is not in memory or the name
    // is empty: the class's object name (the text after the last '.' of the path, e.g. Desc_OreIron_C).
    FString RollChatName(const FString& ClassPath, bool& bOutFallback)
    {
        bOutFallback = false;
        if (UClass* Cls = FindObject<UClass>(nullptr, *ClassPath))
        {
            // UFGResourceDescriptor::StaticClass, not UFGItemDescriptor's: the subsystem already calls this one
            // (NodeShuffleSubsystem.cpp IsChildOf sites), so the guard adds no FactoryGame symbol.
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

    // ONE ROW PER NODE RESOURCE. ORDER (user ruling 2026-11-14):
    // ALPHABETICAL by display name, case-insensitive, tie-broken by the class path compared case-sensitively.
    // The counts play no part in the order.
    TArray<FRollChatRow> RollChatTableRows(const TMap<FString, int32>& NodeCounts, int32& OutFallbacks)
    {
        TArray<FRollChatRow> Rows;
        for (const TPair<FString, int32>& P : NodeCounts)
        {
            bool bFallback = false;
            FRollChatRow& R = Rows.AddDefaulted_GetRef();
            R.ClassPath = P.Key;
            R.Name = RollChatName(P.Key, bFallback);
            R.Nodes = P.Value;
            if (bFallback) { ++OutFallbacks; }
        }
        Rows.StableSort([](const FRollChatRow& A, const FRollChatRow& B)
        {
            const int32 ByName = A.Name.Compare(B.Name, ESearchCase::IgnoreCase);
            if (ByName != 0) { return ByName < 0; }
            return A.ClassPath.Compare(B.ClassPath, ESearchCase::CaseSensitive) < 0;
        });
        // r2 review F4 (verbatim): two class paths sharing a display name get the short class name appended, so
        // the rows are distinguishable. Duplicates are adjacent because the sort key is the case-insensitive name.
        TArray<bool> Dup; Dup.Init(false, Rows.Num());
        for (int32 i = 1; i < Rows.Num(); ++i) { if (Rows[i - 1].Name.Equals(Rows[i].Name, ESearchCase::IgnoreCase)) { Dup[i - 1] = true; Dup[i] = true; } }
        for (int32 i = 0; i < Rows.Num(); ++i)
        {
            if (!Dup[i]) { continue; }
            int32 Dot = INDEX_NONE;
            Rows[i].Name += FString::Printf(TEXT(" (%s)"), *(Rows[i].ClassPath.FindLastChar(TEXT('.'), Dot) ? Rows[i].ClassPath.Mid(Dot + 1) : Rows[i].ClassPath));
        }
        return Rows;
    }

    // THE ONE ROW FORMAT (round 4): COUNT FIRST, no leading spaces, then a run of plain ASCII spaces, then the
    // label. M is the count-column digit figure: max(3, digits of the Total row's number). Gap after the count:
    //   header row ("Count" then "Resource"): 2*M - 2 spaces;
    //   every other row:                       8 + 2*(M - digits(count)) spaces.
    // WHAT WAS MEASURED, AND ITS SCOPE (2026-11-14, ONE machine, ONE UI scale, English): the chat font is
    // proportional (ten 'i' far narrower than ten 'W'), so name-first padding cannot make columns. A digit is
    // about as wide as two spaces there (about 13.9 px against 7.2 px), and runs of plain spaces survive into
    // chat. Five pasted lines in exactly this layout for a Total of 1032 put the labels' left ink edges at 244,
    // 243, 244, 244 and 241 px. Other UI scales, fonts and languages are UNMEASURED; this does not claim the
    // labels line up anywhere else.
    // THIS IS THE ONLY PLACE THE ROW LAYOUT LIVES: count text, label, M and the header flag in; one row out.
    // Padding is a plain loop of single-space AppendChar calls (AppendChar is already used in
    // NodeShuffleAutoAllowExtractors.cpp), not a repeat-character helper, so the row adds no engine import.
    // Nothing on the way to chat trims it: PostChatNotice does FText::FromString(Body) and nothing else.
    FString RollChatCountRow(const FString& Count, const FString& Label, int32 M, bool bHeader)
    {
        const int32 Gap = bHeader ? (2 * M - 2) : (8 + 2 * (M - Count.Len()));
        FString Row = Count;
        for (int32 i = 0; i < Gap; ++i) { Row.AppendChar(TEXT(' ')); }
        Row += Label;
        return Row;
    }
}

void ANodeShuffleSubsystem::RollChatClear(const TCHAR* Why)
{
    if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(RollChatDrainTimer); }
    bRollChatTimerArmed = false;
    const int32 Unsent = RollChatRows.Num() - RollChatNext;
    if (Unsent > 0)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("ROLLCHAT-DONE: table dropped part-way (%s) -- %d of %d row(s) sent, %d dropped | %s"),
            Why, RollChatNext, RollChatRows.Num(), Unsent, *RollChatSummary);
    }
    RollChatRows.Reset();
    RollChatNext = 0;
    RollChatGateTicks = -1;
    RollChatAttempts = 0;
}

void ANodeShuffleSubsystem::RollChatQueueBreakdown(bool bIsReroll, int32 RolledLineActive)
{
    // A table from an earlier roll of THIS session still draining is dropped: the newest roll is the layout
    // in the world, so the old numbers must not reach chat after it.
    RollChatClear(TEXT("superseded by a new roll"));
    // F4 (review, verbatim): "re-dealt" is true for both re-roll routes (toggle and legacy load).
    const TCHAR* Trigger = bIsReroll ? TEXT("re-dealt") : TEXT("first shuffle");

    TMap<FString, int32> NodeCounts;
    int32 NodesNamed = 0, NodesNoResource = 0;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bActive) { continue; }
        if (E.AssignedResourceClassPath.IsEmpty()) { ++NodesNoResource; continue; }
        NodeCounts.FindOrAdd(E.AssignedResourceClassPath)++;
        ++NodesNamed;
    }

    const int32 CensusActive = RollChatCensusActiveEntries;
    RollChatCensusActiveEntries = -1; // read once: a later roll must stash its own figure
    const int32 NodeTotal = NodesNamed + NodesNoResource;
    const bool bNodesMatch = CensusActive >= 0 && NodeTotal == CensusActive;

    int32 NameFallbacks = 0;
    const TArray<FRollChatRow> Table = RollChatTableRows(NodeCounts, NameFallbacks);

    // M: the count-column digit figure, floored at 3. The Total is the largest number in the table (every row
    // count is non-negative and the Total is their sum), so its digit count bounds every row's.
    const FString NoResourceLabel = TEXT("(no resource)");
    const int32 CountDigitsM = FMath::Max(3, FString::FromInt(NodeTotal).Len());

    // F1 (review, adapted to the table): the opening line carries the scope, the Total row carries the number.
    RollChatRows.Add(FString::Printf(TEXT("New layout (%s), counted at the shuffle:"), Trigger));
    RollChatRows.Add(RollChatCountRow(TEXT("Count"), TEXT("Resource"), CountDigitsM, true));
    for (const FRollChatRow& R : Table)
    {
        RollChatRows.Add(RollChatCountRow(FString::FromInt(R.Nodes), R.Name, CountDigitsM, false));
    }
    if (NodesNoResource > 0) // F5 (adapted): the no-resource entries get their own row, without "yet"
    {
        RollChatRows.Add(RollChatCountRow(FString::FromInt(NodesNoResource), NoResourceLabel, CountDigitsM, false));
    }
    RollChatRows.Add(RollChatCountRow(FString::FromInt(NodeTotal), TEXT("Total"), CountDigitsM, false));
    RollChatNext = 0;

    // Field legend (prose): nodeTotal counts active layout entries (no-resource ones included, as the
    // "(no resource)" row shows them); censusActive is the ROLLCENSUS active figure of this roll (-1 = no
    // census ran), a same-population recount; rolledLineActive is the active figure of the "Rolled layout:"
    // line, kept before the relocation pass and NOT required to be equal; wellLayoutEntries is the WellLayout
    // length (log only: wells are not in the chat table); countColumnDigits is M, the digit figure the
    // count-first rows are spaced by (max of 3 and the Total's digit count).
    RollChatSummary = FString::Printf(
        TEXT("trigger %s | rows %d resources %d | nodeTotal %d nodesNoResource %d censusActive %d nodesReconciled %d rolledLineActive %d | wellLayoutEntries %d | countColumnDigits %d | nameFallbacks %d"),
        Trigger, RollChatRows.Num(), Table.Num(), NodeTotal, NodesNoResource, CensusActive, bNodesMatch ? 1 : 0,
        RolledLineActive, WellLayout.Num(), CountDigitsM, NameFallbacks);
    UE_LOG(LogNodeShuffle, Display, TEXT("ROLLCHAT-QUEUED: %s"), *RollChatSummary);
    if (!bNodesMatch)
    {
        UE_LOG(LogNodeShuffle, Warning, TEXT("ROLLCHAT-QUEUED: node total %d differs from the census active figure %d"),
            NodeTotal, CensusActive);
    }
}

void ANodeShuffleSubsystem::TickRollChatEmitter()
{
    if (RollChatNext >= RollChatRows.Num()) { return; } // Idle: the common case
    UWorld* World = GetWorld();
    if (!World) { return; }
    if (bRollChatTimerArmed) { return; } // Draining: our own flag for the pending one-shot (set on arm, cleared when it fires or is cleared)

    const APlayerController* PC = World->GetFirstPlayerController();
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!PC || !Pawn)
    {
        UE_LOG(LogNodeShuffle, Verbose, TEXT("ROLLCHAT: holding %d row(s); pc valid %d, pawn valid %d"),
            RollChatRows.Num() - RollChatNext, PC ? 1 : 0, Pawn ? 1 : 0);
        return;
    }
    if (RollChatNext == 0 && RollChatGateTicks < 0)
    {
        RollChatGateTicks = 0;
        UE_LOG(LogNodeShuffle, Display, TEXT("ROLLCHAT: player pawn present; holding %d row(s) one more tick"),
            RollChatRows.Num());
        return;
    }
    if (RollChatNext == 0) { RollChatStartSeconds = FPlatformTime::Seconds(); }
    // Waiting/Stalled -> Draining. ONE-SHOT timer, re-armed by each accepted row: a looping timer can fire
    // several times inside one long frame (the timer manager catches up), which would put two rows in one
    // tick. A one-shot re-armed from its own callback fires at most once per frame and at least a gap apart.
    World->GetTimerManager().SetTimer(RollChatDrainTimer, this, &ANodeShuffleSubsystem::RollChatDrainStep,
        RollChatRowGapSeconds, false);
    bRollChatTimerArmed = true;
    UE_LOG(LogNodeShuffle, Verbose, TEXT("ROLLCHAT: drain started at row %d of %d (gap %.2f s, refusals so far %d)"),
        RollChatNext + 1, RollChatRows.Num(), RollChatRowGapSeconds, RollChatAttempts);
}

void ANodeShuffleSubsystem::RollChatDrainStep()
{
    bRollChatTimerArmed = false; // the one-shot has fired; every re-arm below sets it again
    UWorld* World = GetWorld();
    if (!World || RollChatNext >= RollChatRows.Num())
    {
        if (World) { World->GetTimerManager().ClearTimer(RollChatDrainTimer); }
        return;
    }
    // ONE ROW PER EMITTER CALL. The index advances only on acceptance, so a refusal resumes at this row.
    if (!FNodeShuffleModule::PostChatNotice(World, RollChatRows[RollChatNext], TEXT("ROLLCHAT")))
    {
        World->GetTimerManager().ClearTimer(RollChatDrainTimer); // Draining -> Stalled
        ++RollChatAttempts;
        // r2 review F5 (verbatim): ONE ROLLCHAT-DONE-family line on a give-up -- RollChatClear's "dropped part-way",
        // which always logs here because a refusal leaves RollChatNext < Num.
        if (RollChatAttempts >= RollChatMaxEmitAttempts)
        { const FString Why = FString::Printf(TEXT("emitter refused %d time(s)"), RollChatAttempts); RollChatClear(*Why); return; }
        UE_LOG(LogNodeShuffle, Verbose, TEXT("ROLLCHAT: row %d refused (refusal %d of %d); retrying on a later tick"),
            RollChatNext + 1, RollChatAttempts, RollChatMaxEmitAttempts);
        return;
    }
    UE_LOG(LogNodeShuffle, Display, TEXT("ROLLCHAT-BODY %d/%d: |%s|"), RollChatNext + 1, RollChatRows.Num(), *RollChatRows[RollChatNext]);
    ++RollChatNext;
    if (RollChatNext < RollChatRows.Num())
    {
        // Still Draining: the next row one gap from now.
        World->GetTimerManager().SetTimer(RollChatDrainTimer, this, &ANodeShuffleSubsystem::RollChatDrainStep,
            RollChatRowGapSeconds, false);
        bRollChatTimerArmed = true;
    }
    else
    {
        World->GetTimerManager().ClearTimer(RollChatDrainTimer); // Draining -> Idle
        UE_LOG(LogNodeShuffle, Display, TEXT("ROLLCHAT-DONE: %d of %d row(s) sent in %.1f s, %d refusal(s) | %s"),
            RollChatNext, RollChatRows.Num(), FPlatformTime::Seconds() - RollChatStartSeconds, RollChatAttempts, *RollChatSummary);
        RollChatRows.Reset();
        RollChatNext = 0;
        RollChatGateTicks = -1;
        RollChatAttempts = 0;
    }
}

void ANodeShuffleSubsystem::RollChatNoteFirstApply() const
{
    // Reached on the first apply after every load and after every roll (RollLayout re-arms bDidInitialApply).
    UE_LOG(LogNodeShuffle, Verbose, TEXT("ROLLCHAT: first layout apply since load or roll; %d row(s) queued"),
        RollChatRows.Num() - RollChatNext);
}

// r2 review F2, ADAPTED: the reviewer's body asked the world timer manager whether RollChatDrainTimer is active; this
// reads bRollChatTimerArmed instead (same predicate, kept by every arm/fire/clear site) so no timer query is added.
// True = rows unsent AND the drain's one-shot pending. A Stalled table (timer cleared after a refusal) is NOT
// draining, so a dead chat manager cannot hold the other two notices back.
bool ANodeShuffleSubsystem::RollChatIsDraining() const
{
    return RollChatNext < RollChatRows.Num() && bRollChatTimerArmed;
}
