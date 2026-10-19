# RWG P2f lint (2026-10-18): void predicate via the bound VoidAt export + the killbox floor, RWGVOIDCHECK grade, RWGKILLBOX read,
# void/killbox re-deal (second SaveGame flag) and RESETTLE-KILLBOX un-settle.
# LOADS and PINS every source file the P2f packet touched. Anchors are site-unique (function bodies or full statements); needles
# naming a forbidden or pinned literal are BUILT BY CONCATENATION so this file cannot match itself.
#   F1  HeightApi: VoidAt + GetKillboxTop bound OPTIONAL -- typed, resolved only after the required-signature refusal and before
#       A.bUsable = true, byte-signature checked (VoidAt in full; GetKillboxTop with its two disp32 wildcarded since RWG 0.1.9; mismatch nulls it), status logged; the query's VoidAt runs our outside test first.
#   F2  RwgRejectV3 = RwgWetV2 first, then the GRADED VoidAt term (bit 8), then bBelowKillbox (bit 16, killbox top + margin, only while
#       known); its body compares nothing against a zero literal.
#   F3  Consumers go through v3: RwgDealOneSpot (water / void / killbox counted separately, still exactly 3 rejects), both RwgResolveAtSpawn
#       checks + the settled-Z killbox refusal, RwgDealWellSite core + satellites; Place and WellRwg no longer call RwgWetV2 directly.
#   F4  RWG's integer-index exports (per-sample iy,ix; `int 3` on a bad index) are never named as a decorated-symbol literal in any source.
#   F5  Re-deal pass: candidates re-open on the void flag only while both P2f terms are on; the void flag is written only as `|= bVoidOn`;
#       the verdict is v3; the sea refusal still precedes every flag write; the census carries voidN / killboxN.
#   F6  RWGVOIDCHECK grade expression + class (a) predicate; no repeated field in any P2f format string.
#   F7  RWGKILLBOX: both known -> the HIGHER of volume and GetKillboxTop (P2g 2026-10-18; was volume-wins); bottom before perimeter; plausibility = finite, below 0, above -200000
#       (review MF1: GlobalMinSurfaceCm is RWG's clamp floor, never the top's ceiling).
#   F8  ROLLCENSUS / WELLROLL carry the killbox fields; RwgSpawnPass runs RESETTLE-KILLBOX once, after the rock pass; the un-settle uses
#       RwgRedealLiveEntry and clears the void flag on success; the vanilla-box well fallback clears it too; RwgRedealLiveEntry
#       (Rock.cpp) clears the void flag right after the rock flag (review L4).
#   F9  bRwgVoidChecked is SaveGame on exactly the two entry structs; constants pinned by value; marker rwg-p2f-2, previous absent.
#   F10 Z-sign detector over the new file (Void.cpp): only the whitelisted measured-only / plausibility statements.
# Mutants: scratchpad mutants_p2f.py. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
if ($args.Count -gt 0) { $src = $args[0] } # mutant runs point this at a scratch copy

