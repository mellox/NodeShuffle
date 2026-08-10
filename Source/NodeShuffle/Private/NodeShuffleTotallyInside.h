// ns-t53-totallyinside (2026-08-10): "IS THIS POINT'S CENTRE TOTALLY INSIDE SOLID GEOMETRY".
// A SHADOW METRIC. NOTHING GATES ON IT, NOTHING REFUSES BECAUSE OF IT, AND NO PLACEMENT PATH CALLS IT.
//
// THE AUTHOR'S RULING THIS ANSWERS (2026-08-10, verbatim): "I'm ok with working on the center being on
// the edge later if we can stop them being totally inside for now." So the question here is narrowed on
// purpose: a centre TOTALLY INSIDE solid geometry. The finer "the centre is exactly on the edge" rule is
// DEFERRED and is not evaluated anywhere in this file or its two companions.
//
// WHY THE SHAPE OF THIS TEST IS THE OPPOSITE OF THE THREE THAT FAILED. docs/TECH-DEBT.md T41, T48 and T50
// record three methods that each tried to prove a NEGATIVE -- "this centre is not inside anything" -- and
// each failed for the same structural reason. The author has ruled that a PARTLY embedded member is
// WANTED, so a point wrongly called not-inside costs nothing, while a point wrongly called inside would
// condemn terrain and cave placements the author asked to keep. This instrument is therefore built to
// make a POSITIVE trustworthy and to tolerate an unreliable NEGATIVE:
//   * every direction must report solid before the point is called inside -- one direction escaping is
//     enough to withhold the verdict;
//   * within a direction, FOUR independent instruments are run and ANY ONE of them reporting solid is
//     enough for that direction. A direction reads clear only when all four say clear.
//
// WHY FOUR INSTRUMENTS PER DIRECTION RATHER THAN ONE OUTWARD RAY. The measured behaviour this project
// keeps hitting is that a query which STARTS INSIDE a body does not report that body: the centre-shadow
// walk has never once reported a hit from a surface's far side on any run, and the shipped enclosure
// predicate read 0 of 8 blocked at a member sitting in a cliff face (T41). An outward ray from a buried
// centre is exactly that query. So each direction is also probed from OUTSIDE IN, where a surface is met
// from the side this engine has been observed to report, and the outer endpoint is itself tested for
// being solid -- which, if it is, is evidence for containment rather than against it. WHICH instrument
// produced each direction's positive is printed, so the log says which of the four actually works in the
// geometry that is being argued about instead of leaving it to be inferred.
//
// EXCLUSIONS, ALL THREE, EACH COUNTED WITH ITS DENOMINATOR: the caller's named subject actor, APawn
// (docs/TECH-DEBT.md T36 -- the author's own capsule once blocked all 8 enclosure rays) and AFGBuildable
// (T40 -- 24 of 28 blocked rays were the author's own extractors). They are applied the way
// ns-t51-ignorelist applies them: handed to the query's own FCollisionQueryParams ignore list and the
// query re-run from the SAME endpoints, so an excluded actor cannot consume the retrace budget more than
// once and cannot make the probe step over geometry.
//
// NODE-TYPE AGNOSTIC BY CONSTRUCTION. It takes a world location and an optional subject actor. Nothing
// here reads a resource descriptor, a purity, a node class or a well record, so a caller with a solid
// node, an oil node or a modded node class (lithium, lead, chlorine) supplies the same two arguments.
//
// IT STATES NO CAUSE. Every field is written from a query this call made on this run.

#pragma once

#include "CoreMinimal.h"

// One probed direction's four readings. Every field is what a query returned, or a sentinel saying the
// query returned nothing to name.
struct FNodeShuffleTotallyInsideRay
{
    int32 Index = 0;                       // 1-based, for the log line
    FString Label;                         // this direction in words, e.g. straight up
    FVector Unit = FVector::ZeroVector;    // the unit vector actually used
    FVector OuterPoint = FVector::ZeroVector;  // the point at the probe reach along it

    // A: a line trace FROM the tested point outward to the outer point.
    bool bOutwardBlocked = false;
    double OutwardSolidAtCm = -1.0;        // distance from the tested point to that impact, -1 when none
    FString OutwardWhat;

    // B: a line trace FROM the outer point back to the tested point.
    bool bInwardBlocked = false;
    double InwardSolidAtCm = -1.0;         // distance from the TESTED POINT to that impact, -1 when none
    FString InwardWhat;

    // C: a sphere sweep along the same inward segment. Reports its own start-penetration separately,
    // which is a statement about the OUTER point rather than about anything between the two.
    bool bSweepBlocked = false;
    bool bSweepStartPenetrating = false;
    double SweepSolidAtCm = -1.0;          // distance from the tested point, -1 when none
    FString SweepWhat;

    // D: a sphere overlap AT the outer point.
    bool bOuterOverlapSolid = false;
    int32 OuterOverlapResults = 0;         // DENOMINATOR: everything that overlap returned
    int32 OuterOverlapBlocking = 0;        // of those, the ones reported as blocking on the channel
    FString OuterOverlapWhat;

    bool bBlocked = false;                 // ANY of A-D reported solid along or at the end of this ray
    double NearestSolidAtCm = -1.0;        // the closest of whatever A-D reported, -1 when none did
    FString BlockedBy;                     // which of the four produced it, or a sentinel
};

