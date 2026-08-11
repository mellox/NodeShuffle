// ns-t64-rock-hide-and-audit, WORK ITEM A (docs/TECH-DEBT.md T64): HIDE A SHUFFLED ORIGINAL'S ROCK THE
// MOMENT ITS MESH ACTOR STREAMS IN.
//
// THE MECHANISM IS MEASURED, NOT REASONED. The trace is scratchpad/solid-hide-latency.md (2026-08-11,
// taken on build 2026-08-10-t61-1) and it is the authority for every number in this paragraph. All 874
// original NODE actors resolved and were hidden on hide-pass 1, ~25 s after mod load
// (`records=874 loaded=874 newlyHidden=874 pathUnresolved=0`). Of those, only 37 had a resolvable
// AFGNodeMeshActor at that instant; 837 did not, 271 of them because no separate mesh actor is authored
// at all and 566 because the assigned one had not streamed in. The record was then marked
// SteadyHiddenOriginals on the NODE result alone -- the steady mark's only other condition is the
// capture flag, never the mesh result -- and the main hide loop skips a steady record for the rest of
// that instance's life, so NOTHING EVER RETRIED THE PAIRING. When world partition finally loaded the
// rock it loaded VISIBLE: of the 220 rocks that became pairable in that session, 212 were still drawn at
// the moment they became pairable, worst delay 1535 s. The only surviving route that could darken one
// was the stray-rock backstop, which is the single player-distance gate in the whole hide path (300 m)
// and runs at most every 30 s -- which is what bounded the window the author watched.
//
// WHY THE HOOK IS HERE AND NOT AT THE HIDE LOOP. RebuildMeshActorCache is the function that FIRST
// observes a newly-streamed AFGNodeMeshActor, and ApplyLayout already calls it every pass BEFORE
// SuppressOriginalNodes. Hooking its two MeshActorCache.Add sites needs no engine event, adds no import
// surface, and leaves the steady-set semantics untouched -- whereas deferring the steady mark until the
// rock hid would put every one of those 566 records back through the full funnel on every pass, which is
// the exact cost the steady set exists to avoid.
//
// CAPTURE-IN-FLOW ORDER -- THE AUTHOR'S RULING, VERBATIM: "all shuffled things should be hidden
// immediately when doing a shuffle... check if captured, capture if needed, then hide."
// CaptureOriginalVisualIfNeeded's first source reads the PAIRED mesh actor's static mesh component
// (NodeShuffleSubsystem.cpp, Source 1), and it resolves that pairing through the very cache whose Add
// just happened. So the call order is: Add the pairing -> capture -> hide. THIS FILE DOES NOT REST ON
// THE OLDER IN-FILE CLAIM that "hiding never invalidates the mesh data, so retries stay correct". That
// claim is a comment in the hide loop and was never measured; ordering around it means it never has to
// be true.
// SCOPE OF THE CAPTURE BENEFIT (T64 cold review F7): gate 2 requires a STEADY record, and the steady
// mark is only written when capture is not pending -- so "a resource whose only donor is a
// late-streaming rock can now capture at all" is reachable ONLY for non-solid and
// placeholder-only-terminal resources. An ordinary solid with no donor stays PENDING, never goes
// steady, and is re-funnelled by the hide loop every pass, exactly as before this file existed.
//
// WHAT THIS FILE WILL NOT DO. It hides ONE thing -- the AFGNodeMeshActor handed to it -- and only when
// three predicates hold. It never hides a node actor, never touches an occupied original (an occupied
// original is never marked steady, so it cannot reach here), never spawns, and never writes a layout
// field. Every rejection is counted so the census can report a zero with its denominator.
//
// DIAGNOSTICS: the first five hides of a session get a Verbose line naming the record, the actor, and
// the three flags read at that instant; after that the counters in ROCKHIDE-CENSUS (emitted by
// SuppressOriginalNodes, always-on and delta-gated) are the whole story. No per-rock spam.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"

#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceNodeBase.h"

