// ns-t39-wellprobe: `NodeShuffle.WellProbe` -- THE PER-MEMBER ENCLOSURE PROBE.
//
// WHY THIS EXISTS, AND WHY THE TWO EXISTING PROBES CANNOT ANSWER IT. docs/TECH-DEBT.md carries an AUTHOR
// RULING (2026-08-09, on T34/T37): a well member PARTLY embedded in terrain is WANTED, and the only case
// that is a defect is a member FULLY INSIDE a rock. Neither existing probe can be run at such a member:
//   * NodeShuffle.Here tests the point the player is STANDING on, and nobody can stand inside a rock.
//   * NodeShuffle.PointAtHere tests where an aim ray TERMINATES, and a ray aimed at a fully buried member
//     stops on the rock face in front of it (T38 cold review) -- it then measures that surface's
//     neighbourhood rather than the member's.
// A coordinate-argument command was rejected by the author for a stated reason: the coordinates of a spot
// nobody can reach are not knowable to type in. So this command takes NO arguments and reads each member's
// location from what the mod already holds.
//
// WHAT IT DOES. Finds the nearest PLACED relocated well group to the player -- through the SAME
// FindNearestPlacedWellForDiag that NodeShuffle.Here's nearest-well line calls, not a second copy -- and
// then, for the core and every satellite, resolves that member's OWN location, NAMES THE SOURCE it came
// from, and runs the SAME ANodeShuffleSubsystem::IsSpotEnclosed both placement paths call, with the same
// optional recorder and the same pawn on the ignore list NodeShuffle.Here already passes.
//
// AN UNRESOLVABLE MEMBER IS COUNTED AND NAMED, NEVER SKIPPED. A silently shrunk population reads as a
// clean result, which is this project's most expensive repeated defect, so the group summary carries the
// denominators: how many members were probed of how many the group has, which sources they resolved from,
// and how many the predicate refused.
//
// IT CHANGES NO PLACEMENT BEHAVIOUR. IsSpotEnclosed's body, parameters, constants, ray count, threshold,
// radius, channel and eye height are untouched, and its two placement call sites stay textually unchanged
// and 3-arg. Nothing here spawns, moves, retypes or writes any layout field.
//
// WHY THE FILE AND THE FUNCTION ARE NOT CALLED `WellProbe`. NodeShuffleWellStage0.h already declares a
// FREE function `LogWellProbeCensus` -- the placement-probe GATE census, a different instrument -- and
// members of this class call it unqualified. A member of that name hides it at those call sites; the
// first build of this packet proved it with C2660 at NodeShuffleWellRelocateApply.cpp's 10-argument
// call. The console command the author asked for keeps the name `NodeShuffle.WellProbe`; only the C++
// symbols carry `Member`, which is what this one iterates.
//
// LOG-ONLY and side-effect-free, so -- SAME PRECEDENT as NodeShuffle.Here (NodeShuffle.cpp:33-45),
// NodeShuffle.PointAtHere and NodeShuffle.DumpWells -- it is NOT gated behind EnableDiagnostics.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleCentreShadow.h" // ns-t42-centreshadow: the one shadow-reading emitter
#include "NodeShuffleTotallyInside.h" // ns-t53-totallyinside: the positive-only containment probe
#include "NodeShuffleGroundIdentity.h" // ns-t45-verticaldiag: hit-identity + cave-store emitters
#include "NodeShuffleWellCensus.h"   // AFGResourceNodeFrackingCore / ...Satellite
#include "NodeShuffleWellRetype.h"   // WellShort

#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

namespace
{
    // Where one member's tested location came from. Printed in words on every member line, because
    // "the probe used this vector" and "the probe used the vector the LIVE ACTOR is standing at" are
    // different claims and the second is the only one that describes the world right now.
    enum class EWellProbeLocSource : uint8
    {
        LiveActor,      // the spawned member actor's own transform, read this call
        LayoutRecord,   // the saved placed location on the layout, no live actor resolvable this call
        Unresolved      // neither: this member is COUNTED and NAMED, never dropped
    };

    const TCHAR* WellProbeSourceWords(EWellProbeLocSource S)
    {
        switch (S)
        {
        case EWellProbeLocSource::LiveActor:
            return TEXT("the LIVE SPAWNED ACTOR's own transform, read from that actor this call");
        case EWellProbeLocSource::LayoutRecord:
            return TEXT("the SAVED LAYOUT RECORD's placed location -- no live spawned actor for this ")
                   TEXT("member was resolvable this call");
        default:
            return TEXT("NOTHING -- this member was NOT probed");
        }
    }
}

static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleWellProbeCmd(
    TEXT("NodeShuffle.WellProbe"),
    TEXT("Probe every member of the NEAREST relocated well group at each member's own location (log-only)."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World) { return; }
        for (TActorIterator<ANodeShuffleSubsystem> It(World); It; ++It)
        {
            It->LogWellMemberProbeCensus();
            return;
        }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLPROBE: NodeShuffle subsystem not found in this world (main menu / no session ")
            TEXT("loaded?) -- no group was searched for and no gate was run."));
    }));

