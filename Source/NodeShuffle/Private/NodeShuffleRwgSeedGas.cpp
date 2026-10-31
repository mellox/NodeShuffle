// rwg-seedgas-1 (2026-10-31, amendment A1). Contract and gate order: NodeShuffleRwgSeedGas.h.
#include "NodeShuffleRwgSeedGas.h"
#include "NodeShuffle.h"
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleRepClass.h"
#include "NodeShuffleRwgDeal.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyArray.h"
#include "Configuration/Properties/ConfigPropertyString.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/WidgetExtension/CP_Integer.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "Resources/FGItemDescriptor.h"
#include "Resources/FGResourceDescriptor.h"
#include "Resources/FGResourceDeposit.h"
#include "Algo/Count.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Misc/PackageName.h"

namespace NodeShuffleRwgSeedGas
{
    const TCHAR* const DefaultLeadResource = TEXT("/bamrenewfp/content/Resource/RawResource/LeadOre/oreleaddesc.oreleaddesc_C");
    const TCHAR* const DefaultLithiumResource = TEXT("/Lithium/Resources/Lithium/Desc_OreLithium.Desc_OreLithium_C");
    const TCHAR* const DefaultLithiumNodeClass = TEXT("/Lithium/World/BP_ResourdeNode_Alkali.BP_ResourdeNode_Alkali_C");
    const TCHAR* const DefaultThoriumResource = TEXT("/RefinedPower/World/ResourceNodes/Thorium/Desc_RP_Thorium.Desc_RP_Thorium_C");

    namespace
    {
        const TCHAR* const RowsKey = TEXT("ModdedVanillaCountRows");

        FString Short(const FString& Path) { return Path.IsEmpty() ? FString(TEXT("none")) : FPackageName::ObjectPathToObjectName(Path); }

        const TCHAR* FormName(UClass* Desc)
        {
            if (!Desc) { return TEXT("unloaded"); }
            switch (UFGItemDescriptor::GetForm(TSubclassOf<UFGItemDescriptor>(Desc)))
            {
            case EResourceForm::RF_SOLID: return TEXT("solid");
            case EResourceForm::RF_LIQUID: return TEXT("liquid");
            case EResourceForm::RF_GAS: return TEXT("gas");
            default: return TEXT("invalid");
            }
        }

        UClass* LoadClassQuiet(const FString& Path)
        {
            if (Path.IsEmpty()) { return nullptr; }
            UClass* C = FindObject<UClass>(nullptr, *Path);
            return C ? C : LoadObject<UClass>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
        }

        // Gate 4: the spawn site's own-class test (own mount AND CDO resource == this resource), never a pickup deposit class.
        bool AcceptOwnClass(const UClass* C, const FString& Path, const FString& Res, const TMap<FString, FString>& Refused)
        {
            return C && C->IsChildOf(AFGResourceNode::StaticClass()) && !C->IsChildOf(AFGResourceDeposit::StaticClass())
                && !C->HasAnyClassFlags(CLASS_Abstract)
                && !Refused.Contains(Path) && NodeShuffleRepClass::IsOwnNodeClassForResource(C, Path, Res);
        }

        UConfigPropertySection* FindRowSection(UConfigProperty* Element, UConfigPropertyString** OutRes,
            UConfigPropertyString** OutClass, UConfigPropertyInteger** OutCount)
        {
            UConfigPropertySection* Section = Cast<UConfigPropertySection>(Element);
            if (!Section) { return nullptr; }
            TObjectPtr<UConfigProperty>* R = Section->SectionProperties.Find(TEXT("ResourcePath"));
            TObjectPtr<UConfigProperty>* K = Section->SectionProperties.Find(TEXT("NodeClassPath"));
            TObjectPtr<UConfigProperty>* N = Section->SectionProperties.Find(TEXT("VanillaCount"));
            *OutRes = R ? Cast<UConfigPropertyString>(R->Get()) : nullptr;
            *OutClass = K ? Cast<UConfigPropertyString>(K->Get()) : nullptr;
            *OutCount = N ? Cast<UConfigPropertyInteger>(N->Get()) : nullptr;
            return (*OutRes && *OutClass && *OutCount) ? Section : nullptr;
        }
    }

