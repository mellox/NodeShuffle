// Copyright. NodeShuffle packet rwg-logflood-1 (2026-11-01). See NodeShuffleWellMeshIndexDump.h for what this file is for.
//
// WHAT CHANGED AND WHAT DID NOT. The two per-entry Verbose lines below are the SAME format strings, the SAME argument order and
// the SAME gate (diagnostics on AND Verbose active) that RebuildWellMeshIndex printed inline before this packet. What changed is
// WHEN they print: the first rebuild in a UWorld, and every later rebuild whose signature differs from the last one printed.
// A rebuild whose signature matches prints only the census line. The NARROWED / WIDENED / BYSTANDER Display lines are not
// touched by this packet: they were already said once per mesh through WellVisualCaptureLogged.
//
// THE SIGNATURE covers every printed field of every entry EXCEPT the world location and the radius (a constexpr, identical on every line). The
// location is left out because the actor name identifies the piece. Distances are hashed as FMath::RoundToInt (half up); %.0f may round an exact .5 the other way.
// FNV-1a over the field bytes, written out here so this file reaches NO engine hash symbol (no new import). A 64-bit collision
// between two different dumps would skip one re-dump; the census line still prints that rebuild's own counts.
//
// IMPORT DISCIPLINE (memory:sf-shipping-export-trap): the prediction is NO new imported symbol. TWeakObjectPtr<const UWorld> and
// HasSameIndexAndSerialNumber are already reached by NodeShuffleWellSnapBox.cpp in this module; UE_LOG_ACTIVE and FMath are
// inline. That is a prediction, not a measurement -- check_imports.ps1 after the build is the measurement.

#include "NodeShuffleWellMeshIndexDump.h"

#include "NodeShuffleSubsystem.h"
#include "NodeShuffle.h"

#include "Engine/World.h"

namespace
{
    constexpr uint64 NsFnvOffset = 14695981039346656037ull;
    constexpr uint64 NsFnvPrime  = 1099511628211ull;

    uint64 NsFnvU64(uint64 H, uint64 V)
    {
        for (int32 i = 0; i < 8; ++i) { H ^= (V & 0xffull); H *= NsFnvPrime; V >>= 8; }
        return H;
    }
    uint64 NsFnvStr(uint64 H, const FString& S)
    {
        for (const TCHAR* P = *S; *P; ++P) { H = NsFnvU64(H, static_cast<uint64>(*P)); }
        return NsFnvU64(H, 0xffffull); // field separator: "ab"+"c" and "a"+"bc" hash differently
    }
}

void FNodeShuffleWellIndexSpatialDump::Add(bool bInAccepted, int32 InMeshType, const FString& InMeshName, const FString& InActorName,
    const FString& InMemberShort, const FVector& InLoc, float InDistCm, float InRunnerUpCm, bool bInInRadius)
{
    FNodeShuffleWellIndexSpatialEntry& E = Entries.AddDefaulted_GetRef();
    E.bAccepted = bInAccepted;
    E.MeshType = InMeshType;
    E.MeshName = InMeshName;
    E.ActorName = InActorName;
    E.MemberShort = InMemberShort;
    E.Loc = InLoc;
    E.DistCm = InDistCm;
    E.RunnerUpCm = InRunnerUpCm;
    E.bInRadius = bInInRadius;
    if (bInAccepted) { ++Accepted; } else { ++Rejected; }

    uint64 H = NsFnvOffset;
    H = NsFnvU64(H, bInAccepted ? 1ull : 2ull);
    H = NsFnvU64(H, static_cast<uint64>(static_cast<uint32>(InMeshType)));
    H = NsFnvStr(H, InMeshName);
    H = NsFnvStr(H, InActorName);
    H = NsFnvStr(H, InMemberShort);
    H = NsFnvU64(H, static_cast<uint64>(static_cast<uint32>(FMath::RoundToInt(InDistCm))));
    H = NsFnvU64(H, static_cast<uint64>(static_cast<uint32>(FMath::RoundToInt(InRunnerUpCm))));
    H = NsFnvU64(H, bInInRadius ? 1ull : 0ull);
    SigSum += H;
}

uint64 FNodeShuffleWellIndexSpatialDump::Signature() const
{
    return NsFnvU64(SigSum, static_cast<uint64>(Entries.Num()));
}

