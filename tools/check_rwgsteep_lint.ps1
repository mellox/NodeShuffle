# RWGSTEEP lint (2026-10-19, packet rwg-steep-1): on RWG a spawn settle that ends unplaceable with a STEEP terrain primary hit tries a
# LOCAL redraw ring (<= RwgSteepRingCandidates spots within RwgSteepRingRadiusCm) before the map-wide API redeal; a primary that stopped
# on a SOLID mesh keeps the map-wide redeal; the measured slope is printed. Handoff: _team/test-logs/2026-10-19-nodeshuffle-rwgsteep-handoff.md.
# LOADS and PINS every file the packet touched. Needles naming a forbidden or superseded literal are BUILT BY CONCATENATION.
#   S1  Ring radius: the constant is 150 m, and the only radius formula is radius * sqrt((K + 0.5) / cap) (always < the radius).
#   S2  Candidate cap: the constant is 12 and the ring loop is bounded by it (one loop, one RwgLocalRedraw call site).
#   S3  Fall-through: TryRedealRwgEntry calls the ring once, BEFORE the salted map-wide stream; a ring miss continues to the map-wide
#       draw; RwgSteepTryRing returns the ring's own result; the map-wide span hashes to its affc801 text.
#   S4  Occluded exception: the re-probe's solid flag is copied into bRwgSteepOccluded; an occluded entry never reaches RwgLocalRedraw.
#   S5  Slope print: RWGSTEEP exact format (normalRiseRun from the smoothed normal, apiRiseRun from RwgApiSlopeTooSteep), arity.
#   S6  Censuses: PLACECENSUS ends with steepRing/steepMapwide/steepOccludedMapwide and its specifier count == argument count;
#       RWGSTEEPSUM exact format + arity (13, ringPassedFromOriginZ last); its re-print signature includes guidMismatch; the
#       ringPassedFromOriginZ discriminator re-traces the passing XY from the ORIGIN's API Z and only counts; the per-pass steep
#       counters are zeroed with the other PLACECENSUS counters.
#   S7  Budget: every ring run increments RwgPcSteepRingRuns and the spawn loop charges it against the attempt cap.
#   S8  Scope: armed in one place, cleared at every RwgResolveAtSpawn and read-and-cleared by RwgSteepTryRing; the re-probe hook runs
#       only on the unplaceable branch; the ring stream is separate from the map-wide stream and only READS the map-wide salt
#       (RedealAttempts.Find); RwgSpawnPass clears the arm after every EnsureNewNodeSpawned; a non-finite candidate Z is a land reject.
#   S9  Marker rwg-steep-2 present once, rwg-steep-1 and rwg-deal-1 absent; no marker string in the new file.
# Mutants run in-file. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    steep = 'NodeShuffle\Private\NodeShuffleMapModeSteep.cpp'
    place = 'NodeShuffle\Private\NodeShuffleMapModePlace.cpp'
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
function Spec-Count([string]$fmt) { return ([regex]::Matches(($fmt -replace '%%', ''), '%[-+ #0]*\d*(?:\.\d+)?[a-zA-Z]')).Count }
function Get-Logs([string]$t, [string]$prefix) {
    $out = @(); $needle = 'UE_LOG(LogNodeShuffle, '; $at = 0
    while (($s = $t.IndexOf($needle, $at)) -ge 0) {
        $at = $s + $needle.Length
        $i = $s + 'UE_LOG'.Length; $depth = 0; $inStr = $false; $args = @(); $cur = New-Object System.Text.StringBuilder
        for (; $i -lt $t.Length; $i++) {
            $c = $t[$i]
            if ($inStr) { [void]$cur.Append($c); if ($c -eq '\') { $i++; [void]$cur.Append($t[$i]) } elseif ($c -eq '"') { $inStr = $false }; continue }
            if ($c -eq '"') { $inStr = $true; [void]$cur.Append($c); continue }
            if ($c -eq '(') { $depth++; if ($depth -eq 1) { continue } }
            if ($c -eq ')') { $depth--; if ($depth -eq 0) { $args += $cur.ToString(); break } }
            if ($c -eq ',' -and $depth -eq 1) { $args += $cur.ToString(); [void]$cur.Clear(); continue }
            [void]$cur.Append($c)
        }
        if ($args.Count -lt 3) { continue }
        $fmt = (([regex]::Matches($args[2], 'TEXT\("((?:[^"\\]|\\.)*)"\)')) | ForEach-Object { $_.Groups[1].Value }) -join ''
        if (-not $fmt.StartsWith($prefix)) { continue }
        $out += @{ Level = $args[1].Trim(); Fmt = $fmt; Args = @($args | Select-Object -Skip 3 | ForEach-Object { ($_ -replace '\s+', ' ').Trim() }) }
    }
    return $out
}
function Check-Log($T, [string]$key, [string]$tag, [string]$prefix, [string]$wantFmt, [int]$wantArgs) {
    $f = @(); $logs = @(Get-Logs $T[$key] $prefix)
    if ($logs.Count -ne 1) { return @("($tag) expected 1 statement with format prefix '$prefix', found $($logs.Count)") }
    $l = $logs[0]
    if ($l.Level -ne 'Display') { $f += "($tag) '$prefix' must log at Display" }
    if ($wantFmt -and $l.Fmt -ne $wantFmt) { $f += "($tag) format drifted: '$($l.Fmt)'" }
    if ((Spec-Count $l.Fmt) -ne $l.Args.Count -or ($wantArgs -gt 0 -and $l.Args.Count -ne $wantArgs)) { $f += "($tag) arity: $(Spec-Count $l.Fmt) specifier(s) vs $($l.Args.Count) argument(s) (want $wantArgs) in '$prefix'" }
    return $f
}
# affc801 map-wide redeal span of TryRedealRwgEntry ('// Same salted per-entry stream' .. the RWG-REDEAL argument line), LF.
$MapwideSpanSha = '5c7392d6cbc759301cfad6510a27248bf65a1f4426ca2afac3c6c7bbf12a9765'
$MapwideStart = '    // Same salted per-entry stream as the vanilla redeal; no cave branch off vanilla.'
$MapwideEnd = '*Entry.EntryGuid.ToString(), bOutMoved ? 1 : 0, Salt, Outside, Water, Disagree, Slope, Spacing);'

function Invoke-RwgsteepChecks($T) {
    $f = @(); $s = $T.steep; $p = $T.place
    # S1 radius
    if ((Count $s 'constexpr float RwgSteepRingRadiusCm = 15000.0f;') -ne 1) { $f += '(S1) RwgSteepRingRadiusCm must be pinned at 15000.0f (150 m)' }
    if ((Count $s 'RwgSteepRingRadiusCm * FMath::Sqrt((K + 0.5) / static_cast<double>(RwgSteepRingCandidates))') -ne 1 -or (Count $s 'RwgSteepRingRadiusCm') -ne 5) { $f += '(S1) the ring radius must be RwgSteepRingRadiusCm * sqrt((K + 0.5) / cap), its only use besides the constant, the two comments and the census' }
    # S2 cap
    if ((Count $s 'constexpr int32 RwgSteepRingCandidates = 12;') -ne 1) { $f += '(S2) RwgSteepRingCandidates must be pinned at 12' }
    if ((Count $s 'for (int32 K = 0; K < RwgSteepRingCandidates; ++K)') -ne 1 -or (Count $s 'for (') -ne 3) { $f += '(S2) the ring must be ONE loop bounded by RwgSteepRingCandidates (plus the two spacing loops)' }
    if ((Count ($s + $p) 'RwgLocalRedraw(') -ne 2) { $f += '(S2) RwgLocalRedraw has exactly one call site (RwgSteepTryRing) besides its definition' }
    # S3 fall-through
    $ring = '    if (RwgSteepTryRing(Q, Entry, SpacingCm)) { bOutMoved = true; return true; }'
    if ((Count $p 'RwgSteepTryRing(') -ne 1 -or (Count $p $ring) -ne 1) { $f += '(S3) TryRedealRwgEntry must call the ring once, returning only when it moved the entry' }
    if (-not (Ordered $p @('bool ANodeShuffleSubsystem::TryRedealRwgEntry(', 'if (!Q.IsUsable()) { return false; }', $ring, 'int32& Salt = RedealAttempts.FindOrAdd(Entry.EntryGuid);', 'for (int32 I = 0; I < Tries && !bOutMoved; ++I)'))) { $f += '(S3) the ring must run after the API check and BEFORE the salted map-wide draw, which must follow it' }
    if (-not (Ordered $s @('bool ANodeShuffleSubsystem::RwgSteepTryRing(', 'bMoved = RwgLocalRedraw(', '    return bMoved;', 'bool ANodeShuffleSubsystem::RwgLocalRedraw('))) { $f += '(S3) RwgSteepTryRing must return the ring result (false on a miss -> map-wide)' }
    $a = $p.IndexOf($MapwideStart); $b = if ($a -ge 0) { $p.IndexOf($MapwideEnd, $a) } else { -1 }
    if ($a -lt 0 -or $b -lt 0) { $f += '(S3) the map-wide redeal span anchors are missing' }
    else {
        $blk = $p.Substring($a, $b + $MapwideEnd.Length - $a)
        $sha = [Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($blk))).ToLower()
        if ($sha -ne $MapwideSpanSha) { $f += "(S3) the map-wide redeal span changed (sha $sha) -- it must stay byte-identical to affc801" }
    }
    # S4 occluded exception
    if ((Count $s 'bRwgSteepOccluded = bRwgLastProbeSolid;') -ne 1 -or (Count $s 'bRwgSteepOccluded =') -ne 1) { $f += '(S4) bRwgSteepOccluded must be the re-probe solid flag, assigned once' }
    if (-not (Ordered $s @('&Normal, &Hit,', 'bRwgSteepOccluded = bRwgLastProbeSolid;'))) { $f += '(S4) the solid flag must be read after the re-probe' }
    if (-not (Ordered $s @("    if (bRwgSteepOccluded)`n    {`n        ++RwgPcSteepOccluded;", "    else`n    {`n        ++RwgPcSteepRingRuns;", 'bMoved = RwgLocalRedraw('))) { $f += '(S4) an occluded entry must take the map-wide branch; RwgLocalRedraw only in the else' }
    # S5 slope print
    $f += Check-Log $T 'steep' 'S5' 'RWGSTEEP entry=' 'RWGSTEEP entry=%s res=%s loc=%.0f/%.0f/%.0f hitMesh=%s normalRiseRun=%.2f apiRiseRun=%.2f ringTried=%d ringPassed=%d ringRejLand=%d ringRejSpacing=%d ringRejTrace=%d ringTo=%s outcome=%s' 15
    if ((Count $s 'RwgSteepNormalRiseRun = bRwgSteepOccluded ? -1.0 : RwgSteepRiseRunOf(Normal);') -ne 1 -or (Count $s 'RwgApiSlopeTooSteep(Q, Entry.Location.X, Entry.Location.Y, RwgSteepApiRiseRun);') -ne 1) { $f += '(S5) the printed slopes must be the smoothed-normal rise/run and the API rise/run at the entry' }
    if ((Count $s '*RwgSteepHitMesh, RwgSteepNormalRiseRun, RwgSteepApiRiseRun,') -ne 1) { $f += '(S5) the slope values must be the RWGSTEEP arguments' }
    # S6 censuses
    $pc = @(Get-Logs $p 'PLACECENSUS: pass=')
    if ($pc.Count -ne 1) { $f += "(S6) expected 1 PLACECENSUS pass statement, found $($pc.Count)" }
    else {
        if (-not $pc[0].Fmt.EndsWith(' settledInsideCaveSurfaceEntry=%d steepRing=%d steepMapwide=%d steepOccludedMapwide=%d')) { $f += '(S6) PLACECENSUS must end with steepRing/steepMapwide/steepOccludedMapwide after the existing fields' }
        if ((Spec-Count $pc[0].Fmt) -ne $pc[0].Args.Count -or $pc[0].Args.Count -ne 42) { $f += "(S6) PLACECENSUS arity: $(Spec-Count $pc[0].Fmt) specifier(s) vs $($pc[0].Args.Count) argument(s) (want 42)" }
        if (($pc[0].Args | Select-Object -Last 3) -join ',' -ne 'RwgPcSteepRing,RwgPcSteepMapwide,RwgPcSteepOccluded') { $f += '(S6) the last three PLACECENSUS arguments must be the steep counters in field order' }
    }
    $f += Check-Log $T 'steep' 'S6' 'RWGSTEEPSUM: ' 'RWGSTEEPSUM: pass=%d steepEvents=%d ring=%d mapwide=%d occludedMapwide=%d ringCandidatesTried=%d ringCandidatesPassed=%d reprobeWater=%d reprobeNoHitOrNotSteep=%d guidMismatch=%d ringRadiusM=%.0f ringCap=%d ringPassedFromOriginZ=%d' 13
    if ((Count $s 'const int32 Sig = RwgSteepSessEvents + RwgSteepSessReprobeWater + RwgSteepSessReprobeOther + RwgSteepSessGuidMismatch; if (RwgSteepSessLastPrinted == Sig) { return; } RwgSteepSessLastPrinted = Sig;') -ne 1) { $f += '(S6) the RWGSTEEPSUM re-print signature must include guidMismatch' }
    if ((Count $s 'RaycastGroundAt(C, O.Z, nullptr, nullptr, L0, R0, bW0, false, &bS0, nullptr, nullptr, true) && !bW0 && !bS0) { ++RwgSteepSessPassedFromOriginZ; }') -ne 1 -or -not (Ordered $s @('RaycastGroundAt(C, O.Z, nullptr, nullptr, L0, R0, bW0, false, &bS0, nullptr, nullptr, true) && !bW0 && !bS0) { ++RwgSteepSessPassedFromOriginZ; }', '++OutPassed;'))) { $f += '(S6) the ringPassedFromOriginZ discriminator must re-trace from the origin API Z before the pass is counted' }
    if ((Count $p 'RwgPcSteepRing = RwgPcSteepMapwide = RwgPcSteepOccluded = RwgPcSteepRingRuns = 0;') -ne 1) { $f += '(S6) the per-pass steep counters must be zeroed with the PLACECENSUS counters' }
    if ((Count $p 'RwgSteepEmitSession();') -ne 1 -or -not (Ordered $p @('if ((RwgPcPasses % 12) == 1)', 'RWGHIDECHECK: pass=', 'RwgSteepEmitSession();'))) { $f += '(S6) RWGSTEEPSUM must be emitted from the 12th-pass census block' }
    # S7 budget
    if ((Count $s '++RwgPcSteepRingRuns;') -ne 1) { $f += '(S7) every ring run must count one RwgPcSteepRingRuns' }
    if ((Count $p 'for (; K < N && K + RwgPcSteepRingRuns < NodeShuffleMapMode::RwgMaxSpawnAttemptsPerPass; ++K)') -ne 1 -or (Count $p 'RwgPcBudgetSkipped += Cands.Num() - K;') -ne 1) { $f += '(S7) the spawn loop must charge ring runs against the attempt cap and count unattempted candidates' }
    # S8 scope
    if ((Count ($s + $p) 'bRwgSteepArmed = true;') -ne 1 -or (Count $s 'bRwgSteepArmed = true;') -ne 1) { $f += '(S8) the ring is armed in exactly one place (RwgSteepNoteUnplaceable)' }
    if ((Count $p 'bOutWaterNoLand = false; bRwgSteepArmed = false;') -ne 1) { $f += '(S8) RwgResolveAtSpawn must clear the arm on entry' }
    if (-not (Ordered $s @('if (!bRwgSteepArmed) { return false; }', 'bRwgSteepArmed = false;', 'if (RwgSteepGuid != Entry.EntryGuid)'))) { $f += '(S8) RwgSteepTryRing must read-and-clear the arm and match the entry guid' }
    if ((Count $p 'RwgSteepNoteUnplaceable(') -ne 1 -or (Count $p 'if (!bOutWaterNoLand) { ++RwgPcNoTerrainDefer; } else { RwgSteepNoteUnplaceable(Q, Entry); }') -ne 1) { $f += '(S8) the re-probe hook runs only on the unplaceable branch of the failed settle' }
    if ((Count $s 'const int32* SaltP = RedealAttempts.Find(Entry.EntryGuid);') -ne 1 -or (Count $s ('RedealAttempts.' + 'FindOrAdd')) -ne 0) { $f += '(S8) the ring must read the map-wide salt without writing it (RedealAttempts.Find only)' }
    if ((Count $p 'bRwgSteepArmed = false; // rwg-steep-1: an arm never outlives the attempt that set it (a redeal that returned at !Q.IsUsable() did not consume it)') -ne 1 -or (Count $p 'EnsureNewNodeSpawned(E, bChangedWorld); bRwgSteepArmed = false; }') -ne 1) { $f += '(S8) RwgSpawnPass must clear the arm after each EnsureNewNodeSpawned' }
    if ((Count $s 'if (!FMath::IsFinite(C.Z)) { ++OutRejLand; continue; }') -ne 1 -or -not (Ordered $s @('const FVector C(X, Y, Q.SurfaceZ(X, Y));', 'if (!FMath::IsFinite(C.Z)) { ++OutRejLand; continue; }', 'bool bTooClose = false;'))) { $f += '(S8) a non-finite candidate Z must be a land reject before spacing and the trace' }
    if ((Count $s ('Salt * ' + '7919')) -ne 0 -or (Count $s '^ (SavedSeed * 131) ^ ((SaltP ? *SaltP : 0) * 104729) ^ 0x2C1B3C6D);') -ne 1) { $f += '(S8) the ring must draw from its own stream, never the map-wide one' }
    # S9 marker
    if ((Count $T.nsmod ('LOADED (2026-10-19-rwg-' + 'steep-2)')) -ne 1 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'steep-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'deal-1')) -ne 0) { $f += '(S9) marker must be rwg-steep-2 with rwg-steep-1 and rwg-deal-1 absent' }
    if ($s.Contains('LOADED' + ' (')) { $f += '(S9) no build-marker string in the new file' }
    return $f
}

$fails = Invoke-RwgsteepChecks $text
$mutants = @(
    @{ Name = 'B1 ring radius unbounded'; Key = 'steep'; Expect = 'S1'; Find = 'RwgSteepRingRadiusCm * FMath::Sqrt((K + 0.5) / static_cast<double>(RwgSteepRingCandidates))'; Repl = 'RwgSteepRingRadiusCm * (K + 1)' }
    @{ Name = 'B2 ring radius constant widened'; Key = 'steep'; Expect = 'S1'; Find = 'RwgSteepRingRadiusCm = 15000.0f;'; Repl = 'RwgSteepRingRadiusCm = 150000.0f;' }
    @{ Name = 'B3 candidate cap removed'; Key = 'steep'; Expect = 'S2'; Find = 'for (int32 K = 0; K < RwgSteepRingCandidates; ++K)'; Repl = 'for (int32 K = 0; K < Layout.Num(); ++K)' }
    @{ Name = 'B4 candidate cap raised'; Key = 'steep'; Expect = 'S2'; Find = 'RwgSteepRingCandidates = 12;'; Repl = 'RwgSteepRingCandidates = 64;' }
    @{ Name = 'B5 fall-through removed (ring miss returns)'; Key = 'place'; Expect = 'S3'; Find = '    if (RwgSteepTryRing(Q, Entry, SpacingCm)) { bOutMoved = true; return true; }'; Repl = '    if (bRwgSteepArmed) { bOutMoved = RwgSteepTryRing(Q, Entry, SpacingCm); return true; }' }
    @{ Name = 'B6 ring reports moved on a miss'; Key = 'steep'; Expect = 'S3'; Find = "    return bMoved;`n}"; Repl = "    return true;`n}" }
    @{ Name = 'B7 map-wide draw edited'; Key = 'place'; Expect = 'S3'; Find = 'for (int32 I = 0; I < Tries && !bOutMoved; ++I)'; Repl = 'for (int32 I = 0; I < Tries / 2 && !bOutMoved; ++I)' }
    @{ Name = 'B8 occluded entries ringed'; Key = 'steep'; Expect = 'S4'; Find = "    if (bRwgSteepOccluded)`n    {`n        ++RwgPcSteepOccluded;"; Repl = "    if (false && bRwgSteepOccluded)`n    {`n        ++RwgPcSteepOccluded;" }
    @{ Name = 'B9 occluded flag dropped'; Key = 'steep'; Expect = 'S4'; Find = 'bRwgSteepOccluded = bRwgLastProbeSolid;'; Repl = 'bRwgSteepOccluded = false;' }
    @{ Name = 'B10 slope print dropped'; Key = 'steep'; Expect = 'S5'; Find = 'normalRiseRun=%.2f apiRiseRun=%.2f '; Repl = '' }
    @{ Name = 'B11 slope argument dropped'; Key = 'steep'; Expect = 'S5'; Find = '*RwgSteepHitMesh, RwgSteepNormalRiseRun, RwgSteepApiRiseRun,'; Repl = '*RwgSteepHitMesh, RwgSteepApiRiseRun,' }
    @{ Name = 'B12 slope from the raw hit'; Key = 'steep'; Expect = 'S5'; Find = 'RwgSteepRiseRunOf(Normal);'; Repl = 'RwgSteepRiseRunOf(Hit.ImpactNormal);' }
    @{ Name = 'B13 PLACECENSUS drops a steep argument'; Key = 'place'; Expect = 'S6'; Find = 'RwgPcSteepRing, RwgPcSteepMapwide, RwgPcSteepOccluded);'; Repl = 'RwgPcSteepRing, RwgPcSteepMapwide);' }
    @{ Name = 'B14 PLACECENSUS drops a steep field'; Key = 'place'; Expect = 'S6'; Find = ' steepMapwide=%d steepOccludedMapwide=%d"),'; Repl = ' steepOccludedMapwide=%d"),' }
    @{ Name = 'B15 RWGSTEEPSUM arity'; Key = 'steep'; Expect = 'S6'; Find = 'RwgSteepRingCandidates, RwgSteepSessPassedFromOriginZ);'; Repl = 'RwgSteepRingCandidates);' }
    @{ Name = 'B16 steep counters never zeroed'; Key = 'place'; Expect = 'S6'; Find = 'RwgPcSteepRing = RwgPcSteepMapwide = RwgPcSteepOccluded = RwgPcSteepRingRuns = 0;'; Repl = '' }
    @{ Name = 'B17 ring not charged to the budget'; Key = 'place'; Expect = 'S7'; Find = 'for (; K < N && K + RwgPcSteepRingRuns < NodeShuffleMapMode::RwgMaxSpawnAttemptsPerPass; ++K)'; Repl = 'for (; K < N; ++K)' }
    @{ Name = 'B18 arm never cleared by the consumer'; Key = 'steep'; Expect = 'S8'; Find = "    if (!bRwgSteepArmed) { return false; }`n    bRwgSteepArmed = false;"; Repl = '    if (!bRwgSteepArmed) { return false; }' }
    @{ Name = 'B19 ring shares the map-wide stream'; Key = 'steep'; Expect = 'S8'; Find = '^ (SavedSeed * 131) ^ ((SaltP ? *SaltP : 0) * 104729) ^ 0x2C1B3C6D);'; Repl = ('^ (SavedSeed * 31) ^ ((SaltP ? *SaltP : 0) * ' + '7919));') }
    @{ Name = 'B20 resolver no longer clears the arm'; Key = 'place'; Expect = 'S8'; Find = 'bOutWaterNoLand = false; bRwgSteepArmed = false;'; Repl = 'bOutWaterNoLand = false;' }
    @{ Name = 'B22 ring writes the map-wide salt'; Key = 'steep'; Expect = 'S8'; Find = 'const int32* SaltP = RedealAttempts.Find(Entry.EntryGuid);'; Repl = ('const int32* SaltP = &++RedealAttempts.' + 'FindOrAdd(Entry.EntryGuid);') }
    @{ Name = 'B23 attempt loop keeps a stale arm'; Key = 'place'; Expect = 'S8'; Find = "`n        bRwgSteepArmed = false; // rwg-steep-1: an arm never outlives the attempt that set it (a redeal that returned at !Q.IsUsable() did not consume it)"; Repl = '' }
    @{ Name = 'B24 RWGSTEEPSUM signature ignores guidMismatch'; Key = 'steep'; Expect = 'S6'; Find = ' + RwgSteepSessGuidMismatch; if (RwgSteepSessLastPrinted == Sig)'; Repl = '; if (RwgSteepSessLastPrinted == Sig)' }
    @{ Name = 'B25 non-finite candidate Z traced'; Key = 'steep'; Expect = 'S8'; Find = 'if (!FMath::IsFinite(C.Z)) { ++OutRejLand; continue; }'; Repl = '' }
    @{ Name = 'B26 discriminator traces from the candidate Z'; Key = 'steep'; Expect = 'S6'; Find = 'RaycastGroundAt(C, O.Z, nullptr'; Repl = 'RaycastGroundAt(C, C.Z, nullptr' }
    @{ Name = 'B21 marker reverted'; Key = 'nsmod'; Expect = 'S9'; Find = ('2026-10-19-rwg-' + 'steep-2'); Repl = ('2026-10-19-rwg-' + 'deal-1') }
)
$mutantFails = @(); $caught = 0
foreach ($m in $mutants) {
    $mut = @{}; foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = Invoke-RwgsteepChecks $mut
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ } else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "RWGSTEEP LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: RWGSTEEP lint -- S1..S9 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
