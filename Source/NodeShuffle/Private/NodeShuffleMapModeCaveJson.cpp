// RWG P5-2 (2026-10-18) -- cave candidates from the per-world cave FILE (user decision 2, binding) and the verify chain shared with the
// random draw. Reached only from RwgDrawCaveSpot / RwgEnsureCaveCheck (NodeShuffleMapModeCave.cpp), which only RWG call sites reach, so
// the vanilla path never enters this file. Log lines report measured values and the predicate that branched, never a cause.
// File shape (Saved/PWMaps/carved-caves-<WorldSeed>.json, 12 files read 2026-10-18): a list of caves; each has `rooms`, `rooms_geom`
// (list of {centre[3], radii[3], floor}), `stand`, `mouth`, `c0_from`/`c0_to`, `c1_from`/`c1_to`, `radius_cm`. rooms_geom was present
// in 12/12 files (74 rooms, 31 caves), so the stand / mouth / c0 / c1 fallback below is not exercised by any file on this machine.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "NodeShuffle.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    // Null-safe 3-vector read (the RWG P5 review F8 guard: a JSON null element is an invalid TSharedPtr, never dereferenced).
    bool CaveVec3(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FVector& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if (!O.IsValid() || !O->TryGetArrayField(Key, A) || !A || A->Num() < 3 || !(*A)[0].IsValid() || !(*A)[1].IsValid() || !(*A)[2].IsValid()) { return false; }
        double V[3] = { 0.0, 0.0, 0.0 };
        for (int32 I = 0; I < 3; ++I) { if (!(*A)[I]->TryGetNumber(V[I])) { return false; } }
        Out = FVector(V[0], V[1], V[2]);
        return true;
    }
    constexpr int32 MaxCaveGens = 256; // bound on generators latched per world (largest file seen: 7 caves)
}

int32 NodeShuffleMapMode::RwgCaveGensFromJson(const TSharedPtr<FJsonObject>& Cave, TArray<FRwgCaveCandidateGen>& Out)
{
    if (!Cave.IsValid()) { return 0; }
    const int32 Before = Out.Num();
    // Rooms first: an axis-aligned XY ellipse (radii[0] x radii[1]) around the centre, weighted by its area; the hint is the room floor.
    // ASSUMED: radii are axis-aligned in XY (the file carries no rotation). Every candidate is verified by FloorCmAt + IsInsideAt anyway.
    const TArray<TSharedPtr<FJsonValue>>* Rooms = nullptr;
    if (Cave->TryGetArrayField(TEXT("rooms_geom"), Rooms) && Rooms)
    {
        for (const TSharedPtr<FJsonValue>& V : *Rooms)
        {
            const TSharedPtr<FJsonObject>* RoomObj = nullptr;
            if (!V.IsValid() || !V->TryGetObject(RoomObj) || !RoomObj || !RoomObj->IsValid() || Out.Num() >= MaxCaveGens) { continue; }
            FVector C, Rad;
            double Floor = 0.0;
            if (!CaveVec3(*RoomObj, TEXT("centre"), C) || !CaveVec3(*RoomObj, TEXT("radii"), Rad) || !(*RoomObj)->TryGetNumberField(TEXT("floor"), Floor)) { continue; }
            if (!(Rad.X > 0.0) || !(Rad.Y > 0.0) || !FMath::IsFinite(Floor)) { continue; }
            FRwgCaveCandidateGen G;
            G.Kind = 0; G.A = C; G.Rx = Rad.X; G.Ry = Rad.Y; G.FloorHintZ = Floor; G.Weight = PI * Rad.X * Rad.Y;
            Out.Add(G);
        }
        if (Out.Num() > Before) { return Out.Num() - Before; }
    }
    // No usable rooms_geom: the stand and mouth points (discs of radius_cm) and the c0 / c1 segments widened by radius_cm (brief fallback).
    double Radius = 0.0;
    if (!Cave->TryGetNumberField(TEXT("radius_cm"), Radius) || !(Radius > 0.0)) { return 0; } // no radius: no area to sample (counted as 0 generators)
    FVector P, B;
    for (const TCHAR* Key : { TEXT("stand"), TEXT("mouth") })
    {
        if (Out.Num() >= MaxCaveGens || !CaveVec3(Cave, Key, P)) { continue; }
        FRwgCaveCandidateGen G;
        G.Kind = 1; G.A = P; G.Rx = G.Ry = Radius; G.FloorHintZ = P.Z; G.Weight = PI * Radius * Radius;
        Out.Add(G);
    }
    const TCHAR* const Segs[2][2] = { { TEXT("c0_from"), TEXT("c0_to") }, { TEXT("c1_from"), TEXT("c1_to") } };
    for (int32 S = 0; S < 2; ++S)
    {
        if (Out.Num() >= MaxCaveGens || !CaveVec3(Cave, Segs[S][0], P) || !CaveVec3(Cave, Segs[S][1], B)) { continue; }
        const double Len = FVector::Dist2D(P, B);
        if (!(Len > 0.0)) { continue; }
        FRwgCaveCandidateGen G;
        G.Kind = 2; G.A = P; G.B = B; G.Rx = G.Ry = Radius; G.FloorHintZ = 0.5 * (P.Z + B.Z); G.Weight = Len * 2.0 * Radius;
        Out.Add(G);
    }
    return Out.Num() - Before;
}

