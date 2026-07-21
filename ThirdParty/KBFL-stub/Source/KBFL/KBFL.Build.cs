using UnrealBuildTool;

// BUILD-TIME HEADER STUB ONLY — see README-STUB.md at the plugin root. Provides the verbatim
// UKBFLCDOCallRequirement header + an import library for NodeShuffle's optional veto module.
// The real KBFL DLL (the player's installed mod) satisfies these imports at runtime.
// Deliberately minimal dependencies: the one stubbed header needs nothing beyond these.
public class KBFL : ModuleRules
{
    public KBFL(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine"
        });
    }
}
