// Packet H0 (ns-wells, branch feature/extractor-automatch): `NodeShuffle.DumpWells` -- the RESOURCE-WELL CENSUS, the
// DISCOVERY HALF of Packet H exactly as DumpExtractors was for the SF+ auto-allow work. H1 (in-place retype) and H2
// (rigid group relocation) are BOTH tuned from the numbers this prints, so everything here is measurement, never
// behaviour. Log-only and side-effect-free, so -- SAME PRECEDENT as NodeShuffle.Here (NodeShuffle.cpp:33-45) and
// NodeShuffle.DumpExtractors -- it is NOT gated behind EnableDiagnostics. The grouping and all arithmetic live in
// NodeShuffleWellCensus.h/.cpp (pure, no logging); this file is the command and every UE_LOG, mirroring Packet G's
// Discovery/Dump split.
//
// WHY EACH SECTION EXISTS (refs are to _team/nodeshuffle-followups/PacketH-design.md):
//
//  * TWO INDEPENDENT VIEWS OF THE CORE<->SATELLITE LINK (Q1/R1) -- the point of the whole command. View B, the
//    AUTHORED link, is AFGResourceNodeFrackingSatellite::mCore: PRIVATE, EditInstanceOnly, NOT SaveGame, NOT
//    replicated. View A, the REGISTRATION, is AFGResourceNodeFrackingCore::mSatellites: a plain runtime TArray with NO
//    DEDUP, filled from the satellite's own BeginPlay. H2 must rebuild BOTH every load, and R1 says a missed re-link
//    kills a well SILENTLY -- no crash, no error, the pressurizer just reports 0 satellites.
//
//  * FOOTPRINT + ANGULAR GAPS (Q3a). Aspect ratio says how anisotropic the offset cloud is, but it is an RMS proxy
//    over ~7 points and does not map linearly onto "how far can this group turn before a satellite sweeps into new
//    ground" -- the actual Q3a question. WELLGAPS answers that directly and is what should tune K.
//
//  * WELLDIST vs WELLCLEARANCE. OverlapsAt (NodeShuffleSubsystem.cpp:3758) rejects a location on TWO tests: (a) any
//    non-deposit AFGResourceNode within OverlapRejectRadius = 800 cm, 3-D; and (b) OverlapMultiByObjectType finding a
//    non-foundation AFGBuildable within BuildingRejectRadius = 600 cm (:3785). H0 measures ONLY test (a) -- WELLDIST
//    for a well's own members, WELLCLEARANCE for everything else. Test (b) is deliberately NOT measured here: H2 does
//    its own footprint validation, and H0's job is honest vanilla-geometry reporting, so every verdict below is
//    explicitly scoped to the resource-node half (W4).
//
//  * RESOURCE-EQUALITY (Q2) -- the one genuinely shared invariant of a well, reported per well rather than assumed.
//
// EVERY SUMMARY VERDICT MUST DISTINGUISH "CLEAN" FROM "COULD NOT ANSWER" (three review rounds found defects here, all
// in the concluding SENTENCE rather than the arithmetic above it). Each verdict carries its own sample size, and any
// non-finite actor location anywhere forces all three geometric verdicts to COULD-NOT-RUN rather than letting a NaN
// fall through a `< 800.0` test into the PASSED/SUFFICIENT branch.

#include "NodeShuffle.h"
#include "NodeShuffleWellCensus.h"
#include "HAL/IConsoleManager.h"
#include "Resources/FGResourceNode.h"     // EResourcePurity, GetResourcePurity()
#include "Resources/FGResourceNodeBase.h" // IsOccupied(), GetResourceClass()
// TWeakObjectPtr<T>::Get() casts UObject* -> T*, which needs T COMPLETE (the fracking headers only forward-declare
// these two). Includes only -- no method of either class is called.
#include "Buildables/FGBuildableFrackingActivator.h"
#include "Buildables/FGBuildableFrackingExtractor.h"

namespace
{
    FString WellClassPath(const UObject* Obj) { return Obj ? Obj->GetClass()->GetPathName() : FString(TEXT("<null>")); }

    // The EFFECTIVE resource -- GetResourceClass() is what the game itself asks, so it is what the equality invariant
    // must be tested on (the override if one is set, else the authored class).
    FString WellResPath(const AFGResourceNodeBase* Node)
    {
        if (!Node) { return FString(TEXT("<null-node>")); }
        const UClass* Res = Node->GetResourceClass();
        return Res ? Res->GetPathName() : FString(TEXT("<none>"));
    }
    FString Num1(bool bHave, double V) { return bHave ? FString::Printf(TEXT("%.1f"), V) : FString(TEXT("<not-measured>")); }
    // LOW-5: the orphan print is the one location this file resolves itself (orphans carry no cached member record).
    bool WellFiniteVec(const FVector& V) { return !V.ContainsNaN() && FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z); }
}

