# rwg-trace-diag-1 lint (2026-10-27): RWGTRACEMISS (per rate-limited failed first spawn trace on RWG) + RWGTRACECENSUS (per placement pass).
# DIAGNOSTICS ONLY: the packet may change NO decision, order, count or retry behaviour. LOADS every file it touched (Place.cpp, the two new
# diag files) plus every file whose decision code it reads around (Subsystem.cpp, Density.cpp, HeightApi.cpp) and NodeShuffle.cpp (marker).
# Needles naming a forbidden literal are BUILT BY CONCATENATION so this file cannot match itself.
#   D1-D5  Decision code byte-identical to HEAD 65ab5b4: five spans (anchored on unique statements) are LF-normalised, the packet's hook
#          lines are dropped by a STRICT whole-line regex (a hook call alone on its line, no `;` inside its arguments, then the packet's
#          comment), and the SHA-256 of the rest must equal the constant computed from 65ab5b4 (reproduce: -PrintHashesAt 65ab5b4). The
#          dropped-line count per span is pinned, so a decision edit hidden on a hook line cannot pass.
#          D1 RwgResolveAtSpawn's settle + census + defer block; D2 RaycastGroundAt + RaycastSettle; D3 RwgSpawnPass + the PLACECENSUS
#          reset; D4 EnsureNewNodeSpawned's settle / redeal / defer block; D5 the RWGSTUCK tracker (RwgStuckCounter .. RwgStuckAfterAttempt).
#   T1  The schedule: one constant pair (1, 12, 48, then every 96) and the one predicate that reads it.
#   T2  Both tags exist exactly once, as the first TEXT literal of a Display UE_LOG; never Warning/Error/Log/Verbose; nowhere else.
#   T3  Formats: no repeated field; every field carries exactly one specifier; the UE_LOG argument count equals the specifier count.
#   T4  Hooks: each of the three is called exactly once, at its pinned neighbour (after `bRwgProbeArmed = false;`, before the attempt
#       radius, before RwgEmitPlaceCensus), with the pinned arguments.
#   T5  Budget: traces-per-event x events-per-pass <= 32, static_assert present, and every probe call sits inside the budget branch.
#   T6  No format names a cause.
#   T7  The diag file never calls the settle helpers, never adds an ignored actor/component and never includes the subsystem header.
#   T8  Mirrors: the decision window / channel / complex flag in Subsystem.cpp and the diag file's mirrored constants agree.
#   T9  Wide window = the RWGHEIGHTAPI plausibility bounds, still present in HeightApi.cpp.
#   V5  Marker rwg-0-1-12-2 present once; the older markers absent (same block as check_rwg019_lint.ps1); no marker string in the diag files.
# Mutants run in-file. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.
param([string]$PrintHashesAt = '')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    place = 'NodeShuffle\Private\NodeShuffleMapModePlace.cpp'
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    dens  = 'NodeShuffle\Private\NodeShuffleMapModeDensity.cpp'
    diag  = 'NodeShuffle\Private\NodeShuffleRwgTraceDiag.cpp'
    diagh = 'NodeShuffle\Private\NodeShuffleRwgTraceDiag.h'
    hapi  = 'NodeShuffle\Private\NodeShuffleMapModeHeightApi.cpp'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
}
$hookRe = '(?m)^[ \t]*' + 'NodeShuffleRwg' + 'TraceDiag::(BeginPass|NoteFirstTrace|EmitPassCensus)\([^;\n]*\);[ \t]*//[ \t]*rwg-trace-diag-1[^\n]*$'
$spans = @(
    @{ Id = 'D1'; Key = 'place'; A = '    const double X0 = Entry.Location.X, Y0 = Entry.Location.Y;'; B = '    Entry.bRwgSettledOnTerrain = true;'; Hooks = 1; Hash = '422C98BBEEFD7EEBEAB011237CEAFD1CEBD0D9DB02AE1042E23B8FE812062613' }
    @{ Id = 'D2'; Key = 'sub'; A = 'bool ANodeShuffleSubsystem::RaycastGroundAt('; B = 'bool ANodeShuffleSubsystem::TryRedealWaterLockedEntry('; Hooks = 0; Hash = '6A84B3F4958E0EA37525F2F561EB10C236883901059DAC7B640D03F72F45F303' }
    @{ Id = 'D3'; Key = 'place'; A = 'void ANodeShuffleSubsystem::RwgSpawnPass(float SpawnRadiusCm, bool& bChangedWorld)'; B = 'void ANodeShuffleSubsystem::RwgLogSkipOnce('; Hooks = 2; Hash = '0846E7385DE65C0A1615D6164AD3F6271E008AB1A5DB4C94A05283EF28E7375F' }
    @{ Id = 'D4'; Key = 'sub'; A = '        bool bSettled = (GetMapMode() == ENodeShuffleMapMode::Rwg) ? RwgResolveAtSpawn(Entry, bRwgApiUsed, bWaterNoLand) : false;'; B = '            // Defer-log backoff: full detail once per entry per session;'; Hooks = 0; Hash = '17F0F252F7BDC0FC46C6584085E3CCE1235A1FC980E0531F5E4DB1D1DE4D07BC' }
    @{ Id = 'D5'; Key = 'dens'; A = 'int32 ANodeShuffleSubsystem::RwgStuckCounter(int32 I) const'; B = 'bool ANodeShuffleSubsystem::RwgDensityLandSample('; Hooks = 0; Hash = '08C016E766FBEDA15CEE1B062C57248AB055869060034EE74E19F8FF4583B3B3' }
)
function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }
function Sha([string]$s) { return ([BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($s))) -replace '-', '') }
function Get-Span([string]$t, $sp) {
    if ((Count $t $sp.A) -ne 1) { return $null }
    $i = $t.IndexOf($sp.A); $j = $t.IndexOf($sp.B, $i)
    if ($j -lt 0) { return $null }
    $raw = $t.Substring($i, $j - $i)
    $dropped = ([regex]::Matches($raw, $hookRe)).Count
    $kept = ($raw.Split("`n") | Where-Object { $_ -notmatch ($hookRe -replace '^\(\?m\)', '') }) -join "`n"
    return @{ Hash = (Sha $kept); Dropped = $dropped }
}
function Fmt-Fields([string]$fmt) { return [regex]::Matches($fmt, '(?<![A-Za-z0-9])([A-Za-z][A-Za-z0-9]*)=') | ForEach-Object { $_.Groups[1].Value } }
function Specs([string]$fmt) { return ([regex]::Matches($fmt, '%[-+ #0]*\d*(?:\.\d+)?[dsfiu]')).Count }
function Log-Args([string]$t, [string]$tag) {
    # Depth-aware top-level comma count of the UE_LOG call whose format starts with the tag; string / char literals skipped.
    $k = $t.IndexOf('TEXT("' + $tag); if ($k -lt 0) { return -1 }
    $u = $t.LastIndexOf('UE_LOG(', $k); if ($u -lt 0) { return -1 }
    $depth = 0; $commas = 0; $i = $u + 7; $inS = $false; $inC = $false
    while ($i -lt $t.Length) {
        $c = $t[$i]
        if ($inS) { if ($c -eq '\') { $i++ } elseif ($c -eq '"') { $inS = $false } }
        elseif ($inC) { if ($c -eq '\') { $i++ } elseif ($c -eq "'") { $inC = $false } }
        elseif ($c -eq '"') { $inS = $true } elseif ($c -eq "'") { $inC = $true }
        elseif ('([{'.Contains($c)) { $depth++ }
        elseif (')]}'.Contains($c)) { if ($depth -eq 0) { return $commas + 1 - 3 }; $depth-- }
        elseif ($c -eq ',' -and $depth -eq 0) { $commas++ }
        $i++
    }
    return -1
}
function Read-Text([string]$p) { return ((Get-Content -Raw -Encoding utf8 -Path $p) -replace "`r`n", "`n") }
function Read-AtRef([string]$ref, [string]$rel) {
    $psi = [Diagnostics.ProcessStartInfo]::new('git', "-C `"$root`" show `"${ref}:Source/$($rel -replace '\\', '/')`"")
    $psi.RedirectStandardOutput = $true; $psi.UseShellExecute = $false
    $p = [Diagnostics.Process]::Start($psi); $ms = [IO.MemoryStream]::new(); $p.StandardOutput.BaseStream.CopyTo($ms); $p.WaitForExit()
    if ($p.ExitCode -ne 0) { throw "git show $ref failed for $rel" }
    return ([Text.Encoding]::UTF8.GetString($ms.ToArray()) -replace "`r`n", "`n")
}

if ($PrintHashesAt) {
    foreach ($sp in $spans) { $r = Get-Span (Read-AtRef $PrintHashesAt $files[$sp.Key]) $sp; Write-Host "$($sp.Id) $(if ($r) { "$($r.Hash) hooks=$($r.Dropped)" } else { 'ANCHOR-NOT-FOUND' })" }
    exit 0
}
$text = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = Read-Text $p
}

function Invoke-TraceChecks($T) {
    $f = @()
    # D1-D5
    foreach ($sp in $spans) {
        $r = Get-Span $T[$sp.Key] $sp
        if (-not $r) { $f += "($($sp.Id)) span anchors not found exactly once / in order"; continue }
        if ($r.Dropped -ne $sp.Hooks) { $f += "($($sp.Id)) expected exactly $($sp.Hooks) hook line(s) inside the span, found $($r.Dropped)" }
        if ($r.Hash -ne $sp.Hash) { $f += "($($sp.Id)) decision code differs from HEAD 65ab5b4 (sha256 $($r.Hash))" }
    }
    # T1
    if ((Count $T.diag 'constexpr int32 RwgTraceMissPrintAt[] = { 1, 12, 48 };') -ne 1 -or (Count $T.diag 'constexpr int32 RwgTraceMissPrintEvery = 96;') -ne 1) { $f += '(T1) the schedule must be the one constant pair 1, 12, 48 then every 96' }
    if ((Count $T.diag 'for (const int32 At : RwgTraceMissPrintAt) { if (Ordinal == At) { return true; } }') -ne 1 -or (Count $T.diag 'return Ordinal > 0 && (Ordinal % RwgTraceMissPrintEvery) == 0;') -ne 1 -or (Count $T.diag 'if (!World || !RwgTraceMissOnSchedule(Ordinal)) { return; }') -ne 1) { $f += '(T1) the line must print only on the schedule predicate' }
    if ((Count $T.diag 'const int32 Ordinal = Prior + 1;') -ne 1 -or (Count $T.diag 'const int32 Prior = Tracker ? Tracker->Key : 0;') -ne 1) { $f += '(T1) attemptOrdinal must be the tracker count before the attempt plus one' }
    # T2
    foreach ($tag in @('RWGTRACEMISS:', 'RWGTRACECENSUS:')) {
        if (([regex]::Matches($T.diag, 'UE_LOG\(LogNodeShuffle, Display,\s*TEXT\("' + $tag + ' ')).Count -ne 1) { $f += "(T2) $tag must be the first TEXT literal of exactly one Display UE_LOG" }
        if ((Count $T.diag ('TEXT("' + $tag)) -ne 1) { $f += "(T2) $tag must appear as a TEXT literal exactly once" }
        if ([regex]::IsMatch($T.diag, 'UE_LOG\(LogNodeShuffle, (Warning|Error|Log|Verbose|Fatal),\s*TEXT\("' + $tag)) { $f += "(T2) $tag must never log above or below Display" }
        foreach ($k in @('place', 'sub', 'dens', 'hapi', 'nsmod', 'diagh')) { if ($T[$k].Contains('TEXT("' + $tag)) { $f += "(T2) $tag must be emitted only from the diag file (found in $k)" } }
    }
    # T3
    $want = @{ 'RWGTRACEMISS:' = 39; 'RWGTRACECENSUS:' = 17 }
    foreach ($tag in $want.Keys) {
        $m = [regex]::Match($T.diag, 'TEXT\("(' + $tag + '[^"]*)"\)')
        if (-not $m.Success) { $f += "(T3) $tag format not found"; continue }
        $fmt = $m.Groups[1].Value; $flds = @(Fmt-Fields $fmt)
        $dup = $flds | Group-Object | Where-Object { $_.Count -gt 1 }
        if ($dup) { $f += "(T3) repeated field(s) $(($dup | ForEach-Object { $_.Name }) -join ',') in $tag" }
        if ($flds.Count -ne $want[$tag]) { $f += "(T3) $tag must carry $($want[$tag]) fields, found $($flds.Count)" }
        if ((Specs $fmt) -ne $flds.Count -or ([regex]::Matches($fmt, '=%')).Count -ne $flds.Count) { $f += "(T3) every $tag field must carry exactly one specifier" }
        $n = Log-Args $T.diag $tag
        if ($n -ne (Specs $fmt)) { $f += "(T3) $tag passes $n argument(s) for $(Specs $fmt) specifier(s)" }
    }
    # T4
    $hooks = [regex]::Matches($T.place, $hookRe)
    if ($hooks.Count -ne 3 -or (Count $T.place ('NodeShuffleRwg' + 'TraceDiag::')) -ne 3) { $f += "(T4) Place.cpp must carry exactly three hook calls, each alone on its line (found $($hooks.Count) strict)" }
    if (-not $T.place.Contains("    bRwgProbeArmed = false;`n    NodeShuffleRwgTraceDiag::NoteFirstTrace(GetWorld(), Entry.EntryGuid, Entry.AssignedResourceClassPath, X0, Y0, ApiZ0, Entry.bUnderground, RwgProbeFirstClass, RwgProbeFirstZ, RwgStuckTracker.Find(Entry.EntryGuid), bRwgKillboxKnown ? RwgKillboxTopZ : NodeShuffleMapMode::RwgNaN()); // rwg-trace-diag-1")) { $f += '(T4) NoteFirstTrace must follow bRwgProbeArmed = false directly, with the pinned arguments' }
    if (-not $T.place.Contains("    NodeShuffleRwgTraceDiag::BeginPass(RwgPcPasses + 1, Pawns); // rwg-trace-diag-1")) { $f += '(T4) BeginPass must take RwgPcPasses + 1 and the pass pawns' }
    $iB = $T.place.IndexOf('NodeShuffleRwgTraceDiag::BeginPass('); $iP = $T.place.IndexOf('if (P) { Pawns.Add(P->GetActorLocation()); }'); $iA = $T.place.IndexOf('    const float AttemptRadiusCm = ')
    if ($iB -lt $iP -or $iB -gt $iA) { $f += '(T4) BeginPass must run after the pawn list and before the attempt radius' }
    if (-not $T.place.Contains("    NodeShuffleRwgTraceDiag::EmitPassCensus(RwgPcPasses, RwgPcAttempted, RwgPcSettledTerrain, RwgPcNoTerrainDefer, DeferredThisPass - DeferredBefore); // rwg-trace-diag-1: before RwgEmitPlaceCensus resets the counters`n    RwgDensityPassHook(Pawns, AttemptRadiusCm);")) { $f += '(T4) EmitPassCensus must run directly before RwgDensityPassHook (check_density_lint D5 keeps that hook adjacent to RwgEmitPlaceCensus, which resets the counters)' }
    if (-not $T.place.Contains("        RwgEmitDzCensus(); // RWG P2c: session apiToTerrainDz distribution by band, emitted with PLACECENSUS`n    }`n    NodeShuffleRwgTraceDiag::EmitPassCensus(")) { $f += '(T4) EmitPassCensus must follow the PLACECENSUS block directly (nothing between the census print and the hook)' }
    if ((Count $T.diag 'if (FirstClass >= 0)') -ne 1) { $f += '(T4) the miss split must be FirstClass < 0, the spawnTraceMiss predicate' }
    # T5
    $tp = [regex]::Match($T.diag, 'constexpr int32 RwgTraceDiagTracesPerEvent = (\d+);'); $ep = [regex]::Match($T.diag, 'constexpr int32 RwgTraceDiagMaxEventsPerPass = (\d+);')
    if (-not $tp.Success -or -not $ep.Success -or ([int]$tp.Groups[1].Value * [int]$ep.Groups[1].Value) -gt 32) { $f += '(T5) traces per event x events per pass must be <= 32' }
    if ((Count $T.diag 'static_assert(RwgTraceDiagTracesPerEvent * RwgTraceDiagMaxEventsPerPass <= 32,') -ne 1) { $f += '(T5) the 32-trace static_assert must be present' }
    $iR = $T.diag.IndexOf("    if (bRun)`n    {"); $iE = $T.diag.IndexOf("    else`n    {`n        ++S.EventsSkipped;")
    $blk = if ($iR -ge 0 -and $iE -gt $iR) { $T.diag.Substring($iR, $iE - $iR) } else { '' }
    if ((Count $T.diag 'RwgTraceDiagTrace(World,') -ne 2 -or (Count $blk 'RwgTraceDiagTrace(World,') -ne 2 -or (Count $T.diag 'RwgTraceDiagChunks(World,') -ne 1 -or (Count $blk 'RwgTraceDiagChunks(World,') -ne 1 -or (Count $blk '++S.EventsRun;') -ne 1) { $f += '(T5) every probe call must sit inside the budget branch, which counts the event' }
    if ((Count $T.diag 'const bool bRun = S.EventsRun < RwgTraceDiagMaxEventsPerPass;') -ne 1 -or (Count $T.diag 'World->LineTraceSingleByChannel(') -ne 1) { $f += '(T5) one trace call site, gated by the per-pass event budget' }
    # T6
    foreach ($m in [regex]::Matches($T.diag, 'TEXT\("((RWGTRACEMISS|RWGTRACECENSUS):[^"]*)"\)')) {
        foreach ($w in @(('bec' + 'ause'), ('stre' + 'amed'), ('not lo' + 'aded'), ('unlo' + 'aded'), ('no ter' + 'rain'))) { if ($m.Groups[1].Value -match ('(?i)' + $w)) { $f += "(T6) format names a cause ('$w')" } }
    }
    # T7
    foreach ($n in @(('Raycast' + 'GroundAt('), ('Raycast' + 'Settle('), ('RecordWater' + 'GridSample'), ('AddIgnored' + 'Actor'), ('AddIgnored' + 'Component'), ('NodeShuffle' + 'Subsystem.h'))) { if ($T.diag.Contains($n) -or $T.diagh.Contains($n)) { $f += "(T7) the diag files must not use '$n'" } }
    # T8
    foreach ($n in @('    const FVector Start(ProbeXY.X, ProbeXY.Y, bShortTrace ? StartZ + 400.f : StartZ + 20000.f);', '    const FVector End(ProbeXY.X, ProbeXY.Y, bShortTrace ? StartZ - 800.f : StartZ - 40000.f);', 'if (!GetWorld()->LineTraceSingleByChannel(H, S, En, ECC_WorldStatic, P)) { return false; }', '    FCollisionQueryParams Params(SCENE_QUERY_STAT(NodeShuffleSettle), true);')) { if ((Count $T.sub $n) -ne 1) { $f += "(T8) decision trace statement changed: $($n.Trim().Substring(0, 40))..." } }
    foreach ($n in @('constexpr double RwgDecisionUpCm = 20000.0;', 'constexpr double RwgDecisionDownCm = 40000.0;', 'constexpr double RwgDecisionShortUpCm = 400.0;', 'constexpr double RwgDecisionShortDownCm = 800.0;', 'FVector(X, Y, ToZ), ECC_WorldStatic, P)', 'const FCollisionQueryParams Fresh(SCENE_QUERY_STAT(NodeShuffleRwgTraceDiag), true);')) { if ((Count $T.diag $n) -ne 1) { $f += "(T8) mirror of the decision trace drifted: $n" } }
    if ((Count $T.place '    Entry.Location.Z = ApiZ0;') -ne 1) { $f += '(T8) the decision window must still be built around ApiZ0' }
    # T9
    if ((Count $T.diag 'constexpr double RwgTraceDiagWideTopZ = 360000.0;') -ne 1 -or (Count $T.diag 'constexpr double RwgTraceDiagWideBottomZ = -200000.0;') -ne 1 -or (Count $T.hapi 'PawnApiZ > -200000.0 && PawnApiZ < 360000.0') -ne 1) { $f += '(T9) the wide window must be the RWGHEIGHTAPI plausibility bounds' }
    # V5
    if ((Count $T.nsmod ('LOADED (2026-10-26-rwg-' + '0-1-12-2)')) -ne 1 -or (Count $T.nsmod ('2026-10-26-rwg-' + '0-1-12-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'steep-2)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'steep-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'deal-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'density-2)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'rockcache-2')) -ne 0 -or (Count $T.nsmod ('2026-10-18-rwg-' + 'p4c-2')) -ne 0) { $f += '(V5) marker must be rwg-0-1-12-1 with rwg-steep-2 and the earlier markers absent' }
    if ($T.diag.Contains('LOADED' + ' (') -or $T.diagh.Contains('LOADED' + ' (')) { $f += '(V5) no build-marker string in the diag files' }
    return $f
}

$fails = Invoke-TraceChecks $text
$mutants = @(
    @{ Name = 'M1 trace-miss counter double-counts'; Key = 'place'; Expect = 'D1'; Find = 'if (RwgProbeFirstClass < 0) { ++RwgPcTraceMiss; }'; Repl = 'if (RwgProbeFirstClass < 0) { RwgPcTraceMiss += 2; }' }
    @{ Name = 'M2 no-terrain defer falls through'; Key = 'place'; Expect = 'D1'; Find = "RwgSteepNoteUnplaceable(Q, Entry); } // rwg-steep-1: re-probe the primary, arm the ring (NodeShuffleMapModeSteep.cpp)`n        return false;"; Repl = 'RwgSteepNoteUnplaceable(Q, Entry); } // rwg-steep-1: re-probe the primary, arm the ring (NodeShuffleMapModeSteep.cpp)' }
    @{ Name = 'M3 decision edit hidden on the hook line'; Key = 'place'; Expect = 'D1'; Find = '    NodeShuffleRwgTraceDiag::NoteFirstTrace(GetWorld(),'; Repl = '    RwgProbeFirstClass = 0; NodeShuffleRwgTraceDiag::NoteFirstTrace(GetWorld(),' }
    @{ Name = 'M4 decision window widened'; Key = 'sub'; Expect = 'D2'; Find = 'StartZ + 20000.f);'; Repl = 'StartZ + 30000.f);' }
    @{ Name = 'M5 skip cap raised in the trace lambda'; Key = 'sub'; Expect = 'D2'; Find = 'for (int32 K = 0; K <= NodeShuffleMapMode::RwgTerrainSkipMax; ++K)'; Repl = 'for (int32 K = 0; K < NodeShuffleMapMode::RwgTerrainSkipMax + 2; ++K)' }
    @{ Name = 'M6 32/pass cap loosened'; Key = 'place'; Expect = 'D3'; Find = 'const int32 N = FMath::Min(Cands.Num(), NodeShuffleMapMode::RwgMaxSpawnAttemptsPerPass);'; Repl = 'const int32 N = Cands.Num();' }
    @{ Name = 'M7 candidates no longer nearest-first'; Key = 'place'; Expect = 'D3'; Find = 'return A.Key < B.Key; });'; Repl = 'return A.Key > B.Key; });' }
    @{ Name = 'M8 deferral counter dropped'; Key = 'sub'; Expect = 'D4'; Find = "            DeferredThisPass++;`n"; Repl = '' }
    @{ Name = 'M9 stuck tracker no longer resets on a move'; Key = 'dens'; Expect = 'D5'; Find = '{ T.Key = 0; } else { ++T.Key; }'; Repl = '{ ++T.Key; } else { ++T.Key; }' }
    @{ Name = 'M10 schedule loses 48'; Key = 'diag'; Expect = 'T1'; Find = '{ 1, 12, 48 };'; Repl = '{ 1, 12 };' }
    @{ Name = 'M11 marker reverted'; Key = 'nsmod'; Expect = 'V5'; Find = ('2026-10-26-rwg-' + '0-1-12-2'); Repl = ('2026-10-26-rwg-' + '0-1-12-1') }
    @{ Name = 'M12 miss line raised to Warning'; Key = 'diag'; Expect = 'T2'; Find = "UE_LOG(LogNodeShuffle, Display,`n        TEXT(`"RWGTRACEMISS: "; Repl = "UE_LOG(LogNodeShuffle, Warning,`n        TEXT(`"RWGTRACEMISS: " }
    @{ Name = 'M13 census repeats a field'; Key = 'diag'; Expect = 'T3'; Find = 'extraTraces=%d channel=%s"'; Repl = 'extraTraces=%d channel=%s attempted=%d"' }
    @{ Name = 'M14 miss line drops an argument'; Key = 'diag'; Expect = 'T3'; Find = 'Ch.CollMaxZ, Ch.Ms, KillboxTopZ);'; Repl = 'Ch.CollMaxZ, KillboxTopZ);' }
    @{ Name = 'M15 NoteFirstTrace hook removed'; Key = 'place'; Expect = 'T4'; Find = '    NodeShuffleRwgTraceDiag::NoteFirstTrace('; Repl = '    // NodeShuffleRwgTraceDiag::NoteFirstTrace(' }
    @{ Name = 'M16 a counter reset slipped in before the census'; Key = 'place'; Expect = 'T4'; Find = "    NodeShuffleRwgTraceDiag::EmitPassCensus("; Repl = "    RwgPcAttempted = 0;`n    NodeShuffleRwgTraceDiag::EmitPassCensus(" }
    @{ Name = 'M24 census moved after RwgEmitPlaceCensus'; Key = 'place'; Expect = 'T4'; Find = "    RwgEmitPlaceCensus();`n}"; Repl = "    RwgEmitPlaceCensus();`n    NodeShuffleRwgTraceDiag::EmitPassCensus(RwgPcPasses, RwgPcAttempted, RwgPcSettledTerrain, RwgPcNoTerrainDefer, DeferredThisPass - DeferredBefore); // rwg-trace-diag-1: x`n}" }
    @{ Name = 'M17 event budget raised to 64'; Key = 'diag'; Expect = 'T5'; Find = 'RwgTraceDiagMaxEventsPerPass = 16;'; Repl = 'RwgTraceDiagMaxEventsPerPass = 32;' }
    @{ Name = 'M18 probe moved outside the budget branch'; Key = 'diag'; Expect = 'T5'; Find = '    const int32 Distinct = '; Repl = "    RwgTraceDiagTrace(World, Fresh, X, Y, 0.0, 1.0, Unset);`n    const int32 Distinct = " }
    @{ Name = 'M19 format names a cause'; Key = 'diag'; Expect = 'T6'; Find = 'downHitClass=%s downHitActor'; Repl = 'downHitClass=%s (terrain not stre' + 'amed) downHitActor' }
    @{ Name = 'M20 probe through the settle helper'; Key = 'diag'; Expect = 'T7'; Find = '        FHitResult H;'; Repl = '        FHitResult H; // Raycast' + 'GroundAt(' }
    @{ Name = 'M21 mirror drifts'; Key = 'diag'; Expect = 'T8'; Find = 'RwgDecisionDownCm = 40000.0;'; Repl = 'RwgDecisionDownCm = 30000.0;' }
    @{ Name = 'M22 wide window shrunk'; Key = 'diag'; Expect = 'T9'; Find = 'RwgTraceDiagWideTopZ = 360000.0;'; Repl = 'RwgTraceDiagWideTopZ = 120000.0;' }
    @{ Name = 'M23 marker in a diag file'; Key = 'diagh'; Expect = 'V5'; Find = '// rwg-trace-diag-1 (2026-10-27) -- DIAGNOSTICS ONLY.'; Repl = '// LOADED (2026-10-26-rwg-0-1-12-2) -- DIAGNOSTICS ONLY.' }
)
$mutantFails = @()
$caught = 0
foreach ($m in $mutants) {
    $mut = @{}
    foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = Invoke-TraceChecks $mut
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ }
    else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "RWG TRACE-DIAG LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: RWG trace-diag lint -- D1..D5, T1..T9, V5 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
