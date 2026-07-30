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

        PrivateDependencyModuleNames.AddRange(new string[] {
            // Packet F (ns-automatch): NodeShuffle.DumpExtractors needs not-yet-loaded Blueprint
            // extractor-class discovery (IAssetRegistry::GetDerivedClassNames) — TObjectIterator<UClass>
            // alone only sees classes already loaded into memory and would silently under-report
            // installed-but-unloaded mod extractors. Same module NodeShuffleVetoKBFL already links
            // successfully (see its Build.cs) — this is the first time the MAIN module links it.
            "AssetRegistry",
        });
    }
}
