// Packet ns-t23-stage0 (branch wip/h2-relocation): T23 STAGE 0 -- THE THREE INSTRUMENTS. LOG ONLY.
//
// READ _team/nodeshuffle-followups/T23-rolltime-commit-design.md FIRST. That design proposes moving the
// vanilla-well capture and suppression from the suppression pass to ROLL time, and its §6 states that
// stage 0's whole purpose is to produce the numbers that decide whether stages 1-3 happen at all. So
// this packet changes NO BEHAVIOUR. It captures nothing, suppresses nothing, hides nothing, spawns
// nothing, destroys nothing, and writes no placement field. The two SaveGame fields it adds
// (FNodeShuffleWellEntry::PassesSinceDealt / PassesFromDealToPlaced) are written by instrumentation and
// read by no decision anywhere in the mod.
//
// WHAT EACH INSTRUMENT ANSWERS, and which killer in the design's §7 table it is aimed at:
//
//   1. K1, THE DESIGN-KILLER PROBE. The design's seam is that vanilla well ACTORS are provably resident
//      at roll time (the enrolment gate is a residency proof) while the MESH PIECES are only assumed to
//      be. Routes 2 and 3 of the index (NodeShuffleWellMeshIndex.cpp) depend on SEPARATE
//      AFGNodeMeshActor / AStaticMeshActor actors being resident and iterable, which the fracking
//      actor's residency does not imply, and measured pairing puts most of the load on route 3. If the
//      index collapses at roll time the design is dead. This probe rebuilds the index AT the roll's
//      phase-3 commit and prints, per enrolled member, how many pieces it holds and by which route.
//
//   2. K3, THE DEFERRAL-WINDOW CENSUS. Under roll-time commit the well is ABSENT FROM THE WORLD for the
//      whole interval between being dealt a destination and being placed. That interval has never been
//      measured. Once per apply pass this walks the whole layout and prints the population, split and
//      distribution, with denominators.
//
//   3. WHY A YAW IS REJECTED, CORE vs SATELLITE. See NodeShuffleWellStage0.h for what that split can and
//      cannot be compared against; the counter block's own header carries the asymmetry that makes a
//      raw ratio meaningless.
//
// A NOTE ON THE ONE THING THIS PACKET DOES CHANGE, WHICH IS LOG TIMING, NOT BEHAVIOUR.
// Instrument 1 calls RebuildWellMeshIndex() at roll time. That function emits its own WELLH2B-INDEX
// summary and consumes one-shot slots in the shared WellVisualCaptureLogged throttle set (the
// narrowed / widened / bystander families). So with diagnostics ON, those "said once per mesh" lines
// are emitted during the ROLL instead of during the first apply pass, and one extra WELLH2B-INDEX
// summary line appears per roll. No consumer of the index runs between the roll and the apply pass:
// every reader (CaptureWellGroupVisuals, SuppressVanillaWellGroup, HideWellMemberMeshes) calls
// EnsureWellMeshIndex first, and the apply pass increments WellAuditPasses before any of them run, so
// the roll-time index is rebuilt from scratch before it is ever read.
//
// WHAT STILL RUNS WITH DIAGNOSTICS OFF, stated exactly because the earlier wording here ("this whole
// packet does not run at all") was FALSE. Instrument 1 does NOT run -- it is gated at its call site, so
// the roll path does no new work. Instrument 3's counters still increment (function locals) and nothing
// is emitted. Instrument 2 still increments PassesSinceDealt and still writes PassesFromDealToPlaced --
// both UPROPERTY(SaveGame), so THE SAVE FILE DIFFERS -- and EmitWellDeferralCensus is still CALLED once
// per apply pass and still emits at Display whenever its population counts change.
//
// IMPORT DISCIPLINE (memory:sf-shipping-export-trap). This file reaches NO new engine or FactoryGame
// entry point: it reads our own layout structs and our own index, calls FPlatformTime::Seconds (already
// used by the roll's own commit timing) and UE_LOG. That is the PREDICTION; the import table is
// MEASURED after the build and every symbol that moves is named. Three predictions have been falsified
// on this machine.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleWellRetype.h"   // WellShort
#include "NodeShuffleWellStage0.h"

