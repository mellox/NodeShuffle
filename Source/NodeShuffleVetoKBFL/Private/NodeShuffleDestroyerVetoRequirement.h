#pragma once

#include "CoreMinimal.h"
#include "Subsystems/HelperClasses/KBFLCDOCallRequirement.h"
#include "NodeShuffleDestroyerVetoRequirement.generated.h"

// coexist-veto-1: the veto itself. Prepended (index 0) into the mRequirements list of every
// node-relevant KBFL actor-destroyer/listener asset by the arm pass. KBFL's Requirements_IsMet
// iterates requirements IN ORDER and SHORT-CIRCUITS on the first false — so for a NodeShuffle-
// managed node this returns false FIRST and the owning asset's own requirement code (whose
// DeferedCall typically performs the destroy) is never even evaluated for that actor; for every
// other actor this returns true and the asset behaves exactly as if NodeShuffle were absent.
//
// T61 (2026-08-10): the hook is now PREPENDED AND EVALUATED EVEN WHEN THE MASTER GATE
// (NodeShuffle.DestroyerVeto) IS OFF — see ResetSessionCounters' bObserveOnly below. In that state the
// sentence above holds for EVERY actor without exception: the requirement returns true for managed
// nodes too, so the asset behaves exactly as if NodeShuffle were absent while we measure. The author's
// ruling that put it there is quoted in docs/TECH-DEBT.md T61.
//
// ABI SAFETY: ZERO added member fields — the instance layout is EXACTLY the (stubbed, verbatim)
// UKBFLCDOCallRequirement base, and all veto state lives in the NodeShuffle module, reached via its
// exported static API. The arm pass additionally size-guards the base class layout at runtime.
//
// INHERITED-METHOD SAFETY (why everything except IsRequirementMet is left un-overridden):
//   - IsRequirementMet_Implementation (below) reads NO base state — mSubsystem may be null/unset.
//   - OnInit / OnModify / OnModified / OnFinishedAll inherit the base's no-op defaults.
//   - DeferedCall inherits the no-op and ShouldCallDefered inherits `return false`, so
//     DispatchDeferedCall never schedules work; it additionally short-circuits when GetWorld() is
//     null (GetWorld() resolves via mSubsystem, which normal KBFL instantiation stamps and the
//     late-arm injection mirrors).
//   This analysis holds ONLY while this class never overrides ShouldCallDefered / DeferedCall —
//   revisit the mSubsystem/world wiring before ever doing so.
UCLASS()
class UNodeShuffleDestroyerVetoRequirement : public UKBFLCDOCallRequirement
{
    GENERATED_BODY()

public:
    // BlueprintNativeEvent override (no UFUNCTION re-declaration). Runs for EVERY actor event that
    // matches an armed asset's target classes while armed — kept to one set lookup plus a
    // spawn-window flag read (spawnrace-1), game-thread only.
    virtual bool IsRequirementMet_Implementation(UKBFLContentCDOHelperSubsystem* Subsystem,
                                                 UKBFLCDOOverwriteBase* From, UObject* Target) override;

    // Called by the arm pass at each world init so "this session" counters mean this world session.
    // T58: also LATCHES the foreign-node protection policy for the whole world session (read once from
    // NodeShuffle.ProtectForeignNodes by the caller), so a console flip mid-sweep can never split one
    // KBFL sweep across two policies — the same "takes effect at world load" rule DestroyerVeto uses.
    //
    // T61 (ns-t61-observe-always, 2026-08-10) ADDED bObserveOnly. THE OBSERVE-ONLY INVARIANT: while it
    // is true, IsRequirementMet returns TRUE for EVERY target class — managed nodes included — so the
    // armed hook returns exactly what the un-hooked chain would have returned and NOTHING in the world
    // changes. The classification, the counters, the per-resource row population and the census still
    // run: that is the entire point of the mode. It is latched here, once per world, exactly like the
    // protection policy beside it. bProtectForeignNodes is passed FALSE by the arm pass whenever
    // bObserveOnly is true, so the two can never disagree about whether protection is in force.
    static void ResetSessionCounters(bool bProtectForeignNodes, bool bObserveOnly);

    // T58 F1 (cold review, 2026-08-10): foreign-node protection is evaluable ONLY on assets whose own
    // target list is a BROAD node sweep (FGResourceNodeBase itself or a superclass of it). An asset
    // targeting a strict SUBCLASS — measured: RefinedPower's ActorListner_RPTurbine on
    // RPWaterTurbineNode — is that mod's own handler for its own nodes, and short-circuiting its
    // requirement chain silently disables it. The arm pass declares the set; the predicate consults it.
    static void ResetForeignProtectAssets();
    static void AddForeignProtectAsset(const UObject* Asset);

    // T58: how many assets the arm pass actually prepended us into, recorded at the END of that pass.
    // It is the DENOMINATOR that tells a zero apart from a no-op: "0 foreign destroy attempts seen" on
    // 0 armed assets says nothing about the world, while the same zero on 2 armed assets does.
    static void SetArmedAssetCount(int32 ArmedAssets);

    // T58: one census line for this world session — every evaluation this requirement saw, partitioned,
    // with the per-class breakdown of the foreign nodes. Called from world timers set by the arm pass.
    // Phase names WHEN the snapshot was taken; it is a label, not a measurement.
    static void EmitForeignCensus(const TCHAR* Phase);
};
