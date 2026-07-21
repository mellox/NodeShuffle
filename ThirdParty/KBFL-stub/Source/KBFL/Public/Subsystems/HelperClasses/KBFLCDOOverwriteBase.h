// KBFL BUILD STUB — see README-STUB.md at the plugin root.
// Minimal UHT-visible declaration only: KBFLCDOCallRequirement.h's reflected
// signatures name this class, and UHT resolves such types module-wide. The
// real class carries many UPROPERTYs (bEnabled, mCallPrio, mRequirements,
// mCachedRequirements, mSubsystem, ...) — all runtime access goes through
// reflection against the REAL loaded class, never through this stub's layout.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "KBFLCDOOverwriteBase.generated.h"

UCLASS()
class KBFL_API UKBFLCDOOverwriteBase : public UPrimaryDataAsset
{
	GENERATED_BODY()
};
