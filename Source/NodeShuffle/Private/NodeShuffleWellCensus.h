#pragma once

// Packet H0 (ns-wells): shared, PURE resource-well census helpers, factored out of NodeShuffleWellDump.cpp -- the SAME
// split Packet G made when it pulled NodeShuffleExtractorDiscovery.h/.cpp out of NodeShuffleExtractorDump.cpp, for the
// same two reasons: the command file stays under the 500-line limit, and H1/H2 will need this grouping as a DECISION
// input rather than only as a dump, so it must never become two copies that drift.
//
// Every function here is PURE (no UE_LOG). Anything the census DISCOVERS that deserves a warning is returned as a note
// string or a counter for the caller to log, so the command owns its own prefix and cadence -- exactly the contract
// NodeShuffleExtractorDiscovery.h states. That is also why the counters are so granular: a pure helper cannot warn, so
// it has to hand back enough for the caller to tell "clean" from "could not answer".
//
// ACTOR LOCATIONS ARE VALIDATED AT THE POINT OF READ (ns-review-h0-2 W2). A non-finite location does not merely
// produce an ugly print: `NaN < Best` is false, so a NaN arriving FIRST is stored as a minimum and can never be
// displaced, and `NaN < 800.0` is false, which routes the poisoned value into the PASSED/SUFFICIENT branch of a
// verdict. Precisely (INFO-6 -- the earlier wording claimed a stronger invariant than the code holds): each well
// member's and core's location is read ONCE, for GROUPING, and cached in bLocationFinite/bCoreLocationFinite, which
// every geometry consumer filters on; MeasureCrossWellClearance RE-READS GetActorLocation for its own sweep and
// RE-VALIDATES each one there. The safety property holds on both paths -- the "exactly once" claim did not.
//
// KNOWN, DOCUMENTED LIMITATION: when two candidate nodes sit at EXACTLY equal distance, which one WhoExOrphans (and
// Who) names is decided by TMap iteration order and is therefore not deterministic across runs. It only names a node
// in a log line and reaches no verdict, so it is a limitation rather than a defect.

#include "CoreMinimal.h"
#include "Resources/FGResourceNodeFrackingSatellite.h" // brings AFGResourceNodeFrackingCore, EResourcePurity, EFrackingSatelliteState

const TCHAR* NodeShuffleWellPurityName(EResourcePurity Purity);
const TCHAR* NodeShuffleWellStateName(EFrackingSatelliteState State);

// One member of a well, carrying which VIEW(S) claimed it. A member in only one view is the discrepancy design R1
// warns about; ViewARegistrations > 1 is the no-dedup double-registration hazard (R2).
struct FNodeShuffleWellMember
{
    AFGResourceNodeFrackingSatellite* Actor = nullptr;
    bool  bInViewA = false;        // appeared in core->Native_GetSatellites()       (the REGISTRATION)
    bool  bInViewB = false;        // satellite->GetCore() pointed back at this core (the AUTHORED link)
    int32 ViewARegistrations = 0;  // >1 == this satellite is in the core's array more than once
    FVector Location = FVector::ZeroVector;
    bool  bLocationFinite = false; // false => excluded from EVERY geometric measurement, never printed as a number
};

struct FNodeShuffleWellRecord
{
    AFGResourceNodeFrackingCore* Core = nullptr;
    TArray<FNodeShuffleWellMember> Members;   // UNION of view A and view B
    bool bAdoptedViaSatellite = false;        // core reached only through a satellite's mCore, not the actor iterator
    FVector CoreLocation = FVector::ZeroVector;
    bool bCoreLocationFinite = false;         // false => this well has no measurable geometry at all
};

struct FNodeShuffleWellCensus
{
    TArray<FNodeShuffleWellRecord> Wells;
    // Core provenance kept separate so the caller's streaming-health line can be EXACT: Wells.Num() is NOT the
    // iterator count once adoption has grown it, and it never included the invalid ones (W6/F6).
    int32 IteratorCores = 0, AdoptedCores = 0, InvalidCoresSkipped = 0;
    int32 SatelliteActorsValid = 0, InvalidSatsSkipped = 0; // valid + skipped == what the iterator actually returned
    int32 NullCoreSatellites = 0;
    int32 DuplicateRegistrations = 0, StaleWeakInCoreArrays = 0;
    int32 NonFiniteCores = 0, NonFiniteSatellites = 0;
    TArray<AFGResourceNodeFrackingSatellite*> OrphanSatellites; // GetCore() null
    TArray<FString> InAOnly, InBOnly;                           // one-sided members, pre-formatted
    TArray<FString> StaleWeakNotes, DuplicateNotes, AdoptionNotes, NonFiniteNotes;
};

// Builds the well grouping TWICE -- once from each core's registration array, once from each satellite's authored
// mCore -- and cross-checks them. Pure: does not log.
void CollectWellCensus(class UWorld* World, FNodeShuffleWellCensus& Out);

