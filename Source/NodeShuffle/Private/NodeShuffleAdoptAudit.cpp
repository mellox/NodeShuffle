// rwg-farspawn-1f C1 (2026-10-31): adopt-on-load accounting. Brief: _team/test-logs/2026-10-31-nodeshuffle-farspawn-1f-brief.md.
// Run 3 measured "veto: pre-registered 616 restored nodes" and "Adopt-on-load ... adopted 615": one restored node passed the
// pre-registration predicate (PreRegisterRestoredNodesForVeto, NodeShuffleSubsystem.cpp) and adopt-on-load did not bind it. No census
// could see it. This file records each pre-registered node with the entry its pre-registration matched, and after
// AdoptRestoredSpawnedNodes prints one ADOPTMISS line per pre-registered node adopt left unbound (UNGATED: a duplicate detector, bounded
// by the pre-registered count) and one ADOPTCENSUS line whose fields reconcile by construction.
// DIAGNOSTIC ONLY: nothing here binds, moves, hides or destroys a node. An unbound restored node may carry a player's extractor, and
// adopt binds whichever matching actor it iterates first without reading occupancy, so no despawn rule is safe until a run grades
// which predicate fires (the occupancy of both actors is printed for that decision).
// Reasons print the predicate adopt pass 2 would have tested, in adopt's own order (IsFrackingActor, our component, then the entry
// search); they never assert why two actors exist near one entry.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffle.h"
#include "NodeShuffleResourceNode.h"
#include "NodeShuffleNodeComponent.h"
#include "Resources/FGResourceNode.h"
#include "Misc/PackageName.h" // rwg-rerolldup-2: ADOPTCLASS prints the short class name

namespace
{
    constexpr float AdoptAuditMatchCm = 300.0f; // mirrors AdoptMatchRadiusCm (NodeShuffleSubsystem.cpp, file-local there)
    FString AdoptShort(const FString& P) { int32 Dot = INDEX_NONE; return P.FindLastChar(TEXT('.'), Dot) ? P.Mid(Dot + 1) : P; }
}

void ANodeShuffleSubsystem::AdoptAuditNotePreRegistered(AFGResourceNode* Node, const FGuid& EntryGuid)
{
    // Called beside each PreRegistered++ in PreRegisterRestoredNodesForVeto; keyed by node, so a second call for one node keeps one row.
    AdoptPreRegistered.Add(TWeakObjectPtr<AFGResourceNode>(Node), EntryGuid);
}

