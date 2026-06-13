#include "NodeShuffleConfig.h"

#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/WidgetExtension/CP_Integer.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

FNodeShuffleConfigStruct FNodeShuffleConfigStruct::GetActiveConfig(UObject* WorldContext)
{
    FNodeShuffleConfigStruct ConfigStruct{};
    const FConfigId ConfigId{"NodeShuffle", ""};
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull))
    {
        UConfigManager* ConfigManager = World->GetGameInstance()->GetSubsystem<UConfigManager>();
        ConfigManager->FillConfigurationStruct(ConfigId, FDynamicStructInfo{FNodeShuffleConfigStruct::StaticStruct(), &ConfigStruct});
    }
    return ConfigStruct;
}

UNodeShuffleConfig::UNodeShuffleConfig()
{
    ConfigId = FConfigId{"NodeShuffle", ""};
    DisplayName = FText::FromString(TEXT("Node Shuffle"));
    Description = FText::FromString(TEXT("Per-save randomized resource node layout: extra node locations, shuffled resources and purities, balance minimums."));
}

void UNodeShuffleConfig::PostInitProperties()
{
    Super::PostInitProperties();

    // Only the class default object's tree is used by SML.
    if (RootSection != nullptr || !HasAnyFlags(RF_ClassDefaultObject))
    {
        return;
    }

    // SML's Blueprint property subclasses wire up the actual editor widgets;
    // the raw native classes serialize but render as empty space in the UI.
    UClass* SectionClass = LoadClass<UConfigPropertySection>(nullptr,
        TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertySection.BP_ConfigPropertySection_C"));
    if (!SectionClass) { SectionClass = UConfigPropertySection::StaticClass(); }

    UClass* BoolClass = LoadClass<UConfigPropertyBool>(nullptr,
        TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyBool.BP_ConfigPropertyBool_C"));
    if (!BoolClass) { BoolClass = UConfigPropertyBool::StaticClass(); }

    UClass* IntegerClass = LoadClass<UConfigPropertyInteger>(nullptr,
        TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyInteger.BP_ConfigPropertyInteger_C"));
    if (!IntegerClass) { IntegerClass = UConfigPropertyInteger::StaticClass(); }

    UConfigPropertySection* Root = NewObject<UConfigPropertySection>(this, SectionClass, TEXT("RootSection"));
    if (UCP_Section* RootWidget = Cast<UCP_Section>(Root))
    {
        RootWidget->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
        RootWidget->HasHeader = false;
    }
    // The subsystem re-reads config periodically, so allow in-game edits
    // (SML's BP classes default bRequiresWorldReload to true, which greys the
    // whole panel out in the pause menu).
    Root->bRequiresWorldReload = false;

    // Properties are outer'd to the section (NOT to this config object) so
    // that MarkDirty's Outer-chain walk reaches the save handler.
    const auto AddBool = [&](const TCHAR* Key, bool Default, const TCHAR* Display, const TCHAR* Tip)
    {
        UConfigPropertyBool* Prop = NewObject<UConfigPropertyBool>(Root, BoolClass, FName(Key));
        Prop->Value = Default;
        // The in-game Reset button does Value = DefaultValue, so DefaultValue
        // must be set explicitly or reset snaps to false.
        Prop->DefaultValue = Default;
        Prop->DisplayName = FText::FromString(Display);
        Prop->Tooltip = FText::FromString(Tip);
        Prop->bRequiresWorldReload = false;
        Root->SectionProperties.Add(Key, Prop);
    };

    const auto AddInt = [&](const TCHAR* Key, int32 Default, int32 Min, int32 Max, const TCHAR* Display, const TCHAR* Tip)
    {
        UConfigPropertyInteger* Prop = NewObject<UConfigPropertyInteger>(Root, IntegerClass, FName(Key));
        Prop->Value = Default;
        Prop->DefaultValue = Default;
        Prop->DisplayName = FText::FromString(Display);
        Prop->Tooltip = FText::FromString(Tip);
        Prop->bRequiresWorldReload = false;
        if (UCP_Integer* IntWidget = Cast<UCP_Integer>(Prop))
        {
            IntWidget->WidgetType = ECP_IntegerWidgetType::CPI_Spinbox;
            IntWidget->MinValue = Min;
            IntWidget->MaxValue = Max;
        }
        Root->SectionProperties.Add(Key, Prop);
    };

    AddBool(TEXT("Enabled"), true,
        TEXT("Enabled"),
        TEXT("Master switch. When off, NodeShuffle does nothing: no new nodes spawn and no vanilla nodes are deactivated. Resource changes already stored in the save persist."));

    AddInt(TEXT("SeedOverride"), 0, 0, 2147483647,
        TEXT("Seed Override"),
        TEXT("0 = roll a random seed when a save first generates its layout. Non-zero = use this exact seed. Combined with Allow Re-roll, changing this re-rolls an existing save at next load."));

    AddBool(TEXT("AllowReroll"), false,
        TEXT("Allow Re-roll Of Existing Saves"),
        TEXT("DANGER: when on, a save whose stored seed differs from a non-zero Seed Override re-rolls its entire node layout at the next load. Nodes with miners on them are never changed."));

    AddInt(TEXT("ActivePercent"), 70, 10, 100,
        TEXT("Active Percent Of Node Pool"),
        TEXT("Percent of all node locations (vanilla + new) that are active in a given save. Applied when the layout is rolled."));

    AddInt(TEXT("NewNodeCount"), 100, 0, 300,
        TEXT("New Node Locations"),
        TEXT("How many extra node locations to generate. Applied when the layout is rolled."));

    AddInt(TEXT("MinNodesPerResource"), 5, 0, 25,
        TEXT("Minimum Active Nodes Per Resource"),
        TEXT("Every resource type keeps at least this many active nodes so the playthrough stays completable. Applied when the layout is rolled."));

    AddBool(TEXT("RandomizePurity"), true,
        TEXT("Randomize Purity"),
        TEXT("Shuffle node purities too (dealt from the vanilla purity distribution, so overall purity balance is preserved). Applied when the layout is rolled."));

    AddBool(TEXT("AllowVanillaDisappear"), true,
        TEXT("Allow Vanilla Nodes To Disappear"),
        TEXT("When off, every vanilla node stays active and only the new locations are subject to the active-percent roll."));

    AddBool(TEXT("IncludeModdedNodes"), true,
        TEXT("Include Modded Nodes"),
        TEXT("Shuffle nodes added by other mods too (e.g. AllMinable's item nodes, modded ores). The per-resource minimum only ever applies to vanilla ores, and non-vanilla resources may lack proper rock visuals for now. Applied when the layout is rolled."));

    AddBool(TEXT("EnableExperimentalFeatures"), false,
        TEXT("Enable Experimental Features"),
        TEXT("Master switch for in-development features that are not yet considered stable. THIS VERSION HAS NO EXPERIMENTAL FEATURES, so this option currently does nothing — leave it off. (Rock-visual swapping graduated to a stable, always-on feature in this release.)"));

    RootSection = Root;
}
