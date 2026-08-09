// Packet H1 (ns-wells-h1, branch feature/extractor-automatch): the DEAL half of the in-place
// resource-well retype -- discovery, the live-over-saved merge, and the resource permutation.
// NodeShuffleWellRetype.cpp owns the WRITE half; NodeShuffleWellRetype.h says what lives where and why
// both are ANodeShuffleSubsystem members defined outside the subsystem's own .cpp.
//
// WHAT THIS PACKET IS, AND WHAT IT DELIBERATELY IS NOT. A resource well becomes NodeShuffle-MANAGED but
// is NOT MOVED. The core and every satellite stay exactly where the level author put them; only the
// RESOURCE they carry is re-rolled. There is no spawning, no relocation and no mCore lifecycle work
// anywhere in this packet -- all of that is H2 (design §6). Kept small on purpose: if a well misbehaves
// in-game after this build, the cause is unambiguous.
//
// GROUPING IS NOT RE-IMPLEMENTED HERE. Discovery calls H0's CollectWellCensus (NodeShuffleWellCensus.h)
// -- the two-view core<->satellite reconciliation, the adopted-core path, the stale-weak-pointer
// handling and the non-finite-location validation are all already proven there and are NOT duplicated.
// H0 built that as a DUMP; this is its first use as a DECISION input, which is exactly why it was
// factored out rather than left inside the console command.
//
// WHAT H0 MEASURED, AND WHAT THIS FILE IS THEREFORE ALLOWED TO ASSUME (design §4b, 20 wells /
// 135 satellites, two identical runs):
//   * resourceMismatchWells = 0 across all 20 wells -- the core and its satellites really do share one
//     resource. The invariant is REAL and H1 may rely on it; the apply half still ASSERTS it after every
//     write, because relying on a measurement is not the same as trusting it forever.
//   * Satellites per well: min 4, mean 6.75, max 10. Nothing here is written for "about 7".
//   * PURITY IS NOT SHARED and is not ours. Vanilla wells MIX purities across their satellites (design
//     §Q2), so there is no group purity to preserve and normalising one would be a silent balance
//     change. This packet NEVER writes mPurity or mPurityOverride; the purity read below is recorded
//     into the layout for the log's benefit and nothing else.
//
// PINNED WELLS. A well with a Resource Well Pressurizer on its core or a Resource Well Extractor on ANY
// satellite is never retyped -- the same rule, for the same reason, as FNodeShuffleEntry::bPinned on an
// occupied original: a node someone has already built on is not ours to change. Decided here from live
// state, re-checked at apply time, and every skip logged with its reason.
//
// T16 (2026-08-08): WHICH ACTORS THAT QUESTION IS PUT TO now lives in ONE place --
// EvaluateWellPin in NodeShuffleWellRetype.h, called from here and from ApplyWellRetype. This file used
// to ask the vanilla original directly, which is unanswerable for a well H2 has RELOCATED (the original
// is hidden, de-collided and deregistered, so nobody can build on it and it can never report a pin)
// while the actors a player CAN build on are our spawned ones. See that header for the full account.
//
// IMPORT DISCIPLINE (memory: sf-shipping-export-trap; H0 baseline 744 / 0 missing). This packet reaches
// for NO new engine entry point: discovery, IsOccupied(), GetActivator(), GetExtractor(),
// GetResourceClassOriginal() and GetResourceClass() are all already reached by H0's census/dump. That is
// the PREDICTION, not the claim -- the import table is MEASURED after the build, never predicted (H0
// falsified "inline in the header => no import" with GetCore).

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleConfig.h"
#include "NodeShuffleWellCensus.h" // H0's grouping -- CollectWellCensus + the well record/member types
#include "NodeShuffleWellRetype.h" // the shared pure helpers -- incl. T16's EvaluateWellPin
// TWeakObjectPtr<T>::IsValid() does not need T complete, but GetActivator()/GetExtractor() return the
// weak pointer BY VALUE, so the type has to be complete to copy one. Includes only -- no method of
// either buildable is called anywhere in this file. Since T16 those accessors are reached through
// EvaluateWellPin/IsWellMemberInUse rather than from this file directly; the includes stay because the
// completeness requirement is the same wherever the inline body is instantiated, and stating it here
// keeps the dependency visible rather than implicit in another header's include list.
#include "Buildables/FGBuildableFrackingActivator.h"
#include "Buildables/FGBuildableFrackingExtractor.h"

