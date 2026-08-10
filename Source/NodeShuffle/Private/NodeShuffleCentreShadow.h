// ns-t42-centreshadow: THE ONE EMITTER for the shadow centre-containment reading, shared by all three
// probe commands (NodeShuffle.Here, NodeShuffle.PointAtHere, NodeShuffle.WellProbe).
//
// It exists for the reason docs/TECH-DEBT.md T26 exists: three copies of one instrument's wording drift,
// and a reader comparing two commands' output then cannot tell a real difference in the WORLD from a
// difference in the two log strings. One definition, three callers.
//
// It is a FREE function, not a member: it reads no subsystem state at all -- the caller has already run
// the reading and hands it in. It is deliberately node-type-agnostic for the same reason the reading
// itself is: a future caller with a solid node, an oil node or a modded node class supplies the same
// arguments. Nothing here traces, decides, gates or refuses.

#pragma once

#include "CoreMinimal.h"

struct FNodeShufflePointInsideReading;

// How the shipped enclosure gate's verdict and the shadow candidate's verdict relate FOR ONE POINT.
// Returned so a caller running over a population can tally the disagreement rather than only print it --
// a per-item disagreement that never gets added up is one nobody can size.
enum class ECentreShadowAgreement : uint8
{
    NotComparable,   // the shadow walks did not run; there is no candidate verdict to compare
    AgreeRefuse,     // both refuse this point
    AgreePass,       // neither refuses this point
    CandidateAdds,   // the candidate would REFUSE a point the shipped gate accepts
    CandidateDrops   // the candidate would ACCEPT a point the shipped gate refuses
};

// ns-t52-memberdetail (2026-08-10): WHICH READINGS PRINT THEIR PER-HIT SHADOWCROSS LIST.
// ns-t49-crossingdetail gave the caller ONE switch that meant both "collect the per-hit list" and
// "print it", and NodeShuffle.WellProbe turned it off to keep 7 members x 2 walks off the screen. After
// ns-t51-ignorelist the only points still reading CENTRE INSIDE are well-member locations, so the
// instrument was off exactly where it was needed. THE TWO ARE NOW SEPARATE: the reading's own
// bCrossingDetailRequested still decides whether the walks BUILD the list (WellProbe now asks for it on
// every member), and this policy decides which of those built lists are PRINTED. It selects lines to
// print; it changes no walk, no count and no verdict.
enum class ECentreShadowDetailPolicy : uint8
{
    // Print the list for every point this caller probes. NodeShuffle.Here and NodeShuffle.PointAtHere,
    // which a reader points at ONE point, pass this and their output is unchanged.
    EveryPoint,
    // Print the list only where the reading is one of the two this packet went looking for: the candidate
    // would REFUSE the point, or the positive control did not read inside so the point's NOT INSIDE is a
    // verdict this method did not demonstrate it could have contradicted. A point the candidate accepts
    // whose control read inside keeps exactly its two summary lines.
    RefusedOrControlDidNotDemonstrate
};

// The selection above, as ONE predicate, so the emitter and any caller tallying how many lists it got
// read the same expression rather than two copies that can drift. Pure: it reads the reading and the
// verdict already taken and touches nothing.
bool CentreShadowDetailSelected(const FNodeShufflePointInsideReading& Centre, bool bCentreInside);

// Emits two Display lines under the caller's own command prefix and returns the comparison.
//   Prefix          -- the command's log prefix, e.g. TEXT("WELLPROBE").
//   Tag             -- what this point is, in the caller's words, e.g. TEXT("member 3 of 7") or
//                      TEXT("the point handed to the enclosure predicate"). Never empty.
//   Centre          -- the reading, already taken by IsPointInsideSolidShadowForDiag.
//   bCentreInside   -- that call's return, passed in rather than re-derived from the struct.
//   SubjectName     -- what the caller named as the subject actor, or its own explicit sentinel.
//   bGateRefuses    -- the SHIPPED enclosure predicate's verdict for the same point, this run.
//   Blocked/Total/Threshold -- that predicate's working, so the comparison line carries both sides'
//                      numbers and a reader never has to pair it with a line further up.
//   DetailPolicy    -- ns-t52-memberdetail. Which of the built per-hit lists this caller wants PRINTED.
//                      Defaults to EveryPoint, which is what the two single-point commands were already
//                      doing; only the population command passes anything else.
ECentreShadowAgreement LogCentreShadowReading(const TCHAR* Prefix, const FString& Tag,
                                              const FNodeShufflePointInsideReading& Centre,
                                              bool bCentreInside, const FString& SubjectName,
                                              bool bGateRefuses, int32 Blocked, int32 Total,
                                              int32 Threshold,
                                              ECentreShadowDetailPolicy DetailPolicy =
                                                  ECentreShadowDetailPolicy::EveryPoint);
