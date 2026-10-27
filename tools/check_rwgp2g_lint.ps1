# RWG P2g lint (2026-10-18): continuous bounded late capture (60 s period, session growth cap) + visible-original re-hide census,
# plus the coordinator's killbox resolution change (both tops known -> the HIGHER Z).
# LOADS and PINS every source file the P2g packet touched. Anchors are site-unique (function bodies or full statements); needles
# naming a forbidden or pinned literal are BUILT BY CONCATENATION so this file cannot match itself.
#   G1  Policy: ScanDue = not capped AND (first scan OR one full period since the LAST scan); 60 s / 2048 pinned by value.
#   G2  MaybeCaptureLateOriginalNodes: cap test (once-only Warning naming the cap) BEFORE ScanDue; ScanDue fed the LAST scan time;
#       every completed scan restarts the period and adds to the session total; LATE-NODES running census, no exhaustion field.
#   G3  Dedupe in CaptureLateOriginalNodes: path (KnownPaths), F2 300 cm/class/resource guard on our active new-node entries,
#       recorded-anchor deferral -- all still continue before a node becomes a candidate.
#   G4  LATE-CAPTURE: one format, logged at the pinned site and on the hidden+dealt original branch, 32 lines per scan then a count.
#   G5  The deferred-locations Warning is latched per resource (the periodic scan would otherwise repeat it every period).
#   G6  Census Pre: every 12th pass, <= 256 records round-robin, identity mirror, SAME steady instance, hidden-flag OR collision test,
#       steady mark removed so SuppressOriginalNodes re-hides; nothing in the census hides an actor itself. REHIDE-VISIBLE / -CENSUS formats.
#   G7  ApplyLayout brackets SuppressOriginalNodes with Pre/Post; Suppress's steady skip still requires the hidden flag; SteadyHiddenAtPass
#       is written beside the steady mark and emptied beside it.
#   G8  Marker rwg-p2g-1 present once, rwg-p2f-2 absent.
#   G9  RwgEnsureKillboxTop: both known -> FMath::Max; never Min.
#   G10 No field repeated inside any new format string; no cause words in the new lines.
#   G11 The two test files are pinned (census bracket, session-total reset, case floor, cap and periodic cases), not just loaded.
#   (G7 also pins Suppress's REHIDE-UNHIDDEN counter: the same steady instance found with its hidden flag cleared.)
# Mutants run in-file (Invoke-P2gChecks on mutated text). Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'

$files = [ordered]@{
    policy  = 'NodeShuffle\Private\NodeShuffleLateNodePolicy.h'
    late    = 'NodeShuffle\Private\NodeShuffleLateNodes.cpp'
    subsys  = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    subsysh = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    nsmod   = 'NodeShuffle\Private\NodeShuffle.cpp'
    void    = 'NodeShuffle\Private\NodeShuffleMapModeVoid.cpp'
}
$text = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = (Get-Content -Raw -Path $p) -replace "`r`n", "`n"
}
foreach ($t in @('tools\test_late_nodes.py', 'tools\test_late_node_policy.cpp')) {
    $p = Join-Path $root $t
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$t' does not exist"; exit 2 }
    $text[$t] = (Get-Content -Raw -Path $p) -replace "`r`n", "`n"
}

$cls = 'ANodeShuffleSubsystem::'
function Get-Body([string]$t, [string]$sig) {
    $i = $t.IndexOf($sig)
    if ($i -lt 0) { return $null }
    $m = [regex]::Match($t.Substring($i), "(?m)^\}$")
    if (-not $m.Success) { return $null }
    return $t.Substring($i, $m.Index)
}
function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }
function Ordered([string]$t, [string[]]$needles) {
    $at = -1
    foreach ($n in $needles) { $i = $t.IndexOf($n, [Math]::Max(0, $at)); if ($i -lt 0 -or $i -lt $at) { return $false }; $at = $i + 1 }
    return $true
}
function Fmt-Fields([string]$fmt) { return @([regex]::Matches($fmt, '([A-Za-z]+)=%') | ForEach-Object { $_.Groups[1].Value }) }

