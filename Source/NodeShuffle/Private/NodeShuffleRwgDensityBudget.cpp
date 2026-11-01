// rwg-density-1 (2026-11-01, PER-CELL BUDGET) -- contract and motivation in NodeShuffleRwgDensityBudget.h.
// Reached only from RWG call sites: RollLayout (behind an Rwg map-mode test) and GenerateRwgLocations (RWG only by construction).
// Nothing here reads or advances the roll's FRandomStream, traces physics, or writes a layout field.
#include "NodeShuffleRwgDensityBudget.h"
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"
#include "Engine/World.h"

void NodeShuffleRwgDensityBudget::Apportion(int32 Target, const TArray<double>& Weights, TArray<int32>& Out)
{
    Out.SetNumZeroed(Weights.Num());
    double Sum = 0.0;
    for (const double W : Weights) { if (W > 0.0) { Sum += W; } }
    if (Target <= 0 || Sum <= 0.0 || Weights.Num() == 0) { return; }
    // Floors first, then the largest fractional remainders take the units the floors left (ties to the lower index).
    TArray<TPair<double, int32>> Remainders;
    Remainders.Reserve(Weights.Num());
    int32 Given = 0;
    int32 Heaviest = 0;
    for (int32 I = 0; I < Weights.Num(); ++I)
    {
        const double Raw = Weights[I] > 0.0 ? static_cast<double>(Target) * Weights[I] / Sum : 0.0;
        const int32 Floor = FMath::FloorToInt32(Raw);
        Out[I] = Floor;
        Given += Floor;
        Remainders.Emplace(Raw - Floor, I);
        if (Weights[I] > Weights[Heaviest]) { Heaviest = I; }
    }
    int32 Left = Target - Given;
    Remainders.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key > B.Key || (A.Key == B.Key && A.Value < B.Value); });
    for (int32 K = 0; K < Remainders.Num() && Left > 0; ++K)
    {
        const int32 I = Remainders[K].Value;
        if (Weights[I] <= 0.0) { continue; }
        ++Out[I];
        --Left;
    }
    if (Left > 0) { Out[Heaviest] += Left; } // floating-point slack only; keeps the sum exact
}

void FNodeShuffleRwgDensityState::Init(int32 InN, double InCellCm)
{
    N = InN;
    CellCm = InCellCm;
    const int32 Cells = N * N;
    LandFrac.SetNumZeroed(Cells);
    Prefill.SetNumZeroed(Cells);
    Accepted.SetNumZeroed(Cells);
    Budget.SetNumZeroed(Cells);
    Cap.SetNumZeroed(Cells);
}

int32 FNodeShuffleRwgDensityState::CellOf(double X, double Y) const
{
    if (N <= 0 || !FMath::IsFinite(X) || !FMath::IsFinite(Y)) { return -1; }
    const int32 Cx = FMath::FloorToInt32(X / CellCm) + N / 2;
    const int32 Cy = FMath::FloorToInt32(Y / CellCm) + N / 2;
    if (Cx < 0 || Cx >= N || Cy < 0 || Cy >= N) { return -1; }
    return Cy * N + Cx;
}

