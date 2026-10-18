# NearestCave lint (2026-10-18): `NodeShuffle.NearestCave` -- nearest RWG cave mouth + nearest dealt cave node (log-only).
# LOADS and PINS every source file the packet touched. Anchors are full statements or a function body; needles naming a
# forbidden literal are BUILT BY CONCATENATION so this file cannot match itself.
#   N1  Registration (NodeShuffle.cpp): the command object, its name and its call of LogNearestCave, in order, once each.
#   N2  Body order: the map-mode test is the first statement; the off-RWG block prints the no-data line and returns (its only
#       return), before the pawn, the cave-file read and the layout walk.
#   N3  Bearing: ONE expression, Here's convention (atan2 of Target - P minus the pawn's actor yaw); both nav lines use it and
#       print "turn %+.0f deg and go"; the inverted forms are absent.
#   N4  Dealt-node population: bRwgCave entries; live = SpawnedNodes lookup; nearest over active entries only.
#   N5  Every UE_LOG format in the new file and the registration's fallback line starts with NEARESTCAVE: ; 9 UE_LOG sites.
#   N6  No field repeated inside any NEARESTCAVE format; no build-marker string in the new file.
#   N7  Header: LogNearestCave declared once, after the class tail, under its own public: with no private: between.
# Mutants run in-file. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    nc      = 'NodeShuffle\Private\NodeShuffleNearestCave.cpp'
    nsmod   = 'NodeShuffle\Private\NodeShuffle.cpp'
    subsysh = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
}
$text = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = (Get-Content -Raw -Path $p) -replace "`r`n", "`n"
}
$cls = 'ANodeShuffleSubsystem::'
function Get-Body([string]$t, [string]$sig) {
    $i = $t.IndexOf($sig); if ($i -lt 0) { return $null }
    $m = [regex]::Match($t.Substring($i), "(?m)^\}$"); if (-not $m.Success) { return $null }
    return $t.Substring($i, $m.Index)
}
function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }
function Ordered([string]$t, [string[]]$needles) {
    $at = -1
    foreach ($n in $needles) { $i = $t.IndexOf($n, $at + 1); if ($i -lt 0) { return $false }; $at = $i }
    return $true
}
function Between([string]$t, [string]$a, [string]$b) {
    $i = $t.IndexOf($a); if ($i -lt 0) { return $null }
    $j = $t.IndexOf($b, $i + $a.Length); if ($j -lt 0) { return $null }
    return $t.Substring($i, $j - $i)
}
function Fmt-Fields([string]$fmt) { return @([regex]::Matches($fmt, '(?<![A-Za-z0-9_])([A-Za-z][A-Za-z0-9_]*)=') | ForEach-Object { $_.Groups[1].Value }) }

