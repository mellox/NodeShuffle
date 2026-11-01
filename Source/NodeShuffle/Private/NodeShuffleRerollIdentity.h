#pragma once

// rwg-rerolldup-2 (2026-11-01): ORIGINAL IDENTITY for the re-roll pool rebuild, beyond the path.
//
// Measured on the user's RWG save (log copy mellox_build3_FactoryGame_3.log): a runtime-spawned original (bamrenew lead, class
// lead_C) gets a NEW actor name every session. Its hide record re-matches by anchor at load (TryRematchStaleRecord) and rebinds
// ONE layout entry to the new name (the first row holding the record's path); a second row of the same original keeps the name
// it got in an older session. The path-keyed dedupe of rwg-rerolldup-1 then compares two different stale names of one original
// and keeps both (l.26467 / l.59375 / l.148600 of that copy: three names for one original, two entries, one twin pair).
//
// Identity here is: SAME PATH unless both rows are anchored >= OriginalIdentityRadiusCm apart, or SAME ORIGINAL RESOURCE AND BOTH
// ANCHORS SET AND WITHIN OriginalIdentityRadiusCm unless the two names differ and both resolve live; both refusals are pairwise. An
// entry with no anchor (a relocated entry never stamped with OriginalTrueLocation) never matches by distance. All helpers are PURE
// over the layout arrays except DedupeOriginals, which renames VanillaNodePath in Layout and Carried; log lines are Display and
// bounded by the caller's cap.

#include "CoreMinimal.h"
#include "NodeShuffleSubsystem.h"

namespace NodeShuffleRerollIdentity
{
    // Mirrors AdoptMatchRadiusCm (NodeShuffleSubsystem.cpp, anonymous namespace): the one radius every identity test in this
    // mod uses (adopt, rematch, veto pre-registration). The subsystem passes its own constant; this one serves DumpLayout,
    // which cannot see the subsystem's. The lint pins both literals.
    constexpr float OriginalIdentityRadiusCm = 300.0f;

    // The spot the entry's ORIGINAL sits at: OriginalTrueLocation when stamped; a raw (not yet relocated) original's own
    // Location; zero for a relocated entry never stamped (its Location is the destination, never the original's spot).
    FVector OriginalAnchor(const FNodeShuffleEntry& E);

    // True when A and B describe one original. bOutByPath says which leg matched; OutDistCm is the anchor distance for the
    // anchor leg (0 for the path leg).
    bool SameOriginal(const FNodeShuffleEntry& A, const FNodeShuffleEntry& B, float RadiusCm, bool& bOutByPath, float& OutDistCm);

    struct FDrop
    {
        FGuid KeeperGuid;
        FString KeeperPath;
        FVector KeeperAnchor = FVector::ZeroVector;
        bool bByPath = false;   // dropped because its path equals the keeper's
        float DistCm = -1.0f;   // anchor distance to the keeper; -1 when the member joined the group through a third entry
    };

    struct FResult
    {
        TMap<FGuid, FDrop> Drops;       // rows the rebuild must skip (never a carried entry)
        TSet<FString> DroppedPaths;     // every dropped row's path: the augments' dedupe set takes them so no name re-enters
        int32 Candidates = 0;           // rows carrying an original
        int32 CandidatesWithoutAnchor = 0;
        int32 Groups = 0;               // identity groups of two or more rows
        int32 AnchorDrops = 0;          // dropped by the anchor leg (different path)
        int32 PathDrops = 0;            // dropped by the path leg where the keeper is NOT carried (the rerolldup-1 poolPathRepeats class)
        int32 CarriedTwinsUnified = 0;  // carried rows of one original given the keeper's name (both stay under their miners)
        int32 PathTransfers = 0;        // keepers whose dead name was replaced by a member's live name
        int32 PathPairsApart = 0;       // same name, both anchored, >= radius apart: NOT united (a swapped name)
        int32 AnchorPairsBothLive = 0;  // anchors within the radius but two different names both resolving live: NOT united
        int32 ThirdRowDrops = 0;        // joined through a third row: neither leg matches the keeper directly
    };

    // Groups Layout rows carrying an original by identity and picks one keeper per group: carried first, then a pinned raw
    // original, then any pinned row, then a row with a stamped anchor, then the earliest row. Non-carried members are listed
    // in Out.Drops, except a member sharing a CARRIED keeper's path, which the rebuild's own carried-path drop handles.
    // Mutates only VanillaNodePath: a keeper whose name is dead takes a member's live name; a carried member takes its carried
    // keeper's name (both in Layout and in the Carried copies).
    void DedupeOriginals(TArray<FNodeShuffleEntry>& Layout, TArray<FNodeShuffleEntry>& Carried, float RadiusCm,
        TFunctionRef<bool(const FString& Path)> PathResolvesLive, FResult& Out);

    // One Display line per distinct rebuilt original path (pool rows that are originals, plus carried originals) that matched
    // no hide record of the previous roll; returns the count (lines beyond LineCap are counted, not printed).
    int32 ReportPathsWithoutPriorRecord(const TArray<FNodeShuffleEntry>& Pool, const TArray<FNodeShuffleEntry>& Carried,
        const TSet<FString>& PriorRecordPaths, int32 LineCap);

    // A VANILLA resource dealt onto a slot whose spawn class is not a base-game class spawns the vanilla representative of its
    // form instead. Measured on one class only: our lead_C-class actors (iron/coal/SAM dealt on bamrenew lead slots) never came
    // back with the save (0 adopted at load; 92 of the 166 load-time respawns in the log were lead_C), so a pinned one sat under
    // its miner with no node every session; the rule reaches every non-base-game slot class (Res_*2_C included), and ADOPTCLASS
    // measures per class which ones the save restores. Writes E.RepClassSpawnPath only, and only when the representative itself
    // is a base-game class.
    struct FVanillaRestamp
    {
        int32 Restamped = 0;
        int32 RepNotGeneric = 0;        // a slot that qualified but the representative was not a base-game class: left alone
        TMap<FString, TPair<FString, int32>> BySlotClass; // slot class -> (representative written, count)
    };
    bool RestampVanillaDealOffForeignClass(FNodeShuffleEntry& E, const FString& SolidRepresentative,
        const FString& LiquidRepresentative, uint8 FormLiquidByte, FVanillaRestamp& Census);
    void EmitVanillaRestampCensus(const TCHAR* Role, const FVanillaRestamp& Census);

    // DumpLayout: active entries that share an original by the ANCHOR leg while holding DIFFERENT paths (the twin shape the
    // path-keyed TWINS census cannot see until a rematch happens to rebind the sibling). Prints one LAYOUTDUMP-IDTWIN line
    // per pair up to LineCap; returns the number of distinct paths involved.
    int32 ReportIdentityTwins(const TArray<FNodeShuffleEntry>& Layout, float RadiusCm, int32 DumpId, int32 LineCap);
}