void FNodeShuffleRwgDensityState::BeginCall(int32 Count)
{
    ++Calls;
    bOpen = false;
    CallCount = Count;
    CallAccepted = CallRejCellFull = CallDraws = CallFallbackCellsOpened = CallRoomOverCount = CallSpotsLeftAtOpen = 0;
    const int32 Cells = N * N;
    TArray<int32> Fill;
    Fill.SetNumZeroed(Cells);
    int32 FillSum = 0;
    for (int32 I = 0; I < Cells; ++I) { Fill[I] = Prefill[I] + Accepted[I]; FillSum += Fill[I]; }
    CallFill = FillSum;
    CallBudgetSum = CallCapSum = 0;
    if (!bEnabled || Cells == 0) { return; }
    // This call's target is everything that will stand in the cells after it: the spots it deals plus what is already there.
    NodeShuffleRwgDensityBudget::Apportion(Count + FillSum, LandFrac, Budget);
    // Cap is a CEILING ON THE ROLL-CUMULATIVE Accepted[I] -- the count Admit and CellsAtCap compare against: the cell's accepted
    // count at this call's start plus its room (budget - fill, never below 0) plus the per-land-cell slack.
    int32 RoomSum = 0;
    for (int32 I = 0; I < Cells; ++I)
    {
        CallBudgetSum += Budget[I];
        const int32 Room = FMath::Max(0, Budget[I] - Fill[I]) + (LandFrac[I] > 0.0 ? NodeShuffleRwgDensityBudget::RoomSlackPerLandCell : 0);
        Cap[I] = Accepted[I] + Room;
        RoomSum += Room;
    }
    // Apportion is exact, so Sum(budget - fill) == Count and RoomSum >= Count whenever a lattice sample passed; RoomSum - Count is
    // the fill standing over budget (dropped by the clamp) plus the slack. RoomSum == 0 only when no sample passed anywhere.
    CallRoomOverCount = RoomSum - Count;
    RoomOverCountSum += CallRoomOverCount;
    if (RoomSum == 0) { bOpen = true; ++FallbackCalls; CallSpotsLeftAtOpen = Count; } // nothing to cap against: this call draws uniform (counted as a fallback call)
    CallCapSum = RoomSum;
}

bool FNodeShuffleRwgDensityState::Admit(const FVector& C)
{
    if (!bEnabled || bOpen) { return true; }
    const int32 I = CellOf(C.X, C.Y);
    if (I < 0) { return true; }
    if (Accepted[I] >= Cap[I]) { ++RejCellFull; ++CallRejCellFull; return false; }
    return true;
}

void FNodeShuffleRwgDensityState::Accept(const FVector& C)
{
    ++AcceptedTotal;
    ++CallAccepted;
    const int32 I = CellOf(C.X, C.Y);
    if (I >= 0) { ++Accepted[I]; }
}

void FNodeShuffleRwgDensityState::MaybeOpen(int32 AttemptsLeft, int32 SpotsLeft)
{
    if (!bEnabled || bOpen || SpotsLeft <= 0) { return; }
    if (AttemptsLeft > SpotsLeft * NodeShuffleRwgDensityBudget::FallbackReserveDrawsPerSpot) { return; }
    bOpen = true;
    ++FallbackCalls;
    const int32 AtCap = CellsAtCap();
    FallbackCellsOpened += AtCap;
    CallFallbackCellsOpened = AtCap;
    CallSpotsLeftAtOpen = SpotsLeft;
}

int32 FNodeShuffleRwgDensityState::CellsAtCap() const
{
    int32 AtCap = 0;
    for (int32 I = 0; I < Cap.Num(); ++I) { if (LandFrac[I] > 0.0 && Accepted[I] >= Cap[I]) { ++AtCap; } }
    return AtCap;
}

void FNodeShuffleRwgDensityState::EndCall(int32 Drawn, int32 Outside, int32 Water, int32 Void, int32 Killbox, int32 Slope, int32 Spacing)
{
    CallDraws = Drawn;
    Draws += Drawn;
    RejOutside += Outside;
    RejWater += Water;
    RejVoid += Void;
    RejKillbox += Killbox;
    RejSlope += Slope;
    RejSpacing += Spacing;
    // One line per GenerateRwgLocations call (the extras deal, then the relocation deal). Its draws and rejectedCellFull close this
    // call's ROLLCENSUS sum: drawn = dealt + rejectedOutside + rejectedWater + rejectedVoid + rejectedKillbox + rejectedSlope + rejectedSpacing + rejectedCellFull.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("ROLLDENSITY-CALL call=%d budgetEnabled=%d requested=%d fillBefore=%d budgetSum=%d capSum=%d roomOverCount=%d accepted=%d rejectedCellFull=%d draws=%d capsLiftedThisCall=%d fallbackCellsOpened=%d spotsLeftAtOpen=%d"),
        Calls, bEnabled ? 1 : 0, CallCount, CallFill, CallBudgetSum, CallCapSum, CallRoomOverCount, CallAccepted, CallRejCellFull, CallDraws, bOpen ? 1 : 0, CallFallbackCellsOpened, CallSpotsLeftAtOpen);
}