#include "Components/StaticMeshComponent.h" // WellMeshIndex holds mesh-component weak pointers

namespace
{
    // Gate names, in the enum's order. Used for the data half of the emitted lines only; the prose half
    // never spells a field's value, because a legend that exemplifies its own fields has twice in this
    // repo produced a recommended grep that matches every line.
    const TCHAR* GateName(int32 G)
    {
        switch (G)
        {
        case FNodeShuffleWellProbeCensus::Gate_Void:         return TEXT("void");
        case FNodeShuffleWellProbeCensus::Gate_Water:        return TEXT("water");
        case FNodeShuffleWellProbeCensus::Gate_Cliff:        return TEXT("cliff");
        case FNodeShuffleWellProbeCensus::Gate_NodeOverlap:  return TEXT("nodeOverlap");
        case FNodeShuffleWellProbeCensus::Gate_Buildable:    return TEXT("buildableOverlap");
        case FNodeShuffleWellProbeCensus::Gate_Enclosed:     return TEXT("enclosed");
        case FNodeShuffleWellProbeCensus::Gate_Sibling:      return TEXT("siblingClearance");
        case FNodeShuffleWellProbeCensus::Gate_RelativeZ:    return TEXT("relativeZ");
        default:                                             return TEXT("unclassified");
        }
    }

    FString GateBreakdown(const int32* Counts)
    {
        FString S;
        for (int32 G = 0; G < FNodeShuffleWellProbeCensus::Gate_Count; ++G)
        {
            S += FString::Printf(TEXT("%s%s:%d"), (G == 0) ? TEXT("") : TEXT(" "), GateName(G), Counts[G]);
        }
        return S;
    }

    int32 SumGates(const int32* Counts)
    {
        int32 T = 0;
        for (int32 G = 0; G < FNodeShuffleWellProbeCensus::Gate_Count; ++G) { T += Counts[G]; }
        return T;
    }

    // Min / median / max over an ARBITRARY sample, sorted in place by the caller's copy. The design asks
    // for exactly these three and explicitly not a histogram struct. Median of an even-sized sample is
    // the lower of the two middles -- stated because an unstated tie-break is a number nobody can
    // reproduce, not because the choice matters.
    void MinMedMax(TArray<int32>& Sample, int32& OutMin, int32& OutMed, int32& OutMax)
    {
        OutMin = OutMed = OutMax = -1; // -1 is the NOT MEASURED sentinel: the sample was empty
        if (Sample.Num() == 0) { return; }
        Sample.Sort();
        OutMin = Sample[0];
        OutMax = Sample[Sample.Num() - 1];
        OutMed = Sample[(Sample.Num() - 1) / 2];
    }
}

