// ns-t45-verticaldiag: THE SHARED EMITTERS that answer "what is actually above/around this point".
//
// WHY THIS FILE EXISTS. All evening the three probe commands printed a long downward ground trace whose
// landing point sat metres ABOVE the point probed (+2 m, +13 m, +13 m, +5.7 m, +5.7 m, +21.5 m on one
// run), and no line said WHAT that trace ended on. A gap with no named owner is ambiguous between a cave
// roof, a rock/cliff overhang and solid burial, and every vertical predicate in the mod depends on which
// of those it is. The first emitter here names the actor and component the trace's blocking hit belonged
// to. The second reports what the MOD'S OWN cave store already holds about the probed point.
//
// NEITHER EMITTER DECIDES ANYTHING. Nothing in this file traces, gates, refuses, spawns, moves or writes
// a layout field, and no placement path reads anything it produces. Both are Display-only.
//
// ONE DEFINITION, THREE CALLERS -- the reason NodeShuffleCentreShadow.h exists (docs/TECH-DEBT.md T26):
// three copies of one instrument's wording drift, and a reader comparing two commands then cannot tell a
// difference in the WORLD from a difference in the two log strings.

#pragma once

#include "CoreMinimal.h"

struct FHitResult;
struct FNodeShuffleCaveCellReading;

// Names what a ground trace's blocking hit belonged to, and how far it was from the point traced from.
//   Prefix      -- the command's log prefix, e.g. TEXT("WELLPROBE").
//   Tag         -- what this point is, in the caller's words. Never empty.
//   ProbedFrom  -- the point the caller handed to RaycastGroundAt (its XY, and the Z the trace started
//                  its window from), so the distance printed is measured against something the reader
//                  can see on the caller's other lines.
//   bTraceHit   -- RaycastGroundAt's own return for that call, passed in rather than inferred.
//   Hit         -- the hit RaycastGroundAt wrote through its OutGroundHit parameter. Read only when
//                  bTraceHit is true; the caller is not required to have zeroed it otherwise.
void LogGroundTraceHitIdentity(const TCHAR* Prefix, const FString& Tag, const FVector& ProbedFrom,
                               bool bTraceHit, const FHitResult& Hit);

// Reports what the mod's own cave store holds at a point. The reading is taken by the subsystem
// (ANodeShuffleSubsystem::ReadCaveStoreAtForDiag) and handed in; this only puts it into words.
void LogCaveStoreReading(const TCHAR* Prefix, const FString& Tag,
                         const FNodeShuffleCaveCellReading& Cave);

// Adds a population of those lookups up under one denominator. Present + Absent must be the count the
// caller actually probed; StoreTotal is the store size the LAST lookup of that run read back, or -1
// when no lookup ran, so the denominator is a number the run measured rather than one re-read after.
void LogCaveStoreGroupSummary(const TCHAR* Prefix, const FString& GroupTag, int32 Probed,
                              int32 Present, int32 Absent, int32 StoreTotal);