    void RegisterConfigRows(UConfigPropertySection* Root, UClass* ArrayClass, UClass* SectionClass, UClass* StringClass,
        UClass* IntegerClass)
    {
        UConfigPropertyArray* Rows = NewObject<UConfigPropertyArray>(Root, ArrayClass, RowsKey);
        Rows->bRequiresWorldReload = true; // read at each layout roll only (same reasoning as T60's F10)
        Rows->DisplayName = FText::FromString(TEXT("Modded Resource Vanilla Counts (Random World Generator)"));
        Rows->Tooltip = FText::FromString(
            TEXT("Used on Random World Generator maps only, at each layout roll or re-roll. Each row names a modded resource, ")
            TEXT("an optional node class, and how many nodes it has on the vanilla map. That number is scaled by this ")
            TEXT("world's amount of vanilla ore nodes and replaces the modded resource's own node count in the shuffle. A ")
            TEXT("gas resource with no node on this world gets new nodes of the listed class; if that class cannot be ")
            TEXT("loaded, nothing is added. Pre-filled: Lead 6, Lithium 23, Thorium 45."));

        UConfigPropertySection* RowTemplate = NewObject<UConfigPropertySection>(Rows, SectionClass, TEXT("ModdedVanillaCountRow"));
        RowTemplate->bRequiresWorldReload = true;
        if (UCP_Section* W = Cast<UCP_Section>(RowTemplate)) { W->WidgetType = ECP_SectionWidgetType::CPS_Vertical; W->HasHeader = true; }
        RowTemplate->DisplayName = FText::FromString(TEXT("Resource row"));

        const auto AddString = [&](const TCHAR* Key, const TCHAR* Display, const TCHAR* Tip)
        {
            UConfigPropertyString* P = NewObject<UConfigPropertyString>(RowTemplate, StringClass, FName(Key));
            P->Value = TEXT(""); P->DefaultValue = TEXT("");
            P->DisplayName = FText::FromString(Display); P->Tooltip = FText::FromString(Tip);
            P->bRequiresWorldReload = true;
            RowTemplate->SectionProperties.Add(Key, P);
        };
        AddString(TEXT("ResourcePath"), TEXT("Resource path"), TEXT("Full path of the resource descriptor class."));
        AddString(TEXT("NodeClassPath"), TEXT("Node class path"), TEXT("Full path of the node class to place for a resource with no node on this world. May be empty."));
        UConfigPropertyInteger* CountProp = NewObject<UConfigPropertyInteger>(RowTemplate, IntegerClass, TEXT("VanillaCount"));
        CountProp->Value = 0; CountProp->DefaultValue = 0;
        CountProp->DisplayName = FText::FromString(TEXT("Vanilla map node count"));
        CountProp->Tooltip = FText::FromString(TEXT("How many nodes of this resource the vanilla map has."));
        CountProp->bRequiresWorldReload = true;
        if (UCP_Integer* IW = Cast<UCP_Integer>(CountProp)) { IW->WidgetType = ECP_IntegerWidgetType::CPI_Spinbox; IW->MinValue = 0; IW->MaxValue = MaxRowCount; }
        RowTemplate->SectionProperties.Add(TEXT("VanillaCount"), CountProp);
        Rows->DefaultValue = RowTemplate;

        // Shipped rows = runtime Values (what the config manager clones for the live tree) AND DefaultValues (Reset target).
        struct FDefault { const TCHAR* Res; const TCHAR* Cls; int32 Count; };
        const FDefault Defaults[] = { { DefaultLeadResource, TEXT(""), DefaultLeadVanillaCount },
                                      { DefaultLithiumResource, DefaultLithiumNodeClass, DefaultLithiumVanillaCount },
                                      { DefaultThoriumResource, TEXT(""), DefaultThoriumVanillaCount } };
        int32 Written = 0;
        for (const FDefault& D : Defaults)
        {
            UConfigProperty* Elem = Rows->AddNewElement();
            UConfigPropertyString* R = nullptr; UConfigPropertyString* K = nullptr; UConfigPropertyInteger* N = nullptr;
            // Each element must own its sub-properties; writing through a template-shared pointer would rewrite the template.
            const UConfigProperty* TplRes = RowTemplate->SectionProperties.FindRef(TEXT("ResourcePath")).Get();
            if (!FindRowSection(Elem, &R, &K, &N) || static_cast<const UConfigProperty*>(R) == TplRes || N == CountProp)
            {
                UE_LOG(LogNodeShuffle, Error, TEXT("MODQUOTA-REGISTER defaultRow=%s ownsSubProperties=0 -- default rows not written"), *Short(D.Res));
                Rows->Values.Empty();
                Rows->DefaultValues.Empty();
                Written = 0;
                break;
            }
            R->Value = D.Res; K->Value = D.Cls; N->Value = D.Count;
            Rows->DefaultValues.Add(DuplicateObject<UConfigProperty>(Elem, Rows));
            ++Written;
        }
        UE_LOG(LogNodeShuffle, Display, TEXT("MODQUOTA-REGISTER defaultRowsWritten=%d values=%d defaults=%d"),
            Written, Rows->Values.Num(), Rows->DefaultValues.Num());
        Root->SectionProperties.Add(RowsKey, Rows);
    }

