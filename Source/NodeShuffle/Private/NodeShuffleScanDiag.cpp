// rwg-farspawn-1c C4 + C5 (2026-10-31): per-scan and per-radar-rescan diagnostics. Brief: _team/test-logs/2026-10-31-nodeshuffle-farspawn-1c-brief.md.
// C4 SCANPING: one line per AFGResourceScanner::GetClosetClusters call (the scanner's own pick of the clusters it marks; exported,
//   protected, friend-granted to ANodeShuffleSubsystem). Our GenerateNodeClusters hook (NodeShuffle.cpp) only fires when the cluster
//   cache was invalidated, so it cannot count scans. Whether GetClosetClusters runs once per scan press is closed-source behaviour:
//   one SCANPING line per scan in game is the measurement. Diagnostics-gated.
// C5 COMPASSWARN: a GLog output device counts the game's own LogGame warnings that START with the compass removal text (position-
//   anchored on the message start, the category and the verbosity, never a substring); RefreshScannersAndRadarTowers prints the
//   count since the previous COMPASSWARN beside its SCANREGEN line (diagnostics-gated there). Counts only; no cause is printed.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffle.h"
#include "EngineUtils.h"
#include "Patching/NativeHookManager.h"
#include "Equipment/FGResourceScanner.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceNodeBase.h"
#include "Buildables/FGBuildableRadarTower.h"
#include "GameFramework/Pawn.h"
#include "Misc/OutputDevice.h"
#include <atomic>

namespace
{
    std::atomic<int32> GNsCompassRemovedWarnings{0};
    int32 GNsCompassRemovedPrinted = 0; // game thread only (read + written by EmitCompassWarnCensus)

    class FNodeShuffleCompassWarnCounter final : public FOutputDevice
    {
    public:
        virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
        {
            static const FName LogGameName(TEXT("LogGame"));
            // The needle is split so this file's own text is never one contiguous copy of it.
            static const FString Prefix = FString(TEXT("[COMPASS]: Removed called")) + TEXT(" for actor: ");
            if (!V || (Verbosity & ELogVerbosity::VerbosityMask) != ELogVerbosity::Warning || Category != LogGameName) { return; }
            if (FCString::Strncmp(V, *Prefix, Prefix.Len()) == 0) { GNsCompassRemovedWarnings.fetch_add(1, std::memory_order_relaxed); }
        }
        virtual bool CanBeUsedOnAnyThread() const override { return true; }
        virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
    };

    double NsNearestM(const FVector& From, const FVector& At, double Best) { return FMath::Min(Best, FVector::Dist(From, At) / 100.0); }
    double NsOrNeg(double V) { return V == TNumericLimits<double>::Max() ? -1.0 : V; }
}

void ANodeShuffleSubsystem::InstallScanDiagHooks()
{
    // Never deleted: GLog keeps the pointer until process exit (a static instance could be destroyed while GLog still holds it).
    if (GLog) { GLog->AddOutputDevice(new FNodeShuffleCompassWarnCounter()); }
    SUBSCRIBE_UOBJECT_METHOD(AFGResourceScanner, GetClosetClusters,
        [](auto& Scope, AFGResourceScanner* Self, TArray<FNodeClusterData>& Closest)
    {
        Scope(Self, Closest); // the scanner's own pick first; this hook only reads what it returned
        if (!FNodeShuffleModule::AreDiagnosticsEnabled() || !IsValid(Self) || !Self->GetWorld()) { return; }
        for (TActorIterator<ANodeShuffleSubsystem> It(Self->GetWorld()); It; ++It) { It->EmitScanPing(Self, Closest); return; }
    });
}

