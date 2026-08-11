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

    AddBool(TEXT("UnlockModdedKnowledge"), true,
        TEXT("Unlock Scanner Knowledge For Shuffled Modded Resources"),
        TEXT("Shuffled modded resources are registered with the resource scanner so scanners recognize them. NOTE: some overhaul mods also gate MINER PLACEMENT on scanner knowledge, so this can let you place miners on modded ores before that overhaul's own research would have allowed it (crafting/recipes stay gated as normal). Turn off if you prefer each mod's own progression to grant this. Vanilla resources are never affected."));

    AddBool(TEXT("EnableDiagnostics"), false,
        TEXT("Enable Diagnostic Logging (Experimental)"),
        TEXT("OFF by default. Turn ON only when troubleshooting (e.g. to capture why a miner won't place): the mod writes verbose placement / node diagnostics to FactoryGame.log. Leave OFF for normal play — it keeps your log clean and avoids any overhead. The mod's actual fixes are always active whether this is on or off."));

    AddBool(TEXT("EnableStarterNodes"), true,
        TEXT("Starter Nodes Near Spawn"),
        TEXT("On a BRAND-NEW game only, place a small starter set (2 Iron, 2 Limestone, 1 Copper, Pure purity) near where you spawn, so the early game is always playable no matter how the shuffle moved the world's nodes. Taken from the shuffled pool when possible. Never added to an existing save."));

    AddInt(TEXT("StarterNodeRadiusMeters"), 200, 50, 1000,
        TEXT("Starter Node Radius (m)"),
        TEXT("How far from your spawn point the starter nodes may be placed. Smaller keeps them right at your feet; larger spreads them out. Only used on a brand-new game's first roll."));

    AddBool(TEXT("ShuffleResourceWells"), false,
        TEXT("Shuffle Resource Wells (In Place)"),
        TEXT("OFF by default. When ON, each RESOURCE WELL is re-rolled to produce a different resource — a nitrogen well may become a water well, and so on. The wells themselves DO NOT MOVE: only what they yield changes, so your map knowledge still works.\n\nThe overall mix is preserved: the resources are dealt from the wells' own existing set, so a save never ends up short of a well-only resource such as Nitrogen Gas.\n\nWells that already have a Resource Well Pressurizer or any Resource Well Extractor on them are NEVER changed. Applied when the layout is rolled — turn this on and then use 'Re-roll Layout' to apply it to an existing save."));

    // Packet H2 (ns-wells-h2). A SECOND, separate toggle -- deliberately not folded into the one above.
    // Retyping a well in place and physically MOVING it are different promises with different risks,
    // and a player who accepted "my nitrogen well now makes water" has not thereby accepted "my
    // nitrogen well is now 4 km away". This one also carries a stage warning the other does not.
    AddBool(TEXT("RelocateResourceWells"), false,
        TEXT("Relocate Resource Wells (EXPERIMENTAL)"),
        TEXT("OFF by default and still experimental — leave it off if you want a quiet save.\n\nWhen ON, a whole resource well (its core and every satellite) is MOVED to a new place as a rigid body: the satellites keep their exact spacing and pattern relative to the core, and the whole group is rotated together to find an orientation that fits the terrain. A well is moved all-or-nothing — if the full footprint cannot be placed, no partial well appears at the new site, and once the mod gives up on the move for good it puts the original back.\n\nA relocated well is DRESSED AND BUILDABLE: its rocks and cracks are re-created at the new site, and a Resource Well Pressurizer and its Extractors snap to it and produce.\n\nTHE ORIGINAL IS REMOVED AS SOON AS THE WELL IS DEALT A DESTINATION, not when the new well appears. The replacement is only built once you travel to the new spot and the terrain there loads, so between those two moments the well is in NEITHER place: it is absent from the world. How long that lasts is NOT bounded — destinations are drawn across the whole map, so a well dealt somewhere you never go stays absent for as long as you do not go there.\n\nKNOWN LIMITS:\n- A well you have BUILT ON is never removed. A Resource Well Pressurizer on the core or any Resource Well Extractor on a satellite leaves that whole well exactly where it is, and the mod re-checks that on every pass, so the move happens by itself if you later remove the building.\n- Re-rolling does NOT move a well that has already moved, unless you turn on 'Re-roll Wells That Have Already Moved' below. With that option off, only wells that have not moved yet are dealt a new destination.\n- A well moves with the satellites that had loaded when it was enrolled. If more of its satellites load later, they are left out of the moved well permanently — the well is smaller, and produces less, until you reload the save.\n- A relocated well claims a large build area, and that has not been tested against ordinary resource nodes closer than about 15 m. If a Miner will not place on an ordinary node right beside a relocated well, please report it — that case is untested. (For a Miner that will not place anywhere near a well, see the note at the top of this panel.)\n- Desert-biome wells are unverified and may arrive without their rock graphics.\n\nRequires 'Shuffle Resource Wells' to be on as well, and applies at ROLL time — turn both on, then use 'Re-roll Layout'."));

    // T7b (ns-t7b-reroll). A THIRD well toggle, and it earns its place for the same reason the second
    // one did: "a well may move" and "a well I have already found may move again" are different
    // promises. DEFAULT OFF also means the behaviour ships dark -- an existing save keeps today's
    // semantics until the player asks for the new ones, which matters because the first re-roll after
    // it is enabled re-considers EVERY already-moved well at once.
    AddBool(TEXT("RerollRelocatedWells"), false,
        TEXT("Re-roll Wells That Have Already Moved"),
        TEXT("OFF by default. Decides what 'Re-roll Layout' does to wells that have ALREADY moved.\n\n")
        TEXT("OFF: a well that has already moved keeps its spot, and only wells that have not moved yet ")
        TEXT("are dealt a destination. This is how the mod behaved before this option existed.\n\n")
        TEXT("ON: an already-moved well is re-considered on every re-roll, like an ordinary node - it is ")
        TEXT("taken from where it stands and dealt a new destination.\n\n")
        TEXT("WHAT TO EXPECT ON AN EXISTING SAVE: every well that has already moved is re-considered on ")
        TEXT("the FIRST re-roll after you turn this on - not a few of them. The save this was developed ")
        TEXT("against held 17 moved wells out of 20.\n\n")
        TEXT("A RE-ROLLED WELL DISAPPEARS UNTIL YOU GO AND FIND IT. The old well is removed the moment ")
        TEXT("you press Re-roll Layout, but the new one is not built until you travel to its new spot ")
        TEXT("and the terrain loads - exactly like a well moving for the first time. Turn this on with ")
        TEXT("many moved wells and most of your wells will be gone from the map until you visit each new ")
        TEXT("location. The log line 'WELLH2-ROLL: re-enrolment teardown' reports how many groups were ")
        TEXT("removed and how long that frame took.\n\n")
        TEXT("WHAT STILL DOES NOT MOVE: a well you have built on. A Resource Well Pressurizer on the ")
        TEXT("core, or any Resource Well Extractor on a satellite, keeps the well exactly where it is. ")
        TEXT("That check reads the well you can actually see and build on, and it is taken a second time ")
        TEXT("immediately before anything is removed.\n\n")
        TEXT("LIMITS YOU CAN HIT:\n")
        TEXT("- A well is only re-considered while its ORIGINAL satellites are loaded. Re-roll from far ")
        TEXT("away and it keeps its current spot for that roll.\n")
        TEXT("- Each well gets a limited number of draws to find a destination that clears water and ")
        TEXT("spacing (the log's 'deal-failed' count says how often a well ran out). A well that runs ")
        TEXT("out keeps the spot it already has: nothing is removed for it that roll.\n")
        TEXT("- A moved well is re-captured from its original satellites, so satellites that loaded ")
        TEXT("since it moved are included this time and the well can come back a different size.\n")
        TEXT("- A moved well is only re-considered while the place it currently stands has been loaded ")
        TEXT("this session. If it has not, the log says '0 of N member handle(s)' and the well keeps its ")
        TEXT("spot.\n\n")
        TEXT("Requires 'Shuffle Resource Wells' and 'Relocate Resource Wells' to be on as well, and ")
        TEXT("applies at ROLL time - turn it on, then use 'Re-roll Layout'."));

    // T23 stage 3 (ns-t23-rollhide). A FOURTH well toggle. It changes no destination and no placement --
    // only WHEN the original is removed -- which is why it is not folded into either of the two above.
    //
    // EVERY FACTUAL ASSERTION IN THE TEXT BELOW IS GRADED, because this panel has shipped false claims
    // three times (workspace CLAUDE.md, "UI copy is a CLAIM"). Graded MEASURED-IN-CODE: the hide happens
    // at the roll commit; the replacement is still gated on IsLocationNearAnyPlayer + a settled footprint,
    // unchanged; a member reporting IsWellMemberInUse is refused by HideOne on both paths; an entry whose
    // capture is incomplete at the roll takes the old path; a terminally-failed entry records a persisted
    // un-hide intent that is re-attempted every apply pass and can only complete while the ORIGINAL actor
    // is resident. NO DURATION IS CLAIMED anywhere in this text: the deferral window has never been
    // measured to a bound, and stage 0 measured 0 of 17 dealt wells placed on the author's own save.
    // ns-t54-immediate-hide REWRITE. EVERY CLAIM BELOW IS RE-GRADED, because this option's old text is a
    // worked example of the failure mode this panel keeps hitting: its OFF paragraph -- "a well that has
    // not moved yet keeps standing until its replacement has actually been built" -- was true when it was
    // written and was made FALSE by a change in a different file. Immediate removal is now what the mod
    // does with this option off, so the only thing this option still decides is whether the removal
    // happens at the instant of the re-roll or a few seconds later on the next maintenance pass.
    // Graded MEASURED-IN-CODE: the apply pass initiates the hide for every entry marked as moving
    // (NodeShuffleWellRelocateApply.cpp, the immediate-hide arm); the roll path hides earlier only for
    // entries whose look is completely captured at that instant; a member reporting in-use is refused on
    // both paths through one shared predicate; a well left absent with nothing working on it now has its
    // original restored (the same file arms the persisted intent). NO DURATION IS CLAIMED: the apply
    // cadence is not a promise this text may make, and the deferral window has never been bounded.
    AddBool(TEXT("CommitWellsAtRoll"), false,
        TEXT("Remove A Moved Well At The Re-roll Itself (EXPERIMENTAL)"),
        TEXT("OFF by default, and it now makes very little difference. This option no longer decides ")
        TEXT("WHETHER the original well is removed early - the mod removes it on its own once the well ")
        TEXT("has been dealt a destination and you are near enough for the original to be loaded. It ")
        TEXT("only decides whether that happens during the re-roll ")
        TEXT("itself or on the mod's next maintenance pass shortly afterwards.\n\n")
        TEXT("OFF: the original is removed on the next maintenance pass after the re-roll, and for a ")
        TEXT("well whose own area is not loaded at that moment, on the first pass after it does load. ")
        TEXT("This path retries every pass, so it reaches every well that was dealt a destination and ")
        TEXT("that you have not built on.\n\n")
        TEXT("ON: the removal is additionally attempted during the re-roll itself, for wells whose ")
        TEXT("appearance the mod has fully recorded at that instant. Recording during the re-roll happens ")
        TEXT("once and is not retried, so wells it does not manage are simply removed by the OFF path ")
        TEXT("instead. The log line 'WELLH2-ROLLHIDE' names which wells took which route.\n\n")
        TEXT("WHAT THE REMOVAL MEANS EITHER WAY: the replacement is only built once you travel to the new ")
        TEXT("location and the terrain there loads. Between the removal and that moment the well is in ")
        TEXT("NEITHER place: it is absent from the world. HOW LONG THAT LASTS IS NOT BOUNDED. Destinations ")
        TEXT("are drawn at random across the whole map, on purpose, so a well dealt somewhere you never go ")
        TEXT("stays absent for as long as you do not go there.\n\n")
        TEXT("WHAT IS NEVER REMOVED: a well you have built on. A Resource Well Pressurizer on the core, or ")
        TEXT("a Resource Well Extractor on any satellite, leaves that whole well exactly where it is, with ")
        TEXT("this option on or off. The mod re-checks it every pass, so removing the building lets the ")
        TEXT("move happen by itself.\n\n")
        TEXT("IF A WELL ENDS UP ABSENT WITH NOTHING WORKING ON IT, the mod puts its original back. That ")
        TEXT("covers a well that can never be placed anywhere, and a well left absent because you turned ")
        TEXT("'Relocate Resource Wells' or 'Shuffle Resource Wells' off. The attempt only runs while the ")
        TEXT("original's own area is loaded, so in practice it completes when you are near it again. ONE ")
        TEXT("CASE IT DOES NOT COVER: a well that keeps failing to assemble at its destination - some of ")
        TEXT("its pieces are standing there, so putting the original back would give you two partial ")
        TEXT("wells. That case is counted in the log line 'WELLH2-STRANDED', which must read zero.\n\n")
        TEXT("Requires 'Shuffle Resource Wells' and 'Relocate Resource Wells' to be on as well."));

    // ns-h1b-notice, anti-nag rule 7: the opt-out. Default TRUE deliberately -- see the struct comment.
    AddBool(TEXT("ShowCompatibilityNotices"), true,
        TEXT("Show Compatibility Notices In Chat"),
        // T61 WIDENED THIS TOOLTIP because a second notice now rides on this same switch, and a
        // description that named only the first would have understated what the switch turns off.
        // Both sentences describe what the code does: the extractor notice is the ns-h1b-notice pass,
        // the resource notice is queued only when a row is actually added to the list below.
        TEXT("Posts a one-off chat message when a mod's extractor has been cleared for use on shuffled ")
        TEXT("nodes but needs a game restart to take effect. It also tells you once when another mod's ")
        TEXT("resource has been added to the protection list further down this page, so you can decide ")
        TEXT("whether to protect it. Both appear at most once per new situation and say nothing at all ")
        TEXT("when there is nothing to say. Turn off to silence them."));

    AddBool(TEXT("EnableExperimentalFeatures"), false,
        TEXT("Enable Experimental Features"),
        // TODO(2026-08-08, pre-release): this tooltip says nothing is gated by this flag. Both
        // TODO(pre-release) sites in NodeShuffleAutoAllowExtractors.cpp plan to gate on it -- update
        // this string in the same commit that does.
        TEXT("A separate developer gate — it is NOT what the word EXPERIMENTAL means in other options' names. Nothing in this version is gated by this switch, so leaving it off changes nothing. A feature marked EXPERIMENTAL elsewhere in this list carries its own toggle and is not controlled from here."));

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
        Rows->Tooltip = FText::FromString(
            TEXT("THIS LIST FILLS ITSELF IN — you do not add to it. A resource appears here after ")
            TEXT("NodeShuffle has seen another mod's cleanup try to remove nodes of that resource at ")
            TEXT("least once, so the list starts empty and grows as you play with other mods.\n\n")
            TEXT("TICKED (the default for everything, including anything not listed yet): NodeShuffle ")
            TEXT("stops that removal, so the other mod's ore nodes survive.\n\n")
            TEXT("UNTICKED: NodeShuffle does not step in for that resource, and the other mod's cleanup ")
            TEXT("runs at that hook. Everything else in the list stays protected.\n\n")
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
            TEXT("NOTHING HERE APPLIES unless the protection feature is on, and it needs BOTH console ")
            TEXT("variables: NodeShuffle.DestroyerVeto is OFF by default and must be set to 1, and ")
            TEXT("NodeShuffle.ProtectForeignNodes is already 1. Both take effect at the next world load. ")
            // T61 REPLACED THE SENTENCE THAT USED TO SIT HERE. It said the list "stays EMPTY" while
            // NodeShuffle.DestroyerVeto is 0 — true of the build that shipped it, and made FALSE by
            // T61, which arms the detection hook in either state precisely so the list fills before a
            // player has to decide anything. Both replacement claims are MEASURED, not assumed: rows
            // are added from the same population pass in both modes (NodeShuffleForeignProtectConfig
            // .cpp), and the ticks are only ever read by the veto's consumption predicate, which is
            // never reached while the hook is observing.
            TEXT("The list still FILLS UP while NodeShuffle.DestroyerVeto is 0 — that is what lets you ")
            TEXT("choose before turning protection on — but every tick in it does nothing at all until ")
            TEXT("you set that variable to 1 and load again. ")
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
        // TODO(2026-08-10, ns-t60 follow-up): once test step 3 has been run, delete whichever of the
        // two mechanisms it proves inert and record the measurement in docs/TECH-DEBT.md T60.
        //  (c) RE-REVIEW B: HasHeader=true with HeaderText never set anywhere in Source/ is a THIRD
        //      plausible behaviour -- a blank header bar per row. There are three label surfaces
        //      (DisplayName, HeaderText, the child String's own label) and the sync stamped only one,
        //      so HeaderText is seeded here and stamped alongside DisplayName at both sync sites.
        //      The fallback reads "(resource)" rather than "Resource": a stamp failure must be
        //      distinguishable from a successful stamp, and it must not collide with the child field's
        //      own label.
        RowTemplate->DisplayName = FText::FromString(TEXT("(resource)"));
        // T65: the TEMPLATE's tooltip is the fallback for a row no sync pass has stamped yet (the same
        // hole F7(a) fixed for the label -- sync runs only from ApplyLayout, which never runs in the
        // main-menu panel). It says where the identity is, and claims nothing about the resource.
        RowTemplate->Tooltip = FText::FromString(
            TEXT("The full asset path in this row's text box is the resource's identity. The row label ")
            TEXT("is a trimmed form of it: the name before the colon is the content folder the asset ")
            TEXT("comes from."));
        if (UCP_Section* RowWidget = Cast<UCP_Section>(RowTemplate))
        {
            RowWidget->WidgetType = ECP_SectionWidgetType::CPS_Horizontal;
            RowWidget->HasHeader = true; // F7(b)
            RowWidget->HeaderText = FText::FromString(TEXT("(resource)")); // re-review B
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
        ProtectedProp->Tooltip = FText::FromString(
            TEXT("Ticked: NodeShuffle keeps this resource's nodes alive when another mod's cleanup ")
            TEXT("tries to remove them. Unticked: that cleanup is allowed to proceed for this one ")
            TEXT("resource. Takes effect the next time you load the save."));
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

    RootSection = Root;
}
