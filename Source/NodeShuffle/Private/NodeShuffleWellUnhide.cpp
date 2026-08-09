// Packet ns-t23-rollhide (branch wip/h2-relocation): THE UN-HIDE, and the detector for the case where it
// never runs.
//
// WHY THIS FILE EXISTS AT ALL, IN ONE PARAGRAPH. T23 stage 3 moves the vanilla-well suppression from
// "after the replacement is built" to "at the roll". Suppression is a one-way door -- it hides the actor,
// hides every indexed mesh piece with its collision, and de-registers the node from the scanner, the
// representation manager and AFGResourceNodeManager::mResourceNodes, once per path, behind a
// session-scoped set, and NOTHING anywhere re-registers. Before the move that was survivable because a
// well that could never be placed was never hidden. After it, a well whose relocation terminally fails
// would be gone from the player's save FOREVER. The author accepted TEMPORARY absence. Permanent loss is
// a different trade and was not authorised, so the un-hide ships in the same commit as the hide.
//
// ------------------------------------------------------------------------------------------------
// THE PARITY LIST -- what the hide does, item by item, and how each item is undone.
// ------------------------------------------------------------------------------------------------
//  1. SetActorEnableCollision(false)  -> restored to the RECORDED prior value, not forced true.
//  2. SetActorHiddenInGame(true)      -> restored to the RECORDED prior value, not forced visible.
//     Both are recorded on the FALSE->TRUE transition of bSuppressedByUs only, so a re-assertion pass
//     cannot overwrite the record with our own hidden state. Another owner (the engine's significance
//     manager, this mod's ordinary-node suppression, another mod) may have hidden the member before we
//     ever saw it, and forcing visible+colliding would be a change we were never asked to make.
//  3. Per-COMPONENT mesh visibility AND collision (HideWellMemberMeshes) -> ShowWellMemberMeshes below.
//     EXACT within a session, from WellMeshPriorCollision; a documented default across a reload, because
//     UStaticMeshComponent identity is not path-stable and cannot be persisted (H2b-review F-3). The
//     count of pieces that had to use the default is PRINTED rather than folded into the total.
//  4. RemoveResourceNodeScan_Local / UpdateNodeRepresentation / DeregisterNodeFromManager.
//     * UpdateNodeRepresentation is re-called: it is what puts the compass/map representation back, and
//       it is the same call the existing re-roll restore uses.
//     * RegisterNodeWithManager is called FOR SATELLITES ONLY. That is not a rule this file chose: a
//       fracking CORE is an AFGResourceNodeBase and fails DeregisterNodeFromManager's own
//       Cast<AFGResourceNode>, so it was never in mResourceNodes and there is nothing to restore; a
//       SATELLITE is an AFGResourceNode, was removed, and must be re-added or it comes back visible and
//       NOT extractor-snappable. RegisterNodeWithManager is typed AFGResourceNode*, so it works for a
//       satellite and cannot take a core. The asymmetry is in the ENGINE's typing, and the code states
//       it as a Cast rather than as a comment.
//     * NOT UNDONE, and named rather than papered over: RemoveResourceNodeScan_Local has no inverse
//       reachable from here. The scanner ping is suppressed at the CLUSTER level by the AFGResourceScanner
//       hook in NodeShuffle.cpp, not per node, so a restored member's scanner behaviour follows that hook,
//       not this call. This is an ASSUMPTION about engine-internal state and is on the runtime checklist.
//  5. ScannerDeregistered (session set) -> the path is removed, so the once-per-path block can fire again
//     if the member is ever suppressed a second time. Without this the second suppression would hide the
//     member and leave it registered in mResourceNodes: an invisible snappable ghost, which is strictly
//     worse than the bug the suppression exists for.
//
// WHAT THE EXISTING PRECEDENT GETS WRONG, SO IT IS NOT COPIED. RestoreOriginalsForReroll
// (NodeShuffleSubsystem.cpp) restores actor collision, the hidden flag, the representation and
// radioactivity -- and never calls RegisterNodeWithManager. A solid original it un-hides is visible and
// collidable but absent from mResourceNodes, i.e. NOT Mk1-snappable. VISIBLE-BUT-NOT-BUILDABLE IS A
// FAILURE, NOT A PASS, and it is the acceptance criterion for this file (T23 K2).
//
// WHY THE INTENT IS PERSISTED AND RE-ATTEMPTED RATHER THAN CALLED. Un-hide can only run while the ORIGIN
// actors are resident, and both terminal-failure sites fire wherever the PLAYER is -- which under
// spawn-on-discovery is at the DESTINATION, kilometres away. A direct call would find nothing to restore
// and silently drop the obligation. So a failure sets FNodeShuffleWellEntry::bUnhidePending (SaveGame)
// and every apply pass re-attempts it until every member is discharged.
//
// IMPORT DISCIPLINE (memory: sf-shipping-export-trap). New engine/game surface reached from this file
// against what the suppression path already reached: NOTHING new on the Engine side (the same
// UStaticMeshComponent visibility/collision setters), and on the FactoryGame side only
// ANodeShuffleSubsystem::RegisterNodeWithManager -- which is OUR OWN member function, already called from
// three other sites in this module, so it adds no import. AFGResourceNodeBase::UpdateNodeRepresentation
// is already called by the suppression path itself. That is the PREDICTION; the import table is MEASURED
// after the build and anything that moves is named.