    bool ReadRows(UObject* WorldContext, TArray<FRow>& Out, int32& OutMalformed)
    {
        OutMalformed = 0;
        if (!GEngine || !WorldContext) { return false; }
        const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
        UConfigManager* Manager = (World && World->GetGameInstance()) ? World->GetGameInstance()->GetSubsystem<UConfigManager>() : nullptr;
        UConfigPropertySection* Root = Manager ? Manager->GetConfigurationRootSection(FConfigId{"NodeShuffle", ""}) : nullptr;
        TObjectPtr<UConfigProperty>* Found = Root ? Root->SectionProperties.Find(RowsKey) : nullptr;
        UConfigPropertyArray* Rows = Found ? Cast<UConfigPropertyArray>(Found->Get()) : nullptr;
        if (!Rows) { return false; }
        for (UConfigProperty* Element : Rows->Values)
        {
            UConfigPropertyString* R = nullptr; UConfigPropertyString* K = nullptr; UConfigPropertyInteger* N = nullptr;
            if (!FindRowSection(Element, &R, &K, &N)) { ++OutMalformed; continue; }
            FRow Row;
            Row.Resource = R->Value.TrimStartAndEnd();
            Row.NodeClassPath = K->Value.TrimStartAndEnd();
            Row.VanillaCount = N->Value;
            Out.Add(Row);
        }
        return true;
    }

