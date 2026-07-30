#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// coexist-veto-1: NODESHUFFLE_API-exported (engine idiom, cf. CoreGlobals.h) so the optional
// NodeShuffleVetoKBFL module logs under the SAME LogNodeShuffle category users already filter on.
NODESHUFFLE_API DECLARE_LOG_CATEGORY_EXTERN(LogNodeShuffle, Log, All);

// Packet F (ns-automatch, extractor-automatch): the result of evaluating ONE extractor building's
// OWN declared acceptance rules (mRestrictToNodeType / mAllowedResourceForms / mOnlyAllowCertainResources
// / mAllowedResources) against an optional node-side comparison target. Plain data, no engine calls of
// its own — a pure, form-agnostic value type so NodeShuffle.DumpExtractors (Packet F) and the
// auto-allow decision (Packet G) can both consume it without re-deriving the same fields. Deliberately
// says nothing about "solid" or "liquid" specifically -- it reasons only from whatever the extractor's
// OWN rules declare, which is what keeps it correct for oil/liquid extractors without ever naming them.
struct NODESHUFFLE_API FNodeShuffleExtractorAcceptance
{
    // False only when the caller passed a null extractor CDO -- every other field then holds its
    // vacuous "nothing to check against" default (mirrors DbgLogAcceptance's pre-existing null-Ext branch).
    bool bHasExtractorCdo = false;

    bool bHasRestriction = false;
    FString RestrictClassName = TEXT("<none>");
    FString RestrictClassPath = TEXT("<none>");
    // True when no NodeClass was supplied to compare (vacuous), OR NodeClass IsChildOf the restriction,
    // OR there is no restriction at all.
    bool bNodeIsA = true;

    // Raw declared form values (EResourceForm cast to int32; see FGItemDescriptor.h for the enum) --
    // empty means "unrestricted" (every form allowed), exactly like the native field's own semantics.
    TArray<int32> AllowedForms;
    FString AllowedFormsCsv; // "%d," per element -- preserves DbgLogAcceptance's existing log format
    bool bFormAllowed = true;

    bool bOnlyCertainResources = false;
    TArray<FString> AllowedResourcePaths; // full paths of each TSubclassOf<UFGResourceDescriptor> entry
    bool bResourceAllowed = true;

    // Packet G (ns-automatch) / Packet F cold review, finding: EvaluateExtractorAcceptance(Ext, nullptr,
    // -1, nullptr).AcceptsNatively() returns TRUE for every extractor, because ALL THREE defaults are
    // permissive when nothing is compared (bNodeIsA/bFormAllowed/bResourceAllowed default true). Packet
    // F's own dump-summary line uses exactly that vacuous shape ON PURPOSE (it prints an extractor's OWN
    // rules; it never calls AcceptsNatively() on the result). true only when a REAL NodeClass was passed
    // in -- the minimal, sufficient condition: analysed field-by-field, a null NodeResourceForm/
    // NodeResourceClass can only ever push a sub-check toward the SAFE (reject) direction, never toward
    // a false accept, but a null NodeClass makes bNodeIsA vacuously true even for a genuinely RESTRICTED
    // extractor -- that is the one input that can flip an otherwise-correct rejection into a false
    // accept. So this flag is keyed on NodeClass alone; see the report for the full four-case argument.
    bool bComparedAgainstNode = false;

    // ns-review-g G4: true only when the accept verdict was actually INFLUENCED by the node -- i.e. the
    // extractor declares at least one real restriction that the supplied node had to satisfy. An
    // extractor with mRestrictToNodeType unset AND an empty mAllowedResourceForms AND
    // !mOnlyAllowCertainResources accepts EVERY node vacuously; bComparedAgainstNode is true for it
    // (a real NodeClass was passed) but nothing about the node decided anything. An ALLOW decision must
    // require this flag; a descriptive dump must not.
    bool bDiscriminated = false;