bool ANodeShuffleSubsystem::RwgDrawCaveSpotJson(const NodeShuffleMapMode::FRwgGroundQuery& Q, FRandomStream& Rng, FVector& OutSpot)
{
    using namespace NodeShuffleMapMode;
    // User decision 2: RwgCaveJsonCandidatesPerSpot area-weighted points from the file's generators, each verified by FloorCmAt + IsInsideAt
    // (RwgVerifyCaveCandidate, the same chain as the random ring) before dealing. No AnyNear call is made here (caveAnyNearCalls stays 0).
    if (!bRwgCaveJsonCandidates || RwgCaveGens.Num() == 0 || !(RwgCaveGenWeight > 0.0)) { return false; }
    for (int32 I = 0; I < RwgCaveJsonCandidatesPerSpot; ++I)
    {
        ++RwgRcCaveCandidatesTried;
        double Pick = Rng.FRand() * RwgCaveGenWeight; // generator chosen by its sampled XY area
        int32 Gi = 0;
        for (; Gi < RwgCaveGens.Num() - 1; ++Gi) { Pick -= RwgCaveGens[Gi].Weight; if (Pick <= 0.0) { break; } }
        const FRwgCaveCandidateGen& G = RwgCaveGens[Gi];
        const double Ang = Rng.FRand() * 2.0 * PI;
        const double Rr = FMath::Sqrt(static_cast<double>(Rng.FRand())); // sqrt: uniform over the disc / ellipse area
        FVector Base = G.A;
        if (G.Kind == 2) { Base = FMath::Lerp(G.A, G.B, static_cast<double>(Rng.FRand())); }
        const double X = Base.X + FMath::Cos(Ang) * Rr * G.Rx;
        const double Y = Base.Y + FMath::Sin(Ang) * Rr * G.Ry;
        const double Hint = G.Kind == 0 ? G.FloorHintZ : Base.Z; // room: its floor; point / segment: the file's Z there (FloorCmAt keeps the floor nearest it)
        if (RwgVerifyCaveCandidate(Q, X, Y, &Hint, TEXT("json"), I, 0, OutSpot)) { return true; }
    }
    return false;
}

bool ANodeShuffleSubsystem::RwgVerifyCaveCandidate(const NodeShuffleMapMode::FRwgGroundQuery& Q, double X, double Y, const double* ZHintOrSurface,
                                                   const TCHAR* Source, int32 Index, int32 AnyNearCalls, FVector& OutSpot)
{
    using namespace NodeShuffleMapMode;
    // ONE predicate chain for both candidate sources (SYMMETRY): FloorCmAt (hint = the given Z, or the API surface Z when null) -> IsInsideAt
    // at floor + probe (brief) -> + vanilla headroom (350 cm) -> + vanilla Miner ceiling (1200 cm, review F6) -> the surface prefilters at the
    // spot's XY (sea / void / killbox / slope, as the surface deal) -> the killbox at the FLOOR (stops deal -> teardown ping-pong).
    if (Q.Outside(X, Y)) { return false; }
    const double SurfZ = Q.SurfaceZ(X, Y);
    const double Hint = ZHintOrSurface ? *ZHintOrSurface : SurfZ;
    double Floor = 0.0;
    if (!Q.CaveFloorAt(X, Y, Hint, Floor)) { return false; }
    ++RwgRcCaveFloorHits;
    if (!(Floor < SurfZ - RwgCaveInsideProbeCm)) { ++RwgRcCaveRejSurface; return false; } // P5-2 re-review: the floor must lie under the API surface
    if (!Q.CaveInsideAt(FVector(X, Y, Floor + RwgCaveInsideProbeCm))) { ++RwgRcCaveRejInside; return false; }
    if (!Q.CaveInsideAt(FVector(X, Y, Floor + RwgCaveHeadroomCmMirror))) { ++RwgRcCaveRejHeadroom; return false; }
    if (!Q.CaveInsideAt(FVector(X, Y, Floor + RwgCaveMinCeilingCmMirror))) { ++RwgRcCaveRejCeiling; return false; } // RWG P5 review F6 (was + 1100)
    double RiseRun = 0.0;
    if (RwgRejectV3(Q, FVector(X, Y, SurfZ)) != 0 || RwgApiSlopeTooSteep(Q, X, Y, RiseRun)) { ++RwgRcCaveRejSurface; return false; }
    if (bRwgKillboxKnown && Floor < RwgKillboxTopZ + RwgKillboxMarginCm) { ++RwgRcCaveRejKillbox; return false; }
    OutSpot = FVector(X, Y, Floor);
    if (RwgCaveDealSamples < 8)
    {
        ++RwgCaveDealSamples;
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGCAVEDEAL: sample=%d source=%s spot=(%.0f,%.0f) floorHintZ=%.0f floorZ=%.0f floorMinusHintCm=%.0f surfaceMinusFloorCm=%.0f anyNearCallsThisSpot=%d candidateIndex=%d"),
            RwgCaveDealSamples, Source, X, Y, Hint, Floor, Floor - Hint, SurfZ - Floor, AnyNearCalls, Index);
    }
    return true;
}
