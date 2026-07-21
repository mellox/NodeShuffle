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
    static void ResetSessionCounters();
};
