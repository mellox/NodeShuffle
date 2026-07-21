#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// coexist-veto-1: NODESHUFFLE_API-exported (engine idiom, cf. CoreGlobals.h) so the optional
// NodeShuffleVetoKBFL module logs under the SAME LogNodeShuffle category users already filter on.
NODESHUFFLE_API DECLARE_LOG_CATEGORY_EXTERN(LogNodeShuffle, Log, All);

// Module installs diagnostic hooks on the Mk1 extractor hologram (redesign-13..19).
// coexist-veto-1: NODESHUFFLE_API so the optional NodeShuffleVetoKBFL module can link the statics
// below (diagnostics gate, managed-node registry, arm-hook registration).
class NODESHUFFLE_API FNodeShuffleModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override;
    virtual bool IsGameModule() const override { return true; }
    // redesign-19 DIAGNOSTIC: AccessTransformers friends this module to AFGResourceExtractorHologram so this
    // static helper can call the hologram's PROTECTED CanOccupyResource/IsAllowedOnResource on the resource
    // our trace hit, and log which check rejects our node (TrySnapToActor -> 0 with a valid resource).
    static void DbgLogAcceptance(class AFGResourceExtractorHologram* Hologram, class AActor* ResourceActor);

    // Diagnostics gate (config-driven, OFF by default). The behavioral hooks (Mk1 accept-fix)
    // ALWAYS run; only the verbose diagnostic LOGGING is gated by this so normal users get a
    // clean log and zero overhead. The subsystem pushes the config value here each ApplyLayout
    // pass, so toggling "Enable Diagnostic Logging" in the Mods menu takes effect live.
    static void SetDiagnosticsEnabled(bool bEnabled);
    static bool AreDiagnosticsEnabled();

    // ---- coexist-veto-1: managed-node registry + KBFL destroyer-veto bridge ----
    // Actor-keyed mirror of the nodes NodeShuffle SPAWNED or ADOPTED this world (the guid-keyed truth
    // stays in the subsystem's SpawnedNodes). The optional veto module queries it in O(1) per actor
    // event, so it must stay cheap. SCOPE (deliberate): managed = nodes we spawned/adopted ONLY.
    // Hidden ORIGINALS are NEVER registered — on an SF+ world, removing vanilla originals is that
    // mod's intended core behavior and we stay neutral; our originals are hidden anyway and
    // reload-healable. Game-thread only (spawn/adopt/wipe sites and KBFL's actor delegates all run
    // on the game thread) — plain containers, no locking.
    static void RegisterManagedNode(const class AActor* Node);
    static void UnregisterManagedNode(const class AActor* Node);
    static void ResetManagedNodes(); // world init: the registry is module-static and outlives worlds
    static bool IsManagedSpawnedNode(const class AActor* Node);

    // The optional NodeShuffleVetoKBFL module registers its per-world arm entry point here from its
    // StartupModule. Function-pointer indirection keeps the dependency arrow one-way (veto -> main):
    // this module has ZERO KBFL includes/links and never sees the veto module's headers.
    static void SetKBFLVetoArmFunction(void (*ArmFn)(class UWorld* World));
    // Called once per world init (subsystem BeginPlay, authority only): reads the
    // NodeShuffle.DestroyerVeto CVar, checks KBFL presence by module NAME, loads the veto module on
    // demand, and invokes its registered arm function for this world.
    static void ArmDestroyerVetoIfEnabled(class UWorld* World);
};
