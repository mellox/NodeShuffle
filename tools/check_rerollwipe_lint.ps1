# REROLLWIPE lint (2026-10-31, packet rwg-rerollwipe-1): the re-roll wipe drops each destroyed node from the resource-node manager's
# raw Mk1 snap list (mResourceNodes), its representation and its scan reveal BEFORE Destroy, and prints one ungated REROLLWIPE census
# whose stale count is a pointer compare. Brief: _team/test-logs/2026-10-31-nodeshuffle-reroll-wipe-brief.md.
# LOADS and PINS: NodeShuffleSubsystem.cpp (the wipe call sites), NodeShuffleRerollWipe.cpp/.h (helper + census), NodeShuffleSubsystem.h
# (declarations), NodeShuffle.cpp (marker), NodeShuffleWellDespawn.cpp (review F1: the well-member teardown).
#   R1 wipe order: Begin -> iterate -> [IsValid: teardown helper, then Destroy] -> Unregister -> RemoveCurrent -> MeshActorCache.Reset -> census
#   R2 helper order: snap-list RemoveSingleSwap (return summed), representation by actor, by node, scan reveal, UpdateNodeRepresentation;
#      no Destroy and no SpawnedNodes write inside the helper
#   R3 stale counts are Contains over the recorded addresses; no dereference of a recorded address; no derived-formula shortcut
#   R4 census format string and argument order, field list exact; each census field token appears once in the file
#   R5 census ungated (no diagnostics gate in the census function)
#   R6 marker rwg-rerollwipe-1 once on its own line in NodeShuffle.cpp, farspawn-1f LOADED line still once; no marker in the new files
#   R7 new .cpp under 500 lines; RemoveSingleSwap stays a single (DeregisterNodeFromManager) site in Subsystem.cpp
#   R8 header declarations + the include
#   R11 (review F3) REROLLAUDIT: one ungated call after RollLayout's EmitRollCensus statement, as the function's last statement (every roll; after the density trigger, which check_density_lint D5 pins adjacent to EmitRollCensus); whole-list pointer compare
#      against TActorIterator, no dereference, no gate; exact format/args
#   R12 (re-review A1) before-wipe snapshot RerollWipeCollectNotYielded: one call ahead of the wipe block and the audit call; same predicate, no
#      dereference, no gate; REROLLAUDIT's notYieldedNewSinceWipe excludes it (the per-roll control, same frame)
#   R13 (re-review S1) EnsureNewNodeSpawned external-destroy: dead address dropped from the snap list via the helper before Unregister/slot removal
#   R10 (review F2) RerollWipeNoteInvalidSlot: null counted; non-null: Contains measured BEFORE RemoveSingleSwap, by address, no dereference, no Destroy
#   R9 (review F1) DestroyWellMemberIfUnused: after DeregisterNodeFromManager (which refuses OUR nodes), an explicit snap-list RemoveSingleSwap
#      and representation removal (by actor, by node), all BEFORE Destroy; the WELLDESPAWN-SNAP line is diagnostics-gated; the three includes
# LOGFLOOD (2026-11-01, packet rwg-logflood-1; handoff _team/test-logs/2026-11-01-nodeshuffle-logflood-handoff.md). Extends this lint
# because no lint loaded NodeShuffleWellMeshIndex.cpp and this one already pins the packet-marker banner. LOADS NodeShuffleWellMeshIndex.cpp
# and NodeShuffleWellMeshIndexDump.cpp/.h. RebuildWellMeshIndex's per-entry Verbose lines print only when the signature changes:
#   L1 flush: signature compare decides the dump; stored signature/entries/skip counter written only on a dump, after the census
#   L2 census: exact format + args, Verbose, printed on every flush BEFORE the dump decision returns; each census token once (no legend echo)
#   L3 reset-on-world: one ResetWellIndexDumpForWorld call right after ResetWellSnapBoxDiagForWorld; the body clears all stored state
#   L4 gate: buffering active only when diagnostics on AND Verbose active; one flush call, before the pass summary
#   L5 shortcut forbidden: neither per-entry literal inline in NodeShuffleWellMeshIndex.cpp; each once in the dump file, unchanged text
#   L6 signature: every printed field except the location is hashed; order-independent sum; the entry count folded in
#   L7 two buffering sites (accepted, REJECT) in route 3
#   L8 marker rwg-logflood-1 once on its own line in NodeShuffle.cpp; never in the packet files
#   L9 new files under 500 lines; L10 header declarations
# Mutants run in-file. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    subh  = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    wipe  = 'NodeShuffle\Private\NodeShuffleRerollWipe.cpp'
    wipeh = 'NodeShuffle\Private\NodeShuffleRerollWipe.h'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
    welld = 'NodeShuffle\Private\NodeShuffleWellDespawn.cpp'
    wmi   = 'NodeShuffle\Private\NodeShuffleWellMeshIndex.cpp'       # rwg-logflood-1
    wmid  = 'NodeShuffle\Private\NodeShuffleWellMeshIndexDump.cpp'   # rwg-logflood-1
    wmidh = 'NodeShuffle\Private\NodeShuffleWellMeshIndexDump.h'     # rwg-logflood-1
}
$text = [ordered]@{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = ((Get-Content -Raw -Encoding utf8 -Path $p) -replace "`r`n", "`n")
}
function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }
function Ordered([string]$t, [string[]]$needles) {
    $at = 0
    foreach ($n in $needles) { $i = $t.IndexOf($n, $at); if ($i -lt 0) { return $false }; $at = $i + $n.Length }
    return $true
}
function Span([string]$t, [string]$a, [string]$b) {
    if ((Count $t $a) -ne 1) { return '' }
    $i = $t.IndexOf($a); $j = $t.IndexOf($b, $i + $a.Length)
    if ($j -lt 0) { return '' }
    return $t.Substring($i, $j - $i)
}

