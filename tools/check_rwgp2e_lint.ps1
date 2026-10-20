# RWG P2e lint (2026-10-18): open-sea water predicate via the bound InSeaVolume export, RWGSEACHECK grade, one-time sea re-deal.
# LOADS and PINS every source file the P2e packet touched. Anchors are site-unique (function bodies or full statements); needles
# naming a forbidden or pinned literal are BUILT BY CONCATENATION so this file cannot match itself.
#   E1  HeightApi: InSeaVolume is bound OPTIONAL -- typed (this, X, Y, Z), resolved only after the required-signature refusal and
#       before A.bUsable = true, byte-signature checked (mismatch nulls it), status logged once; the query exposes HasSea/InSea, and
#       InSea calls nothing unless HasSea().
#   E2  RwgWetV2 (Sea.cpp) = graded sea term OR the volume test OR the finite inland level above the surface, and its body holds NO
#       comparison against zero at all (user rule: water only where a water surface exists above the point; never a Z-sign rule).
#   E3  Consumers go through v2: RwgDealOneSpot, both RwgResolveAtSpawn checks, RwgDealWellSite core + satellites; none of those bodies
#       calls the bare volume test any more. RWG P2f (2026-10-18): retargeted to predicate v3 (RwgRejectV3 = v2 | void | below killbox),
#       whose body calls RwgWetV2 first (pinned by check_rwgp2f_lint.ps1 F2).
#   E4  Z-SIGN DETECTOR: in Place/WellRwg/Sea every comparison of an identifier containing Z against a numeric literal (<, <=, >, >=,
#       either operand order; P2e review R3) must be one of the seven whitelisted statements (by exact line start); anything else fails.
#   E5  RwgSeaRedealPass: refuses before any flag write unless the query is usable AND the sea grade is usable; candidate filters skip
#       already-flagged entries (idempotent); placed/pinned skips precede the re-deal; the flag is set only on success; bounded per pass.
#   E6  RWGSEACHECK grade expression + the Z=0 probe + open-sea class (b) predicate; RWGSEAREDEAL census prefix as briefed; no repeated
#       field in either format.
#   E7  ROLLCENSUS / WELLROLL carry rejectedSea; RWGLAYOUT carries seaEntries; RwgSpawnPass runs the sea pass once, before its candidates.
#   E8  bRwgSeaChecked is a SaveGame property on both entry structs (exactly 2).
#   E9  Constants pinned by value (incl. the P2e review's RwgRedealTries 80 / RwgSeaRedealPerPass 64 / RwgSeaWellDealDraws 128); the marker
#       is rwg-p2e-2 and the previous marker is absent.
# Mutants: scratchpad mutants_p2e.py. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
if ($args.Count -gt 0) { $src = $args[0] } # mutant runs point this at a scratch copy

$files = [ordered]@{
    hapi    = 'NodeShuffle\Private\NodeShuffleMapModeHeightApi.cpp'
    sea     = 'NodeShuffle\Private\NodeShuffleMapModeSea.cpp'
    place   = 'NodeShuffle\Private\NodeShuffleMapModePlace.cpp'
    wrwg    = 'NodeShuffle\Private\NodeShuffleWellRwg.cpp'
    maph    = 'NodeShuffle\Public\NodeShuffleMapMode.h'
    subsysh = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    nsmod   = 'NodeShuffle\Private\NodeShuffle.cpp'
}
$text = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = (Get-Content -Raw -Path $p) -replace "`r`n", "`n"
}
$fails = @()
$cls = 'ANodeShuffleSubsystem::'
function Get-Body([string]$t, [string]$sig) {
    $i = $t.IndexOf($sig)
    if ($i -lt 0) { return $null }
    $m = [regex]::Match($t.Substring($i), "(?m)^\}$")
    if (-not $m.Success) { return $null }
    return $t.Substring($i, $m.Index)
}
function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }

