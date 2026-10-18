# RWG P5 lint (2026-10-18): cave spots on RWG through the bound FPWCarvedCaves group (vanilla share rule, 25 % cap), cave settle
# geometry, settledInsideCave census. LOADS and PINS every source file the P5 packet touched. Anchors are full statements or function
# bodies; needles naming a forbidden literal are BUILT BY CONCATENATION so this file cannot match itself.
#   P1  Cave binding: five decorated names; gated on RwgHeightApiBoundModule (non-null only after the P2b gates); byte signatures with
#       CarvedCaves/Num pinned in full; all-or-nothing (any miss nulls all five); FloorCmAt typed with the disassembled ZHint + double*.
#   P2  HeightApi publishes the module handle only beside bUsable = true, and the accessor answers null unless bUsable.
#   P3  RWGCAVECHECK grade = numOk AND standOk AND floorOk AND nodeSampleNotAllInside; numOk compares the JSON count when it was read.
#   P4  Share = vanilla rule: 0 unless graded usable with num > 0; min(cap, seeds / denominator); mirrors equal the Subsystem constants.
#   P5  GenerateRwgLocations: share from RwgCaveShare(PoolSize) only when cave indices are taken; a failed cave draw falls through to
#       a surface draw in the same iteration; cave picks space in 3D; GenerateNewLocations routes the indices + pool size on RWG.
#   P6  RwgDrawCaveSpot order: AnyNear -> FloorCmAt -> IsInsideAt verify -> headroom -> ceiling -> surface prefilters -> killbox at floor.
#   P7  Cave settle: RwgResolveAtSpawn branches on bRwgCave BEFORE its surface predicates; the trace starts at floor + headroom.
#   P8  SYMMETRY: all three cave-index consumers move the flag to bRwgCave on RWG (roll, relocation, late capture).
#   P9  bRwgCave is SaveGame on the node entry; resettle / rock / sea passes skip it; the overlap nudge and rock dress treat it as cave.
#   P10 Censuses: ROLLCENSUS prints caveSpots as a field (no literal), PLACECENSUS settledInsideCave; settledInsideCave noted on both
#       settle paths; the per-pass counters are zeroed.
#   P11 Marker rwg-p5-1 present once, rwg-p2g-1 absent.
#   P12 No field repeated inside any new/changed format string.
#   P13 Redeal: TryRedealRwgEntry asks RwgTryCaveRedeal first; it mirrors vanilla (liquids excluded, share > 0 before FRand).
# Mutants run in-file. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    cave     = 'NodeShuffle\Private\NodeShuffleMapModeCave.cpp'
    hapi     = 'NodeShuffle\Private\NodeShuffleMapModeHeightApi.cpp'
    place    = 'NodeShuffle\Private\NodeShuffleMapModePlace.cpp'
    resettle = 'NodeShuffle\Private\NodeShuffleMapModeResettle.cpp'
    rock     = 'NodeShuffle\Private\NodeShuffleMapModeRock.cpp'
    sea      = 'NodeShuffle\Private\NodeShuffleMapModeSea.cpp'
    subsys   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    late     = 'NodeShuffle\Private\NodeShuffleLateNodes.cpp'
    nsmod    = 'NodeShuffle\Private\NodeShuffle.cpp'
    r1       = 'NodeShuffle\Private\NodeShuffleMapModeR1.cpp'
    mmh      = 'NodeShuffle\Public\NodeShuffleMapMode.h'
    subsysh  = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
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
    foreach ($n in $needles) { $i = $t.IndexOf($n, [Math]::Max(0, $at)); if ($i -lt 0 -or $i -lt $at) { return $false }; $at = $i + 1 }
    return $true
}
function Fmt-Fields([string]$fmt) { return @([regex]::Matches($fmt, '([A-Za-z]+)=%') | ForEach-Object { $_.Groups[1].Value }) }

