# RWG 0.2.0 lint (2026-11-13, packet rwg-0-2-0-1; handoff _team/test-logs/2026-11-13-nodeshuffle-rwg-0-2-0-handoff.md).
# Measurements: _team/test-logs/2026-11-13-rwg-0.2.0-export-diff.md, _team/test-logs/2026-11-13-rwg-0.2.0-height-semantics.md;
# offline mask check: tools/check_rwg_masks.py. LOADS and PINS every file the packet touched (8 source files, the mask tool, the
# three lints it edited). Anchors are full statements or parsed declarations; needles naming a forbidden literal are BUILT BY
# CONCATENATION so this file cannot match itself.
#   G1 Generation coherence (D1): SurfaceHeightCm classifies the generation; a neither-generation result takes the established Check
#      refusal; InSeaVolume's other-generation match is generationMixed; the cave exports are classified on EVERY build; a mixed result
#      refuses (status, Warning, notice, return) BEFORE the unlisted verdict and before bUsable.
#   G2 Edge getters REQUIRED on generation B (D4): resolved only inside the generation-B branch, a missing getter refuses with a return,
#      both masks go through the required Check; the value check (finite, G > 0, H > G) refuses process-wide; the ground query's
#      generation-B branch reads the guard before F is published; the census reads it before its first SurfaceHeightCm call.
#   G3 Single choke point: no file other than NodeShuffleMapModeHeightApi.cpp calls RWG's raw outside test; FRwgGroundQuery::Outside returns
#      the raw answer unchanged when StripInnerCm <= 0 (generation A) and ORs the strip test otherwise.
#   G4 Cave masks: generation A arrays keep their exact text; generation B arrays pin CarvedCaves @3 = 0x38, AnyNear @22 = 0x50,
#      FloorCmAt @30 = 0x50 (no wildcard -- D2); the Cave.cpp and GateNotice.cpp copies are token-equal; both select by generation.
#   G5 Accept list carries {0.2.0,1063} with the sha256 prefix and the record of the census that ran (rwg-0-2-0-2).
#   G6 Marker 2026-11-13-rwg-0-2-0-1 newest in the header, once in NodeShuffle.cpp, absent from the packet's source files.
#   G7 New formats: each starts with its tag (Display/Warning), no field repeats in a format, no `word=` inside a legend parenthetical.
#   G8 The mask tool parses masks from source (no hard-coded mask bytes); the edited lints carry the 0.2.0 anchors.
#   G9 rwg-0-2-1-1 (2026-11-15, RWG 0.2.1 build 1130; _team/test-logs/2026-11-15-rwg-0.2.1-export-diff.md): generation B accepts
#      CarvedCaves in exactly two fully pinned forms (0x38 / 0x40 at @3), the 1130 array selected only on a FULL match, the same
#      statement shape in the cave bind and the gate (token-equal pair); ClassifyCaveGenerations counts either form as B; the two
#      1130 readiness forms are pinned beside their 1063 forms; RWGCAVEBIND / RWGGATE-CAVESIG / RWGGETTERS print the form and the
#      byte read this run; the marker 2026-11-15-rwg-0-2-1-1 is newest and the three newest-marker lints are rotated; the mask tool and
#      docs/DIAGNOSTICS.md carry the two forms. LOADS every file the packet touched.
# Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$files = [ordered]@{
    hapi  = 'Source\NodeShuffle\Private\NodeShuffleMapModeHeightApi.cpp'
    cave  = 'Source\NodeShuffle\Private\NodeShuffleMapModeCave.cpp'
    gate  = 'Source\NodeShuffle\Private\NodeShuffleRwgGateNotice.cpp'
    gateh = 'Source\NodeShuffle\Private\NodeShuffleRwgGateNotice.h'
    mmh   = 'Source\NodeShuffle\Public\NodeShuffleMapMode.h'
    mk    = 'Source\NodeShuffle\Private\NodeShuffleBuildMarkers.h'
    nsmod = 'Source\NodeShuffle\Private\NodeShuffle.cpp'
    tool  = 'tools\check_rwg_masks.py'
    l019  = 'tools\check_rwg019_lint.ps1'
    lmcp  = 'tools\check_mcpcontract_lint.ps1'
    lthh  = 'tools\check_thoriumhalf_lint.ps1'
    lsf   = 'tools\check_scanfilter_lint.ps1'  # rwg-0-2-1-1
    lrf   = 'tools\check_rollfirst_lint.ps1'   # rwg-0-2-1-1
    doc   = 'docs\DIAGNOSTICS.md'              # rwg-0-2-1-1
}
$T = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $root $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $T[$k] = (Get-Content -Raw -Path $p) -replace "`r`n", "`n"
}
# Every other source file, for G3 (the raw outside test must not appear outside the height-API file).
$others = @{}
foreach ($f in Get-ChildItem -Path (Join-Path $root 'Source\NodeShuffle') -Recurse -Include *.cpp, *.h) {
    if ($f.Name -ne 'NodeShuffleMapModeHeightApi.cpp') { $others[$f.FullName] = (Get-Content -Raw -Path $f.FullName) -replace "`r`n", "`n" }
}
function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }
function Mask([string]$t, [string]$name) {
    $ms = [regex]::Matches($t, '(?m)^\s*(?:static )?const int16 ' + [regex]::Escape($name) + '\[\] = \{([^}]*)\};')
    if ($ms.Count -ne 1) { return $null }
    return @($ms[0].Groups[1].Value -split ',' | ForEach-Object { $_.Trim() })
}
function Ordered([string]$t, [string[]]$needles) { $at = -1; foreach ($n in $needles) { $i = $t.IndexOf($n, $at + 1); if ($i -lt 0) { return $false }; $at = $i }; return $true }
function Span([string]$t, [string]$a, [string]$b) { $i = $t.IndexOf($a); if ($i -lt 0) { return '' }; $j = $t.IndexOf($b, $i + $a.Length); if ($j -lt 0) { return '' }; return $t.Substring($i, $j - $i) }
$R = 'ret' + 'urn;'
$marker = '2026-11-13-' + 'rwg-0-2-0-1'
$marker021 = '2026-11-15-' + 'rwg-0-2-1-1'

