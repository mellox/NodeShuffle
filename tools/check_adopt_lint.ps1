# ADOPT lint (2026-10-31, packet rwg-farspawn-1f C1): adopt-on-load accounting (ADOPTMISS / ADOPTCENSUS), diagnostic only.
# Brief: _team/test-logs/2026-10-31-nodeshuffle-farspawn-1f-brief.md. LOADS and PINS NodeShuffleAdoptAudit.cpp, its two call-site files.
# Pins anchor on context unique to their site; needles naming a forbidden literal are BUILT BY CONCATENATION. Run under pwsh 7.
#   A1 ADOPTCENSUS reconciles by construction: preRegistered = adopted + matchedNotBound + unmatched; each row raises exactly one counter.
#      (round-1f review F4) lineReconciles = the independent identity adoptLineAdopted == adopted + boundNotPreRegistered, printed beside it.
#   A2 ADOPTMISS: one UNGATED Display line per valid pre-registered node left unbound; format + 13 arguments verbatim.
#   A3 Call sites: a note beside each PreRegistered++ (two), the emit at the end of AdoptRestoredSpawnedNodes after the Adopt-on-load line.
#   A4 Diagnostic only: the file binds, registers, moves, pins and destroys nothing.
#   A5 The match radius mirrors AdoptMatchRadiusCm (300 cm) and the reasons follow adopt pass 2's own order.
#   A6 The legend is prose: no '=' in it (no field=value token can collide with a field).
#   A7 Header declarations.
# Mutants run in-file. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    adopt = 'NodeShuffle\Private\NodeShuffleAdoptAudit.cpp'
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    subh  = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
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
function Invoke-AdoptChecks($T) {
    $f = @()
    $a = $T.adopt
    $emit = Span $a 'void ANodeShuffleSubsystem::AdoptAuditEmit(int32 AdoptLineAdopted)' 'RwgProvCandMarkDirty(TEXT("reload"));'
    if ($emit -eq '') { $f += '(A0) AdoptAuditEmit span missing'; return $f }
    # A1
    if ((Count $emit 'const int32 MatchedNotBound = AlreadyBound + Fracking + Component;') -ne 1 -or (Count $emit 'const int32 Unmatched = Gone + ResMismatch + Distance + EntryInactive;') -ne 1 -or
        (Count $emit '(PreRegistered == AdoptedPre + MatchedNotBound + Unmatched) ? 1 : 0') -ne 1 -or (Count $emit 'const int32 PreRegistered = AdoptPreRegistered.Num();') -ne 1) { $f += '(A1) the census identity terms' }
    foreach ($c in @('++Gone; continue;', '++AdoptedPre; continue;', '++Fracking;', '++Component;', '++AlreadyBound;', '++EntryInactive;', '++ResMismatch;', '++Distance;')) { if ((Count $emit $c) -ne 1) { $f += "(A1) '$c' must appear exactly once (one counter per row)" } }
    if (-not (Ordered $emit @('if (!IsValid(Node)) { ++Gone; continue; }', 'if (BoundEntry.Contains(Node)) { ++AdoptedPre; continue; }', 'IsFrackingActor(Node)', 'UNodeShuffleNodeComponent::Find(Node)', 'Reason = TEXT("alreadyBound"); ++AlreadyBound;', 'OtherWhy = TEXT("entryInactiveAtAdopt"); ++EntryInactive;', 'Reason = TEXT("resourceMismatch"); ++ResMismatch;', 'Reason = TEXT("distance"); ++Distance;', 'TEXT("ADOPTMISS: node='))) { $f += '(A1) row classification order: gone, bound, fracking, component, alreadyBound, entry tests, then the line' }
    # A2
    $miss = 'TEXT("ADOPTMISS: node=%s res=%s at=%s matchedEntry=%s reason=%s otherWhy=%s distCm=%.0f preRegEntry=%s boundTo=%s boundDistCm=%.0f thisOccupied=%d boundOccupied=%d legacy=%d"),'
    if ((Count $emit ('UE_LOG(LogNodeShuffle, Display, ' + $miss)) -ne 1) { $f += '(A2) ADOPTMISS format' }
    if ((Count $emit "*Node->GetName(), *AdoptShort(Res), *At.ToCompactString(), *Matched.ToString().Left(8), Reason, OtherWhy, DistCm,`n            *Row.Value.ToString().Left(8), *BoundName, bBoundValid ? FVector::Dist(At, BoundTo->GetActorLocation()) : -1.0,`n            IsNodeOccupiedAnyway(Node) ? 1 : 0, bBoundValid ? (IsNodeOccupiedAnyway(BoundTo) ? 1 : 0) : -1, bLegacy ? 1 : 0);") -ne 1) { $f += '(A2) ADOPTMISS arguments (13)' }
    if ($a.Contains('AreDiagnostics' + 'Enabled')) { $f += '(A2) ADOPTMISS / ADOPTCENSUS print ungated' }
    $cen = 'TEXT("ADOPTCENSUS: preRegistered=%d adopted=%d matchedNotBound=%d unmatched=%d alreadyBound=%d frackingSkipped=%d componentSkipped=%d goneAtAdopt=%d entryInactiveAtAdopt=%d resourceMismatchAtAdopt=%d distanceAtAdopt=%d boundNotPreRegistered=%d adoptLineAdopted=%d provisionalFlagUnbound=%d reconciles=%d lineReconciles=%d"),'
    if ((Count $emit $cen) -ne 1 -or (Count $emit "PreRegistered, AdoptedPre, MatchedNotBound, Unmatched, AlreadyBound, Fracking, Component, Gone, EntryInactive, ResMismatch, Distance,`n        BoundNotPre, AdoptLineAdopted, FlagUnbound, (PreRegistered == AdoptedPre + MatchedNotBound + Unmatched) ? 1 : 0,`n        (AdoptLineAdopted == AdoptedPre + BoundNotPre) ? 1 : 0);") -ne 1) { $f += '(A1) ADOPTCENSUS format/args (16)' }
    # A3
    if ((Count $T.sub "PreRegistered++;`n                    AdoptAuditNotePreRegistered(Node, Legacy->EntryGuid);") -ne 1 -or (Count $T.sub "PreRegistered++;`n                AdoptAuditNotePreRegistered(Node, E->EntryGuid);") -ne 1 -or (Count $T.sub 'PreRegistered++;') -ne 2 -or (Count $T.sub 'AdoptAuditNotePreRegistered(') -ne 2) { $f += '(A3) a note directly after each PreRegistered++ (two sites)' }
    $adoptFn = Span $T.sub 'void ANodeShuffleSubsystem::AdoptRestoredSpawnedNodes()' 'bool ANodeShuffleSubsystem::FinalizeAdoptedNode('
    if ($adoptFn -eq '' -or -not (Ordered $adoptFn @('TEXT("Adopt-on-load (real-class): adopted %d', '    AdoptAuditEmit(Adopted);')) -or (Count $T.sub 'AdoptAuditEmit(') -ne 1) { $f += '(A3) one emit, at the end of AdoptRestoredSpawnedNodes after its Adopt-on-load line' }
    # A4
    foreach ($bad in @(('Destroy' + '('), ('SpawnedNodes.' + 'Add('), ('SpawnedNodes.' + 'Remove('), ('RegisterManaged' + 'Node('), ('bPinned' + ' ='), ('bActive' + ' ='), ('SetActor' + 'Location'))) { if ($a.Contains($bad)) { $f += "(A4) diagnostic only: '$bad' must not appear in the audit file" } }
    # A5
    if ((Count $a 'constexpr float AdoptAuditMatchCm = 300.0f;') -ne 1 -or (Count $T.sub 'constexpr float AdoptMatchRadiusCm = 300.0f;') -ne 1) { $f += '(A5) match radius 300 cm, mirrored' }
    if (-not (Ordered $emit @('if (!E.bIsNewNode) { continue; }', 'if (!E.bActive) { continue; }', 'if (DSq >= BestSq) { continue; }', 'Res != E.AssignedResourceClassPath) { continue; }', 'BestSq = DSq; BestIdx = I;'))) { $f += '(A5) adopt pass 2 entry predicate (new, active, distance, resource; nearest wins)' }
    # A6
    $leg = Span $emit '    // Legend (prose only):' 'UE_LOG(LogNodeShuffle, Display, TEXT("ADOPTCENSUS'
    if ($leg -eq '' -or $leg.Contains('=')) { $f += '(A6) the ADOPTCENSUS legend must be prose with no = sign' }
    # A7
    if ((Count $T.subh 'void AdoptAuditNotePreRegistered(class AFGResourceNode* Node, const FGuid& EntryGuid);') -ne 1 -or (Count $T.subh 'void AdoptAuditEmit(int32 AdoptLineAdopted);') -ne 1 -or (Count $T.subh 'TMap<TWeakObjectPtr<AFGResourceNode>, FGuid> AdoptPreRegistered;') -ne 1) { $f += '(A7) header declarations' }
    if ((Count $a ('2026-10-31-rwg-' + 'farspawn-1f')) -ne 0) { $f += '(A7) no build marker inside the audit file (marker lives in NodeShuffle.cpp)' }
    return $f
}
$fails = @(Invoke-AdoptChecks $text)
$mutants = @(
    @{ Name = 'M1 identity drops goneAtAdopt'; Key = 'adopt'; Expect = 'A1'; Find = 'const int32 Unmatched = Gone + ResMismatch + Distance + EntryInactive;'; Repl = 'const int32 Unmatched = ResMismatch + Distance + EntryInactive;' }
    @{ Name = 'M2 reconciles hard-coded'; Key = 'adopt'; Expect = 'A1'; Find = '(PreRegistered == AdoptedPre + MatchedNotBound + Unmatched) ? 1 : 0,'; Repl = '1,' }
    @{ Name = 'M12 lineReconciles hard-coded (review F4)'; Key = 'adopt'; Expect = 'A1'; Find = '(AdoptLineAdopted == AdoptedPre + BoundNotPre) ? 1 : 0);'; Repl = '1);' }
    @{ Name = 'M13 lineReconciles dropped from the format (review F4)'; Key = 'adopt'; Expect = 'A1'; Find = ' reconciles=%d lineReconciles=%d"),'; Repl = ' reconciles=%d"),' }
    @{ Name = 'M3 alreadyBound counts nothing'; Key = 'adopt'; Expect = 'A1'; Find = 'Reason = TEXT("alreadyBound"); ++AlreadyBound;'; Repl = 'Reason = TEXT("alreadyBound");' }
    @{ Name = 'M4 ADOPTMISS diagnostics-gated'; Key = 'adopt'; Expect = 'A2'; Find = '        UE_LOG(LogNodeShuffle, Display, TEXT("ADOPTMISS'; Repl = ('        if (FNodeShuffleModule::AreDiagnostics' + 'Enabled()) UE_LOG(LogNodeShuffle, Display, TEXT("ADOPTMISS') }
    @{ Name = 'M5 ADOPTMISS drops boundOccupied'; Key = 'adopt'; Expect = 'A2'; Find = ' thisOccupied=%d boundOccupied=%d legacy=%d"),'; Repl = ' thisOccupied=%d legacy=%d"),' }
    @{ Name = 'M6 legacy note removed'; Key = 'sub'; Expect = 'A3'; Find = "PreRegistered++;`n                    AdoptAuditNotePreRegistered(Node, Legacy->EntryGuid);"; Repl = 'PreRegistered++;' }
    @{ Name = 'M7 emit removed'; Key = 'sub'; Expect = 'A3'; Find = '    AdoptAuditEmit(Adopted);'; Repl = '' }
    @{ Name = 'M8 the audit destroys the duplicate'; Key = 'adopt'; Expect = 'A4'; Find = '        UE_LOG(LogNodeShuffle, Display, TEXT("ADOPTMISS'; Repl = ('        if (Reason) { Node->Destroy' + '(); }' + "`n" + '        UE_LOG(LogNodeShuffle, Display, TEXT("ADOPTMISS') }
    @{ Name = 'M9 radius drifts'; Key = 'adopt'; Expect = 'A5'; Find = 'constexpr float AdoptAuditMatchCm = 300.0f;'; Repl = 'constexpr float AdoptAuditMatchCm = 600.0f;' }
    @{ Name = 'M10 legend gains a field token'; Key = 'adopt'; Expect = 'A6'; Find = '    // Legend (prose only): preRegistered counts'; Repl = ('    // Legend (prose only): preRegistered' + '=N counts') }
    @{ Name = 'M11 resource guard dropped'; Key = 'adopt'; Expect = 'A5'; Find = '                if (!Res.IsEmpty() && !E.AssignedResourceClassPath.IsEmpty() && Res != E.AssignedResourceClassPath) { continue; }'; Repl = '' }
)
$mutantFails = @(); $caught = 0
foreach ($m in $mutants) {
    $mut = [ordered]@{}; foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = @(Invoke-AdoptChecks $mut)
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ } else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) { foreach ($x in $all) { Write-Host "FAIL: $x" }; Write-Host "ADOPT LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."; exit 1 }
Write-Host "PASS: ADOPT lint -- A1..A7 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
