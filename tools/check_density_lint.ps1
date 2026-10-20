# check_density_lint.ps1 -- rwg-density-2 (packet rwg-density-1 + cold-review fixes F1-F7): RWGDENSITY / RWGSTUCK / HERE-DENSITY / HERE-LAND (NodeShuffleMapModeDensity.cpp). Diagnostics only.
# Source-text pins, then an in-file mutation suite (every mutant must be caught by the pin it names).
#   D1  Formats: RWGDENSITY, RWGDENSITY-ROW, RWGDENSITY-SKIP, RWGSTUCK, RWGSTUCK-ENTRY, HERE-DENSITY, HERE-LAND -- one line each, exact head,
#       spec count == arg count == expected; no field repeats (HERE-DENSITY repeats its five fields once per radius by design);
#       every `name=` in a format is followed by a conversion (no legend example in field=value form).
#   D2  Cadence: RWGSTUCK runs from RwgDensityPassHook behind the 12th-pass test, one call site.
#   D3  Same land predicate as the deal: RwgDensityLandSample calls Outside -> RwgRejectV3 at the API surface -> RwgApiSlopeTooSteep
#       (the functions RwgDealOneSpot calls, in its order); the density file never re-implements a term; one sample call site.
#   D4  Stuck threshold: RwgStuckMinFailedAttempts defined once as 3 and referenced by both comparisons; no literal threshold compare.
#   D5  Hooks: Place.cpp snapshot directly before / note directly after the attempt's EnsureNewNodeSpawned; pass hook after
#       RwgSeaRedealPass and directly before RwgEmitPlaceCensus; RollLayout and LogHereCensus hooks behind an Rwg map-mode test.
#   D6  Diagnostics only: the density file writes no layout, spawn, water-lock or PLACECENSUS state.
#   D7  RWGDENSITY load trigger latches once (bRwgDensityEmitted set by every census); the roll trigger clears the stuck tracker.
#   D8  Marker rwg-density-2 present once, rwg-density-1 and rwg-rockcache-2 absent.
#   D9  Grid constants pinned by value (10 cells per side, 8 x 8 land sub-sample).
#   D10 Cold-review fixes F1-F7: HERE-LAND lattice + pawn bits (F1), XY-move reset of the failed-attempt count + legend (F2), the
#       RWGSTUCK-ENTRY parked/dormant tail (F3, in D1), latch masks around the sample (F4), InSquare numerator (F5), apiOutside -1 and the
#       deferredSettle counter (F6), the dated TODO on the sub-sample constant and the NodeShuffle.Here comment (F7).
# Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    den   = 'NodeShuffle\Private\NodeShuffleMapModeDensity.cpp'
    place = 'NodeShuffle\Private\NodeShuffleMapModePlace.cpp'
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    hdr   = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
}
$text = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = (Get-Content -Raw -Path $p) -replace "`r`n", "`n"
}
function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }
function Ordered([string]$t, [string[]]$needles) {
    $at = 0
    foreach ($n in $needles) { $i = $t.IndexOf($n, $at); if ($i -lt 0) { return $false }; $at = $i + $n.Length }
    return $true
}
# Body of `<sig> ... {` up to the matching close brace (first occurrence of $sig).
function Body([string]$t, [string]$sig) {
    $s = $t.IndexOf($sig); if ($s -lt 0) { return '' }
    $o = $t.IndexOf('{', $s); $d = 0
    for ($i = $o; $i -lt $t.Length; $i++) { if ($t[$i] -eq '{') { $d++ } elseif ($t[$i] -eq '}') { $d--; if ($d -eq 0) { return $t.Substring($o, $i - $o + 1) } } }
    return ''
}
function Fmt-Fields([string]$fmt) { return [regex]::Matches($fmt, '(?<![A-Za-z0-9])([A-Za-z][A-Za-z0-9]*)=') | ForEach-Object { $_.Groups[1].Value } }
function Spec-Count([string]$fmt) { return ([regex]::Matches(($fmt -replace '%%', ''), '%[-+ #0]*\d*(?:\.\d+)?[a-zA-Z]')).Count }
function Get-Logs([string]$t, [string]$prefix) {
    $out = @()
    $needle = 'UE_LOG(LogNodeShuffle, '
    $at = 0
    while (($s = $t.IndexOf($needle, $at)) -ge 0) {
        $at = $s + $needle.Length
        $i = $s + 'UE_LOG'.Length; $depth = 0; $inStr = $false; $a = @(); $cur = New-Object System.Text.StringBuilder
        for (; $i -lt $t.Length; $i++) {
            $c = $t[$i]
            if ($inStr) { [void]$cur.Append($c); if ($c -eq '\') { $i++; [void]$cur.Append($t[$i]) } elseif ($c -eq '"') { $inStr = $false }; continue }
            if ($c -eq '"') { $inStr = $true; [void]$cur.Append($c); continue }
            if ($c -eq '(') { $depth++; if ($depth -eq 1) { continue } }
            if ($c -eq ')') { $depth--; if ($depth -eq 0) { $a += $cur.ToString(); break } }
            if ($c -eq ',' -and $depth -eq 1) { $a += $cur.ToString(); [void]$cur.Clear(); continue }
            [void]$cur.Append($c)
        }
        if ($a.Count -lt 3) { continue }
        $fmt = (([regex]::Matches($a[2], 'TEXT\("((?:[^"\\]|\\.)*)"\)')) | ForEach-Object { $_.Groups[1].Value }) -join ''
        if (-not $fmt.StartsWith($prefix)) { continue }
        $out += @{ Fmt = $fmt; Args = @($a | Select-Object -Skip 3) }
    }
    return $out
}

function Invoke-DensityChecks($T) {
    $f = @()
    # D1
    $specs = @(
        @{ P = 'RWGDENSITY: '; N = 22; Tail = ' layoutEntries=%d latchesChangedDuringSample=%d'; Dup = $false; Head = 'RWGDENSITY: trigger=%s totalActive=%d inSquare=%d caveActive=%d gridN=%d cellM=%.0f landSamplesPerCell=%d landSamples=%d samples=%d landWeightedCells=%.2f expectedPerFullLandCell=%.2f landCells50=%d minEntriesLandCells50=%d lowerMedianEntriesLandCells50=%d maxEntriesLandCells50=%d meanExpectedLandCells50=%.2f' }
        @{ P = 'RWGDENSITY-ROW: '; N = 7; Dup = $false; Head = 'RWGDENSITY-ROW: trigger=%s row=%d yMinM=%.0f xMinM=%.0f cellM=%.0f landSamplesPerCell=%d activeSlashLand=%s' }
        @{ P = 'RWGDENSITY-SKIP: '; N = 4; Dup = $false; Head = 'RWGDENSITY-SKIP: trigger=%s apiUsable=%d apiStatus=''%s'' layoutEntries=%d' }
        @{ P = 'RWGSTUCK: '; N = 8; Dup = $false; Head = 'RWGSTUCK: pass=%d threshold=%d attemptRadiusM=%.0f pawns=%d stuckInAttemptRadius=%d stuckTotalMap=%d trackedEntries=%d entryLinesPrinted=%d' }
        @{ P = 'RWGSTUCK-ENTRY: '; N = 20; Tail = ' apiSlopeRiseRun=%.2f parkedThisSession=%d dormantThisSession=%d'; Dup = $false; Head = 'RWGSTUCK-ENTRY: pass=%d entry=%s res=%s loc=%.0f/%.0f/%.0f distM=%.0f failedAttemptsAtLoc=%d lastAttemptCounters=%s settled=%d caveEntry=%d apiUsable=%d apiOutside=%d apiSurfaceZ=%.0f apiWater=%d volumeWater=%d rejectV3Bits=%d apiSlopeRiseRun=%.2f' }
        @{ P = 'HERE-DENSITY: '; N = 13; Dup = $true; Head = 'HERE-DENSITY: within %.0f m: active=%d live=%d deferred=%d stuck=%d noFailedAttempt=%d; within %.0f m: active=%d live=%d deferred=%d stuck=%d noFailedAttempt=%d (' }
        @{ P = 'HERE-LAND: '; N = 10; Dup = $false; Head = 'HERE-LAND: within %.0f m: landPoints=%d diskPoints=%d spacingM=%.0f pawnRejectV3Bits=%d apiUsable=%d seaUsable=%d voidUsable=%d killboxKnown=%d landSampleMs=%.0f' }
    )
    foreach ($s in $specs) {
        $logs = @(Get-Logs $T.den $s.P)
        if ($logs.Count -ne 1) { $f += "(D1) expected exactly one $($s.P.Trim()) line, found $($logs.Count)"; continue }
        $fmt = $logs[0].Fmt; $sc = Spec-Count $fmt; $ac = $logs[0].Args.Count
        if ($sc -ne $s.N -or $ac -ne $s.N) { $f += "(D1) $($s.P.Trim()): specs=$sc args=$ac expected=$($s.N)" }
        if (-not $fmt.StartsWith($s.Head)) { $f += "(D1) $($s.P.Trim()) must start with: $($s.Head)" }
        if ($s.Tail -and -not $fmt.EndsWith($s.Tail)) { $f += "(D1) $($s.P.Trim()) must end with: $($s.Tail)" }
        $fields = @(Fmt-Fields $fmt)
        if (-not $s.Dup -and ($fields | Select-Object -Unique).Count -ne $fields.Count) { $f += "(D1) $($s.P.Trim()) repeats a field" }
        if ([regex]::IsMatch($fmt, '(?<![A-Za-z0-9])[A-Za-z][A-Za-z0-9]*=(?!''?%)')) { $f += "(D1) $($s.P.Trim()) has a name= not followed by a conversion (legend example?)" }
    }
    # D2
    $hook = Body $T.den 'void ANodeShuffleSubsystem::RwgDensityPassHook('
    if ((Count $hook 'if ((RwgPcPasses % 12) == 1) { RwgEmitStuckCensus(Pawns, AttemptRadiusCm); }') -ne 1) { $f += '(D2) RWGSTUCK must run behind the 12th-pass test in RwgDensityPassHook' }
    if ((Count $T.den 'RwgEmitStuckCensus(Pawns, AttemptRadiusCm);') -ne 1 -or (Count $T.place 'RwgEmitStuckCensus(') -ne 0 -or (Count $T.sub 'RwgEmitStuckCensus(') -ne 0) { $f += '(D2) exactly one RwgEmitStuckCensus call site' }
    # D3
    $land = Body $T.den 'bool ANodeShuffleSubsystem::RwgDensityLandSample('
    if (-not (Ordered $land @('if (Q.Outside(X, Y)) { return false; }', 'if (RwgRejectV3(Q, FVector(X, Y, Q.SurfaceZ(X, Y))) != 0) { return false; }', 'return !RwgApiSlopeTooSteep(Q, X, Y, RiseRun);'))) { $f += '(D3) the land sample must call Outside -> RwgRejectV3 -> RwgApiSlopeTooSteep in the deal order' }
    $deal = Body $T.place 'bool ANodeShuffleSubsystem::RwgDealOneSpot('
    if (-not (Ordered $deal @('if (Q.Outside(X, Y))', 'const double Z = Q.SurfaceZ(X, Y);', 'RwgRejectV3(Q, OutSpot)', 'RwgApiSlopeTooSteep(Q, X, Y, RiseRun)'))) { $f += '(D3) RwgDealOneSpot no longer calls the three land functions in that order: re-derive the land sample' }
    foreach ($n in @(('RwgWet' + 'V2('), ('Void' + 'At('), ('InS' + 'ea('), ('WaterLevel' + 'Z('), ('FMath::T' + 'an('), ('RwgIsPointInWater' + 'Cached('))) {
        if ((Count $T.den $n) -ne 0) { $f += "(D3) the density file must not re-implement a deal term: $n" }
    }
    if ((Count $T.den 'RwgDensityLandSample(Q, X, Y)') -ne 1) { $f += '(D3) exactly one land-sample call site (the grid loop)' }
    # D4
    if ((Count $T.den ('constexpr int32 RwgStuckMinFailedAttempts = ' + '3;')) -ne 1) { $f += '(D4) RwgStuckMinFailedAttempts must be defined once as 3' }
    if ((Count $T.den 'if (!T || T->Key < RwgStuckMinFailedAttempts) { continue; }') -ne 1) { $f += '(D4) RWGSTUCK must compare against RwgStuckMinFailedAttempts' }
    if ((Count $T.den 'if (T->Key >= RwgStuckMinFailedAttempts) { ++St[R]; }') -ne 1) { $f += '(D4) HERE-DENSITY stuck must compare against RwgStuckMinFailedAttempts' }
    if ([regex]::IsMatch($T.den, 'Key\s*(<|>=|>|<=)\s*[2-9](?![0-9])')) { $f += '(D4) no literal threshold compare on a failed-attempt count' }
    # D5
    $spawn = Body $T.place 'void ANodeShuffleSubsystem::RwgSpawnPass('
    if ((Count $spawn "        RwgStuckBeforeAttempt(E); // rwg-density-1: RWGSTUCK snapshot (diagnostics only, NodeShuffleMapModeDensity.cpp)`n        EnsureNewNodeSpawned(E, bChangedWorld);`n        AFGResourceNode* const* Now = SpawnedNodes.Find(E.EntryGuid);`n        if (Now && IsValid(*Now)) { ++RwgPcSpawned; }`n        RwgStuckAfterAttempt(E);") -ne 1) { $f += '(D5) the snapshot/note must bracket the attempt loop EnsureNewNodeSpawned' }
    if ((Count $T.place 'RwgStuckBeforeAttempt(') -ne 1 -or (Count $T.place 'RwgStuckAfterAttempt(') -ne 1) { $f += '(D5) one snapshot and one note call site' }
    if ((Count $spawn "RwgDensityPassHook(Pawns, AttemptRadiusCm); // rwg-density-1: RWGDENSITY on load (first pass, after RwgSeaRedealPass) + RWGSTUCK every 12th pass`n    RwgEmitPlaceCensus();") -ne 1) { $f += '(D5) the pass hook must run directly before RwgEmitPlaceCensus' }
    if (-not (Ordered $spawn @('RwgSeaRedealPass();', 'RwgKillboxResettlePass();', 'RwgDensityPassHook('))) { $f += '(D5) the pass hook must run after RwgSeaRedealPass and the pass work' }
    if ((Count $T.sub "                   VanillaResourceCounts);`n    if (GetMapMode() == ENodeShuffleMapMode::Rwg) { RwgDensityOnRoll(); }") -ne 1 -or (Count $T.sub 'RwgDensityOnRoll(') -ne 1) { $f += '(D5) the roll trigger must follow EmitRollCensus behind an Rwg test, once' }
    if ((Count $T.sub 'if (GetMapMode() == ENodeShuffleMapMode::Rwg) { LogHereDensity(P, static_cast<float>(CensusRadiusCm)); }') -ne 1 -or (Count $T.sub 'LogHereDensity(') -ne 1) { $f += '(D5) HERE-DENSITY must be behind an Rwg test, once' }
    # D6
    foreach ($n in @(('EnsureNewNode' + 'Spawned('), ('TryRedeal'), ('.Location ' + '='), ('bActive ' + '='), ('bRayCasted ' + '='), ('SpawnedNodes.' + 'Add('), ('SpawnedNodes.' + 'Remove('), ('WaterLockedThisSession.' + 'Add('), ('Layout.' + 'Add'), ('Layout.' + 'Remove'))) {
        if ((Count $T.den $n) -ne 0) { $f += "(D6) the density file must not write layout/spawn state: $n" }
    }
    if ([regex]::IsMatch($T.den, '(\+\+|--)\s*RwgPc|RwgPc[A-Za-z]+\s*(\+\+|--|[-+]?=(?!=))')) { $f += '(D6) the density file must not write a PLACECENSUS counter' }
    # D7
    if ((Count $T.den 'if (!bRwgDensityEmitted) { RwgEmitDensityCensus(TEXT("load")); }') -ne 1) { $f += '(D7) the load trigger must be latched by bRwgDensityEmitted' }
    $emit = Body $T.den 'void ANodeShuffleSubsystem::RwgEmitDensityCensus('
    if (-not $emit.StartsWith("{`n    bRwgDensityEmitted = true;")) { $f += '(D7) every density census must set the latch first' }
    $roll = Body $T.den 'void ANodeShuffleSubsystem::RwgDensityOnRoll('
    if (-not (Ordered $roll @('RwgStuckTracker.Reset();', 'RwgEmitDensityCensus(TEXT("roll"));'))) { $f += '(D7) the roll trigger must clear the stuck tracker then print' }
    # D8
    if ((Count $T.nsmod ('LOADED (2026-10-19-rwg-' + 'steep-1)')) -ne 1 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'deal-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'density-2)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'density-1')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'rockcache-2')) -ne 0) { $f += '(D8) marker must be rwg-density-2 with rwg-density-1 and rwg-rockcache-2 absent' }
    # D9
    if ((Count $T.den ('constexpr int32 RwgDensityGridN = ' + '10;')) -ne 1 -or (Count $T.den ('constexpr int32 RwgDensitySubSample = ' + '8;')) -ne 1) { $f += '(D9) grid constants must be 10 cells per side and an 8 x 8 sub-sample' }
    # D10
    $here = Body $T.den 'void ANodeShuffleSubsystem::LogHereDensity('
    foreach ($n in @('constexpr double Step = 10000.0;', 'const double R = Radii[0];', 'for (double Dy = -R + 0.5 * Step; Dy < R; Dy += Step)', 'for (double Dx = -R + 0.5 * Step; Dx < R; Dx += Step)', 'if (Dx * Dx + Dy * Dy >= R * R) { continue; }', 'if (Self->RwgDensityLandSample(Q, P.X + Dx, P.Y + Dy)) { ++LandPts; }', 'if (!Q.Outside(P.X, P.Y)) { PawnBits = Self->RwgRejectV3(Q, FVector(P.X, P.Y, Q.SurfaceZ(P.X, P.Y))); }')) {
        if ((Count $here $n) -ne 1) { $f += "(D10) F1 HERE-LAND: LogHereDensity must contain exactly once: $n" }
    }
    $before = Body $T.den 'void ANodeShuffleSubsystem::RwgStuckBeforeAttempt('
    if ((Count $before 'RwgStuckSnapLoc = E.Location;') -ne 1) { $f += '(D10) F2 the pre-attempt snapshot must record the entry location' }
    $after = Body $T.den 'void ANodeShuffleSubsystem::RwgStuckAfterAttempt('
    if ((Count $after 'if (FVector::DistSquared2D(RwgStuckSnapLoc, E.Location) > 1.0) { T.Key = 0; } else { ++T.Key; }') -ne 1) { $f += '(D10) F2 the failed-attempt count must reset when the attempt moved the entry in XY' }
    if ((Count $T.hdr 'FVector RwgStuckSnapLoc = FVector::ZeroVector;') -ne 1) { $f += '(D10) F2 header must declare RwgStuckSnapLoc once' }
    if ((Count $T.den 'with at least one failed spawn attempt since they were last live or last moved in XY') -ne 1) { $f += '(D10) F2 the HERE-DENSITY legend must define deferred as since last live or last moved in XY' }
    $latch = 'const int32 LatchMask{0} = (bRwgSeaUsable ? 1 : 0) | (bRwgVoidUsable ? 2 : 0) | (bRwgKillboxKnown ? 4 : 0);'
    $emit2 = Body $T.den 'void ANodeShuffleSubsystem::RwgEmitDensityCensus('
    if (-not (Ordered $emit2 @(($latch -f 'Before'), 'const double T0 = FPlatformTime::Seconds();', 'const double LandMs = (FPlatformTime::Seconds() - T0) * 1000.0;', ($latch -f 'After')))) { $f += '(D10) F4 the latch mask must be taken before the sample loop and again right after LandMs' }
    if ((Count $T.den 'LatchMaskBefore != LatchMaskAfter ? 1 : 0);') -ne 1) { $f += '(D10) F4 latchesChangedDuringSample must print LatchMaskBefore != LatchMaskAfter' }
    if ((Count $T.den 'InSquare / LandWeighted : 0.0;') -ne 1 -or (Count $T.den ('Total' + 'Active / LandWeighted')) -ne 0) { $f += '(D10) F5 expectedPerFullLandCell must divide InSquare' }
    if ((Count $T.den 'bQ ? (bOut ? 1 : 0) : -1, ApiZ,') -ne 1) { $f += '(D10) F6 apiOutside must print -1 when the API is unusable' }
    if ((Count $T.den 'case 23: return DeferredThisPass;') -ne 1 -or (Count $T.den 'TEXT("deferredSettle") };') -ne 1 -or (Count $T.den 'RwgStuckCounterN == 24') -ne 1) { $f += '(D10) F6 deferredSettle must be counter 23 (name, case, static_assert 24)' }
    if (-not [regex]::IsMatch($T.den, 'constexpr int32 RwgDensitySubSample = 8;[^\n]*TODO\(2026-10-19 rwg-density-1\)')) { $f += '(D10) F7 the sub-sample constant must carry its dated TODO' }
    if ((Count $T.nsmod "side-effect-free (except RwgRejectV3's latch grading if it is still pending)") -ne 1) { $f += '(D10) F1 the NodeShuffle.Here comment must name the RwgRejectV3 latch grading' }
    return $f
}

