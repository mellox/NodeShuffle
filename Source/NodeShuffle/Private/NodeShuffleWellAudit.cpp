// Packet H2 (ns-wells-h2, branch feature/extractor-automatch): THE ACCEPTANCE GATE.
//
// Split out of NodeShuffleWellLink.cpp for the 500-line limit; the seam is real, because this is the
// only file in the packet whose job is to DISBELIEVE the rest of it. Everything else here tries to
// place and link a well; this reads the world back and says whether it worked.
//
// WHY IT MATTERS MORE THAN A LOG LINE USUALLY DOES. A relocated well fails SILENTLY: mCore is not
// SaveGame and not replicated, so an unlinked satellite produces no crash, no error, and no visible
// symptom -- the Pressurizer just quietly reports fewer satellites. There is no other detector. So
// this line IS the gate, and design §Q3 point 5 specifies its shape:
//     WELL guid=<g> core=<name> res=<r> yaw=<deg> satellites=<expected>/<spawned>/<registered>
// printed for EVERY placed group whether healthy or not, because a well that is fine must be visibly
// fine or the one that is not will not stand out.
//
// ns-review-h2 F4 -- WHAT THE COLD REVIEW FOUND, AND WHY IT WAS THE CENTRAL FINDING. The verdict
// ignored coreArray entirely, so `satellites=7/7/7 ... coreArray=14 ... OK` was a printable line: a
// well whose extraction rate is DOUBLE what it should be, reported as fully linked. That one omission
// is what let three independent input paths each produce a broken well the gate called healthy. A gate
// that cannot fail is not a gate. The core's own satellite array size is now part of the verdict and
// has its own greppable token.
//
// ns-review-h2 F12 -- CADENCE. Auditing once at a fixed pass (~40 s after load) covered exactly the
// wells that were already placed at load. Relocation is spawn-on-discovery, so the wells a tester
// actually flies to are placed minutes or hours later and were never audited at all. There are now
// three triggers: the instant a group is placed, the settled sweep, and a slow repeating sweep.
//
// A MEMBER function: it reads the private mCore through GetCore() and the core's registration array,
// which depend on this class's AccessTransformers Friend grants.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleWellCensus.h"   // AFGResourceNodeFrackingCore / ...Satellite
#include "NodeShuffleWellRetype.h"   // WellShort
// ns-review-h3 H1: IsFiniteVector for the drift measurement, and WellAdoptMatchRadiusCm -- the ONE
// identity tolerance, shared with adoption and the stale-handle guard so the audit's *** GROUP
// SCATTERED *** threshold is by construction the same number that decides "is this the same actor".
#include "NodeShuffleWellRelocate.h"

