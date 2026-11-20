// ns-finalflag-1 (2026-11-20): FNodeShuffleEntry::bPlacementFinal, "this row's placement is final", read by other mods by reflected name.
// THE ONE WRITER. ReconcilePlacementFinal moves, spawns, destroys and re-rolls NOTHING, and writes no entry field other than
// bPlacementFinal; besides that one field it writes only the subsystem's PlacementFinalEpoch and this packet's session state
// (FinalAnchors, FinalRevokedAt, FinalApplyPass, bFinalLoadPending, FinalLastCensus). tools/check_finalflag_lint.ps1 pins this (F1, F9).
// Design: _team/test-logs/2026-11-20-nodeshuffle-finalflag-1-design.md (D1-D6). Consumer contract: docs/DIAGNOSTICS.md.
//
// Stages: Seed (PostLoadGame) rebuilds the anchors from the saved flags and writes no flag. ApplyStart (first statement of ApplyLayout)
// verifies flagged rows and may clear, never sets; it is skipped while PlacementFinalEpoch is 0. ApplyEnd (the line before
// EmitConsumerCensus) verifies, then sets rows that pass the predicate and are out of the dwell, then increments the epoch.
#include "NodeShuffleSubsystem.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceNodeBase.h"

namespace NsFinal
{
    static constexpr float ToleranceCm = 200.0f;   // strict <, 3-D (FVector::DistSquared): the same test PrimalFauna applies at intake
    static constexpr int32 DwellReconciles = 3;    // a row whose flag went true->false stays false for at least this many ApplyEnd reconciles
    static constexpr int32 MovedLineCap = 16;      // FINALMOVED lines per stage call; the rest are counted in linesSuppressed
    static constexpr int32 CensusFields = 16;      // compared fields of FINALCENSUS (everything after mode)

    static bool Within(const FVector& A, const FVector& B) { return FVector::DistSquared(A, B) < FMath::Square(ToleranceCm); }
    static FString Vec(const FVector& V) { return FString::Printf(TEXT("(%.0f,%.0f,%.0f)"), V.X, V.Y, V.Z); }
    static FString ShortRes(const FString& Path)
    {
        int32 Dot = INDEX_NONE;
        return Path.FindLastChar(TEXT('.'), Dot) ? Path.Mid(Dot + 1) : Path;
    }
}

