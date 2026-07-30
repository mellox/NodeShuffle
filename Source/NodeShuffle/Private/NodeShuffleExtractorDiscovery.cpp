#include "NodeShuffleExtractorDiscovery.h"

#include "EngineUtils.h"
#include "Resources/FGResourceNodeBase.h"
#include "Resources/FGResourceNode.h"
#include "NodeShuffle.h"
#include "NodeShuffleNodeComponent.h"
#include "UObject/UnrealType.h"

// EResourceForm values (FGItemDescriptor.h): RF_INVALID=0, RF_SOLID=1, RF_LIQUID=2, RF_GAS=3. A plain
// switch (not StaticEnum<EResourceForm>()->GetNameStringByValue) so this diagnostic adds no new engine
// call beyond what EvaluateExtractorAcceptance already makes -- the values are a stable, documented
// native enum, cited at the point of use rather than re-derived at runtime.
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

// Reads SF+'s mAllowedExtractors array by NAME via reflection. This measures the exact UPROPERTY type
// (PropertyInnerKind) rather than assuming it (open question flagged in PacketA-review.md) --
// CastField<FObjectPropertyBase> resolves it regardless of whether the inner is FClassProperty
// (TSubclassOf), FObjectProperty, or FSoftClassProperty, because GetObjectPropertyValue is virtual on
// FObjectPropertyBase (all three derive from it). Pure -- no logging; callers log their own summary.
FSfPlusAllowListReadout ReadSfPlusAllowList()
{
    FSfPlusAllowListReadout Out;
    static const TCHAR* AssetPath = TEXT("/SatisfactoryPlus/AssetDatas/Modules/PDA_SFP_ExtractorList.PDA_SFP_ExtractorList");
    UObject* Asset = FindObject<UObject>(nullptr, AssetPath);
    if (!Asset) { Asset = LoadObject<UObject>(nullptr, AssetPath); }
    if (!Asset) { return Out; }
    Out.bAssetFound = true;

    FArrayProperty* Prop = FindFProperty<FArrayProperty>(Asset->GetClass(), TEXT("mAllowedExtractors"));
    FObjectPropertyBase* Inner = Prop ? CastField<FObjectPropertyBase>(Prop->Inner) : nullptr;
    Out.PropertyInnerKind = !Prop
        ? FString(TEXT("<mAllowedExtractors property not found on this asset>"))
        : (Inner ? Inner->GetClass()->GetName()
                 : FString::Printf(TEXT("<non-object-array inner: %s>"),
                       Prop->Inner ? *Prop->Inner->GetClass()->GetName() : TEXT("null")));
    if (!Inner) { return Out; }
    Out.bPropertyReadable = true;

    FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Asset));
    Out.ArrayEntryCount = Helper.Num();
    for (int32 i = 0; i < Helper.Num(); ++i)
    {
        if (const UClass* Cls = Cast<UClass>(Inner->GetObjectPropertyValue(Helper.GetRawPtr(i))))
        {
            Out.AllowedExtractorClasses.Add(Cls);
        }
        else
        {
            ++Out.UnresolvedEntries;
        }
    }
    return Out;
}

// Census of every resource-node group present in the given (loaded) world. Pure -- no logging.
void CollectNodeGroups(UWorld* World, TMap<FString, FNodeShuffleNodeGroupInfo>& OutGroups, int32& OutTotalInstances)
{
    OutTotalInstances = 0;
    if (!World) { return; }
    for (TActorIterator<AFGResourceNodeBase> It(World); It; ++It)
    {
        AFGResourceNodeBase* RN = *It;
        if (!IsValid(RN)) { continue; }
        ++OutTotalInstances;
        UClass* NodeClass = RN->GetClass();
        UClass* ResourceClass = RN->GetResourceClass().Get();
        const int32 Form = (int32)RN->GetResourceForm();
        const FString Key = NodeClass->GetPathName() + TEXT("|")
            + (ResourceClass ? ResourceClass->GetPathName() : TEXT("<none>")) + TEXT("|") + FString::FromInt(Form);
        FNodeShuffleNodeGroupInfo& G = OutGroups.FindOrAdd(Key);
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
}
