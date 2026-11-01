// rwg-rerolldup-2 (2026-11-01): see NodeShuffleRerollIdentity.h. Every line here prints what was MEASURED (which leg matched,
// the distance, which row kept the original); none asserts why a name went stale.
#include "NodeShuffleRerollIdentity.h"
#include "NodeShuffleRepClass.h"
#include "NodeShuffle.h"
#include "Misc/PackageName.h"

namespace NodeShuffleRerollIdentity
{
    FVector OriginalAnchor(const FNodeShuffleEntry& E)
    {
        if (!E.OriginalTrueLocation.IsNearlyZero()) { return E.OriginalTrueLocation; }
        if (!E.bIsNewNode) { return E.Location; } // a raw original still sits where it was captured
        return FVector::ZeroVector;              // a relocated entry's Location is its destination
    }

    bool SameOriginal(const FNodeShuffleEntry& A, const FNodeShuffleEntry& B, float RadiusCm, bool& bOutByPath, float& OutDistCm)
    {
        bOutByPath = false;
        OutDistCm = -1.0f;
        if (!A.VanillaNodePath.IsEmpty() && A.VanillaNodePath == B.VanillaNodePath) { const FVector PA = OriginalAnchor(A), PB = OriginalAnchor(B); if (!PA.IsNearlyZero() && !PB.IsNearlyZero() && FVector::DistSquared(PA, PB) >= FMath::Square(RadiusCm)) { return false; } bOutByPath = true; OutDistCm = 0.0f; return true; } // one name at two anchored spots is a swapped name, not one original
        if (A.OriginalResourceClassPath.IsEmpty() || A.OriginalResourceClassPath != B.OriginalResourceClassPath) { return false; }
        const FVector AnchorA = OriginalAnchor(A);
        const FVector AnchorB = OriginalAnchor(B);
        if (AnchorA.IsNearlyZero() || AnchorB.IsNearlyZero()) { return false; } // an unanchored row never matches by distance
        const float Dist = FVector::Dist(AnchorA, AnchorB);
        if (Dist >= RadiusCm) { return false; }
        OutDistCm = Dist;
        return true;
    }

    namespace
    {
        int32 FindRoot(TArray<int32>& Parent, int32 I)
        {
            while (Parent[I] != I) { Parent[I] = Parent[Parent[I]]; I = Parent[I]; }
            return I;
        }
        void Unite(TArray<int32>& Parent, int32 A, int32 B)
        {
            A = FindRoot(Parent, A);
            B = FindRoot(Parent, B);
            if (A != B) { Parent[FMath::Max(A, B)] = FMath::Min(A, B); }
        }
        // keeper order: carried, pinned raw original, pinned, anchored, earliest
        int32 KeeperRank(const FNodeShuffleEntry& E, bool bCarried)
        {
            if (bCarried) { return 4; }
            if (!E.bIsNewNode && E.bPinned) { return 3; }
            if (E.bPinned) { return 2; }
            if (!E.OriginalTrueLocation.IsNearlyZero()) { return 1; }
            return 0;
        }
        FString ShortName(const FString& Path) { return FPackageName::ObjectPathToObjectName(Path); }
    }