# E1
if ((Count $text.hapi ('using FnInSea = bool (*)(const void* This, double X, double Y, ' + 'double Z);')) -ne 1) { $fails += 'E1: FnInSea must be typed (This, X, Y, Z) once' }
$tb = $text.hapi.IndexOf('void TryBind(FRwgHeightApi& A)')
if ($tb -lt 0) { $fails += 'E1: TryBind not found' } else {
    $t = $text.hapi.Substring($tb)
    $iSig = $t.IndexOf('if (bRequiredSigBad) { A.Status = ' + 'TEXT("signatureMismatch"); return; }')
    $iRes = $t.IndexOf('A.InSea = reinterpret_cast<FnInSea>(::GetProc' + 'Address(H, SymInSea));')
    $iChk = $t.IndexOf('else if (!SigOk(reinterpret_cast<const void*>(A.InSea), SigInSea, UE_ARRAY_COUNT(SigInSea))) { A.InSea = ' + 'nullptr;')
    $iUse = $t.IndexOf('A.bUsable = ' + 'true;')
    if ($iSig -lt 0 -or $iRes -lt $iSig -or $iChk -lt $iRes -or $iUse -lt $iChk) { $fails += 'E1: InSeaVolume must resolve after the required-signature refusal, be signature-checked (mismatch nulls it), all before A.bUsable = true' }
    if ((Count $t ('static const int16 SigInSea[] = { 0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x30,0x0F,0x57,0xC0,0x0F,0x28,0xEA,0x66,0x0F,0x2F,0xD8,0x48,0x8B,' + '0xF9 };')) -ne 1) { $fails += 'E1: SigInSea must be the 23 build-705 bytes' }
    if ((Count $t ('A.InSea = reinterpret_cast<FnInSea>(::GetProc' + 'Address(')) -ne 1) { $fails += 'E1: InSeaVolume must be resolved at exactly one site' }
}
if ($text.hapi -notmatch ('TEXT\("' + 'RWGSEABIND: status=%s ')) { $fails += 'E1: the RWGSEABIND status line is missing' }
if (-not $text.hapi.Contains('return F != nullptr && S != nullptr && Api().InSea != ' + 'nullptr;') -or -not $text.hapi.Contains('return HasSea() ? Api().InSea(S, X, Y, Z) : ' + 'false;')) { $fails += 'E1: HasSea must require the bound export and the spliced world, and InSea must call nothing without it' }

# E2
$v2 = Get-Body $text.sea ('int32 ' + $cls + 'RwgWetV2(const NodeShuffleMapMode::FRwgGroundQuery& Q, const FVector& P)')
if (-not $v2) { $fails += 'E2: RwgWetV2 body not found' } else {
    if (-not $v2.Contains('if (RwgEnsureSeaCheck(Q) && FMath::IsFinite(P.Z) && Q.InSea(P.X, P.Y, P.Z)) { Wet |= ' + '1; }')) { $fails += 'E2: v2 must OR in the graded InSeaVolume term at the surface Z' }
    if (-not $v2.Contains('if (RwgIsPointInWaterCached(P)) { Wet |= ' + '2; }')) { $fails += 'E2: v2 must OR in the lake/river volume test' }
    if (-not $v2.Contains('if (FMath::IsFinite(Wl) && Wl > P.Z + NodeShuffleMapMode::RwgWaterApiMarginCm) { Wet |= ' + '4; }')) { $fails += 'E2: v2 must OR in the finite inland level above the surface' }
    $zeroCmp = '(<|<=|>|>=)\s*-?' + '0(\.0*)?\b(?!x)|\b0(\.0*)?\s*(<|<=|>|>=)'
    $code = ($v2 -split "`n" | ForEach-Object { ($_ -replace '//.*$', '') }) -join "`n"
    if ($code -match $zeroCmp) { $fails += 'E2: v2 compares something against zero -- a point is water only where a water surface exists above it, never by a Z sign' }
}

