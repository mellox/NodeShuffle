// ---- T60 (ns-t60-protect-checkboxes, 2026-08-10) -------------------------------------------------
// The per-resource opt-out list that configures T58's foreign-node protection.
//
// TWO HALVES, DELIBERATELY SEPARATED BY LIFECYCLE:
//   POPULATION  — SyncForeignResourceRowsToConfig, called from ApplyLayout. Writes rows INTO the live
//                 SML config tree for every foreign resource the veto has seen. Never removes a row,
//                 never reorders one; a player's tick is never touched by this side.
//   CONSUMPTION — IsForeignResourceProtectedByConfig, called from the veto's requirement evaluation,
//                 which runs hundreds of times inside the KBFL sweep that begins ~0.9 s AFTER world
//                 init (scoped re-review: that figure is a LATENCY, not the sweep's duration, which is
//                 unmeasured). It reads a LATCHED
//                 TSet<FString> and nothing else: no config-tree walk, no UObject traversal, no
//                 allocation. The set is built ONCE per world init by
//                 LatchForeignResourceOptOutsFromConfig, before the veto arms.
//
// WHY LATCHED RATHER THAN LIVE-POLLED. Three reasons, in order of weight. (1) It is the SAME rule
// NodeShuffle.ProtectForeignNodes and NodeShuffle.DestroyerVeto already publish -- "takes effect at
// world load" -- so one KBFL sweep can never be split across two policies. (2) The sweep this governs
// runs ~0.9 s after world init (docs/TECH-DEBT.md T58), so a change made in the pause menu could not
// affect the destroys of the session it was made in even if we re-read it live; a live-apply promise
// would be a false claim in the UI. (3) It removes any need for a config-save callback, which SML does
// not expose: UConfigManager::MarkConfigurationDirty queues a save and reinitialises the cached structs
// but broadcasts no delegate (ConfigManager.cpp:143-151).
//
// ROW IDENTITY is the resource descriptor class PATH. See FNodeShuffleSeenForeignResource in
// NodeShuffle.h for why, and docs/TECH-DEBT.md T60 for the population and default-protected policies.

#include "NodeShuffle.h"
#include "NodeShuffleConfig.h"

#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyArray.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyString.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h" // re-review B: HeaderText / HasHeader
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

// ---- session state. Game-thread only, exactly like the managed-node registry it sits beside. ----

// Every FOREIGN resource this world session has seen, keyed by resource class path. Grows only.
static TMap<FString, FNodeShuffleSeenForeignResource> GNodeShuffleSeenForeignResources;
// Bumped whenever a NEW key enters the map above. The population pass compares it against the value it
// last synced, so a steady-state ApplyLayout pass does no config work at all.
static int32 GNodeShuffleSeenForeignRevision = 0;
static int32 GNodeShuffleLastSyncedRevision = -1;

// S1 SUPPRESSION (see the ENodeShuffleNodeOrigin comment in NodeShuffle.h): a VANILLA-class resource
// well that NodeShuffle itself retyped to a modded resource classifies as Foreign and is protected --
// correctly, because it is protecting our own retype. It is NOT another mod's resource and must never
// be offered as a checkbox. Counted rather than silently dropped: a zero here on a save with relocated
// wells is itself evidence, and the count is the denominator that tells "none happened" from "the
// filter never ran".
static int32 GNodeShuffleS1SuppressedSightings = 0;
static TSet<FString> GNodeShuffleS1SuppressedPaths;

// The LATCHED opt-out set: resource paths whose row the player has UNTICKED. Absence means protected.
static TSet<FString> GNodeShuffleForeignOptOutPaths;
// Set by the latch so the census can report whether the latch ever ran for this world.
static bool GNodeShuffleForeignOptOutsLatched = false;

// Cumulative, this world session, for the census.
static int32 GNodeShuffleRowsAddedThisLoad = 0;
static int32 GNodeShuffleRowsLoadedFromDisk = -1; // -1 until the first sync measures it
// T65: label derivations for NEWLY SEEN resources this session, and how many of those could not be
// parsed into "<mount>: <name>" and fell back to the full path. Counted as a PAIR so the fallback count
// always prints with its denominator -- a bare "0 fallbacks" cannot tell a clean session from a session
// in which nothing was ever derived. Disk rows are counted separately, per pass, on the T65ROUNDTRIP
// line: those are a different population and merging them would hide which one had the failures.
static int32 GNodeShuffleLabelsDerivedThisSession = 0;
static int32 GNodeShuffleLabelFallbacksThisSession = 0;
// One-shot gate for the "list not reachable" warning. A file static reset per world session, NOT a
// function-local static: the population pass runs on the ApplyLayout cadence, so a per-pass warning is
// a firehose, while a process-lifetime one-shot would go silent for every world after the first.
static bool GNodeShuffleWarnedListUnreachable = false;

