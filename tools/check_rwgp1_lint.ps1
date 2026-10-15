# RWG P1 (2026-10-14) -- THE MAP-MODE / STORE-ISOLATION LINT.
#
# WHAT P1 PROMISES, AND WHAT EACH PIN STOPS:
#   R1  Off-vanilla roll is OFF: the parked constant is false, exactly once. P2 flips it -- deliberately,
#       by changing THIS pin in the same commit. A silent flip is the one-line shortcut this forbids.
#   R2  RefreshTick returns into RwgObserveTick off vanilla BEFORE the starter-node capture and BEFORE its
#       IsWorldReadyForRoll() call. Presence is not enough; this is an ORDER inside one function body.
#   R3  Store isolation, both stores, both directions (SYMMETRY): each Ensure*Loaded body tests the map mode
#       BEFORE its load latch is set and before any file path is built; each Flush*IfDirty body tests it
#       BEFORE its dirty check and before SaveStringToFile. A guard on one store only is the half-fix.
#   R4  The vanilla and RWG package literals live once, in NodeShuffleMapMode.h, and GetMapMode compares
#       with Equals (no fuzzy StartsWith/Contains on the package that would hedge a miss into "vanilla").
#   R5  IsWorldReadyForReroll ANDs the RWG latch off vanilla (spec B4), at its single site.
#   R6  The R1 lines exist with their tags at line-initial position inside TEXT(" and the late-node gate
#       still calls IsWorldReadyForReroll (the one site R5 relies on).
#   R7  No format string in the new files carries a legend: a `(...` parenthetical holding name=value
#       would collide with the field it explains (this repo has shipped that trap five times).
#
# Needles that name forbidden or pinned literals are BUILT BY CONCATENATION so this file cannot match itself.
# Exit 0 = all hold. Exit 1 = at least one does not. Exit 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'

$files = [ordered]@{
    mapcpp  = 'NodeShuffle\Private\NodeShuffleMapMode.cpp'
    r1cpp   = 'NodeShuffle\Private\NodeShuffleMapModeR1.cpp'
    maph    = 'NodeShuffle\Public\NodeShuffleMapMode.h'
    subsys  = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    subsysh = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    late    = 'NodeShuffle\Private\NodeShuffleLateNodes.cpp'
}
$text = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) {
        Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist -- this suite cannot check what it cannot read."
        exit 2
    }
    $text[$k] = Get-Content -Raw -Path $p
}
$fails = @()

# Body of one function, anchored on its full signature line (unique to its site), up to the next
# top-level "}\n" at column 0.
function Get-Body([string]$t, [string]$sig) {
    $i = $t.IndexOf($sig)
    if ($i -lt 0) { return $null }
    $m = [regex]::Match($t.Substring($i), "(?m)^\}\r?$")
    if (-not $m.Success) { return $null }
    return $t.Substring($i, $m.Index)
}
$cls = 'ANodeShuffle' + 'Subsystem::'
$notVanilla = 'GetMapMode() != ENodeShuffleMapMode::' + 'Vanilla'

# R1
$rollConst = 'constexpr bool bRwgRoll' + 'Enabled = false;'
$n = ([regex]::Matches($text.maph, [regex]::Escape($rollConst))).Count
if ($n -ne 1) { $fails += "R1: the parked off-vanilla roll constant must appear exactly once as false in NodeShuffleMapMode.h (found $n)" }
foreach ($k in @('mapcpp','r1cpp','subsys','subsysh')) {
    if ($text[$k] -match ('bRwgRoll' + 'Enabled\s*=')) { $fails += "R1: '$($files[$k])' assigns the roll constant; it lives only in NodeShuffleMapMode.h" }
}

