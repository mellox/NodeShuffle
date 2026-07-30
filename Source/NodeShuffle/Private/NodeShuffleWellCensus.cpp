// Packet H0 (ns-wells): the PURE half of the resource-well census -- grouping, reconciliation, per-well geometry,
// footprint algebra, angular gaps, and cross-well clearance. NodeShuffleWellDump.cpp owns the console command and ALL
// logging; see NodeShuffleWellCensus.h for why the split exists and NodeShuffleWellDump.cpp's header for the design
// rationale behind each measurement.
//
// IMPORT DISCIPLINE (memory: sf-shipping-export-trap). MEASURED with dumpbin on the built DLL: this packet adds four
// symbols -- the two TActorIterator StaticClass paths (Z_Construct_UClass_AFGResourceNodeFrackingCore/
// Satellite_NoRegister), FGenericPlatformMath::Atan2, and AFGResourceNodeFrackingSatellite::GetCore.
//
// GetCore has an INLINE BODY in the header and is STILL imported: FACTORYGAME_API makes the class
// __declspec(dllimport), and MSVC may call the imported symbol for such a class's inline member instead of inlining
// it. So never grade an inline accessor "zero import surface" from the header -- dump the artifact. (Same shape as the
// 2026-07-26 "virtual => vtable-dispatched => no import surface" falsification.)
//
// The converse claim is just as unsafe, so it is NOT made here (ns-review-h0-2 W11): GetActivator, GetExtractor,
// GetState and Native_GetSatellites are absent from the import table, but ABSENCE ONLY PROVES THE MODULE DOES NOT
// IMPORT THEM -- it does not prove "MSVC inlined them", and a DLL-level dump cannot distinguish the two. Other
// inline-bodied accessors this packet calls (GetResourcePurity, GetResourceClassOriginal, GetResourceClassOverride)
// ARE in the import table, from pre-existing NodeShuffleSubsystem.cpp call sites -- which is exactly why the
// inline-implies-no-import inference must never be written down as measured.

#include "NodeShuffleWellCensus.h"
#include "NodeShuffle.h"                 // FNodeShuffleModule::IsManagedSpawnedNode (W5)
#include "NodeShuffleNodeComponent.h"    // UNodeShuffleNodeComponent::Find           (W5)
#include "EngineUtils.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceDeposit.h" // AFGResourceDeposit -- excluded, exactly as OverlapsAt excludes it

namespace
{
    bool IsFiniteVec(const FVector& V)
    {
        return !V.ContainsNaN() && FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
    }
}

const TCHAR* NodeShuffleWellPurityName(EResourcePurity Purity)
{
    switch (Purity)
    {
    case RP_Inpure: return TEXT("Impure");
    case RP_Normal: return TEXT("Normal");
    case RP_Pure:   return TEXT("Pure");
    case RP_MAX:    return TEXT("<RP_MAX-sentinel>"); // mPurity itself left at the hidden max value
    default:        return TEXT("<out-of-range>");
    }
}

const TCHAR* NodeShuffleWellStateName(EFrackingSatelliteState State)
{
    switch (State)
    {
    case EFrackingSatelliteState::FSS_Untouched: return TEXT("Untouched");
    case EFrackingSatelliteState::FSS_Active:    return TEXT("Active");
    case EFrackingSatelliteState::FSS_Inactive:  return TEXT("Inactive");
    default:                                     return TEXT("<out-of-range>");
    }
}

