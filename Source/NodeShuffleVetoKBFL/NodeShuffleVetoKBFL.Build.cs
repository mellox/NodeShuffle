using UnrealBuildTool;

// coexist-veto-1: OPTIONAL KBFL-bridge module. LoadingPhase "None" in NodeShuffle.uplugin — the
// engine never auto-loads it (ModuleDescriptor.cpp only loads a module when the requested phase
// EQUALS the descriptor's phase, and "None" is documented "Do not automatically load this module").
// The main NodeShuffle module loads it BY NAME at world init, and only after confirming the real
// KBFL module is present — so when KBFL is not installed this DLL is never loaded and its imports
// against KBFL-Win64-Shipping are never resolved (no missing-DLL error for KBFL-less users).
public class NodeShuffleVetoKBFL : ModuleRules
{
    public NodeShuffleVetoKBFL(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            // Not-yet-loaded destroyer/listener asset discovery (IAssetRegistry::GetAssetsByClass).
            "AssetRegistry",
            // BUILD-TIME HEADER STUB at SatisfactoryModLoader\Mods\KBFL — provides the verbatim
            // UKBFLCDOCallRequirement header + an import library. At runtime the PLAYER'S installed
            // KBFL DLL satisfies those imports (same DLL name); the stub itself is never shipped.
            "KBFL",
            // Managed-node registry query, diagnostics gate, arm-hook registration, LogNodeShuffle.
            "NodeShuffle",
        });
    }
}
