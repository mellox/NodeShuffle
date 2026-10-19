#pragma once

// rwg-repclass-1 (2026-10-19): per-resource node-class RANKING for the roll's NodeClassByResource latch,
// the registry fallback that finds a modded resource's own node class when no live instance of it was
// seen, and the own-class predicate the spawn/adopt visual branches use to keep a mod class's native look.
// Spec: _team/test-logs/2026-10-19-nodeshuffle-modclass-lookup-feasibility.md (Recommended design A-C).
//
// Predicates (all string-only, PURE, no UE_LOG -- callers own their log lines):
//   IsGenericNodeClassPath(C) := C starts with "/Game/" OR C starts with "/Script/FactoryGame."
//   NodeShuffleMountRoot(P)   := "/Script/<Module>" for a /Script/ path, else "/<first segment>/" (lower-cased)
//   RepClassRank(C, R)        := 0 when R itself is generic (a base-game resource: ranking is a no-op, first-seen kept)
//                                 2 when !generic(C) && MountRoot(C) == MountRoot(R)          ("own-mount")
//                                 1 when !generic(C)                                          ("non-generic")
//                                 0 otherwise                                                 ("generic")
//   RepClassShouldReplace(Existing, New, R) := Rank(New, R) > Rank(Existing, R)  (ties keep the first seen)
//   IsOwnNodeClass(C, R)      := RepClassRank(C, R) == 2

#include "CoreMinimal.h"

class UClass;

namespace NodeShuffleRepClass
{
    // Upper bound on candidate classes loaded per resource by the registry fallback (sync LoadObject on the
    // game thread). Printed as cap= in every REPCLASS-REGISTRY line.
    constexpr int32 RegistryLoadCap = 4;

    struct FStat
    {
        FString FirstSeen;           // first class latched for this resource this roll
        TSet<FString> Seen;          // every distinct class observed for this resource this roll
        bool bReplacedFirstSeen = false;
        bool bFromRegistry = false;
    };

    struct FRegistryQuery
    {
        TArray<FString> CandidatePaths; // non-/Game/, non-/Script/ derived class paths (Blueprint generated classes)
        int32 Derived = 0;              // everything GetDerivedClassNames returned
        int32 BaseGameBp = 0;           // positive control: count of /Game/ derived classes (BP_ResourceNode_C lives there)
        double Ms = 0.0;
    };

    struct FRegistryMatch
    {
        int32 MountFiltered = 0;
        int32 Loaded = 0;
        int32 Failed = 0;
        int32 Rejected = 0; // loaded but abstract / not a node / refused / CDO resource differs
        FString Matched;
        double Ms = 0.0;
    };

    bool IsGenericNodeClassPath(const FString& ClassPath);
    FString NodeShuffleMountRoot(const FString& Path);
    int32 RepClassRank(const FString& ClassPath, const FString& ResourcePath);
    bool RepClassShouldReplace(const FString& ExistingClassPath, const FString& NewClassPath, const FString& ResourcePath);
    bool IsOwnNodeClass(const FString& ClassPath, const FString& ResourcePath);
    const TCHAR* RepClassRankName(int32 Rank);

    // One GetDerivedClassNames({/Script/FactoryGame.FGResourceNode}) call. Never loads anything.
    void QueryDerivedNodeClasses(FRegistryQuery& Out);

    // Filters Query.CandidatePaths by MountRoot(ResourcePath), loads at most RegistryLoadCap of them (each class at
    // most once per roll via LoadCache), and returns the first whose CDO GetResourceClass() path == ResourcePath.
    // Any load failure or mismatch is skipped (fail closed): Out.Matched stays empty.
    void MatchByRegistry(const FString& ResourcePath, const FRegistryQuery& Query, const TMap<FString, FString>& RefusedClasses,
        TMap<FString, UClass*>& LoadCache, FRegistryMatch& Out);
}
