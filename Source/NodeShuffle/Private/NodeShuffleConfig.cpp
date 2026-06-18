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
        TEXT("0 = roll a random seed when a save first generates its layout. Non-zero = use this exact seed. To apply a new seed to an existing save, set it and then use 'Re-roll Now'."));

    AddBool(TEXT("RerollNow"), false,
        TEXT("Re-roll Layout"),
        TEXT("Turn this ON to re-roll the ENTIRE node layout (using the current Seed Override, or a fresh random seed if it is 0). It applies LIVE within a few seconds if you toggle it while in-game, or on the next load otherwise, then turns itself back off so it never loops.\n\nIMPORTANT — what to expect: the new node locations are map-wide, and like normal play they only appear once you get near them, so right after a re-roll the world looks emptier and the new nodes reveal as you EXPLORE (even areas you visited before, since the nodes moved). Nodes with a miner on them are kept (never moved or retyped); nodes removed in a previous roll cannot return."));

    AddInt(TEXT("ActivePercent"), 70, 10, 100,
        TEXT("Active Percent Of Node Pool"),
        TEXT("Percent of all node locations (vanilla + new) that are active in a given save. Applied when the layout is rolled."));

    AddInt(TEXT("NewNodeCount"), 100, 0, 300,
        TEXT("New Node Locations"),
        TEXT("How many extra node locations to generate. Applied when the layout is rolled."));

    AddInt(TEXT("MinNodesPerResource"), 5, 0, 25,
        TEXT("Minimum Active Nodes Per Resource"),
        TEXT("Every resource type keeps at least this many active nodes so the playthrough stays completable. Applied when the layout is rolled."));

    AddInt(TEXT("MinNodesPerModdedResource"), 2, 0, 25,
        TEXT("Minimum Active Nodes Per Modded Resource"),
        TEXT("Like the above, but for resources added by other mods (outside /Game/). 0 = no minimum (modded types may collapse to a single location). Default 2 stops any modded resource type from appearing at only one spot. Applied when the layout is rolled."));

    AddBool(TEXT("RandomizePurity"), true,
        TEXT("Randomize Purity"),
        TEXT("Shuffle node purities too (dealt from the vanilla purity distribution, so overall purity balance is preserved). Applied when the layout is rolled."));

    AddBool(TEXT("AllowVanillaDisappear"), true,
        TEXT("Allow Vanilla Nodes To Disappear"),
        TEXT("When off, every vanilla node stays active and only the new locations are subject to the active-percent roll."));

    AddBool(TEXT("IncludeModdedNodes"), true,
        TEXT("Include Modded Nodes"),
        TEXT("Shuffle nodes added by other mods too (e.g. AllMinable's item nodes, modded ores). When on, modded SOLID nodes also RELOCATE to new locations on a re-roll, like vanilla nodes, instead of shuffling in place. Modded resources use the separate 'Minimum Active Nodes Per Modded Resource' floor. Applied when the layout is rolled."));

    AddInt(TEXT("SpawnRadiusMeters"), 600, 100, 5000,
        TEXT("Spawn-On-Discovery Radius (m)"),
        TEXT("New node locations only materialize (their rock + minable node appear) once you come within this many metres AND the terrain there has streamed in, so they always settle correctly on the ground. Smaller = more exploration, fewer live actors at once; larger = nodes pop in from further away. Far, undiscovered nodes stay as data until you reach them."));

    AddBool(TEXT("EnableDiagnostics"), false,
        TEXT("Enable Diagnostic Logging (Experimental)"),
        TEXT("OFF by default. Turn ON only when troubleshooting (e.g. to capture why a miner won't place): the mod writes verbose placement / node diagnostics to FactoryGame.log. Leave OFF for normal play — it keeps your log clean and avoids any overhead. The mod's actual fixes are always active whether this is on or off."));

    AddBool(TEXT("EnableStarterNodes"), true,
        TEXT("Starter Nodes Near Spawn"),
        TEXT("On a BRAND-NEW game only, place a small starter set (2 Iron, 2 Limestone, 1 Copper, Pure purity) near where you spawn, so the early game is always playable no matter how the shuffle moved the world's nodes. Taken from the shuffled pool when possible. Never added to an existing save."));

    AddInt(TEXT("StarterNodeRadiusMeters"), 200, 50, 1000,
        TEXT("Starter Node Radius (m)"),
        TEXT("How far from your spawn point the starter nodes may be placed. Smaller keeps them right at your feet; larger spreads them out. Only used on a brand-new game's first roll."));

    RootSection = Root;
}