// ---- T65 LABEL DERIVATION: "<MountRoot>: <DescriptorName>" ---------------------------------------
// e.g. /KLib/Assets/.../Res_LiquidBioWater.Res_LiquidBioWater_C  ->  "KLib: LiquidBioWater"
//
// THE MOUNT SEGMENT IS LOAD-BEARING AND IS NEVER DROPPED. It is the FIRST path segment, which is
// exactly the string the Foreign classification itself is decided on (NodeShuffle.cpp:171-172 compares
// the same prefix against /Game/), so it is a measurement this list already rests on rather than a new
// claim. Without it two mods' similarly-named ores -- and a vanilla resource carried by a mod's node
// versus the same-named asset from a content pack -- render identically, which is the ambiguity T62/T63
// hit when a notice trimmed it away. For a base-game asset the segment reads "Game", which is the
// literal mount root and not an interpretation of it.
//
// THE TRIM IS DELIBERATELY TIMID. "_C" comes off (it is the Blueprint-generated-class suffix, always
// present on these paths); a leading "Desc_"/"Res_" comes off ONLY when the remainder is non-empty and
// starts with a letter, so nothing can trim a name down to "" or to a bare number. Anything else is
// left alone. RESIDUAL, NOT FIXED: two assets under one mount whose leaves differ only by which of the
// two prefixes they use would render the same label. Both surfaces that carry the identity -- the row's
// text box and its tooltip -- still show the full path, so the ambiguity is cosmetic; recorded here so
// nobody has to rediscover it.
//
// PARSE FAILURE FALLS BACK TO THE FULL PATH, never to a leaf: a label that cannot name its mount must
// not silently look like one that can. bOutParsed reports which happened; it is counted, with a
// denominator, on the T65LABEL and T65ROUNDTRIP lines.
static FString NodeShuffleMakeForeignResourceLabel(const FString& ResourceClassPath, bool* bOutParsed)
{
    if (bOutParsed) { *bOutParsed = false; }
    if (!ResourceClassPath.StartsWith(TEXT("/"), ESearchCase::CaseSensitive))
    {
        return ResourceClassPath;
    }
    // Mount root = text between the leading slash and the next one.
    const FString AfterLeadingSlash = ResourceClassPath.Mid(1);
    int32 SlashIndex = INDEX_NONE;
    if (!AfterLeadingSlash.FindChar(TEXT('/'), SlashIndex) || SlashIndex <= 0)
    {
        return ResourceClassPath;
    }
    const FString MountRoot = AfterLeadingSlash.Left(SlashIndex);

    // Descriptor name = text after the last '.', or after the last '/' when the path carries no object
    // suffix at all (a package-only path).
    FString Leaf = ResourceClassPath;
    int32 DotIndex = INDEX_NONE;
    if (Leaf.FindLastChar(TEXT('.'), DotIndex))
    {
        Leaf = Leaf.Mid(DotIndex + 1);
    }
    else
    {
        int32 LastSlash = INDEX_NONE;
        if (Leaf.FindLastChar(TEXT('/'), LastSlash)) { Leaf = Leaf.Mid(LastSlash + 1); }
    }
    if (Leaf.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive)) { Leaf = Leaf.LeftChop(2); }
    static const TCHAR* const TrimmablePrefixes[] = { TEXT("Desc_"), TEXT("Res_") };
    for (const TCHAR* Prefix : TrimmablePrefixes)
    {
        if (Leaf.StartsWith(Prefix, ESearchCase::CaseSensitive))
        {
            const FString Rest = Leaf.RightChop(FCString::Strlen(Prefix));
            // AN EXPLICIT A-Z/a-z TEST, NOT FChar::IsAlpha. MEASURED, not stylistic: IsAlpha on a wide
            // char compiles down to the UCRT's iswalpha, and the imports diff for this packet showed it
            // as a NEW entry in the DLL's import table (api-ms-win-crt-string-l1-1-0.dll) where the
            // whole packet is otherwise import-neutral. It is also the wrong predicate -- iswalpha is
            // locale-dependent, and what this test wants is "the remainder still starts like an asset
            // identifier". Asset path leaves are ASCII identifiers, so the range test IS the intent.
            const TCHAR First = (Rest.Len() > 0) ? Rest[0] : TEXT('\0');
            const bool bRestStartsWithLetter =
                (First >= TEXT('A') && First <= TEXT('Z')) || (First >= TEXT('a') && First <= TEXT('z'));
            if (bRestStartsWithLetter) { Leaf = Rest; }
            break;
        }
    }
    if (MountRoot.IsEmpty() || Leaf.IsEmpty())
    {
        return ResourceClassPath;
    }
    if (bOutParsed) { *bOutParsed = true; }
    return MountRoot + TEXT(": ") + Leaf;
}

