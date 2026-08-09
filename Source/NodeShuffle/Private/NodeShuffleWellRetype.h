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
// T16: the pin predicate below needs the layout entry (FNodeShuffleWellEntry, for bGroupPlaced and the
// satellite paths that key the spawned handles) and H2's ONE occupancy predicate, IsWellMemberInUse --
// which also brings AFGResourceNodeFrackingCore / ...Satellite and the by-value TWeakObjectPtr
// completeness those accessors need. Neither of those headers includes THIS one (both mentions of
// "NodeShuffleWellRetype.h" in them are comments), so there is no cycle.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleWellRelocate.h"

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

// =================================================================================================
// T16 -- WHICH ACTOR ANSWERS "HAS SOMEONE BUILT ON THIS WELL?"   (docs/TECH-DEBT.md T16,
//        docs/reviews/2026-08-08-reroll-guard-coldreview.md F1)
// =================================================================================================
// THE DEFECT THIS EXISTS TO REMOVE. Both consumers used to ask the VANILLA ORIGINAL core/satellites.
// For a RELOCATED well (bGroupPlaced) that question is unanswerable by construction: the relocation
// hides the original, disables its collision and deregisters it from the node manager, so a player
// CANNOT build on it and the occupancy signals can never be true. Measured, one roll, from the
// author's log: 17 of 20 wells relocated, every one `managed=1 pinned=0`. The actor a player can
// actually build on is OUR SPAWNED core/satellites -- the very actors the ns-review-h2 F1 census
// filter excludes, deliberately, from enrolment. So a well with a Pressurizer and extractors on it
// reported UNPINNED, in both the deal and the apply-time re-check.
//
// WHY IT LIVES IN THIS HEADER AND NOT AT EITHER CALL SITE. This file's own docblock states its
// reason for existing: the shared PURE helpers of the well retype, "split out so
// NodeShuffleWellRoll.cpp (the deal) and NodeShuffleWellRetype.cpp (the apply) share ONE
// implementation instead of two that drift". The pin is exactly that -- ONE rule with TWO
// evaluation times (design: "the two pin rules are the SAME rule evaluated at two times, which is
// what stops them diverging again", ns-review-h1 W1). T8 and ns-review-h2 F3 in this project were
// both caused by one rule living in two places, only one of which was complete; a two-call-site
// patch of T16 would be the third instance.
//
// PURE, like everything else here: it reads live actor state, writes nothing, and LOGS NOTHING --
// it returns the provenance so each caller can print it with its own prefix and cadence.
enum class ENodeShuffleWellPinSource : uint8
{
    NotEvaluated,           // this roll never reached the well (its core did not stream in, etc.)
    Nothing,                // evaluated, but not one candidate actor resolved -- no answer at all
    Original,               // the vanilla level actors: the well has NOT been relocated
    Spawned,                // OUR relocated actors: the only ones a player can build on
    OriginalWhileRelocated, // relocated, but no spawned core handle resolved -- fallback, see below
};

inline const TCHAR* WellPinSourceName(ENodeShuffleWellPinSource Source)
{
    switch (Source)
    {
    case ENodeShuffleWellPinSource::Nothing:                return TEXT("NOTHING-RESOLVED");
    case ENodeShuffleWellPinSource::Original:               return TEXT("original(level actors)");
    case ENodeShuffleWellPinSource::Spawned:                return TEXT("spawned(our relocated actors)");
    case ENodeShuffleWellPinSource::OriginalWhileRelocated: return TEXT("original-FALLBACK(relocated, no spawned handle)");
    default:                                                return TEXT("not-evaluated");
    }
}

// WHAT WAS TESTED AND WHAT FIRED. Core and satellite verdicts are kept SEPARATE because the apply
// half distinguishes them: a core-occupancy pin is ignored once the resource is already written to
// that core (there is nothing left to change), while a satellite-extractor pin stands down
// unconditionally (ns-review-h1 W1). Collapsing them into one bool here would silently change that.
struct FNodeShuffleWellPinCheck
{
    ENodeShuffleWellPinSource Source = ENodeShuffleWellPinSource::NotEvaluated;

    bool bCoreInUse = false;
    bool bSatelliteInUse = false;
    const TCHAR* CoreWhy = TEXT("");       // which signal fired, verbatim from IsWellMemberInUse
    const TCHAR* SatelliteWhy = TEXT("");
    FString FiredActorName;                // the first actor that reported in-use, for the log

    // The core the verdict was actually read from -- SPAWNED for a relocated well, otherwise the
    // original. The caller reads its live resource through this pointer rather than re-deriving which
    // actor it should have asked (that re-derivation IS the defect). Null when nothing resolved.
    AFGResourceNodeFrackingCore* ResolvedCore = nullptr;

    int32 CoresTested = 0;         // 0 or 1
    int32 SatellitesTested = 0;    // how many candidate satellites actually resolved to live actors
    int32 SatellitesExpected = 0;  // how many the layout entry lists

    bool IsPinned() const { return bCoreInUse || bSatelliteInUse; }

    // DECISIVE = "false really means false". A pin found is always decisive. A pin NOT found is only
    // decisive when the actors tested are the ones a player could have built on: the originals for an
    // un-relocated well, our spawns for a relocated one. The OriginalWhileRelocated fallback is NOT
    // decisive -- it tests a hidden, de-collided, deregistered actor, which is precisely the false
    // negative T16 is about. Callers must not CLEAR an existing pin on a non-decisive verdict; that
    // unconditional clear is the second half of the defect (NodeShuffleWellRoll.cpp's old `:190`).
    bool IsDecisive() const
    {
        return IsPinned()
            || ((Source == ENodeShuffleWellPinSource::Original || Source == ENodeShuffleWellPinSource::Spawned)
                && CoresTested > 0);
    }
};