function Invoke-P5Checks($T) {
    $f = @()
    # P1
    $c = $T.cave
    foreach ($n in @('?CarvedCaves@FPWDensityField@@QEBAAEBVFPWCarvedCaves@@XZ', '?Num@FPWCarvedCaves@@QEBAHXZ', '?AnyNear@FPWCarvedCaves@@QEBA_NNNN@Z',
                     '?FloorCmAt@FPWCarvedCaves@@QEBA_NNNNAEAN@Z', '?IsInsideAt@FPWCarvedCaves@@QEBA_NAEBU?$TVector@N@Math@UE@@@Z')) {
        if ((Count $c ('"' + $n + '"')) -ne 1) { $f += "(P1) decorated name missing: $n" }
    }
    $tb = Get-Body $c 'void TryBindCaves(FRwgCaveApi& A)'
    if (-not $tb -or -not $tb.Contains('HMODULE H = static_cast<HMODULE>(NodeShuffleMapMode::RwgHeightApiBoundModule());') -or -not $tb.Contains('if (!H) { A.Status = TEXT("heightApiNotBound"); return; }')) { $f += '(P1) the cave group must bind only from the gated HeightApi module' }
    if (-not $tb -or -not $tb.Contains('SigCaves[] = { 0x48,0x8D,0x41,0x28,0xC3 };') -or -not $tb.Contains('SigNum[] = { 0x8B,0x41,0x08,0xC3 };')) { $f += '(P1) CarvedCaves / Num signatures must be pinned in full' }
    if (-not $tb -or -not $tb.Contains('if (!A.Bound()) { A.Caves = nullptr; A.Num = nullptr; A.AnyNear = nullptr; A.Floor = nullptr; A.Inside = nullptr; }')) { $f += '(P1) the cave group must be all-or-nothing' }
    if (-not $c.Contains('using FnFloor = bool (*)(const void* Caves, double X, double Y, double ZHint, double* OutFloor);')) { $f += '(P1) FloorCmAt must be typed (this, X, Y, ZHint, double*) as disassembled' }
    # P2
    if (-not ($T.hapi -match ('A\.Hmod = H;[^\n]*\n\s*A\.bUsable = ' + 'true;'))) { $f += '(P2) Hmod must be published beside bUsable = true only' }
    if (-not $T.hapi.Contains('return A.bUsable ? A.Hmod : nullptr;')) { $f += '(P2) RwgHeightApiBoundModule must answer null unless bUsable' }
    # P3
    $ck = Get-Body $c ($cls + 'RwgEnsureCaveCheck(')
    if (-not $ck) { $f += '(P3) RwgEnsureCaveCheck body not found' } else {
        if (-not $ck.Contains('bRwgCaveUsable = bNumOk && bStandOk && bFloorOk && bNotAllInside;')) { $f += '(P3) the census grade must AND all four controls' }
        if (-not $ck.Contains('const bool bNumOk = Num >= 0 && (JsonStatus != TEXT("read") || JsonCaves == Num);')) { $f += '(P3) numOk must compare the JSON count when read' }
        if (-not $ck.Contains('RWGCAVECHECK: num=%d seed=%lld')) { $f += '(P3) RWGCAVECHECK census line missing' }
        if (-not $ck.Contains('if (FVector::DistSquared(O, L) < FMath::Square(300.0)) { bOurs = true; break; }') -or -not $ck.Contains('if (bOurs) { ++ExcludedOurs; continue; }')) { $f += '(P3) seeds must exclude nodes on our active entries (F2 mirror)' }
        foreach ($bad in @(('num=' + '0 '), ('originalsInsideCave=' + '0 '))) { if ($ck.Contains('TEXT("RWGCAVECHECK: ' + $bad)) { $f += "(P3) a prose line carries the census token $bad" } }
    }
    $rp = Get-Body $T.r1 ($cls + 'RwgLogReprobes(')
    if (-not $rp -or -not $rp.Contains('if (bFull) { const NodeShuffleMapMode::FRwgGroundQuery Q(GetWorld(), RwgGenerator.Get()); RwgEnsureCaveCheck(Q); }')) { $f += '(P3) RWGCAVECHECK must run at readiness (the latch reprobe)' }
    # P4
    $sh = Get-Body $c ($cls + 'RwgCaveShare(')
    if (-not $sh -or -not $sh.Contains('if (!bRwgCaveCheckDone || !bRwgCaveUsable || RwgCaveNumAtCheck <= 0) { return 0.0f; }') -or -not $sh.Contains('static_cast<float>(RwgCaveSeedCount) / FMath::Max(1, Denominator)')) { $f += '(P4) the share must be 0 unless graded usable, else min(cap, seeds / denominator)' }
    foreach ($pair in @(@('RwgCaveHeadroomCmMirror = 350.0;', 'CaveHeadroomCm = 350.0f;'), @('RwgCaveMinCeilingCmMirror = 1200.0;', 'CaveMinPlaceCeilingCm = 1200.0f;'), @('RwgCaveShareCapMirror = 0.25;', 'CaveShareCap = 0.25f;'))) {
        if ((Count $T.mmh $pair[0]) -ne 1 -or (Count $T.subsys $pair[1]) -ne 1) { $f += "(P4) mirror drifted: $($pair[0]) vs $($pair[1])" }
    }
    # P5
    $gl = Get-Body $T.place ($cls + 'GenerateRwgLocations(')
    if (-not $gl) { $f += '(P5) GenerateRwgLocations body not found' } else {
        if (-not $gl.Contains('const float CaveChance = (OutCaveIndices && RwgEnsureCaveCheck(Q)) ? RwgCaveShare(PoolSize) : 0.0f;')) { $f += '(P5) the RWG roll must take the vanilla-rule share' }
        if (-not $gl.Contains('if (CaveChance > 0.0f && !bRwgRcCaveBudgetSpent && Rng.FRand() < CaveChance)')) { $f += '(P5) the cave designation must roll the share' }
        if (-not $gl.Contains('if (!bCavePick && !RwgDealOneSpot(Q, Rng, C, Outside, Water, Disagree, Slope)) { continue; }')) { $f += '(P5) a failed cave draw must fall through to a surface draw' }
        if (-not $gl.Contains('return bCavePick ? FVector::DistSquared(E, C) < SpacingSq : FVector::DistSquared2D(E, C) < SpacingSq;')) { $f += '(P5) cave picks must space in 3D, surface in 2D' }
        if (-not $gl.Contains('if (bCavePick) { OutCaveIndices->Add(OutLocations.Num()); ++RwgRcCaveSpots; }')) { $f += '(P5) cave picks must be reported by index' }
    }
    if (-not $T.subsys.Contains('GenerateRwgLocations(Rng, AvoidLocations, Count, MinNodeSpacing, OutLocations, VanillaLocations.Num() + Count, RwgCaveIndices)')) { $f += '(P5) GenerateNewLocations must route the pool size and cave indices on RWG' }
    # P6
    $dr = Get-Body $c ($cls + 'RwgDrawCaveSpot(')
    if (-not $dr -or -not (Ordered $dr @('Q.CaveAnyNear(X0, Y0, RwgCaveNearRadiusCm)', 'Q.CaveFloorAt(X, Y, SurfZ, Floor)',
        'if (!Q.CaveInsideAt(FVector(X, Y, Floor + RwgCaveInsideProbeCm))) { ++RwgRcCaveRejInside; continue; }',
        'Floor + RwgCaveHeadroomCmMirror', 'Floor + RwgCaveMinCeilingCmMirror - RwgCaveInsideProbeCm', 'RwgRejectV3(Q, FVector(X, Y, SurfZ))',
        'if (bRwgKillboxKnown && Floor < RwgKillboxTopZ + RwgKillboxMarginCm) { ++RwgRcCaveRejKillbox; continue; }', 'OutSpot = FVector(X, Y, Floor);'))) { $f += '(P6) the cave draw predicates are missing or reordered' }
    # P7
    $rs = Get-Body $T.place ($cls + 'RwgResolveAtSpawn(')
    if (-not $rs -or $rs.IndexOf('if (Entry.bRwgCave) { return RwgResolveCaveAtSpawn(Q, Entry, bOutWaterNoLand); }') -lt 0 -or $rs.IndexOf('if (Entry.bRwgCave) { return RwgResolveCaveAtSpawn(') -gt $rs.IndexOf('RwgRejectV3(')) { $f += '(P7) the cave branch must precede the surface predicates' }
    $cs = Get-Body $c ($cls + 'RwgResolveCaveAtSpawn(')
    if (-not $cs -or -not $cs.Contains('const FVector S(Entry.Location.X, Entry.Location.Y, FloorZ + RwgCaveHeadroomCmMirror);') -or -not $cs.Contains('const double FloorZ = Entry.Location.Z;')) { $f += '(P7) the cave settle must trace from floor + headroom' }
    if (-not $cs -or -not $cs.Contains('else if (Cls == 4) { ++RwgPcCaveSolid; bOutWaterNoLand = ' + 'true;') -or -not $cs.Contains('FMath::Abs(Hit.ImpactPoint.Z - FloorZ) > RwgCaveFloorMatchCm')) { $f += '(P7) solid / far hits must redeal' }
    # P8
    $mv = '{ E.bRwgCave = E.bUnderground; E.bUnderground = ' + 'false; }'
    if ((Count $T.subsys $mv) -ne 2 -or (Count $T.late $mv) -ne 1) { $f += '(P8) all three cave-index consumers must move the flag to bRwgCave on RWG' }
    # P9
    if ((Count $T.subsysh 'UPROPERTY(SaveGame) bool bRwgCave = false;') -ne 1) { $f += '(P9) bRwgCave must be SaveGame on the node entry only' }
    if (-not $T.resettle.Contains('E.bRwgSettledOnTerrain || E.bUnderground || E.bRwgCave) { continue; }')) { $f += '(P9) the resettle pass must skip cave entries' }
    if (-not $T.rock.Contains('E.bRwgRockChecked || E.bUnderground || E.bRwgCave) { continue; }')) { $f += '(P9) the rock pass must skip cave entries' }
    if (-not $T.sea.Contains('if (E.bPinned || E.bUnderground || E.bRwgCave) { ++SkipPinned;')) { $f += '(P9) the sea pass must skip cave entries' }
    if (-not $T.subsys.Contains('/*bCaveFlagged=*/Entry.bUnderground || Entry.bRwgCave);') -or -not $T.subsys.Contains('Reuse->bRwgCave = false;')) { $f += '(P9) the inside gate must exempt RWG cave entries and the starter re-home must clear bRwgCave' }
    if (-not $T.subsys.Contains('Entry.bUnderground || Entry.bRwgCave, &bTrySteep,') -or -not $T.subsys.Contains('bUndergroundEntry = E.bUnderground || E.bRwgCave;')) { $f += '(P9) the overlap nudge and rock dress must treat bRwgCave as cave' }
    # P10
    if ((Count $T.place ('caveSpots=' + '0 ')) -ne 0 -or (Count $T.place 'caveSpots=%d caveDrawsTried=%d caveDealFailed=%d') -ne 1) { $f += '(P10) ROLLCENSUS must print caveSpots as a measured field' }
    if ((Count $T.place 'insideCaveChecked=%d settledInsideCave=%d settledInsideCaveSurfaceEntry=%d') -ne 1) { $f += '(P10) PLACECENSUS must carry settledInsideCave' }
    if ((Count $T.place 'RwgNoteSettledInsideCave(Q, Entry);') -ne 1 -or (Count $c 'RwgNoteSettledInsideCave(Q, Entry);') -ne 1) { $f += '(P10) settledInsideCave must be noted on both settle paths' }
    if (-not $T.place.Contains('RwgPcCaveRedealCave = RwgPcCaveRedealSurface = RwgPcInsideCaveChecked = RwgPcSettledInsideCave = RwgPcSettledInsideCaveSurfaceEntry = 0;')) { $f += '(P10) the per-pass cave counters must be zeroed' }
    # P11
    if ((Count $T.nsmod ('LOADED (2026-10-18-rwg-' + 'p5-2)')) -ne 1 -or (Count $T.nsmod ('2026-10-18-rwg-' + 'p5-1')) -ne 0 -or (Count $T.nsmod ('2026-10-18-rwg-' + 'p2g-1')) -ne 0) { $f += '(P11) marker must be rwg-p5-2 with rwg-p5-1 and rwg-p2g-1 absent' }
    # P12
    foreach ($fmt in @([regex]::Matches(($T.cave + $T.place), 'TEXT\("((?:RWGCAVE|ROLLCENSUS: map=rwg deal=api|PLACECENSUS: pass)[^"]*)"\)') | ForEach-Object { $_.Groups[1].Value })) {
        $fl = Fmt-Fields $fmt
        $dup = $fl | Group-Object | Where-Object { $_.Count -gt 1 }
        if ($dup) { $f += "(P12) repeated field(s) $(($dup | ForEach-Object { $_.Name }) -join ',') in '$($fmt.Substring(0, [Math]::Min(40, $fmt.Length)))'" }
    }
    # P13
    $rd = Get-Body $T.place ($cls + 'TryRedealRwgEntry(')
    if (-not $rd -or -not $rd.Contains('if (RwgTryCaveRedeal(Q, Entry, Rng, SpacingCm)) { bOutMoved = true; ++RwgPcRedealApi; return true; }') -or $rd.IndexOf('RwgTryCaveRedeal(') -gt $rd.IndexOf('for (int32 I = 0;')) { $f += '(P13) the RWG redeal must ask the cave rule first' }
    $tc = Get-Body $c ($cls + 'RwgTryCaveRedeal(')
    if (-not $tc -or -not $tc.Contains('if (!bWasCave && (Entry.ResourceForm == 2 || !(Share > 0.0f && Rng.FRand() < Share))) { return false; }')) { $f += '(P13) the cave redeal must mirror the vanilla redeal rule' }
    return $f
}