void CollectWellCensus(UWorld* World, FNodeShuffleWellCensus& Out)
{
    if (!World) { return; }

    TArray<AFGResourceNodeFrackingCore*> Cores;
    for (TActorIterator<AFGResourceNodeFrackingCore> It(World); It; ++It)
    {
        if (IsValid(*It)) { Cores.Add(*It); } else { ++Out.InvalidCoresSkipped; }
    }
    Out.IteratorCores = Cores.Num();

    // Records live in an ORDERED TArray addressed by index, never by a held reference: EnsureWell can grow that array
    // and invalidate any outstanding pointer. Every access below re-reads through the index.
    TMap<AFGResourceNodeFrackingCore*, int32> WellIndexByCore;
    auto EnsureWell = [&](AFGResourceNodeFrackingCore* C, bool bAdopted) -> int32
    {
        if (int32* Found = WellIndexByCore.Find(C)) { return *Found; }
        FNodeShuffleWellRecord R;
        R.Core = C;
        R.bAdoptedViaSatellite = bAdopted;
        R.CoreLocation = C->GetActorLocation();          // W2: read ONCE, here
        R.bCoreLocationFinite = IsFiniteVec(R.CoreLocation);
        if (!R.bCoreLocationFinite)
        {
            ++Out.NonFiniteCores;
            Out.NonFiniteNotes.Add(FString::Printf(TEXT("core='%s' has a NON-FINITE location"), *C->GetName()));
        }
        const int32 Idx = Out.Wells.Add(R);
        WellIndexByCore.Add(C, Idx);
        return Idx;
    };

    // F3: view A as a LAMBDA so the ADOPTED-core path can run the very same body. Before this, view A ran only over
    // the cores the actor iterator returned; a core reached later through a satellite's mCore never had its
    // mSatellites read, so EVERY one of its satellites landed in InBOnly and was reported as "registration did not
    // happen -- the shape design R1 says kills a well silently". That is a FALSE ALARM on the most important line this
    // command prints, and it blinded the duplicate detector for those cores. Reachable because mCore is a STRONG
    // TObjectPtr: a core in an unloaded sublevel stays a live UObject the iterator no longer returns.
    auto AddViewAFor = [&](AFGResourceNodeFrackingCore* C)
    {
        // W12: Find + return, never FindChecked. This command's selling point is that it is always safe to run, and in
        // Shipping check() compiles out, so FindChecked would degrade to a null deref rather than a clean assert.
        const int32* IdxPtr = WellIndexByCore.Find(C);
        if (!IdxPtr) { return; } // structurally unreachable today; kept so it stays unreachable by construction
        TArray<FNodeShuffleWellMember>& Members = Out.Wells[*IdxPtr].Members;
        for (const TWeakObjectPtr<AFGResourceNodeFrackingSatellite>& Weak : C->Native_GetSatellites())
        {
            AFGResourceNodeFrackingSatellite* Sat = Weak.Get();
            if (!IsValid(Sat))
            {
                ++Out.StaleWeakInCoreArrays;
                Out.StaleWeakNotes.Add(FString::Printf(TEXT("core='%s'"), *C->GetName()));
                continue;
            }
            if (FNodeShuffleWellMember* Existing = Members.FindByPredicate([Sat](const FNodeShuffleWellMember& M) { return M.Actor == Sat; }))
            {
                ++Existing->ViewARegistrations;
                ++Out.DuplicateRegistrations;
                Out.DuplicateNotes.Add(FString::Printf(TEXT("sat='%s' core='%s' occurrences=%d"),
                    *Sat->GetName(), *C->GetName(), Existing->ViewARegistrations));
                continue;
            }
            FNodeShuffleWellMember M;
            M.Actor = Sat; M.bInViewA = true; M.ViewARegistrations = 1;
            M.Location = Sat->GetActorLocation();        // W2: read ONCE, here
            M.bLocationFinite = IsFiniteVec(M.Location);
            if (!M.bLocationFinite)
            {
                ++Out.NonFiniteSatellites;
                Out.NonFiniteNotes.Add(FString::Printf(TEXT("sat='%s' (core='%s') has a NON-FINITE location"), *Sat->GetName(), *C->GetName()));
            }
            Members.Add(M);
        }
    };

    for (AFGResourceNodeFrackingCore* C : Cores) { EnsureWell(C, false); AddViewAFor(C); }

    for (TActorIterator<AFGResourceNodeFrackingSatellite> It(World); It; ++It)
    {
        AFGResourceNodeFrackingSatellite* Sat = *It;
        if (!IsValid(Sat)) { ++Out.InvalidSatsSkipped; continue; }
        ++Out.SatelliteActorsValid;
        AFGResourceNodeFrackingCore* Core = Sat->GetCore().Get();
        if (!IsValid(Core))
        {
            // LOW-5: orphans used to be continue'd before any location read, so a non-finite orphan reached neither
            // NonFiniteSatellites nor NonFiniteNotes -- yet the dump printed its location as a number and the
            // clearance sweep silently ticked NonFiniteNodesExcluded with no matching note. Validated and counted
            // here instead. NOTE the knock-on: this feeds bNonFinite, so a non-finite ORPHAN now also forces the
            // three geometric verdicts to COULD-NOT-RUN. That is the safe direction and is deliberate.
            ++Out.NullCoreSatellites;
            Out.OrphanSatellites.Add(Sat);
            if (!IsFiniteVec(Sat->GetActorLocation()))
            {
                ++Out.NonFiniteSatellites;
                Out.NonFiniteNotes.Add(FString::Printf(TEXT("ORPHAN sat='%s' (GetCore() null) has a NON-FINITE location"), *Sat->GetName()));
            }
            continue;
        }

        int32 Idx;
        if (const int32* Found = WellIndexByCore.Find(Core)) { Idx = *Found; }
        else
        {
            ++Out.AdoptedCores;
            Out.AdoptionNotes.Add(FString::Printf(TEXT("sat='%s' core='%s'"), *Sat->GetName(), *Core->GetName()));
            Idx = EnsureWell(Core, true);
            AddViewAFor(Core); // adopted core now gets BOTH views (F3)
        }
        TArray<FNodeShuffleWellMember>& Members = Out.Wells[Idx].Members;
        if (FNodeShuffleWellMember* Existing = Members.FindByPredicate([Sat](const FNodeShuffleWellMember& M) { return M.Actor == Sat; }))
        {
            Existing->bInViewB = true;
        }
        else
        {
            FNodeShuffleWellMember M;
            M.Actor = Sat; M.bInViewB = true;
            M.Location = Sat->GetActorLocation();
            M.bLocationFinite = IsFiniteVec(M.Location);
            if (!M.bLocationFinite)
            {
                ++Out.NonFiniteSatellites;
                Out.NonFiniteNotes.Add(FString::Printf(TEXT("sat='%s' (core='%s') has a NON-FINITE location"), *Sat->GetName(), *Core->GetName()));
            }
            Members.Add(M);
        }
    }

    // One-sided members, computed once BOTH views are complete -- never at insertion time, or an adopted core's
    // members would be classified before its view-A pass had run.
    for (const FNodeShuffleWellRecord& R : Out.Wells)
    {
        for (const FNodeShuffleWellMember& M : R.Members)
        {
            const FString Pair = FString::Printf(TEXT("sat='%s' core='%s'"), *M.Actor->GetName(), *R.Core->GetName());
            if (M.bInViewA && !M.bInViewB) { Out.InAOnly.Add(Pair); }
            else if (M.bInViewB && !M.bInViewA) { Out.InBOnly.Add(Pair); }
        }
    }
}