// ------------------------------------------------------------------------------------------------
// THE AUDIT -- design §Q3 point 5
// ------------------------------------------------------------------------------------------------
// "Log at both ends every session: WELL guid=<g> core=<name> res=<r> yaw=<deg>
//  satellites=<expected>/<spawned>/<registered>. A shrunk or unlinked well must be impossible to miss
//  in one log read." Printed for EVERY placed group whether or not it is healthy: a well that is fine
//  must be visibly fine, or the one that is not will not stand out.
// ns-review-h2 F12: ONE group, audited on demand. Called the instant a group is placed, as well as
// from the two sweeps -- relocation is spawn-on-discovery, so under the old fixed-pass-8-only rule the
// wells a tester actually flies to (placed minutes or hours after load) were NEVER audited at all.
ANodeShuffleSubsystem::FWellAuditVerdict
ANodeShuffleSubsystem::AuditOneWellGroup(const FNodeShuffleWellEntry& E, const TCHAR* Phase)
{
    AFGResourceNodeFrackingCore* Core = SpawnedWellCores.FindRef(E.CorePath);
    const int32 Expected = ExpectedRelocatedSatelliteCount(E);
    const int32 Uncaptured = E.Satellites.Num() - Expected;
    int32 SpawnedCount = 0, RegisteredCount = 0, FlagDisagreements = 0;

    // ns-review-h3 H1: the position term. Every count could match while the members sat kilometres
    // apart, because the verdict had no idea where anything was. maxMemberDrift is the largest
    // distance between a live member and the coordinate this entry says it should occupy.
    double MaxDrift = 0.0;
    FString DriftWho = TEXT("<none>");
    const auto NoteDrift = [&](const AActor* A, const FVector& Target, const FString& Label)
    {
        if (!IsValid(A) || !IsFiniteVector(Target)) { return; }
        const double D = FVector::Dist(A->GetActorLocation(), Target);
        if (D > MaxDrift) { MaxDrift = D; DriftWho = Label; }
    };
    NoteDrift(Core, E.PlacedCoreLocation, FString(TEXT("<core>")));

    for (const FNodeShuffleWellSatellite& S : E.Satellites)
    {
        if (!S.bCaptured) { continue; } // never spawned by design; counted separately, not as missing
        AFGResourceNodeFrackingSatellite* Sat = SpawnedWellSatellites.FindRef(S.SatellitePath);
        // ns-review-h3 H9: bPlaced now has exactly one meaning -- "a relocated actor exists for this
        // record" -- and this is its reader. A record claiming placement with no live actor (or the
        // reverse) means the bookkeeping and the world disagree, which is worth seeing on its own.
        if (S.bPlaced != IsValid(Sat)) { ++FlagDisagreements; }
        if (!IsValid(Sat)) { continue; }
        ++SpawnedCount;
        NoteDrift(Sat, S.PlacedLocation, WellShort(S.SatellitePath));
        if (!IsValid(Core)) { continue; }
        if (Sat->GetCore().Get() != Core) { continue; }
        for (const TWeakObjectPtr<AFGResourceNodeFrackingSatellite>& W : Core->Native_GetSatellites())
        {
            if (W.Get() == Sat) { ++RegisteredCount; break; }
        }
    }

    // ns-review-h3 H3: COUNT ONLY LIVE ENTRIES FOR THE VERDICT. Native_GetSatellites() is a raw
    // TArray<TWeakObjectPtr> that nothing prunes -- this file's own EnsureSatelliteLinked counts stale
    // entries and says so -- so a dead weak pointer inflated the raw Num() and could fire *** RATE
    // INFLATED *** on a perfectly healthy well. A false alarm on the packet's loudest token is worse
    // than no token: it teaches the reader to ignore it. Raw and stale are still both reported, since
    // a growing stale count is itself informative.
    int32 ArrayLive = 0, ArrayStale = 0, ArrayRaw = 0;
    if (IsValid(Core))
    {
        const TArray<TWeakObjectPtr<AFGResourceNodeFrackingSatellite>>& Sats = Core->Native_GetSatellites();
        ArrayRaw = Sats.Num();
        for (const TWeakObjectPtr<AFGResourceNodeFrackingSatellite>& W : Sats)
        {
            if (W.IsValid()) { ++ArrayLive; } else { ++ArrayStale; }
        }
    }

    // ns-review-h2 F4 (HIGH) -- THE ACCEPTANCE GATE MUST BE ABLE TO FAIL.
    // bHealthy previously ignored the core's array entirely, so `satellites=7/7/7 ... coreArray=14 ...
    // OK` was a printable line: a well whose extraction rate is DOUBLE what it should be, reported as
    // fully linked. That single omission is what made the double-registration hazard silent.
    const bool bRateInflated = IsValid(Core) && (ArrayLive > Expected);
    // ns-review-h3 H1: a scattered group is a distinct failure from a short or unlinked one, and it is
    // the only one that a purely count-based gate can never see.
    const bool bScattered = (MaxDrift >= WellAdoptMatchRadiusCm);
    // ns-review-h3 H5: a group carrying uncaptured records is PERMANENTLY SHORT -- their vanilla twins
    // are suppressed but no relocated twin is ever spawned for them -- so it must not print OK. It is
    // not the same failure as a broken link, so it gets its own token rather than being lumped in.
    const bool bShortByDesign = (Uncaptured > 0);
    const bool bHealthy = IsValid(Core) && (SpawnedCount == Expected) && (RegisteredCount == Expected)
                          && (ArrayLive == Expected) && (Expected > 0) && !bScattered && !bShortByDesign;

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELL [%s] guid=%s core=%s res=%s yaw=%.1f satellites=%d/%d/%d (expected/spawned/")
        TEXT("registered) coreArray=%d live (%d raw, %d stale) uncaptured=%d maxMemberDrift=%.0fcm (%s) ")
        TEXT("flagMismatch=%d at=%s -- %s"),
        Phase, *WellShort(E.CorePath), IsValid(Core) ? *Core->GetName() : TEXT("<NO CORE>"),
        *WellShort(E.AssignedResourceClassPath), E.GroupYawDeg,
        Expected, SpawnedCount, RegisteredCount, ArrayLive, ArrayRaw, ArrayStale, Uncaptured,
        MaxDrift, *DriftWho, FlagDisagreements, *E.PlacedCoreLocation.ToCompactString(),
        bHealthy
            ? TEXT("OK")
            : (!IsValid(Core)
                ? TEXT("*** NO LIVE CORE -- if this persists after the world has streamed, the group is DEAD ***")
                : (bScattered
                    ? TEXT("*** GROUP SCATTERED -- a member is nowhere near where this entry says it is ***")
                    : (bRateInflated
                        ? TEXT("*** RATE INFLATED: the core's LIVE satellite array is larger than this well should have ***")
                        : (bShortByDesign
                            ? TEXT("*** SHORT BY DESIGN -- uncaptured records exist; their vanilla twins are suppressed but no relocated twin was ever spawned ***")
                            : TEXT("*** SHORT OR UNLINKED -- the Pressurizer will under-report this well; mCore is not ")
                              TEXT("SaveGame, so an unlinked satellite produces NO crash and NO error anywhere ***"))))));

    // The distinct greppable tokens, each on its own line so they survive any rewording of the verdict.
    if (bScattered)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELL [%s] core=%s *** GROUP SCATTERED: maxMemberDrift=%.0fcm on '%s' (tolerance %.0fcm) ***"),
            Phase, *WellShort(E.CorePath), MaxDrift, *DriftWho, WellAdoptMatchRadiusCm);
    }
    if (bRateInflated)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELL [%s] core=%s *** RATE INFLATED: coreArray=%d > expected=%d ***"),
            Phase, *WellShort(E.CorePath), ArrayLive, Expected);
    }
    if (bShortByDesign)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELL [%s] core=%s *** SHORT BY DESIGN (uncaptured=%d) ***"),
            Phase, *WellShort(E.CorePath), Uncaptured);
    }

    FWellAuditVerdict V;
    V.bHealthy = bHealthy;
    V.bRateInflated = bRateInflated;
    V.bScattered = bScattered;
    V.bShortByDesign = bShortByDesign;
    V.bNoCore = !IsValid(Core);
    return V;
}