# E3
$d1 = Get-Body $text.place ('bool ' + $cls + 'RwgDealOneSpot(')
if (-not $d1) { $fails += 'E3: RwgDealOneSpot body not found' } else {
    if (-not $d1.Contains('const int32 Wet = RwgRejectV3(Q, ' + 'OutSpot);') -or $d1 -notmatch ('\n    if \(Wet != 0\)\n    \{[\s\S]*?\+\+Water;[\s\S]*?\n        return ' + 'false;\n    \}')) { $fails += 'E3: RwgDealOneSpot must reject on v3 (if (Wet != 0) ... ++Water ... return false)' }
    if ($d1.Contains('RwgIsPointInWater' + 'Cached(')) { $fails += 'E3: RwgDealOneSpot must not call the bare volume test' }
}
$ra = Get-Body $text.place ('bool ' + $cls + 'RwgResolveAtSpawn(')
if (-not $ra -or (Count $ra ('if (RwgRejectV3(Q, FVector(X0, Y0, ApiZ0)) != 0) { bOutWaterNoLand = true; return ' + 'false; }')) -ne 1 -or (Count $ra ('if (RwgRejectV3(Q, FVector(X1, Y1, ApiZ1)) != 0) { bOutWaterNoLand = true; return ' + 'false; }')) -ne 1 -or $ra.Contains('RwgIsPointInWater' + 'Cached(')) { $fails += 'E3: both RwgResolveAtSpawn water checks must go through v2' }
$ws = Get-Body $text.wrwg ('bool ' + $cls + 'RwgDealWellSite(')
if (-not $ws -or -not $ws.Contains('const int32 WetCore = RwgRejectV3(Q, FVector(X, Y, ' + 'Zc));') -or -not $ws.Contains('const int32 WetSat = RwgRejectV3(Q, FVector(Sx, Sy, ' + 'Sz));') -or $ws.Contains('RwgIsPointInWater' + 'Cached(')) { $fails += 'E3: RwgDealWellSite core and satellites must go through v2' }

# E4
$allowed = @(('if (Z < 0.0) { ++RwgRc' + 'WaterZBelow0; }'), ('if (C.Z < 0.0) { ++RwgRc' + 'DealtBelowZ0;'), ('if (Zc < 0.0) { ++RwgWr' + 'WaterZBelow0; }'), ('if (Zc < 0.0) { ++RwgWr' + 'DealtBelowZ0;'),
             ('if (bVol && Z >= 0.0) { ++Lake' + 'Seen; }'), ('if (D.bVol && D.Z >= 0.0 && LakeN < NodeShuffleMapMode::RwgSea' + 'CheckPerClass)'), ('if (ZProbeN < ' + '8)'))
# Both directions, either operand order, any numeric literal (a sign rule can be written as >= 0, > -1.0 or 0 > Z).
$zNeedle = '\b[A-Za-z_.]*[Zz][A-Za-z0-9_]*\s*(<|<=|>|>=)\s*-?' + '[0-9]+(\.[0-9]*)?f?\b|\b-?[0-9]+(\.[0-9]*)?f?\s*(<|<=|>|>=)\s*[A-Za-z_.]*' + '[Zz]'
foreach ($k in @('place','wrwg','sea')) {
    foreach ($line in ($text[$k] -split "`n")) {
        $c = ($line -replace '//.*$', '')
        if ($c -notmatch $zNeedle) { continue }
        $tl = $c.Trim(); $ok = $false
        foreach ($a in $allowed) { if ($tl.StartsWith($a)) { $ok = $true } }
        if (-not $ok) { $fails += "E4: a Z-sign comparison outside the measured-only diagnostics in $($files[$k]): '$tl'" }
    }
}

