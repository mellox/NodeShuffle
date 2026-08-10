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

// ---- T58 (ns-t58-foreign-protect, 2026-08-10) session state. Same game-thread-only contract. ----
// The protection policy for THIS world session, latched by the arm pass (see the header).
static bool GNodeShuffleProtectForeignLatched = false;
static int32 GNodeShuffleVetoArmedAssets = 0;
// Evaluation partition. Every IsRequirementMet call lands in EXACTLY ONE of the four buckets:
//   managed (GNodeShuffleVetoCount) + foreign + vanilla + non-node == GNodeShuffleVetoEvalCount.
// F10: in THIS build partitionSum cannot disagree with evals — there is no early return between the
// eval increment and the bucket increment, so a match proves nothing about today's code. It is printed
// so that a FUTURE edit which adds an early return shows up in the log as a mismatch.
static int32 GNodeShuffleVetoEvalCount = 0;
static int32 GNodeShuffleForeignSeen = 0;       // = protected + allowed, by construction
static int32 GNodeShuffleForeignProtected = 0;  // vetoed because protection was latched ON
// Allowed through for ANY reason -- the latch being OFF, or (F1/R2) the evaluating asset not being in
// the broad-node-sweep set. The sub-counter below isolates the second case, because that is the
// evidence a breadth mistake leaves behind. It is NOT part of the partition (it is a subset of
// GNodeShuffleForeignAllowed) and must never be added into partitionSum.
static int32 GNodeShuffleForeignAllowed = 0;
static int32 GNodeShuffleForeignAllowedAssetNotInBroadSet = 0;
static int32 GNodeShuffleVanillaSeen = 0;       // vanilla originals: ALWAYS allowed, never protected
static int32 GNodeShuffleNonNodeSeen = 0;       // target was null or not AFGResourceNodeBase-derived

// T58 F1: the assets on which foreign protection may fire at all — a BROAD node sweep only (see the
// header). Populated by the arm pass, consulted per evaluation. Empty ⇒ no foreign protection anywhere,
// which is the correct fail-CLOSED direction for an unrecognised asset.
static TSet<FObjectKey> GNodeShuffleForeignProtectAssets;

// Per-DISTINCT-foreign-class tally. Keyed by actor class NAME (what the destroy log line names). The
// first PROTECTED veto of each class logs one line; every later instance only increments — 29 kills in
// one load must not become 29 log lines.
struct FNodeShuffleForeignClassTally
{
    int32 Seen = 0;
    int32 ProtectedCount = 0;
    int32 ResourceNodeType = -1;
    FString ResourceClassPath = TEXT("<null>");
    // T58 F4: WHICH armed asset was evaluating when this class was last seen. Without it the census
    // cannot tell "the node remover vetoed 29" from "some other listener vetoed its own nodes" — the
    // exact discrimination F1 turned out to need. Last-writer-wins: one class judged by two assets
    // shows the later one, and the per-class log line names the asset at first sighting.
    FString LastFromAssetPath = TEXT("<none>");
    bool bFirstProtectLogged = false;
};
static TMap<FString, FNodeShuffleForeignClassTally> GNodeShuffleForeignByClass;
// Census de-dup. T58 F3: keyed on foreignSeen, NOT on total evaluations — evals moves for every
// ordinary actor event the armed assets match, so keying on it would print a second census line that a
// reader would misread as a later wave of foreign-node activity.
static int32 GNodeShuffleLastCensusForeignSeen = -1;

