#include "NodeShuffleRootInstance.h"
#include "NodeShuffleConfig.h"
#include "NodeShuffle.h"
#include "Engine/World.h"

URootInstance_NodeShuffle::URootInstance_NodeShuffle()
{
    bRootModule = true;
    ModConfigurations.Add(UNodeShuffleConfig::StaticClass());
}

void URootInstance_NodeShuffle::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
    // Super FIRST and unconditionally: the base class registers this module's configuration during
    // INITIALIZATION, and the phase we act on runs after it. Skipping or reordering it would mean there
    // is no configuration to stamp.
    Super::DispatchLifecycleEvent(Phase);

    // T67 item C: stamp the protection rows' labels as soon as the configuration exists in THIS game
    // instance. In the main menu that is the only pass that ever runs (the population pass is
    // in-world/authority-only); in a loaded world it runs first and the population pass re-stamps the
    // same values afterwards, which is idempotent — one shared helper writes both label slots.
    if (Phase != ELifecyclePhase::POST_INITIALIZATION)
    {
        return;
    }
    // One line naming the branch, so the log shows this ran at all and which context it ran in. It
    // reports what was read; it states no cause.
    const UWorld* ModuleWorld = GetWorld();
    UE_LOG(LogNodeShuffle, Display,
        TEXT("T67MENUSTAMP hook: root game instance module reached POST_INITIALIZATION (module world '%s'). ")
        TEXT("Stamping the protection list's row labels from the paths already in the config."),
        ModuleWorld ? *ModuleWorld->GetName() : TEXT("<none>"));
    FNodeShuffleModule::StampForeignResourceRowLabelsFromConfig(this);
}