// ------------------------------------------------------------------------------------------------
// THE ONE NEAREST-PLACED-WELL SEARCH -- shared with NodeShuffle.Here
// ------------------------------------------------------------------------------------------------
// This body was inline in LogHereCensus. It is lifted here UNCHANGED in what it tests and in which group
// it picks (same three-part placed test, same 2D distance, same strict-less-than tie-break, so the first
// of two equidistant groups in layout order still wins) and LogHereCensus now calls it. A second copy in
// the new command would be two definitions of one question that must agree, which is T26's defect one
// indirection later. Read-only: no trace, no actor read, no layout write.
const FNodeShuffleWellEntry* ANodeShuffleSubsystem::FindNearestPlacedWellForDiag(
    const FVector& From, int32& OutTotalGroups, int32& OutRelocateFlagged, int32& OutPlacedGroups,
    double& OutNearestDist2DCm) const
{
    OutTotalGroups = WellLayout.Num();
    OutRelocateFlagged = 0;
    OutPlacedGroups = 0;
    OutNearestDist2DCm = 0.0;

    const FNodeShuffleWellEntry* Nearest = nullptr;
    double NearestD2 = 0.0;
    for (const FNodeShuffleWellEntry& W : WellLayout)
    {
        if (W.bRelocate) { OutRelocateFlagged++; }
        // Only a group that is actually PLACED has a destination a player can walk to; invariant A3 binds
        // bPlacementClaimLive to a non-zero PlacedCoreLocation and the third test asserts it rather than
        // trusting it. A group merely flagged bRelocate would produce a fictional distance.
        if (!W.bGroupPlaced || !W.bPlacementClaimLive || W.PlacedCoreLocation.IsNearlyZero()) { continue; }
        OutPlacedGroups++;
        const double D2 = FVector::DistSquared2D(W.PlacedCoreLocation, From);
        if (!Nearest || D2 < NearestD2) { Nearest = &W; NearestD2 = D2; }
    }
    if (Nearest) { OutNearestDist2DCm = FMath::Sqrt(NearestD2); }
    return Nearest;
}

