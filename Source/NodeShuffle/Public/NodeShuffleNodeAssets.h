#pragma once

#include "CoreMinimal.h"

// Fallback visual data for spawned (new) solid resource nodes, used only when
// the native AFGNodeMeshActor::OverrideMeshAndMaterials path yields no mesh.
// Asset paths are factual locations of the game's own node meshes/materials
// (discovered by the Resource Roulette / oukibt randomizer projects and
// re-verified at runtime by LoadObject — a miss just logs and skips).
struct FNodeShuffleVisual
{
    const TCHAR* MeshPath;
    TArray<const TCHAR*> MaterialPaths;
    FVector MeshOffset;   // relative offset of the mesh vs. the node origin
    FVector MeshScale;
};

class FNodeShuffleNodeAssets
{
public:
    // Key: resource descriptor class name, e.g. "Desc_OreIron_C".
    static const FNodeShuffleVisual* FindVisual(const FName& ResourceClassName);

    // Mesh-name prefixes recognizing the separate level rock actors that carry
    // vanilla ore-node visuals (those nodes have no mesh component of their
    // own). Derived from the table's mesh names: trailing _NN variant suffixes
    // stripped (ResourceNode_OreIron_01 -> ResourceNode_OreIron), plus the
    // family prefixes up to the first underscore (ResourceNode_, Resource_) so
    // per-type variants the table does not list (uranium/bauxite rocks etc.)
    // still match. Too-short family prefixes like SM_ are excluded.
    static TArray<FString> GetMeshNamePrefixes();
};