void ANodeShuffleSubsystem::RwgDensityBudgetBegin(bool bToggleOn, const TArray<FVector>& Prefill)
{
    RwgDensityState = MakeShared<FNodeShuffleRwgDensityState>();
    FNodeShuffleRwgDensityState& S = *RwgDensityState;
    const NodeShuffleMapMode::FRwgGroundQuery Q(GetWorld(), RwgGenerator.Get());
    S.bApiUsable = Q.IsUsable();
    S.bEnabled = bToggleOn && S.bApiUsable;
    if (!S.bApiUsable) { return; } // the fallback deal runs; End prints the census with apiUsable=0
    const double H = Q.HalfExtent();
    double CellCm = NodeShuffleRwgDensityBudget::CellSizeCm;
    int32 Half = FMath::Max(1, FMath::CeilToInt32(H / CellCm));
    if (Half * 2 > NodeShuffleRwgDensityBudget::MaxCellsPerSide) { Half = NodeShuffleRwgDensityBudget::MaxCellsPerSide / 2; CellCm = H / Half; }
    S.Init(Half * 2, CellCm);
    for (const FVector& P : Prefill)
    {
        const int32 I = S.CellOf(P.X, P.Y);
        if (I >= 0) { ++S.Prefill[I]; } else { ++S.PrefillOutsideGrid; }
    }
    if (!S.bEnabled) { return; }
    RwgBuildWaterVolumeCache(); // the deal rebuilds it at its own start (NodeShuffleMapModePlace.cpp); the lattice samples the same volumes
    // The land lattice: the dealer's own land chain at fixed sub-centres of every cell (no FRandomStream draw, no physics trace).
    // RwgRejectV3's graders latch on their first call of a session; the masks before/after say whether this sampling was that call.
    S.bGradesDone = bRwgSeaCheckDone && bRwgVoidCheckDone && bRwgKillboxDone;
    S.LatchMaskBefore = (bRwgSeaUsable ? 1 : 0) | (bRwgVoidUsable ? 2 : 0) | (bRwgKillboxKnown ? 4 : 0) | (bRwgSeaCheckDone ? 8 : 0) | (bRwgVoidCheckDone ? 16 : 0) | (bRwgKillboxDone ? 32 : 0);
    const double T0 = FPlatformTime::Seconds();
    constexpr int32 Sub = NodeShuffleRwgDensityBudget::LandSamplesPerCellSide;
    for (int32 I = 0; I < S.N * S.N; ++I)
    {
        const int32 Cx = S.CellX(I), Cy = S.CellY(I);
        int32 Passed = 0;
        for (int32 Sy = 0; Sy < Sub; ++Sy)
        {
            for (int32 Sx = 0; Sx < Sub; ++Sx)
            {
                const double X = (Cx + (Sx + 0.5) / Sub) * CellCm;
                const double Y = (Cy + (Sy + 0.5) / Sub) * CellCm;
                if (RwgDensityLandSample(Q, X, Y)) { ++Passed; }
            }
        }
        S.LandFrac[I] = static_cast<double>(Passed) / (Sub * Sub);
        S.LandSamplesPassed += Passed;
        S.LandSamplesTotal += Sub * Sub;
        if (Passed > 0) { ++S.LandCells; }
    }
    S.LandSampleMs = (FPlatformTime::Seconds() - T0) * 1000.0;
    S.LatchMaskAfter = (bRwgSeaUsable ? 1 : 0) | (bRwgVoidUsable ? 2 : 0) | (bRwgKillboxKnown ? 4 : 0) | (bRwgSeaCheckDone ? 8 : 0) | (bRwgVoidCheckDone ? 16 : 0) | (bRwgKillboxDone ? 32 : 0);
}