void FNodeShuffleModule::ResetSeenForeignResources()
{
    // T61: the notice queue is reset from HERE rather than from its own world-init call site, so the
    // queue and the sighting registry can never describe two different world sessions.
    ResetForeignNoticeState();
    GNodeShuffleSeenForeignResources.Empty();
    GNodeShuffleSeenForeignRevision = 0;
    GNodeShuffleLastSyncedRevision = -1;
    GNodeShuffleS1SuppressedSightings = 0;
    GNodeShuffleS1SuppressedPaths.Empty();
    GNodeShuffleRowsAddedThisLoad = 0;
    GNodeShuffleRowsLoadedFromDisk = -1;
    GNodeShuffleWarnedListUnreachable = false;
    GNodeShuffleLabelsDerivedThisSession = 0;   // T65
    GNodeShuffleLabelFallbacksThisSession = 0;  // T65
}

void FNodeShuffleModule::NoteForeignResourceSighting(const FString& ResourceClassPath,
    const FString& NodeClassName, bool bNodeClassIsVanilla)
{
    // A node whose resource class could not be read carries no identity a row could be keyed on. The
    // classifier writes the literal "<null>" there; a row named that would match every such node of
    // every mod at once, which is exactly the population error this key was chosen to avoid.
    if (ResourceClassPath.IsEmpty() || ResourceClassPath == TEXT("<null>"))
    {
        return;
    }
    if (bNodeClassIsVanilla)
    {
        // S1: vanilla actor class + modded resource == our own well retype. Never a row.
        GNodeShuffleS1SuppressedSightings++;
        GNodeShuffleS1SuppressedPaths.Add(ResourceClassPath);
        return;
    }
    if (FNodeShuffleSeenForeignResource* Existing = GNodeShuffleSeenForeignResources.Find(ResourceClassPath))
    {
        Existing->Sightings++;
        return;
    }
    FNodeShuffleSeenForeignResource& Row = GNodeShuffleSeenForeignResources.Add(ResourceClassPath);
    Row.ResourceClassPath = ResourceClassPath;
    bool bLabelParsed = false;
    Row.DisplayName = NodeShuffleMakeForeignResourceLabel(ResourceClassPath, &bLabelParsed);
    ++GNodeShuffleLabelsDerivedThisSession;
    if (!bLabelParsed) { ++GNodeShuffleLabelFallbacksThisSession; }
    Row.FirstNodeClassName = NodeClassName;
    Row.Sightings = 1;
    GNodeShuffleSeenForeignRevision++;
    // T65: one line per NEWLY SEEN resource -- the same bound as the "added a protection row" line
    // below, i.e. once per distinct resource per session, not per evaluation. It reports the two
    // strings and the branch taken; it does not say why a parse failed, only that it did.
    // Arity hand-counted: 5 format specifiers, 5 arguments.
    UE_LOG(LogNodeShuffle, Verbose,
        TEXT("T65LABEL new-resource: path '%s' rendered as label '%s' (mount-and-name parse succeeded ")
        TEXT("%d). Derivations for newly seen resources this session %d, of which fell back to the ")
        TEXT("full path %d."),
        *ResourceClassPath, *Row.DisplayName, bLabelParsed ? 1 : 0,
        GNodeShuffleLabelsDerivedThisSession, GNodeShuffleLabelFallbacksThisSession);
}

bool FNodeShuffleModule::IsForeignResourceProtectedByConfig(const FString& ResourceClassPath)
{
    // DEFAULT PROTECTED (author's both-mods-work ruling): only an explicit unticked row opts out. An
    // empty set -- a world where the latch never ran, or a profile with no unticks -- therefore
    // protects everything, which is the same direction T58 shipped in.
    return !GNodeShuffleForeignOptOutPaths.Contains(ResourceClassPath);
}

void FNodeShuffleModule::GetSeenForeignResources(TArray<FNodeShuffleSeenForeignResource>& Out)
{
    Out.Reset(GNodeShuffleSeenForeignResources.Num());
    for (const TPair<FString, FNodeShuffleSeenForeignResource>& Pair : GNodeShuffleSeenForeignResources)
    {
        Out.Add(Pair.Value);
    }
}

// ---- live config tree access -------------------------------------------------------------------
// Both halves below walk the LIVE tree (a runtime duplicate of the CDO tree), never the CDO and never
// the struct mirror. GetConfigurationRootSection is the documented accessor for it.

namespace
{
    UConfigManager* NodeShuffleGetConfigManager(UObject* WorldContext)
    {
        if (!GEngine || !WorldContext) { return nullptr; }
        const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
        if (!World || !World->GetGameInstance()) { return nullptr; }
        return World->GetGameInstance()->GetSubsystem<UConfigManager>();
    }

    UConfigPropertyArray* NodeShuffleGetRowsArray(UObject* WorldContext, UConfigManager** OutManager)
    {
        UConfigManager* Manager = NodeShuffleGetConfigManager(WorldContext);
        if (OutManager) { *OutManager = Manager; }
        if (!Manager) { return nullptr; }
        UConfigPropertySection* Root = Manager->GetConfigurationRootSection(FConfigId{"NodeShuffle", ""});
        if (!Root) { return nullptr; }
        TObjectPtr<UConfigProperty>* Found = Root->SectionProperties.Find(TEXT("ProtectedForeignResources"));
        return Found ? Cast<UConfigPropertyArray>(Found->Get()) : nullptr;
    }