void ANodeShuffleSubsystem::RollWellLayout(int32 Seed, bool bIsReroll)
{
    const FNodeShuffleConfigStruct Config = FNodeShuffleConfigStruct::GetActiveConfig(this);
    if (!Config.ShuffleResourceWells)
    {
        // Leave any EXISTING assignment in the save exactly as it is rather than clearing it. This is
        // the master switch's own documented semantics ("Resource changes already stored in the save
        // persist"): turning the toggle off stops us acting, it does not un-retype a world the player
        // has already been playing. ApplyWellRetype also returns immediately while it is off, so
        // nothing further touches a well this session.
        //
        // RT-6 (ns-review-h1b, DECIDED AND INTENDED -- not an oversight, do not "fix" it). Because
        // WellLayout is kept rather than cleared, BuildManagedNodeGroupsFromLayout keeps emitting this
        // save's well groups, and since H1b those groups are LIVE allow-list evidence: fracking machines
        // stay allow-listed even with this toggle nominally off. Under H1 that was inert; it is not any
        // more, so it is written down here. It is the correct behaviour and follows directly from the
        // sentence above: turning the toggle off does NOT un-retype the wells -- they are still retyped in
        // the save. Withdrawing the allow-list would leave the player holding retyped wells they can no
        // longer build on, which is strictly worse than either consistent state. The toggle stops us
        // CHANGING things; it was never a promise to undo what is already written.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH1-ROLL: SKIPPED -- 'Shuffle Resource Wells' is OFF (existing well data in this save: %d wells, kept untouched). ")
            TEXT("NOTE (RT-6, intended): those %d wells STAY retyped and keep contributing managed groups, so any ")
            TEXT("fracking machine H1b's pairing rule allow-listed REMAINS allow-listed -- withdrawing it would leave ")
            TEXT("retyped wells nothing can be built on."),
            WellLayout.Num(), WellLayout.Num());
        return;
    }

    // Session log throttles are per-ROLL as well as per-session: a re-roll deals new resources, so
    // every well's outcome deserves to be announced again.
    WellAppliedLogged.Empty();
    WellSkipLogged.Empty();
    // ns-review-h1 I3: the apply-pass counter is part of the same per-roll log state and was being left
    // alone, so the once-per-session streaming census (which fires on a FIXED pass number) could never
    // re-fire after a MID-SESSION re-roll -- and a mid-session re-roll is exactly how a user adopts this
    // feature, i.e. the census was guaranteed to be missing for the very roll it was written to measure.
    WellApplyPasses = 0;

    // 1. DISCOVER. H0's census, unmodified -- both views of every core<->satellite link, adopted cores
    //    included. This file does not walk wells itself.
    FNodeShuffleWellCensus Census;
    CollectWellCensus(GetWorld(), Census);
    int32 CensusSatellites = 0;
    for (const FNodeShuffleWellRecord& W : Census.Wells) { CensusSatellites += W.Members.Num(); }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH1-ROLL: census saw %d wells / %d satellites (iteratorCores=%d adoptedCores=%d invalidCores=%d "
             "orphanSats=%d) -- compare with NodeShuffle.DumpWells. Wells NOT seen here keep their previous "
             "assignment (or stay vanilla) -- H1 never fails to a broken well, only to an untouched one."),
        Census.Wells.Num(), CensusSatellites, Census.IteratorCores, Census.AdoptedCores,
        Census.InvalidCoresSkipped, Census.OrphanSatellites.Num());
    // ns-review-h2 F1: the exclusion has to be VISIBLE, not merely correct -- a silent filter is
    // indistinguishable from a census that never saw the wells at all. Logged after the merge below.

    // 2. MERGE live census over whatever the save already holds. Starting from the saved array rather
    //    than rebuilding from scratch is the same streaming-determinism rule RollLayout applies to
    //    regular nodes: a well that streamed in during SOME earlier session stays in the pool even if
    //    it is not loaded during THIS re-roll, so the deal does not silently shrink with distance.
    TArray<FNodeShuffleWellEntry> Merged = WellLayout;
    TMap<FString, int32> IndexByCore;
    for (int32 i = 0; i < Merged.Num(); ++i) { IndexByCore.Add(Merged[i].CorePath, i); }

    int32 NewlyDiscovered = 0, SkippedOurSpawned = 0;
    // T16: the pin verdict for every well this roll actually evaluated, keyed by CorePath, so the
    // per-well log line below can state WHICH ACTOR the pin was resolved against instead of only its
    // outcome. An entry absent from this map was never evaluated this roll (its core did not stream in,
    // or it is one of our own spawned cores the F1 filter excluded) -- and the line says exactly that
    // rather than implying a measurement that never happened.
    TMap<FString, FNodeShuffleWellPinCheck> PinByCore;
    for (const FNodeShuffleWellRecord& W : Census.Wells)
    {
        AFGResourceNodeFrackingCore* Core = W.Core;
        if (!IsValid(Core)) { continue; } // census only stores valid cores; kept so that stays true by construction

        // ns-review-h2 F1 (CRITICAL). CollectWellCensus iterates EVERY live fracking core with no
        // filter -- correctly, because NodeShuffle.DumpWells wants an unfiltered view of the world.
        // But once H2 SPAWNS wells, that view includes OUR OWN relocated cores, and this merge loop
        // would enrol them as if they were level wells: a spawned core with a non-empty
        // GetResourceClassOriginal() would contribute a card to the deck AND become a Recipient, so
        // the nitrogen conservation this deck exists to protect would be contaminated, and H2 would
        // relocate its own relocated well -- doubling the actor count on every re-roll. H1 was safe
        // only because H1 spawns nothing; that is no longer true.
        //
        // The filter belongs HERE and at RollWellRelocation's equivalent loop, NOT inside
        // CollectWellCensus: the census is a shared, deliberately unfiltered primitive and the dump
        // depends on seeing everything. Reachable by the exact sequence this feature's own config
        // tooltip instructs: enable both toggles, roll, let a well relocate, then Re-roll.
        if (FNodeShuffleModule::IsManagedSpawnedNode(Core)) { ++SkippedOurSpawned; continue; }

        const FString CorePath = WellPathOf(Core);
        if (CorePath.IsEmpty()) { continue; }

        int32 Idx;
        if (const int32* Found = IndexByCore.Find(CorePath)) { Idx = *Found; }
        else { Idx = Merged.AddDefaulted(); Merged[Idx].CorePath = CorePath; IndexByCore.Add(CorePath, Idx); ++NewlyDiscovered; }
        FNodeShuffleWellEntry& E = Merged[Idx];

        E.CoreNodeClassPath = WellClassPathOf(Core);
        const FString Authored = WellAuthoredResourcePath(Core);
        if (!Authored.IsEmpty()) { E.OriginalResourceClassPath = Authored; }

        // PIN. Live state decides whenever the well's actors are loaded -- the same rule RollLayout uses
        // for a carried spawned node ("with a live actor, LIVE occupancy decides"), so a well whose
        // pressurizer was dismantled correctly frees back into the pool.
        //
        // T16: WHICH ACTOR IS ASKED IS NOT OBVIOUS AND WAS WRONG. The question is decided by
        // EvaluateWellPin (NodeShuffleWellRetype.h) -- the ONE implementation the apply-time re-check
        // also calls -- because for a RELOCATED well the vanilla core this loop is standing on is
        // hidden, de-collided and deregistered: a player cannot build on it, so it could never report a
        // pin, and the well someone HAS built on reported managed=1 pinned=0 for 17 of 20 wells in the
        // author's own log. The candidate satellites are collected below and the verdict is taken AFTER
        // the merge loop, because the spawned satellites are looked up by the SatellitePath keys that
        // loop maintains.
        TArray<AFGResourceNodeFrackingSatellite*> OriginalSats;

        // MERGE, NEVER REPLACE. The obvious version of this loop -- Satellites.Reset() then rebuild from
        // the census -- is destructive: a core can be loaded while some of its satellites are not (H0
        // measured emptyCores=0, but that is ONE measurement and this operation would be irreversible),
        // and rebuilding from a partial view would DELETE the missing satellites' records. They would
        // then never be retyped, so the core and half the well would carry one resource and the rest
        // another -- the exact shared-resource invariant this packet exists to hold. Level satellites do
        // not disappear, so "add and refresh, never remove" is both safe and complete.
        TMap<FString, int32> SatIndexByPath;
        for (int32 s = 0; s < E.Satellites.Num(); ++s) { SatIndexByPath.Add(E.Satellites[s].SatellitePath, s); }
        for (const FNodeShuffleWellMember& M : W.Members)
        {
            AFGResourceNodeFrackingSatellite* Sat = M.Actor;
            if (!IsValid(Sat)) { continue; }
            // FIRST SATELLITE SEEN WINS the class, and that is a real (accepted) limitation, not an
            // oversight: a modded well whose satellites are NOT all one class emits an auto-allow group
            // for only ONE of them, so an extractor restricted to the other class would not be
            // allow-listed by us. No vanilla well mixes classes; if a mod ever does, this is the line
            // that needs to become a set.
            if (E.SatelliteNodeClassPath.IsEmpty()) { E.SatelliteNodeClassPath = WellClassPathOf(Sat); }
            const FString SatPath = WellPathOf(Sat);
            int32 SatIdx;
            if (const int32* FoundSat = SatIndexByPath.Find(SatPath)) { SatIdx = *FoundSat; }
            else { SatIdx = E.Satellites.AddDefaulted(); E.Satellites[SatIdx].SatellitePath = SatPath; SatIndexByPath.Add(SatPath, SatIdx); }
            // *** H2, READ THIS BEFORE USING E.Satellites AS A VECTOR ***
            // The ORDER of E.Satellites is census/merge order, which is ACTOR-ITERATION order for newly
            // seen satellites -- i.e. STREAMING-DEPENDENT and NOT stable across sessions or vantage
            // points. H1 is order-immune because every consumer looks satellites up BY PATH and writes
            // the SAME resource to all of them. Design §Q2 calls this "the ordered purity vector" and
            // proposes writing purities back "in stored order": DOING THAT AGAINST THIS ARRAY WOULD
            // SILENTLY PERMUTE A WELL'S PURITIES depending on where the player was standing when the
            // roll ran. If H2 needs an ordered vector, sort by SatellitePath first (as the deal already
            // sorts wells by CorePath) or key it by path -- do not consume this by index.
            E.Satellites[SatIdx].OriginalPurity = Sat->GetResourcePurity(); // RECORDED ONLY -- never written back
            // T16: candidate only. Whether this satellite is the one the pin question is put to depends
            // on whether this well has been relocated -- EvaluateWellPin decides, once, for the group.
            OriginalSats.Add(Sat);
        }

        // T16, THE VERDICT. Note what is NOT here any more: an unconditional `E.bPinned = <this roll's
        // answer>`. A NON-DECISIVE answer -- a relocated well whose spawned core handle has not been
        // rebuilt this session -- tested a hidden actor that cannot be built on, so it may not CLEAR a
        // pin a decisive earlier evaluation set. It may still SET one: an in-use signal is a positive
        // fact wherever it is found. The direction of the asymmetry is the protective one, and it is
        // the other half of the defect (the old `E.bPinned = bPinned;` reset a true pin to false).
        const FNodeShuffleWellPinCheck Pin =
            EvaluateWellPin(E, SpawnedWellCores, SpawnedWellSatellites, Core, OriginalSats);
        PinByCore.Add(CorePath, Pin);
        if (Pin.IsPinned())        { E.bPinned = true; }
        else if (Pin.IsDecisive()) { E.bPinned = false; }
    }

    if (SkippedOurSpawned > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH1-ROLL: skippedOurSpawned=%d -- %d live fracking core(s) in the census are wells H2 ")
            TEXT("RELOCATED (NodeShuffle-managed spawns), excluded from the deck and from re-enrolment. ")
            TEXT("Without this they would contribute a card AND receive one, contaminating the well-only ")
            TEXT("resource conservation and re-relocating our own wells once per re-roll (ns-review-h2 F1)."),
            SkippedOurSpawned, SkippedOurSpawned);
    }

    // 3. DEAL. A DECK PERMUTATION of the wells' own resources, not an independent per-well draw. This
    //    is not a stylistic preference: NITROGEN GAS IS OBTAINABLE ONLY FROM RESOURCE WELLS, so an
    //    independent uniform draw over 20 wells can legitimately roll ZERO nitrogen wells and make the
    //    save uncompletable. A permutation preserves the per-resource well COUNT exactly, which is the
    //    same balance-preserving philosophy as the mod's existing purity deck. It also satisfies design
    //    §2.3 (a well's resource is drawn only from resources observed ON WELLS) by construction -- a
    //    well can never be dealt a solid, because no solid is ever in the deck.
    //
    //    The stream is DERIVED FROM the layout seed but SEPARATE from it. Consuming draws from
    //    RollLayout's own Rng would shift every subsequent draw and change the NODE layout for a seed
    //    that previously produced a different world -- i.e. turning this feature on would silently
    //    re-shuffle the player's ordinary nodes too. A salted sibling stream keeps the node layout
    //    byte-identical whether wells are on or off, while staying fully deterministic and re-roll
    //    reproducible (design §Q3a's determinism rule).
    FRandomStream WellRng(Seed ^ 0x57454C4C /* 'WELL' */);

    // Deterministic order. Actor-iteration order depends on what has streamed in, so dealing in it
    // would make the same seed produce different worlds from different vantage points.
    Merged.Sort([](const FNodeShuffleWellEntry& A, const FNodeShuffleWellEntry& B)
    {
        return A.CorePath < B.CorePath;
    });

    // THE DECK IS THE WHOLE WORLD, MINUS WHAT THE PINNED WELLS ARE ACTUALLY HOLDING (ns-review-h1 F2).
    //
    // The first version of this built the deck from the ORIGINALS OF THE UNPINNED WELLS ONLY. That
    // conserves on roll 1 and DECAYS afterwards: a well we retyped in an earlier roll and which the
    // player has since pressurized becomes pinned, so it keeps its PREVIOUS ASSIGNMENT while its own
    // ORIGINAL resource is still sitting in the deck to be dealt to somebody else -- the world gains a
    // copy of the assignment and loses a copy of the original, once per re-roll-after-a-build. Repeat
    // that and NITROGEN GAS -- the well-only resource this permutation exists to protect -- trends to
    // zero and the save quietly becomes uncompletable. Exactly the failure the deck was chosen to
    // prevent, arriving by a slower road.
    //
    // The fix is a MEMBERSHIP FILTER, deliberately nothing more (construction and dealing are
    // untouched): every well with an authored resource contributes its card, and every PINNED well then
    // WITHDRAWS one card equal to what it EFFECTIVELY holds today. What remains is dealt to the unpinned
    // wells. Adds one and removes one per pinned well, so Deck.Num() == Recipients.Num() by
    // construction, and the world total is conserved across any number of re-roll/pin cycles.
    TArray<FString> Deck;
    TArray<int32> Recipients;
    TArray<TPair<FString, FString>> PinnedTakes; // {effective resource held, this well's own original}
    // T16: counted separately from the pinned wells. The summary line used to print ONE number called
    // "pinned/unresolved", which is why `0 pinned` read as a fact about the world instead of as the
    // impossible value it was -- a reader had no way to tell a pinned well from a well with no authored
    // resource from an undealt tail entry. Three states, three counters, all printed.
    int32 NoAuthoredResource = 0;
    for (int32 i = 0; i < Merged.Num(); ++i)
    {
        FNodeShuffleWellEntry& E = Merged[i];
        if (E.OriginalResourceClassPath.IsEmpty())
        {
            ++NoAuthoredResource;
            // No authored resource to reason about: contributes no card and receives none. Fail SAFE to
            // "left exactly as it stands" (design §Q3 point 4).
            E.bManaged = false;
            continue;
        }
        Deck.Add(E.OriginalResourceClassPath); // EVERY well contributes, pinned or not
        if (E.bPinned)
        {
            // ONLY-IF-EMPTY, deliberately: a well we retyped in an EARLIER roll and which the player has
            // since built a pressurizer on still physically carries that earlier assignment
            // (mResourceClassOverride is SaveGame and we never revert it under a working pressurizer).
            // Overwriting the record with the ORIGINAL would make the layout lie about the world -- the
            // log would read "nitrogen -> nitrogen" for a well that is visibly producing water -- AND it
            // would make this withdrawal take the wrong card, re-opening F2. Only a never-dealt well
            // defaults to its original.
            if (E.AssignedResourceClassPath.IsEmpty()) { E.AssignedResourceClassPath = E.OriginalResourceClassPath; }
            E.bManaged = false;
            PinnedTakes.Emplace(E.AssignedResourceClassPath, E.OriginalResourceClassPath);
            continue;
        }
        Recipients.Add(i);
    }

    int32 UnmatchedTakes = 0;
    for (const TPair<FString, FString>& Take : PinnedTakes)
    {
        if (Deck.RemoveSingle(Take.Key) > 0) { continue; }
        // Only reachable if a pinned well holds a resource no well in this save was ever authored with
        // (a mod removed a well resource between sessions, say). Fall back to withdrawing this well's
        // OWN card, which it definitely contributed two loops ago -- that keeps the deal well-formed at
        // the cost of a one-card drift, and the conservation check below will PRINT that drift rather
        // than letting it pass silently.
        ++UnmatchedTakes;
        // ns-review-h1 I1: BRANCH ON THE RESULT. The fallback card is normally present -- this well
        // contributed it two loops ago -- but an EARLIER unmatched take may already have consumed it, and
        // the old line asserted "withdrew its own original instead" unconditionally. That is a claim
        // about a withdrawal that may not have happened, printed in the one branch that exists because
        // something is already wrong.
        const bool bFallbackTaken = (Deck.RemoveSingle(Take.Value) > 0);
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH1-ROLL: a pinned well holds '%s', which no card in the deck matched -- %s. "
                 "World totals will drift; see the pool lines below."),
            *WellShort(Take.Key),
            bFallbackTaken
                ? *FString::Printf(TEXT("withdrew its own original '%s' instead"), *WellShort(Take.Value))
                : *FString::Printf(TEXT("its own original '%s' was ALREADY consumed by an earlier unmatched "
                                        "withdrawal, so NOTHING was withdrawn for this well"), *WellShort(Take.Value)));
    }

    for (int32 i = Deck.Num() - 1; i > 0; --i) { Deck.Swap(i, WellRng.RandRange(0, i)); } // Fisher-Yates

    // Defensive: the arithmetic above makes these equal by construction (one add, one remove per pinned
    // well). Stated rather than assumed, because dealing past the end of either would be a crash and a
    // short deal would silently un-manage the tail.
    if (Deck.Num() != Recipients.Num())
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH1-ROLL: deck/recipient mismatch (%d cards for %d unpinned wells) -- dealing the overlap only. "
                 "This is a BUG in the membership filter; the pool lines below will show the resulting drift."),
            Deck.Num(), Recipients.Num());
    }

    int32 Changed = 0;
    const int32 DealCount = FMath::Min(Deck.Num(), Recipients.Num());
    for (int32 k = 0; k < DealCount; ++k)
    {
        FNodeShuffleWellEntry& E = Merged[Recipients[k]];
        E.AssignedResourceClassPath = Deck[k];
        E.bManaged = true;
        if (E.AssignedResourceClassPath != E.OriginalResourceClassPath) { ++Changed; }
    }
    for (int32 k = DealCount; k < Recipients.Num(); ++k)
    {
        // Undealt tail (only reachable via the guard above): leave it exactly vanilla rather than
        // managed-with-no-assignment.
        FNodeShuffleWellEntry& E = Merged[Recipients[k]];
        E.AssignedResourceClassPath = E.OriginalResourceClassPath;
        E.bManaged = false;
    }

    // T16 provenance totals, accumulated while the per-well lines print so the summary can state HOW the
    // pins were reached, not merely how many there are.
    int32 PinnedViaSpawned = 0, PinnedViaOriginal = 0, PinnedCarried = 0, RelocatedFallback = 0;
    int32 WellIndex = 0;
    for (const FNodeShuffleWellEntry& E : Merged)
    {
        ++WellIndex;
        // T16: WHICH ACTOR ANSWERED, per well, in the line that already reports the outcome. A pin whose
        // provenance is not printed is exactly how "0 pinned" survived 20 wells without anyone asking
        // whether it was even answerable.
        const FNodeShuffleWellPinCheck* Pin = PinByCore.Find(E.CorePath);
        FString PinDetail;
        if (!Pin)
        {
            // MEASURED: this roll's merge loop never reached this entry (its core did not stream in, or
            // it is one of our own spawned cores, excluded by the F1 filter above). NOT MEASURED: which
            // of those it was -- this line does not test it and must not name one.
            if (E.bPinned) { ++PinnedCarried; } // only a PIN can be carried; an unevaluated unpinned well is not one
            PinDetail = FString::Printf(
                TEXT("pinSrc=%s -- no pin evaluation ran for this entry this roll, so pinned=%d is the value "
                     "carried in the save"),
                WellPinSourceName(ENodeShuffleWellPinSource::NotEvaluated), E.bPinned ? 1 : 0);
        }
        else
        {
            if (Pin->Source == ENodeShuffleWellPinSource::OriginalWhileRelocated) { ++RelocatedFallback; }
            if (Pin->IsPinned())
            {
                if (Pin->Source == ENodeShuffleWellPinSource::Spawned) { ++PinnedViaSpawned; }
                else { ++PinnedViaOriginal; }
            }
            else if (E.bPinned) { ++PinnedCarried; } // not decisive, so the saved pin was kept
            PinDetail = FString::Printf(
                TEXT("pinSrc=%s tested=%dcore+%d/%dsat groupPlaced=%d coreInUse=%d(%s) satInUse=%d(%s) "
                     "firedOn='%s' decisive=%d"),
                WellPinSourceName(Pin->Source), Pin->CoresTested, Pin->SatellitesTested,
                Pin->SatellitesExpected, E.bGroupPlaced ? 1 : 0,
                Pin->bCoreInUse ? 1 : 0, Pin->CoreWhy,
                Pin->bSatelliteInUse ? 1 : 0, Pin->SatelliteWhy,
                Pin->FiredActorName.IsEmpty() ? TEXT("<none>") : *Pin->FiredActorName,
                Pin->IsDecisive() ? 1 : 0);
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH1-ROLL well #%d core='%s' sats=%d res '%s' -> '%s' pinned=%d managed=%d %s corePath='%s'"),
            WellIndex, *WellShort(E.CorePath), E.Satellites.Num(), *WellShort(E.OriginalResourceClassPath),
            *WellShort(E.AssignedResourceClassPath), E.bPinned ? 1 : 0, E.bManaged ? 1 : 0, *PinDetail,
            *E.CorePath);
    }

    // CONSERVATION CHECK -- WORLD TOTALS, NOT THE DECK (ns-review-h1 F3).
    //
    // The check this replaces compared the recipients' ORIGINALS against the resources they were DEALT.
    // Those are the same multiset by construction -- the deck IS that multiset, permuted -- so no input
    // could ever have made it fire. It was Fisher-Yates restated as an invariant, and this workspace has
    // shipped that exact shape twice before.
    //
    // The question worth asking is whether the WORLD still holds what it held: every well's authored
    // resource, against every well's EFFECTIVE resource afterwards, PINNED WELLS INCLUDED. That one can
    // fire, and it is precisely what F2's drift would have tripped -- a nitrogen card silently becoming
    // a water card is a difference between these two totals and nothing else.
    TMap<FString, int32> WorldBefore, WorldAfter, ShuffledHeld, PinnedHeld;
    for (const FNodeShuffleWellEntry& E : Merged)
    {
        if (!E.OriginalResourceClassPath.IsEmpty()) { WorldBefore.FindOrAdd(E.OriginalResourceClassPath)++; }
        if (E.AssignedResourceClassPath.IsEmpty()) { continue; }
        WorldAfter.FindOrAdd(E.AssignedResourceClassPath)++;
        // Keyed on the EFFECTIVE resource for BOTH columns: a pinned well reports what it actually
        // produces today, which is the number a reader of this log is trying to total up.
        (E.bManaged ? ShuffledHeld : PinnedHeld).FindOrAdd(E.AssignedResourceClassPath)++;
    }

    // Union of both key sets, so a resource that survives ONLY on pinned wells still gets a line -- and
    // so does one that has VANISHED from the world entirely, which would otherwise be invisible for
    // exactly the reason it matters most.
    TSet<FString> PoolKeys;
    for (const TPair<FString, int32>& P : WorldBefore) { PoolKeys.Add(P.Key); }
    for (const TPair<FString, int32>& P : WorldAfter)  { PoolKeys.Add(P.Key); }
    int32 ConservationBreaks = 0;
    for (const FString& Key : PoolKeys)
    {
        const int32 Before = WorldBefore.FindRef(Key);
        const int32 After = WorldAfter.FindRef(Key);
        if (After != Before) { ++ConservationBreaks; }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH1-ROLL pool '%s': world %d -> %d wells (%d shuffled + %d pinned)%s"),
            *WellShort(Key), Before, After, ShuffledHeld.FindRef(Key), PinnedHeld.FindRef(Key),
            After == Before ? TEXT("")
                            : TEXT("  <<< WORLD TOTAL CHANGED -- a well resource was created or destroyed, this is a BUG"));
    }
    if (ConservationBreaks > 0 || UnmatchedTakes > 0)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH1-ROLL: CONSERVATION FAILED -- %d resource(s) changed world total (%d unmatched pinned "
                 "withdrawals). A well-only resource such as Nitrogen Gas trending toward zero makes a save "
                 "uncompletable; read the pool lines above."),
            ConservationBreaks, UnmatchedTakes);
    }

    WellLayout = MoveTemp(Merged);
    bWellLayoutRolled = true;
    // T16: THE CONFLATION IS GONE. This line used to print one number, `%d pinned/unresolved`, computed
    // as WellLayout.Num() - DealCount -- so a pinned well, a well with no authored resource and an
    // undealt tail entry were one figure, and `0 pinned/unresolved` read as "nobody has built on a well"
    // when it was in fact "the pin question was never answerable for the 17 relocated wells". Four
    // disjoint counts now, and they sum to the well total by construction.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH1-ROLL: %s complete -- %d wells = %d managed (dealt a card) + %d PINNED (bPinned set "
             "-- see the per-well pinSrc for whether THIS roll measured it -- and "
             "holding an authored resource) + %d with no authored resource (pinned or not) + %d undealt "
             "(see any deck/recipient mismatch above). %d newly "
             "discovered this roll, %d actually changed resource. PIN PROVENANCE (T16, what the pin was "
             "MEASURED against): %d pinned via OUR SPAWNED relocated actors, %d via the vanilla level "
             "actors, %d carried from the save (not evaluated or not decisive this roll); %d relocated "
             "well(s) had to fall back to the HIDDEN original because no spawned core handle resolved -- "
             "an unpinned verdict on those is not a measurement of anything a player could build on. "
             "Seed %d (well stream %d)."),
        bIsReroll ? TEXT("re-roll") : TEXT("initial roll"), WellLayout.Num(), DealCount, PinnedTakes.Num(),
        NoAuthoredResource, Recipients.Num() - DealCount, NewlyDiscovered, Changed,
        PinnedViaSpawned, PinnedViaOriginal, PinnedCarried, RelocatedFallback, Seed, Seed ^ 0x57454C4C);
}