// Per-well geometry derived ONLY from members whose location was validated finite. Lives here rather than in the
// command so the command file stays a logger and the filtering rule has exactly one implementation.
struct FNodeShuffleWellGeometry
{
    TArray<FVector> Offsets;      // finite members only, relative to the core
    bool  bCoreFinite = false;
    int32 MembersUsed = 0, MembersExcludedNonFinite = 0;
    int32 PairsCompared = 0, PairsUnder800 = 0;
    bool  bHavePair = false;
    double MinSatSat3D = 0.0, MinSatSatXY = 0.0;
    // Tracked separately: the closest pair in 3-D need not be the closest in XY, and naming one pair beside both
    // numbers claimed a correspondence that does not hold (W10c).
    FString MinPair3DWho = TEXT("<none>"), MinPairXYWho = TEXT("<none>");
    bool  bHaveCoreSat = false;
    double MinCoreSat3D = 0.0;
    FString MinCoreSatWho = TEXT("<none>");
    int32 CoreSatUnder800 = 0;
};
void ComputeWellGeometry(const FNodeShuffleWellRecord& W, FNodeShuffleWellGeometry& Out);

// Second moment of the satellites' XY offsets ABOUT THE CORE -- deliberately NOT about the satellite centroid. H2's
// search rotates the group about the CORE (design Q3a step 2), so the core is the correct origin for the anisotropy
// number that decides how many yaws are worth trying; a centroid-relative moment would describe a DIFFERENT rotation
// than the one we actually perform.
struct FNodeShuffleWellFootprint
{
    bool bValid = false;          // W3: false => every numeric field below is meaningless and must print as a sentinel
    int32 N = 0, NonFiniteOffsets = 0;
    double Sxx = 0.0, Syy = 0.0, Sxy = 0.0, LambdaMax = 0.0, LambdaMin = 0.0;
    FString AspectStr = TEXT("<uncomputed>"), AxisDegStr = TEXT("<uncomputed>");
    double BoundRadiusXY = 0.0, MinDistXY = 0.0, MeanDistXY = 0.0, ZSpread = 0.0;
};
void ComputeWellFootprint(const TArray<FVector>& Offsets, FNodeShuffleWellFootprint& Out);

// Angular distribution of the satellites as seen from the core. Aspect ratio is an RMS proxy over ~7 points and does
// NOT map linearly onto "how far can this group turn before a satellite sweeps into new ground", which is the actual
// Q3a question -- the sorted gap list answers that directly, so it is what should tune K, with aspect demoted to a
// ranking signal. NOTE (W8): gaps depend on X/Y ONLY, so a Z-only non-finite offset does not invalidate them.
struct FNodeShuffleWellGaps
{
    bool bValid = false;
    FString Sentinel = TEXT("<uncomputed>");
    TArray<double> SortedGapsDeg;  // the N gaps around the full circle, ascending; they sum to 360 by construction
    double MinGapDeg = 0.0, MaxGapDeg = 0.0;
    int32 ZeroRadiusSatellites = 0; // bearing undefined for these (atan2(0,0) == 0), so the gaps are partly fabricated
};
void ComputeWellAngularGaps(const TArray<FVector>& Offsets, FNodeShuffleWellGaps& Out);

// Distance from each grouped satellite to the nearest resource node that is NOT a member of its OWN well. W1: ORPHAN
// satellites (GetCore() null) are real geometry H2 must respect, so they stay in the population -- but an orphan is
// very often a loaded satellite whose CORE simply has not streamed, i.e. a PHYSICAL SIBLING of the satellite being
// tested. Folding those into the minimum reports an INTRA-well distance as an inter-well one, so a second minimum
// EXCLUDING them is computed and the sufficiency verdict is gated on that one.
struct FNodeShuffleWellClearance
{
    bool bRan = false;
    int32 SatellitesTested = 0, SkippedInvalidSatellites = 0;
    int32 NodesScanned = 0, DepositsExcluded = 0, NonFiniteNodesExcluded = 0;
    // OrphanComparisons counts COMPARISONS involving an orphan, not contributions to a minimum (INFO-7).
    int32 OrphanNodesInPopulation = 0, OrphanComparisons = 0;
    int32 ManagedNodesScanned = 0, RelocatedNodesScanned = 0; // W5: is this world shuffled? measured, not assumed
    int64 PairsCompared = 0;                                   // the ACTUAL comparisons, not satellites x nodes
    bool bHaveMin = false;           double GlobalMin = 0.0;           FString Who = TEXT("<none>");
    int32 CountUnder800 = 0;
    bool bHaveMinExOrphans = false;  double GlobalMinExOrphans = 0.0;  FString WhoExOrphans = TEXT("<none>");
    int32 CountUnder800ExOrphans = 0;
};
void MeasureCrossWellClearance(class UWorld* World, const TArray<FNodeShuffleWellRecord>& Wells,
                               const TArray<AFGResourceNodeFrackingSatellite*>& OrphanSatellites,
                               FNodeShuffleWellClearance& Out);
