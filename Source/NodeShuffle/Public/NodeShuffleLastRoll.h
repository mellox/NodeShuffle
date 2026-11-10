#pragma once

#include "CoreMinimal.h"

// mcp-lastroll-1 (2026-11-10): roll-time censuses kept as DATA for McpSnapshot section lastRoll (_team/mcp-mod-contract-v1.md).
// PLAIN C++ ON PURPOSE: these types are not reflected, the member that holds them carries no property macro, so nothing here is
// saved, replicated or visible to reflection, and the subsystem's reflected surface is unchanged. The member is written ONLY by
// non-const roll code (BeginRoll as the first statement of RollLayout; field copies placed directly after the census UE_LOG statement
// whose arguments they copy) and read ONLY by FNodeShuffleMcpReader::LastRoll through a const reference.
// Field names mirror the name= tokens of the census line they copy (tools/check_mcpcontract_lint.ps1, rule L, pins the mapping).

// ROLLDENSITY (ANodeShuffleSubsystem::RwgDensityBudgetEnd; RWG roll tail only). cellsAtCap is NOT copied: the line computes it with a
// call inside the log arguments, and the packet may copy only values already held in a variable. CellLinesPrintable is the local the
// line subtracts from to print cellLinesOmitted; the reader prints that difference.
struct FNodeShuffleLastRollDensity
{
    int32 CapturedInSeq = 0; // the RollSeq whose ROLLDENSITY line these fields copy; 0 = not printed in this session's latest roll
    bool bBudgetEnabled = false, bApiUsable = false, bGradesDone = false;
    int32 Calls = 0, CellsPerSide = 0, Cells = 0, LandCells = 0, LandSamplesPassed = 0, LandSamples = 0;
    int32 LatchMaskBefore = 0, LatchMaskAfter = 0, PinnedPrefilled = 0, PrefillOutsideGrid = 0, BudgetSum = 0, RoomOverCount = 0, Accepted = 0;
    int32 RejectedCellFull = 0, RejectedWater = 0, RejectedVoid = 0, RejectedKillbox = 0, RejectedOutside = 0, RejectedSlope = 0, RejectedSpacing = 0;
    int32 Draws = 0, FallbackCalls = 0, FallbackCellsOpened = 0, CellLinesPrinted = 0, CellLinesPrintable = 0;
    double CellCm = 0.0, LandSampleMs = 0.0; // doubles: the reader omits a non-finite one (contract R4.4)
};

// NOT HELD (2026-11-10, see the packet handoff): REROLLPOOL-CARRY. Its copies cannot sit next to its UE_LOG without breaking the
// rerolldup lint's U6 adjacency pin (census within 1800 characters of the Rolled layout line); widening that pin is the requester's call.
// PURITYCENSUS, RWGDEAL-RES and MODQUOTA-CFG print inside free functions that do not hold the subsystem.

struct FNodeShuffleLastRoll
{
    int32 RollSeq = 0;      // entries into RollLayout in this session (a roll that aborts still counts); 0 = none
    int32 Seed = 0;         // RollLayout's Seed argument on that entry
    bool bIsReroll = false; // RollLayout's bIsReroll argument on that entry
    FNodeShuffleLastRollDensity Density;

    // The ONE reset-and-sequence statement (first statement of RollLayout): clears every family, so no reply mixes two rolls.
    void BeginRoll(int32 InSeed, bool bInIsReroll)
    {
        const int32 Next = RollSeq + 1;
        *this = FNodeShuffleLastRoll();
        RollSeq = Next;
        Seed = InSeed;
        bIsReroll = bInIsReroll;
    }
};