$fails = @(Invoke-DensityChecks $text)
Write-Host ("CONTROL (unmutated tree): {0}" -f $(if ($fails.Count -eq 0) { 'PASS' } else { "FAILING with $($fails.Count)" }))

$mutants = @(
    @{ Name = 'Y1 RWGDENSITY loses an argument'; Key = 'den'; Expect = 'D1'; Find = 'LandMs, Layout.Num(), LatchMaskBefore != LatchMaskAfter ? 1 : 0);'; Repl = 'LandMs);' }
    @{ Name = 'Y2 RWGSTUCK loses stuckTotalMap'; Key = 'den'; Expect = 'D1'; Find = 'stuckInAttemptRadius=%d stuckTotalMap=%d '; Repl = 'stuckInAttemptRadius=%d ' }
    @{ Name = 'Y3 RWGSTUCK-ENTRY loses the API water verdict'; Key = 'den'; Expect = 'D1'; Find = 'apiSurfaceZ=%.0f apiWater=%d '; Repl = 'apiSurfaceZ=%.0f ' }
    @{ Name = 'Y4 HERE-DENSITY legend gains a field example'; Key = 'den'; Expect = 'D1'; Find = 'stuck needs %d such passes'; Repl = ('stuck needs ' + 'failedAttempts=3') }
    @{ Name = 'Y5 RWGDENSITY-ROW drops its pairs'; Key = 'den'; Expect = 'D1'; Find = ' landSamplesPerCell=%d activeSlashLand=%s"),'; Repl = ' landSamplesPerCell=%d"),' }
    @{ Name = 'Y6 stuck cadence becomes every 6th pass'; Key = 'den'; Expect = 'D2'; Find = 'if ((RwgPcPasses % 12) == 1) { RwgEmitStuckCensus('; Repl = 'if ((RwgPcPasses % 6) == 1) { RwgEmitStuckCensus(' }
    @{ Name = 'Y7 land sample uses the water-only predicate'; Key = 'den'; Expect = 'D3'; Find = 'if (RwgRejectV3(Q, FVector(X, Y, Q.SurfaceZ(X, Y))) != 0) { return false; }'; Repl = ('if (RwgWet' + 'V2(Q, FVector(X, Y, Q.SurfaceZ(X, Y))) != 0) { return false; }') }
    @{ Name = 'Y8 land sample drops the slope term'; Key = 'den'; Expect = 'D3'; Find = 'return !RwgApiSlopeTooSteep(Q, X, Y, RiseRun);'; Repl = 'return true;' }
    @{ Name = 'Y9 land sample re-implements the void term'; Key = 'den'; Expect = 'D3'; Find = '    if (Q.Outside(X, Y)) { return false; }'; Repl = ('    if (Q.Outside(X, Y) || Q.Void' + 'At(X, Y)) { return false; }') }
    @{ Name = 'Y10 deal order changed (slope before v3)'; Key = 'place'; Expect = 'D3'; Find = '    const int32 Wet = RwgRejectV3(Q, OutSpot);'; Repl = '    const int32 Wet = 0;' }
    @{ Name = 'Y11 stuck threshold inlined as a literal'; Key = 'den'; Expect = 'D4'; Find = 'if (!T || T->Key < RwgStuckMinFailedAttempts) { continue; }'; Repl = ('if (!T || T->Key < ' + '3) { continue; }') }
    @{ Name = 'Y12 stuck threshold changed to 2'; Key = 'den'; Expect = 'D4'; Find = ('constexpr int32 RwgStuckMinFailedAttempts = ' + '3;'); Repl = ('constexpr int32 RwgStuckMinFailedAttempts = ' + '2;') }
    @{ Name = 'Y13 snapshot moved after the attempt'; Key = 'place'; Expect = 'D5'; Find = "        RwgStuckBeforeAttempt(E); // rwg-density-1: RWGSTUCK snapshot (diagnostics only, NodeShuffleMapModeDensity.cpp)`n        EnsureNewNodeSpawned(E, bChangedWorld);"; Repl = "        EnsureNewNodeSpawned(E, bChangedWorld);`n        RwgStuckBeforeAttempt(E); // rwg-density-1: RWGSTUCK snapshot (diagnostics only, NodeShuffleMapModeDensity.cpp)" }
    @{ Name = 'Y14 roll trigger runs on vanilla'; Key = 'sub'; Expect = 'D5'; Find = 'if (GetMapMode() == ENodeShuffleMapMode::Rwg) { RwgDensityOnRoll(); }'; Repl = 'if (true) { RwgDensityOnRoll(); }' }
    @{ Name = 'Y15 HERE-DENSITY printed on vanilla'; Key = 'sub'; Expect = 'D5'; Find = 'if (GetMapMode() == ENodeShuffleMapMode::Rwg) { LogHereDensity('; Repl = 'if (true) { LogHereDensity(' }
    @{ Name = 'Y16 diagnostic writes a PLACECENSUS counter'; Key = 'den'; Expect = 'D6'; Find = '    if (Now && IsValid(*Now)) { RwgStuckTracker.Remove(E.EntryGuid); return; }'; Repl = '    if (Now && IsValid(*Now)) { ++RwgPcSpawned; RwgStuckTracker.Remove(E.EntryGuid); return; }' }
    @{ Name = 'Y17 diagnostic redeals an entry'; Key = 'den'; Expect = 'D6'; Find = '        ++StuckMap;'; Repl = ('        ++StuckMap; TryRedeal' + 'WaterLockedEntry(Layout[I]);') }
    @{ Name = 'Y18 load latch dropped (prints every pass)'; Key = 'den'; Expect = 'D7'; Find = "{`n    bRwgDensityEmitted = true;`n"; Repl = "{`n" }
    @{ Name = 'Y19 roll keeps the old tracker'; Key = 'den'; Expect = 'D7'; Find = '    RwgStuckTracker.Reset();'; Repl = '' }
    @{ Name = 'Y20 marker reverted'; Key = 'nsmod'; Expect = 'D8'; Find = ('2026-10-19-rwg-' + 'steep-1'); Repl = ('2026-10-19-rwg-' + 'density-1') }
    @{ Name = 'Y21 sub-sample shrunk to 4'; Key = 'den'; Expect = 'D9'; Find = ('constexpr int32 RwgDensitySubSample = ' + '8;'); Repl = ('constexpr int32 RwgDensitySubSample = ' + '4;') }
    @{ Name = 'Y22 HERE-LAND loses an argument'; Key = 'den'; Expect = 'D1'; Find = 'bRwgKillboxKnown ? 1 : 0, (FPlatformTime::Seconds() - T0) * 1000.0);'; Repl = 'bRwgKillboxKnown ? 1 : 0);' }
    @{ Name = 'Y23 HERE-LAND lattice spacing doubled'; Key = 'den'; Expect = 'D10'; Find = 'constexpr double Step = 10000.0;'; Repl = 'constexpr double Step = 20000.0;' }
    @{ Name = 'Y24 HERE-LAND samples the 300 m radius'; Key = 'den'; Expect = 'D10'; Find = 'const double R = Radii[0];'; Repl = 'const double R = Radii[1];' }
    @{ Name = 'Y25 HERE-LAND pawn bits through a re-implemented term'; Key = 'den'; Expect = 'D10'; Find = 'PawnBits = Self->RwgRejectV3(Q, FVector(P.X, P.Y, Q.SurfaceZ(P.X, P.Y)));'; Repl = ('PawnBits = Self->RwgWet' + 'V2(Q, FVector(P.X, P.Y, Q.SurfaceZ(P.X, P.Y)));') }
    @{ Name = 'Y26 HERE-LAND lattice no longer asks the deal land test'; Key = 'den'; Expect = 'D10'; Find = 'if (Self->RwgDensityLandSample(Q, P.X + Dx, P.Y + Dy)) { ++LandPts; }'; Repl = '{ ++LandPts; }' }
    @{ Name = 'Y27 RWGSTUCK-ENTRY loses the parked/dormant tail'; Key = 'den'; Expect = 'D1'; Find = ' parkedThisSession=%d dormantThisSession=%d"),'; Repl = '"),' }
    @{ Name = 'Y28 RWGSTUCK-ENTRY loses the dormant argument'; Key = 'den'; Expect = 'D1'; Find = ', DormantThisSession.Contains(E.EntryGuid) ? 1 : 0);'; Repl = ');' }
    @{ Name = 'Y29 failedAttemptsAtLoc rename reverted'; Key = 'den'; Expect = 'D1'; Find = 'failedAttemptsAtLoc=%d'; Repl = 'failedAttempts=%d' }
    @{ Name = 'Y30 stuck count not reset on an XY move'; Key = 'den'; Expect = 'D10'; Find = 'if (FVector::DistSquared2D(RwgStuckSnapLoc, E.Location) > 1.0) { T.Key = 0; } else { ++T.Key; }'; Repl = '++T.Key;' }
    @{ Name = 'Y31 snapshot no longer records the location'; Key = 'den'; Expect = 'D10'; Find = "    RwgStuckSnapLoc = E.Location;`n"; Repl = '' }
    @{ Name = 'Y32 HERE-DENSITY legend reverted'; Key = 'den'; Expect = 'D10'; Find = 'with at least one failed spawn attempt since they were last live or last moved in XY'; Repl = 'whose tracked spawn attempts since last live all failed' }
    @{ Name = 'Y33 RWGDENSITY loses latchesChangedDuringSample'; Key = 'den'; Expect = 'D1'; Find = ' layoutEntries=%d latchesChangedDuringSample=%d"),'; Repl = ' layoutEntries=%d"),' }
    @{ Name = 'Y34 latchesChangedDuringSample hard-coded to 0'; Key = 'den'; Expect = 'D10'; Find = 'LatchMaskBefore != LatchMaskAfter ? 1 : 0);'; Repl = '0);' }
    @{ Name = 'Y35 latch mask after the sample is the mask before'; Key = 'den'; Expect = 'D10'; Find = 'const int32 LatchMaskAfter = (bRwgSeaUsable ? 1 : 0) | (bRwgVoidUsable ? 2 : 0) | (bRwgKillboxKnown ? 4 : 0);'; Repl = 'const int32 LatchMaskAfter = LatchMaskBefore;' }
    @{ Name = 'Y36 expected-per-cell numerator back to TotalActive'; Key = 'den'; Expect = 'D10'; Find = 'InSquare / LandWeighted : 0.0;'; Repl = ('Total' + 'Active / LandWeighted : 0.0;') }
    @{ Name = 'Y37 apiOutside reads 0 when the API is unusable'; Key = 'den'; Expect = 'D10'; Find = 'bQ ? (bOut ? 1 : 0) : -1, ApiZ,'; Repl = 'bOut ? 1 : 0, ApiZ,' }
    @{ Name = 'Y38 deferredSettle counter case dropped'; Key = 'den'; Expect = 'D10'; Find = "    case 23: return DeferredThisPass;`n"; Repl = '' }
    @{ Name = 'Y39 sub-sample TODO removed'; Key = 'den'; Expect = 'D10'; Find = '// TODO(2026-10-19 rwg-density-1): cost unmeasured'; Repl = '// cost unmeasured' }
    @{ Name = 'Y40 NodeShuffle.Here comment reverted'; Key = 'nsmod'; Expect = 'D10'; Find = "side-effect-free (except RwgRejectV3's latch grading if it is still pending),"; Repl = 'side-effect-free,' }
)
$caught = 0
$mutantFails = @()
foreach ($m in $mutants) {
    $mut = @{}; foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = Invoke-DensityChecks $mut
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ }
    else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "DENSITY LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: DENSITY lint -- D1..D10 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