void UNodeShuffleDestroyerVetoRequirement::ResetSessionCounters(bool bProtectForeignNodes)
{
    GNodeShuffleVetoCount = 0;
    GNodeShuffleVetoNextSummaryAt = 4;
    GNodeShuffleVetoLastSummaryTime = 0.0;
    GNodeShuffleVetoDistinctNodes.Empty();

    GNodeShuffleProtectForeignLatched = bProtectForeignNodes;
    GNodeShuffleVetoArmedAssets = 0;
    GNodeShuffleVetoEvalCount = 0;
    GNodeShuffleForeignSeen = 0;
    GNodeShuffleForeignProtected = 0;
    GNodeShuffleForeignAllowed = 0;
    GNodeShuffleForeignAllowedAssetNotInBroadSet = 0;
    GNodeShuffleVanillaSeen = 0;
    GNodeShuffleNonNodeSeen = 0;
    GNodeShuffleForeignByClass.Empty();
    GNodeShuffleLastCensusForeignSeen = -1;
}

void UNodeShuffleDestroyerVetoRequirement::ResetForeignProtectAssets()
{
    GNodeShuffleForeignProtectAssets.Empty();
}

void UNodeShuffleDestroyerVetoRequirement::AddForeignProtectAsset(const UObject* Asset)
{
    if (Asset) { GNodeShuffleForeignProtectAssets.Add(FObjectKey(Asset)); }
}

void UNodeShuffleDestroyerVetoRequirement::SetArmedAssetCount(int32 ArmedAssets)
{
    GNodeShuffleVetoArmedAssets = ArmedAssets;
}

void UNodeShuffleDestroyerVetoRequirement::EmitForeignCensus(const TCHAR* Phase)
{
    // Speak once, and on a later phase only when FOREIGN-NODE evaluations moved (F3). A second line
    // therefore means exactly that — foreignSeen moved after the earlier phase. It does NOT by itself
    // identify a retry loop, and nothing here tests for one.
    if (GNodeShuffleLastCensusForeignSeen == GNodeShuffleForeignSeen && GNodeShuffleLastCensusForeignSeen >= 0)
    {
        return;
    }
    GNodeShuffleLastCensusForeignSeen = GNodeShuffleForeignSeen;

    // Per-class breakdown, capped so one pathological world cannot produce a 300-class line. No '='
    // anywhere inside the bracket: every 'name=value' token on this line must be countable exactly once.
    FString ClassList;
    int32 Listed = 0;
    for (const TPair<FString, FNodeShuffleForeignClassTally>& Pair : GNodeShuffleForeignByClass)
    {
        if (Listed >= 8)
        {
            ClassList += FString::Printf(TEXT(", (+%d more class(es) not listed)"),
                GNodeShuffleForeignByClass.Num() - Listed);
            break;
        }
        ClassList += (Listed == 0 ? TEXT("") : TEXT(", "));
        ClassList += FString::Printf(TEXT("%s seen %d protected %d type %d res %s by %s"),
            *Pair.Key, Pair.Value.Seen, Pair.Value.ProtectedCount, Pair.Value.ResourceNodeType,
            *Pair.Value.ResourceClassPath, *Pair.Value.LastFromAssetPath);
        ++Listed;
    }
    if (GNodeShuffleForeignByClass.Num() == 0) { ClassList = TEXT("none"); }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("VETOCENSUS %s: protectCvar=%d armedAssets=%d evals=%d managedVetoed=%d foreignSeen=%d ")
        TEXT("foreignProtected=%d foreignAllowed=%d foreignAllowedAssetNotInBroadSet=%d vanillaAllowed=%d ")
        TEXT("nonNodeAllowed=%d partitionSum=%d ")
        TEXT("distinctForeignClasses=%d [%s]"),
        Phase,
        GNodeShuffleProtectForeignLatched ? 1 : 0,
        GNodeShuffleVetoArmedAssets,
        GNodeShuffleVetoEvalCount,
        GNodeShuffleVetoCount,
        GNodeShuffleForeignSeen,
        GNodeShuffleForeignProtected,
        GNodeShuffleForeignAllowed,
        GNodeShuffleForeignAllowedAssetNotInBroadSet,
        GNodeShuffleVanillaSeen,
        GNodeShuffleNonNodeSeen,
        GNodeShuffleVetoCount + GNodeShuffleForeignSeen + GNodeShuffleVanillaSeen + GNodeShuffleNonNodeSeen,
        GNodeShuffleForeignByClass.Num(),
        *ClassList);
}

