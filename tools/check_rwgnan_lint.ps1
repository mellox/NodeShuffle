# RWG NaN lint (2026-11-13, packet rwg-0-2-0-2; handoff _team/test-logs/2026-11-13-nodeshuffle-rwg-0-2-0-2-handoff.md).
# Why: the module compiles with /fp:fast (design _team/test-logs/2026-11-13-nodeshuffle-rwg-0-2-0-2-design.md Q4). There, a compare whose
# operand is NaN is NOT IEEE (the deployed DLL's `!=` against a NaN sentinel had no parity branch and read false), so every value that can
# be NaN must pass FMath::IsFinite / FMath::IsNaN BEFORE any relational or equality compare. Needles that name a NaN producer are BUILT BY
# CONCATENATION so this file cannot match itself.
#   N1 Module-wide (every .cpp/.h under Source/NodeShuffle): no relational/equality compare DIRECTLY against a NaN producer; and no variable
#      or member initialised to a NaN producer is compared on a line that does not also test it with IsFinite / IsNaN.
#   N2 The EDGESTRIP gate keys on bEdgeLogged (members start at 0.0, the flag false; set before the print).
#   N3 The fixed decision / census sites test with FMath::IsFinite before their compare: RwgRejectV3 (a non-finite surface takes bit 16),
#      RwgWetV2 bit 4 (both sides), the slope test (non-finite = too steep), the cave verify (Floor and SurfZ), the cave check's floor
#      control (Fz), the height-API census water split (WaterZ).
#   N4 RWGNANGUARD: declared in NodeShuffleMapMode.h, defined once, prints the first 4 per site, Warning, tag first.
# Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$files = [ordered]@{
    hapi  = 'Source\NodeShuffle\Private\NodeShuffleMapModeHeightApi.cpp'
    void  = 'Source\NodeShuffle\Private\NodeShuffleMapModeVoid.cpp'
    sea   = 'Source\NodeShuffle\Private\NodeShuffleMapModeSea.cpp'
    place = 'Source\NodeShuffle\Private\NodeShuffleMapModePlace.cpp'
    cjson = 'Source\NodeShuffle\Private\NodeShuffleMapModeCaveJson.cpp'
    cave  = 'Source\NodeShuffle\Private\NodeShuffleMapModeCave.cpp'
    mmh   = 'Source\NodeShuffle\Public\NodeShuffleMapMode.h'
}
$T = [ordered]@{}
foreach ($k in $files.Keys) {
    $p = Join-Path $root $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $T[$k] = (Get-Content -Raw -Path $p) -replace "`r`n", "`n"
}
$all = [ordered]@{}
foreach ($f in Get-ChildItem -Path (Join-Path $root 'Source\NodeShuffle') -Recurse -Include *.cpp, *.h) { $all[$f.Name] = (Get-Content -Raw -Path $f.FullName) -replace "`r`n", "`n" }
function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }
function Span([string]$t, [string]$a, [string]$b) { $i = $t.IndexOf($a); if ($i -lt 0) { return '' }; $j = $t.IndexOf($b, $i + $a.Length); if ($j -lt 0) { return '' }; return $t.Substring($i, $j - $i) }
function Ordered([string]$t, [string[]]$needles) { $at = -1; foreach ($n in $needles) { $i = $t.IndexOf($n, $at + 1); if ($i -lt 0) { return $false }; $at = $i }; return $true }
$qn = 'std::numeric_limits<double>::' + 'quiet_' + 'NaN'
$rn = 'Rwg' + 'NaN'
$prod = '(?:NodeShuffleMapMode::)?(?:' + [regex]::Escape($qn) + '|' + $rn + ')\(\)'
$cmp = '(?:==|!=|<=|>=|<|>)'

function Invoke-N1($all) {
    $f = @()
    foreach ($name in $all.Keys) {
        $t = $all[$name]
        foreach ($m in [regex]::Matches($t, $cmp + '\s*' + $prod)) { $f += "(N1) $name compares directly against a NaN producer: '$($m.Value)'" }
        foreach ($m in [regex]::Matches($t, $prod + '\s*' + $cmp)) { $f += "(N1) $name compares a NaN producer directly: '$($m.Value)'" }
        $names = @{}
        foreach ($m in [regex]::Matches($t, '(?:\bdouble\s+|,\s*)([A-Za-z_]\w*)\s*=\s*' + $prod)) { $names[$m.Groups[1].Value] = 1 }
        foreach ($v in $names.Keys) {
            $use = '(?<![\w])(?:[A-Za-z_]\w*(?:\.|->))?' + [regex]::Escape($v) + '\b\s*' + $cmp + '(?!=)|' + $cmp + '\s*(?:[A-Za-z_]\w*(?:\.|->))?' + [regex]::Escape($v) + '\b'
            foreach ($line in ($t -split "`n")) {
                if ($line -match '^\s*//') { continue }
                $code = ($line -split '//')[0]
                $code = [regex]::Replace($code, 'TEXT\("[^"]*"\)', '')
                $code = [regex]::Replace($code, '<[A-Za-z_:]+>', '')
                if ($code -match $use -and $code -notmatch ('Is(?:Finite|NaN)\(\s*(?:[A-Za-z_]\w*(?:\.|->))?' + [regex]::Escape($v) + '\b')) { $f += "(N1) $name compares NaN-initialised '$v' without IsFinite/IsNaN on that line: '$($line.Trim())'" }
            }
        }
    }
    return $f
}