#include "NodeShuffleSubsystem.h"

#include "NodeShuffle.h"
#include "NodeShuffleConfig.h"
#include "NodeShuffleWellCensus.h"   // AFGResourceNodeFrackingCore / ...Satellite
#include "NodeShuffleWellRetype.h"   // WellPathOf / WellShort

#include "Resources/FGResourceNode.h"
#include "Components/StaticMeshComponent.h"

namespace
{
    // The documented fallback for a mesh piece whose pre-suppression collision was not recorded this
    // session (i.e. it was suppressed in an EARLIER session -- the map is transient because component
    // identity is not path-stable). QueryAndPhysics is what a vanilla well rock carries: it is the state
    // that makes the piece both a build-gun surface and a physical obstacle, and it is the only value
    // that can restore buildability, which is this packet's acceptance criterion. It is a DEFAULT, not a
    // measurement, and every use of it is counted and printed.
    constexpr ECollisionEnabled::Type WellMeshRestoreDefaultCollision = ECollisionEnabled::QueryAndPhysics;
}

// ------------------------------------------------------------------------------------------------
// MESH PIECES -- the inverse of HideWellMemberMeshes
// ------------------------------------------------------------------------------------------------
int32 ANodeShuffleSubsystem::ShowWellMemberMeshes(AFGResourceNodeBase* Node, int32& OutGuessed)
{
    if (!IsValid(Node)) { return 0; }
    const FString Path = WellPathOf(Node);
    const TArray<TWeakObjectPtr<UStaticMeshComponent>>* Pieces = WellMeshIndex.Find(Path);
    if (!Pieces) { return 0; }
    int32 Restored = 0;
    for (const TWeakObjectPtr<UStaticMeshComponent>& Weak : *Pieces)
    {
        UStaticMeshComponent* C = Weak.Get();
        if (!IsValid(C)) { continue; }
        const uint8* Packed = WellMeshPriorCollision.Find(C);
        // Packing is defined at the write site in NodeShuffleWellVisuals.cpp: low nibble = the
        // ECollisionEnabled value, bit 7 = was visible. Read here in the same order it is written there.
        const ECollisionEnabled::Type WantCollision = Packed
            ? static_cast<ECollisionEnabled::Type>(*Packed & 0x0F)
            : WellMeshRestoreDefaultCollision;
        const bool bWantVisible = Packed ? ((*Packed & 0x80) != 0) : true;
        if (!Packed) { ++OutGuessed; }
        // COLLISION FIRST AND UNCONDITIONALLY, mirroring the hide (ns-review-h2b F-5 established that
        // ordering on the other side): a piece restored to visible while still de-collided is a rock you
        // can see and cannot build on, which is the exact failure this packet's acceptance test looks for.
        if (C->GetCollisionEnabled() != WantCollision) { C->SetCollisionEnabled(WantCollision); }
        if (C->IsVisible() != bWantVisible) { C->SetVisibility(bWantVisible, true); }
        WellMeshPriorCollision.Remove(C);
        ++Restored;
    }
    return Restored;
}

