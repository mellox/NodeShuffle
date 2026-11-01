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
#   R10 (review F2) RerollWipeNoteInvalidSlot: null counted; non-null: Contains measured BEFORE RemoveSingleSwap, by address, no dereference, no Destroy
#   R9 (review F1) DestroyWellMemberIfUnused: after DeregisterNodeFromManager (which refuses OUR nodes), an explicit snap-list RemoveSingleSwap
#      and representation removal (by actor, by node), all BEFORE Destroy; the WELLDESPAWN-SNAP line is diagnostics-gated; the three includes
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

$censusFmt = 'TEXT("REROLLWIPE: destroyed=%d removedFromManager=%d staleInManagerAfter=%d representationsRemoved=%d managerCountBefore=%d managerCountAfter=%d kept=%d repByActor=%d repByNode=%d invalidSlots=%d invalidSlotsStillInManager=%d noManager=%d noRepManager=%d invalidSlotsNull=%d invalidInManagerAtWipe=%d invalidRemovedFromManager=%d destroyRefused=%d repPresentAfterUpdate=%d")'
$censusArgs = "        T.Destroyed, T.RemovedFromManager, StaleAfter, T.RepAny, T.ManagerCountBefore, ManagerCountAfter, T.Kept,`n        T.RepByActor, T.RepByNode, T.InvalidSlots, InvalidStillIn, T.NoManager, T.NoRepManager, T.InvalidSlotsNull, T.InvalidInManagerAtWipe, T.InvalidRemovedFromManager,`n        T.DestroyRefused, T.RepPresentAfterUpdate);"
$teardownLine = '            if (IsValid(It->Value)) { RerollWipeTeardownRegistrations(It->Value, WipeTally); if (!It->Value->Destroy()) { ++WipeTally.DestroyRefused; } }'
$staleLoop = "        StaleAfter = 0;`n        for (AFGResourceNode* Ptr : T.DestroyedPtrs) { if (Mgr->mResourceNodes.Contains(Ptr)) { ++StaleAfter; } }"
$invalidLoop = "        InvalidStillIn = 0;`n        for (AFGResourceNode* Ptr : T.InvalidPtrs) { if (Mgr->mResourceNodes.Contains(Ptr)) { ++InvalidStillIn; } }"
$marker = '2026-10-31-rwg-' + 'rerollwipe-1'
# review F1: the WHOLE well-member snap-list drop line, so mutant Q20 moves the identical text after Destroy (order, not presence)
$wellSnapLine = "    if (AFGResourceNode* AsNode = Cast<AFGResourceNode>(Member)) { if (AFGResourceNodeManager* Mgr = GetNodeManager()) { SnapDropped = Mgr->mResourceNodes.RemoveSingle" + "Swap(AsNode); } }`n"

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
    foreach ($tok in @('destroyed=', 'removedFromManager=', 'staleInManagerAfter=', 'representationsRemoved=', 'managerCountBefore=', 'managerCountAfter=', 'invalidSlotsNull=', 'invalidInManagerAtWipe=', 'invalidRemovedFromManager=', 'destroyRefused=', 'repPresentAfterUpdate=')) {
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
        'void RerollWipeNoteInvalidSlot(class AFGResourceNode* Node, struct FNodeShuffleRerollWipeTally& T);', 'void RerollWipeEmitCensus(const struct FNodeShuffleRerollWipeTally& T);')) {
        if ((Count $T.subh $d) -ne 1) { $f += "(R8) header declaration missing: $d" }
    }
    if ((Count $T.sub '#include "NodeShuffleRerollWipe.h"') -ne 1 -or (Count $T.wipeh 'struct FNodeShuffleRerollWipeTally') -ne 1) { $f += '(R8) include / tally struct' }
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
        if ((Count $well ('if (FNodeShuffleModule::AreDiagnostics' + 'Enabled()) { UE_LOG(LogNodeShuffle, Display, TEXT("WELLDESPAWN-SNAP %s: snapListRemovedAfterDeregister %d"), *Member->GetName(), SnapDropped); }')) -ne 1) { $f += '(R9) WELLDESPAWN-SNAP line, diagnostics-gated, arity 2' }
    }
    foreach ($inc in @('#include "Resources/FGResourceNodeManager.h"', '#include "FGActorRepresentationManager.h"', '#include "Representation/FGResourceNodeRepresentation.h"')) {
        if ((Count $T.welld $inc) -ne 1) { $f += "(R9) WellDespawn include missing: $inc" }
    }
    return $f
}