function Invoke-P2gChecks($T) {
    $f = @()
    # G1
    if (-not $T.policy.Contains('return !GrowthCapReached(TotalAddedSession, Cap) && (CompletedScans == 0 || Now - LastScan' + 'Time >= Period);')) { $f += '(G1) ScanDue must be: not capped AND (first scan OR a full period since the last scan)' }
    if (-not $T.policy.Contains('return TotalAddedSession >= ' + 'Cap;')) { $f += '(G1) GrowthCapReached must be inclusive (>= Cap)' }
    if (-not $T.policy.Contains('constexpr double ScanPeriodSeconds = 60.' + '0;') -or -not $T.policy.Contains('constexpr int GrowthCapEntries = ' + '2048;')) { $f += '(G1) period 60 s and cap 2048 must be pinned by value' }
    if ($T.policy.Contains('CompletedScans == ' + '1 &&')) { $f += '(G1) the two-scan window must be gone' }
    # G2
    $m = Get-Body $T.late ('void ' + $cls + 'MaybeCaptureLateOriginalNodes()')
    if (-not $m) { $f += '(G2) MaybeCaptureLateOriginalNodes body not found' } else {
        if (-not (Ordered $m @('if (NodeShuffleLateNodePolicy::GrowthCapReached(LateNodeTotalAddedSession, NodeShuffleLateNodePolicy::GrowthCapEntries))', 'if (!bLateNodeGrowthCapWarned)', 'bLateNodeGrowthCapWarned = true;', 'TEXT("LATE-NODES growth cap reached: ', 'NodeShuffleLateNodePolicy::ScanDue('))) { $f += '(G2) the once-only growth-cap Warning must precede ScanDue' }
        if (-not $m.Contains('ScanDue(LateNodeScansCompleted, Now, LastLateNodeScan' + 'Time,')) { $f += '(G2) ScanDue must be fed the LAST scan time' }
        if (-not (Ordered $m @('CaptureLateOriginalNodes();', ('LastLateNodeScanTime = ' + 'Now;'), '++LateNodeScansCompleted;', ('LateNodeTotalAddedSession += ' + 'AddedEntries;')))) { $f += '(G2) every completed scan must restart the period and add to the session total' }
        if (-not $m.Contains('TEXT("LATE-NODES scan=%d addedEntries=%d addedHideRecords=%d totalAddedSession=%d layoutTotal=%d hideRecordsTotal=%d durationMs=%.1f elapsedSinceFirstS=%.0f")')) { $f += '(G2) the LATE-NODES running census format changed' }
        if ($m.Contains('schedule' + 'Exhausted')) { $f += '(G2) the exhaustion field must be gone' }
    }
    # G3
    $c = Get-Body $T.late ('void ' + $cls + 'CaptureLateOriginalNodes()')
    if (-not $c) { $f += '(G3) CaptureLateOriginalNodes body not found' } else {
        if (-not $c.Contains('if (KnownPaths.Contains(Node->GetPathName())) { ++Known; ' + 'continue; }')) { $f += '(G3) path dedupe (KnownPaths) must continue' }
        if (-not $c.Contains('if (!E.bIsNewNode || !E.bActive) { ' + 'continue; }') -or -not $c.Contains('&& FVector::DistSquared(E.Location, Node->GetActorLocation()) < FMath::Square(300.' + '0f))')) { $f += '(G3) the F2 guard must test active new-node entries within 300 cm' }
        if (-not $c.Contains('if (bOnOurDestination) { ++OnOurDestination; ' + 'continue; }')) { $f += '(G3) the F2 guard must continue' }
        if (-not $c.Contains('if (bRecordedAnchor) { ++AnchorDeferred; ' + 'continue; }')) { $f += '(G3) the recorded-anchor deferral must continue' }
        if (-not (Ordered $c @('if (KnownPaths.Contains(', 'if (bOnOurDestination) {', 'if (bRecordedAnchor) { ++AnchorDeferred;', 'Candidates.Add(Node);'))) { $f += '(G3) all three dedupe guards must precede Candidates.Add' }
        # G4
        if ((Count $c ('LogLate' + 'Capture(Node);')) -ne 2) { $f += '(G4) LATE-CAPTURE must be logged at exactly the two capture sites' }
        if (-not $c.Contains('if (bHasOriginal) { OriginalNodeRecord.Add(Rec); LogLate' + 'Capture(Node); }')) { $f += '(G4) the dealt-original site must log only for a real original' }
        # G5
        if (-not (Ordered $c @('if (Locations.Num() != Active)', 'if (!LateDeferredLocationWarnings.Contains(ResourcePath))', 'LateDeferredLocationWarnings.Add(ResourcePath);', 'TEXT("LATE-NODES deferred: resource='))) { $f += '(G5) the deferred-locations Warning must be latched per resource' }
        if ($c.Contains('remaining scheduled ' + 'scan')) { $f += '(G5) stale two-scan wording remains' }
    }
    $lc = Get-Body $T.late ('void ' + $cls + 'LogLateCapture(')
    if (-not $lc -or -not (Ordered $lc @('if (LateCaptureLinesThisScan >= 32) { ++LateCaptureUnlistedThisScan; return; }', 'TEXT("LATE-CAPTURE: class=%s path=%s at=(%.0f,%.0f,%.0f) scan=%d")'))) { $f += '(G4) LATE-CAPTURE must be bounded at 32 lines per scan with the brief format' }
    # G6
    $pre = Get-Body $T.late ('void ' + $cls + 'RehideVisibleOriginalsPre()')
    $post = Get-Body $T.late ('void ' + $cls + 'RehideVisibleOriginalsPost()')
    if (-not $pre -or -not $post) { $f += '(G6) census bodies not found' } else {
        if (-not (Ordered $pre @('++RehideCensusPass;', 'if ((RehideCensusPass % 12) != 1) { return; }', 'constexpr int32 MaxChecksPerPass = 256;', 'FMath::Min(Records, MaxChecksPerPass)'))) { $f += '(G6) census must run every 12th pass over at most 256 records' }
        if (-not $pre.Contains('RehideCensusCursor = (RehideCensusCursor + 1) % ' + 'Records;')) { $f += '(G6) census must walk round-robin' }
        if (-not $pre.Contains('FVector::Dist(Orig->GetActorLocation(), Anchor) >= IdentityMatchCm) { ++RehideCensusIdentityReject; ' + 'continue; }')) { $f += '(G6) the identity mirror must reject before any re-hide' }
        if (-not $pre.Contains('if (Steady->Get() != Orig) { ++RehideCensusNewInstance; ' + 'continue; }')) { $f += '(G6) only the SAME steady instance may count as visible again' }
        if (-not $pre.Contains('if (bActorHidden && !bCollision) { ++RehideCensusHiddenOk; ' + 'continue; }')) { $f += '(G6) visible-again = hidden flag cleared OR collision on' }
        if (-not (Ordered $pre @('if (bActorHidden && !bCollision)', 'SteadyHiddenOriginals.Remove(Rec.VanillaNodePath);', 'RehidePendingNodes.Add(Orig);'))) { $f += '(G6) a visible-again original must lose its steady mark (re-hide through SuppressOriginalNodes)' }
        foreach ($b in @($pre, $post)) { if ($b.Contains('SetActorHidden' + 'InGame') -or $b.Contains('SetActorEnable' + 'Collision')) { $f += '(G6) the census must not hide anything itself' } }
        if (-not $pre.Contains('TEXT("REHIDE-VISIBLE: class=%s path=%s at=(%.0f,%.0f,%.0f) passesSinceHidden=%d actorHidden=%d collisionOn=%d")')) { $f += '(G6) REHIDE-VISIBLE format changed' }
        if (-not $post.Contains('TEXT("REHIDE-CENSUS: pass=%d checked=%d visibleAgain=%d rehidden=%d unresolved=%d identityReject=%d occupied=%d notSteady=%d newInstance=%d hiddenOk=%d actorGone=%d records=%d steadyUnhiddenSinceCensus=%d")')) { $f += '(G6) REHIDE-CENSUS format changed' }
        if (-not (Ordered $post @('RehideSteadyUnhiddenSinceCensus);', ('RehideSteadyUnhiddenSinceCensus = ' + '0;')))) { $f += '(G6) the steady-unhidden count must be printed, then reset per census line' }
        if (-not $post.Contains('if (N->IsHidden() && !N->GetActorEnable' + 'Collision()) { ++Rehidden; }')) { $f += '(G6) rehidden must be measured after the hide pass' }
    }
    # G7
    $ap = Get-Body $T.subsys ('void ' + $cls + 'ApplyLayout()')
    if (-not $ap -or -not (Ordered $ap @('RehideVisibleOriginalsPre();', 'SuppressOriginalNodes();', 'RehideVisibleOriginalsPost();')) -or (Count $ap 'RehideVisibleOriginalsPre();') -ne 1) { $f += '(G7) ApplyLayout must bracket SuppressOriginalNodes with census Pre/Post' }
    $su = Get-Body $T.subsys ('void ' + $cls + 'SuppressOriginalNodes()')
    if (-not $su -or -not $su.Contains('if (Steady->Get() == Node && Node->Is' + 'Hidden())')) { $f += '(G7) the steady skip must still require the hidden flag (it is what re-hides an un-hidden instance)' }
    if (-not $su -or -not (Ordered $su @(('if (Steady->Get() == Node && Node->Is' + 'Hidden())'), ('if (Steady->Get() == ' + 'Node)' + "`n"), '++RehideSteadyUnhiddenSinceCensus;', ('if (++RehideSteadyUnhiddenLinesThisPass <= ' + '8)'), 'TEXT("REHIDE-UNHIDDEN: class=%s path=%s pass=%d")'))) { $f += '(G7) Suppress must count (and boundedly log) a same steady instance found un-hidden, after the skip test' }
    if (-not $su -or -not (Ordered $su @('SteadyHiddenOriginals.Add(Rec.VanillaNodePath, Node);', 'SteadyHiddenAtPass.Add(Rec.VanillaNodePath, RehideCensusPass);'))) { $f += '(G7) SteadyHiddenAtPass must be written beside the steady mark' }
    if (-not $T.subsys.Contains("SteadyHiddenOriginals.Empty();`n    SteadyHiddenAtPass.Empty();")) { $f += '(G7) SteadyHiddenAtPass must be emptied beside the steady marks' }
    $bp = Get-Body $T.subsys ('void ' + $cls + 'BeginPlay()')
    if (-not $bp -or -not $bp.Contains('LateNodeTotalAddedSession = ' + '0;') -or (Count $T.subsys ('LateNodeTotalAddedSession = ' + '0;')) -ne 1) { $f += '(G7) the session total resets only at BeginPlay' }
    if ($T.subsysh.Contains('UPROPERTY(SaveGame) int32 LateNodeTotal' + 'AddedSession')) { $f += '(G7) the session total must not be SaveGame' }
    # G11 the test files are read, not merely loaded
    $py = $T['tools\test_late_nodes.py']; $cpp = $T['tools\test_late_node_policy.cpp']
    if (-not $py.Contains("assert apply.index('RehideVisibleOriginalsPre();') < apply.index('SuppressOriginalNodes();') < apply.index('RehideVisibleOriginalsPost();')")) { $f += '(G11) test_late_nodes.py must pin the census bracket order' }
    if (-not $py.Contains("assert subsystem.count('LateNodeTotalAddedSession = " + "0;') == 1")) { $f += '(G11) test_late_nodes.py must pin the single session-total reset' }
    if (-not $py.Contains('assert cases >= ' + '36')) { $f += '(G11) test_late_nodes.py must keep the case-count floor' }
    if (-not $cpp.Contains('static_assert(!ScanDue(9, 999., 0., P, Cap, Cap), "reaching the cap stops ' + 'scanning");')) { $f += '(G11) the cap case must stay in the policy test' }
    if (-not $cpp.Contains('static_assert(ScanDue(2, 420., 360., P, 0, Cap), "a third scan is due one period after the ' + 'second (no exhaustion)");')) { $f += '(G11) the periodic third-scan case must stay in the policy test' }
    if (([regex]::Matches($cpp, '(?m)^static_assert\(')).Count -lt 36) { $f += '(G11) the policy test must keep at least 36 cases' }
    # G8
    if ((Count $T.nsmod ('LOADED (2026-10-26-rwg-' + '0-1-12-2)')) -ne 1 -or (Count $T.nsmod ('2026-10-26-rwg-' + '0-1-12-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'steep-2)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'steep-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'deal-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'density-2)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'rockcache-2')) -ne 0 -or (Count $T.nsmod ('2026-10-18-rwg-' + 'p4c-2')) -ne 0 -or (Count $T.nsmod ('2026-10-18-rwg-' + 'p4c-1')) -ne 0 -or (Count $T.nsmod ('2026-10-18-rwg-' + 'p5-2')) -ne 0) { $f += '(G8) marker must be rwg-repclass-3 (rwg-repclass-2 absent) with rwg-p4c-2, rwg-p4c-1 and rwg-p5-2 absent (moved by the RWG 0.1.9 accept list)' }
    # G9
    $kb = Get-Body $T.void ('void ' + $cls + 'RwgEnsureKillboxTop(')
    if (-not $kb -or -not $kb.Contains('RwgKillboxTopZ = FMath::Max(RwgKillboxTopActorZ, RwgKillboxTop' + 'ApiZ);') -or $kb.Contains('FMath::Min(RwgKillboxTopActorZ, RwgKillboxTop' + 'ApiZ)')) { $f += '(G9) both known -> the HIGHER killbox top (Max), never Min' }
    # G10
    $fmts = [regex]::Matches($T.late, 'TEXT\("((?:LATE-CAPTURE|LATE-NODES scan|REHIDE-VISIBLE|REHIDE-CENSUS)[^"]*)"\)') | ForEach-Object { $_.Groups[1].Value }
    foreach ($fmt in $fmts) {
        $fl = Fmt-Fields $fmt
        if (($fl | Select-Object -Unique).Count -ne $fl.Count) { $f += "(G10) repeated field in '$($fmt.Substring(0, [Math]::Min(40, $fmt.Length)))'" }
        if ($fmt -match ('All' + 'Minable|because|re-' + 'showed')) { $f += "(G10) cause words in '$($fmt.Substring(0, [Math]::Min(40, $fmt.Length)))'" }
    }
    if (@($fmts).Count -lt 4) { $f += '(G10) expected the four new format strings' }
    return $f
}

$fails = Invoke-P2gChecks $text

$mutants = @(
    @{ Name = 'M1 scan period never fires';        Key = 'policy'; Expect = 'G1'; Find = '(CompletedScans == 0 || Now - LastScanTime >= Period)'; Repl = '(CompletedScans == 0)' }
    @{ Name = 'M2 growth cap removed (policy)';    Key = 'policy'; Expect = 'G1'; Find = 'return !GrowthCapReached(TotalAddedSession, Cap) && ('; Repl = 'return (' }
    @{ Name = 'M3 growth cap removed (schedule)';  Key = 'late';   Expect = 'G2'; Find = 'if (NodeShuffleLateNodePolicy::GrowthCapReached(LateNodeTotalAddedSession, NodeShuffleLateNodePolicy::GrowthCapEntries))'; Repl = 'if (false)' }
    @{ Name = 'M4 period never restarts';          Key = 'late';   Expect = 'G2'; Find = 'LastLateNodeScanTime = Now;'; Repl = '' }
    @{ Name = 'M5 F2 dedupe guard removed';        Key = 'late';   Expect = 'G3'; Find = 'if (bOnOurDestination) { ++OnOurDestination; continue; }'; Repl = 'if (bOnOurDestination) { ++OnOurDestination; }' }
    @{ Name = 'M6 path dedupe removed';            Key = 'late';   Expect = 'G3'; Find = 'if (KnownPaths.Contains(Node->GetPathName())) { ++Known; continue; }'; Repl = '' }
    @{ Name = 'M7 re-hide skipped';                Key = 'late';   Expect = 'G6'; Find = 'SteadyHiddenOriginals.Remove(Rec.VanillaNodePath);'; Repl = '' }
    @{ Name = 'M8 steady skip ignores hidden flag'; Key = 'subsys'; Expect = 'G7'; Find = 'if (Steady->Get() == Node && Node->IsHidden())'; Repl = 'if (Steady->Get() == Node)' }
    @{ Name = 'M9 census after the hide pass';     Key = 'subsys'; Expect = 'G7'; Find = "RehideVisibleOriginalsPre();`n    SuppressOriginalNodes();"; Repl = "SuppressOriginalNodes();`n    RehideVisibleOriginalsPre();" }
    @{ Name = 'M10 killbox min instead of max';    Key = 'void';   Expect = 'G9'; Find = 'FMath::Max(RwgKillboxTopActorZ, RwgKillboxTopApiZ)'; Repl = 'FMath::Min(RwgKillboxTopActorZ, RwgKillboxTopApiZ)' }
    @{ Name = 'M11 LATE-CAPTURE unbounded';        Key = 'late';   Expect = 'G4'; Find = 'if (LateCaptureLinesThisScan >= 32) { ++LateCaptureUnlistedThisScan; return; }'; Repl = '' }
    @{ Name = 'M13 steady-unhidden count deleted';  Key = 'subsys'; Expect = 'G7'; Find = '++RehideSteadyUnhiddenSinceCensus;'; Repl = '' }
    @{ Name = 'M14 policy cap case deleted';        Key = 'tools\test_late_node_policy.cpp'; Expect = 'G11'; Find = 'static_assert(!ScanDue(9, 999., 0., P, Cap, Cap), "reaching the cap stops scanning");'; Repl = '' }
    @{ Name = 'M15 census bracket test deleted';    Key = 'tools\test_late_nodes.py'; Expect = 'G11'; Find = "assert apply.index('RehideVisibleOriginalsPre();')"; Repl = "pass  # apply.index('RehideVisibleOriginalsPre();')" }
    @{ Name = 'M12 deferred warning unlatched';    Key = 'late';   Expect = 'G5'; Find = 'if (!LateDeferredLocationWarnings.Contains(ResourcePath))'; Repl = 'if (true)' }
)
$mutantFails = @()
$caught = 0
foreach ($m in $mutants) {
    $mut = @{}
    foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = Invoke-P2gChecks $mut
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ }
    else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}

$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "RWG P2g LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: RWG P2g lint -- G1..G11 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
