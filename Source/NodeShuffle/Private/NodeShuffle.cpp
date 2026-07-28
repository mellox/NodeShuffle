#include "NodeShuffle.h"

#include "NodeShuffleSubsystem.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ObjectKey.h" // coexist-veto-1: FObjectKey for the managed-node registry
#include "Patching/NativeHookManager.h"
#include "Hologram/FGResourceExtractorHologram.h"
#include "Buildables/FGBuildableResourceExtractorBase.h"
#include "Buildables/FGBuildableFrackingActivator.h"
#include "Buildables/FGBuildableFrackingExtractor.h"
#include "Equipment/FGResourceScanner.h"
#include "Equipment/FGPortableMinerDispenser.h"
#include "NodeShuffleResourceNode.h"
#include "NodeShuffleNodeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceNodeBase.h"
#include "Resources/FGExtractableResourceInterface.h"
#include "UObject/ScriptInterface.h"
#include "Engine/HitResult.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY(LogNodeShuffle);

// playtest-fixes-1: `NodeShuffle.Here` console command (backtick console). Logs the player's exact
// position + a census of every NodeShuffle-relevant thing within 300 m (layout entries with state,
// streamed originals with hidden/radiation status, water/depth test). Log-only and side-effect-free,
// so it is NOT gated behind EnableDiagnostics — it exists precisely so the user can report a location.
static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleHereCmd(
    TEXT("NodeShuffle.Here"),
    TEXT("Log player position + census of NodeShuffle entries/originals within 300 m (log-only)."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World) { return; }
        for (TActorIterator<ANodeShuffleSubsystem> It(World); It; ++It)
        {
            It->LogHereCensus();
            return;
        }
        UE_LOG(LogNodeShuffle, Display, TEXT("HERE: NodeShuffle subsystem not found in this world (main menu / no session loaded?)"));
    }));

// cave-nodes-1: manual cave seed at the player's position (roofed spots without a vanilla node —
// rock bridges, shelves, side tunnels). The flood-fill then maps the space from that seed.
static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleSeedHereCmd(
    TEXT("NodeShuffle.SeedHere"),
    TEXT("Plant a manual cave-floor seed where you stand (must be under a natural roof)."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World) { return; }
        for (TActorIterator<ANodeShuffleSubsystem> It(World); It; ++It)
        {
            It->SeedCaveCellAtPlayer();
            return;
        }
        UE_LOG(LogNodeShuffle, Display, TEXT("SEEDHERE: NodeShuffle subsystem not found in this world"));
    }));

// Diagnostics gate. OFF by default; set from config (EnableDiagnostics) by the subsystem each
// ApplyLayout pass. Game-thread only (placement hooks + config read both run on the game thread),
// so a plain bool is sufficient. Gates the verbose HOLOGRAMHOOK logging ONLY — the Mk1 accept-hook
// behavior is never gated.
static bool GNodeShuffleDiagnosticsEnabled = false;
void FNodeShuffleModule::SetDiagnosticsEnabled(bool bEnabled) { GNodeShuffleDiagnosticsEnabled = bEnabled; }
bool FNodeShuffleModule::AreDiagnosticsEnabled() { return GNodeShuffleDiagnosticsEnabled; }

// ---------------------------------------------------------------------------------------------
// coexist-veto-1: KBFL destroyer veto — CVar, managed-node registry, and the veto-module bridge.
// This module stays 100% KBFL-free: presence is a module-NAME string check, the veto module is
// loaded by name on demand, and its arm entry point arrives via a plain function pointer that the
// veto module registers in its own StartupModule. When the veto never arms (CVar off, KBFL absent,
// module load failure, ABI guard trip) the coexist-1 tombstone backoff remains the coexistence path.
// ---------------------------------------------------------------------------------------------

// Experimental veto gate. Default OFF; read once per world init (subsystem BeginPlay), so toggling
// mid-session takes effect at the next world load. Users set it via console or Engine.ini
// [ConsoleVariables] / [SystemSettings].
static int32 GNodeShuffleDestroyerVeto = 0;
static FAutoConsoleVariableRef CVarNodeShuffleDestroyerVeto(
    TEXT("NodeShuffle.DestroyerVeto"),
    GNodeShuffleDestroyerVeto,
    TEXT("EXPERIMENTAL. 1 = when KBFL is installed, veto KBFL-based actor destroyers/listeners for the ")
    TEXT("nodes NodeShuffle spawned (instead of letting them be destroyed and tombstoned per session). ")
    TEXT("0 = off (default). Takes effect at world load."),
    ECVF_Default);

// Managed-node registry. FObjectKey (object index + serial) is stable across GC, cheap to hash, and
// never matches a different (later) actor even if the memory slot is reused — safe against stale
// entries. Game-thread only by contract (see NodeShuffle.h).
static TSet<FObjectKey> GNodeShuffleManagedNodes;