# E5
$sp = Get-Body $text.sea ('void ' + $cls + 'RwgSeaRedealPass()')
if (-not $sp) { $fails += 'E5: RwgSeaRedealPass body not found' } else {
    $iG = $sp.IndexOf('if (!Q.IsUsable() || !RwgEnsureSeaCheck(Q))'); $iF = $sp.IndexOf('bRwgSeaChecked = ' + 'true')
    if ($iG -lt 0 -or $iF -lt $iG) { $fails += 'E5: the pass must refuse (usable query AND graded sea) before any flag write' }
    # RWG P2f: the skip also re-opens entries whose void flag is unset while both P2f terms are on (pinned again by check_rwgp2f_lint.ps1 F5).
    if (-not $sp.Contains('if (!E.bIsNewNode || !E.bActive || (E.bRwgSeaChecked && (E.bRwgVoidChecked || !bVoidOn)) || RwgSeaDealFailed.Contains(E.EntryGuid)) { ' + 'continue; }')) { $fails += 'E5: node candidates must skip flagged entries (idempotent)' }
    if (-not $sp.Contains('if (!W.bDestDealt || (W.bRwgSeaChecked && (W.bRwgVoidChecked || !bVoidOn)) || RwgSeaWellDealFailed.Contains(W.CorePath)) { ' + 'continue; }')) { $fails += 'E5: well candidates must skip flagged entries (idempotent)' }
    $iP = $sp.IndexOf('E.bRayCasted || E.bRwgSettledOnTerrain) { ++Skip' + 'Placed;'); $iPin = $sp.IndexOf('if (E.bPinned || E.bUnderground || E.bRwgCave) { ++Skip' + 'Pinned;'); <#RWG P5: cave entries skip with pinned/underground#> $iR = $sp.IndexOf('TryRedealWaterLocked' + 'Entry(E);')
    if ($iP -lt 0 -or $iPin -lt $iP -or $iR -lt $iPin) { $fails += 'E5: placed then pinned skips must precede the node re-deal' }
    if (-not $sp.Contains('if (bMoved) { ++Redealt; E.bRwgSeaChecked = ' + 'true; E.bRwgVoidChecked |= bVoidOn; }')) { $fails += 'E5: a node is flagged after a re-deal only when it moved' }
    $iWp = $sp.IndexOf('if (W.bGroupPlaced) { ++Skip' + 'Placed;'); $iWf = $sp.IndexOf('if (W.bRelocationFailed) { ++Skip' + 'FailedWell;'); $iWd = $sp.IndexOf('RwgDealWell' + 'Site(Q, Rng, Offs, Site)')
    if ($iWp -lt 0 -or $iWf -lt $iWp -or $iWd -lt $iWf) { $fails += 'E5: placed and failed wells must be skipped before the well re-deal' }
    if ((Count $sp ('int32 Left = NodeShuffleMapMode::RwgSeaRedeal' + 'PerPass;')) -ne 1 -or (Count $sp ('&& Left > ' + '0; ++I)')) -ne 2) { $fails += 'E5: both loops must be bounded by RwgSeaRedealPerPass' }
}

# E6
$ec = Get-Body $text.sea ('bool ' + $cls + 'RwgEnsureSeaCheck(')
if (-not $ec) { $fails += 'E6: RwgEnsureSeaCheck body not found' } else {
    if (-not $ec.Contains('bRwgSeaUsable = bSeaOk && ' + 'bNodeOk;') -or -not $ec.Contains('SeaN >= 8 && SeaT * 100 >= SeaN * ' + '95') -or -not $ec.Contains('NodeN >= 8 && (NodeN - NodeT) * 100 >= NodeN * ' + '95')) { $fails += 'E6: the grade must be open sea >= 95 % true AND RWG nodes >= 95 % false (n >= 8 each; the pawn is printed, not graded)' }
    if (-not $ec.Contains('Q.InSea(D.X, D.Y, ' + '0.0)')) { $fails += 'E6: the Z=0 probe is missing' }
    if (-not $ec.Contains('else if (bHaveFloor && !FMath::IsFinite(D.Wl) && FMath::Abs(D.Z - FloorZ) <= 1.0 && SeaN < NodeShuffleMapMode::RwgSea' + 'CheckPerClass)')) { $fails += 'E6: open-sea class (b) must require the floor Z AND no inland level' }
    if ($ec.IndexOf('if (!Pawn)') -lt 0 -or $ec.IndexOf('if (!Pawn)') -gt $ec.IndexOf('Draws.Reserve(')) { $fails += 'E6: the pawn test must precede the draw phase (no repeated census while deferred)' }
}
if ($text.sea -notmatch ('TEXT\("' + 'RWGSEAREDEAL: pass=%d checked=%d sea=%d redealt=%d kept=%d dealFailed=%d ')) { $fails += 'E6: the RWGSEAREDEAL census prefix is missing or reordered' }
if ($text.sea -notmatch ('TEXT\("' + 'RWGSEACHECK: graded usable=%d ')) { $fails += 'E6: the RWGSEACHECK census line is missing' }
foreach ($m in [regex]::Matches($text.sea, 'TEXT\("(RWGSEA[A-Z-]*:[^"]*)"\)')) {
    $names = [regex]::Matches($m.Groups[1].Value, '([A-Za-z]+)=') | ForEach-Object { $_.Groups[1].Value }
    $dup = $names | Group-Object | Where-Object { $_.Count -gt 1 }
    if ($dup) { $fails += "E6: repeated field '$($dup[0].Name)' in one RWGSEA format string" }
}

