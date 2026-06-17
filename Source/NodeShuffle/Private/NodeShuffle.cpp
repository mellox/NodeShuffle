#include "NodeShuffle.h"

#include "Patching/NativeHookManager.h"
#include "Hologram/FGResourceExtractorHologram.h"
#include "Buildables/FGBuildableResourceExtractorBase.h"
#include "Resources/FGResourceNodeBase.h"
#include "Resources/FGExtractableResourceInterface.h"
#include "UObject/ScriptInterface.h"
#include "Engine/HitResult.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY(LogNodeShuffle);

// Diagnostics gate. OFF by default; set from config (EnableDiagnostics) by the subsystem each
// ApplyLayout pass. Game-thread only (placement hooks + config read both run on the game thread),
// so a plain bool is sufficient. Gates the verbose HOLOGRAMHOOK logging ONLY — the Mk1 accept-hook
// behavior is never gated.
static bool GNodeShuffleDiagnosticsEnabled = false;
void FNodeShuffleModule::SetDiagnosticsEnabled(bool bEnabled) { GNodeShuffleDiagnosticsEnabled = bEnabled; }
bool FNodeShuffleModule::AreDiagnosticsEnabled() { return GNodeShuffleDiagnosticsEnabled; }

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

void FNodeShuffleModule::StartupModule()
{
    UE_LOG(LogNodeShuffle, Log, TEXT("NodeShuffle module loaded"));
    UE_LOG(LogNodeShuffle, Display, TEXT("===== NodeShuffle BUILD 2026-06-17-modded-relocate-1 LOADED ====="));

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
        const bool bOurs = HitActor && HitActor->GetClass()->GetName() == TEXT("NodeShuffleResourceNode");
        static TSet<FString> Logged;
        const FString Key = (HitActor ? HitActor->GetName() : TEXT("<null>"));
        if (!Logged.Contains(Key))
        {
            Logged.Add(Key);
            UE_LOG(LogNodeShuffle, Display, TEXT("HOLOGRAMHOOK TrySnapToActor -> %d | hitActor='%s' class='%s'%s"),
                r ? 1 : 0, HitActor ? *HitActor->GetName() : TEXT("<null>"),
                HitActor ? *HitActor->GetClass()->GetName() : TEXT("<null>"),
                bOurs ? TEXT("  <-- OUR NODE") : TEXT(""));
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
    auto IsOurNode = [](const TScriptInterface<IFGExtractableResourceInterface>& Resource) -> bool
    {
        const UObject* Obj = Resource.GetObject();
        return Obj && Obj->GetClass()->GetName() == TEXT("NodeShuffleResourceNode");
    };
    SUBSCRIBE_UOBJECT_METHOD(AFGResourceExtractorHologram, IsAllowedOnResource,
        [IsOurNode](auto& Scope, const AFGResourceExtractorHologram* Self,
                    const TScriptInterface<IFGExtractableResourceInterface>& resource)
    {
        if (IsOurNode(resource))
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
        if (IsOurNode(resource))
        {
            static bool bLoggedOnce = false;
            if (GNodeShuffleDiagnosticsEnabled && !bLoggedOnce) { bLoggedOnce = true;
                UE_LOG(LogNodeShuffle, Display, TEXT("HOLOGRAMHOOK FORCE-ACCEPT CanOccupyResource->true (our node)")); }
            Scope.Override(true); // ALWAYS active (the fix); only the log above is diagnostics-gated
        }
    });
#endif
}

IMPLEMENT_GAME_MODULE(FNodeShuffleModule, NodeShuffle);