// The veto module's per-world arm entry point (null until/unless NodeShuffleVetoKBFL loads).
static void (*GNodeShuffleKBFLVetoArmFn)(UWorld* World) = nullptr;

void FNodeShuffleModule::RegisterManagedNode(const AActor* Node)
{
    if (Node) { GNodeShuffleManagedNodes.Add(FObjectKey(Node)); }
}

void FNodeShuffleModule::UnregisterManagedNode(const AActor* Node)
{
    if (Node) { GNodeShuffleManagedNodes.Remove(FObjectKey(Node)); }
}

void FNodeShuffleModule::ResetManagedNodes()
{
    GNodeShuffleManagedNodes.Empty();
}

bool FNodeShuffleModule::IsManagedSpawnedNode(const AActor* Node)
{
    return Node != nullptr && GNodeShuffleManagedNodes.Contains(FObjectKey(Node));
}

// spawnrace-1: spawn-window depth counter. Game-thread only (our SpawnActor calls and KBFL's
// OnActorSpawned delegate both run there) — plain int32, no atomics. A counter rather than a bool
// so a wrapped spawn that re-enters another wrapped spawn can never clear the window early.
static int32 GNodeShuffleSpawningDepth = 0;

FNodeShuffleSpawningScope::FNodeShuffleSpawningScope() { GNodeShuffleSpawningDepth++; }
FNodeShuffleSpawningScope::~FNodeShuffleSpawningScope() { GNodeShuffleSpawningDepth--; }

bool FNodeShuffleModule::IsSpawningManagedNode()
{
    return GNodeShuffleSpawningDepth > 0;
}

void FNodeShuffleModule::SetKBFLVetoArmFunction(void (*ArmFn)(UWorld* World))
{
    GNodeShuffleKBFLVetoArmFn = ArmFn;
}

void FNodeShuffleModule::ArmDestroyerVetoIfEnabled(UWorld* World)
{
    if (GNodeShuffleDestroyerVeto == 0)
    {
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("veto: NodeShuffle.DestroyerVeto=0 (off) — coexist-1 tombstone backoff is the coexistence path this session"));
        return;
    }
    UE_LOG(LogNodeShuffle, Display, TEXT("veto: NodeShuffle.DestroyerVeto=1 at world init — arming"));
    if (!FModuleManager::Get().IsModuleLoaded(TEXT("KBFL")))
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("veto: enabled but KBFL is not installed — inactive (nothing to veto)"));
        return;
    }
    UE_LOG(LogNodeShuffle, Display, TEXT("veto: KBFL module present"));
    if (!GNodeShuffleKBFLVetoArmFn)
    {
        // On-demand load; StartupModule of the veto module registers the arm pointer synchronously
        // inside this call. A null result (or a still-null pointer) means the DLL failed to load —
        // most plausibly a version-incompatible KBFL whose exports no longer satisfy our imports.
        FModuleManager::Get().LoadModulePtr<IModuleInterface>(FName(TEXT("NodeShuffleVetoKBFL")));
        if (!GNodeShuffleKBFLVetoArmFn)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("veto: NodeShuffleVetoKBFL module failed to load (version-incompatible KBFL?) — veto off, tombstone backoff covers"));
            return;
        }
    }
    GNodeShuffleKBFLVetoArmFn(World);
}

namespace
{
    // redesign-13: dump a component's FULL collision so the comparator (nodes the build trace DID hit:
    // Res_PolymerResin2_C, lead_C) reveals EXACTLY which channel/objtype the build trace lands on — vs ours
    // (never hit). SNAPDIAG only logged 4 channels + profile/response; this adds Visibility/Camera/WorldStatic/
    // WorldDynamic and the actual collision-enabled/objtype, which is what the trace geometry depends on.
    void LogCompCollision(const TCHAR* Tag, UPrimitiveComponent* Comp)
    {
        if (!Comp) { UE_LOG(LogNodeShuffle, Display, TEXT("HOLOGRAMHOOK %s comp=<null>"), Tag); return; }
        auto R = [](ECollisionResponse X) -> const TCHAR*
        {
            return X == ECR_Block ? TEXT("Block") : X == ECR_Overlap ? TEXT("Overlap") : TEXT("Ignore");
        };
        // Channels (DefaultEngine.ini): Hologram=GTC2, Resource=GTC3, Clearance=GTC4, BuildGun=GTC5.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("HOLOGRAMHOOK %s comp='%s' class='%s' collEnabled=%d profile='%s' objType=%d | Visibility=%s Camera=%s WorldStatic=%s WorldDynamic=%s Resource=%s BuildGun=%s"),
            Tag, *Comp->GetName(), *Comp->GetClass()->GetName(),
            (int32)Comp->GetCollisionEnabled(), *Comp->GetCollisionProfileName().ToString(),
            (int32)Comp->GetCollisionObjectType(),
            R(Comp->GetCollisionResponseToChannel(ECC_Visibility)),
            R(Comp->GetCollisionResponseToChannel(ECC_Camera)),
            R(Comp->GetCollisionResponseToChannel(ECC_WorldStatic)),
            R(Comp->GetCollisionResponseToChannel(ECC_WorldDynamic)),
            R(Comp->GetCollisionResponseToChannel(ECC_GameTraceChannel3)),  // Resource
            R(Comp->GetCollisionResponseToChannel(ECC_GameTraceChannel5))); // BuildGun
    }
}

