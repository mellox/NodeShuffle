# RWG P4 lint (2026-10-18): resource wells on RWG -- claim identity, API footprint deal, rigid-body move.
# LOADS and PINS every source file the P4 packet touched. Anchors are site-unique (function signature bodies or full
# statements); needles naming pinned literals are BUILT BY CONCATENATION so this file cannot match itself.
#   W1  RollLayout: the well roll is NOT wrapped in a map-mode test any more (no vanilla-only wrapper, no skip line).
#   W2  ApplyLayout: the claim re-bind runs once, behind the off-vanilla test, BEFORE ApplyWellRetype, and the well
#       apply block carries no vanilla-only wrapper.
#   W3  RollWellLayout: re-bind behind the off-vanilla test BEFORE CollectWellCensus; the mint refusal precedes the
#       new-entry append; the claim stamp is behind the off-vanilla test.
#   W4  RwgClaimGuid is assigned at exactly ONE site (the mint in RwgStampWellClaim); CorePath is written in
#       NodeShuffleWellRwg.cpp only inside RwgRebindWellClaimsOnce, which early-returns on vanilla.
#   W5  RwgDealWellSite: core outside + water, then EVERY satellite offset outside + water, then the original keep-out,
#       all before the resolve; the measured z spread gates nothing (no reject after it).
#   W6  RollWellRelocation: the vanilla draw is preserved verbatim in the else branch; the RWG draw uses the SAME
#       'WLR2' stream; the RWG test is the map-mode test.
#   W7  EscalateWellPlacement: the vanilla box draw runs whenever the RWG branch did not re-deal; the RWG branch is
#       behind the off-vanilla test.
#   W8  ApplyWellRelocation: the RWG attempt ring is behind the off-vanilla test and AFTER the vanilla spawn-radius
#       gate; WELLPLACE is behind the off-vanilla test.
#   W9  WellLink: all four runtime-adoption loops refuse a claimed RWG original.
#   W10 RunWellLocationBackstop: the off-vanilla return precedes the first actor iteration.
#   W11 Tags at line-initial position inside TEXT(": WELLROLL, WELLPLACE, WELLCLAIM (WellRwg), RWGWATERCACHE and the
#       S1 HIDECHECK fields (Place).
#   W12 The three claim fields are SaveGame properties.
#   W13 No field name repeats inside one WELL* format string (legend-collision guard).
#   W14 The LOADED marker line is present once with a dated tag.
# Exit 0 = all hold. Exit 1 = at least one does not. Exit 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
if ($args.Count -gt 0) { $src = $args[0] } # mutant runs point this at a scratch copy

$files = [ordered]@{
    subsys  = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    subsysh = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    wrwg    = 'NodeShuffle\Private\NodeShuffleWellRwg.cpp'
    wroll   = 'NodeShuffle\Private\NodeShuffleWellRoll.cpp'
    wrr     = 'NodeShuffle\Private\NodeShuffleWellRelocateRoll.cpp'
    wesc    = 'NodeShuffle\Private\NodeShuffleWellEscalate.cpp'
    wapp    = 'NodeShuffle\Private\NodeShuffleWellRelocateApply.cpp'
    wlink   = 'NodeShuffle\Private\NodeShuffleWellLink.cpp'
    wbk     = 'NodeShuffle\Private\NodeShuffleWellBackstop.cpp'
    place   = 'NodeShuffle\Private\NodeShuffleMapModePlace.cpp'
    mod     = 'NodeShuffle\Private\NodeShuffle.cpp'
}
$text = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = (Get-Content -Raw -Path $p) -replace "`r`n", "`n"
}
$fails = @()
$cls = 'ANodeShuffleSubsystem::'
$notV = 'GetMapMode() != ENodeShuffleMapMode::' + 'Vanilla'
$isV  = 'GetMapMode() == ENodeShuffleMapMode::' + 'Vanilla'

function Get-Body([string]$t, [string]$sig) {
    $i = $t.IndexOf($sig)
    if ($i -lt 0) { return $null }
    $m = [regex]::Match($t.Substring($i), "(?m)^\}$")
    if (-not $m.Success) { return $null }
    return $t.Substring($i, $m.Index)
}
function Count([string]$t, [string]$needle) { return ([regex]::Matches($t, [regex]::Escape($needle))).Count }