// ------------------------------------------------------------------------------------------------
// ONE MEMBER
// ------------------------------------------------------------------------------------------------
bool ANodeShuffleSubsystem::UnhideWellMember(const FString& Path, FNodeShuffleWellSuppressionRecord& Rec,
                                             const FString& CoreLabel, const TCHAR* Kind, const TCHAR* Why)
{
    // Nothing owed. Returned as DISCHARGED so a group that was never suppressed (the toggle was off, or
    // its capture was incomplete and it took the spawn-then-suppress path) clears its intent immediately
    // instead of retrying forever against an obligation that does not exist.
    if (!Rec.bSuppressedByUs) { return true; }

    AFGResourceNodeBase* Node = FindOriginalBaseByPath(Path);
    if (!IsValid(Node))
    {
        // NOT resolved. Reported as the predicate that produced this branch -- "the path did not resolve
        // to a live actor" -- and NOT as a cause. Whether that is streaming, a destroyed actor or a stale
        // path is not tested here, and a diagnostic that named one of them would be asserting a cause it
        // never measured (memory: lessons-log-asserted-a-cause).
        return false;
    }

    int32 Guessed = 0;
    const int32 Restored = ShowWellMemberMeshes(Node, Guessed);

    // Actor level, from the RECORD, in the same order the hide wrote them.
    Node->SetActorEnableCollision(!Rec.bWasCollisionDisabledBefore);
    Node->SetActorHiddenInGame(Rec.bWasActorHiddenBefore);

    // Registration. SYMMETRY IS THE POINT OF THIS BLOCK: the representation call applies to BOTH sides
    // because the de-registration removed the representation of both; the manager call applies to the
    // satellite side ONLY because only the satellite side was ever removed from mResourceNodes.
    bool bReRegistered = false;
    if (Rec.bDeregisteredByUs)
    {
        Node->UpdateNodeRepresentation();
        if (AFGResourceNode* AsNode = Cast<AFGResourceNode>(Node))
        {
            RegisterNodeWithManager(AsNode);
            bReRegistered = true;
        }
        ScannerDeregistered.Remove(Path);
    }

    const bool bWasDeregistered = Rec.bDeregisteredByUs;
    const bool bRestoredHidden = Rec.bWasActorHiddenBefore;
    const bool bRestoredNoCollision = Rec.bWasCollisionDisabledBefore;
    Rec = FNodeShuffleWellSuppressionRecord();

    const FString Key = Path + TEXT("|unhide");
    if (!WellUnhideLogged.Contains(Key))
    {
        WellUnhideLogged.Add(Key);
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-UNHIDE core='%s' %s='%s' (%s): restored %d indexed mesh piece(s), %d of them to ")
            TEXT("the documented default because no pre-suppression state for that piece exists in this ")
            TEXT("session's record. Actor restored to the state recorded when we first hid it: hidden %d, ")
            TEXT("collision-disabled %d. We had de-registered it: %d; re-added to the node manager's ")
            TEXT("buildable list: %d (only an AFGResourceNode can be, and a fracking core is not one, so ")
            TEXT("a zero on a core is the engine's typing and not a missed step). ")
            TEXT("RemoveResourceNodeScan_Local has no inverse called here."),
            *CoreLabel, Kind, *WellShort(Path), Why, Restored, Guessed,
            bRestoredHidden ? 1 : 0, bRestoredNoCollision ? 1 : 0,
            bWasDeregistered ? 1 : 0, bReRegistered ? 1 : 0);
    }
    return true;
}

