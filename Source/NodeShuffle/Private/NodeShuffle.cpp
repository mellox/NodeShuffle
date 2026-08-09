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
#include "FGConstructDisqualifier.h" // fu1diag-1: TSubclassOf<UFGConstructDisqualifier>::Get() needs
                                      // the complete type (IsChildOf(T::StaticClass()) internally)

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
        // Packet F (ns-automatch): the four sub-checks below (restrict/nodeIsA, forms/formAllowed,
        // onlyCertain/resAllowed) used to be inlined here. They now live in the reusable
        // EvaluateExtractorAcceptance helper (see its declaration comment in NodeShuffle.h) so
        // NodeShuffle.DumpExtractors' match matrix can call the SAME predicate instead of a second copy.
        // Parity argument (zero behavior change): the original ran two separate loops over
        // mAllowedResourceForms (one that broke on first form match, one that always built the full CSV);
        // the helper runs a single merged loop that both appends to the CSV AND sets bFormAllowed on
        // match, never breaking early -- the CSV was always built in full regardless, and once
        // bFormAllowed is set true nothing later can un-set it, so the merge changes no observable value.
        const FNodeShuffleExtractorAcceptance A = FNodeShuffleModule::EvaluateExtractorAcceptance(
            Ext, ResourceActor ? ResourceActor->GetClass() : nullptr, NodeForm,
            (RN && RN->GetResourceClass()) ? RN->GetResourceClass().Get() : nullptr);
        // fu1diag-1 (FU1 §6.1): identity fields — Hologram is proven non-null by the early return above;
        // GetBuildClass() already has a call site in NodeShuffleIsFrackingExtractor (zero new symbol).
        const UClass* AcceptBuildClass = Hologram->GetBuildClass().Get();
        UE_LOG(LogNodeShuffle, Display,
            TEXT("HOLOGRAMHOOK ACCEPT-EXT restrictToNodeType='%s' nodeIsA=%d | allowedForms=[%s] formAllowed=%d | onlyCertain=%d resAllowed=%d | extractorType='%s' | holo='%s' build='%s'"),
            *A.RestrictClassName, A.bNodeIsA ? 1 : 0,
            *A.AllowedFormsCsv, A.bFormAllowed ? 1 : 0,
            A.bOnlyCertainResources ? 1 : 0, A.bResourceAllowed ? 1 : 0,
            *Ext->GetExtractorTypeName().ToString(),
            *Hologram->GetClass()->GetName(), AcceptBuildClass ? *AcceptBuildClass->GetName() : TEXT("<none>"));
    }
    else
    {
        const UClass* AcceptBuildClass = Hologram->GetBuildClass().Get();
        UE_LOG(LogNodeShuffle, Display, TEXT("HOLOGRAMHOOK ACCEPT-EXT mDefaultExtractor=<null> | holo='%s' build='%s'"),
            *Hologram->GetClass()->GetName(), AcceptBuildClass ? *AcceptBuildClass->GetName() : TEXT("<none>"));
    }
}