function Invoke-NChecks($T) {
    $f = @()
    # N2
    if ((Count $T.hapi '        bool bEdgeLogged = false;') -ne 1 -or (Count $T.hapi '        double EdgeLoggedH = 0.0;') -ne 1 -or (Count $T.hapi '        double EdgeLoggedG = 0.0;') -ne 1) { $f += '(N2) FRwgHeightApi must hold bEdgeLogged = false and the pair at 0.0' }
    $eg = Span $T.hapi 'bool RwgEdgeGuardRead(' 'double ReadWorldSizeMeters('
    if (-not (Ordered $eg @('if (!A.bEdgeLogged || OutH != A.EdgeLoggedH || OutG != A.EdgeLoggedG)', 'A.bEdgeLogged = true;', 'TEXT("RWGEDGESTRIP: generation=B '))) { $f += '(N2) the RWGEDGESTRIP gate must key on bEdgeLogged and set it before the print' }
    # N3
    if ((Count $T.hapi 'const bool bWaterAbove = FMath::IsFinite(WaterZ) && WaterZ > TraceZ;') -ne 1) { $f += '(N3) the census water split must test WaterZ with IsFinite first' }
    $v3 = Span $T.void 'int32 ANodeShuffleSubsystem::RwgRejectV3(' 'void ANodeShuffleSubsystem::RwgEnsureKillboxTop('
    if (-not (Ordered $v3 @('const bool bSurfaceFinite = FMath::IsFinite(P.Z);', 'const bool bBelowKillbox = !bSurfaceFinite || (bRwgKillboxKnown && P.Z < RwgKillboxTopZ + NodeShuffleMapMode::RwgKillboxMarginCm);', 'if (bBelowKillbox) { Bits |= 16; }'))) { $f += '(N3) RwgRejectV3 must give a non-finite surface bit 16' }
    if ((Count $T.sea 'if (FMath::IsFinite(Wl) && FMath::IsFinite(P.Z) && Wl > P.Z + NodeShuffleMapMode::RwgWaterApiMarginCm) { Wet |= 4; }') -ne 1) { $f += '(N3) RwgWetV2 bit 4 must test both Wl and P.Z with IsFinite' }
    $sl = Span $T.place 'bool ANodeShuffleSubsystem::RwgApiSlopeTooSteep(' 'void ANodeShuffleSubsystem::'
    if (-not (Ordered $sl @('OutRiseRun = FMath::Sqrt(Gx * Gx + Gy * Gy);', 'if (!FMath::IsFinite(OutRiseRun)) { NodeShuffleMapMode::RwgNoteNonFinite(TEXT("apiSlopeRiseRun"), X, Y, OutRiseRun); return true; }', 'return OutRiseRun > FMath::Tan('))) { $f += '(N3) the slope test must answer too steep on a non-finite gradient, before its compare' }
    $vc = Span $T.cjson 'bool ANodeShuffleSubsystem::RwgVerifyCaveCandidate(' 'OutSpot = FVector(X, Y, Floor);'
    if (-not (Ordered $vc @('if (!Q.CaveFloorAt(X, Y, Hint, Floor)) { return false; }', 'if (!FMath::IsFinite(Floor) || !FMath::IsFinite(SurfZ)) {', 'return false; }', 'if (!(Floor < SurfZ - RwgCaveInsideProbeCm))'))) { $f += '(N3) the cave verify must refuse a non-finite Floor or SurfZ before its first compare' }
    if ((Count $T.cave 'bWithin = bFound && FMath::IsFinite(Fz) && FMath::Abs(Fz - S.Z) <= RwgCaveFloorMatchCm;') -ne 1) { $f += '(N3) the cave check floor control must test Fz with IsFinite' }
    # N4
    if ((Count $T.mmh 'void RwgNoteNonFinite(const TCHAR* Site, double X, double Y, double Value);') -ne 1 -or (Count $T.hapi 'void NodeShuffleMapMode::RwgNoteNonFinite(const TCHAR* Site, double X, double Y, double Value)') -ne 1) { $f += '(N4) RwgNoteNonFinite must be declared once and defined once' }
    $ng = Span $T.hapi 'void NodeShuffleMapMode::RwgNoteNonFinite(' 'Site, X, Y, Value, N);'
    if (-not (Ordered $ng @('++N;', 'if (N <= 4)', 'UE_LOG(LogNodeShuffle, Warning, TEXT("RWGNANGUARD: site=%s x=%.0f y=%.0f value=%f countThisSite=%d; '))) { $f += '(N4) RWGNANGUARD must print the first 4 per site as a Warning, tag first' }
    return $f
}

