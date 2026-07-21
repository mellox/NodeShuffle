//

#pragma once

#include "CoreMinimal.h"
#include "KBFLCDOCallRequirement.generated.h"

/**
 * This class can be used to create requirements that must be met for a CDO Helper to be called
 * This is mainly used for KBFLCDOOverwrite classes to determine if they should be applied or not
 * Since DataAssets doesn't support blueprint events, we need to use this helper class
 * You can also use this to trigger events when a CDO overwrite is applied
 */
UCLASS(BlueprintType, Blueprintable, HideCategories = (Object))
class KBFL_API UKBFLCDOCallRequirement : public UObject
{
	GENERATED_BODY()

public:
	/** Called once when this requirement is first set up. Override to do one-time initialization. Default: no-op. */
	UFUNCTION(BlueprintNativeEvent)
	void OnInit();

	/**
	 * Returns whether the gating condition for applying the overwrite to Target is satisfied.
	 * Default implementation returns true (always met). Override to add custom guard logic.
	 * @param Subsystem The CDO helper subsystem driving the apply pass.
	 * @param From      The overwrite asking whether it may apply.
	 * @param Target    The object (CDO or instance) about to be modified.
	 * @return true if the overwrite is allowed to run on Target.
	 */
	UFUNCTION(BlueprintNativeEvent)
	bool IsRequirementMet(class UKBFLContentCDOHelperSubsystem* Subsystem, class UKBFLCDOOverwriteBase* From,
						  UObject* Target);

	/** Called immediately before Target is modified by the overwrite. Default: no-op. */
	UFUNCTION(BlueprintNativeEvent)
	void OnModify(class UKBFLContentCDOHelperSubsystem* Subsystem, class UKBFLCDOOverwriteBase* From, UObject* Target);

	/** Called immediately after Target has been modified by the overwrite. Default: no-op. */
	UFUNCTION(BlueprintNativeEvent)
	void OnModified(class UKBFLContentCDOHelperSubsystem* Subsystem, class UKBFLCDOOverwriteBase* From,
					UObject* Target);

	/** Called once after the overwrite has finished modifying all of its targets. Default: no-op. */
	UFUNCTION(BlueprintNativeEvent)
	void OnFinishedAll(class UKBFLContentCDOHelperSubsystem* Subsystem, class UKBFLCDOOverwriteBase* From);

	/**
	 * Object-scoped deferred callback for Target (e.g. deferred actor modification or destroy).
	 * When ShouldCallDefered returns true it is scheduled on the next tick. Use this when the work
	 * must not happen during a level-add / mixin pass (e.g. destroying actors), which can corrupt
	 * the actor array SML iterates.
	 * @param Target The object the deferred work should act on.
	 */
	UFUNCTION(BlueprintNativeEvent)
	void DeferedCall(class UKBFLContentCDOHelperSubsystem* Subsystem, class UKBFLCDOOverwriteBase* From,
					 UObject* Target);

	/**
	 * Gates DeferedCall for Target. BlueprintNativeEvent so it can depend on runtime state. Return
	 * true to have DeferedCall scheduled on the next tick; return false (the default) to skip
	 * DeferedCall entirely — it is NOT run inline.
	 * @param Target The object DeferedCall would act on.
	 */
	UFUNCTION(BlueprintNativeEvent)
	bool ShouldCallDefered(class UKBFLContentCDOHelperSubsystem* Subsystem, class UKBFLCDOOverwriteBase* From,
						   UObject* Target);

	/**
	 * Dispatches DeferedCall for Target, honoring ShouldCallDefered. When deferral is requested the
	 * call runs on the next tick (after the current apply pass, including OnFinishedAll); when not
	 * requested, DeferedCall is skipped.
	 */
	void DispatchDeferedCall(class UKBFLContentCDOHelperSubsystem* Subsystem, class UKBFLCDOOverwriteBase* From,
							 UObject* Target);

	UPROPERTY(Transient)
	TObjectPtr<class UKBFLContentCDOHelperSubsystem> mSubsystem;

	/** Resolves the world via the owning subsystem (mSubsystem). Returns null if the subsystem is invalid. */
	virtual class UWorld* GetWorld() const override;
};