    // RE-REVIEW B: ONE function stamps ALL of a row's label surfaces, so the two sync sites cannot
    // drift apart. There are three surfaces in play -- the section's DisplayName, the section widget's
    // HeaderText (live because the template sets HasHeader), and the child String property's own label,
    // which is fixed schema text and deliberately not touched here. WHICH ONE THE WIDGET RENDERS IS A
    // BLUEPRINT QUESTION THIS FILE CANNOT ANSWER; runtime test 3 settles it, and the dated TODO in
    // NodeShuffleConfig.cpp says to delete the inert surfaces once it has.
    // T65 adds a FOURTH surface: the row section's own Tooltip, carrying the FULL PATH. The visible
    // label is now trimmed, so the identity has to remain readable somewhere -- and NOTHING IS DELETED
    // TO MAKE ROOM: the row's Resource text box still shows the path verbatim, which is the surface that
    // is certain to render, and this tooltip is the one the design asks for. Whether a SECTION tooltip
    // renders at all is a Blueprint question this file cannot answer (the same class of unknown as
    // DisplayName vs HeaderText); runtime test step 2 decides it, and until it has, the text box is the
    // guarantee. THIS FUNCTION IS STILL THE ONLY PLACE ANY ROW LABEL SURFACE IS WRITTEN, so the two
    // sync sites cannot drift -- that was re-review B's whole point and T65 keeps it.
    //
    // FullPath may be empty at the template-fallback call sites; an empty one leaves the tooltip alone
    // rather than stamping a blank over the template's fallback text.
    void NodeShuffleStampRowLabel(UConfigPropertySection* Section, const FString& Label,
        const FString& FullPath)
    {
        if (!Section) { return; }
        const FText LabelText = FText::FromString(Label);
        Section->DisplayName = LabelText;
        if (!FullPath.IsEmpty())
        {
            // EVERY ASSERTION GRADED. "full asset path" and "the row's identity" -- MEASURED: this is
            // the exact string the row's Resource property holds and the exact string the consumption
            // predicate compares (IsForeignResourceProtectedByConfig does a set lookup on it). "the name
            // before the colon is the content folder it comes from" -- MEASURED: the label is built from
            // the first path segment. NOT CLAIMED: anything about who authored the resource, whether it
            // is a base-game asset, or what the other mod would have done to it.
            Section->Tooltip = FText::FromString(FString::Printf(
                TEXT("%s\n\nThat is the resource's full asset path and this row's identity. In the row ")
                TEXT("label, the name before the colon is the content folder the asset comes from."),
                *FullPath));
        }
        if (UCP_Section* RowWidget = Cast<UCP_Section>(Section))
        {
            RowWidget->HeaderText = LabelText;
        }
    }

    // Reads one row. Returns false when the element is not the shape the schema declares -- which is
    // what a hand-mangled .cfg, or a future schema change, looks like from here.
    bool NodeShuffleReadRow(UConfigProperty* Element, UConfigPropertyString** OutResource,
        UConfigPropertyBool** OutProtected)
    {
        UConfigPropertySection* Section = Cast<UConfigPropertySection>(Element);
        if (!Section) { return false; }
        TObjectPtr<UConfigProperty>* ResourceEntry = Section->SectionProperties.Find(TEXT("Resource"));
        TObjectPtr<UConfigProperty>* ProtectedEntry = Section->SectionProperties.Find(TEXT("Protected"));
        UConfigPropertyString* ResourceProp = ResourceEntry ? Cast<UConfigPropertyString>(ResourceEntry->Get()) : nullptr;
        UConfigPropertyBool* ProtectedProp = ProtectedEntry ? Cast<UConfigPropertyBool>(ProtectedEntry->Get()) : nullptr;
        if (!ResourceProp || !ProtectedProp) { return false; }
        if (OutResource) { *OutResource = ResourceProp; }
        if (OutProtected) { *OutProtected = ProtectedProp; }
        return true;
    }
}

