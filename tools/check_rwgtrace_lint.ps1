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
#   D6  NodeShuffleMapMode.h: the 32/pass cap, the 1000 m attempt radius and the terrain skip cap (review R4).
#   T10 Chunk census (review R2): collision-enabled box UNION, registered + collision-enabled prims, valid-body-instance prims.
#   T11 Dated release TODO above both RWGTRACEMISS/CENSUS logs and both constant blocks; probeMs times the two traces inside the budget branch (R6).
#   T12 Legend wording (R7 probeEventsThisPass, R9 bounds arithmetic).
#   (T2/T3/T5 also cover RWGTRACECTRL, the hit-spot positive control of review R1; T4 the settleReturned argument of R5.)
#   rwg-trace-diag-2 (2026-10-28), also loading NodeShuffleHereUnspawned.cpp (here) and NodeShuffleSubsystem.h (subh):
#   H1  HERE-NEAREST-UNSPAWNED (3 sites) / HERE-UNSPAWNED-BANDS (1): Display only, only in the new file, field + specifier + argument counts.
#   H2  Bearing: the new file's ONE Atan2 is Here's well expression byte-for-byte, reached through TurnFromFacing; the reference stays in Subsystem.cpp.
#   H3  Population and bands: bIsNewNode && bActive, not live; 3000 m radius; band edges 250/500/750/1000/3000 m; 1000 m strict attempt test; tracker read.
#   H4  Wiring: one RWG-gated call directly above the LogHereDensity line; one header declaration.
#   B1  RWGTRACECENSUS player-distance bands: one 2-D nearest-pawn distance before the hit/miss split, one band function, hit and miss slots.
#   C1  RWGTRACECTRL fires at the first hit after a same-XY miss record (no pass-modulo schedule), inside the probe budget; every miss is
#       recorded before the RWGTRACEMISS schedule return; the record map is bounded with evict-oldest and cleared on a pass-number restart.
#   W1  The old spawn-on-discovery Verbose line states its predicate, at the same level and gate; the old cause text is gone.
#   T13 Dated rwg-trace-diag-2 TODO above every new UE_LOG and both new constants.
#   V5  Marker rwg-clifftop-1 present once; rwg-layout-dump-1, rwg-trace-diag-2, rwg-trace-diag-1, rwg-0-1-12-2 and the older markers absent (same block as check_rwg019_lint.ps1); no marker string in the diag files.
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
    mmh   = 'NodeShuffle\Public\NodeShuffleMapMode.h'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
    here  = 'NodeShuffle\Private\NodeShuffleHereUnspawned.cpp'
    subh  = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
}
$hookRe = '(?m)^[ \t]*' + 'NodeShuffleRwg' + 'TraceDiag::(BeginPass|NoteFirstTrace|EmitPassCensus)\([^;\n]*\);[ \t]*//[ \t]*rwg-trace-diag-1[^\n]*$'
$spans = @(
    @{ Id = 'D1'; Key = 'place'; A = 'bool ANodeShuffleSubsystem::RwgResolveAtSpawn('; B = 'void ANodeShuffleSubsystem::RwgSpawnPass('; Hooks = 1; Hash = 'BB6C21B19CCA1A3A2BE3C8A3FCD6DF8735A0270E70B4B27FFC886B1B1DF7BF9D' }
    @{ Id = 'D2'; Key = 'sub'; A = 'bool ANodeShuffleSubsystem::RaycastGroundAt('; B = 'bool ANodeShuffleSubsystem::TryRedealWaterLockedEntry('; Hooks = 0; Hash = '9CD59F03FFC9DF35F29642F77D3273959DAB1C2A871C41EBB49B0DBF36DBF829' }
    @{ Id = 'D3'; Key = 'place'; A = 'void ANodeShuffleSubsystem::RwgSpawnPass(float SpawnRadiusCm, bool& bChangedWorld)'; B = 'void ANodeShuffleSubsystem::RwgLogSkipOnce('; Hooks = 2; Hash = '5E31D2219AAC3B9F5C096AFC0CBD8E90EAFBB7E7F91AB1C78C5DC51AF17E79EC' } # rwg-farspawn-1: RwgSpawnPass gains the provisional call sites + 2 PLACECENSUS fields; 1b F2: distance test before the pin check; 1b F3: RWGSTUCK bracket on the provisional settle
    @{ Id = 'D4'; Key = 'sub'; A = '        bool bSettled = (GetMapMode() == ENodeShuffleMapMode::Rwg) ? RwgResolveAtSpawn(Entry, bRwgApiUsed, bWaterNoLand) : false;'; B = '            // Defer-log backoff: full detail once per entry per session;'; Hooks = 0; Hash = '17F0F252F7BDC0FC46C6584085E3CCE1235A1FC980E0531F5E4DB1D1DE4D07BC' }
    @{ Id = 'D5'; Key = 'dens'; A = 'int32 ANodeShuffleSubsystem::RwgStuckCounter(int32 I) const'; B = 'bool ANodeShuffleSubsystem::RwgDensityLandSample('; Hooks = 0; Hash = '480BB4AFCCD2E82902DB8100FC6075E9A43266D4B0A3C9D5ECB5A874F11EDF91' } # 1b F3: a provisional actor is not live in the RWGSTUCK note
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
        elseif ($c -eq '/' -and $i + 1 -lt $t.Length -and $t[$i + 1] -eq '/') { $nl = $t.IndexOf("`n", $i); if ($nl -lt 0) { return -1 }; $i = $nl; continue }
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
    foreach ($tag in @('RWGTRACEMISS:', 'RWGTRACECENSUS:', 'RWGTRACECTRL:')) {
        if (([regex]::Matches($T.diag, 'UE_LOG\(LogNodeShuffle, Display,\s*TEXT\("' + $tag + ' ')).Count -ne 1) { $f += "(T2) $tag must be the first TEXT literal of exactly one Display UE_LOG" }
        if ((Count $T.diag ('TEXT("' + $tag)) -ne 1) { $f += "(T2) $tag must appear as a TEXT literal exactly once" }
        if ([regex]::IsMatch($T.diag, 'UE_LOG\(LogNodeShuffle, (Warning|Error|Log|Verbose|Fatal),\s*TEXT\("' + $tag)) { $f += "(T2) $tag must never log above or below Display" }
        foreach ($k in @('place', 'sub', 'dens', 'hapi', 'nsmod', 'diagh', 'here', 'subh')) { if ($T[$k].Contains('TEXT("' + $tag)) { $f += "(T2) $tag must be emitted only from the diag file (found in $k)" } }
    }
    # T3
    $want = @{ 'RWGTRACEMISS:' = 43; 'RWGTRACECENSUS:' = 24; 'RWGTRACECTRL:' = 22 }
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
    if ((Count $T.diag 'firstHitKindsTop3=%s') -ne 1 -or $T.diag.Contains('firstHitClasses' + 'Top3')) { $f += '(T3) the census label field must be firstHitKindsTop3 (RwgHitClassOf labels, not UClass names)' }
    foreach ($n in @('settleReturned=%d', 'probeMs=%.2f', 'chunkActorsCollisionEnabledBoxContainXY=%d', 'containingCollisionEnabledBoxUnionMinZ=%.0f', 'containingCollisionEnabledBoxUnionMaxZ=%.0f', 'containingPrimsCollisionEnabled=%d', 'containingPrimsWithPhysicsBody=%d')) { if ((Count $T.diag $n) -ne 1) { $f += "(T3) RWGTRACEMISS must carry $n once" } }
    if ($T.diag.Contains('Colliding' + 'Box')) { $f += '(T3) the old Colliding-box field names must be gone' }
    # T4
    $hooks = [regex]::Matches($T.place, $hookRe)
    if ($hooks.Count -ne 3 -or (Count $T.place ('NodeShuffleRwg' + 'TraceDiag::')) -ne 3) { $f += "(T4) Place.cpp must carry exactly three hook calls, each alone on its line (found $($hooks.Count) strict)" }
    if (-not $T.place.Contains("    bRwgProbeArmed = false;`n    NodeShuffleRwgTraceDiag::NoteFirstTrace(GetWorld(), Entry.EntryGuid, Entry.AssignedResourceClassPath, X0, Y0, ApiZ0, Entry.bUnderground, RwgProbeFirstClass, RwgProbeFirstZ, bSettled, RwgStuckTracker.Find(Entry.EntryGuid), bRwgKillboxKnown ? RwgKillboxTopZ : NodeShuffleMapMode::RwgNaN()); // rwg-trace-diag-1")) { $f += '(T4) NoteFirstTrace must follow bRwgProbeArmed = false directly, with the pinned arguments (bSettled = the settle return)' }
    if ((Count $T.diag 'int32 FirstClass, double FirstZ, bool bSettleReturned, const TPair<int32, FString>* Tracker, double KillboxTopZ)') -ne 1 -or (Count $T.diagh 'int32 FirstClass, double FirstZ, bool bSettleReturned, const TPair<int32, FString>* Tracker, double KillboxTopZ);') -ne 1 -or (Count $T.diag 'bShortWindow ? 1 : 0, bSettleReturned ? 1 : 0, WinTop,') -ne 1) { $f += '(T4) settleReturned must print the hook argument bSettleReturned, in the settleReturned position' }
    if ((Count $T.place '    const bool bSettled = RaycastSettle(Entry, nullptr, nullptr, &bOutWaterNoLand, /*bTerrainOnly=*/true);') -ne 1) { $f += '(T4) bSettled must be the primary settle return' }
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
    if ((Count $T.diag 'RwgTraceDiagTrace(World,') -ne 4 -or (Count $blk 'RwgTraceDiagTrace(World,') -ne 2 -or (Count $T.diag 'RwgTraceDiagChunks(World,') -ne 1 -or (Count $blk 'RwgTraceDiagChunks(World,') -ne 1 -or (Count $blk '++S.EventsRun;') -ne 1) { $f += '(T5) every probe call must sit inside the budget branch, which counts the event' }
    if ((Count $T.diag 'const bool bRun = S.EventsRun < RwgTraceDiagMaxEventsPerPass;') -ne 1 -or (Count $T.diag 'World->LineTraceSingleByChannel(') -ne 1) { $f += '(T5) one trace call site, gated by the per-pass event budget' }
    # rwg-trace-diag-2: the control's gate is the miss-to-hit transition (C1); its two traces sit inside the same per-pass event budget.
    $ctrlGate = '        if (World && bTransition)'
    $iC = $T.diag.IndexOf($ctrlGate); $iCe = $T.diag.IndexOf('TEXT("RWGTRACECTRL:')
    $cb = if ($iC -ge 0 -and $iCe -gt $iC) { $T.diag.Substring($iC, $iCe - $iC) } else { '' }
    $ctrlRun = "            if (bCtrlRun)`n            {`n                ++S.EventsRun; S.ExtraTraces += RwgTraceDiagTracesPerEvent;`n                Down = RwgTraceDiagTrace(World, Fresh, X, Y, RwgTraceDiagWideTopZ, RwgTraceDiagWideBottomZ, CtrlUnset);`n                Up = RwgTraceDiagTrace(World, Fresh, X, Y, RwgTraceDiagWideBottomZ, RwgTraceDiagWideTopZ, CtrlUnset);`n            }`n            else`n            {`n                ++S.EventsSkipped;`n            }"
    if ((Count $T.diag $ctrlGate) -ne 1 -or (Count $cb 'RwgTraceDiagTrace(World,') -ne 2 -or (Count $cb $ctrlRun) -ne 1 -or (Count $cb 'const bool bCtrlRun = S.EventsRun < RwgTraceDiagMaxEventsPerPass;') -ne 1 -or (Count $cb '++S.EventsRun;') -ne 1) { $f += '(T5) the control must run inside the same event budget, count its two traces, and count a skipped event' }
    $iH = $T.diag.IndexOf('    if (FirstClass >= 0)'); $iM = $T.diag.IndexOf('    ++S.Misses;')
    if ($iC -lt $iH -or $iC -gt $iM) { $f += '(T5) the control must sit in the HIT branch' }
    # T6
    foreach ($m in @([regex]::Matches($T.diag, 'TEXT\("((RWGTRACEMISS|RWGTRACECENSUS|RWGTRACECTRL):[^"]*)"\)')) + @([regex]::Matches($T.here, 'TEXT\("((HERE-NEAREST-UNSPAWNED|HERE-UNSPAWNED-BANDS):[^"]*)"\)'))) {
        foreach ($w in @(('bec' + 'ause'), ('stre' + 'amed'), ('not lo' + 'aded'), ('unlo' + 'aded'), ('no ter' + 'rain'))) { if ($m.Groups[1].Value -match ('(?i)' + $w)) { $f += "(T6) format names a cause ('$w')" } }
    }
    # T7
    foreach ($n in @(('Raycast' + 'GroundAt('), ('Raycast' + 'Settle('), ('RecordWater' + 'GridSample'), ('AddIgnored' + 'Actor'), ('AddIgnored' + 'Component'), ('NodeShuffle' + 'Subsystem.h'))) { if ($T.diag.Contains($n) -or $T.diagh.Contains($n)) { $f += "(T7) the diag files must not use '$n'" } }
    # T8
    foreach ($n in @('    const FVector Start(ProbeXY.X, ProbeXY.Y, bShortTrace ? StartZ + 400.f : StartZ + NodeShuffleMapMode::RwgPrimaryTraceUpCm); // rwg-clifftop-1: named, same float value', '    const FVector End(ProbeXY.X, ProbeXY.Y, bShortTrace ? StartZ - 800.f : StartZ - 40000.f);', 'if (!GetWorld()->LineTraceSingleByChannel(H, S, En, ECC_WorldStatic, P)) { return false; }', '    FCollisionQueryParams Params(SCENE_QUERY_STAT(NodeShuffleSettle), true);')) { if ((Count $T.sub $n) -ne 1) { $f += "(T8) decision trace statement changed: $($n.Trim().Substring(0, 40))..." } }
    foreach ($n in @('constexpr double RwgDecisionUpCm = 20000.0;', 'constexpr double RwgDecisionDownCm = 40000.0;', 'constexpr double RwgDecisionShortUpCm = 400.0;', 'constexpr double RwgDecisionShortDownCm = 800.0;', 'FVector(X, Y, ToZ), ECC_WorldStatic, P)', 'const FCollisionQueryParams Fresh(SCENE_QUERY_STAT(NodeShuffleRwgTraceDiag), true);')) { if ((Count $T.diag $n) -ne 1) { $f += "(T8) mirror of the decision trace drifted: $n" } }
    if ((Count $T.place '    Entry.Location.Z = ApiZ0;') -ne 1) { $f += '(T8) the decision window must still be built around ApiZ0' }
    # T9
    if ((Count $T.diag 'constexpr double RwgTraceDiagWideTopZ = 360000.0;') -ne 1 -or (Count $T.diag 'constexpr double RwgTraceDiagWideBottomZ = -200000.0;') -ne 1 -or (Count $T.hapi 'PawnApiZ > -200000.0 && PawnApiZ < 360000.0') -ne 1) { $f += '(T9) the wide window must be the RWGHEIGHTAPI plausibility bounds' }
    # D6
    foreach ($n in @('constexpr int32 RwgMaxSpawnAttemptsPerPass = 32;', 'constexpr float RwgAttemptRadiusCm = 100000.f;', 'constexpr int32 RwgTerrainSkipMax = 8;')) { if ((Count $T.mmh $n) -ne 1) { $f += "(D6) NodeShuffleMapMode.h constant changed: $n" } }
    # T10
    foreach ($n in @('R.CollMinZ = R.ContainColliding == 1 ? BC.Min.Z : FMath::Min(R.CollMinZ, BC.Min.Z);', 'R.CollMaxZ = R.ContainColliding == 1 ? BC.Max.Z : FMath::Max(R.CollMaxZ, BC.Max.Z);', 'TInlineComponentArray<UPrimitiveComponent*> Prims(*It);', 'if (!P || !P->IsRegistered() || P->GetCollisionEnabled() == ECollisionEnabled::NoCollision) { continue; }', 'if (P->BodyInstance.IsValidBodyInstance()) { ++R.PrimsWithBody; }', 'const FBox BC = It->GetComponentsBoundingBox(false);', 'R.PrimsCollisionEnabled = R.PrimsWithBody = 0;')) { if ((Count $T.diag $n) -ne 1) { $f += "(T10) chunk census statement missing: $($n.Substring(0, [Math]::Min(50, $n.Length)))" } }
    if ((Count $T.diag '++R.PrimsCollisionEnabled;') -ne 1) { $f += '(T10) collision-enabled prims counted once per prim' }
    # T11
    $todo = '// TODO(2026-10-27, rwg-trace-diag-1): diagnostic build only -- gate on FNodeShuffleModule::AreDiagnosticsEnabled() or remove before release.'
    foreach ($nb in @("$todo`n    // RWGTRACEMISS schedule", "$todo`n    // Extra-trace budget", "$todo`n    UE_LOG(LogNodeShuffle, Display,`n        TEXT(`"RWGTRACEMISS: ", "$todo`n        UE_LOG(LogNodeShuffle, Display,`n            TEXT(`"RWGTRACECENSUS: ")) { if ((Count $T.diag $nb) -ne 1) { $f += "(T11) dated release TODO missing above: $($nb.Substring($todo.Length).Trim().Substring(0, 20))" } }
    if ((Count $blk 'ProbeMs = (FPlatformTime::Seconds() - TProbe) * 1000.0;') -ne 1 -or (Count $blk 'const double TProbe = FPlatformTime::Seconds();') -ne 1 -or (Count $T.diag 'S.EventsRun, ProbeMs, RwgTraceDiagWideTopZ,') -ne 1) { $f += '(T11) probeMs must time the two probe traces inside the budget branch and print in the probeMs position' }
    # T12
    if ((Count $T.diag 'probeEventsThisPass counts the probe events run so far this pass (this one included only when probesRun is 1).') -ne 1) { $f += '(T12) the probeEventsThisPass legend must say when this event is included' }
    if ((Count $T.diag 'the ceiling is 3x the .key ceiling 120000 cm, the floor lies below 3x the .key floor -60000 cm') -ne 1) { $f += '(T12) the wide-window comment must state the bounds arithmetic correctly' }
    # H1 (rwg-trace-diag-2, change A)
    $hereFmts = @(
        @{ P = 'HERE-NEAREST-UNSPAWNED: entry='; N = 11; Free = 1 }
        @{ P = 'HERE-NEAREST-UNSPAWNED: none within'; N = 4; Free = 1 }
        @{ P = 'HERE-NEAREST-UNSPAWNED: no player pawn'; N = 0; Free = 0 }
        @{ P = 'HERE-UNSPAWNED-BANDS: '; N = 7; Free = 0 }
    )
    foreach ($x in $hereFmts) {
        $m = [regex]::Match($T.here, 'TEXT\("(' + [regex]::Escape($x.P) + '[^"]*)"\)')
        if (-not $m.Success) { $f += "(H1) format '$($x.P)' not found"; continue }
        $fmt = $m.Groups[1].Value; $flds = @(Fmt-Fields $fmt)
        if ($flds | Group-Object | Where-Object { $_.Count -gt 1 }) { $f += "(H1) repeated field in '$($x.P)'" }
        if ($flds.Count -ne $x.N -or ([regex]::Matches($fmt, '=%')).Count -ne $x.N -or (Specs $fmt) -ne ($x.N + $x.Free)) { $f += "(H1) '$($x.P)' must carry $($x.N) field(s) with one specifier each plus $($x.Free) prose specifier(s)" }
        $n = Log-Args $T.here $x.P
        if ($n -ne (Specs $fmt)) { $f += "(H1) '$($x.P)' passes $n argument(s) for $(Specs $fmt) specifier(s)" }
    }
    if (([regex]::Matches($T.here, 'UE_LOG\(LogNodeShuffle, Display,\s*TEXT\("HERE-NEAREST-UNSPAWNED: ')).Count -ne 3 -or ([regex]::Matches($T.here, 'UE_LOG\(LogNodeShuffle, Display,\s*TEXT\("HERE-UNSPAWNED-BANDS: ')).Count -ne 1 -or (Count $T.here 'UE_LOG(') -ne 4) { $f += '(H1) the new file must hold exactly 3 Display HERE-NEAREST-UNSPAWNED logs and 1 Display HERE-UNSPAWNED-BANDS log, and no other UE_LOG' }
    if ([regex]::IsMatch($T.here, 'UE_LOG\(LogNodeShuffle, (Warning|Error|Log|Verbose|Fatal),')) { $f += '(H1) the HERE lines must log at Display' }
    foreach ($k in @('place', 'sub', 'dens', 'diag', 'diagh', 'hapi', 'nsmod', 'subh')) { foreach ($tg in @('TEXT("HERE-NEAREST-UNSPAWNED', 'TEXT("HERE-UNSPAWNED-BANDS')) { if ($T[$k].Contains($tg)) { $f += "(H1) $tg must be emitted only from the new file (found in $k)" } } }
    # H2
    if ((Count $T.here 'return FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)) - Pawn->GetActorRotation().Yaw);') -ne 1 -or (Count $T.here 'Atan2(') -ne 1 -or (Count $T.here 'const FVector D = Target - P;') -ne 1 -or (Count $T.here 'TurnFromFacing(Nearest->Location));') -ne 1 -or (Count $T.here 'turn %+.0f deg and go') -ne 1) { $f += '(H2) the HERE turn must be Here''s bearing expression, once, reached through TurnFromFacing' }
    if ((Count $T.sub 'FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)) - Pawn->GetActorRotation().Yaw);') -ne 1 -or (Count $T.sub 'from where you are standing and facing, turn %+.0f deg ') -ne 1) { $f += '(H2) Here''s well bearing, the reference the new line mirrors, must still be in NodeShuffleSubsystem.cpp unchanged' }
    foreach ($bad in @(('Atan2(D.X' + ', D.Y)'), ('.Yaw - FMath::Radians' + 'ToDegrees'), ('D = P' + ' - Target'))) { if ($T.here.Contains($bad)) { $f += '(H2) inverted bearing form present' } }
    # H3
    foreach ($n in @('void ANodeShuffleSubsystem::LogHereUnspawned(const FVector& P, const APawn* Pawn) const', 'if (!E.bIsNewNode || !E.bActive) { continue; }', 'AFGResourceNode* const* Live = SpawnedNodes.Find(E.EntryGuid);', 'if (Live && IsValid(*Live)) { continue; }', 'const double D2 = FVector::DistSquared2D(E.Location, P);', 'constexpr double RadiusCm = 300000.0;', 'constexpr int32 NumEdges = 5;', 'const double EdgeCm[NumEdges] = { 25000.0, 50000.0, 75000.0, 100000.0, RadiusCm };', 'while (B < NumEdges && D2 >= FMath::Square(EdgeCm[B])) { ++B; }', 'if (Nearest && NearestD2 < FMath::Square(RadiusCm))', 'const bool bInsideAttempt = NearestD2 < FMath::Square(static_cast<double>(NodeShuffleMapMode::RwgAttemptRadiusCm));', 'const TPair<int32, FString>* T = RwgStuckTracker.Find(Nearest->EntryGuid);', 'const FString Failed = T ? FString::FromInt(T->Key) : FString(TEXT("untracked"));', 'Band[0], Band[1], Band[2], Band[3], Band[4], Band[5], MapWide);', "        ++Band[B];`n        if (E.bRwgCave || DormantThisSession.Contains(E.EntryGuid) || SpawnParkedThisSession.Contains(E.EntryGuid)) { ++Excluded; continue; }", "{ ++Excluded; ++ExcludedSettled; continue; } // review round, fourth set: EnsureNewNodeSpawned skips the settle (and the first trace) for an already-settled entry`n        if (D2 < NearestD2) { NearestD2 = D2; Nearest = &E; }", 'if (E.bRayCasted) { ++Excluded; ++ExcludedSettled; continue; }', 'D.Y / 100.0, Pawn->GetActorRotation().Yaw, *Failed, bInsideAttempt ? 1 : 0, Excluded, ExcludedSettled, TurnFromFacing(Nearest->Location));', 'RadiusCm / 100.0, MapWide, Excluded, ExcludedSettled, Nearest ?')) { if ((Count $T.here $n) -ne 1) { $f += "(H3) population / band statement missing: $($n.Substring(0, [Math]::Min(60, $n.Length)))" } }
    # H4
    $hcall = "    if (GetMapMode() == ENodeShuffleMapMode::Rwg) { LogHereUnspawned(P, Pawn); } // rwg-trace-diag-2: HERE-NEAREST-UNSPAWNED + HERE-UNSPAWNED-BANDS (NodeShuffleHereUnspawned.cpp)`n    if (GetMapMode() == ENodeShuffleMapMode::Rwg) { LogHereDensity(P, static_cast<float>(CensusRadiusCm)); }"
    if ((Count $T.sub $hcall) -ne 1 -or (Count $T.sub 'LogHereUnspawned(') -ne 1 -or (Count $T.subh 'void LogHereUnspawned(const FVector& P, const class APawn* Pawn) const;') -ne 1) { $f += '(H4) one RWG-gated LogHereUnspawned call directly above the HERE-DENSITY call, and one declaration' }
    foreach ($k in @('place', 'dens', 'diag', 'diagh', 'hapi', 'nsmod')) { if ($T[$k].Contains('LogHereUnspawned(')) { $f += "(H4) LogHereUnspawned must not be called from $k" } }
    # B1 (change B)
    $bandHead = "    double Best = TNumericLimits<double>::Max();`n    for (const FVector& P : S.Pawns) { Best = FMath::Min(Best, FVector::DistSquared2D(P, FVector(X, Y, 0.0))); }`n    const double PawnDistM = S.Pawns.Num() > 0 ? FMath::Sqrt(Best) / 100.0 : NodeShuffleMapMode::RwgNaN();`n    const int32 Band = RwgTraceDiagBand(PawnDistM);`n    if (FirstClass >= 0)`n    {`n        ++S.Hits;`n        ++S.BandHits[Band];"
    if ((Count $T.diag $bandHead) -ne 1 -or (Count $T.diag 'DistSquared2D(P, FVector(X, Y, 0.0))') -ne 1 -or (Count $T.diag '++S.BandHits[') -ne 1 -or (Count $T.diag '++S.BandMisses[') -ne 1) { $f += '(B1) one 2-D nearest-pawn distance before the hit/miss split feeds one band, counted once per hit and once per miss' }
    foreach ($n in @('constexpr int32 RwgTraceDiagBandSlots = 6;', 'if (FMath::IsNaN(DistM)) { return 5; }', 'return DistM < 250.0 ? 0 : DistM < 500.0 ? 1 : DistM < 750.0 ? 2 : DistM < 1000.0 ? 3 : 4;', 'return FString::Printf(TEXT("%d/%d/%d/%d/%d/%d"), C[0], C[1], C[2], C[3], C[4], C[5]);', 'TEXT("WorldStatic"), *RwgTraceDiagBandString(S.BandHits), *RwgTraceDiagBandString(S.BandMisses),', 'hitsByPawnDistBand=%s missesByPawnDistBand=%s')) { if ((Count $T.diag $n) -ne 1) { $f += "(B1) band statement missing: $($n.Substring(0, [Math]::Min(60, $n.Length)))" } }
    # C1 (change C)
    if ($T.diag.Contains('(S.Pass ' + '% 12)') -or $T.diag.Contains('bCtrl' + 'Done')) { $f += '(C1) the control must not run on a pass-modulo schedule' }
    foreach ($n in @('if (const FRwgMissRecord* Found = GRwgMissRecords.Find(Guid))', 'bTransition = Found->Xcm == RwgTraceDiagCm(X) && Found->Ycm == RwgTraceDiagCm(Y) && Found->Misses >= 1;', 'GRwgMissRecords.Remove(Guid);', 'int64 RwgTraceDiagCm(double V) { return static_cast<int64>(FMath::RoundToDouble(V)); }', 'constexpr int32 RwgTraceDiagMissRecordBound = 256;', 'if (R->Xcm == Xcm && R->Ycm == Ycm) { ++R->Misses; R->LastMissPass = Pass; R->DistAtLastMissM = DistM; R->Seq = ++GRwgMissSeq; return; }', 'N.LastMissPass = Pass; N.DistAtLastMissM = DistM;', '    GRwgLastPass = Pass;', '            ++S.CtrlLines;', 'if (KV.Value.Seq < OldestSeq) { OldestSeq = KV.Value.Seq; Oldest = KV.Key; }', 'else if (GRwgMissRecords.Num() >= RwgTraceDiagMissRecordBound)', 'for (const TPair<FGuid, FRwgMissRecord>& KV : GRwgMissRecords) { if (KV.Value.Seq < OldestSeq) { OldestSeq = KV.Value.Seq; Oldest = KV.Key; } }', 'GRwgMissRecords.Remove(Oldest);', 'N.Xcm = Xcm; N.Ycm = Ycm; N.FirstMissPass = Pass; N.Misses = 1; N.DistAtFirstMissM = DistM; N.Seq = ++GRwgMissSeq;', 'if (Pass <= GRwgLastPass) { GRwgMissRecords.Reset(); GRwgMissEvicted = GRwgMissXYChanged = 0; }', 'Rec.Misses, Rec.FirstMissPass, Rec.LastMissPass, S.Pass, Rec.DistAtFirstMissM, Rec.DistAtLastMissM, PawnDistM);', 'S.CtrlLines, GRwgMissRecords.Num(), RwgTraceDiagMissRecordBound, GRwgMissEvicted, GRwgMissXYChanged);')) { if ((Count $T.diag $n) -ne 1) { $f += "(C1) transition / record statement missing: $($n.Substring(0, [Math]::Min(60, $n.Length)))" } }
    $noteMiss = "    ++S.Misses;`n    ++S.BandMisses[Band];`n    RwgTraceDiagNoteMiss(Guid, X, Y, S.Pass, PawnDistM);`n    const int32 Prior = Tracker ? Tracker->Key : 0;"
    if ((Count $T.diag $noteMiss) -ne 1 -or (Count $T.diag 'RwgTraceDiagNoteMiss(') -ne 2) { $f += '(C1) every miss must be recorded, before the RWGTRACEMISS schedule return' }
    $iF = $T.diag.IndexOf('if (const FRwgMissRecord* Found = GRwgMissRecords.Find(Guid))')
    if ($iF -lt $iH -or $iF -gt $iC) { $f += '(C1) the record lookup must sit in the HIT branch, before the control gate' }
    # W1 (change D)
    if ($T.sub.Contains('no terrain / out of range; further' + ' retries silent')) { $f += '(W1) the old spawn-on-discovery cause text must be gone' }
    $wl = "                if (FNodeShuffleModule::AreDiagnosticsEnabled())`n                {`n                    UE_LOG(LogNodeShuffle, Verbose,`n                        TEXT(`"Spawn-on-discovery: deferred %s node at %s (the settle call returned false; further retries silent)`"), // rwg-trace-diag-2: states the predicate (!bSettled), not a cause`n                        *ResourceClass->GetName(), *Entry.Location.ToCompactString());"
    if ((Count $T.sub $wl) -ne 1) { $f += '(W1) the spawn-on-discovery Verbose line must state its predicate, at Verbose, behind AreDiagnosticsEnabled, with its two arguments' }
    # T13
    $todo2 = '// TODO(2026-10-28, rwg-trace-diag-2): diagnostic build only -- gate or remove before release.'
    foreach ($nb in @("$todo2`n    // Player-distance band slots", "$todo2`n    // RWGTRACECTRL miss records", "$todo2`n            UE_LOG(LogNodeShuffle, Display, TEXT(`"RWGTRACECTRL: ")) { if ((Count $T.diag $nb) -ne 1) { $f += "(T13) dated rwg-trace-diag-2 TODO missing above: $($nb.Substring($todo2.Length).Trim().Substring(0, 24))" } }
    foreach ($nb in @("$todo2`n        UE_LOG(LogNodeShuffle, Display, TEXT(`"HERE-NEAREST-UNSPAWNED: no player pawn", "$todo2`n        UE_LOG(LogNodeShuffle, Display,`n            TEXT(`"HERE-NEAREST-UNSPAWNED: entry=", "$todo2`n        UE_LOG(LogNodeShuffle, Display,`n            TEXT(`"HERE-NEAREST-UNSPAWNED: none within", "$todo2`n    UE_LOG(LogNodeShuffle, Display,`n        TEXT(`"HERE-UNSPAWNED-BANDS: ")) { if ((Count $T.here $nb) -ne 1) { $f += "(T13) dated rwg-trace-diag-2 TODO missing above: $($nb.Substring($todo2.Length).Trim().Substring(0, 24))" } }
    # V5
    if ($T.here.Contains('LOADED' + ' (')) { $f += '(V5) no build-marker string in the new HERE file' }
    if ((Count $T.nsmod ('LOADED (2026-10-31-rwg-' + 'farspawn-1)')) -ne 1 -or (Count $T.nsmod ('2026-10-31-rwg-' + 'seedgas-1b)')) -ne 0 -or (Count $T.nsmod ('2026-10-31-rwg-' + 'seedgas-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-30-rwg-' + 'clifftop-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-29-rwg-' + 'layout-dump-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-28-rwg-' + 'trace-diag-2)')) -ne 0 -or (Count $T.nsmod ('2026-10-27-rwg-' + 'trace-diag-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-26-rwg-' + '0-1-12-2)')) -ne 0 -or (Count $T.nsmod ('2026-10-26-rwg-' + '0-1-12-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'steep-2)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'steep-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'deal-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'density-2)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'rockcache-2')) -ne 0 -or (Count $T.nsmod ('2026-10-18-rwg-' + 'p4c-2')) -ne 0) { $f += '(V5) marker must be rwg-0-1-12-1 with rwg-steep-2 and the earlier markers absent' }
    if ($T.diag.Contains('LOADED' + ' (') -or $T.diagh.Contains('LOADED' + ' (')) { $f += '(V5) no build-marker string in the diag files' }
    return $f
}

$fails = Invoke-TraceChecks $text
$mutants = @(
    @{ Name = 'M1 trace-miss counter double-counts'; Key = 'place'; Expect = 'D1'; Find = 'if (RwgProbeFirstClass < 0) { ++RwgPcTraceMiss; }'; Repl = 'if (RwgProbeFirstClass < 0) { RwgPcTraceMiss += 2; }' }
    @{ Name = 'M2 no-terrain defer falls through'; Key = 'place'; Expect = 'D1'; Find = "RwgSteepNoteUnplaceable(Q, Entry); } // rwg-steep-1: re-probe the primary, arm the ring (NodeShuffleMapModeSteep.cpp)`n        return false;"; Repl = 'RwgSteepNoteUnplaceable(Q, Entry); } // rwg-steep-1: re-probe the primary, arm the ring (NodeShuffleMapModeSteep.cpp)' }
    @{ Name = 'M3 decision edit hidden on the hook line'; Key = 'place'; Expect = 'D1'; Find = '    NodeShuffleRwgTraceDiag::NoteFirstTrace(GetWorld(),'; Repl = '    RwgProbeFirstClass = 0; NodeShuffleRwgTraceDiag::NoteFirstTrace(GetWorld(),' }
    @{ Name = 'M4 decision window widened'; Key = 'sub'; Expect = 'D2'; Find = 'StartZ + NodeShuffleMapMode::RwgPrimaryTraceUpCm);'; Repl = 'StartZ + 30000.f);' }
    @{ Name = 'M5 skip cap raised in the trace lambda'; Key = 'sub'; Expect = 'D2'; Find = 'for (int32 K = 0; K <= NodeShuffleMapMode::RwgTerrainSkipMax; ++K)'; Repl = 'for (int32 K = 0; K < NodeShuffleMapMode::RwgTerrainSkipMax + 2; ++K)' }
    @{ Name = 'M6 32/pass cap loosened'; Key = 'place'; Expect = 'D3'; Find = 'const int32 N = FMath::Min(Cands.Num(), NodeShuffleMapMode::RwgMaxSpawnAttemptsPerPass);'; Repl = 'const int32 N = Cands.Num();' }
    @{ Name = 'M7 candidates no longer nearest-first'; Key = 'place'; Expect = 'D3'; Find = 'return A.Key < B.Key; });'; Repl = 'return A.Key > B.Key; });' }
    @{ Name = 'M8 deferral counter dropped'; Key = 'sub'; Expect = 'D4'; Find = "            DeferredThisPass++;`n"; Repl = '' }
    @{ Name = 'M9 stuck tracker no longer resets on a move'; Key = 'dens'; Expect = 'D5'; Find = '{ T.Key = 0; } else { ++T.Key; }'; Repl = '{ ++T.Key; } else { ++T.Key; }' }
    @{ Name = 'M10 schedule loses 48'; Key = 'diag'; Expect = 'T1'; Find = '{ 1, 12, 48 };'; Repl = '{ 1, 12 };' }
    @{ Name = 'M11 marker reverted'; Key = 'nsmod'; Expect = 'V5'; Find = ('2026-10-31-rwg-' + 'farspawn-1'); Repl = ('2026-10-31-rwg-' + 'seedgas-1b') }
    @{ Name = 'M12 miss line raised to Warning'; Key = 'diag'; Expect = 'T2'; Find = "UE_LOG(LogNodeShuffle, Display,`n        TEXT(`"RWGTRACEMISS: "; Repl = "UE_LOG(LogNodeShuffle, Warning,`n        TEXT(`"RWGTRACEMISS: " }
    @{ Name = 'M13 census repeats a field'; Key = 'diag'; Expect = 'T3'; Find = 'missRecordsXYChanged=%d"'; Repl = 'missRecordsXYChanged=%d attempted=%d"' }
    @{ Name = 'M14 miss line drops an argument'; Key = 'diag'; Expect = 'T3'; Find = 'Ch.PrimsWithBody, Ch.Ms, KillboxTopZ);'; Repl = 'Ch.PrimsWithBody, KillboxTopZ);' }
    @{ Name = 'M15 NoteFirstTrace hook removed'; Key = 'place'; Expect = 'T4'; Find = '    NodeShuffleRwgTraceDiag::NoteFirstTrace('; Repl = '    // NodeShuffleRwgTraceDiag::NoteFirstTrace(' }
    @{ Name = 'M16 a counter reset slipped in before the census'; Key = 'place'; Expect = 'T4'; Find = "    NodeShuffleRwgTraceDiag::EmitPassCensus("; Repl = "    RwgPcAttempted = 0;`n    NodeShuffleRwgTraceDiag::EmitPassCensus(" }
    @{ Name = 'M24 census moved after RwgEmitPlaceCensus'; Key = 'place'; Expect = 'T4'; Find = "    RwgEmitPlaceCensus();`n}"; Repl = "    RwgEmitPlaceCensus();`n    NodeShuffleRwgTraceDiag::EmitPassCensus(RwgPcPasses, RwgPcAttempted, RwgPcSettledTerrain, RwgPcNoTerrainDefer, DeferredThisPass - DeferredBefore); // rwg-trace-diag-1: x`n}" }
    @{ Name = 'M17 event budget raised to 64'; Key = 'diag'; Expect = 'T5'; Find = 'RwgTraceDiagMaxEventsPerPass = 16;'; Repl = 'RwgTraceDiagMaxEventsPerPass = 32;' }
    @{ Name = 'M18 probe moved outside the budget branch'; Key = 'diag'; Expect = 'T5'; Find = '    const int32 Distinct = '; Repl = "    RwgTraceDiagTrace(World, Fresh, X, Y, 0.0, 1.0, Unset);`n    const int32 Distinct = " }
    @{ Name = 'M19 format names a cause'; Key = 'diag'; Expect = 'T6'; Find = 'downHitClass=%s downHitActor'; Repl = 'downHitClass=%s (terrain not stre' + 'amed) downHitActor' }
    @{ Name = 'M20 probe through the settle helper'; Key = 'diag'; Expect = 'T7'; Find = '        FHitResult H;'; Repl = '        FHitResult H; // Raycast' + 'GroundAt(' }
    @{ Name = 'M21 mirror drifts'; Key = 'diag'; Expect = 'T8'; Find = 'RwgDecisionDownCm = 40000.0;'; Repl = 'RwgDecisionDownCm = 30000.0;' }
    @{ Name = 'M22 wide window shrunk'; Key = 'diag'; Expect = 'T9'; Find = 'RwgTraceDiagWideTopZ = 360000.0;'; Repl = 'RwgTraceDiagWideTopZ = 120000.0;' }
    @{ Name = 'M25 control fires on every hit (diag-2 re-target)'; Key = 'diag'; Expect = 'T5'; Find = '        if (World && bTransition)'; Repl = '        if (World)' }
    @{ Name = 'M26 control outside the event budget (diag-2 re-target)'; Key = 'diag'; Expect = 'T5'; Find = '            if (bCtrlRun)'; Repl = '            if (true)' }
    @{ Name = 'MC1 transition ignores the XY'; Key = 'diag'; Expect = 'C1'; Find = 'bTransition = Found->Xcm == RwgTraceDiagCm(X) && Found->Ycm == RwgTraceDiagCm(Y) && Found->Misses >= 1;'; Repl = 'bTransition = true;' }
    @{ Name = 'MC2 pass-modulo schedule back on the control'; Key = 'diag'; Expect = 'C1'; Find = '        if (World && bTransition)'; Repl = ('        if (World && bTransition && (S.Pass ' + '% 12) == 1)') }
    @{ Name = 'MC3 misses no longer recorded'; Key = 'diag'; Expect = 'C1'; Find = "    RwgTraceDiagNoteMiss(Guid, X, Y, S.Pass, PawnDistM);`n    const int32 Prior"; Repl = '    const int32 Prior' }
    @{ Name = 'MC4 record bound raised'; Key = 'diag'; Expect = 'C1'; Find = 'RwgTraceDiagMissRecordBound = 256;'; Repl = 'RwgTraceDiagMissRecordBound = 4096;' }
    @{ Name = 'MC5 eviction dropped'; Key = 'diag'; Expect = 'C1'; Find = "            GRwgMissRecords.Remove(Oldest);`n"; Repl = '' }
    @{ Name = 'MC6 session reset dropped'; Key = 'diag'; Expect = 'C1'; Find = 'if (Pass <= GRwgLastPass) { GRwgMissRecords.Reset(); GRwgMissEvicted = GRwgMissXYChanged = 0; }'; Repl = '' }
    @{ Name = 'MC7 record kept after the hit'; Key = 'diag'; Expect = 'C1'; Find = "            GRwgMissRecords.Remove(Guid);`n"; Repl = '' }
    @{ Name = 'MC8 control line drops a transition argument'; Key = 'diag'; Expect = 'T3'; Find = 'Rec.Misses, Rec.FirstMissPass, Rec.LastMissPass, S.Pass,'; Repl = 'Rec.Misses, Rec.FirstMissPass, S.Pass,' }
    @{ Name = 'MC10 session-restart detector never advances'; Key = 'diag'; Expect = 'C1'; Find = "    GRwgLastPass = Pass;`n"; Repl = '' }
    @{ Name = 'MC11 ctrlLines never counted'; Key = 'diag'; Expect = 'C1'; Find = "            ++S.CtrlLines;`n"; Repl = '' }
    @{ Name = 'MC12 eviction comparison flipped'; Key = 'diag'; Expect = 'C1'; Find = 'KV.Value.Seq < OldestSeq'; Repl = 'KV.Value.Seq > OldestSeq' }
    @{ Name = 'MC13 latest miss not refreshed on a same-XY miss'; Key = 'diag'; Expect = 'C1'; Find = ' R->LastMissPass = Pass; R->DistAtLastMissM = DistM; R->Seq = ++GRwgMissSeq;'; Repl = '' }
    @{ Name = 'MH13 cave/dormant/parked not excluded from the nearest pick'; Key = 'here'; Expect = 'H3'; Find = 'if (E.bRwgCave || DormantThisSession.Contains(E.EntryGuid) || SpawnParkedThisSession.Contains(E.EntryGuid)) { ++Excluded; continue; }'; Repl = '' }
    @{ Name = 'MH14 settled entries not excluded from the nearest pick'; Key = 'here'; Expect = 'H3'; Find = 'if (E.bRayCasted) { ++Excluded; ++ExcludedSettled; continue; }'; Repl = '' }
    @{ Name = 'MH16 settled split not counted'; Key = 'here'; Expect = 'H3'; Find = '{ ++Excluded; ++ExcludedSettled; continue; }'; Repl = '{ ++Excluded; continue; }' }
    @{ Name = 'MH15 excluded entries no longer counted in the bands'; Key = 'here'; Expect = 'H3'; Find = "        ++Band[B];`n        if (E.bRwgCave"; Repl = "        if (E.bRwgCave" }
    @{ Name = 'MC9 control line names a cause'; Key = 'diag'; Expect = 'T6'; Find = 'playerDistNow=%.0f"'; Repl = ('playerDistNow=%.0f (terrain stre' + 'amed in)"') }
    @{ Name = 'MB1 band edge moved'; Key = 'diag'; Expect = 'B1'; Find = 'DistM < 250.0 ? 0'; Repl = 'DistM < 300.0 ? 0' }
    @{ Name = 'MB2 hits not banded'; Key = 'diag'; Expect = 'B1'; Find = "        ++S.BandHits[Band];`n"; Repl = '' }
    @{ Name = 'MB3 band from a 3-D distance'; Key = 'diag'; Expect = 'B1'; Find = 'FVector::DistSquared2D(P, FVector(X, Y, 0.0))'; Repl = 'FVector::DistSquared(P, FVector(X, Y, 0.0))' }
    @{ Name = 'MB4 census prints the hit bands twice'; Key = 'diag'; Expect = 'B1'; Find = '*RwgTraceDiagBandString(S.BandMisses),'; Repl = '*RwgTraceDiagBandString(S.BandHits),' }
    @{ Name = 'MB5 no-pawn folded into the first band'; Key = 'diag'; Expect = 'B1'; Find = 'if (FMath::IsNaN(DistM)) { return 5; }'; Repl = 'if (FMath::IsNaN(DistM)) { return 0; }' }
    @{ Name = 'MH1 HERE bearing re-derived inline'; Key = 'here'; Expect = 'H2'; Find = 'TurnFromFacing(Nearest->Location));'; Repl = 'FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X))));' }
    @{ Name = 'MH2 HERE bearing inverted'; Key = 'here'; Expect = 'H2'; Find = 'FMath::Atan2(D.Y, D.X)) - Pawn'; Repl = ('FMath::Atan2(D.X' + ', D.Y)) - Pawn') }
    @{ Name = 'MH3 well reference bearing changed'; Key = 'sub'; Expect = 'H2'; Find = 'FMath::Atan2(D.Y, D.X)) - Pawn->GetActorRotation().Yaw);'; Repl = 'FMath::Atan2(D.Y, D.X)) + Pawn->GetActorRotation().Yaw);' }
    @{ Name = 'MH4 live entries counted as unspawned'; Key = 'here'; Expect = 'H3'; Find = "        if (Live && IsValid(*Live)) { continue; }`n"; Repl = '' }
    @{ Name = 'MH5 HERE band edge moved'; Key = 'here'; Expect = 'H3'; Find = '{ 25000.0, 50000.0'; Repl = '{ 30000.0, 50000.0' }
    @{ Name = 'MH6 inside-attempt tested against 3000 m'; Key = 'here'; Expect = 'H3'; Find = 'static_cast<double>(NodeShuffleMapMode::RwgAttemptRadiusCm)'; Repl = 'RadiusCm' }
    @{ Name = 'MH7 bands line at Warning'; Key = 'here'; Expect = 'H1'; Find = "UE_LOG(LogNodeShuffle, Display,`n        TEXT(`"HERE-UNSPAWNED-BANDS: "; Repl = "UE_LOG(LogNodeShuffle, Warning,`n        TEXT(`"HERE-UNSPAWNED-BANDS: " }
    @{ Name = 'MH8 bands line drops an argument'; Key = 'here'; Expect = 'H1'; Find = 'Band[4], Band[5], MapWide);'; Repl = 'Band[4], MapWide);' }
    @{ Name = 'MH9 HERE call no longer RWG-gated'; Key = 'sub'; Expect = 'H4'; Find = '    if (GetMapMode() == ENodeShuffleMapMode::Rwg) { LogHereUnspawned('; Repl = '    if (true) { LogHereUnspawned(' }
    @{ Name = 'MH10 TODO dropped above the bands log'; Key = 'here'; Expect = 'T13'; Find = "remove before release.`n    UE_LOG(LogNodeShuffle, Display,`n        TEXT(`"HERE-UNSPAWNED-BANDS: "; Repl = "remove before release.`n`n    UE_LOG(LogNodeShuffle, Display,`n        TEXT(`"HERE-UNSPAWNED-BANDS: " }
    @{ Name = 'MH11 TODO dropped above the control log'; Key = 'diag'; Expect = 'T13'; Find = "remove before release.`n            UE_LOG(LogNodeShuffle, Display, TEXT(`"RWGTRACECTRL: "; Repl = "remove before release.`n`n            UE_LOG(LogNodeShuffle, Display, TEXT(`"RWGTRACECTRL: " }
    @{ Name = 'MH12 marker in the HERE file'; Key = 'here'; Expect = 'V5'; Find = '// rwg-trace-diag-2 (2026-10-28) -- DIAGNOSTICS ONLY.'; Repl = '// LOADED (x) -- DIAGNOSTICS ONLY.' }
    @{ Name = 'MW1 Verbose line back to the cause text'; Key = 'sub'; Expect = 'W1'; Find = '(the settle call returned false; further retries silent)'; Repl = ('(no terrain / out of range; further' + ' retries silent)') }
    @{ Name = 'MW2 Verbose line ungated'; Key = 'sub'; Expect = 'W1'; Find = "                if (FNodeShuffleModule::AreDiagnosticsEnabled())`n                {`n                    UE_LOG(LogNodeShuffle, Verbose,`n                        TEXT(`"Spawn-on-discovery: deferred"; Repl = "                if (true)`n                {`n                    UE_LOG(LogNodeShuffle, Verbose,`n                        TEXT(`"Spawn-on-discovery: deferred" }
    @{ Name = 'MW3 Verbose line raised to Display'; Key = 'sub'; Expect = 'W1'; Find = "UE_LOG(LogNodeShuffle, Verbose,`n                        TEXT(`"Spawn-on-discovery: deferred %s node at %s (the settle"; Repl = "UE_LOG(LogNodeShuffle, Display,`n                        TEXT(`"Spawn-on-discovery: deferred %s node at %s (the settle" }
    @{ Name = 'M27 control line at Warning'; Key = 'diag'; Expect = 'T2'; Find = 'UE_LOG(LogNodeShuffle, Display, TEXT("RWGTRACECTRL:'; Repl = 'UE_LOG(LogNodeShuffle, Warning, TEXT("RWGTRACECTRL:' }
    @{ Name = 'M28 control line drops an argument'; Key = 'diag'; Expect = 'T3'; Find = 'Up.Z, (Down.bHit && Up.bHit'; Repl = '(Down.bHit && Up.bHit' }
    @{ Name = 'M29 box union reverted to last box'; Key = 'diag'; Expect = 'T10'; Find = 'R.CollMinZ = R.ContainColliding == 1 ? BC.Min.Z : FMath::Min(R.CollMinZ, BC.Min.Z);'; Repl = 'R.CollMinZ = BC.Min.Z;' }
    @{ Name = 'M30 unregistered prims counted'; Key = 'diag'; Expect = 'T10'; Find = '!P->IsRegistered() || '; Repl = '' }
    @{ Name = 'M31 physics-body test dropped'; Key = 'diag'; Expect = 'T10'; Find = 'if (P->BodyInstance.IsValidBodyInstance()) { ++R.PrimsWithBody; }'; Repl = '++R.PrimsWithBody;' }
    @{ Name = 'M32 settleReturned not the settle return'; Key = 'place'; Expect = 'T4'; Find = 'RwgProbeFirstZ, bSettled, RwgStuckTracker'; Repl = 'RwgProbeFirstZ, true, RwgStuckTracker' }
    @{ Name = 'M33 32/pass cap raised in MapMode.h'; Key = 'mmh'; Expect = 'D6'; Find = 'constexpr int32 RwgMaxSpawnAttemptsPerPass = 32;'; Repl = 'constexpr int32 RwgMaxSpawnAttemptsPerPass = 33;' }
    @{ Name = 'M34 release TODO dropped above the miss log'; Key = 'diag'; Expect = 'T11'; Find = "remove before release.`n    UE_LOG(LogNodeShuffle, Display,`n        TEXT(`"RWGTRACEMISS: "; Repl = "remove before release.`n`n    UE_LOG(LogNodeShuffle, Display,`n        TEXT(`"RWGTRACEMISS: " }
    @{ Name = 'M35 kinds field renamed back'; Key = 'diag'; Expect = 'T3'; Find = 'firstHitKindsTop3=%s'; Repl = ('firstHitClasses' + 'Top3=%s') }
    @{ Name = 'M36 probeEventsThisPass claim reverted'; Key = 'diag'; Expect = 'T12'; Find = 'this pass (this one included only when probesRun is 1).'; Repl = 'this pass, this one included.' }
    @{ Name = 'M37 bounds arithmetic reverted'; Key = 'diag'; Expect = 'T12'; Find = 'the floor lies below 3x the .key floor -60000 cm'; Repl = '3x the .key floor -60000 cm' }
    @{ Name = 'M38 cave defer edited (widened D1)'; Key = 'place'; Expect = 'D1'; Find = 'if (Entry.bRwgCave) { bOutApiUsed = true; ++RwgPcCaveDeferred; return false; }'; Repl = 'if (Entry.bRwgCave) { bOutApiUsed = true; return false; }' }
    @{ Name = 'M39 settled counter dropped (widened D1)'; Key = 'place'; Expect = 'D1'; Find = '    ++RwgPcSettledTerrain; RwgPcSettledOnSolidTop'; Repl = '    RwgPcSettledOnSolidTop' }
    @{ Name = 'M40 probeMs timer moved outside the traces'; Key = 'diag'; Expect = 'T11'; Find = "        ProbeMs = (FPlatformTime::Seconds() - TProbe) * 1000.0;`n"; Repl = '' }
    @{ Name = 'M41 control moved to the miss path'; Key = 'diag'; Expect = 'T5'; Find = "    ++S.Misses;`n"; Repl = '' }
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
Write-Host "PASS: RWG trace-diag lint -- D1..D6, T1..T13, H1..H4, B1, C1, W1, V5 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