// ------------------------------------------------------------------------------------------------
// ONE GROUP -- drain the deferred intent
// ------------------------------------------------------------------------------------------------
bool ANodeShuffleSubsystem::TryUnhideWellGroup(FNodeShuffleWellEntry& E, const TCHAR* Why)
{
    const FString CoreLabel = WellShort(E.CorePath);
    int32 Owed = 0, Discharged = 0;

    // SYMMETRY: core first, then every satellite, through the SAME function and the SAME record type.
    // An uncaptured satellite is included deliberately -- it was suppressed with the rest of the group
    // (it is never spawned, so leaving it visible would strand it at the abandoned origin), which means
    // it carries the same obligation.
    if (E.CoreSuppression.bSuppressedByUs) { ++Owed; }
    if (UnhideWellMember(E.CorePath, E.CoreSuppression, CoreLabel, TEXT("core"), Why)) { ++Discharged; }
    for (FNodeShuffleWellSatellite& S : E.Satellites)
    {
        if (S.Suppression.bSuppressedByUs) { ++Owed; }
        if (UnhideWellMember(S.SatellitePath, S.Suppression, CoreLabel, TEXT("satellite"), Why))
        {
            ++Discharged;
        }
    }

    const int32 Members = 1 + E.Satellites.Num();
    const bool bComplete = (Discharged == Members);
    if (bComplete)
    {
        E.bUnhidePending = false;
        E.PassesSinceSuppressed = 0;
    }

    const FString Key = FString::Printf(TEXT("%s|%d|%d|%d"), *E.CorePath, Owed, Discharged, Members);
    if (!WellUnhideLogged.Contains(Key))
    {
        WellUnhideLogged.Add(Key);
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-UNHIDE core='%s' (%s): %d of %d member(s) owed a restore at the start of this ")
            TEXT("attempt; %d of %d member(s) are now discharged (a member owing nothing counts as ")
            TEXT("discharged). Intent still pending: %d. A member that is not discharged is one whose ")
            TEXT("original path did not resolve to a live actor on this pass; the intent is persisted and ")
            TEXT("re-attempted every apply pass."),
            *CoreLabel, Why, Owed, Members, Discharged, Members, bComplete ? 0 : 1);
    }
    return bComplete;
}

bool ANodeShuffleSubsystem::WellGroupHasSuppressedMember(const FNodeShuffleWellEntry& E) const
{
    if (E.CoreSuppression.bSuppressedByUs) { return true; }
    for (const FNodeShuffleWellSatellite& S : E.Satellites)
    {
        if (S.Suppression.bSuppressedByUs) { return true; }
    }
    return false;
}

// ------------------------------------------------------------------------------------------------
// THE ROLL-TIME CAPTURE GATE
// ------------------------------------------------------------------------------------------------
// STRICTER THAN CaptureWellGroupVisuals' OWN RETURN VALUE, on purpose. That function early-returns true
// whenever bGroupVisualsComplete is set, and that flag is STICKY across a re-enrolment which ADDED
// satellites -- so it can report COMPLETE for a group holding a freshly-captured satellite with no
// visual pieces at all. At apply time that costs nothing (the capture retries every pass while the
// origin streams). At ROLL time the capture is ONE SHOT, so believing a sticky flag would hide an origin
// whose look we do not actually hold, and the well would come back at the destination missing pieces
// with no way to recover them. This asks the per-member question directly.
bool ANodeShuffleSubsystem::IsWellGroupCaptureComplete(const FNodeShuffleWellEntry& E, int32& OutMembers,
                                                       int32& OutMissing) const
{
    OutMembers = 0;
    OutMissing = 0;
    ++OutMembers;
    if (!E.bCoreVisualsCaptured || E.CoreVisuals.Num() == 0) { ++OutMissing; }
    for (const FNodeShuffleWellSatellite& S : E.Satellites)
    {
        // Only CAPTURED satellites are ever spawned or dressed, so only they can be missing a look.
        // An uncaptured record is still suppressed with the group; it is simply not part of this question.
        if (!S.bCaptured) { continue; }
        ++OutMembers;
        if (!S.bVisualsCaptured || S.Visuals.Num() == 0) { ++OutMissing; }
    }
    return OutMissing == 0;
}

