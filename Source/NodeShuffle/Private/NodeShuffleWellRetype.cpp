// Packet H1 (ns-wells-h1, branch feature/extractor-automatch): the WRITE half of the in-place
// resource-well retype -- resolving each well's members, writing the dealt resource, and asserting the
// group still shares one. NodeShuffleWellRoll.cpp owns the DEAL half; NodeShuffleWellRetype.h says what
// lives where and why both are ANodeShuffleSubsystem members defined outside the subsystem's own .cpp.
//
// THE WELL IS NEVER MOVED. Nothing here spawns, relocates or touches mCore -- that is all H2 (design
// §6). This file writes one field on actors that already exist, and rebuilds their visual.
//
// PURITY IS NEVER WRITTEN. Vanilla wells MIX purities across their satellites (design §Q2, and H0
// measured it), so there is no shared purity to preserve and normalising one would be a silent balance
// change. mPurity/mPurityOverride do not appear in this file at all; GetResourcePurity() therefore keeps
// returning each satellite's authored value.
//
// IMPORT DISCIPLINE (memory: sf-shipping-export-trap; H0 baseline 744 / 0 missing). Deliberately no new
// engine entry point:
//   * the resource write is a direct write to the PRIVATE FIELD mResourceClassOverride via the Friend
//     grant -- NOT AFGResourceNodeBase::SetResourceClassOverride(), which is declared in the header but
//     whose presence in the shipping export table is unknown and would have to be measured;
//   * the post-write rebuild goes through RebuildNodeNativeVisual, i.e. ProcessEvent on the reflected
//     OnRep_ResourceClassOverride -- the idiom NodeShuffleSubsystem.cpp already documents as the way to
//     reach that rebuild without depending on an exported symbol.
// That is the PREDICTION, not the claim -- the import table is MEASURED after every build.

// UBT's IWYU rule requires Foo.cpp to include Foo.h FIRST. This file had NodeShuffleSubsystem.h first
// and got away with it only while adaptive unity happened to fold it into a shared translation unit;
// the moment it was compiled standalone (ns-review-h3 round, when NodeShuffleSubsystem.h changed) the
// check fired. Latent since H1, not caused by H2 -- fixed here because it blocks the build.
#include "NodeShuffleWellRetype.h" // the shared pure helpers -- MUST be first (IWYU)

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleWellCensus.h" // AFGResourceNodeFrackingCore / ...Satellite
#include "EngineUtils.h"           // TActorIterator, for the once-per-session live-vs-layout census
#include "Resources/FGResourceDescriptor.h"
// Completeness for the TWeakObjectPtr<T> returned BY VALUE from GetActivator(). Include only.
#include "Buildables/FGBuildableFrackingActivator.h"

// MemberRole, not Role: `Role` is an AActor member (deprecated net role) and shadowing it is a C4458
// that this project builds as an error.
bool ANodeShuffleSubsystem::RetypeWellMember(AFGResourceNodeBase* Member, UClass* ResourceClass,
                                             const TCHAR* MemberRole, const TCHAR* CoreName)
{
    if (!IsValid(Member) || !ResourceClass) { return false; }
    // Idempotent: the apply pass runs every ~5 s over every well, so the steady state must be a pointer
    // compare and nothing else -- no write, no visual rebuild, no log line.
    if (Member->mResourceClassOverride.Get() == ResourceClass) { return false; }

    const FString Was = WellShort(WellPathOf(Member->GetResourceClass().Get()));
    // The Friend-granted field write, NOT SetResourceClassOverride() -- see this file's import-discipline
    // header. mResourceClassOverride is SaveGame, so the retype persists with the save exactly like a
    // spawned node's does, and it is the field GetResourceClass() actually consults.
    Member->mResourceClassOverride = ResourceClass;
    // The game's own post-override rebuild, via reflection. For a LEVEL well this is what re-dresses the
    // paired AFGNodeMeshActor (MT_Core / MT_Crack / MT_Satellite), i.e. the engine's own Random Nodes
    // Game Mode path -- which is why H1 needs no visual work of its own, unlike H2 (design §2.4).
    // ASSUMED, runtime-verify: that the mesh actor really does re-dress for the well mesh types. It runs
    // through engine code we cannot read, so it is a test step, not a conclusion (see the report).
    RebuildNodeNativeVisual(Member);
    UE_LOG(LogNodeShuffle, Verbose, TEXT("WELLH1-WRITE core='%s' %s '%s': res '%s' -> '%s'"),
        CoreName, MemberRole, *Member->GetName(), *Was, *WellShort(ResourceClass->GetPathName()));
    ++WellMembersWrittenThisSession;
    return true;
}

