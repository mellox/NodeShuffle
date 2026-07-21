// KBFL BUILD STUB — see README-STUB.md at the plugin root.
// Minimal UHT-visible declaration only: KBFLCDOCallRequirement.h's reflected
// signatures name this class, and UHT resolves such types module-wide, so the
// stub module must declare it somewhere. The real class (in
// Satisfactory-KMods/public-source) additionally inherits
// FUObjectArray::FUObjectCreateListener — deliberately omitted here; never use
// this stub for typed member/layout access, only as an opaque pointer type.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "KBFLContentCDOHelperSubsystem.generated.h"

UCLASS()
class KBFL_API UKBFLContentCDOHelperSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
};
