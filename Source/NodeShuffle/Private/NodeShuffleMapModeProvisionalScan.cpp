// rwg-farspawn-1f C3 (2026-10-31): the per-session provisional candidate list. Brief: _team/test-logs/2026-10-31-nodeshuffle-farspawn-1f-brief.md.
// Before 1f, RwgProvisionalSpawnPass (NodeShuffleMapModeProvisional.cpp) walked the whole Layout and sorted every candidate on every pass,
// on the budget clock. Now the walk + sort run only when the list is (re)built: first pass of the session, a layout change marked by
// RwgProvCandMarkDirty (roll, re-roll, reload, redeal, pin), a Layout size change or a (guid, index) pair that no longer matches
// (a structural check that catches a replaced Layout without a mark), or every RwgProvisionalCandRebuildPasses (12) passes.
// Between builds each pass walks the list in its built order (nearest-first to any player AT BUILD TIME), drops entries that fail the
// set-aside tests (they re-enter only at a build), and applies the SAME per-entry tests as the 1e scan, in the same order, with the same
// counters. Population difference vs a full scan, stated: an entry that becomes actorless or eligible after a build without a dirty mark
// (an external destroy, a stale flag cleared) waits for the next build (at most 12 passes); far entries keep the build-time order.
// Within-radius entries whose fresh attempt this pass left no actor (FreshNoActor) are tested first every pass, nearest first, whether or
// not they are on the list, so 1e C1 does not wait for a build.
#include "NodeShuffleMapModeProvisional.h"
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"

void ANodeShuffleSubsystem::RwgProvCandMarkDirty(const TCHAR* Why)
{
    if (!RwgProvCandDirtyWhy) { RwgProvCandDirtyWhy = Why; } // the first reason since the last build is the one printed
}