// ------------------------------------------------------------------------------------------------
// WELLH2-STRANDED -- the detector that ships WITH the hide (T23 §4)
// ------------------------------------------------------------------------------------------------
// A failure with no detector is the defect. Every existing stranding counter in this packet
// (WellVoidDefers, WellIncompleteSpawnCounts, WellClaimMidAssemblyPasses, ScannerDeregistered) is
// session-scoped, so a well stranded across a reload is invisible to all of them; PassesSinceSuppressed
// is SaveGame precisely so this line can survive one.
//
// LEGEND DISCIPLINE (this repo has shipped the trap twice, most recently a literal token that made a
// commit message's own recommended grep match every line): the fields below are described in PROSE and
// are never exemplified in the shape they print. To count occurrences of this census, anchor on the
// line prefix, which appears exactly once per line.
void ANodeShuffleSubsystem::EmitWellStrandedCensus()
{
    int32 TotalEntries = 0, TotalPlaced = 0, TotalSuppressed = 0, SuppressedAndPlaced = 0;
    int32 Failed = 0, DeferredNoPlayer = 0, VoidProbing = 0, Searching = 0, UnhidePending = 0;
    int32 WorstPasses = 0;
    FString WorstCore, FailedNames;

    for (const FNodeShuffleWellEntry& E : WellLayout)
    {
        ++TotalEntries;
        if (E.bGroupPlaced) { ++TotalPlaced; }
        if (!WellGroupHasSuppressedMember(E)) { continue; }
        ++TotalSuppressed;
        if (E.bGroupPlaced) { ++SuppressedAndPlaced; continue; }
        if (E.bUnhidePending) { ++UnhidePending; }

        // CLASSIFICATION ORDER IS FIXED AND STATED, because the buckets are not mutually exclusive by
        // construction and a reader summing them must know which one wins. Terminal failure first (it is
        // the only bucket that is permanent); then no-player-near-the-destination (the search cannot even
        // run); then void probing (the search ran and found no terrain); then searching.
        if (E.bRelocationFailed)
        {
            ++Failed;
            if (FailedNames.Len() < 400)
            {
                FailedNames += (FailedNames.IsEmpty() ? TEXT("") : TEXT(", "));
                FailedNames += WellShort(E.CorePath);
            }
        }
        else if (!IsLocationNearAnyPlayer(E.DestCoreLocation, WellLastApplySpawnRadiusCm))
        {
            ++DeferredNoPlayer;
        }
        else if (const int32* Voids = WellVoidDefers.Find(E.CorePath))
        {
            if (*Voids > 0) { ++VoidProbing; } else { ++Searching; }
        }
        else { ++Searching; }

        if (E.PassesSinceSuppressed > WorstPasses)
        {
            WorstPasses = E.PassesSinceSuppressed;
            WorstCore = WellShort(E.CorePath);
        }
    }

    const int32 SuppressedUnplaced = TotalSuppressed - SuppressedAndPlaced;
    if (TotalSuppressed == 0) { return; } // nothing suppressed by us: this census has nothing to report

    const FString Key = FString::Printf(TEXT("%d|%d|%d|%d|%d|%d|%d|%d|%d"), TotalEntries, TotalPlaced,
                                        TotalSuppressed, SuppressedUnplaced, Failed, DeferredNoPlayer,
                                        VoidProbing, Searching, UnhidePending);
    // Captured BEFORE the assignment below. Testing the key against its own freshly-written value in the
    // warning block would make that term dead and silently turn the warning into a pure cadence timer.
    const bool bPictureChanged = (Key != WellStrandedCensusLastKey);
    if (bPictureChanged)
    {
        WellStrandedCensusLastKey = Key;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-STRANDED pass %d: %d well entr(ies) in the layout, %d of them placed. We hold a ")
            TEXT("suppression record on %d of them; %d of those are placed (correctly suppressed) and %d ")
            TEXT("are NOT placed -- for those the player has no well at either end right now. Split of ")
            TEXT("the not-placed ones, in this classification order: relocation permanently FAILED %d, ")
            TEXT("no player near the destination %d, probing found no terrain %d, still searching %d. ")
            TEXT("Entries carrying a deferred restore intent: %d. Longest wait so far: %d apply pass(es) ")
            TEXT("on core '%s' (~5 s per pass; the counter survives a reload)."),
            WellAuditPasses, TotalEntries, TotalPlaced, TotalSuppressed, SuppressedAndPlaced,
            SuppressedUnplaced, Failed, DeferredNoPlayer, VoidProbing, Searching, UnhidePending,
            WorstPasses, WorstCore.IsEmpty() ? TEXT("<none>") : *WorstCore);
    }

    // THE WARNING BUCKET. Must read zero in a healthy save. Re-printed when the picture changes and, if
    // it does not, on the slow audit cadence -- a stranded well that printed once at minute 3 and never
    // again is a detector that reports the defect exactly once and then hides it.
    if (Failed > 0
        && (bPictureChanged || WellAuditPasses - WellStrandedWarnLastPass >= WellLinkAuditCadence))
    {
        WellStrandedWarnLastPass = WellAuditPasses;
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-STRANDED *** %d WELL(S) SUPPRESSED WITH NO PLACEMENT AND NO REMAINING BUDGET *** ")
            TEXT("out of %d entr(ies) we suppressed and %d in the layout. These wells are absent from the ")
            TEXT("world at BOTH ends: '%s'. %d of the %d carry a deferred restore intent, which completes ")
            TEXT("only while the ORIGINAL site is loaded. A non-zero count here names a well the player ")
            TEXT("has lost; it must read zero in a healthy save."),
            Failed, TotalSuppressed, TotalEntries,
            FailedNames.IsEmpty() ? TEXT("<none named>") : *FailedNames, UnhidePending, Failed);
    }
}