// The console command. See the file-header comment for the "why" behind each section.
static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleDumpWellsCmd(
    TEXT("NodeShuffle.DumpWells"),
    TEXT("Log a full census of every LOADED resource well (fracking core + its satellites): purities, offsets, footprint ")
    TEXT("anisotropy, angular gaps, resource-equality, inter-satellite and cross-well distances vs the 800 cm resource-node ")
    TEXT("half of the overlap guard, and a two-view cross-check of the core<->satellite link (log-only)."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
    {
        if (!World)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("DUMPWELLS: no world (main menu / no session loaded?) -- nothing censused."));
            return;
        }
        UE_LOG(LogNodeShuffle, Display, TEXT("===== DUMPWELLS BEGIN ====="));

        FNodeShuffleWellCensus C;
        CollectWellCensus(World, C);
        // W2: ANY non-finite actor location poisons every minimum it can reach (NaN never loses a `<` comparison and
        // never trips `< 800.0`), so it disqualifies all three geometric verdicts rather than being merely noted.
        const bool bNonFinite = (C.NonFiniteCores + C.NonFiniteSatellites) > 0;

        for (const FString& S : C.StaleWeakNotes)
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("DUMPWELLS: %s has a STALE/NULL entry in mSatellites (weak ptr no longer ")
                TEXT("resolves) -- SKIPPED; that core's viewA count is short by 1."), *S);
        }
        for (const FString& S : C.DuplicateNotes)
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("DUMPWELLS: DOUBLE REGISTRATION -- %s. Design R2: mSatellites has NO ")
                TEXT("DEDUP, so a duplicate inflates the satellite count and the well's reported rate."), *S);
        }
        for (const FString& S : C.AdoptionNotes)
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("DUMPWELLS: %s -- that core was NOT returned by TActorIterator (mCore is a ")
                TEXT("STRONG pointer, so a core in an unloaded sublevel stays alive). ADOPTED and given BOTH views, so its ")
                TEXT("satellites are not misreported as unregistered."), *S);
        }
        for (const FString& S : C.NonFiniteNotes)
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("DUMPWELLS: %s -- EXCLUDED from every distance, offset and minimum. All ")
                TEXT("geometric verdicts below are forced to COULD-NOT-RUN."), *S);
        }

        // ---- STREAMING HEALTH: printed BEFORE the data so it frames every number below. Counts are given by
        // PROVENANCE, and each stated number is the one actually counted (W6/F6, W10b).
        UE_LOG(LogNodeShuffle, Display,
            TEXT("DUMPWELLS: STREAMING HEALTH -- this command walks LIVE ACTORS via TActorIterator, so it reports what is ")
            TEXT("LOADED RIGHT NOW, not what the level contains. CORES: %d valid from TActorIterator + %d skipped as invalid ")
            TEXT("(= %d returned), + %d adopted via a satellite's mCore -> %d wells censused. SATELLITES: %d valid + %d ")
            TEXT("skipped as invalid (= %d returned). READ IT LIKE THIS: a LOWER-than-expected count means the world had not ")
            TEXT("streamed those wells in -- it does NOT mean they do not exist. 'Found none' here is 'COULD NOT ANSWER', ")
            TEXT("never 'there are none'. Indicative prior (design M3, itself streaming-limited): ~18 cores / ~121 satellites."),
            C.IteratorCores, C.InvalidCoresSkipped, C.IteratorCores + C.InvalidCoresSkipped, C.AdoptedCores, C.Wells.Num(),
            C.SatelliteActorsValid, C.InvalidSatsSkipped, C.SatelliteActorsValid + C.InvalidSatsSkipped);

        double GlobalMinSatSat3D = 0.0, GlobalMinCoreSat3D = 0.0;
        bool bHaveGlobalSatSat = false, bHaveGlobalCoreSat = false;
        FString GlobalMinSatSatWho = TEXT("<none>"), GlobalMinCoreSatWho = TEXT("<none>");
        int32 GlobalPairsCompared = 0, GlobalPairsUnder800 = 0, GlobalCoreSatUnder800 = 0;
        // INFO-8: this increments when a well has ANY excluded member, so a well with 6 good members and 1 non-finite
        // is NOT "refused" -- the per-well WELLGEOM line carries the real breakdown.
        int32 GroupedSatellites = 0, MismatchWells = 0, WellsWithNoSatellites = 0, WellsWithExcludedMembers = 0;
        int32 MinSatsPerWell = MAX_int32, MaxSatsPerWell = 0;
        TMap<FString, int32> WellsPerResource;
        int32 PurityHistogram[RP_MAX + 1] = { 0 };
        int32 PurityOutOfRange = 0, WellIndex = 0;

        for (const FNodeShuffleWellRecord& W : C.Wells)
        {
            AFGResourceNodeFrackingCore* Core = W.Core;
            const TArray<FNodeShuffleWellMember>& Members = W.Members;
            ++WellIndex;
            int32 ViewACount = 0, ViewBCount = 0;
            for (const FNodeShuffleWellMember& M : Members) { if (M.bInViewA) { ++ViewACount; } if (M.bInViewB) { ++ViewBCount; } }
            const FString CoreRes = WellResPath(Core);
            const UClass* CoreResOrig = Core->GetResourceClassOriginal().Get();
            const UClass* CoreResOvr = Core->GetResourceClassOverride().Get();
            AFGBuildableFrackingActivator* Activator = Core->GetActivator().Get();
            const FString CoreLocStr = W.bCoreLocationFinite
                ? FString::Printf(TEXT("(%.1f,%.1f,%.1f)"), W.CoreLocation.X, W.CoreLocation.Y, W.CoreLocation.Z)
                : FString(TEXT("<NON-FINITE>"));
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELL #%d core='%s' corePath='%s' coreClass='%s' res='%s' resOrig='%s' resOvr='%s' loc=%s occupied=%d ")
                TEXT("activator='%s' viewA=%d viewB=%d members=%d adoptedCore=%d"),
                WellIndex, *Core->GetName(), *Core->GetPathName(), *WellClassPath(Core), *CoreRes,
                CoreResOrig ? *CoreResOrig->GetPathName() : TEXT("<none>"), CoreResOvr ? *CoreResOvr->GetPathName() : TEXT("<none>"),
                *CoreLocStr, Core->IsOccupied() ? 1 : 0, IsValid(Activator) ? *Activator->GetName() : TEXT("<none>"),
                ViewACount, ViewBCount, Members.Num(), W.bAdoptedViaSatellite ? 1 : 0);

            WellsPerResource.FindOrAdd(CoreRes)++;
            GroupedSatellites += Members.Num();
            MinSatsPerWell = FMath::Min(MinSatsPerWell, Members.Num());
            MaxSatsPerWell = FMath::Max(MaxSatsPerWell, Members.Num());
            if (Members.Num() == 0)
            {
                ++WellsWithNoSatellites;
                UE_LOG(LogNodeShuffle, Warning, TEXT("WELLEMPTY well=%d core='%s' -- ZERO satellites from EITHER view. On a ")
                    TEXT("partially-streamed world that just means they had not loaded with the core; after H2 it would mean ")
                    TEXT("the mCore re-link was missed (design R1, the silent killer)."), WellIndex, *Core->GetName());
            }

            TArray<FString> ResMismatches;
            for (int32 i = 0; i < Members.Num(); ++i)
            {
                AFGResourceNodeFrackingSatellite* Sat = Members[i].Actor;
                // Cast before the range test: EResourcePurity is an UNSCOPED enum with no fixed underlying type, so
                // comparing the enum itself against 0 is the kind of expression a compiler may fold to "always true".
                const EResourcePurity Purity = Sat->GetResourcePurity();
                const int32 PurityIdx = static_cast<int32>(Purity);
                if (PurityIdx >= 0 && PurityIdx <= RP_MAX) { ++PurityHistogram[PurityIdx]; }
                else
                {
                    ++PurityOutOfRange;
                    UE_LOG(LogNodeShuffle, Warning, TEXT("DUMPWELLS: satellite '%s' (well=%d core='%s') reports purity raw ")
                        TEXT("value %d, outside EResourcePurity [0..%d] -- H2 cannot carry a purity it cannot name."),
                        *Sat->GetName(), WellIndex, *Core->GetName(), PurityIdx, (int32)RP_MAX);
                }
                const FString SatRes = WellResPath(Sat);
                if (SatRes != CoreRes) { ResMismatches.Add(FString::Printf(TEXT("sat='%s' res='%s'"), *Sat->GetName(), *SatRes)); }
                AFGBuildableFrackingExtractor* Extractor = Sat->GetExtractor().Get();
                const bool bGeom = Members[i].bLocationFinite && W.bCoreLocationFinite;
                const FVector Off = bGeom ? (Members[i].Location - W.CoreLocation) : FVector::ZeroVector;
                const FString LocStr = Members[i].bLocationFinite
                    ? FString::Printf(TEXT("(%.1f,%.1f,%.1f)"), Members[i].Location.X, Members[i].Location.Y, Members[i].Location.Z)
                    : FString(TEXT("<NON-FINITE>"));
                const FString GeomStr = bGeom
                    ? FString::Printf(TEXT("offsetX=%.1f offsetY=%.1f offsetZ=%.1f distXY=%.1f dist3D=%.1f"), Off.X, Off.Y, Off.Z,
                        FVector::Dist2D(Members[i].Location, W.CoreLocation), FVector::Dist(Members[i].Location, W.CoreLocation))
                    : FString(TEXT("offset=<NOT-MEASURED-non-finite-location>"));
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("  WELLSAT well=%d #%d/%d actor='%s' actorPath='%s' class='%s' purity=%s(%d) res='%s' loc=%s %s ")
                    TEXT("state=%s(%d) occupied=%d extractor='%s' inViewA=%d inViewB=%d regCount=%d"),
                    WellIndex, i + 1, Members.Num(), *Sat->GetName(), *Sat->GetPathName(), *WellClassPath(Sat),
                    NodeShuffleWellPurityName(Purity), PurityIdx, *SatRes, *LocStr, *GeomStr,
                    NodeShuffleWellStateName(Sat->GetState()), (int32)Sat->GetState(), Sat->IsOccupied() ? 1 : 0,
                    IsValid(Extractor) ? *Extractor->GetName() : TEXT("<none>"),
                    Members[i].bInViewA ? 1 : 0, Members[i].bInViewB ? 1 : 0, Members[i].ViewARegistrations);
            }

            // ---- resource equality (Q2) ----
            if (Members.Num() == 0)
            {
                UE_LOG(LogNodeShuffle, Warning, TEXT("  WELLRES well=%d result=COULD-NOT-CHECK -- no satellites to compare the core's resource against."), WellIndex);
            }
            else if (ResMismatches.Num() == 0)
            {
                UE_LOG(LogNodeShuffle, Display, TEXT("  WELLRES well=%d result=OK res='%s' (core + all %d satellites carry the same resource class)"),
                    WellIndex, *CoreRes, Members.Num());
            }
            else
            {
                ++MismatchWells;
                UE_LOG(LogNodeShuffle, Warning, TEXT("  WELLRES well=%d result=MISMATCH core='%s' coreRes='%s' -- %d of %d ")
                    TEXT("satellites DIFFER: [%s]. Design Q2 assumes core and every satellite share one resource class; a ")
                    TEXT("MISMATCH means VANILLA violates it, and H1's retype assert must be softened to match reality."),
                    WellIndex, *Core->GetName(), *CoreRes, ResMismatches.Num(), Members.Num(), *FString::Join(ResMismatches, TEXT("; ")));
            }

            // ---- geometry, footprint, gaps, distances ----
            FNodeShuffleWellGeometry G;
            ComputeWellGeometry(W, G);
            if (!G.bCoreFinite || G.MembersExcludedNonFinite > 0)
            {
                ++WellsWithExcludedMembers;
                UE_LOG(LogNodeShuffle, Warning, TEXT("  WELLGEOM well=%d core='%s' coreFinite=%d membersUsed=%d ")
                    TEXT("membersExcludedNonFinite=%d -- excluded members contribute to NO offset, distance or minimum."),
                    WellIndex, *Core->GetName(), G.bCoreFinite ? 1 : 0, G.MembersUsed, G.MembersExcludedNonFinite);
            }
            FNodeShuffleWellFootprint FP;
            ComputeWellFootprint(G.Offsets, FP);
            if (FP.bValid)
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("  WELLFOOT well=%d n=%d Sxx=%.1f Syy=%.1f Sxy=%.1f lambdaMax=%.1f lambdaMin=%.1f aspect=%s axisDeg=%s ")
                    TEXT("boundRadiusXY=%.1f minDistXY=%.1f meanDistXY=%.1f zSpread=%.1f (second moment about the CORE -- the ")
                    TEXT("origin H2 actually rotates about)"),
                    WellIndex, FP.N, FP.Sxx, FP.Syy, FP.Sxy, FP.LambdaMax, FP.LambdaMin, *FP.AspectStr, *FP.AxisDegStr,
                    FP.BoundRadiusXY, FP.MinDistXY, FP.MeanDistXY, FP.ZSpread);
            }
            else
            {
                // W3: a refused well used to print eight confident 0.0s, byte-identical to a real well whose satellites
                // all sit on the core. Numeric fields are suppressed entirely rather than defaulted.
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("  WELLFOOT well=%d n=%d REFUSED (nonFiniteOffsets=%d) -- Sxx/Syy/Sxy/lambdaMax/lambdaMin/")
                    TEXT("boundRadiusXY/minDistXY/meanDistXY/zSpread are NOT MEASURED and are omitted, not zero. aspect=%s ")
                    TEXT("axisDeg=%s. Footprint REFUSED (gaps depend only on X/Y and are reported separately below)."),
                    WellIndex, FP.N, FP.NonFiniteOffsets, *FP.AspectStr, *FP.AxisDegStr);
            }

            FNodeShuffleWellGaps Gaps;
            ComputeWellAngularGaps(G.Offsets, Gaps);
            if (!Gaps.bValid)
            {
                UE_LOG(LogNodeShuffle, Display, TEXT("  WELLGAPS well=%d n=%d gaps=%s (no angular distribution to measure)"),
                    WellIndex, G.Offsets.Num(), *Gaps.Sentinel);
            }
            else
            {
                TArray<FString> GapStrs;
                for (double Gp : Gaps.SortedGapsDeg) { GapStrs.Add(FString::Printf(TEXT("%.1f"), Gp)); }
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("  WELLGAPS well=%d n=%d minGapDeg=%.1f maxGapDeg=%.1f sortedGapsDeg=[%s] -- bearings from the CORE; ")
                    TEXT("gaps sum to 360 by construction (so their mean is 360/n and carries no information). maxGap is the ")
                    TEXT("widest EMPTY sector, i.e. how far this group can yaw before a satellite sweeps into new ground; ")
                    TEXT("minGap is the tightest angular pair. THIS, not aspect, is the number that should tune Q3a's K."),
                    WellIndex, Gaps.SortedGapsDeg.Num(), Gaps.MinGapDeg, Gaps.MaxGapDeg, *FString::Join(GapStrs, TEXT(", ")));
                if (Gaps.ZeroRadiusSatellites > 0)
                {
                    UE_LOG(LogNodeShuffle, Warning, TEXT("  WELLGAPS well=%d -- %d of %d satellites sit on the core's XY, so ")
                        TEXT("their bearing is FABRICATED (atan2(0,0)==0). The gap list above is biased and must not tune K ")
                        TEXT("for this well."), WellIndex, Gaps.ZeroRadiusSatellites, G.Offsets.Num());
                }
            }

            GlobalPairsCompared += G.PairsCompared;
            GlobalPairsUnder800 += G.PairsUnder800;
            GlobalCoreSatUnder800 += G.CoreSatUnder800;
            if (G.bHavePair && (!bHaveGlobalSatSat || G.MinSatSat3D < GlobalMinSatSat3D))
            {
                bHaveGlobalSatSat = true; GlobalMinSatSat3D = G.MinSatSat3D;
                GlobalMinSatSatWho = FString::Printf(TEXT("well=%d core='%s' %s"), WellIndex, *Core->GetName(), *G.MinPair3DWho);
            }
            if (G.bHaveCoreSat && (!bHaveGlobalCoreSat || G.MinCoreSat3D < GlobalMinCoreSat3D))
            {
                bHaveGlobalCoreSat = true; GlobalMinCoreSat3D = G.MinCoreSat3D;
                GlobalMinCoreSatWho = FString::Printf(TEXT("well=%d %s"), WellIndex, *G.MinCoreSatWho);
            }
            UE_LOG(LogNodeShuffle, Display,
                TEXT("  WELLDIST well=%d membersUsed=%d pairsCompared=%d minSatSat3D=%s closestPair3D=%s minSatSatXY=%s ")
                TEXT("closestPairXY=%s pairsUnder800=%d minCoreSat3D=%s (3-D is the one that matters: OverlapsAt uses ")
                TEXT("FVector::DistSquared. The 3-D and XY minima can name DIFFERENT pairs, so both are named.)"),
                WellIndex, G.MembersUsed, G.PairsCompared, *Num1(G.bHavePair, G.MinSatSat3D), *G.MinPair3DWho,
                *Num1(G.bHavePair, G.MinSatSatXY), *G.MinPairXYWho, G.PairsUnder800, *Num1(G.bHaveCoreSat, G.MinCoreSat3D));
        }

        // ---- W6: TWO SEPARATE VERDICTS. Conflating them made H2's acceptance gate one that VANILLA ITSELF cannot
        // pass on a partially-streamed world -- a core loaded without its satellites is streaming incompleteness, not
        // a disagreement between the two views, and a gate vanilla cannot pass is not a gate.
        UE_LOG(LogNodeShuffle, Display, TEXT("===== DUMPWELLS LINK AGREEMENT (the design-R1 shape): inAonly=%d inBonly=%d duplicateRegistrations=%d ====="),
            C.InAOnly.Num(), C.InBOnly.Num(), C.DuplicateRegistrations);
        for (const FString& S : C.InAOnly)
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("  WELLRECON IN-A-NOT-B %s -- the core has it REGISTERED but the satellite's ")
                TEXT("own mCore does not point back. Vanilla should never show this."), *S);
        }
        for (const FString& S : C.InBOnly)
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("  WELLRECON IN-B-NOT-A %s -- the satellite's authored mCore names this core ")
                TEXT("but the core's mSatellites does NOT contain it: registration did not happen (or was undone). This is ")
                TEXT("exactly the shape design R1 says kills a well silently."), *S);
        }
        const bool bLinkRan = (C.Wells.Num() > 0 && GroupedSatellites > 0);
        const bool bLinkAgree = bLinkRan && C.InAOnly.Num() == 0 && C.InBOnly.Num() == 0 && C.DuplicateRegistrations == 0;
        if (!bLinkRan)
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("  LINK AGREEMENT: COULD-NOT-RUN -- %d wells / %d grouped satellites, so the ")
                TEXT("two views were never compared. This is NOT agreement."), C.Wells.Num(), GroupedSatellites);
        }
        else if (bLinkAgree)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("  LINK AGREEMENT: AGREE -- both views match on all %d wells / %d satellites ")
                TEXT("that WERE examined. *** THIS IS H2'S ACCEPTANCE GATE: after relocation this line must still read AGREE, ")
                TEXT("on first placement AND after a save/quit/reload (design T3). *** It deliberately ignores streaming ")
                TEXT("completeness below, which vanilla fails routinely. adoptedCores (%d) are excluded from this test because ")
                TEXT("F3 gives an adopted core BOTH views, so its members are compared on the same footing as any other."),
                C.Wells.Num(), GroupedSatellites, C.AdoptedCores);
        }
        else
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("  LINK AGREEMENT: DISAGREE -- inAonly=%d inBonly=%d dupReg=%d. Each is a real ")
                TEXT("discrepancy between the registration and the authored link, NOT a streaming artifact."),
                C.InAOnly.Num(), C.InBOnly.Num(), C.DuplicateRegistrations);
        }

        for (AFGResourceNodeFrackingSatellite* Sat : C.OrphanSatellites)
        {
            // LOW-5: this was the one location print in the packet with no finiteness guard, so a non-finite orphan
            // printed as `loc=(-1.#IND,...)`. The census now also counts such an orphan into nonFiniteSats.
            const FVector L = Sat->GetActorLocation();
            const FString LocStr = WellFiniteVec(L) ? FString::Printf(TEXT("(%.1f,%.1f,%.1f)"), L.X, L.Y, L.Z) : FString(TEXT("<NON-FINITE>"));
            UE_LOG(LogNodeShuffle, Warning, TEXT("  WELLRECON NULL-CORE sat='%s' actorPath='%s' class='%s' loc=%s ")
                TEXT("-- GetCore() is null. Usually its core has not streamed in; possibly the link is genuinely unset."),
                *Sat->GetName(), *Sat->GetPathName(), *WellClassPath(Sat), *LocStr);
        }
        // MED-1: COMPLETE needs a sample-size term exactly as LINK AGREEMENT has one -- without it, a run where no
        // well streamed in leaves every counter at 0 and prints COMPLETE at Display, i.e. "found none" reading as
        // "verified consistent". This was the only summary line still missing that guard.
        const bool bCensusRan = (C.Wells.Num() > 0 && GroupedSatellites > 0);
        const bool bComplete = (C.NullCoreSatellites == 0 && WellsWithNoSatellites == 0 && C.StaleWeakInCoreArrays == 0
            && C.InvalidCoresSkipped == 0 && C.InvalidSatsSkipped == 0 && !bNonFinite);
        // UE_LOG needs a literal verbosity token (the macro pastes it), so the branch is on the statement, not the arg.
