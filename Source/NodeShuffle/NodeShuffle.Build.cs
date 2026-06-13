using UnrealBuildTool;

public class NodeShuffle : ModuleRules
{
    public NodeShuffle(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore",
            "FactoryGame", "SML",
            "Json"
        });
    }
}