// ------------------------------------------------------------------------------------------------
// INSTRUMENT 1 (K1) -- THE ROLL-TIME MESH-INDEX PROBE
// ------------------------------------------------------------------------------------------------
// PRECONDITION, AND IT IS THE WHOLE POINT: the caller must have called RebuildWellMeshIndex() -- NOT
// EnsureWellMeshIndex() -- immediately before the first call to this in a given roll.
// EnsureWellMeshIndex early-returns when WellMeshIndexPass == WellAuditPasses
// (NodeShuffleWellMeshIndex.cpp), and on a mid-session re-roll the roll runs BEFORE ApplyLayout, so
// WellAuditPasses still holds the previous tick's value and the index would be silently reused from
// before this roll's decisions. That is Appendix item 1 of the T23 design, a pre-existing defect: on the
// FIRST roll the defaults happen to make EnsureWellMeshIndex do the right thing, which is accidental
// correctness on the path nobody tested.
//
// The counts printed here are the ones a future roll-time capture would actually have to work with, and
// they are deliberately computed the same way NodeShuffleWellRelocateApply.cpp computes its
// suppression-time `indexedPieces` -- summed over the core path plus every satellite path -- so the two
// numbers are directly comparable without a reader having to reconcile two definitions.
void ANodeShuffleSubsystem::ProbeRollTimeWellMeshIndex(const FNodeShuffleWellEntry& E,
                                                      double IndexRebuildMs,
                                                      int32 BuiltAtCommitOrdinal) const
{
    const FString CoreLabel = WellShort(E.CorePath);

    int32 GroupPieces = 0, GroupOwn = 0, GroupLink = 0, GroupSpatial = 0;
    int32 MembersWithPieces = 0, MembersProbed = 0, MembersEmpty = 0;

    const auto ProbeMember = [&](const FString& Path, const TCHAR* Kind) -> void
    {
        ++MembersProbed;
        const TArray<TWeakObjectPtr<UStaticMeshComponent>>* Pieces = WellMeshIndex.Find(Path);
        const FIntVector* Routes = WellMeshIndexRouteCounts.Find(Path);
        const int32 N = Pieces ? Pieces->Num() : 0;
        const int32 Own = Routes ? Routes->X : 0;
        const int32 Link = Routes ? Routes->Y : 0;
        const int32 Spatial = Routes ? Routes->Z : 0;
        // How many of the recorded components are still resolvable RIGHT NOW. The index stores weak
        // pointers, so "the index has N entries" and "N components exist" are different statements and
        // only the second one is what a capture could read.
        int32 Live = 0;
        if (Pieces)
        {
            for (const TWeakObjectPtr<UStaticMeshComponent>& W : *Pieces) { if (W.IsValid()) { ++Live; } }
        }
        GroupPieces += N; GroupOwn += Own; GroupLink += Link; GroupSpatial += Spatial;
        if (N > 0) { ++MembersWithPieces; } else { ++MembersEmpty; }

        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2B-ROLLPROBE core='%s' %s member='%s': pieces=%d (live=%d) own=%d link=%d ")
            TEXT("spatial=%d. MEASURED at the roll's phase-3 commit, from an index rebuilt this roll by ")
            TEXT("RebuildWellMeshIndex (not the Ensure wrapper, which would have reused a pre-roll ")
            TEXT("index). The piece total is the count of components the index holds for this member's ")
            TEXT("path; the live count is how many of those weak pointers still resolve; the three route ")
            TEXT("counters name which of the index's three pairing routes contributed each piece, and ")
            TEXT("they sum to the piece total. NOT MEASURED here: whether a capture from these pieces ")
            TEXT("would succeed -- nothing is captured, this is a count."),
            *CoreLabel, Kind, *WellShort(Path), N, Live, Own, Link, Spatial);
    };

    ProbeMember(E.CorePath, TEXT("core"));
    for (const FNodeShuffleWellSatellite& S : E.Satellites)
    {
        // The same subset the suppression-time capture walks: an uncaptured satellite record is never
        // spawned and never dressed, so including it would inflate the empty-member count against a
        // population the comparison line does not carry either.
        if (!S.bCaptured) { continue; }
        ProbeMember(S.SatellitePath, TEXT("satellite"));
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2B-ROLLPROBE core='%s' GROUP: pieces=%d own=%d link=%d spatial=%d over %d probed ")
        TEXT("member(s), of which %d hold at least one piece and %d hold none. Index rebuilt once this ")
        TEXT("roll at commit ordinal %d, costing %.1f ms; that rebuild is the ONLY new work this ")
        TEXT("instrument puts on the roll path and it is charged once per roll, not once per well. ")
        TEXT("Index-wide this rebuild saw %d member(s) and %d piece(s). ")
        TEXT("THIS IS T23 STAGE 0 INSTRUMENT 1 (K1) AND IT IS THE COMPARISON THAT DECIDES THE DESIGN. ")
        TEXT("HOW TO RUN IT: grep WELLH2B-ROLLPROBE and WELLH2-SUPPRESS for the SAME core and compare ")
        TEXT("the two piece totals. WHAT THE TWO TOTALS ARE, AND WHY THEY ARE NOT THE SAME DEFINITION: ")
        TEXT("the total on THIS line sums the index over the core path plus every satellite path whose ")
        TEXT("record is bCaptured, while WELLH2-SUPPRESS's indexedPieces sums the core plus EVERY ")
        TEXT("satellite record with no bCaptured filter. The two are one measurement only while every ")
        TEXT("record of this group is captured, so read CapturedSatelliteCount against the record count ")
        TEXT("on this core's WELLH1-ROLL line FIRST: an uncaptured record is enough to make the totals ")
        TEXT("differ on its own, and that has to be excluded before a gap between them is read as a ")
        TEXT("roll-time collapse. WHAT THE COMPARISON DECIDES: the design's gate asks whether the ")
        TEXT("roll-time total reaches the suppression-time total; a roll-time total that collapses, and ")
        TEXT("in particular one whose spatial component collapses, is the measurement that kills ")
        TEXT("roll-time capture. NOTHING WAS CAPTURED, SUPPRESSED OR HIDDEN BY THIS LINE. No reason for ")
        TEXT("any number here is stated, because none was tested."),
        *CoreLabel, GroupPieces, GroupOwn, GroupLink, GroupSpatial, MembersProbed,
        MembersWithPieces, MembersEmpty, BuiltAtCommitOrdinal, IndexRebuildMs,
        WellMeshIndexMembers, WellMeshIndexPieces);
}

