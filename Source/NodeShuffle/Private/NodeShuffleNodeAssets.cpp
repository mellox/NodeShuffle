#include "NodeShuffleNodeAssets.h"

namespace
{
    const TMap<FName, FNodeShuffleVisual>& GetVisualMap()
    {
        static const TMap<FName, FNodeShuffleVisual> Map = []()
        {
            TMap<FName, FNodeShuffleVisual> M;
            const FVector OreOffset(-400.0f, 0.0f, -40.0f);
            const FVector OreScale(2.0f, 2.0f, 2.5f);

            M.Add("Desc_OreIron_C", {
                TEXT("/Game/FactoryGame/Resource/RawResources/Nodes/ResourceNode_OreIron_01.ResourceNode_OreIron_01"),
                { TEXT("/Game/FactoryGame/Resource/RawResources/OreIron/Material/ResourceNode_Iron_Inst.ResourceNode_Iron_Inst") },
                OreOffset, OreScale });

            M.Add("Desc_OreCopper_C", {
                TEXT("/Game/FactoryGame/Resource/RawResources/Nodes/ResourceNode_OreCopper_01.ResourceNode_OreCopper_01"),
                { TEXT("/Game/FactoryGame/Resource/RawResources/OreCopper/Material/ResourceNode_Copper_Inst.ResourceNode_Copper_Inst") },
                OreOffset, OreScale });

            M.Add("Desc_OreGold_C", {
                TEXT("/Game/FactoryGame/Resource/RawResources/Nodes/ResourceNode_OreGold_01.ResourceNode_OreGold_01"),
                { TEXT("/Game/FactoryGame/Resource/RawResources/OreGold/Material/ResourceNode_Gold_Inst.ResourceNode_Gold_Inst") },
                OreOffset, OreScale });

            M.Add("Desc_OreBauxite_C", {
                TEXT("/Game/FactoryGame/Resource/RawResources/Nodes/ResourceNode_OreIron_01.ResourceNode_OreIron_01"),
                { TEXT("/Game/FactoryGame/Resource/RawResources/OreBauxite/Material/MI_ResourceNode_OreBauxite.MI_ResourceNode_OreBauxite"),
                  TEXT("/Game/FactoryGame/Resource/RawResources/OreBauxite/Material/MI_ResourceNode_Middle_OreBauxite.MI_ResourceNode_Middle_OreBauxite") },
                OreOffset, OreScale });

            M.Add("Desc_OreUranium_C", {
                TEXT("/Game/FactoryGame/Resource/RawResources/Nodes/ResourceNode_OreIron_01.ResourceNode_OreIron_01"),
                { TEXT("/Game/FactoryGame/Resource/RawResources/OreUranium/Material/ResourceNode_OreUranium_Inst.ResourceNode_OreUranium_Inst"),
                  TEXT("/Game/FactoryGame/Resource/RawResources/OreUranium/Material/MI_ResourceNode_Middle_OreUranium.MI_ResourceNode_Middle_OreUranium") },
                OreOffset, OreScale });

            M.Add("Desc_RawQuartz_C", {
                TEXT("/Game/FactoryGame/Resource/RawResources/Nodes/ResourceNode_Quartz.ResourceNode_Quartz"),
                { TEXT("/Game/FactoryGame/Resource/RawResources/OreQuartz/Material/ResourceNode_Quartz_Inst.ResourceNode_Quartz_Inst") },
                OreOffset, OreScale });

            M.Add("Desc_Sulfur_C", {
                TEXT("/Game/FactoryGame/Resource/RawResources/Nodes/SulfurResource_01.SulfurResource_01"),
                { TEXT("/Game/FactoryGame/Resource/RawResources/Sulfur/Material/Resource_Sulfur_Inst.Resource_Sulfur_Inst") },
                FVector(0.0f, 0.0f, -10.0f), FVector(1.0f, 1.0f, 1.0f) });

            M.Add("Desc_Coal_C", {
                TEXT("/Game/FactoryGame/Resource/RawResources/Nodes/CoalResource_01.CoalResource_01"),
                { TEXT("/Game/FactoryGame/Resource/RawResources/Coal/Material/CoalResource_01_Inst.CoalResource_01_Inst") },
                FVector(0.0f, 0.0f, -10.0f), FVector(1.0f, 1.0f, 1.0f) });

            M.Add("Desc_Stone_C", {
                TEXT("/Game/FactoryGame/Resource/RawResources/Nodes/Resource_Stone_01.Resource_Stone_01"),
                { TEXT("/Game/FactoryGame/Resource/RawResources/Stone/Material/MI_ResourceNode_Stone_Blocks.MI_ResourceNode_Stone_Blocks") },
                FVector(0.0f, 0.0f, -5.0f), FVector(2.4f, 2.4f, 2.0f) });

            M.Add("Desc_SAM_C", {
                TEXT("/Game/FactoryGame/Resource/RawResources/SAM/Mesh/SM_SAM_Node_01.SM_SAM_Node_01"),
                { TEXT("/Game/FactoryGame/Resource/RawResources/SAM/Material/MI_SAM_Node_01.MI_SAM_Node_01") },
                FVector(0.0f, 0.0f, 50.0f), FVector(1.3333f, 1.3333f, 1.8f) });

            return M;
        }();
        return Map;
    }
}

const FNodeShuffleVisual* FNodeShuffleNodeAssets::FindVisual(const FName& ResourceClassName)
{
    return GetVisualMap().Find(ResourceClassName);
}