function Invoke-020Checks($T, $others) {
    $f = @()
    $tb = Span $T.hapi 'void TryBind(FRwgHeightApi& A)' 'bool RwgEdgeGuardRead('
    if ($tb -eq '') { return @('(G1) TryBind / RwgEdgeGuardRead not found') }
    # G1
    if ((Count $tb 'A.Gen = ClassifyGen(A.Surface, SigSurface, UE_ARRAY_COUNT(SigSurface), SigSurface1063, UE_ARRAY_COUNT(SigSurface1063), SurfDiffA, SurfDiffB);') -ne 1 -or
        (Count $tb 'if (A.Gen == 0) { Check(A.Surface, SigSurface, UE_ARRAY_COUNT(SigSurface), TEXT("SurfaceHeightCm")); }') -ne 1) { $f += '(G1) SurfaceHeightCm must fix the generation, and a neither-generation match must take the required Check refusal' }
    if ((Count $tb ('return (bA && !bB) ? kRwgHapiGenA : ((bB && !bA) ? kRwgHapiGenB : ' + '0);')) -ne 1) { $f += '(G1) ClassifyGen must answer A or B only when exactly one generation matched' }
    if ((Count $tb ('else if (SeaGen > 0 && SeaGen != A.Gen) { A.InSea = nullptr; A.SeaStatus = TEXT("generation' + 'Mixed"); }')) -ne 1) { $f += '(G1) an InSeaVolume match on the other generation must null it as generationMixed' }
    if ((Count $tb 'const int32 CaveGenBits = NodeShuffleRwgGate::ClassifyCaveGenerations(H, CaveGenDetail);') -ne 1 -or (Count $tb 'const bool bCaveMixed = (CaveGenBits & (1 << OtherGen)) != 0;') -ne 1) { $f += '(G1) the cave exports must be classified in TryBind and a match on the OTHER generation counted as mixed' }
    $mixBlk = Span $tb 'if (bCaveMixed || bSeaMixed)' 'if (bUnlisted)'
    if ($mixBlk -eq '' -or -not $mixBlk.Contains('A.Status = TEXT("generationMixed");') -or -not $mixBlk.Contains('UE_LOG(LogNodeShuffle, Warning, TEXT("RWGHEIGHTAPI-REFUSED: status=generationMixed ') -or -not $mixBlk.Contains('NodeShuffleRwgGate::QueueRefusalNotice(A.SemVer, A.Build, A.Status,') -or -not $mixBlk.Contains($R)) { $f += '(G1) the mixed-generation refusal must set generationMixed, warn, queue the notice and return, before the unlisted verdict' }
    if (-not (Ordered $tb @('TEXT("RWGVOIDBIND-OFF:', 'ClassifyCaveGenerations(H, CaveGenDetail)', 'if (bCaveMixed || bSeaMixed)', 'if (bUnlisted)', 'A.bUsable = true;'))) { $f += '(G1) coherence must be decided after the optional binds and before the unlisted verdict and bUsable' }
    if ((Count $tb 'NodeShuffleRwgGate::CheckCaveSignatures(H, A.Gen, CaveMissing, CaveBad);') -ne 1) { $f += '(G1) the unlisted cave half must check the accepted generation' }
    # G2
    $gb = Span $tb 'if (A.Gen == kRwgHapiGenB)' 'if (A.Gen == 0)'
    if ($gb -eq '' -or (Count $gb 'A.HalfWorld = reinterpret_cast<FnScalar>(::GetProcAddress(H, SymHalfWorld));') -ne 1 -or (Count $gb 'A.GridCm = reinterpret_cast<FnScalar>(::GetProcAddress(H, SymGridCm));') -ne 1) { $f += '(G2) the edge getters must be resolved inside the generation-B branch' }
    if ((Count $T.hapi 'GetProcAddress(H, SymHalfWorld)') -ne 1 -or (Count $T.hapi 'GetProcAddress(H, SymGridCm)') -ne 1) { $f += '(G2) the edge getters must be resolved at one site only (generation B)' }
    if ($gb -eq '' -or -not ($gb -match ('if \(EdgeGettersResolved != 2\)\s*\{[^}]*A\.Status = TEXT\("exportMissing"\);[\s\S]*?QueueRefusalNotice\([\s\S]*?' + [regex]::Escape($R) + '\s*\}'))) { $f += '(G2) a missing edge getter on generation B must refuse with a notice and return' }
    if ((Count $gb 'Check(A.HalfWorld, SigHalfWorld1063, UE_ARRAY_COUNT(SigHalfWorld1063), TEXT("HalfWorldCm"));') -ne 1 -or (Count $gb 'Check(A.GridCm, SigGridCm1063, UE_ARRAY_COUNT(SigGridCm1063), TEXT("GridCmValue"));') -ne 1) { $f += '(G2) both edge-getter masks must go through the REQUIRED Check' }
    $expB = @{ SigSurface1063 = '0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x57,0x48,0x83,0xEC,0x70,0x80,0xB9,-1,-1,-1,-1,0x00,0x48,0x8B,0xD9'; SigInSea1063 = '0x40,0x53,0x56,0x41,0x54,0x48,0x83,0xEC,0x40,0x0F,0x57,0xC0,0x0F,0x28,0xEA,0x66,0x0F,0x2F,0xD8,0x48,0x8B,0xD9' }
    foreach ($n in $expB.Keys) { $m = Mask $T.hapi $n; if (-not $m -or ($m -join ',') -ne $expB[$n]) { $f += "(G2) generation-B mask $n must be exactly the measured 1063 bytes (only SurfaceHeightCm's member disp32 @14..17 wildcarded)" } }
    if ((Count $tb 'else if (SeaGen == kRwgHapiGenB) { A.SeaStatus = TEXT("bound"); }') -ne 1 -or -not (Ordered $tb @('else if (SeaGen > 0 && SeaGen != A.Gen)', 'else if (SeaGen == kRwgHapiGenB)', 'else if (!SigOk(reinterpret_cast<const void*>(A.InSea), SigInSea, UE_ARRAY_COUNT(SigInSea)))'))) { $f += '(G1) InSeaVolume: other-generation first, then the generation-B bind, then the unchanged generation-A check' }
    $lhS = Span $T.hapi 'void ANodeShuffleSubsystem::RwgLogHeightApi(bool bFull)' 'NodeShuffleMapMode::FRwgGroundQuery::FRwgGroundQuery('
    if (-not (Ordered $lhS @('if (A.Gen == kRwgHapiGenB)', 'if (!S)', $R, 'if (!RwgEdgeGuardRead(A, F, S, EdgeH, EdgeG))'))) { $f += '(G2) the census must skip (not refuse) on a null spliced world before the edge-guard read' }
    if ((Count $T.mmh 'mutable int32 EdgeRejectedByStripOnly = 0;') -ne 1 -or (Count $T.mmh 'double StripInnerCm = -1.0;') -ne 1) { $f += '(G3) StripInnerCm must default to -1 (generation A = raw answer)' }
    if ((Count $T.gateh 'int32 CheckCaveSignatures(void* Hmod, int32 Gen, FString& OutMissing, FString& OutMismatched);') -ne 1) { $f += '(G4) the gate header must declare the generation-aware cave check' }
    foreach ($n in @('SigHalfWorld1063', 'SigGridCm1063')) { $m = Mask $T.hapi $n; if (-not $m -or ($m -join ',') -ne '0xF2,0x0F,0x10,0x81,-1,-1,-1,-1,0xC3') { $f += "(G2) $n must be movsd xmm0,[rcx+disp32]; ret with only the disp32 wildcarded" } }
    $eg = Span $T.hapi 'bool RwgEdgeGuardRead(' '// GenerationParams.WorldSizeMeters'
    if ((Count $eg 'const bool bOk = bGetters && S && F && FMath::IsFinite(OutH) && FMath::IsFinite(OutG) && OutG > 0.0 && OutH > OutG;') -ne 1 -or (Count $eg 'A.bUsable = false;') -ne 1 -or (Count $eg 'A.Status = TEXT("edgeGuardImplausible");') -ne 1 -or (Count $eg 'NodeShuffleRwgGate::QueueRefusalNotice(') -ne 1) { $f += '(G2) an implausible edge-getter value must refuse the API process-wide with a notice' }
    $ct = Span $T.hapi 'NodeShuffleMapMode::FRwgGroundQuery::FRwgGroundQuery(UWorld* World, UObject* Generator)' 'NodeShuffleMapMode::FRwgGroundQuery::~FRwgGroundQuery()'
    if (-not (Ordered $ct @('if (A.Gen == kRwgHapiGenB)', 'if (!RwgEdgeGuardRead(A, Fp, S, EdgeH, EdgeG))', 'S = nullptr;', $R, 'StripInnerCm = EdgeH - 0.5 * EdgeG;', 'F = Fp;'))) { $f += '(G2) the ground query must read the edge guard (refusing on failure) before it publishes F' }
    $lh = Span $T.hapi 'void ANodeShuffleSubsystem::RwgLogHeightApi(bool bFull)' 'NodeShuffleMapMode::FRwgGroundQuery::FRwgGroundQuery('
    if (-not (Ordered $lh @('if (A.Gen == kRwgHapiGenB)', 'if (!RwgEdgeGuardRead(A, F, S, EdgeH, EdgeG))', $R, 'const double PawnApiZ = A.Surface(F, P.X, P.Y);'))) { $f += '(G2) the census must read the edge guard before its first SurfaceHeightCm call' }
    # G3
    foreach ($k in $others.Keys) { foreach ($n in @(('Api().' + 'Outside('), ('A.' + 'Outside(F'))) { if ($others[$k].Contains($n)) { $f += "(G3) raw outside test '$n' called outside the height-API file: $k" } } }
    $ob = Span $T.hapi 'bool NodeShuffleMapMode::FRwgGroundQuery::Outside(double X, double Y) const' 'double NodeShuffleMapMode::FRwgGroundQuery::SurfaceZ('
    if (-not (Ordered $ob @('if (!F) { return true; }', 'const bool bApiOutside = Api().Outside(F, X, Y);', 'if (StripInnerCm <= 0.0) { return bApiOutside; }', 'const bool bStripOutside = FMath::Max(FMath::Abs(X), FMath::Abs(Y)) > StripInnerCm;', 'return bApiOutside || bStripOutside;'))) { $f += '(G3) FRwgGroundQuery::Outside must return the raw answer on generation A and OR the strip test on B' }
    if ((Count $T.hapi ('Api().' + 'Outside(')) -ne 1) { $f += '(G3) the query must reach the raw outside test at ONE site' }
    if ((Count $T.hapi 'if (!HasVoid() || Outside(X, Y)) { return false; }') -ne 1) { $f += '(G3) the query VoidAt must still ask the choke point first' }
    # G4
    $expA = @{ SigCaves = '0x48,0x8D,0x41,0x28,0xC3'; SigNum = '0x8B,0x41,0x08,0xC3'; SigAnyNear = '0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x57,0x48,0x83,0xEC,0x70,0x8B,0x59,0x48,0x48,0x8B,0xF1'; SigFloor = '0x4C,0x8B,0xDC,0x53,0x41,0x55,0x48,0x81,0xEC,0xF8,0x00,0x00,0x00,0x8B,0x59,0x48,0x4C,0x8B,0xE9' }
    foreach ($n in $expA.Keys) { foreach ($k in @('cave', 'gate')) { $m = Mask $T[$k] $(if ($k -eq 'gate') { 'Gate' + $n } else { $n }); if (-not $m -or ($m -join ',') -ne $expA[$n]) { $f += "(G4) generation-A $n changed in $k" } } }
    $pairs = @{ SigCaves1063 = 'GateSigCaves1063'; SigAnyNear1063 = 'GateSigAnyNear1063'; SigFloor1063 = 'GateSigFloor1063' }
    $pins = @{ SigCaves1063 = @(3, '0x38', 5); SigAnyNear1063 = @(22, '0x50', 26); SigFloor1063 = @(30, '0x50', 34) }
    foreach ($n in $pairs.Keys) {
        $a = Mask $T.cave $n; $g = Mask $T.gate $pairs[$n]
        if (-not $a -or -not $g) { $f += "(G4) $n / $($pairs[$n]) must be declared once in each file"; continue }
        if (($a -join ',') -ne ($g -join ',')) { $f += "(G4) $n differs between NodeShuffleMapModeCave.cpp and NodeShuffleRwgGateNotice.cpp" }
        $p = $pins[$n]
        if ($a.Count -ne $p[2] -or $a[$p[0]] -ne $p[1]) { $f += "(G4) $n must be $($p[2]) bytes with byte $($p[0]) PINNED to $($p[1]) (a moved cave field fails closed)" }
        if (@($a | Where-Object { $_ -eq '-1' }).Count -ne 0) { $f += "(G4) $n must carry no wildcard (D2)" }
    }
    if ((Count $T.cave 'const bool bGenB = Gen == NodeShuffleMapMode::RwgHeightApiGenB;') -ne 1 -or (Count $T.cave 'A.Caves = reinterpret_cast<FnCaves>(Bind(SymCaves, CavesSig, CavesN, TEXT("CarvedCaves")));') -ne 1 -or (Count $T.cave 'A.Floor = reinterpret_cast<FnFloor>(Bind(SymFloor, FloorSig, FloorN, TEXT("FloorCmAt")));') -ne 1 -or (Count $T.cave 'A.AnyNear = reinterpret_cast<FnAnyNear>(Bind(SymAnyNear, AnyNearSig, AnyNearN, TEXT("AnyNear")));') -ne 1) { $f += '(G4) the cave bind must select its three generation-specific masks by the accepted generation' }
    if ((Count $T.gate 'const bool bGenB = Gen == GenB;') -ne 1 -or (Count $T.gate 'const int16* const CavesSig = bGenB ? (bCaves1130 ? GateSigCaves1130 : GateSigCaves1063) : GateSigCaves;') -ne 1 -or (Count $T.gate 'const int16* const FloorSig = bGenB ? GateSigFloor1063 : GateSigFloor;') -ne 1 -or (Count $T.gate 'const int16* const AnyNearSig = bGenB ? GateSigAnyNear1063 : GateSigAnyNear;') -ne 1) { $f += '(G4) the unlisted cave half must select the same masks by generation' }
    if ((Count $T.mmh 'constexpr int32 RwgHeightApiGenB = 2;') -ne 1 -or (Count $T.gateh 'constexpr int32 GenB = 2;') -ne 1 -or (Count $T.hapi 'static_assert(NodeShuffleMapMode::RwgHeightApiGenA == NodeShuffleRwgGate::GenA && NodeShuffleMapMode::RwgHeightApiGenB == NodeShuffleRwgGate::GenB,') -ne 1) { $f += '(G4) the two generation constant pairs must be equal and asserted' }
    # G5
    if ((Count $T.hapi ', { TEXT("0.2.0"), 1063 } };') -ne 1 -or (Count $T.hapi '0.2.0 build 1063, sha256 6AEF57A9652D77AD') -ne 1 -or (Count $T.hapi 'RAN 2026-11-13 (first 0.2.0 load, world') -ne 1 -or (Count $T.hapi 'RWGHEIGHTAPI census usable=1, terrain n=84, median |dZ| 8.6 cm, p95 40.3 cm') -ne 1) { $f += '(G5) the accept list must carry {0.2.0,1063} with its sha256 prefix and the owed-census sentence' }
    # G6
    $arr = [regex]::Match($T.mk, 'PacketMarkers\[\] = \{([\s\S]*?)\};')
    $hdrTags = @(if ($arr.Success) { [regex]::Matches($arr.Groups[1].Value, 'TEXT\("([\w-]+)"\)') | ForEach-Object { $_.Groups[1].Value } })
    if (@($hdrTags | Where-Object { $_ -eq $marker }).Count -ne 1 -or (Count $T.nsmod ('packet marker (' + $marker + ')=====')) -ne 1) { $f += '(G6) marker once in the header and once in NodeShuffle.cpp' } # rwg-0-2-0-2: no longer the newest; pinned present
    foreach ($k in @('hapi', 'cave', 'gate', 'gateh', 'mmh')) { if ($T[$k].Contains($marker)) { $f += "(G6) no marker literal in $k" } }
    # G7
    $tags = 'RWGGENERATION|RWGEDGESTRIP|RWGEDGESTRIP-SAMPLE|RWGEDGEGUARD'
    $fmts = @([regex]::Matches($T.hapi, 'UE_LOG\(LogNodeShuffle, (?:Display|Warning), TEXT\("((?:' + $tags + '): [^"]*|RWGHEIGHTAPI-REFUSED: status=(?:generationMixed|edgeGuardImplausible|exportMissing semVer=''%s'' build=%d matchedEntry=''%s'' generation=B)[^"]*)"\)') | ForEach-Object { $_.Groups[1].Value })
    if ($fmts.Count -ne 9) { $f += "(G7) expected 9 new tag-initial formats (2 RWGGENERATION, RWGEDGESTRIP, RWGEDGESTRIP-SAMPLE, 2 RWGEDGEGUARD, 3 REFUSED), found $($fmts.Count)" }
    foreach ($fm in $fmts) {
        $paren = $fm.IndexOf(' ('); $body = if ($paren -ge 0) { $fm.Substring(0, $paren) } else { $fm }; $legend = if ($paren -ge 0) { $fm.Substring($paren) } else { '' }
        $dup = [regex]::Matches($body, '(?<![A-Za-z0-9])([A-Za-z][A-Za-z0-9]*)=') | ForEach-Object { $_.Groups[1].Value } | Group-Object | Where-Object { $_.Count -gt 1 }
        if ($dup) { $f += "(G7) repeated field(s) $(($dup | ForEach-Object { $_.Name }) -join ',') in '$($fm.Substring(0, [Math]::Min(40, $fm.Length)))'" }
        if ($legend -match '[A-Za-z]=') { $f += "(G7) legend carries field=value syntax: '$($fm.Substring(0, [Math]::Min(40, $fm.Length)))'" }
        foreach ($w in @(('bec' + 'ause'), ('mo' + 'ved'), ('upd' + 'ated'))) { if ($fm -match ('\b' + $w + '\b')) { $f += "(G7) format names a cause ('$w')" } }
    }
    # G8
    if (-not $T.tool.Contains("SIG_RX = re.compile(") -or $T.tool.Contains('0x48,' + '0x8B,0xC4') -or $T.tool.Contains('0x48, ' + '0x8B, 0xC4') -or -not $T.tool.Contains("'git', '-C', ROOT, 'show'")) { $f += '(G8) the mask tool must parse masks from source, carry no mask bytes of its own, and compare against the base commit' }
    if (-not $T.l019.Contains('{ TEXT("0.2.0"), 1063 } };') -or -not $T.l019.Contains('CheckCaveSignatures(H, A.Gen, CaveMissing, CaveBad);') -or -not $T.lmcp.Contains('# rwg-0-2-0-2: no longer the newest; pinned present') -or -not $T.lthh.Contains('# rwg-0-2-0-1: no longer the newest; pinned present')) { $f += '(G8) the edited lints must carry their 0.2.0 anchors' }
    # G9 (rwg-0-2-1-1)
    $c130 = Mask $T.cave 'SigCaves1130'; $g130 = Mask $T.gate 'GateSigCaves1130'
    if (-not $c130 -or -not $g130) { $f += '(G9) SigCaves1130 / GateSigCaves1130 must be declared once in each file' }
    else {
        if (($c130 -join ',') -ne ($g130 -join ',')) { $f += '(G9) SigCaves1130 differs between NodeShuffleMapModeCave.cpp and NodeShuffleRwgGateNotice.cpp' }
        if (($c130 -join ',') -ne '0x48,0x8D,0x41,0x40,0xC3') { $f += '(G9) the 1130 CarvedCaves form must be exactly lea rax,[rcx+40h]; ret, every byte pinned' }
    }
    $cb = Span $T.cave 'void TryBindCaves(FRwgCaveApi& A)' 'const FRwgCaveApi& BoundCaves()'
    if ((Count $cb 'bool bCaves1130 = bGenB && CavesBytes != nullptr;') -ne 1 -or
        (Count $cb 'for (int32 I = 0; bCaves1130 && I < static_cast<int32>(UE_ARRAY_COUNT(SigCaves1130)); ++I) { bCaves1130 = CavesBytes[I] == static_cast<uint8>(SigCaves1130[I]); }') -ne 1 -or
        (Count $cb 'const int16* const CavesSig = bGenB ? (bCaves1130 ? SigCaves1130 : SigCaves1063) : SigCaves;') -ne 1 -or
        (Count $cb 'const int32 CavesN = bGenB ? (bCaves1130 ? static_cast<int32>(UE_ARRAY_COUNT(SigCaves1130)) : static_cast<int32>(UE_ARRAY_COUNT(SigCaves1063))) : static_cast<int32>(UE_ARRAY_COUNT(SigCaves));') -ne 1 -or
        -not (Ordered $cb @('const uint8* const CavesBytes = reinterpret_cast<const uint8*>(::GetProcAddress(H, SymCaves));', 'const int16* const CavesSig = bGenB ?', 'A.Caves = reinterpret_cast<FnCaves>(Bind(SymCaves, CavesSig, CavesN, TEXT("CarvedCaves")));'))) { $f += '(G9) the cave bind must select the 1130 CarvedCaves array only on a full match on generation B, else the 1063 array, before its single Bind' }
    $gh = Span $T.gate '    bool GateCavesIs1130(const uint8* B)' 'FRwgGateNotice& Notice()'
    $cs = Span $T.gate 'int32 NodeShuffleRwgGate::CheckCaveSignatures(' 'int32 NodeShuffleRwgGate::ClassifyCaveGenerations('
    if ((Count $gh 'for (int32 I = 0; I < static_cast<int32>(UE_ARRAY_COUNT(GateSigCaves1130)); ++I) { if (B[I] != static_cast<uint8>(GateSigCaves1130[I])) { return false; } }') -ne 1 -or
        (Count $cs 'const bool bCaves1130 = bGenB && GateCavesIs1130(CavesBytes);') -ne 1 -or
        (Count $cs 'const int32 CavesN = bGenB ? (bCaves1130 ? static_cast<int32>(UE_ARRAY_COUNT(GateSigCaves1130)) : static_cast<int32>(UE_ARRAY_COUNT(GateSigCaves1063))) : static_cast<int32>(UE_ARRAY_COUNT(GateSigCaves));') -ne 1 -or
        -not (Ordered $cs @('const uint8* const CavesBytes = reinterpret_cast<const uint8*>(::GetProcAddress(H, GateSymCaves));', 'const int16* const CavesSig = bGenB ? (bCaves1130 ?', 'Check(GateSymCaves, CavesSig, CavesN, TEXT("CarvedCaves"));'))) { $f += '(G9) the unlisted cave half must select the CarvedCaves form exactly as the cave bind does (full match of the 1130 array)' }
    foreach ($k in @('cave', 'gate')) { foreach ($n in @(('CavesBytes' + '[3] =='), ('CavesBytes' + '[3] !='), ('B' + '[3] =='))) { if ($T[$k].Contains($n)) { $f += "(G9) the CarvedCaves form must not be selected by one byte ('$n' in $k)" } } }
    $cg = Span $T.gate 'int32 NodeShuffleRwgGate::ClassifyCaveGenerations(' '// ---- bodies.'
    if ((Count $cg 'const bool bB = B != nullptr && (Matches(B, SigGenB, NB) || (SigGenB2 != nullptr && Matches(B, SigGenB2, NB2)));') -ne 1 -or
        (Count $cg 'One(GateSymCaves, GateSigCaves, UE_ARRAY_COUNT(GateSigCaves), GateSigCaves1063, UE_ARRAY_COUNT(GateSigCaves1063), GateSigCaves1130, UE_ARRAY_COUNT(GateSigCaves1130), TEXT("CarvedCaves"));') -ne 1 -or
        (Count $cg 'GateSigAnyNear1063, UE_ARRAY_COUNT(GateSigAnyNear1063), nullptr, 0, TEXT("AnyNear"));') -ne 1 -or (Count $cg 'GateSigFloor1063, UE_ARRAY_COUNT(GateSigFloor1063), nullptr, 0, TEXT("FloorCmAt"));') -ne 1) { $f += '(G9) RWGGENERATION must label CarvedCaves B on either generation-B form (and only CarvedCaves has a second form)' }
    $rd = @{ SigGetState1063 = '0x0F,0xB6,0x81,0xC0,0x17,0x00,0x00,0xC3'; SigContentSpawned1063 = '0x0F,0xB6,0x81,0x70,0x0F,0x00,0x00,0xC3'; SigGetState1130 = '0x0F,0xB6,0x81,0x88,0x19,0x00,0x00,0xC3'; SigContentSpawned1130 = '0x0F,0xB6,0x81,0x18,0x11,0x00,0x00,0xC3' }
    foreach ($n in $rd.Keys) { $m = Mask $T.hapi $n; if (-not $m -or ($m -join ',') -ne $rd[$n]) { $f += "(G9) readiness form $n must be exactly its measured 8 bytes, fully pinned" } }
    $rg = Span $T.hapi '    void TryBindReadyGetters(FRwgReadyGetters& R)' 'FString NodeShuffleMapMode::RwgReadinessFields('
    if ((Count $rg 'const int16* const Sigs2[4] = { SigGetState1130, nullptr, nullptr, SigContentSpawned1130 };') -ne 1 -or
        (Count $rg 'for (int32 K = 0; K < 8; ++K) { if (B[K] != static_cast<uint8>(Sigs2[I][K])) { Diff2 = K; break; } }') -ne 1 -or
        (Count $rg 'if (Diff2 < 0) { Diff = -1; Form = TEXT("1130"); }') -ne 1) { $f += '(G9) the readiness bind must try the fully pinned 1130 form only when the 1063 form does not match' }
    if ((Count $T.cave 'cavesMaskApplied=%s cavesDisp=%s optional=1;') -ne 1 -or (Count $T.cave '*A.Status, *Missing, *SigBad, bGenB ? TEXT("B") : TEXT("A"), CavesMask, *CavesDisp);') -ne 1 -or
        (Count $T.gate "mismatchedList='%s' cavesMaskApplied=%s cavesDisp=%s (") -ne 1 -or (Count $T.gate 'Bad, *OutMissing, *OutMismatched, CavesMask, *CavesDisp);') -ne 1 -or
        (Count $T.cave 'FString::Printf(TEXT("0x%02X"), static_cast<int32>(CavesBytes[3]))') -ne 1 -or (Count $T.gate 'FString::Printf(TEXT("0x%02X"), static_cast<int32>(CavesBytes[3]))') -ne 1 -or
        (Count $rg 'TEXT("%s%s:%s@%d/form%s/disp0x%X"), Detail.IsEmpty() ? TEXT("") : TEXT(" "), Names[I], St, Diff, Form, Disp);') -ne 1) { $f += '(G9) RWGCAVEBIND, RWGGATE-CAVESIG and RWGGETTERS must print the form applied and the bytes read this run' }
    $arr9 = [regex]::Match($T.mk, 'PacketMarkers\[\] = \{([\s\S]*?)\};')
    $tags9 = @(if ($arr9.Success) { [regex]::Matches($arr9.Groups[1].Value, 'TEXT\("([\w-]+)"\)') | ForEach-Object { $_.Groups[1].Value } })
    if ($tags9.Count -eq 0 -or $tags9[-1] -ne $marker021 -or @($tags9 | Where-Object { $_ -eq $marker021 }).Count -ne 1 -or (Count $T.nsmod ('packet marker (' + $marker021 + ')=====')) -ne 1) { $f += '(G9) marker rwg-0-2-1-1 must be the newest header entry, once, and once in NodeShuffle.cpp' }
    foreach ($k in @('hapi', 'cave', 'gate', 'gateh', 'tool', 'doc')) { if ($T[$k].Contains($marker021)) { $f += "(G9) no rwg-0-2-1-1 marker literal in $k" } }
    if (-not $T.lmcp.Contains("`$own = '2026-11-15-' + 'rwg-0-2-1-1'") -or -not $T.lsf.Contains("'2026-11-15-' + 'rwg-0-2-1-1'") -or -not $T.lsf.Contains('(S9) check_mcpcontract C4 must name rwg-0-2-1-1 as the newest') -or
        -not $T.lrf.Contains("if (`$hdrTags.Count -eq 0 -or `$hdrTags -notcontains `$own -or `$hdrTags -notcontains `$prev)") -or $T.lrf.Contains('$hdrTags[-1]')) { $f += '(G9) mcpcontract C4 and scanfilter S9 must name rwg-0-2-1-1 as the newest, and rollfirst R9 must pin rollfirst-1 listed, not newest' }
    if (-not $T.tool.Contains("('SigCaves1063', 'SigCaves1130')") -or -not $T.tool.Contains("'SigCaves1130': 'GateSigCaves1130'") -or -not $T.tool.Contains("('SigGetState1063', 'SigGetState1130')") -or -not $T.tool.Contains("('SigContentSpawned1063', 'SigContentSpawned1130')") -or -not $T.tool.Contains('def control_d(')) { $f += '(G9) the mask tool must mirror the two-form masks and run control (d)' }
    if (-not $T.doc.Contains('`cavesMaskApplied`') -or -not $T.doc.Contains('`RWGGETTERS`') -or -not $T.doc.Contains('`cavesDisp`')) { $f += '(G9) docs/DIAGNOSTICS.md must describe cavesMaskApplied, cavesDisp and the RWGGETTERS form' }
    return $f
}