// THE ACTOR-TESTING HALF, ON ITS OWN (T17, 2026-08-08).
// A PURE EXTRACTION of EvaluateWellPin's tail -- not one line of the test changed, and
// EvaluateWellPin below now delegates to it, so its two existing consumers (the roll and
// ApplyWellRetype) behave exactly as they did when T16 landed.
//
// WHY THE SPLIT EXISTS. The pin is ONE rule, but its callers arrive with the population in two
// different states. The roll and the apply hold a LAYOUT ENTRY and need the population SELECTED from
// it; SpawnWellGroup has just resolved the live spawned core and satellites itself and must NOT
// re-derive them (re-deriving which actor to ask IS the T16 defect, and doing it twice in one pass
// would also be the "one rule in two places" shape that caused T8 and ns-review-h2 F3). So:
// selection lives in ONE function, the test lives in ONE function, and neither is duplicated.
//
// Candidates is used verbatim -- nothing is filtered here, because IsWellMemberInUse is null-safe and
// the FiredActorName read only happens once it has returned true (which requires a live actor).
// SatellitesExpected is REPORTED, never used to decide anything.
inline FNodeShuffleWellPinCheck EvaluateWellPinOnActors(
    ENodeShuffleWellPinSource Source,
    AFGResourceNodeFrackingCore* ResolvedCore,
    const TArray<AFGResourceNodeFrackingSatellite*>& Candidates,
    int32 SatellitesExpected)
{
    FNodeShuffleWellPinCheck Check;
    Check.Source = Source;
    Check.SatellitesExpected = SatellitesExpected;
    if (IsValid(ResolvedCore)) { Check.ResolvedCore = ResolvedCore; }

    Check.CoresTested = Check.ResolvedCore ? 1 : 0;
    Check.SatellitesTested = Candidates.Num();
    if (Check.CoresTested == 0 && Check.SatellitesTested == 0)
    {
        Check.Source = ENodeShuffleWellPinSource::Nothing;
        return Check;
    }

    if (Check.ResolvedCore)
    {
        const TCHAR* Why = TEXT("");
        if (IsWellMemberInUse(Check.ResolvedCore, Why))
        {
            Check.bCoreInUse = true;
            Check.CoreWhy = Why;
            Check.FiredActorName = Check.ResolvedCore->GetName();
        }
    }
    for (AFGResourceNodeFrackingSatellite* Sat : Candidates)
    {
        const TCHAR* Why = TEXT("");
        if (IsWellMemberInUse(Sat, Why))
        {
            Check.bSatelliteInUse = true;
            Check.SatelliteWhy = Why;
            if (Check.FiredActorName.IsEmpty()) { Check.FiredActorName = Sat->GetName(); }
            break; // one is enough: the pin is a group property
        }
    }
    return Check;
}

// OriginalCore / OriginalSats are whatever the CALLER already resolved for the vanilla group (the
// census actors at roll time, the FindOriginalBaseByPath results at apply time) -- this helper does
// no lookups of its own beyond the two spawned-handle maps, so it stays free of the subsystem.
inline FNodeShuffleWellPinCheck EvaluateWellPin(
    const FNodeShuffleWellEntry& E,
    const TMap<FString, AFGResourceNodeFrackingCore*>& SpawnedCores,
    const TMap<FString, AFGResourceNodeFrackingSatellite*>& SpawnedSats,
    AFGResourceNodeFrackingCore* OriginalCore,
    const TArray<AFGResourceNodeFrackingSatellite*>& OriginalSats)
{
    TArray<AFGResourceNodeFrackingSatellite*> Candidates;

    // SOURCE SELECTION, the whole fix in five lines. A relocated well is answered by our spawned
    // group; anything else by the level actors. The fallback is deliberately NOT "test both": mixing
    // populations would make the provenance in the log unreadable, and the fallback's own result is
    // marked non-decisive above rather than being trusted.
    AFGResourceNodeFrackingCore* const* FoundSpawnedCore = E.bGroupPlaced ? SpawnedCores.Find(E.CorePath) : nullptr;
    AFGResourceNodeFrackingCore* SpawnedCore = (FoundSpawnedCore && IsValid(*FoundSpawnedCore)) ? *FoundSpawnedCore : nullptr;

    ENodeShuffleWellPinSource Source = ENodeShuffleWellPinSource::NotEvaluated;
    AFGResourceNodeFrackingCore* Resolved = nullptr;

    if (SpawnedCore)
    {
        Source = ENodeShuffleWellPinSource::Spawned;
        Resolved = SpawnedCore;
        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            AFGResourceNodeFrackingSatellite* const* FoundSat = SpawnedSats.Find(S.SatellitePath);
            if (FoundSat && IsValid(*FoundSat)) { Candidates.Add(*FoundSat); }
        }
    }
    else
    {
        Source = E.bGroupPlaced ? ENodeShuffleWellPinSource::OriginalWhileRelocated
                                : ENodeShuffleWellPinSource::Original;
        Resolved = OriginalCore; // EvaluateWellPinOnActors applies the IsValid test, as this did
        for (AFGResourceNodeFrackingSatellite* Sat : OriginalSats)
        {
            if (IsValid(Sat)) { Candidates.Add(Sat); }
        }
    }

    return EvaluateWellPinOnActors(Source, Resolved, Candidates, E.Satellites.Num());
}
