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
int32 ANodeShuffleSubsystem::ShowWellMemberMeshes(AFGResourceNodeBase* Node, int32& OutGuessed,
                                                  bool& bOutIndexHadEntry, int32& OutIndexedForMember)
{
    if (!IsValid(Node)) { return 0; }
    const FString Path = WellPathOf(Node);
    const TArray<TWeakObjectPtr<UStaticMeshComponent>>* Pieces = WellMeshIndex.Find(Path);
    // ns-t23-rollhide REVIEW FIX (cold review F6): a bare 0 return here was indistinguishable from "this
    // member genuinely has no pieces", and the deal-failure restore can run during the roll BEFORE the
    // index exists. The caller must be able to tell the two apart or it discharges an obligation it never
    // performed.
    if (!Pieces) { bOutIndexHadEntry = false; return 0; }
    bOutIndexHadEntry = true;
    OutIndexedForMember = Pieces->Num();
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
    // Reached ONLY when the index had an entry, so this is the discharge of the mesh obligation. The
    // recorded count is hide EVENTS on distinct component instances, not live pieces, so it and Restored
    // are NOT expected to match and a gap is not evidence of a lost rock: a component that streamed out
    // and back is counted twice by the hide and once here. Nothing in this function measures which.
    WellMeshHiddenByUs.Remove(Path);
    WellMeshUnhideRetries.Remove(Path);
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
    bool bIndexHadEntry = false;
    int32 IndexedForMember = 0;
    const int32 Restored = ShowWellMemberMeshes(Node, Guessed, bIndexHadEntry, IndexedForMember);
    // ns-t23-rollhide -- COLD REVIEW F6. THE BEHAVIOUR HALF NOW LANDS, IN A DIFFERENT FORM, AND THIS
    // COMMENT IS THE RECORD OF WHY THE FIRST FORM WAS REFUSED, DATED 2026-08-09.
    // The finding is correct: a bare 0 from ShowWellMemberMeshes cannot distinguish "this member has no
    // pieces" from "the index has not been built yet", and the second case discharges an obligation that
    // was never performed. The FIRST spec's remedy -- return false and keep the intent pending whenever
    // the index holds no entry -- COULD NOT BE APPLIED TO THIS TREE. WellMeshIndex entries are created
    // only by AddPiece (NodeShuffleWellMeshIndex.cpp), i.e. only for a member with at least one PAIRED
    // piece, so "no entry" is the ORDINARY case for a member with no rock, not the exceptional case. The
    // packet's own stage-0 measurement on the author's save is 15 paired pieces across 135 members, so
    // that guard would have refused to discharge the large majority of members -- leaving them hidden,
    // never restored, and counted as stranded forever. That is a WORSE permanent-loss outcome than the
    // one F6 fixes, so it was reported instead of forced.
    // REVIEW-2 replaces the KEY rather than the guard: the obligation is now keyed on the pieces WE
    // ACTUALLY HID (WellMeshHiddenByUs, written by HideWellMemberMeshes), which is zero for a member with
    // no rock and therefore never gates it. The retry is BOUNDED and discharges LOUDLY when the budget is
    // spent, so no member can be held hidden forever by this path either.
    // ns-t23-rollhide REVIEW-2 (F6, THE BEHAVIOUR HALF). Keyed on pieces WE HID, never on the index.
    const int32* Owed = WellMeshHiddenByUs.Find(Path);
    const int32 StillOwed = Owed ? *Owed : 0;
    if (StillOwed > 0)
    {
        int32& Tries = WellMeshUnhideRetries.FindOrAdd(Path);
        ++Tries;
        const bool bBudgetSpent = (Tries >= WellMeshUnhideRetryBudget);
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-UNHIDE core='%s' %s='%s' (%s): the mesh index holds NO ENTRY for this member's ")
            TEXT("path on this pass, and %d recorded mesh-hide(s) for it are still undischarged. THAT ")
            TEXT("COUNT IS HIDE EVENTS ON DISTINCT COMPONENT INSTANCES, NOT A LIVE PIECE COUNT: a ")
            TEXT("streaming round trip at the origin re-creates a rock as a NEW component and the next ")
            TEXT("re-assertion hides it again, so it can exceed the number of rocks standing there. ")
            TEXT("Pieces restored on this pass and the index-entry flag are BOTH implied by this branch ")
            TEXT("rather than measured on it, so they are not printed. The index currently covers %d ")
            TEXT("member(s) that had at least one paired piece, out of %d well member(s) the last ")
            TEXT("rebuild walked. Attempt %d of %d. %s"),
            *CoreLabel, Kind, *WellShort(Path), Why, StillOwed,
            WellMeshIndex.Num(), WellMeshIndexMembers, Tries, WellMeshUnhideRetryBudget,
            bBudgetSpent
                ? TEXT("BUDGET SPENT -- discharging anyway. The member's ACTOR is restored and its rocks "
                       "may still be invisible and de-collided: check whether a Pressurizer will place "
                       "there. Holding it hidden forever is the worse failure, so it is not held.")
                : TEXT("NOT discharged: the member's ACTOR stays hidden and de-registered this pass, and "
                       "the intent is re-attempted on every apply pass on which the origin resolves."));
        if (!bBudgetSpent) { return false; }
        WellMeshHiddenByUs.Remove(Path);
        WellMeshUnhideRetries.Remove(Path);
    }

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
            TEXT("WELLH2-UNHIDE core='%s' %s='%s' (%s): restored %d of the %d indexed mesh piece(s) ")
            TEXT("recorded for this member, %d of them to ")
            TEXT("the documented default because no pre-suppression state for that piece exists in this ")
            TEXT("session's record. Actor restored to the state recorded when we first hid it: hidden %d, ")
            TEXT("collision-disabled %d. We had de-registered it: %d; re-added to the node manager's ")
            TEXT("buildable list: %d (only an AFGResourceNode can be, and a fracking core is not one, so ")
            TEXT("a zero on a core is the engine's typing and not a missed step). ")
            TEXT("RemoveResourceNodeScan_Local has no inverse called here."),
            *CoreLabel, Kind, *WellShort(Path), Why, Restored, IndexedForMember, Guessed,
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

