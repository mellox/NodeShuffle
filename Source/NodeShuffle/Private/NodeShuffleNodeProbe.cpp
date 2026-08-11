// ns-t54-immediate-hide, SCOPE ADDITION 1 (author, 2026-08-10): `NodeShuffle.ProbeNearestNode`.
// THE SOLID-NODE EQUIVALENT OF NodeShuffle.WellProbe. LOG-ONLY. IT GATES NOTHING AND CHANGES NO STATE.
//
// THE GAP IT CLOSES, stated as what was MEASURED tonight rather than as a theory: two of our placed
// nodes -- lithium (Desc_OreLithium_C) and lead (oreleaddesc_C, actor BP_ResourceNode_C_2147425393) --
// sit almost entirely inside rock, and no command could probe a solid node's CENTRE. NodeShuffle.Here
// probes where the player stands; NodeShuffle.PointAtHere probes wherever the aim trace lands, which on
// the author's own attempt was a cliff 5.4 m away and then a rock mesh. The centre is the point that
// decides the defect -- it is where an extractor snaps -- and it had never been probed on this
// population. WellProbe does exactly this for well members and has no ordinary-node sibling.
//
// NEAREST ONE ENTRY, NO ARGUMENT, NO ENUMERATION. The author has ruled against commands accreting into
// directories, so this takes no argument and reports one entry.
//
// NO RESOURCE-FORM FILTER, AND THAT IS DELIBERATE. "Solid node" here means "an entry in Layout" as
// opposed to WellLayout -- the ordinary spawned-node population. Filtering on ResourceForm would have
// excluded LITHIUM, which is one of the two nodes that motivated this command (the mod classifies it
// with the gas/liquid forms). A filter that excludes the reported case is worse than no filter.
//
// IT MIRRORS WellProbe'S ARGUMENT SPLIT EXACTLY, INCLUDING THE ASYMMETRY, WHICH IS THEREFORE NOT A BUG
// HERE: the shipped enclosure predicate is handed the PAWN as its ignore actor (what all three existing
// probe commands pass it), while the containment instrument is handed the NODE'S OWN live actor as its
// subject so the node's own collision is excluded from its own reading. Two different exclusions
// answering two different questions. Named in the log so a reader cannot mistake one for the other.
//
// NOT DIAGNOSTICS-GATED, and the dispatch brief's premise that the siblings are is FALSE: none of
// NodeShuffle.Here, NodeShuffle.PointAtHere or NodeShuffle.WellProbe tests AreDiagnosticsEnabled. They
// are commands a human types, so the request IS the gate. Matching them is what "consistent with the
// sibling commands" can mean without making this one silently do nothing when someone runs it.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleTotallyInside.h" // ns-t53-totallyinside: the positive-only containment probe

#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

#include "Resources/FGResourceNode.h"

static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleProbeNearestNodeCmd(
    TEXT("NodeShuffle.ProbeNearestNode"),
    TEXT("Probe the recorded CENTRE of the nearest settled/live shuffled node to you (log-only)."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World) { return; }
        for (TActorIterator<ANodeShuffleSubsystem> It(World); It; ++It)
        {
            It->LogNearestNodeProbe();
            return;
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("NODEPROBE: NodeShuffle subsystem not found in this world (main menu / no session ")
            TEXT("loaded?) -- no entry was searched for and no gate was run."));
    }));

