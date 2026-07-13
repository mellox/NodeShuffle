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
            // bakedmaps-2: "Projects"/IPluginManager removed — baked knowledge is EMBEDDED in the DLL
            // (NodeShuffleBakedData.h); the packaging pipeline ships no loose plugin files.
        });
    }
}
