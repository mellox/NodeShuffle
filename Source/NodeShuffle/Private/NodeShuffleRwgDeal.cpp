#include "NodeShuffleRwgDeal.h"
#include "NodeShuffle.h"
#include "Misc/PackageName.h"

// rwg-deal-1 (2026-10-19). See NodeShuffleRwgDeal.h for the contract. RWG-only: the single caller sits inside
// RollLayout's `if (bRwgDeal)` branch; Vanilla maps keep the FIX B deal untouched.
namespace NodeShuffleRwgDeal
{
    void Deal(TArray<FNodeShuffleEntry>& Layout, const FInputs& In, FRandomStream& Rng, FResult& Out)
    {
        check(In.SolidDeck && In.LiquidDeck && In.PurityDeck && In.Kinds);
        const TMap<FString, FKindInfo>& Kinds = *In.Kinds;
        const auto KindOf = [&](const FString& Card) -> FKindInfo
        {
            const FKindInfo* K = Kinds.Find(Card);
            return K ? *K : FKindInfo();
        };

        // ONE merged deck, shuffled here (RWG only -- the Vanilla RNG stream never sees these draws).
        TArray<FString> Deck;
        Deck.Append(*In.SolidDeck);
        Deck.Append(*In.LiquidDeck);
        for (int32 i = Deck.Num() - 1; i > 0; i--) { Deck.Swap(i, Rng.RandRange(0, i)); }

        // Per-resource census + reservation counts. A non-Dealable card can only ever land on an EXISTING slot of
        // its own form, so it is "reserved" for those: when the reserved cards of a form are as many as the existing
        // slots of that form still to come, the slot draws from the reserved cards only (keeps them from stranding
        // in the tail where only new slots remain).
        TMap<FString, TPair<int32, int32>> PerRes; // kind -> (cards, dealt)
        TMap<uint8, int32> ReserveRemaining, ExistingRemaining;
        for (const FString& Card : Deck)
        {
            PerRes.FindOrAdd(Card, TPair<int32, int32>(0, 0)).Key++;
            if (Card.StartsWith(TEXT("/Game/"))) { Out.VanillaCards++; } else { Out.ModdedCards++; }
            const FKindInfo K = KindOf(Card);
            if (!K.Dealable()) { ReserveRemaining.FindOrAdd(K.Form)++; }
        }
        Out.Cards = Deck.Num();
        const auto SlotFormOf = [&](const FNodeShuffleEntry& E) -> uint8 { return E.ResourceForm != 0 ? E.ResourceForm : In.FormSolid; };
        const auto IsSlot = [&](const FNodeShuffleEntry& E) -> bool
        {
            return E.bActive && !E.bPinned && !(!E.bIsNewNode && In.IsGas && In.IsGas(E.AssignedResourceClassPath));
        };
        for (const FNodeShuffleEntry& E : Layout)
        {
            if (IsSlot(E) && !E.bIsNewNode) { ExistingRemaining.FindOrAdd(SlotFormOf(E))++; }
        }
        const auto HaveRep = [&](uint8 Form) -> bool { return Form == In.FormLiquid ? In.bHaveLiquidRep : In.bHaveSolidRep; };

        int32 Cursor = 0, PurityIdx = 0;
        for (FNodeShuffleEntry& E : Layout)
        {
            if (!IsSlot(E)) { continue; }
            Out.ActiveSlots++;
            const bool bNew = E.bIsNewNode;
            const uint8 SlotForm = bNew ? 0 : SlotFormOf(E);
            bool bForceReserve = false;
            if (bNew) { Out.NewSlots++; }
            else
            {
                Out.ExistingSlots++;
                const int32 Res = ReserveRemaining.FindRef(SlotForm);
                bForceReserve = Res > 0 && Res >= ExistingRemaining.FindRef(SlotForm);
                ExistingRemaining.FindOrAdd(SlotForm)--;
            }

            // Uniform draw with bounded rejection: pick j uniformly in [Cursor, End); a card this slot cannot take is
            // swapped behind End (End--) and stays in the deck for later slots. At most Deck.Num() draws per pass.
            bool bRuleRejected = false;
            FString Card;
            for (int32 Pass = 0; Pass < 2 && Card.IsEmpty(); Pass++)
            {
                const bool bReserveOnly = (Pass == 0) && bForceReserve;
                if (Pass == 1 && !bForceReserve) { break; } // the unrestricted pass already ran as pass 0
                int32 End = Deck.Num();
                while (Cursor < End)
                {
                    const int32 j = Rng.RandRange(Cursor, End - 1);
                    const FKindInfo K = KindOf(Deck[j]);
                    bool bOk;
                    if (bReserveOnly) { bOk = !K.Dealable() && K.Form == SlotForm; }
                    else if (!K.Dealable())
                    {
                        bOk = !bNew && K.Form == SlotForm;          // no class, no look: existing slot of its own form only
                        if (!bOk) { bRuleRejected = true; }
                    }
                    else if (!bNew && K.Form != SlotForm && !HaveRep(K.Form))
                    {
                        bOk = false;                                 // form change with no spawn class for that form
                        Out.FormChangeNoRepRejects++;
                    }
                    else { bOk = true; }
                    if (bOk) { Deck.Swap(Cursor, j); Card = Deck[Cursor]; Cursor++; break; }
                    Deck.Swap(j, End - 1);
                    End--;
                }
                if (bReserveOnly && !Card.IsEmpty()) { Out.ReserveForced++; }
            }
            if (bRuleRejected) { Out.VanillaOnlyFallbacks++; }

            if (Card.IsEmpty())
            {
                if (bNew)
                {
                    E.bActive = false; // no card this new slot can take: same outcome as the FIX B starved new node
                    Out.SlotsLeftInactiveNoCard++;
                }
                else
                {
                    Out.ExistingKeptNoCard++; // keep its vanilla assignment (FIX B behaviour for an existing slot)
                }
                continue;
            }

            const FKindInfo K = KindOf(Card);
            if (!K.Dealable()) { ReserveRemaining.FindOrAdd(K.Form)--; }
            E.AssignedResourceClassPath = Card;
            if (!bNew && K.Form != SlotForm)
            {
                Out.FormChanged.Add(E.EntryGuid);
                if (K.Form == In.FormLiquid) { Out.SolidToLiquid++; } else { Out.LiquidToSolid++; }
            }
            E.ResourceForm = K.Form; // form follows the card
            const TArray<TEnumAsByte<EResourcePurity>>& PD = *In.PurityDeck;
            if ((In.bRandomizePurity || E.bIsNewNode) && PD.Num() > 0)
            {
                E.AssignedPurity = PD[PurityIdx % PD.Num()];
            }
            PurityIdx++;
            Out.Dealt++;
            PerRes.FindOrAdd(Card, TPair<int32, int32>(0, 0)).Value++;
        }

        UE_LOG(LogNodeShuffle, Display,
            TEXT("RWGDEAL: activeSlots=%d cards=%d dealt=%d vanillaCards=%d moddedCards=%d undealtCards=%d slotsLeftInactiveNoCard=%d formChangedSolidToLiquid=%d liquidToSolid=%d noClassNoLookRejectSlots=%d"),
            Out.ActiveSlots, Out.Cards, Out.Dealt, Out.VanillaCards, Out.ModdedCards, Out.Cards - Out.Dealt,
            Out.SlotsLeftInactiveNoCard, Out.SolidToLiquid, Out.LiquidToSolid, Out.VanillaOnlyFallbacks);
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RWGDEAL-SLOTS existingSlots=%d newSlots=%d existingKeptNoCard=%d reserveForced=%d formChangeNoRepRejects=%d"),
            Out.ExistingSlots, Out.NewSlots, Out.ExistingKeptNoCard, Out.ReserveForced, Out.FormChangeNoRepRejects);
        TArray<FString> ResKeys;
        PerRes.GetKeys(ResKeys);
        ResKeys.Sort();
        for (const FString& Res : ResKeys)
        {
            const TPair<int32, int32>& C = PerRes.FindChecked(Res);
            const FKindInfo K = KindOf(Res);
            UE_LOG(LogNodeShuffle, Display, TEXT("RWGDEAL-RES res=%s cards=%d dealt=%d dealable=%d ownClass=%d look=%d"),
                *FPackageName::ObjectPathToObjectName(Res), C.Key, C.Value, K.Dealable() ? 1 : 0, K.bOwnClass ? 1 : 0, K.bLook ? 1 : 0);
        }
    }
}
