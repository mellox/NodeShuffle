#include "NodeShuffleRwgDeal.h"
#include "NodeShuffle.h"
#include "Misc/PackageName.h"

// rwg-deal-1 (2026-10-19). See NodeShuffleRwgDeal.h for the contract. RWG-only: the single caller sits inside
// RollLayout's `if (bRwgDeal)` branch; Vanilla maps keep the FIX B deal untouched.
namespace NodeShuffleRwgDeal
{
    namespace
    {
        int32 PurityBin(const TEnumAsByte<EResourcePurity> P)
        {
            const int32 V = (int32)P.GetValue();
            return (V >= 0 && V < 3) ? V : 1; // RP_MAX never reaches a deck; count anything out of range as Normal
        }
        void ShuffleSource(TArray<TEnumAsByte<EResourcePurity>>& Src, FRandomStream& Rng)
        {
            for (int32 i = Src.Num() - 1; i > 0; i--) { Src.Swap(i, Rng.RandRange(0, i)); }
        }
    }

    // rwg-purity-1 (2026-10-26). Contract in NodeShuffleRwgDeal.h. The ONLY RNG use is ShuffleSource, called once per
    // card class that has originals (sorted class order) and once for the map-wide source when a class has none.
    void BuildPurityDecks(const TArray<FNodeShuffleEntry>& Layout, const TArray<TEnumAsByte<EResourcePurity>>& MapSourceIn,
        const TArray<FString>& SolidDeck, const TArray<FString>& LiquidDeck, FRandomStream& Rng, FPurityDecks& Out)
    {
        for (const FString& Card : SolidDeck)  { Out.Census.FindOrAdd(Card).Cards++; }
        for (const FString& Card : LiquidDeck) { Out.Census.FindOrAdd(Card).Cards++; }
        Out.Census.GetKeys(Out.Classes);
        Out.Classes.Sort();

        TMap<FString, TArray<TEnumAsByte<EResourcePurity>>> Sources; // capture order; shuffled below
        for (const FNodeShuffleEntry& E : Layout)
        {
            if (E.bIsNewNode || E.OriginalResourceClassPath.IsEmpty() || E.OriginalPurity == RP_MAX) { continue; }
            FPurityCensus* C = Out.Census.Find(E.OriginalResourceClassPath);
            if (!C) { continue; } // not a card class this roll (gas, or no cards left after pinning)
            C->Orig[PurityBin(E.OriginalPurity)]++;
            Sources.FindOrAdd(E.OriginalResourceClassPath).Add(E.OriginalPurity);
        }

        bool bNeedMap = false;
        for (const FString& Class : Out.Classes)
        {
            TArray<TEnumAsByte<EResourcePurity>>* Src = Sources.Find(Class);
            if (!Src || Src->Num() == 0) { bNeedMap = true; continue; }
            ShuffleSource(*Src, Rng);
            Out.ByClass.Add(Class, MoveTemp(*Src));
        }
        if (bNeedMap)
        {
            Out.MapSource = MapSourceIn;
            ShuffleSource(Out.MapSource, Rng);
        }
    }

    void ApplyPurity(FNodeShuffleEntry& E, bool bRandomizePurity, FPurityDecks& D)
    {
        FPurityCensus& C = D.Census.FindOrAdd(E.AssignedResourceClassPath);
        C.Dealt++;
        TEnumAsByte<EResourcePurity> Draw = RP_MAX;
        bool bFromMap = false;
        if (const TArray<TEnumAsByte<EResourcePurity>>* Src = D.ByClass.Find(E.AssignedResourceClassPath))
        {
            int32& Cur = D.Cursor.FindOrAdd(E.AssignedResourceClassPath);
            Draw = (*Src)[Cur % Src->Num()];
            Cur++;
        }
        else if (D.MapSource.Num() > 0)
        {
            Draw = D.MapSource[D.MapCursor % D.MapSource.Num()];
            D.MapCursor++;
            bFromMap = true;
        }
        if ((bRandomizePurity || E.bIsNewNode) && Draw != RP_MAX)
        {
            E.AssignedPurity = Draw;
            C.Drawn[PurityBin(Draw)]++;
            if (bFromMap) { C.FallbackDrawn++; }
        }
        C.Assigned[PurityBin(E.AssignedPurity)]++;
    }

    void LogPurityCensus(const FPurityDecks& D, const TCHAR* DealName)
    {
        const auto Emit = [DealName](const FString& Res, const FPurityCensus& C)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("PURITYCENSUS: deal=%s res=%s cards=%d origImpure=%d origNormal=%d origPure=%d dealt=%d drawnImpure=%d drawnNormal=%d drawnPure=%d assignedImpure=%d assignedNormal=%d assignedPure=%d fallbackToMapDeck=%d"),
                DealName, *Res, C.Cards, C.Orig[0], C.Orig[1], C.Orig[2], C.Dealt, C.Drawn[0], C.Drawn[1], C.Drawn[2],
                C.Assigned[0], C.Assigned[1], C.Assigned[2], C.FallbackDrawn);
        };
        FPurityCensus Total;
        for (const FString& Class : D.Classes)
        {
            const FPurityCensus* C = D.Census.Find(Class);
            if (!C) { continue; }
            Emit(FPackageName::ObjectPathToObjectName(Class), *C);
            Total.Cards += C->Cards; Total.Dealt += C->Dealt; Total.FallbackDrawn += C->FallbackDrawn;
            for (int32 b = 0; b < 3; b++) { Total.Orig[b] += C->Orig[b]; Total.Drawn[b] += C->Drawn[b]; Total.Assigned[b] += C->Assigned[b]; }
        }
        Emit(TEXT("ALL"), Total);
    }

    void Deal(TArray<FNodeShuffleEntry>& Layout, const FInputs& In, FRandomStream& Rng, FResult& Out)
    {
        check(In.SolidDeck && In.LiquidDeck && In.Purity && In.Kinds);
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

        int32 Cursor = 0;
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
            ApplyPurity(E, In.bRandomizePurity, *In.Purity); // rwg-purity-1: per-class draw, same call as the Vanilla deal
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
