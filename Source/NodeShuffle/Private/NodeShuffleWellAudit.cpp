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
// T19 (2026-08-08) -- THE RESOURCE TERM, AND WHY THIS FILE WAS THE REASON T17 WENT UNNOTICED.
// The line printed `res=` from E.AssignedResourceClassPath -- the LAYOUT -- while :53 already held the
// SPAWNED core, so the two could never be seen to disagree; and bHealthy was counts + positions +
// links with no resource term at all. The gate therefore printed `-- OK` through the whole pre-T17
// defect (a relocated well's spawned actors were never re-typed) and was structurally UNABLE to fail
// for that class. It now prints both values and gates on their agreement over the core AND every live
// spawned satellite -- a satellite left on the old resource is exactly as wrong as a core left on it,
// and exactly as invisible.
// `res=` still means the LAYOUT so committed review reports that grep it do not change meaning;
// `worldHolds=` is the new, deliberately un-greppable-as-`res=` token for what the world holds.
//
// A MEMBER function: it reads the private mCore through GetCore() and the core's registration array,
// and the private mResourceClassOverride for the pin's bCoreAlready term, all of which depend on this
// class's AccessTransformers Friend grants.

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

    // T19: resolve the layout's assignment ONCE, BEFORE the member loop, so each member's resource is
    // compared inside the loop that already resolves it -- no second pass and no second lookup. The
    // comparison is POINTER-to-UClass, against the very class SpawnWellGroup/the maintenance pass load
    // from this same field (NodeShuffleWellRelocateApply.cpp:444/:500), so the audit and the retype
    // agree by construction rather than by string formatting.
    // NULL means the assignment did not resolve: nothing is compared and it is reported as such below.
    UClass* const AssignedRes = LoadClassByPath(E.AssignedResourceClassPath);
    const bool bResourceUnknown = (AssignedRes == nullptr);
    // POPULATION, stated because a wrong SET passes every correctness gate: the resource denominator is
    // the LIVE SPAWNED ACTORS ONLY -- the core when IsValid, plus every satellite record that both
    // survived the !bCaptured skip below and resolved to a live actor. It EXCLUDES uncaptured records
    // (no actor was ever spawned for them; they are counted by uncaptured=) and dead handles (no actor
    // to hold a resource; they are counted by satellites=<spawned>). It is the same population
    // NodeShuffleWellSpawn.cpp:357-363 writes through, which is what makes the two comparable.
    int32 ResTested = 0, ResDisagree = 0;
    FString ResDisagreeOn = TEXT("<none>");
    bool bNamedDisagree = false;   // a flag, not a string compare, so the label can be anything
    TArray<AFGResourceNodeFrackingSatellite*> LiveSats;

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
        LiveSats.Add(Sat);
        // T19 SYMMETRY -- BOTH SIDES OF THE RELATIONSHIP. This project's most-repeated defect is a rule
        // applied to one side only, four sightings in one day (T16, ns-review-h2 F1, T8, T20). A
        // satellite left on the old resource is exactly as wrong as a core left on it and exactly as
        // invisible, so the check runs here, on the loop that already holds the actor, and again on the
        // core below. GetResourceClass() is what the game itself asks (override if set, else authored)
        // -- the same accessor RetypeWellMember reports its `Was` from.
        // Deliberately placed BEFORE the !IsValid(Core) bail below: a dead core must not stop the
        // satellites being resource-tested, or the check loses one side again.
        if (AssignedRes)
        {
            ++ResTested;
            if (Sat->GetResourceClass().Get() != AssignedRes)
            {
                ++ResDisagree;
                if (!bNamedDisagree) { ResDisagreeOn = WellShort(S.SatellitePath); bNamedDisagree = true; }
            }
        }
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

    // T19 -- THE CORE'S HALF OF THE RESOURCE TERM, and the value the line prints. Read through
    // GetResourceClass() (override if set, else authored) from the SPAWNED core -- never from the
    // hidden vanilla original, because the two can genuinely differ and that difference IS the defect.
    // The core is preferred as the named disagreeing member when it is one of them, since it is the
    // one a reader can identify without a path.
    const UClass* CoreHolds = IsValid(Core) ? Core->GetResourceClass().Get() : nullptr;
    if (AssignedRes && IsValid(Core))
    {
        ++ResTested;
        if (CoreHolds != AssignedRes)
        {
            // Overwrites any satellite label on purpose: the core is the member a reader can identify
            // without a path, and the log text says so rather than claiming this is the "first" one.
            ++ResDisagree; ResDisagreeOn = TEXT("<core>"); bNamedDisagree = true;
        }
    }

    // T19 -- WHY A DISAGREEMENT CAN BE HEALTHY, AND WHAT IS ACTUALLY TESTED HERE.
    // NodeShuffleWellSpawn.cpp:375-382 (T17) deliberately DECLINES to retype a spawned group that an
    // in-use signal fires on, so a pinned well legitimately and PERMANENTLY holds a resource other than
    // the layout's assignment. If that raised the alarm token, every pinned well would alarm forever --
    // exactly the false-alarm failure the ns-review-h3 H3 comment above warns about: a false alarm on
    // the loudest token is worse than no token, because it teaches the reader to ignore it.
    // So the pin is evaluated HERE, on the population this audit has already resolved, with the SAME
    // predicate the decline uses -- transcribed, not reinterpreted (see the two lines below).
    // MEASURED: an in-use signal on the live SPAWNED actors, right now. NOT MEASURED: whether a retype
    // was in fact attempted and declined -- this reads live state, not history -- nor what was built.
    // Evaluated only when there is a disagreement to explain, so a converged world pays nothing.
    FNodeShuffleWellPinCheck ResPin;
    bool bResourcePinned = false, bResourceMismatch = false;
    if (ResDisagree > 0)
    {
        ResPin = EvaluateWellPinOnActors(
            ENodeShuffleWellPinSource::Spawned, Core, LiveSats, E.Satellites.Num());
        // TRANSCRIBED VERBATIM from NodeShuffleWellSpawn.cpp:381-382 -- the same asymmetry
        // (ns-review-h1 W1, T16): a core-occupancy pin is ignored once the resource is already on that
        // core, while a satellite-extractor pin stands down unconditionally. bCoreAlready reads
        // mResourceClassOverride, as the decline does, NOT GetResourceClass().
        const bool bCoreAlready = IsValid(Core) && (Core->mResourceClassOverride.Get() == AssignedRes);
        // T19 review F3: the && IsValid(Core) mirrors the DECLINE'S OWN REACHABILITY GUARD
        // (NodeShuffleWellSpawn.cpp:363, `IsValid(Core) && StaleRes > 0`). With a dead core T17 never
        // evaluates this predicate, so the audit must not report a pin decision that was never taken --
        // a coreless group is already unhealthy and already carries *** NO LIVE CORE ***, and
        // resDisagree=/resDisagreeOn= on the main line still print the raw measurement.
        bResourcePinned   = IsValid(Core)
                            && ((ResPin.bCoreInUse && !bCoreAlready) || ResPin.bSatelliteInUse);
        bResourceMismatch = IsValid(Core) && !bResourcePinned;
    }

    // T19 review F1: THE RETYPE IS PROXIMITY-GATED AND THIS AUDIT IS NOT. The maintenance retype runs
    // inside SpawnWellGroup, called at NodeShuffleWellRelocateApply.cpp:501 only for a group that is
    // IsLocationNearAnyPlayer(PlacedCoreLocation, SpawnRadiusCm); this sweep walks EVERY placed group.
    // So after a shuffle that re-deals an already-placed well (the RerollRelocatedWells=OFF default),
    // every relocated well the player has not visited legitimately still holds the old resource, and
    // alarming on all of them would fire this file's loudest token ~15x per sweep on a CORRECT build.
    // MEASURED with the SAME predicate and the SAME radius that gate uses: WellLastApplySpawnRadiusCm
    // is written by that pass at :394, and the `<= 0` guard plus this call are the idiom
    // NodeShuffleWellClaim.cpp:497-505 already uses. NOT MEASURED: whether a retype was attempted --
    // only whether it COULD have been.
    // T19 O2 (2026-08-08) -- STRUCTURAL COUPLING, NOT A GUARD. The `> 0.0f` term above only decides
    // this bool; the RESOURCE MISMATCH line below prints WellLastApplySpawnRadiusCm unconditionally,
    // so an unwritten radius would print `within -1cm of a player` (initialiser: NodeShuffleSubsystem.h
    // :1581). MEASURED by call-site ordering, not by any check: the write at
    // NodeShuffleWellRelocateApply.cpp:394 precedes all three audit entry points (:486, :522, :525) and
    // every writer of SpawnedWellCores, all inside the same ApplyWellRelocation call, so no valid Core
    // can reach this audit with the radius unwritten. IF THE AUDIT IS EVER MOVED off that call --
    // onto a timer, a console command, or a load-time sweep -- `-1cm` becomes printable and this needs
    // a real guard on the log line, not just on the bool.
    const bool bRetypeReachable = (WellLastApplySpawnRadiusCm > 0.0f)
        && IsLocationNearAnyPlayer(E.PlacedCoreLocation, WellLastApplySpawnRadiusCm);

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
    // T19: the resource term joins the gate. A pin-explained disagreement is NOT a fault, so only
    // bResourceMismatch appears here -- bResourcePinned deliberately does not.
    const bool bHealthy = IsValid(Core) && (SpawnedCount == Expected) && (RegisteredCount == Expected)
                          && (ArrayLive == Expected) && (Expected > 0) && !bScattered && !bShortByDesign
                          && !bResourceMismatch;

    // T19: an if/else chain rather than the previous six-deep nested ternary, because this adds a
    // seventh arm and a nested TEXT() is one of the two build breaks this repo hit yesterday. Every
    // pre-existing verdict string is byte-identical to the one the ternary produced, and the order is
    // equivalent: each of the specific flags below already forces bHealthy false, so testing them
    // before the OK case cannot change which arm a pre-T19 state selected.
    // INVARIANT FOR ANYONE ADDING AN ARM: every `***` arm below must force bHealthy false, or a group
    // this gate considers healthy will print an alarm verdict. Check bHealthy's definition above first.
    const TCHAR* Verdict = TEXT("OK");
    bool bShortOrUnlinked = false;
    if (!IsValid(Core))
    {
        Verdict = TEXT("*** NO LIVE CORE -- if this persists after the world has streamed, the group is DEAD ***");
    }
    else if (bScattered)
    {
        Verdict = TEXT("*** GROUP SCATTERED -- a member is nowhere near where this entry says it is ***");
    }
    else if (bRateInflated)
    {
        Verdict = TEXT("*** RATE INFLATED: the core's LIVE satellite array is larger than this well should have ***");
    }
    else if (bResourceMismatch)
    {
        // Ranked above SHORT BY DESIGN deliberately: a member on the wrong resource changes what the
        // well PRODUCES, and both states still get their own dedicated token line below regardless.
        Verdict = TEXT("*** RESOURCE MISMATCH -- a live spawned member holds a resource other than the one ")
                  TEXT("this entry assigns, and the retype-decline predicate is FALSE on this group, so the ")
                  TEXT("pin does not explain it (read coreWhy=/satWhy= below before concluding nothing is ")
                  TEXT("built here) ***");
    }
    else if (bShortByDesign)
    {
        Verdict = TEXT("*** SHORT BY DESIGN -- uncaptured records exist; their vanilla twins are suppressed but no relocated twin was ever spawned ***");
    }
    else if (!bHealthy)
    {
        bShortOrUnlinked = true;
        Verdict = TEXT("*** SHORT OR UNLINKED -- the Pressurizer will under-report this well; mCore is not ")
                  TEXT("SaveGame, so an unlinked satellite produces NO crash and NO error anywhere ***");
    }
    else if (bResourceUnknown)
    {
        // T19 review F5: still `-- OK` so existing greps keep matching, with the caveat appended. This
        // does NOT feed bHealthy -- that would change what the gate gates for a defect class T19 never
        // scoped -- but the GATE LINE must not read as a clean pass while its resource term did not run.
        Verdict = TEXT("OK -- BUT THE RESOURCE TERM DID NOT RUN ON THIS GROUP: its assigned resource ")
                  TEXT("path did not resolve to a class, so resDisagree=0/0 means 'nothing was ")
                  TEXT("compared', NOT 'everything agreed'. See the RESOURCE NOT COMPARED line below");
    }
    else if (bResourcePinned)
    {
        // Still `-- OK` so the existing greps keep matching, with the reason appended -- otherwise a
        // reader sees a non-zero resDisagree beside a bare OK and has to guess.
        Verdict = TEXT("OK -- RESOURCE PINNED BY USE: the disagreement below is explained by a live in-use ")
                  TEXT("signal on this group, which is the same predicate that declines the retype");
    }

    UE_LOG(LogNodeShuffle, Display,
        // ns-t27-corefirst: coreYaw= was yaw= and read E.GroupYawDeg, which T27 pins to 0.0 for every
        // group it places. It now reports the spawned core's own yaw, which is what a reader auditing
        // a placed well was always looking for.
        TEXT("WELL [%s] guid=%s core=%s res=%s worldHolds=%s resDisagree=%d/%d resDisagreeOn='%s' coreYaw=%.1f ")
        TEXT("satellites=%d/%d/%d (expected/spawned/registered) coreArray=%d live (%d raw, %d stale) ")
        TEXT("uncaptured=%d maxMemberDrift=%.0fcm (%s) flagMismatch=%d at=%s -- %s"),
        Phase, *WellShort(E.CorePath), IsValid(Core) ? *Core->GetName() : TEXT("<NO CORE>"),
        // res= is the LAYOUT's assignment, unchanged in meaning since H2. worldHolds= is the SPAWNED
        // core's live resource; resDisagree= counts live spawned members against the live members
        // TESTED, so a zero always arrives with the number of chances it had to be non-zero.
        *WellShort(E.AssignedResourceClassPath), *WellShort(WellPathOf(CoreHolds)),
        ResDisagree, ResTested, *ResDisagreeOn, E.PlacedCoreRotation.Yaw,
        Expected, SpawnedCount, RegisteredCount, ArrayLive, ArrayRaw, ArrayStale, Uncaptured,
        MaxDrift, *DriftWho, FlagDisagreements, *E.PlacedCoreLocation.ToCompactString(),
        Verdict);

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

    // T19: the resource tokens. The alarm and the not-an-alarm are DIFFERENT tokens at DIFFERENT
    // verbosities on purpose -- a pinned well is healthy and must never train the reader to skip the
    // loud one.
    if (bResourceMismatch)
    {
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELL [%s] core=%s *** RESOURCE MISMATCH: %d of %d live spawned member(s) tested hold a ")
            TEXT("resource other than the assigned '%s' -- the spawned core holds '%s', and one named ")
            TEXT("disagreeing member is '%s' (the core whenever the core is one of them, otherwise the ")
            TEXT("first satellite found; it is A disagreeing member, not necessarily the only one). ")
            TEXT("MEASURED: GetResourceClass() on each live spawned member of THIS group, ")
            TEXT("and the RETYPE-DECLINE predicate -- (coreInUse AND the core does not already hold the ")
            TEXT("assigned resource) OR satelliteInUse -- evaluated FALSE on it. A CORE in-use signal can ")
            TEXT("still be firing and be ignored by that predicate, so read coreWhy=/satWhy= rather than ")
            TEXT("inferring nothing is built here (resolvedAgainst=%s, tested %d core ")
            TEXT("+ %d/%d listed satellite record(s); coreWhy='%s' satWhy='%s'). ALSO MEASURED: ")
            TEXT("retypeReachable=%d -- the maintenance retype (NodeShuffleWellRelocateApply.cpp:501) ")
            TEXT("runs only for a group within %.0fcm of a player, and this reports whether ANY PLAYER IS ")
            TEXT("WITHIN THAT RADIUS AS OF THIS SWEEP (1=yes, 0=no). ")
            TEXT("NOT MEASURED: whether a player was near it on any earlier pass, and whether the ")
            TEXT("maintenance retype actually ran, attempted a write, or failed on this group -- this ")
            TEXT("field is a proximity test taken at audit time, not a record of the retype. (This ")
            TEXT("sentence deliberately names no literal value for that field, so a grep for one ")
            TEXT("matches only real occurrences.) ")
            TEXT("NOT MEASURED: why they disagree -- reuse, adopt-late and a re-roll under an ")
            TEXT("already-placed group all reach this state and this line tests none of them. ***"),
            Phase, *WellShort(E.CorePath), ResDisagree, ResTested,
            *WellShort(E.AssignedResourceClassPath), *WellShort(WellPathOf(CoreHolds)), *ResDisagreeOn,
            WellPinSourceName(ResPin.Source), ResPin.CoresTested, ResPin.SatellitesTested,
            ResPin.SatellitesExpected, ResPin.CoreWhy, ResPin.SatelliteWhy,
            bRetypeReachable ? 1 : 0, WellLastApplySpawnRadiusCm);
    }
    if (bResourcePinned)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELL [%s] core=%s RESOURCE PINNED BY USE (this DISAGREEMENT is not a fault; this line ")
            TEXT("says nothing about the group's overall verdict -- read the WELL [ line above for that): ")
            TEXT("%d of %d live spawned ")
            TEXT("member(s) tested hold a resource other than the assigned '%s' -- the spawned core holds ")
            TEXT("'%s', and one named disagreeing member is '%s' (the core whenever the core is one of ")
            TEXT("them, otherwise the first satellite found). MEASURED: the in-use pin predicate is TRUE ")
            TEXT("on this group's live spawned actors (resolvedAgainst=%s, tested %d core + %d/%d ")
            TEXT("listed satellite record(s); coreWhy='%s' satWhy='%s' firedOn='%s'), which is the same predicate ")
            TEXT("NodeShuffleWellSpawn.cpp declines the retype on. NOT MEASURED: whether a retype was in ")
            TEXT("fact attempted for this group, or what was built on it -- this reads live state, not ")
            TEXT("history."),
            Phase, *WellShort(E.CorePath), ResDisagree, ResTested,
            *WellShort(E.AssignedResourceClassPath), *WellShort(WellPathOf(CoreHolds)), *ResDisagreeOn,
            WellPinSourceName(ResPin.Source), ResPin.CoresTested, ResPin.SatellitesTested,
            ResPin.SatellitesExpected, ResPin.CoreWhy, ResPin.SatelliteWhy,
            ResPin.FiredActorName.IsEmpty() ? TEXT("<none>") : *ResPin.FiredActorName);
    }
    if (bResourceUnknown)
    {
        // Expected to be rare rather than impossible: a group only reaches bGroupPlaced after this same
        // path loaded and type-checked the class (NodeShuffleWellRelocateApply.cpp:444-445), so this
        // fires only if the assignment CHANGED to an unresolvable path after placement, or the class
        // went away. Reported loudly anyway, because while it is true the resource term is silently
        // vacuous over this group -- which is the very shape of defect T19 exists to remove.
        // The counts are printed from THIS run, not asserted, so the reader sees the real denominator.
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELL [%s] core=%s *** RESOURCE NOT COMPARED: this entry's assigned resource path '%s' ")
            TEXT("did not resolve to a class, so no member was tested -- this group contributed %d ")
            TEXT("disagreement(s) out of %d member(s) tested, and that zero denominator means 'no test ")
            TEXT("ran', NOT 'agreement'. This alone does not mark the group unhealthy. MEASURED: ")
            TEXT("LoadClassByPath returned null for that path. ***"),
            Phase, *WellShort(E.CorePath),
            E.AssignedResourceClassPath.IsEmpty() ? TEXT("<empty>") : *E.AssignedResourceClassPath,
            ResDisagree, ResTested);
    }

    FWellAuditVerdict V;
    V.bHealthy = bHealthy;
    V.bRateInflated = bRateInflated;
    V.bScattered = bScattered;
    V.bShortByDesign = bShortByDesign;
    V.bNoCore = !IsValid(Core);
    V.bShortOrUnlinked = bShortOrUnlinked;
    V.bResourceMismatch = bResourceMismatch;
    V.bResourcePinned = bResourcePinned;
    V.bResourceUnknown = bResourceUnknown;
    return V;
}