$fails = Invoke-020Checks $T $others
$N = "`n"
$mutants = @(
    @{ Name = 'M1 mixed generation no longer refused'; Key = 'hapi'; Expect = 'G1'; Find = 'if (bCaveMixed || bSeaMixed)' + $N; Repl = 'if (false)' + $N }
    @{ Name = 'M2 per-export OR: InSea other-generation match accepted'; Key = 'hapi'; Expect = 'G1'; Find = 'else if (SeaGen > 0 && SeaGen != A.Gen) { A.InSea = nullptr; A.SeaStatus = TEXT("generationMixed"); }'; Repl = 'else if (SeaGen > 0) { A.SeaStatus = TEXT("bound"); }' }
    @{ Name = 'M3 cave mixed bit tests the SAME generation'; Key = 'hapi'; Expect = 'G1'; Find = '(CaveGenBits & (1 << OtherGen)) != 0'; Repl = '(CaveGenBits & (1 << A.Gen)) == 0' }
    @{ Name = 'M4 neither-generation SurfaceHeightCm no longer refused'; Key = 'hapi'; Expect = 'G1'; Find = 'if (A.Gen == 0) { Check(A.Surface, SigSurface, UE_ARRAY_COUNT(SigSurface), TEXT("SurfaceHeightCm")); }'; Repl = 'if (A.Gen == 0) { A.Gen = kRwgHapiGenA; }' }
    @{ Name = 'M5 missing edge getter falls through'; Key = 'hapi'; Expect = 'G2'; Find = ('*EdgeMissing));' + $N + '                ' + $R); Repl = '*EdgeMissing));' }
    @{ Name = 'M6 edge getter masks checked as optional'; Key = 'hapi'; Expect = 'G2'; Find = 'Check(A.HalfWorld, SigHalfWorld1063, UE_ARRAY_COUNT(SigHalfWorld1063), TEXT("HalfWorldCm"));'; Repl = 'SigOk(A.HalfWorld, SigHalfWorld1063, UE_ARRAY_COUNT(SigHalfWorld1063));' }
    @{ Name = 'M7 plausibility drops H > G'; Key = 'hapi'; Expect = 'G2'; Find = 'OutG > 0.0 && OutH > OutG;'; Repl = 'OutG > 0.0;' }
    @{ Name = 'M8 ground query skips the guard'; Key = 'hapi'; Expect = 'G2'; Find = 'if (!RwgEdgeGuardRead(A, Fp, S, EdgeH, EdgeG))'; Repl = 'if (false)' }
    @{ Name = 'M9 census calls SurfaceHeightCm before the guard'; Key = 'hapi'; Expect = 'G2'; Find = 'if (!RwgEdgeGuardRead(A, F, S, EdgeH, EdgeG))'; Repl = 'if (false)' }
    @{ Name = 'M10 a consumer calls the raw test'; Key = '@other'; Expect = 'G3'; Find = ''; Repl = 'bool X = Api().' + 'Outside(F, 0, 0);' }
    @{ Name = 'M11 strip test applied on generation A too'; Key = 'hapi'; Expect = 'G3'; Find = 'if (StripInnerCm <= 0.0) { return bApiOutside; }'; Repl = '' }
    @{ Name = 'M12 strip test dropped from the answer'; Key = 'hapi'; Expect = 'G3'; Find = 'return bApiOutside || bStripOutside;'; Repl = 'return bApiOutside;' }
    @{ Name = 'M13 CarvedCaves B offset wildcarded (gate copy)'; Key = 'gate'; Expect = 'G4'; Find = 'GateSigCaves1063[] = { 0x48,0x8D,0x41,0x38,0xC3 };'; Repl = 'GateSigCaves1063[] = { 0x48,0x8D,0x41,-1,0xC3 };' }
    @{ Name = 'M14 AnyNear B offset wildcarded in both copies'; Key = 'cave'; Expect = 'G4'; Find = '0x57,0x48,0x83,0xEC,0x70,0x8B,0x59,0x50,0x48,0x8B,0xF1 };'; Repl = '0x57,0x48,0x83,0xEC,0x70,0x8B,0x59,-1,0x48,0x8B,0xF1 };' }
    @{ Name = 'M15 FloorCmAt B copy drifts'; Key = 'gate'; Expect = 'G4'; Find = '0x18,0x01,0x00,0x00,0x8B,0x59,0x50,0x4C,0x8B,0xE9 };'; Repl = '0x18,0x01,0x00,0x00,0x8B,0x59,0x48,0x4C,0x8B,0xE9 };' }
    @{ Name = 'M16 generation-A SigCaves edited'; Key = 'cave'; Expect = 'G4'; Find = 'SigCaves[] = { 0x48,0x8D,0x41,0x28,0xC3 };'; Repl = 'SigCaves[] = { 0x48,0x8D,0x41,-1,0xC3 };' }
    @{ Name = 'M17 cave bind ignores the generation'; Key = 'cave'; Expect = 'G4'; Find = 'Bind(SymCaves, CavesSig, CavesN, TEXT("CarvedCaves"))'; Repl = 'Bind(SymCaves, SigCaves, UE_ARRAY_COUNT(SigCaves), TEXT("CarvedCaves"))' }
    @{ Name = 'M18 accept entry dropped'; Key = 'hapi'; Expect = 'G5'; Find = ', { TEXT("0.2.0"), 1063 } };'; Repl = ' };' }
    @{ Name = 'M19 census record dropped'; Key = 'hapi'; Expect = 'G5'; Find = 'RAN 2026-11-13 (first 0.2.0 load, world'; Repl = 'OWED (first 0.2.0 load, world' }
    @{ Name = 'M20 marker dropped from the header'; Key = 'mk'; Expect = 'G6'; Find = ('        TEXT("' + $marker + '"),' + $N); Repl = '' }
    @{ Name = 'M21 marker literal in the gate file'; Key = 'gate'; Expect = 'G6'; Find = '// RWG 0.1.12 gate lane (2026-10-26)'; Repl = ('// ' + $marker + ' gate lane (2026-10-26)') }
    @{ Name = 'M22 field=value in the RWGGENERATION legend'; Key = 'hapi'; Expect = 'G7'; Find = 'a FirstDiff of -1 means'; Repl = 'surfaceFirstDiffA=-1 means' }
    @{ Name = 'M23 RWGEDGESTRIP repeats a field'; Key = 'hapi'; Expect = 'G7'; Find = 'bisectionValid=%d thresholdMinusStripInnerCm='; Repl = 'bisectionValid=%d gridCm=' }
    @{ Name = 'M24 mask tool hard-codes a mask'; Key = 'tool'; Expect = 'G8'; Find = 'BASE_COMMIT = '; Repl = ('X = [0x48,' + '0x8B,0xC4]' + $N + 'BASE_COMMIT = ') }
    @{ Name = 'M25 rwg019 lint loses the 1063 entry'; Key = 'l019'; Expect = 'G8'; Find = '{ TEXT("0.2.0"), 1063 } };'; Repl = '};' }
    @{ Name = 'M27 SigSurface1063 widened (pinned byte wildcarded)'; Key = 'hapi'; Expect = 'G2'; Find = '0x80,0xB9,-1,-1,-1,-1,0x00,0x48,0x8B,0xD9 };'; Repl = '0x80,0xB9,-1,-1,-1,-1,-1,0x48,0x8B,0xD9 };' }
    @{ Name = 'M28 SigInSea1063 widened (pinned byte wildcarded)'; Key = 'hapi'; Expect = 'G2'; Find = '0x0F,0x28,0xEA,0x66,0x0F,0x2F,0xD8,0x48,0x8B,0xD9 };'; Repl = '0x0F,0x28,0xEA,0x66,0x0F,0x2F,0xD8,0x48,-1,0xD9 };' }
    @{ Name = 'M29 InSea generation-B bind removed'; Key = 'hapi'; Expect = 'G1'; Find = 'else if (SeaGen == kRwgHapiGenB) { A.SeaStatus = TEXT("bound"); }'; Repl = '' }
    @{ Name = 'M30 census null-spliced skip removed'; Key = 'hapi'; Expect = 'G2'; Find = '        if (!S)' + $N + '        {' + $N; Repl = '        if (false)' + $N + '        {' + $N }
    @{ Name = 'M31 StripInnerCm defaults active'; Key = 'mmh'; Expect = 'G3'; Find = 'double StripInnerCm = -1.0;'; Repl = 'double StripInnerCm = 0.0;' }
    @{ Name = 'M32 gate header drops the generation'; Key = 'gateh'; Expect = 'G4'; Find = 'CheckCaveSignatures(void* Hmod, int32 Gen, FString&'; Repl = 'CheckCaveSignatures(void* Hmod, FString&' }
    @{ Name = 'M33 marker line dropped from NodeShuffle.cpp'; Key = 'nsmod'; Expect = 'G6'; Find = ('packet marker (' + $marker + ')====='); Repl = 'packet marker (x)=====' }
    @{ Name = 'M34 mcpcontract lint drops the rwg-0-2-0-2 note'; Key = 'lmcp'; Expect = 'G8'; Find = '# rwg-0-2-0-2: no longer the newest; pinned present'; Repl = '' } # ns-rollchat-1 (2026-11-14): the newest marker moved on; the note replaces the old literal anchor
    @{ Name = 'M35 thoriumhalf lint pins newest again'; Key = 'lthh'; Expect = 'G8'; Find = '# rwg-0-2-0-1: no longer the newest; pinned present'; Repl = '' }
    @{ Name = 'M36 (a) 0x40 form changed in the gate copy only'; Key = 'gate'; Expect = 'G9'; Find = 'GateSigCaves1130[] = { 0x48,0x8D,0x41,0x40,0xC3 };'; Repl = 'GateSigCaves1130[] = { 0x48,0x8D,0x41,0x48,0xC3 };' }
    @{ Name = 'M37 (a) 0x40 form changed in the cave copy only'; Key = 'cave'; Expect = 'G9'; Find = 'SigCaves1130[] = { 0x48,0x8D,0x41,0x40,0xC3 };'; Repl = 'SigCaves1130[] = { 0x48,0x8D,0x41,0x48,0xC3 };' }
    @{ Name = 'M38 (b) 1063 form dropped from the cave bind selection'; Key = 'cave'; Expect = 'G9'; Find = '(bCaves1130 ? SigCaves1130 : SigCaves1063) : SigCaves;'; Repl = 'SigCaves1130 : SigCaves;' }
    @{ Name = 'M39 (b) 1063 form dropped from the gate selection'; Key = 'gate'; Expect = 'G9'; Find = '(bCaves1130 ? GateSigCaves1130 : GateSigCaves1063) : GateSigCaves;'; Repl = 'GateSigCaves1130 : GateSigCaves;' }
    @{ Name = 'M40 (c) 1130 form wildcarded at @3 in both copies'; Key = 'cave'; Expect = 'G9'; Find = 'SigCaves1130[] = { 0x48,0x8D,0x41,0x40,0xC3 };'; Repl = 'SigCaves1130[] = { 0x48,0x8D,0x41,-1,0xC3 };'; Also = @{ Key = 'gate'; Find = 'GateSigCaves1130[] = { 0x48,0x8D,0x41,0x40,0xC3 };'; Repl = 'GateSigCaves1130[] = { 0x48,0x8D,0x41,-1,0xC3 };' } }
    @{ Name = 'M41 (c) selection by one byte (any byte)'; Key = 'cave'; Expect = 'G9'; Find = 'bCaves1130 = CavesBytes[I] == static_cast<uint8>(SigCaves1130[I]);'; Repl = ('bCaves1130 = ' + 'CavesBytes[3] != ' + '0x38;') }
    @{ Name = 'M42 (c) gate helper ignores byte 3'; Key = 'gate'; Expect = 'G9'; Find = 'if (B[I] != static_cast<uint8>(GateSigCaves1130[I])) { return false; }'; Repl = 'if (I != 3 && B[I] != static_cast<uint8>(GateSigCaves1130[I])) { return false; }' }
    @{ Name = 'M43 (d) generation-A CarvedCaves touched in the gate copy'; Key = 'gate'; Expect = 'G4'; Find = 'GateSigCaves[] = { 0x48,0x8D,0x41,0x28,0xC3 };'; Repl = 'GateSigCaves[] = { 0x48,0x8D,0x41,0x40,0xC3 };' }
    @{ Name = 'M44 (e) 1130 GetState form altered'; Key = 'hapi'; Expect = 'G9'; Find = 'SigGetState1130[] = { 0x0F,0xB6,0x81,0x88,0x19,0x00,0x00,0xC3 };'; Repl = 'SigGetState1130[] = { 0x0F,0xB6,0x81,-1,-1,0x00,0x00,0xC3 };' }
    @{ Name = 'M45 (e) 1130 IsContentSpawned form altered'; Key = 'hapi'; Expect = 'G9'; Find = 'SigContentSpawned1130[] = { 0x0F,0xB6,0x81,0x18,0x11,0x00,0x00,0xC3 };'; Repl = 'SigContentSpawned1130[] = { 0x0F,0xB6,0x81,0x18,0x12,0x00,0x00,0xC3 };' }
    @{ Name = 'M46 readiness second form not tried'; Key = 'hapi'; Expect = 'G9'; Find = '{ SigGetState1130, nullptr, nullptr, SigContentSpawned1130 };'; Repl = '{ nullptr, nullptr, nullptr, nullptr };' }
    @{ Name = 'M47 RWGGENERATION labels the 1130 form none'; Key = 'gate'; Expect = 'G9'; Find = 'GateSigCaves1063, UE_ARRAY_COUNT(GateSigCaves1063), GateSigCaves1130, UE_ARRAY_COUNT(GateSigCaves1130), TEXT("CarvedCaves"));'; Repl = 'GateSigCaves1063, UE_ARRAY_COUNT(GateSigCaves1063), nullptr, 0, TEXT("CarvedCaves"));' }
    @{ Name = 'M48 RWGGATE-CAVESIG drops cavesDisp'; Key = 'gate'; Expect = 'G9'; Find = "mismatchedList='%s' cavesMaskApplied=%s cavesDisp=%s ("; Repl = "mismatchedList='%s' cavesMaskApplied=%s (" }
    @{ Name = 'M49 RWGGETTERS drops the form'; Key = 'hapi'; Expect = 'G9'; Find = 'TEXT("%s%s:%s@%d/form%s/disp0x%X"), Detail.IsEmpty() ? TEXT("") : TEXT(" "), Names[I], St, Diff, Form, Disp);'; Repl = 'TEXT("%s%s:%s@%d"), Detail.IsEmpty() ? TEXT("") : TEXT(" "), Names[I], St, Diff);' }
    @{ Name = 'M50 marker rwg-0-2-1-1 dropped from the header'; Key = 'mk'; Expect = 'G9'; Find = ('        TEXT("' + $marker021 + '"),' + $N); Repl = '' }
    @{ Name = 'M51 mcpcontract C4 not rotated'; Key = 'lmcp'; Expect = 'G9'; Find = "`$own = '2026-11-15-' + 'rwg-0-2-1-1'"; Repl = "`$own = '2026-11-14-' + 'rollfirst-1'" }
    @{ Name = 'M52 scanfilter S9 not rotated'; Key = 'lsf'; Expect = 'G9'; Find = "'2026-11-15-' + 'rwg-0-2-1-1'"; Repl = "'2026-11-14-' + 'rollfirst-1'" }
    @{ Name = 'M53 rollfirst R9 still pins newest'; Key = 'lrf'; Expect = 'G9'; Find = '$hdrTags -notcontains $own -or $hdrTags -notcontains $prev)'; Repl = '$hdrTags[-1] -ne $own -or $hdrTags -notcontains $prev)' }
    @{ Name = 'M54 mask tool loses the 1130 cave form'; Key = 'tool'; Expect = 'G9'; Find = "('SigCaves1063', 'SigCaves1130')"; Repl = "'SigCaves1063'" }
    @{ Name = 'M55 DIAGNOSTICS row dropped'; Key = 'doc'; Expect = 'G9'; Find = '`cavesMaskApplied`'; Repl = 'cavesMaskApplied' }
    @{ Name = 'M26 unlisted cave half checks generation A always'; Key = 'hapi'; Expect = 'G1'; Find = 'CheckCaveSignatures(H, A.Gen, CaveMissing, CaveBad);'; Repl = 'CheckCaveSignatures(H, 1, CaveMissing, CaveBad);' }
)
$mutantFails = @(); $caught = 0
foreach ($m in $mutants) {
    $mut = @{}; foreach ($k in $T.Keys) { $mut[$k] = $T[$k] }
    $mo = @{}; foreach ($k in $others.Keys) { $mo[$k] = $others[$k] }
    if ($m.Key -eq '@other') { $first = @($mo.Keys)[0]; $mo[$first] = $mo[$first] + $N + $m.Repl }
    else {
        if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
        $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
        if ($m.Also) { if (-not $mut[$m.Also.Key].Contains($m.Also.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' second anchor absent from '$($m.Also.Key)'"; continue }; $mut[$m.Also.Key] = $mut[$m.Also.Key].Replace($m.Also.Find, $m.Also.Repl) } # rwg-0-2-1-1: a mutant that edits both copies
    }
    $res = Invoke-020Checks $mut $mo
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ } else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "RWG020 LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: RWG020 lint -- G1..G9 hold across $($T.Count) pinned files + $($others.Count) scanned sources; mutants caught $caught/$($mutants.Count)."
exit 0