function Invoke-NcChecks($T) {
    $f = @()
    # N1
    $cmdName = 'TEXT("NodeShuffle.' + 'NearestCave"),'
    if ((Count $T.nsmod $cmdName) -ne 1 -or (Count $T.nsmod 'It->LogNearestCave();') -ne 1 -or
        -not (Ordered $T.nsmod @('static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleNearestCaveCmd(', $cmdName, 'It->LogNearestCave();'))) {
        $f += '(N1) NodeShuffle.NearestCave must be registered once and call LogNearestCave'
    }
    # N2
    $b = Get-Body $T.nc ('void ' + $cls + 'LogNearestCave() const')
    if (-not $b) { $f += '(N2) LogNearestCave body not found'; return $f }
    $firstStmt = $b.IndexOf('const ENodeShuffleMapMode Mode = GetMapMode();')
    if ($firstStmt -lt 0 -or $firstStmt -gt $b.IndexOf('GetWorld()') -or $firstStmt -gt $b.IndexOf('Layout')) { $f += '(N2) the map-mode read must be the first statement' }
    $off = Between $b 'if (Mode != ENodeShuffleMapMode::Rwg)' 'const UWorld* World = GetWorld();'
    if (-not $off -or -not $off.Contains('TEXT("NEARESTCAVE: no RWG cave data on this map') -or (Count $off 'return;') -ne 1) { $f += '(N2) the off-RWG block must print the no-data line and return' }
    if (-not (Ordered $b @('if (Mode != ENodeShuffleMapMode::Rwg)', 'const APawn* Pawn = Pc ? Pc->GetPawn() : nullptr;',
        'FFileHelper::LoadFileToString(Text, *JsonPath)', 'for (const FNodeShuffleEntry& E : Layout)'))) { $f += '(N2) order: map mode, pawn, cave file, layout walk' }
    # N3
    $bearing = 'return FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)) - Pawn->GetActorRotation().Yaw);'
    if ((Count $b $bearing) -ne 1 -or (Count $b 'const FVector D = Target - P;') -ne 1) { $f += '(N3) the bearing must be Here''s expression, once' }
    if ((Count $b 'TurnFromFacing(BestMouth)') -ne 1 -or (Count $b 'TurnFromFacing(Best->Location)') -ne 1 -or (Count $b 'turn %+.0f deg and go') -ne 2) { $f += '(N3) both nav lines must use TurnFromFacing and print the turn wording' }
    foreach ($bad in @(('Atan2(D.X' + ', D.Y)'), ('.Yaw - FMath::Radians' + 'ToDegrees'), ('D = P' + ' - Target'))) { if ($b.Contains($bad)) { $f += "(N3) inverted bearing form present" } }
    # N4
    if ((Count $b 'if (!E.bRwgCave) { continue; }') -ne 1 -or (Count $b 'AFGResourceNode* const* Spawned = SpawnedNodes.Find(E.EntryGuid);') -ne 1 -or
        -not (Ordered $b @('if (!E.bRwgCave) { continue; }', 'SpawnedNodes.Find(E.EntryGuid);', 'if (!E.bActive) { ++Inactive; continue; }', 'Best = &E;'))) { $f += '(N4) population: bRwgCave entries, live by SpawnedNodes, nearest over active only' }
    # N5
    $fmts = @([regex]::Matches($T.nc, 'UE_LOG\(LogNodeShuffle, Display,\s*TEXT\("([^"]*)"') | ForEach-Object { $_.Groups[1].Value })
    if ($fmts.Count -ne 9) { $f += "(N5) expected 9 UE_LOG sites in the new file, found $($fmts.Count)" }
    foreach ($x in $fmts) { if (-not $x.StartsWith('NEARESTCAVE: ')) { $f += "(N5) format not line-initial NEARESTCAVE: '$($x.Substring(0, [Math]::Min(30, $x.Length)))'" } }
    if ((Count $T.nsmod 'TEXT("NEARESTCAVE: NodeShuffle subsystem not found') -ne 1) { $f += '(N5) registration fallback line must start with NEARESTCAVE:' }
    # N6
    foreach ($x in $fmts) {
        $dup = (Fmt-Fields $x) | Group-Object | Where-Object { $_.Count -gt 1 }
        if ($dup) { $f += "(N6) repeated field(s) $(($dup | ForEach-Object { $_.Name }) -join ',')" }
    }
    if ($T.nc.Contains('LOADED' + ' (')) { $f += '(N6) no build-marker string in the new file' }
    # N7
    $tail = Between $T.subsysh 'static constexpr float WellSelfOverlapFloorCm = 800.0f;' 'void LogNearestCave() const;'
    if ((Count $T.subsysh 'void LogNearestCave() const;') -ne 1 -or -not $tail -or -not $tail.Contains("`npublic:`n") -or $tail.Contains('private:')) { $f += '(N7) header declaration must sit under its own public: after the class tail' }
    return $f
}

$fails = Invoke-NcChecks $text
$mutants = @(
    @{ Name = 'M1 command not registered'; Key = 'nsmod'; Expect = 'N1'; Find = ('    TEXT("NodeShuffle.' + 'NearestCave"),' + "`n"); Repl = '' }
    @{ Name = 'M2 off-RWG early return removed'; Key = 'nc'; Expect = 'N2'; Find = ('TEXT("unlatched")));' + "`n" + '        return;'); Repl = 'TEXT("unlatched")));' }
    @{ Name = 'M3 bearing atan2 arguments swapped'; Key = 'nc'; Expect = 'N3'; Find = 'FMath::Atan2(D.Y, D.X)'; Repl = ('FMath::Atan2(D.X' + ', D.Y)') }
    @{ Name = 'M4 bearing delta inverted'; Key = 'nc'; Expect = 'N3'; Find = 'const FVector D = Target - P;'; Repl = ('const FVector D = P' + ' - Target;') }
    @{ Name = 'M5 live test nulled'; Key = 'nc'; Expect = 'N4'; Find = 'AFGResourceNode* const* Spawned = SpawnedNodes.Find(E.EntryGuid);'; Repl = 'AFGResourceNode* const* Spawned = nullptr;' }
    @{ Name = 'M6 header public: dropped'; Key = 'subsysh'; Expect = 'N7'; Find = ("`npublic:`n" + '    // RWG cave navigation'); Repl = ("`n" + '    // RWG cave navigation') }
    @{ Name = 'M7 map-mode test moved after the pawn'; Key = 'nc'; Expect = 'N2'; Find = 'if (Mode != ENodeShuffleMapMode::Rwg)'; Repl = 'if (false)' }
)
$mutantFails = @()
$caught = 0
foreach ($m in $mutants) {
    $mut = @{}
    foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = Invoke-NcChecks $mut
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ }
    else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "NEARESTCAVE LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: NearestCave lint -- N1..N7 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