    // The extractor's OWN, unhooked verdict for the node-side inputs supplied -- ANDs the three
    // sub-checks exactly like the native acceptance path does.
    //
    // ns-review-g G3 (corrected): the ensureMsgf below is a DEVELOPMENT-ONLY guard. In this project's
    // Shipping configuration DO_CHECK is 0, so ensureMsgf collapses to a bare condition evaluation --
    // no log, no report. MEASURED: the message text below is absent from a raw-byte scan of the
    // deployed Shipping DLL, and dumpbin shows no Ensure* import. So in the DLL the user actually
    // runs, this guard has ZERO effect; the real protection is CALL-SITE DISCIPLINE, audited here:
    //   - NodeShuffleExtractorDump.cpp:278 (match matrix) passes G.NodeClass, never null.
    //   - NodeShuffleAutoAllowExtractors.cpp:226 (the auto-allow decision) passes G.NodeClass, never null.
    //   - Packet F's descriptive EXTRACTOR summary line uses the vacuous shape but does NOT call this.
    // Keep the ensure for Editor/Development/PIE, but do not describe it as a shipping safety net.
    //
    // SECOND, SEPARATE TRAP -- bComparedAgainstNode does NOT cover it: a real NodeClass can still yield
    // a DEGENERATE comparison when the extractor declares no restrictions at all (mRestrictToNodeType
    // unset AND mAllowedResourceForms empty AND !mOnlyAllowCertainResources). All three sub-checks are
    // then vacuously true and this returns true for EVERY node. Any caller making an ALLOW decision
    // must additionally require a positive, node-specific signal -- see bDiscriminated below.
    bool AcceptsNatively() const
    {
        ensureMsgf(bComparedAgainstNode,
            TEXT("FNodeShuffleExtractorAcceptance::AcceptsNatively() called on the VACUOUS shape (no real ")
            TEXT("node compared -- NodeClass was null) -- this returns true for EVERY extractor by ")
            TEXT("default and must never back an allow/accept DECISION. The vacuous shape exists only so ")
            TEXT("a descriptive dump can print an extractor's OWN rules with no comparison target -- read ")
            TEXT("RestrictClassPath/AllowedFormsCsv/AllowedResourcePaths directly for that. Any caller ")
            TEXT("making a decision (e.g. auto-allow) MUST supply a real NodeClass."));
        return bNodeIsA && bFormAllowed && bResourceAllowed;
    }
};

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

    // redesign-24 (Packet E): classifies the CALLING extractor's own node-type restriction
    // (Hologram->mDefaultExtractor->mRestrictToNodeType -- both protected, both already friend-granted
    // via the SAME AccessTransformers entries DbgLogAcceptance uses; no new grant needed) as GENERIC
    // (unset, or the one vanilla node class every ordinary Miner/Pump restricts to) vs a SPECIAL,
    // narrower type a mod defines for its own resource (e.g. AlkaLib's Lithium/Alkali reactive-ore
    // node). The force-accept hooks below only waive the native class check for a GENERIC extractor --
    // a SPECIAL extractor's own restriction is left to run natively so it correctly rejects a node
    // that isn't its own type. Named static member function, not a lambda inside the hook -- a lambda
    // can't touch these protected members even inside a friended module (same constraint
    // DbgLogAcceptance documents). OutRestrictName is optional, for the hook's own diagnostic log.
    static bool IsGenericExtractorRestriction(const class AFGResourceExtractorHologram* Hologram, FString* OutRestrictName = nullptr);

    // Packet F (ns-automatch): the SAME acceptance predicate DbgLogAcceptance's ACCEPT-EXT block has
    // always computed (restrictToNodeType/nodeIsA, allowedForms/formAllowed, onlyCertain/resAllowed),
    // factored into one reusable, named static member function -- required (not just nice-to-have)
    // because mRestrictToNodeType/mAllowedResourceForms/mOnlyAllowCertainResources/mAllowedResources are
    // PROTECTED on AFGBuildableResourceExtractorBase and only a MEMBER of the friended FNodeShuffleModule
    // class can read them (a free function or lambda in another .cpp of this same module cannot, even
    // though friendship is module-wide in spirit -- C++ friendship is class-to-class, not module-to-
    // module). DbgLogAcceptance now calls this instead of inlining the same checks; NodeShuffle.DumpExtractors
    // (this packet) calls it per (extractor, node-class) pair for the match matrix; Packet G will call it
    // to decide auto-allowing. NodeClass/NodeResourceClass may be null (vacuous "nothing to compare",
    // matching DbgLogAcceptance's existing null-safety); NodeResourceForm is a raw EResourceForm int32
    // (pass a value that cannot appear in mAllowedResourceForms, e.g. -1, when no node form applies).
    static FNodeShuffleExtractorAcceptance EvaluateExtractorAcceptance(
        const class AFGBuildableResourceExtractorBase* Extractor,
        const class UClass* NodeClass,
        int32 NodeResourceForm,
        const class UClass* NodeResourceClass);

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
    // spawnrace-1: TRUE while NodeShuffle is synchronously inside SpawnActor for one of its own
    // resource-node actors (see FNodeShuffleSpawningScope below). WHY THIS EXISTS: KBFL's listener
    // binds World->AddOnActorSpawnedHandler, and that delegate fires INSIDE UWorld::SpawnActor —
    // before our spawn call returns and therefore before the post-spawn RegisterManagedNode above
    // can run. Registry-only checks lose that race for every node born mid-session (live evidence:
    // 429 destroys/session of respawned nodes while load-time nodes stayed fully protected).
    // During our synchronous game-thread SpawnActor the only actor reaching that delegate is ours,
    // so a scoped flag is a sound identity signal for the veto to honor alongside the registry.
    static bool IsSpawningManagedNode();

    // The optional NodeShuffleVetoKBFL module registers its per-world arm entry point here from its
    // StartupModule. Function-pointer indirection keeps the dependency arrow one-way (veto -> main):
    // this module has ZERO KBFL includes/links and never sees the veto module's headers.
    static void SetKBFLVetoArmFunction(void (*ArmFn)(class UWorld* World));
    // Called once per world init (subsystem BeginPlay, authority only): reads the
    // NodeShuffle.DestroyerVeto CVar, checks KBFL presence by module NAME, loads the veto module on
    // demand, and invokes its registered arm function for this world.
    static void ArmDestroyerVetoIfEnabled(class UWorld* World);

    // Packet G (ns-automatch): "allow an extractor if it natively accepts a node type NodeShuffle
    // manages" -- generates a machine-written KDataForge pack under DataForge/NodeShuffleAutoAllow/ that
    // appends any newly-qualifying extractor to SF+'s PDA_SFP_ExtractorList.mAllowedExtractors, so KDF
    // applies it on the NEXT boot (see the report for why runtime mutation of the live PDA array cannot
    // work: KAPI resolves/merges that list at GAME-INSTANCE INIT, ~270 ms after KDataForge's own
    // "Initial load finished", both of which complete long before any world (and therefore any
    // NodeShuffle-managed node) exists -- our subsystem starts at world load, structurally after the
    // window KAPI actually reads). Gated by NodeShuffle.AutoAllowExtractors (default ON in this dev
    // build; TODO(pre-release): move behind EnableExperimentalFeatures, default false, before public
    // release). Mirrors ANodeShuffleSubsystem::UnlockModdedScannerKnowledge()'s retry idiom: returns
    // false when a dependency (the recipe manager) is not ready yet and the caller should retry next
    // tick; true when the pass COMPLETED this tick (including "disabled", "SF+ not installed", and "ran
    // and wrote/cleared the generated pack") -- the caller latches on true only.
    static bool RunAutoAllowExtractorsIfEnabled(class UWorld* World);
    // Logs the CVar's configured value once, called from StartupModule (module load, before any world) --
    // "log the state once at startup" is a hard requirement so a user reading the log from boot alone
    // can tell whether this experimental pass is armed for the session.
    static void LogAutoAllowExtractorsState();
};

// spawnrace-1: RAII spawn-window guard for FNodeShuffleModule::IsSpawningManagedNode(). Construct
// one in a block IMMEDIATELY around a SpawnActor call for a NodeShuffle-owned resource-node actor
// (and only node actors — the KBFL listeners the veto arms target FGResourceNodeBase). Backed by a
// game-thread depth COUNTER, not a bool, so nested/reentrant spawn scopes stay correct; the
// ctor/dtor are a plain inc/dec pair (exception-agnostic — no cleanup beyond the decrement).
// Keep scopes tight: anything a wrapped spawn re-enters that itself spawns an unrelated
// FGResourceNodeBase actor would be shielded from KBFL for that one judgment too.
struct NODESHUFFLE_API FNodeShuffleSpawningScope
{
    FNodeShuffleSpawningScope();
    ~FNodeShuffleSpawningScope();
    FNodeShuffleSpawningScope(const FNodeShuffleSpawningScope&) = delete;
    FNodeShuffleSpawningScope& operator=(const FNodeShuffleSpawningScope&) = delete;
};