    void DedupeOriginals(TArray<FNodeShuffleEntry>& Layout, TArray<FNodeShuffleEntry>& Carried, float RadiusCm,
        TFunctionRef<bool(const FString& Path)> PathResolvesLive, FResult& Out)
    {
        TSet<FGuid> CarriedGuids;
        TMap<FGuid, int32> CarriedIndex;
        for (int32 i = 0; i < Carried.Num(); ++i) { CarriedGuids.Add(Carried[i].EntryGuid); CarriedIndex.Add(Carried[i].EntryGuid, i); }

        // Candidates: every row carrying an original (the rebuild's own filter).
        TArray<int32> Cand;
        for (int32 i = 0; i < Layout.Num(); ++i) { if (!Layout[i].OriginalResourceClassPath.IsEmpty()) { Cand.Add(i); } }
        Out.Candidates = Cand.Num();
        TArray<int32> Parent;
        Parent.SetNum(Cand.Num());
        for (int32 c = 0; c < Cand.Num(); ++c) { Parent[c] = c; }

        // Path leg: rows sharing a name are one original. Anchor leg: per resource, rows whose anchors sit within the radius.
        TMap<FString, TArray<int32>> ByPath;
        TMap<FString, TArray<int32>> ByResource;
        for (int32 c = 0; c < Cand.Num(); ++c)
        {
            const FNodeShuffleEntry& E = Layout[Cand[c]];
            if (!E.VanillaNodePath.IsEmpty()) { ByPath.FindOrAdd(E.VanillaNodePath).Add(c); }
            if (OriginalAnchor(E).IsNearlyZero()) { ++Out.CandidatesWithoutAnchor; }
            else { ByResource.FindOrAdd(E.OriginalResourceClassPath).Add(c); }
        }
        const float RadiusSq = FMath::Square(RadiusCm);
        // review F2: one name at two anchored spots >= the radius apart is a swapped name: never united DIRECTLY (a zero-anchor third row can still join them).
        for (const TPair<FString, TArray<int32>>& KV : ByPath) { const TArray<int32>& Same = KV.Value; for (int32 x = 0; x < Same.Num(); ++x) { const FVector AX = OriginalAnchor(Layout[Cand[Same[x]]]); for (int32 y = x + 1; y < Same.Num(); ++y) { const FVector AY = OriginalAnchor(Layout[Cand[Same[y]]]); if (!AX.IsNearlyZero() && !AY.IsNearlyZero() && FVector::DistSquared(AX, AY) >= RadiusSq) { ++Out.PathPairsApart; continue; } Unite(Parent, Same[x], Same[y]); } } }
        for (const TPair<FString, TArray<int32>>& KV : ByResource)
        {
            const TArray<int32>& Bucket = KV.Value;
            for (int32 x = 0; x < Bucket.Num(); ++x)
            {
                const FVector AnchorX = OriginalAnchor(Layout[Cand[Bucket[x]]]);
                for (int32 y = x + 1; y < Bucket.Num(); ++y)
                {
                    // review F3: two different names that BOTH resolve live are two originals: never united DIRECTLY (a third row in the radius can still join them; residual).
                    const FNodeShuffleEntry& EX = Layout[Cand[Bucket[x]]]; const FNodeShuffleEntry& EY = Layout[Cand[Bucket[y]]]; if (FVector::DistSquared(AnchorX, OriginalAnchor(EY)) >= RadiusSq) { continue; } if (!EX.VanillaNodePath.IsEmpty() && !EY.VanillaNodePath.IsEmpty() && EX.VanillaNodePath != EY.VanillaNodePath && PathResolvesLive(EX.VanillaNodePath) && PathResolvesLive(EY.VanillaNodePath)) { ++Out.AnchorPairsBothLive; continue; } Unite(Parent, Bucket[x], Bucket[y]);
                }
            }
        }

        TMap<int32, TArray<int32>> Groups;
        for (int32 c = 0; c < Cand.Num(); ++c) { Groups.FindOrAdd(FindRoot(Parent, c)).Add(c); }
        for (TPair<int32, TArray<int32>>& G : Groups)
        {
            const TArray<int32>& Members = G.Value;
            if (Members.Num() < 2) { continue; }
            ++Out.Groups;

            int32 K = Members[0];
            int32 KRank = KeeperRank(Layout[Cand[K]], CarriedGuids.Contains(Layout[Cand[K]].EntryGuid));
            for (int32 m = 1; m < Members.Num(); ++m)
            {
                const int32 R = KeeperRank(Layout[Cand[Members[m]]], CarriedGuids.Contains(Layout[Cand[Members[m]]].EntryGuid));
                if (R > KRank) { K = Members[m]; KRank = R; }
            }
            FNodeShuffleEntry& Keeper = Layout[Cand[K]];
            const bool bKeeperCarried = CarriedGuids.Contains(Keeper.EntryGuid);

            const FNodeShuffleEntry KeeperBefore = Keeper; // legs graded on the pre-pass names (a transfer can make an anchor twin path-equal)
            // A keeper whose name is dead takes a member's live name, so one record resolves by path at the next load and the
            // live-scan augments see the original's current name in the dedupe set.
            const bool bKeeperPathLive = !Keeper.VanillaNodePath.IsEmpty() && PathResolvesLive(Keeper.VanillaNodePath);
            if (!bKeeperPathLive)
            {
                for (const int32 m : Members)
                {
                    if (m == K) { continue; }
                    const FString& P = Layout[Cand[m]].VanillaNodePath;
                    if (P.IsEmpty() || P == Keeper.VanillaNodePath || !PathResolvesLive(P)) { continue; }
                    Keeper.VanillaNodePath = P;
                    ++Out.PathTransfers;
                    break;
                }
            }
            if (bKeeperCarried)
            {
                if (const int32* CI = CarriedIndex.Find(Keeper.EntryGuid)) { Carried[*CI].VanillaNodePath = Keeper.VanillaNodePath; }
            }

            for (const int32 m : Members)
            {
                if (m == K) { continue; }
                FNodeShuffleEntry& E = Layout[Cand[m]];
                bool bByPath = false;
                float DistCm = -1.0f;
                SameOriginal(KeeperBefore, E, RadiusCm, bByPath, DistCm); // a member joined through a third row leaves both unset
                if (CarriedGuids.Contains(E.EntryGuid))
                {
                    // Two mined copies of one original both stay under their miners; sharing one name makes the path-keyed
                    // composition, record and carried-path sets count that original once. The keeper is carried too (rank).
                    if (E.VanillaNodePath != Keeper.VanillaNodePath)
                    {
                        E.VanillaNodePath = Keeper.VanillaNodePath;
                        if (const int32* CI = CarriedIndex.Find(E.EntryGuid)) { Carried[*CI].VanillaNodePath = Keeper.VanillaNodePath; }
                        ++Out.CarriedTwinsUnified;
                    }
                    continue;
                }
                if (bKeeperCarried && !E.VanillaNodePath.IsEmpty() && E.VanillaNodePath == Keeper.VanillaNodePath)
                {
                    continue; // the rebuild's carried-path drop handles it (REROLLPOOL-PATHDUPE)
                }
                FDrop D;
                D.KeeperGuid = Keeper.EntryGuid;
                D.KeeperPath = Keeper.VanillaNodePath;
                D.KeeperAnchor = OriginalAnchor(Keeper);
                D.bByPath = bByPath;
                D.DistCm = DistCm;
                Out.Drops.Add(E.EntryGuid, D);
                if (!E.VanillaNodePath.IsEmpty()) { Out.DroppedPaths.Add(E.VanillaNodePath); }
                if (bByPath) { ++Out.PathDrops; } else if (DistCm >= 0.0f) { ++Out.AnchorDrops; } else { ++Out.ThirdRowDrops; }
            }
        }
    }