# W1
$stamp = 'RwgLayoutVersion = NodeShuffleMapMode::RwgLayoutVersion' + 'P2b; }'
$i1 = $text.subsys.IndexOf($stamp); $i2 = $text.subsys.IndexOf('RollWell' + 'Layout(Seed, bIsReroll);')
if ($i1 -lt 0 -or $i2 -lt 0 -or $i2 -lt $i1) { $fails += 'W1: the RollLayout stamp / RollWellLayout call not found in order' }
elseif ($text.subsys.Substring($i1, $i2 - $i1).Contains('Vanilla')) { $fails += 'W1: the well roll is wrapped in a map-mode test again' }
if ($text.subsys.Contains('RwgLogSkipOnce(0, TEXT("well ' + 'roll')) { $fails += 'W1: the well-roll RWGSKIP line is back' }

# W2
$rebA = 'if (' + $notV + ') { RwgRebindWellClaimsOnce(TEXT("' + 'apply")); }'
$iA = $text.subsys.IndexOf($rebA); $iR = $text.subsys.IndexOf('    ApplyWell' + 'Retype();')
if ((Count $text.subsys ('RwgRebindWellClaimsOnce(TEXT("' + 'apply"))')) -ne 1 -or $iA -lt 0) { $fails += 'W2: the apply re-bind must appear once, behind the off-vanilla test' }
elseif ($iR -lt 0 -or $iR -lt $iA) { $fails += 'W2: the re-bind must run BEFORE ApplyWellRetype' }
elseif ($text.subsys.Substring($iA, $iR - $iA).Contains($isV)) { $fails += 'W2: the well apply block carries a vanilla-only wrapper again' }

# W3
$wl = Get-Body $text.wroll ('void ' + $cls + 'RollWellLayout(int32 Seed, bool bIsReroll)')
if (-not $wl) { $fails += 'W3: RollWellLayout body not found' } else {
    $rr = $wl.IndexOf('if (' + $notV + ') { RwgRebindWellClaimsOnce(TEXT("' + 'roll")); RwgWellRollBegin(); }')
    $cc = $wl.IndexOf('CollectWell' + 'Census(GetWorld(), Census);')
    if ($rr -lt 0 -or $cc -lt 0 -or $rr -gt $cc) { $fails += 'W3: the roll re-bind must be behind the off-vanilla test and BEFORE CollectWellCensus' }
    $mr = $wl.IndexOf('else if (' + $notV + ' && RwgWellCore' + 'NearExistingClaim(Core->GetActorLocation(), Merged)) { ++RwgWrMintRefused; continue; }')
    $ad = $wl.IndexOf('else { Idx = Merged.Add' + 'Defaulted();')
    if ($mr -lt 0 -or $ad -lt 0 -or $mr -gt $ad) { $fails += 'W3: the mint refusal must precede the new-entry append' }
    $st = $wl.IndexOf('RwgStamp' + 'WellClaim(Merged[*StampIdx], W.Core, W.Members);')
    $sg = $wl.LastIndexOf('if (' + $notV + ')', [Math]::Max(0, $st))
    if ($st -lt 0 -or $sg -lt 0 -or ($st - $sg) -gt 600) { $fails += 'W3: the claim stamp must sit inside the off-vanilla block' }
}

# W4
$assign = 0
foreach ($k in $files.Keys) { $assign += ([regex]::Matches($text[$k], 'RwgClaim' + 'Guid\s*=[^=]')).Count }
if ($assign -ne 1) { $fails += "W4: RwgClaimGuid must be assigned at exactly one site (found $assign)" }
$mint = Get-Body $text.wrwg ('void ' + $cls + 'RwgStampWellClaim(')
if (-not $mint -or $mint -notmatch ('E\.RwgClaim' + 'Guid = FGuid::NewGuid\(\);')) { $fails += 'W4: the mint must live in RwgStampWellClaim' }
$rb = Get-Body $text.wrwg ('void ' + $cls + 'RwgRebindWellClaimsOnce(const TCHAR* Caller)')
if (-not $rb) { $fails += 'W4: RwgRebindWellClaimsOnce body not found' } else {
    if ($rb -notmatch ('if \(bRwgWellRebindDone \|\| GetMapMode\(\) == ENodeShuffleMapMode::' + 'Vanilla\) \{ return; \}')) { $fails += 'W4: the re-bind must early-return on vanilla and after its first run' }
    if ((Count $text.wrwg ('E.Core' + 'Path = ')) -ne (Count $rb ('E.Core' + 'Path = '))) { $fails += 'W4: CorePath is written outside RwgRebindWellClaimsOnce in NodeShuffleWellRwg.cpp' }
    if ($rb -notmatch 'if \(InTol > 1\) \{ \+\+Ambiguous; Best = INDEX_NONE; \}') { $fails += 'W4: two live cores in tolerance must leave the claim unbound (no double claim)' }
    if ($rb -notmatch 'UsedLive\.Contains\(i\)') { $fails += 'W4: a live core may be bound to at most one claim' }
}

