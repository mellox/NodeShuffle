// ns-rollfirst-1 (2026-11-14): "Every resource first." After a shuffle the nearest few entries of every resource load before anything
// else, then the existing nearest-first order carries on. Research: _team/test-logs/2026-11-14-nodeshuffle-postshuffle-scan.md (sections 5-6).
// The pick is by POSITION: for each resource, the K entries nearest to any player (the candidate build's own squared 2-D distance) among its
// active new-node entries. An entry with an actor holds a slot and is never a priority entry; an actorless entry the provisional path can no
// longer give a node (RwgRollFirstCanLoad false) is skipped, so the next nearest takes its slot. The actorless picks are the priority entries.
// RwgRollFirstReorder runs inside the candidate list build (RwgProvisionalCollectCandidates), so it runs at the same cadence (initial, roll,
// re-roll, reload, redeal, pin, layout size or (guid, index) change, every 12 passes) and adds no Layout walk anywhere else. The sea pass walks
// RwgRollFirstList first (NodeShuffleMapModeSea.cpp); the candidate list carries the same entries as its prefix. K = 0: nothing is reordered.
// Log lines print what this run measured (counts, distances, seconds), never a cause.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapModeProvisional.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"
#include "HAL/IConsoleManager.h"
#include "Equipment/FGResourceScanner.h"
#include "Resources/FGResourceNode.h"
#include "GameFramework/Pawn.h"

namespace
{
    TAutoConsoleVariable<int32> CVarNodeShuffleRwgFirstPerResource(
        TEXT("NodeShuffle.RwgFirstPerResource"),
        3,
        TEXT("RWG maps only. Each time the provisional candidate list is rebuilt (a roll, re-roll, reload, redeal or pin, or every 12 spawn ")
        TEXT("passes), the N new node spots of each resource nearest to any player are picked; a spot that already has a node takes one of the N, ")
        TEXT("and a spot set aside this session is passed over. The picked spots without a node are sea-checked first and tried first by the ")
        TEXT("provisional spawn pass, after only that pass's within-radius spots whose ground fit left no node; every resource's nearest is ")
        TEXT("tried before any resource's second. Default 3. 0: off, the list keeps its nearest-first order. Values are clamped to 0..16. ")
        TEXT("Read when the list is rebuilt."),
        ECVF_Default);
}

int32 NodeShuffleMapMode::RwgFirstPerResource()
{
    return FMath::Clamp(CVarNodeShuffleRwgFirstPerResource.GetValueOnGameThread(), 0, RwgFirstPerResourceMax);
}

bool ANodeShuffleSubsystem::RwgRollFirstCanLoad(const FNodeShuffleEntry& E) const
{
    // The candidate build's set-aside tests, plus the PassOk exclusions that never clear by themselves within a session.
    if (!E.bIsNewNode || !E.bActive || E.bPinned) { return false; }
    if (DormantThisSession.Num() > 0 && DormantThisSession.Contains(E.EntryGuid)) { return false; }
    if (SpawnParkedThisSession.Num() > 0 && SpawnParkedThisSession.Contains(E.EntryGuid)) { return false; }
    if (RwgProvRefused.Contains(E.EntryGuid)) { return false; }
    if (E.bRayCasted) { return false; }                          // settled spot: only the attempt loop near a player gives it a node
    if (RwgSeaDealFailed.Contains(E.EntryGuid)) { return false; } // the sea pass found no dry spot for it this session
    return !RwgProvisionalAtWetSpot(E);                         // water-locked and still at its lock spot
}

bool ANodeShuffleSubsystem::RwgRollFirstSeaDone(const FNodeShuffleEntry& E) const
{
    // The candidate scan's sea-done expression, term for term (NodeShuffleMapModeProvisionalScan.cpp PassOk); read for logs only.
    const bool bVoidMaybe = !(bRwgVoidCheckDone && bRwgKillboxDone) || RwgVoidPassTermsOn();
    const bool bSeaPassIdle = bRwgSeaCheckDone && !bRwgSeaUsable;
    const bool bSeaDone = bSeaPassIdle || (E.bRwgSeaChecked && (E.bRwgVoidChecked || !bVoidMaybe));
    return bSeaDone;
}

void ANodeShuffleSubsystem::RwgRollFirstNoteRoll() { RwgRollFirstRollTime = FPlatformTime::Seconds(); }

