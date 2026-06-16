#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNodeShuffle, Log, All);

// Module installs diagnostic hooks on the Mk1 extractor hologram (redesign-13..19).
class FNodeShuffleModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override;
    virtual bool IsGameModule() const override { return true; }
    // redesign-19 DIAGNOSTIC: AccessTransformers friends this module to AFGResourceExtractorHologram so this
    // static helper can call the hologram's PROTECTED CanOccupyResource/IsAllowedOnResource on the resource
    // our trace hit, and log which check rejects our node (TrySnapToActor -> 0 with a valid resource).
    static void DbgLogAcceptance(class AFGResourceExtractorHologram* Hologram, class AActor* ResourceActor);
};