// ------------------------------------------------------------------------------------------------
// THE COMMAND
// ------------------------------------------------------------------------------------------------
void ANodeShuffleSubsystem::LogWellMemberProbeCensus() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLPROBE: this subsystem has no world -- no group was searched for, no location was ")
            TEXT("resolved and no gate was run. Nothing below this line was measured."));
        return;
    }
    const APlayerController* Pc = World->GetFirstPlayerController();
    const APawn* Pawn = Pc ? Pc->GetPawn() : nullptr;
    if (!Pawn)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLPROBE: no player pawn in this world. The nearest group is chosen by distance from ")
            TEXT("one and the pawn is also what the enclosure probe puts on its ignore list, so no group ")
            TEXT("was chosen and no gate was run. This line reports that the pawn lookup returned ")
            TEXT("nothing; it does not say why."));
        return;
    }
    const FVector P = Pawn->GetActorLocation();
    const double PawnYaw = Pawn->GetActorRotation().Yaw;

    int32 GroupsTotal = 0, GroupsRelocateFlagged = 0, GroupsPlaced = 0;
    double NearestDistCm = 0.0;
    const FNodeShuffleWellEntry* E =
        FindNearestPlacedWellForDiag(P, GroupsTotal, GroupsRelocateFlagged, GroupsPlaced, NearestDistCm);
    if (!E)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLPROBE: nearest relocated well group -- NONE. Of %d well group(s) in the layout, %d ")
            TEXT("are flagged for relocation and %d are actually PLACED (a group is PLACED when it is ")
            TEXT("flagged group-placed, its placement claim is live, and its saved core location is ")
            TEXT("non-zero). With none placed there is no relocated well ANYWHERE in this save, which is ")
            TEXT("a different statement from one being far away -- that is why this prints a sentence ")
            TEXT("rather than a distance of zero. No member was resolved and no gate was run."),
            GroupsTotal, GroupsRelocateFlagged, GroupsPlaced);
        return;
    }

    const int32 MembersInGroup = 1 + E->Satellites.Num();
    {
        const FVector D = E->PlacedCoreLocation - P;
        const double Turn = FRotator::NormalizeAxis(
            FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)) - PawnYaw);
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLPROBE: group '%s' res=%s -- the nearest PLACED relocated well to you, %.0f m away ")
            TEXT("in 2D from its saved core location; from where you stand and face, turn %+.0f deg and ")
            TEXT("go. Chosen from %d group(s) in the layout, %d flagged for relocation, %d placed. This ")
            TEXT("group has %d member(s) to probe: 1 core and %d satellite record(s), of which %d ")
            TEXT("satellite(s) had a rigid body captured. Each member below is probed AT ITS OWN ")
            TEXT("location and every line names where that location came from. MEASURED: the saved ")
            TEXT("layout, your pawn's position and yaw, and per member whatever each line states. NOT ")
            TEXT("MEASURED anywhere below: whether this group is dressed, linked, or buildable."),
            *WellShort(E->CorePath), *WellShort(E->AssignedResourceClassPath),
            NearestDistCm / 100.0, Turn,
            GroupsTotal, GroupsRelocateFlagged, GroupsPlaced,
            MembersInGroup, E->Satellites.Num(), E->CapturedSatelliteCount);
    }

    // Group tallies. Every one of these has its denominator printed on the summary line.
    int32 Probed = 0, Unresolved = 0, FromLiveActor = 0, FromLayout = 0;
    int32 RefusedByPredicate = 0, EyeInsideSolid = 0, EyeNotMeasured = 0, PredicateCastNoRays = 0;
    int32 ThresholdSeen = -1;
    // ns-t42-centreshadow: the shadow candidate's own tallies, all with the same `Probed` denominator the
    // line above uses. They are counted so the group summary can say how many members EACH test would
    // refuse rather than only how many the shipped one did -- a per-member disagreement that never gets
    // added up is a disagreement nobody can size.
    int32 ShadowInside = 0, ShadowNotMeasured = 0, ShadowControlDidNotDemonstrate = 0;
    // ns-t52-memberdetail: how many probed members had their per-hit SHADOWCROSS list PRINTED. Counted
    // through the same predicate the emitter selects with, so this number cannot disagree with the
    // number of lists in the log.
    int32 ShadowDetailEmitted = 0;
    // The per-walk report cap, read back from a reading this run rather than typed into a log string.
    // Stays -1 when no member produced a reading at all.
    int32 ShadowDetailCapSeen = -1;
    int32 AgreeWithGate = 0, CandidateWouldAddRefusal = 0, CandidateWouldDropRefusal = 0;
    // ns-t53-totallyinside: the positive-only instrument's own tallies, all with the same `Probed`
    // denominator every line above uses. Counted so the group summary can say how many members EACH
    // instrument would call contained rather than only how many the shipped gate refused.
    int32 TIPositive = 0, TINotMeasured = 0, TICentreOverlapSolid = 0;
    int32 TIAgree = 0, TIAdds = 0, TIDrops = 0;
    int32 TIDirectionsBlocked = 0, TIDirectionsProbed = 0;
    int32 TICarriedOutward = 0, TICarriedInward = 0, TICarriedSweep = 0, TICarriedSweepStart = 0,
          TICarriedOuterOverlap = 0;
    int32 TIIgnoredRehits = 0, TIRetraceCapHits = 0;
    // The per-point direction count and the probe reach, read back from a reading THIS run rather than
    // typed into the summary string. They stay at -1 when no member produced a reading at all.
    int32 TIDirectionCountSeen = -1;
    double TIProbeReachSeen = -1.0;
    // ns-t45-verticaldiag: the cave-store lookup's own tallies, same `Probed` denominator as the rest.
    int32 CaveCellPresent = 0, CaveCellAbsent = 0;
    // The store size AS THE LAST LOOKUP OF THIS RUN READ IT BACK, so the summary's denominator is a
    // number this run measured rather than one re-read afterwards. -1 means no lookup ran this call.
    int32 LastCaveStoreTotalSeen = -1;

    for (int32 MemberIdx = 0; MemberIdx < MembersInGroup; ++MemberIdx)
    {
        const bool bIsCore = (MemberIdx == 0);
        const int32 SatIdx = MemberIdx - 1;
        const FNodeShuffleWellSatellite* S = bIsCore ? nullptr : &E->Satellites[SatIdx];

        const FString MemberWhat = bIsCore
            ? FString::Printf(TEXT("the CORE '%s'"), *WellShort(E->CorePath))
            : FString::Printf(TEXT("SATELLITE record %d of %d, '%s'"),
                              SatIdx + 1, E->Satellites.Num(), *WellShort(S->SatellitePath));

        // ---- RESOLVE THIS MEMBER'S LOCATION, AND NAME THE SOURCE ----
        // A live spawned actor is preferred over the saved record because it is the only one of the two
        // that describes where the member is NOW. When both exist the disagreement between them is
        // printed as a measured distance; this states no cause for any difference.
        EWellProbeLocSource Source = EWellProbeLocSource::Unresolved;
        FVector MemberLoc = FVector::ZeroVector;
        FString SourceDetail;

        const AActor* LiveActor = bIsCore
            ? static_cast<const AActor*>(SpawnedWellCores.FindRef(E->CorePath))
            : static_cast<const AActor*>(SpawnedWellSatellites.FindRef(S->SatellitePath));
        const bool bHandleKeyed = bIsCore
            ? (SpawnedWellCores.Find(E->CorePath) != nullptr)
            : (SpawnedWellSatellites.Find(S->SatellitePath) != nullptr);
        const FVector RecordLoc = bIsCore ? E->PlacedCoreLocation : S->PlacedLocation;
        const bool bRecordUsable = bIsCore
            ? !E->PlacedCoreLocation.IsNearlyZero()
            : (S->bPlaced && !S->PlacedLocation.IsNearlyZero());

        if (IsValid(LiveActor))
        {
            Source = EWellProbeLocSource::LiveActor;
            MemberLoc = LiveActor->GetActorLocation();
            SourceDetail = FString::Printf(
                TEXT("actor '%s' of class '%s'; the saved record for this member %s"),
                *LiveActor->GetName(), *LiveActor->GetClass()->GetName(),
                bRecordUsable
                    ? *FString::Printf(TEXT("holds %s, which is %.0f cm from the actor's transform"),
                                       *RecordLoc.ToCompactString(),
                                       FVector::Dist(RecordLoc, MemberLoc))
                    : TEXT("holds no usable placed location to compare against"));
        }
        else if (bRecordUsable)
        {
            Source = EWellProbeLocSource::LayoutRecord;
            MemberLoc = RecordLoc;
            SourceDetail = FString::Printf(
                TEXT("the spawned-actor map %s for this member's path, so the probe below tests where ")
                TEXT("the record SAYS the member is and not where an actor stands"),
                bHandleKeyed ? TEXT("held a handle that did not pass a validity test")
                             : TEXT("held no handle at all"));
        }
        else
        {
            SourceDetail = FString::Printf(
                TEXT("the spawned-actor map %s, and the saved record %s"),
                bHandleKeyed ? TEXT("held a handle that did not pass a validity test")
                             : TEXT("held no handle at all"),
                bIsCore
                    ? TEXT("carries a core location that reads as zero")
                    : (S->bPlaced ? TEXT("is flagged placed but carries a location that reads as zero")
                                  : TEXT("is not flagged placed")));
        }

        if (Source == EWellProbeLocSource::Unresolved)
        {
            Unresolved++;
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLPROBE: member %d of %d -- %s: NOT PROBED, because its location could not be ")
                TEXT("resolved from any source (%s). It is counted in the summary below rather than ")
                TEXT("dropped: a member missing from a probe run makes the run look cleaner than the ")
                TEXT("group is. This line reports which lookups returned nothing; it does not say why ")
                TEXT("they did."),
                MemberIdx + 1, MembersInGroup, *MemberWhat, *SourceDetail);
            continue;
        }

        Probed++;
        if (Source == EWellProbeLocSource::LiveActor) { FromLiveActor++; } else { FromLayout++; }

        const FVector ToMember = MemberLoc - P;
        const double MemberTurn = FRotator::NormalizeAxis(
            FMath::RadiansToDegrees(FMath::Atan2(ToMember.Y, ToMember.X)) - PawnYaw);
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLPROBE: member %d of %d -- %s, res=%s. The probe below used %s, and that location ")
            TEXT("came from %s (%s). From you: %.0f m in 2D, %+.0f m in Z, turn %+.0f deg from your ")
            TEXT("current facing."),
            MemberIdx + 1, MembersInGroup, *MemberWhat,
            *WellShort(E->AssignedResourceClassPath),
            *MemberLoc.ToCompactString(), WellProbeSourceWords(Source), *SourceDetail,
            FVector::Dist2D(MemberLoc, P) / 100.0, ToMember.Z / 100.0, MemberTurn);

        // ---- THE PROBE EYE READING, BEFORE ANY VERDICT (T38 cold review F1) ----
        // T66 cold review F2: the self-ignore locals are hoisted above this reading so the eye overlap
        // ignores the member's own live actor exactly as the gate leg below does — without it, a member
        // probed at its own live actor's transform reads "eye inside solid" from its own collision,
        // printed directly above a now-clean gate verdict. GateSelfIgnoreName is a NAMED local, not a
        // temporary dereferenced inline, so the verdict line's argument is one named value a lint can
        // pin, rather than an inline temporary.
        const AActor* GateSelfIgnore = IsValid(LiveActor) ? LiveActor : nullptr;
        const FString GateSelfIgnoreName = GateSelfIgnore
            ? GateSelfIgnore->GetName()
            : FString(TEXT("none"));
        FNodeShuffleProbeEyeReading Eye;
        const bool bEyeInside = IsProbeEyeInsideSolidForDiag(MemberLoc, Pawn, Eye, GateSelfIgnore);
        if (!Eye.bRan) { EyeNotMeasured++; } else if (bEyeInside) { EyeInsideSolid++; }
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLPROBE: member %d of %d -- probe eye inside solid geometry: %s. The eye sits at %s, ")
            TEXT("which is where the enclosure predicate starts its rays for this member. One sphere of ")
            TEXT("radius %.0f cm was overlapped there on the same channel those rays are cast on, with ")
            TEXT("the same ignore list as those rays (pawn, ignoredOwnActor=%s); it returned %d ")
            TEXT("result(s), %d of which report blocking on that channel: %s. WHY THIS LINE COMES FIRST: ")
            TEXT("when that eye starts inside a ")
            TEXT("blocking body, every ray below terminates at once and the verdict reads as a confident ")
            TEXT("full refusal that contains no terrain. A negative reading is a statement about this ")
            TEXT("channel at this radius and is not a claim that the eye stands in open air. This line ")
            TEXT("states no cause."),
            MemberIdx + 1, MembersInGroup,
            !Eye.bRan ? TEXT("UNMEASURED -- the overlap did not run, so neither answer is reported")
                      : (bEyeInside ? TEXT("YES") : TEXT("NO")),
            *Eye.Eye.ToCompactString(), Eye.ProbeRadiusCm, *GateSelfIgnoreName,
            Eye.OverlapResults, Eye.BlockingOverlaps, *Eye.BlockingActors);

        // ---- THE ENCLOSURE GATE, AT THIS MEMBER'S OWN LOCATION ----
        // The SAME member function both placement paths call, with the same recorder and threshold
        // out-params NodeShuffle.Here uses and the pawn on the ignore list exactly as it passes it.
        // Nothing here reimplements the predicate.
        // ns-t66-probe-self-ignore (P2 check on the T66 packet: this leg IS exposed, because MemberLoc
        // is this member's own live actor's transform whenever Source == LiveActor): this member's own
        // live actor rides along as the gate's SECOND ignore actor, mirroring what the shadow and
        // containment instruments below already do with their own per-block Subject locals. Null when
        // there is no live actor for this member (IsValid-checked; LiveActor above is null or stale
        // when this member resolved from the saved record instead), which makes this call
        // byte-identical to the pre-T66 one. GateSelfIgnore/GateSelfIgnoreName are declared above the
        // eye reading (T66 cold review F2) and shared by both legs.
        TArray<FNodeShuffleEnclosureRay> Rays;
        int32 Blocked = 0, Total = 0, Threshold = -1;
        const bool bEnclosed =
            IsSpotEnclosed(MemberLoc, Blocked, Total, &Rays, &Threshold, Pawn, GateSelfIgnore);
        ThresholdSeen = Threshold;
        if (Rays.Num() == 0) { PredicateCastNoRays++; }
        if (bEnclosed) { RefusedByPredicate++; }

        for (int32 i = 0; i < Rays.Num(); ++i)
        {
            const FNodeShuffleEnclosureRay& R = Rays[i];
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLPROBE: member %d of %d -- enclosure ray %d of %d, bearing %.0f deg: %s%s%s"),
                MemberIdx + 1, MembersInGroup, i + 1, Rays.Num(), R.BearingDeg,
                R.bBlocked ? TEXT("BLOCKED") : TEXT("clear"),
                R.bBlocked ? *FString::Printf(TEXT(" at %.0f cm by "), R.HitDistanceCm) : TEXT(""),
                R.bBlocked ? *R.HitActor : TEXT(""));
        }

        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLPROBE: member %d of %d -- ENCLOSURE GATE at %s: %d of %d rays blocked, and this ")
            TEXT("build refuses a spot at %d or more blocked, so the verdict for this member is %s. ")
            TEXT("ignoredOwnActor=%s. The threshold and the ray count here were read back from the ")
            TEXT("predicate this run. This is the same function both placement paths call, but it is ")
            TEXT("NOT called with the same arguments: this command hands it the pawn to ignore, and now ")
            TEXT("(ns-t66) this member's own live actor too when one resolved this run, while the ")
            TEXT("placement paths hand it nothing at all, and a placement probe tests a location it ")
            TEXT("settles for itself rather than the one this member ended up at. So this is a statement ")
            TEXT("about the terrain around this member now, not a prediction of what a placement probe ")
            TEXT("would return. WHAT THIS SHAPE OF TEST CANNOT SEE, by construction and not by ")
            TEXT("observation: anything blocking beyond one ray's reach, anything above or below the ray ")
            TEXT("height, and any gap between two bearings. Read it together with this member's eye line ")
            TEXT("above. A ray count of zero means the predicate cast no rays at all and the verdict is ")
            TEXT("not a measurement of this spot. Each ray reports the trace it made and nothing about ")
            TEXT("why the world is shaped that way."),
            MemberIdx + 1, MembersInGroup, *MemberLoc.ToCompactString(),
            Blocked, Total, Threshold,
            bEnclosed
                ? TEXT("REFUSED (this predicate, called with this command's exclusion, refuses this point)")
                : TEXT("not refused (this predicate, called with this command's exclusion, does not ")
                  TEXT("refuse this point)"),
            *GateSelfIgnoreName);

        // ---- THE SHADOW CENTRE-CONTAINMENT READING, AND AN EXPLICIT AGREE/DISAGREE ----
        // ns-t42-centreshadow. NOTHING GATES ON THIS. It is printed AFTER the shipped verdict above, on
        // its own lines, so the two can be compared member by member without either being mistaken for
        // the other. The candidate rule it evaluates is the author's: a member whose OWN CENTRE is inside
        // solid geometry is the defect, and a centre on the surface -- a partly embedded member -- is
        // wanted and must keep passing.
        {
            const AActor* Subject = IsValid(LiveActor) ? LiveActor : nullptr;
            FNodeShufflePointInsideReading Centre;
            // ns-t52-memberdetail: the per-hit list is now BUILT for every probed member. ns-t49
            // switched it off here to keep the volume down and ns-t51 then left the members as the only
            // points still reading inside, so the instrument was off where the reading is. Building it
            // is pure observation -- each detail string is composed from values the classification has
            // already produced, the cursor advance sits outside that block, and no trace, count or
            // verdict reads it. WHICH of these lists gets PRINTED is the policy on the emitter call
            // below, applied after the walks have run.
            const bool bCentreInside = IsPointInsideSolidShadowForDiag(MemberLoc, Subject, Centre,
                                                                      /*bWantCrossingDetail=*/true);
            if (!Centre.bRan) { ShadowNotMeasured++; }
            else
            {
                if (bCentreInside) { ShadowInside++; }
                if (!Centre.bControlInside) { ShadowControlDidNotDemonstrate++; }
            }
            if (CentreShadowDetailSelected(Centre, bCentreInside)) { ShadowDetailEmitted++; }
            ShadowDetailCapSeen = Centre.CrossingDetailCap;

            const ECentreShadowAgreement Agreement = LogCentreShadowReading(
                TEXT("WELLPROBE"),
                FString::Printf(TEXT("member %d of %d"), MemberIdx + 1, MembersInGroup),
                Centre, bCentreInside,
                Subject ? Subject->GetName()
                        : FString(TEXT("<none: this member resolved from the saved record>")),
                bEnclosed, Blocked, Total, Threshold,
                ECentreShadowDetailPolicy::RefusedOrControlDidNotDemonstrate);
            switch (Agreement)
            {
                case ECentreShadowAgreement::AgreeRefuse:
                case ECentreShadowAgreement::AgreePass:      AgreeWithGate++; break;
                case ECentreShadowAgreement::CandidateAdds:  CandidateWouldAddRefusal++; break;
                case ECentreShadowAgreement::CandidateDrops: CandidateWouldDropRefusal++; break;
                case ECentreShadowAgreement::NotComparable:  break;
            }
        }

        // ---- THE POSITIVE-ONLY CONTAINMENT INSTRUMENT, AT THIS MEMBER'S OWN LOCATION ----
        // ns-t53-totallyinside. NOTHING GATES ON THIS EITHER. It is printed AFTER both verdicts above so
        // the three can be compared member by member. It answers the narrowed question the author asked
        // for on 2026-08-10 -- is this member's own centre TOTALLY INSIDE solid geometry -- and it makes
        // no statement at all about a centre sitting on an edge, which the author deferred.
        {
            const AActor* Subject = IsValid(LiveActor) ? LiveActor : nullptr;
            FNodeShuffleTotallyInsideReading Inside;
            RunTotallyInsideProbe(GetWorld(), MemberLoc, Subject, Inside);
            if (!Inside.bRan) { TINotMeasured++; }
            else
            {
                if (Inside.bTotallyInside) { TIPositive++; }
                if (Inside.bCentreOverlapSolid) { TICentreOverlapSolid++; }
                TIDirectionsBlocked += Inside.BlockedDirections;
                TIDirectionsProbed += Inside.DirectionCount;
                TICarriedOutward += Inside.CarriedByOutward;
                TICarriedInward += Inside.CarriedByInward;
                TICarriedSweep += Inside.CarriedBySweep;
                TICarriedSweepStart += Inside.CarriedBySweepStart;
                TICarriedOuterOverlap += Inside.CarriedByOuterOverlap;
                TIIgnoredRehits += Inside.IgnoredRehits;
                TIRetraceCapHits += Inside.RetraceCapHits;
                TIDirectionCountSeen = Inside.DirectionCount;
                TIProbeReachSeen = Inside.ProbeReachCm;
            }

            const ETotallyInsideAgreement TIAgreement = LogTotallyInsideReading(
                TEXT("WELLPROBE"),
                FString::Printf(TEXT("member %d of %d"), MemberIdx + 1, MembersInGroup),
                Inside,
                Subject ? Subject->GetName()
                        : FString(TEXT("<none: this member resolved from the saved record>")),
                bEnclosed, Blocked, Total, Threshold);
            switch (TIAgreement)
            {
                case ETotallyInsideAgreement::AgreeRefuse:
                case ETotallyInsideAgreement::AgreePass:      TIAgree++; break;
                case ETotallyInsideAgreement::CandidateAdds:  TIAdds++; break;
                case ETotallyInsideAgreement::CandidateDrops: TIDrops++; break;
                case ETotallyInsideAgreement::NotComparable:  break;
            }
        }

        // ---- WHAT THE MOD'S OWN CAVE STORE HOLDS AT THIS MEMBER'S LOCATION ----
        // ns-t45-verticaldiag. The member's OWN point, the same one the enclosure gate above was given.
        // A lookup, not a test: NOTHING GATES ON IT and no trace is made for it.
        {
            FNodeShuffleCaveCellReading Cave;
            ReadCaveStoreAtForDiag(MemberLoc, Cave);
            if (Cave.bCellPresent) { CaveCellPresent++; } else { CaveCellAbsent++; }
            LastCaveStoreTotalSeen = Cave.StoreCellsTotal;
            LogCaveStoreReading(TEXT("WELLPROBE"),
                FString::Printf(TEXT("member %d of %d, at its own location"),
                                MemberIdx + 1, MembersInGroup),
                Cave);
        }

        // ---- SLOPE + CLIFF VERDICT AT THIS MEMBER ----
        // The same long downward ground trace NodeShuffle.Here and NodeShuffle.PointAtHere run, from this
        // member's own position, so the three commands' slope lines compare. Its landing point is NOT
        // what the enclosure call above was given, and the line says so.
        {
            FVector SlopeLoc = FVector::ZeroVector;
            FRotator SlopeRot = FRotator::ZeroRotator;
            bool bSlopeWater = false;
            bool bSlopeCliff = false;
            FVector SlopeN = FVector::UpVector;
            // ns-t45-verticaldiag: the same call with the write-only hit tap added, so the line below
            // NAMES the actor and component this trace ended on. THIS IS THE PACKET'S CENTRAL QUESTION:
            // that landing point has been printing metres above the member all evening with no owner.
            FHitResult GroundHit;
            const bool bHaveSettled = RaycastGroundAt(MemberLoc, static_cast<float>(MemberLoc.Z), Pawn,
                                                      nullptr, SlopeLoc, SlopeRot, bSlopeWater,
                                                      /*bShortTrace=*/false, &bSlopeCliff, &SlopeN,
                                                      &GroundHit);
            LogGroundTraceHitIdentity(TEXT("WELLPROBE"),
                FString::Printf(TEXT("member %d of %d, traced from its own location"),
                                MemberIdx + 1, MembersInGroup),
                MemberLoc, bHaveSettled, GroundHit);
            if (bHaveSettled)
            {
                const float SlopeDeg = FMath::RadiansToDegrees(
                    FMath::Acos(FMath::Clamp(static_cast<float>(SlopeN.Z), -1.0f, 1.0f)));
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WELLPROBE: member %d of %d -- ground slope where a long downward trace from ")
                    TEXT("this member landed = %.1f deg; the cliff gate (%.0f deg) %s, and the water ")
                    TEXT("test there returned %d. THAT LANDING POINT IS %s, WHICH IS %+.0f m in Z FROM ")
                    TEXT("THE POINT THE ENCLOSURE PREDICATE WAS GIVEN ABOVE and was not itself given to ")
                    TEXT("it. This line does not explain the difference between the two points."),
                    MemberIdx + 1, MembersInGroup, SlopeDeg, GetCliffSlopeDegForDiag(),
                    bSlopeCliff ? TEXT("WOULD REJECT a settle there") : TEXT("accepts a settle there"),
                    bSlopeWater ? 1 : 0,
                    *SlopeLoc.ToCompactString(), (SlopeLoc.Z - MemberLoc.Z) / 100.0);
            }
            else
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WELLPROBE: member %d of %d -- a long downward ground trace from this member ")
                    TEXT("found nothing to settle on, so there is no slope figure and no cliff-gate ")
                    TEXT("verdict for it. The enclosure verdict above is unaffected: it was given this ")
                    TEXT("member's own location and never this trace's result. This line reports that ")
                    TEXT("the trace returned nothing; it does not say why."),
                    MemberIdx + 1, MembersInGroup);
            }
        }
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLPROBE: group '%s' summary -- %d of this group's %d member(s) were probed and %d were ")
        TEXT("NOT, each of those named on its own line above. Of the %d probed, %d used a live spawned ")
        TEXT("actor's transform and %d used the saved layout record. Of the %d probed, the enclosure ")
        TEXT("predicate refused %d at a threshold of %d blocked rays, and for %d the predicate cast no ")
        TEXT("rays at all (those verdicts measure nothing). Of the %d probed, the eye reading was ")
        TEXT("positive for %d and could not be taken for %d. The member count is 1 core plus %d ")
        TEXT("satellite record(s) held on this group; this command probes every record it can resolve ")
        TEXT("and makes no judgement about which members SHOULD exist. A threshold of -1 here would ")
        TEXT("mean no member reached the predicate this run."),
        *WellShort(E->CorePath),
        Probed, MembersInGroup, Unresolved,
        Probed, FromLiveActor, FromLayout,
        Probed, RefusedByPredicate, ThresholdSeen, PredicateCastNoRays,
        Probed, EyeInsideSolid, EyeNotMeasured,
        E->Satellites.Num());

    // ns-t42-centreshadow: THE TWO TESTS SIDE BY SIDE OVER THE WHOLE GROUP, WITH THE SAME DENOMINATOR.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLPROBE: group '%s' shadow summary -- of the %d member(s) probed, the SHIPPED enclosure ")
        TEXT("gate would refuse %d and the SHADOW centre-containment candidate would refuse %d. They ")
        TEXT("agree on %d member(s) and disagree on %d: on %d the candidate would refuse a member the ")
        TEXT("shipped gate accepts, and on %d it would accept a member the shipped gate refuses. For %d ")
        TEXT("member(s) the shadow walks did not run at all, and those are in neither column. For %d ")
        TEXT("member(s) the positive control did NOT read inside -- for those, a CENTRE NOT INSIDE is a ")
        TEXT("reading this method did not demonstrate it could have contradicted, so treat it as ")
        TEXT("unproven rather than as open air. NO PLACEMENT BEHAVIOUR IN THIS BUILD DEPENDS ON THE ")
        TEXT("CANDIDATE COLUMN: it is computed and printed and nothing reads it. This line counts ")
        TEXT("verdicts; it states no cause for any of them."),
        *WellShort(E->CorePath),
        Probed, RefusedByPredicate, ShadowInside,
        AgreeWithGate, CandidateWouldAddRefusal + CandidateWouldDropRefusal,
        CandidateWouldAddRefusal, CandidateWouldDropRefusal,
        ShadowNotMeasured, ShadowControlDidNotDemonstrate);

    // ns-t52-memberdetail: WHICH MEMBERS GOT A PER-HIT LIST, WITH THE SAME DENOMINATOR AS EVERY LINE
    // ABOVE. A selective instrument that never says how selective it was is one nobody can size.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLPROBE: group '%s' shadow per-hit detail -- a SHADOWCROSS list was printed for %d of ")
        TEXT("the %d member(s) probed. THE RULE THIS COMMAND PASSED, and the only rule it passed: print ")
        TEXT("for a member the candidate would refuse, or for one whose positive control did not read ")
        TEXT("inside; a member the candidate accepts whose control read inside keeps its two summary ")
        TEXT("lines and gets no list. Of the %d probed, %d had a candidate that would refuse, %d had a ")
        TEXT("positive control that did not read inside, and for %d the walks did not run at all -- a ")
        TEXT("member can fall in more than one of those, so they need not sum to the printed figure. ")
        TEXT("Each printed list reports at most %d hit(s) per walk, that figure read back from a ")
        TEXT("reading this run rather than typed here, and each list's own closing line states how many ")
        TEXT("hits it did not report; a figure of -1 would mean no member produced a reading at all. ")
        TEXT("The list was BUILT for every one of the %d probed member(s): this is a choice about what ")
        TEXT("was printed, it was made after each member's walks had already run, and it moved no ")
        TEXT("trace, no count and no verdict. This line counts printed lists and states no cause for ")
        TEXT("any verdict."),
        *WellShort(E->CorePath),
        ShadowDetailEmitted, Probed,
        Probed, ShadowInside, ShadowControlDidNotDemonstrate, ShadowNotMeasured,
        ShadowDetailCapSeen,
        Probed);

    // ns-t53-totallyinside: THE POSITIVE-ONLY INSTRUMENT OVER THE WHOLE GROUP, SAME DENOMINATOR.
    UE_LOG(LogNodeShuffle, Display,
        TEXT("WELLPROBE: group '%s' containment summary -- of the %d member(s) probed, the SHIPPED ")
        TEXT("enclosure gate would refuse %d and this instrument gave its positive verdict for %d. They ")
        TEXT("agree on %d member(s) and disagree on %d: on %d this instrument is positive where the ")
        TEXT("shipped gate accepts, and on %d it is not positive where the shipped gate refuses. For %d ")
        TEXT("member(s) no query ran at all and those are in neither column. ACROSS THE WHOLE GROUP, %d ")
        TEXT("of the %d direction(s) probed over every member reported solid; each member is probed in ")
        TEXT("%d direction(s) out to %.0f cm, both of those read back from a reading this run rather ")
        TEXT("than typed into this line, and a figure of -1 in either would mean no member produced a ")
        TEXT("reading at all. WHICH INSTRUMENT CARRIED THOSE DIRECTIONS: the outward line query %d, the ")
        TEXT("inward line query %d, the inward sweep %d, of which %d were that sweep reporting it was ")
        TEXT("already overlapping at its own start, and the overlap at the far end %d; one direction can ")
        TEXT("be carried by more than one instrument, so those need not sum to %d. THE CORROBORATING ")
        TEXT("OVERLAP at the member's own location found solid for %d of the %d probed, and it neither ")
        TEXT("grants nor vetoes any verdict above. Over the whole group, %d query result(s) came back ")
        TEXT("on an actor already on a query's own ignore list and %d query(ies) spent their whole ")
        TEXT("re-run budget. NO PLACEMENT BEHAVIOUR IN THIS BUILD DEPENDS ON THIS COLUMN: it is ")
        TEXT("computed and printed and nothing reads it. This line counts verdicts and states no cause ")
        TEXT("for any of them."),
        *WellShort(E->CorePath),
        Probed, RefusedByPredicate, TIPositive,
        TIAgree, TIAdds + TIDrops, TIAdds, TIDrops, TINotMeasured,
        TIDirectionsBlocked, TIDirectionsProbed,
        TIDirectionCountSeen, TIProbeReachSeen,
        TICarriedOutward, TICarriedInward, TICarriedSweep, TICarriedSweepStart,
        TICarriedOuterOverlap, TIDirectionsBlocked,
        TICentreOverlapSolid, Probed,
        TIIgnoredRehits, TIRetraceCapHits);

    // ns-t45-verticaldiag: THE CAVE-STORE LOOKUP OVER THE WHOLE GROUP, WITH THE SAME DENOMINATOR.
    LogCaveStoreGroupSummary(TEXT("WELLPROBE"), WellShort(E->CorePath), Probed,
                             CaveCellPresent, CaveCellAbsent, LastCaveStoreTotalSeen);
}