    void DecideAndApply(UObject* WorldContext, bool bEnabled, bool bMapIsRwg, const TMap<FString, FString>& RefusedClasses,
        TMap<FString, int32>& VanillaResourceCounts, int32& VanillaCount, TMap<FString, uint8>& FormByResource, uint8 FormSolid,
        TMap<FString, FString>& NodeClassByResource, FState& Out)
    {
        Out = FState();
        Out.bEnabled = bEnabled;
        Out.bMapIsRwg = bMapIsRwg;
        Out.VanillaCountBefore = VanillaCount;
        TArray<FRow> Rows;
        Out.bTreeFound = ReadRows(WorldContext, Rows, Out.RowsMalformed);

        for (const TPair<FString, int32>& KV : VanillaResourceCounts)
        {
            if (KV.Key.StartsWith(TEXT("/Game/")) && FormByResource.FindRef(KV.Key) == FormSolid) { Out.VanillaOreOriginals += KV.Value; }
        }
        Out.LandFactor = FMath::Clamp(static_cast<float>(Out.VanillaOreOriginals) / RwgBaseSolidNodes, LandFactorMin, LandFactorMax);

        const bool bActive = bEnabled && bMapIsRwg;
        NodeShuffleRepClass::FRegistryQuery RegQuery;
        bool bRegQueried = false;
        TMap<FString, UClass*> RegCache;
        TSet<FString> SeenRows;
        for (const FRow& Row : Rows)
        {
            FRowResult& R = Out.Rows.AddDefaulted_GetRef();
            R.Row = Row;
            UClass* Desc = Row.Resource.IsEmpty() ? nullptr : LoadClassQuiet(Row.Resource);
            R.Form = FormName(Desc);
            R.CapturedOriginals = VanillaResourceCounts.FindRef(Row.Resource);
            R.Target = FMath::RoundToInt(FMath::Clamp(Row.VanillaCount, 0, MaxRowCount) * Out.LandFactor);
            if (!bEnabled) { R.Skipped = TEXT("disabled"); continue; }
            if (!bMapIsRwg) { R.Skipped = TEXT("notRwg"); continue; }
            if (Row.Resource.IsEmpty() || Row.VanillaCount < 1 || Row.VanillaCount > MaxRowCount) { R.Skipped = TEXT("badRow"); continue; }
            if (SeenRows.Contains(Row.Resource)) { R.Skipped = TEXT("duplicate"); continue; }
            SeenRows.Add(Row.Resource);

            if (R.CapturedOriginals > 0)
            {
                // OVERRIDE: the count the quota reads becomes the target; the captured originals themselves are untouched.
                Out.SavedCounts.Add(Row.Resource, R.CapturedOriginals);
                VanillaResourceCounts.Add(Row.Resource, R.Target);
                VanillaCount += R.Target - R.CapturedOriginals;
                R.Action = TEXT("override");
                Out.bApplied = true;
                continue;
            }
            // SEED candidate (zero captured originals).
            if (Desc == nullptr) { R.Skipped = TEXT("descUnloaded"); continue; }
            if (R.Form != TEXT("gas")) { R.Skipped = TEXT("notGas"); continue; }
            R.ResolvedBy = TEXT("none");
            if (!bRegQueried) { NodeShuffleRepClass::QueryDerivedNodeClasses(RegQuery); bRegQueried = true; }
            NodeShuffleRepClass::FRegistryMatch Match;
            NodeShuffleRepClass::MatchByRegistry(Row.Resource, RegQuery, RefusedClasses, RegCache, Match);
            R.RegistryMountFiltered = Match.MountFiltered;
            R.RegistryMatched = Short(Match.Matched);
            if (!Match.Matched.IsEmpty() && AcceptOwnClass(LoadClassQuiet(Match.Matched), Match.Matched, Row.Resource, RefusedClasses))
            {
                R.ResolvedClass = Match.Matched;
                R.ResolvedBy = TEXT("registry");
            }
            if (!Row.NodeClassPath.IsEmpty())
            {
                UClass* Cfg = LoadClassQuiet(Row.NodeClassPath);
                R.bCfgLoaded = Cfg != nullptr;
                R.bCfgNode = Cfg && Cfg->IsChildOf(AFGResourceNode::StaticClass());
                R.bCfgAbstract = Cfg && Cfg->HasAnyClassFlags(CLASS_Abstract);
                R.bCfgRefused = RefusedClasses.Contains(Row.NodeClassPath);
                R.bCfgOwnMount = NodeShuffleRepClass::IsOwnNodeClass(Row.NodeClassPath, Row.Resource);
                const AFGResourceNode* Cdo = R.bCfgNode ? Cfg->GetDefaultObject<AFGResourceNode>() : nullptr;
                const UClass* CdoRes = Cdo ? Cdo->GetResourceClass().Get() : nullptr;
                R.CfgCdoResource = CdoRes ? Short(CdoRes->GetPathName()) : FString(TEXT("none"));
                if (R.ResolvedClass.IsEmpty() && AcceptOwnClass(Cfg, Row.NodeClassPath, Row.Resource, RefusedClasses))
                {
                    R.ResolvedClass = Cfg->GetPathName();
                    R.ResolvedBy = TEXT("config");
                }
            }
            if (R.ResolvedClass.IsEmpty()) { R.Skipped = TEXT("noClass"); continue; }
            VanillaResourceCounts.Add(Row.Resource, R.Target);
            VanillaCount += R.Target;
            FormByResource.Add(Row.Resource, FormSolid); // gas capture convention: gas records FormSolid (own rock mesh)
            NodeClassByResource.Add(Row.Resource, R.ResolvedClass);
            Out.SeededKinds.Add(Row.Resource);
            R.Action = TEXT("seed");
            Out.bApplied = true;
        }
        if (Out.bApplied && VanillaCount < 1)
        {
            // The quota divides by VanillaCount: an all-zero pool would divide by zero. Undo every change, report it per row.
            Restore(Out, VanillaResourceCounts, VanillaCount);
            for (const FString& K : Out.SeededKinds) { FormByResource.Remove(K); NodeClassByResource.Remove(K); }
            for (FRowResult& R : Out.Rows)
            {
                if (FCString::Strcmp(R.Action, TEXT("skip")) != 0) { R.Action = TEXT("skip"); R.Skipped = TEXT("emptyPool"); }
            }
            Out.SeededKinds.Reset();
            Out.SavedCounts.Reset();
        }
        if (!bActive) { return; }
        // Census-only rows: every captured MODDED resource with no table row (the user's list of rows still to add).
        for (const TPair<FString, int32>& KV : VanillaResourceCounts)
        {
            if (NodeShuffleRepClass::IsGenericNodeClassPath(KV.Key) || SeenRows.Contains(KV.Key)) { continue; }
            FRowResult& R = Out.Rows.AddDefaulted_GetRef();
            R.bHasRow = false;
            R.Row.Resource = KV.Key;
            R.Form = FormName(LoadClassQuiet(KV.Key));
            R.CapturedOriginals = KV.Value;
            R.Skipped = TEXT("hasNoRow");
        }
    }