// Packet F (ns-automatch): see the declaration comment in NodeShuffle.h for why this must be a named
// static member (protected-member friend access) and what it's reused for. Reads the SAME four
// protected fields DbgLogAcceptance above always has (mRestrictToNodeType, mAllowedResourceForms,
// mOnlyAllowCertainResources, mAllowedResources) — already friend-granted by the SAME
// AccessTransformers entry DbgLogAcceptance uses; no new grant needed. Pure: no logging, no engine
// mutation, and it never reasons about which form is "normal" — it only ever compares against whatever
// NodeResourceForm/NodeResourceClass the caller supplies, which is what keeps it correct for a liquid
// (oil) extractor without a single oil-specific line anywhere in this function.
FNodeShuffleExtractorAcceptance FNodeShuffleModule::EvaluateExtractorAcceptance(
    const AFGBuildableResourceExtractorBase* Extractor, const UClass* NodeClass,
    int32 NodeResourceForm, const UClass* NodeResourceClass)
{
    FNodeShuffleExtractorAcceptance Out;
    if (!Extractor) { return Out; } // nothing to check against -- mirrors DbgLogAcceptance's null-Ext branch
    Out.bHasExtractorCdo = true;
    // Packet G: the ONE input that can turn a vacuous "nothing to compare" into a false ACCEPT (see the
    // struct comment in NodeShuffle.h for the four-case argument) -- set unconditionally, not gated.
    Out.bComparedAgainstNode = (NodeClass != nullptr);

    const UClass* Restrict = Extractor->mRestrictToNodeType.Get();
    Out.bHasRestriction = (Restrict != nullptr);
    Out.RestrictClass = Restrict; // H1b: hierarchy-queryable form of the same value (see the struct comment)
    Out.RestrictClassName = Restrict ? Restrict->GetName() : TEXT("<none>");
    Out.RestrictClassPath = Restrict ? Restrict->GetPathName() : TEXT("<none>");
    Out.bNodeIsA = (Restrict && NodeClass) ? NodeClass->IsChildOf(Restrict) : true;

    Out.bFormAllowed = Extractor->mAllowedResourceForms.Num() == 0; // empty = unrestricted
    for (EResourceForm F : Extractor->mAllowedResourceForms)
    {
        Out.AllowedForms.Add((int32)F);
        Out.AllowedFormsCsv += FString::Printf(TEXT("%d,"), (int32)F);
        if ((int32)F == NodeResourceForm) { Out.bFormAllowed = true; }
    }

    Out.bOnlyCertainResources = Extractor->mOnlyAllowCertainResources;
    Out.bResourceAllowed = !Extractor->mOnlyAllowCertainResources;
    for (const TSubclassOf<UFGResourceDescriptor>& R : Extractor->mAllowedResources)
    {
        UClass* RCls = R.Get();
        if (!RCls) { continue; }
        Out.AllowedResourcePaths.Add(RCls->GetPathName());
        if (Extractor->mOnlyAllowCertainResources && RCls == NodeResourceClass) { Out.bResourceAllowed = true; }
    }

    // ns-review-g G4: the verdict is only node-EVIDENCED if at least one sub-check was non-vacuous.
    Out.bDiscriminated = Out.bHasRestriction
        || Extractor->mAllowedResourceForms.Num() > 0
        || Extractor->mOnlyAllowCertainResources;
    return Out;
}