// redesign-19 DIAGNOSTIC: this module is friended to AFGResourceExtractorHologram (AccessTransformers), so
// this static member can call the hologram's PROTECTED acceptance checks on the resource the trace hit, and
// log which one rejects our node. (A lambda can't access protected members even inside a friend; a named
// member of the friend class can.)
void FNodeShuffleModule::DbgLogAcceptance(AFGResourceExtractorHologram* Hologram, AActor* ResourceActor)
{
    if (!Hologram || !ResourceActor) { return; }
    if (!GNodeShuffleDiagnosticsEnabled) { return; } // diagnostics gated OFF by default (config: EnableDiagnostics)
    TScriptInterface<IFGExtractableResourceInterface> Res;
    Res.SetObject(ResourceActor);
    Res.SetInterface(Cast<IFGExtractableResourceInterface>(ResourceActor));
    const bool bCan = Hologram->CanOccupyResource(Res);
    const bool bAllowed = Hologram->IsAllowedOnResource(Res);
    UE_LOG(LogNodeShuffle, Display,
        TEXT("HOLOGRAMHOOK ACCEPTANCE CanOccupyResource=%d IsAllowedOnResource=%d | resource='%s'"),
        bCan ? 1 : 0, bAllowed ? 1 : 0, *ResourceActor->GetName());

    // redesign-22 DIAGNOSTIC: dump every sub-condition the acceptance checks read, so we KNOW which one
    // rejects our node instead of guessing. (1) node-side interface answers; (2) the Miner default-extractor's
    // restrictions (node-type/forms/resource-list). The friend grants let us read the protected members.
    AFGResourceNodeBase* RN = Cast<AFGResourceNodeBase>(ResourceActor);
    const int32 NodeForm = RN ? (int32)RN->GetResourceForm() : -1;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("HOLOGRAMHOOK ACCEPT-NODE canPlaceExtractor=%d canBecomeOccupied=%d isOccupied=%d form=%d nodeClass='%s' resClass='%s'"),
        (RN && RN->CanPlaceResourceExtractor()) ? 1 : 0,
        (RN && RN->CanBecomeOccupied()) ? 1 : 0,
        (RN && RN->IsOccupied()) ? 1 : 0,
        NodeForm,
        *ResourceActor->GetClass()->GetName(),
        (RN && RN->GetResourceClass()) ? *RN->GetResourceClass()->GetName() : TEXT("<null>"));

    const AFGBuildableResourceExtractorBase* Ext = Hologram->mDefaultExtractor;
    if (Ext)
    {
        const UClass* Restrict = Ext->mRestrictToNodeType.Get();
        const bool bIsAType = (Restrict && ResourceActor) ? ResourceActor->IsA(Restrict) : true;
        bool bFormAllowed = Ext->mAllowedResourceForms.Num() == 0; // empty = unrestricted
        for (EResourceForm F : Ext->mAllowedResourceForms) { if ((int32)F == NodeForm) { bFormAllowed = true; break; } }
        FString FormsStr;
        for (EResourceForm F : Ext->mAllowedResourceForms) { FormsStr += FString::Printf(TEXT("%d,"), (int32)F); }
        bool bResAllowed = !Ext->mOnlyAllowCertainResources;
        if (Ext->mOnlyAllowCertainResources && RN)
        {
            for (const TSubclassOf<UFGResourceDescriptor>& R : Ext->mAllowedResources)
            { if (R.Get() == RN->GetResourceClass().Get()) { bResAllowed = true; break; } }
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("HOLOGRAMHOOK ACCEPT-EXT restrictToNodeType='%s' nodeIsA=%d | allowedForms=[%s] formAllowed=%d | onlyCertain=%d resAllowed=%d | extractorType='%s'"),
            Restrict ? *Restrict->GetName() : TEXT("<none>"), bIsAType ? 1 : 0,
            *FormsStr, bFormAllowed ? 1 : 0,
            Ext->mOnlyAllowCertainResources ? 1 : 0, bResAllowed ? 1 : 0,
            *Ext->GetExtractorTypeName().ToString());
    }
    else
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("HOLOGRAMHOOK ACCEPT-EXT mDefaultExtractor=<null>"));
    }
}

