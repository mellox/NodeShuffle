// Packet F (ns-automatch, branch feature/extractor-automatch): `NodeShuffle.DumpExtractors` console
// command. THE DISCOVERY HALF of replacing a hardcoded SF+ extractor allow-list enumeration with one
// that generalises. Today's KDF compat pack hand-lists extractor classes; a mod can ship an extractor
// (AlkaLib's Reactive Ore Extractor Mk3, at time of writing) we never knew existed, so it never lands
// on the allow-list and cannot be built at all. This command makes the GAME tell us what is actually
// installed, with EXACT PATHS, so nothing is ever guessed again -- three boots were previously burned
// on wrong asset paths and one bad path aborts a whole KDF document.
//
// Log-only and side-effect-free, so -- SAME PRECEDENT as NodeShuffle.Here (NodeShuffle.cpp:29-45) --
// it is NOT gated behind EnableDiagnostics. It exists precisely so the user can report findings.
//
// FORM-AGNOSTIC BY DESIGN (mid-flight requirement, 2026-07-30): oil is a deliberate CONTROL CASE the
// user wants to observe through the general mechanism untouched. Nothing in this file names "oil",
// "liquid", "pump", or any oil-specific class. Every check reasons from the extractor's/node's OWN
// declared rules (FNodeShuffleModule::EvaluateExtractorAcceptance, in NodeShuffle.cpp) via a plain
// int32 form value; the one place this file mentions RF_LIQUID's numeric value (2) is a REPORTING
// filter that groups the SAME generic per-form data for a prominent log section (Ask #3) -- it makes
// no decision and shares no code path with anything oil-specific, because there isn't any.

#include "NodeShuffle.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Buildables/FGBuildableResourceExtractorBase.h"
#include "Resources/FGResourceNodeBase.h"
#include "Resources/FGResourceNode.h"
#include "NodeShuffleNodeComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/TopLevelAssetPath.h"
#include "UObject/UnrealType.h"

namespace
{
    // EResourceForm values (FGItemDescriptor.h): RF_INVALID=0, RF_SOLID=1, RF_LIQUID=2, RF_GAS=3. A
    // plain switch (not StaticEnum<EResourceForm>()->GetNameStringByValue) so this diagnostic adds no
    // new engine call beyond what EvaluateExtractorAcceptance already makes -- the values are a stable,
    // documented native enum, cited at the point of use rather than re-derived at runtime.
    const TCHAR* NodeShuffleFormName(int32 Form)
    {
        switch (Form)
        {
        case 0: return TEXT("Invalid");
        case 1: return TEXT("Solid");
        case 2: return TEXT("Liquid");
        case 3: return TEXT("Gas");
        default: return TEXT("Unknown");
        }
    }

    // ---- SF+ allow-list read (reflection only -- ZERO compile-time KAPI/SF+ dependency) ----
    struct FSfPlusAllowListReadout
    {
        bool bAssetFound = false;
        bool bPropertyReadable = false;
        FString PropertyInnerKind = TEXT("<unread>");
        TSet<const UClass*> AllowedExtractorClasses;
    };

