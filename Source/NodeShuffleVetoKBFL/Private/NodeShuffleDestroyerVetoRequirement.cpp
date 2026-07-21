#include "NodeShuffleDestroyerVetoRequirement.h"

#include "NodeShuffle.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectKey.h"

// Session veto counters (file-static; reset by the arm pass at each world init). Game-thread only,
// like every KBFL requirement evaluation (actor spawn/destroy delegates + next-tick timers).
static int32 GNodeShuffleVetoCount = 0;
static int32 GNodeShuffleVetoNextSummaryAt = 4;   // powers-of-4-ish summary thresholds: 4, 16, 64, ...
static double GNodeShuffleVetoLastSummaryTime = 0.0;
static TSet<FObjectKey> GNodeShuffleVetoDistinctNodes;

void UNodeShuffleDestroyerVetoRequirement::ResetSessionCounters()
{
    GNodeShuffleVetoCount = 0;
    GNodeShuffleVetoNextSummaryAt = 4;
    GNodeShuffleVetoLastSummaryTime = 0.0;
    GNodeShuffleVetoDistinctNodes.Empty();
}

bool UNodeShuffleDestroyerVetoRequirement::IsRequirementMet_Implementation(
    UKBFLContentCDOHelperSubsystem* /*Subsystem*/, UKBFLCDOOverwriteBase* From, UObject* Target)
{
    AActor* TargetActor = Cast<AActor>(Target);
    const bool bManaged = TargetActor && FNodeShuffleModule::IsManagedSpawnedNode(TargetActor);

    // Per-decision trace (diagnostics-gated, Verbose): names the actor/class and which way we ruled.
    // Ungated this would firehose — the initial KBFL sweep runs every matching actor in the world.
    if (FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        UE_LOG(LogNodeShuffle, Verbose, TEXT("veto: IsRequirementMet target='%s' class='%s' -> %s"),
            *GetNameSafe(Target), Target ? *Target->GetClass()->GetName() : TEXT("<null>"),
            bManaged ? TEXT("VETO (managed node)") : TEXT("allow (not ours)"));
    }

    if (!bManaged)
    {
        return true; // not one of ours — the owning asset behaves exactly as if NodeShuffle were absent
    }

    // VETO. Note KBFL corroborates independently in its own categories: the listener logs
    // "OnActorEvent: Requirements not met for actor X" (LogKBFLActorListener, Log) and the destroyer
    // "HandleDestroyActor: Requirements not met for actor X" (LogKBFLActorDestroyer, Verbose).
    GNodeShuffleVetoCount++;
    GNodeShuffleVetoDistinctNodes.Add(FObjectKey(TargetActor));
    const double Now = FPlatformTime::Seconds();

    // 'From' is declaration-only here (deliberate one-class stub surface). UKBFLCDOOverwriteBase sits
    // on a single-inheritance UObject chain (UPrimaryDataAsset -> UDataAsset -> UObject), so the
    // address is the UObject subobject — cast is for LOGGING only, never dereferenced as the derived.
    const UObject* FromAsObject = reinterpret_cast<const UObject*>(From);

    if (GNodeShuffleVetoCount == 1)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("veto: first destroy vetoed for %s by %s"),
            *TargetActor->GetName(), *GetPathNameSafe(FromAsObject));
        GNodeShuffleVetoLastSummaryTime = Now;
    }
    else if (GNodeShuffleVetoCount >= GNodeShuffleVetoNextSummaryAt
             && (Now - GNodeShuffleVetoLastSummaryTime) >= 60.0)
    {
        // Threshold + 60 s rate limit: informative under a persistent destroyer, never a firehose.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("veto: spared %d destroy attempts on %d distinct nodes so far this session"),
            GNodeShuffleVetoCount, GNodeShuffleVetoDistinctNodes.Num());
        while (GNodeShuffleVetoNextSummaryAt <= GNodeShuffleVetoCount)
        {
            GNodeShuffleVetoNextSummaryAt *= 4;
        }
        GNodeShuffleVetoLastSummaryTime = Now;
    }

    return false; // short-circuits the requirement chain: the asset skips this actor entirely
}