// CRASH GUARD (fracking-crash-fix): our nodes are regular Node-type resource nodes, NOT fracking
// core/satellite nodes. A FRACKING extractor (Resource Well Pressurizer = AFGBuildableFrackingActivator, or the
// satellite Resource Well Extractor = AFGBuildableFrackingExtractor; modded wells derive from these) does a
// CHECKED cast of the node to a fracking node type in OnExtractableResourceSet — placing one on our node is a
// FATAL crash (user-reported). The force-accept hooks must NOT accept fracking extractors: without our Override
// the base check rejects our node (invalid placement, like vanilla on a non-fracking node) instead of crashing.
// Detected via the hologram's PUBLIC GetBuildClass() (the buildable class being constructed — catches modded
// subclasses too). Free function (not a lambda) so the hook capture lists stay single-item — SUBSCRIBE_UOBJECT_
// METHOD is a function-like macro and a comma in the lambda CAPTURE list `[a, b]` is read as an extra macro arg.
static bool NodeShuffleIsFrackingExtractor(const AFGResourceExtractorHologram* H)
{
    const UClass* BuildClass = H ? H->GetBuildClass().Get() : nullptr;
    return BuildClass && (BuildClass->IsChildOf(AFGBuildableFrackingActivator::StaticClass())
                       || BuildClass->IsChildOf(AFGBuildableFrackingExtractor::StaticClass()));
}