void ANodeShuffleSubsystem::AuditWellGroupLinks(const TCHAR* Phase)
{
    int32 Groups = 0, Healthy = 0, Broken = 0, Inflated = 0, Scattered = 0, ShortByDesign = 0, NoCore = 0;
    // T19: totalled from the per-group verdict like every counter here -- NOT re-derived.
    int32 ResMismatch = 0, ResPinned = 0, ResUnknown = 0, ResPinnedAndHealthy = 0, ShortOrUnlinked = 0;

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
        if (V.bShortOrUnlinked) { ++ShortOrUnlinked; }
        if (V.bResourceMismatch) { ++ResMismatch; }
        if (V.bResourcePinned) { ++ResPinned; }
        // T19 review F2: bResourcePinned only says the DISAGREEMENT is explained. A pinned group can
        // still be not-OK for an unrelated reason (short by design, scattered, no live core), so the
        // containment is MEASURED here rather than asserted in the log text.
        if (V.bResourcePinned && V.bHealthy) { ++ResPinnedAndHealthy; }
        if (V.bResourceUnknown) { ++ResUnknown; }
    }

    if (Groups > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-AUDIT [%s]: %d relocated group(s) -- %d fully linked (this total is THE GATE ")
            TEXT("VERDICT: fully linked AND not scattered AND not short by design AND no unexplained ")
            TEXT("resource mismatch -- it is not a link-only count), %d not OK. The not-OK breakdown is ")
            TEXT("a set of FLAGS THAT OVERLAP, not a partition, so it can sum to more than the not-OK ")
            TEXT("count -- but every not-OK group carries at least one of them, so it can never sum to ")
            TEXT("less: %d no live core, %d SCATTERED, %d RATE-INFLATED, %d RESOURCE ")
            TEXT("MISMATCH, %d SHORT BY DESIGN, %d SHORT OR UNLINKED. Separately, ")
            TEXT("%d group(s) hold a resource other than their ")
            TEXT("assignment WITH a live in-use signal that explains it -- that DISAGREEMENT is not a ")
            TEXT("fault, but such a group can still be not-OK for an unrelated reason, so this count is ")
            TEXT("NOT a subset of either total: %d of them are in the fully-linked total. And %d ")
            TEXT("group(s) had an assignment that did not resolve to a class, so no resource comparison ")
            TEXT("ran on them at all. This is H2's acceptance gate (design R1 / H0's LINK AGREEMENT ")
            TEXT("baseline of inAonly=0 inBonly=0 dupReg=0 across 20 vanilla wells). It counts the core's ")
            TEXT("own LIVE satellite array (ns-review-h2 F4, h3 H3), each member's DISTANCE from where ")
            TEXT("this entry says it is (h3 H1), and the resource every live spawned member holds against ")
            TEXT("the one this entry assigns (T19) -- without the first a rate-doubled well printed OK, ")
            TEXT("without the second a well whose members are kilometres apart printed OK too, and ")
            TEXT("without the third a well extracting the WRONG RESOURCE printed OK for the whole of T17. ")
            TEXT("Every number here is a total of the per-group verdicts above; this sweep derives none ")
            TEXT("of them."),
            Phase, Groups, Healthy, Broken, NoCore, Scattered, Inflated, ResMismatch, ShortByDesign, ShortOrUnlinked,
            ResPinned, ResPinnedAndHealthy, ResUnknown);
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