double ANodeShuffleSubsystem::RwgRollFirstSecondsSinceRoll() const
{
    return RwgRollFirstRollTime < 0.0 ? -1.0 : FPlatformTime::Seconds() - RwgRollFirstRollTime;
}

void ANodeShuffleSubsystem::RwgRollFirstReorder(TArray<TPair<double, int32>>& Sorted, const TArray<TPair<double, int32>>& Loaded, const TCHAR* Why)
{
    const double T0 = FPlatformTime::Seconds();
    const int32 K = NodeShuffleMapMode::RwgFirstPerResource();
    RwgRollFirstK = K;
    RwgRollFirstList.Reset();
    RwgRollFirstPicks.Reset();
    RwgRollFirstPickRes.Reset();
    RwgRollFirstResources = 0;
    RwgRollFirstShortOfK = 0;
    if (K <= 0) { return; } // off: Sorted keeps its nearest-first order, and the sea pass walks Layout by index alone
    // Per resource, the K nearest picks: insertion into an array of at most K (no sort). SortedPos is the entry's position in Sorted,
    // -1 for an entry that has an actor (it holds a slot but is never a priority entry).
    struct FRfSlot { double D; int32 I; int32 SortedPos; };
    TMap<FString, int32> ResOrd;
    TArray<TArray<FRfSlot>> Top;
    const auto Offer = [&](double D, int32 I, int32 SortedPos)
    {
        const FString& Path = Layout[I].AssignedResourceClassPath;
        const int32* Found = ResOrd.Find(Path);
        const int32 R = Found ? *Found : Top.Num();
        if (!Found) { ResOrd.Add(Path, R); Top.AddDefaulted(); }
        TArray<FRfSlot>& T = Top[R];
        if (T.Num() >= K && D >= T.Last().D) { return; }
        int32 At = T.Num();
        while (At > 0 && T[At - 1].D > D) { --At; }
        T.Insert(FRfSlot{ D, I, SortedPos }, At);
        if (T.Num() > K) { T.SetNum(K); }
    };
    int32 PassedOver = 0;
    for (int32 P = 0; P < Sorted.Num(); ++P)
    {
        if (!RwgRollFirstCanLoad(Layout[Sorted[P].Value])) { ++PassedOver; continue; } // set aside: the next nearest takes the slot
        Offer(Sorted[P].Key, Sorted[P].Value, P);
    }
    for (const TPair<double, int32>& L : Loaded) { Offer(L.Key, L.Value, -1); }
    // Rank of each priority entry within its resource (0 = nearest); everything else stays -1.
    TArray<int32> RankOfPos;
    RankOfPos.Init(-1, Sorted.Num());
    for (int32 R = 0; R < Top.Num(); ++R)
    {
        if (Top[R].Num() < K) { ++RwgRollFirstShortOfK; }
        for (int32 J = 0; J < Top[R].Num(); ++J)
        {
            const FRfSlot& S = Top[R][J];
            RwgRollFirstPicks.Emplace(Layout[S.I].EntryGuid, S.I);
            RwgRollFirstPickRes.Add(R);
            if (S.SortedPos >= 0) { RankOfPos[S.SortedPos] = J; }
        }
    }
    RwgRollFirstResources = Top.Num();
    // Reorder: one bucket per rank, each filled in Sorted's nearest-first order, so the priority prefix is ordered by (rank, distance) and
    // every resource's nearest comes before any resource's second; then the rest in Sorted's order, unchanged.
    TArray<TArray<int32>> Buckets;
    Buckets.SetNum(K);
    for (int32 P = 0; P < Sorted.Num(); ++P) { if (RankOfPos[P] >= 0) { Buckets[RankOfPos[P]].Add(P); } }
    TArray<TPair<double, int32>> Out;
    Out.Reserve(Sorted.Num());
    for (int32 J = 0; J < K; ++J) { for (const int32 P : Buckets[J]) { Out.Add(Sorted[P]); RwgRollFirstList.Emplace(Layout[Sorted[P].Value].EntryGuid, Sorted[P].Value); } }
    for (int32 P = 0; P < Sorted.Num(); ++P) { if (RankOfPos[P] < 0) { Out.Add(Sorted[P]); } }
    Sorted = MoveTemp(Out);
    const int32 Wanted = RwgRollFirstList.Num();
    if (Wanted > 0) { ++RwgRollFirstGeneration; bRwgRollFirstDoneLogged = false; } // a new generation: DONE re-armed
    if (Wanted > 0 || FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        // Legend (prose only): generation numbers the builds that picked at least one priority entry this session; build is the candidate
        // list's own build reason; perResource is K; resources counts the resources with at least one pickable entry; picks counts the
        // picked entries, wanted the picks without an actor; resourcesShortOfK counts resources with fewer than K pickable entries;
        // passedOver counts actorless entries the pick skipped as set aside; buildMs is this function's own time.
        UE_LOG(LogNodeShuffle, Display, TEXT("ROLLFIRST-BUILD: generation=%d build=%s perResource=%d resources=%d picks=%d wanted=%d resourcesShortOfK=%d passedOver=%d listEntries=%d buildMs=%.2f secondsSinceRoll=%.0f"),
            RwgRollFirstGeneration, Why ? Why : TEXT("none"), K, RwgRollFirstResources, RwgRollFirstPicks.Num(), Wanted, RwgRollFirstShortOfK,
            PassedOver, Sorted.Num(), (FPlatformTime::Seconds() - T0) * 1000.0, RwgRollFirstSecondsSinceRoll());
    }
}