void ComputeWellGeometry(const FNodeShuffleWellRecord& W, FNodeShuffleWellGeometry& Out)
{
    Out.bCoreFinite = W.bCoreLocationFinite;
    if (!Out.bCoreFinite) { return; } // no origin => no offsets, no distances; the caller reports it refused

    TArray<const FNodeShuffleWellMember*> Usable;
    for (const FNodeShuffleWellMember& M : W.Members)
    {
        if (M.bLocationFinite) { Usable.Add(&M); } else { ++Out.MembersExcludedNonFinite; }
    }
    Out.MembersUsed = Usable.Num();

    for (const FNodeShuffleWellMember* M : Usable)
    {
        Out.Offsets.Add(M->Location - W.CoreLocation);
        const double D = FVector::Dist(M->Location, W.CoreLocation);
        if (!Out.bHaveCoreSat || D < Out.MinCoreSat3D)
        {
            Out.bHaveCoreSat = true;
            Out.MinCoreSat3D = D;
            Out.MinCoreSatWho = FString::Printf(TEXT("core='%s' sat='%s'"), *W.Core->GetName(), *M->Actor->GetName());
        }
        if (D < 800.0) { ++Out.CoreSatUnder800; }
    }

    for (int32 a = 0; a < Usable.Num(); ++a)
    {
        for (int32 b = a + 1; b < Usable.Num(); ++b)
        {
            ++Out.PairsCompared;
            const double D3 = FVector::Dist(Usable[a]->Location, Usable[b]->Location);
            const double DXY = FVector::Dist2D(Usable[a]->Location, Usable[b]->Location);
            if (D3 < 800.0) { ++Out.PairsUnder800; }
            const FString Who = FString::Printf(TEXT("'%s'<->'%s'"), *Usable[a]->Actor->GetName(), *Usable[b]->Actor->GetName());
            if (!Out.bHavePair || D3 < Out.MinSatSat3D) { Out.MinSatSat3D = D3; Out.MinPair3DWho = Who; }
            if (!Out.bHavePair || DXY < Out.MinSatSatXY) { Out.MinSatSatXY = DXY; Out.MinPairXYWho = Who; }
            Out.bHavePair = true;
        }
    }
}