$censusFmt = 'TEXT("REROLLWIPE: destroyed=%d removedFromManager=%d staleInManagerAfter=%d representationsRemoved=%d managerCountBefore=%d managerCountAfter=%d kept=%d repByActor=%d repByNode=%d invalidSlots=%d invalidSlotsStillInManager=%d noManager=%d noRepManager=%d invalidSlotsNull=%d invalidInManagerAtWipe=%d invalidRemovedFromManager=%d destroyRefused=%d nodeRepPresentAfterUpdate=%d")'
$censusArgs = "        T.Destroyed, T.RemovedFromManager, StaleAfter, T.RepAny, T.ManagerCountBefore, ManagerCountAfter, T.Kept,`n        T.RepByActor, T.RepByNode, T.InvalidSlots, InvalidStillIn, T.NoManager, T.NoRepManager, T.InvalidSlotsNull, T.InvalidInManagerAtWipe, T.InvalidRemovedFromManager,`n        T.DestroyRefused, T.RepPresentAfterUpdate);"
$teardownLine = '            if (IsValid(It->Value)) { RerollWipeTeardownRegistrations(It->Value, WipeTally); if (!It->Value->Destroy()) { ++WipeTally.DestroyRefused; } }'
$staleLoop = "        StaleAfter = 0;`n        for (AFGResourceNode* Ptr : T.DestroyedPtrs) { if (Mgr->mResourceNodes.Contains(Ptr)) { ++StaleAfter; } }"
$invalidLoop = "        InvalidStillIn = 0;`n        for (AFGResourceNode* Ptr : T.InvalidPtrs) { if (Mgr->mResourceNodes.Contains(Ptr)) { ++InvalidStillIn; } }"
$marker = '2026-10-31-rwg-' + 'rerollwipe-1'
$auditCall = "{ RwgDensityOnRoll(); } // rwg-density-1: RWGDENSITY trigger=roll (diagnostics only)`n    RerollWipeEmitListAudit(bIsReroll, AuditNotYieldedBeforeWipe); // rwg-rerollwipe-1 review F3 / re-review A1: every roll; notYieldedNewSinceWipe is the per-roll control`n}`n"
$auditFmt = 'TEXT("REROLLAUDIT: reroll=%d managerEntries=%d entriesNotYieldedByActorIterator=%d notYieldedBeforeWipe=%d notYieldedNewSinceWipe=%d actorsIterated=%d"),'
$auditArgs = '        bIsReroll ? 1 : 0, Mgr->mResourceNodes.Num(), NotIterated, NotYieldedBeforeWipe.Num(), NewSinceWipe, Iterated.Num());'
# re-review A1: the before-wipe snapshot call (R12), ordered before the wipe block and the audit call
$snapCall = '    RerollWipeCollectNotYielded(AuditNotYieldedBeforeWipe);'
# re-review S1
$s1Call = '        const int32 DeadSnapDropped = RerollWipeDropDeadSlotAddress(*Existing);'
$s1Helper = ('{ if (!Node) { return 0; } AFGResourceNodeManager* Mgr = GetNodeManager(); return Mgr ? Mgr->mResourceNodes.RemoveSingle' + 'Swap(Node) : 0; }')
$auditLoop = 'for (const AFGResourceNode* Ptr : Mgr->mResourceNodes) { if (!Iterated.Contains(Ptr)) { ++NotIterated; if (!NotYieldedBeforeWipe.Contains(Ptr)) { ++NewSinceWipe; } } }'
# review F1: the WHOLE well-member snap-list drop line, so mutant Q20 moves the identical text after Destroy (order, not presence)
$wellSnapLine = "    if (AFGResourceNode* AsNode = Cast<AFGResourceNode>(Member)) { if (AFGResourceNodeManager* Mgr = GetNodeManager()) { SnapDropped = Mgr->mResourceNodes.RemoveSingle" + "Swap(AsNode); } }`n"

# ---------------- LOGFLOOD (rwg-logflood-1) needles; every one built by concatenation ----------------
$lfMarker = '2026-11-01-rwg-' + 'logflood-1'
$lfCensusFmt = '        TEXT("WELLH2B-INDEX spatial ' + 'CENSUS: pass=%d entries=%d accepted=%d rejected=%d narrowedByType=%d widenedByType=%d bystanderRejects=%d worldReset=%d sigChanged=%d dumping=%d prevEntries=%d skippedSinceDump=%d sig=%08x%08x"),'
$lfCensusArgs = "        D.Pass, D.Entries.Num(), D.Accepted, D.Rejected, D.NarrowedByType, D.WidenedByType, D.BystanderRejects,`n" +
    "        D.bWorldReset ? 1 : 0, bSigChanged ? 1 : 0, bDumping ? 1 : 0, WellIndexDumpEntries, WellIndexDumpSkipped,`n" +
    '        static_cast<uint32>(Sig >> 32), static_cast<uint32>(Sig & 0xffffffffull));'