$fails = Invoke-P5Checks $text
$mutants = @(
    @{ Name = 'M1 cave share ignored on RWG';        Key = 'place'; Expect = 'P5'; Find = '? RwgCaveShare(PoolSize) : 0.0f;'; Repl = '? 0.0f : 0.0f;' }
    @{ Name = 'M2 IsInsideAt verify skipped';         Key = 'cave';  Expect = 'P6'; Find = 'if (!Q.CaveInsideAt(FVector(X, Y, Floor + RwgCaveInsideProbeCm))) { ++RwgRcCaveRejInside; continue; }'; Repl = '' }
    @{ Name = 'M3 cave settle traces from the surface'; Key = 'cave'; Expect = 'P7'; Find = 'const FVector S(Entry.Location.X, Entry.Location.Y, FloorZ + RwgCaveHeadroomCmMirror);'; Repl = 'const FVector S(Entry.Location.X, Entry.Location.Y, Q.SurfaceZ(Entry.Location.X, Entry.Location.Y) + 20000.0);' }
    @{ Name = 'M4 share fill never falls back to surface'; Key = 'place'; Expect = 'P5'; Find = 'if (!bCavePick && !RwgDealOneSpot(Q, Rng, C, Outside, Water, Disagree, Slope)) { continue; }'; Repl = 'if (!bCavePick) { continue; }' }
    @{ Name = 'M5 cave branch removed from the spawn resolver'; Key = 'place'; Expect = 'P7'; Find = 'if (Entry.bRwgCave) { return RwgResolveCaveAtSpawn(Q, Entry, bOutWaterNoLand); }'; Repl = '' }
    @{ Name = 'M6 late capture keeps bUnderground on RWG'; Key = 'late'; Expect = 'P8'; Find = '{ E.bRwgCave = E.bUnderground; E.bUnderground = false; }'; Repl = '{ }' }
    @{ Name = 'M7 rock pass probes cave entries';      Key = 'rock';  Expect = 'P9'; Find = 'E.bUnderground || E.bRwgCave) { continue; }'; Repl = 'E.bUnderground) { continue; }' }
    @{ Name = 'M8 killbox at the floor skipped';       Key = 'cave';  Expect = 'P6'; Find = 'if (bRwgKillboxKnown && Floor < RwgKillboxTopZ + RwgKillboxMarginCm) { ++RwgRcCaveRejKillbox; continue; }'; Repl = '' }
    @{ Name = 'M9 cave bind not all-or-nothing';       Key = 'cave';  Expect = 'P1'; Find = 'if (!A.Bound()) { A.Caves = nullptr; A.Num = nullptr; A.AnyNear = nullptr; A.Floor = nullptr; A.Inside = nullptr; }'; Repl = '' }
    @{ Name = 'M10 census grade drops the JSON count'; Key = 'cave';  Expect = 'P3'; Find = 'bRwgCaveUsable = bNumOk && bStandOk'; Repl = 'bRwgCaveUsable = bStandOk' }
    @{ Name = 'M11 cave redeal rolls FRand at share 0'; Key = 'cave'; Expect = 'P13'; Find = '!(Share > 0.0f && Rng.FRand() < Share)'; Repl = '!(Rng.FRand() < Share)' }
    @{ Name = 'M13 census not run at readiness';       Key = 'r1';    Expect = 'P3'; Find = 'RwgEnsureCaveCheck(Q); }'; Repl = '}' }
    @{ Name = 'M14 inside gate not exempting caves';   Key = 'subsys'; Expect = 'P9'; Find = '/*bCaveFlagged=*/Entry.bUnderground || Entry.bRwgCave);'; Repl = '/*bCaveFlagged=*/Entry.bUnderground);' }
    @{ Name = 'M15 seed count feeds on our spawns';     Key = 'cave';  Expect = 'P3'; Find = 'if (bOurs) { ++ExcludedOurs; continue; }'; Repl = '' }
    @{ Name = 'M12 surface settle census dropped';     Key = 'place'; Expect = 'P10'; Find = 'RwgNoteSettledInsideCave(Q, Entry);'; Repl = '' }
)
$mutantFails = @()
$caught = 0
foreach ($m in $mutants) {
    $mut = @{}
    foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = Invoke-P5Checks $mut
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ }
    else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "RWG P5 LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: RWG P5 lint -- P1..P13 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
