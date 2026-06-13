#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNodeShuffle, Log, All);

// Module is intentionally minimal: NodeShuffle installs no function hooks.
// All behavior lives in ANodeShuffleSubsystem.
class FNodeShuffleModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override;
    virtual bool IsGameModule() const override { return true; }
};