bool UNodeShuffleDestroyerVetoRequirement::IsRequirementMet_Implementation(
    UKBFLContentCDOHelperSubsystem* /*Subsystem*/, UKBFLCDOOverwriteBase* From, UObject* Target)
{
    AActor* TargetActor = Cast<AActor>(Target);
    const bool bRegistered = TargetActor && FNodeShuffleModule::IsManagedSpawnedNode(TargetActor);
    // spawnrace-1: registry alone loses the newborn race — KBFL's OnActorSpawned listener judges an
    // actor INSIDE UWorld::SpawnActor, before NodeShuffle's post-spawn RegisterManagedNode runs. If
    // we are synchronously inside one of our own node spawns right now, the actor being judged IS
    // that newborn (game thread, one spawn at a time) — honor the spawn window as managed. The
    // registry remains the durable signal for every later judgment.
    const bool bSpawnWindow = !bRegistered && TargetActor && FNodeShuffleModule::IsSpawningManagedNode();
    const bool bManaged = bRegistered || bSpawnWindow;

    GNodeShuffleVetoEvalCount++;

    // ---- T58 (ns-t58-foreign-protect): the THIRD outcome. Only reached for a target that is NOT ours,
    // so nothing about the managed-node path above changed. See NodeShuffle.h's declaration comment for
    // why the founding as-if-absent contract is deliberately amended here (docs/TECH-DEBT.md T58).
    // The classification runs even when protection is latched OFF: it costs one Cast plus two path
    // compares, and it is what makes the OFF state's census an honest measurement rather than a blank.
    //
    // TWO KNOWN POPULATION ESCAPES, documented rather than fixed (F7, and T58 in docs/TECH-DEBT.md):
    //   (a) UNDER-COVERAGE — a mod that patches vanilla assets in place (KDataForge) or ships its node
    //       BPs under /Game/ grades VanillaOriginal and is never protected. Silent by construction.
    //   (b) OVER-REACH — if an overhaul re-points a node's GetResourceClass() to its own descriptor,
    //       that node grades Foreign and we would veto the owning mod's removal of its own node. The F1
    //       breadth set does NOT cure this; only reading the per-class resource paths in the census can.
    bool bProtectForeign = false;
    // F8: the classifier leaves these untouched for a non-node target, so the defaults are the caller's.
    FString ForeignClassName = TEXT("<null>");
    FString ForeignResourcePath = TEXT("<null>");
    FString FromAssetPath = TEXT("<none>");
    int32 ForeignNodeType = -1;
    if (!bManaged)
    {
        const ENodeShuffleNodeOrigin Origin = FNodeShuffleModule::ClassifyResourceNodeOrigin(
            TargetActor, &ForeignClassName, &ForeignResourcePath, &ForeignNodeType);
        switch (Origin)
        {
        case ENodeShuffleNodeOrigin::Foreign:
            GNodeShuffleForeignSeen++;
            // F1: protection is evaluable ONLY on an asset the arm pass classified as a BROAD node
            // sweep. An asset targeting a strict subclass of FGResourceNodeBase (measured:
            // RefinedPower's ActorListner_RPTurbine on RPWaterTurbineNode) is that mod's own handler
            // for its own nodes; short-circuiting its chain disables it with no line naming a problem.
            // The 'From -> UObject' reinterpret_cast is the same LOGGING-ONLY conversion justified
            // further down this file — the address is the UObject subobject and is used for identity
            // and path only, never dereferenced as the derived type.
            {
                const UObject* FromAsObj = reinterpret_cast<const UObject*>(From);
                FromAssetPath = GetPathNameSafe(FromAsObj);
                bProtectForeign = GNodeShuffleProtectForeignLatched
                    && FromAsObj && GNodeShuffleForeignProtectAssets.Contains(FObjectKey(FromAsObj));
            }
            if (bProtectForeign)
            {
                GNodeShuffleForeignProtected++;
            }
            else
            {
                GNodeShuffleForeignAllowed++;
                // R4: the latch is ON but the evaluating asset is not in the broad-node-sweep set --
                // which INCLUDES a null 'From' (no asset is identifiable, so it cannot be in the set).
                // The name states exactly that test and nothing about why the asset is absent.
                if (GNodeShuffleProtectForeignLatched) { GNodeShuffleForeignAllowedAssetNotInBroadSet++; }
            }
            {
                FNodeShuffleForeignClassTally& Tally = GNodeShuffleForeignByClass.FindOrAdd(ForeignClassName);
                Tally.Seen++;
                Tally.ResourceNodeType = ForeignNodeType;
                Tally.ResourceClassPath = ForeignResourcePath;
                Tally.LastFromAssetPath = FromAssetPath; // F4
                if (bProtectForeign)
                {
                    Tally.ProtectedCount++;
                    if (!Tally.bFirstProtectLogged)
                    {
                        Tally.bFirstProtectLogged = true;
                        // NOT diagnostics-gated, and Display not Verbose, ON PURPOSE: the evaluations
                        // this exists to witness land ~0.9 s after world init, and the diagnostics flag
                        // is pushed by the subsystem seconds LATER — a gated line would be silent
                        // exactly when it matters. One line per distinct class per world session.
                        // F2: this vetoes a REQUIREMENT EVALUATION. It is not evidence of a destroy —
                        // an armed asset may never destroy anything.
                        UE_LOG(LogNodeShuffle, Display,
                            TEXT("veto: FOREIGN-PROTECT first requirement evaluation vetoed for a node ")
                            TEXT("of class '%s' (resource '%s', resourceNodeType %d, by asset '%s'). ")
                            TEXT("Later nodes of this class are counted only; the VETOCENSUS line ")
                            TEXT("carries the totals."),
                            *ForeignClassName, *ForeignResourcePath, ForeignNodeType, *FromAssetPath);
                    }
                }
            }
            break;
        case ENodeShuffleNodeOrigin::VanillaOriginal:
            // NEVER protected: removing vanilla originals is the overhaul mod's own intended behaviour
            // and T58 does not touch it (same neutrality the managed-node registry documents).
            GNodeShuffleVanillaSeen++;
            break;
        case ENodeShuffleNodeOrigin::NotAResourceNode:
        default:
            GNodeShuffleNonNodeSeen++;
            break;
        }
    }

    // Per-decision trace (diagnostics-gated, Verbose): names the actor/class and which way we ruled.
    // Ungated this would firehose — the initial KBFL sweep runs every matching actor in the world.
    // T58: the new verdict token appears ONLY when protection actually fired, so with
    // NodeShuffle.ProtectForeignNodes=0 every line on this trace is byte-identical to the pre-T58 build.
    if (FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        UE_LOG(LogNodeShuffle, Verbose, TEXT("veto: IsRequirementMet target='%s' class='%s' -> %s"),
            *GetNameSafe(Target), Target ? *Target->GetClass()->GetName() : TEXT("<null>"),
            bSpawnWindow ? TEXT("VETO (spawn-window)")
                : bManaged ? TEXT("VETO (managed node)")
                : bProtectForeign ? TEXT("VETO (foreign-node protect)") : TEXT("allow (not ours)"));
    }

    if (bProtectForeign)
    {
        // Short-circuits the requirement chain exactly like the managed-node veto below, but WITHOUT
        // touching that path's counters: "spared N destroy attempts on M distinct nodes" keeps meaning
        // NodeShuffle's own nodes, and the T58 population is reported by its own VETOCENSUS line.
        return false;
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