$fails = @(Invoke-N1 $all) + @(Invoke-NChecks $T)
# Mutants: each must trip the named check. The N1 mutants run against a copy of the module text with ONE file edited.
$mut = @(
    @{ Name = 'M1 EDGESTRIP sentinel back to NaN'; Key = 'hapi'; Expect = 'N1'; Find = '        double EdgeLoggedH = 0.0;'; Repl = ('        double EdgeLoggedH = ' + $qn + '();') }
    @{ Name = 'M2 EDGESTRIP gate drops the flag'; Key = 'hapi'; Expect = 'N2'; Find = 'if (!A.bEdgeLogged || OutH != A.EdgeLoggedH'; Repl = 'if (OutH != A.EdgeLoggedH' }
    @{ Name = 'M3 flag never set'; Key = 'hapi'; Expect = 'N2'; Find = '            A.bEdgeLogged = true;'; Repl = '            // flag removed' }
    @{ Name = 'M4 census water split unguarded'; Key = 'hapi'; Expect = 'N3'; Find = 'const bool bWaterAbove = FMath::IsFinite(WaterZ) && WaterZ > TraceZ;'; Repl = 'const bool bWaterAbove = WaterZ > TraceZ;' }
    @{ Name = 'M5 v3 NaN surface dealable again'; Key = 'void'; Expect = 'N3'; Find = 'const bool bBelowKillbox = !bSurfaceFinite || ('; Repl = 'const bool bBelowKillbox = bSurfaceFinite && (' }
    @{ Name = 'M6 sea bit 4 drops the P.Z test'; Key = 'sea'; Expect = 'N3'; Find = 'FMath::IsFinite(Wl) && FMath::IsFinite(P.Z) && Wl >'; Repl = 'FMath::IsFinite(Wl) && Wl >' }
    @{ Name = 'M7 slope NaN passes'; Key = 'place'; Expect = 'N3'; Find = 'OutRiseRun); return true; }'; Repl = 'OutRiseRun); return false; }' }
    @{ Name = 'M8 cave verify guard removed'; Key = 'cjson'; Expect = 'N3'; Find = 'if (!FMath::IsFinite(Floor) || !FMath::IsFinite(SurfZ)) {'; Repl = 'if (false) {' }
    @{ Name = 'M9 cave floor control unguarded'; Key = 'cave'; Expect = 'N3'; Find = 'bWithin = bFound && FMath::IsFinite(Fz) && FMath::Abs('; Repl = 'bWithin = bFound && FMath::Abs(' }
    @{ Name = 'M10 RWGNANGUARD unbounded'; Key = 'hapi'; Expect = 'N4'; Find = '    if (N <= 4)'; Repl = '    if (N > 0)' }
    @{ Name = 'M11 direct compare against the producer'; Key = 'void'; Expect = 'N1'; Find = '    if (bBelowKillbox) { Bits |= 16; }'; Repl = ('    if (bBelowKillbox) { Bits |= 16; } if (P.Z != NodeShuffleMapMode::' + $rn + '()) { Bits |= 0; }') }
    @{ Name = 'M12 NaN-initialised local compared'; Key = 'void'; Expect = 'N1'; Find = '    if (bBelowKillbox) { Bits |= 16; }'; Repl = ('    if (bBelowKillbox) { Bits |= 16; } const double KbProbe = ' + $qn + '(); if (P.Z > KbProbe) { Bits |= 0; }') }
)
$caught = 0
foreach ($m in $mut) {
    if ((Count $T[$m.Key] $m.Find) -lt 1) { $fails += "(mutant) $($m.Name): anchor not found"; continue }
    $T2 = [ordered]@{}; foreach ($k in $T.Keys) { $T2[$k] = $T[$k] }
    $T2[$m.Key] = $T[$m.Key].Replace($m.Find, $m.Repl)
    $all2 = [ordered]@{}; foreach ($k in $all.Keys) { $all2[$k] = $all[$k] }
    $all2[(Split-Path -Leaf $files[$m.Key])] = $T2[$m.Key]
    $r = @(Invoke-N1 $all2) + @(Invoke-NChecks $T2)
    if (@($r | Where-Object { $_ -like "($($m.Expect))*" }).Count -gt 0) { $caught++ } else { $fails += "(mutant) $($m.Name) was NOT caught by $($m.Expect)" }
}
if ($fails.Count -gt 0) { Write-Host "RWGNAN LINT: FAIL -- $($fails.Count) problem(s). Mutants caught $caught/$($mut.Count)."; $fails | ForEach-Object { Write-Host "  $_" }; exit 1 }
Write-Host "PASS: RWGNAN lint -- N1..N4 hold across $($all.Count) module files ($($T.Count) pinned); mutants caught $caught/$($mut.Count)."
exit 0