void ANodeShuffleSubsystem::ApplyWellRetype(bool bWellShuffleEnabled)
{
    if (!bWellShuffleEnabled)
    {
        if (WellLayout.Num() > 0 && !bWellDisabledLogged)
        {
            bWellDisabledLogged = true;
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH1: 'Shuffle Resource Wells' is OFF but this save holds %d rolled wells -- NOT applying. "
                     "Resource changes already written to the save persist (same as the master switch)."),
                WellLayout.Num());
        }
        return;
    }
    // Gated on "a roll has happened", NOT on "the layout is non-empty". A roll that found ZERO wells is
    // the single most important case for the honesty scan below, and an emptiness early-out would be the
    // one thing that hides it.
    if (!bWellLayoutRolled)
    {
        // ns-review-h1 W3. This branch used to return in silence -- no line, no counter, nothing. It is
        // also the MOST LIKELY first state a user reaches, because this feature's own config tooltip
        // tells them to turn it on and THEN re-roll, so "enabled it, forgot the re-roll" produces a
        // completely empty log and no way to tell it apart from a broken build. Every other skip branch
        // in this packet states its reason; so does this one now.
        if (!bWellNoRollLogged)
        {
            bWellNoRollLogged = true;
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH1: 'Shuffle Resource Wells' is ON but this save holds no well roll yet -- nothing to "
                     "apply. Use 'Re-roll Layout' to deal the wells (the toggle takes effect at ROLL time, not "
                     "at load time)."));
        }
        return;
    }
    ++WellApplyPasses;

    int32 WellsResolved = 0, WellsUnstreamed = 0, WellsSkipped = 0, MembersWritten = 0;
    int32 SatsResolved = 0, SatsMissing = 0, Mismatches = 0;

    for (FNodeShuffleWellEntry& E : WellLayout)
    {
        if (!E.bManaged)
        {
            ++WellsSkipped;
            if (!WellSkipLogged.Contains(E.CorePath))
            {
                WellSkipLogged.Add(E.CorePath);
                // ns-review-h1 F6: report the EFFECTIVE resource (Assigned), falling back to Original only
                // when there is none. A pinned well may be holding an assignment from an EARLIER roll, and
                // printing Original here contradicted the pool lines -- which correctly key on Assigned --
                // for exactly the well a reader is most likely to be checking.
                // ns-review-h1 I2: THREE reasons, not two. The undealt-tail path in
                // NodeShuffleWellRoll.cpp leaves a well un-managed and UNPINNED but with a perfectly
                // good Original, and the two-way ternary mislabelled it "no original resource resolved
                // at roll time" -- a diagnostic pointing at the wrong subsystem entirely, for a state
                // only reachable when the deck/recipient guard has already fired.
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("WELLH1-SKIP core='%s' reason=%s res='%s' -- left exactly as it stands."),
                    *WellShort(E.CorePath),
                    E.bPinned ? TEXT("PINNED (pressurizer/extractor already built on this well)")
                        : (!E.OriginalResourceClassPath.IsEmpty()
                            ? TEXT("UNDEALT (no card reached this well -- see the deck/recipient mismatch warning at roll time)")
                            : TEXT("no original resource resolved at roll time")),
                    *WellShort(E.AssignedResourceClassPath.IsEmpty() ? E.OriginalResourceClassPath
                                                                    : E.AssignedResourceClassPath));
            }
            continue;
        }

        UClass* ResourceClass = LoadClassByPath(E.AssignedResourceClassPath);
        // Boundary validation: a resource path that resolves to something that is not a resource
        // descriptor must never reach a TSubclassOf<UFGResourceDescriptor> field.
        if (!ResourceClass || !ResourceClass->IsChildOf(UFGResourceDescriptor::StaticClass()))
        {
            ++WellsSkipped;
            if (!WellSkipLogged.Contains(E.CorePath))
            {
                WellSkipLogged.Add(E.CorePath);
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("WELLH1-SKIP core='%s' reason=ASSIGNED RESOURCE '%s' %s -- well left vanilla."),
                    *WellShort(E.CorePath), *E.AssignedResourceClassPath,
                    ResourceClass ? TEXT("is not a UFGResourceDescriptor") : TEXT("failed to load"));
            }
            continue;
        }

        AFGResourceNodeFrackingCore* Core = Cast<AFGResourceNodeFrackingCore>(FindOriginalBaseByPath(E.CorePath));
        if (!IsValid(Core)) { ++WellsUnstreamed; continue; } // not loaded yet -- retried every pass, silently
        ++WellsResolved;

        // RESOLVE EVERY MEMBER FIRST; WRITE NOTHING YET (ns-review-h1 W1/W2). This loop used to resolve
        // and WRITE a satellite in the same step, which made two necessary things impossible: the
        // apply-time pin could not consult the satellites before the core had already been written, and
        // neither pin path could describe the group it was standing down from. Resolution is now a pure
        // read. A satellite that has not streamed in is NORMAL, not an error -- it is written on
        // whichever later pass it appears, which is why this whole apply is re-run every pass.
        int32 Resolved = 0, Missing = 0;
        TArray<AFGResourceNodeFrackingSatellite*> Live;
        for (const FNodeShuffleWellSatellite& S : E.Satellites)
        {
            AFGResourceNodeFrackingSatellite* Sat =
                Cast<AFGResourceNodeFrackingSatellite>(FindOriginalBaseByPath(S.SatellitePath));
            if (!IsValid(Sat)) { ++Missing; continue; }
            ++Resolved;
            Live.Add(Sat);
        }
        SatsResolved += Resolved;
        SatsMissing += Missing;

        // ns-review-h1 W2. Both pin paths stand down with a `continue`, which happens BEFORE the
        // WELLH1-MISMATCH assert further down -- so a well left SPLIT across two resources (core written
        // on an earlier pass, some satellites not yet resolved, a pressurizer arriving in between) would
        // exit with no diagnostic at all, while the pin line asserted "the well keeps the resource it
        // currently has". That is a confident falsehood in precisely the state a reader needs the truth.
        // So the pin line now CARRIES the group's state. Reporting only, deliberately: the alternative --
        // finish writing the group and then pin -- changes write ordering under a live pressurizer, and
        // that is not a change a scoped fix gets to make.
        const auto DescribeGroupState = [&Live](const AFGResourceNodeFrackingCore* C, int32& OutDisagree) -> FString
        {
            OutDisagree = 0;
            const UClass* CRes = C->GetResourceClass().Get();
            for (const AFGResourceNodeFrackingSatellite* Sat : Live)
            {
                if (Sat->GetResourceClass().Get() != CRes) { ++OutDisagree; }
            }
            if (Live.Num() == 0) { return FString(TEXT("group state UNVERIFIED -- no satellite streamed, nothing compared")); }
            if (OutDisagree == 0) { return FString::Printf(TEXT("group consistent across core + %d resolved satellite(s)"), Live.Num()); }
            return FString::Printf(
                TEXT("*** GROUP IS SPLIT: %d of %d resolved satellites disagree with the core's '%s' -- this well "
                     "produces TWO resources and, now that it is pinned, will STAY that way until the next roll ***"),
                OutDisagree, Live.Num(), *WellShort(WellPathOf(CRes)));
        };

        // LIVE PIN RE-CHECK, the well analogue of ApplyLayout's occupied-spawned-node pin: if someone
        // built on this well between the roll and now, stand down rather than changing the resource
        // under working machinery.
        const bool bOccupiedNow = Core->GetActivator().IsValid() || Core->IsOccupied();
        const bool bAlreadyApplied = (Core->mResourceClassOverride.Get() == ResourceClass);

        // ns-review-h1 W1 -- THE HOLE THIS CLOSES. The roll-time pin checks satellites
        // (NodeShuffleWellRoll.cpp); this apply-time re-check did NOT, and the justification for that
        // asymmetry ("a Well Extractor cannot exist on a well with no pressurizer") was REASONED, never
        // MEASURED -- and it was load-bearing for the only safety property this packet has. Reachable:
        // the roll runs with the core streamed and some satellites not, so the roll's satellite loop
        // never sees them, the well is dealt, and later the core resolves reporting unoccupied while a
        // satellite carries a Well Extractor. The write would then land under that extractor. The two
        // pin rules are now the SAME rule evaluated at two times, which is what stops them diverging
        // again. Unconditional on bAlreadyApplied, exactly as the roll-time rule is: a well the player
        // has finished building on is not ours, and the next roll re-derives the same verdict from live
        // state, so F2's withdrawal still takes the right card.
        const AFGResourceNodeFrackingSatellite* OccupiedSat = nullptr;
        for (const AFGResourceNodeFrackingSatellite* Sat : Live)
        {
            if (const_cast<AFGResourceNodeFrackingSatellite*>(Sat)->GetExtractor().IsValid() || Sat->IsOccupied())
            {
                OccupiedSat = Sat;
                break;
            }
        }

        if ((bOccupiedNow && !bAlreadyApplied) || OccupiedSat)
        {
            E.bPinned = true;
            E.bManaged = false;
            // ns-review-h1 F5: source the record from the LIVE EFFECTIVE class, not from Original. We
            // are standing down precisely because this assignment is not (fully) ours, but "not ours" is
            // not the same as "authored" -- a previous roll's assignment may already be on this core.
            // Reading the world is the only statement true in both cases, and it is what keeps F2's
            // withdrawal taking the right card on the next roll.
            const UClass* LiveRes = Core->GetResourceClass().Get();
            E.AssignedResourceClassPath = LiveRes ? LiveRes->GetPathName() : E.OriginalResourceClassPath;
            int32 Disagree = 0;
            const FString GroupState = DescribeGroupState(Core, Disagree);
            if (Disagree > 0) { ++Mismatches; } // W2: the assert below is unreachable from here, so count it HERE
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH1-PIN core='%s' reason=%s -- un-managed until the next roll (which re-evaluates the "
                     "pin from live state and may free it again). Core now holds '%s'; %s."),
                *WellShort(E.CorePath),
                OccupiedSat ? TEXT("satellite-extractor (a Resource Well Extractor is on one of its satellites)")
                            : TEXT("pressurizer-on-core (appeared before we retyped it)"),
                *WellShort(E.AssignedResourceClassPath), *GroupState);
            continue;
        }

        // Every pin question is now answered; from here the pass writes.
        int32 WroteHere = RetypeWellMember(Core, ResourceClass, TEXT("core"), *WellShort(E.CorePath)) ? 1 : 0;
        for (AFGResourceNodeFrackingSatellite* Sat : Live)
        {
            // PURITY IS NOT TOUCHED HERE, and that is the point of the packet: mPurity/mPurityOverride
            // are never written, so GetResourcePurity() keeps returning the vanilla per-satellite value.
            WroteHere += RetypeWellMember(Sat, ResourceClass, TEXT("sat"), *WellShort(E.CorePath)) ? 1 : 0;
        }
        MembersWritten += WroteHere;

        // THE ASSERT. H0 measured resourceMismatchWells=0 on all 20 wells, so this should never fire --
        // but the shared resource is the ONE invariant a retype can break, and an invariant that is only
        // measured once is not an invariant. Compared on GetResourceClass(), because that is what the
        // game itself asks (the override if set, else the authored class).
        //
        // ns-review-h1 F4: with ZERO satellites streamed there is NOTHING TO COMPARE, and the old code
        // reported equal=1 anyway -- a confident falsehood about the one invariant this packet exists to
        // hold, printed exactly when the well is least verified. H0's emptyCores=0 is ONE measurement and
        // is not licence to assume satellites are always present. So the verdict is now three-valued and
        // only a REAL comparison can produce a verdict at all.
        const UClass* CoreRes = Core->GetResourceClass().Get();
        const bool bComparable = (Resolved > 0);
        bool bEqual = (CoreRes == ResourceClass);
        for (const AFGResourceNodeFrackingSatellite* Sat : Live)
        {
            if (Sat->GetResourceClass().Get() != CoreRes) { bEqual = false; }
        }
        if (bComparable && !bEqual)
        {
            ++Mismatches;
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("WELLH1-MISMATCH core='%s': the group does NOT share one resource after the write "
                     "(core='%s' assigned='%s', %d satellites resolved). Design §Q2 says this cannot happen and "
                     "H0 measured 0/20 violations -- if you are reading this line, that measurement no longer holds."),
                *WellShort(E.CorePath), *WellShort(WellPathOf(CoreRes)),
                *WellShort(ResourceClass->GetPathName()), Resolved);
        }

        if (WroteHere > 0 || !WellAppliedLogged.Contains(E.CorePath))
        {
            WellAppliedLogged.Add(E.CorePath);
            UE_LOG(LogNodeShuffle, Display,
                TEXT("WELLH1 core='%s' res='%s' (was '%s') members=1core+%d/%dsat written=%d equal=%s"),
                *WellShort(E.CorePath), *WellShort(E.AssignedResourceClassPath),
                *WellShort(E.OriginalResourceClassPath), Resolved, E.Satellites.Num(), WroteHere,
                // n/a, never 1, when no satellite has streamed: with nothing compared there IS no verdict,
                // and printing a pass would be the "clean vs could-not-answer" conflation H0's own summary
                // verdicts were rewritten three times to avoid.
                !bComparable ? TEXT("n/a(no satellite streamed)") : (bEqual ? TEXT("1") : TEXT("0")));
        }
    }

    // ONE-SHOT STREAMING HONESTY SCAN. The roll can only manage wells that were LOADED when it ran, and
    // a well that was not is left vanilla -- safe, but INVISIBLE unless we say so. This answers "did all
    // of them make the layout?" directly, in numbers directly comparable with NodeShuffle.DumpWells'
    // own totals, and it is the measurement that decides whether H1's roll-time discovery is good enough
    // or H2 needs a different discovery point. Fired on a FIXED pass number (~30 s in, apply runs every
    // ~5 s) rather than on first resolution: it must report a SETTLED world, and it must still run when
    // the layout is empty -- "the roll found no wells at all" is precisely the failure it exists to name.
    if (WellApplyPasses == WellCensusScanPass)
    {
        TSet<FString> Known;
        for (const FNodeShuffleWellEntry& E : WellLayout) { Known.Add(E.CorePath); }
        int32 LiveCores = 0, Unknown = 0, UnknownOurs = 0, UnknownLevelPlaced = 0;
        for (TActorIterator<AFGResourceNodeFrackingCore> It(GetWorld()); It; ++It)
        {
            if (!IsValid(*It)) { continue; }
            ++LiveCores;
            const FString Path = It->GetPathName();
            if (Known.Contains(Path)) { continue; }
            ++Unknown;
            // ns-truth-diagnostics A1 -- THE RULE THIS LINE EXISTS TO ENFORCE: a diagnostic may report
            // what it MEASURED; it may not assert WHY. The old text ended "(it had not streamed in when
            // the roll ran)" -- a CAUSE THIS CODE NEVER TESTED. Measured 2026-08-08 (17:19 boot): all 14
            // cores that fired it were NodeShuffle's OWN relocated wells -- the very next roll printed
            // "WELLH1-ROLL: skippedOurSpawned=14", i.e. the roll excluded them BY DESIGN (ns-review-h2
            // F1). Two investigations quoted the invented explanation as evidence and the user was told
            // it as fact. So print the two discriminators that ARE testable here and name the test for
            // each, and say plainly that the remaining case is undetermined.
            const bool bOurs = FNodeShuffleModule::IsManagedSpawnedNode(*It);
            if (bOurs) { ++UnknownOurs; }
            const bool bLevelPlaced = It->IsNetStartupActor();
            if (bLevelPlaced) { ++UnknownLevelPlaced; }
            // ns-t1t2 (user-observed 2026-08-08): SEVERITY NOW MATCHES THE MEASUREMENT. This fired
            // Warning 14 times in one session, every one of them nodeShuffleSpawned=1 -- the case the
            // line's own text calls "nothing is wrong". A warning on the healthy steady state trains a
            // reader to skip warnings, which is how a real one gets missed. The text is unchanged and
            // still says exactly what it measured; only the level is now derived from the discriminator
            // the line ALREADY computes. bOurs => Display (routine, still greppable), NOT ours =>
            // Warning (genuinely unexplained: an unmanaged core absent from the layout).
            // The text is built ONCE and then logged at one of two levels. It cannot be a ternary in
            // the UE_LOG verbosity slot -- that argument is token-pasted (ELogVerbosity::##Verbosity),
            // so it must be a bare identifier; a ternary there does not compile.
            const FString UnknownMsg = FString::Printf(
                TEXT("WELLH1-UNKNOWN core='%s' is loaded but is NOT in the rolled well layout. "
                     "MEASURED: nodeShuffleSpawned=%d (test: FNodeShuffleModule::IsManagedSpawnedNode -- "
                     "the SAME registry the roll's skippedOurSpawned filter uses, so 1 means WE spawned "
                     "this core and the roll excluded it deliberately; nothing is wrong), levelPlaced=%d "
                     "(test: AActor::IsNetStartupActor). NOT MEASURED: WHY an unmanaged level-placed core "
                     "would be absent -- this line does not test streaming, load order, mod init order or "
                     "anything else, and must never name one. OBSERVED EFFECT either way: LEFT VANILLA; a "
                     "re-roll re-scans live and would enrol it. path='%s'"),
                *It->GetName(), bOurs ? 1 : 0, bLevelPlaced ? 1 : 0, *Path);
            if (bOurs)
            {
                UE_LOG(LogNodeShuffle, Display, TEXT("%s"), *UnknownMsg);
            }
            else
            {
                UE_LOG(LogNodeShuffle, Warning, TEXT("%s"), *UnknownMsg);
            }
        }
        // The caveat is IN THE LINE, not only in this comment (ns-review-h1): it is a SNAPSHOT at one
        // instant, so a core that ENTERS THE WORLD later is never counted, and a player who loaded far
        // from every well gets "0 loaded-but-unmanaged" purely because nothing was loaded to disagree
        // with. Read alongside "cores loaded now" -- that number, not the zero, is what says whether the
        // snapshot was worth anything.
        // ns-truth-diagnostics A1: "streams in" was the only arrival mechanism the old wording admitted.
        // The provenance split is now IN the line, so the reader never has to guess which population the
        // unmanaged count is made of.
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH1-CENSUS (single snapshot at apply pass %d, ~%d s after load -- NOT a running total; "
                 "cores that enter the world after this instant -- by any mechanism, including spawns by "
                 "us or by another mod -- are never counted, so '0 unmanaged' with a low 'cores loaded "
                 "now' means UNMEASURED, not clean): %d cores loaded now, %d wells in the layout, %d "
                 "loaded-but-unmanaged (of those: %d are OUR OWN relocated spawns [expected, excluded by "
                 "design], %d are level-placed [IsNetStartupActor], %d are neither -- cause untested)."),
            WellCensusScanPass, WellCensusScanPass * 5, LiveCores, WellLayout.Num(), Unknown,
            UnknownOurs, UnknownLevelPlaced, Unknown - UnknownOurs - UnknownLevelPlaced);
    }

    // Delta-driven summary only. On a steady world every well is already at its assigned resource, so
    // MembersWritten is 0 and this pass prints nothing at all.
    if (MembersWritten > 0 || Mismatches > 0)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH1 pass: %d wells resolved (%d not streamed, %d skipped), satellites %d resolved / %d not "
                 "streamed, %d members written this pass (%d this session), %d resource-equality violations."),
            WellsResolved, WellsUnstreamed, WellsSkipped, SatsResolved, SatsMissing,
            MembersWritten, WellMembersWrittenThisSession, Mismatches);
    }
}