    // Reads SF+'s `/SatisfactoryPlus/AssetDatas/Modules/PDA_SFP_ExtractorList.PDA_SFP_ExtractorList`
    // instance's `mAllowedExtractors` array by NAME via reflection. This packet's report documents why:
    // the exact UPROPERTY type of mAllowedExtractors has never been measured (flagged as an open
    // question in PacketA-review.md) -- CastField<FObjectPropertyBase> resolves it regardless of
    // whether the inner is FClassProperty (TSubclassOf), FObjectProperty, or FSoftClassProperty, because
    // GetObjectPropertyValue is virtual on FObjectPropertyBase (all three derive from it); this measures
    // which one it actually is via PropertyInnerKind, settling the question instead of assuming it.
    // READ-ONLY: this packet never writes mAllowedExtractors (Packet G does). Absent asset/property both
    // degrade to "unknown/no-sfplus" rather than false, so a vanilla (SF+-less) profile still works.
    FSfPlusAllowListReadout ReadSfPlusAllowList()
    {
        FSfPlusAllowListReadout Out;
        static const TCHAR* AssetPath = TEXT("/SatisfactoryPlus/AssetDatas/Modules/PDA_SFP_ExtractorList.PDA_SFP_ExtractorList");
        UObject* Asset = FindObject<UObject>(nullptr, AssetPath);
        if (!Asset) { Asset = LoadObject<UObject>(nullptr, AssetPath); }
        if (!Asset)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("DUMPEXTRACTORS: SF+ allow-list asset '%s' not found -- SF+ not installed (or its path changed). ")
                TEXT("Every extractor below will read sfPlusAllowList=<no-sfplus-installed>."), AssetPath);
            return Out;
        }
        Out.bAssetFound = true;

        FArrayProperty* Prop = FindFProperty<FArrayProperty>(Asset->GetClass(), TEXT("mAllowedExtractors"));
        FObjectPropertyBase* Inner = Prop ? CastField<FObjectPropertyBase>(Prop->Inner) : nullptr;
        Out.PropertyInnerKind = !Prop
            ? FString(TEXT("<mAllowedExtractors property not found on this asset>"))
            : (Inner ? Inner->GetClass()->GetName()
                     : FString::Printf(TEXT("<non-object-array inner: %s>"),
                           Prop->Inner ? *Prop->Inner->GetClass()->GetName() : TEXT("null")));
        if (!Inner)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("DUMPEXTRACTORS: SF+ allow-list asset '%s' found but mAllowedExtractors is unreadable (%s) -- ")
                TEXT("every extractor below will read sfPlusAllowList=<unreadable>."),
                AssetPath, *Out.PropertyInnerKind);
            return Out;
        }
        Out.bPropertyReadable = true;

        FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Asset));
        int32 UnresolvedEntries = 0;
        for (int32 i = 0; i < Helper.Num(); ++i)
        {
            if (const UClass* Cls = Cast<UClass>(Inner->GetObjectPropertyValue(Helper.GetRawPtr(i))))
            {
                Out.AllowedExtractorClasses.Add(Cls);
            }
            else
            {
                ++UnresolvedEntries;
            }
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("DUMPEXTRACTORS: SF+ allow-list read OK -- mAllowedExtractors inner property kind='%s' ")
            TEXT("(measures the open question flagged in PacketA-review.md): %d array entries, %d resolved ")
            TEXT("to a UClass, %d UNRESOLVED. ns-review-f F3: GetObjectPropertyValue does NOT force a load ")
            TEXT("-- if the inner kind is a SOFT class property, an unloaded entry reads as null here, so ")
            TEXT("any extractor it named will be reported sfPlusAllowList=NO even though SF+ allows it. A ")
            TEXT("nonzero UNRESOLVED count means every 'NO' below is unreliable."),
            *Out.PropertyInnerKind, Helper.Num(), Out.AllowedExtractorClasses.Num(), UnresolvedEntries);
        return Out;
    }

    // ---- extractor building-class discovery ----
    // Discovers EVERY resource-extractor building class this session can find, loaded or not. Two
    // passes, because TObjectIterator<UClass> alone only sees classes already resident in memory and
    // would silently under-report an installed-but-never-yet-loaded Blueprint extractor -- exactly the
    // failure mode this whole packet exists to eliminate. See the packet report for the full reasoning
    // and what this mechanism can still miss.
    void CollectExtractorClasses(TArray<UClass*>& OutClasses, int32& OutLoadedCount,
                                  int32& OutRegistryOnlyCount, int32& OutRegistryLoadFailures)
    {
        TSet<FString> Seen;

        // (1) Already-loaded classes (native + any Blueprint class already resident in memory).
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* Cls = *It;
            if (Cls == AFGBuildableResourceExtractorBase::StaticClass()) { continue; } // the abstract base itself
            if (Cls->HasAnyClassFlags(CLASS_Abstract)) { continue; }
            if (!Cls->IsChildOf(AFGBuildableResourceExtractorBase::StaticClass())) { continue; }
            const FString Path = Cls->GetPathName();
            if (!Seen.Contains(Path))
            {
                Seen.Add(Path);
                OutClasses.Add(Cls);
            }
        }
        OutLoadedCount = OutClasses.Num();

        // (2) Not-yet-loaded Blueprint classes via the asset registry's class-hierarchy cache (built
        // from the ParentClass tag every cooked Blueprint asset carries, matched against the native
        // hierarchy) -- the standard "find every subclass without loading it first" idiom, and the one
        // named in this packet's brief. FTopLevelAssetPath built the SAME literal way
        // NodeShuffleVetoKBFL.cpp already does for KBFL's classes (proven pattern, this module).
        IAssetRegistry& AssetRegistry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        TArray<FTopLevelAssetPath> BaseClassNames;
        BaseClassNames.Add(FTopLevelAssetPath(TEXT("/Script/FactoryGame"), TEXT("FGBuildableResourceExtractorBase")));
        TSet<FTopLevelAssetPath> Excluded;
        TSet<FTopLevelAssetPath> DerivedClassNames;
        AssetRegistry.GetDerivedClassNames(BaseClassNames, Excluded, DerivedClassNames);

        // ns-review-f F1: registry health probe -- the log must never let "found none" read the same as
        // "the registry could not answer". In a COOKED SHIPPING build the asset registry is never
        // scanned (ShouldSearchAllAssetsAtStart() is false for a non-editor game, so no gatherer is
        // constructed and OnContentPathMounted -- which fires when SML mounts a mod pak -- only records
        // the path and adds ZERO FAssetData). The only population route is the premade registry:
        // <Project>/AssetRegistry.bin plus, per plugin, <PluginBaseDir>/AssetRegistry.bin. MEASURED
        // 2026-07-30: no Satisfactory mod on this install ships that file, so mod Blueprint classes are
        // EXPECTED to be absent from pass 2 -- pass 1 (TObjectIterator) is what actually finds them.
        // GetDerivedClassNames still always returns the NATIVE subclasses, so a zero BLUEPRINT count --
        // not a zero total -- is the signal that the registry holds no Blueprint class data at all.
        int32 RegistryNativeNames = 0;
        int32 RegistryBlueprintNames = 0;
        for (const FTopLevelAssetPath& ClassPath : DerivedClassNames)
        {
            if (ClassPath.ToString().StartsWith(TEXT("/Script/"))) { ++RegistryNativeNames; }
            else { ++RegistryBlueprintNames; }
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("DUMPEXTRACTORS: REGISTRY HEALTH -- GetDerivedClassNames returned %d names (%d native ")
            TEXT("'/Script/', %d Blueprint). READ IT LIKE THIS: blueprint>0 = the registry answered, so an ")
            TEXT("extractor missing below really is not installed. blueprint=0 = the registry HAS NO ")
            TEXT("BLUEPRINT CLASS DATA in this build -- pass 2 answered NOTHING, everything below comes ")
            TEXT("from pass 1 (already-loaded classes) alone, and an absent mod extractor proves nothing."),
            DerivedClassNames.Num(), RegistryNativeNames, RegistryBlueprintNames);

        for (const FTopLevelAssetPath& ClassPath : DerivedClassNames)
        {
            const FString PathStr = ClassPath.ToString();
            if (Seen.Contains(PathStr)) { continue; }
            UClass* Cls = FindObject<UClass>(nullptr, *PathStr);
            if (!Cls) { Cls = LoadObject<UClass>(nullptr, *PathStr); } // deliberate: this command's whole job is exhaustive discovery
            if (Cls && !Cls->HasAnyClassFlags(CLASS_Abstract) && Cls->IsChildOf(AFGBuildableResourceExtractorBase::StaticClass()))
            {
                Seen.Add(PathStr);
                OutClasses.Add(Cls);
                ++OutRegistryOnlyCount;
            }
            else if (!Cls)
            {
                ++OutRegistryLoadFailures;
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("DUMPEXTRACTORS: asset registry named a derived class at '%s' but LoadObject FAILED -- ")
                    TEXT("MISSED, undercount by at least 1"), *PathStr);
            }
        }
    }

    // One (node class, resource class, form) group observed in the world. Grouped on all three -- NOT
    // class alone -- because the real-class-redesign relocates nodes AS THEIR ORIGINAL CLASS, so the one
    // generic vanilla class (BP_ResourceNode_C) legitimately hosts many different resources across
    // instances; collapsing to class-only would silently hide that variation from the match matrix for
    // any extractor whose mOnlyAllowCertainResources narrows by resource, not just by class/form.
    struct FNodeGroupInfo
    {
        UClass* NodeClass = nullptr;
        UClass* ResourceClass = nullptr; // may be null
        int32 Form = -1;
        int32 InstanceCount = 0;
        int32 ManagedCount = 0;    // FNodeShuffleModule::IsManagedSpawnedNode
        int32 RelocatedCount = 0;  // carries a UNodeShuffleNodeComponent
    };
}