$lfDecide = '    const bool bDumping = !bHasPrev ' + '|| bSigChanged;'
$lfCensusHead = "    if (!bDumping) { ++WellIndexDumpSkipped; }`n    UE_LOG(LogNodeShuffle, Verbose,`n" + $lfCensusFmt
$lfAcceptLit = 'TEXT("WELLH2B-INDEX spatial' + ': '''
$lfRejectLit = 'TEXT("WELLH2B-INDEX spatial ' + 'REJECT: '
$lfAcceptText = $lfAcceptLit + "%s' (nodeMeshType=%d) on actor '%s' at %s -> member `")`n" + '                TEXT("''%s'' at %.0f cm (runner-up %.0f cm, radius %.0f cm). Nearest-wins; a runner-up ")'
$lfRejectText = $lfRejectLit + "'%s' (nodeMeshType=%d) on '%s' at %s -- nearest well `")`n" + '                TEXT("member ''%s'' is %.0f cm away, radius is %.0f cm, inRadius=%d. The type gate ")'
$lfResetCall = '    const bool bDumpWorldReset = ResetWellIndex' + 'DumpForWorld(World); // rwg-logflood-1: a fresh world dumps once'
$lfGate = '    SpatialDump.bActive = bDiag && UE_LOG_' + 'ACTIVE(LogNodeShuffle, Verbose);'
$lfFlushCall = '        FlushWellIndex' + 'SpatialDump(SpatialDump);'
function Invoke-LogFloodChecks($T) {
    $f = @()
    $flush = Span $T.wmid 'void ANodeShuffleSubsystem::FlushWellIndexSpatialDump(' 'for (const FNodeShuffleWellIndexSpatialEntry& E : D.Entries)'
    # L1
    if ($flush -eq '') { $f += '(L1) FlushWellIndexSpatialDump span missing or duplicated' }
    elseif (-not (Ordered $flush @('    const uint64 Sig = D.Signature();', '    const bool bHasPrev = WellIndexDumpEntries >= 0;',
        '    const bool bSigChanged = bHasPrev && Sig != WellIndexDumpSig;', $lfDecide, '    if (!bDumping) { ++WellIndexDumpSkipped; }',
        $lfCensusFmt, '    if (!bDumping) { return; }', '    WellIndexDumpSig = Sig;', '    WellIndexDumpEntries = D.Entries.Num();', '    WellIndexDumpSkipped = 0;'))) {
        $f += '(L1) flush: signature compare decides the dump; stored state written after the census, only on a dump' }
    if ((Count $T.wmid $lfDecide) -ne 1 -or (Count $T.wmid ('if (!bDumping) { ' + 'return; }')) -ne 1 -or (Count $T.wmid ('WellIndexDumpSig = ' + 'Sig;')) -ne 1) {
        $f += '(L1) one dump decision, one early return, one signature store' }
    # L2
    if ((Count $T.wmid $lfCensusHead) -ne 1 -or (Count $T.wmid $lfCensusArgs) -ne 1) { $f += '(L2) census: Verbose, exact format and args, printed before the dump decision returns' }
    foreach ($tok in @(' pass=', ' entries=', ' accepted=', ' rejected=', ' narrowedByType=', ' widenedByType=', ' bystanderRejects=', ' worldReset=',
        ' sigChanged=', ' dumping=', ' prevEntries=', ' skippedSinceDump=', ' sig=')) {
        if ((Count $T.wmid $tok) -ne 1 -or (Count $T.wmi $tok) -ne 0 -or (Count $T.wmidh $tok) -ne 0) { $f += "(L2) census token '$tok' must appear once, in the census only (no legend echo)" }
    }
    # L3
    if ((Count $T.wmi ('ResetWellIndex' + 'DumpForWorld(')) -ne 1 -or -not (Ordered $T.wmi @('    ResetWellSnapBoxDiagForWorld(World);', $lfResetCall,
        '    SpatialDump.bWorldReset = bDumpWorldReset;'))) { $f += '(L3) one reset call right after ResetWellSnapBoxDiagForWorld, its result on the census' }
    $reset = Span $T.wmid 'bool ANodeShuffleSubsystem::ResetWellIndexDumpForWorld(' 'void ANodeShuffleSubsystem::FlushWellIndexSpatialDump('
    if ($reset -eq '' -or -not (Ordered $reset @('if (WellIndexDumpWorld.HasSameIndexAndSerialNumber(Incoming)) { return false; }', '    WellIndexDumpWorld = Incoming;',
        '    WellIndexDumpSig = 0;', ('    WellIndexDumpEntries = ' + '-1;'), '    WellIndexDumpSkipped = 0;', '    return true;'))) { $f += '(L3) reset body: index+serial compare, then clears signature, entries and skip counter' }
    # L4
    if ((Count $T.wmi $lfGate) -ne 1) { $f += '(L4) buffering gate: diagnostics AND Verbose active' }
    if ((Count $T.wmi ('FlushWellIndex' + 'SpatialDump(')) -ne 1 -or -not (Ordered $T.wmi @('    if (SpatialDump.bActive) // rwg-logflood-1: census every rebuild', $lfFlushCall,
        'TEXT("WELLH2B-INDEX pass %d: '))) { $f += '(L4) one gated flush call, before the pass summary' }
    # L5
    if ((Count $T.wmi $lfAcceptLit) -ne 0 -or (Count $T.wmi $lfRejectLit) -ne 0) { $f += '(L5) a per-entry spatial line is printed inline in NodeShuffleWellMeshIndex.cpp (bypasses the dedupe)' }
    if ((Count $T.wmid $lfAcceptText) -ne 1 -or (Count $T.wmid $lfRejectText) -ne 1 -or (Count $T.wmid $lfAcceptLit) -ne 1 -or (Count $T.wmid $lfRejectLit) -ne 1) {
        $f += '(L5) each per-entry line once in the dump file with its original text' }
    # L6
    $add = Span $T.wmid '    uint64 H = NsFnvOffset;' '    SigSum += H;'
    if ($add -eq '' -or -not (Ordered $add @('bInAccepted ? 1ull : 2ull', 'InMeshType', 'NsFnvStr(H, InMeshName);', 'NsFnvStr(H, InActorName);', 'NsFnvStr(H, InMemberShort);',
        'FMath::RoundToInt(InDistCm)', 'FMath::RoundToInt(InRunnerUpCm)', 'bInInRadius ? 1ull : 0ull'))) { $f += '(L6) the signature must hash every printed field but the location' }
    if ($add -ne '' -and (Count $add ('In' + 'Loc')) -ne 0) { $f += '(L6) the location must not be hashed' }
    if ((Count $T.wmid '    SigSum += H;') -ne 1 -or (Count $T.wmid 'return NsFnvU64(SigSum, static_cast<uint64>(Entries.Num()));') -ne 1) { $f += '(L6) order-independent sum with the entry count folded in' }
    $sigBody = Span $T.wmid 'uint64 FNodeShuffleWellIndexSpatialDump::Signature() const' '// Forget the last printed signature'
    if ($sigBody -eq '' -or ($sigBody -replace '\s+', ' ').Trim() -ne 'uint64 FNodeShuffleWellIndexSpatialDump::Signature() const { return NsFnvU64(SigSum, static_cast<uint64>(Entries.Num())); }') { $f += '(L6) Signature() body must be exactly the folded sum (a constant signature never re-dumps)' }
    # L7
    if ((Count $T.wmi 'SpatialDump.Add(/*bAccepted=*/true,') -ne 1 -or (Count $T.wmi 'SpatialDump.Add(/*bAccepted=*/false,') -ne 1 -or (Count $T.wmi 'SpatialDump.Add(') -ne 2 -or
        (Count $T.wmi '                if (SpatialDump.bActive) // rwg-logflood-1: the accepted-piece line') -ne 1 -or
        (Count $T.wmi '            else if (SpatialDump.bActive && Best != nullptr) // rwg-logflood-1: the REJECT line') -ne 1) { $f += '(L7) two gated buffering sites (accepted, REJECT)' }
    # L8
    if ((Count $T.nsmod ('    UE_LOG(LogNodeShuffle, Display, TEXT("===== NodeShuffle packet marker (' + $lfMarker + ')====="));')) -ne 1 -or (Count $T.nsmod $lfMarker) -ne 1) {
        $f += '(L8) marker rwg-logflood-1 once on its own line' }
    if ((Count $T.wmi $lfMarker) -ne 0 -or (Count $T.wmid $lfMarker) -ne 0 -or (Count $T.wmidh $lfMarker) -ne 0) { $f += '(L8) no marker inside the packet files' }
    # L9 / L10
    if (($T.wmid -split "`n").Count -ge 500 -or ($T.wmidh -split "`n").Count -ge 500) { $f += '(L9) new files must stay under 500 lines' }
    foreach ($d in @('bool ResetWellIndexDumpForWorld(const class UWorld* World);', 'void FlushWellIndexSpatialDump(const struct FNodeShuffleWellIndexSpatialDump& D);',
        'TWeakObjectPtr<const UWorld> WellIndexDumpWorld;', ('int32 WellIndexDumpEntries = ' + '-1;'))) {
        if ((Count $T.subh $d) -ne 1) { $f += "(L10) header declaration missing: $d" }
    }
    if ((Count $T.wmi '#include "NodeShuffleWellMeshIndexDump.h"') -ne 1 -or (Count $T.wmid '#include "NodeShuffleWellMeshIndexDump.h"') -ne 1) { $f += '(L10) include' }
    return $f
}