// One point's reading. Nothing in it is read by any gate.
struct FNodeShuffleTotallyInsideReading
{
    bool bRan = false;                     // false = no query was made; no count below is a reading
    FVector Point = FVector::ZeroVector;   // the point tested, exactly as handed in
    double ProbeReachCm = 0.0;             // how far each direction was probed, from its constant
    double SweepRadiusCm = 0.0;            // the sweep's radius, from its constant
    double OverlapRadiusCm = 0.0;          // the overlap radius, from its constant
    int32 RetraceCap = 0;                  // most re-runs one query may make to get past excluded actors

    int32 DirectionCount = 0;              // DENOMINATOR for every direction count below
    TArray<FNodeShuffleTotallyInsideRay> Rays;
    int32 BlockedDirections = 0;
    int32 ClearDirections = 0;
    bool bTotallyInside = false;           // BlockedDirections == DirectionCount, and DirectionCount > 0

    // Which instrument would have carried each blocked direction on its own. A direction can be counted
    // in more than one of these, so they need not sum to BlockedDirections.
    int32 CarriedByOutward = 0;
    int32 CarriedByInward = 0;
    int32 CarriedBySweep = 0;
    int32 CarriedBySweepStart = 0;
    int32 CarriedByOuterOverlap = 0;

    // Of the blocked directions, the FURTHEST any of them had to go to meet solid. A reach shorter than
    // this would have left at least one direction reading clear, so it is the sensitivity of the verdict
    // to the reach constant, measured rather than argued. -1 when no direction was blocked.
    double FurthestNearestSolidCm = -1.0;

    // Corroboration only. An overlap that finds solid AT the tested point supports the verdict; one that
    // finds none does NOT contradict it, because an overlap cannot report containment inside a landscape
    // heightfield or a complex-as-simple triangle mesh (the T39 cold review). Neither outcome enters
    // bTotallyInside.
    bool bCentreOverlapRan = false;
    int32 CentreOverlapResults = 0;
    int32 CentreOverlapBlocking = 0;
    bool bCentreOverlapSolid = false;
    FString CentreOverlapWhat;

    // Exclusions and their denominators.
    int32 QueriesMade = 0;                 // DENOMINATOR: line traces and sweeps issued, re-runs included
    int32 HitsSeen = 0;                    // of those, the ones that returned a blocking result
    int32 ExcludedSubjectHits = 0;         // of those, on the actor the caller named as the subject
    int32 ExcludedPawnHits = 0;            // of those, on an APawn
    int32 ExcludedBuildableHits = 0;       // of those, on an AFGBuildable
    int32 IgnoredRehits = 0;               // a query returned an actor it had already handed to ignore
    int32 RetraceCapHits = 0;              // a query used its whole retrace budget and gave no reading

    int32 OverlapsMade = 0;                // DENOMINATOR: overlaps issued at the outer points and centre
    int32 OverlapResultsSeen = 0;          // everything those overlaps returned
    int32 ExcludedOverlapSubject = 0;      // of the blocking ones, dropped for being the subject
    int32 ExcludedOverlapPawn = 0;
    int32 ExcludedOverlapBuildable = 0;
};

// How this instrument's verdict and the SHIPPED enclosure gate's verdict relate FOR ONE POINT. Returned
// so a caller running a population can add the disagreement up rather than only print it.
enum class ETotallyInsideAgreement : uint8
{
    NotComparable,   // no query ran, so there is no verdict to compare
    AgreeRefuse,     // the shipped gate refuses this point and this instrument's verdict is positive
    AgreePass,       // the shipped gate does not refuse it and this instrument's verdict is not positive
    CandidateAdds,   // this instrument is positive where the shipped gate accepts
    CandidateDrops   // this instrument is not positive where the shipped gate refuses
};

// Runs every query for ONE point and fills Out. Returns Out.bTotallyInside. Traces and overlaps only:
// it spawns nothing, moves nothing and writes no layout field. World may be null, in which case Out is
// left with bRan false and every count at zero -- a caller must be able to tell "nothing was run" from
// "it ran and found nothing".
bool RunTotallyInsideProbe(class UWorld* World, const FVector& At, const class AActor* SubjectActor,
                           FNodeShuffleTotallyInsideReading& Out);

// Emits this reading under the caller's own log prefix and returns the comparison above. One definition
// for all three probe commands, for the reason docs/TECH-DEBT.md T26 exists: three copies of one
// instrument's wording drift, and a reader then cannot tell a difference in the WORLD from a difference
// in the log strings.
//   Prefix        -- the command's log prefix, e.g. TEXT("WELLPROBE").
//   Tag           -- what this point is, in the caller's words. Never empty.
//   R             -- the reading, already taken by RunTotallyInsideProbe.
//   SubjectName   -- what the caller named as the subject actor, or its own explicit sentinel.
//   bGateRefuses  -- the SHIPPED enclosure predicate's verdict for the same point, this run.
//   Blocked/Total/Threshold -- that predicate's working, so the comparison carries both sides' numbers.
ETotallyInsideAgreement LogTotallyInsideReading(const TCHAR* Prefix, const FString& Tag,
                                                const FNodeShuffleTotallyInsideReading& R,
                                                const FString& SubjectName,
                                                bool bGateRefuses, int32 Blocked, int32 Total,
                                                int32 Threshold);