    void Restore(FState& S, TMap<FString, int32>& VanillaResourceCounts, int32& VanillaCount)
    {
        if (!S.bApplied) { return; }
        for (const TPair<FString, int32>& KV : S.SavedCounts) { VanillaResourceCounts.Add(KV.Key, KV.Value); }
        for (const FString& K : S.SeededKinds) { VanillaResourceCounts.Remove(K); }
        VanillaCount = S.VanillaCountBefore;
        S.bApplied = false;
    }

    bool IsSeededKind(const FState& S, const FString& Kind)
    {
        return S.SeededKinds.Num() > 0 && S.SeededKinds.Contains(Kind);
    }

    void LogRoll(const FState& S, const TMap<FString, int32>& Quota, const TMap<FString, int32>& PinnedPerResource,
        const TArray<FString>& SolidDeck, const TArray<FString>& LiquidDeck, const NodeShuffleRwgDeal::FPurityDecks& Purity)
    {
        int32 Overridden = 0;
        for (const FRowResult& R : S.Rows) { if (FCString::Strcmp(R.Action, TEXT("override")) == 0) { ++Overridden; } }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("MODQUOTA-CFG enabled=%d mapIsRwg=%d rowsTreeFound=%d rowsRead=%d rowsMalformed=%d vanillaOreOriginals=%d landBase=%d landFactor=%.3f overridden=%d seeded=%d"),
            S.bEnabled ? 1 : 0, S.bMapIsRwg ? 1 : 0, S.bTreeFound ? 1 : 0,
            static_cast<int32>(Algo::CountIf(S.Rows, [](const FRowResult& R) { return R.bHasRow; })), S.RowsMalformed,
            S.VanillaOreOriginals, RwgBaseSolidNodes, S.LandFactor, Overridden, S.SeededKinds.Num());
        for (const FRowResult& R : S.Rows)
        {
            const FString& Res = R.Row.Resource;
            int32 Cards = 0;
            for (const FString& C : SolidDeck) { if (C == Res) { ++Cards; } }
            for (const FString& C : LiquidDeck) { if (C == Res) { ++Cards; } }
            const TCHAR* Branch = TEXT("n/a");
            if (Cards > 0)
            {
                Branch = Purity.ByClass.Contains(Res) ? TEXT("perResource") : (Purity.MapSource.Num() > 0 ? TEXT("aggregate") : TEXT("noSource"));
            }
            UE_LOG(LogNodeShuffle, Display,
                TEXT("MODQUOTA resource=%s form=%s mapIsRwg=%d capturedOriginals=%d vanillaCount=%d vanillaOreOriginals=%d landBase=%d landFactor=%.3f target=%d action=%s classResolvedBy=%s pinnedCarried=%d quota=%d cardsAdded=%d purityBranch=%s skipped=%s"),
                *Short(Res), *R.Form, S.bMapIsRwg ? 1 : 0, R.CapturedOriginals, R.Row.VanillaCount, S.VanillaOreOriginals,
                RwgBaseSolidNodes, S.LandFactor, R.Target, R.Action, R.ResolvedBy, PinnedPerResource.FindRef(Res),
                Quota.FindRef(Res), Cards, Branch, R.Skipped);
            if (FCString::Strcmp(R.ResolvedBy, TEXT("n/a")) != 0)
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("MODQUOTA-CLASS resource=%s configPath=%s configLoaded=%d configNode=%d configAbstract=%d configRefused=%d configOwnMount=%d configCdoResource=%s registryMountFiltered=%d registryMatched=%s chosen=%s"),
                    *Short(Res), *Short(R.Row.NodeClassPath), R.bCfgLoaded ? 1 : 0, R.bCfgNode ? 1 : 0, R.bCfgAbstract ? 1 : 0,
                    R.bCfgRefused ? 1 : 0, R.bCfgOwnMount ? 1 : 0, *R.CfgCdoResource, R.RegistryMountFiltered, *R.RegistryMatched,
                    *Short(R.ResolvedClass));
            }
        }
    }

    void EmitSpawnCensus(UObject* WorldContext, bool bMapIsRwg, const TArray<FNodeShuffleEntry>& Layout,
        const TMap<FGuid, AFGResourceNode*>& SpawnedNodes, const TMap<FGuid, int32>& SpawnFailCounts,
        const TSet<FGuid>& SpawnParked)
    {
        static TWeakObjectPtr<UWorld> LastWorld;
        static TMap<FString, FString> LastSig;
        if (!bMapIsRwg || !WorldContext) { return; }
        UWorld* World = WorldContext->GetWorld();
        if (LastWorld.Get() != World) { LastWorld = World; LastSig.Reset(); }
        TArray<FRow> Rows;
        int32 Malformed = 0;
        if (!ReadRows(WorldContext, Rows, Malformed)) { return; }
        for (const FRow& Row : Rows)
        {
            UClass* Desc = Row.Resource.IsEmpty() ? nullptr : FindObject<UClass>(nullptr, *Row.Resource);
            if (FCString::Strcmp(FormName(Desc), TEXT("gas")) != 0) { continue; } // gas cards exist only when seeded
            int32 Entries = 0, Ok = 0, Failed = 0, Parked = 0;
            TArray<FString> Guids;
            for (const FNodeShuffleEntry& E : Layout)
            {
                if (!E.bIsNewNode || !E.bActive || E.AssignedResourceClassPath != Row.Resource
                    || E.OriginalResourceClassPath == Row.Resource) { continue; }
                ++Entries;
                AFGResourceNode* const* Live = SpawnedNodes.Find(E.EntryGuid);
                if (Live && IsValid(*Live)) { ++Ok; }
                if (SpawnFailCounts.Contains(E.EntryGuid)) { ++Failed; }
                if (SpawnParked.Contains(E.EntryGuid)) { ++Parked; }
                if (Guids.Num() < 8) { Guids.Add(E.EntryGuid.ToString().Left(8)); }
            }
            const FString Guid8 = Guids.Num() > 0 ? FString::Join(Guids, TEXT(",")) : FString(TEXT("none"));
            const FString Sig = FString::Printf(TEXT("%d/%d/%d/%d/%s"), Entries, Ok, Failed, Parked, *Guid8);
            if (const FString* Prev = LastSig.Find(Row.Resource)) { if (*Prev == Sig) { continue; } }
            LastSig.Add(Row.Resource, Sig);
            UE_LOG(LogNodeShuffle, Display, TEXT("SEEDGASSPAWN resource=%s entries=%d spawnedOk=%d spawnFailed=%d parked=%d guids=%s"),
                *Short(Row.Resource), Entries, Ok, Failed, Parked, *Guid8);
        }
    }
}