void ANodeShuffleSubsystem::EmitScanPing(AFGResourceScanner* Scanner, const TArray<FNodeClusterData>& Closest)
{
    UClass* Res = Scanner->mResourceDescriptorToScanFor.Get();
    const APawn* P = Scanner->GetInstigator();
    const FVector PawnAt = P ? P->GetActorLocation() : Scanner->GetActorLocation();
    int32 ForRes = 0;
    for (const FNodeClusterData& C : Scanner->mNodeClusters) { if (C.ResourceDescriptor.Get() == Res) { ++ForRes; } }
    constexpr double None = TNumericLimits<double>::Max();
    double Marked = None, Live = None, Prov = None, NoActor = None;
    for (const FNodeClusterData& C : Closest) { for (const AFGResourceNodeBase* N : C.Nodes) { if (IsValid(N)) { Marked = NsNearestM(PawnAt, N->GetActorLocation(), Marked); } } }
    const FString ResPath = Res ? Res->GetPathName() : FString();
    TSet<const AActor*> ProvActors;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bIsNewNode || E.AssignedResourceClassPath != ResPath) { continue; }
        const AFGResourceNode* A = SpawnedNodes.FindRef(E.EntryGuid);
        if (IsValid(A)) { if (E.bRwgProvisional) { ProvActors.Add(A); Prov = NsNearestM(PawnAt, A->GetActorLocation(), Prov); } }
        else if (E.bActive) { NoActor = NsNearestM(PawnAt, E.Location, NoActor); }
    }
    // Live = any world node of this resource the scanner hook keeps (valid, not hidden, collision on), minus our provisional actors.
    for (TActorIterator<AFGResourceNodeBase> It(GetWorld()); It; ++It)
    {
        const AFGResourceNodeBase* N = *It;
        const UClass* NRes = IsValid(N) ? N->GetResourceClass() : nullptr;
        if (!IsValid(N) || NRes != Res || N->IsHidden() || !N->GetActorEnableCollision() || ProvActors.Contains(N)) { continue; }
        Live = NsNearestM(PawnAt, N->GetActorLocation(), Live);
    }
    // Legend (prose only): distances are 3-D metres from the scanning pawn; -1 means none exists in that set. marked counts the
    // clusters the scanner's pick returned; the nearest-live set is the world's scanner-eligible nodes of this resource other than
    // our provisional actors; the provisional set is our provisional actors; the no-actor set is our active new-node entries
    // with no live actor. pawn is the scanner's instigator location, or the scanner's own location when it has no instigator;
    // pawnFrom names which of the two was read (rwg-farspawn-1d F10).
    UE_LOG(LogNodeShuffle, Display, TEXT("SCANPING: resource=%s clustersForResource=%d clustersTotal=%d marked=%d nearestMarkedM=%.0f nearestLiveActorM=%.0f nearestProvisionalM=%.0f nearestNoActorM=%.0f pawn=%s pawnFrom=%s"),
        Res ? *Res->GetName() : TEXT("<none>"), ForRes, Scanner->mNodeClusters.Num(), Closest.Num(), NsOrNeg(Marked), NsOrNeg(Live), NsOrNeg(Prov),
        NsOrNeg(NoActor), *PawnAt.ToCompactString(), P ? TEXT("instigator") : TEXT("scanner"));
}

void ANodeShuffleSubsystem::EmitCompassWarnCensus(int32 TowersRescanned)
{
    const int32 Now = GNsCompassRemovedWarnings.load(std::memory_order_relaxed);
    const int32 Removed = Now - GNsCompassRemovedPrinted;
    GNsCompassRemovedPrinted = Now;
    int32 Stripped = -1; // -1 = not measured (no radar tower re-scanned in this call)
    if (TowersRescanned > 0)
    {
        TArray<TPair<FVector, float>> Towers;
        for (TActorIterator<AFGBuildableRadarTower> It(GetWorld()); It; ++It) { if (IsValid(*It)) { Towers.Emplace(It->GetActorLocation(), It->GetRevealRadius()); } }
        Stripped = 0;
        for (const FNodeShuffleSuppressedOriginal& Rec : OriginalNodeRecord)
        {
            const AFGResourceNodeBase* Orig = FindOriginalBaseByPath(Rec.VanillaNodePath);
            if (!Orig || !Orig->IsHidden()) { continue; }
            for (const TPair<FVector, float>& T : Towers) { if (FVector::Dist2D(T.Key, Orig->GetActorLocation()) <= T.Value) { ++Stripped; break; } }
        }
    }
    int32 Prov = 0;
    for (const FNodeShuffleEntry& E : Layout) { if (E.bRwgProvisional && IsValid(SpawnedNodes.FindRef(E.EntryGuid))) { ++Prov; } }
    // Legend (prose only): rescan is the radar towers re-scanned in this call; removedWarnings counts the game's compass removal
    // warnings logged since the previous COMPASSWARN line (they print before this line); hiddenOriginalsInTowerRadius counts recorded
    // originals that are hidden now and stand within a re-scanned tower's reveal radius (2-D), or -1 when no tower re-scanned;
    // provisional counts live provisional actors.
    UE_LOG(LogNodeShuffle, Display, TEXT("COMPASSWARN: rescan=%d removedWarnings=%d hiddenOriginalsInTowerRadius=%d provisional=%d"),
        TowersRescanned, Removed, Stripped, Prov);
}
