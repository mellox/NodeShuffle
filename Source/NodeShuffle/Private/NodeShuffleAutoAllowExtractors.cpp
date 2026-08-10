// Packet G (ns-automatch, branch feature/extractor-automatch): NodeShuffle.AutoAllowExtractors.
//
// THE RULE: allow an extractor onto SF+'s allow-list if it NATIVELY accepts a node type NodeShuffle
// MANAGES. Replaces the hand-picked KDF enumeration (three known victims blocked purely because nobody
// enumerated them: Build_ReactiveOreExtractorMk3_C, Build_OilPump_Mk2_C, build_oilmk4_C) with a rule
// that generalises to any future mod/tier/rename.
//
// MECHANISM DECISION (see the packet report for the full evidence): generates a machine-written
// KDataForge pack under DataForge/NodeShuffleAutoAllow/ instead of mutating SF+'s live PDA array at
// runtime. MEASURED from this machine's own FactoryGame.log AND from KAPI's shipped PDB (cold review,
// ns-review-g §1): KDataForge's "Initial load finished" and KAPI's allow-list scan both fire during
// GAME-INSTANCE INIT, ~270 ms apart, before any world exists -- AND the PDB proves KAPI's disqualifier
// consults its OWN cached `UKAPIDataAssetSubsystem::mAllowedResourceExtractors` TSet, built once at boot
// by `ScanForAllowList()`, never the live `UKAPIExtractorAllowList::mAllowedExtractors` TArray on the PDA
// (a DIFFERENT property on a DIFFERENT object). Runtime mutation would not merely be racy -- it would
// write to the wrong object entirely. Writing a KDF document instead costs one boot to take effect
// (identical to how the existing hand-written pack already works) and touches nothing outside
// NodeShuffle's own DataForge subtree. Two bonuses the same PDB measurement closes (ns-review-g §1):
// `mAllowedExtractors`'s inner is a HARD `FClassProperty`, not soft -- Packet F's soft-reference worry
// does not apply to this specific property; and because the consumer is a TSet, a duplicate class-path
// append is collapsed by KAPI itself -- PROVABLY harmless, not merely assumed-safe.
//
// FORM-AGNOSTIC BY DESIGN: oil is the user's deliberate untouched CONTROL for whether this generalises.
// No line in this file names "oil", "liquid", or "pump" -- the decision reasons only from
// FNodeShuffleModule::EvaluateExtractorAcceptance's generic form/resource/class fields, and the
// extractor CLASS list comes from AFGRecipeManager::GetAvailableBuildingsOfType<AFGBuildableResourceExtractorBase>(),
// which is equally native to Miners and Pumps (no separate C++ subclass distinguishes them). Confirmed
// clean by the cold review (ns-review-g §9).
//
// UNLOCK-SCOPING (explicit property, not a surprise): GetAvailableBuildingsOfType() only returns
// buildings the player can currently BUILD (their recipe/schematic is unlocked). An extractor whose
// schematic hasn't been researched yet will not appear this pass -- it is simply not yet a candidate,
// and (since bAutoAllowExtractorsDone re-arms on every re-roll) it becomes one the next time this pass
// runs after that schematic unlocks. No entry is ever permanently missed; it is only ever deferred.
//
// ns-review-g G1 (CRITICAL fix): the managed-node census now comes from the ROLLED LAYOUT
// (ANodeShuffleSubsystem::BuildManagedNodeGroupsFromLayout), NOT from live/spawned actors. The review
// proved the previous live-actor census was streaming-scoped (NodeShuffle spawns relocated nodes lazily,
// only within ~600 m of a player) and, combined with the pass latching on first success, could produce a
// sparse, arbitrary, non-deterministic result that a LATER pass could then use to DELETE correct
// entries. The layout is the per-save source of truth, dealt in full at roll time regardless of
// streaming -- walking it directly makes the census complete and deterministic. See
// BuildManagedNodeGroupsFromLayout's own comment (NodeShuffleSubsystem.cpp) for the full argument.
//
// WHY LATCHING IS STILL CORRECT under the new design: bAutoAllowExtractorsDone latches once per load and
// re-arms on every re-roll (NodeShuffleSubsystem.cpp, alongside bKnowledgeUnlockDone). The layout-based
// census is a pure function of Layout, and Layout only CHANGES at RollLayout (which already resets the
// latch) -- so re-running mid-session, between re-rolls, would recompute the identical census and
// produce the identical result. Latching is therefore not a scope limitation anymore; it is the correct
// "run exactly when the input can have changed" cadence.
//
// WHY CLEAR-AND-REBUILD IS NOW SAFE: previously, clearing before regenerating could delete entries a
// PREVIOUS, richer pass had correctly produced (if this pass's live-actor sample was sparser). Now that
// the census is deterministic per Layout, every completed pass within the same roll produces the SAME
// ToGenerate set (modulo extractors that unlock/de-list between passes) -- so clearing first is safe
// idempotent regeneration. NARROW SCOPE (ns-review-g2 F1): that argument covers the CENSUS only.
// ToGenerate has two other inputs that can collapse it to empty independently of the layout -- an
// unreliable SF+ allow-list read, and a census that resolves no groups at all -- and clearing on either
// of those WOULD be the old deletion vector with a new cause. Both are therefore checked and made
// non-destructive (return false, keep the existing pack, retry) immediately before the clear, NOT
// argued away here.
//
// MEASURED FALSIFICATION, AND THE THIRD INPUT (2026-07-30). The paragraph above enumerated TWO non-layout
// inputs that can collapse ToGenerate to empty. There was a THIRD, and it was the one that actually
// fired: the loop used to SKIP any extractor already present in SF+'s allow-list array -- an array our
// OWN generated pack appends to. Measured across three real boots: boot A generated 7 documents; boot B
// saw all 7 as already-present, skipped all 7, produced an EMPTY ToGenerate, and the clear-and-rebuild
// below DELETED the entire pack (log: "pass complete -- 0 matched, 0 document(s) WRITTEN", leaving a
// lone 627-byte pack.yml); boot C had nothing and regenerated. The pack oscillated every other boot, and
// the "every completed pass within the same roll produces the SAME ToGenerate set" claim above was FALSE
// for exactly this reason. The skip is gone (see the OSCILLATION FIX comment in the loop): PDA
// membership is now REPORTED as sfPlusAlreadyAllows=%d and never acted on, which makes ToGenerate a pure
// function of (available extractors x managed node groups) and restores that claim. The general lesson,
// worth more than the fix: AN INPUT DERIVED FROM THIS PASS'S OWN PREVIOUS OUTPUT IS A FEEDBACK LOOP, NOT
// A SHORT-CIRCUIT. Do not add one back.

#include "NodeShuffle.h"
#include "NodeShuffleExtractorDiscovery.h"
#include "NodeShuffleSubsystem.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Buildables/FGBuildableResourceExtractorBase.h"
#include "Buildables/FGBuildableFrackingActivator.h" // fracking crash guard (mirrors NodeShuffle.cpp)
#include "Buildables/FGBuildableFrackingExtractor.h"
// H1b: the NODE side of the pairing rule. Both are UCLASS(Abstract) bases -- BP_FrackingCore_C /
// BP_FrackingSatellite_C derive from them -- so IsChildOf against StaticClass() is the hierarchy test
// the packet requires, with no class name or /Game/... path anywhere in the decision.
#include "Resources/FGResourceNodeFrackingCore.h"
#include "Resources/FGResourceNodeFrackingSatellite.h"
#include "FGRecipeManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"

