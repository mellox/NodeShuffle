// rwg-farspawn-1c C7 (2026-10-31): WELLMOVE -- one Display line at every site that writes a well-layout location after the roll, so a
// dump-to-dump well move can be joined to the write that made it (round-1 run: well A3FC5EA1 moved 4105 m between two LAYOUTDUMP
// dumps; the move was an EscalateWellPlacement RWG redeal, named in the log only by core path). Call sites (one line each):
// rollDeal + dealCleared (NodeShuffleWellRelocateRoll.cpp), nudge + rwgRedeal + boxRedeal (NodeShuffleWellEscalate.cpp), seaRedeal
// (NodeShuffleMapModeSea.cpp), resettleRedeal (NodeShuffleMapModeRock.cpp), placedCommit (NodeShuffleWellRelocateApply.cpp).
// The line prints the site's own name, never why the site was reached.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffle.h"
#include "Misc/PackageName.h"
#include "NodeShuffleWellRetype.h"

FVector ANodeShuffleSubsystem::WellMoveAt(const FNodeShuffleWellEntry& W)
{
    // Same precedence as the LAYOUTDUMP-W row (NodeShuffleLayoutDump.cpp), so from/to compare with that row's x/y/z.
    if (W.bGroupPlaced && !W.PlacedCoreLocation.IsNearlyZero()) { return W.PlacedCoreLocation; }
    if (W.bDestDealt && !W.DestCoreLocation.IsNearlyZero()) { return W.DestCoreLocation; }
    if (!W.RwgClaimCoreLocation.IsNearlyZero()) { return W.RwgClaimCoreLocation; }
    return W.VanillaCoreLocation;
}

void ANodeShuffleSubsystem::WellMoveLog(const FNodeShuffleWellEntry& W, const FVector& From, const FVector& To, const TCHAR* Reason) const
{
    // Legend (prose only): core is the short core path (the label WELLH2 and RWGSEAREDEAL-WELL lines print); claim is the first 8 hex
    // of the RWG claim guid, the same 8 characters the LAYOUTDUMP-W row prints as its guid; distM is the 3-D distance in metres;
    // reason is the name of the write site that printed the line; nudges and redeals are the entry's counters after the write.
    UE_LOG(LogNodeShuffle, Display, TEXT("WELLMOVE: core=%s claim=%s from=%s to=%s distM=%.1f reason=%s nudges=%d redeals=%d"),
        *WellShort(W.CorePath), *W.RwgClaimGuid.ToString().Left(8), *From.ToCompactString(), *To.ToCompactString(),
        FVector::Dist(From, To) / 100.0, Reason, static_cast<int32>(W.GroupNudges), static_cast<int32>(W.GroupRedeals));
}
