#pragma once

// ns-scanfilter-4 (2026-11-14): module-static state SHARED by the hook side (NodeShuffleScanFilter.cpp) and the row side
// (NodeShuffleScanFilterRows.cpp), split out so NodeShuffleScanFilter.cpp comes back under the 500-line rule. Game thread only.
// Round 3 added GAutoRow and the row-state entry points (NodeShuffleScanFilterState.cpp); the rest are the variables that lived
// in NodeShuffleScanFilter.cpp's anonymous namespace.

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UWorld;
class UClass;
class UObject;
class UConfigPropertyBool;

namespace NodeShuffleScanFilter
{
    struct FCallFigures { int32 In = 0; int32 Out = 0; int32 HidByRow = 0; int32 HidByDefault = 0; int32 Modded = 0; int32 Vanilla = 0; bool bSnapshot = false; int32 AddedByRow = 0; };

    extern TWeakObjectPtr<UWorld> GSnapWorld;      // the world whose rows GRowShown holds; the hook filters only for this world
    extern TMap<FString, bool> GRowShown;          // PLAYER-SET row path -> switch value, rebuilt from the live tree by every Tick
    extern TSet<FString> GAutoRow;                 // round 3: AUTO row paths (modded); the hook keeps their entry unless All Minable
    extern TSet<FString> GSeenNoRow;               // source (b): modded classes the hook met with no row; kept for the world session
    extern TArray<TWeakObjectPtr<UClass>> GAddOrder; // ON rows whose class the periodic pass resolved, in row order (weak)
    extern int32 GCallsTotal;
    extern int32 GCallsSinceLine;
    extern double GLastCallLineTime;
    extern FCallFigures GLatest;
    extern FCallFigures GLastCensus;

    bool SameFigures(const FCallFigures& A, const FCallFigures& B);
    void EmitCallCensus(double Now);
    // Resets the hook-side half of the per-world state (the row side resets its own half and calls this).
    void ResetHookSideForWorld();

    // ---- round 3 (user decision "One box, follows game"): row states, NodeShuffleScanFilterState.cpp. Called from Tick only. ----
    struct FShowRow { FString Path; UConfigPropertyBool* Show = nullptr; };
    struct FRowStateCounts { int32 AutoRows = 0; int32 PlayerSetRows = 0; int32 BecamePlayerSet = 0; int32 Mirrored = 0; int32 MirrorUnknown = 0; };
    void EnsureRowStateRecordLoaded();            // reads the record file once per process
    void ApplyPendingRowStateForgets();          // after the file is read, BEFORE the carry-over notes its rows
    void NoteCarriedRow(const FString& Path, bool bValue); // the carry-over: that row is PLAYER-SET with its old value
    void ResolveMissingRecordOnce(const TSet<FString>& RowsBeforeAdds, bool bCarriedThisPass);
    int32 DropRecordsWithoutRows(const TSet<FString>& RowsBeforeAdds); // before the adds: a deleted then re-created row is AUTO
    FRowStateCounts UpdateRowStates(const UObject* WorldContext, const TArray<FShowRow>& Shows); // touch detection, then the mirror, then the record
    bool IsRowPlayerSet(const FString& Path);
    void FlushRowStateRecordIfDirty();            // after the config save; never from a hook
}