void ComputeWellFootprint(const TArray<FVector>& Offsets, FNodeShuffleWellFootprint& Out)
{
    Out.N = Offsets.Num();
    if (Out.N == 0)
    {
        Out.AspectStr = TEXT("<no-satellites>");
        Out.AxisDegStr = TEXT("<no-satellites>");
        return; // bValid stays false -- W3: the caller must print sentinels, not eight confident zeros
    }
    // W2/F5: reject non-finite input AT THE SOURCE. Every sentinel test below is a `<=` comparison, and a comparison
    // against NaN is false, so one NaN offset would fall through every guard to the %.4f print.
    for (const FVector& O : Offsets)
    {
        if (!IsFiniteVec(O)) { ++Out.NonFiniteOffsets; }
    }
    if (Out.NonFiniteOffsets > 0)
    {
        Out.AspectStr = TEXT("<non-finite-check-satellite-locations>");
        Out.AxisDegStr = TEXT("<non-finite-check-satellite-locations>");
        return; // every numeric field stays 0 AND bValid stays false, so nothing prints as a measurement
    }

    double SumDistXY = 0.0, MinZ = 0.0, MaxZ = 0.0;
    Out.MinDistXY = TNumericLimits<double>::Max();
    for (int32 i = 0; i < Out.N; ++i)
    {
        const double dx = Offsets[i].X, dy = Offsets[i].Y, dz = Offsets[i].Z;
        Out.Sxx += dx * dx; Out.Syy += dy * dy; Out.Sxy += dx * dy;
        const double DistXY = FMath::Sqrt(dx * dx + dy * dy);
        SumDistXY += DistXY;
        Out.BoundRadiusXY = FMath::Max(Out.BoundRadiusXY, DistXY);
        Out.MinDistXY = FMath::Min(Out.MinDistXY, DistXY);
        MinZ = (i == 0) ? dz : FMath::Min(MinZ, dz);
        MaxZ = (i == 0) ? dz : FMath::Max(MaxZ, dz);
    }
    const double InvN = 1.0 / static_cast<double>(Out.N);
    Out.Sxx *= InvN; Out.Syy *= InvN; Out.Sxy *= InvN;
    Out.MeanDistXY = SumDistXY * InvN;
    Out.ZSpread = MaxZ - MinZ;

    // Eigenvalues of the symmetric 2x2 [[Sxx,Sxy],[Sxy,Syy]]. Diff^2 + 4*Sxy^2 is a sum of squares, so >= 0 in exact
    // arithmetic; clamped anyway, because a hair-negative FP result would make FMath::Sqrt a NaN.
    const double Trace = Out.Sxx + Out.Syy, Diff = Out.Sxx - Out.Syy;
    const double Root = FMath::Sqrt(FMath::Max(Diff * Diff + 4.0 * Out.Sxy * Out.Sxy, 0.0));
    Out.LambdaMax = 0.5 * (Trace + Root);
    // Positive semi-definite, so LambdaMin >= 0 exactly; clamp for the same reason.
    Out.LambdaMin = FMath::Max(0.5 * (Trace - Root), 0.0);

    // AbsFloor is in cm^2: 1e-6 cm^2 is a scatter of ~0.01 mm, far below anything a level can author, so at-or-under
    // it is genuinely "no extent" rather than a small well. RelFloor caps a reportable aspect at 1000:1 -- past that
    // the value is dominated by FP noise, not by geometry.
    constexpr double AbsFloor = 1.0e-6, RelFloor = 1.0e-6;
    if (Out.N == 1) { Out.AspectStr = TEXT("<single-satellite-no-second-axis>"); } // one point defines one axis
    else if (Out.LambdaMax <= AbsFloor) { Out.AspectStr = TEXT("<all-satellites-coincident-with-core-XY>"); }
    else if (Out.LambdaMin <= RelFloor * Out.LambdaMax) { Out.AspectStr = TEXT("<collinear-aspect-over-1000>"); }
    else
    {
        const double Aspect = FMath::Sqrt(Out.LambdaMax / Out.LambdaMin);
        Out.AspectStr = FMath::IsFinite(Aspect) ? FString::Printf(TEXT("%.4f"), Aspect) : FString(TEXT("<non-finite-aspect>"));
    }

    // atan2(0,0) is 0 by convention, which would print a confident "0 deg" for a perfectly isotropic cloud that HAS no
    // principal axis -- named instead.
    if (FMath::Abs(Diff) <= AbsFloor && FMath::Abs(Out.Sxy) <= AbsFloor)
    {
        Out.AxisDegStr = (Out.LambdaMax <= AbsFloor) ? FString(TEXT("<no-extent>")) : FString(TEXT("<isotropic-no-principal-axis>"));
    }
    else
    {
        const double AxisDeg = FMath::RadiansToDegrees(0.5 * FMath::Atan2(2.0 * Out.Sxy, Diff));
        Out.AxisDegStr = FMath::IsFinite(AxisDeg) ? FString::Printf(TEXT("%.2f"), AxisDeg) : FString(TEXT("<non-finite-axis>"));
    }
    Out.bValid = true;
}

