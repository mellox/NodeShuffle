# THORIUMHALF lint (2026-11-11, packet ns-thorium-half-1; handoff _team/test-logs/2026-11-11-nodeshuffle-thorium-half-handoff.md).
# The CAPTURED Refined Power thorium visual is dressed at SourceScale x 0.5 in SpawnVisualRockForNode, the single site every dress
# path (spawn, adopt, capture re-dress, provisional move) reaches. SourceScale is the persisted capture; nothing reads a component's
# current scale into the computation, and neither the capture nor the resolve stores a factored value, so nothing compounds.
# LOADS: NodeShuffleSubsystem.cpp (factor, key, dress, read-back, census, capture/resolve sites), NodeShuffleSubsystem.h (counters),
#        NodeShuffleRwgSeedGas.cpp/.h (the thorium class path), NodeShuffle.cpp + NodeShuffleBuildMarkers.h (marker),
#        NodeShuffleNodeComponent.cpp + NodeShuffleResourceNode.cpp (the two DressRock bodies that SET the scale).
#   T1 the factor: one constexpr definition, value 0.5f
#   T2 the key: full-path Equals (IgnoreCase) on NodeShuffleRwgSeedGas::DefaultThoriumResource; that constant's exact path; its extern
#   T3 source, not current: SourceScale from CapturedScale/table; WantScale from SourceScale; no current-scale read in the function
#      before DressRock; the capture and resolve sites store the unfactored scale; the factor never appears in the capture function
#   T4 only thorium: bThoriumHalf = bThoriumPath && bCaptured; exactly one multiplication by the factor, under that ternary
#   T5 census: once per load on the zero path (latch + adopt flag), ungated, after EmitT72DressCensus, counters reset after print
#   T6 marker 2026-11-11-thorium-half-1 newest in the header, once in NodeShuffle.cpp, absent from the subsystem files
#   T7 both DressRock calls and the centring term take WantScale
#   T8 the counters are plain members (no reflection macro on their lines)
#   T9 exact formats of THORIUMHALF-DRESS and THORIUMHALF census
#   T10 the read-back compares against WantScale
#   T11 both DressRock bodies SET the relative scale to the passed Scale (never multiply the current one)
# Every forbidden needle is BUILT BY CONCATENATION. Mutants run in-file. Exit 0 = all hold; 1 = a rule or mutant fails; 2 = missing file.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    hdr   = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    sg    = 'NodeShuffle\Private\NodeShuffleRwgSeedGas.cpp'
    sgh   = 'NodeShuffle\Private\NodeShuffleRwgSeedGas.h'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
    mk    = 'NodeShuffle\Private\NodeShuffleBuildMarkers.h'
    nc    = 'NodeShuffle\Private\NodeShuffleNodeComponent.cpp'
    rn    = 'NodeShuffle\Private\NodeShuffleResourceNode.cpp'
}
$text = [ordered]@{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
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
$N = "`n"
$fac      = 'ThoriumHalf' + 'VisualFactor'
$marker   = '2026-11-11-' + 'thorium-half-1'
$diagGate = 'AreDiagnostics' + 'Enabled'
$curRel   = 'GetRelative' + 'Scale3D('
$curWorld = 'GetComponent' + 'Scale('
$reflRx   = 'U' + 'PROPERTY'

# ---- anchors (unique context) ----
$facDef    = '    constexpr float ' + $fac + ' = 0.5f; // ns-thorium-half-1: captured Refined Power thorium visual only (SpawnVisualRockForNode)'
$keyLine   = '    const bool bThoriumPath = ResourceClass->GetPathName().Equals(NodeShuffleRwgSeedGas::DefaultThoriumResource, ESearchCase::IgnoreCase);'
$sgDef     = '    const TCHAR* const DefaultThoriumResource = TEXT("/RefinedPower/World/ResourceNodes/Thorium/Desc_RP_Thorium.Desc_RP_Thorium_C");'
$sgExtern  = '    extern const TCHAR* const DefaultThoriumResource;'
$srcLine   = '    const FVector SourceScale = bCaptured && !CapturedScale.IsNearlyZero() ? CapturedScale' + $N + '                              : Visual ? Visual->MeshScale : FVector(2.0f, 2.0f, 2.0f);'
$halfLine  = '    const bool bThoriumHalf = bThoriumPath && bCaptured;'
$wantLine  = '    const FVector WantScale = bThoriumHalf ? SourceScale * ' + $fac + ' : SourceScale;'
$capStore  = '    Cap.MeshScale = Smc->' + $curWorld + '); // WORLD scale ' + [char]0x2014 + ' what the mesh visually renders at'
$resStore  = '    Resolved.Scale = Cap->MeshScale;'
$fnHead    = 'void ANodeShuffleSubsystem::SpawnVisualRockForNode(AFGResourceNode* Node, UClass* ResourceClass, const FGuid& EntryGuid)'
$dressComp = '    if (Comp) { Comp->DressRock(Mesh, MatPtrs, WantScale, RelOffset, RockRelQuat.Rotator()); }'
$dressLeg  = '    else { Legacy->DressRock(Mesh, MatPtrs, WantScale, RelOffset, RockRelQuat.Rotator()); }'
$center    = '    const FVector ScaledCenter(WantScale.X * MeshLocalCenter.X, WantScale.Y * MeshLocalCenter.Y, 0.0f);'
$capHead   = 'bool ANodeShuffleSubsystem::CaptureOriginalVisualIfNeeded(AFGResourceNodeBase* Node)'
$capEnd    = 'void ANodeShuffleSubsystem::RedressSpawnedOfResource(const FString& ResourceClassName)'
$t72Call   = '    EmitT72DressCensus();'
$censusIf  = '    if ((!bThoriumHalfCensusDone && bAdoptedRestoredNodes) || ThoriumHalfNameSeen > 0 || ThoriumHalfPathMatched > 0)'
$latchSet  = '        bThoriumHalfCensusDone = true;'
$resetLine = '        ThoriumHalfDressCalls = ThoriumHalfNameSeen = ThoriumHalfPathMatched = ThoriumHalfApplied = ThoriumHalfOffTarget = ThoriumHalfNoCapture = 0;'
$fmtCensus = 'TEXT("THORIUMHALF census dressCalls=%d thoriumNameSeen=%d thoriumPathMatched=%d factorApplied=%d offTarget=%d noCapture=%d factor=%.3f")'
$fmtDress  = 'TEXT("THORIUMHALF-DRESS node=%s mesh=%s srcScale=%.3f/%.3f/%.3f factor=%.3f relScale=%.3f/%.3f/%.3f worldScale=%.3f/%.3f/%.3f boundsR=%.0f boundsBottomMinusNodeZ=%.1f offTarget=%d")'
$readBack  = '        const bool bOffTarget = !IsValid(RockMC) || !RockMC->' + $curRel + ').Equals(WantScale, 0.01f);'
$setScale  = '        RockMesh->SetRelative' + 'Scale3D(Scale);'
$dressHead = @{ nc = 'void UNodeShuffleNodeComponent::DressRock('; rn = 'void ANodeShuffleResourceNode::DressRock(' }
$members   = @('    int32 ThoriumHalfDressCalls = 0;', '    int32 ThoriumHalfNameSeen = 0;', '    int32 ThoriumHalfPathMatched = 0;',
               '    int32 ThoriumHalfApplied = 0;', '    int32 ThoriumHalfOffTarget = 0;', '    int32 ThoriumHalfNoCapture = 0;',
               '    bool bThoriumHalfCensusDone = false;')

function Invoke-Checks($T) {
    $f = @()
    $s = $T.sub
    # T1
    if ((Count $s $facDef) -ne 1 -or (Count $s ('constexpr float ' + $fac)) -ne 1) { $f += '(T1) the factor is one constexpr definition of value 0.5f' }
    # T2
    if ((Count $s $keyLine) -ne 1 -or (Count $T.sg $sgDef) -ne 1 -or (Count $T.sgh $sgExtern) -ne 1) { $f += '(T2) the key is a full-path Equals on the exact thorium class path constant' }
    # T3
    $fn = Span $s $fnHead $dressComp
    if ($fn -eq '' -or (Count $fn $srcLine) -ne 1 -or (Count $fn $wantLine) -ne 1 -or -not (Ordered $fn @($srcLine, $keyLine, $halfLine, $wantLine))) { $f += '(T3) WantScale is computed from SourceScale (capture/table) after the key' }
    if ($fn -ne '' -and ((Count $fn $curRel) -ne 0 -or (Count $fn $curWorld) -ne 0)) { $f += '(T3) no current-scale read before DressRock in SpawnVisualRockForNode' }
    $cap = Span $s $capHead $capEnd
    if ($cap -eq '' -or (Count $cap $capStore) -ne 1 -or (Count $cap $fac) -ne 0 -or (Count $s $resStore) -ne 1) { $f += '(T3) capture and resolve store the unfactored scale' }
    # T4
    if ((Count $s $halfLine) -ne 1 -or (Count $s ('bThoriumHalf =')) -ne 1 -or (Count $s ('* ' + $fac)) -ne 1 -or (Count $s ($fac + ' *')) -ne 0) { $f += '(T4) the factor multiplies only under bThoriumHalf = bThoriumPath && bCaptured' }
    # T5
    $census = Span $s $censusIf $resetLine
    if ($census -eq '' -or -not (Ordered $s @($t72Call, $censusIf, $latchSet, $fmtCensus, $resetLine)) -or (Count $census $diagGate) -ne 0) { $f += '(T5) census once per load on the zero path, ungated, after the T72 census, reset after print' }
    # T6
    $hdrTags = @([regex]::Matches($T.mk, 'TEXT\("(20\d\d-[\w-]+)"\)') | ForEach-Object { $_.Groups[1].Value })
    if ($hdrTags.Count -eq 0 -or $hdrTags[-1] -ne $marker -or (Count $T.nsmod ('packet marker (' + $marker + ')=====')) -ne 1) { $f += '(T6) marker newest in the header and once in NodeShuffle.cpp' }
    foreach ($k in @('sub', 'hdr')) { if ($T[$k].Contains($marker)) { $f += "(T6) no marker literal in $k" } }
    # T7
    if ((Count $s $dressComp) -ne 1 -or (Count $s $dressLeg) -ne 1 -or (Count $s $center) -ne 1) { $f += '(T7) both DressRock calls and the centring term take WantScale' }
    # T8
    foreach ($m in $members) { if ((Count $T.hdr $m) -ne 1) { $f += "(T8) plain member line missing or duplicated: $m" } }
    if ([regex]::IsMatch($T.hdr, $reflRx + '[^\n]*\n\s*(int32|bool) b?ThoriumHalf')) { $f += '(T8) a reflection macro precedes a ThoriumHalf member' }
    # T9
    if ((Count $s $fmtCensus) -ne 1 -or (Count $s $fmtDress) -ne 1) { $f += '(T9) THORIUMHALF formats changed' }
    # T10
    if ((Count $s $readBack) -ne 1) { $f += '(T10) the read-back compares the relative scale against WantScale' }
    # T11
    foreach ($k in @('nc', 'rn')) {
        $body = Span $T[$k] $dressHead[$k] 'ForceVisible();'
        if ($body -eq '' -or (Count $body $setScale) -ne 1 -or (Count $body ('Scale3D() ' + '*')) -ne 0 -or (Count $body ('* ' + 'Scale)')) -ne 0) { $f += "(T11) DressRock in $k sets the relative scale to Scale" }
    }
    return $f
}

$fails = @(Invoke-Checks $text)
$mutants = @(
    @{ Name = 'M1 factor 0.25'; Key = 'sub'; Expect = 'T1'; Find = ($fac + ' = 0.5f;'); Repl = ($fac + ' = 0.25f;') }
    @{ Name = 'M2 key becomes a suffix match'; Key = 'sub'; Expect = 'T2'; Find = 'GetPathName().Equals(NodeShuffleRwgSeedGas::DefaultThoriumResource'; Repl = ('GetPathName().' + 'EndsWith(NodeShuffleRwgSeedGas::DefaultThoriumResource') }
    @{ Name = 'M3 key on the lead constant'; Key = 'sub'; Expect = 'T2'; Find = 'Equals(NodeShuffleRwgSeedGas::DefaultThoriumResource'; Repl = ('Equals(NodeShuffleRwgSeedGas::Default' + 'LeadResource') }
    @{ Name = 'M4 thorium path edited'; Key = 'sg'; Expect = 'T2'; Find = 'Thorium/Desc_RP_Thorium.Desc_RP_Thorium_C'; Repl = ('Thorium/Desc_RP_' + 'thorium.Desc_RP_Thorium_C') }
    @{ Name = 'M5 current scale read before the dress'; Key = 'sub'; Expect = 'T3'; Find = $wantLine; Repl = ('    const FVector NowScale = Comp ? Comp->RockMesh->' + $curRel + ') : FVector::OneVector;' + $N + $wantLine) }
    @{ Name = 'M6 factor stored at capture'; Key = 'sub'; Expect = 'T3'; Find = ('Smc->' + $curWorld + ');'); Repl = ('Smc->' + $curWorld + ') * ' + $fac + ';') }
    @{ Name = 'M7 factor at resolve'; Key = 'sub'; Expect = 'T3'; Find = $resStore; Repl = ('    Resolved.Scale = Cap->MeshScale * ' + '0.5f;') }
    @{ Name = 'M8 gate drops the path test'; Key = 'sub'; Expect = 'T4'; Find = $halfLine; Repl = ('    const bool bThoriumHalf = ' + 'bCaptured;') }
    @{ Name = 'M9 gate on the short name'; Key = 'sub'; Expect = 'T4'; Find = $halfLine; Repl = ('    const bool bThoriumHalf = bThoriumName' + 'Seen && bCaptured;') }
    @{ Name = 'M10 factor applied to every resource'; Key = 'sub'; Expect = 'T3'; Find = $wantLine; Repl = ('    const FVector WantScale = SourceScale * ' + $fac + ';') }
    @{ Name = 'M11 census loses its zero-path latch'; Key = 'sub'; Expect = 'T5'; Find = $censusIf; Repl = ('    if (ThoriumHalfNameSeen > 0 || ' + 'ThoriumHalfPathMatched > 0)') }
    @{ Name = 'M12 census gated'; Key = 'sub'; Expect = 'T5'; Find = $latchSet; Repl = ($latchSet + $N + '        if (!FNodeShuffleModule::' + $diagGate + '()) { return; }') }
    @{ Name = 'M13 marker dropped from the header'; Key = 'mk'; Expect = 'T6'; Find = ('        TEXT("' + $marker + '"),' + $N); Repl = '' }
    @{ Name = 'M14 marker literal in the subsystem'; Key = 'sub'; Expect = 'T6'; Find = $resStore; Repl = ($resStore + ' // ' + $marker) }
    @{ Name = 'M15 legacy dress takes the source scale'; Key = 'sub'; Expect = 'T7'; Find = $dressLeg; Repl = ('    else { Legacy->DressRock(Mesh, MatPtrs, ' + 'SourceScale, RelOffset, RockRelQuat.Rotator()); }') }
    @{ Name = 'M16 centring uses the source scale'; Key = 'sub'; Expect = 'T7'; Find = $center; Repl = ('    const FVector ScaledCenter(' + 'SourceScale.X * MeshLocalCenter.X, SourceScale.Y * MeshLocalCenter.Y, 0.0f);') }
    @{ Name = 'M17 counter saved'; Key = 'hdr'; Expect = 'T8'; Find = '    int32 ThoriumHalfApplied = 0;'; Repl = ('    ' + $reflRx + '(SaveGame)' + $N + '    int32 ThoriumHalfApplied = 0;') }
    @{ Name = 'M18 census field renamed'; Key = 'sub'; Expect = 'T9'; Find = 'noCapture=%d factor=%.3f")'; Repl = ('noCaptured' + '=%d factor=%.3f")') }
    @{ Name = 'M20 component DressRock scales in place'; Key = 'nc'; Expect = 'T11'; Find = $setScale; Repl = ('        RockMesh->SetRelative' + 'Scale3D(RockMesh->' + $curRel + ') * Scale);') }
    @{ Name = 'M21 legacy DressRock scales in place'; Key = 'rn'; Expect = 'T11'; Find = $setScale; Repl = ('        RockMesh->SetRelative' + 'Scale3D(RockMesh->' + $curRel + ') * Scale);') }
    @{ Name = 'M19 read-back against the source'; Key = 'sub'; Expect = 'T10'; Find = '.Equals(WantScale, 0.01f);'; Repl = ('.Equals(' + 'SourceScale, 0.01f);') }
)
$mutantFails = @(); $caught = 0
foreach ($m in $mutants) {
    $mut = [ordered]@{}; foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = @(Invoke-Checks $mut)
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++; Write-Host "RED  (ok) $($m.Name)" } else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) { foreach ($x in $all) { Write-Host "FAIL: $x" }; Write-Host "THORIUMHALF lint: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."; exit 1 }
Write-Host "PASS: THORIUMHALF lint -- T1..T11 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
