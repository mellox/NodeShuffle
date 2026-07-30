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
    // DataForge/NodeShuffleAutoAllow/ -- a pack directory EXCLUSIVELY owned by this generator (never
    // the hand-written NodeShuffleSFPlus/ pack, never touched by anything else). Computed from
    // FPaths::ProjectDir() (already-linked symbol, confirmed by Packet F's own import list) the SAME way
    // KDataForge itself finds every mod's DataForge root (measured: FactoryGame.log's own
    // "Found 7 DataForge root(s) under <ProjectDir>/" line enumerates exactly this pattern).
    FString GetAutoAllowPackDir()
    {
        return FPaths::Combine(FPaths::ProjectDir(), TEXT("Mods/NodeShuffle/DataForge/NodeShuffleAutoAllow"));
    }

    // Derives the SML "ModReference" (a generated document's extra hasMod entry) from an object path
    // like "/AlkaLib/Buildables/.../Build_X_C" -- the first path segment, the SAME convention the
    // hand-written pack documents ("SML's ModReference is the folder/.uplugin stem"). Empty for vanilla
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

bool FNodeShuffleModule::RunAutoAllowExtractorsIfEnabled(UWorld* World)
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
    UE_LOG(LogNodeShuffle, Display,
        TEXT("AUTOALLOW: %d managed node group(s) derived from the ROLLED LAYOUT (streaming-independent -- ")
        TEXT("does not depend on where the player loaded or what has streamed in). %d active layout ")
        TEXT("entries considered, %d could not be resolved to a resource/node class and were skipped."),
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

        // FAIL-CLOSED INVARIANT for the GENERATED pack, mirroring NodeShuffle.cpp's
        // NodeShuffleIsFrackingExtractor (fracking-crash-fix). A FRACKING extractor (Resource Well
        // Pressurizer = AFGBuildableFrackingActivator, or the satellite Resource Well Extractor =
        // AFGBuildableFrackingExtractor; both are UCLASS(Abstract), so every concrete well -- vanilla or
        // MODDED -- is necessarily a subclass) does a CHECKED cast of the node to a fracking node type in
        // OnExtractableResourceSet. Placing one on one of our regular Node-type nodes is a FATAL,
        // user-reported crash. This pass therefore never appends one to SF+'s allow-list.
        //
        // WHAT THIS IS AND IS NOT (ns-review-frack, 2026-07-30, boot-2 MEASURED) -- do not let a later
        // reader mistake this for a crash fix:
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
        // Value retained despite all of the above: the pack below is MACHINE-GENERATED and fully rewritten
        // every world load against a mod set that changes without notice, so "the generated pack never
        // contains a fracking-derived class" is worth holding as a structural invariant rather than as an
        // accident of how today's skips happen to land. Detected on the buildable CLASS -- the same axis,
        // and the same predicate, the hologram-side guard applies to GetBuildClass().
        if (RawCls->IsChildOf(AFGBuildableFrackingActivator::StaticClass())
            || RawCls->IsChildOf(AFGBuildableFrackingExtractor::StaticClass()))
        {
            // ns-review-frack F4: report SF+ allow-list membership on THIS line rather than reordering the
            // guard behind the already-in-PDA-array check. Both branches merely `continue`, so the order is
            // behaviourally a no-op and the only question is what the log preserves -- and for a fracking
            // class, membership is exactly the fact that proves this skip protected nothing (see above).
            // Keeping the guard first also keeps it observable: behind the PDA check, a silently broken
            // guard would be indistinguishable from a working one for every SF+-approved class.
            UE_LOG(LogNodeShuffle, Display,
                TEXT("AUTOALLOW extractor='%s' decision=SKIP reason=fracking-extractor-crash-guard ")
                TEXT("sfPlusAlreadyAllows=%d (this class derives from AFGBuildableFrackingActivator/")
                TEXT("AFGBuildableFrackingExtractor, whose OnExtractableResourceSet does a CHECKED cast to a ")
                TEXT("fracking node type -- fatal on one of our Node-type nodes. Fail-closed: we decline to ")
                TEXT("APPEND it. This removes NOTHING -- if sfPlusAlreadyAllows=1, SF+ permits it with or ")
                TEXT("without us and this skip protects nothing; what normally keeps such a machine off our ")
                TEXT("nodes is its own native node-type rule, not this list.)"),
                *ExtPath, AllowList.AllowedExtractorClasses.Contains(RawCls) ? 1 : 0);
            continue;
        }

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
            if (A.AcceptsNatively() && A.bDiscriminated)
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
                break;
            }
        }

        if (!bMatched)
        {
            // ns-review-g G4: distinguish "the rule rejected it" from "it declares no restrictions at
            // all, so an accept would have been vacuous". Both are SKIPs; only the log tells them apart.
            const FNodeShuffleExtractorAcceptance Desc =
                FNodeShuffleModule::EvaluateExtractorAcceptance(Ext, RawCls, -1, nullptr);
            UE_LOG(LogNodeShuffle, Display,
                TEXT("AUTOALLOW extractor='%s' sfPlusAlreadyAllows=%d decision=SKIP reason=%s (hasRestriction=%d allowedForms=[%s] onlyCertainResources=%d)"),
                *ExtPath, bAlreadyInPdaArray ? 1 : 0,
                Desc.bDiscriminated ? TEXT("no-managed-node-type-natively-accepted")
                                    : TEXT("declares-no-restrictions-accept-would-be-vacuous"),
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
            UE_LOG(LogNodeShuffle, Display,
                TEXT("AUTOALLOW extractor='%s' decision=ADD -- WROTE '%s' (verified), takes effect NEXT boot"),
                *Doc.ExtractorPath, *FileName);
        }
        else
        {
            ++FailedCount;
            UE_LOG(LogNodeShuffle, Error,
                TEXT("AUTOALLOW extractor='%s' decision=ADD-FAILED -- could NOT write '%s'. This extractor ")
                TEXT("will NOT be allow-listed next boot."), *Doc.ExtractorPath, *DocPath);
        }
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("AUTOALLOW: pass complete -- %d matched, %d document(s) WRITTEN, %d FAILED, at '%s' ")
        TEXT("(0 matched is a valid, harmless state, not an error; a nonzero FAILED count is an error)"),
        ToGenerate.Num(), WrittenCount, FailedCount, *PackDir);
    return FailedCount == 0; // a partial write must not latch -- retry next tick
}
