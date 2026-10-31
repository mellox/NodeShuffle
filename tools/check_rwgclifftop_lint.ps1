# RWGCLIFFTOP lint (2026-10-30, packet rwg-clifftop-1): on RWG a node-placement trace that STOPS on a class-4 solid mesh accepts a GENTLE top
# (same 60 deg gate, same-mesh footprint, inside-start guard) as ground and marks the entry bRwgOnSolidTop; the rock pass and the resettle keep
# a marked node whose solid hit is at its own Z (ON) and redeal one with a solid hit well above it (UNDER); occluded spots run the local ring
# before the map-wide redeal; PLACEDZ keeps solid-top settles out of the terrain bands. Handoff: _team/test-logs/2026-10-30-nodeshuffle-clifftop-handoff.md.
# LOADS and PINS every source file the packet touched. Needles naming a forbidden or superseded literal are BUILT BY CONCATENATION.
#   C1  Inside-start guard: three predicates (bStartPenetrating false, ImpactNormal.Z > 0, impact below the TRACE START minus the named margin),
#       all three in the guard, the guard in the accept; the call passes the trace start (Start.Z), never the window origin.
#   C2  Footprint + gentle: 4 outer probes at the terrain smoothing ring; each must stop on the SAME component and instance item; all 4 required;
#       the smoothed normal is tested against the terrain gate (MinSettleNormalZ); an accepted top is water-tested.
#   C3  Scope: the accept is offered only when armed and only on long traces, BEFORE the P2d refusal; armed in exactly one function, called from
#       RwgResolveAtSpawn (settle) and RwgLocalRedraw (ring) only, disarmed right after each; wells / resettle / rock never arm it.
#   C4  Marker: one SaveGame bool on the entry; RaycastSettle writes it from the probe's own signal on both success paths; cleared at every
#       resolve, by the overlap nudge, by the cave settle and by RwgRedealLiveEntry; the signal is reset by every RaycastGroundAt.
#   C5  Consumers (SYMMETRY): the resettle and the rock pass each test ON (marker AND hit within RwgSolidTopOnTolCm of the node Z) BEFORE their
#       redeal; the rock probe reports the solid hit Z.
#   C6  Ring on occluded: one unconditional RwgLocalRedraw call precedes the occluded/steep outcome split.
#   C7  PLACEDZ split: the resolver's dz sample goes to RwgRecordSolidTopDz when marked, RwgRecordDz otherwise; PLACEDZ prints both, arity.
#   C8  RejectedRock: an accepted top never increments RwgProbeSolidHits (the refusal still does, after the accept).
#   C9  Start height: the named float constant, static_assert-equal to the rock window; the literal is gone from the trace.
#   C10 Diagnostics: RWGCLIFFTOP exact format + arity, capped per pass and zeroed with the PLACECENSUS counters; PLACECENSUS carries the fields.
#   C11 Vanilla: the bSolidBlocks gate line is unchanged; the arm call sites live only in RWG-only files.
#   C12 Marker rwg-clifftop-1 present once, rwg-layout-dump-1 absent; no marker string in the new file.
#   C13 (review F1b) LayoutDump.cpp and Density.cpp are loaded: E-row purity argument live; RWGSTUCK moved token unplaceableNew.
# Mutants run in-file. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    sub    = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    subh   = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    mmh    = 'NodeShuffle\Public\NodeShuffleMapMode.h'
    cliff  = 'NodeShuffle\Private\NodeShuffleMapModeCliffTop.cpp'
    place  = 'NodeShuffle\Private\NodeShuffleMapModePlace.cpp'
    steep  = 'NodeShuffle\Private\NodeShuffleMapModeSteep.cpp'
    rset   = 'NodeShuffle\Private\NodeShuffleMapModeResettle.cpp'
    rock   = 'NodeShuffle\Private\NodeShuffleMapModeRock.cpp'
    wfoot  = 'NodeShuffle\Private\NodeShuffleWellFootprint.cpp'
    nsmod  = 'NodeShuffle\Private\NodeShuffle.cpp'
    dump   = 'NodeShuffle\Private\NodeShuffleLayoutDump.cpp'
    dens   = 'NodeShuffle\Private\NodeShuffleMapModeDensity.cpp'
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
            if ($c -eq '/' -and $i + 1 -lt $t.Length -and $t[$i + 1] -eq '/') { $nl = $t.IndexOf("`n", $i); if ($nl -lt 0) { break }; $i = $nl - 1; continue } # review r2: a // comment inside the argument list is not an argument
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

