#pragma once

// Packet G (ns-automatch): shared, PURE discovery helpers factored out of Packet F's
// NodeShuffleExtractorDump.cpp so the auto-allow pass (NodeShuffleAutoAllowExtractors.cpp) and the
// diagnostic dump command consume the SAME data instead of two copies that could drift -- the exact
// concern that already justified factoring FNodeShuffleModule::EvaluateExtractorAcceptance in Packet F.
//
// Deliberately NOT shared here: extractor-CLASS discovery. Packet F's DIAGNOSTIC dump wants to see
// EVERY resource-extractor class that exists (loaded or not, unlock-state irrelevant) via
// TObjectIterator<UClass> + IAssetRegistry::GetDerivedClassNames, which stays local to
// NodeShuffleExtractorDump.cpp. Packet G's DECISION only cares about extractors the player can actually
// BUILD right now -- AFGRecipeManager::GetAvailableBuildingsOfType<...>() (unlock-scoped, by design) --
// which stays local to NodeShuffleAutoAllowExtractors.cpp. Using the registry for the auto-allow
// decision was considered and rejected: the Packet F cold review measured (from CSS engine source) that
// a cooked Shipping build's asset registry never sees modded Blueprint classes at all -- see this
// packet's report for the full evidence trail.
//
// Every function here is PURE (no UE_LOG calls) -- each caller decides its own log prefix/cadence so a
// diagnostic dump and an automatic pass never emit confusingly-mislabeled lines.

#include "CoreMinimal.h"

const TCHAR* NodeShuffleFormName(int32 Form);

// ---- SF+ allow-list read (reflection only -- ZERO compile-time KAPI/SF+ dependency) ----
struct FSfPlusAllowListReadout
{
    bool bAssetFound = false;
    bool bPropertyReadable = false;
    FString PropertyInnerKind = TEXT("<unread>");
    TSet<const UClass*> AllowedExtractorClasses;
    int32 ArrayEntryCount = 0;
    // Packet G F3 (ns-review-f, folded into Packet F): GetObjectPropertyValue does NOT force a load, so
    // an unresolved (still-null) entry when the inner is a SOFT class property reads as "absent" even
    // though SF+ genuinely allows it. A nonzero count here means every "not on the list" conclusion
    // drawn from AllowedExtractorClasses is UNVERIFIED for this read -- see NodeShuffleAutoAllowExtractors.cpp
    // for how Packet G refuses to act on an unreliable read.
    int32 UnresolvedEntries = 0;
};

// Reads SF+'s `/SatisfactoryPlus/AssetDatas/Modules/PDA_SFP_ExtractorList.PDA_SFP_ExtractorList`
// instance's `mAllowedExtractors` array by NAME via reflection. READ-ONLY (Packet G writes via a
// generated KDataForge document instead of mutating this asset directly -- see the report for why
// direct mutation cannot work). Pure: does not log.
FSfPlusAllowListReadout ReadSfPlusAllowList();

// One (node class, resource class, form) group observed in the world. Grouped on all three -- NOT class
// alone -- because the real-class-redesign relocates nodes AS THEIR ORIGINAL CLASS, so the one generic
// vanilla class (BP_ResourceNode_C) legitimately hosts many different resources across instances;
// collapsing to class-only would hide that variation from any extractor whose mOnlyAllowCertainResources
// narrows by resource, not just by class/form.
struct FNodeShuffleNodeGroupInfo
{
    UClass* NodeClass = nullptr;
    UClass* ResourceClass = nullptr; // may be null
    int32 Form = -1;
    int32 InstanceCount = 0;
    int32 ManagedCount = 0;    // FNodeShuffleModule::IsManagedSpawnedNode
    int32 RelocatedCount = 0;  // carries a UNodeShuffleNodeComponent
};

// Census of every resource-node group present in the given (loaded) world. Pure: does not log.
void CollectNodeGroups(class UWorld* World, TMap<FString, FNodeShuffleNodeGroupInfo>& OutGroups, int32& OutTotalInstances);
