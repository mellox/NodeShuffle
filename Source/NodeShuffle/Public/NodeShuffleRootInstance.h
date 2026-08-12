#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "NodeShuffleRootInstance.generated.h"

// Root game instance module. SML discovers it automatically because
// bRootModule is true. Registers the mod configuration.
UCLASS()
class NODESHUFFLE_API URootInstance_NodeShuffle : public UGameInstanceModule
{
    GENERATED_BODY()

public:
    URootInstance_NodeShuffle();

    // T67 item C. SML registers this module's configuration (and loads NodeShuffle.cfg from disk) during
    // the INITIALIZATION phase — UGameInstanceModule::DispatchLifecycleEvent calls RegisterDefaultContent
    // there, and UConfigManager::RegisterModConfiguration loads the file inline. POST_INITIALIZATION is
    // therefore the first phase at which the deserialized rows exist, and it runs in the MAIN MENU game
    // instance, which is where the protection list is editable.
    virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;
};