void ANodeShuffleSubsystem::AuditWellGroupLinks(const TCHAR* Phase)
{
    int32 Groups = 0, Healthy = 0, Broken = 0, Inflated = 0, Scattered = 0, ShortByDesign = 0, NoCore = 0;

    // The sweep TOTALS the per-group verdicts and derives nothing of its own. It used to recompute the
    // whole health rule -- two copies of the one predicate that decides whether this packet's silent
    // failure is visible at all, which is the exact shape of the two-ladders bug (ns-review-h2 F3).
    for (const FNodeShuffleWellEntry& E : WellLayout)
    {
        if (!E.bGroupPlaced) { continue; }
        ++Groups;
        const FWellAuditVerdict V = AuditOneWellGroup(E, Phase);
        if (V.bHealthy) { ++Healthy; } else { ++Broken; }
        if (V.bRateInflated) { ++Inflated; }
        if (V.bScattered) { ++Scattered; }
        if (V.bShortByDesign) { ++ShortByDesign; }
        if (V.bNoCore) { ++NoCore; }
    }

    if (Groups > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-AUDIT [%s]: %d relocated group(s) -- %d fully linked, %d not OK (%d no live core, ")
            TEXT("%d SCATTERED, %d RATE-INFLATED, %d SHORT BY DESIGN). This is H2's acceptance gate ")
            TEXT("(design R1 / H0's LINK AGREEMENT baseline of inAonly=0 inBonly=0 dupReg=0 across 20 ")
            TEXT("vanilla wells). It counts the core's own LIVE satellite array (ns-review-h2 F4, h3 H3) ")
            TEXT("and each member's DISTANCE from where this entry says it is (h3 H1) -- without the first ")
            TEXT("a rate-doubled well printed OK, and without the second a well whose members are ")
            TEXT("kilometres apart printed OK too."),
            Phase, Groups, Healthy, Broken, NoCore, Scattered, Inflated, ShortByDesign);
    }

    // ns-review-h3 H2: THE ADOPTION VERDICT BELONGS HERE, NOT IN AdoptRestoredWellGroups.
    // The F6 fix moved a never-fires alarm to an always-fires one: `CoresAdopted < ExpectedGroups` was
    // evaluated ~5 s after load, when almost nothing has streamed, so "NOT ONE runtime fracking core
    // exists" fired on essentially every load -- the same defect as the original, in the opposite
    // direction. This audit is the only point where "did we get back what we placed" is answerable,
    // because it runs on a settled world and again on a slow cadence.
    if (Groups > 0 && NoCore > 0)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-AUDIT [%s]: %d of %d relocated group(s) still have NO LIVE CORE at this point. ")
            TEXT("Near a load this can simply mean 'not streamed yet' and the lazy adoption will pick them ")
            TEXT("up; if it persists across the cadence sweeps, our spawned wells were NOT save-collected ")
            TEXT("and those wells are gone. Compare with the WELLH2-ADOPT counters from load time."),
            Phase, NoCore, Groups);
    }
}