# W5
$dw = Get-Body $text.wrwg ('bool ' + $cls + 'RwgDealWellSite(')
if (-not $dw) { $fails += 'W5: RwgDealWellSite body not found' } else {
    $order = @('if (Q.Outside(X, Y)) { ++RwgWrOutside; return false; }',
               'if (RwgIsPointInWaterCached(FVector(X, Y, Zc))) { ++RwgWrWater; return false; }',
               'if (Q.Outside(Sx, Sy)) { ++RwgWrSatOutside; return false; }',
               'if (RwgIsPointInWaterCached(FVector(Sx, Sy, Sz))) { ++RwgWrSatWater; return false; }',
               '++RwgWrNearOriginal;', ('++RwgWr' + 'ResolvedZ;'))
    $last = -1
    foreach ($n in $order) {
        $ix = $dw.IndexOf($n)
        if ($ix -lt 0 -or $ix -lt $last) { $fails += "W5: dealer check missing or out of order: $n"; break }
        $last = $ix
    }
    $zs = $dw.IndexOf('RwgWrZSpreadMaxCm = FMath::Max')
    if ($zs -lt 0 -or $dw.Substring($zs).Contains('return ' + 'false')) { $fails += 'W5: the z spread must be measured only, never a reject' }
}

# W6
$rw = Get-Body $text.wrr ('void ' + $cls + 'RollWellRelocation(int32 Seed, bool bIsReroll)')
if (-not $rw) { $fails += 'W6: RollWellRelocation body not found' } else {
    if ($rw -notmatch ('else\s*\{\s*Cand = FVector\(DealRng\.FRandRange\(MinX, MaxX\), DealRng\.FRandRange\(MinY, MaxY\), ProbeZ\);\s*if \(IsKnownWater' + 'Cell\(Cand\)\) \{ continue; \}\s*\}')) { $fails += 'W6: the vanilla draw must be preserved verbatim in the else branch' }
    if (-not $rw.Contains('const bool bRwgDeal = ' + $notV + ';')) { $fails += 'W6: the RWG deal test must be the map-mode test' }
    if (-not $rw.Contains('RwgDealWellSite(*RwgQ, ' + 'DealRng, RwgSatOffsets, Cand)')) { $fails += 'W6: the RWG draw must use the same WLR2 stream and the footprint offsets' }
}

# W7
$es = $text.wesc
$eb = $es.IndexOf('if (' + $notV + ')'); $ev = $es.IndexOf('if (!bRwg' + 'Redealt)')
$ed = $es.IndexOf('E.DestCoreLocation = FVector(Rng.FRandRange(BoxMin.X, BoxMax.X),')
if ($eb -lt 0 -or $ev -lt 0 -or $ed -lt 0 -or -not ($eb -lt $ev -and $ev -lt $ed)) { $fails += 'W7: the vanilla box redeal must run under !bRwgRedealt, after the off-vanilla branch' }
if ((Count $es ('bRwgRedealt = ' + 'true')) -ne 1) { $fails += 'W7: bRwgRedealt must be set true at one site (the API branch)' }