bool ANodeShuffleSubsystem::TryHideStreamedRockForSteadyOriginal(AFGResourceNodeBase* Node,
                                                                 AFGNodeMeshActor* MeshActor)
{
    if (!IsValid(Node) || !IsValid(MeshActor)) { return false; }
    ++RockCacheAddPairsSeenThisPass;

    // GATE 1 -- the node actor is hidden RIGHT NOW. Reading the live flag rather than trusting the
    // record is what stops this route re-hiding a rock whose node something else has un-hidden (the
    // re-roll restore un-hides node and rock together, and runs before the next cache rebuild).
    if (!Node->IsHidden())
    {
        ++RockCacheAddNodeNotHiddenThisPass;
        return false;
    }

    // GATE 2 -- a SteadyHiddenOriginals record resolves to THIS EXACT INSTANCE. The map is keyed by
    // FNodeShuffleSuppressedOriginal::VanillaNodePath, and VanillaNodeCache -- the map every path lookup
    // in the hide path goes through -- is built with `It->GetPathName()` as its key, so GetPathName() is
    // the same key by construction. Comparing the stored weak pointer against this actor is what makes a
    // key left behind by a previous instance at the same path fail CLOSED rather than hide a stranger's
    // rock. A node with no such record is somebody else's business: this route never widens the set of
    // originals this mod suppresses, it only changes WHEN their rock goes dark.
    const TWeakObjectPtr<AFGResourceNodeBase>* Steady = SteadyHiddenOriginals.Find(Node->GetPathName());
    if (!Steady || Steady->Get() != Node)
    {
        ++RockCacheAddNoSteadyRecordThisPass;
        return false;
    }

    // GATE 3 -- there is something to do. Both flags are read before anything is changed so the log line
    // below states what was true at the moment of the decision, not afterwards.
    const bool bMeshWasVisible = !MeshActor->IsHidden();
    const bool bMeshHadCollision = MeshActor->GetActorEnableCollision();
    if (!bMeshWasVisible && !bMeshHadCollision)
    {
        ++RockCacheAddAlreadyDarkThisPass;
        return false;
    }

    // CAPTURE FIRST (the author's ruling above). This is the SAME entry point the hide loop calls, not a
    // reimplementation, so a resource captured here is captured on identical terms -- including its
    // retry-budget bookkeeping, which SuppressOriginalNodes consumes later in this same pass.
    const bool bCapturePending = CaptureOriginalVisualIfNeeded(Node);
    ++RocksCaptureFirstThisPass;
    if (bCapturePending) { ++RocksCapturePendingAtHideThisPass; }

    // THEN HIDE. Same two calls, in the same order, as the hide loop's mesh-hide branch.
    MeshActor->SetActorHiddenInGame(true);
    MeshActor->SetActorEnableCollision(false);
    ++RocksHiddenAtCacheAddThisPass;
    ++RocksHiddenAtCacheAddTotal;
    // Recorded so the MESHHIDE-LATENCY report for this record can say the rock it found dark was
    // darkened by THIS route moments ago in THIS pass, instead of leaving a reader to infer that the
    // rock arrived hidden -- which nothing measures.
    RockHiddenAtCacheAddPathsThisPass.Add(Node->GetPathName());

    if (RocksHiddenAtCacheAddTotal <= 5 && FNodeShuffleModule::AreDiagnosticsEnabled())
    {
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("ROCKSTREAMHIDE record='%s': its AFGNodeMeshActor '%s' was paired by this pass's ")
            TEXT("mesh-actor cache rebuild and hidden in the same statement. MEASURED at the moment of ")
            TEXT("the decision -- the node actor was hidden, a steady-hidden record resolved to this ")
            TEXT("same instance, the rock was drawn: %d, the rock had collision: %d, and the visual ")
            TEXT("capture step ran before the hide and reported still-pending: %d. NOT MEASURED: how ")
            TEXT("long that mesh actor had been loaded, what streamed it in, and whether anything ")
            TEXT("would have hidden it later. Said for the first 5 rock(s) of this session only; every ")
            TEXT("one after that is in ROCKHIDE-CENSUS."),
            *Node->GetPathName(), *MeshActor->GetName(),
            bMeshWasVisible ? 1 : 0, bMeshHadCollision ? 1 : 0, bCapturePending ? 1 : 0);
    }
    return true;
}
