#include "NodeShuffleVetoKBFL.h"

#include "NodeShuffle.h"
#include "NodeShuffleDestroyerVetoRequirement.h"
#include "Subsystems/HelperClasses/KBFLCDOCallRequirement.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/World.h"
#include "TimerManager.h" // T58: the per-world VETOCENSUS timers (FTimerManager/FTimerDelegate/FTimerHandle)
#include "UObject/TopLevelAssetPath.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

// coexist-veto-1 ARM PASS. Re-run at EVERY world init (idempotent): KBFL's world CDO subsystem
// re-instantiates each asset's transient requirement list from mRequirements at every world's
// Start() (KBFLCDOOverwriteBase::LoadRequirements, driven by OnWorldBeginPlay -> OnWorldPostInit ->
// ApplyAllWorldCDOOverwrites), so a class prepended into mRequirements BEFORE that broadcast — our
// caller runs during actor BeginPlay, which precedes it — is live for the whole world, including the
// initial existing-actors destroy sweep. mRequirements edits are in-memory only (assets are never
// re-saved), so re-arming per world also covers any asset GC'd + reloaded between worlds.

namespace
{
    UClass* ResolveClassByPath(const TCHAR* Path)
    {
        UClass* Cls = FindObject<UClass>(nullptr, Path);
        if (!Cls)
        {
            Cls = LoadObject<UClass>(nullptr, Path);
        }
        return Cls;
    }