void FNodeShuffleModule::StartupModule()
{
    UE_LOG(LogNodeShuffle, Log, TEXT("NodeShuffle module loaded"));
    UE_LOG(LogNodeShuffle, Display, TEXT("===== NodeShuffle 1.3.0 LOADED (2026-07-28-sfplus-showdown-1) ====="));

#if !WITH_EDITOR
    // redesign-13 HOLOGRAM HOOK (DIAGNOSTICS). r12 proved the Mk1 build trace NEVER hits our node (0 hits on
    // NodeShuffleResourceNode) but DID hit other resource nodes (Res_PolymerResin2_C, lead_C) with -> 1. So
    // the gate is NOT validation — the trace geometrically MISSES our collision. Extend the hook to dump the
    // FULL collision of WHATEVER the trace lands on, so the comparator names the channel/objtype the build
    // trace uses (very likely Visibility/Camera, which our 'Resource' profile Ignores). Log-only: we forward
    // Scope unchanged and only READ its return — zero behavior change. Rate-limited to one line per hit.
    SUBSCRIBE_UOBJECT_METHOD(AFGResourceExtractorHologram, IsValidHitResult,
        [](auto& Scope, const AFGResourceExtractorHologram* Self, const FHitResult& hitResult)
    {
        const bool bOriginal = Scope(Self, hitResult); // run the real check, READ but never Override()
        if (!GNodeShuffleDiagnosticsEnabled) { return; } // logging gated OFF by default (config: EnableDiagnostics)
        AActor* HitActor = hitResult.GetActor();
        UPrimitiveComponent* HitComp = hitResult.GetComponent();
        static TSet<FString> LoggedHits;
        const FString Key = (HitActor ? HitActor->GetName() : TEXT("<null>"))
            + TEXT("|") + (HitComp ? HitComp->GetName() : TEXT("<null>"));
        if (!LoggedHits.Contains(Key))
        {
            LoggedHits.Add(Key);
            UE_LOG(LogNodeShuffle, Display,
                TEXT("HOLOGRAMHOOK IsValidHitResult -> %d | hitActor='%s' class='%s'"),
                bOriginal ? 1 : 0,
                HitActor ? *HitActor->GetName() : TEXT("<null>"),
                HitActor ? *HitActor->GetClass()->GetName() : TEXT("<null>"));
            LogCompCollision(TEXT("HIT"), HitComp); // FULL collision of the component the trace landed on
            // redesign-17: when the trace hits OUR node, log node Z vs the trace impact Z. r16 PROVED the
            // trace now hits our node and IsValidHitResult -> 1 (valid). The user reports the Mk1 STILL
            // won't place AND that our node's resource-manager convergence is BELOW the surface — so the
            // gate is now placement/position, not detection. This names how far below the impact our node
            // origin sits (a buried node origin makes the snapped miner fail clearance/position).
            if (HitActor && HitActor->GetClass()->GetName() == TEXT("NodeShuffleResourceNode"))
            {
                const FVector NL = HitActor->GetActorLocation();
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("HOLOGRAMHOOK OURNODE-POS nodeActorLoc=%s | traceImpact=%s | node.Z - impact.Z = %.1f"),
                    *NL.ToCompactString(), *hitResult.ImpactPoint.ToCompactString(),
                    NL.Z - hitResult.ImpactPoint.Z);
            }
        }
    });

    // redesign-17: r16 proved IsValidHitResult -> 1, yet the Mk1 won't place — a later step rejects it.
    // CanOccupyResource/IsAllowedOnResource are PROTECTED (not hookable from here). TrySnapToActor IS public
    // (AFGHologram interface) — it runs AFTER IsValidHitResult to snap the extractor to the resource. Hook it
    // to log whether the snap to OUR node succeeds: if it returns 0 on our node, the snap itself fails; if 1,
    // the gate is the later clearance/placement check (and the OURNODE-POS log above tells us if our node is
    // buried). Log-only (forward Scope, read return, never Override).
    SUBSCRIBE_UOBJECT_METHOD(AFGResourceExtractorHologram, TrySnapToActor,
        [](auto& Scope, AFGResourceExtractorHologram* Self, const FHitResult& hitResult)
    {
        const bool r = Scope(Self, hitResult);
        if (!GNodeShuffleDiagnosticsEnabled) { return; } // logging gated OFF by default (config: EnableDiagnostics)
        AActor* HitActor = hitResult.GetActor();
        // real-class redesign: detect OUR node robustly — the trace hits our uniquely-named rock component
        // (NodeShuffleRockMesh / _Rt), OR the actor carries our component, OR it's a legacy subclass. The old
        // name-only check missed real-class nodes (BP_ResourceNode_C), so this dump never fired for them.
        UPrimitiveComponent* HitComp = hitResult.GetComponent();
        const bool bOursRock = HitComp && HitComp->GetName().StartsWith(TEXT("NodeShuffleRockMesh"));
        const UNodeShuffleNodeComponent* OurComp = HitActor ? UNodeShuffleNodeComponent::Find(HitActor) : nullptr;
        const bool bOurs = (HitActor && HitActor->GetClass()->GetName() == TEXT("NodeShuffleResourceNode"))
            || bOursRock || (OurComp != nullptr);
        static TSet<FString> Logged;
        const FString Key = (HitActor ? HitActor->GetName() : TEXT("<null>"));
        if (!Logged.Contains(Key))
        {
            Logged.Add(Key);
            UE_LOG(LogNodeShuffle, Display, TEXT("HOLOGRAMHOOK TrySnapToActor -> %d | hitActor='%s' class='%s'%s | hitComp='%s' compFound=%d bForceAccept=%d"),
                r ? 1 : 0, HitActor ? *HitActor->GetName() : TEXT("<null>"),
                HitActor ? *HitActor->GetClass()->GetName() : TEXT("<null>"),
                bOurs ? TEXT("  <-- OUR NODE") : TEXT(""),
                HitComp ? *HitComp->GetName() : TEXT("<null>"),
                OurComp ? 1 : 0, OurComp ? (OurComp->bForceAccept ? 1 : 0) : -1);
            // redesign-18: when the snap fails on OUR node, log its resource state so we can confirm the
            // mResourceClass-null-after-reload hypothesis (GetResourceClass() = override, valid; original
            // = mResourceClass, was null post-reload — the suspected snap gate).
            if (bOurs)
            {
                if (AFGResourceNodeBase* RN = Cast<AFGResourceNodeBase>(HitActor))
                {
                    const UClass* RC = RN->GetResourceClass();
                    const UClass* OC = RN->GetResourceClassOriginal().Get();
                    UE_LOG(LogNodeShuffle, Display,
                        TEXT("HOLOGRAMHOOK SNAP-RES resClass='%s'(null=%d) origClass(mResourceClass)='%s'(null=%d) form=%d occupied=%d hasAnyResources=%d containsOwn=%d"),
                        RC ? *RC->GetName() : TEXT("<null>"), RC ? 0 : 1,
                        OC ? *OC->GetName() : TEXT("<null>"), OC ? 0 : 1,
                        (int32)RN->GetResourceForm(), RN->IsOccupied() ? 1 : 0,
                        RN->HasAnyResources() ? 1 : 0,
                        RN->DoesContainResource(RN->GetResourceClass()) ? 1 : 0);
                }
                // redesign-19: call the hologram's PROTECTED acceptance checks (via the friended helper) to
                // see which one rejects our node inside TrySnapToActor.
                FNodeShuffleModule::DbgLogAcceptance(Self, HitActor);
            }
        }
    });

    // redesign-23 (THE FIX — surgical accept-hook). r22 diagnostics PROVED the only remaining gate: the Miner's
    // mRestrictToNodeType='BP_ResourceNode_C' and our custom subclass fails IsA() (ACCEPT-EXT nodeIsA=0); every
    // other sub-condition already passes (canPlaceExtractor=1, form ok, resource ok). The class-identity check
    // lives inside IsAllowedOnResource/CanOccupyResource, which TrySnapToActor calls. We can't change what class
    // our actor IsA, so instead force these two protected acceptance checks to return true FOR OUR NODE ONLY,
    // letting the real TrySnapToActor complete its normal snap. Surgical (vanilla nodes/other actors untouched —
    // we Override only when the resource is a NodeShuffleResourceNode) and MP-correct (the hook runs identically
    // on server + clients). SML detours patch the function body, so TrySnapToActor's INTERNAL calls to these are
    // intercepted too. Friend grant (AccessTransformers) makes the protected method addresses takeable here.
    // real-class redesign: relocated nodes are now their ORIGINAL class + a UNodeShuffleNodeComponent. We
    // force-accept the Mk hologram based on the component's bForceAccept flag (computed at spawn from the
    // node's native mCanPlacePortableMiner): vanilla nodes AND modded nodes that allow normal mining
    // (AllMinable item-nodes) are force-accepted (a runtime-spawned node otherwise fails the hologram's
    // IsA(BP_ResourceNode_C) gate); SPECIAL modded nodes that reject portable mining (lithium's Alkali
    // reactive-ore node) are NOT force-accepted, so their native rules stand and only their own extractor
    // binds. Legacy old-save subclass nodes are always force-accepted (they are our generic node).
    auto IsOurNode = [](const TScriptInterface<IFGExtractableResourceInterface>& Resource) -> bool
    {
        const UObject* Obj = Resource.GetObject();
        const AActor* Actor = Cast<AActor>(Obj);
        const UNodeShuffleNodeComponent* Comp = Actor ? UNodeShuffleNodeComponent::Find(Actor) : nullptr;
        const bool bLegacy = (Obj && Obj->GetClass()->GetName() == TEXT("NodeShuffleResourceNode"));
        const bool bResult = Comp ? Comp->bForceAccept : bLegacy;
        // ISOURNODE diag (capped): show WHY a real-class node does/doesn't get force-accepted — whether the
        // component was found and its bForceAccept — so we can pin the real-vanilla-node rejection.
        static int32 sIsOurNodeLog = 0;
        if (GNodeShuffleDiagnosticsEnabled && sIsOurNodeLog < 50)
        {
            sIsOurNodeLog++;
            UE_LOG(LogNodeShuffle, Display,
                TEXT("ISOURNODE obj='%s' class='%s' compFound=%d bForceAccept=%d legacy=%d -> result=%d"),
                Obj ? *Obj->GetName() : TEXT("<null>"), Obj ? *Obj->GetClass()->GetName() : TEXT("<null>"),
                Comp ? 1 : 0, Comp ? (Comp->bForceAccept ? 1 : 0) : -1, bLegacy ? 1 : 0, bResult ? 1 : 0);
        }
        return bResult;
    };
    // Fracking extractors are rejected via the free function NodeShuffleIsFrackingExtractor() (above) — see its
    // comment for the crash it prevents and why it is a free function, not a captured lambda.
    SUBSCRIBE_UOBJECT_METHOD(AFGResourceExtractorHologram, IsAllowedOnResource,
        [IsOurNode](auto& Scope, const AFGResourceExtractorHologram* Self,
                    const TScriptInterface<IFGExtractableResourceInterface>& resource)
    {
        if (IsOurNode(resource) && !NodeShuffleIsFrackingExtractor(Self)) // never force-accept fracking (crash)
        {
            static bool bLoggedOnce = false;
            if (GNodeShuffleDiagnosticsEnabled && !bLoggedOnce) { bLoggedOnce = true;
                UE_LOG(LogNodeShuffle, Display, TEXT("HOLOGRAMHOOK FORCE-ACCEPT IsAllowedOnResource->true (our node)")); }
            Scope.Override(true); // accept our node; skip the BP_ResourceNode_C IsA() rejection (ALWAYS active)
        }
    });
    SUBSCRIBE_UOBJECT_METHOD(AFGResourceExtractorHologram, CanOccupyResource,
        [IsOurNode](auto& Scope, const AFGResourceExtractorHologram* Self,
                    const TScriptInterface<IFGExtractableResourceInterface>& resource)
    {
        if (IsOurNode(resource) && !NodeShuffleIsFrackingExtractor(Self)) // never force-accept fracking (crash)
        {
            static bool bLoggedOnce = false;
            if (GNodeShuffleDiagnosticsEnabled && !bLoggedOnce) { bLoggedOnce = true;
                UE_LOG(LogNodeShuffle, Display, TEXT("HOLOGRAMHOOK FORCE-ACCEPT CanOccupyResource->true (our node)")); }
            Scope.Override(true); // ALWAYS active (the fix); only the log above is diagnostics-gated
        }
    });

    // SCANNER PHANTOM-PING ROOT FIX (scanner-7). scanner-5/6 cancelled the per-cluster map REPRESENTATION, but
    // SCANDIAG proved the scan PING still fires on empty ground: the scanner builds mNodeClusters from ALL
    // resource nodes (ignoring hidden/collision), then plays the scan effect (PlayClusterEffects) AND picks
    // closest clusters from that SAME list — so suppressing only CreateResourceNodeRepresentations leaves the
    // ping effect intact (12 SUPPRESS lines, yet the user still saw those exact iron clusters ping). Fix at the
    // SOURCE: hook GenerateNodeClusters, let it build mNodeClusters, then strip hidden-only clusters and
    // recenter mixed ones IN PLACE — so every downstream consumer (effects, compass/map markers, closest pick)
    // sees only real, visible nodes. mNodeClusters + GenerateNodeClusters are protected; Friend-granted to the
    // module via AccessTransformers. The scanner-6 representation hook below is kept as a harmless backstop.
    SUBSCRIBE_UOBJECT_METHOD(AFGResourceScanner, GenerateNodeClusters,
        [](auto& Scope, AFGResourceScanner* Self)
    {
        Scope(Self); // build mNodeClusters normally first
        if (!IsValid(Self)) { return; }

        int32 Dropped = 0;
        int32 Cleaned = 0;
        TArray<FNodeClusterData>& Clusters = Self->mNodeClusters;
        for (int32 i = Clusters.Num() - 1; i >= 0; --i)
        {
            FNodeClusterData& C = Clusters[i];
            TArray<TObjectPtr<AFGResourceNodeBase>> Visible;
            FVector Sum(FVector::ZeroVector);
            for (AFGResourceNodeBase* N : C.Nodes)
            {
                if (IsValid(N) && !N->IsHidden() && N->GetActorEnableCollision())
                {
                    Visible.Add(N);
                    Sum += N->GetActorLocation();
                }
            }
            if (Visible.Num() == 0)
            {
                Clusters.RemoveAt(i); // phantom cluster of only hidden originals -> gone before any consumer
                ++Dropped;
            }
            else if (Visible.Num() != C.Nodes.Num())
            {
                C.Nodes = MoveTemp(Visible); // drop the hidden originals dragging the midpoint
                C.MidPoint = Sum / C.Nodes.Num();
                ++Cleaned;
            }
        }
        if (GNodeShuffleDiagnosticsEnabled && (Dropped > 0 || Cleaned > 0))
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("SCANDIAG GENCLUSTERS filtered: dropped=%d cleaned=%d remaining=%d"),
                Dropped, Cleaned, Clusters.Num());
        }
    });

    // SCANNER PHANTOM-PING FIX (scanner-6). scanner-5 proved (SCANDIAG) the resource scanner groups EVERY
    // AFGResourceNode by resource class with NO filter on hidden/collision, so our hidden originals
    // (BP_ResourceNode_C, hidden=1, collEnabled=0) still get clustered. scanner-5 cancelled a cluster only when
    // EVERY node was hidden — but GenerateNodeClusters MERGES nodes within mDistBetweenNodesInCluster, so a MIXED
    // cluster (one visible relocated node + nearby hidden originals) slipped through: the kept hidden nodes still
    // produced markers and dragged the averaged MidPoint onto empty ground. Fix: rebuild the cluster with
    // VISIBLE-only nodes (valid, not hidden, collision on) and recompute the midpoint over just those, then run
    // the original on the cleaned cluster. 0 visible -> cancel (pure phantom). all visible -> forward unchanged.
    // Calling Scope(Self, Clean) runs the original with substituted args and suppresses the dirty auto-forward
    // (TCallScope<void>::operator() sets bForwardCall=false). CreateResourceNodeRepresentations is public.
    SUBSCRIBE_UOBJECT_METHOD(AFGResourceScanner, CreateResourceNodeRepresentations,
        [](auto& Scope, AFGResourceScanner* Self, const FNodeClusterData& cluster)
    {
        const int32 Total = cluster.Nodes.Num();
        if (Total == 0) { return; } // empty cluster: let the game handle it

        // A node counts as "real/visible" only if it is valid, not hidden, AND has collision enabled. Our
        // suppressed originals are BOTH hidden AND collision-disabled (SCANDIAG: hidden=1 collEnabled=0).
        FNodeClusterData Clean;
        Clean.ResourceDescriptor = cluster.ResourceDescriptor;
        FVector Sum(FVector::ZeroVector);
        for (AFGResourceNodeBase* N : cluster.Nodes)
        {
            if (IsValid(N) && !N->IsHidden() && N->GetActorEnableCollision())
            {
                Clean.Nodes.Add(N);
                Sum += N->GetActorLocation();
                // Diagnostic: a KEPT node that is NOT one of our relocated copies is a candidate "escaped
                // original" (a vanilla node we failed to hide). If phantom pings persist after this fix, these
                // lines name the offender. Logged once each, gated.
                if (GNodeShuffleDiagnosticsEnabled && N->GetClass()->GetName() != TEXT("NodeShuffleResourceNode"))
                {
                    static TSet<FString> LoggedForeign;
                    const FString Key = N->GetName();
                    if (!LoggedForeign.Contains(Key))
                    {
                        LoggedForeign.Add(Key);
                        UE_LOG(LogNodeShuffle, Display,
                            TEXT("SCANDIAG KEPT-FOREIGN node='%s' class='%s' loc=%s hidden=%d coll=%d res='%s'"),
                            *Key, *N->GetClass()->GetName(), *N->GetActorLocation().ToCompactString(),
                            N->IsHidden() ? 1 : 0, N->GetActorEnableCollision() ? 1 : 0,
                            cluster.ResourceDescriptor.Get() ? *cluster.ResourceDescriptor.Get()->GetName() : TEXT("<null>"));
                    }
                }
            }
        }
        const int32 Kept = Clean.Nodes.Num();

        if (Kept == 0)
        {
            if (GNodeShuffleDiagnosticsEnabled)
            {
                const UClass* RC = cluster.ResourceDescriptor.Get();
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("SCANDIAG SUPPRESS phantom cluster res='%s' mid=%s nodes=%d (all hidden originals)"),
                    RC ? *RC->GetName() : TEXT("<null>"), *cluster.MidPoint.ToCompactString(), Total);
            }
            Scope.Cancel(); // pure phantom: create no representation at all
            return;
        }

        if (Kept == Total)
        {
            return; // nothing hidden in this cluster: forward the original unchanged (auto-forward)
        }

        // Mixed cluster: drop the hidden originals and recenter on the visible nodes only.
        Clean.MidPoint = Sum / Kept;
        if (GNodeShuffleDiagnosticsEnabled)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("SCANDIAG CLEAN mixed cluster res='%s' kept=%d/%d oldMid=%s newMid=%s"),
                cluster.ResourceDescriptor.Get() ? *cluster.ResourceDescriptor.Get()->GetName() : TEXT("<null>"),
                Kept, Total, *cluster.MidPoint.ToCompactString(), *Clean.MidPoint.ToCompactString());
        }
        Scope(Self, Clean); // run the original on the cleaned, recentered cluster (suppresses the dirty auto-forward)
    });

    // PORTABLE-MINER SURFACE PLACEMENT (polish-9). A portable miner is spawned by AFGPortableMinerDispenser at a
    // location its placement trace computed — and that trace passes THROUGH our cosmetic rock (the rock only
    // blocks Pawn/BuildGun), so the miner lands on the terrain UNDER the rock and is buried (invisible + the
    // solid rock wins the look-at, so you can't reach it). Hook the server spawn and, when the target is one of
    // OUR nodes, raise the spawn point onto the ROCK SURFACE at the aimed X/Y (a line trace against the rock
    // mesh). The miner then sits ON the rock where you placed it — visible, interactable, any number of them,
    // coexisting with a Mk1. Node origin/snapping is untouched; only the portable miner's spawn Z is adjusted.
    SUBSCRIBE_UOBJECT_METHOD(AFGPortableMinerDispenser, Server_SpawnPortableMiner_Implementation,
        [](auto& Scope, AFGPortableMinerDispenser* Self, const FVector& location, AFGResourceNode* resourceNode)
    {
        // real-class redesign: the fallback rock lives on our component (vanilla-origin nodes) or, for legacy
        // old-save nodes, the ANodeShuffleResourceNode subobject. Modded-origin nodes have no fallback rock
        // (native visual) and typically reject portable miners anyway -> default placement.
        UStaticMeshComponent* RM = nullptr;
        if (const UNodeShuffleNodeComponent* Comp = UNodeShuffleNodeComponent::Find(resourceNode))
        {
            RM = Comp->RockMesh;
        }
        else if (ANodeShuffleResourceNode* Legacy = Cast<ANodeShuffleResourceNode>(resourceNode))
        {
            RM = Legacy->RockMesh;
        }
        if (!IsValid(RM) || !RM->GetStaticMesh())
        {
            return; // not our node with a fallback rock -> default placement (auto-forward unchanged)
        }
        const float TopZ = RM->Bounds.Origin.Z + RM->Bounds.BoxExtent.Z + 200.f;
        const float BotZ = RM->Bounds.Origin.Z - RM->Bounds.BoxExtent.Z - 200.f;
        FVector NewLoc = location;
        FHitResult Hit;
        FCollisionQueryParams Params(FName(TEXT("NodeShufflePortableSurface")), /*bTraceComplex=*/true);
        if (RM->LineTraceComponent(Hit, FVector(location.X, location.Y, TopZ), FVector(location.X, location.Y, BotZ), Params))
        {
            NewLoc.Z = Hit.ImpactPoint.Z; // sit the miner's base on the rock surface at the aimed X/Y
        }
        if (GNodeShuffleDiagnosticsEnabled)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("PORTABLE-SURFACE node='%s' in=%s -> out=%s hit=%d"),
                resourceNode ? *resourceNode->GetName() : TEXT("<null>"), *location.ToCompactString(), *NewLoc.ToCompactString(), Hit.bBlockingHit ? 1 : 0);
        }
        Scope(Self, NewLoc, resourceNode); // spawn the portable miner on the rock surface
    });
#endif
}

IMPLEMENT_GAME_MODULE(FNodeShuffleModule, NodeShuffle);
