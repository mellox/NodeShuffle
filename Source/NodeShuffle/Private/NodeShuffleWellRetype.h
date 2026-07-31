#pragma once

// Packet H1 (ns-wells-h1): the shared, PURE helpers of the in-place resource-well retype, split out so
// NodeShuffleWellRoll.cpp (the deal) and NodeShuffleWellRetype.cpp (the apply) share ONE implementation
// instead of two that drift. Exactly the split H0 made between NodeShuffleWellCensus.h and
// NodeShuffleWellDump.cpp, and Packet G before it between ExtractorDiscovery and ExtractorDump -- and
// forced here by the same 500-line file limit once the ns-review-h1 fixes landed.
//
// Every function here is PURE and does not log: each caller owns its own prefix and cadence.
//
// WHAT LIVES WHERE, so the next reader does not hunt:
//   * NodeShuffleWellRoll.cpp    -- ANodeShuffleSubsystem::RollWellLayout (discovery, merge, the deal)
//   * NodeShuffleWellRetype.cpp  -- ANodeShuffleSubsystem::ApplyWellRetype + RetypeWellMember (the write)
// Both are MEMBER functions of ANodeShuffleSubsystem, merely defined in those files: writing
// AFGResourceNodeBase's PRIVATE mResourceClassOverride depends on this project's AccessTransformers
// Friend grant, and C++ friendship is class-to-class, not file-to-file -- the same constraint
// NodeShuffle.h documents for FNodeShuffleModule::DbgLogAcceptance. Defining members across several
// translation units keeps ~600 lines out of an already 8000-line file without giving up that access.

#include "CoreMinimal.h"
#include "Misc/PackageName.h"
#include "Resources/FGResourceNodeBase.h"

inline FString WellPathOf(const UObject* Obj) { return Obj ? Obj->GetPathName() : FString(); }
inline FString WellClassPathOf(const UObject* Obj) { return Obj ? Obj->GetClass()->GetPathName() : FString(); }

// Short, readable label for a log line; the full path is printed alongside where it matters.
inline FString WellShort(const FString& Path)
{
    return Path.IsEmpty() ? FString(TEXT("<none>")) : FPackageName::ObjectPathToObjectName(Path);
}

// The AUTHORED resource, never the effective one. GetResourceClassOriginal() reads mResourceClass, which
// NodeShuffle never writes, so it stays truthful even on a well we have already retyped -- whereas
// GetResourceClass() would hand our own previous override back into the next deck and make the pool a
// function of its own output (memory: lessons-output-derived-input-is-a-loop).
inline FString WellAuthoredResourcePath(const AFGResourceNodeBase* Node)
{
    if (!Node) { return FString(); }
    const UClass* Res = Node->GetResourceClassOriginal().Get();
    return Res ? Res->GetPathName() : FString();
}