# W8
$ap = $text.wapp
$g1 = $ap.IndexOf('if (!IsLocationNearAnyPlayer(E.DestCoreLocation, SpawnRadiusCm)) { ++Deferred; continue; }')
$g2 = $ap.IndexOf('FMath::Min(SpawnRadiusCm, NodeShuffleMapMode::RwgAttempt' + 'RadiusCm)')
$g2t = $ap.LastIndexOf('if (' + $notV, [Math]::Max(0, $g2))
if ($g1 -lt 0 -or $g2 -lt 0 -or $g2 -lt $g1 -or $g2t -lt $g1) { $fails += 'W8: the RWG attempt ring must follow the vanilla spawn-radius gate, behind the off-vanilla test' }
if (-not $ap.Contains('if (' + $notV + ') { RwgEmitWell' + 'Place(Searching, PlacedNow, Deferred); }')) { $fails += 'W8: WELLPLACE must be behind the off-vanilla test' }

# W9
$sigs = @(('AFGResourceNodeFrackingCore* ' + $cls + 'FindExistingRuntimeWellCoreAt('),
          ('AFGResourceNodeFrackingSatellite* ' + $cls + 'FindExistingRuntimeWellSatelliteAt('))
foreach ($s in $sigs) {
    $b = Get-Body $text.wlink $s
    if (-not $b -or -not $b.Contains('RwgIsClaimed' + 'Original(')) { $fails += "W9: '$s' must refuse a claimed RWG original" }
}
$ad = Get-Body $text.wlink ('void ' + $cls + 'AdoptRestoredWellGroups()')
if (-not $ad -or (Count $ad ('RwgIsClaimed' + 'Original(')) -ne 2) { $fails += 'W9: AdoptRestoredWellGroups must refuse claimed RWG originals in both its core and satellite loops' }

# W10
$bk = Get-Body $text.wbk ('void ' + $cls + 'RunWellLocationBackstop(')
if (-not $bk) { $fails += 'W10: RunWellLocationBackstop body not found' } else {
    $r0 = $bk.IndexOf('if (' + $notV + ')'); $it = $bk.IndexOf('TActor' + 'Iterator')
    if ($r0 -lt 0 -or ($it -ge 0 -and $it -lt $r0) -or -not $bk.Substring($r0, 300).Contains('return;')) { $fails += 'W10: pass B must return off vanilla before any actor iteration' }
}

# W11
foreach ($tag in @('WELLROLL', 'WELLPLACE', 'WELLCLAIM')) {
    if ($text.wrwg -notmatch ('TEXT\("' + $tag + ': ')) { $fails += "W11: '$tag' line missing from NodeShuffleWellRwg.cpp" }
}
if ($text.place -notmatch ('TEXT\("' + 'RWGWATERCACHE: built volumes=%d"\)')) { $fails += 'W11: the RWGWATERCACHE line (re-review S4) is missing' }
if ($text.place -notmatch ('resolvedByPath=%d identity' + 'Ok=%d occupied=%d hidden=%d records=%d')) { $fails += 'W11: RWGHIDECHECK must carry the S1 identityOk/occupied split' }

# W12
foreach ($f in @('FGuid RwgClaimGuid;', 'FVector RwgClaimCoreLocation = FVector::ZeroVector;', 'FVector RwgClaimLocation = FVector::ZeroVector;')) {
    if ((Count $text.subsysh ('UPROPERTY(Save' + 'Game) ' + $f)) -ne 1) { $fails += "W12: '$f' must be a single SaveGame property" }
}

# W13
foreach ($m in [regex]::Matches($text.wrwg, 'TEXT\("(WELL[A-Z]+: [^"]*)"\)')) {
    $names = [regex]::Matches($m.Groups[1].Value, '(\w+)=') | ForEach-Object { $_.Groups[1].Value }
    $dup = $names | Group-Object | Where-Object { $_.Count -gt 1 }
    if ($dup) { $fails += "W13: field name repeated in a WELL* format: $($dup.Name -join ',')" }
}

# W14
if (([regex]::Matches($text.mod, 'TEXT\("===== NodeShuffle 1\.4\.0 LOADED \(\d{4}-\d\d-\d\d-[a-z0-9-]+\)====="\)')).Count -ne 1) { $fails += 'W14: the LOADED marker line must be present once with a dated tag' }

if ($fails.Count -gt 0) { $fails | ForEach-Object { Write-Host "FAIL $_" }; exit 1 }
Write-Host "PASS: RWG P4 lint -- W1..W14 hold across $($files.Count) loaded files"
exit 0