    int32 ReportPathsWithoutPriorRecord(const TArray<FNodeShuffleEntry>& Pool, const TArray<FNodeShuffleEntry>& Carried,
        const TSet<FString>& PriorRecordPaths, int32 LineCap)
    {
        int32 Count = 0;
        TSet<FString> Seen;
        const auto Visit = [&](const FNodeShuffleEntry& E, bool bCarried)
        {
            if (E.VanillaNodePath.IsEmpty() || E.OriginalResourceClassPath.IsEmpty()) { return; }
            bool bSeen = false;
            Seen.Add(E.VanillaNodePath, &bSeen);
            if (bSeen || PriorRecordPaths.Contains(E.VanillaNodePath)) { return; }
            ++Count;
            if (Count <= LineCap)
            {
                UE_LOG(LogNodeShuffle, Display, TEXT("REROLLPOOL-NORECORD path='%s' res='%s' guid=%s trueLoc=%s carried=%d pinnedNow=%d"),
                    *E.VanillaNodePath, *ShortName(E.OriginalResourceClassPath), *E.EntryGuid.ToString(),
                    *E.OriginalTrueLocation.ToCompactString(), bCarried ? 1 : 0, E.bPinned ? 1 : 0);
            }
        };
        for (const FNodeShuffleEntry& E : Pool) { if (!E.bIsNewNode) { Visit(E, false); } }
        for (const FNodeShuffleEntry& C : Carried) { Visit(C, true); }
        return Count;
    }