void ANodeShuffleSubsystem::AdoptAuditEmit(int32 AdoptLineAdopted)
{
    // Bound = every valid SpawnedNodes value after both adopt passes (the actor -> its entry).
    TMap<const AFGResourceNode*, FGuid> BoundEntry;
    for (const TPair<FGuid, AFGResourceNode*>& KV : SpawnedNodes) { if (IsValid(KV.Value)) { BoundEntry.Add(KV.Value, KV.Key); } }
    int32 AdoptedPre = 0, AlreadyBound = 0, Fracking = 0, Component = 0, Gone = 0, ResMismatch = 0, Distance = 0, EntryInactive = 0;
    TSet<const AFGResourceNode*> PreSet;
    const float MatchSq = FMath::Square(AdoptAuditMatchCm);
    for (const TPair<TWeakObjectPtr<AFGResourceNode>, FGuid>& Row : AdoptPreRegistered)
    {
        AFGResourceNode* Node = Row.Key.Get();
        if (!IsValid(Node)) { ++Gone; continue; } // pre-registered at BeginPlay, no valid actor at adopt time
        PreSet.Add(Node);
        if (BoundEntry.Contains(Node)) { ++AdoptedPre; continue; }
        const FVector At = Node->GetActorLocation();
        const FString Res = Node->GetResourceClass() ? Node->GetResourceClass()->GetPathName() : FString();
        const TCHAR* Reason = TEXT("other");
        const TCHAR* OtherWhy = TEXT("none");
        FGuid Matched = Row.Value;
        double DistCm = -1.0;
        const bool bLegacy = Node->IsA<ANodeShuffleResourceNode>();
        if (!bLegacy && IsFrackingActor(Node)) { OtherWhy = TEXT("frackingActor"); ++Fracking; }
        else if (!bLegacy && UNodeShuffleNodeComponent::Find(Node)) { OtherWhy = TEXT("hasOurComponent"); ++Component; }
        else
        {
            // Adopt pass 2's entry predicate WITHOUT its "entry already has a node" test: active new-node entry, 3-D distance below the
            // radius, resource agreement when both are known; nearest wins. Legacy nodes: pass 1 binds by SaveGame guid to any new-node
            // entry (active or not) whose slot holds no valid actor.
            int32 BestIdx = INDEX_NONE;
            double BestSq = MatchSq;
            for (int32 I = 0; I < Layout.Num(); ++I)
            {
                const FNodeShuffleEntry& E = Layout[I];
                if (!E.bIsNewNode) { continue; }
                if (bLegacy) { if (E.EntryGuid == static_cast<const ANodeShuffleResourceNode*>(Node)->EntryGuid) { BestIdx = I; break; } continue; }
                if (!E.bActive) { continue; }
                const double DSq = FVector::DistSquared(At, E.Location);
                if (DSq >= BestSq) { continue; }
                if (!Res.IsEmpty() && !E.AssignedResourceClassPath.IsEmpty() && Res != E.AssignedResourceClassPath) { continue; }
                BestSq = DSq; BestIdx = I;
            }
            if (BestIdx != INDEX_NONE)
            {
                // A matching entry exists and adopt did not bind this node to it: adopt skips an entry already in SpawnedNodes.
                Reason = TEXT("alreadyBound"); ++AlreadyBound;
                Matched = Layout[BestIdx].EntryGuid;
                DistCm = FVector::Dist(At, Layout[BestIdx].Location);
            }
            else
            {
                // No entry matches now: test the entry the BeginPlay pre-registration matched, in the predicate's order.
                const FNodeShuffleEntry* R = nullptr;
                for (const FNodeShuffleEntry& E : Layout) { if (E.EntryGuid == Row.Value) { R = &E; break; } }
                if (!R || !R->bIsNewNode || !R->bActive) { OtherWhy = TEXT("entryInactiveAtAdopt"); ++EntryInactive; }
                else
                {
                    DistCm = FVector::Dist(At, R->Location);
                    if (!Res.IsEmpty() && !R->AssignedResourceClassPath.IsEmpty() && Res != R->AssignedResourceClassPath) { Reason = TEXT("resourceMismatch"); ++ResMismatch; }
                    else { Reason = TEXT("distance"); ++Distance; }
                }
            }
        }
        // boundTo: the actor SpawnedNodes holds for the matched entry (none = no slot; invalidSlot = a slot whose actor is not valid).
        AFGResourceNode* const* Slot = SpawnedNodes.Find(Matched);
        const AFGResourceNode* BoundTo = Slot ? *Slot : nullptr;
        const bool bBoundValid = Slot && IsValid(BoundTo);
        const FString BoundName = !Slot ? FString(TEXT("none")) : bBoundValid ? BoundTo->GetName() : FString(TEXT("invalidSlot"));
        UE_LOG(LogNodeShuffle, Display, TEXT("ADOPTMISS: node=%s res=%s at=%s matchedEntry=%s reason=%s otherWhy=%s distCm=%.0f preRegEntry=%s boundTo=%s boundDistCm=%.0f thisOccupied=%d boundOccupied=%d legacy=%d"),
            *Node->GetName(), *AdoptShort(Res), *At.ToCompactString(), *Matched.ToString().Left(8), Reason, OtherWhy, DistCm,
            *Row.Value.ToString().Left(8), *BoundName, bBoundValid ? FVector::Dist(At, BoundTo->GetActorLocation()) : -1.0,
            IsNodeOccupiedAnyway(Node) ? 1 : 0, bBoundValid ? (IsNodeOccupiedAnyway(BoundTo) ? 1 : 0) : -1, bLegacy ? 1 : 0);
    }
    int32 BoundNotPre = 0;
    for (const TPair<const AFGResourceNode*, FGuid>& KV : BoundEntry) { if (!PreSet.Contains(KV.Key)) { ++BoundNotPre; } }
    int32 FlagUnbound = 0;
    for (const FNodeShuffleEntry& E : Layout) { if (E.bRwgProvisional && !IsValid(SpawnedNodes.FindRef(E.EntryGuid))) { ++FlagUnbound; } }
    const int32 PreRegistered = AdoptPreRegistered.Num();
    const int32 MatchedNotBound = AlreadyBound + Fracking + Component;
    const int32 Unmatched = Gone + ResMismatch + Distance + EntryInactive;
    // Legend (prose only): preRegistered counts the restored nodes the BeginPlay veto pre-registration matched to an entry; adopted counts
    // those that adopt-on-load bound to an entry. matchedNotBound counts those still valid and unbound for which an entry passes adopt's
    // match predicate now (alreadyBound) or which adopt skips before its entry search (frackingSkipped, componentSkipped). unmatched
    // counts the rest: no valid actor at adopt (goneAtAdopt), or no entry passes the predicate now, split by the pre-registered entry's
    // first failing test (entryInactiveAtAdopt, resourceMismatchAtAdopt, distanceAtAdopt). boundNotPreRegistered counts bound actors
    // the pre-registration did not record; adoptLineAdopted is the Adopt-on-load line's own count; provisionalFlagUnbound counts
    // provisional-flagged entries with no bound actor, which the next spawn pass clears (PROVCENSUS staleFlagCleared).
    // rwg-farspawn-1f review F4 (prose only): reconciles holds by construction (every pre-registered row raises exactly one counter);
    // lineReconciles is the independent identity, 1 when the Adopt-on-load line's count equals adopted plus boundNotPreRegistered, i.e.
    // every bind adopt counted is a distinct actor still valid here. It relies on adopt running before any spawn this session (ApplyLayout
    // adopts before its first EnsureNewNodeSpawned, RwgPipelineMayRun adopts before RwgSpawnPass, SpawnedNodes is not saved).
    UE_LOG(LogNodeShuffle, Display, TEXT("ADOPTCENSUS: preRegistered=%d adopted=%d matchedNotBound=%d unmatched=%d alreadyBound=%d frackingSkipped=%d componentSkipped=%d goneAtAdopt=%d entryInactiveAtAdopt=%d resourceMismatchAtAdopt=%d distanceAtAdopt=%d boundNotPreRegistered=%d adoptLineAdopted=%d provisionalFlagUnbound=%d reconciles=%d lineReconciles=%d"),
        PreRegistered, AdoptedPre, MatchedNotBound, Unmatched, AlreadyBound, Fracking, Component, Gone, EntryInactive, ResMismatch, Distance,
        BoundNotPre, AdoptLineAdopted, FlagUnbound, (PreRegistered == AdoptedPre + MatchedNotBound + Unmatched) ? 1 : 0,
        (AdoptLineAdopted == AdoptedPre + BoundNotPre) ? 1 : 0);
    // rwg-rerolldup-2 ADOPTCLASS: per resolved spawn class, how many active spawn entries exist and how many hold a valid actor after
    // the two adopt passes. One line per class, every load, ungated. Measured on the user's save before this packet: the lead_C
    // class had entries and no bound actor at all, which is the population the vanilla-deal restamp targets.
    {
        struct FClassRow { int32 Entries = 0; int32 Bound = 0; int32 Provisional = 0; };
        TMap<FString, FClassRow> ByClass;
        for (const FNodeShuffleEntry& E : Layout)
        {
            if (!E.bIsNewNode || !E.bActive) { continue; }
            FClassRow& Row = ByClass.FindOrAdd(ResolveSpawnNodeClassPath(E));
            ++Row.Entries;
            if (IsValid(SpawnedNodes.FindRef(E.EntryGuid))) { ++Row.Bound; }
            if (E.bRwgProvisional) { ++Row.Provisional; }
        }
        TArray<FString> Classes;
        ByClass.GetKeys(Classes);
        Classes.Sort();
        for (const FString& C : Classes)
        {
            const FClassRow& Row = ByClass.FindChecked(C);
            UE_LOG(LogNodeShuffle, Display, TEXT("ADOPTCLASS class=%s entries=%d bound=%d unbound=%d provisional=%d"),
                *FPackageName::ObjectPathToObjectName(C), Row.Entries, Row.Bound, Row.Entries - Row.Bound, Row.Provisional);
        }
    }
    RwgProvCandMarkDirty(TEXT("reload")); // rwg-farspawn-1f C3: the provisional candidate list is rebuilt after adopt-on-load
}
