# check_rocks_lint.ps1 -- ns-rocks-1 (2026-11-14): rock near search (A), rule B sunken solid tops (B), censuses (C), chat probe (D).
# Handoff: _team/test-logs/2026-11-14-nodeshuffle-rocks-1-handoff.md
# LOADS every file the packet touched: Subsystem.cpp/.h, MapModeCliffTop.cpp, MapModePlace.cpp, MapModeSpawnPass.cpp,
# MapModeProvisional.cpp, MapModeRocks.cpp, NodeShuffleRocks.h, NodeShuffleChatProbe.cpp, NodeShuffle.cpp, NodeShuffleBuildMarkers.h,
# NodeShuffleMcpDescribe.cpp, and the lints it edited (farspawn, rollchat, mcpcontract, rwgtrace).
#   K1  marker 2026-11-14-rocks-1: newest in the markers header, once on its own line in NodeShuffle.cpp, absent from the new files
#   K2  near search runs BEFORE the spiral, only after a rock-refused primary: NearTries gated on bPrimaryRockRefused, near steps first
#   K3  ONE acceptance: RaycastSettle holds exactly three RaycastGroundAt calls (cave short trace, primary, shared loop) and one relocation commit block
#   K4  rule B: margin 50 cm vs the API Z at the TOP's own XY; terrain-absent defers through RaycastGroundAt; refusal returns false
#   K5  no re-placement: the new files never settle, redeal or read settle/pin state; the pin and bRayCasted guards still precede settles
#   K6  every census and sample line exists; the census call sits in the PLACECENSUS block before RwgEmitDzCensus; PROVSETTLE fields
#   K7  no field=value syntax inside the prose legends of the new lines
#   K8  chat probe: registered, six variants each naming itself, one broadcast, limits read by reflection only (no direct call)
#   K9  first-hit capture armed for the primary only and cleared right after it
#   K10 spiral/near probe start raised to the API Z at the probe XY
#   K11 the new members are plain (no UPROPERTY, never saved)
#   K12 new files under 500 lines
# Forbidden needles are built by concatenation. Mutants run in-file. Exit 0 = all hold and every mutant is caught; 1 = failure; 2 = missing file.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    subh  = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    cliff = 'NodeShuffle\Private\NodeShuffleMapModeCliffTop.cpp'
    place = 'NodeShuffle\Private\NodeShuffleMapModePlace.cpp'
    spass = 'NodeShuffle\Private\NodeShuffleMapModeSpawnPass.cpp'
    prov  = 'NodeShuffle\Private\NodeShuffleMapModeProvisional.cpp'
    rocks = 'NodeShuffle\Private\NodeShuffleMapModeRocks.cpp'
    rockh = 'NodeShuffle\Private\NodeShuffleRocks.h'
    probe = 'NodeShuffle\Private\NodeShuffleChatProbe.cpp'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
    mark  = 'NodeShuffle\Private\NodeShuffleBuildMarkers.h'
    desc  = 'NodeShuffle\Private\NodeShuffleMcpDescribe.cpp'
}
$lintFiles = [ordered]@{ lfar = 'check_farspawn_lint.ps1'; lroll = 'check_rollchat_lint.ps1'; lmcp = 'check_mcpcontract_lint.ps1'; ltrace = 'check_rwgtrace_lint.ps1' }
$text = [ordered]@{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = ((Get-Content -Raw -Encoding utf8 -Path $p) -replace "`r`n", "`n")
}
foreach ($k in $lintFiles.Keys) {
    $p = Join-Path $PSScriptRoot $lintFiles[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned lint '$($lintFiles[$k])' does not exist"; exit 2 }
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

$marker   = '2026-11-14-' + 'rocks-1'
$nearGate = 'const int32 NearTries = bPrimaryRockRefused ? NodeShuffleRocks::NearProbeCount : 0;'
$loopHead = 'for (int32 Step = 0; Step < NearTries + LandRelocationTries; Step++)'
$nearPick = 'const bool bNear = Step < NearTries;'
$probeSel = 'const FVector Probe = bNear ? NodeShuffleRocks::NearProbe(OriginXY, Entry.EntryGuid, Step)'
$rockGate = 'const bool bPrimaryRockRefused = bPrimaryHit && bSteep && !bWater && bRwgLastProbeSolid;'
$commit   = 'Entry.Location = TryLoc; // persist the corrected location'
$margin   = 'constexpr double SunkenMarginCm = 50.0;'
$apiAtTop = 'const double ApiZ = Q.IsUsable() ? Q.SurfaceZ(P.X, P.Y) : NodeShuffleMapMode::RwgNaN();'
$testLine = 'const bool bTest = bApiKnown && DepthCm > NodeShuffleRocks::SunkenMarginCm;'
$verdict  = 'const int32 SunkenVerdict = bAccepted ? RwgRocksSunkenVerdict(Hit, TraceStartZ, Params, RingCm) : 0;'
$refuse   = '    if (bSunkenRefused) { return false; }'
$deferLn  = 'if (bRwgRocksTopDeferred) { bRwgRocksTopDeferred = false; if (OutGroundHit) { *OutGroundHit = FHitResult(); } return false; }'
$pinWins  = '    if (RwgProvisionalPinWins(E)) { return; }'
$censusCall = "        RwgRocksEmitCensus();"
$raise    = 'RaycastGroundAt(Probe, ProbeBaseZ, IgnoreNode, IgnoreMesh, TryLoc, TryRot, bTryWater, false, &bTrySteep,'
$capArm   = 'bRwgRocksCaptureFirst = bTerrainOnly;'
$capTap   = 'if (K == 0 && bRwgRocksCaptureFirst) { bRwgRocksCaptureFirst = false; RwgRocksNoteFirstHit(H); }'
$fmts = @('TEXT("ROCKFIRSTHIT: ', 'TEXT("ROCKSIZE: family=', 'TEXT("ROCKSIZE-MESH: ', 'TEXT("ROCKMOVE: ', 'TEXT("ROCKSUNKEN-CENSUS: ', 'TEXT("ROCKSETTLEDZ: ', 'TEXT("ROCKTOP: ')
$noSettle = @(('RaycastSettle' + '('), ('RwgResolveAtSpawn' + '('), ('bRay' + 'Casted'), ('bPin' + 'ned'), ('TryRedeal' + 'WaterLockedEntry'))
$directCalls = @(('GetMaxNumMessagesInHistory' + '()'), ('GetMessageVisibleDuration' + '()'))

function Invoke-Checks($T) {
    $f = @()
    $settle = Span $T.sub 'bool ANodeShuffleSubsystem::RaycastSettle(' 'bool ANodeShuffleSubsystem::TryRedealWaterLockedEntry('
    $ground = Span $T.sub 'bool ANodeShuffleSubsystem::RaycastGroundAt(' 'bool ANodeShuffleSubsystem::RaycastSettle('
    # K1
    $hdrTags = @([regex]::Matches($T.mark, 'TEXT\("(20\d\d-\d\d-\d\d-[\w-]+)"\),') | ForEach-Object { $_.Groups[1].Value })
    if ($hdrTags.Count -eq 0 -or $hdrTags[-1] -ne $marker) { $f += '(K1) marker must be the newest entry in the markers header' }
    if ((Count $T.nsmod ('packet marker (' + $marker + ')=====')) -ne 1) { $f += '(K1) marker once on its own line in NodeShuffle.cpp' }
    foreach ($k in @('rocks', 'rockh', 'probe')) { if ((Count $T[$k] $marker) -ne 0) { $f += "(K1) no marker inside the new file ($k)" } }
    if (-not $T.lmcp.Contains("`$own = '2026-11-14-' + 'rocks-1'")) { $f += '(K1) check_mcpcontract C4 must name this marker as the newest' }
    # K2
    if ($settle -eq '' -or -not (Ordered $settle @($rockGate, $nearGate, $loopHead, $nearPick, $probeSel))) { $f += '(K2) the near search is gated on a rock-refused primary and its steps run first in the shared loop' }
    if ((Count $T.rockh 'constexpr int32 NearProbeCount = 12;') -ne 1 -or (Count $T.rockh 'constexpr float NearRing1Cm = 1000.0f;') -ne 1 -or (Count $T.rockh 'constexpr float NearRing2Cm = 2000.0f;') -ne 1) { $f += '(K2) near pattern constants (12 points, 10 m and 20 m)' }
    # K3
    if ($settle -eq '' -or (Count $settle ('RaycastGround' + 'At(')) -ne 3 -or (Count $settle $commit) -ne 1 -or (Count $settle 'Entry.bRwgOnSolidTop = bRwgLastProbeTopAccepted;') -ne 2) { $f += '(K3) near and spiral share one RaycastGroundAt call and one commit block' }
    # K4
    if ((Count $T.rockh $margin) -ne 1) { $f += '(K4) rule B margin must be 50 cm' }
    if ((Count $T.rocks $apiAtTop) -ne 1 -or (Count $T.rocks $testLine) -ne 1) { $f += '(K4) rule B compares the top with the API Z at the TOP XY, beyond the margin' }
    if ((Count $T.cliff $verdict) -ne 1 -or -not (Ordered $T.cliff @($verdict, '    if (!bAccepted) { return false; }', $refuse, ('bRwgLastProbeTopAccepted = ' + 'true;')))) { $f += '(K4) the verdict is taken on an accepted top and a refusal returns before the accept signal' }
    if ($ground -eq '' -or (Count $ground $deferLn) -ne 1 -or -not (Ordered $ground @('    if (bPrimarySolid)', $deferLn, 'bRwgLastProbeSolid = true;'))) { $f += '(K4) a terrain-absent verdict reads as no hit (defer) in RaycastGroundAt' }
    # K5
    foreach ($k in @('rocks', 'rockh', 'probe')) { foreach ($n in $noSettle) { if ((Count $T[$k] $n) -ne 0) { $f += "(K5) the new file must not settle, redeal or read settle/pin state ($k)" } } }
    if (-not (Ordered $T.prov @($pinWins, 'bool bSettled = RwgResolveAtSpawn(E, bApiUsed, bWaterNoLand);'))) { $f += '(K5) the pin/occupied check returns before the provisional settle' }
    if (-not (Ordered $T.sub @('    if (!Entry.bRayCasted)', 'RwgResolveAtSpawn(Entry, bRwgApiUsed, bWaterNoLand)'))) { $f += '(K5) EnsureNewNodeSpawned settles only an unsettled entry' }
    # K6
    foreach ($fm in $fmts) { if ((Count $T.rocks $fm) -ne 1) { $f += "(K6) census or sample format missing or duplicated: $fm" } }
    if ((Count $T.sub 'TEXT("ROCKNEAR: ') -ne 1) { $f += '(K6) the ROCKNEAR sample line' }
    if (-not (Ordered $T.spass @($censusCall, '        RwgEmitDzCensus();'))) { $f += '(K6) the census call precedes RwgEmitDzCensus in the PLACECENSUS block' }
    if ((Count $T.prov 'origin=%s firstHit=%s sideways=%s sidewaysM=%.1f"),') -ne 1) { $f += '(K6) PROVSETTLE carries firstHit / sideways / sidewaysM' }
    if ((Count $T.place 'RwgRocksRecordSettle(') -ne 2) { $f += '(K6) two settle records in RwgResolveAtSpawn (first hit, completed)' }
    # K7
    foreach ($m in [regex]::Matches($T.rocks, '\(([a-z][^()"]*)\)"\)')) { if ($m.Groups[1].Value.Contains('=')) { $f += '(K7) a prose legend contains field=value syntax' } }
    # K8
    if ((Count $T.nsmod 'TEXT("NodeShuffle.ChatProbe"),') -ne 1 -or (Count $T.desc 'TEXT("NodeShuffle.ChatProbe"), TEXT("write"), false, TEXT("none"),') -ne 1) { $f += '(K8) ChatProbe registered and described' }
    foreach ($v in 1..6) { if ((Count $T.probe ("NodeShuffle chat probe $v of 6: ")) -ne 1) { $f += "(K8) variant $v must name itself once" } }
    if ((Count $T.probe ('BroadcastChat' + 'Message(')) -ne 1 -or (Count $T.probe 'FindFunction(FName(TEXT("GetMaxNumMessagesInHistory")))') -ne 1) { $f += '(K8) one broadcast; limits through reflection' }
    foreach ($n in $directCalls) { foreach ($k in $text.Keys) { if ($k -notlike 'l*' -and (Count $T[$k] $n) -ne 0) { $f += "(K8) a direct chat-limit call adds an import ($k)" } } }
    # K9
    if ($settle -eq '' -or -not (Ordered $settle @($capArm, 'const bool bPrimaryHit = RaycastGroundAt(', 'bRwgRocksCaptureFirst = false;', $rockGate))) { $f += '(K9) first-hit capture armed for the primary only and cleared after it' }
    if ($ground -eq '' -or (Count $ground $capTap) -ne 1) { $f += '(K9) the capture tap in TraceDown' }
    # K10
    if ($settle -eq '' -or (Count $settle $raise) -ne 1) { $f += '(K10) spiral/near probes start from ProbeBaseZ (API Z at the probe XY when higher)' }
    # K11
    $blk = Span $T.subh '// ---- ns-rocks-1 (2026-11-14)' '    void ChatProbeStep();'
    if ($blk -eq '' -or (Count $blk ('UPROP' + 'ERTY')) -ne 0) { $f += '(K11) the ns-rocks-1 members are plain, never saved' }
    # K12
    foreach ($k in @('rocks', 'probe')) { if (($T[$k] -split "`n").Count -ge 500) { $f += "(K12) $k must stay under 500 lines" } }
    return ,$f
}

$fails = Invoke-Checks $text
foreach ($x in $fails) { Write-Host "FAIL$x" }

$mutants = @(
    @{ Name = 'M1 spiral before near';           Key = 'sub';   Expect = 'K2';  Find = $nearPick; Repl = 'const bool bNear = Step >= LandRelocationTries;' }
    @{ Name = 'M2 near ungated by rock';         Key = 'sub';   Expect = 'K2';  Find = $nearGate; Repl = 'const int32 NearTries = NodeShuffleRocks::NearProbeCount;' }
    @{ Name = 'M3 separate near acceptance';     Key = 'sub';   Expect = 'K3';  Find = '        if (NearTries > 0) { ++RwgRkNearRun; }'; Repl = ('        if (NearTries > 0) { ++RwgRkNearRun; } // RaycastGround' + 'At(OriginXY') }
    @{ Name = 'M4 margin widened';               Key = 'rockh'; Expect = 'K4';  Find = $margin; Repl = 'constexpr double SunkenMarginCm = 300.0;' }
    @{ Name = 'M5 API at the dealt XY';          Key = 'rocks'; Expect = 'K4';  Find = 'Q.SurfaceZ(P.X, P.Y)'; Repl = 'Q.SurfaceZ(Hit.TraceStart.X, Hit.TraceStart.Y)' }
    @{ Name = 'M6 refusal ignored';              Key = 'cliff'; Expect = 'K4';  Find = $refuse; Repl = '   ' }
    @{ Name = 'M7 defer branch removed';         Key = 'sub';   Expect = 'K4';  Find = $deferLn; Repl = '' }
    @{ Name = 'M8 pin check dropped';            Key = 'prov';  Expect = 'K5';  Find = $pinWins; Repl = '   ' }
    @{ Name = 'M9 new file reads settle state';  Key = 'rocks'; Expect = 'K5';  Find = '    UWorld* World = GetWorld();'; Repl = ('    UWorld* World = GetWorld(); // E.bRay' + 'Casted') }
    @{ Name = 'M10 census call removed';         Key = 'spass'; Expect = 'K6';  Find = $censusCall; Repl = '       ' }
    @{ Name = 'M11 sunken census removed';       Key = 'rocks'; Expect = 'K6';  Find = 'TEXT("ROCKSUNKEN-CENSUS: '; Repl = 'TEXT("ROCKSUNKENX: ' }
    @{ Name = 'M12 settled-dz census removed';   Key = 'rocks'; Expect = 'K6';  Find = 'TEXT("ROCKSETTLEDZ: '; Repl = 'TEXT("ROCKDZ: ' }
    @{ Name = 'M13 legend syntax';               Key = 'rocks'; Expect = 'K7';  Find = '(width: largest horizontal'; Repl = '(width=largest horizontal' }
    @{ Name = 'M14 direct limit call';           Key = 'probe'; Expect = 'K8';  Find = '        int32 MaxHistory = -1;'; Repl = ('        int32 MaxHistory = Chat->GetMaxNumMessagesInHistory' + '();') }
    @{ Name = 'M15 capture never cleared';       Key = 'sub';   Expect = 'K9';  Find = "    bRwgRocksCaptureFirst = false;`n"; Repl = "`n" }
    @{ Name = 'M16 start not raised';            Key = 'sub';   Expect = 'K10'; Find = $raise; Repl = $raise.Replace('ProbeBaseZ', 'OriginXY.Z') }
    @{ Name = 'M17 member saved';                Key = 'subh';  Expect = 'K11'; Find = '    int32 RwgRkFirstKind[9] = {};'; Repl = ('    UPROP' + 'ERTY(SaveGame) int32 RwgRkFirstKind[9] = {};') }
    @{ Name = 'M18 marker not newest';           Key = 'mark';  Expect = 'K1';  Find = ('        TEXT("' + $marker + '"),'); Repl = '' }
    @{ Name = 'M19 near pattern changed';        Key = 'rockh'; Expect = 'K2';  Find = 'constexpr float NearRing1Cm = 1000.0f;'; Repl = 'constexpr float NearRing1Cm = 4000.0f;' }
)
$caught = 0
foreach ($m in $mutants) {
    $copy = [ordered]@{}; foreach ($k in $text.Keys) { $copy[$k] = $text[$k] }
    if (-not $copy[$m.Key].Contains($m.Find)) { Write-Host "FAIL(mutant) $($m.Name): anchor not found"; $fails += 'mutant-anchor'; continue }
    $copy[$m.Key] = $copy[$m.Key].Replace($m.Find, $m.Repl)
    $mf = Invoke-Checks $copy
    if (@($mf | Where-Object { $_.StartsWith('(' + $m.Expect + ')') }).Count -gt 0) { $caught++ }
    else { Write-Host "FAIL(mutant) $($m.Name) survived (expected $($m.Expect))"; $fails += 'mutant-survived' }
}

if ($fails.Count -eq 0) { Write-Host "PASS check_rocks_lint: K1-K12 hold; $caught/$($mutants.Count) mutants caught"; exit 0 }
Write-Host "check_rocks_lint: $($fails.Count) failure(s); $caught/$($mutants.Count) mutants caught"
exit 1
