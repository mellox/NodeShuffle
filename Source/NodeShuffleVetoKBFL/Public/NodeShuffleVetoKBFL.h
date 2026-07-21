#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// coexist-veto-1: optional KBFL destroyer-veto module. KBFL-specific BY DESIGN (this module's whole
// point is speaking KBFL's requirement protocol) — but it never hardcodes SatisfactoryPlus names:
// destroyer/listener assets are discovered by their KBFL base CLASS, whatever mod ships them.
//
// Lifecycle: never auto-loaded (LoadingPhase None). FNodeShuffleModule::ArmDestroyerVetoIfEnabled
// loads it by name when the NodeShuffle.DestroyerVeto CVar is on AND the real KBFL module is
// present. StartupModule registers this module's per-world arm function with the main module via
// FNodeShuffleModule::SetKBFLVetoArmFunction — a plain function pointer, so the main module needs
// no include of anything here and the dependency arrow stays strictly veto -> main.
class FNodeShuffleVetoKBFLModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    virtual bool IsGameModule() const override { return true; }
};
