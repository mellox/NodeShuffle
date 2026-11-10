// mcp-lastroll-1 (2026-11-10): McpSnapshot section lastRoll -- READ-ONLY. Serves the roll censuses that roll code copied into the plain
// member ANodeShuffleSubsystem::LastRoll (NodeShuffleLastRoll.h) right after printing them. Same hard properties as
// NodeShuffleMcpSnapshot.cpp (tools/check_mcpcontract_lint.ps1 pins them here too): the subsystem and every LastRoll struct are reached
// only through CONST references; no load, trace, spawn, config or save access; no log line in this file (McpSnapshot logs the branch).
// Every field is a value copied right after its census UE_LOG statement ran; none states a cause. A family whose copy did not run in the
// latest roll reads captured false and carries no other field.

#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMcpCommon.h"
#include "NodeShuffleRwgDensityBudget.h" // LandSamplesPerCellSide: the constant the ROLLDENSITY line squares for landSamplesPerCell
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    // Contract R4.4: finite numbers only. A non-finite value is left out of the reply and counted.
    void NsMcpFinite(const TSharedRef<FJsonObject>& O, const TCHAR* Name, double V, int32& NonFinite)
    {
        if (FMath::IsFinite(V)) { O->SetNumberField(Name, FMath::RoundToDouble(V)); } else { ++NonFinite; }
    }

    // ROLLDENSITY: one object per roll, fields in the line's order. cellsAtCap is not served (not copied: see NodeShuffleLastRoll.h).
    void NsMcpDensityFields(const FNodeShuffleLastRollDensity& D, const TSharedRef<FJsonObject>& O)
    {
        int32 NonFinite = 0;
        O->SetBoolField(TEXT("budgetEnabled"), D.bBudgetEnabled);
        O->SetBoolField(TEXT("apiUsable"), D.bApiUsable);
        O->SetNumberField(TEXT("calls"), D.Calls);
        O->SetNumberField(TEXT("cellsPerSide"), D.CellsPerSide);
        O->SetNumberField(TEXT("cells"), D.Cells);
        NsMcpFinite(O, TEXT("cellCm"), D.CellCm, NonFinite);
        O->SetNumberField(TEXT("landCells"), D.LandCells);
        O->SetNumberField(TEXT("landSamplesPerCell"), NodeShuffleRwgDensityBudget::LandSamplesPerCellSide * NodeShuffleRwgDensityBudget::LandSamplesPerCellSide);
        O->SetNumberField(TEXT("landSamplesPassed"), D.LandSamplesPassed);
        O->SetNumberField(TEXT("landSamples"), D.LandSamples);
        NsMcpFinite(O, TEXT("landSampleMs"), D.LandSampleMs, NonFinite);
        O->SetBoolField(TEXT("gradesDone"), D.bGradesDone);
        O->SetNumberField(TEXT("latchMaskBefore"), D.LatchMaskBefore);
        O->SetNumberField(TEXT("latchMaskAfter"), D.LatchMaskAfter);
        O->SetNumberField(TEXT("pinnedPrefilled"), D.PinnedPrefilled);
        O->SetNumberField(TEXT("prefillOutsideGrid"), D.PrefillOutsideGrid);
        O->SetNumberField(TEXT("budgetSum"), D.BudgetSum);
        O->SetNumberField(TEXT("roomOverCount"), D.RoomOverCount);
        O->SetNumberField(TEXT("accepted"), D.Accepted);
        O->SetNumberField(TEXT("rejectedCellFull"), D.RejectedCellFull);
        O->SetNumberField(TEXT("rejectedWater"), D.RejectedWater);
        O->SetNumberField(TEXT("rejectedVoid"), D.RejectedVoid);
        O->SetNumberField(TEXT("rejectedKillbox"), D.RejectedKillbox);
        O->SetNumberField(TEXT("rejectedOutside"), D.RejectedOutside);
        O->SetNumberField(TEXT("rejectedSlope"), D.RejectedSlope);
        O->SetNumberField(TEXT("rejectedSpacing"), D.RejectedSpacing);
        O->SetNumberField(TEXT("draws"), D.Draws);
        O->SetNumberField(TEXT("fallbackCalls"), D.FallbackCalls);
        O->SetNumberField(TEXT("fallbackCellsOpened"), D.FallbackCellsOpened);
        O->SetNumberField(TEXT("cellLinesPrinted"), D.CellLinesPrinted);
        O->SetNumberField(TEXT("cellLinesOmitted"), D.CellLinesPrintable - D.CellLinesPrinted);
        O->SetNumberField(TEXT("nonFiniteOmitted"), NonFinite);
    }
}

FString FNodeShuffleMcpReader::LastRoll(const ANodeShuffleSubsystem& Sub, FString& OutBranch)
{
    const FNodeShuffleLastRoll& L = Sub.LastRoll;
    const bool bCaptured = L.RollSeq > 0;
    const bool bDensity = bCaptured && L.Density.CapturedInSeq == L.RollSeq;

    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    AddEnvelope(Root);
    Root->SetStringField(TEXT("section"), TEXT("lastRoll"));
    Root->SetBoolField(TEXT("captured"), bCaptured);
    Root->SetNumberField(TEXT("rollSeq"), L.RollSeq);
    if (!bCaptured) { Root->SetStringField(TEXT("note"), TEXT("rollSeq is 0: the roll routine has not been entered in this session")); }
    else
    {
        Root->SetNumberField(TEXT("seed"), L.Seed);
        Root->SetBoolField(TEXT("isReroll"), L.bIsReroll);
    }

    const TSharedRef<FJsonObject> Dens = MakeShared<FJsonObject>();
    Dens->SetBoolField(TEXT("captured"), bDensity);
    if (bDensity) { NsMcpDensityFields(L.Density, Dens); }
    Root->SetObjectField(TEXT("rollDensity"), Dens);

    // 64 KB rule: the reply is a fixed set of fields with no list to cut, so over the cap is too-large (never reached in practice).
    const FString Out = Serialize(Root);
    if (Utf8Bytes(Out) > MaxOutputBytes) { OutBranch += TEXT(" error: over cap"); return ErrorJson(TEXT("too-large"), TEXT("lastRoll: output exceeds the 64 KB cap")); }
    OutBranch += FString::Printf(TEXT(" rollSeq %d density %d"), L.RollSeq, bDensity ? 1 : 0);
    return Out;
}
