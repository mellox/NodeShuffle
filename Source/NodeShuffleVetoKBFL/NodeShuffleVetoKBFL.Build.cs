using UnrealBuildTool;

// coexist-veto-1: OPTIONAL KBFL-bridge module. LoadingPhase "None" in NodeShuffle.uplugin — the
// engine never auto-loads it (ModuleDescriptor.cpp only loads a module when the requested phase
// EQUALS the descriptor's phase, and "None" is documented "Do not automatically load this module").
// The main NodeShuffle module loads it BY NAME at world init, and only after confirming the real
// KBFL module is present, so when KBFL is not installed this module is never STARTED (StartupModule never runs).
// CORRECTED ns-logfix-1 (2026-11-22): the DLL is still ATTEMPTED. In a packaged (pak-file) game the engine preloads every
// enabled plugin's module binaries at PostConfigInit without checking LoadingPhase (LaunchEngineLoop.cpp SetPreloadBinaries;
// PluginManager.cpp LoadModulesForEnabledPlugins -> LoadModuleBinaryOnly), so a KBFL-less game logs one LogWindows
// "Failed to load ... (GetLastError=126) / Missing import: FactoryGameSteam-KBFL-Win64-Shipping.dll" at startup.
// Read from the CSS engine source; see docs/DIAGNOSTICS.md. UNodeShuffleDestroyerVetoRequirement derives from a KBFL
// class, so delay-loading the KBFL import is not a safe fix (registering that class needs its KBFL parent; untested).
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