$files = [ordered]@{
    hapi    = 'NodeShuffle\Private\NodeShuffleMapModeHeightApi.cpp'
    void    = 'NodeShuffle\Private\NodeShuffleMapModeVoid.cpp'
    sea     = 'NodeShuffle\Private\NodeShuffleMapModeSea.cpp'
    place   = 'NodeShuffle\Private\NodeShuffleMapModePlace.cpp'
    wrwg    = 'NodeShuffle\Private\NodeShuffleWellRwg.cpp'
    esc     = 'NodeShuffle\Private\NodeShuffleWellEscalate.cpp'
    maph    = 'NodeShuffle\Public\NodeShuffleMapMode.h'
    subsysh = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    nsmod   = 'NodeShuffle\Private\NodeShuffle.cpp'
    rock    = 'NodeShuffle\Private\NodeShuffleMapModeRock.cpp'
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

# F1
if ((Count $text.hapi ('using FnVoidAt = bool (*)(const void* This, double X, ' + 'double Y);')) -ne 1) { $fails += 'F1: FnVoidAt must be typed (This, X, Y) once' }
if ((Count $text.hapi ('using FnKillboxTop = bool (*)(const void* This, ' + 'double* OutZ);')) -ne 1) { $fails += 'F1: FnKillboxTop must be typed (This, double* OutZ) once' }
if ((Count $text.hapi ('"?VoidAt@FPWSplicedWorld@@QEBA' + '_NNN@Z"')) -ne 1 -or (Count $text.hapi ('"?GetKillboxTop@APWWorldGenerator@@QEBA' + '_NAEAN@Z"')) -ne 1) { $fails += 'F1: the two decorated names must appear exactly once each, exactly as dumped' }
$tb = $text.hapi.IndexOf('void TryBind(FRwgHeightApi& A)')
if ($tb -lt 0) { $fails += 'F1: TryBind not found' } else {
    $t = $text.hapi.Substring($tb)
    $iSig = $t.IndexOf('if (bRequiredSigBad) { A.Status = ' + 'TEXT("signatureMismatch"); return; }')
    $iV = $t.IndexOf('A.VoidAt = reinterpret_cast<FnVoidAt>(::GetProc' + 'Address(H, SymVoidAt));')
    $iVc = $t.IndexOf('else if (!SigOk(reinterpret_cast<const void*>(A.VoidAt), SigVoidAt, UE_ARRAY_COUNT(SigVoidAt))) { A.VoidAt = ' + 'nullptr;')
    $iK = $t.IndexOf('A.KillboxTop = reinterpret_cast<FnKillboxTop>(::GetProc' + 'Address(H, SymKillboxTop));')
    $iKc = $t.IndexOf('else if (!SigOk(reinterpret_cast<const void*>(A.KillboxTop), SigKillboxTop, UE_ARRAY_COUNT(SigKillboxTop))) { A.KillboxTop = ' + 'nullptr;')
    $iUse = $t.IndexOf('A.bUsable = ' + 'true;')
    if ($iSig -lt 0 -or $iV -lt $iSig -or $iVc -lt $iV -or $iK -lt $iVc -or $iKc -lt $iK -or $iUse -lt $iKc) { $fails += 'F1: VoidAt / GetKillboxTop must resolve after the required-signature refusal, each signature-checked (mismatch nulls it), all before A.bUsable = true' }
    if ((Count $t ('static const int16 SigVoidAt[] = { 0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x83,0xB9,0xE8,0x02,0x00,0x00,0x00,0x0F,0x28,0xE2,0x0F,0x28,0xC1,0x48,0x8B,0xD9,0x0F,' + '0x84 };')) -ne 1) { $fails += 'F1: SigVoidAt must be the 25 build-705 bytes' }
    if ((Count $t ('static const int16 SigKillboxTop[] = { 0x48,0x8B,0x81,-1,-1,-1,-1,0x48,0x89,0x02,0x83,0xB9,-1,-1,-1,-1,0x00,0x0F,0x9F,0xC0,' + '0xC3 };')) -ne 1) { $fails += 'F1: SigKillboxTop must be the 21 build-705 bytes with exactly its two disp32 member displacements wildcarded (RWG 0.1.9)' }
    if ((Count $t ('::GetProc' + 'Address(H, SymVoidAt)')) -ne 1 -or (Count $t ('::GetProc' + 'Address(H, SymKillboxTop)')) -ne 1) { $fails += 'F1: each optional export must be resolved at exactly one site' }
}
if ($text.hapi -notmatch ('TEXT\("' + 'RWGVOIDBIND: voidAtStatus=%s ')) { $fails += 'F1: the RWGVOIDBIND status line is missing' }
$qv = Get-Body $text.hapi 'bool NodeShuffleMapMode::FRwgGroundQuery::VoidAt(double X, double Y) const'
if (-not $qv -or $qv.IndexOf('if (!HasVoid() || Outside(X, Y)) { return ' + 'false; }') -lt 0 -or $qv.IndexOf('return Api().VoidAt(S, X, ' + 'Y);') -lt $qv.IndexOf('if (!HasVoid() || Outside(X, Y))')) { $fails += 'F1: the query VoidAt must test HasVoid and our outside check BEFORE calling RWG' }
if (-not $text.hapi.Contains('return F != nullptr && S != nullptr && Api().VoidAt != ' + 'nullptr;')) { $fails += 'F1: HasVoid must require the bound export and the spliced world' }

# F2
$v3 = Get-Body $text.void ('int32 ' + $cls + 'RwgRejectV3(const NodeShuffleMapMode::FRwgGroundQuery& Q, const FVector& P)')
if (-not $v3) { $fails += 'F2: RwgRejectV3 body not found' } else {
    $iW = $v3.IndexOf('int32 Bits = RwgWetV2(Q, ' + 'P);'); $iVd = $v3.IndexOf('if (RwgEnsureVoidCheck(Q) && RwgVoidCross(Q, P.X, P.Y)) { Bits |= ' + '8; }')
    $iKb = $v3.IndexOf('const bool bBelowKillbox = bRwgKillboxKnown && FMath::IsFinite(P.Z) && P.Z < RwgKillboxTopZ + NodeShuffleMapMode::RwgKillbox' + 'MarginCm;')
    $iKs = $v3.IndexOf('if (bBelowKillbox) { Bits |= ' + '16; }')
    if ($iW -lt 0 -or $iVd -lt $iW -or $iKb -lt $iVd -or $iKs -lt $iKb) { $fails += 'F2: v3 must be v2, then the graded VoidAt term (8), then bBelowKillbox (16) on the killbox top + margin' }
    $vc = Get-Body $text.void 'bool RwgVoidCross(const NodeShuffleMapMode::FRwgGroundQuery& Q, double X, double Y)'
    if (-not $vc -or -not $vc.Contains('return Q.VoidAt(X, Y) || Q.VoidAt(X + R, Y) || Q.VoidAt(X - R, Y) || Q.VoidAt(X, Y + R) || Q.VoidAt(X, Y - ' + 'R);') -or -not $vc.Contains('const double R = NodeShuffleMapMode::RwgVoid' + 'RingCm;')) { $fails += 'F2: the dealer void term must be the 5-point cross (P and +-RwgVoidRingCm on X / Y; RWG NearVoid parity)' }
    $code = (($v3 -split "`n") | ForEach-Object { $_ -replace '//.*$', '' }) -join "`n"
    if ($code -match ('(<|<=|>|>=|==)\s*-?0(\.0*)?f?\b')) { $fails += 'F2: v3 compares something against zero -- the killbox floor is a measured top, never a Z sign' }
}

# F3
$d1 = Get-Body $text.place ('bool ' + $cls + 'RwgDealOneSpot(')
if (-not $d1) { $fails += 'F3: RwgDealOneSpot body not found' } else {
    if (-not $d1.Contains('const int32 Wet = RwgRejectV3(Q, ' + 'OutSpot);')) { $fails += 'F3: RwgDealOneSpot must deal on v3' }
    $iWa = $d1.IndexOf('if ((Wet & 7) != ' + '0)'); $iVo = $d1.IndexOf('else if ((Wet & 8) != 0) { ++RwgRc' + 'RejectedVoid; }'); $iKo = $d1.IndexOf('else { ++RwgRc' + 'RejectedKillbox; }')
    if ($iWa -lt 0 -or $iVo -lt $iWa -or $iKo -lt $iVo) { $fails += 'F3: RwgDealOneSpot must count water, then rejectedVoid, then rejectedKillbox (each once)' }
    if ((Count $d1 ('return ' + 'false;')) -ne 3) { $fails += 'F3: RwgDealOneSpot must keep exactly 3 rejects (outside, v3, slope)' }
}
$ra = Get-Body $text.place ('bool ' + $cls + 'RwgResolveAtSpawn(')
if (-not $ra) { $fails += 'F3: RwgResolveAtSpawn body not found' } else {
    $i0 = $ra.IndexOf('if (RwgRejectV3(Q, FVector(X0, Y0, ApiZ0)) != 0) { bOutWaterNoLand = true; return ' + 'false; }')
    $i1 = $ra.IndexOf('if (RwgRejectV3(Q, FVector(X1, Y1, ApiZ1)) != 0) { bOutWaterNoLand = true; return ' + 'false; }')
    $iK = $ra.IndexOf('if (bRwgKillboxKnown && Entry.Location.Z < RwgKillbox' + 'TopZ)')
    $iS = $ra.IndexOf('Entry.bRwgSettledOnTerrain = ' + 'true;')
    if ($i0 -lt 0 -or $i1 -lt $i0 -or $iK -lt $i1 -or $iS -lt $iK) { $fails += 'F3: RwgResolveAtSpawn must check v3 at both XYs, then refuse a settled Z below the killbox top, before marking it settled' }
    elseif (-not $ra.Substring($iK, $iS - $iK).Contains('bOutWaterNoLand = true;')) { $fails += 'F3: the settled-killbox refusal must report unplaceable (bOutWaterNoLand) so the caller redeals' }
}
$ws = Get-Body $text.wrwg ('bool ' + $cls + 'RwgDealWellSite(')
if (-not $ws -or -not $ws.Contains('const int32 WetCore = RwgRejectV3(Q, FVector(X, Y, ' + 'Zc));') -or -not $ws.Contains('const int32 WetSat = RwgRejectV3(Q, FVector(Sx, Sy, ' + 'Sz));')) { $fails += 'F3: RwgDealWellSite core and satellites must go through v3' }
elseif ((Count $ws ('{ ++RwgWr' + 'RejectedVoid; } else { ++RwgWrRejectedKillbox; } return false; }')) -ne 2) { $fails += 'F3: RwgDealWellSite must count rejectedVoid / rejectedKillbox at the core AND each satellite' }
if ((Count $text.place ('RwgWet' + 'V2(')) -ne 0 -or (Count $text.wrwg ('RwgWet' + 'V2(')) -ne 0) { $fails += 'F3: Place / WellRwg must not call v2 directly any more (v3 only)' }

# F4 -- structural anchor: a C string literal that OPENS with the decorated prefix of an integer-index classifier.
$forbidden = @(('"?Is' + 'Void@'), ('"?Is' + 'SeaWater@'), ('"?Is' + 'OpenSea@'), ('"?FamilyAt' + 'Sample@'), ('"?Paint' + 'At@FPWBaseMap@'), ('"?Paint' + 'LayersAt@'))
foreach ($f in (Get-ChildItem -Path $src -Recurse -Include *.cpp, *.h)) {
    $body = Get-Content -Raw -Path $f.FullName
    foreach ($n in $forbidden) { if ($body.Contains($n)) { $fails += "F4: an integer-index RWG export is named in $($f.Name) ($n...) -- those int 3 on a bad index; bind the XY export instead" } }
}

# F5
$sp = Get-Body $text.sea ('void ' + $cls + 'RwgSeaRedealPass()')
if (-not $sp) { $fails += 'F5: RwgSeaRedealPass body not found' } else {
    $iG = $sp.IndexOf('if (!Q.IsUsable() || !RwgEnsureSeaCheck(Q))'); $iOn = $sp.IndexOf('const bool bVoidOn = RwgVoidPass' + 'TermsOn();'); $iF = $sp.IndexOf('bRwgVoidChecked |' + '= bVoidOn')
    if ($iG -lt 0 -or $iOn -lt $iG -or $iF -lt $iOn) { $fails += 'F5: the sea refusal, then bVoidOn, must precede every void-flag write' }
    if ((Count $sp ('bRwgVoidChecked = ' + 'true')) -ne 0) { $fails += 'F5: the void flag may only be written as |= bVoidOn (never unconditionally)' }
    if ((Count $sp ('bRwgVoidChecked |' + '= bVoidOn;')) -ne 11) { $fails += 'F5: every sea-flag write must carry the void flag (|= bVoidOn) -- expected 11 sites' }
    if (-not $sp.Contains('if (!E.bIsNewNode || !E.bActive || (E.bRwgSeaChecked && (E.bRwgVoidChecked || !bVoidOn)) || RwgSeaDealFailed.Contains(E.EntryGuid)) { ' + 'continue; }')) { $fails += 'F5: node candidates must re-open on the void flag while both terms are on' }
    if (-not $sp.Contains('if (!W.bDestDealt || (W.bRwgSeaChecked && (W.bRwgVoidChecked || !bVoidOn)) || RwgSeaWellDealFailed.Contains(W.CorePath)) { ' + 'continue; }')) { $fails += 'F5: well candidates must re-open on the void flag while both terms are on' }
    if ((Count $sp ('RwgRejectV3(Q, ' + 'At);')) -ne 1 -or (Count $sp ('RwgRejectV3(Q, FVector(X, Y, Q.SurfaceZ(X, ' + 'Y)));')) -ne 1) { $fails += 'F5: both re-deal verdicts must be v3' }
}
if ($text.subsysh -notmatch [regex]::Escape('bool RwgVoidPassTermsOn() const { return bRwgVoidCheckDone && bRwgVoidUsable && bRwgKillbox' + 'Known; }')) { $fails += 'F5: RwgVoidPassTermsOn must require the graded void term AND the measured killbox top' }
if (-not $text.sea.Contains('durationMs=%.0f void' + 'N=%d killboxN=%d voidTermsOn=%d"),')) { $fails += 'F5: RWGSEAREDEAL must carry voidN / killboxN / voidTermsOn' }

# F6
$ec = Get-Body $text.void ('bool ' + $cls + 'RwgEnsureVoidCheck(')
if (-not $ec) { $fails += 'F6: RwgEnsureVoidCheck body not found' } else {
    if (-not $ec.Contains('bRwgVoidUsable = bVoidOk && ' + 'bDryOk;') -or -not $ec.Contains('AN >= 8 && AT * 100 >= AN * ' + '95') -or -not $ec.Contains('DryN >= 8 && DryFalse * 100 >= DryN * ' + '95')) { $fails += 'F6: the grade must be plateau >= 95 % true AND (RWG nodes + land) >= 95 % false, n >= 8 each' }
    if (-not $ec.Contains('if (FMath::Abs(Z - NodeShuffleMapMode::RwgVoid' + 'PlateauCm) <= 1.0 && !bSea && AN < PerClass)')) { $fails += 'F6: class (a) must be the plateau Z +- 1 cm AND raw InSeaVolume false' }
}
foreach ($m in [regex]::Matches(($text.void + $text.hapi), 'TEXT\("((RWGVOID|RWGKILLBOX|RESETTLE-KILLBOX)[A-Z-]*:[^"]*)"\)')) {
    $names = [regex]::Matches($m.Groups[1].Value, '([A-Za-z]+)=') | ForEach-Object { $_.Groups[1].Value }
    $dup = $names | Group-Object | Where-Object { $_.Count -gt 1 }
    if ($dup) { $fails += "F6: repeated field '$($dup[0].Name)' in one P2f format string" }
}
foreach ($lit in @(('RWGVOID' + 'CHECK: graded usable=%d '), ('RWGKILL' + 'BOX: known=%d '), ('RESETTLE-' + 'KILLBOX: pass=%d '))) { if (-not $text.void.Contains($lit)) { $fails += "F6: census line missing: $lit" } }

# F7
$kb = Get-Body $text.void ('void ' + $cls + 'RwgEnsureKillboxTop(')
if (-not $kb) { $fails += 'F7: RwgEnsureKillboxTop body not found' } else {
    # RWG P2g (2026-10-18): both known -> the HIGHER Z (max, the conservative floor); single-source arms follow it.
    $iM = $kb.IndexOf('RwgKillboxTopZ = FMath::Max(RwgKillboxTopActorZ, RwgKillboxTop' + 'ApiZ); bRwgKillboxKnown = true; Chosen = TEXT("max");')
    $iBoth = $kb.IndexOf('if (bRwgKillboxActorKnown && bRwgKillbox' + 'ApiKnown)' + "`n")
    $iA = $kb.IndexOf('else if (bRwgKillboxActorKnown) { RwgKillboxTopZ = RwgKillboxTop' + 'ActorZ;'); $iP = $kb.IndexOf('else if (bRwgKillboxApiKnown) { RwgKillboxTopZ = RwgKillboxTop' + 'ApiZ;')
    if ($iBoth -lt 0 -or $iM -lt $iBoth -or $iA -lt $iM -or $iP -lt $iA) { $fails += 'F7: with both tops known the higher Z (FMath::Max) must be used, before the actor-only and api-only arms' }
    if ($kb.Contains('FMath::Min(RwgKillboxTopActorZ, RwgKillboxTop' + 'ApiZ)')) { $fails += 'F7: the deeper (min) top must never be chosen when both are known' }
    if (-not $kb.Contains('Chosen = TEXT("actor' + 'Only");') -or -not $kb.Contains('Chosen = TEXT("api' + 'Only");') -or -not $kb.Contains('source=%s chosen=%s killbox' + 'TopActorZ')) { $fails += 'F7: RWGKILLBOX must print chosen=max|actorOnly|apiOnly' }
    if (-not $kb.Contains('TEXT("RWGKILLBOX-' + 'DISAGREE: ')) { $fails += 'F7: the DISAGREE warning must stay' }
    $iB = $kb.IndexOf('if (BottomN > 0 && Plausible(Bottom' + 'Top))'); $iPe = $kb.IndexOf('else if (PerimN > 0 && Plausible(Perim' + 'Top))')
    if ($iB -lt 0 -or $iPe -lt $iB) { $fails += 'F7: the bottom volume must be read before the perimeter (a perimeter wall may reach above the terrain)' }
    if (-not $kb.Contains('const auto Plausible = [](double Z) { return FMath::IsFinite(Z) && Z < 0.' + '0 && Z > -200000.0; };')) { $fails += 'F7: the killbox top must be finite, below 0 and above -200000 (GlobalMinSurfaceCm is a clamp floor, never its ceiling)' }
    if ($kb.Contains('Z < Ceil' + 'Z')) { $fails += 'F7: GlobalMinSurfaceCm must never bound the killbox top from above (-60000 on R7: every real top failed)' }
}

# F8
if (-not $text.place.Contains('rejectedVoid=%d rejectedKillbox=%d dealtBelowKillbox=%d killboxTopZ=%.0f killboxTopActorZ=%.0f killboxTop' + 'ApiZ=%.0f voidUsable=%d"),')) { $fails += 'F8: ROLLCENSUS must carry the P2f fields' }
# RWG P4c (2026-10-18): WELLROLL's tail grew authoredFrom*/membersFromClaim* after voidUsable; the P2f run is pinned up to the first P4c field.
if (-not $text.wrwg.Contains('rejectedVoid=%d rejectedKillbox=%d dealtBelowKillbox=%d killboxTopZ=%.0f killboxTopActorZ=%.0f killboxTop' + 'ApiZ=%.0f voidUsable=%d authoredFromLive=%d')) { $fails += 'F8: WELLROLL must carry the P2f fields' }
$spb = Get-Body $text.place ('void ' + $cls + 'RwgSpawnPass(float SpawnRadiusCm, bool& bChangedWorld)')
if ($spb) { $spb = (($spb -split "`n") | ForEach-Object { $_ -replace '//.*$', '' }) -join "`n" } # a commented-out call is not a call
if (-not $spb -or (Count $spb ('RwgKillboxResettle' + 'Pass();')) -ne 1 -or $spb.IndexOf('RwgKillboxResettle' + 'Pass();') -lt $spb.IndexOf('RwgRockOcclusion' + 'Pass();')) { $fails += 'F8: RwgSpawnPass must run RESETTLE-KILLBOX once, after the rock pass' }
$kp = Get-Body $text.void ('void ' + $cls + 'RwgKillboxResettlePass()')
if (-not $kp -or -not $kp.Contains('if (RwgRedealLiveEntry(E, TEXT("actor below the killbox ' + 'top")))') -or $kp.IndexOf('E.bRwgVoidChecked = ' + 'false;') -lt $kp.IndexOf('if (RwgRedealLiveEntry(')) { $fails += 'F8: the un-settle must tear down via RwgRedealLiveEntry and then clear the void flag' }
elseif ($kp.IndexOf('if (From.Z >= Top) { ' + 'continue; }') -lt 0 -or $kp.IndexOf('if (!IsValid(Node))') -gt $kp.IndexOf('if (From.Z >= Top)')) { $fails += 'F8: only a LIVE actor whose Z is below the top may be un-settled' }
$eb = $text.esc.IndexOf('E.bRwgSeaChecked = false; // RWG P2e review'); $ev = $text.esc.IndexOf('E.bRwgVoidChecked = ' + 'false; // RWG P2f')
if ($eb -lt 0 -or $ev -lt $eb -or ($ev - $eb) -gt 200) { $fails += 'F8: the vanilla-box well fallback must clear the void flag next to the sea flag' }
$rl = Get-Body $text.rock ('bool ' + $cls + 'RwgRedealLiveEntry(FNodeShuffleEntry& E, const TCHAR* Why)')
if (-not $rl -or -not $rl.Contains("    E.bRwgRockChecked = false;`n    E.bRwgVoidChecked = " + 'false; // RWG P2f: the new spot is re-checked by the re-deal pass')) { $fails += 'F8: RwgRedealLiveEntry must clear the void flag right after the rock flag (review L4)' }

# F9
if ((Count $text.subsysh ('UPROPERTY(Save' + 'Game) bool bRwgVoidChecked = false;')) -ne 2) { $fails += 'F9: bRwgVoidChecked must be a SaveGame property on exactly the two entry structs' }
if ((Count $text.maph ('constexpr double RwgVoidPlateauCm = -3000.' + '0;')) -ne 1 -or (Count $text.maph ('constexpr int32 RwgVoidCheckPerClass = ' + '32;')) -ne 1 -or (Count $text.maph ('constexpr double RwgKillboxMarginCm = 1000.' + '0;')) -ne 1 -or (Count $text.maph ('constexpr int32 RwgKillboxResettlePerPass = ' + '16;')) -ne 1 -or (Count $text.maph ('constexpr double RwgVoidRingCm = 1500.' + '0;')) -ne 1) { $fails += 'F9: RwgVoidPlateauCm -3000 / RwgVoidCheckPerClass 32 / RwgKillboxMarginCm 1000 / RwgKillboxResettlePerPass 16 / RwgVoidRingCm 1500 must be pinned' }
if ((Count $text.nsmod ('LOADED (2026-10-19-rwg-' + 'repclass-2)')) -ne 1 -or (Count $text.nsmod ('2026-10-19-rwg-' + 'repclass-1')) -ne 0 -or (Count $text.nsmod ('2026-10-18-rwg-' + 'p4c-2')) -ne 0 -or (Count $text.nsmod ('2026-10-18-rwg-' + 'p4c-1')) -ne 0 -or (Count $text.nsmod ('2026-10-18-rwg-' + 'p5-2')) -ne 0) { $fails += 'F9: the marker must be rwg-repclass-2 (rwg-repclass-1 absent) (RWG 0.1.9 accept list, moved from rwg-p4c-2) and the previous markers absent' }

# F10
$allowed = @(('else if (Z > 0.0 && CN < ' + 'PerClass)'), ('const auto Plausible = [](double Z) { return FMath::IsFinite(Z) && Z < 0.' + '0 && Z > -200000.0; };'))
$zNeedle = '\b[A-Za-z_.]*[Zz][A-Za-z0-9_]*\s*(<|<=|>|>=)\s*-?' + '[0-9]+(\.[0-9]*)?f?\b|\b-?[0-9]+(\.[0-9]*)?f?\s*(<|<=|>|>=)\s*[A-Za-z_.]*' + '[Zz]'
foreach ($line in ($text.void -split "`n")) {
    $c = ($line -replace '//.*$', '')
    if ($c -notmatch $zNeedle) { continue }
    $tl = $c.Trim(); $ok = $false
    foreach ($a in $allowed) { if ($tl.StartsWith($a)) { $ok = $true } }
    if (-not $ok) { $fails += "F10: a Z-against-literal comparison outside the whitelist in $($files.void): '$tl'" }
}

if ($fails.Count -gt 0) { $fails | ForEach-Object { Write-Host "FAIL $_" }; exit 1 }
Write-Host "PASS: RWG P2f lint -- F1..F10 hold across $($files.Count) loaded files"
exit 0
