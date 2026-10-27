#pragma once

// rwg-deal-1 (2026-10-19): the RWG card deal is SLOT-KIND-AGNOSTIC. On an RWG map (GetMapMode() == Rwg) every
// active, non-pinned slot -- existing original or new location -- draws from ONE merged deck (solid + liquid), and
// the CARD decides the node's form. Vanilla maps never call Deal(): they keep the FIX B kind-split deal in
// ANodeShuffleSubsystem::RollLayout, unchanged except for its purity draw (check_rwgdeal_lint pins both).
//
// rwg-purity-1 (2026-10-26, user decision): the PURITY decks below are SHARED by both deals (RWG Deal() and the
// Vanilla FIX B loop) -- one rule, one implementation. One shuffled purity source per CARD CLASS, built from the
// purities of that class's captured originals; a dealt slot draws from its card's class at the point the card is
// known. Spec: _team/nodeshuffle-followups/rwg-purity-spec.md.
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

    // rwg-purity-1: per-class purity sources. Built once per roll by BuildPurityDecks (all RNG use happens there, in
    // sorted class order); ApplyPurity uses no RNG. A class's source is its originals' purities SHUFFLED (a sample,
    // not a capture-order prefix); the class cursor walks it and wraps only when the class deals more cards than it
    // has originals. A card class with no original purity on record draws from the shuffled map-wide source instead.
    struct FPurityCensus
    {
        int32 Cards = 0;            // cards of this class in the deal decks
        int32 Orig[3] = {0, 0, 0};  // captured originals of this class, by purity (Impure, Normal, Pure)
        int32 Dealt = 0;            // dealt slots holding this class's card
        int32 Drawn[3] = {0, 0, 0}; // dealt slots whose purity was taken from a deck (RandomizePurity or a new node)
        int32 Assigned[3] = {0, 0, 0}; // every dealt slot's final AssignedPurity (drawn or kept)
        int32 FallbackDrawn = 0;    // drawn slots whose purity came from the map-wide source
    };
    struct FPurityDecks
    {
        TMap<FString, TArray<TEnumAsByte<EResourcePurity>>> ByClass; // card class -> shuffled originals' purities
        TMap<FString, int32> Cursor;                                  // card class -> next index into ByClass
        TArray<TEnumAsByte<EResourcePurity>> MapSource;               // shuffled map-wide purities (built only if a class needs it)
        int32 MapCursor = 0;
        TArray<FString> Classes;                                      // card classes, sorted (census order)
        TMap<FString, FPurityCensus> Census;
    };

    // Groups Layout's originals (!bIsNewNode, non-empty OriginalResourceClassPath, OriginalPurity != RP_MAX) by
    // OriginalResourceClassPath for every card class in the two piles, shuffles each class's source with Rng in sorted
    // class order, then shuffles MapSourceIn into Out.MapSource only when at least one card class has no source.
    void BuildPurityDecks(const TArray<FNodeShuffleEntry>& Layout, const TArray<TEnumAsByte<EResourcePurity>>& MapSourceIn,
        const TArray<FString>& SolidDeck, const TArray<FString>& LiquidDeck, FRandomStream& Rng, FPurityDecks& Out);
    // The purity rule of BOTH deals, called once per dealt slot after AssignedResourceClassPath is set: the slot's
    // class cursor advances; the drawn purity is written only when bRandomizePurity || E.bIsNewNode (else the slot
    // keeps the purity it was captured with). Records the census.
    void ApplyPurity(FNodeShuffleEntry& E, bool bRandomizePurity, FPurityDecks& D);
    // PURITYCENSUS: one Display line per card class plus one totals line whose res is ALL. DealName is "rwg" or "vanilla".
    void LogPurityCensus(const FPurityDecks& D, const TCHAR* DealName);

    struct FInputs
    {
        const TArray<FString>* SolidDeck = nullptr;   // already shuffled by RollLayout (same RNG draws as Vanilla)
        const TArray<FString>* LiquidDeck = nullptr;
        FPurityDecks* Purity = nullptr;               // rwg-purity-1: per-class purity, shared with the Vanilla deal
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

    // One pass over Layout in index order. Writes AssignedResourceClassPath, ResourceForm, AssignedPurity (ApplyPurity,
    // the rule the Vanilla deal also calls) and clears bActive on a new slot no card fits. Emits RWGDEAL, RWGDEAL-RES,
    // RWGDEAL-SLOTS (PURITYCENSUS is emitted by the caller, once, after either deal).
    void Deal(TArray<FNodeShuffleEntry>& Layout, const FInputs& In, FRandomStream& Rng, FResult& Out);
}