// redesign-24 (Packet E): see the declaration comment in NodeShuffle.h for the full reasoning. Reads
// mDefaultExtractor (protected on AFGResourceExtractorHologram) and mRestrictToNodeType (protected on
// AFGBuildableResourceExtractorBase) -- both already friend-granted to this module by the SAME
// AccessTransformers entries DbgLogAcceptance above uses; no new grant was added for this.
bool FNodeShuffleModule::IsGenericExtractorRestriction(const AFGResourceExtractorHologram* Hologram, FString* OutRestrictName)
{
    if (!Hologram)
    {
        if (OutRestrictName) { *OutRestrictName = TEXT("<no-holo>"); }
        return true; // nothing to restrict on -- treat as generic (matches the pre-Packet-E unconditional override)
    }
    const AFGBuildableResourceExtractorBase* Ext = Hologram->mDefaultExtractor;
    if (!Ext)
    {
        if (OutRestrictName) { *OutRestrictName = TEXT("<no-default-extractor>"); }
        return true; // mirrors DbgLogAcceptance's own null-Ext branch -- nothing to check against, don't newly reject
    }
    const UClass* Restrict = Ext->mRestrictToNodeType.Get();
    if (OutRestrictName) { *OutRestrictName = Restrict ? Restrict->GetName() : TEXT("<none>"); }
    // (a) Unset -- no node-type restriction at all. Generic by definition (the header documents
    // "If None, there is no node-type restriction for this extractor type").
    if (!Restrict) { return true; }
    // (b) The vanilla generic node class every ordinary Miner restricts to. MEASURED two ways: the
    // Build_MinerMk5_C CDO export carries
    //   mRestrictToNodeType = "/Game/FactoryGame/Resource/BP_ResourceNode.BP_ResourceNode_C"
    // and 12 in-game ACCEPT-EXT lines print restrictToNodeType='BP_ResourceNode_C' for both
    // Build_MinerMk5_C and Build_ModularMiner_01_C. Compared by NAME, not path, so a repackaged or
    // relocated vanilla asset still matches -- same idiom as NodeShuffleIsOursForDiag's bLegacy check.
    static const FString GenericNodeClassName = TEXT("BP_ResourceNode_C");
    if (Restrict->GetName() == GenericNodeClassName) { return true; }
    // (c) ANY stock-game node class. (b) is measured for SOLID-ore Miners only -- the Oil Pump's
    // restriction has never appeared in any log we hold (zero Pump ACCEPT-EXT lines across 14 sessions)
    // and no Oil Pump CDO export exists, so if it restricts to some OTHER vanilla node class then (b)
    // alone would newly refuse oil pumps on the two node cases that still depend on the override (the
    // ANodeShuffleResourceNode spawn fallback, and legacy old-save oil nodes). Every class shipped by
    // the base game lives under /Game/FactoryGame/; a mod defines its node class under its OWN mount
    // root (MEASURED: AlkaLib restricts to
    // "/Lithium/World/BP_ResourdeNode_Alkali.BP_ResourdeNode_Alkali_C"). So this clause can only WIDEN
    // "generic" to other stock classes -- it is structurally incapable of re-admitting the mod-special
    // extractor this packet exists to reject. GetPathName()/StartsWith are both already-used symbols
    // (NodeShuffleSubsystem.cpp:1045, :2901) -- no new import.
    return Restrict->GetPathName().StartsWith(TEXT("/Game/FactoryGame/"));
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

// D2-A (fu1diag-1, FU1 design §4.3 / §6.5): shared "is this actor one of OUR nodes" predicate for
// DIAGNOSTIC purposes — the SAME robust three-way test the TrySnapToActor hook already used (rock-
// mesh component name prefix OR UNodeShuffleNodeComponent OR legacy subclass name), hoisted into one
// place so the OURNODE-POS and KEPT-FOREIGN sites stop using the legacy-name-ONLY check that matches
// NOTHING since the real-class redesign (a dead diagnostic that carried no information about where a
// failure sits — not evidence of an un-reached branch; FU1 §4.3). HitComp may be null: sites not
// driven by a line-trace hit (KEPT-FOREIGN's cluster-node iteration) have no hit component to offer
// for the rock-name leg — the component-find + legacy-name legs alone are the robust test there.
static bool NodeShuffleIsOursForDiag(const AActor* Actor, const UPrimitiveComponent* HitComp)
{
    if (!Actor) { return false; }
    const bool bLegacy = (Actor->GetClass()->GetName() == TEXT("NodeShuffleResourceNode"));
    const bool bRock = HitComp && HitComp->GetName().StartsWith(TEXT("NodeShuffleRockMesh"));
    const bool bComp = (UNodeShuffleNodeComponent::Find(Actor) != nullptr);
    return bLegacy || bRock || bComp;
}

// fu1diag-1: SUBSCRIBE_UOBJECT_METHOD is a function-like macro, and the preprocessor argument
// splitter tracks only round-paren nesting — a bare comma inside angle brackets (TMap<K, V>) used
// directly inside a hooked lambda's body is misread as an extra macro argument (the same class of
// trap the file's own comment above already documents for lambda CAPTURE-list commas). Alias every
// multi-arg template used inside a macro-wrapped hook body to a single identifier here, OUTSIDE any
// macro invocation, so the hook bodies below never spell a bare top-level comma.
using FNodeShuffleFlipResultMap = TMap<FString, bool>;
using FNodeShuffleDqSignatureMap = TMap<FString, FString>;
using FNodeShuffleDqTimeMap = TMap<FString, float>;
// redesign-24 (Packet E): the FORCE-ACCEPT-EVAL log dedup key -- (actor, extractor build class) --
// same trap as above (TPair<A, B> has a bare comma), aliased here for the same reason.
using FNodeShuffleActorExtractorKey = TPair<FObjectKey, FObjectKey>;
using FNodeShuffleActorExtractorLoggedSet = TSet<FNodeShuffleActorExtractorKey>;

void FNodeShuffleModule::StartupModule()
{
    UE_LOG(LogNodeShuffle, Log, TEXT("NodeShuffle module loaded"));
    UE_LOG(LogNodeShuffle, Display, TEXT("===== NodeShuffle 1.3.0 LOADED (2026-08-09-t23hide-2) ====="));
    FNodeShuffleModule::LogAutoAllowExtractorsState(); // Packet G: log the CVar state once at startup

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
        // fu1diag-1 (FU1 §6.1): identity fields reused by every log line in this hook — WHICH hologram
        // instance and which buildable class it's constructing. GetBuildClass() already has a call site
        // in NodeShuffleIsFrackingExtractor above — zero new symbol.
        const FString HoloName = Self ? Self->GetClass()->GetName() : TEXT("<none>");
        const UClass* BuildClassPtr = Self ? Self->GetBuildClass().Get() : nullptr;
        const FString BuildName = BuildClassPtr ? BuildClassPtr->GetName() : TEXT("<none>");
        // fu1diag-1: robust ours-predicate (D2-A), needed below by both OURNODE-POS and the DQ dump.
        const bool bOurs = NodeShuffleIsOursForDiag(HitActor, HitComp);

        static TSet<FString> LoggedHits;
        const FString Key = (HitActor ? HitActor->GetName() : TEXT("<null>"))
            + TEXT("|") + (HitComp ? HitComp->GetName() : TEXT("<null>"));
        if (!LoggedHits.Contains(Key))
        {
            LoggedHits.Add(Key);
            UE_LOG(LogNodeShuffle, Display,
                TEXT("HOLOGRAMHOOK IsValidHitResult -> %d | hitActor='%s' class='%s' | holo='%s' build='%s'"),
                bOriginal ? 1 : 0,
                HitActor ? *HitActor->GetName() : TEXT("<null>"),
                HitActor ? *HitActor->GetClass()->GetName() : TEXT("<null>"),
                *HoloName, *BuildName);
            LogCompCollision(TEXT("HIT"), HitComp); // FULL collision of the component the trace landed on
            // redesign-17: when the trace hits OUR node, log node Z vs the trace impact Z. r16 PROVED the
            // trace now hits our node and IsValidHitResult -> 1 (valid). The user reports the Mk1 STILL
            // won't place AND that our node's resource-manager convergence is BELOW the surface — so the
            // gate is now placement/position, not detection. This names how far below the impact our node
            // origin sits (a buried node origin makes the snapped miner fail clearance/position).
            // D2-A (fu1diag-1, FU1 §4.3): was a legacy-name-ONLY check that matches nothing since the
            // real-class redesign (S3 in FU1's evidence — a dead diagnostic, not evidence of an
            // un-reached branch). Now the shared robust predicate. SPEC GAP (flagged, see final report):
            // the design also asks for mBoxComponent name/extent on this line; mBoxComponent is
            // protected on AFGResourceNodeBase and FNodeShuffleModule has no friend grant to that class
            // (only ANodeShuffleSubsystem does) — this packet authorizes exactly one
            // AccessTransformers.ini addition (AFGHologram, for mConstructDisqualifiers) and explicitly
            // nothing else, and the only public accessor (GetBoxExtent()) would be an unverified NEW
            // engine call, which this diagnostic-only/zero-new-import packet is designed to avoid.
            // Omitted rather than silently widening scope.
            if (bOurs)
            {
                const FVector NL = HitActor->GetActorLocation();
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("HOLOGRAMHOOK OURNODE-POS nodeActorLoc=%s | traceImpact=%s | node.Z - impact.Z = %.1f"),
                    *NL.ToCompactString(), *hitResult.ImpactPoint.ToCompactString(),
                    NL.Z - hitResult.ImpactPoint.Z);
            }
        }

        // fu1diag-1 (FU1 §6.2): result-CHANGE (FLIP) logging — runs every invocation (NOT gated by the
        // first-sight dedup above), so a hit-validity flip on an already-logged actor is never invisible
        // again. Bounded: transitions only, keyed per (hologram, actor).
        {
            static FNodeShuffleFlipResultMap LastResult;
            static int32 sCallCount = 0;
            ++sCallCount;
            const FString FlipKey = HoloName + TEXT("|") + (HitActor ? HitActor->GetName() : TEXT("<null>"));
            const bool* Prev = LastResult.Find(FlipKey);
            if (!Prev || *Prev != bOriginal)
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("HOLOGRAMHOOK FLIP IsValidHitResult %s->%d actor='%s' holo='%s' frame=%d"),
                    Prev ? (*Prev ? TEXT("1") : TEXT("0")) : TEXT("?"), bOriginal ? 1 : 0,
                    HitActor ? *HitActor->GetName() : TEXT("<null>"), *HoloName, sCallCount);
                LastResult.Add(FlipKey, bOriginal);
            }
        }

        // fu1diag-1 (FU1 §6.3, the load-bearing line): read mSnappedExtractableResource (Friend,
        // AccessTransformers.ini:19 on the derived class) + mConstructDisqualifiers (Friend, the NEW
        // AFGHologram grant this packet adds — the base-class member) directly. This is the LAST
        // COMPLETED validate's disqualifier list, i.e. exactly what the HUD showed. Member READS
        // ONLY — no new engine calls. On-change + 1/s heartbeat, and ONLY while the hit actor carries
        // our component — never floods for foreign nodes/empty ground. No CheckValidPlacement hook
        // (explicitly excluded from v1 — a new protected-member address-take, the shipping-export
        // 127-risk class; build it only if this dump proves ambiguous, behind its own import gate).
        if (bOurs)
        {
            const bool bSnapped = Self->mSnappedExtractableResource.GetObject() != nullptr;
            FString DisqStr;
            for (const TSubclassOf<UFGConstructDisqualifier>& DQ : Self->mConstructDisqualifiers)
            {
                if (UClass* DQClass = DQ.Get())
                {
                    DisqStr += (DisqStr.IsEmpty() ? TEXT("") : TEXT(",")) + DQClass->GetName();
                }
            }
            static FNodeShuffleDqSignatureMap LastDqSignature;
            static FNodeShuffleDqTimeMap LastDqLogSeconds;
            const FString DqKey = HoloName + TEXT("|") + (HitActor ? HitActor->GetName() : TEXT("<null>"));
            const FString Signature = FString::Printf(TEXT("%d|%s"), bSnapped ? 1 : 0, *DisqStr);
            const FString* PrevSig = LastDqSignature.Find(DqKey);
            const float* PrevTime = LastDqLogSeconds.Find(DqKey);
            const float NowSeconds = Self->GetWorld() ? Self->GetWorld()->GetTimeSeconds() : 0.f;
            const bool bChanged = !PrevSig || (*PrevSig != Signature);
            const bool bHeartbeatDue = PrevTime && (NowSeconds - *PrevTime >= 1.0f);
            if (bChanged || bHeartbeatDue)
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("HOLOGRAMHOOK DQ holo='%s' snapped=%d disq=[%s]"),
                    *HoloName, bSnapped ? 1 : 0, DisqStr.IsEmpty() ? TEXT("<none>") : *DisqStr);
                LastDqSignature.Add(DqKey, Signature);
                LastDqLogSeconds.Add(DqKey, NowSeconds);
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
        // D2-A (fu1diag-1): hoisted into the shared NodeShuffleIsOursForDiag helper (also used by
        // OURNODE-POS and KEPT-FOREIGN) — identical test, identical result, single source of truth.
        // OurComp is kept as its own lookup (not exposed by the shared bool-returning helper) — the
        // log line below needs the ACTUAL component pointer for compFound=/bForceAccept=.
        UPrimitiveComponent* HitComp = hitResult.GetComponent();
        const UNodeShuffleNodeComponent* OurComp = HitActor ? UNodeShuffleNodeComponent::Find(HitActor) : nullptr;
        const bool bOurs = NodeShuffleIsOursForDiag(HitActor, HitComp);
        // fu1diag-1 (FU1 §6.1): identity fields.
        const FString HoloName = Self ? Self->GetClass()->GetName() : TEXT("<none>");
        const UClass* BuildClassPtr = Self ? Self->GetBuildClass().Get() : nullptr;
        const FString BuildName = BuildClassPtr ? BuildClassPtr->GetName() : TEXT("<none>");

        static TSet<FString> Logged;
        const FString Key = (HitActor ? HitActor->GetName() : TEXT("<null>"));
        if (!Logged.Contains(Key))
        {
            Logged.Add(Key);
            UE_LOG(LogNodeShuffle, Display, TEXT("HOLOGRAMHOOK TrySnapToActor -> %d | hitActor='%s' class='%s'%s | hitComp='%s' compFound=%d bForceAccept=%d | holo='%s' build='%s'"),
                r ? 1 : 0, HitActor ? *HitActor->GetName() : TEXT("<null>"),
                HitActor ? *HitActor->GetClass()->GetName() : TEXT("<null>"),
                bOurs ? TEXT("  <-- OUR NODE") : TEXT(""),
                HitComp ? *HitComp->GetName() : TEXT("<null>"),
                OurComp ? 1 : 0, OurComp ? (OurComp->bForceAccept ? 1 : 0) : -1,
                *HoloName, *BuildName);
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

        // fu1diag-1 (FU1 §6.2): result-CHANGE (FLIP) logging for TrySnapToActor — mirrors the
        // IsValidHitResult hook's FLIP block. Runs every invocation, transitions only.
        {
            static FNodeShuffleFlipResultMap LastResult;
            static int32 sCallCount = 0;
            ++sCallCount;
            const FString FlipKey = HoloName + TEXT("|") + Key;
            const bool* Prev = LastResult.Find(FlipKey);
            if (!Prev || *Prev != r)
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("HOLOGRAMHOOK FLIP TrySnapToActor %s->%d actor='%s' holo='%s' frame=%d"),
                    Prev ? (*Prev ? TEXT("1") : TEXT("0")) : TEXT("?"), r ? 1 : 0,
                    HitActor ? *HitActor->GetName() : TEXT("<null>"), *HoloName, sCallCount);
                LastResult.Add(FlipKey, r);
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
    // force-accept the Mk hologram based on the component's bForceAccept flag. redesign-24 correction: that
    // flag is computed at spawn/adopt as (Node->GetResourceForm() != RF_GAS) -- NOT from the node's native
    // mCanPlacePortableMiner, as this comment previously claimed. So EVERY non-gas node of ours is
    // force-accept-eligible; the node side performs NO special-resource discrimination whatsoever. Gas-form
    // nodes (lithium's Alkali reactive-ore node) are excluded purely by that form test, which is why their
    // native rules stand and only their own extractor binds. Believing the node side filtered special
    // resources is what let a mod's SPECIAL extractor be waved onto an ordinary coal node for a full packet
    // cycle -- the extractor-side narrowing below (IsGenericExtractorRestriction) is what actually filters.
    // Legacy old-save subclass nodes are always force-accepted (they are our generic node).
    auto IsOurNode = [](const TScriptInterface<IFGExtractableResourceInterface>& Resource) -> bool
    {
        const UObject* Obj = Resource.GetObject();
        const AActor* Actor = Cast<AActor>(Obj);
        const UNodeShuffleNodeComponent* Comp = Actor ? UNodeShuffleNodeComponent::Find(Actor) : nullptr;
        const bool bLegacy = (Obj && Obj->GetClass()->GetName() == TEXT("NodeShuffleResourceNode"));
        const bool bResult = Comp ? Comp->bForceAccept : bLegacy;
        // ISOURNODE diag: show WHY a real-class node does/doesn't get force-accepted — whether the
        // component was found and its bForceAccept — so we can pin the real-vanilla-node rejection.
        // fu1diag-1 (FU1 §6.4): PER-ACTOR cap (was a flat 50/process cap — a second attempt, or a
        // second tool, on an already-capped actor went invisible for the rest of the session, exactly
        // the gap FU1's test matrix needs closed). FObjectKey-keyed, session lifetime; TMap only grows
        // while diagnostics is on (matches the original zero-cost-when-off profile).
        static constexpr int32 IsOurNodeMaxLogsPerActor = 12;
        static TMap<FObjectKey, int32> sIsOurNodeLogCounts;
        if (GNodeShuffleDiagnosticsEnabled)
        {
            int32& LogCount = sIsOurNodeLogCounts.FindOrAdd(FObjectKey(Obj));
            if (LogCount < IsOurNodeMaxLogsPerActor)
            {
                LogCount++;
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("ISOURNODE obj='%s' class='%s' compFound=%d bForceAccept=%d legacy=%d -> result=%d"),
                    Obj ? *Obj->GetName() : TEXT("<null>"), Obj ? *Obj->GetClass()->GetName() : TEXT("<null>"),
                    Comp ? 1 : 0, Comp ? (Comp->bForceAccept ? 1 : 0) : -1, bLegacy ? 1 : 0, bResult ? 1 : 0);
            }
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
            // redesign-24 (Packet E): the blanket override used to fire for ANY extractor on ANY of our
            // non-gas nodes, regardless of what the CALLING extractor's own mRestrictToNodeType demands —
            // so a mod's SPECIAL extractor (AlkaLib's Reactive Ore Extractor Mk.2, restricted to its own
            // Lithium/Alkali node class) got waved through onto an ordinary node (e.g. coal) it was never
            // meant to accept. Only waive the native IsA(...) rejection for a GENERIC extractor (unset
            // restriction, or the one vanilla class every ordinary Miner/Pump restricts to) — the case
            // this hook was built for (redesign-23: our node failing IsA(BP_ResourceNode_C)). Computed
            // UNCONDITIONALLY (behavior) — only the log below is diagnostics-gated.
            FString RestrictName;
            // Only pay for the name when it will actually be printed -- GetName() returns FString BY
            // VALUE (a heap allocation) and these hooks run per hologram tick. The RETURN VALUE is
            // unaffected by the out-param, so bGeneric (the decision) stays diagnostics-independent.
            const bool bGeneric = FNodeShuffleModule::IsGenericExtractorRestriction(
                Self, GNodeShuffleDiagnosticsEnabled ? &RestrictName : nullptr);
            // fu1diag-1 (FU1 §6.4) / redesign-24 (Packet E): keyed per (actor, extractor build class) now,
            // not per-actor — the old per-actor dedup let the FIRST extractor tried on a node (e.g. a
            // Mk8 Miner) consume the log slot for every other extractor later tried on the SAME node
            // (e.g. the Reactive Ore Extractor), making exactly this bug unprovable from the log.
            static FNodeShuffleActorExtractorLoggedSet sLoggedActorExtractor;
            const UObject* Obj = resource.GetObject();
            const UClass* BuildClass = Self ? Self->GetBuildClass().Get() : nullptr;
            // '=' initialization (not brace-init, not bare parens): brace-init's comma is just as
            // unprotected from the macro's paren-only nesting tracker as the earlier TMap<K, V> trap
            // (SUBSCRIBE_UOBJECT_METHOD only tracks ROUND parens); plain LogKey(a, b) parens hit the
            // "most vexing parse" (reads as a function declaration). This form keeps the only bare
            // comma inside a round-paren constructor call, which the macro splitter DOES track.
            const FNodeShuffleActorExtractorKey LogKey = FNodeShuffleActorExtractorKey(FObjectKey(Obj), FObjectKey(BuildClass));
            if (GNodeShuffleDiagnosticsEnabled && Obj && !sLoggedActorExtractor.Contains(LogKey))
            {
                sLoggedActorExtractor.Add(LogKey);
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("HOLOGRAMHOOK FORCE-ACCEPT-EVAL IsAllowedOnResource obj='%s' extractor='%s' restrict='%s' generic=%d -> %s"),
                    *Obj->GetName(), BuildClass ? *BuildClass->GetName() : TEXT("<none>"),
                    *RestrictName, bGeneric ? 1 : 0, bGeneric ? TEXT("OVERRIDE->true") : TEXT("native (no override)"));
            }
            if (bGeneric)
            {
                Scope.Override(true); // accept our node; skip the BP_ResourceNode_C IsA() rejection -- GENERIC extractor only
            }
            // else: fall through -- Scope is never called, so the ORIGINAL (native) IsAllowedOnResource
            // runs and correctly rejects a SPECIAL extractor whose own restriction this node doesn't satisfy.
        }
    });
    SUBSCRIBE_UOBJECT_METHOD(AFGResourceExtractorHologram, CanOccupyResource,
        [IsOurNode](auto& Scope, const AFGResourceExtractorHologram* Self,
                    const TScriptInterface<IFGExtractableResourceInterface>& resource)
    {
        if (IsOurNode(resource) && !NodeShuffleIsFrackingExtractor(Self)) // never force-accept fracking (crash)
        {
            // redesign-24 (Packet E): mirrors the IsAllowedOnResource hook above — see its comment.
            FString RestrictName;
            // Only pay for the name when it will actually be printed -- GetName() returns FString BY
            // VALUE (a heap allocation) and these hooks run per hologram tick. The RETURN VALUE is
            // unaffected by the out-param, so bGeneric (the decision) stays diagnostics-independent.
            const bool bGeneric = FNodeShuffleModule::IsGenericExtractorRestriction(
                Self, GNodeShuffleDiagnosticsEnabled ? &RestrictName : nullptr);
            static FNodeShuffleActorExtractorLoggedSet sLoggedActorExtractor;
            const UObject* Obj = resource.GetObject();
            const UClass* BuildClass = Self ? Self->GetBuildClass().Get() : nullptr;
            // '=' initialization (not brace-init, not bare parens): brace-init's comma is just as
            // unprotected from the macro's paren-only nesting tracker as the earlier TMap<K, V> trap
            // (SUBSCRIBE_UOBJECT_METHOD only tracks ROUND parens); plain LogKey(a, b) parens hit the
            // "most vexing parse" (reads as a function declaration). This form keeps the only bare
            // comma inside a round-paren constructor call, which the macro splitter DOES track.
            const FNodeShuffleActorExtractorKey LogKey = FNodeShuffleActorExtractorKey(FObjectKey(Obj), FObjectKey(BuildClass));
            if (GNodeShuffleDiagnosticsEnabled && Obj && !sLoggedActorExtractor.Contains(LogKey))
            {
                sLoggedActorExtractor.Add(LogKey);
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("HOLOGRAMHOOK FORCE-ACCEPT-EVAL CanOccupyResource obj='%s' extractor='%s' restrict='%s' generic=%d -> %s"),
                    *Obj->GetName(), BuildClass ? *BuildClass->GetName() : TEXT("<none>"),
                    *RestrictName, bGeneric ? 1 : 0, bGeneric ? TEXT("OVERRIDE->true") : TEXT("native (no override)"));
            }
            if (bGeneric)
            {
                Scope.Override(true); // GENERIC extractor only -- see IsAllowedOnResource hook above
            }
            // else: fall through -- native CanOccupyResource runs and correctly rejects a SPECIAL
            // extractor whose own restriction this node doesn't satisfy.
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
                // D2-A (fu1diag-1, FU1 §4.3): was a legacy-name-ONLY check — since the real-class
                // redesign it named EVERY one of our own spawned nodes "foreign" (contaminating this
                // census, not just going quiet — see FU1 §4.3's audience-count breakage). The shared
                // robust predicate now excludes ours from the foreign census entirely; ours=%d on the
                // line is a self-verifying field (should always read 0 here — a 1 would mean this gate
                // broke).
                // FU1v1-F1 (verbatim, review warning): short-circuit the predicate itself, not just the
                // log — NodeShuffleIsOursForDiag is a component-list walk + FString compare that ran on
                // every kept node in every cluster EVEN WITH DIAGNOSTICS OFF. Printed ours= value under
                // diag-on is identical either way.
                const bool bOursNode = GNodeShuffleDiagnosticsEnabled && NodeShuffleIsOursForDiag(N, nullptr);
                if (GNodeShuffleDiagnosticsEnabled && !bOursNode)
                {
                    static TSet<FString> LoggedForeign;
                    const FString Key = N->GetName();
                    if (!LoggedForeign.Contains(Key))
                    {
                        LoggedForeign.Add(Key);
                        UE_LOG(LogNodeShuffle, Display,
                            TEXT("SCANDIAG KEPT-FOREIGN node='%s' class='%s' loc=%s hidden=%d coll=%d res='%s' ours=%d"),
                            *Key, *N->GetClass()->GetName(), *N->GetActorLocation().ToCompactString(),
                            N->IsHidden() ? 1 : 0, N->GetActorEnableCollision() ? 1 : 0,
                            cluster.ResourceDescriptor.Get() ? *cluster.ResourceDescriptor.Get()->GetName() : TEXT("<null>"),
                            bOursNode ? 1 : 0);
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