// Forget the last printed signature when the UWorld changes, so the first rebuild of a fresh load prints the full dump.
// index+serial compare, NOT an address compare (same reason as ResetWellSnapBoxDiagForWorld: a recycled slot is not the same
// world). Returns true when it reset; the caller prints that on the census. Ungated and idempotent.
bool ANodeShuffleSubsystem::ResetWellIndexDumpForWorld(const UWorld* World)
{
    const TWeakObjectPtr<const UWorld> Incoming(World);
    if (WellIndexDumpWorld.HasSameIndexAndSerialNumber(Incoming)) { return false; }
    WellIndexDumpWorld = Incoming;
    WellIndexDumpSig = 0;
    WellIndexDumpEntries = -1;
    WellIndexDumpSkipped = 0;
    return true;
}

// Called by RebuildWellMeshIndex only when D.bActive. Census first, then the per-entry lines if this rebuild dumps, so the census
// is the line that states sigChanged and the previous dump's entry count ahead of a re-dump.
void ANodeShuffleSubsystem::FlushWellIndexSpatialDump(const FNodeShuffleWellIndexSpatialDump& D)
{
    const uint64 Sig = D.Signature();
    const bool bHasPrev = WellIndexDumpEntries >= 0;
    const bool bSigChanged = bHasPrev && Sig != WellIndexDumpSig;
    const bool bDumping = !bHasPrev || bSigChanged;
    if (!bDumping) { ++WellIndexDumpSkipped; }
    UE_LOG(LogNodeShuffle, Verbose,
        TEXT("WELLH2B-INDEX spatial CENSUS: pass=%d entries=%d accepted=%d rejected=%d narrowedByType=%d widenedByType=%d bystanderRejects=%d worldReset=%d sigChanged=%d dumping=%d prevEntries=%d skippedSinceDump=%d sig=%08x%08x"),
        D.Pass, D.Entries.Num(), D.Accepted, D.Rejected, D.NarrowedByType, D.WidenedByType, D.BystanderRejects,
        D.bWorldReset ? 1 : 0, bSigChanged ? 1 : 0, bDumping ? 1 : 0, WellIndexDumpEntries, WellIndexDumpSkipped,
        static_cast<uint32>(Sig >> 32), static_cast<uint32>(Sig & 0xffffffffull));
    if (!bDumping) { return; }
    WellIndexDumpSig = Sig;
    WellIndexDumpEntries = D.Entries.Num();
    WellIndexDumpSkipped = 0;
    for (const FNodeShuffleWellIndexSpatialEntry& E : D.Entries)
    {
        if (E.bAccepted)
        {
            UE_LOG(LogNodeShuffle, Verbose,
                TEXT("WELLH2B-INDEX spatial: '%s' (nodeMeshType=%d) on actor '%s' at %s -> member ")
                TEXT("'%s' at %.0f cm (runner-up %.0f cm, radius %.0f cm). Nearest-wins; a runner-up ")
                TEXT("close to the winner means the H0 spacing measurement no longer holds. ")
                TEXT("nodeMeshType is ENodeMeshType: 1=MT_Core 2=MT_Crack 3=MT_Satellite ")
                TEXT("4=MT_DesertCore 5=MT_DesertCrack 6=MT_DesertSatellite."),
                *E.MeshName, E.MeshType, *E.ActorName, *E.Loc.ToCompactString(),
                *E.MemberShort,
                E.DistCm, E.RunnerUpCm,
                D.RadiusCm);
        }
        else
        {
            UE_LOG(LogNodeShuffle, Verbose,
                TEXT("WELLH2B-INDEX spatial REJECT: '%s' (nodeMeshType=%d) on '%s' at %s -- nearest well ")
                TEXT("member '%s' is %.0f cm away, radius is %.0f cm, inRadius=%d. The type gate ")
                TEXT("accepted this piece and this pass did not claim it; the fields above are what ")
                TEXT("was measured, classify from them. A piece the TYPE gate rejected appears on a ")
                TEXT("NARROWED line instead, one it newly ADMITTED on a WIDENED line. If well pieces ")
                TEXT("are being missed, read all three."),
                *E.MeshName, E.MeshType, *E.ActorName, *E.Loc.ToCompactString(),
                *E.MemberShort, E.DistCm, D.RadiusCm, E.bInRadius ? 1 : 0);
        }
    }
}
