// ns-t46-cavetruth: `NodeShuffle.WhereCaveNodes` -- THE DIRECTORY OF EVERY CAVE-FLAGGED LAYOUT ENTRY.
//
// WHY THIS EXISTS, AND WHY IT IS NOT A LINE INSIDE NodeShuffle.Here. T45 added a `nearest cave-flagged
// node entry` line to LogHereCensus so the author could walk to a cave placement and take a reading
// there. The author's call (verbatim): "Why would you change here, won't that make it not work correctly
// for what it already does? Should we have a WhereCaveNode that identifies them all?" Both halves are
// right. NodeShuffle.Here answers "what is true at the point I am standing at", and a directory lookup is
// a different question that had been bolted onto that command by three consecutive packets. And a
// NEAREST-only answer HIDES THE POPULATION: it cannot show how many cave entries exist, how they are
// spread, or how many of them are records with no actor behind them -- and a wrong population is this
// project's most expensive defect class, twice caught by the author from direct observation after a
// reviewer had audited the arithmetic. So this lists ALL of them, one row each.
//
// EVERY ROW STATES LIVE OR RECORD-ONLY (T45 cold review F3). The line this replaces printed the settle
// flag but never whether an actor exists, and this repo has a whole known defect class of stale records:
// the author walks several hundred metres, finds nothing, and then takes their readings at a point where
// nothing was ever placed -- a control that reads clean and means nothing. The summary counts the two
// separately, and a LIVE row also prints the actor's OWN location and how far it sits from the record.
//
// LOG-ONLY. It traces nothing, spawns nothing, moves nothing and writes no layout field. It reads the
// layout, the spawned-node map and (through ReadCaveStoreAtForDiag) the cave store, and prints. NOTHING
// PLACES OR REFUSES ANYTHING ON ANY FIGURE HERE. Same precedent as NodeShuffle.Here / .PointAtHere /
// .WellProbe: not gated behind EnableDiagnostics, because it exists for the author to report with.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleGroundIdentity.h" // the ONE cave state-word vocabulary and its legend

#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Resources/FGResourceNode.h"

namespace
{
    // One row's worth of already-measured facts, so the rows can be printed nearest-first without
    // re-reading the layout. Every field is copied from the layout entry or measured against the
    // player's position in the single pass below.
    struct FWhereCaveRow
    {
        FString ResourceShort;
        FVector RecordLoc = FVector::ZeroVector;
        double Dist2DCm = 0.0;
        double DzCm = 0.0;
        double TurnDeg = 0.0;
        bool bLive = false;
        FVector ActorLoc = FVector::ZeroVector;   // meaningful only when bLive
        double RecordToActorCm = 0.0;             // meaningful only when bLive
        bool bSettled = false;
        FString Flags;
        FString StateWord;
    };

    FString WhereCaveShortName(const FString& Path)
    {
        int32 Dot = INDEX_NONE;
        return Path.FindLastChar(TEXT('.'), Dot) ? Path.Mid(Dot + 1) : Path;
    }
}

static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleWhereCaveNodesCmd(
    TEXT("NodeShuffle.WhereCaveNodes"),
    TEXT("List EVERY cave-flagged layout entry: distance, bearing, Z, cave-store state, LIVE or record-only (log-only)."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World) { return; }
        for (TActorIterator<ANodeShuffleSubsystem> It(World); It; ++It)
        {
            It->LogWhereCaveNodesCensus();
            return;
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WHERECAVE: NodeShuffle subsystem not found in this world (main menu / no session ")
            TEXT("loaded?) -- no layout was read and no row is printed below."));
    }));