$CliffFmt = 'RWGCLIFFTOP: sample=%d via=%s entry=%s at=%.0f/%.0f res=%s hitMesh=%s smoothedRiseRun=%.2f hitZ=%.0f apiZ=%.0f dzCm=%.0f startZ=%.0f startPenetrating=%d normalZ=%.3f smoothedNormalZ=%.3f gateNormalZ=%.3f outerProbesSameMesh=%d/4 steepOuterProbes=%d accepted=%d rejectReason=%s aboveComponent=%s'

function Invoke-CliffChecks($T) {
    $f = @(); $c = $T.cliff; $s = $T.sub; $p = $T.place
    # C1 inside-start guard
    if ((Count $c 'const bool bStartPen = Hit.bStartPenetrating;') -ne 1 -or (Count $c 'const bool bNormalUp = Hit.ImpactNormal.Z > 0.0;') -ne 1 -or (Count $c 'const bool bBelowStart = P.Z < TraceStartZ - NodeShuffleMapMode::RwgSolidTopStartMarginCm;') -ne 1) { $f += '(C1) the three inside-start predicates must be measured on the hit against the trace start minus the named margin' }
    if ((Count $c 'const bool bGuard = !bStartPen && bNormalUp && bBelowStart;') -ne 1 -or (Count $c 'const bool bAccepted = bGuard && bClearAbove && bFootprint && bGentle;') -ne 1 -or (Count $c '    if (!bAccepted) { return false; }') -ne 1) { $f += '(C1) the guard must AND all three predicates and gate the accept' }
    if ((Count $c 'const bool bClearAbove = RwgRockOcclusionProbe(P, nullptr, AboveMesh) != 1;') -ne 1 -or (Count $c '    else if (!bClearAbove) { ++RwgPcSolidTopRejStartInside; Reject = TEXT("solidAbove"); }') -ne 1) { $f += '(C1) the accept must test the rock-pass window above the HIT point and report solidAbove' }
    if ((Count $T.mmh 'constexpr double RwgSolidTopStartMarginCm = 100.0;') -ne 1) { $f += '(C1) the inside-start margin must be the named constant (100 cm)' }
    if ((Count $s '&& RwgSolidTopTryAccept(Hit, Start.Z, Params, MinSettleNormalZ, SmoothNormalRingCm, NodeTiltClampDeg, bSkippedWaterSheet, OutLoc, OutRot, bOutWater,') -ne 1) { $f += '(C1/C2) the accept must receive the trace START Z, the terrain gate, the terrain smoothing ring and the tilt clamp' }
    # C2 footprint + gentle + water
    if ((Count $c 'for (int32 I = 0; I < 4 && World; ++I)') -ne 1 -or (Count $c 'if (bStopped && HitComp && R.GetComponent() == HitComp && R.Item == Hit.Item) { ++SameMesh; Acc += R.ImpactNormal; SameMeshSteep += (R.ImpactNormal.Z < MinNormalZ) ? 1 : 0; }') -ne 1 -or (Count $c 'const bool bFootprint = SameMesh == 4 && SameMeshSteep == 0;') -ne 1) { $f += '(C2) all 4 outer probes must stop on the SAME component and instance item (mixed footprint fails)' }
    if ((Count $c 'if (Cls == 0 || Cls == 4 || (Owner && (Owner->IsA<AFGBuildable>()') -ne 1 -or (Count $c 'RingParams.AddIgnoredComponent(R.GetComponent());') -ne 1) { $f += '(C2) an outer probe must stop on terrain, solid or a buildable and step through skip-class hits only' }
    if ((Count $c 'const bool bGentle = N.Z >= MinNormalZ;') -ne 1 -or (Count $c 'FVector Acc = Hit.ImpactNormal;') -ne 1) { $f += '(C2) the smoothed normal (hit + outer normals) must meet the terrain cliff gate' }
    if ((Count $c 'bOutWater = bSkippedWater || IsPointInWater(OutLoc);') -ne 1) { $f += '(C2) an accepted top must be water-tested (volume + skipped sheet)' }
    # C3 scope
    $acc = "    if (bPrimarySolid && bRwgSolidTopArmed && !bShortTrace`n"
    if ((Count $s $acc) -ne 1 -or -not (Ordered $s @('bool ANodeShuffleSubsystem::RaycastGroundAt(', $acc, "    {`n        return true;`n    }`n    if (bPrimarySolid)`n    {", 'bool ANodeShuffleSubsystem::RaycastSettle('))) { $f += '(C3) the accept must be armed-only, long-trace-only and precede the P2d refusal' }
    $all = $T.sub + $T.place + $T.steep + $T.rset + $T.rock + $T.wfoot + $T.cliff
    if ((Count $all ('bRwgSolidTopArmed = ' + 'true;')) -ne 1 -or (Count $c ('bRwgSolidTopArmed = ' + 'true;')) -ne 1) { $f += '(C3) the arm is set in exactly one place (RwgSolidTopArm)' }
    if ((Count $p 'RwgSolidTopArm(Entry, TEXT("settle"));') -ne 1 -or (Count $T.steep 'RwgSolidTopArm(Entry, TEXT("ring"));') -ne 1 -or (Count $all 'RwgSolidTopArm(') -ne 3) { $f += '(C3) RwgSolidTopArm is called only by the resolver settle and the ring' }
    if ((Count $p 'const bool bSettled = RaycastSettle(Entry, nullptr, nullptr, &bOutWaterNoLand, /*bTerrainOnly=*/true); bRwgSolidTopArmed = false;') -ne 1 -or (Count $T.steep 'ON_SCOPE_EXIT { bRwgSolidTopArmed = false; };') -ne 1) { $f += '(C3) each arm must be cleared right after its traces' }
    if (-not (Ordered $p @('RwgSolidTopArm(Entry, TEXT("settle"));', 'RwgProbeSolidHits = 0;', 'const bool bSettled = RaycastSettle('))) { $f += '(C3) the settle arm must be set before the settle' }
    if ((Count ($T.wfoot + $T.rset + $T.rock) 'RwgSolidTopArm') -ne 0) { $f += '(C3) wells, resettle and the rock pass never arm the accept' }
    # C4 marker
    if ((Count $T.subh 'UPROPERTY(SaveGame) bool bRwgOnSolidTop = false;') -ne 1) { $f += '(C4) bRwgOnSolidTop must be one SaveGame property' }
    if ((Count $s 'Entry.bRwgOnSolidTop = bRwgLastProbeTopAccepted;') -ne 2 -or -not (Ordered $s @('bool ANodeShuffleSubsystem::RaycastSettle(', 'Entry.bRwgOnSolidTop = false; // rwg-clifftop-1: short (cave) traces', 'Entry.bRwgOnSolidTop = bRwgLastProbeTopAccepted;', 'Entry.Location = TryLoc; // persist the corrected location', 'Entry.bRwgOnSolidTop = bRwgLastProbeTopAccepted;', 'bool ANodeShuffleSubsystem::TryRedealWaterLockedEntry('))) { $f += '(C4) RaycastSettle must write the marker on the cave, primary and spiral success paths' }
    if ((Count $p 'bOutWaterNoLand = false; bRwgSteepArmed = false; Entry.bRwgOnSolidTop = false;') -ne 1) { $f += '(C4) every resolve must clear the marker first' }
    if ((Count $s "bRelocated = true;`n                    Entry.bRwgOnSolidTop = false;") -ne 1) { $f += '(C4) the overlap nudge must clear the marker' }
    if ((Count $T.rock "    E.bRwgSettledOnTerrain = false;`n    E.bRwgOnSolidTop = false;") -ne 1) { $f += '(C4) RwgRedealLiveEntry must clear the marker' }
    if ((Count $s 'bRwgLastProbeSolid = false; bRwgLastProbeTopAccepted = false;') -ne 1 -or (Count $all ('bRwgLastProbeTopAccepted = ' + 'true;')) -ne 1 -or -not (Ordered $c @('    if (!bAccepted) { return false; }', ('bRwgLastProbeTopAccepted = ' + 'true;')))) { $f += '(C4) the accept signal must be reset by every probe and set only after the accept' }
    # C5 consumers
    $rsOn = 'if (E.bRwgOnSolidTop && FMath::Abs(HitLoc.Z - Cur.Z) <= NodeShuffleMapMode::RwgSolidTopOnTolCm)'
    if ((Count $T.rset $rsOn) -ne 1 -or -not (Ordered $T.rset @('        if (bRwgLastProbeSolid)', $rsOn, '++KeptOnSolidTop;', 'RwgResettleGaveUp.Add(E.EntryGuid);', 'continue;', 'RwgRedealLiveEntry(E, TEXT("resettle probe stopped on a solid mesh"))'))) { $f += '(C5) the resettle must keep an ON solid-top node BEFORE its solid-stop redeal' }
    $rkOn = 'if (E.bRwgOnSolidTop && FMath::Abs(HitDzCm) <= NodeShuffleMapMode::RwgSolidTopOnTolCm) { ++OnSolidTopKept; E.bRwgRockChecked = true; continue; }'
    if ((Count $T.rock $rkOn) -ne 1 -or -not (Ordered $T.rock @('RwgRockOcclusionProbe(Node->GetActorLocation(), Node, Mesh, &HitZ);', 'const double HitDzCm = HitZ - Node->GetActorLocation().Z;', $rkOn, '++Occluded;', 'RwgRedealLiveEntry(E, TEXT("rock-occluded"))'))) { $f += '(C5) the rock pass must keep an ON solid-top node BEFORE counting it occluded and redealing' }
    if (-not (Ordered $T.rock @('if (Cls == 4)', 'if (OutHitZ) { *OutHitZ = H.ImpactPoint.Z; }', 'return 1;'))) { $f += '(C5) the rock probe must report the solid hit Z' }
    if ((Count $T.mmh 'constexpr double RwgSolidTopOnTolCm = 100.0;') -ne 1) { $f += '(C5) the ON tolerance must be the named constant (100 cm)' }
    # C6 ring on occluded
    $call = '    const bool bMoved = RwgLocalRedraw(Q, Entry, SpacingCm, RadiusCm, Tried, Passed, RejLand, RejSpacing, RejTrace);'
    if ((Count $T.steep $call) -ne 1 -or -not (Ordered $T.steep @($call, '    if (bRwgSteepOccluded)', 'Outcome = TEXT("occludedRing");', 'Outcome = TEXT("occludedMapwide");'))) { $f += '(C6) occluded entries must run the ring (unconditional call before the outcome split)' }
    # C7 PLACEDZ split
    if ((Count $p 'if (Entry.bRwgOnSolidTop) { RwgRecordSolidTopDz(Entry.RwgApiToTerrainDzCm); } else { RwgRecordDz(Entry.Location, Entry.RwgApiToTerrainDzCm); }') -ne 1 -or (Count $p 'RwgRecordDz(') -ne 1) { $f += '(C7) the resolver must split its dz sample by the marker' }
    $dz = @(Get-Logs $T.rset 'PLACEDZ: ')
    if ($dz.Count -ne 1 -or -not $dz[0].Fmt.EndsWith(' solidTopSettlesExcluded=%d solidTopMaxAbsDzCm=%.0f') -or (Spec-Count $dz[0].Fmt) -ne $dz[0].Args.Count -or (($dz[0].Args | Select-Object -Last 2) -join ',') -ne 'RwgDzSolidTopN,RwgDzSolidTopMaxAbsCm') { $f += '(C7) PLACEDZ must print the solid-top count and max after the bands (arity)' }
    if ((Count $T.rset "    ++RwgDzSolidTopN;`n    RwgDzSolidTopMaxAbsCm = FMath::Max(") -ne 1) { $f += '(C7) RwgRecordSolidTopDz must count, never band' }
    # C8 rejected-rock accounting
    if ((Count $c 'RwgProbeSolidHits') -ne 0 -or -not (Ordered $s @($acc, "        return true;`n    }`n    if (bPrimarySolid)", 'bRwgLastProbeSolid = true;', '++RwgProbeSolidHits;'))) { $f += '(C8) an accepted top must not count as a solid rejection; the refusal still counts after the accept' }
    # C9 start height
    if ((Count $T.mmh 'constexpr float RwgPrimaryTraceUpCm = 20000.0f;') -ne 1 -or (Count $T.mmh 'static_assert(static_cast<double>(RwgPrimaryTraceUpCm) == RwgRockOcclusionHeightCm,') -ne 1) { $f += '(C9) the start height must be the named float constant, static_assert-equal to the rock window' }
    if ((Count $s ('StartZ + ' + '20000.f')) -ne 0 -or (Count $s 'bShortTrace ? StartZ + 400.f : StartZ + NodeShuffleMapMode::RwgPrimaryTraceUpCm);') -ne 1) { $f += '(C9) the trace start must use the named constant' }
    # C10 diagnostics
    $cl = @(Get-Logs $c 'RWGCLIFFTOP: ')
    if ($cl.Count -ne 1 -or $cl[0].Fmt -ne $CliffFmt -or $cl[0].Level -ne 'Display' -or (Spec-Count $cl[0].Fmt) -ne $cl[0].Args.Count -or $cl[0].Args.Count -ne 21) { $f += '(C10) RWGCLIFFTOP format drifted or arity is wrong' }
    if ((Count $c 'constexpr int32 RwgCliffTopSampleCap = 8;') -ne 1 -or (Count $c 'if (RwgCliffTopSamples < RwgCliffTopSampleCap && FNodeShuffleModule::AreDiagnosticsEnabled())') -ne 1 -or (Count $p 'RwgPcSolidTopRejFootprint = RwgCliffTopSamples = 0;') -ne 1) { $f += '(C10) RWGCLIFFTOP must be capped per pass and the cap zeroed with the PLACECENSUS counters' }
    foreach ($n in @('settledOnSolidTop=%d', 'solidTopAcceptedProbes=%d', 'solidTopRejectedGuardOrAbove=%d', 'solidTopRejectedNormal=%d', 'solidTopRejectedFootprint=%d')) { if ((Count $p $n) -ne 1) { $f += "(C10) PLACECENSUS must carry $n once" } }
    # C11 vanilla
    if ((Count $s 'const bool bSolidBlocks = bTerrainOnly && bOutTooSteep != nullptr;') -ne 1) { $f += '(C11) the RWG-only bSolidBlocks gate must be unchanged' }
    # C13 (review F1b): the two packet-touched files this lint did not load. The LAYOUTDUMP-E purity argument stays CODE (not swallowed by a
    # line comment); the stuck-tracker moved token carries the renamed field and not the old name.
    if ((Count $T.dump 'I, *E.EntryGuid.ToString(), *LayoutDumpShortName(E.AssignedResourceClassPath), static_cast<int32>(E.AssignedPurity.GetValue()),') -ne 1) { $f += '(C13) the LAYOUTDUMP-E purity argument must be live code' }
    if ((Count $T.dens 'Moved += TEXT("unplaceableNew"); }') -ne 1 -or (Count $T.dens ('Moved += TEXT("water' + 'LockedNew")')) -ne 0) { $f += '(C13) the RWGSTUCK moved token must be unplaceableNew' }
    # C12 marker
    if ((Count $T.nsmod ('LOADED (2026-10-30-rwg-' + 'clifftop-1)')) -ne 1 -or (Count $T.nsmod ('2026-10-29-rwg-' + 'layout-dump-1)')) -ne 0) { $f += '(C12) marker rwg-clifftop-1 must be present once and rwg-layout-dump-1 absent' }
    if ($c.Contains('LOADED' + ' (')) { $f += '(C12) no build-marker string in the new file' }
    return $f
}