void ANodeShuffleSubsystem::RwgRollFirstEmitPass(const TArray<int32>& Cands, int32 Attempted)
{
    const int32 SeaFirst = RwgRollFirstSeaFirstChecked;
    RwgRollFirstSeaFirstChecked = 0;
    if (RwgRollFirstK <= 0 || bRwgRollFirstDoneLogged) { return; }
    // This pass's candidates and attempts among the priority set (the attempt loop tried Cands[0 .. Attempted-1]).
    TSet<int32> Prio;
    for (const TPair<FGuid, int32>& G : RwgRollFirstList) { Prio.Add(G.Value); }
    int32 CandN = 0, SpawnedN = 0, FailedN = 0;
    for (int32 C = 0; C < Cands.Num(); ++C)
    {
        if (!Prio.Contains(Cands[C])) { continue; }
        ++CandN;
        if (C >= Attempted) { continue; }
        if (IsValid(SpawnedNodes.FindRef(Layout[Cands[C]].EntryGuid))) { ++SpawnedN; } else { ++FailedN; }
    }
    // State of the priority set now.
    int32 With = 0, Left = 0, SeaPend = 0, Aside = 0;
    for (const TPair<FGuid, int32>& G : RwgRollFirstList)
    {
        if (!Layout.IsValidIndex(G.Value) || Layout[G.Value].EntryGuid != G.Key) { ++Aside; continue; }
        const FNodeShuffleEntry& E = Layout[G.Value];
        if (IsValid(SpawnedNodes.FindRef(E.EntryGuid))) { ++With; continue; }
        if (!RwgRollFirstCanLoad(E)) { ++Aside; continue; }
        ++Left;
        if (!RwgRollFirstSeaDone(E)) { ++SeaPend; }
    }
    TArray<int32> PicksPerRes, LoadedPerRes;
    PicksPerRes.Init(0, RwgRollFirstResources);
    LoadedPerRes.Init(0, RwgRollFirstResources);
    for (int32 X = 0; X < RwgRollFirstPicks.Num(); ++X)
    {
        const int32 R = RwgRollFirstPickRes[X];
        if (!PicksPerRes.IsValidIndex(R)) { continue; }
        ++PicksPerRes[R];
        const int32 I = RwgRollFirstPicks[X].Value;
        if (Layout.IsValidIndex(I) && Layout[I].EntryGuid == RwgRollFirstPicks[X].Key && IsValid(SpawnedNodes.FindRef(Layout[I].EntryGuid))) { ++LoadedPerRes[R]; }
    }
    int32 AtK = 0;
    for (int32 R = 0; R < PicksPerRes.Num(); ++R) { AtK += (PicksPerRes[R] == RwgRollFirstK && LoadedPerRes[R] == RwgRollFirstK) ? 1 : 0; }
    // Legend (prose only): wanted counts this generation's priority entries; withActor those that have an actor now; seaPending those still
    // waiting for the sea pass by the candidate scan's own test; seaCheckedFirst the entries the sea pass checked from the priority walk on
    // its latest run; candidates the priority entries among this pass's candidates; attempted, spawnedThisPass and failedThisPass the
    // priority candidates the attempt loop reached, and of those the ones with and without an actor afterwards; setAside the priority
    // entries left without an actor that the pick would now pass over (or whose layout slot was replaced); resourcesAtK counts resources
    // whose K picks all have an actor. The PASS line prints while a priority entry is left to load; DONE prints once per generation.
    if (Left > 0)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("ROLLFIRST-PASS: pass=%d generation=%d wanted=%d withActor=%d seaPending=%d seaCheckedFirst=%d candidates=%d attempted=%d spawnedThisPass=%d failedThisPass=%d setAside=%d resources=%d resourcesAtK=%d secondsSinceRoll=%.0f"),
            RwgPcPasses, RwgRollFirstGeneration, RwgRollFirstList.Num(), With, SeaPend, SeaFirst, CandN, SpawnedN + FailedN, SpawnedN, FailedN, Aside,
            RwgRollFirstResources, AtK, RwgRollFirstSecondsSinceRoll());
        return;
    }
    bRwgRollFirstDoneLogged = true;
    UE_LOG(LogNodeShuffle, Display, TEXT("ROLLFIRST-DONE: pass=%d generation=%d secondsSinceRoll=%.0f wanted=%d withActor=%d resources=%d resourcesAtK=%d resourcesShortOfK=%d setAside=%d perResource=%d"),
        RwgPcPasses, RwgRollFirstGeneration, RwgRollFirstSecondsSinceRoll(), RwgRollFirstList.Num(), With, RwgRollFirstResources, AtK,
        RwgRollFirstShortOfK, Aside, RwgRollFirstK);
}