void ANodeShuffleSubsystem::LogNearestNodeProbe()
{
    UWorld* W = GetWorld();
    if (!W)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("NODEPROBE: no world -- nothing was searched for and no query was made."));
        return;
    }

    // The pawn is used for exactly two things, the same two WellProbe uses it for: the enclosure
    // predicate's ignore list, and the distance this command sorts by. It is NOT the containment
    // instrument's subject.
    APawn* Pawn = nullptr;
    for (FConstPlayerControllerIterator PIt = W->GetPlayerControllerIterator(); PIt; ++PIt)
    {
        if (APlayerController* Pc = PIt->Get())
        {
            if (APawn* P = Pc->GetPawn()) { Pawn = P; break; }
        }
    }
    if (!Pawn)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("NODEPROBE: no player pawn resolved, so there is no position to measure distance from ")
            TEXT("and no entry was chosen. Nothing was probed. The predicate this branch tested is that ")
            TEXT("the controller iterator yielded no pawn; nothing here measured why."));
        return;
    }
    const FVector PlayerLoc = Pawn->GetActorLocation();

    // ---- CHOOSE ONE ENTRY: the nearest of OUR spawned entries whose recorded centre means something ----
    // ELIGIBILITY, stated as predicates rather than as a category: it must be one of OUR spawned entries
    // (bIsNewNode -- a non-new entry's Location is a vanilla original's, which this command is not about),
    // it must be active, and its recorded centre must be a settled one (bRayCasted) OR it must currently
    // have a live spawned actor. Both halves are reported per entry below, because an entry chosen on the
    // second half alone has a centre that was recorded before any ground trace ran.
    int32 BestIdx = INDEX_NONE;
    double BestDistCm = 0.0;
    int32 Eligible = 0;
    for (int32 i = 0; i < Layout.Num(); ++i)
    {
        const FNodeShuffleEntry& E = Layout[i];
        if (!E.bIsNewNode || !E.bActive) { continue; }
        AFGResourceNode* const* Found = SpawnedNodes.Find(E.EntryGuid);
        const bool bHasLiveActor = (Found != nullptr) && IsValid(*Found);
        if (!E.bRayCasted && !bHasLiveActor) { continue; }
        ++Eligible;
        const double D = FVector::Dist(PlayerLoc, E.Location);
        if (BestIdx == INDEX_NONE || D < BestDistCm) { BestIdx = i; BestDistCm = D; }
    }

    if (BestIdx == INDEX_NONE)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("NODEPROBE: no eligible entry. MEASURED -- %d entr(ies) in the layout, %d of them ")
            TEXT("eligible (one of ours, active, and either settled by a ground trace or currently ")
            TEXT("holding a live spawned actor). Nothing was probed and no gate was run. This states no ")
            TEXT("cause: whether the save holds no shuffled nodes, or none has settled yet, is not ")
            TEXT("tested here."),
            Layout.Num(), Eligible);
        return;
    }

    const FNodeShuffleEntry& E = Layout[BestIdx];
    AFGResourceNode* const* FoundBest = SpawnedNodes.Find(E.EntryGuid);
    AFGResourceNode* LiveActor = (FoundBest != nullptr) ? *FoundBest : nullptr;
    const AActor* Subject = IsValid(LiveActor) ? LiveActor : nullptr;
    const FString SubjectName = Subject
        ? Subject->GetName()
        : FString(TEXT("<none: no live spawned actor resolved for this entry's guid>"));

    // ---- WHAT IS BEING PROBED. FIRST, so every reading below is attributable. ----
    // MEASURED ONLY: the entry's own recorded fields, the distance computed just now, and whether the
    // guid resolved to a live actor. Nothing here claims why an actor did or did not resolve, and the
    // resource path is the entry's ASSIGNED one -- what this node is dealt to carry -- printed beside the
    // original so a re-typed node cannot be mistaken for an untouched one.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("NODEPROBE: probing the RECORDED CENTRE of layout entry %d of %d -- assigned resource '%s' ")
        TEXT("(originally '%s'), recorded at %s, %.0f cm from you, settled by a ground trace: %d, cave ")
        TEXT("cell: %d, resource form byte %d. Live spawned actor: %s. It was the nearest of %d eligible ")
        TEXT("entr(ies). THE CENTRE IS THE POINT PROBED -- not an aim impact and not the ground under ")
        TEXT("you -- because that is where an extractor snaps. Two exclusions follow and they are still ")
        TEXT("not interchangeable, though ns-t66 narrowed the gap between them: the shipped enclosure ")
        TEXT("gate is handed YOUR PAWN, and now this entry's own live actor too when one resolved this ")
        TEXT("run, both on its ignore list -- the pawn exactly as NodeShuffle.Here and NodeShuffle.WellProbe ")
        TEXT("hand it, the entry's own actor added by ns-t66 so a probe at this entry's own centre does ")
        TEXT("not self-hit the entry it is measuring -- while the containment instrument is handed THIS ")
        TEXT("NODE'S OWN actor as its SUBJECT, a different mechanism (hits on it are classified after the ")
        TEXT("query runs, not withheld from the trace) answering a different question."),
        BestIdx + 1, Layout.Num(), *E.AssignedResourceClassPath, *E.OriginalResourceClassPath,
        *E.Location.ToCompactString(), BestDistCm, E.bRayCasted ? 1 : 0, E.bUnderground ? 1 : 0,
        static_cast<int32>(E.ResourceForm), *SubjectName, Eligible);

    // ---- THE SHIPPED ENCLOSURE GATE, AT THE SAME POINT ----
    // The SAME member function both placement paths call, with the same recorder and threshold
    // out-params and the same pawn on the ignore list. Nothing here reimplements the predicate.
    // ns-t66-probe-self-ignore: Subject (this entry's own live actor, IsValid-checked, computed above)
    // now rides along as the gate's SECOND ignore actor. Null when this entry has no live actor, which
    // makes this call byte-identical to the pre-T66 one -- there was nothing to self-hit in that case
    // and there is still nothing added to the ignore list for it.
    TArray<FNodeShuffleEnclosureRay> Rays;
    int32 Blocked = 0, Total = 0, Threshold = -1;
    const bool bEnclosed = IsSpotEnclosed(E.Location, Blocked, Total, &Rays, &Threshold, Pawn, Subject);

    for (int32 i = 0; i < Rays.Num(); ++i)
    {
        const FNodeShuffleEnclosureRay& R = Rays[i];
        UE_LOG(LogNodeShuffle, Display,
            TEXT("NODEPROBE: enclosure ray %d of %d, bearing %.0f deg: %s%s%s"),
            i + 1, Rays.Num(), R.BearingDeg,
            R.bBlocked ? TEXT("BLOCKED") : TEXT("clear"),
            R.bBlocked ? *FString::Printf(TEXT(" at %.0f cm by "), R.HitDistanceCm) : TEXT(""),
            R.bBlocked ? *R.HitActor : TEXT(""));
    }

    // ns-t66-probe-self-ignore: read back from the SAME Subject pointer the call above was actually
    // given, never a separate claim -- "none" when this entry had no live actor this run, which is the
    // only case where this field's value differs from what the pre-T66 build would have produced.
    const TCHAR* IgnoredOwnActorTok = Subject ? *SubjectName : TEXT("none");
    UE_LOG(LogNodeShuffle, Display,
        TEXT("NODEPROBE: ENCLOSURE GATE at %s: %d of %d rays blocked, and this build refuses a spot at %d ")
        TEXT("or more blocked, so the verdict for this entry is %s. ignoredOwnActor=%s. The threshold and ")
        TEXT("the ray count were read back from the predicate on this run. This gate is horizontal-only ")
        TEXT("at one height (docs/TECH-DEBT.md T37, T41) -- it is not a containment test and a pass from ")
        TEXT("it is not evidence that the centre is in open air."),
        *E.Location.ToCompactString(), Blocked, Total, Threshold,
        bEnclosed ? TEXT("REFUSE") : TEXT("accept"), IgnoredOwnActorTok);

    // ---- THE POSITIVE-ONLY CONTAINMENT INSTRUMENT, AT THE SAME POINT ----
    // ONE EMITTER, T26. The wording is LogTotallyInsideReading's, not this file's: three copies of one
    // instrument's prose drift, and a reader then cannot tell a difference in the WORLD from a difference
    // in the log strings. This command adds a prefix and a tag and nothing else.
    FNodeShuffleTotallyInsideReading Inside;
    RunTotallyInsideProbe(W, E.Location, Subject, Inside);
    LogTotallyInsideReading(
        TEXT("NODEPROBE"),
        FString::Printf(TEXT("the recorded centre of layout entry %d of %d, resource '%s'"),
                        BestIdx + 1, Layout.Num(), *E.AssignedResourceClassPath),
        Inside,
        SubjectName,
        bEnclosed, Blocked, Total, Threshold);
}