void ComputeWellAngularGaps(const TArray<FVector>& Offsets, FNodeShuffleWellGaps& Out)
{
    const int32 N = Offsets.Num();
    if (N == 0) { Out.Sentinel = TEXT("<no-satellites>"); return; }
    // Gaps are a function of X and Y ONLY -- a Z-only non-finite offset leaves them perfectly well defined, which is
    // why this test is narrower than the footprint's and why the caller must not claim gaps were refused (W8).
    for (const FVector& O : Offsets)
    {
        if (!FMath::IsFinite(O.X) || !FMath::IsFinite(O.Y))
        {
            Out.Sentinel = TEXT("<non-finite-XY-check-satellite-locations>");
            return;
        }
    }
    if (N == 1) { Out.Sentinel = TEXT("<single-satellite-gap-360>"); return; }

    TArray<double> Bearings;
    Bearings.Reserve(N);
    for (const FVector& O : Offsets)
    {
        // A satellite sitting exactly on the core's XY has no defined bearing; atan2(0,0) returns 0, which would fake
        // a real direction. Counted so the caller can warn that this many bearings are fabricated (W7).
        if (FMath::Abs(O.X) <= 1.0e-3 && FMath::Abs(O.Y) <= 1.0e-3) { ++Out.ZeroRadiusSatellites; }
        double Deg = FMath::RadiansToDegrees(FMath::Atan2(O.Y, O.X));
        if (Deg < 0.0) { Deg += 360.0; }
        Bearings.Add(Deg);
    }
    Bearings.Sort();

    // The N gaps around the FULL circle (the last one wraps), so they sum to 360 by construction. The largest gap is
    // the widest empty sector -- how far this group can be yawed before a satellite sweeps into ground that was clear
    // -- and the smallest is the tightest angular pair, which is what constrains a fine yaw step.
    Out.SortedGapsDeg.Reserve(N);
    for (int32 i = 0; i < N - 1; ++i) { Out.SortedGapsDeg.Add(Bearings[i + 1] - Bearings[i]); }
    Out.SortedGapsDeg.Add(Bearings[0] + 360.0 - Bearings[N - 1]);
    Out.SortedGapsDeg.Sort();
    Out.MinGapDeg = Out.SortedGapsDeg[0];
    Out.MaxGapDeg = Out.SortedGapsDeg.Last();
    Out.bValid = true;
    Out.Sentinel = TEXT("<valid>");
}