# R2
$tick = Get-Body $text.subsys ('void ' + $cls + 'RefreshTick()')
if (-not $tick) { $fails += 'R2: RefreshTick body not found' } else {
    $obs  = $tick.IndexOf('RwgObserve' + 'Tick();')
    $cap  = $tick.IndexOf('if (!bPlayerStart' + 'Captured && !bLayoutGenerated)')
    $roll = $tick.IndexOf('IsWorldReady' + 'ForRoll()')
    $guard = $tick.IndexOf($notVanilla)
    if ($obs -lt 0 -or $guard -lt 0) { $fails += 'R2: RefreshTick lacks the off-vanilla observe branch' }
    elseif ($cap -lt 0 -or $roll -lt 0) { $fails += 'R2: RefreshTick lost the starter capture or the roll gate (pins cannot order what is missing)' }
    elseif (-not ($guard -lt $obs -and $obs -lt $cap -and $cap -lt $roll)) { $fails += 'R2: the observe branch must precede the starter capture and the IsWorldReadyForRoll gate' }
    else {
        $after = $tick.Substring($obs, 80)
        if ($after -notmatch '^RwgObserveTick\(\);\s*return;') { $fails += 'R2: RwgObserveTick() must be followed immediately by return;' }
    }
}

# R3
$stores = @(
    @{ fn = 'EnsureWaterGridLoaded() const'; latch = 'bWaterGridLoaded = true;'; io = 'FPaths::Combine(' },
    @{ fn = 'EnsureCaveStoreLoaded() const'; latch = 'bCaveStoreLoaded = true;'; io = 'FPaths::Combine(' },
    @{ fn = 'FlushWaterGridIfDirty() const'; latch = 'if (!bWaterGridDirty) { return; }'; io = 'SaveStringToFile(' },
    @{ fn = 'FlushCaveStoreIfDirty() const'; latch = 'if (!bCaveStoreDirty) { return; }'; io = 'SaveStringToFile(' }
)
foreach ($s in $stores) {
    $b = Get-Body $text.subsys ('void ' + $cls + $s.fn)
    if (-not $b) { $fails += "R3: body of $($s.fn) not found"; continue }
    $g = $b.IndexOf($notVanilla)
    $l = $b.IndexOf($s.latch)
    $io = $b.IndexOf($s.io)
    if ($g -lt 0) { $fails += "R3: $($s.fn) has no map-mode guard" }
    elseif ($l -lt 0 -or $io -lt 0) { $fails += "R3: $($s.fn) lost its latch/dirty check or its file I/O (pin anchors gone)" }
    elseif (-not ($g -lt $l -and $g -lt $io)) { $fails += "R3: $($s.fn) tests the map mode AFTER its latch/dirty check or file I/O" }
}
$gen = Get-Body $text.subsys ('void ' + $cls + 'WriteGeneratedLocationsJson(const TArray<FVector>& Locations) const')
if (-not $gen -or $gen.IndexOf($notVanilla) -lt 0 -or $gen.IndexOf($notVanilla) -gt $gen.IndexOf('SaveStringToFile(')) {
    $fails += 'R3: WriteGeneratedLocationsJson must test the map mode before writing'
}
# R3b: each off-vanilla guard block must END in `return;` directly before the vanilla path (R3 pins order, not the return).
$tails = [ordered]@{
    'EnsureWaterGridLoaded() const' = 'return;\s*\}\s*bWaterGridLoaded = true;\s*// 1\. The user'
    'EnsureCaveStoreLoaded() const' = 'return;\s*\}\s*bCaveStoreLoaded = true;\s*// 1\. The user'
    'FlushWaterGridIfDirty() const' = 'WaterGridNewSamples = 0;\s*return;\s*\}\s*if \(!bWaterGridDirty\) \{ return; \}'
    'FlushCaveStoreIfDirty() const' = 'CaveStoreNewRecords = 0;\s*return;\s*\}\s*if \(!bCaveStoreDirty\) \{ return; \}'
}
foreach ($fn in $tails.Keys) {
    $b = Get-Body $text.subsys ('void ' + $cls + $fn)
    if (-not $b -or $b -notmatch $tails[$fn]) { $fails += "R3b: $fn guard block does not end in return; before the vanilla path" }
}

