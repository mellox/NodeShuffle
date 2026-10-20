#pragma once

// rwg-deal-1 (2026-10-19): the RWG card deal is SLOT-KIND-AGNOSTIC. On an RWG map (GetMapMode() == Rwg) every
// active, non-pinned slot -- existing original or new location -- draws from ONE merged deck (solid + liquid), and
// the CARD decides the node's form. Vanilla maps never call this: they keep the FIX B kind-split deal in
// ANodeShuffleSubsystem::RollLayout byte for byte (check_rwgdeal_lint pins both).
// Spec + measurements: _team/test-logs/2026-10-19-nodeshuffle-inactive-entries.md (deck exhaustion, 134 undealt cards).
//
// Dealable(kind) := generic resource (/Game/ or /Script/FactoryGame.) OR rank-2 own node class (latch/registry)
//                   OR an authored/captured look (ResourceHasAuthoredLook).
// A NEW slot takes only Dealable cards. An EXISTING slot takes any Dealable card of either form, and a
// non-Dealable card only when its form equals the slot's pre-deal form (the FIX B behaviour for existing slots).
// A form-changing card on an existing slot needs a spawn class for the card's form (HaveRep), else it is rejected.

#include "CoreMinimal.h"
#include "NodeShuffleSubsystem.h"

namespace NodeShuffleRwgDeal
{
    struct FKindInfo
    {
        uint8 Form = 1;          // EResourceForm numeric value of the card (FormSolid when unknown)
        bool bGeneric = false;   // NodeShuffleRepClass::IsGenericNodeClassPath(kind)
        bool bOwnClass = false;  // latched/registry class is rank-2 own-mount for this kind
        bool bLook = false;      // ResourceHasAuthoredLook(kind)
        bool Dealable() const { return bGeneric || bOwnClass || bLook; }
    };

    struct FInputs
    {
        const TArray<FString>* SolidDeck = nullptr;   // already shuffled by RollLayout (same RNG draws as Vanilla)
        const TArray<FString>* LiquidDeck = nullptr;
        const TArray<TEnumAsByte<EResourcePurity>>* PurityDeck = nullptr;
        const TMap<FString, FKindInfo>* Kinds = nullptr;
        TFunction<bool(const FString&)> IsGas;        // existing gas slots stay relocate-only (same skip as FIX B)
        bool bRandomizePurity = false;
        bool bHaveSolidRep = false;                   // SpawnableNodeClassPath non-empty
        bool bHaveLiquidRep = false;                  // SpawnableLiquidNodeClassPath non-empty
        uint8 FormSolid = 1;
        uint8 FormLiquid = 2;
    };

    struct FResult
    {
        TSet<FGuid> FormChanged;   // existing slots whose card form differs from the slot's pre-deal form
        int32 ActiveSlots = 0, ExistingSlots = 0, NewSlots = 0;
        int32 Cards = 0, Dealt = 0, VanillaCards = 0, ModdedCards = 0;
        int32 SlotsLeftInactiveNoCard = 0, ExistingKeptNoCard = 0;
        int32 SolidToLiquid = 0, LiquidToSolid = 0;
        int32 VanillaOnlyFallbacks = 0, ReserveForced = 0, FormChangeNoRepRejects = 0;
    };

    // One pass over Layout in index order. Writes AssignedResourceClassPath, ResourceForm, AssignedPurity (same
    // purity rule as FIX B) and clears bActive on a new slot no card fits. Emits RWGDEAL, RWGDEAL-RES, RWGDEAL-SLOTS.
    void Deal(TArray<FNodeShuffleEntry>& Layout, const FInputs& In, FRandomStream& Rng, FResult& Out);
}