void ANodeShuffleSubsystem::RwgDensityBudgetEnd()
{
    if (!RwgDensityState.IsValid()) { return; }
    const FNodeShuffleRwgDensityState& S = *RwgDensityState;
    const int32 Cells = S.N * S.N;
    int32 PrefillSum = 0, BudgetSum = 0, Printable = 0;
    for (int32 I = 0; I < Cells; ++I)
    {
        PrefillSum += S.Prefill[I];
        BudgetSum += S.Budget[I];
        if (S.Budget[I] > 0 || S.Prefill[I] > 0) { ++Printable; }
    }
    const int32 Printed = FMath::Min(Printable, NodeShuffleRwgDensityBudget::CellLinesMax);
    // 32 fields, 32 arguments (hand-counted). Every value is a count or a predicate measured in this roll.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("ROLLDENSITY: budgetEnabled=%d apiUsable=%d calls=%d cellsPerSide=%d cells=%d cellCm=%.0f landCells=%d landSamplesPerCell=%d landSamplesPassed=%d landSamples=%d landSampleMs=%.0f gradesDone=%d latchMaskBefore=%d latchMaskAfter=%d pinnedPrefilled=%d prefillOutsideGrid=%d budgetSum=%d roomOverCount=%d accepted=%d rejectedCellFull=%d rejectedWater=%d rejectedVoid=%d rejectedKillbox=%d rejectedOutside=%d rejectedSlope=%d rejectedSpacing=%d draws=%d fallbackCalls=%d fallbackCellsOpened=%d cellsAtCap=%d cellLinesPrinted=%d cellLinesOmitted=%d"),
        S.bEnabled ? 1 : 0, S.bApiUsable ? 1 : 0, S.Calls, S.N, Cells, S.CellCm, S.LandCells, NodeShuffleRwgDensityBudget::LandSamplesPerCellSide * NodeShuffleRwgDensityBudget::LandSamplesPerCellSide, S.LandSamplesPassed, S.LandSamplesTotal, S.LandSampleMs,
        S.bGradesDone ? 1 : 0, S.LatchMaskBefore, S.LatchMaskAfter, PrefillSum, S.PrefillOutsideGrid, BudgetSum, S.RoomOverCountSum, S.AcceptedTotal, S.RejCellFull, S.RejWater, S.RejVoid, S.RejKillbox, S.RejOutside, S.RejSlope, S.RejSpacing,
        S.Draws, S.FallbackCalls, S.FallbackCellsOpened, S.CellsAtCap(), Printed, Printable - Printed);
    UE_LOG(LogNodeShuffle, Display, TEXT("ROLLDENSITY-LEGEND: Cells are squares of the printed cell size (1000 m unless the world is wider than 256 km), anchored at the world origin; a cell index is the floor of the coordinate in centimetres divided by the cell size. A cell's budget is its land share of the last call's target, land being the lattice samples per cell that pass the dealer's land chain. The pinned count is the carried and pinned entries already standing in the cell at the roll. A cell's cap is the most accepted spots it may hold by the end of a call: its accepted count when the call began plus its room, the room being its budget less what already stands in it, never below zero, plus a slack on every land cell. The call line's cap sum is the room summed over cells and its room over count is that sum less the spots the call requested. A draw counted as cell full passed the land chain and met a cell whose accepted count had reached its cap. The fallback lifts every cap for the rest of a call once the draws left fall to the reserve per spot still needed; the opened count is the land cells at their cap at that moment and the spots left are the spots the call still needed then. The latch masks add 1, 2 and 4 for the sea, void and killbox terms usable and 8, 16 and 32 for each grader having run. Cell lines print at Display with diagnostic logging on, else at Verbose.")));
    const bool bDiag = FNodeShuffleModule::AreDiagnosticsEnabled();
    int32 Lines = 0;
    for (int32 I = 0; I < Cells && Lines < NodeShuffleRwgDensityBudget::CellLinesMax; ++I)
    {
        if (S.Budget[I] <= 0 && S.Prefill[I] <= 0) { continue; }
        ++Lines;
        const FString Line = FString::Printf(TEXT("ROLLDENSITY-CELL cx=%d cy=%d landFrac=%.3f budget=%d pinned=%d accepted=%d cap=%d"),
            S.CellX(I), S.CellY(I), S.LandFrac[I], S.Budget[I], S.Prefill[I], S.Accepted[I], S.Cap[I]);
        if (bDiag) { UE_LOG(LogNodeShuffle, Display, TEXT("%s"), *Line); } else { UE_LOG(LogNodeShuffle, Verbose, TEXT("%s"), *Line); }
    }
    RwgDensityState.Reset(); // the late-node deal (NodeShuffleLateNodes.cpp) runs with no state: uniform over land, as before
}