void ANodeShuffleSubsystem::EmitScanCover(AFGResourceScanner* Scanner)
{
    UClass* Res = Scanner->mResourceDescriptorToScanFor.Get();
    const APawn* P = Scanner->GetInstigator();
    const FVector PawnAt = P ? P->GetActorLocation() : Scanner->GetActorLocation();
    const FString ResPath = Res ? Res->GetPathName() : FString();
    int32 Dealt = 0, With = 0, Without = 0, NearestI = INDEX_NONE;
    double NearestM = TNumericLimits<double>::Max();
    for (int32 I = 0; I < Layout.Num(); ++I)
    {
        const FNodeShuffleEntry& E = Layout[I];
        if (!E.bIsNewNode || !E.bActive || E.AssignedResourceClassPath != ResPath) { continue; }
        ++Dealt;
        if (IsValid(SpawnedNodes.FindRef(E.EntryGuid))) { ++With; continue; }
        ++Without;
        const double M = FVector::Dist(PawnAt, E.Location) / 100.0;
        if (M < NearestM) { NearestM = M; NearestI = I; }
    }
    int32 SeaDone = -1, QueuePos = -1;
    if (NearestI != INDEX_NONE)
    {
        SeaDone = RwgRollFirstSeaDone(Layout[NearestI]) ? 1 : 0;
        for (int32 Q = 0; Q < RwgProvCandList.Num(); ++Q)
        {
            if (RwgProvCandList[Q].Value == NearestI && RwgProvCandList[Q].Key == Layout[NearestI].EntryGuid) { QueuePos = Q; break; }
        }
    }
    int32 ForRes = 0;
    for (const FNodeClusterData& C : Scanner->mNodeClusters) { if (C.ResourceDescriptor.Get() == Res) { ++ForRes; } }
    // Legend (prose only): counts are this machine's active new-node layout entries of the scanned resource (a client that holds no
    // layout prints zeros); the actor test is a valid spawned node for the entry. The nearest-without-actor fields describe the one such
    // entry nearest the scanning pawn (3-D metres; -1 when none): its sea-checked flag is the candidate scan's sea-done test (1/0), and its
    // queue position is its index in the provisional candidate list as last compacted (-1 when not on it; this pass's within-radius
    // fresh entries are tried ahead of the list). clustersForResource counts this scanner's cluster list entries for the resource.
    UE_LOG(LogNodeShuffle, Display, TEXT("SCANCOVER: resource=%s dealtActive=%d withActor=%d withoutActor=%d nearestWithoutActorM=%.0f nearestWithoutActorSeaChecked=%d nearestWithoutActorQueuePos=%d clustersForResource=%d secondsSinceRoll=%.0f"),
        Res ? *Res->GetName() : TEXT("<none>"), Dealt, With, Without, NearestI != INDEX_NONE ? NearestM : -1.0, SeaDone, QueuePos, ForRes,
        RwgRollFirstSecondsSinceRoll());
}
