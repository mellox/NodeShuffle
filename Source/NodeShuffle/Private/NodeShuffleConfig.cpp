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
        TEXT("Posts a one-off chat message when a mod's extractor has been cleared for use on shuffled ")
        TEXT("nodes but needs a game restart to take effect. It appears at most once per new situation ")
        TEXT("and says nothing at all when there is nothing to say. Turn off to silence it."));

    AddBool(TEXT("EnableExperimentalFeatures"), false,
        TEXT("Enable Experimental Features"),
        // TODO(2026-08-08, pre-release): this tooltip says nothing is gated by this flag. Both
        // TODO(pre-release) sites in NodeShuffleAutoAllowExtractors.cpp plan to gate on it -- update
        // this string in the same commit that does.
        TEXT("A separate developer gate — it is NOT what the word EXPERIMENTAL means in other options' names. Nothing in this version is gated by this switch, so leaving it off changes nothing. A feature marked EXPERIMENTAL elsewhere in this list carries its own toggle and is not controlled from here."));

    RootSection = Root;
}