void ANodeShuffleSubsystem::RwgProvisionalCollectCandidates(const TArray<FVector>& Pawns, float AttemptRadiusCm, const TSet<int32>& FreshNoActor,
    TArray<int32>& OutCands, TSet<int32>& OutWithin, int32& SeaPending, int32& RayCastedNoActor, int32& SeaDealFailed, int32& WetSpot, int32& WithinUnderground,
    const TCHAR*& OutBuild)
{
    OutBuild = TEXT("none");
    // The set-aside tests (the 1e scan's silent skips): new, active, unpinned, not dormant / parked / refused, and no valid actor.
    const auto SetAsideOk = [this](const FNodeShuffleEntry& E)
    {
        if (!E.bIsNewNode || !E.bActive || E.bPinned) { return false; }
        if (DormantThisSession.Num() > 0 && DormantThisSession.Contains(E.EntryGuid)) { return false; }
        if (SpawnParkedThisSession.Num() > 0 && SpawnParkedThisSession.Contains(E.EntryGuid)) { return false; }
        if (RwgProvRefused.Contains(E.EntryGuid)) { return false; }
        return !IsValid(SpawnedNodes.FindRef(E.EntryGuid));
    };
    const auto NearestSq = [&Pawns](const FVector& L)
    {
        double Best = TNumericLimits<double>::Max();
        for (const FVector& P : Pawns) { Best = FMath::Min(Best, FVector::DistSquared2D(P, L)); }
        return Best;
    };
    // The sea/void re-deal pass (RwgSeaRedealPass) flags a placed entry without checking it, so an entry it has not checked yet is
    // left to it first: same per-entry done test it uses, plus its own idle state (sea grade unusable -> it never runs).
    const bool bVoidMaybe = !(bRwgVoidCheckDone && bRwgKillboxDone) || RwgVoidPassTermsOn();
    const bool bSeaPassIdle = bRwgSeaCheckDone && !bRwgSeaUsable;
    // The per-pass tests after the set-aside ones (unchanged from the 1e scan, same order, same counters). true = candidate.
    const auto PassOk = [&](int32 I)
    {
        const FNodeShuffleEntry& E = Layout[I];
        // rwg-farspawn-1e C1: within the attempt radius, only an entry whose FRESH attempt this pass (the attempt loop, which ran first) left no actor; a within entry that loop did not reach this pass (budget) is left to it. An attempt that redealt the entry to a new spot inside the radius also lands here, so that entry is provisioned at a spot no fresh attempt has tried.
        const bool bWithin = IsLocationNearAnyPlayer(E.Location, AttemptRadiusCm);
        if (bWithin && !FreshNoActor.Contains(I)) { return false; } // within: the attempt loop spawns it (settled)
        if (bWithin && E.bUnderground) { ++WithinUnderground; return false; } // legacy flag only on RWG (rolls and redeals clear bUnderground); RWG caves (bRwgCave) are provisioned at their dealt floor Z like far caves, and their 8-miss escape (RwgResolveCaveAtSpawn) runs on the settle path too
        if (E.bRayCasted) { ++RayCastedNoActor; return false; } // fix F7 (V7): a settled entry keeps its settled spot; it spawns through the attempt loop
        if (RwgSeaDealFailed.Contains(E.EntryGuid)) { ++SeaPending; ++SeaDealFailed; return false; } // fix F7 (V7b): flagged by the sea pass, not re-dealt: no provisional node
        const bool bSeaDone = bSeaPassIdle || (E.bRwgSeaChecked && (E.bRwgVoidChecked || !bVoidMaybe));
        if (!bSeaDone) { ++SeaPending; return false; }
        if (RwgProvisionalAtWetSpot(E)) { ++WetSpot; return false; } // rwg-farspawn-1e C1, both origins: never at a spot a ground fit found unplaceable
        if (bWithin) { OutWithin.Add(I); }
        return true;
    };

    // Build decision. The (guid, index) check is structural: a roll or re-roll replaces Layout, so a pair that no longer matches forces a build.
    const TCHAR* Why = (RwgProvCandBuiltPass < 0) ? TEXT("initial")
        : RwgProvCandDirtyWhy ? RwgProvCandDirtyWhy
        : (RwgProvCandBuiltLayoutNum != Layout.Num()) ? TEXT("layoutNum")
        : (RwgPcPasses - RwgProvCandBuiltPass >= NodeShuffleMapMode::RwgProvisionalCandRebuildPasses) ? TEXT("periodic") : nullptr;
    if (!Why)
    {
        for (const TPair<FGuid, int32>& C : RwgProvCandList)
        {
            if (!Layout.IsValidIndex(C.Value) || Layout[C.Value].EntryGuid != C.Key) { Why = TEXT("guidMismatch"); break; }
        }
    }
    if (Why)
    {
        TArray<TPair<double, int32>> Sorted;
        for (int32 I = 0; I < Layout.Num(); ++I) { if (SetAsideOk(Layout[I])) { Sorted.Emplace(NearestSq(Layout[I].Location), I); } }
        Sorted.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; }); // nearest to any player first
        RwgProvCandList.Reset(Sorted.Num());
        for (const TPair<double, int32>& S : Sorted) { RwgProvCandList.Emplace(Layout[S.Value].EntryGuid, S.Value); }
        RwgProvCandBuiltPass = RwgPcPasses;
        RwgProvCandBuiltLayoutNum = Layout.Num();
        RwgProvCandDirtyWhy = nullptr;
        ++RwgProvCandBuilds;
        OutBuild = Why;
    }

    // Phase 1: this pass's fresh attempts that left no actor (at most the attempt budget), nearest first, on the list or not.
    TArray<TPair<double, int32>> Fresh;
    for (const int32 I : FreshNoActor) { if (Layout.IsValidIndex(I) && SetAsideOk(Layout[I])) { Fresh.Emplace(NearestSq(Layout[I].Location), I); } }
    Fresh.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
    TSet<int32> Tested;
    for (const TPair<double, int32>& F : Fresh) { Tested.Add(F.Value); if (PassOk(F.Value)) { OutCands.Add(F.Value); } }
    // Phase 2: the list in built order. An entry failing the set-aside tests is dropped from the list (compaction, kept order).
    int32 W = 0;
    for (int32 R = 0; R < RwgProvCandList.Num(); ++R)
    {
        const int32 I = RwgProvCandList[R].Value;
        if (!SetAsideOk(Layout[I])) { continue; }
        RwgProvCandList[W++] = RwgProvCandList[R];
        if (Tested.Contains(I)) { continue; } // already tested in phase 1
        if (PassOk(I)) { OutCands.Add(I); }
    }
    RwgProvCandList.SetNum(W);
}