void FNodeShuffleModule::LatchForeignResourceOptOutsFromConfig(UObject* WorldContext)
{
    // Fresh world session. The sighting registry is module-static and outlives worlds, exactly like the
    // managed-node registry, so it is cleared here -- at the same moment the veto's own session
    // counters are reset -- rather than being left to accumulate across a return-to-menu.
    ResetSeenForeignResources();

    GNodeShuffleForeignOptOutPaths.Empty();
    GNodeShuffleForeignOptOutsLatched = false;

    UConfigPropertyArray* Rows = NodeShuffleGetRowsArray(WorldContext, nullptr);
    if (!Rows)
    {
        // Not fatal and not silent. Everything stays protected (the empty set is default-protected),
        // which is the fail-CLOSED direction, but a player's untick would be ignored -- so it is named.
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("T60: the per-resource protection list could not be read at world init (config array ")
            TEXT("not resolvable). Every foreign resource is treated as PROTECTED for this world, ")
            TEXT("including any the player unticked."));
        return;
    }

    int32 Malformed = 0;
    int32 Blank = 0;
    int32 UntickedRows = 0;
    for (UConfigProperty* Element : Rows->Values)
    {
        UConfigPropertyString* ResourceProp = nullptr;
        UConfigPropertyBool* ProtectedProp = nullptr;
        if (!NodeShuffleReadRow(Element, &ResourceProp, &ProtectedProp)) { ++Malformed; continue; }
        const FString Path = ResourceProp->Value.TrimStartAndEnd();
        if (Path.IsEmpty()) { ++Blank; continue; }
        if (!ProtectedProp->Value)
        {
            ++UntickedRows;
            GNodeShuffleForeignOptOutPaths.Add(Path);
        }
    }
    GNodeShuffleForeignOptOutsLatched = true;

    // Census part 1: what the world STARTED with, before any node was judged. Arity hand-counted:
    // 5 format specifiers, 5 arguments. Field names are explained in prose on purpose -- writing an
    // example in the printed shape would make a grep for the field match its own legend.
    // untickedRows and optOutPathsLatched are counted SEPARATELY and are not the same measurement:
    // rows are counted, paths are de-duplicated, so two rows naming one path make them differ. That
    // difference is the only evidence a duplicated row leaves.
    // COLD REVIEW F11: this field is 'rowsInFileAtLatch', NOT 'rowsInConfig' -- the sync line below uses
    // 'rowsInConfig' and a shared name across two lines cannot be counted separately by a grep.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("T60CENSUS latch: rowsInFileAtLatch %d untickedRows %d malformedRows %d blankRows %d ")
        TEXT("optOutPathsLatched %d"),
        Rows->Values.Num(),
        UntickedRows,
        Malformed,
        Blank,
        GNodeShuffleForeignOptOutPaths.Num());
}