// ------------------------------------------------------------------------------------------------
// INSTRUMENT 2 (K3) -- THE DEFERRAL-WINDOW CENSUS
// ------------------------------------------------------------------------------------------------
// FIRE CONDITION, stated here and restated in the line itself: called once per ApplyWellRelocation pass,
// unconditionally, and it emits whenever the layout holds at least one entry that has been dealt a
// destination (placed or not). With diagnostics ON it emits every such pass; with diagnostics OFF it
// emits only when its own numbers change, so a quiet save does not carry a line every five seconds. A
// pass in which the layout holds no dealt entry at all emits NOTHING, and that silence is the one thing
// this line cannot tell you about -- read a WELLH2-ROLL line to see whether a deal ever happened.
void ANodeShuffleSubsystem::EmitWellDeferralCensus()
{
    if (WellLayout.Num() == 0) { return; }

    int32 Entries = WellLayout.Num();
    int32 Dealt = 0, Placed = 0, UnplacedDealt = 0, UnplacedFailed = 0, UnplacedLive = 0;
    int32 PlacedWithSample = 0, PlacedWithoutSample = 0, WaitingWithoutSample = 0;
    TArray<int32> WaitingSample;   // PassesSinceDealt over dealt-but-unplaced entries
    TArray<int32> PlacedSample;    // PassesFromDealToPlaced over placed entries that carry one

    for (const FNodeShuffleWellEntry& E : WellLayout)
    {
        if (!E.bDestDealt) { continue; }
        ++Dealt;
        if (E.bGroupPlaced)
        {
            ++Placed;
            if (E.PassesFromDealToPlaced > 0) { ++PlacedWithSample; PlacedSample.Add(E.PassesFromDealToPlaced); }
            else { ++PlacedWithoutSample; }
            continue;
        }
        ++UnplacedDealt;
        if (E.bRelocationFailed) { ++UnplacedFailed; } else { ++UnplacedLive; }
        // ns-t23-stage0 REVIEW FIX (SYMMETRY): the waiting side needs the SAME "no value was ever
        // recorded" bucket the placed side already has, and the same sentinel is available. The
        // increment runs at the top of ApplyWellRelocation's entry loop and this census runs at the end
        // of the same function, and bDestDealt goes false->true only in RollWellRelocation -- so an
        // entry dealt in THIS process has had at least one increment before it can appear here.
        // PassesSinceDealt == 0 on a dealt-but-unplaced entry therefore means exactly one thing: the
        // value came from a save written by a build older than ns-t23-stage0, and the true wait is
        // UNKNOWN and LONGER than zero. Folding it in as a zero biases K3 -- the number that decides
        // the whole design -- toward "the absence window is short".
        if (E.PassesSinceDealt > 0) { WaitingSample.Add(E.PassesSinceDealt); }
        else { ++WaitingWithoutSample; }
    }

    if (Dealt == 0) { return; }

    int32 WMin = -1, WMed = -1, WMax = -1, PMin = -1, PMed = -1, PMax = -1;
    MinMedMax(WaitingSample, WMin, WMed, WMax);
    MinMedMax(PlacedSample, PMin, PMed, PMax);

    // ns-t23-stage0 REVIEW FIX: the throttle key must NOT contain the min/median/max slots. Every entry
    // in the waiting sample is incremented once per apply pass by construction, so WMin/WMed/WMax each
    // rise by 1 every pass while any entry waits -- a key containing them changes every pass and
    // throttles nothing. Unfixed, one permanently relocation-FAILED entry puts a Display line in every
    // non-diagnostic player's log every ~5 s for the life of the save. The key is now the POPULATION
    // only; the distribution still prints in full whenever the population moves.
    const FString Key = FString::Printf(TEXT("%d|%d|%d|%d|%d|%d|%d"),
        Entries, Dealt, Placed, UnplacedDealt, UnplacedFailed, PlacedWithSample, PlacedWithoutSample);
    const bool bDiag = FNodeShuffleModule::AreDiagnosticsEnabled();
    if (!bDiag && Key == WellDeferCensusLastKey) { return; }
    WellDeferCensusLastKey = Key;

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-DEFERCENSUS pass %d: layout holds %d well entry/entries, of which %d have been ")
        TEXT("dealt a destination. Of those %d dealt: %d placed and %d dealt-but-unplaced (%d of the ")
        TEXT("unplaced are permanently relocation-failed, %d are still live). Waiting distribution over ")
        TEXT("the %d dealt-but-unplaced entries that carry a recorded value (%d carry none and are ")
        TEXT("EXCLUDED from it: a dealt-but-unplaced entry is incremented once per apply pass before ")
        TEXT("this line runs, so a zero there means the value came from a save written before ")
        TEXT("ns-t23-stage0 and the true wait is unknown and LONGER), in APPLY PASSES since this ")
        TEXT("entry's destination was FIRST dealt at the roll -- a re-deal inside the escalation ladder ")
        TEXT("deliberately does NOT restart it, so this is the whole absence window and not the age of ")
        TEXT("the current destination: min=%d median=%d max=%d. Deal-to-placed distribution over the %d ")
        TEXT("placed entries that carry a recorded value (%d placed entries carry none: the smallest ")
        TEXT("value this counter can hold is 1, so a zero means no value was ever recorded -- an entry ")
        TEXT("placed by a build older than ns-t23-stage0): min=%d median=%d max=%d. A value of -1 in ")
        TEXT("any of those six slots means the sample it summarises was EMPTY, not that the measurement ")
        TEXT("was zero. One apply pass is the mod's own well cadence, not a second and not a frame. ")
        TEXT("THIS IS T23 STAGE 0 INSTRUMENT 2 (K3): under the proposed roll-time commit the well would ")
        TEXT("be ABSENT FROM THE WORLD for exactly the interval these counters measure, so this ")
        TEXT("distribution is the cost side of that trade. Fire condition: emitted once per apply pass ")
        TEXT("while at least one entry is dealt; with diagnostics off it is emitted only when the ")
        TEXT("POPULATION counts above change -- the min/median/max slots rise every pass by ")
        TEXT("construction and are deliberately not part of that test. Every number here was counted ")
        TEXT("this pass; no reason for any of them is stated, because none was tested."),
        WellAuditPasses, Entries, Dealt, Dealt, Placed, UnplacedDealt, UnplacedFailed, UnplacedLive,
        WaitingSample.Num(), WaitingWithoutSample, WMin, WMed, WMax,
        PlacedWithSample, PlacedWithoutSample, PMin, PMed, PMax);
}