$fails = Invoke-CliffChecks $text
$mutants = @(
    @{ Name = 'M1 start-penetrating ignored'; Key = 'cliff'; Expect = 'C1'; Find = 'const bool bGuard = !bStartPen && bNormalUp && bBelowStart;'; Repl = 'const bool bGuard = bNormalUp && bBelowStart;' }
    @{ Name = 'M2 margin dropped'; Key = 'cliff'; Expect = 'C1'; Find = 'P.Z < TraceStartZ - NodeShuffleMapMode::RwgSolidTopStartMarginCm;'; Repl = 'P.Z < TraceStartZ;' }
    @{ Name = 'M3 guard against the window origin'; Key = 'sub'; Expect = 'C1'; Find = 'RwgSolidTopTryAccept(Hit, Start.Z,'; Repl = 'RwgSolidTopTryAccept(Hit, StartZ,' }
    @{ Name = 'M4 footprint averaged'; Key = 'cliff'; Expect = 'C2'; Find = 'const bool bFootprint = SameMesh == 4 && SameMeshSteep == 0;'; Repl = 'const bool bFootprint = SameMesh >= 1;' }
    @{ Name = 'M5 footprint any component'; Key = 'cliff'; Expect = 'C2'; Find = 'R.GetComponent() == HitComp && R.Item == Hit.Item'; Repl = 'R.GetComponent() != nullptr' }
    @{ Name = 'M6 gentle gate loosened'; Key = 'cliff'; Expect = 'C2'; Find = 'const bool bGentle = N.Z >= MinNormalZ;'; Repl = 'const bool bGentle = N.Z >= 0.0f;' }
    @{ Name = 'M7 wet top accepted'; Key = 'cliff'; Expect = 'C2'; Find = 'bOutWater = bSkippedWater || IsPointInWater(OutLoc);'; Repl = 'bOutWater = false;' }
    @{ Name = 'M8 accept offered to every caller'; Key = 'sub'; Expect = 'C3'; Find = 'if (bPrimarySolid && bRwgSolidTopArmed && !bShortTrace'; Repl = 'if (bPrimarySolid && !bShortTrace' }
    @{ Name = 'M9 settle arm never cleared'; Key = 'place'; Expect = 'C3'; Find = '/*bTerrainOnly=*/true); bRwgSolidTopArmed = false;'; Repl = '/*bTerrainOnly=*/true);' }
    @{ Name = 'M10 well probe armed'; Key = 'wfoot'; Expect = 'C3'; Find = 'if (bSteep) { if (bRwgLastProbeSolid) {'; Repl = 'RwgSolidTopArm(FNodeShuffleEntry(), TEXT("well")); if (bSteep) { if (bRwgLastProbeSolid) {' }
    @{ Name = 'M11 marker not saved'; Key = 'subh'; Expect = 'C4'; Find = 'UPROPERTY(SaveGame) bool bRwgOnSolidTop = false;'; Repl = 'bool bRwgOnSolidTop = false;' }
    @{ Name = 'M12 redeal keeps the marker'; Key = 'rock'; Expect = 'C4'; Find = "    E.bRwgSettledOnTerrain = false;`n    E.bRwgOnSolidTop = false;"; Repl = '    E.bRwgSettledOnTerrain = false;' }
    @{ Name = 'M13 nudge keeps the marker'; Key = 'sub'; Expect = 'C4'; Find = "bRelocated = true;`n                    Entry.bRwgOnSolidTop = false;"; Repl = 'bRelocated = true;' }
    @{ Name = 'M14 resettle ON test removed'; Key = 'rset'; Expect = 'C5'; Find = 'if (E.bRwgOnSolidTop && FMath::Abs(HitLoc.Z - Cur.Z) <= NodeShuffleMapMode::RwgSolidTopOnTolCm)'; Repl = 'if (false)' }
    @{ Name = 'M15 rock ON test widened to the window'; Key = 'rock'; Expect = 'C5'; Find = 'FMath::Abs(HitDzCm) <= NodeShuffleMapMode::RwgSolidTopOnTolCm'; Repl = 'FMath::Abs(HitDzCm) <= NodeShuffleMapMode::RwgRockOcclusionHeightCm' }
    @{ Name = 'M16 rock ON test without the marker'; Key = 'rock'; Expect = 'C5'; Find = 'if (E.bRwgOnSolidTop && FMath::Abs(HitDzCm)'; Repl = 'if (FMath::Abs(HitDzCm)' }
    @{ Name = 'M17 occluded entries skip the ring'; Key = 'steep'; Expect = 'C6'; Find = '    const bool bMoved = RwgLocalRedraw(Q, Entry, SpacingCm, RadiusCm,'; Repl = '    const bool bMoved = !bRwgSteepOccluded && RwgLocalRedraw(Q, Entry, SpacingCm, RadiusCm,' }
    @{ Name = 'M18 PLACEDZ merged'; Key = 'place'; Expect = 'C7'; Find = 'if (Entry.bRwgOnSolidTop) { RwgRecordSolidTopDz(Entry.RwgApiToTerrainDzCm); } else { RwgRecordDz(Entry.Location, Entry.RwgApiToTerrainDzCm); }'; Repl = 'RwgRecordDz(Entry.Location, Entry.RwgApiToTerrainDzCm);' }
    @{ Name = 'M19 accepted top counted as rock'; Key = 'cliff'; Expect = 'C8'; Find = '    ++RwgPcSolidTopAccepted;'; Repl = '    ++RwgPcSolidTopAccepted; ++RwgProbeSolidHits;' }
    @{ Name = 'M20 literal start height'; Key = 'sub'; Expect = 'C9'; Find = 'StartZ + NodeShuffleMapMode::RwgPrimaryTraceUpCm);'; Repl = ('StartZ + ' + '20000.f);') }
    @{ Name = 'M21 RWGCLIFFTOP drops a field'; Key = 'cliff'; Expect = 'C10'; Find = ' outerProbesSameMesh=%d/4 steepOuterProbes=%d accepted=%d'; Repl = ' accepted=%d' }
    @{ Name = 'M22 RWGCLIFFTOP uncapped'; Key = 'cliff'; Expect = 'C10'; Find = 'if (RwgCliffTopSamples < RwgCliffTopSampleCap && '; Repl = 'if (' }
    @{ Name = 'M23 marker reverted'; Key = 'nsmod'; Expect = 'C12'; Find = ('2026-10-30-rwg-' + 'clifftop-1'); Repl = ('2026-10-29-rwg-' + 'layout-dump-1') }
    @{ Name = 'M25 vanilla solid gate widened'; Key = 'sub'; Expect = 'C11'; Find = 'const bool bSolidBlocks = bTerrainOnly && bOutTooSteep != nullptr;'; Repl = 'const bool bSolidBlocks = bOutTooSteep != nullptr;' }
    @{ Name = 'M26 LayoutDump E purity argument commented out'; Key = 'dump'; Expect = 'C13'; Find = '*LayoutDumpShortName(E.AssignedResourceClassPath), static_cast<int32>(E.AssignedPurity.GetValue()),'; Repl = ('*LayoutDumpShortName(E.AssignedResourceClassPath), ' + '/' + '/ static_cast<int32>(E.AssignedPurity.GetValue()),') }
    @{ Name = 'M27 stuck moved token back to the old name'; Key = 'dens'; Expect = 'C13'; Find = 'Moved += TEXT("unplaceableNew"); }'; Repl = ('Moved += TEXT("water' + 'LockedNew"); }') }
    @{ Name = 'M28 accept drops the rock-window clear test'; Key = 'cliff'; Expect = 'C1'; Find = 'const bool bAccepted = bGuard && bClearAbove && bFootprint && bGentle;'; Repl = 'const bool bAccepted = bGuard && bFootprint && bGentle;' }
    @{ Name = 'M29 footprint ignores a steep same-mesh probe'; Key = 'cliff'; Expect = 'C2'; Find = 'const bool bFootprint = SameMesh == 4 && SameMeshSteep == 0;'; Repl = 'const bool bFootprint = SameMesh == 4;' }
    @{ Name = 'M33 RWGCLIFFTOP arguments swallowed by a line comment'; Key = 'cliff'; Expect = 'C10'; Find = '*RwgSolidTopGuid.ToString().Left(8), P.X, P.Y, *RwgCliffTopShort(RwgSolidTopRes),'; Repl = ('*RwgSolidTopGuid.ToString().Left(8), ' + '/' + '/ at: P.X, P.Y, *RwgCliffTopShort(RwgSolidTopRes),') }
    @{ Name = 'M30 clear-above test forced true'; Key = 'cliff'; Expect = 'C1'; Find = 'const bool bClearAbove = RwgRockOcclusionProbe(P, nullptr, AboveMesh) != 1;'; Repl = 'const bool bClearAbove = true;' }
    @{ Name = 'M31 clear-above window origin moved off the hit'; Key = 'cliff'; Expect = 'C1'; Find = 'RwgRockOcclusionProbe(P, nullptr, AboveMesh) != 1;'; Repl = 'RwgRockOcclusionProbe(FVector(P.X, P.Y, TraceStartZ), nullptr, AboveMesh) != 1;' }
    @{ Name = 'M32 solidAbove reject branch removed'; Key = 'cliff'; Expect = 'C1'; Find = '    else if (!bClearAbove) { ++RwgPcSolidTopRejStartInside; Reject = TEXT("solidAbove"); }'; Repl = '' }
    @{ Name = 'M24 accept signal set before the gate'; Key = 'cliff'; Expect = 'C4'; Find = '    const bool bAccepted = bGuard && bClearAbove && bFootprint && bGentle;'; Repl = ('    const bool bAccepted = bGuard && bClearAbove && bFootprint && bGentle; bRwgLastProbeTopAccepted = ' + 'true;') }
)
$mutantFails = @(); $caught = 0
foreach ($m in $mutants) {
    $mut = @{}; foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = Invoke-CliffChecks $mut
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" -or $_ -like "*($($m.Expect)/*" -or $_ -like "*/$($m.Expect))*" }) { $caught++ } else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "RWGCLIFFTOP LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: RWGCLIFFTOP lint -- C1..C13 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