    // Reads a TArray<TSubclassOf<AActor>> UPROPERTY via reflection. Reflection (not a header stub) is
    // deliberate ABI minimization: the ONLY KBFL class we compile against is the requirement base;
    // the listener/destroyer classes and their target arrays are touched purely by name.
    bool ReadTargetClasses(UObject* Asset, const TCHAR* PropName, TArray<UClass*>& OutClasses)
    {
        FArrayProperty* Prop = FindFProperty<FArrayProperty>(Asset->GetClass(), PropName);
        FObjectPropertyBase* Inner = Prop ? CastField<FObjectPropertyBase>(Prop->Inner) : nullptr;
        if (!Inner)
        {
            return false;
        }
        FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Asset));
        for (int32 i = 0; i < Helper.Num(); ++i)
        {
            OutClasses.Add(Cast<UClass>(Inner->GetObjectPropertyValue(Helper.GetRawPtr(i))));
        }
        return true;
    }

    // The per-world arm entry point, registered with the main module by StartupModule below.
    void NodeShuffleVetoArmForWorld(UWorld* World)
    {
        // Fresh world session: "this session" veto counters restart. T58 also LATCHES the foreign-node
        // protection policy here — one read of NodeShuffle.ProtectForeignNodes for the whole world.
        const bool bProtectForeign = FNodeShuffleModule::IsForeignNodeProtectionEnabled();
        UNodeShuffleDestroyerVetoRequirement::ResetSessionCounters(bProtectForeign);
        UNodeShuffleDestroyerVetoRequirement::ResetForeignProtectAssets(); // T58 F1: rebuilt by this pass
        // Announced ONLY when the amendment is active. With NodeShuffle.ProtectForeignNodes=0 this
        // module must emit not one line the pre-T58 build did not — the OFF state's only new evidence
        // is the VETOCENSUS line, which reports protectCvar=0 honestly.
        if (bProtectForeign)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("veto: NodeShuffle.ProtectForeignNodes=1 latched for this world — at this hook, ")
                TEXT("requirement chains of node-sweeping KBFL assets are short-circuited for resource ")
                TEXT("nodes belonging to OTHER mods (T58). A stock node is unaffected unless NodeShuffle ")
                TEXT("itself retyped it to a modded resource (well retype writes mResourceClassOverride, ")
                TEXT("which GetResourceClass reads). Set it to 0 for the pre-T58 behaviour on a save that ")
                TEXT("has never run with it on."));
        }

        // ---- (a) ABI guard. StaticClass() here resolves through our import table into the REAL
        // installed KBFL DLL, so GetPropertiesSize() is the RUNTIME layout size of the base class;
        // sizeof() is the layout we compiled against (the verbatim header stub). A mismatch means the
        // installed KBFL's class layout drifted from the version NodeShuffle was built against —
        // instantiating our subclass would be undefined behavior, so arming aborts. (Residual risk a
        // size check cannot see: pure virtual-table reordering with identical property size.)
        const int32 RuntimeSize = UKBFLCDOCallRequirement::StaticClass()->GetPropertiesSize();
        const int32 CompiledSize = static_cast<int32>(sizeof(UKBFLCDOCallRequirement));
        if (RuntimeSize != CompiledSize)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("veto: KBFL class layout differs from the version NodeShuffle was built against ")
                TEXT("(runtime %d bytes vs compiled %d) — veto disabled, tombstone fallback remains active"),
                RuntimeSize, CompiledSize);
            return;
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("veto: ABI guard passed (UKBFLCDOCallRequirement = %d bytes); arming for world '%s'"),
            CompiledSize, *GetNameSafe(World));

        // ---- (b) resolve the destroyer/listener asset base classes + FGResourceNodeBase by path.
        // (FGResourceNodeBase by path too — this module intentionally has no FactoryGame dependency.)
        UClass* ListenerClass = ResolveClassByPath(TEXT("/Script/KBFL.KBFLWorldCDOActorListener"));
        UClass* DestroyerClass = ResolveClassByPath(TEXT("/Script/KBFL.KBFLWorldCDOActorDestroyer"));
        UClass* NodeBaseClass = ResolveClassByPath(TEXT("/Script/FactoryGame.FGResourceNodeBase"));
        if (!NodeBaseClass)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("veto: FGResourceNodeBase class not resolvable — veto disabled this session"));
            return;
        }
        if (!ListenerClass && !DestroyerClass)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("veto: KBFL is loaded but has no actor listener/destroyer classes (unexpected version?) — idle"));
            return;
        }

        // Collect candidate assets: already-loaded instances of either class (or subclasses)...
        TArray<UObject*> Candidates;
        if (ListenerClass)
        {
            GetObjectsOfClass(ListenerClass, Candidates, /*bIncludeDerivedClasses=*/true);
        }
        if (DestroyerClass)
        {
            TArray<UObject*> DestroyerObjects;
            GetObjectsOfClass(DestroyerClass, DestroyerObjects, /*bIncludeDerivedClasses=*/true);
            Candidates.Append(DestroyerObjects);
        }
        // ...plus not-yet-loaded assets via the asset registry (LoadObject'd here). In practice KBFL's
        // own game-instance subsystem loads and hard-references every CDO-overwrite asset at startup,
        // so this pass usually finds nothing new — belt and braces for lazy-mounted content.
        IAssetRegistry& AssetRegistry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        TArray<FAssetData> AssetDataList;
        if (ListenerClass)
        {
            AssetRegistry.GetAssetsByClass(
                FTopLevelAssetPath(TEXT("/Script/KBFL"), TEXT("KBFLWorldCDOActorListener")),
                AssetDataList, /*bSearchSubClasses=*/true);
        }
        if (DestroyerClass)
        {
            AssetRegistry.GetAssetsByClass(
                FTopLevelAssetPath(TEXT("/Script/KBFL"), TEXT("KBFLWorldCDOActorDestroyer")),
                AssetDataList, /*bSearchSubClasses=*/true);
        }
        for (const FAssetData& AssetData : AssetDataList)
        {
            if (UObject* Loaded = AssetData.GetAsset())
            {
                Candidates.Add(Loaded);
            }
        }

        TSet<UObject*> Seen;
        int32 ArmedCount = 0;
        for (UObject* Asset : Candidates)
        {
            if (!IsValid(Asset) || Seen.Contains(Asset))
            {
                continue;
            }
            Seen.Add(Asset);
            UClass* AssetClass = Asset->GetClass();

            // ---- (c) node-relevance filter: read the asset's target-class array by name and keep the
            // asset only when a target overlaps FGResourceNodeBase (its sub- OR superclass — a
            // destroyer targeting a node subclass, the base itself, or something as broad as AActor).
            const bool bIsDestroyer = DestroyerClass && AssetClass->IsChildOf(DestroyerClass);
            const TCHAR* TargetsPropName =
                bIsDestroyer ? TEXT("mActorClassesToDestroy") : TEXT("mActorClassesToListenFor");
            TArray<UClass*> TargetClasses;
            if (!ReadTargetClasses(Asset, TargetsPropName, TargetClasses))
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("veto: asset '%s' class=%s has no readable %s — skipped"),
                    *Asset->GetPathName(), *AssetClass->GetName(), TargetsPropName);
                continue;
            }
            FString TargetList;
            bool bNodeRelevant = false;
            // T58 F1 + R2: which assets may offer FOREIGN-node protection.
            // ACCEPTED SET, exactly: an asset with a target class that IS `FGResourceNodeBase` —
            // nothing narrower, nothing wider. Measured shape of the real node sweep: SF+'s
            // ActorListner_ResearchNodeRemover has targets=[FGResourceNodeBase].
            //   - a strict SUBCLASS (RefinedPower's RPWaterTurbineNode) is that mod's own handler for
            //     its own nodes  -> rejected (F1's measured collateral);
            //   - a strict ANCESTOR (AActor, AFGStaticReplicatedActor, UObject-level) is a generic
            //     actor tracker that happens to overlap nodes -> rejected as TOO BROAD (R2): the arm
            //     pass's own comment already notes assets "as broad as AActor" get armed, and
            //     short-circuiting such a listener for every foreign node is F1's failure again.
            // An unrecognised FUTURE listener therefore joins ONLY on an exact FGResourceNodeBase
            // target; anything else is absent from the set and can never be foreign-protected. The
            // veto for our OWN managed nodes is unchanged and still applies on every armed asset.
            bool bBroadNodeTarget = false;
            bool bAncestorTargetRejected = false;
            for (UClass* TargetClass : TargetClasses)
            {
                TargetList += (TargetClass ? TargetClass->GetName() : FString(TEXT("<null>"))) + TEXT(" ");
                if (TargetClass
                    && (TargetClass->IsChildOf(NodeBaseClass) || NodeBaseClass->IsChildOf(TargetClass)))
                {
                    bNodeRelevant = true;
                }
                if (TargetClass == NodeBaseClass) { bBroadNodeTarget = true; }
                else if (TargetClass && NodeBaseClass->IsChildOf(TargetClass)) { bAncestorTargetRejected = true; }
            }
            TargetList.TrimEndInline();
            if (!bNodeRelevant)
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("veto: asset '%s' class=%s targets=[%s] — not node-relevant, untouched"),
                    *Asset->GetPathName(), *AssetClass->GetName(), *TargetList);
                continue;
            }

            // knowledge-1 item 6: cooperative-by-default for SUBCLASSED listeners. The destroy risk
            // the veto exists for lives in the STOCK UKBFLWorldCDOActorListener pattern (behavior in
            // config-driven requirement BPs whose DeferedCall performs the destroy — SF+'s
            // ResearchNodeRemover). A listener SUBCLASS is bespoke cooperative C++ (e.g. SF+'s
            // SFPResourceNodeMaterialListener, a material mapper) — vetoing it blocked processing of
            // all 399 managed nodes with zero benefit. So: prepend on exact-class listeners only;
            // DESTROYERS (any subclass) stay unconditionally vetoed — destruction is their whole
            // contract. Residual: a hypothetically DESTRUCTIVE subclassed listener would slip this
            // filter, but the external-destroy tombstone backstop (coexist-1) still contains it.
            if (!bIsDestroyer && ListenerClass && AssetClass != ListenerClass)
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("veto: asset '%s' class=%s — subclassed listener (cooperative), not vetoed"),
                    *Asset->GetPathName(), *AssetClass->GetName());
                continue;
            }

            // ---- (d) PREPEND our requirement class at index 0 of the PUBLIC mRequirements array
            // (reflection). Index 0 (not append) so KBFL's short-circuiting Requirements_IsMet never
            // reaches the asset's own — side-effectful — requirement code for a vetoed node. Nothing
            // else on the asset is touched: not bEnabled, not the target arrays (rule (e)).
            FArrayProperty* ReqProp = FindFProperty<FArrayProperty>(AssetClass, TEXT("mRequirements"));
            FObjectPropertyBase* ReqInner = ReqProp ? CastField<FObjectPropertyBase>(ReqProp->Inner) : nullptr;
            if (!ReqInner)
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("veto: asset '%s' has no readable mRequirements array — skipped"), *Asset->GetPathName());
                continue;
            }
            UClass* OurClass = UNodeShuffleDestroyerVetoRequirement::StaticClass();
            FScriptArrayHelper Requirements(ReqProp, ReqProp->ContainerPtrToValuePtr<void>(Asset));
            const int32 CountBefore = Requirements.Num();
            bool bAlreadyPresent = false;
            for (int32 i = 0; i < Requirements.Num(); ++i)
            {
                if (ReqInner->GetObjectPropertyValue(Requirements.GetRawPtr(i)) == OurClass)
                {
                    bAlreadyPresent = true;
                    break;
                }
            }
            if (!bAlreadyPresent)
            {
                Requirements.InsertValues(0, 1);
                ReqInner->SetObjectPropertyValue(Requirements.GetRawPtr(0), OurClass);
            }
            const int32 CountAfter = Requirements.Num();

            // Late-arm safety net: if this asset's TRANSIENT instance cache (mCachedRequirements) was
            // already built for this world — i.e. KBFL's LoadRequirements ran before our arm, a
            // non-standard ordering — the class prepend alone would only take effect NEXT world. Inject
            // a live instance at the cache's front too (mirroring LoadRequirements' outer/flags; our
            // instance never reads mSubsystem, so leaving it unset is safe: the base GetWorld() just
            // returns null and DispatchDeferedCall no-ops).
            if (FArrayProperty* CachedProp = FindFProperty<FArrayProperty>(AssetClass, TEXT("mCachedRequirements")))
            {
                if (FObjectPropertyBase* CachedInner = CastField<FObjectPropertyBase>(CachedProp->Inner))
                {
                    FScriptArrayHelper Cached(CachedProp, CachedProp->ContainerPtrToValuePtr<void>(Asset));
                    if (Cached.Num() > 0)
                    {
                        bool bInstancePresent = false;
                        for (int32 i = 0; i < Cached.Num(); ++i)
                        {
                            UObject* Instance = CachedInner->GetObjectPropertyValue(Cached.GetRawPtr(i));
                            if (Instance && Instance->IsA(OurClass))
                            {
                                bInstancePresent = true;
                                break;
                            }
                        }
                        if (!bInstancePresent)
                        {
                            UObject* NewInstance =
                                NewObject<UObject>(Asset, OurClass, NAME_None, RF_Public | RF_Transactional);
                            // Mirror KBFL's own LoadRequirements construction, which stamps each
                            // instance's mSubsystem from the asset's mSubsystem. Both reads/writes go
                            // through reflection: UKBFLCDOOverwriteBase is declaration-only here (the
                            // deliberate one-class stub surface), and the instance's
                            // TObjectPtr<UKBFLContentCDOHelperSubsystem> member cannot be assigned in
                            // C++ without the pointee's complete type — the runtime FProperty (with
                            // the REAL KBFL's offset) sidesteps both while staying exact.
                            if (FObjectPropertyBase* AssetSubsystemProp =
                                    FindFProperty<FObjectPropertyBase>(AssetClass, TEXT("mSubsystem")))
                            {
                                UObject* SubsystemObj = AssetSubsystemProp->GetObjectPropertyValue(
                                    AssetSubsystemProp->ContainerPtrToValuePtr<void>(Asset));
                                if (FObjectPropertyBase* InstanceSubsystemProp =
                                        FindFProperty<FObjectPropertyBase>(OurClass, TEXT("mSubsystem")))
                                {
                                    InstanceSubsystemProp->SetObjectPropertyValue(
                                        InstanceSubsystemProp->ContainerPtrToValuePtr<void>(NewInstance),
                                        SubsystemObj);
                                }
                            }
                            Cached.InsertValues(0, 1);
                            CachedInner->SetObjectPropertyValue(Cached.GetRawPtr(0), NewInstance);
                            UE_LOG(LogNodeShuffle, Display,
                                TEXT("veto: late-arm — live requirement instance injected into already-built cache of '%s'"),
                                *Asset->GetPathName());
                        }
                    }
                }
            }

            // T58 F1: declare the foreign-protect set. Done for the already-present case too, because
            // re-arming the same asset in a new world must restore the same set.
            if (bBroadNodeTarget)
            {
                UNodeShuffleDestroyerVetoRequirement::AddForeignProtectAsset(Asset);
            }

            UE_LOG(LogNodeShuffle, Display,
                TEXT("veto: asset '%s' class=%s targets=[%s] broadNodeTarget=%d ancestorTargetRejected=%d requirements %d -> %d (%s)"),
                *Asset->GetPathName(), *AssetClass->GetName(), *TargetList, bBroadNodeTarget ? 1 : 0,
                bAncestorTargetRejected ? 1 : 0,
                CountBefore, CountAfter, bAlreadyPresent ? TEXT("already present") : TEXT("prepended"));
            ArmedCount++;
        }

        // T58: the census denominator — how many assets we are actually inside. Recorded whatever the
        // count, INCLUDING zero, so "0 foreign destroy attempts seen" can be told apart from "we were
        // never in the chain that destroys them".
        UNodeShuffleDestroyerVetoRequirement::SetArmedAssetCount(ArmedCount);

        if (ArmedCount > 0)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("veto: armed on %d asset(s) — destroys of NodeShuffle-managed nodes will be vetoed"),
                ArmedCount);
        }
        else
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("veto: armed, 0 node-destroyer assets found — idle"));
        }

        // ---- T58: schedule the per-world census. Two one-shot world timers, never a repeating one:
        // the sweep this measures lands ~1 s after world init, so T+30 s captures it with margin, and
        // T+300 s exists to expose a LATER wave. F3: the second line prints only when foreignSeen moved
        // after T+30 s — that is all it reports; it does NOT identify a retry loop, and nothing here
        // tests for one. World timers die with the world; the callbacks touch only module statics.
        if (World)
        {
            FTimerHandle EarlyHandle;
            FTimerHandle LateHandle;
            World->GetTimerManager().SetTimer(EarlyHandle, FTimerDelegate::CreateLambda([]()
            {
                UNodeShuffleDestroyerVetoRequirement::EmitForeignCensus(TEXT("T+30s"));
            }), 30.0f, /*bLoop=*/false);
            World->GetTimerManager().SetTimer(LateHandle, FTimerDelegate::CreateLambda([]()
            {
                UNodeShuffleDestroyerVetoRequirement::EmitForeignCensus(TEXT("T+300s"));
            }), 300.0f, /*bLoop=*/false);
        }
        else
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("veto: armed with a null world — the VETOCENSUS timers were not scheduled this session"));
        }
    }
}

