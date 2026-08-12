#include "NodeShuffleConfig.h"

#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyArray.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertyString.h"
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
    // ns-h1b-notice, Option A2: a static breadcrumb for the player who missed the chat line. It cannot
    // name specifics (this text is set once on the CDO and never sees runtime state), which is exactly
    // why it is an ADD-ON to the chat notice and not a substitute for it.
    Description = FText::FromString(TEXT("Per-save randomized resource node layout: extra node locations, shuffled resources and purities, balance minimums. If a new extractor won't build on a shuffled node, restart the game once - compatibility patches are written during play and read at startup."));
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

    // T60: the two extra SML BP property classes the opt-out list needs. Same fallback shape as the
    // three above — the raw native class serializes correctly but renders as empty space in the UI, so
    // a fallback means "the list persists but you cannot see it", not "the list is broken".
    UClass* ArrayClass = LoadClass<UConfigPropertyArray>(nullptr,
        TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyArray.BP_ConfigPropertyArray_C"));
    if (!ArrayClass) { ArrayClass = UConfigPropertyArray::StaticClass(); }

    UClass* StringClass = LoadClass<UConfigPropertyString>(nullptr,
        TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyString.BP_ConfigPropertyString_C"));
    if (!StringClass) { StringClass = UConfigPropertyString::StaticClass(); }

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

    // ---- T68 (ns-t68-release-config, 2026-08-11): REGISTRATION ORDER IS THE PANEL ORDER. --------------
    // SectionProperties is a TMap iterated in INSERTION ORDER, and nothing here ever removes, so the
    // order of these Add calls is the single lever C++ has over how the panel reads. The grouping is
    // audit §3: THE SHUFFLE -> OTHER MODS (including the protection list) -> RESOURCE WELLS ->
    // TROUBLESHOOTING. There are no visual sub-headers: UCP_Section exposes only WidgetType / HasHeader /
    // HeaderText / Collapsed and every property is added flat to one RootSection, so the groups are
    // ordering only. WHETHER THE BLUEPRINT PANEL RENDERS IN SectionProperties ORDER IS NOT PROVABLE FROM
    // C++ (Widget_CP_Section is Blueprint) -- runtime test step 1 is the decider, exactly as T65 recorded
    // for the row field order.
    //
    // SERIALIZATION IS UNAFFECTED AND THIS IS PROVABLE: UConfigPropertySection::Serialize writes a KEYED
    // object and Deserialize reads it back with ObjectValue->GetValue(Property.Key) (SML
    // ConfigPropertySection.cpp:18-42), so nothing is positional. An existing NodeShuffle.cfg round-trips
    // unchanged; only the key order inside the JSON flips on the next save.
    //
    // FIVE PROPERTIES WERE DELETED HERE (EnableExperimentalFeatures, CommitWellsAtRoll,
    // ShowCompatibilityNotices, AllowVanillaDisappear, RerollRelocatedWells). SML ignores a file key that
    // is not in the schema on load and drops it on the next save (ConfigPropertySection.cpp:18-42), and
    // the mod version bump to 1.4.0 forces that save on first load (ConfigManager.cpp:103-117). A player
    // who had set one of them silently gets the hard-wired value -- see docs/TECH-DEBT.md T68.
    // ---------------------------------- THE SHUFFLE ---------------------------------------------------
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

    // T68 (audit §5.8): 'Allow Vanilla Nodes To Disappear' is GONE, hard-wired to its shipped default
    // (ON = vanilla entries take part in the active-percent roll like everything else). Its name and
    // tooltip were read as "vanilla nodes stay put", which was never what it did -- with it off, vanilla
    // nodes still relocated and still got retyped; it only forced their bActive to true. The two
    // consumers that read it are deleted, not defaulted, in NodeShuffleSubsystem.cpp.

    AddBool(TEXT("EnableStarterNodes"), true,
        TEXT("Starter Nodes Near Spawn"),
        TEXT("On a BRAND-NEW game only, place a small starter set (2 Iron, 2 Limestone, 1 Copper, Pure purity) near where you spawn, so the early game is always playable no matter how the shuffle moved the world's nodes. Taken from the shuffled pool when possible. Never added to an existing save."));

    AddInt(TEXT("StarterNodeRadiusMeters"), 200, 50, 1000,
        TEXT("Starter Node Radius (m)"),
        TEXT("How far from your spawn point the starter nodes may be placed. Smaller keeps them right at your feet; larger spreads them out. Only used on a brand-new game's first roll."));

    AddInt(TEXT("SpawnRadiusMeters"), 600, 100, 5000,
        TEXT("Spawn-On-Discovery Radius (m)"),
        TEXT("New node locations only materialize (their rock + minable node appear) once you come within this many metres AND the terrain there has streamed in, so they always settle correctly on the ground. Smaller = more exploration, fewer live actors at once; larger = nodes pop in from further away. Far, undiscovered nodes stay as data until you reach them."));

    // ---------------------------------- OTHER MODS ----------------------------------------------------
    AddBool(TEXT("IncludeModdedNodes"), true,
        TEXT("Include Modded Nodes"),
        TEXT("Shuffle nodes added by other mods too (e.g. AllMinable's item nodes, modded ores). When on, modded SOLID nodes also RELOCATE to new locations on a re-roll, like vanilla nodes, instead of shuffling in place. Modded resources use the separate 'Minimum Active Nodes Per Modded Resource' floor. Applied when the layout is rolled."));

    AddBool(TEXT("UnlockModdedKnowledge"), true,
        TEXT("Unlock Scanner Knowledge For Shuffled Modded Resources"),
        TEXT("Shuffled modded resources are registered with the resource scanner so scanners recognize them. NOTE: some overhaul mods also gate MINER PLACEMENT on scanner knowledge, so this can let you place miners on modded ores before that overhaul's own research would have allowed it (crafting/recipes stay gated as normal). Turn off if you prefer each mod's own progression to grant this. Vanilla resources are never affected."));

    // ---- T68 (audit §5.12, option (b)): THE PROTECTION MASTER SWITCH, PROMOTED TO THE PANEL. ----------
    // Before T68 the only way to turn the per-resource protection list on was the console variable
    // NodeShuffle.DestroyerVeto -- the one place in this whole panel that asked a player to use the
    // console. This checkbox drives THE SAME LATCH that CVar gates (FNodeShuffleModule::
    // ArmDestroyerVetoIfEnabled resolves the two into one bObserveOnly, and there is exactly one
    // resolver so the two readers of the latch cannot disagree).
    //
    // PRECEDENCE, DECIDED HERE AND DOCUMENTED AT THE RESOLVER: this checkbox is the PERSISTED source of
    // truth; NodeShuffle.DestroyerVeto is a SESSION-SCOPED console override that wins only when someone
    // has actually set it (SetBy is anything other than the constructor default). An untouched CVar does
    // not fight the checkbox, and a console/Engine.ini set still wins for that session -- which keeps
    // every existing "set NodeShuffle.DestroyerVeto 1" instruction working.
    //
    // DEFAULT ON, AND THAT IS A BEHAVIOUR CHANGE FOR EXISTING INSTALLS: protection was off by default
    // before this build and is on after it. Say so in the changelog rather than softening it.
    AddBool(TEXT("ProtectOtherModsNodes"), true,
        // T68 F4, DECIDED BY THE AUTHOR 2026-08-11: the label is GENERIC and the concrete case moved
        // into the tooltip. The cold review graded the old label ("... From Removal") as ASSUMED
        // PRESENTED AS MEASURED -- T67 established that this feature may say it ANSWERS A CHECK and
        // REFUSES A CONDITION, and may not assert what the other mod does afterwards, which "From
        // Removal" did in the most-read string on the page. THE LABEL IS QUOTED IN FOUR OTHER PLACES
        // (the CVar help, the protection-list tooltip, the chat notice, the veto module's log line);
        // all four were updated in this same commit, because a label quoted differently in two places
        // is this workspace's most-repeated defect class.
        TEXT("Protect Other Mods' Nodes"),
        // EVERY FACTUAL ASSERTION HERE IS GRADED -- see docs/TECH-DEBT.md T68 for the grading table.
        // MEASURED-IN-CODE: the setting is read once while the world loads (ArmDestroyerVetoIfEnabled,
        // called from the subsystem's BeginPlay) and not re-read while you play; with it off the hook
        // still arms and still classifies, which is what fills the list below (T61); the per-resource
        // ticks are only consulted while this is on. NOT CLAIMED: that anything would have been removed,
        // that anything was saved, or what the other mod does after it gets our answer.
        // THE OPENING SENTENCE NAMES SATISFACTORY PLUS, and that naming is MEASURED, not folklore:
        // docs/TECH-DEBT.md T58 records SF+'s KBFL ResearchNodeRemover destroying every third-party
        // resource node ~0.9 s after world init on a new game. "the known case" is deliberate -- it
        // says SF+ is the one we have measured, and does NOT claim it is the only mod that does this.
        // "answers those removal checks" describes OUR side of the exchange (we answer, we refuse the
        // condition); it never says the removal was stopped, which is the T67 line.
        TEXT("ON by default. Some overhaul mods — Satisfactory Plus is the known case — remove other ")
        TEXT("mods' resource nodes. When this is on, NodeShuffle answers those removal checks for the ")
        TEXT("resources ticked in the list below, refusing the handler's condition.\n\n")
        TEXT("OFF: NodeShuffle answers nothing, and each other mod's handler decides on its own. The list ")
        TEXT("below still fills up as you play, so you can see what is being checked before you turn this ")
        TEXT("on.\n\n")
        TEXT("WHEN A CHANGE TAKES EFFECT: THE NEXT TIME YOU LOAD THE SAVE. This is read once while the ")
        TEXT("world is loading and is not re-read while you play. It does not bring back nodes that have ")
        TEXT("already been removed.\n\n")
        // T68 COLD REVIEW F2 (HIGH) -- THE PARAGRAPH BELOW IS THE REVIEWER'S TEXT, VERBATIM.
        // THE OLD WORDING ("...OVERRIDES this checkbox for the rest of the session if you set it") WAS
        // FALSE FOR THE VERY POPULATION THE CVar WAS KEPT FOR. The resolver decides by SetBy priority,
        // and an Engine.ini [ConsoleVariables] entry is re-applied at EVERY startup with
        // ECVF_SetByConsoleVariablesIni (0x07000000) -- above ECVF_SetByConstructor (0x00000000), which
        // the reviewer confirmed against the engine's own IConsoleManager.h. For that player the
        // override is permanent and this checkbox is inert forever, not for one session.
        // RE-VERIFIED AGAINST THE PREDICATE after applying: "for the rest of the session if you typed it
        // at the console" = ECVF_SetByConsole, which is not persisted and is re-asked at every world
        // init; "on every launch if it is in Engine.ini" = ECVF_SetByConsoleVariablesIni, re-applied by
        // the engine at startup; "T68VETOGATE reports which of the two decided each world load" = the
        // census line's decidedBy field, which is the only surface that can say it.
        TEXT("The console variable NodeShuffle.DestroyerVeto still works and OVERRIDES this checkbox ")
        TEXT("whenever it has been set — for the rest of the session if you typed it at the console, ")
        TEXT("or on every launch if it is in Engine.ini. Remove it from Engine.ini if you want this ")
        TEXT("checkbox to decide. The log line T68VETOGATE reports which of the two decided each ")
        TEXT("world load."));

    // ---- T60 (ns-t60-protect-checkboxes, 2026-08-10): the per-resource protection opt-out list ----
    // A UConfigPropertyArray whose element TEMPLATE is a section of {Resource: String, Protected: Bool}.
    // The SCHEMA is fixed here at build time (SML has no dynamic-key section: UConfigPropertySection's
    // Serialize AND Deserialize both iterate SectionProperties, so a key hand-added to NodeShuffle.cfg
    // is ignored on load and deleted on the next save). The CONTENTS are dynamic: array Deserialize
    // empties Values and re-allocates one element per JSON entry, so runtime-added rows round-trip.
    //
    // DefaultValues is deliberately left EMPTY, so a Reset CLEARS the list rather than restoring some
    // canned set. The rows come back on the next sighting; a player's unticks do not.
    //
    // COLD REVIEW F10: bRequiresWorldReload is TRUE on all three T60 properties, unlike every other
    // property on this panel. In SML C++ that flag gates CanResetNow() (ConfigProperty.cpp:39-59), so
    // false would permit an in-world Reset that wipes the whole list -- and TRUE is also the honest
    // value here, because these settings genuinely do not apply until the next world load (the opt-out
    // set is latched in BeginPlay). The root section stays false so the rest of the panel is still
    // editable in the pause menu; the comment at the top of this function records that SML's BP classes
    // grey out a bRequiresWorldReload property in the pause menu, so expect this list to be editable
    // from the MAIN MENU. That greying is a Blueprint-side behaviour this file cannot verify --
    // runtime test step 11 is what settles it.
    {
        UConfigPropertyArray* Rows = NewObject<UConfigPropertyArray>(Root, ArrayClass,
            TEXT("ProtectedForeignResources"));
        Rows->bRequiresWorldReload = true; // F10
        // T65 RETITLED. The old title was "Protect Other Mods' RESOURCES", which asserts the resource
        // itself belongs to another mod -- FALSE for the population that motivated T61's copy fix:
        // vanilla NitrogenGas / LiquidOil carried by another mod's well actors. What every row in this
        // list has in common is MEASURED and is the NODE side: the sighting only reaches the population
        // pass when the node's ACTOR CLASS path is outside /Game/ (NodeShuffle.cpp:172, and
        // NoteForeignResourceSighting returns early otherwise). "(Per Resource)" still says what a ROW
        // is. THE T61 CHAT NOTICE QUOTES THIS TITLE VERBATIM -- the two are pinned together by
        // tools/check_t65_lint.ps1 so they cannot drift.
        Rows->DisplayName = FText::FromString(TEXT("Protect Other Mods' Nodes (Per Resource)"));

        // EVERY FACTUAL ASSERTION BELOW IS GRADED, per the workspace rule that UI copy is a claim.
        // MEASURED-IN-CODE: rows are added only from foreign sightings inside the KBFL requirement
        // evaluation (NodeShuffleDestroyerVetoRequirement.cpp) via NoteForeignResourceSighting;
        // an unlisted resource is protected (IsForeignResourceProtectedByConfig returns true for any
        // path not in the latched opt-out set); the opt-out set is read ONCE per world init, before the
        // veto arms (LatchForeignResourceOptOutsFromConfig, called from the subsystem's BeginPlay);
        // a removed row is re-added by the next population pass that sees that resource again; the
        // effect is confined to the KBFL requirement hook, which is the only place this is consulted.
        // NOT CLAIMED, because nothing tests it: that unticking brings back nodes already removed;
        // that the list is a complete inventory of the other mod's resources; any timing in seconds.
        // ---- T67 (2026-08-11): THE THREE "TRIES TO REMOVE" SENTENCES ON THIS PAGE, RE-GRADED. --------
        // The old text said another mod's cleanup "tries to remove" nodes and that NodeShuffle "stops
        // that removal". WHAT THE HOOK ACTUALLY OBSERVES IS A REQUIREMENT EVALUATION: KBFL asks the
        // requirement asset whether its condition is met, and our requirement answers. T58 measured an
        // asset evaluating that requirement on nodes it never destroys, so "tries to remove" asserts an
        // intent this code cannot see, and "stops that removal" asserts an outcome inside the other
        // mod's own handler. MEASURED and therefore said: another mod's handler CHECKED nodes of this
        // resource; NodeShuffle answers that check. NOT CLAIMED: that anything was going to be removed,
        // that anything was saved, or what the other mod does after it gets our answer.
        // All three sentences on this page plus the chat notice's equivalent are fixed in ONE pass on
        // purpose (docs/TECH-DEBT.md T67; the T65 review's L5 failure scenario is a partial fix).
        Rows->Tooltip = FText::FromString(
            TEXT("THIS LIST FILLS ITSELF IN — you do not add to it. A resource appears here after ")
            TEXT("another mod's node handler has checked nodes of that resource at least once, so the ")
            TEXT("list starts empty and grows as you play with other mods.\n\n")
            TEXT("TICKED (the default for everything, including anything not listed yet): NodeShuffle ")
            TEXT("answers that check for this resource, refusing the handler's condition at that hook.\n\n")
            TEXT("UNTICKED: NodeShuffle does not answer for that resource, and the other mod's handler ")
            TEXT("decides on its own at that check. Everything else in the list stays protected.\n\n")
            TEXT("A ROW IS A RESOURCE, NOT A NODE TYPE. Several different node types — possibly from ")
            TEXT("several different mods — can yield the same resource, and one row covers all of them. ")
            TEXT("Untick a row and you hand back every node type that yields that resource, not just the ")
            TEXT("one you had in mind. A row can also name an ORDINARY game resource, which means some ")
            TEXT("mod added its own node type that yields it.\n\n")
            TEXT("WHEN A CHANGE TAKES EFFECT: THE NEXT TIME YOU LOAD THE SAVE. The list is read once ")
            TEXT("while the world is loading, before the other mod's cleanup runs, and is not re-read ")
            TEXT("while you play. Unticking in the pause menu changes nothing until you load again. ")
            TEXT("Unticking does NOT bring back nodes that have already been removed.\n\n")
            TEXT("THE TEXT BOX IS THE RESOURCE'S IDENTITY, not a label to edit. It is the full asset ")
            TEXT("path of the resource, which is what tells two mods' similarly-named ores apart. ")
            TEXT("Change it and the row stops matching anything and does nothing; the real resource ")
            TEXT("goes back to being protected and gets a fresh row. Delete a row and it comes back the ")
            TEXT("next time that resource is seen. Adding a row by hand does nothing unless the text ")
            TEXT("happens to be an exact resource path.\n\n")
            // ---- T68 (2026-08-11): THE CONSOLE INSTRUCTION IS GONE. ------------------------------
            // This paragraph used to tell the player to set TWO console variables -- the only place in
            // the whole panel that asked for the console. NodeShuffle.ProtectForeignNodes has been
            // deleted (it had no reachable state of its own: inert unless the master gate was on, and
            // already at its intended value), and the master gate is now the checkbox above.
            // BOTH REPLACEMENT CLAIMS ARE MEASURED, not assumed: the ticks are read only by the veto's
            // consumption predicate, which is never reached while the hook is observing; and the rows
            // are added from the same population pass in either mode (NodeShuffleForeignProtectConfig
            // .cpp), which is what the sentence after this one says.
            // T68 F4 introduced an AMBIGUITY THIS LINE HAS TO RESOLVE, and it is named here because the
            // F4 decision created it: the checkbox's new generic label is now a strict PREFIX of THIS
            // list's own title ("Protect Other Mods' Nodes (Per Resource)"), so an unqualified quote
            // could be read as pointing at the list the player is already reading about. The
            // parenthetical says which control. It adds no new claim -- "above" was already asserted by
            // the sentence this replaces, and rests on the same registration-order argument as every
            // other ordering statement in this file (runtime test step 2 is the decider, not C++).
            TEXT("NOTHING HERE APPLIES unless the \"Protect Other Mods' Nodes\" tick box above this ")
            TEXT("list is ticked. It is ON by default. A change to it takes effect at the next world ")
            TEXT("load. ")
            // T61 REPLACED THE SENTENCE THAT USED TO SIT HERE. It said the list "stays EMPTY" while
            // NodeShuffle.DestroyerVeto is 0 — true of the build that shipped it, and made FALSE by
            // T61, which arms the detection hook in either state precisely so the list fills before a
            // player has to decide anything. Both replacement claims are MEASURED, not assumed: rows
            // are added from the same population pass in both modes (NodeShuffleForeignProtectConfig
            // .cpp), and the ticks are only ever read by the veto's consumption predicate, which is
            // never reached while the hook is observing.
            TEXT("The list still FILLS UP while that setting is off — that is what lets you choose ")
            TEXT("before turning protection on — but every tick in it does nothing at all until it is ")
            TEXT("ticked and you load again. ")
            TEXT("Resource wells that NodeShuffle itself changed to a modded resource are ")
            TEXT("deliberately NOT listed here, and unticking a row can never affect one — they are ")
            TEXT("NodeShuffle's own doing, not another mod's."));

        // The element TEMPLATE. AddNewElement() clones this (NewObject with it as archetype), and
        // SectionProperties is an Instanced UPROPERTY, so each row gets its OWN String and Bool. The
        // population pass ASSERTS that at runtime rather than trusting it — see the pointer-identity
        // check in NodeShuffleForeignProtectConfig.cpp, which is the only static-analysis gap here.
        UConfigPropertySection* RowTemplate = NewObject<UConfigPropertySection>(Rows, SectionClass,
            TEXT("ForeignResourceRow"));
        RowTemplate->bRequiresWorldReload = true; // F10
        // COLD REVIEW F7, both halves.
        //  (a) THE TEMPLATE NOW CARRIES A FALLBACK LABEL. Every element created by
        //      UConfigPropertyArray::Deserialize is cloned from this template, so without it every row
        //      is unlabelled until a sync pass stamps it -- and sync runs only from ApplyLayout, which
        //      is authority-only and in-world, i.e. NEVER in the main-menu mod panel.
        //  (b) HasHeader IS NOW TRUE, where the first draft set it false. The two mechanisms
        //      contradicted each other: the design stamps a per-row DisplayName every sync, and a
        //      suppressed header is the most plausible place for that label to render. NEITHER HALF CAN
        //      BE PROVEN DEAD FROM C++ -- Widget_CP_Array and the CP_Section widgets are Blueprint --
        //      so nothing is deleted here; the two are made CONSISTENT instead, in the direction that
        //      makes the label reachable. RUNTIME TEST STEP 3 IS THE DECIDER: open the panel from the
        //      MAIN MENU and record whether a row shows its label.
        // TODO(2026-08-10, ns-t60 follow-up) — CLOSED 2026-08-11 by T67: test step 3 was run and NEITHER
        // mechanism is inert; both are slots of one SML format string (the measurement is in the T67
        // block below, which is the record). Nothing is deleted; the two slots now carry different parts.
        //  (c) RE-REVIEW B: HasHeader=true with HeaderText never set anywhere in Source/ is a THIRD
        //      plausible behaviour -- a blank header bar per row. There are three label surfaces
        //      (DisplayName, HeaderText, the child String's own label) and the sync stamped only one,
        //      so HeaderText is seeded here and stamped alongside DisplayName at both sync sites.
        //      The fallback reads "(resource)" rather than "Resource": a stamp failure must be
        //      distinguishable from a successful stamp, and it must not collide with the child field's
        //      own label.
        //
        // ---- T67 (2026-08-11): TEST STEP 3 HAS BEEN RUN, AND THE ANSWER WAS "BOTH". ----------------
        // The 2026-08-11 in-world panel rendered every row as `AllMinable: esc_Wire (AllMinable:
        // esc_Wire)` -- the SAME string twice, once in brackets. THE ROOT CAUSE IS MEASURED, not
        // inferred: SML's own section widget builds its header from a FormatText node whose literal
        // pattern is `{HeaderText} ({DisplayName})`. That string is present verbatim in
        // SML/Content/Interface/UI/Menu/Mods/ConfigProperties/Widgets/BaseClasses/Widget_CP_Section_Base
        // .uasset (byte-scan of the packaged asset, both encodings, 2026-08-11). So DisplayName and
        // HeaderText are not two candidate surfaces of which one is inert -- they are the two SLOTS OF
        // ONE FORMAT, and T65 stamped the same text into both.
        // NEITHER SURFACE IS DELETED, because deleting one cannot produce a clean single label: the
        // parentheses are literal in the format, so an empty slot renders as "X ()" or " (X)". The two
        // slots are given DIFFERENT PARTS of the same derivation instead -- header = the resource name,
        // brackets = the content it comes from -- which removes the repetition and keeps the mount
        // segment T65 declared load-bearing. NodeShuffleForeignProtectConfig.cpp's stamp helper is the
        // single writer of both, as it has been since re-review B.
        // NOT REACHABLE FROM C++, and not attempted: the header line's justification and the row's
        // horizontal slot sizing. UCP_Section exposes exactly WidgetType / HasHeader / HeaderText /
        // Collapsed (SML CP_Section.h) -- no alignment, no slot size -- so the left-justify and
        // compaction requests live entirely in SML's Blueprint widgets. See the T67 handoff.
        //
        // THE TEMPLATE FALLBACK reads as a two-slot pair for the same reason, and it must stay
        // distinguishable from a stamped row: a row that has been through the stamp names a resource,
        // an unstamped one says so. It states only what is true of the row -- no stamp has run -- and
        // never guesses at the resource.
        RowTemplate->DisplayName = FText::FromString(TEXT("no label stamped yet"));
        // T65: the TEMPLATE's tooltip is the fallback for a row no sync pass has stamped yet (the same
        // hole F7(a) fixed for the label -- sync runs only from ApplyLayout, which never runs in the
        // main-menu panel; T67 adds a main-menu stamp pass, and this text remains the fallback for a
        // row neither pass reached). It says where the identity is, and claims nothing about the
        // resource.
        RowTemplate->Tooltip = FText::FromString(
            TEXT("The full asset path in this row's text box is the resource's identity. The row label ")
            TEXT("is a trimmed form of it: the name in brackets says which content the asset comes ")
            TEXT("from."));
        if (UCP_Section* RowWidget = Cast<UCP_Section>(RowTemplate))
        {
            RowWidget->WidgetType = ECP_SectionWidgetType::CPS_Horizontal;
            RowWidget->HasHeader = true; // F7(b)
            RowWidget->HeaderText = FText::FromString(TEXT("(unlabelled row)")); // re-review B / T67
        }

        // ---- T65 ROW FIELD ORDER: THE TICK BOX IS DECLARED FIRST, ON PURPOSE. ----------------------
        // The author's design: the checkbox and its "Protected" label render at the LEFT of the row and
        // the resource label after them, so a short name and a long name line up identically instead of
        // the tick box drifting right with the text width. SectionProperties is a TMap and the row
        // widget is CPS_Horizontal, so the ONLY lever C++ has over field order is the order of these
        // two Add calls (TMap iteration is insertion order while nothing is removed, and nothing here
        // ever removes). WHETHER THE BLUEPRINT ROW WIDGET RENDERS IN SectionProperties ORDER IS NOT
        // PROVABLE FROM C++ -- Widget_CP_Section is Blueprint. Runtime test step 1 is the decider.
        //
        // SERIALIZATION IS UNAFFECTED AND THIS IS PROVABLE: UConfigPropertySection::Serialize writes a
        // KEYED object and Deserialize reads it back with ObjectValue->GetValue(Property.Key)
        // (SML ConfigPropertySection.cpp), so nothing about either is positional; the struct mirror
        // FillConfigStruct is keyed by name too. An existing NodeShuffle.cfg therefore round-trips
        // unchanged -- only the key ORDER inside each row's JSON object flips on the next save.
        // tools/check_t65_lint.ps1 pins this order; the T65ROUNDTRIP census line measures the reload.
        UConfigPropertyBool* ProtectedProp = NewObject<UConfigPropertyBool>(RowTemplate, BoolClass,
            TEXT("Protected"));
        ProtectedProp->Value = true;
        ProtectedProp->DefaultValue = true;
        ProtectedProp->DisplayName = FText::FromString(TEXT("Protected"));
        // T67: the fourth of the four "tries to remove" surfaces. Same regrade as the list tooltip above.
        ProtectedProp->Tooltip = FText::FromString(
            TEXT("Ticked: when another mod's node handler checks this resource's nodes, NodeShuffle ")
            TEXT("answers that check and refuses the handler's condition. Unticked: that handler's own ")
            TEXT("answer stands for this one resource. Takes effect the next time you load the save."));
        ProtectedProp->bRequiresWorldReload = true; // F10
        RowTemplate->SectionProperties.Add(TEXT("Protected"), ProtectedProp);

        UConfigPropertyString* ResourceProp = NewObject<UConfigPropertyString>(RowTemplate, StringClass,
            TEXT("Resource"));
        ResourceProp->Value = TEXT("");
        ResourceProp->DefaultValue = TEXT("");
        ResourceProp->DisplayName = FText::FromString(TEXT("Resource"));
        ResourceProp->Tooltip = FText::FromString(
            TEXT("The resource's full asset path. This is the row's identity — the tick box beside it ")
            TEXT("applies to whatever this path names. Editing it makes the row match nothing."));
        ResourceProp->bRequiresWorldReload = true; // F10
        RowTemplate->SectionProperties.Add(TEXT("Resource"), ResourceProp);

        Rows->DefaultValue = RowTemplate;
        Root->SectionProperties.Add(TEXT("ProtectedForeignResources"), Rows);
    }

    // ---------------------------------- RESOURCE WELLS (opt-in) ---------------------------------------
    AddBool(TEXT("ShuffleResourceWells"), false,
        TEXT("Shuffle Resource Wells (In Place)"),
        TEXT("OFF by default. When ON, each RESOURCE WELL is re-rolled to produce a different resource — a nitrogen well may become a water well, and so on. The wells themselves DO NOT MOVE: only what they yield changes, so your map knowledge still works.\n\nThe overall mix is preserved: the resources are dealt from the wells' own existing set, so a save never ends up short of a well-only resource such as Nitrogen Gas.\n\nWells that already have a Resource Well Pressurizer or any Resource Well Extractor on them are NEVER changed. Applied when the layout is rolled — turn this on and then use 'Re-roll Layout' to apply it to an existing save."));

    // Packet H2 (ns-wells-h2). A SECOND, separate toggle -- deliberately not folded into the one above.
    // Retyping a well in place and physically MOVING it are different promises with different risks,
    // and a player who accepted "my nitrogen well now makes water" has not thereby accepted "my
    // nitrogen well is now 4 km away". This one also carries a stage warning the other does not.
    AddBool(TEXT("RelocateResourceWells"), false,
        TEXT("Relocate Resource Wells (EXPERIMENTAL)"),
        TEXT("OFF by default and still experimental — leave it off if you want a quiet save.\n\nWhen ON, a whole resource well (its core and every satellite) is MOVED to a new place as a rigid body: the satellites keep their exact spacing and pattern relative to the core, and the whole group is rotated together to find an orientation that fits the terrain. A well is moved all-or-nothing — if the full footprint cannot be placed, no partial well appears at the new site, and once the mod gives up on the move for good it puts the original back.\n\nA relocated well is DRESSED AND BUILDABLE: its rocks and cracks are re-created at the new site, and a Resource Well Pressurizer and its Extractors snap to it and produce.\n\nTHE ORIGINAL IS REMOVED AS SOON AS THE WELL IS DEALT A DESTINATION, not when the new well appears. The replacement is only built once you travel to the new spot and the terrain there loads, so between those two moments the well is in NEITHER place: it is absent from the world. How long that lasts is NOT bounded — destinations are drawn across the whole map, so a well dealt somewhere you never go stays absent for as long as you do not go there.\n\nKNOWN LIMITS:\n- A well you have BUILT ON is never removed. A Resource Well Pressurizer on the core or any Resource Well Extractor on a satellite leaves that whole well exactly where it is, and the mod re-checks that on every pass, so the move happens by itself if you later remove the building.\n- RE-ROLLING RE-CONSIDERS A WELL THAT HAS ALREADY MOVED. It is taken from where it stands and dealt a new destination, like any other node, and it is absent until you travel to that new spot. On a save with many moved wells, most of them are gone from the map until you visit each new location. (A well you have built on is still never touched.)\n- A well moves with the satellites that had loaded when it was enrolled. If more of its satellites load later, they are left out of the moved well permanently — the well is smaller, and produces less, until you reload the save.\n- A relocated well claims a large build area, and that has not been tested against ordinary resource nodes closer than about 15 m. If a Miner will not place on an ordinary node right beside a relocated well, please report it — that case is untested. (For a Miner that will not place anywhere near a well, see the note at the top of this panel.)\n- Desert-biome wells are unverified and may arrive without their rock graphics.\n\nRequires 'Shuffle Resource Wells' to be on as well, and applies at ROLL time — turn both on, then use 'Re-roll Layout'."));

    // T68 (audit §5.2, §5.9): TWO WELL TOGGLES WERE DELETED HERE.
    //  * 'Re-roll Wells That Have Already Moved' (RerollRelocatedWells) is HARD-WIRED ON. A re-roll
    //    now re-considers a well that has already moved, like any other node -- the author's standing
    //    ruling that a shuffle hides ALL the things we shuffle. The first re-roll after this build
    //    churns most already-moved wells on an existing save; the tooltip above says what that means.
    //  * 'Remove A Moved Well At The Re-roll Itself' (CommitWellsAtRoll) is GONE, feature and all. The
    //    apply pass already initiates the hide for every entry marked as moving on every pass (T54), so
    //    the roll-time arm only moved the disappearance earlier by roughly one pass, at the cost of a
    //    one-shot capture that could not retry. Its opposite-polarity test pair (T23-A/T23-B) and
    //    tools/check_t23_writers.ps1 were retired in the same commit -- with the toggle gone the pair's
    //    question no longer exists, which is the one legitimate way a red/green pair dies.

    // ---------------------------------- TROUBLESHOOTING -----------------------------------------------
    // Kept on the panel deliberately (audit §5.11): it is the route a bug reporter uses, and "tick this
    // box, reload, send me the log" succeeds far more often than a console instruction.
    AddBool(TEXT("EnableDiagnostics"), false,
        TEXT("Enable Diagnostic Logging"),
        TEXT("OFF by default. Turn ON only when troubleshooting (e.g. to capture why a miner won't place): the mod writes verbose placement / node diagnostics to FactoryGame.log. Leave OFF for normal play — it keeps your log clean and avoids any overhead. The mod's actual fixes are always active whether this is on or off."));


    RootSection = Root;
}