// ------------------------------------------------------------------------------------------------
// THE OPPOSITE-POLARITY PAIR (T23 §6)
// ------------------------------------------------------------------------------------------------
// This change spans two landings -- the ledger + un-hide, and the move of suppression to roll time -- and
// the dangerous state of a two-part change is not "broken", it is HALF APPLIED AND GREEN. So the pair is
// built as EXACT COMPLEMENTS over one population, which makes a both-green or both-red state
// unreachable without editing this function:
//
//   T23-A  "at least one entry is suppressed by us AND not placed, with the toggle on."
//          RED before roll-time hide lands (nothing suppresses an unplaced entry). GREEN after.
//   T23-B  "no member is hidden by us while its entry is not placed, with the toggle on."
//          GREEN before. RED after, at exactly the same instant A turns green.
//
// A ZERO DENOMINATOR IS NOT A PASS. With no enrolled, unplaced, dealt entries the question was never
// asked, and both verdicts print VACUOUS rather than PASS -- this repo has shipped a vacuous green twice.
// NEITHER ASSERTION IS SKIPPED OR IGNORED under any condition; the toggle only decides whether the
// population exists, and when it is off the line says so instead of disappearing.
void ANodeShuffleSubsystem::EmitWellRollHideTestPair(bool bCommitAtRoll)
{
    int32 Candidates = 0, SuppressedUnplaced = 0;
    for (const FNodeShuffleWellEntry& E : WellLayout)
    {
        if (E.bGroupPlaced) { continue; }
        if (!E.bDestDealt) { continue; }
        ++Candidates;
        if (WellGroupHasSuppressedMember(E)) { ++SuppressedUnplaced; }
    }

    const TCHAR* VerdictA = (Candidates == 0) ? TEXT("VACUOUS")
                          : (SuppressedUnplaced > 0 ? TEXT("PASS") : TEXT("FAIL"));
    const TCHAR* VerdictB = (Candidates == 0) ? TEXT("VACUOUS")
                          : (SuppressedUnplaced == 0 ? TEXT("PASS") : TEXT("FAIL"));

    const FString Key = FString::Printf(TEXT("%d|%d|%d"), bCommitAtRoll ? 1 : 0, Candidates,
                                        SuppressedUnplaced);
    if (Key == WellRollHideTestLastKey) { return; }
    WellRollHideTestLastKey = Key;

    UE_LOG(LogNodeShuffle, Display,
        TEXT("[NodeShuffle][TEST] T23-A %s | T23-B %s -- toggle 'Remove A Moved Well Immediately' is %s. ")
        TEXT("Population: %d entr(ies) that are dealt a destination and not yet placed; of those, %d have ")
        TEXT("at least one member in our suppression ledger. T23-A asserts that number is above zero, ")
        TEXT("T23-B asserts it is zero: they are exact complements over the same population, so exactly ")
        TEXT("one of them is red at any time and neither can be quietly skipped. VACUOUS means the ")
        TEXT("population was empty and the question was never asked -- it is not a pass. Before roll-time ")
        TEXT("removal landed, T23-A was the DELIBERATELY RED one; after it lands they swap."),
        VerdictA, VerdictB, bCommitAtRoll ? TEXT("ON") : TEXT("OFF"), Candidates, SuppressedUnplaced);
}
