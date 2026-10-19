// rwg-repclass-1 (2026-10-19): see NodeShuffleRepClass.h for the predicates. PURE helpers -- no UE_LOG here;
// the roll function (NodeShuffleSubsystem.cpp) prints the REPCLASS / REPCLASS-REGISTRY census lines.
#include "NodeShuffleRepClass.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/TopLevelAssetPath.h"
#include "Resources/FGResourceNodeBase.h"
#include "Resources/FGResourceDescriptor.h"
#include "HAL/PlatformTime.h"

namespace NodeShuffleRepClass
{
    bool IsGenericNodeClassPath(const FString& ClassPath)
    {
        return ClassPath.StartsWith(TEXT("/Game/")) || ClassPath.StartsWith(TEXT("/Script/FactoryGame."));
    }

    FString NodeShuffleMountRoot(const FString& Path)
    {
        if (Path.StartsWith(TEXT("/Script/")))
        {
            int32 Dot = INDEX_NONE;
            return Path.FindChar(TEXT('.'), Dot) ? Path.Left(Dot).ToLower() : Path.ToLower();
        }
        if (!Path.StartsWith(TEXT("/"))) { return FString(); }
        const int32 Second = Path.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
        return Second == INDEX_NONE ? FString() : Path.Left(Second + 1).ToLower();
    }

    int32 RepClassRank(const FString& ClassPath, const FString& ResourcePath)
    {
        if (IsGenericNodeClassPath(ResourcePath)) { return 0; } // base-game resource: ranking is a no-op
        if (IsGenericNodeClassPath(ClassPath)) { return 0; }
        const FString ClassRoot = NodeShuffleMountRoot(ClassPath);
        if (!ClassRoot.IsEmpty() && ClassRoot == NodeShuffleMountRoot(ResourcePath)) { return 2; }
        return 1;
    }

    bool RepClassShouldReplace(const FString& ExistingClassPath, const FString& NewClassPath, const FString& ResourcePath)
    {
        return RepClassRank(NewClassPath, ResourcePath) > RepClassRank(ExistingClassPath, ResourcePath);
    }

    bool IsOwnNodeClass(const FString& ClassPath, const FString& ResourcePath)
    {
        return RepClassRank(ClassPath, ResourcePath) == 2;
    }

    const TCHAR* RepClassRankName(int32 Rank)
    {
        return Rank == 2 ? TEXT("own-mount") : (Rank == 1 ? TEXT("non-generic") : TEXT("generic"));
    }

    void QueryDerivedNodeClasses(FRegistryQuery& Out)
    {
        const double T0 = FPlatformTime::Seconds();
        IAssetRegistry& AssetRegistry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        TArray<FTopLevelAssetPath> BaseClassNames;
        BaseClassNames.Add(FTopLevelAssetPath(TEXT("/Script/FactoryGame"), TEXT("FGResourceNode")));
        TSet<FTopLevelAssetPath> Excluded;
        TSet<FTopLevelAssetPath> DerivedClassNames;
        AssetRegistry.GetDerivedClassNames(BaseClassNames, Excluded, DerivedClassNames);
        Out.Derived = DerivedClassNames.Num();
        for (const FTopLevelAssetPath& P : DerivedClassNames)
        {
            const FString S = P.ToString();
            if (S.StartsWith(TEXT("/Game/"))) { Out.BaseGameBp++; continue; }
            if (S.StartsWith(TEXT("/Script/"))) { continue; } // natives are always loaded; the live scan already saw any instance
            Out.CandidatePaths.Add(S);
        }
        Out.CandidatePaths.Sort(); // deterministic load order within the cap
        Out.Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
    }

    void MatchByRegistry(const FString& ResourcePath, const FRegistryQuery& Query, const TMap<FString, FString>& RefusedClasses,
        TMap<FString, UClass*>& LoadCache, FRegistryMatch& Out)
    {
        const double T0 = FPlatformTime::Seconds();
        const FString ResRoot = NodeShuffleMountRoot(ResourcePath);
        for (const FString& Path : Query.CandidatePaths)
        {
            if (ResRoot.IsEmpty() || NodeShuffleMountRoot(Path) != ResRoot) { continue; }
            Out.MountFiltered++;
            if (RefusedClasses.Contains(Path)) { Out.Rejected++; continue; }
            if (Out.Loaded + Out.Failed >= RegistryLoadCap) { continue; } // cap: counted in mountFiltered, never loaded
            UClass* Cls = nullptr;
            if (UClass** Cached = LoadCache.Find(Path)) { Cls = *Cached; }
            else
            {
                Cls = FindObject<UClass>(nullptr, *Path);
                if (!Cls) { Cls = LoadObject<UClass>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet); }
                LoadCache.Add(Path, Cls);
            }
            if (!Cls) { Out.Failed++; continue; }
            Out.Loaded++;
            if (Cls->HasAnyClassFlags(CLASS_Abstract) || !Cls->IsChildOf(AFGResourceNodeBase::StaticClass())) { Out.Rejected++; continue; }
            const AFGResourceNodeBase* Cdo = Cls->GetDefaultObject<AFGResourceNodeBase>();
            const UClass* CdoRes = Cdo ? Cdo->GetResourceClass().Get() : nullptr;
            if (!CdoRes || CdoRes->GetPathName() != ResourcePath) { Out.Rejected++; continue; }
            Out.Matched = Cls->GetPathName();
            break;
        }
        Out.Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
    }
}