// Gate. Default ON in THIS DEV BUILD so the user can boot and test the whole mechanism without a
// rebuild -- their rollback/A-B lever (0 fully restores current, pre-Packet-G behaviour: the pass is
// skipped AND any previously-generated pack is deleted, so a stale generated entry can never outlive
// the CVar being turned off). TODO(pre-release): before any public release, move this behind the
// existing EnableExperimentalFeatures config flag (default false) -- default ON here is deliberate for
// this dev build only, per the workspace convention that the stable core ships with experimental
// features off.
static int32 GNodeShuffleAutoAllowExtractors = 1;
static FAutoConsoleVariableRef CVarNodeShuffleAutoAllowExtractors(
    TEXT("NodeShuffle.AutoAllowExtractors"),
    GNodeShuffleAutoAllowExtractors,
    TEXT("1 (default, THIS DEV BUILD ONLY) = once per world load, generate a machine-written KDataForge ")
    TEXT("pack that allow-lists any extractor NodeShuffle's own LAYOUT census shows natively accepts a ")
    TEXT("node type it manages -- takes effect on the NEXT boot. 0 = fully restores current behaviour: ")
    TEXT("the pass is skipped and any previously-generated pack is deleted. TODO(pre-release): gate ")
    TEXT("behind EnableExperimentalFeatures (default false) before public release."),
    ECVF_Default);

namespace
{
    // DataForge/NodeShuffleAutoAllow/ -- a pack directory EXCLUSIVELY owned by this generator, and since
    // 2026-07-31 the ONLY NodeShuffle pack there is. The hand-written NodeShuffleSFPlus/ pack this comment
    // used to contrast against was RETIRED in 1ab8868, for two reasons worth stating so nobody restores it
    // after finding it in git history: (a) its live entries were already covered by this generator's rule,
    // so it was duplicated by something that self-maintains; (b) DataForge/ is in no build mirror list, so
    // every build DELETED the deployed copy and only a manual re-sideload brought it back -- which is
    // exactly what bit the user (four builds, no re-sideload, KDF loading 3 packs/63 documents instead of
    // 6/79, Mk8 Miner and Reactive Ore Extractor red in-game). Nothing under DataForge/ now needs a human
    // to put it back. This path is hardcoded here and never depended on that pack. Computed from
    // FPaths::ProjectDir() (already-linked symbol, confirmed by Packet F's own import list) the SAME way
    // KDataForge itself finds every mod's DataForge root (measured: FactoryGame.log's own
    // "Found 7 DataForge root(s) under <ProjectDir>/" line enumerates exactly this pattern).
    FString GetAutoAllowPackDir()
    {
        return FPaths::Combine(FPaths::ProjectDir(), TEXT("Mods/NodeShuffle/DataForge/NodeShuffleAutoAllow"));
    }

    // Derives the SML "ModReference" (a generated document's extra hasMod entry) from an object path
    // like "/AlkaLib/Buildables/.../Build_X_C" -- the first path segment. The rule, stated on its own terms
    // rather than by citation (the hand-written pack that used to document it was retired in 1ab8868):
    // SML's ModReference IS the mod's folder / .uplugin stem, and a mounted mod's content root is that
    // same stem, so the leading path segment of any object inside it is the ModReference. Empty for vanilla
    // (/Game/) or native (/Script/) content, which needs nothing beyond the pack's base gate.
    FString DeriveModReferenceFromPath(const FString& ObjectPath)
    {
        if (!ObjectPath.StartsWith(TEXT("/"))) { return FString(); }
        if (ObjectPath.StartsWith(TEXT("/Game/")) || ObjectPath.StartsWith(TEXT("/Script/"))) { return FString(); }
        const FString Rest = ObjectPath.RightChop(1);
        int32 SlashIdx = INDEX_NONE;
        if (Rest.FindChar(TEXT('/'), SlashIdx) && SlashIdx > 0) { return Rest.Left(SlashIdx); }
        return FString();
    }