// T68 (2026-08-11): WellGroupHasRollSuppressedMember WAS HERE AND IS DELETED with the T23 pair it fed.
// Its only caller was EmitWellRollHideTestPair. The SaveGame field it read (bSuppressedAtRoll) is kept
// -- old saves still carry Roll-stamped records -- but nothing reads the phase for a decision any more.

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
//
// ns-t24-groupgate -- WHAT CHANGED AND WHY, because the predicate here did not change at all: the
// POPULATION it is asked of did. The old version required a captured look from EVERY dressable member,
// which reads as "prove the capture succeeded" and is in fact "prove every member had something to
// capture". Most well members have no paired mesh piece by ANY of the index's three routes -- stage 0
// measured 15 pieces across 135 members -- so on the author's 2026-08-09 roll this refused 16 of 17
// committed wells, one of them reporting all 8 of 8 dressable members holding no captured look. Same
// error class as review-F6: "has no pieces" was read as "capture failed". A member with no indexed piece
// has nothing that can be lost by hiding it, so it cannot be evidence of an incomplete capture. A member
// that HAS pieces and still holds no look is a genuine partial capture and still refuses the group --
// that fallback is unchanged, and it is the only thing this gate now judges.
//
// MEASURED AT THIS INSTANT, from the live index. The caller runs EnsureRollMeshIndexOnce() and then
// CaptureWellGroupVisuals(E) immediately before this call, so the index this reads is the one the capture
// just worked from. Nothing here infers WHY a member has no pieces (not streamed / not paired / no mesh
// component at all are indistinguishable from here and this function tests none of them) -- it counts.
bool ANodeShuffleSubsystem::IsWellGroupCaptureComplete(const FNodeShuffleWellEntry& E, int32& OutDressable,
                                                       int32& OutWithPieces, int32& OutCaptured,
                                                       int32& OutMissing) const
{
    OutDressable = 0;
    OutWithPieces = 0;
    OutCaptured = 0;
    OutMissing = 0;

    // SYMMETRY: the core and every captured satellite go through ONE lambda, so the core cannot acquire a
    // different rule from the satellites through an edit that touches only one of the two call sites below.
    const auto Consider = [&](const FString& Path, bool bMemberCaptured, int32 CapturedPieces) -> void
    {
        ++OutDressable;
        const TArray<TWeakObjectPtr<UStaticMeshComponent>>* Indexed = WellMeshIndex.Find(Path);
        const int32 IndexedPieces = Indexed ? Indexed->Num() : 0;
        const bool bHoldsLook = bMemberCaptured && CapturedPieces > 0;
        if (bHoldsLook) { ++OutCaptured; }
        if (IndexedPieces > 0)
        {
            ++OutWithPieces;
            if (!bHoldsLook) { ++OutMissing; }
        }
    };

    Consider(E.CorePath, E.bCoreVisualsCaptured, E.CoreVisuals.Num());
    for (const FNodeShuffleWellSatellite& S : E.Satellites)
    {
        // Only CAPTURED satellites are ever spawned or dressed, so only they can be missing a look.
        // An uncaptured record is still suppressed with the group; it is simply not part of this question.
        if (!S.bCaptured) { continue; }
        Consider(S.SatellitePath, S.bVisualsCaptured, S.Visuals.Num());
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
//
// ============================ ns-t54-immediate-hide: THE POLARITY FLIPPED ========================
// THIS CENSUS'S HEADLINE NUMBER CHANGED MEANING ON 2026-08-10 AND THE CODE IS UNCHANGED, WHICH IS
// EXACTLY THE FAILURE MODE [[lessons-checklist-predates-the-feature]] DESCRIBES. Before T54,
// "suppressed but not placed" was a rare, suspicious state: suppression only happened AFTER the
// replacement existed, so a suppressed-and-unplaced entry meant something had gone wrong. After T54 it
// is the ORDINARY, INTENDED, TRANSIENT state of every well the roll marked as moving -- the author
// ruled the origin must disappear at once and accepted the window in which the resource exists nowhere.
// A reader who carries the old reading forward will see 18 of 20 and conclude the mod is broken.
//
// THE SPLIT IS THEREFORE STATED IN THE LINE ITSELF rather than left to a reader:
//   * TRANSIENT-AWAITING-PLACEMENT = the buckets where a search is running or will run again --
//     no player near the destination, probing found no terrain, still searching. Expected non-zero.
//     THIS IS NOT A DEFECT AND THE LINE MUST NOT BE READ AS REPORTING ONE.
//   * STUCK = the warning bucket below, unchanged in definition: permanently failed, nobody working on
//     it, keeps failing to assemble. Still must read zero in a healthy save.
// The buckets and the classification order are NOT changed by T54 -- only the reading of the total is,
// so nothing here is deleted and no counter is retired. The `NotWorked` bucket additionally now has a
// REMEDY as well as a warning: ApplyWellRelocation arms a persisted restore on exactly that predicate
// (ns-t54-immediate-hide), so a non-zero there should now drain over subsequent passes rather than sit.
void ANodeShuffleSubsystem::EmitWellStrandedCensus()
{
    int32 TotalEntries = 0, TotalPlaced = 0, TotalSuppressed = 0, SuppressedAndPlaced = 0;
    int32 Failed = 0, DeferredNoPlayer = 0, VoidProbing = 0, Searching = 0, UnhidePending = 0;
    // ns-t23-rollhide REVIEW FIX (cold review F2): three buckets the old chain could not name.
    int32 NotWorked = 0, StuckAssembling = 0, NoDestination = 0;
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
        // permanent); then nobody-is-working-on-it (the entry is no longer marked as relocating, so
        // no later bucket can be true of it); then stuck-assembling; then no destination dealt; then
        // no-player-near-the-destination; then void probing (the search ran and found no terrain); then
        // searching. Updated for cold review F2/F7 -- the previous order printed an entry with no
        // destination as "no player near the destination" and an entry no longer marked as relocating
        // as "still
        // searching", which are labels that lie.
        // ns-t23-rollhide REVIEW FIX (cold review F2): the warning population is "suppressed, unplaced and
        // NOT BEING WORKED ON", not bRelocationFailed alone. ONE state reaches that outcome with no
        // failure flag set: an entry that is no longer marked as relocating (a re-roll clears bRelocate;
        // until T71 the commoner route was the player switching relocation off, and that toggle is gone).
        // A second, distinct state is an entry whose spawn is permanently incomplete --
        // NoteWellIncompleteSpawn retries indefinitely by design and never stops.
        if (E.bRelocationFailed)
        {
            ++Failed;
            if (FailedNames.Len() < 400)
            {
                FailedNames += (FailedNames.IsEmpty() ? TEXT("") : TEXT(", "));
                FailedNames += WellShort(E.CorePath);
            }
        }
        // T71 cold review F1 (AUTHORED, 2026-08-13): the first disjunct was `!bWellLastApplyRelocationOn`,
        // which T71 made a compile-time false -- so this bucket is decided by !E.bRelocate ALONE and both
        // log lines BELOW (the Display split and the WELLH2-STRANDED Warning) now say exactly that. The dead disjunct is removed rather than left in place:
        // leaving it would keep a reader believing the bucket has two causes when it has one, which is the
        // same misreading the two rewritten sentences exist to stop.
        else if (!E.bRelocate)
        {
            ++NotWorked;
        }
        else
        {
            const int32* Stuck = WellIncompleteSpawnCounts.Find(E.CorePath);
            if (Stuck && *Stuck >= WellIncompleteSpawnWarnAt) { ++StuckAssembling; }
            // ns-t23-rollhide REVIEW-2 (F-C): TESTED BEFORE THE PROXIMITY TEST, NOT NESTED INSIDE IT.
            // An entry with no destination has DestCoreLocation at the world origin, so nested inside,
            // this bucket was decided by whether a player happened to be standing near (0,0,0) -- and the
            // stated order above claimed otherwise. IF THIS BUCKET NEVER READS NON-ZERO IN A REAL
            // SESSION, DELETE IT: a permanently-zero field that reads as a measurement is worse than an
            // absent one (TECH-DEBT T5).
            else if (!E.bDestDealt) { ++NoDestination; }
            else if (!IsLocationNearAnyPlayer(E.DestCoreLocation, WellLastApplySpawnRadiusCm))
            {
                ++DeferredNoPlayer;
            }
            else if (const int32* Voids = WellVoidDefers.Find(E.CorePath))
            {
                if (*Voids > 0) { ++VoidProbing; } else { ++Searching; }
            }
            else { ++Searching; }
        }

        if (E.PassesSinceSuppressed > WorstPasses)
        {
            WorstPasses = E.PassesSinceSuppressed;
            WorstCore = WellShort(E.CorePath);
        }
    }

    const int32 SuppressedUnplaced = TotalSuppressed - SuppressedAndPlaced;
    if (TotalSuppressed == 0) { return; } // nothing suppressed by us: this census has nothing to report

    // ns-t23-rollhide REVIEW FIX (cold review F2): the three new buckets are part of the picture, so they
    // are part of the key -- a key that cannot see a bucket cannot re-print when that bucket changes.
    // ns-t23-rollhide REVIEW FIX (cold review F9, option A -- the coarse bucket): WorstPasses grows every
    // pass and was absent from the key entirely, so the printed "longest wait" froze at the last picture
    // change while reading as current. Bucketed by 12 (~1 minute at ~5 s per pass) so a still-stranded
    // well re-states its age about once a minute instead of never, without re-printing every pass.
    const FString Key = FString::Printf(TEXT("%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d"), TotalEntries,
                                        TotalPlaced, TotalSuppressed, SuppressedUnplaced, Failed,
                                        NotWorked, StuckAssembling, NoDestination, DeferredNoPlayer,
                                        VoidProbing, Searching, UnhidePending, WorstPasses / 12);
    // Captured BEFORE the assignment below. Testing the key against its own freshly-written value in the
    // warning block would make that term dead and silently turn the warning into a pure cadence timer.
    const bool bPictureChanged = (Key != WellStrandedCensusLastKey);
    if (bPictureChanged)
    {
        WellStrandedCensusLastKey = Key;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("WELLH2-STRANDED pass %d: %d well entr(ies) in the layout, %d of them placed. We hold a ")
            TEXT("suppression record on %d of them; %d of those are placed (correctly suppressed) and %d ")
            TEXT("are NOT placed -- for those the player has no well at either end right now. SINCE T54 ")
            TEXT("THAT LAST NUMBER IS EXPECTED TO BE NON-ZERO AND IS NOT BY ITSELF A DEFECT: the author ")
            TEXT("ruled on 2026-08-10 that a shuffled origin disappears immediately and accepted the ")
            TEXT("window in which the resource exists nowhere, so every well waiting for a player to ")
            TEXT("reach its destination sits here by design. STILL A DEFECT: the FAILED, nobody-working ")
            TEXT("and keeps-failing-to-assemble buckets, which are the three the WARNING line counts, ")
            TEXT("plus never-dealt-a-destination, which is an invariant break rather than a wait. ")
            TEXT("TRANSIENT-AWAITING-PLACEMENT: no-player-near, probing-found-no-terrain and ")
            TEXT("still-searching. Split of ")
            TEXT("the not-placed ones, in this classification order: relocation permanently FAILED %d, ")
            TEXT("nobody is working on it: this entry is no longer marked as relocating ")
            TEXT("(bRelocate=0) %d, keeps failing to assemble at its destination %d, has never been ")
            TEXT("dealt a ")
            TEXT("destination %d (this is expected to be structurally zero: every writer that leaves an ")
            TEXT("unplaced entry undealt also clears bRelocate, so a non-zero here means that invariant ")
            TEXT("broke), no player near the destination %d, probing found no terrain %d, still ")
            TEXT("searching %d. ")
            TEXT("Entries carrying a deferred restore intent: %d. Longest wait so far: %d apply pass(es) ")
            TEXT("on core '%s' (~5 s per pass; the counter survives a reload)."),
            WellAuditPasses, TotalEntries, TotalPlaced, TotalSuppressed, SuppressedAndPlaced,
            SuppressedUnplaced, Failed, NotWorked, StuckAssembling, NoDestination, DeferredNoPlayer,
            VoidProbing, Searching, UnhidePending,
            WorstPasses, WorstCore.IsEmpty() ? TEXT("<none>") : *WorstCore);
    }

    // THE WARNING BUCKET. Must read zero in a healthy save. Re-printed when the picture changes and, if
    // it does not, on the slow audit cadence -- a stranded well that printed once at minute 3 and never
    // again is a detector that reports the defect exactly once and then hides it.
    // ns-t23-rollhide REVIEW FIX (cold review F2): the warning gate was bRelocationFailed alone, which is
    // blind to two populations with the IDENTICAL player outcome -- well absent at both ends, and nothing
    // running that could ever put it back. Only the FAILED bucket carries names, because it is the only
    // one that names itself; the other two are counted and identified by bucket.
    const int32 NoRouteBack = Failed + NotWorked + StuckAssembling;
    if (NoRouteBack > 0
        && (bPictureChanged || WellAuditPasses - WellStrandedWarnLastPass >= WellLinkAuditCadence))
    {
        WellStrandedWarnLastPass = WellAuditPasses;
        UE_LOG(LogNodeShuffle, Warning,
            TEXT("WELLH2-STRANDED *** %d WELL(S) ARE SUPPRESSED BY US, NOT PLACED, AND IN A STATE WHERE ")
            TEXT("NO SEARCH FOR A DESTINATION IS RUNNING *** out of %d entr(ies) we suppressed and %d in ")
            TEXT("the layout. Which kind: relocation ")
            TEXT("permanently FAILED %d, no longer marked as relocating while its origin was already ")
            TEXT("removed %d, keeps failing to assemble at its destination %d. THE FIRST TWO KINDS ARE ")
            TEXT("ABSENT AT ")
            TEXT("BOTH ENDS. THE KEEPS-FAILING-TO-ASSEMBLE KIND IS NOT: an incomplete spawn leaves the ")
            TEXT("members that DID spawn standing at the destination, so that bucket is a PARTIAL well, ")
            TEXT("not an absent one. Named where we have names (the FAILED kind only): '%s'. THIS LINE ")
            TEXT("DOES NOT TEST FOR PERMANENT LOSS AND MUST NOT BE READ AS ONE: %d of the %d ")
            TEXT("suppressed-and-unplaced entr(ies) carry a deferred restore intent that IS re-attempted ")
            TEXT("every apply pass, and it completes only while the ORIGINAL site is loaded. It must read ")
            TEXT("zero in a healthy save."),
            NoRouteBack, TotalSuppressed, TotalEntries, Failed, NotWorked, StuckAssembling,
            FailedNames.IsEmpty() ? TEXT("<none named>") : *FailedNames, UnhidePending,
            SuppressedUnplaced);
    }
}

// T68 (2026-08-11): THE T23 OPPOSITE-POLARITY PAIR (EmitWellRollHideTestPair) WAS HERE AND IS RETIRED.
// It asked what the roll-time removal toggle did; audit §5.2 deleted that toggle and its roll-time arm,
// so the pair has no subject left and would print VACUOUS forever. A pair is retired when its FEATURE is
// removed -- never to make a red half green. tools/check_t23_writers.ps1 goes in the same commit.
