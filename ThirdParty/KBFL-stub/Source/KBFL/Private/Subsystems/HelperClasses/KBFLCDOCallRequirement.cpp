// BUILD-TIME HEADER STUB ONLY — see README-STUB.md at the plugin root.
// No-op bodies for every non-inline function of the verbatim-copied UKBFLCDOCallRequirement header.
// These bodies exist ONLY to produce the import library; at runtime the player's REAL KBFL DLL
// provides /Script/KBFL.KBFLCDOCallRequirement and these implementations. Return values mirror the
// real defaults (IsRequirementMet -> true, ShouldCallDefered -> false) for honesty, nothing more.

#include "Subsystems/HelperClasses/KBFLCDOCallRequirement.h"

void UKBFLCDOCallRequirement::OnInit_Implementation()
{
}

bool UKBFLCDOCallRequirement::IsRequirementMet_Implementation(UKBFLContentCDOHelperSubsystem* Subsystem,
                                                              UKBFLCDOOverwriteBase* From, UObject* Target)
{
	return true;
}

void UKBFLCDOCallRequirement::OnModify_Implementation(UKBFLContentCDOHelperSubsystem* Subsystem,
                                                      UKBFLCDOOverwriteBase* From, UObject* Target)
{
}

void UKBFLCDOCallRequirement::OnModified_Implementation(UKBFLContentCDOHelperSubsystem* Subsystem,
                                                        UKBFLCDOOverwriteBase* From, UObject* Target)
{
}

void UKBFLCDOCallRequirement::OnFinishedAll_Implementation(UKBFLContentCDOHelperSubsystem* Subsystem,
                                                           UKBFLCDOOverwriteBase* From)
{
}

void UKBFLCDOCallRequirement::DeferedCall_Implementation(UKBFLContentCDOHelperSubsystem* Subsystem,
                                                         UKBFLCDOOverwriteBase* From, UObject* Target)
{
}

bool UKBFLCDOCallRequirement::ShouldCallDefered_Implementation(UKBFLContentCDOHelperSubsystem* Subsystem,
                                                               UKBFLCDOOverwriteBase* From, UObject* Target)
{
	return false;
}

void UKBFLCDOCallRequirement::DispatchDeferedCall(UKBFLContentCDOHelperSubsystem* Subsystem,
                                                  UKBFLCDOOverwriteBase* From, UObject* Target)
{
}

UWorld* UKBFLCDOCallRequirement::GetWorld() const
{
	return nullptr;
}