    // Sanitizes a class path into a filesystem-safe stem (the class's own leaf name is already a valid
    // identifier -- e.g. "Build_ReactiveOreExtractorMk3_C" -- so this just extracts that leaf).
    FString ExtractorClassStem(const FString& ObjectPath)
    {
        FString Left, Right;
        if (ObjectPath.Split(TEXT("."), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
        {
            return Right;
        }
        return ObjectPath;
    }
}

void FNodeShuffleModule::LogAutoAllowExtractorsState()
{
    UE_LOG(LogNodeShuffle, Display,
        TEXT("AUTOALLOW: NodeShuffle.AutoAllowExtractors=%d (%s). %s"),
        GNodeShuffleAutoAllowExtractors,
        GNodeShuffleAutoAllowExtractors != 0 ? TEXT("armed for this session") : TEXT("disabled"),
        GNodeShuffleAutoAllowExtractors != 0
            ? TEXT("TODO(pre-release): move behind EnableExperimentalFeatures before public release.")
            : TEXT("current (pre-Packet-G) behaviour fully restored; any previously-generated pack will be deleted."));
}

bool FNodeShuffleModule::RunAutoAllowExtractorsIfEnabled(UWorld* World,
    TArray<FNodeShufflePendingEntry>* OutPending)
{
    const FString PackDir = GetAutoAllowPackDir();
    // ns-review-g G8 / ns-review-g2 F4: OWNERSHIP ASSERT AT THE SEAM. This function DELETES a directory
    // tree inside the game install; prove ownership before any delete rather than asserting it in a
    // comment. NOTE the deviation from the reviewer's original block: it tested FPaths::IsRelative(PackDir)
    // == false, but FPaths::ProjectDir() in a PACKAGED game is relative to the executable
    // (canonically "../../../FactoryGame/"), so that clause would have failed here and -- because the
    // branch returns true -- silently LATCHED the whole pass off. Convert to a full path first and assert
    // on the converted form, which is correct whether ProjectDir() is relative or absolute.
    {
        const FString ProjDir = FPaths::ProjectDir();
        const FString FullPackDir = FPaths::ConvertRelativePathToFull(PackDir);
        const bool bOwned = !ProjDir.IsEmpty()
            && !FPaths::IsRelative(FullPackDir)
            && FullPackDir.EndsWith(TEXT("NodeShuffleAutoAllow"), ESearchCase::CaseSensitive)
            && FullPackDir.Contains(TEXT("Mods/NodeShuffle/DataForge"), ESearchCase::IgnoreCase);
        if (!bOwned)
        {
            UE_LOG(LogNodeShuffle, Error,
                TEXT("AUTOALLOW: REFUSING to act -- computed pack dir '%s' (full '%s', ProjectDir='%s') ")
                TEXT("failed the ownership assert (must resolve absolute, sit under Mods/NodeShuffle/")
                TEXT("DataForge, and end in 'NodeShuffleAutoAllow'). Nothing written, nothing deleted."),
                *PackDir, *FullPackDir, *ProjDir);
            return true; // completed -- do not retry a path that will not change
        }
    }

    if (GNodeShuffleAutoAllowExtractors == 0)
    {
        // ns-review-g G2: every filesystem result is checked -- a rollback lever that reports success
        // it did not achieve is worse than no lever.
        if (IFileManager::Get().DirectoryExists(*PackDir))
        {
            if (IFileManager::Get().DeleteDirectory(*PackDir, /*RequireExists=*/false, /*Tree=*/true))
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("AUTOALLOW: disabled -- DELETED the previously-generated pack at '%s' (full rollback, verified)"), *PackDir);
            }
            else
            {
                UE_LOG(LogNodeShuffle, Error,
                    TEXT("AUTOALLOW: disabled -- FAILED to delete the previously-generated pack at '%s'. ")
                    TEXT("ROLLBACK IS INCOMPLETE: KDataForge will still apply that pack on the next boot. ")
                    TEXT("Delete the directory by hand. Will retry next tick."), *PackDir);
                return false;
            }
        }
        else
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("AUTOALLOW: disabled -- nothing to do"));
        }
        return true; // completed (nothing to retry)
    }

    if (!World)
    {
        UE_LOG(LogNodeShuffle, Warning, TEXT("AUTOALLOW: no world -- skipped this pass (will retry)"));
        return false;
    }

    // AFGRecipeManager::Get() -- the reviewer's recommended, registry-free discovery mechanism (public
    // template, FGRecipeManager.h:117; unlock-scoped by design, see the file header). May legitimately
    // not exist yet this early -- mirrors ANodeShuffleSubsystem::UnlockModdedScannerKnowledge()'s own
    // "not ready, retry next tick" idiom for the SAME class of dependency.
    AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World);
    if (!RecipeManager)
    {
        // ns-review-g G10: Verbose is invisible in the shipping log, which would make a PERMANENT stall
        // here indistinguishable from "the pass was never called". Log the first N at Display so the
        // retry is observable, then fall back to Verbose so a slow-but-working boot stays quiet.
        static int32 NotReadyLogCount = 0;
        if (NotReadyLogCount < 5)
        {
            ++NotReadyLogCount;
            UE_LOG(LogNodeShuffle, Display,
                TEXT("AUTOALLOW: recipe manager not ready yet -- retry next tick (attempt %d; if you never ")
                TEXT("see an 'AUTOALLOW: %%d resource-extractor building(s)' line, this is where it stalled)"),
                NotReadyLogCount);
        }
        else
        {
            UE_LOG(LogNodeShuffle, Verbose, TEXT("AUTOALLOW: recipe manager not ready yet -- retry next tick"));
        }
        return false;
    }

    // ns-review-g G1: the subsystem owns Layout and BuildManagedNodeGroupsFromLayout. Not finding it yet
    // is the SAME class of "world still settling" dependency as the recipe manager above -- retry.
    ANodeShuffleSubsystem* Subsystem = nullptr;
    for (TActorIterator<ANodeShuffleSubsystem> It(World); It; ++It) { Subsystem = *It; break; }
    if (!Subsystem)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("AUTOALLOW: NodeShuffle subsystem not found in this world yet -- retry next tick"));
        return false;
    }

    const FSfPlusAllowListReadout AllowList = ReadSfPlusAllowList();
    if (!AllowList.bAssetFound)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("AUTOALLOW: SF+ allow-list asset not found -- SF+ not installed, nothing to auto-allow. Completed."));
        return true;
    }

    // ns-review-f F3 (Packet F) raised: GetObjectPropertyValue does not force a load, so a soft-class
    // entry that hasn't been loaded reads as null (UNRESOLVED), making "not on the list" unverified.
    // ns-review-g §1 MEASURED (from KAPI's shipped PDB) that mAllowedExtractors's inner is a HARD
    // FClassProperty, not soft -- so UnresolvedEntries should read 0 in practice, and even if it didn't,
    // the consumer is a TSet, so a duplicate append is PROVABLY collapsed harmlessly by KAPI itself, not
    // merely assumed-safe. The refusal below is therefore over-conservative but costs nothing (fail-
    // closed) and is kept as a belt-and-braces guard against a future property-shape change.
    const bool bAllowListUnreliable = !AllowList.bPropertyReadable || AllowList.UnresolvedEntries > 0;

    TArray<TSubclassOf<AFGBuildableResourceExtractorBase>> AvailableExtractors =
        RecipeManager->GetAvailableBuildingsOfType<AFGBuildableResourceExtractorBase>();
    UE_LOG(LogNodeShuffle, Display,
        TEXT("AUTOALLOW: %d resource-extractor building(s) currently available (unlock-scoped -- an ")
        TEXT("un-researched extractor simply isn't a candidate yet, not a permanent miss) | SF+ allow-list ")
        TEXT("%d entries, %d resolved, %d UNRESOLVED (%s)"),
        AvailableExtractors.Num(), AllowList.ArrayEntryCount, AllowList.AllowedExtractorClasses.Num(),
        AllowList.UnresolvedEntries, bAllowListUnreliable ? TEXT("UNRELIABLE -- no new entries this pass") : TEXT("reliable"));

    // ns-review-g G1: LAYOUT-based census (streaming-independent), not a live-actor walk. See the file
    // header and BuildManagedNodeGroupsFromLayout's own comment for the full argument.
    TArray<FNodeShuffleManagedGroup> NodeGroups;
    int32 TotalActiveEntries = 0;
    int32 UnresolvedActiveEntries = 0;
    Subsystem->BuildManagedNodeGroupsFromLayout(NodeGroups, TotalActiveEntries, UnresolvedActiveEntries);
    // ns-truth-diagnostics A2. The old text read "(streaming-independent -- does not depend on where the
    // player loaded or what has streamed in)". That is true of THE DERIVATION and false of the reading
    // every human takes, which is that the LAYOUT'S CONTENTS are streaming-independent. It demonstrably
    // misled: an orchestrator quoted it to the user as "the whole map is dealt at once" (see
    // _team/nodeshuffle-followups/lithium-extractor-investigation.md H3). Both halves are now explicit,
    // and the one-word summary is gone so it cannot be quoted on its own.
    // truthdiag-fixes F2: the first version of this rewrite hard-coded OUR measurements into the RUNTIME
    // STRING ("630 of 630 resolved in one frame, three separate boots", "28 of them"). Those constants
    // would print unchanged in every player's world, on every map, forever -- the same defect class this
    // packet exists to delete, just true-of-our-machine instead of false. THE RULE: print the predicate
    // FROM THIS RUN. A borrowed measurement hard-coded into a log string is the same failure with a
    // longer fuse. The live split now comes from ROLLCENSUS, which counts this world.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("AUTOALLOW: %d managed node group(s) derived from the ROLLED LAYOUT. TWO SEPARATE CLAIMS, ")
        TEXT("BOTH STATED: (1) THE DERIVATION is streaming-independent -- this pass reads Layout, never ")
        TEXT("live actors, so it returns the SAME answer wherever the player is standing and whatever is ")
        TEXT("loaded right now (that is what ns-review-g G1 fixed). (2) THE LAYOUT'S CONTENTS ARE NOT -- ")
        TEXT("Layout was captured by a LIVE actor scan at ROLL time, so anything not in the world at that ")
        TEXT("instant is not in it and contributes no group here, until a re-roll re-scans. The two ")
        TEXT("populations that behave differently at that instant are LEVEL-PLACED nodes and nodes ")
        TEXT("RUNTIME-SPAWNED BY OTHER MODS -- for THIS world's actual split, grep the ROLLCENSUS line of ")
        TEXT("this run, which counts both from the live world; docs/TECH-DEBT.md records what we measured ")
        TEXT("on our own machine, which is not a measurement of yours. ")
        TEXT("%d active layout entries considered, %d could not be resolved to a resource/node class and ")
        TEXT("were skipped."),
        NodeGroups.Num(), TotalActiveEntries, UnresolvedActiveEntries);
    if (UnresolvedActiveEntries > 0)
    {
        // Residual, much narrower scope limitation than the old streaming gap (ns-review-g G1's honesty-
        // log recommendation, kept for this remaining case): a class that fails to resolve THIS pass
        // (e.g. a mod's content momentarily unavailable) contributes no group and is not retried until
        // the next full pass (next load/re-roll) -- not "never", but not "this tick" either.
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("AUTOALLOW: %d active layout entries' resource/node class could not be resolved this pass ")
            TEXT("-- they are NOT reflected in the %d group(s) above and will not be reconsidered until the ")
            TEXT("next load/re-roll."), UnresolvedActiveEntries, NodeGroups.Num());
    }

    struct FGeneratedDoc
    {
        FString ExtractorPath;
        FString OwningMod; // may be empty (vanilla)
        FString MatchedNodeClass;
        FString MatchedResourceClass;
        int32 MatchedForm = -1;
        // ns-h1b-notice: the two fields the player notice needs, and NOTHING the decision reads.
        // bAlreadyAllowed is the PENDING signal itself -- a document we wrote for a class SF+ already
        // permits is not pending on anything, it is already live. Recorded here, at the point the
        // decision already computed it, rather than re-derived later against a possibly-changed read.
        const UClass* ExtractorClass = nullptr;
        bool bAlreadyAllowed = false;
    };
    TArray<FGeneratedDoc> ToGenerate;

    for (const TSubclassOf<AFGBuildableResourceExtractorBase>& ExtCls : AvailableExtractors)
    {
        UClass* RawCls = ExtCls.Get();
        if (!RawCls) { continue; }
        const AFGBuildableResourceExtractorBase* Ext = RawCls->GetDefaultObject<AFGBuildableResourceExtractorBase>();
        if (!Ext)
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("AUTOALLOW extractor='%s' decision=SKIP reason=no-CDO"), *RawCls->GetPathName());
            continue;
        }
        const FString ExtPath = RawCls->GetPathName();

        // ================= H1b (ns-h1b, 2026-07-31): FAIL-CLOSED *PAIRING* RULE =================
        // Replaces a blanket "skip every fracking-derived class" guard that used to sit HERE, before the
        // group-matching loop ever ran. Read this whole block before touching it: it is the
        // highest-consequence decision in this file.
        //
        // WHY IT CHANGED. Design decisions 2 and 3 (both signed off 2026-07-30, neither reviewed against
        // the other) are mutually exclusive: 2 says managing wells auto-allow-lists fracking machines "by
        // the rule", 3 says leave the blanket guard alone -- and the blanket guard made 2 unreachable. That
        // is not theory; it is MEASURED and user-visible. On 2026-07-31 the user retyped wells successfully
        // and still could not place bamrenew's machines on them:
        //   AUTOALLOW extractor='...build_frqking_C'        decision=SKIP reason=fracking-extractor-crash-guard sfPlusAlreadyAllows=0
        //   AUTOALLOW extractor='...build_pressuresqtmk5_C' decision=SKIP reason=fracking-extractor-crash-guard sfPlusAlreadyAllows=0
        //
        // WHAT THE GUARD PROTECTS, PRECISELY. A fracking machine (Resource Well Pressurizer =
        // AFGBuildableFrackingActivator, satellite Resource Well Extractor = AFGBuildableFrackingExtractor;
        // both UCLASS(Abstract), so every concrete well -- vanilla or MODDED -- is a subclass) does a
        // CHECKED cast of the node to a fracking node type in OnExtractableResourceSet
        // (FGBuildableFrackingExtractor.h declares that override). On one of our ordinary Node-type nodes
        // that cast is a FATAL, user-reported crash. So the invariant worth holding is NOT "never
        // allow-list a fracking machine" -- it is "never allow-list a machine that could reach that cast
        // with a NON-fracking node". On a genuine BP_FrackingSatellite_C / BP_FrackingCore_C the cast
        // succeeds, so that case was never the hazard.
        //
        // THE PREDICATE, AND WHY IT TESTS *BOTH* SIDES. Two axes were candidates:
        //   (i)  the matched GROUP's node class -- is this evidence coming from a fracking node?
        //   (ii) the EXTRACTOR's own mRestrictToNodeType -- is this machine natively confined to fracking
        //        nodes EVERYWHERE, not merely compatible with the one group we happened to match first?
        // BOTH are required, and (i) alone is NOT sufficient. mRestrictToNodeType is a bare
        // TSubclassOf<AFGResourceNodeBase> with no native default (FGBuildableResourceExtractorBase.h:140-142
        // -- read directly), so a fracking-derived class may legitimately leave it UNSET or set it to a
        // broad base. Such a machine is still bDiscriminated on mAllowedResourceForms alone, so under (i)
        // alone it would match a fracking group whose resource is liquid/gas, get allow-listed, and then --
        // its native node-type rule restricting nothing -- be placeable on one of OUR ordinary liquid
        // nodes, reaching the checked cast with a non-fracking node. That is the original crash, re-opened
        // through the front door by the very change meant to be safe. (ii) closes it: the restriction must
        // itself be a class confined to the fracking hierarchy, which makes "cannot be placed on a
        // non-fracking node" a property of the MACHINE rather than of our matching order.
        // MEASURED, so this is not a hypothetical cost: both bamrenew classes report hasRestriction=1
        // (FactoryGame.log 2026-07-30 19:59:53, the last boot before the blanket guard existed), so (ii) is
        // expected to PASS for them. What is NOT yet measured is WHICH class they restrict to -- the old
        // SKIP line never printed it. The FRACKPAIR line below prints it, so one boot settles it either way.
        //
        // PER-KIND, NOT "ANY FRACKING NODE" (design SS Q4's table): a Pressurizer belongs on the CORE, a
        // Well Extractor on a SATELLITE. Accepting a cross-paired group would be evidence of the wrong
        // shape, so the required node base is chosen from the machine's kind.
        //
        // FAIL CLOSED, EVERYWHERE -- unknown means skip. A null node class, a class that is not a child of
        // the required base, an unset or broader restriction, and the both-bases case all leave the pairing
        // false. Nothing is inferred from a name or a path: the whole decision is IsChildOf against
        // StaticClass(). (A path/name test was already rejected once on measured evidence -- it would have
        // mis-skipped Build_PneuMk1_C and missed the misspelled build_frqking_C.)
        //
        // WHAT THIS PACKET DELIBERATELY DOES NOT TOUCH: the two hologram hooks in NodeShuffle.cpp
        // (NodeShuffleIsFrackingExtractor + the IsAllowedOnResource / CanOccupyResource subscriptions).
        // They are a SEPARATE, still-blanket defence -- they withhold our Scope.Override(true) for EVERY
        // fracking machine on EVERY one of our nodes -- and they, not this list, are what prevents the
        // crash at placement time. Allow-listing is not the last block. Both defences must hold
        // independently, and this packet narrows only this one.
        //
        // WHAT THIS IS AND IS NOT (ns-review-frack, 2026-07-30, boot-2 MEASURED) -- do not let a later
        // reader mistake this for a crash fix. Every bullet below described the BLANKET guard and is kept
        // because each is a measurement, not an opinion; they are the reason narrowing this guard is safe
        // rather than brave:
        //  * MEASURED: on the profile this was written against it caught six classes
        //    (vanilla + MkPlus Smasher/Extractor, bamrenew build_frqking_C / build_pressuresqtmk5_C) and
        //    changed ZERO decisions -- all six were already being skipped, four as already-in-PDA-array
        //    and two as no-managed-node-type-natively-accepted. It averted nothing observed.
        //  * MEASURED: Build_PneuMk1/2/3_C (PneumaticFrackingMachine), the classes whose boot-1 ADD
        //    prompted this guard, do NOT derive from either base -- the guard does not fire for them and
        //    they still ADD. The alarm was the mod's NAME. They are ordinary resource extractors and were
        //    never exposed to the checked cast.
        //  * Allow-listing is NOT the last block, so this is NOT what keeps a well off our nodes. Vanilla
        //    Build_FrackingSmasher_C / Build_FrackingExtractor_C are ALREADY on SF+'s list (SF+ ships them)
        //    and do not crash on our nodes -- the extractor's own NATIVE node-type rule rejects them, and
        //    the KAPI allow-list does not touch that rule. Correspondingly, for anything SF+ already
        //    allows, our declining to append it protects NOTHING: we only ever append, never remove.
        //  * THE REAL GAP IS ELSEWHERE AND IS STILL OPEN: NodeShuffleIsFrackingExtractor is PASSIVE -- it
        //    withholds our Override and lets the native check decide. A fracking-derived class that is
        //    already SF+-approved AND whose native rule accepts our node would still place, and still
        //    crash. Closing that needs Scope.Override(false) in the two hologram hooks, not this guard.
        //    Do not treat this line as covering that case.
        // THE STRUCTURAL INVARIANT, RESTATED FOR H1b. The blanket guard's stated value was that "the
        // generated pack never contains a fracking-derived class" held as a structural property rather than
        // as an accident of how today's skips happen to land. That sentence is NO LONGER TRUE and must not
        // be left standing: this pack may now contain a fracking-derived class. The invariant that replaces
        // it, and that carries the same weight, is:
        //     the generated pack never contains a fracking-derived class UNLESS that class is natively
        //     confined to the fracking node hierarchy AND the evidence for it came from a fracking node
        //     group of the matching kind.
        // Still decided on the buildable CLASS -- the same axis, though no longer the same predicate, as
        // the hologram-side guard applies to GetBuildClass(). The hologram side stays BLANKET on purpose
        // (see the "does not touch" paragraph above); the two are allowed to differ because they answer
        // different questions -- "may this be on a list" vs "may we waive the native check right now".
        const bool bFrackActivator = RawCls->IsChildOf(AFGBuildableFrackingActivator::StaticClass());
        const bool bFrackExtractor = RawCls->IsChildOf(AFGBuildableFrackingExtractor::StaticClass());
        const bool bFrackingDerived = bFrackActivator || bFrackExtractor;

        // The node class this machine's evidence MUST come from, chosen per kind (design SS Q4's table).
        // Stays NULL for the (currently impossible -- UCLASSes are single-inheritance) case of a class
        // deriving from BOTH bases, which makes every pairing below fail closed instead of guessing.
        // NOTE (ns-h1b-notice): this local is now for the LOG LINE only. The DECISION lives in
        // FNodeShuffleModule::ClassifyFrackingPairing, which derives the same base the same way -- see
        // that function for why the rule had to become shared rather than stay inline here.
        const UClass* RequiredFrackingNodeBase = nullptr;
        if (bFrackActivator && !bFrackExtractor)
        {
            RequiredFrackingNodeBase = AFGResourceNodeFrackingCore::StaticClass();
        }
        else if (bFrackExtractor && !bFrackActivator)
        {
            RequiredFrackingNodeBase = AFGResourceNodeFrackingSatellite::StaticClass();
        }

        // Axis (ii), evaluated ONCE per extractor because it is a property of the machine, not of any
        // group: is this machine's OWN mRestrictToNodeType a class confined to the fracking hierarchy?
        // The vacuous (null-node) evaluation shape is used deliberately -- we read RestrictClass /
        // RestrictClassPath from it and NEVER call AcceptsNatively() on it, which is exactly the
        // descriptive use FNodeShuffleExtractorAcceptance documents as legitimate.
        const FNodeShuffleExtractorAcceptance FrackSelf = bFrackingDerived
            ? FNodeShuffleModule::EvaluateExtractorAcceptance(Ext, nullptr, -1, nullptr)
            : FNodeShuffleExtractorAcceptance();
        const bool bRestrictionConfinedToFracking = RequiredFrackingNodeBase && FrackSelf.RestrictClass
            && FrackSelf.RestrictClass->IsChildOf(RequiredFrackingNodeBase);

        // Pairing telemetry, accumulated across the group loop below and printed on ONE FRACKPAIR line per
        // fracking-derived extractor. Per-group lines were rejected: with ~6 fracking classes x N managed
        // groups, and the pass re-running on every re-roll, that is a log flood that buries its own answer.
        int32 FrackGroupsWithFrackingNode = 0; // groups whose node class IS the required fracking type
        int32 FrackPairingVetoes = 0;          // groups natively accepted but REFUSED by the pairing rule
        FString FirstVetoNodeClass = TEXT("<none>");
        const TCHAR* FirstVetoReason = TEXT("<none>");

        // OSCILLATION FIX (2026-07-30, MEASURED in the wild). This USED TO `continue`, which made the
        // generated pack oscillate on alternate boots: boot N generated 7 documents; KDF applied them at
        // boot N+1, so every one of them then read as already-in-PDA, ToGenerate came out EMPTY, and the
        // "0 matched is a valid, harmless state" path DELETED the whole pack -- so boot N+2 had nothing,
        // regenerated, and boot N+3 worked again. Observed exactly: a pass logging
        // "pass complete -- 0 matched, 0 document(s) WRITTEN" left a 0-document pack.yml on disk after the
        // 7-entry boot. That is the "it worked, then it stopped" failure ns-review-g predicted, reached
        // through the one door its F1 guard did not cover (F1 covers an unreliable allow-list read and an
        // empty census -- NOT an empty ToGenerate caused by our own entries having landed).
        //
        // The pack must therefore be a PURE FUNCTION of (available extractors x managed node groups),
        // independent of what is already applied -- the same determinism property the census fix gave the
        // node side. PDA membership is now recorded on the line and nothing more. Re-appending an entry
        // SF+ already has is safe: ns-review-g MEASURED from KAPI's shipped PDB that the effective consumer
        // is a TSet (UKAPIDataAssetSubsystem::mAllowedResourceExtractors), so duplicate appends collapse.
        // COST, stated rather than hidden: we now also emit documents for extractors SF+ already allows
        // (ModularMiner, BioWater, MiniEx, the vanilla pumps...) when they match one of our managed node
        // groups, so the document count rises. If SF+ ever DELIBERATELY removes one of those, we would
        // re-add it -- accepted, because the alternative is a pack that deletes itself every other boot.
        const bool bAlreadyInPdaArray = AllowList.AllowedExtractorClasses.Contains(RawCls);

        // Find a node group NodeShuffle actually MANAGES that this extractor natively accepts AND that
        // the comparison actually DISCRIMINATED on (ns-review-g G4: AcceptsNatively() alone is true for
        // an extractor that declares no restrictions at all, against ANY node -- that is not evidence,
        // it is a vacuous pass. bDiscriminated requires at least one real restriction to have been
        // satisfied). First non-vacuous match is sufficient evidence to allow-list it.
        bool bMatched = false;
        const FNodeShuffleManagedGroup* MatchedGroup = nullptr;
        for (const FNodeShuffleManagedGroup& G : NodeGroups)
        {
            const FNodeShuffleExtractorAcceptance A =
                FNodeShuffleModule::EvaluateExtractorAcceptance(Ext, G.NodeClass, G.Form, G.ResourceClass);

            // H1b: the pairing veto. Non-fracking extractors are completely unaffected (bPairingOk stays
            // true and not one IsChildOf runs for them) -- this narrows ONE class of machine and leaves
            // every other decision in this loop byte-identical to Packet G's.
            bool bPairingOk = true;
            if (bFrackingDerived)
            {
                // ns-h1b-notice: the rule itself now lives in ClassifyFrackingPairing so the notice's
                // resource-naming pass consumes the SAME predicate. The counters below stay here --
                // they are this pass's telemetry, not part of the rule.
                const ENodeShuffleFrackPair Pair =
                    FNodeShuffleModule::ClassifyFrackingPairing(RawCls, G.NodeClass, FrackSelf.RestrictClass);
                const bool bNodeIsFrackingType = RequiredFrackingNodeBase && G.NodeClass
                    && G.NodeClass->IsChildOf(RequiredFrackingNodeBase);
                if (bNodeIsFrackingType) { ++FrackGroupsWithFrackingNode; }
                bPairingOk = (Pair == ENodeShuffleFrackPair::Allowed);
                if (!bPairingOk && A.AcceptsNatively() && A.bDiscriminated)
                {
                    // The machine WOULD have been allow-listed on this group's evidence and the pairing
                    // rule refused it. This counter is the whole point of the packet being observable:
                    // a nonzero value on a NON-fracking node class is a live sighting of the crash vector
                    // axis (ii) exists to close, and the log names it rather than leaving it inferred.
                    ++FrackPairingVetoes;
                    if (FrackPairingVetoes == 1)
                    {
                        FirstVetoNodeClass = G.NodeClass ? G.NodeClass->GetPathName() : TEXT("<null-node-class>");
                        switch (Pair)
                        {
                        case ENodeShuffleFrackPair::RejectedUnclassifiableKind:
                            FirstVetoReason = TEXT("unclassifiable-fracking-kind(derives-from-both-bases)"); break;
                        case ENodeShuffleFrackPair::RejectedNodeNotFracking:
                            FirstVetoReason = TEXT("node-class-is-not-the-required-fracking-type"); break;
                        case ENodeShuffleFrackPair::RejectedRestrictionNotConfined:
                            FirstVetoReason = TEXT("extractor-restriction-not-confined-to-fracking"); break;
                        default:
                            FirstVetoReason = TEXT("<unexpected-classification>"); break;
                        }
                    }
                }
            }
            if (A.AcceptsNatively() && A.bDiscriminated && bPairingOk)
            {
                if (!bMatched)
                {
                    bMatched = true;
                    MatchedGroup = &G;
                    // sfPlusAlreadyAllows is REPORTED, never acted on (oscillation fix above): an entry SF+
                    // already has is still regenerated, so the pack stays a pure function of the rule and
                    // cannot delete itself once its own entries land. sfPlusAlreadyAllows=1 on a first-ever
                    // pass means SF+ ships it; on a later pass it usually means WE put it there last boot.
                    UE_LOG(LogNodeShuffle, Display,
                        TEXT("AUTOALLOW extractor='%s' sfPlusAlreadyAllows=%d decision=%s matchedNode(class='%s' resource='%s' form=%d(%s)) ")
                        TEXT("nodeIsA=%d formAllowed=%d resAllowed=%d"),
                        *ExtPath, bAlreadyInPdaArray ? 1 : 0,
                        bAllowListUnreliable ? TEXT("SKIP(unreliable-read)") : TEXT("ADD"),
                        *G.NodeClass->GetPathName(), G.ResourceClass ? *G.ResourceClass->GetPathName() : TEXT("<none>"),
                        G.Form, NodeShuffleFormName(G.Form), A.bNodeIsA ? 1 : 0, A.bFormAllowed ? 1 : 0, A.bResourceAllowed ? 1 : 0);
                }
                // ns-review-h1b F2: a NON-fracking extractor stops at its first match exactly as Packet G
                // always did -- one match is sufficient evidence and the rest of the census is irrelevant
                // to it. A FRACKING-derived one deliberately does NOT break: the FRACKPAIR line below
                // claims to report whether ANY managed group tempted this machine onto a non-fracking
                // node, and breaking early would make that claim untestable rather than false-in-a-visible
                // way. BuildManagedNodeGroupsFromLayout emits {Satellite, Core} per well, so a Well
                // Extractor pairing on the satellite group would otherwise leave that well's core group --
                // and every later well -- unscanned, and pairingVetoes=0 would read as "nothing tempted
                // it" when nothing was ever asked. The cost is a few extra IsChildOf per well machine per
                // pass; the decision (bMatched/MatchedGroup, both first-hit-wins above) is unchanged.
                if (!bFrackingDerived) { break; }
            }
        }

        // H1b: ONE line per fracking-derived extractor, printed whatever the outcome, carrying every input
        // the pairing rule used and the outcome it produced. Reading this single line must be enough to
        // say why a well machine is or is not on the list -- including the two facts that were previously
        // unmeasurable from the log: which node class the machine restricts to, and whether any managed
        // group tempted it onto a NON-fracking node. Because the loop above does not break for a fracking
        // class (F2), groupsConsidered/frackingNodeGroups/pairingVetoes cover the WHOLE census, not the
        // prefix that happened to precede the first match.
        if (bFrackingDerived)
        {
            // ns-review-h1b F1: the decision token is THREE-valued, not two. bMatched says the pairing
            // rule was satisfied; it does NOT say a document gets written. When the SF+ allow-list read is
            // unreliable the pass deliberately generates nothing (the ADD line prints SKIP(unreliable-read)
            // and the guard below returns without writing), so keying this token on bMatched alone would
            // report a pressurizer as ADDed on a pass that wrote nothing -- exactly the diagnosis dead end
            // this line exists to remove.
            const TCHAR* FrackDecision = !bMatched
                ? TEXT("SKIP(no valid fracking pairing)")
                : (bAllowListUnreliable ? TEXT("PAIRED(SUPPRESSED: unreliable-allow-list-read, nothing written this pass)")
                                        : TEXT("PAIRED(proceeds to ADD)"));
            UE_LOG(LogNodeShuffle, Display,
                TEXT("AUTOALLOW FRACKPAIR extractor='%s' kind=%s requiredNodeBase='%s' restrictToNodeType='%s' ")
                TEXT("restrictionConfinedToFracking=%d groupsConsidered=%d frackingNodeGroups=%d pairingVetoes=%d ")
                TEXT("firstVeto(nodeClass='%s' reason=%s) sfPlusAlreadyAllows=%d decision=%s"),
                *ExtPath,
                (bFrackActivator && bFrackExtractor) ? TEXT("BOTH-BASES(unclassifiable-fail-closed)")
                    : (bFrackActivator ? TEXT("activator(pressurizer->core)") : TEXT("extractor(well-extractor->satellite)")),
                RequiredFrackingNodeBase ? *RequiredFrackingNodeBase->GetName() : TEXT("<none-fail-closed>"),
                *FrackSelf.RestrictClassPath, bRestrictionConfinedToFracking ? 1 : 0,
                NodeGroups.Num(), FrackGroupsWithFrackingNode, FrackPairingVetoes,
                *FirstVetoNodeClass, FirstVetoReason, bAlreadyInPdaArray ? 1 : 0, FrackDecision);
        }

        if (!bMatched)
        {
            // ns-review-g G4: distinguish "the rule rejected it" from "it declares no restrictions at
            // all, so an accept would have been vacuous". Both are SKIPs; only the log tells them apart.
            const FNodeShuffleExtractorAcceptance Desc =
                FNodeShuffleModule::EvaluateExtractorAcceptance(Ext, RawCls, -1, nullptr);
            // H1b: a fracking-derived class must never report the generic reason -- "no managed node type
            // natively accepted" would be a LIE when the truth is "one was accepted and the pairing rule
            // refused it". Three distinct fracking outcomes, each actionable on sight.
            const TCHAR* SkipReason;
            if (bFrackingDerived)
            {
                SkipReason = (FrackPairingVetoes > 0)
                    ? TEXT("fracking-pairing-veto-fail-closed")
                    : ((FrackGroupsWithFrackingNode > 0)
                        ? TEXT("fracking-node-group-managed-but-not-natively-accepted")
                        : TEXT("no-managed-fracking-node-group-of-the-required-kind"));
            }
            else
            {
                SkipReason = Desc.bDiscriminated ? TEXT("no-managed-node-type-natively-accepted")
                                                 : TEXT("declares-no-restrictions-accept-would-be-vacuous");
            }
            UE_LOG(LogNodeShuffle, Display,
                TEXT("AUTOALLOW extractor='%s' sfPlusAlreadyAllows=%d decision=SKIP reason=%s (hasRestriction=%d allowedForms=[%s] onlyCertainResources=%d)"),
                *ExtPath, bAlreadyInPdaArray ? 1 : 0, SkipReason,
                Desc.bHasRestriction ? 1 : 0, *Desc.AllowedFormsCsv, Desc.bOnlyCertainResources ? 1 : 0);
            continue;
        }
        if (bAllowListUnreliable)
        {
            continue; // logged above; do not generate
        }

        FGeneratedDoc Doc;
        Doc.ExtractorPath = ExtPath;
        Doc.OwningMod = DeriveModReferenceFromPath(ExtPath);
        Doc.MatchedNodeClass = MatchedGroup->NodeClass->GetPathName();
        Doc.MatchedResourceClass = MatchedGroup->ResourceClass ? MatchedGroup->ResourceClass->GetPathName() : TEXT("<none>");
        Doc.MatchedForm = MatchedGroup->Form;
        Doc.ExtractorClass = RawCls;
        Doc.bAlreadyAllowed = bAlreadyInPdaArray; // ns-h1b-notice: the pending signal, captured in place
        ToGenerate.Add(Doc);
    }

    // Regenerate the pack directory fresh every completed pass -- fully idempotent. Clearing first means
    // an extractor that no longer qualifies (mod removed, or the layout re-rolled away from its resource)
    // never leaves a stale entry behind.
    //
    // "AN EMPTY PACK IS A HARMLESS STATE" WAS MEASURED FALSE (2026-07-30) AND IS ONLY TRUE AGAIN BECAUSE
    // OF THE OSCILLATION FIX ABOVE -- do NOT restore the old unconditional wording. An empty pack
    // un-allow-lists every extractor a good earlier pass added, and for one whole boot the user gets the
    // pre-mod behaviour back. The old code reached that state by SKIPPING every extractor whose entry our
    // OWN previous pack had already landed, so ToGenerate collapsed to empty on alternate boots and this
    // very clear deleted a correct 7-document pack. ToGenerate no longer reads the allow-list at all, so
    // it is now a pure function of (available extractors x managed node groups) -- neither of which
    // depends on the pack -- and an empty ToGenerate therefore means the RULE genuinely matched nothing,
    // which IS a legitimately empty answer. That is the ONLY reason clearing on empty is safe now. Any
    // future change that reintroduces a pack-derived input to ToGenerate reintroduces this bug.
    //
    // SAFE also because the census is deterministic (see the file header's "WHY CLEAR-AND-REBUILD IS NOW
    // SAFE"): there is no earlier, richer pass this session whose data could be lost by clearing.
    // ns-review-g G2: EVERY filesystem result is checked. This pass writes into the GAME INSTALL; a
    // failed write must never be reported as a generated document, and the pass must not latch on a
    // failure (returning false makes RefreshTick retry next tick).
    // ns-review-g2 F1: DO NOT DESTROY ON A DEGRADED PASS. Clear-and-rebuild is only safe when this
    // pass's ToGenerate is the *authoritative* answer for the current Layout. Two inputs can collapse
    // it to empty for reasons that have nothing to do with the layout: (a) an unreliable SF+ allow-list
    // read (bAllowListUnreliable short-circuits every match before it reaches ToGenerate), and (b) a
    // census that resolved NO groups at all despite the layout having active entries. In both cases the
    // previously-generated pack is the better answer than an empty one, so keep it and retry next tick
    // instead of deleting it. Returning false deliberately does NOT latch -- a transient condition
    // (content still loading) then self-heals within the same session; a permanent one keeps saying so
    // in the log every tick, which is what we want a permanent fault to do.
    if (bAllowListUnreliable)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("AUTOALLOW: allow-list read was UNRELIABLE this pass -- KEEPING any previously-generated ")
            TEXT("pack at '%s' untouched rather than replacing it with an empty one (deleting it would ")
            TEXT("un-allow-list extractors that a good earlier pass correctly added). Retrying next tick."),
            *PackDir);
        // ns-h1b-notice: SAY SO EXPLICITLY rather than relying on the reader to notice that OutPending
        // was never filled. A degraded read is NOT a pending state: nothing was written, so nothing is
        // waiting on a restart, and telling the player "restart to fix this" would be a confident
        // falsehood -- in the one packet whose entire purpose is to stop confident falsehoods. The
        // player is told nothing; the log says why. Do not "helpfully" emit a notice from here.
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("PENDINGNOTICE: SUPPRESSED(degraded-pass) -- the SF+ allow-list read was unreliable, so NO ")
            TEXT("document was written and there is nothing pending. No chat message. This is a BROKEN READ, ")
            TEXT("not a restart-required state; a restart would not change it."));
        return false;
    }
    if (NodeGroups.Num() == 0 && TotalActiveEntries > 0)
    {
        UE_LOG(LogNodeShuffle, Error,
            TEXT("AUTOALLOW: census resolved 0 managed node groups from %d ACTIVE layout entries (%d ")
            TEXT("unresolvable) -- that is a census FAILURE, not an empty world. KEEPING any previously-")
            TEXT("generated pack at '%s' untouched and retrying next tick; regenerating from an empty ")
            TEXT("census would delete correct entries."),
            TotalActiveEntries, UnresolvedActiveEntries, *PackDir);
        return false;
    }
    IFileManager& FM = IFileManager::Get();
    // ns-t55-churn (T55, 2026-08-10): READ THE PACK BEFORE WE DESTROY IT, so the log can say what this
    // pass CHANGED rather than only what it wrote. MEASUREMENT ONLY -- nothing below reads PriorDocNames
    // for a decision, and ToGenerate is computed above without ever consulting the pack (the oscillation
    // fix depends on that and is not being touched here).
    //
    // WHY THIS EXISTS. T55 was filed as "the chat notice re-announces the same extractors every load,
    // because the announced-key set is session-scoped". The logs say otherwise: on the author's machine
    // /AlkaLib/...ReactiveOreExtractorMk2+Mk3 were WRITTEN and announced on the 17.13.38 boot, read back
    // sfPlusAlreadyAllows=1 on the 17.15.16 boot (so the document DID take effect), and were pending
    // again -- sfPlusAlreadyAllows=0 -- on the next boot. The pending state is genuinely re-created, not
    // merely re-announced. This directory is per-INSTALL while ToGenerate is a function of the layout of
    // whichever SAVE is loaded, so loading save X after save Y deletes every document Y needed and X
    // does not. These counts are what makes that visible in one line instead of a cross-log diff.
    TArray<FString> PriorDocFiles;
    if (FM.DirectoryExists(*PackDir))
    {
        FM.FindFiles(PriorDocFiles, *FPaths::Combine(PackDir, TEXT("*.cdo.yml")), /*Files=*/true, /*Directories=*/false);
    }
    const TSet<FString> PriorDocNames(PriorDocFiles);
    if (FM.DirectoryExists(*PackDir) && !FM.DeleteDirectory(*PackDir, /*RequireExists=*/false, /*Tree=*/true))
    {
        UE_LOG(LogNodeShuffle, Error,
            TEXT("AUTOALLOW: FAILED to clear '%s' (permissions / read-only install / file lock / AV?). ")
            TEXT("NOTHING was generated this pass and any PREVIOUS generated pack is still on disk and ")
            TEXT("will still apply next boot. Will retry next tick."), *PackDir);
        return false;
    }
    if (!FM.MakeDirectory(*PackDir, /*Tree=*/true))
    {
        UE_LOG(LogNodeShuffle, Error,
            TEXT("AUTOALLOW: FAILED to create '%s' (permissions / read-only install?). NOTHING was ")
            TEXT("generated this pass. Will retry next tick."), *PackDir);
        return false;
    }

    const FString Timestamp = FDateTime::Now().ToString();
    const FString PackYaml = FString::Printf(
        TEXT("# MACHINE-GENERATED by NodeShuffle Packet G (NodeShuffle.AutoAllowExtractors, default ON in\n")
        TEXT("# this dev build). DO NOT HAND-EDIT -- fully overwritten every world load while the CVar is 1.\n")
        TEXT("# To regenerate now: reload a save. To roll back completely: set NodeShuffle.AutoAllowExtractors=0\n")
        TEXT("# and reload once -- the pass deletes this whole directory.\n")
        TEXT("# Generated: %s | %d document(s) this run.\n")
        TEXT("ref: NodeShuffleAutoAllow\n")
        TEXT("name: Node Shuffle Auto-Allow Extractors (generated)\n")
        TEXT("version: 1.0.0\n")
        TEXT("contributer: NodeShuffle (machine-generated, Packet G)\n")
        TEXT("enabled: true\n")
        TEXT("conditions:\n")
        TEXT("  hasMod:\n")
        TEXT("  - SatisfactoryPlus\n")
        TEXT("  - NodeShuffle\n"),
        *Timestamp, ToGenerate.Num());
    const FString PackYamlPath = FPaths::Combine(PackDir, TEXT("pack.yml"));
    if (!FFileHelper::SaveStringToFile(PackYaml, *PackYamlPath))
    {
        UE_LOG(LogNodeShuffle, Error,
            TEXT("AUTOALLOW: FAILED to write '%s'. Without pack.yml KDataForge will not read this ")
            TEXT("directory at all, so NO generated document can take effect. Will retry next tick."),
            *PackYamlPath);
        return false;
    }

    int32 WrittenCount = 0;
    int32 FailedCount = 0;
    // ns-h1b-notice: PENDING is built from documents that were actually WRITTEN, never from ToGenerate.
    // A document that failed to write is not "pending on a restart" -- it is broken, and gets its own,
    // louder message. Keeping both in one array with a flag (rather than two arrays) means the notice
    // builder sees them in one pass and cannot accidentally report a failure as a restart.
    TArray<FNodeShufflePendingRaw> NoticeRaw;
    int32 AlreadyAllowedCount = 0;
    TSet<FString> WrittenDocNames; // ns-t55-churn: measurement only -- compared against PriorDocNames below
    for (const FGeneratedDoc& Doc : ToGenerate)
    {
        FString HasModLines = TEXT("    - SatisfactoryPlus\n    - NodeShuffle\n");
        if (!Doc.OwningMod.IsEmpty())
        {
            HasModLines += FString::Printf(TEXT("    - %s\n"), *Doc.OwningMod);
        }
        const FString DocYaml = FString::Printf(
            TEXT("# MACHINE-GENERATED by NodeShuffle Packet G -- DO NOT HAND-EDIT, overwritten every regeneration.\n")
            TEXT("# Extractor: %s\n")
            TEXT("# Matched node group NodeShuffle manages: class='%s' resource='%s' form=%d(%s)\n")
            TEXT("# Native acceptance measured via FNodeShuffleModule::EvaluateExtractorAcceptance (the SAME\n")
            TEXT("# predicate the in-game hologram hooks use) -- nodeIsA/formAllowed/resAllowed all true.\n")
            TEXT("# NOTE: the matched group above is the FIRST match found, not the only one.\n")
            TEXT("# Generated: %s\n")
            TEXT("type: cdo\n")
            TEXT("conditions:\n")
            TEXT("  hasMod:\n")
            TEXT("%s")
            TEXT("patches:\n")
            TEXT("  - target: \"/SatisfactoryPlus/AssetDatas/Modules/PDA_SFP_ExtractorList.PDA_SFP_ExtractorList\"\n")
            TEXT("    properties:\n")
            TEXT("      - path: mAllowedExtractors\n")
            TEXT("        op: append\n")
            TEXT("        value:\n")
            TEXT("          - \"%s\"\n"),
            *Doc.ExtractorPath, *Doc.MatchedNodeClass, *Doc.MatchedResourceClass, Doc.MatchedForm,
            NodeShuffleFormName(Doc.MatchedForm), *Timestamp, *HasModLines, *Doc.ExtractorPath);

        // ns-review-g G9: the class LEAF alone is not unique across mounts -- two mods can ship the same
        // leaf name and silently overwrite each other's document. Prefix with the owning mod reference
        // (empty for vanilla) so the filename is unique per (mod, class).
        const FString FileName = Doc.OwningMod.IsEmpty()
            ? FString::Printf(TEXT("auto-allow-%s.cdo.yml"), *ExtractorClassStem(Doc.ExtractorPath))
            : FString::Printf(TEXT("auto-allow-%s-%s.cdo.yml"), *Doc.OwningMod, *ExtractorClassStem(Doc.ExtractorPath));
        const FString DocPath = FPaths::Combine(PackDir, FileName);
        if (FFileHelper::SaveStringToFile(DocYaml, *DocPath))
        {
            ++WrittenCount;
            WrittenDocNames.Add(FileName); // ns-t55-churn
            UE_LOG(LogNodeShuffle, Display,
                TEXT("AUTOALLOW extractor='%s' decision=ADD -- WROTE '%s' (verified), takes effect NEXT boot"),
                *Doc.ExtractorPath, *FileName);
            // The pending set: WRITTEN, and SF+ does not permit it yet. A class SF+ already permits was
            // still (deliberately) regenerated by the oscillation fix, but it is live right now -- the
            // player needs no restart for it and must not be told otherwise.
            if (Doc.bAlreadyAllowed) { ++AlreadyAllowedCount; }
            else
            {
                FNodeShufflePendingRaw& R = NoticeRaw.AddDefaulted_GetRef();
                R.ExtractorClass = Doc.ExtractorClass;
                R.ExtractorPath = Doc.ExtractorPath;
                R.bWriteFailed = false;
                R.bAlreadyAllowed = false; // by construction: this branch is the !bAlreadyAllowed case
            }
        }
        else
        {
            ++FailedCount;
            UE_LOG(LogNodeShuffle, Error,
                TEXT("AUTOALLOW extractor='%s' decision=ADD-FAILED -- could NOT write '%s'. This extractor ")
                TEXT("will NOT be allow-listed next boot."), *Doc.ExtractorPath, *DocPath);
            // ns-review-notice F1: the failure branch MUST carry bAlreadyAllowed too. Unlike the success
            // branch above, a failed write is reported for EVERY class -- including ones SF+ ships
            // natively (ModularMiner, BioWater, MiniEx, the vanilla pumps), which work today and will
            // keep working. Telling that player their buildings are permanently broken would be a
            // confident falsehood in the packet built to stop them. See EmitPendingNotice for the two
            // sentences this selects between.
            FNodeShufflePendingRaw& R = NoticeRaw.AddDefaulted_GetRef();
            R.ExtractorClass = Doc.ExtractorClass;
            R.ExtractorPath = Doc.ExtractorPath;
            R.bWriteFailed = true;
            R.bAlreadyAllowed = Doc.bAlreadyAllowed;
        }
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("AUTOALLOW: pass complete -- %d matched, %d document(s) WRITTEN, %d FAILED, at '%s' ")
        TEXT("(0 matched is a valid, harmless state, not an error; a nonzero FAILED count is an error)"),
        ToGenerate.Num(), WrittenCount, FailedCount, *PackDir);

    // ns-t55-churn (T55): the one line that explains a repeat "restart required" notice without a
    // cross-log diff. Counts only -- it reports what the two directory listings were, never why the
    // sets differ; the per-extractor SKIP lines above carry each reason in their own words.
    {
        int32 Unchanged = 0;
        for (const FString& N : WrittenDocNames) { if (PriorDocNames.Contains(N)) { ++Unchanged; } }
        TArray<FString> RemovedNames;
        for (const FString& N : PriorDocNames) { if (!WrittenDocNames.Contains(N)) { RemovedNames.Add(N); } }
        RemovedNames.Sort();
        const int32 MaxNamed = 6;
        FString RemovedCsv = FString::Join(
            TArray<FString>(RemovedNames.GetData(), FMath::Min(RemovedNames.Num(), MaxNamed)), TEXT(", "));
        if (RemovedNames.Num() > MaxNamed)
        {
            RemovedCsv += FString::Printf(TEXT(", +%d more"), RemovedNames.Num() - MaxNamed);
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("AUTOALLOW PACKCHURN: docsBefore %d, docsNow %d -- unchanged %d, added %d, removed %d. ")
            TEXT("Removed: [%s]. This directory is cleared and rebuilt every completed pass from THIS ")
            TEXT("world's managed node groups, so a removed document stops applying at the next boot and ")
            TEXT("its extractor can become pending again. Counts, not a diagnosis."),
            PriorDocNames.Num(), WrittenDocNames.Num(), Unchanged,
            WrittenDocNames.Num() - Unchanged, RemovedNames.Num(), *RemovedCsv);
    }

    // ns-h1b-notice: the pass's own one-line answer to "what was the player told, and why".
    UE_LOG(LogNodeShuffle, Display,
        TEXT("PENDINGNOTICE: pass summary -- written=%d alreadyAllowed=%d PENDING=%d writeFailed=%d ")
        TEXT("| sfPlusArrayEntries=%d resolved=%d | netMode=%d"),
        WrittenCount, AlreadyAllowedCount, NoticeRaw.Num() - FailedCount, FailedCount,
        AllowList.ArrayEntryCount, AllowList.AllowedExtractorClasses.Num(),
        World ? (int32)World->GetNetMode() : -1);

    if (NoticeRaw.Num() == 0)
    {
        // Distinguishes "the notice never fired" from "the code never ran" -- and states that silence is
        // the NORMAL steady state, so a reader does not go looking for a broken notice.
        // ns-review-notice F5: the two silences are NOT the same fact and must not share a sentence.
        // "all 0 written document(s) are already on SF+'s allow-list" is nonsense, and it appeared in the
        // exact block a reader consults to tell a working-but-quiet notice from a dead one.
        if (WrittenCount > 0)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("PENDINGNOTICE: nothing pending this pass -- all %d written document(s) are already on ")
                TEXT("SF+'s allow-list. No message shown (this is the normal steady state)."), WrittenCount);
        }
        else
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("PENDINGNOTICE: nothing pending this pass -- the rule matched NO extractor at all, so no ")
                TEXT("document was written (matched=%d). Nothing to announce; this is a legitimately empty ")
                TEXT("answer, not a failure."), ToGenerate.Num());
        }
    }
    else if (OutPending)
    {
        FNodeShuffleModule::BuildPendingNotice(NoticeRaw, NodeGroups, *OutPending);
    }
    else
    {
        // Only reachable if a future caller drops the out-param. Say so rather than going quiet.
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("PENDINGNOTICE: %d entr(ies) are pending but this caller passed no OutPending array -- ")
            TEXT("the player will NOT be told. (Log-only fallback; see RunAutoAllowExtractorsIfEnabled.)"),
            NoticeRaw.Num());
    }

    return FailedCount == 0; // a partial write must not latch -- retry next tick
}