    bool RestampVanillaDealOffForeignClass(FNodeShuffleEntry& E, const FString& SolidRepresentative,
        const FString& LiquidRepresentative, uint8 FormLiquidByte, FVanillaRestamp& Census)
    {
        if (E.AssignedResourceClassPath.IsEmpty() || !NodeShuffleRepClass::IsGenericNodeClassPath(E.AssignedResourceClassPath)) { return false; }
        // The class the entry would spawn as before any session substitution: restamp, else saved representative, else the slot's.
        const FString& Slot = !E.RepClassSpawnPath.IsEmpty() ? E.RepClassSpawnPath
            : (E.SpawnNodeClassPath.IsEmpty() ? E.NodeClassPath : E.SpawnNodeClassPath);
        if (Slot.IsEmpty() || NodeShuffleRepClass::IsGenericNodeClassPath(Slot)) { return false; }
        const FString& Representative = (E.ResourceForm == FormLiquidByte) ? LiquidRepresentative : SolidRepresentative;
        if (Representative.IsEmpty() || !NodeShuffleRepClass::IsGenericNodeClassPath(Representative)) { ++Census.RepNotGeneric; return false; }
        TPair<FString, int32>& Row = Census.BySlotClass.FindOrAdd(Slot, TPair<FString, int32>(Representative, 0));
        Row.Value++;
        E.RepClassSpawnPath = Representative;
        ++Census.Restamped;
        return true;
    }

    void EmitVanillaRestampCensus(const TCHAR* Role, const FVanillaRestamp& Census)
    {
        TArray<FString> Slots;
        Census.BySlotClass.GetKeys(Slots);
        Slots.Sort();
        for (const FString& Slot : Slots)
        {
            const TPair<FString, int32>& Row = Census.BySlotClass.FindChecked(Slot);
            UE_LOG(LogNodeShuffle, Display, TEXT("REPCLASS-VANRESTAMP role=%s slotClass=%s to=%s restamped=%d"),
                Role, *ShortName(Slot), *ShortName(Row.Key), Row.Value);
        }
        UE_LOG(LogNodeShuffle, Display, TEXT("REPCLASS-VANRESTAMP-END role=%s restamped=%d repNotGeneric=%d slotClasses=%d"),
            Role, Census.Restamped, Census.RepNotGeneric, Slots.Num());
    }

    int32 ReportIdentityTwins(const TArray<FNodeShuffleEntry>& Layout, float RadiusCm, int32 DumpId, int32 LineCap)
    {
        TMap<FString, TArray<int32>> ByResource;
        for (int32 i = 0; i < Layout.Num(); ++i)
        {
            const FNodeShuffleEntry& E = Layout[i];
            if (!E.bActive || E.VanillaNodePath.IsEmpty() || E.OriginalResourceClassPath.IsEmpty()) { continue; }
            if (OriginalAnchor(E).IsNearlyZero()) { continue; }
            ByResource.FindOrAdd(E.OriginalResourceClassPath).Add(i);
        }
        const float RadiusSq = FMath::Square(RadiusCm);
        TSet<FString> Paths;
        int32 Pairs = 0;
        for (const TPair<FString, TArray<int32>>& KV : ByResource)
        {
            const TArray<int32>& Bucket = KV.Value;
            for (int32 x = 0; x < Bucket.Num(); ++x)
            {
                const FNodeShuffleEntry& A = Layout[Bucket[x]];
                const FVector AnchorA = OriginalAnchor(A);
                for (int32 y = x + 1; y < Bucket.Num(); ++y)
                {
                    const FNodeShuffleEntry& B = Layout[Bucket[y]];
                    if (A.VanillaNodePath == B.VanillaNodePath) { continue; } // the path-keyed TWINS census already counts these
                    const float DSq = FVector::DistSquared(AnchorA, OriginalAnchor(B));
                    if (DSq >= RadiusSq) { continue; }
                    Paths.Add(A.VanillaNodePath);
                    Paths.Add(B.VanillaNodePath);
                    if (++Pairs <= LineCap)
                    {
                        UE_LOG(LogNodeShuffle, Display, TEXT("LAYOUTDUMP-IDTWIN dumpId=%d res=%s distCm=%.0f guidA=%s pathA=%s pinnedA=%d guidB=%s pathB=%s pinnedB=%d"),
                            DumpId, *ShortName(KV.Key), FMath::Sqrt(DSq), *A.EntryGuid.ToString(), *ShortName(A.VanillaNodePath), A.bPinned ? 1 : 0,
                            *B.EntryGuid.ToString(), *ShortName(B.VanillaNodePath), B.bPinned ? 1 : 0);
                    }
                }
            }
        }
        return Paths.Num();
    }
}