// ------------------------------------------------------------------------------------------------
// INSTRUMENT 3 -- THE PER-ATTEMPT REJECTION AGGREGATE
// ------------------------------------------------------------------------------------------------
// One line per TryPlaceWellGroup call, i.e. per group per apply pass in which that group actually got a
// placement attempt. Groups deferred for having no player near the destination never reach the function
// and are therefore absent from this line entirely -- they are counted by instrument 2 instead.
// ns-t23-stage0 REVIEW FIX (F2): AttemptsToExhaust is passed IN rather than computed here.
// ANodeShuffleSubsystem::WellYawSteps / WellYawAttemptsPerPass are private members of the subsystem and
// this is a free function, so the reviewer's inline expression does not compile from here; the spec's
// own named fallback is taken and the value is computed at the call site in TryPlaceWellGroup.
void LogWellProbeCensus(const FString& CoreLabel, const FNodeShuffleWellProbeCensus& C,
                        const TCHAR* Outcome, const TCHAR* TerminatedBy,
                        int32 AttemptCursor, int32 AttemptLimit, int32 CapturedSatellites,
                        int32 AttemptsToExhaust, int32 SatPlacementTries, double ElapsedMs)
{
    const int32 CoreTotal = SumGates(C.CoreRejects);
    const int32 SatTotal = SumGates(C.SatRejects);
    // ns-t27-corefirst: the LAYOUT gates' own denominator. Gate_Sibling and Gate_RelativeZ are tested
    // only on a candidate that already cleared ValidateWellMemberSpot, so their chances-to-fire are the
    // probes that survived the footprint, not all probes. Computed here so no reader has to.
    // ns-t27-review F9: Gate_Unclassified is a FOOTPRINT reject too -- it is written by
    // ValidateWellMemberSpot for any reason string the classifier does not recognise (no-world today) --
    // and it sits at index 8, outside the contiguous 0..Gate_Enclosed run. Omitting it inflates the
    // layout gates' denominator, in the direction that makes them look less binding than they are.
    int32 SatFootprintRejects = C.SatRejects[FNodeShuffleWellProbeCensus::Gate_Unclassified];
    for (int32 G = FNodeShuffleWellProbeCensus::Gate_Void; G <= FNodeShuffleWellProbeCensus::Gate_Enclosed; ++G)
    {
        SatFootprintRejects += C.SatRejects[G];
    }
    const int32 SatLayoutChances = C.SatProbes - SatFootprintRejects;
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLH2-PROBECENSUS core='%s': attempt ended %s, terminated by the %s side. Independent ")
        TEXT("layout attempts made in this call %d, attempt cursor now %d of %d. CORE probes %d, core ")
        TEXT("rejects %d [%s]. SATELLITE probes %d over %d captured satellite record(s), satellite ")
        TEXT("rejects %d [%s]. Of those satellite probes, %d cleared the footprint test and were the ")
        TEXT("only ones the two LAYOUT gates could fire on. Satellites that exhausted their whole draw ")
        TEXT("budget without finding any spot %d; the most draws any single satellite spent, whether or ")
        TEXT("not it found a spot %d of a per-satellite budget of %d. The bracketed lists carry one ")
        TEXT("counter per gate in evaluation ")
        TEXT("order, and each list sums to the reject total printed beside it. THE LIST SPANS TWO ")
        TEXT("FUNCTIONS: the ")
        TEXT("first six gates are ValidateWellMemberSpot's, in the order it evaluates them; the last ")
        TEXT("two are layout gates applied afterwards by TryPlaceWellGroup, which is why they have the ")
        TEXT("separate, smaller denominator named above -- AND WHY THEY ARE STRUCTURALLY ZERO IN THE CORE ")
        TEXT("LIST: the core is placed before any sibling exists and has no relative Z of its own, so ")
        TEXT("those two core counters can never be anything but zero and say nothing about the world. ")
        TEXT("A ninth counter, unclassified, closes the list and is not an evaluation position -- it is ")
        TEXT("any reason string the classifier did not recognise, counted rather than dropped, and it is ")
        TEXT("a FOOTPRINT reject, so it is inside the smaller denominator above and not outside it. ")
        TEXT("THE ENCLOSURE GATE IS ASYMMETRIC BY DESIGN AND ")
        TEXT("ITS TWO COUNTERS ARE NOT COMPARABLE: it always runs on the core and runs on satellites ")
        TEXT("only when WellEnclosureGateOnSatellites is compiled true, so a zero in the satellite ")
        TEXT("enclosure bucket may mean the gate is off rather than that nothing was enclosed -- read ")
        TEXT("the constant, not the zero. READ EACH REJECT COUNT AGAINST ITS OWN PROBE DENOMINATOR AND ")
        TEXT("NOT AGAINST THE OTHER SIDE'S: the core is validated ONCE per call and its settled ")
        TEXT("location is then reused by every attempt, while each satellite is drawn up to its full ")
        TEXT("budget within every attempt -- so the two populations differ in size by construction and ")
        TEXT("a raw core-vs-satellite ratio measures that construction, not the world. The comparable ")
        TEXT("fact is which side produced the rejection that ENDED this call, which is the side field ")
        TEXT("above and is taken from the branch this call returned on, never inferred from the ")
        TEXT("counters. COMPARE ONLY THE TWO ESCALATING OUTCOMES -- ESCALATED-core-rejected against ")
        TEXT("ESCALATED-layouts-exhausted -- AND DO NOT POOL THE SIDE FIELD ACROSS OUTCOMES. A core ")
        TEXT("rejection ends a call on the pass it happens, while layout exhaustion needs ")
        TEXT("WellLayoutAttempts/WellLayoutAttemptsPerPass consecutive passes (%d as this build is ")
        TEXT("compiled), so per unit time the core outcome is emitted about that many times as often ")
        TEXT("for the same underlying rate; per ESCALATION the two are one-for-one, because each ")
        TEXT("spends exactly one nudge. A BUDGET-SPENT-resuming line is not a termination at all. A ")
        TEXT("DEFERRED-void line also carries a side field and MUST NOT be pooled with either: void ")
        TEXT("means no terrain was found under the probe, which is a streaming state, not a fit ")
        TEXT("refusal. A SATELLITE THAT EXHAUSTS ITS BUDGET KILLS THE WHOLE ATTEMPT AND THE GROUP IS ")
        TEXT("NEVER PLACED SHORT -- all-or-nothing still governs the group -- so the exhaustion count ")
        TEXT("above is the price of that rule, not a count of missing satellites in the world. ")
        TEXT("ns-t27-perf -- THE NODE-OVERLAP GATE NOW READS A HOISTED SET, AND THIS IS ITS SIZE: ")
        TEXT("resource-node actors kept by the ONE world scan this call made %d, where a negative ")
        TEXT("value means no scan was made at all because the call ended at the core gate first. ")
        TEXT("Every satellite probe tested against that set instead of walking the level itself. The ")
        TEXT("set is the nodes within the satellite draw's outer radius plus the node reject radius, ")
        TEXT("measured in XY of the settled core, which is a superset of everything the per-probe walk ")
        TEXT("could have reached; the same deposit exclusion, the same self-exclusion and the same ")
        TEXT("radius test then run per probe exactly as before. Nothing ")
        TEXT("here states WHY any gate fired; each gate name is the predicate that returned false in ")
        TEXT("this run. TIME, LAST, AND IT IS THE ONLY TIME ON THIS LINE: this call spent %.2f ms of ")
        TEXT("wall clock on the game thread. That figure is this one call in this one frame on this ")
        TEXT("one machine; it attributes nothing and names no cause. Divide it by the probe counts ")
        TEXT("above to get a per-probe cost, and do not compare it across saves without also ")
        TEXT("comparing how built-up they are."),
        *CoreLabel, Outcome, TerminatedBy, C.AttemptsTried, AttemptCursor, AttemptLimit,
        C.CoreProbes, CoreTotal, *GateBreakdown(C.CoreRejects),
        C.SatProbes, CapturedSatellites, SatTotal, *GateBreakdown(C.SatRejects),
        SatLayoutChances, C.SatBudgetExhausted, C.MaxDrawsAnySat, SatPlacementTries,
        AttemptsToExhaust, C.NodesInScope, ElapsedMs);
}