function Invoke-RerollWipeChecks($T) {
    $f = @()
    # R1
    $wipeBlock = Span $T.sub '        TSet<FGuid> KeptGuids;' 'RerollWipeEmitCensus(WipeTally); // rwg-rerollwipe-1'
    if ($wipeBlock -eq '') { $f += '(R1) the wipe block span (KeptGuids .. census call) is missing or duplicated' }
    else {
        if (-not (Ordered $wipeBlock @('RerollWipeBegin(WipeTally);', 'SpawnedNodes.CreateIterator()', '{ ++WipeTally.Kept; continue; }', $teardownLine,
            '            else { RerollWipeNoteInvalidSlot(It->Value, WipeTally); }', '            FNodeShuffleModule::UnregisterManagedNode(It->Value);',
            '            It.RemoveCurrent();', '        MeshActorCache.Reset();'))) { $f += '(R1) wipe order: Begin, teardown-then-Destroy under IsValid, Unregister, RemoveCurrent, Reset' }
        if ((Count $wipeBlock $teardownLine) -ne 1 -or (Count $wipeBlock ('->Dest' + 'roy()')) -ne 1) { $f += '(R1) exactly one Destroy, preceded on its line by the teardown helper' }
    }
    if ((Count $T.sub 'RerollWipeTeardownRegistrations(') -ne 1 -or (Count $T.sub 'RerollWipeEmitCensus(') -ne 1 -or (Count $T.sub 'RerollWipeBegin(') -ne 1) { $f += '(R1) one call site each for Begin, teardown and census' }
    # R2
    $helper = Span $T.wipe 'void ANodeShuffleSubsystem::RerollWipeTeardownRegistrations(' 'void ANodeShuffleSubsystem::RerollWipeNoteInvalidSlot('
    if ($helper -eq '') { $f += '(R2) helper span missing' }
    else {
        if (-not (Ordered $helper @('T.DestroyedPtrs.Add(Node);', 'Removed = Mgr->mResourceNodes.RemoveSingleSwap(Node); T.RemovedFromManager += Removed;',
            'bByActor = RepMgr->RemoveRepresentationOfActor(Node);', 'RepMgr->FindResourceNodeRepresentation(Node)', 'RepMgr->RemoveRepresentation(NodeRep); bByNode = true;',
            '    Node->RemoveResourceNodeScan_Local();', '    Node->UpdateNodeRepresentation();',
            'if (RepMgr2->FindResourceNodeRepresentation(Node)) { ++T.RepPresentAfterUpdate; }'))) { $f += '(R2) helper order: snap list, rep by actor, rep by node, scan, UpdateNodeRepresentation, then the review-F6 presence measurement' }
        if ((Count $helper ('->Dest' + 'roy();')) -ne 0 -or (Count $helper 'SpawnedNodes') -ne 0) { $f += '(R2) the helper must not Destroy or write SpawnedNodes' }
    }
    # R3
    $census = Span $T.wipe 'void ANodeShuffleSubsystem::RerollWipeEmitCensus(' 'TEXT("REROLLWIPE: '
    if ($census -eq '') { $f += '(R3) census span missing' }
    else {
        if ((Count $census $staleLoop) -ne 1 -or (Count $census $invalidLoop) -ne 1) { $f += '(R3) stale counts must be Contains over the recorded addresses' }
        if ((Count $census ('IsVal' + 'id(')) -ne 0 -or (Count $census ('Ptr' + '->')) -ne 0 -or (Count $census ('(*' + 'Ptr')) -ne 0 -or (Count $census ('Ptr' + '.')) -ne 0) { $f += '(R3) a recorded address is dereferenced' }
        if ((Count $census ('StaleAfter = ' + 'T.')) -ne 0 -or (Count $census ('StaleAfter = ' + 'ManagerCount')) -ne 0) { $f += '(R3) the stale count is derived from a formula instead of measured' }
    }
    # R4
    if ((Count $T.wipe $censusFmt) -ne 1 -or (Count $T.wipe $censusArgs) -ne 1) { $f += '(R4) census field list / argument order' }
    foreach ($tok in @('destroyed=', 'removedFromManager=', 'staleInManagerAfter=', 'representationsRemoved=', 'managerCountBefore=', 'managerCountAfter=', 'invalidSlotsNull=', 'invalidInManagerAtWipe=', 'invalidRemovedFromManager=', 'destroyRefused=', 'nodeRepPresentAfterUpdate=', 'managerEntries=', 'entriesNotYieldedByActorIterator=', 'notYieldedBeforeWipe=', 'notYieldedNewSinceWipe=', 'actorsIterated=')) {
        if ((Count $T.wipe $tok) -ne 1) { $f += "(R4) census token $tok must appear once in the file (no legend echo)" }
    }
    # R5
    if ($census -ne '' -and (Count $census ('AreDiagnostics' + 'Enabled')) -ne 0) { $f += '(R5) the census must be ungated' }
    # R6
    if ((Count $T.nsmod ('    UE_LOG(LogNodeShuffle, Display, TEXT("===== NodeShuffle packet marker (' + $marker + ')====="));')) -ne 1 -or (Count $T.nsmod $marker) -ne 1 -or
        (Count $T.nsmod ('LOADED (2026-10-31-rwg-' + 'farspawn-1f)')) -ne 1) { $f += '(R6) marker rwg-rerollwipe-1 once on its own line, farspawn-1f LOADED line once' }
    if ((Count $T.wipe $marker) -ne 0 -or (Count $T.wipeh $marker) -ne 0 -or (Count $T.sub $marker) -ne 0) { $f += '(R6) no marker inside the packet files' }
    # R7
    if (($T.wipe -split "`n").Count -ge 500) { $f += '(R7) NodeShuffleRerollWipe.cpp must stay under 500 lines' }
    if ((Count $T.sub ('RemoveSingle' + 'Swap(')) -ne 1) { $f += '(R7) Subsystem.cpp keeps one RemoveSingleSwap (DeregisterNodeFromManager); the wipe goes through the helper' }
    # R8
    foreach ($d in @('void RerollWipeBegin(struct FNodeShuffleRerollWipeTally& T);', 'void RerollWipeTeardownRegistrations(class AFGResourceNode* Node, struct FNodeShuffleRerollWipeTally& T);',
        'void RerollWipeNoteInvalidSlot(class AFGResourceNode* Node, struct FNodeShuffleRerollWipeTally& T);', 'void RerollWipeEmitCensus(const struct FNodeShuffleRerollWipeTally& T);', 'void RerollWipeCollectNotYielded(TSet<const class AFGResourceNode*>& Out);', 'int32 RerollWipeDropDeadSlotAddress(class AFGResourceNode* Node);',
        'void RerollWipeEmitListAudit(bool bIsReroll, const TSet<const class AFGResourceNode*>& NotYieldedBeforeWipe);')) {
        if ((Count $T.subh $d) -ne 1) { $f += "(R8) header declaration missing: $d" }
    }
    if ((Count $T.sub '#include "NodeShuffleRerollWipe.h"') -ne 1 -or (Count $T.wipeh 'struct FNodeShuffleRerollWipeTally') -ne 1 -or (Count $T.wipe '#include "EngineUtils.h"') -ne 1) { $f += '(R8) include / tally struct / EngineUtils' }
    # R11 (review F3)
    if ((Count $T.sub $auditCall) -ne 1 -or (Count $T.sub 'RerollWipeEmitListAudit(') -ne 1) { $f += '(R11) one ungated REROLLAUDIT call, the last statement of RollLayout after EmitRollCensus' }
    $ai = $T.wipe.IndexOf('void ANodeShuffleSubsystem::RerollWipeEmitListAudit(')
    if ($ai -lt 0 -or (Count $T.wipe 'void ANodeShuffleSubsystem::RerollWipeEmitListAudit(') -ne 1) { $f += '(R11) audit function missing or duplicated' }
    else {
        $audit = $T.wipe.Substring($ai)
        if (-not (Ordered $audit @('for (TActorIterator<AFGResourceNode> It(World); It; ++It) { Iterated.Add(*It); }',
            $auditLoop, $auditFmt, $auditArgs))) { $f += '(R11) audit: iterate, pointer-compare every list entry (new-since-wipe excludes the snapshot), exact format/args' }
        if ((Count $audit ('IsVal' + 'id(')) -ne 0 -or (Count $audit ('Ptr' + '->')) -ne 0 -or (Count $audit ('(*' + 'Ptr')) -ne 0 -or (Count $audit ('Ptr' + '.')) -ne 0) { $f += '(R11) audit dereferences a list entry' }
        if ((Count $audit ('AreDiagnostics' + 'Enabled')) -ne 0) { $f += '(R11) audit must be ungated' }
    }
    # R12 (re-review A1) the before-wipe snapshot: one call, before the wipe block, before the audit call; same predicate, no dereference, no gate
    if ((Count $T.sub 'RerollWipeCollectNotYielded(') -ne 1 -or -not (Ordered $T.sub @($snapCall, '        TSet<FGuid> KeptGuids;', 'RerollWipeEmitListAudit(bIsReroll, AuditNotYieldedBeforeWipe);'))) { $f += '(R12) one before-wipe snapshot call, ahead of the wipe block and the audit call' }
    $coll = Span $T.wipe 'void ANodeShuffleSubsystem::RerollWipeCollectNotYielded(' 'void ANodeShuffleSubsystem::RerollWipeEmitListAudit('
    if ($coll -eq '') { $f += '(R12) snapshot function span missing (it must sit above the audit)' }
    else {
        if (-not (Ordered $coll @('    Out.Reset();', 'for (TActorIterator<AFGResourceNode> It(World); It; ++It) { Iterated.Add(*It); }',
            'for (const AFGResourceNode* Ptr : Mgr->mResourceNodes) { if (!Iterated.Contains(Ptr)) { Out.Add(Ptr); } }'))) { $f += '(R12) snapshot: reset, iterate, the same pointer-compare predicate' }
        if ((Count $coll ('IsVal' + 'id(')) -ne 0 -or (Count $coll ('Ptr' + '->')) -ne 0 -or (Count $coll ('(*' + 'Ptr')) -ne 0 -or (Count $coll ('Ptr' + '.')) -ne 0) { $f += '(R12) snapshot dereferences a list entry' }
        if ((Count $coll ('AreDiagnostics' + 'Enabled')) -ne 0) { $f += '(R12) snapshot must be ungated' }
    }
    # R13 (re-review S1) EnsureNewNodeSpawned's external-destroy branch: the dead address leaves the snap list through the helper (R7 keeps
    #     Subsystem.cpp at one RemoveSingleSwap), BEFORE the veto Unregister and the slot removal that drops the hard ref; helper by address
    if ((Count $T.sub 'RerollWipeDropDeadSlotAddress(') -ne 1 -or -not (Ordered $T.sub @($s1Call, '        FNodeShuffleModule::UnregisterManagedNode(*Existing);', '        SpawnedNodes.Remove(Entry.EntryGuid);'))) { $f += '(R13) external-destroy branch: one dead-address snap drop, before Unregister and the slot removal' }
    if ((Count $T.wipe $s1Helper) -ne 1) { $f += '(R13) RerollWipeDropDeadSlotAddress body: null-guarded RemoveSingleSwap by address' }
    if ((Count $T.sub ('TEXT("EXTDESTROY-SNAP %s: slotNull %d snapListRemoved %d"), *Entry.EntryGuid.ToString(), (*Existing == nullptr) ? 1 : 0, DeadSnapDropped); }')) -ne 1) { $f += '(R13) EXTDESTROY-SNAP line, arity 3' }
    # R10 (review F2)
    $inv = Span $T.wipe 'void ANodeShuffleSubsystem::RerollWipeNoteInvalidSlot(' 'void ANodeShuffleSubsystem::RerollWipeEmitCensus('
    if ($inv -eq '') { $f += '(R10) invalid-slot span missing' }
    else {
        if (-not (Ordered $inv @('++T.InvalidSlots;', 'if (!Node) { ++T.InvalidSlotsNull; return; }', 'T.InvalidPtrs.Add(Node);',
            'if (Mgr->mResourceNodes.Contains(Node)) { ++T.InvalidInManagerAtWipe; }', ('T.InvalidRemovedFromManager += Mgr->mResourceNodes.RemoveSingle' + 'Swap(Node);')))) {
            $f += '(R10) invalid slot: null counted, Contains measured before the by-address RemoveSingleSwap' }
        if ((Count $inv 'Node->') -ne 0 -or (Count $inv ('IsVal' + 'id(')) -ne 0 -or (Count $inv '(*Node') -ne 0 -or (Count $inv ('Dest' + 'roy(')) -ne 0) { $f += '(R10) invalid slot: dereferenced or destroyed' }
    }
    # R9 (review F1)
    $well = Span $T.welld 'bool ANodeShuffleSubsystem::DestroyWellMemberIfUnused(' 'bool ANodeShuffleSubsystem::DespawnWellGroup('
    if ($well -eq '') { $f += '(R9) DestroyWellMemberIfUnused span missing' }
    else {
        if ((Count $well 'DeregisterNodeFromManager(Member);') -ne 1 -or (Count $well ('Member->Dest' + 'roy();')) -ne 1) { $f += '(R9) one Deregister and one Destroy in the well-member teardown' }
        if (-not (Ordered $well @('DeregisterNodeFromManager(Member);', ('SnapDropped = Mgr->mResourceNodes.RemoveSingle' + 'Swap(AsNode);'),
            'RepMgr->RemoveRepresentationOfActor(Member);', 'RepMgr->FindResourceNodeRepresentation(Member)) { RepMgr->RemoveRepresentation(NodeRep); }',
            ('Member->Dest' + 'roy();')))) { $f += '(R9) well-member order: Deregister, snap-list drop, rep by actor, rep by node, then Destroy' }
        if ((Count $well 'FNodeShuffleModule::UnregisterManagedNode(Member);') -ne 1 -or -not (Ordered $well @(('    Member->Dest' + 'roy();'), '    FNodeShuffleModule::UnregisterManagedNode(Member); // coexist-veto-1 order'))) { $f += '(R9) re-review E1: one veto-registry Unregister, AFTER Destroy (coexist-veto-1 order)' }
        if ((Count $well ('if (FNodeShuffleModule::AreDiagnostics' + 'Enabled()) { UE_LOG(LogNodeShuffle, Display, TEXT("WELLDESPAWN-SNAP %s: isSnapListType %d snapListRemovedAfterDeregister %d"), *Member->GetName(), Cast<AFGResourceNode>(Member) ? 1 : 0, SnapDropped); }')) -ne 1) { $f += '(R9) WELLDESPAWN-SNAP line, diagnostics-gated, arity 3 (re-review D1: the Cast predicate printed)' }
    }
    foreach ($inc in @('#include "Resources/FGResourceNodeManager.h"', '#include "FGActorRepresentationManager.h"', '#include "Representation/FGResourceNodeRepresentation.h"')) {
        if ((Count $T.welld $inc) -ne 1) { $f += "(R9) WellDespawn include missing: $inc" }
    }
    $f += @(Invoke-LogFloodChecks $T) # rwg-logflood-1
    return $f
}