void FNodeShuffleModule::SyncForeignResourceRowsToConfig(UObject* WorldContext)
{
    // Steady state costs one integer compare. The first pass of a world always runs (LastSynced is -1)
    // because loaded rows arrive without a DisplayName -- SML does not serialize it -- so the labels
    // must be stamped once even when no new resource has been seen.
    if (GNodeShuffleLastSyncedRevision == GNodeShuffleSeenForeignRevision)
    {
        return;
    }

    UConfigManager* Manager = nullptr;
    UConfigPropertyArray* Rows = NodeShuffleGetRowsArray(WorldContext, &Manager);
    if (!Rows || !Manager)
    {
        // Do NOT mark the revision as synced: a later pass (once the config subsystem is reachable)
        // must still get the chance to write these rows. The line is one-shot per world session --
        // this runs on the ApplyLayout cadence, and a per-pass warning would be a firehose for a
        // condition whose whole point is that it does not change between passes.
        if (!GNodeShuffleWarnedListUnreachable)
        {
            GNodeShuffleWarnedListUnreachable = true;
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("T60: the per-resource protection list is not reachable this pass; %d seen foreign ")
                TEXT("resource(s) not yet offered as rows. Retried silently every pass from here."),
                GNodeShuffleSeenForeignResources.Num());
        }
        return;
    }

    const bool bFirstSyncThisLoad = (GNodeShuffleLastSyncedRevision < 0);

    // Index what is already there. NEVER removed, NEVER reordered: a row the player may have set is
    // the player's, and the index is what makes the add idempotent across loads.
    // COLD REVIEW F8: THE SHARED-SUBOBJECT GUARD RUNS OVER *EVERY* ROW, not only the ones this pass
    // adds. Rows created by UConfigPropertyArray::Deserialize -- i.e. every row on every boot, which is
    // exactly the reload the in-game independence test exercises -- never pass through the add path, so
    // a guard sited there alone is silent when it matters most. Two distinct sharing modes are measured:
    //   (a) a row sharing a value object with the element TEMPLATE (the archetype-instancing failure);
    //   (b) two ROWS sharing one value object with each other (the failure a player actually sees:
    //       untick one, they all move). (b) is the stronger test and (a) cannot occur without it once
    //       there are two rows, but both are counted because they point at different causes.
    // These are MEASUREMENTS -- pointer identities compared equal. Neither says why.
    UConfigPropertyString* TemplateResource = nullptr;
    UConfigPropertyBool* TemplateProtected = nullptr;
    const bool bTemplateReadable = NodeShuffleReadRow(Rows->DefaultValue, &TemplateResource, &TemplateProtected);
    int32 SharedWithTemplateRows = 0;
    int32 SharedBetweenRows = 0;
    TSet<const void*> SeenValueObjects;

    TMap<FString, UConfigPropertySection*> ExistingByPath;
    int32 Malformed = 0;
    // T65, all four local to THIS pass. They describe the rows that were already in the array when this
    // pass started -- on the first pass of a world that is exactly the set read back from
    // NodeShuffle.cfg, which is what makes the T65ROUNDTRIP line below a reload measurement.
    int32 BlankPaths = 0;
    int32 RowsWithPathKey = 0;
    int32 LabelsDerivedThisPass = 0;
    int32 LabelFallbacksThisPass = 0;
    TArray<FString> FirstSyncLabels; // filled only on the first pass; the T65LABEL listing
    for (UConfigProperty* Element : Rows->Values)
    {
        UConfigPropertyString* ResourceProp = nullptr;
        UConfigPropertyBool* ProtectedProp = nullptr;
        if (!NodeShuffleReadRow(Element, &ResourceProp, &ProtectedProp)) { ++Malformed; continue; }
        if (bTemplateReadable && (ResourceProp == TemplateResource || ProtectedProp == TemplateProtected))
        {
            ++SharedWithTemplateRows;
        }
        bool bAlreadySeen = false;
        SeenValueObjects.Add(static_cast<const void*>(ResourceProp), &bAlreadySeen);
        if (bAlreadySeen) { ++SharedBetweenRows; }
        bAlreadySeen = false;
        SeenValueObjects.Add(static_cast<const void*>(ProtectedProp), &bAlreadySeen);
        if (bAlreadySeen) { ++SharedBetweenRows; }

        const FString Path = ResourceProp->Value.TrimStartAndEnd();
        if (Path.IsEmpty()) { ++BlankPaths; continue; }
        ++RowsWithPathKey;
        UConfigPropertySection* Section = Cast<UConfigPropertySection>(Element);
        ExistingByPath.Add(Path, Section);
        // Stamp the UI label every pass we run. DisplayName is not serialized, so a row read back from
        // NodeShuffle.cfg arrives unlabelled; re-stamping is idempotent and costs nothing on the passes
        // that do not run at all. RE-REVIEW B: HeaderText is stamped too -- the row template sets
        // HasHeader, and a header whose text nobody writes is a blank bar. Which of the two surfaces
        // the widget actually renders is a Blueprint question that runtime test 3 settles.
        // T65: derive once, use for the label AND count it. The FULL PATH goes to the tooltip; it is
        // still the row's stored key and this pass does not write it.
        bool bRowLabelParsed = false;
        const FString RowLabel = NodeShuffleMakeForeignResourceLabel(Path, &bRowLabelParsed);
        ++LabelsDerivedThisPass;
        if (!bRowLabelParsed) { ++LabelFallbacksThisPass; }
        if (bFirstSyncThisLoad) { FirstSyncLabels.Add(RowLabel); }
        NodeShuffleStampRowLabel(Section, RowLabel, Path);
    }
    if (bFirstSyncThisLoad)
    {
        GNodeShuffleRowsLoadedFromDisk = Rows->Values.Num();

        // ---- T65 ROUND-TRIP CENSUS: the proof that an existing NodeShuffle.cfg survived the rework ---
        // FIELD NAMES ARE DELIBERATELY DISTINCT FROM T60CENSUS's. T60CENSUS sync carries
        // rowsLoadedFromDisk and T60CENSUS latch carries rowsInFileAtLatch, both unchanged in name and
        // meaning by this packet; a shared field name across two lines cannot be counted separately by a
        // grep (cold review F11), so every field below is T65's own. What each one measures, in prose so
        // that no legend text can collide with the printed form: how many array elements existed when
        // this first pass started; how many of those carried a non-empty resource path (the key that
        // must survive a reload); how many were not the declared row shape; how many were the right
        // shape with an empty path; how many labels this pass derived from those rows; and how many of
        // those derivations could not be parsed and fell back to printing the full path.
        // A NON-ZERO diskRowsRead WITH AN EQUAL diskRowsWithPathKey IS THE ROUND-TRIP EVIDENCE.
        // Arity hand-counted: 6 format specifiers, 6 arguments.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("T65ROUNDTRIP first-sync: diskRowsRead %d diskRowsWithPathKey %d diskRowsMalformedShape ")
            TEXT("%d diskRowsBlankPath %d labelsDerivedFromDiskRows %d labelFallbacksToFullPath %d"),
            Rows->Values.Num(), RowsWithPathKey, Malformed, BlankPaths,
            LabelsDerivedThisPass, LabelFallbacksThisPass);

        // The listing. VERBOSE and once per world session: it is the only place the derived text itself
        // is visible, which is what makes a wrong trim readable in a log instead of only on screen. It
        // is CAPPED WITH ITS OWN DENOMINATOR -- a truncated list that did not say what it truncated
        // would be the same defect as a count without one.
        constexpr int32 MaxListedLabels = 60;
        FString Listing;
        for (int32 i = 0; i < FirstSyncLabels.Num() && i < MaxListedLabels; ++i)
        {
            Listing += FString::Printf(TEXT("\n    [%d] %s"), i, *FirstSyncLabels[i]);
        }
        // Arity hand-counted: 4 format specifiers, 4 arguments.
        UE_LOG(LogNodeShuffle, Verbose,
            TEXT("T65LABEL first-sync listing: showing %d of %d derived row label(s); %d of them fell ")
            TEXT("back to the full path because the mount-and-name parse failed.%s"),
            FMath::Min(FirstSyncLabels.Num(), MaxListedLabels), FirstSyncLabels.Num(),
            LabelFallbacksThisPass, *Listing);
    }

    // COLD REVIEW F9: A HARD CAP, because nothing prunes. A row whose mod is later uninstalled is KEPT
    // ON PURPOSE -- the player's untick must survive a temporary uninstall, and no code resolves the
    // path (it is a pure string compare), so a stale row costs a string and a line of UI, never a crash.
    // The cost of never pruning is unbounded growth across many mod sets, which the cap bounds instead.
    // TODO(2026-08-10, ns-t60 follow-up): decide whether stale rows should be prunable at all, and if so
    // by what evidence -- "the path no longer resolves" is NOT sufficient (an unloaded mod's path does
    // not resolve either). Recorded in docs/TECH-DEBT.md T60. Until then this cap is the whole policy.
    constexpr int32 MaxProtectionRows = 256;

    int32 AddedThisPass = 0;
    int32 RefusedOverCap = 0;
    int32 WithdrawnThisPass = 0; // R1 (scoped re-review): counted, so the Error line can report it
    for (const TPair<FString, FNodeShuffleSeenForeignResource>& Pair : GNodeShuffleSeenForeignResources)
    {
        if (ExistingByPath.Contains(Pair.Key)) { continue; }
        if (Rows->Values.Num() >= MaxProtectionRows) { ++RefusedOverCap; continue; }

        UConfigProperty* NewElement = Rows->AddNewElement();
        UConfigPropertyString* ResourceProp = nullptr;
        UConfigPropertyBool* ProtectedProp = nullptr;
        if (!NodeShuffleReadRow(NewElement, &ResourceProp, &ProtectedProp))
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("T60: AddNewElement produced an element that is not the declared row shape; ")
                TEXT("resource '%s' has no row this session."), *Pair.Key);
            if (NewElement) { Rows->RemoveElementAtIndex(Rows->Values.Num() - 1); }
            continue;
        }

        // COLD REVIEW F8, second half: WHEN THE GUARD FIRES ON A NEW ROW, DO NOT WRITE IT. The first
        // draft logged and then assigned anyway -- and on a build where the element shares the
        // template's value objects, that assignment writes straight through Rows->DefaultValue, so the
        // template itself would carry a resource path for the rest of the session and every later row
        // would clone it. The row is removed again and the resource simply has no row this session.
        if (bTemplateReadable && (ResourceProp == TemplateResource || ProtectedProp == TemplateProtected))
        {
            ++SharedWithTemplateRows;
            ++WithdrawnThisPass; // R1: the Error line reports this MEASUREMENT, never an assumption
            Rows->RemoveElementAtIndex(Rows->Values.Num() - 1);
            continue;
        }

        ResourceProp->Value = Pair.Key;
        ProtectedProp->Value = true; // default PROTECTED -- the author's both-mods-work ruling
        if (UConfigPropertySection* Section = Cast<UConfigPropertySection>(NewElement))
        {
            // T65: the stored key (Pair.Key, the full path) is what the tooltip carries; the trimmed
            // DisplayName is what the row shows. Both come from the same string.
            NodeShuffleStampRowLabel(Section, Pair.Value.DisplayName, Pair.Key); // re-review B: both surfaces
        }
        ++AddedThisPass;

        UE_LOG(LogNodeShuffle, Display,
            TEXT("T60: added a protection row for resource '%s' (label '%s', first seen on node class ")
            TEXT("'%s', %d sighting(s) so far). It starts PROTECTED; untick it in the mod settings and ")
            TEXT("reload the save to hand this resource back to the other mod's cleanup."),
            *Pair.Key, *Pair.Value.DisplayName, *Pair.Value.FirstNodeClassName, Pair.Value.Sightings);

        // T61 (ns-t61-observe-always): TELL THE PLAYER, once. The queue point is HERE and not at the
        // sighting on purpose -- reaching this line means the row was actually written, so the notice's
        // "it is now in your settings list" sentence is a report rather than a prediction. It is also
        // the whole cross-session dedup: a resource that already has a row never reaches this branch,
        // so it is never announced twice, with nothing persisted to remember it.
        FNodeShuffleModule::NoteUnlistedForeignResourceForNotice(Pair.Key, Pair.Value.DisplayName);
    }

    if (SharedWithTemplateRows > 0 || SharedBetweenRows > 0)
    {
        // A measurement, not an inference: the pointers compared equal. It does not say WHY, and it
        // does not distinguish a UE instancing failure from any other cause of pointer equality.
        // R1 (scoped re-review): the withdrawal is now a COUNTED FIELD, not a claim. The previous
        // wording asserted "any row added this pass was withdrawn" on a condition that includes
        // SharedBetweenRows -- and only the TEMPLATE branch withdraws, so two shared DISK rows with a
        // clean template made the line contradict `addedThisPass` on the very next census line.
        // Arity hand-counted: 3 format specifiers, 3 arguments.
        UE_LOG(LogNodeShuffle, Error,
            TEXT("T60: the protection list's rows do not hold independent values -- %d row(s) share a ")
            TEXT("value object with the element template and %d value object(s) are shared between two ")
            TEXT("rows (pointer identities compared equal). Rows withdrawn this pass rather than ")
            TEXT("written: %d. Report this with the build marker; do not rely on the ticks."),
            SharedWithTemplateRows, SharedBetweenRows, WithdrawnThisPass);
    }
    if (RefusedOverCap > 0)
    {
        // Arity hand-counted: 3 format specifiers, 3 arguments.
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("T60: the protection list is at its cap of %d row(s); %d seen resource(s) were not ")
            TEXT("offered a row this pass (of %d seen this session). Those resources stay PROTECTED -- ")
            TEXT("an unlisted resource is always protected -- but they cannot be handed back until rows ")
            TEXT("are freed."),
            MaxProtectionRows, RefusedOverCap, GNodeShuffleSeenForeignResources.Num());
    }

    if (AddedThisPass > 0)
    {
        // THE SAME THREE-CALL SEQUENCE ClearRerollNowFlag USES (NodeShuffleSubsystem.cpp:548-557) --
        // copied deliberately rather than re-derived, because that call site carries the finding that
        // the LIVE tree is properly outer'd (the CDO tree built in PostInitProperties is not), which is
        // what makes MarkDirty's Outer-chain walk reach the save handler at all.
        //   MarkDirty              -- walks the live property's Outer chain to the save handler.
        //   MarkConfigurationDirty -- queues the save AND calls ReinitializeCachedStructs, without which
        //                             FillConfigurationStruct keeps serving the stale cached struct for
        //                             every other config value the mod reads (ConfigManager.cpp:143-151).
        //   FlushPendingSaves      -- writes it now. Rows are added only on a resource's FIRST sighting,
        //                             so this runs a handful of times per save, not per pass.
        Rows->MarkDirty();
        Manager->MarkConfigurationDirty(FConfigId{"NodeShuffle", ""});
        Manager->FlushPendingSaves();
        GNodeShuffleRowsAddedThisLoad += AddedThisPass;
    }
    GNodeShuffleLastSyncedRevision = GNodeShuffleSeenForeignRevision;

    // Count the unticked rows from the LIVE tree rather than from the latched set: the latched set is
    // what this world is ACTING on, and these two numbers differing is precisely the evidence that a
    // player edited the list mid-session and it has not taken effect yet.
    int32 UntickedNow = 0;
    for (UConfigProperty* Element : Rows->Values)
    {
        UConfigPropertyString* ResourceProp = nullptr;
        UConfigPropertyBool* ProtectedProp = nullptr;
        if (!NodeShuffleReadRow(Element, &ResourceProp, &ProtectedProp)) { continue; }
        if (!ProtectedProp->Value) { ++UntickedNow; }
    }

    // Census part 2. Arity hand-counted: 9 format specifiers, 9 arguments. Every field is a count this
    // function measured on this pass; none of them states a cause. 'untickedInFileNow' vs
    // 'optOutsActingThisWorld' is the mid-session-edit tell described above.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("T60CENSUS sync: seenForeignResources %d rowsInConfig %d rowsLoadedFromDisk %d ")
        TEXT("rowsAddedThisLoad %d addedThisPass %d untickedInFileNow %d optOutsActingThisWorld %d ")
        TEXT("latchRan %d s1RetypedWellSightingsSuppressed %d"),
        GNodeShuffleSeenForeignResources.Num(),
        Rows->Values.Num(),
        GNodeShuffleRowsLoadedFromDisk,
        GNodeShuffleRowsAddedThisLoad,
        AddedThisPass,
        UntickedNow,
        GNodeShuffleForeignOptOutPaths.Num(),
        GNodeShuffleForeignOptOutsLatched ? 1 : 0,
        GNodeShuffleS1SuppressedSightings);

    if (Malformed > 0)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("T60: %d row(s) in the protection list are not the declared shape and were skipped. ")
            TEXT("The resources they were meant to name are treated as PROTECTED."), Malformed);
    }
    if (GNodeShuffleS1SuppressedPaths.Num() > 0)
    {
        // Not a problem -- the designed S1 filter firing. Reported so a reader can tell "the filter
        // saw nothing" from "the filter never ran", which a bare zero above cannot.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("T60: %d distinct resource(s) reached the veto on a VANILLA node class and were kept ")
            TEXT("out of the list (%d sighting(s)) -- those are resource wells NodeShuffle itself ")
            TEXT("retyped, not another mod's resources."),
            GNodeShuffleS1SuppressedPaths.Num(), GNodeShuffleS1SuppressedSightings);
    }
}