# E7
if (-not $text.place.Contains('dealtBelowZ0=%d rejected' + 'Sea=%d rejectedApiLevelOnly=%d waterApiDisagree=%d')) { $fails += 'E7: ROLLCENSUS must carry rejectedSea' }
if (-not $text.wrwg.Contains('rejectedSatWater=%d rejected' + 'Sea=%d rejectedNearOriginal=%d')) { $fails += 'E7: WELLROLL must carry rejectedSea' }
if (-not $text.place.Contains('rwgLayoutVersion=%d sea' + 'Entries=%d wetEntries=%d')) { $fails += 'E7: RWGLAYOUT (applying saved layout) must carry seaEntries' }
$spb = Get-Body $text.place ('void ' + $cls + 'RwgSpawnPass(float SpawnRadiusCm, bool& bChangedWorld)')
if (-not $spb -or (Count $spb ('RwgSeaRedeal' + 'Pass();')) -ne 1 -or $spb.IndexOf('RwgSeaRedeal' + 'Pass();') -gt $spb.IndexOf('TArray<TPair<double, int32>> Cands;')) { $fails += 'E7: RwgSpawnPass must run the sea pass once, before it collects spawn candidates' }

# E8
if ((Count $text.subsysh ('UPROPERTY(Save' + 'Game) bool bRwgSeaChecked = false;')) -ne 2) { $fails += 'E8: bRwgSeaChecked must be a SaveGame property on exactly the two entry structs' }

# E9
if ((Count $text.maph ('constexpr int32 RwgSeaRedealPerPass = ' + '64;')) -ne 1 -or (Count $text.maph ('constexpr int32 RwgSeaCheckPerClass = ' + '32;')) -ne 1 -or (Count $text.maph ('constexpr int32 RwgRedealTries = ' + '80;')) -ne 1 -or (Count $text.maph ('constexpr int32 RwgSeaWellDealDraws = ' + '128;')) -ne 1) { $fails += 'E9: RwgSeaRedealPerPass 64 / RwgSeaCheckPerClass 32 / RwgRedealTries 80 / RwgSeaWellDealDraws 128 must be pinned' }
if ((Count $text.nsmod ('LOADED (2026-10-19-rwg-' + 'deal-1)')) -ne 1 -or (Count $text.nsmod ('2026-10-19-rwg-' + 'density-2)')) -ne 0 -or (Count $text.nsmod ('2026-10-19-rwg-' + 'rockcache-2')) -ne 0 -or (Count $text.nsmod ('2026-10-18-rwg-' + 'p4c-2')) -ne 0 -or (Count $text.nsmod ('2026-10-18-rwg-' + 'p4c-1')) -ne 0 -or (Count $text.nsmod ('2026-10-18-rwg-' + 'p5-2')) -ne 0) { $fails += 'E9: the marker must be rwg-repclass-3 (rwg-repclass-2 absent) (RWG 0.1.9 accept list, moved from rwg-p4c-2) and the previous markers absent' }

if ($fails.Count -gt 0) { $fails | ForEach-Object { Write-Host "FAIL $_" }; exit 1 }
Write-Host "PASS: RWG P2e lint -- E1..E9 hold across $($files.Count) loaded files"
exit 0
