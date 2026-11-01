// Copyright. NodeShuffle packet rwg-logflood-1 (2026-11-01).
// The per-entry Verbose lines of RebuildWellMeshIndex (NodeShuffleWellMeshIndex.cpp) -- the accepted-piece line and its REJECT
// sibling -- are BUFFERED here during route 3 and printed by FlushWellIndexSpatialDump (NodeShuffleWellMeshIndexDump.cpp) only
// when this rebuild's signature differs from the last one printed in this UWorld. One census line is printed on every rebuild.
// They print AFTER route 3, so they no longer interleave with NARROWED/WIDENED/BYSTANDER, and a crash mid-route-3 leaves none.
// Measured cause (_team/test-logs/2026-11-01-nodeshuffle-farspawn-play1e.md Q6): the accepted-piece line printed 423 lines on
// each of 2674 apply passes over an unchanged index, 579 MB of an 800 MB log.
// A stack local of RebuildWellMeshIndex; never saved, never outlives the rebuild. Holds no UObject pointer.
#pragma once

#include "CoreMinimal.h"

struct FNodeShuffleWellIndexSpatialEntry
{
    bool bAccepted = false;     // true = the accepted-piece line, false = the REJECT line
    int32 MeshType = 0;         // static_cast<int32>(ENodeMeshType) as printed
    FString MeshName;
    FString ActorName;
    FString MemberShort;        // WellShort(member path) as printed
    FVector Loc = FVector::ZeroVector; // printed only; NOT part of the signature (nor is the radius)
    float DistCm = 0.0f;
    float RunnerUpCm = -1.0f;   // accepted line only; -1 = no runner-up
    bool bInRadius = false;     // REJECT line only
};

struct FNodeShuffleWellIndexSpatialDump
{
    bool bActive = false;       // diagnostics on AND LogNodeShuffle Verbose not suppressed, decided once per rebuild
    bool bWorldReset = false;   // ResetWellIndexDumpForWorld cleared the stored signature on this rebuild
    int32 Pass = -1;            // WellAuditPasses at the rebuild
    int32 Accepted = 0;
    int32 Rejected = 0;
    int32 NarrowedByType = 0;   // copies of RebuildWellMeshIndex's own counters, printed on the census
    int32 WidenedByType = 0;
    int32 BystanderRejects = 0;
    float RadiusCm = 0.0f;
    uint64 SigSum = 0;          // wrapping sum of per-entry hashes: order-independent (TActorIterator order is not proven stable)
    TArray<FNodeShuffleWellIndexSpatialEntry> Entries;

    void Add(bool bInAccepted, int32 InMeshType, const FString& InMeshName, const FString& InActorName, const FString& InMemberShort,
        const FVector& InLoc, float InDistCm, float InRunnerUpCm, bool bInInRadius);
    uint64 Signature() const;   // SigSum folded with the entry count
};
