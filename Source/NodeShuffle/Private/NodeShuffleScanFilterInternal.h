#pragma once

// ns-scanfilter-4 (2026-11-14): module-static state SHARED by the hook side (NodeShuffleScanFilter.cpp) and the row side
// (NodeShuffleScanFilterRows.cpp), split out so NodeShuffleScanFilter.cpp comes back under the 500-line rule. Game thread only.
// Nothing here is new behaviour: these are the same variables that lived in NodeShuffleScanFilter.cpp's anonymous namespace.

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UWorld;
class UClass;

namespace NodeShuffleScanFilter
{
    struct FCallFigures { int32 In = 0; int32 Out = 0; int32 HidByRow = 0; int32 HidByDefault = 0; int32 Modded = 0; int32 Vanilla = 0; bool bSnapshot = false; int32 AddedByRow = 0; };

    extern TWeakObjectPtr<UWorld> GSnapWorld;      // the world whose rows GRowShown holds; the hook filters only for this world
    extern TMap<FString, bool> GRowShown;          // row path -> switch value, rebuilt from the live tree by every Tick
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
}