$base = Invoke-RerollWipeChecks $text
if ($base.Count -gt 0) { Write-Host 'FAIL (baseline):'; $base | ForEach-Object { Write-Host "  $_" }; exit 1 }
Write-Host 'PASS baseline: R1-R13 and L1-L10 hold'

$mutants = @(
    @{ Name = 'Q1 teardown call dropped'; Key = 'sub'; Expect = 'R1'; Find = 'RerollWipeTeardownRegistrations(It->Value, WipeTally); if (!It->Value->Destroy())'; Repl = 'if (!It->Value->Destroy())' }
    @{ Name = 'Q2 teardown after Destroy'; Key = 'sub'; Expect = 'R1'; Find = '{ RerollWipeTeardownRegistrations(It->Value, WipeTally); if (!It->Value->Destroy()) { ++WipeTally.DestroyRefused; } }'; Repl = '{ if (!It->Value->Destroy()) { ++WipeTally.DestroyRefused; } RerollWipeTeardownRegistrations(It->Value, WipeTally); }' }
    @{ Name = 'Q26 Destroy return ignored'; Key = 'sub'; Expect = 'R1'; Find = 'if (!It->Value->Destroy()) { ++WipeTally.DestroyRefused; }'; Repl = 'It->Value->Destroy();' }
    @{ Name = 'Q27 destroyRefused dropped from census'; Key = 'wipe'; Expect = 'R4'; Find = ' destroyRefused=%d'; Repl = '' }
    @{ Name = 'Q3 snap-list removal dropped'; Key = 'wipe'; Expect = 'R2'; Find = 'Removed = Mgr->mResourceNodes.RemoveSingleSwap(Node); '; Repl = 'Removed = 0; ' }
    @{ Name = 'Q4 representation-by-actor removal dropped'; Key = 'wipe'; Expect = 'R2'; Find = 'bByActor = RepMgr->RemoveRepresentationOfActor(Node);'; Repl = 'bByActor = false;' }
    @{ Name = 'Q5 scan reveal removal dropped'; Key = 'wipe'; Expect = 'R2'; Find = "    Node->RemoveResourceNodeScan_Local();`n"; Repl = '' }
    @{ Name = 'Q6 stale count dereferences (re-review N2: insert-only, every needle intact)'; Key = 'wipe'; Expect = 'R3'; ExpectMsg = 'a recorded address is dereferenced'; Find = "        InvalidStillIn = 0;`n"; Repl = ("        for (AFGResourceNode* P : T.DestroyedPtrs) { if (IsVal" + "id(P)) { ++StaleAfter; } }`n        InvalidStillIn = 0;`n") }
    @{ Name = 'Q7 census field renamed'; Key = 'wipe'; Expect = 'R4'; Find = 'staleInManagerAfter=%d'; Repl = 'stale=%d' }
    @{ Name = 'Q8 census gated'; Key = 'wipe'; Expect = 'R5'; Find = "    // UNGATED: once per re-roll; it is the packet's control line.`n"; Repl = ("    if (FNodeShuffleModule::AreDiagnostics" + "Enabled())`n") }
    @{ Name = 'Q9 marker reverted'; Key = 'nsmod'; Expect = 'R6'; Find = $marker; Repl = ('2026-10-31-rwg-' + 'rerollwipe-0') }
    @{ Name = 'Q10 Unregister before Destroy'; Key = 'sub'; Expect = 'R1'; Find = ($teardownLine + "`n            else { RerollWipeNoteInvalidSlot(It->Value, WipeTally); }`n            FNodeShuffleModule::UnregisterManagedNode(It->Value);`n"); Repl = ("            FNodeShuffleModule::UnregisterManagedNode(It->Value);`n" + $teardownLine + "`n            else { RerollWipeNoteInvalidSlot(It->Value, WipeTally); }`n") }
    @{ Name = 'Q11 census call dropped'; Key = 'sub'; Expect = 'R1'; Find = 'RerollWipeEmitCensus(WipeTally); // rwg-rerollwipe-1'; Repl = '// census removed' }
    @{ Name = 'Q12 helper destroys'; Key = 'wipe'; Expect = 'R2'; Find = "    Node->UpdateNodeRepresentation();`n"; Repl = ("    Node->UpdateNodeRepresentation();`n    Node->Dest" + "roy();`n") }
    @{ Name = 'Q13 stale count derived by formula'; Key = 'wipe'; Expect = 'R3'; Find = "        StaleAfter = 0;`n"; Repl = ("        StaleAfter = " + "T.Destroyed - T.RemovedFromManager; StaleAfter = 0;`n") }
    @{ Name = 'Q14 removal inlined at the wipe (third copy in Subsystem.cpp)'; Key = 'sub'; Expect = 'R7'; Find = "        MeshActorCache.Reset();`n        RerollWipeEmitCensus"; Repl = ("        if (AFGResourceNodeManager* M = GetNodeManager()) { M->mResourceNodes.RemoveSingle" + "Swap(nullptr); }`n        MeshActorCache.Reset();`n        RerollWipeEmitCensus") }
    @{ Name = 'Q15 legend echoes a census token'; Key = 'wipe'; Expect = 'R4'; Find = '    // UNGATED: once per re-roll'; Repl = '    // staleInManagerAfter= counts survivors. UNGATED: once per re-roll' }
    @{ Name = 'Q34 (review F7) representation-by-node removal dropped'; Key = 'wipe'; Expect = 'R2'; Find = 'RepMgr->RemoveRepresentation(NodeRep); bByNode = true;'; Repl = 'bByNode = true;' }
    @{ Name = 'Q35 (review F7) UpdateNodeRepresentation dropped'; Key = 'wipe'; Expect = 'R2'; Find = "    Node->UpdateNodeRepresentation();`n"; Repl = '' }
    @{ Name = 'Q16 invalid-slot snap-list removal dropped'; Key = 'wipe'; Expect = 'R10'; Find = ('T.InvalidRemovedFromManager += Mgr->mResourceNodes.RemoveSingle' + 'Swap(Node);'); Repl = 'T.InvalidRemovedFromManager += 0;' }
    @{ Name = 'Q23 invalid-slot Contains measured after the removal'; Key = 'wipe'; Expect = 'R10'; Find = "        if (Mgr->mResourceNodes.Contains(Node)) { ++T.InvalidInManagerAtWipe; }`n"; Repl = ''
       Find2 = ("Swap(Node);`n    }`n}"); Repl2 = ("Swap(Node);`n        if (Mgr->mResourceNodes.Contains(Node)) { ++T.InvalidInManagerAtWipe; }`n    }`n}") }
    @{ Name = 'Q24 invalid slot dereferenced'; Key = 'wipe'; Expect = 'R10'; Find = 'if (!Node) { ++T.InvalidSlotsNull; return; }'; Repl = ('if (!Node) { ++T.InvalidSlotsNull; return; } if (!IsVal' + 'id(Node)) { }') }
    @{ Name = 'Q25 invalid-slot field dropped from census'; Key = 'wipe'; Expect = 'R4'; Find = ' invalidInManagerAtWipe=%d'; Repl = '' }
    @{ Name = 'Q28 F6 presence measured before UpdateNodeRepresentation'; Key = 'wipe'; Expect = 'R2'; Find = "    Node->UpdateNodeRepresentation();`n"; Repl = ''
       Find2 = "++T.RepPresentAfterUpdate; } }`n"; Repl2 = "++T.RepPresentAfterUpdate; } }`n    Node->UpdateNodeRepresentation();`n" }
    @{ Name = 'Q29 REROLLAUDIT call dropped'; Key = 'sub'; Expect = 'R11'; Find = '    RerollWipeEmitListAudit(bIsReroll, AuditNotYieldedBeforeWipe); // rwg'; Repl = '    // audit removed // rwg' }
    @{ Name = 'Q30 REROLLAUDIT call gated'; Key = 'sub'; Expect = 'R11'; Find = '    RerollWipeEmitListAudit(bIsReroll, AuditNotYieldedBeforeWipe); // rwg'; Repl = ('    if (FNodeShuffleModule::AreDiagnostics' + 'Enabled()) RerollWipeEmitListAudit(bIsReroll, AuditNotYieldedBeforeWipe); // rwg') }
    @{ Name = 'Q31 REROLLAUDIT dereferences an entry (re-review N2: insert-only)'; Key = 'wipe'; Expect = 'R11'; ExpectMsg = 'audit dereferences a list entry'; Find = "    int32 NotIterated = 0;`n"; Repl = ("    int32 NotIterated = 0;`n    for (const AFGResourceNode* P : Mgr->mResourceNodes) { if (IsVal" + "id(P)) { } }`n") }
    @{ Name = 'Q32 REROLLAUDIT gated in its body'; Key = 'wipe'; Expect = 'R11'; Find = '    if (!Mgr || !World) { UE_LOG(LogNodeShuffle, Display, TEXT("REROLLAUDIT: reroll=%d noManagerOrWorld=1"), bIsReroll ? 1 : 0); return; }'; Repl = ('    if (!FNodeShuffleModule::AreDiagnostics' + 'Enabled()) { return; }`n    if (!Mgr || !World) { return; }') }
    @{ Name = 'Q33 REROLLAUDIT field renamed'; Key = 'wipe'; Expect = 'R11'; Find = 'entriesNotYieldedByActorIterator=%d'; Repl = 'dead=%d' }
    @{ Name = 'Q36 (re-review A1) before-wipe snapshot call dropped'; Key = 'sub'; Expect = 'R12'; Find = ($snapCall + "`n"); Repl = '' }
    @{ Name = 'Q37 (re-review A1) snapshot moved after the wipe'; Key = 'sub'; Expect = 'R12'; Find = ($snapCall + "`n"); Repl = ''
       Find2 = '        RerollWipeEmitCensus(WipeTally); // rwg-rerollwipe-1: REROLLWIPE, ungated, once per re-roll'; Repl2 = ('        RerollWipeEmitCensus(WipeTally); // rwg-rerollwipe-1: REROLLWIPE, ungated, once per re-roll' + "`n" + $snapCall) }
    @{ Name = 'Q38 (re-review A1) new-since-wipe ignores the snapshot'; Key = 'wipe'; Expect = 'R11'; Find = 'if (!NotYieldedBeforeWipe.Contains(Ptr)) { ++NewSinceWipe; }'; Repl = '++NewSinceWipe;' }
    @{ Name = 'Q39 (re-review A1) snapshot dereferences an entry'; Key = 'wipe'; Expect = 'R12'; ExpectMsg = 'snapshot dereferences a list entry'; Find = "    Out.Reset();`n"; Repl = ("    Out.Reset();`n    for (const AFGResourceNode* P : Mgr->mResourceNodes) { if (IsVal" + "id(P)) { } }`n") }
    @{ Name = 'Q40 (re-review A1) snapshot predicate dropped'; Key = 'wipe'; Expect = 'R12'; Find = 'if (!Iterated.Contains(Ptr)) { Out.Add(Ptr); }'; Repl = 'Out.Add(Ptr);' }
    @{ Name = 'Q43 (re-review S1) external-destroy snap drop removed'; Key = 'sub'; Expect = 'R13'; Find = ($s1Call + ' // re-review S1'); Repl = '        const int32 DeadSnapDropped = 0; // re-review S1' }
    @{ Name = 'Q44 (re-review S1) snap drop moved after the slot removal'; Key = 'sub'; Expect = 'R13'; Find = ($s1Call + ' // re-review S1: the review-F2 rule on its other side, before the hard ref goes' + "`n"); Repl = ''
       Find2 = "        SteadyAliveNodes.Remove(Entry.EntryGuid);`n        Existing = nullptr; // Remove() invalidated the slot pointer`n"; Repl2 = ("        SteadyAliveNodes.Remove(Entry.EntryGuid);`n" + $s1Call + "`n        Existing = nullptr; // Remove() invalidated the slot pointer`n") }
    @{ Name = 'Q17 well-member snap-list drop removed'; Key = 'welld'; Expect = 'R9'; Find = ('SnapDropped = Mgr->mResourceNodes.RemoveSingle' + 'Swap(AsNode);'); Repl = 'SnapDropped = 0;' }
    @{ Name = 'Q18 well-member rep-by-actor drop removed'; Key = 'welld'; Expect = 'R9'; Find = "        RepMgr->RemoveRepresentationOfActor(Member);`n"; Repl = '' }
    @{ Name = 'Q19 well-member rep-by-node drop removed'; Key = 'welld'; Expect = 'R9'; Find = '{ RepMgr->RemoveRepresentation(NodeRep); }'; Repl = '{ }' }
    @{ Name = 'Q20 well-member snap-list drop moved after Destroy'; Key = 'welld'; Expect = 'R9'; Find = $wellSnapLine; Repl = ''
       Find2 = ("    Member->Dest" + "roy();`n"); Repl2 = ("    Member->Dest" + "roy();`n" + $wellSnapLine) }
    @{ Name = 'Q21 WELLDESPAWN-SNAP ungated'; Key = 'welld'; Expect = 'R9'; Find = ('    if (FNodeShuffleModule::AreDiagnostics' + 'Enabled()) { UE_LOG(LogNodeShuffle, Display, TEXT("WELLDESPAWN-SNAP'); Repl = '    { UE_LOG(LogNodeShuffle, Display, TEXT("WELLDESPAWN-SNAP' }
    @{ Name = 'Q41 (re-review D1) WELLDESPAWN-SNAP Cast predicate hard-coded'; Key = 'welld'; Expect = 'R9'; Find = 'Cast<AFGResourceNode>(Member) ? 1 : 0, SnapDropped'; Repl = '1, SnapDropped' }
    @{ Name = 'Q42 (re-review E1) well-member Unregister moved back before Destroy'; Key = 'welld'; Expect = 'R9'; Find = ("    Member->Dest" + "roy();`n    FNodeShuffleModule::UnregisterManagedNode(Member);"); Repl = ("    FNodeShuffleModule::UnregisterManagedNode(Member);`n    Member->Dest" + "roy();") }
    @{ Name = 'Q22 WellDespawn manager include dropped'; Key = 'welld'; Expect = 'R9'; Find = '#include "Resources/FGResourceNodeManager.h"'; Repl = '' }
    # rwg-logflood-1 (L1..L10)
    @{ Name = 'F1 signature compare removed (always dump)'; Key = 'wmid'; Expect = 'L1'; Find = $lfDecide; Repl = '    const bool bDumping = true;' }
    @{ Name = 'F2 census line removed'; Key = 'wmid'; Expect = 'L2'; Find = ($lfCensusFmt + "`n"); Repl = '' }
    @{ Name = 'F3 reset-on-world call removed'; Key = 'wmi'; Expect = 'L3'; Find = $lfResetCall; Repl = '    const bool bDumpWorldReset = false; // rwg-logflood-1: a fresh world dumps once' }
    @{ Name = 'F4 reset body keeps the stored entry count'; Key = 'wmid'; Expect = 'L3'; Find = ('    WellIndexDumpEntries = ' + "-1;`n"); Repl = '' }
    @{ Name = 'F5 census printed only on a dump'; Key = 'wmid'; Expect = 'L2'; Find = "    if (!bDumping) { ++WellIndexDumpSkipped; }`n    UE_LOG(LogNodeShuffle, Verbose,`n"; Repl = "    if (!bDumping) { ++WellIndexDumpSkipped; }`n    if (bDumping) UE_LOG(LogNodeShuffle, Verbose,`n" }
    @{ Name = 'F6 buffering ungated by verbosity'; Key = 'wmi'; Expect = 'L4'; Find = $lfGate; Repl = '    SpatialDump.bActive = bDiag;' }
    @{ Name = 'F7 flush call dropped'; Key = 'wmi'; Expect = 'L4'; Find = ($lfFlushCall + "`n"); Repl = '' }
    @{ Name = 'F8 per-entry line printed inline again (the shortcut)'; Key = 'wmi'; Expect = 'L5'; Find = '                if (SpatialDump.bActive) // rwg-logflood-1: the accepted-piece line';
       Repl = ('                UE_LOG(LogNodeShuffle, Verbose, ' + $lfAcceptLit + '%s''"), *MeshName);' + "`n" + '                if (SpatialDump.bActive) // rwg-logflood-1: the accepted-piece line') }
    @{ Name = 'F9 signature drops the member field'; Key = 'wmid'; Expect = 'L6'; Find = "    H = NsFnvStr(H, InMemberShort);`n"; Repl = '' }
    @{ Name = 'F10 signature hashes the location'; Key = 'wmid'; Expect = 'L6'; Find = "    uint64 H = NsFnvOffset;`n"; Repl = ("    uint64 H = NsFnvOffset;`n    H = NsFnvU64(H, static_cast<uint64>(In" + "Loc.X));`n") }
    @{ Name = 'F11 legend echoes a census token'; Key = 'wmid'; Expect = 'L2'; Find = '// Called by RebuildWellMeshIndex only when D.bActive.'; Repl = ('// Called by RebuildWellMeshIndex only when D.bActive; sig' + 'Changed=1 means re-dump.') }
    @{ Name = 'F12 REJECT buffering site dropped'; Key = 'wmi'; Expect = 'L7'; Find = 'SpatialDump.Add(/*bAccepted=*/false,'; Repl = '(void)(/*bAccepted=*/false,' }
    @{ Name = 'F14 constant signature (never re-dumps on change)'; Key = 'wmid'; Expect = 'L6'; Find = "    return NsFnvU64(SigSum, static_cast<uint64>(Entries.Num()));`n"; Repl = "    return 0;`n    return NsFnvU64(SigSum, static_cast<uint64>(Entries.Num()));`n" }
    @{ Name = 'F13 logflood marker reverted'; Key = 'nsmod'; Expect = 'L8'; Find = $lfMarker; Repl = ('2026-11-01-rwg-' + 'logflood-0') }
)
$bad = 0
foreach ($m in $mutants) {
    $t2 = [ordered]@{}; foreach ($k in $text.Keys) { $t2[$k] = $text[$k] }
    if ((Count $t2[$m.Key] $m.Find) -ne 1) { Write-Host "MUTANT-SETUP FAIL $($m.Name): find text not unique in $($m.Key)"; $bad++; continue }
    $t2[$m.Key] = $t2[$m.Key].Replace($m.Find, $m.Repl)
    if ($m.ContainsKey('Find2')) {
        if ((Count $t2[$m.Key] $m.Find2) -ne 1) { Write-Host "MUTANT-SETUP FAIL $($m.Name): find2 text not unique in $($m.Key)"; $bad++; continue }
        $t2[$m.Key] = $t2[$m.Key].Replace($m.Find2, $m.Repl2)
    }
    $r = Invoke-RerollWipeChecks $t2
    $hits = @($r | Where-Object { $_.StartsWith("($($m.Expect))") })
    # re-review N2: ExpectMsg = the mutant must go red through THAT sub-check alone (every same-rule failure carries the message)
    $ok = $hits.Count -gt 0
    if ($ok -and $m.ContainsKey('ExpectMsg')) { $ok = (@($hits | Where-Object { -not $_.Contains($m.ExpectMsg) })).Count -eq 0 }
    if ($ok) { Write-Host "RED  (ok) $($m.Name)" }
    else { Write-Host "GREEN (BAD) $($m.Name): expected $($m.Expect), got: $($r -join ' | ')"; $bad++ }
}
if ($bad -gt 0) { Write-Host "FAIL: $bad mutant(s) not caught"; exit 1 }
Write-Host "PASS: $($mutants.Count) mutants red"
exit 0