# R4
$van = '/Game/FactoryGame/Map/GameLevel01/' + 'Persistent_Level'
$rwg = '/RandomWorldGenerator/Maps/ProceduralWorldLevel/' + 'ProceduralWorldLevel'
foreach ($lit in @($van, $rwg)) {
    $inH = ([regex]::Matches($text.maph, [regex]::Escape($lit))).Count
    $elsewhere = 0
    foreach ($k in @('mapcpp','r1cpp','subsys','subsysh','late')) { $elsewhere += ([regex]::Matches($text[$k], [regex]::Escape($lit))).Count }
    if ($inH -ne 1 -or $elsewhere -ne 0) { $fails += "R4: package literal '$lit' must appear once, in NodeShuffleMapMode.h only (h=$inH elsewhere=$elsewhere)" }
}
$gm = Get-Body $text.mapcpp ('ENodeShuffleMapMode ' + $cls + 'GetMapMode() const')
if (-not $gm) { $fails += 'R4: GetMapMode body not found' } else {
    if (([regex]::Matches($gm, 'MapPackageName\.Equals\(')).Count -ne 2) { $fails += 'R4: GetMapMode must classify with exactly two Equals comparisons' }
    if ($gm -match ('MapPackageName\.(Starts' + 'With|Con' + 'tains|Ends' + 'With)')) { $fails += 'R4: GetMapMode uses a fuzzy package match' }
}
if ($gm -and $gm -notmatch ('Equals\(NodeShuffleMapMode::VanillaPackage\(\), ESearchCase::IgnoreCase\)\)\s*\{\s*MapMode = ENodeShuffleMapMode::' + 'Vanilla;')) { $fails += 'R4b: the vanilla package literal no longer classifies as Vanilla' }

# R5
$rr = Get-Body $text.subsys ('bool ' + $cls + 'IsWorldReadyForReroll() const')
if (-not $rr -or $rr.IndexOf($notVanilla + ' && !bRwg' + 'Ready') -lt 0) { $fails += 'R5: IsWorldReadyForReroll lacks the off-vanilla readiness AND' }

# R6
foreach ($tag in @('MAPMODE','RWGWAIT','RWGREADY','RWGPROBE','RWGGEN','RWGOBSERVE')) {
    if ($text.mapcpp -notmatch ('TEXT\("' + $tag + '[: ]')) { $fails += "R6: '$tag' line missing from NodeShuffleMapMode.cpp" }
}
foreach ($tag in @('RWGCOLLISION','RWGWATER','RWGROCK')) {
    if ($text.r1cpp -notmatch ('TEXT\("' + $tag + '[: ]')) { $fails += "R6: '$tag' line missing from NodeShuffleMapModeR1.cpp" }
}
if ($text.late.IndexOf('!IsWorldReady' + 'ForReroll()') -lt 0) { $fails += 'R6: the late-node first-scan gate no longer calls IsWorldReadyForReroll (R5 would not reach it)' }

# R7: inside every TEXT("...") of the new files, no parenthetical contains name=value.
foreach ($k in @('mapcpp','r1cpp')) {
    foreach ($m in [regex]::Matches($text[$k], 'TEXT\("((?:[^"\\]|\\.)*)"\)')) {
        $fmt = $m.Groups[1].Value
        foreach ($p in [regex]::Matches($fmt, '\(([^()]*)\)')) {
            if ($p.Groups[1].Value -match '[A-Za-z]=') { $fails += "R7: '$($files[$k])' format string has name=value inside a parenthetical: ($($p.Groups[1].Value))" }
        }
    }
}

if ($fails.Count -gt 0) {
    $fails | ForEach-Object { Write-Host "FAIL $_" }
    exit 1
}
Write-Host "PASS: RWG P1 lint -- R1..R7 hold across $($files.Count) loaded files"
exit 0