// The console command. See the file-header comment for the "why" and the form-agnostic guarantee.
static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleDumpExtractorsCmd(
    TEXT("NodeShuffle.DumpExtractors"),
    TEXT("Log every resource-extractor building class + every resource-node group present in the world, ")
    TEXT("their own accept rules, SF+ allow-list membership, and the full extractor x node-group match ")
    TEXT("matrix (log-only, side-effect-free -- not gated behind EnableDiagnostics, same precedent as ")
    TEXT("NodeShuffle.Here)."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
    {
        if (!World)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("DUMPEXTRACTORS: no world (main menu / no session loaded?)"));
            return;
        }

        UE_LOG(LogNodeShuffle, Display, TEXT("===== DUMPEXTRACTORS BEGIN ====="));

        // ---- 1. SF+ allow-list ----
        const FSfPlusAllowListReadout AllowList = ReadSfPlusAllowList();

        // ---- 2. Extractor building classes ----
        TArray<UClass*> ExtractorClasses;
        int32 LoadedCount = 0, RegistryOnlyCount = 0, RegistryLoadFailures = 0;
        CollectExtractorClasses(ExtractorClasses, LoadedCount, RegistryOnlyCount, RegistryLoadFailures);
        UE_LOG(LogNodeShuffle, Display,
            TEXT("DUMPEXTRACTORS: extractor classes found: %d total (%d already-loaded via TObjectIterator<UClass>, ")
            TEXT("%d additional via IAssetRegistry::GetDerivedClassNames, %d registry-named classes FAILED to load ")
            TEXT("[undercount if >0])"),
            ExtractorClasses.Num(), LoadedCount, RegistryOnlyCount, RegistryLoadFailures);
        UE_LOG(LogNodeShuffle, Display,
            TEXT("DUMPEXTRACTORS: enumeration LIMITATION -- in a cooked Shipping build the asset registry ")
            TEXT("is populated ONLY from premade AssetRegistry.bin files (the game's own, plus one per ")
            TEXT("plugin that ships one). Satisfactory mods do NOT ship one, so pass 2 is expected to add ")
            TEXT("nothing for modded extractors and pass 1 is what finds them. A mod extractor is therefore ")
            TEXT("only guaranteed to appear below if its class is already LOADED (it is, once you have a ")
            TEXT("save open with its recipes available). If a known-installed mod's extractor is absent, ")
            TEXT("report which mod/class AND the REGISTRY HEALTH line above."));

        TArray<FString> LiquidCapableExtractorPaths; // reporting-only grouping, see file header
        for (UClass* ExtCls : ExtractorClasses)
        {
            const AFGBuildableResourceExtractorBase* Ext = ExtCls->GetDefaultObject<AFGBuildableResourceExtractorBase>();
            if (!Ext)
            {
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("DUMPEXTRACTORS: extractor class '%s' produced no CDO -- SKIPPED from both the ")
                    TEXT("EXTRACTOR list and the match matrix (undercount by 1)"), *ExtCls->GetPathName());
                continue;
            }
            const FNodeShuffleExtractorAcceptance A =
                FNodeShuffleModule::EvaluateExtractorAcceptance(Ext, nullptr, -1, nullptr);
            const bool bOnAllowList = AllowList.AllowedExtractorClasses.Contains(ExtCls);
            const FString AllowedResStr = A.AllowedResourcePaths.Num()
                ? FString::Join(A.AllowedResourcePaths, TEXT(";"))
                : TEXT("<any>");
            const TCHAR* AllowListState = !AllowList.bAssetFound
                ? TEXT("<no-sfplus-installed>")
                : (!AllowList.bPropertyReadable ? TEXT("<unreadable>") : (bOnAllowList ? TEXT("YES") : TEXT("NO")));

            UE_LOG(LogNodeShuffle, Display,
                TEXT("EXTRACTOR path='%s' restrictToNodeType='%s' allowedForms=[%s] onlyCertainResources=%d ")
                TEXT("allowedResources=[%s] extractorType='%s' sfPlusAllowList=%s"),
                *ExtCls->GetPathName(), *A.RestrictClassPath, *A.AllowedFormsCsv,
                A.bOnlyCertainResources ? 1 : 0, *AllowedResStr,
                *Ext->GetExtractorTypeName().ToString(), AllowListState);

            // Reporting-only form filter (generic; see file header) -- 2 == RF_LIQUID.
            if (A.AllowedForms.Num() == 0 || A.AllowedForms.Contains(2))
            {
                LiquidCapableExtractorPaths.Add(ExtCls->GetPathName());
            }
        }

        // ---- Prominent LIQUID section (Ask #3): the vanilla Oil Pump's mRestrictToNodeType has never
        // been observed in any log or CDO export this project holds -- this is the line that finally
        // measures it, whatever it turns out to be.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("===== LIQUID-CAPABLE EXTRACTORS (RF_LIQUID in allowedForms, or unrestricted): %d of %d ====="),
            LiquidCapableExtractorPaths.Num(), ExtractorClasses.Num());
        for (const FString& Path : LiquidCapableExtractorPaths)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("  LIQUID-CAPABLE: %s (see its EXTRACTOR line above for full rules)"), *Path);
        }

        // ---- 3. Resource-node groups present in the world ----
        TMap<FString, FNodeGroupInfo> NodeGroups;
        int32 TotalNodeInstances = 0;
        for (TActorIterator<AFGResourceNodeBase> It(World); It; ++It)
        {
            AFGResourceNodeBase* RN = *It;
            if (!IsValid(RN)) { continue; }
            ++TotalNodeInstances;
            UClass* NodeClass = RN->GetClass();
            UClass* ResourceClass = RN->GetResourceClass().Get();
            const int32 Form = (int32)RN->GetResourceForm();
            const FString Key = NodeClass->GetPathName() + TEXT("|")
                + (ResourceClass ? ResourceClass->GetPathName() : TEXT("<none>")) + TEXT("|") + FString::FromInt(Form);
            FNodeGroupInfo& G = NodeGroups.FindOrAdd(Key);
            if (G.InstanceCount == 0)
            {
                G.NodeClass = NodeClass;
                G.ResourceClass = ResourceClass;
                G.Form = Form;
            }
            ++G.InstanceCount;
            if (FNodeShuffleModule::IsManagedSpawnedNode(RN)) { ++G.ManagedCount; }
            if (UNodeShuffleNodeComponent::Find(RN)) { ++G.RelocatedCount; }
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("DUMPEXTRACTORS: node groups found: %d distinct (class,resource,form) combinations across %d node actors in this world"),
            NodeGroups.Num(), TotalNodeInstances);

        TArray<FString> LiquidNodeGroupKeys;
        for (const TPair<FString, FNodeGroupInfo>& Pair : NodeGroups)
        {
            const FNodeGroupInfo& G = Pair.Value;
            UE_LOG(LogNodeShuffle, Display,
                TEXT("NODE class='%s' resource='%s' form=%d(%s) instances=%d managedByUs=%d relocatedComponent=%d"),
                *G.NodeClass->GetPathName(), G.ResourceClass ? *G.ResourceClass->GetPathName() : TEXT("<none>"),
                G.Form, NodeShuffleFormName(G.Form), G.InstanceCount, G.ManagedCount, G.RelocatedCount);
            if (G.Form == 2) { LiquidNodeGroupKeys.Add(Pair.Key); } // reporting-only, see file header
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("===== LIQUID NODE GROUPS (form=Liquid): %d of %d ====="),
            LiquidNodeGroupKeys.Num(), NodeGroups.Num());

        // ---- 4. Match matrix: for each (extractor, node-group), does the extractor NATIVELY accept it?
        // Reuses FNodeShuffleModule::EvaluateExtractorAcceptance -- the SAME predicate DbgLogAcceptance
        // has always used -- never a second copy.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("===== MATCH MATRIX: %d extractors x %d node-groups = %d pairs ====="),
            ExtractorClasses.Num(), NodeGroups.Num(), ExtractorClasses.Num() * NodeGroups.Num());
        for (UClass* ExtCls : ExtractorClasses)
        {
            const AFGBuildableResourceExtractorBase* Ext = ExtCls->GetDefaultObject<AFGBuildableResourceExtractorBase>();
            if (!Ext) { continue; }
            const bool bOnAllowList = AllowList.AllowedExtractorClasses.Contains(ExtCls);
            for (const TPair<FString, FNodeGroupInfo>& Pair : NodeGroups)
            {
                const FNodeGroupInfo& G = Pair.Value;
                const FNodeShuffleExtractorAcceptance A =
                    FNodeShuffleModule::EvaluateExtractorAcceptance(Ext, G.NodeClass, G.Form, G.ResourceClass);
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("MATCHMATRIX extractor='%s' node='%s' resource='%s' form=%d(%s) -> accepts=%d ")
                    TEXT("(nodeIsA=%d formAllowed=%d resAllowed=%d) sfPlusAllowList=%s"),
                    *ExtCls->GetPathName(), *G.NodeClass->GetPathName(),
                    G.ResourceClass ? *G.ResourceClass->GetPathName() : TEXT("<none>"),
                    G.Form, NodeShuffleFormName(G.Form), A.AcceptsNatively() ? 1 : 0,
                    A.bNodeIsA ? 1 : 0, A.bFormAllowed ? 1 : 0, A.bResourceAllowed ? 1 : 0,
                    !AllowList.bAssetFound
                        ? TEXT("<no-sfplus-installed>")
                        : (!AllowList.bPropertyReadable ? TEXT("<unreadable>")
                                                        : (bOnAllowList ? TEXT("YES") : TEXT("NO"))));
            }
        }

        UE_LOG(LogNodeShuffle, Display, TEXT("===== DUMPEXTRACTORS END ====="));
    }));