void ANodeShuffleSubsystem::LogWhereCaveNodesCensus() const
{
    const UWorld* World = GetWorld();
    const APlayerController* Pc = World ? World->GetFirstPlayerController() : nullptr;
    const APawn* Pawn = Pc ? Pc->GetPawn() : nullptr;
    if (!Pawn)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WHERECAVE: no player pawn resolved, so there is no position to measure distances and ")
            TEXT("bearings against and no row is printed. The layout was not read. This line reports ")
            TEXT("that the pawn lookup returned nothing; it does not say why."));
        return;
    }
    const FVector P = Pawn->GetActorLocation();
    const double Yaw = Pawn->GetActorRotation().Yaw;

    // ONE PASS over the whole layout. THE PREDICATE IS `E.bUnderground` ALONE -- deliberately wider than
    // CountUndergroundEntries() (bIsNewNode && bActive && bUnderground), which is what NodeShuffle.Here
    // and the roll census print. T45 cold review F2: the line this command replaces printed
    // "0 of this layout's N entries carry the underground flag" using Layout.Num() as the denominator
    // while its predicate also required bIsNewNode && bActive, so a save whose cave entries had been
    // deactivated read as "the cave feature dealt nothing" when the flags were all still there. Here the
    // denominator IS the population the predicate ran over -- every entry in the layout -- and the rows
    // say per entry what the narrower predicate would have dropped.
    TArray<FWhereCaveRow> Rows;
    int32 Flagged = 0, LiveCount = 0, RecordOnly = 0, Settled = 0, Unsettled = 0;
    int32 ActiveCount = 0, InactiveCount = 0, NewNodeCount = 0, NotNewCount = 0;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bUnderground) { continue; }
        Flagged++;

        AFGResourceNode* const* Live = SpawnedNodes.Find(E.EntryGuid);
        const bool bLive = Live && IsValid(*Live);
        if (bLive) { LiveCount++; } else { RecordOnly++; }
        if (E.bRayCasted) { Settled++; } else { Unsettled++; }
        if (E.bActive) { ActiveCount++; } else { InactiveCount++; }
        if (E.bIsNewNode) { NewNodeCount++; } else { NotNewCount++; }

        FWhereCaveRow Row;
        Row.ResourceShort = WhereCaveShortName(E.AssignedResourceClassPath);
        Row.RecordLoc = E.Location;
        Row.Dist2DCm = FMath::Sqrt(FVector::DistSquared2D(E.Location, P));
        Row.DzCm = E.Location.Z - P.Z;
        Row.TurnDeg = FRotator::NormalizeAxis(
            FMath::RadiansToDegrees(FMath::Atan2(E.Location.Y - P.Y, E.Location.X - P.X)) - Yaw);
        Row.bLive = bLive;
        if (bLive)
        {
            Row.ActorLoc = (*Live)->GetActorLocation();
            Row.RecordToActorCm = FVector::Dist(Row.ActorLoc, E.Location);
        }
        Row.bSettled = E.bRayCasted;

        // The layout flags this row's own entry carries, including the two the narrower predicate used
        // elsewhere would have silently dropped it for. Reports stored flags only.
        // TOKEN HYGIENE, MEASURED AGAINST THE BUILT DLL AND NOT AGAINST THIS COMMENT (this project's
        // three-time trap). The first build of this file used `active|`, which is a substring of
        // NodeShuffle.Here's existing `inactive|` flag, and `new-location`, which its own summary and
        // no-rows legends both spell in prose -- a legend exemplifying its own field's value is exactly
        // the defect that made a grep return 384 matches over 128 lines. Both are prefixed `entry-` so
        // no token here is a substring of any other token in this mod, and the legends below say
        // "new node location" in words the field never prints.
        Row.Flags = E.bActive ? TEXT("entry-active|") : TEXT("entry-deactivated|");
        Row.Flags += E.bIsNewNode ? TEXT("entry-new-location") : TEXT("entry-vanilla-record");
        if (E.bPinned) { Row.Flags += TEXT("|pinned"); }
        if (WaterLockedThisSession.Contains(E.EntryGuid)) { Row.Flags += TEXT("|WATER-LOCKED"); }
        if (DormantThisSession.Contains(E.EntryGuid)) { Row.Flags += TEXT("|DORMANT"); }
        if (SpawnParkedThisSession.Contains(E.EntryGuid)) { Row.Flags += TEXT("|SPAWN-PARKED"); }

        // What the mod's OWN cave store holds at this entry's RECORDED point, through the same reader
        // and the same vocabulary the per-point CAVE-STORE READING line uses. A lookup, not a test.
        FNodeShuffleCaveCellReading Cave;
        ReadCaveStoreAtForDiag(E.Location, Cave);
        Row.StateWord = NodeShuffleCaveCellStateWord(Cave);

        Rows.Add(MoveTemp(Row));
    }

    // THE SUMMARY PRINTS FIRST, BEFORE THE ROWS. A directory can be long, and a denominator that only
    // arrives after N rows is a denominator a truncated or scrolled log does not carry.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WHERECAVE: %d of this layout's %d entry(ies) carry the underground flag, and every one of ")
        TEXT("them gets a row below -- this command lists all of them and never a nearest one. Of those ")
        TEXT("%d: %d have a live actor right now and %d are records with no actor; %d are settled and ")
        TEXT("%d are not; %d are active and %d are not; %d were dealt as new node locations and %d are ")
        TEXT("records of vanilla nodes instead. ")
        TEXT("THE PREDICATE BEHIND EVERY NUMBER ON THIS LINE is the underground flag ALONE, evaluated ")
        TEXT("over EVERY entry in the layout, so the denominator is the population the predicate ran ")
        TEXT("over. That is deliberately wider than the predicate NodeShuffle.Here and the roll census ")
        TEXT("count with, which also requires the entry to be a new location AND active: a save whose ")
        TEXT("cave entries have been deactivated counts zero under that one while the flags are all ")
        TEXT("still set, so each row states those two flags for itself. WHAT THE FLAG IS: the layout's ")
        TEXT("own record that this entry was DEALT a cave-floor cell, which is why it settles with the ")
        TEXT("short in-cave trace instead of the 200 m top-down ray. THIS IS A DIRECTORY, NOT A VERDICT: ")
        TEXT("the flag is what the deal wrote, the apply path can clear it later, and nothing here ")
        TEXT("checks where a node ended up -- read a row's live-or-record column before walking to it. ")
        TEXT("NOTHING PLACES OR REFUSES ANYTHING ON THIS LINE. It states no cause."),
        Flagged, Layout.Num(),
        Flagged, LiveCount, RecordOnly, Settled, Unsettled,
        ActiveCount, InactiveCount, NewNodeCount, NotNewCount);

    if (Rows.Num() == 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WHERECAVE: NO ROWS -- not one of this layout's %d entry(ies) carries the underground ")
            TEXT("flag, so there is no cave-placed node to walk to in this save and no row, distance or ")
            TEXT("bearing is printed. That is a different statement from one being far away, and a ")
            TEXT("different statement again from the flags existing on entries that are inactive: the ")
            TEXT("count above is over every entry in the layout, regardless of whether the entry is ")
            TEXT("active and regardless of whether it is a new node location, so a deactivated cave ")
            TEXT("entry WOULD have appeared. A layout of 0 entries makes this ")
            TEXT("line vacuous. It counts flags on the layout and does not say why none are set."),
            Layout.Num());
        return;
    }

    // Nearest first: the author is standing somewhere and walking to one of these.
    Rows.Sort([](const FWhereCaveRow& A, const FWhereCaveRow& B) { return A.Dist2DCm < B.Dist2DCm; });

    // The state-word vocabulary, printed ONCE for the whole directory rather than on every row -- and
    // from the same shared legend the per-point CAVE-STORE READING line prints, so the two commands
    // cannot drift into two accounts of one value.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WHERECAVE: the cave-store column on each row below is a LOOKUP of what the mod's own store ")
        TEXT("holds at that entry's RECORDED point -- no trace is made and no geometry is inspected, and ")
        TEXT("a point the store holds nothing for is a point its flood-fill has not reached, which this ")
        TEXT("cannot tell apart from a point in no cavern at all. The store held %d cell(s) and %d ")
        TEXT("underground seed(s) when these rows were built. %s"),
        CaveFloors.Num(), CaveSeedCount, *NodeShuffleCaveCellStateWordLegend());

    for (int32 i = 0; i < Rows.Num(); ++i)
    {
        const FWhereCaveRow& R = Rows[i];
        const FString ActorText = R.bLive
            ? FString::Printf(
                TEXT("LIVE: an actor for this entry exists right now, at %s, which is %.0f m from the ")
                TEXT("recorded point above"), *R.ActorLoc.ToCompactString(), R.RecordToActorCm / 100.0)
            // ns-t47-recordonly (T46 cold review F1): the ONLY thing tested here is the SpawnedNodes
            // lookup above -- whether the mod currently holds a spawned actor for this entry. It does
            // NOT test whether anything is standing at the recorded point, and shortly after a load most
            // distant entries have not spawned. LogHereCensus carries this same caveat (a no-actor row is
            // indistinguishable from not-yet-streamed); this whole-map path dropped it and must not.
            : FString(TEXT("RECORD-ONLY: the mod holds no spawned actor for this entry at this moment, ")
                      TEXT("which is the whole of what was tested -- it does not tell a node never ")
                      TEXT("placed, or cleared later, apart from one simply not spawned yet because you ")
                      TEXT("are not near it, so walk to the recorded point before reading it as empty"));
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WHERECAVE: row %d of %d -- entry %s, recorded at %s: %.0f m away from you in 2D, ")
            TEXT("%+.0f m in Z, and from where you stand and face, turn %+.0f deg and go. %s. Settle ")
            TEXT("flag: %s. Layout flags: %s. Cave store at the recorded point: %s."),
            i + 1, Rows.Num(), *R.ResourceShort, *R.RecordLoc.ToCompactString(),
            R.Dist2DCm / 100.0, R.DzCm / 100.0, R.TurnDeg,
            *ActorText,
            R.bSettled ? TEXT("settled") : TEXT("unsettled"),
            *R.Flags, *R.StateWord);
    }
}