void ANodeShuffleSubsystem::ReconcilePlacementFinal(ENsFinalStage Stage)
{
    if (Stage == ENsFinalStage::Seed)
    {
        // Not gated on the map mode: PostLoadGame may run before GetMapMode latches, and the saved flag plus the saved Location IS the anchor.
        FinalAnchors.Reset();
        FinalRevokedAt.Reset();
        FinalLastCensus.Reset();
        int32 Flagged = 0;
        for (const FNodeShuffleEntry& E : Layout)
        {
            if (E.bPlacementFinal) { ++Flagged; FinalAnchors.Add(E.EntryGuid, E.Location); }
        }
        bFinalLoadPending = true;
        UE_LOG(LogNodeShuffle, Display, TEXT("FINALSEED: flaggedInSave=%d"), Flagged);
        return;
    }
    if (Stage == ENsFinalStage::ApplyStart) { ++FinalApplyPass; }
    const ENodeShuffleMapMode Mode = GetMapMode();
    if (Mode != ENodeShuffleMapMode::Vanilla && Mode != ENodeShuffleMapMode::Rwg) { return; } // the epoch is not bumped on any other mode
    if (Stage == ENsFinalStage::ApplyStart && PlacementFinalEpoch == 0) { return; } // flags as loaded: the first ApplyEnd validates them
    const bool bVanilla = Mode == ENodeShuffleMapMode::Vanilla;
    const bool bEnd = Stage == ENsFinalStage::ApplyEnd;
    const bool bLoadStage = bEnd && bFinalLoadPending;
    const TCHAR* StageName = !bEnd ? TEXT("applyStart") : (bLoadStage ? TEXT("load") : TEXT("applyEnd"));
    const int32 Recon = PlacementFinalEpoch + 1; // the number this pass's ApplyEnd reconcile carries (a revoke at ApplyStart is charged to it)

    // C[]: rows checked kept revokedRowMoved revokedActorMoved revokedActorLost revokedPredicate noAnchor newlyFinal dwellHeld finalNow
    //      armPinned armNewRow armOriginal linesSuppressed stickyUnresolved
    int32 C[NsFinal::CensusFields] = {};
    C[0] = Layout.Num();
    int32 MovedLines = 0;
    for (FNodeShuffleEntry& E : Layout)
    {
        // The predicate's row half, as designed (D5): bActive && (bPinned || (new, ray-casted, not provisional, and on vanilla or the RWG clause)).
        const bool bRowBits = E.bActive && (E.bPinned || (E.bIsNewNode && E.bRayCasted && !E.bRwgProvisional
            && (bVanilla || E.bRwgSettledOnTerrain || E.bUnderground || E.bRwgCave)));
        if (!E.bPlacementFinal && !(bEnd && bRowBits)) { continue; } // nothing to verify and nothing to set
        const AActor* Actor = nullptr;
        if (E.bIsNewNode)
        {
            AFGResourceNode* const* Slot = SpawnedNodes.Find(E.EntryGuid);
            if (Slot && IsValid(*Slot)) { Actor = *Slot; }
        }
        else if (!E.VanillaNodePath.IsEmpty())
        {
            AFGResourceNodeBase* Base = FindOriginalBaseByPath(E.VanillaNodePath);
            if (IsValid(Base)) { Actor = Base; }
        }
        const bool bActorBelow = Actor && NsFinal::Within(Actor->GetActorLocation(), E.Location);
        const bool bSticky = !Actor && !E.bIsNewNode && E.bPinned; // decision 5: a pinned original whose path does not resolve
        bool bNext = E.bPlacementFinal;
        if (E.bPlacementFinal)
        {
            ++C[1];
            const FVector* Anchor = FinalAnchors.Find(E.EntryGuid);
            const bool bRowMoved = Anchor && !NsFinal::Within(E.Location, *Anchor);
            const bool bActorMoved = Actor && !bActorBelow;
            const bool bPredicateNow = bRowBits && (bActorBelow || bSticky);
            if (!Anchor) { ++C[7]; bNext = false; }
            else if (bRowMoved || bActorMoved)
            {
                ++C[bRowMoved ? 3 : 4];
                bNext = false;
                if (MovedLines < NsFinal::MovedLineCap)
                {
                    ++MovedLines;
                    UE_LOG(LogNodeShuffle, Display, TEXT("FINALMOVED: stage=%s applyPass=%d guid=%s res=%s anchor=%s now=%s actorAt=%s rowDeltaCm=%.0f actorDeltaCm=%.0f predicateNow=%d pinned=%d rayCasted=%d provisional=%d active=%d"),
                        StageName, FinalApplyPass, *E.EntryGuid.ToString(), *NsFinal::ShortRes(E.AssignedResourceClassPath), *NsFinal::Vec(*Anchor),
                        *NsFinal::Vec(E.Location), Actor ? *NsFinal::Vec(Actor->GetActorLocation()) : TEXT("none"), FVector::Dist(E.Location, *Anchor),
                        Actor ? FVector::Dist(Actor->GetActorLocation(), E.Location) : -1.0f, bPredicateNow ? 1 : 0, E.bPinned ? 1 : 0,
                        E.bRayCasted ? 1 : 0, E.bRwgProvisional ? 1 : 0, E.bActive ? 1 : 0);
                }
                else { ++C[14]; }
            }
            else if (!Actor && !bSticky) { ++C[5]; bNext = false; }
            else if (bEnd && !bPredicateNow) { ++C[6]; bNext = false; }
            else { ++C[2]; if (bSticky) { ++C[15]; } }
        }
        else if (bActorBelow) // bEnd && bRowBits hold here (the continue above); setting needs a resolved actor < 2 m
        {
            const int32* RevokedAt = FinalRevokedAt.Find(E.EntryGuid);
            if (RevokedAt && Recon - *RevokedAt < NsFinal::DwellReconciles) { ++C[9]; }
            else { bNext = true; ++C[8]; }
        }
        if (bNext != E.bPlacementFinal)
        {
            if (bNext) { FinalAnchors.Add(E.EntryGuid, E.Location); FinalRevokedAt.Remove(E.EntryGuid); }
            else { FinalAnchors.Remove(E.EntryGuid); FinalRevokedAt.Add(E.EntryGuid, Recon); }
            E.bPlacementFinal = bNext;
        }
        if (bNext) { ++C[10]; ++C[!E.bIsNewNode ? 13 : (E.bPinned ? 11 : 12)]; }
    }
    if (bEnd) { ++PlacementFinalEpoch; }

    const int32 Slot = bEnd ? 1 : 0;
    if (FinalLastCensus.Num() != 2 * NsFinal::CensusFields) { FinalLastCensus.Init(-1, 2 * NsFinal::CensusFields); }
    bool bChanged = false;
    for (int32 I = 0; I < NsFinal::CensusFields; ++I)
    {
        int32& Last = FinalLastCensus[Slot * NsFinal::CensusFields + I];
        if (Last != C[I]) { bChanged = true; }
        Last = C[I];
    }
    if (bEnd) { bFinalLoadPending = false; }
    if (!bChanged && MovedLines == 0 && C[14] == 0 && !bLoadStage) { return; }
    // checked = rows flagged at stage start (the tripwire's denominator); rows = entries in Layout. Measured counts and predicate bits only.
    UE_LOG(LogNodeShuffle, Display, TEXT("FINALCENSUS: stage=%s epoch=%d applyPass=%d mode=%s rows=%d checked=%d kept=%d revokedRowMoved=%d revokedActorMoved=%d revokedActorLost=%d revokedPredicate=%d noAnchor=%d newlyFinal=%d dwellHeld=%d finalNow=%d armPinned=%d armNewRow=%d armOriginal=%d linesSuppressed=%d stickyUnresolved=%d"),
        StageName, PlacementFinalEpoch, FinalApplyPass, bVanilla ? TEXT("vanilla") : TEXT("rwg"), C[0], C[1], C[2], C[3], C[4], C[5], C[6], C[7],
        C[8], C[9], C[10], C[11], C[12], C[13], C[14], C[15]);
}