void MeasureCrossWellClearance(UWorld* World, const TArray<FNodeShuffleWellRecord>& Wells,
                               const TArray<AFGResourceNodeFrackingSatellite*>& OrphanSatellites,
                               FNodeShuffleWellClearance& Out)
{
    if (!World) { return; }

    // Which well each grouped satellite belongs to, so a member is never measured against its own group. Non-finite
    // members are left out entirely -- they cannot be compared and must not seed a minimum (W2).
    TMap<const AActor*, int32> WellOfSatellite;
    for (int32 w = 0; w < Wells.Num(); ++w)
    {
        for (const FNodeShuffleWellMember& M : Wells[w].Members)
        {
            if (M.bLocationFinite) { WellOfSatellite.Add(M.Actor, w); }
        }
    }
    if (WellOfSatellite.Num() == 0) { return; } // bRan stays false: no satellites means no answer, not a pass

    // W1: orphans are NOT in WellOfSatellite (they have no well), so without this set they would be scored as foreign
    // nodes -- even though an orphan is most often a physical sibling whose core just has not streamed.
    TSet<const AActor*> OrphanSet;
    for (AFGResourceNodeFrackingSatellite* S : OrphanSatellites) { if (IsValid(S)) { OrphanSet.Add(S); } }

    // ONE actor-iterator pass into a flat array, then an in-memory sweep. Re-running TActorIterator per satellite (the
    // shape OverlapsAt uses per candidate location) would be ~121 full world walks for no gain here.
    TArray<TPair<FVector, AFGResourceNode*>> Nodes;
    for (TActorIterator<AFGResourceNode> It(World); It; ++It)
    {
        AFGResourceNode* Node = *It;
        if (!IsValid(Node)) { continue; }
        if (Node->IsA<AFGResourceDeposit>()) { ++Out.DepositsExcluded; continue; } // mirrors OverlapsAt (W9)
        const FVector L = Node->GetActorLocation();
        if (!IsFiniteVec(L)) { ++Out.NonFiniteNodesExcluded; continue; }
        // W5: measured, not assumed -- on a shuffled save the hidden originals AND our spawned nodes are both live
        // AFGResourceNodes. Counting the hidden ones is CORRECT (OverlapsAt has no IsHidden test either, so this
        // population matches the guard's), but the reader has to be told which kind of world this is.
        if (FNodeShuffleModule::IsManagedSpawnedNode(Node)) { ++Out.ManagedNodesScanned; }
        if (UNodeShuffleNodeComponent::Find(Node)) { ++Out.RelocatedNodesScanned; }
        if (OrphanSet.Contains(Node)) { ++Out.OrphanNodesInPopulation; }
        Nodes.Add(TPair<FVector, AFGResourceNode*>(L, Node));
    }
    Out.NodesScanned = Nodes.Num();

    for (const TPair<const AActor*, int32>& Entry : WellOfSatellite)
    {
        const AActor* SatActor = Entry.Key;
        if (!IsValid(SatActor)) { ++Out.SkippedInvalidSatellites; continue; } // W9: diagnosed, not silent
        const FVector SatLoc = SatActor->GetActorLocation();
        if (!IsFiniteVec(SatLoc)) { ++Out.SkippedInvalidSatellites; continue; }
        ++Out.SatellitesTested;
        for (const TPair<FVector, AFGResourceNode*>& Node : Nodes)
        {
            if (Node.Value == SatActor) { continue; }
            if (const int32* OtherWell = WellOfSatellite.Find(Node.Value))
            {
                if (*OtherWell == Entry.Value) { continue; } // same well -- that is WELLDIST's measurement, not this one
            }
            ++Out.PairsCompared;
            const double D = FVector::Dist(SatLoc, Node.Key);
            const bool bOrphan = OrphanSet.Contains(Node.Value);
            // LOW-3: on a shuffled save the population deliberately includes NodeShuffle's OWN relocated spawns, so a
            // NOT-SUFFICIENT verdict can be caused entirely by one of our nodes with nothing in the line saying so.
            // The error is ASYMMETRIC: hidden originals stay live at their vanilla locations, so SUFFICIENT cannot be
            // falsely clear -- only NOT-SUFFICIENT can be falsely triggered.
            const TCHAR* Provenance = FNodeShuffleModule::IsManagedSpawnedNode(Node.Value) ? TEXT(" [NodeShuffle-MANAGED]")
                : (UNodeShuffleNodeComponent::Find(Node.Value) ? TEXT(" [relocated]") : TEXT(""));
            if (D < 800.0) { ++Out.CountUnder800; }
            if (!Out.bHaveMin || D < Out.GlobalMin)
            {
                Out.bHaveMin = true; Out.GlobalMin = D;
                Out.Who = FString::Printf(TEXT("sat='%s' nearest='%s'%s%s"), *SatActor->GetName(), *Node.Value->GetName(),
                    bOrphan ? TEXT(" [ORPHAN satellite -- possibly a physical sibling]") : TEXT(""), Provenance);
            }
            if (bOrphan) { ++Out.OrphanComparisons; continue; } // excluded from the verdict-bearing minimum
            if (D < 800.0) { ++Out.CountUnder800ExOrphans; }
            if (!Out.bHaveMinExOrphans || D < Out.GlobalMinExOrphans)
            {
                Out.bHaveMinExOrphans = true; Out.GlobalMinExOrphans = D;
                Out.WhoExOrphans = FString::Printf(TEXT("sat='%s' nearest='%s'%s"), *SatActor->GetName(), *Node.Value->GetName(), Provenance);
            }
        }
    }
    Out.bRan = true;
}