#define NS_WELLS_COMPLETENESS_FMT \
            TEXT("===== DUMPWELLS CENSUS COMPLETENESS: %s -- nullCoreSatellites=%d emptyCores=%d staleWeakEntries=%d ") \
            TEXT("invalidCores=%d invalidSats=%d nonFiniteCores=%d nonFiniteSats=%d adoptedCores=%d. These are ") \
            TEXT("STREAMING/LIFETIME facts, not view disagreements -- all of them are EXPECTED on a partially-streamed ") \
            TEXT("world, which is why H2's gate is pinned to LINK AGREEMENT above and not to this line. ====="), \
            !bCensusRan ? TEXT("COULD-NOT-ANSWER (nothing censused)") : (bComplete ? TEXT("COMPLETE") : TEXT("INCOMPLETE")), \
            C.NullCoreSatellites, WellsWithNoSatellites, \
            C.StaleWeakInCoreArrays, C.InvalidCoresSkipped, C.InvalidSatsSkipped, C.NonFiniteCores, C.NonFiniteSatellites, C.AdoptedCores
        if (bCensusRan && bComplete) { UE_LOG(LogNodeShuffle, Display, NS_WELLS_COMPLETENESS_FMT); }
        else                         { UE_LOG(LogNodeShuffle, Warning, NS_WELLS_COMPLETENESS_FMT); }