// T58 R1: called by the main module when a world initialises with NodeShuffle.DestroyerVeto=0 after
// this module has already armed earlier in the process. Our requirement instance stays in the KBFL
// chain (the prepend is not undone), so the only way OFF can mean off is to clear the session state
// it reads. Managed-node vetoing is deliberately NOT changed here — that is the pre-T58 behaviour of
// a stale prepend and is outside T58's surface.
static void NodeShuffleVetoDisarmSessionState()
{
    UNodeShuffleDestroyerVetoRequirement::ResetSessionCounters(/*bProtectForeignNodes=*/false);
    UNodeShuffleDestroyerVetoRequirement::ResetForeignProtectAssets();
}

void FNodeShuffleVetoKBFLModule::StartupModule()
{
    // Hand the main module our per-world arm entry point. This runs synchronously inside the main
    // module's LoadModulePtr call, so the pointer is set before ArmDestroyerVetoIfEnabled proceeds.
    FNodeShuffleModule::SetKBFLVetoArmFunction(&NodeShuffleVetoArmForWorld);
    FNodeShuffleModule::SetKBFLVetoDisarmFunction(&NodeShuffleVetoDisarmSessionState);
    UE_LOG(LogNodeShuffle, Display,
        TEXT("veto: NodeShuffleVetoKBFL module loaded (KBFL present) — arm hook registered"));
}

void FNodeShuffleVetoKBFLModule::ShutdownModule()
{
    FNodeShuffleModule::SetKBFLVetoArmFunction(nullptr);
    FNodeShuffleModule::SetKBFLVetoDisarmFunction(nullptr);
}

IMPLEMENT_GAME_MODULE(FNodeShuffleVetoKBFLModule, NodeShuffleVetoKBFL);
