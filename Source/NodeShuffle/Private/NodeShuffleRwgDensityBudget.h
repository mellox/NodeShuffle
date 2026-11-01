// rwg-density-1 (2026-11-01, PER-CELL BUDGET) -- not the 2026-10-19 RWGDENSITY census packet that shares the name.
// The RWG dealer (GenerateRwgLocations, NodeShuffleMapModePlace.cpp) draws X,Y uniformly over the whole deal square and keeps
// whatever passes the land chain (outside, RwgRejectV3, slope) and the spacing test. Measured on the user's save (2026-11-01):
// 972 fresh entries over 118 occupied 1 km cells, variance/mean 2.52 against the Poisson 1, and the cell at the base held 13
// pinned entries plus 12 fresh. Decision (user, 2026-11-01): keep uniform-over-land as the model, but cap each 1 km cell at
// its LAND SHARE of the target, count carried and pinned entries as already filling their cell, and spread any shortfall
// over cells that still have room so the configured total is still dealt.
//
// Contract:
//   - RwgDensityBudgetBegin (RollLayout, RWG only, before the extras deal) samples a lattice through the dealer's own land test
//     (RwgDensityLandSample: Outside -> RwgRejectV3 -> RwgApiSlopeTooSteep, the same functions -- never a physics trace, so the
//     roll stays independent of what is streamed around the player) and pre-fills each cell with the carried + pinned entries.
//   - Each GenerateRwgLocations call apportions ITS target (this call's count plus everything already in the cells) over the
//     cells by land fraction (largest remainder, ties to the lower index), caps a cell at budget minus fill, and re-apportions
//     the deficit (cells whose fill already exceeds their budget) over cells that still have room. A land draw whose cell is
//     at its cap is counted rejectedCellFull and redrawn; nothing here consumes the roll's FRandomStream, so a seed's draw
//     SEQUENCE is unchanged and only which draws are kept differs (a given seed gives a different layout than the pre-packet
//     build; the OFF toggle restores the pre-packet layout for the same seed).
//   - FALLBACK: once the draws left in a call fall to FallbackReserveDrawsPerSpot per spot still needed, every cap is lifted
//     for the rest of that call (uniform over land again, the pre-packet rule) so the total is dealt rather than given up on;
//     the census prints how many land cells were at their cap at that moment (fallbackCellsOpened).
//   - RwgDensityBudgetEnd (RollLayout tail) prints ROLLDENSITY once per roll and up to CellLinesMax ROLLDENSITY-CELL lines
//     (Display with Enable Diagnostic Logging on, else Verbose), then drops the state; the late-node deal
//     (NodeShuffleLateNodes.cpp) runs with no state and keeps the uniform draw.
// Every printed value is a count or a predicate measured in THIS roll; no line states why a cell is full.
#pragma once

#include "CoreMinimal.h"

namespace NodeShuffleRwgDensityBudget
{
    constexpr double CellSizeCm = 100000.0;          // 1 km cells, anchored at the world origin: cell index = floor(cm / 100000), the density_cells.py convention
    constexpr int32 LandSamplesPerCellSide = 4;      // 4 x 4 lattice per cell = 16 samples; 900 cells -> 14,400 samples. TODO(2026-11-01 rwg-density-1 budget): roll-time cost predicted ~0.8 s from RWGDENSITY's measured 57 us/sample; read landSampleMs on the first re-roll and drop to 3 if above 1500 ms
    constexpr int32 MaxCellsPerSide = 256;           // bound on the grid; a world wider than 256 km gets coarser cells (CellCm grows), printed as cellCm
    constexpr int32 FallbackReserveDrawsPerSpot = 20; // caps lift when the draws left in a call <= this * spots still needed (today's uniform deal needs ~14 draws per accept)
    constexpr int32 CellLinesMax = 120;              // ROLLDENSITY-CELL lines per roll, in index order; the rest are counted in cellLinesOmitted

    // Largest-remainder apportionment of Target over Weights, ties to the lower index. Sum(Out) == Target when the weights sum
    // to more than zero and Target >= 0; otherwise Out is all zero.
    void Apportion(int32 Target, const TArray<double>& Weights, TArray<int32>& Out);
}

struct FNodeShuffleRwgDensityState
{
    bool bEnabled = false;      // toggle ON and the API usable at Begin: caps apply. false = the draw is unchanged; the census still counts
    bool bApiUsable = false;
    int32 N = 0;                // cells per side (even); the grid covers [-N/2 * CellCm, N/2 * CellCm) on both axes
    double CellCm = NodeShuffleRwgDensityBudget::CellSizeCm;
    TArray<double> LandFrac;    // per cell: lattice samples passing the land chain / samples per cell
    TArray<int32> Prefill;      // per cell: carried + pinned entries at Begin
    TArray<int32> Accepted;     // per cell: spots accepted by every call of this roll
    TArray<int32> Budget;       // per cell: the last call's apportionment of (count + fill)
    TArray<int32> Cap;          // per cell: the last call's cap on Accepted (budget - fill, plus its deficit share)
    bool bOpen = false;         // caps lifted for the rest of the current call (fallback)
    // Roll totals (across calls).
    int32 Calls = 0, Draws = 0, AcceptedTotal = 0, RejCellFull = 0, RejWater = 0, RejVoid = 0, RejKillbox = 0, RejOutside = 0, RejSlope = 0, RejSpacing = 0;
    int32 FallbackCalls = 0, FallbackCellsOpened = 0, CapDeficitSpread = 0, PrefillOutsideGrid = 0;
    // Begin's land sampling.
    int32 LandCells = 0, LandSamplesPassed = 0, LandSamplesTotal = 0, LatchMaskBefore = 0, LatchMaskAfter = 0;
    bool bGradesDone = false;
    double LandSampleMs = 0.0;
    // Per-call (reset by BeginCall).
    int32 CallCount = 0, CallFill = 0, CallBudgetSum = 0, CallCapSum = 0, CallCapDeficit = 0, CallAccepted = 0, CallRejCellFull = 0, CallDraws = 0, CallFallbackCellsOpened = 0;

    void Init(int32 InN, double InCellCm);
    int32 CellOf(double X, double Y) const;  // -1 = off the grid
    int32 CellX(int32 Index) const { return Index % N - N / 2; } // origin-anchored index, as density_cells.py prints it
    int32 CellY(int32 Index) const { return Index / N - N / 2; }
    void BeginCall(int32 Count);             // apportion this call's target, set the caps, clear bOpen
    bool Admit(const FVector& C);            // false = the cell's accepted count has reached its cap (counted); always true when !bEnabled or bOpen
    void Accept(const FVector& C);
    void MaybeOpen(int32 AttemptsLeft, int32 SpotsLeft); // the fallback test, once per draw
    void EndCall(int32 Drawn, int32 Outside, int32 Water, int32 Void, int32 Killbox, int32 Slope, int32 Spacing); // accumulate + ROLLDENSITY-CALL
    int32 CellsAtCap() const;                // land cells whose accepted count is at or above the last cap
};