#undef NS_WELLS_COMPLETENESS_FMT

        // ---- THE 800 cm RESOURCE-NODE OVERLAP-GUARD VERDICT (gated on PAIRS COMPARED, and on finiteness) ----
        if (bNonFinite || GroupedSatellites == 0 || GlobalPairsCompared == 0)
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("DUMPWELLS: OVERLAP-GUARD CHECK COULD NOT RUN -- %d satellite(s) grouped, %d ")
                TEXT("same-well PAIRS compared, nonFiniteLocations=%d. No trustworthy inter-satellite distance was measured. ")
                TEXT("This is NOT a pass; see the STREAMING HEALTH line."),
                GroupedSatellites, GlobalPairsCompared, C.NonFiniteCores + C.NonFiniteSatellites);
        }
        else if (GlobalMinSatSat3D < 800.0)
        {
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("***** DUMPWELLS WARNING: GLOBAL MINIMUM INTER-SATELLITE DISTANCE IS %.1f cm over %d pairs compared, ")
                TEXT("BELOW OverlapRejectRadius 800 cm (NodeShuffleSubsystem.cpp:3754). %d pair(s) are closer than 8 m. A ")
                TEXT("fracking satellite IS an AFGResourceNode, so OverlapsAt COUNTS IT: if H2 spawns a well's satellites one ")
                TEXT("at a time through the existing per-node guard, THE WELL'S OWN SATELLITES WILL REJECT EACH OTHER and the ")
                TEXT("group can never place. H2 MUST exempt same-group members from the overlap test. Closest pair: %s. *****"),
                GlobalMinSatSat3D, GlobalPairsCompared, GlobalPairsUnder800, *GlobalMinSatSatWho);
        }
        else
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("DUMPWELLS: OVERLAP-GUARD CHECK PASSED -- global minimum inter-satellite ")
                TEXT("distance is %.1f cm over %d pairs compared across %d grouped satellites, at or above 800 cm, so vanilla ")
                TEXT("satellites would not reject each other through the resource-node half of the guard. Closest pair: %s."),
                GlobalMinSatSat3D, GlobalPairsCompared, GroupedSatellites, *GlobalMinSatSatWho);
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("DUMPWELLS: minimum CORE->SATELLITE distance = %s cm (%s); %d core/satellite pair(s) under 800 cm. FOR ")
            TEXT("REFERENCE ONLY: a core is an AFGResourceNodeBase but NOT an AFGResourceNode, so OverlapsAt does not iterate ")
            TEXT("cores and this distance does not trip the guard -- it bounds a well's footprint clearance instead."),
            *Num1(bHaveGlobalCoreSat && !bNonFinite, GlobalMinCoreSat3D), *GlobalMinCoreSatWho, GlobalCoreSatUnder800);

        // ---- CROSS-WELL CLEARANCE: the question H2 needs immediately after the group-exemption one ----
        FNodeShuffleWellClearance Cl;
        MeasureCrossWellClearance(World, C.Wells, C.OrphanSatellites, Cl);
        // MED-2: MeasureCrossWellClearance early-returns BEFORE building Nodes when there are no grouped finite
        // satellites, so logging this row unconditionally handed the reader nine struct defaults as measurements --
        // including the managed= count the same sentence explains how to interpret.
        if (Cl.bRan)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLCLEARANCE POPULATION: %d nodes scanned (%d deposits excluded, %d non-finite excluded), of which %d are ")
                TEXT("NodeShuffle-MANAGED and %d carry a relocation component, %d are ORPHAN satellites. %d satellites tested (%d ")
                TEXT("skipped), %lld actual pair comparisons. WORLD STATE: managed>0 means this is a SHUFFLED save, where hidden ")
                TEXT("originals AND relocated spawns are both live AFGResourceNodes -- counting the hidden ones is CORRECT here ")
                TEXT("because OverlapsAt has no IsHidden test either, so this population matches the guard's. But managed==0 does ")
                TEXT("NOT prove a vanilla world -- relocation spawns incrementally, so an early run can read 0 on a shuffled save."),
                Cl.NodesScanned, Cl.DepositsExcluded, Cl.NonFiniteNodesExcluded, Cl.ManagedNodesScanned, Cl.RelocatedNodesScanned,
                Cl.OrphanNodesInPopulation, Cl.SatellitesTested, Cl.SkippedInvalidSatellites, Cl.PairsCompared);
        }
        else
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("WELLCLEARANCE POPULATION: <NOT RUN -- no grouped finite satellites; nothing was scanned>"));
        }
        if (bNonFinite || !Cl.bRan || !Cl.bHaveMinExOrphans)
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("WELLCLEARANCE: COULD NOT RUN -- ran=%d, orphan-excluded minimum available=%d, ")
                TEXT("nonFiniteLocations=%d. No trustworthy cross-well distance was measured. This is NOT a pass."),
                Cl.bRan ? 1 : 0, Cl.bHaveMinExOrphans ? 1 : 0, C.NonFiniteCores + C.NonFiniteSatellites);
        }
        else
        {
            // W1: the verdict is gated on the ORPHAN-EXCLUDED minimum. An orphan satellite is usually a physical sibling
            // whose core has not streamed, so including it would report an INTRA-well distance as an inter-well one --
            // and, because it is just an ordinary number, that contamination has no visible tell.
            UE_LOG(LogNodeShuffle, Display, TEXT("WELLCLEARANCE MINIMA: including orphans = %s cm (%s), %d comparison(s) under ")
                TEXT("800 cm; EXCLUDING orphans = %.1f cm (%s), %d comparison(s) under 800 cm. Both counts are ORDERED: a ")
                TEXT("satellite<->satellite proximity is counted once from each side, unlike WELLDIST's unordered ")
                TEXT("pairsUnder800. %d comparison(s) involved an orphan. The verdict below uses the ORPHAN-EXCLUDED number; ")
                TEXT("the including-orphans number is still real geometry H2 must respect."),
                *Num1(Cl.bHaveMin, Cl.GlobalMin), *Cl.Who, Cl.CountUnder800, Cl.GlobalMinExOrphans, *Cl.WhoExOrphans,
                Cl.CountUnder800ExOrphans, Cl.OrphanComparisons);
            if (Cl.GlobalMinExOrphans < 800.0)
            {
                UE_LOG(LogNodeShuffle, Warning, TEXT("WELLCLEARANCE: NOT SUFFICIENT (resource-node half of OverlapsAt only) -- ")
                    TEXT("the nearest non-same-well, non-orphan resource node to a grouped satellite is %.1f cm, INSIDE the 800 ")
                    TEXT("cm radius. Even once H2 exempts a well's OWN members, these wells sit within the guard radius of ")
                    TEXT("FOREIGN nodes, so exempting the group is NECESSARY BUT NOT SUFFICIENT and H2's footprint validation ")
                    TEXT("must also decide what to do about the neighbours. NOT MEASURED HERE: the 600 cm AFGBuildable half of ")
                    TEXT("OverlapsAt (:3785), which can reject independently of anything above."), Cl.GlobalMinExOrphans);
            }
            else
            {
                UE_LOG(LogNodeShuffle, Display, TEXT("WELLCLEARANCE: SUFFICIENT with respect to the RESOURCE-NODE half of ")
                    TEXT("OverlapsAt ONLY -- nearest non-same-well, non-orphan resource node is %.1f cm, at or above 800 cm, so ")
                    TEXT("exempting a well's own members would be enough for these wells AT THE LOCATIONS MEASURED IN THIS RUN. ")
                    TEXT("NOT MEASURED HERE: the 600 cm non-foundation AFGBuildable half of OverlapsAt (:3785), which can reject ")
                    TEXT("independently -- so this is not a verdict on the guard as a whole."), Cl.GlobalMinExOrphans);
            }
        }

        // ---- TOTALS ----
        const double MeanSatsPerWell = (C.Wells.Num() > 0) ? static_cast<double>(GroupedSatellites) / static_cast<double>(C.Wells.Num()) : 0.0;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("===== DUMPWELLS TOTALS: wells=%d validSatelliteActors=%d groupedIntoWells=%d orphans(nullCore)=%d satsPerWell ")
            TEXT("min=%d mean=%.2f max=%d resourceMismatchWells=%d wellsWithExcludedMembers=%d ====="), C.Wells.Num(),
            C.SatelliteActorsValid, GroupedSatellites, C.NullCoreSatellites, (C.Wells.Num() > 0) ? MinSatsPerWell : 0,
            MeanSatsPerWell, MaxSatsPerWell, MismatchWells, WellsWithExcludedMembers);
        if (GroupedSatellites + C.NullCoreSatellites != C.SatelliteActorsValid)
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("DUMPWELLS: ACCOUNTING MISMATCH -- grouped(%d) + orphans(%d) != validSatellites(%d). ")
                TEXT("EXPECTED when a core was ADOPTED (%d this run: its satellites are grouped but the core came from mCore, not ")
                TEXT("the iterator). ALSO PRODUCED BY a satellite registered in one core's mSatellites while its own mCore names a ")
                TEXT("DIFFERENT core -- that satellite gets a member record in BOTH wells, inflating groupedIntoWells and the ")
                TEXT("purity histogram by one. That case is never silent: it also shows as two one-sided entries under LINK ")
                TEXT("AGREEMENT (DISAGREE) plus this line."),
                GroupedSatellites, C.NullCoreSatellites, C.SatelliteActorsValid, C.AdoptedCores);
        }
        for (const TPair<FString, int32>& Pair : WellsPerResource)
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("  WELLRESCOUNT resource='%s' wells=%d (design 2.3: the well resource POOL is ")
                TEXT("drawn from exactly this observed set -- never a hardcoded list)"), *Pair.Key, Pair.Value);
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("  WELLPURITYHIST impure=%d normal=%d pure=%d rpMaxSentinel=%d outOfRange=%d (over the %d grouped satellites; ")
            TEXT("design Q2: purity is PER-SATELLITE, so H2 must carry the ordered purity VECTOR, not one scalar per well)"),
            PurityHistogram[RP_Inpure], PurityHistogram[RP_Normal], PurityHistogram[RP_Pure],
            PurityHistogram[RP_MAX], PurityOutOfRange, GroupedSatellites);

        UE_LOG(LogNodeShuffle, Display, TEXT("===== DUMPWELLS END ====="));
    }));