$base = Invoke-RerollWipeChecks $text
if ($base.Count -gt 0) { Write-Host 'FAIL (baseline):'; $base | ForEach-Object { Write-Host "  $_" }; exit 1 }
Write-Host 'PASS baseline: R1-R10 hold'

$mutants = @(
    @{ Name = 'Q1 teardown call dropped'; Key = 'sub'; Expect = 'R1'; Find = 'RerollWipeTeardownRegistrations(It->Value, WipeTally); if (!It->Value->Destroy())'; Repl = 'if (!It->Value->Destroy())' }
    @{ Name = 'Q2 teardown after Destroy'; Key = 'sub'; Expect = 'R1'; Find = '{ RerollWipeTeardownRegistrations(It->Value, WipeTally); if (!It->Value->Destroy()) { ++WipeTally.DestroyRefused; } }'; Repl = '{ if (!It->Value->Destroy()) { ++WipeTally.DestroyRefused; } RerollWipeTeardownRegistrations(It->Value, WipeTally); }' }
    @{ Name = 'Q26 Destroy return ignored'; Key = 'sub'; Expect = 'R1'; Find = 'if (!It->Value->Destroy()) { ++WipeTally.DestroyRefused; }'; Repl = 'It->Value->Destroy();' }
    @{ Name = 'Q27 destroyRefused dropped from census'; Key = 'wipe'; Expect = 'R4'; Find = ' destroyRefused=%d'; Repl = '' }
    @{ Name = 'Q3 snap-list removal dropped'; Key = 'wipe'; Expect = 'R2'; Find = 'Removed = Mgr->mResourceNodes.RemoveSingleSwap(Node); '; Repl = 'Removed = 0; ' }
    @{ Name = 'Q4 representation-by-actor removal dropped'; Key = 'wipe'; Expect = 'R2'; Find = 'bByActor = RepMgr->RemoveRepresentationOfActor(Node);'; Repl = 'bByActor = false;' }
    @{ Name = 'Q5 scan reveal removal dropped'; Key = 'wipe'; Expect = 'R2'; Find = "    Node->RemoveResourceNodeScan_Local();`n"; Repl = '' }
    @{ Name = 'Q6 stale count dereferences'; Key = 'wipe'; Expect = 'R3'; Find = 'if (Mgr->mResourceNodes.Contains(Ptr)) { ++StaleAfter; }'; Repl = ('if (IsVal' + 'id(Ptr) && Mgr->mResourceNodes.Contains(Ptr)) { ++StaleAfter; }') }
    @{ Name = 'Q7 census field renamed'; Key = 'wipe'; Expect = 'R4'; Find = 'staleInManagerAfter=%d'; Repl = 'stale=%d' }
    @{ Name = 'Q8 census gated'; Key = 'wipe'; Expect = 'R5'; Find = "    // UNGATED: once per re-roll; it is the packet's control line.`n"; Repl = ("    if (FNodeShuffleModule::AreDiagnostics" + "Enabled())`n") }
    @{ Name = 'Q9 marker reverted'; Key = 'nsmod'; Expect = 'R6'; Find = $marker; Repl = ('2026-10-31-rwg-' + 'rerollwipe-0') }
    @{ Name = 'Q10 Unregister before Destroy'; Key = 'sub'; Expect = 'R1'; Find = ($teardownLine + "`n            else { RerollWipeNoteInvalidSlot(It->Value, WipeTally); }`n            FNodeShuffleModule::UnregisterManagedNode(It->Value);`n"); Repl = ("            FNodeShuffleModule::UnregisterManagedNode(It->Value);`n" + $teardownLine + "`n            else { RerollWipeNoteInvalidSlot(It->Value, WipeTally); }`n") }
    @{ Name = 'Q11 census call dropped'; Key = 'sub'; Expect = 'R1'; Find = 'RerollWipeEmitCensus(WipeTally); // rwg-rerollwipe-1'; Repl = '// census removed' }
    @{ Name = 'Q12 helper destroys'; Key = 'wipe'; Expect = 'R2'; Find = "    Node->UpdateNodeRepresentation();`n"; Repl = ("    Node->UpdateNodeRepresentation();`n    Node->Dest" + "roy();`n") }
    @{ Name = 'Q13 stale count derived by formula'; Key = 'wipe'; Expect = 'R3'; Find = "        StaleAfter = 0;`n"; Repl = ("        StaleAfter = " + "T.Destroyed - T.RemovedFromManager; StaleAfter = 0;`n") }
    @{ Name = 'Q14 removal inlined at the wipe (third copy in Subsystem.cpp)'; Key = 'sub'; Expect = 'R7'; Find = "        MeshActorCache.Reset();`n        RerollWipeEmitCensus"; Repl = ("        if (AFGResourceNodeManager* M = GetNodeManager()) { M->mResourceNodes.RemoveSingle" + "Swap(nullptr); }`n        MeshActorCache.Reset();`n        RerollWipeEmitCensus") }
    @{ Name = 'Q15 legend echoes a census token'; Key = 'wipe'; Expect = 'R4'; Find = '    // UNGATED: once per re-roll'; Repl = '    // staleInManagerAfter= counts survivors. UNGATED: once per re-roll' }
    @{ Name = 'Q16 invalid-slot snap-list removal dropped'; Key = 'wipe'; Expect = 'R10'; Find = ('T.InvalidRemovedFromManager += Mgr->mResourceNodes.RemoveSingle' + 'Swap(Node);'); Repl = 'T.InvalidRemovedFromManager += 0;' }
    @{ Name = 'Q23 invalid-slot Contains measured after the removal'; Key = 'wipe'; Expect = 'R10'; Find = "        if (Mgr->mResourceNodes.Contains(Node)) { ++T.InvalidInManagerAtWipe; }`n"; Repl = ''
       Find2 = ("Swap(Node);`n    }`n}"); Repl2 = ("Swap(Node);`n        if (Mgr->mResourceNodes.Contains(Node)) { ++T.InvalidInManagerAtWipe; }`n    }`n}") }
    @{ Name = 'Q24 invalid slot dereferenced'; Key = 'wipe'; Expect = 'R10'; Find = 'if (!Node) { ++T.InvalidSlotsNull; return; }'; Repl = ('if (!Node) { ++T.InvalidSlotsNull; return; } if (!IsVal' + 'id(Node)) { }') }
    @{ Name = 'Q25 invalid-slot field dropped from census'; Key = 'wipe'; Expect = 'R4'; Find = ' invalidInManagerAtWipe=%d'; Repl = '' }
    @{ Name = 'Q28 F6 presence measured before UpdateNodeRepresentation'; Key = 'wipe'; Expect = 'R2'; Find = "    Node->UpdateNodeRepresentation();`n"; Repl = ''
       Find2 = "++T.RepPresentAfterUpdate; } }`n"; Repl2 = "++T.RepPresentAfterUpdate; } }`n    Node->UpdateNodeRepresentation();`n" }
    @{ Name = 'Q17 well-member snap-list drop removed'; Key = 'welld'; Expect = 'R9'; Find = ('SnapDropped = Mgr->mResourceNodes.RemoveSingle' + 'Swap(AsNode);'); Repl = 'SnapDropped = 0;' }
    @{ Name = 'Q18 well-member rep-by-actor drop removed'; Key = 'welld'; Expect = 'R9'; Find = "        RepMgr->RemoveRepresentationOfActor(Member);`n"; Repl = '' }
    @{ Name = 'Q19 well-member rep-by-node drop removed'; Key = 'welld'; Expect = 'R9'; Find = '{ RepMgr->RemoveRepresentation(NodeRep); }'; Repl = '{ }' }
    @{ Name = 'Q20 well-member snap-list drop moved after Destroy'; Key = 'welld'; Expect = 'R9'; Find = $wellSnapLine; Repl = ''
       Find2 = ("    Member->Dest" + "roy();`n"); Repl2 = ("    Member->Dest" + "roy();`n" + $wellSnapLine) }
    @{ Name = 'Q21 WELLDESPAWN-SNAP ungated'; Key = 'welld'; Expect = 'R9'; Find = ('    if (FNodeShuffleModule::AreDiagnostics' + 'Enabled()) { UE_LOG(LogNodeShuffle, Display, TEXT("WELLDESPAWN-SNAP'); Repl = '    { UE_LOG(LogNodeShuffle, Display, TEXT("WELLDESPAWN-SNAP' }
    @{ Name = 'Q22 WellDespawn manager include dropped'; Key = 'welld'; Expect = 'R9'; Find = '#include "Resources/FGResourceNodeManager.h"'; Repl = '' }
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
    if (($r | Where-Object { $_.StartsWith("($($m.Expect))") }).Count -gt 0) { Write-Host "RED  (ok) $($m.Name)" }
    else { Write-Host "GREEN (BAD) $($m.Name): expected $($m.Expect), got: $($r -join ' | ')"; $bad++ }
}
if ($bad -gt 0) { Write-Host "FAIL: $bad mutant(s) not caught"; exit 1 }
Write-Host "PASS: $($mutants.Count) mutants red"
exit 0
