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
ECentreShadowAgreement LogCentreShadowReading(const TCHAR* Prefix, const FString& Tag,
                                              const FNodeShufflePointInsideReading& Centre,
                                              bool bCentreInside, const FString& SubjectName,
                                              bool bGateRefuses, int32 Blocked, int32 Total,
                                              int32 Threshold);
