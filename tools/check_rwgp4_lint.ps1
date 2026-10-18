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
#   W15 2026-10-18 cold-review fixes: m11 the re-bind marks each bound live core used (UsedLive.Add); m12
#       RwgIsClaimedOriginal really tests the set; F1 ApplyWellRelocation skips a sentinel-path group off vanilla
#       BEFORE the search; F2 the unbound Warning; F3 legacy entries get the legacy sentinel; F4 the roll merge
#       re-binds a sentinel satellite record before appending.
# RWG P2c (2026-10-18, floating nodes; spec _team/nodeshuffle-rwg-p2-spec.md "R4 finding"), pins C1..C11:
#   C1  RaycastGroundAt / RaycastSettle take bTerrainOnly defaulted FALSE (every vanilla caller keeps the plain trace).
#   C2  The trace helper: off -> the plain single trace; on -> terrain or player-buildable hits accepted, others ignored by component, re-traced, the cap
#       reads as no hit; the primary and the ring traces both go through it.
#   C3  RaycastSettle forwards bTerrainOnly to BOTH surface probes (primary + spiral).
#   C4  Every other RWG settle/normal site passes the RWG test (nudge, legacy settle, adopt refit, rock probe, well probe).
#   C5  RwgIsTerrainHit = owner class has TerrainChunk AND component name has Terrain (HeightApi F1 mirror).
#   C6  Resettle: the moved and kept branches write the flag; the void, occupied and wet branches do not; occupied never moves.
#   C7  RwgSpawnPass runs the resettle; the resettle only re-queues actor-less entries (bRayCasted = false), never spawns.
#   C8  Slope reject in RwgDealOneSpot and RwgDealWellSite; the mirror constant equals CliffSlopeDeg; census fields present.
#   C9  RESETTLE / RWGTERRAINSKIP / apiToTerrainDz lines at line-initial position; no repeated field in their formats.
#   C10 bRwgSettledOnTerrain (entry + well) and RwgApiToTerrainDzCm are single SaveGame properties.
#   C11 The flag is set true only in RwgResolveAtSpawn (1) and the resettle file (2: node moved + node kept; wells are observe-only).
# RWG P2c review fixes + P4b/C (2026-10-18, marker rwg-p2c-2), pins C12..C14 and W16..W17:
#   C12 MapMode.h constants pinned by value (RwgTerrainSkipMax 8, RwgResettleKeepCm 50); the void give-up counts only within 250 m
#       and still gives up (node + well); the move is read back before the entry/flag/AdoptRotRefit are touched (MF1).
#   C13 MF5 RWGTERRAINSKIP fields; S2 water-sheet skip marks the primary settle wet; MF4 abstract-instance owner accepted.
#   C14 S5: PLACEDZ prefix for the dZ census; the renamed PLACECENSUS counters.
#   W16 P4b: the satellite retry runs every pass behind the off-vanilla test, after the once-rebind and before ApplyWellRetype;
#       its body re-keys our spawned copy, uses RwgClaimedOriginals as the used set, suppresses a re-bound group, prints its census.
#   W17 Part C: the immediate apply clause requires a saved P2b+ layout (never a new game, never a legacy layout) and no stability term.
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
    rsettle = 'NodeShuffle\Private\NodeShuffleMapModeResettle.cpp'   # RWG P2c
    wfoot   = 'NodeShuffle\Private\NodeShuffleWellFootprint.cpp'     # RWG P2c
    maph    = 'NodeShuffle\Public\NodeShuffleMapMode.h'              # RWG P2c
    mm      = 'NodeShuffle\Private\NodeShuffleMapMode.cpp'           # RWG P4b part C (readiness clause)
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
    # W15 m11: Contains alone is vacuous unless the bound core is ADDED to the used set.
    if ((Count $rb ('UsedLive.' + 'Add(Best);')) -ne 1) { $fails += 'W15: the re-bind must mark each bound live core used (two claims could bind one core)' }
    # W15 F2 + F3
    if (-not $rb.Contains('TEXT("WELLCLAIM-' + 'UNBOUND: caller=%s ') -or -not $rb.Contains('UE_LOG(LogNodeShuffle, ' + 'Warning,')) { $fails += 'W15: the WELLCLAIM-UNBOUND Warning (review F2) is missing from the re-bind' }
    if (-not $rb.Contains('FString::Printf(TEXT("RWGUNBOUND_' + 'LEGACY_%d"), Legacy)')) { $fails += 'W15: legacy (claim-less) entries must get the legacy sentinel path (review F3)' }
}
# W15 m12: the predicate body must test the session set, not merely exist at its call sites (W9).
$ico = Get-Body $text.wrwg ('bool ' + $cls + 'RwgIsClaimedOriginal(const AActor* A) const')
if (-not $ico -or -not $ico.Contains('RwgClaimedOriginals.' + 'Contains(FObjectKey(A))')) { $fails += 'W15: RwgIsClaimedOriginal must test RwgClaimedOriginals' }
# W15 F1: sentinel-path skip, off vanilla, before the group is searched.
$apb = Get-Body $text.wapp ('void ' + $cls + 'ApplyWellRelocation(')
$f1 = 'if (' + $notV + ' && E.CorePath.StartsWith(TEXT("RWGUNBOUND_' + '"))) { ++Deferred; continue; }'
if (-not $apb) { $fails += 'W15: ApplyWellRelocation body not found' } else {
    $i1 = $apb.IndexOf($f1); $is = $apb.IndexOf('++Search' + 'ing;')
    if ($i1 -lt 0 -or $is -lt 0 -or $i1 -gt $is) { $fails += 'W15: ApplyWellRelocation must skip an RWGUNBOUND_ group off vanilla before searching it (review F1)' }
}
# W15 F4
if ($wl -and -not $wl.Contains('if (Reb != INDEX_NONE) { SatIdx = Reb; E.Satellites[Reb].Satellite' + 'Path = SatPath; }')) { $fails += 'W15: the roll merge must re-bind a sentinel satellite record before appending (review F4)' }

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

# ---------------- RWG P2c (C1..C11) ----------------
$isRwg = 'GetMapMode() == ENodeShuffleMapMode::' + 'Rwg'
# C1
if ((Count $text.subsysh ('struct FHitResult* OutGroundHit = nullptr,' + "`n" + '                         bool bTerrainOnly = ' + 'false) const;')) -ne 1) { $fails += 'C1: RaycastGroundAt must take bTerrainOnly defaulted false' }
if ((Count $text.subsysh ('bool* bOutWaterNoLand = nullptr, bool bTerrainOnly = ' + 'false) const;')) -ne 1) { $fails += 'C1: RaycastSettle must take bTerrainOnly defaulted false' }
# C2
$rg = Get-Body $text.subsys ('bool ' + $cls + 'RaycastGroundAt(const FVector& ProbeXY, float StartZ, const AActor* IgnoreNode,')
if (-not $rg) { $fails += 'C2: RaycastGroundAt body not found' } else {
    $off = $rg.IndexOf('if (!bTerrainOnly) { return GetWorld()->LineTrace' + 'SingleByChannel(H, S, En, ECC_WorldStatic, P); }')
    $acc = $rg.IndexOf('|| HitOwner->GetClass()->GetName().Contains(TEXT("AbstractInstance' + 'Manager"))))) { return ' + 'true; }'); $ign = $rg.IndexOf('P.AddIgnored' + 'Component(H.GetComponent());')
    if ($off -lt 0 -or $acc -lt $off -or $ign -lt $acc) { $fails += 'C2: the trace helper must keep the plain trace when off and skip non-terrain hits by component when on' }
    if ($rg -notmatch ('\+\+RwgProbeCapHits;\s*H = FHitResult\(\);\s*return ' + 'false;')) { $fails += 'C2: exhausting the skip cap must read as no hit' }
    if ((Count $rg ('if (!TraceDown(Hit, Start, End, ' + 'Params))')) -ne 1 -or (Count $rg ('if (TraceDown(RingHit, ')) -ne 1) { $fails += 'C2: the primary and ring traces must both go through the helper' }
    if ((Count $rg ('LineTrace' + 'SingleByChannel(')) -ne 2) { $fails += 'C2: a raw trace bypasses the helper in RaycastGroundAt' }
}
# C3
$rsb = Get-Body $text.subsys ('bool ' + $cls + 'RaycastSettle(FNodeShuffleEntry& Entry, const AActor* IgnoreNode, const AActor* IgnoreMesh,')
if (-not $rsb -or (Count $rsb ('nullptr, nullptr, bTerrain' + 'Only)')) -ne 2) { $fails += 'C3: RaycastSettle must forward bTerrainOnly to the primary probe and the spiral' }
# C4
$sites = @(
    @($text.subsys, ('Entry.bUnderground, &bTrySteep, nullptr, nullptr, ' + $isRwg + ')'), 'overlap nudge'),
    @($text.subsys, ('RaycastSettle(Entry, *Node, nullptr, &bWaterNoLand, ' + $isRwg + ');'), 'legacy settle near players'),
    @($text.subsys, ('bRefitWater, false, nullptr, nullptr, nullptr,' + "`n" + '                                ' + $isRwg + '))'), 'adopt rotation refit'),
    @($text.subsys, ('nullptr, ' + $isRwg + ')) // RWG P2c: terrain-mesh normal on RWG'), 'rock-dress slope probe'),
    @($text.wfoot, ('&bSteep, nullptr, nullptr, /*bTerrainOnly=*/' + $isRwg + '))'), 'well member probe'))
foreach ($st in $sites) { if ((Count $st[0] $st[1]) -ne 1) { $fails += "C4: the $($st[2]) must pass the RWG terrain-only test" } }
# C5
$th = Get-Body $text.rsettle ('bool ' + $cls + 'RwgIsTerrainHit(const FHitResult& Hit)')
if (-not $th -or -not $th.Contains('A->GetClass()->GetName().Contains(TEXT("Terrain' + 'Chunk")) && C->GetName().Contains(TEXT("Ter' + 'rain"))')) { $fails += 'C5: RwgIsTerrainHit must test TerrainChunk owner AND Terrain component' }
# C6 / C7
$rp = Get-Body $text.rsettle ('void ' + $cls + 'RwgResettlePass()')
$flag = 'E.bRwgSettledOn' + 'Terrain = true;'
if (-not $rp) { $fails += 'C6: RwgResettlePass body not found' } else {
    $mv = $rp.IndexOf('E.Location = ' + 'HitLoc;'); $mm = $rp.IndexOf('++Moved;')
    if ($mv -lt 0 -or $mm -lt $mv -or -not $rp.Substring($mv, $mm - $mv).Contains($flag)) { $fails += 'C6: the moved branch must write the settled-on-terrain flag' }
    $kp = $rp.IndexOf('<= NodeShuffleMapMode::RwgResettle' + 'KeepCm)'); $kk = $rp.IndexOf('++Kept;')
    if ($kp -lt 0 -or $kk -lt $kp -or -not $rp.Substring($kp, $kk - $kp).Contains($flag)) { $fails += 'C6: the kept branch must write the flag' }
    $vd = $rp.IndexOf('/*bTerrainOnly=*/' + 'true))'); $ve = $rp.IndexOf('const double DzMove')
    if ($vd -lt 0 -or $ve -lt $vd -or $rp.Substring($vd, $ve - $vd).Contains($flag)) { $fails += 'C6: the void branch must not write the flag' }
    if ($rp -notmatch ('if \(E\.bPinned \|\| IsNodeOccupiedAnyway\(Node\)\) \{ \+\+Occupied; RwgResettleGaveUp\.Add\(E\.EntryGuid\); ' + 'continue; \}')) { $fails += 'C6: an occupied or pinned node must be skipped without a move or a flag' }
    if ($rp -notmatch ('if \(bWater\) \{ \+\+Wet; RwgResettleGaveUp\.Add\(E\.EntryGuid\); ' + 'continue; \}')) { $fails += 'C6: wet terrain must not move the node' }
    if (-not $rp.Contains('if (E.bRayCasted) { E.bRayCasted = ' + 'false; ++Requeued; }')) { $fails += 'C7: actor-less unflagged entries must be re-queued for resolver v3' }
    if ($rp.Contains('EnsureNewNode' + 'Spawned(')) { $fails += 'C7: the resettle must not spawn' }
}
$spb = Get-Body $text.place ('void ' + $cls + 'RwgSpawnPass(float SpawnRadiusCm, bool& bChangedWorld)')
if (-not $spb -or (Count $spb ('RwgResettle' + 'Pass();')) -ne 1) { $fails += 'C7: RwgSpawnPass must run the resettle once per pass' }
$rwb = Get-Body $text.rsettle ('void ' + $cls + 'RwgResettleWells(')
if (-not $rwb -or -not $rwb.Contains('if (bInUse) { ++Occupied; RwgResettleWellGaveUp.Add(E.CorePath); ' + 'continue; }') -or -not $rwb.Contains('RwgResettleWellGaveUp.Add(E.CorePath); // probed ' + 'once') -or $rwb.Contains('SetActorLocation' + 'AndRotation(')) { $fails += 'C6: the well resettle must skip in-use groups, give up after one probe, and never move a member (observe-only, MF2)' }
# C8
$do = Get-Body $text.place ('bool ' + $cls + 'RwgDealOneSpot(')
if (-not $do -or $do -notmatch ('if \(RwgApiSlope' + 'TooSteep\(Q, X, Y, RiseRun\)\) \{ \+\+Slope; return false; \}\s*(//[^\n]*)?\s*return true;')) { $fails += 'C8: RwgDealOneSpot must reject on the API slope before accepting' }
if (-not $dw -or -not $dw.Contains('if (RwgApiSlope' + 'TooSteep(Q, X, Y, CoreRiseRun)) { ++RwgWrSlope; return false; }')) { $fails += 'C8: RwgDealWellSite must reject on the core-site API slope' }
$m1 = [regex]::Match($text.place, 'constexpr double RwgCliffSlopeDegMirror = ([0-9.]+);'); $m2 = [regex]::Match($text.subsys, 'constexpr float CliffSlopeDeg = ([0-9.]+)f;')
if (-not $m1.Success -or -not $m2.Success -or [double]$m1.Groups[1].Value -ne [double]$m2.Groups[1].Value) { $fails += 'C8: the slope mirror constant must equal CliffSlopeDeg' }
if (-not $text.place.Contains('waterApiDisagree=%d rejected' + 'Slope=%d rejectedSpacing=%d resolvedZ=%d')) { $fails += 'C8: ROLLCENSUS must carry rejectedSlope' }
if (-not $text.wrwg.Contains('cores=%d cores' + 'Ours=%d entries=%d') -or -not $text.wrwg.Contains('rejectedWater=%d rejected' + 'Slope=%d rejectedSatOutside=%d')) { $fails += 'C8: WELLROLL must carry coresOurs and rejectedSlope' }
# C9
if ($text.rsettle -notmatch ('TEXT\("' + 'RESETTLE: pass=%d checked=%d moved=%d kept=%d deferred=%d maxDzCm=%d ')) { $fails += 'C9: the RESETTLE census line is missing or reordered' }
if ($text.rsettle -notmatch ('TEXT\("' + 'RWGTERRAINSKIP: ')) { $fails += 'C9: the RWGTERRAINSKIP line is missing' }
if ($text.rsettle -notmatch ('TEXT\("' + 'PLACEDZ: apiToTerrainDz ')) { $fails += 'C9: the apiToTerrainDz census line is missing (PLACEDZ prefix, S5)' }
foreach ($src2 in @($text.rsettle, $text.place)) {
    foreach ($m in [regex]::Matches($src2, 'TEXT\("((?:RESETTLE|PLACECENSUS|PLACEDZ|ROLLCENSUS)[A-Z-]*: [^"]*)"\)')) {
        $names = [regex]::Matches($m.Groups[1].Value, '(\w+)=') | ForEach-Object { $_.Groups[1].Value }
        $dup = $names | Group-Object | Where-Object { $_.Count -gt 1 }
        if ($dup) { $fails += "C9: field name repeated in a census format: $($dup.Name -join ',')" }
    }
}
# C10
if ((Count $text.subsysh ('UPROPERTY(Save' + 'Game) bool bRwgSettledOnTerrain = false;')) -ne 2) { $fails += 'C10: bRwgSettledOnTerrain must be a SaveGame property on the entry and the well entry' }
if ((Count $text.subsysh ('UPROPERTY(Save' + 'Game) float RwgApiToTerrainDzCm = 0.0f;')) -ne 1) { $fails += 'C10: RwgApiToTerrainDzCm must be a single SaveGame property' }
# C11
$fp = ([regex]::Matches($text.place, 'bRwgSettledOn' + 'Terrain = true;')).Count; $fr = ([regex]::Matches($text.rsettle, 'bRwgSettledOn' + 'Terrain = true;')).Count
$fo = 0; foreach ($k in $files.Keys) { if ($k -ne 'place' -and $k -ne 'rsettle') { $fo += ([regex]::Matches($text[$k], 'bRwgSettledOn' + 'Terrain = true;')).Count } }
if ($fp -ne 1 -or $fr -ne 2 -or $fo -ne 0) { $fails += "C11: the flag must be set only by the resolver (1) and the resettle (2); found place=$fp resettle=$fr other=$fo" }

# ---------------- RWG P2c review fixes (C12..C14) ----------------
# C12
if ((Count $text.maph ('constexpr int32 RwgTerrainSkipMax = ' + '8;')) -ne 1) { $fails += 'C12: RwgTerrainSkipMax must stay 8 (0 turns every non-terrain first hit into a deferral)' }
if ((Count $text.maph ('constexpr double RwgResettleKeepCm = ' + '50.0;')) -ne 1) { $fails += 'C12: RwgResettleKeepCm must be 50.0 (S1)' }
if ($rp) {
    $gn = 'if (++V >= NodeShuffleMapMode::RwgResettleVoidGiveUp) { RwgResettleGaveUp.Add(E.EntryGuid); ++' + 'GaveUp; }'
    $g250 = $rp.IndexOf('if (Cands[K].Key < ' + '25000.0)'); $gi = $rp.IndexOf($gn)
    if ((Count $rp $gn) -ne 1 -or $g250 -lt 0 -or $gi -lt $g250) { $fails += 'C12: the node void give-up must exist once, inside the 250 m test (MF3)' }
    $rb = $rp.IndexOf('if (FMath::Abs(ActorZAfter - HitLoc.Z) > ' + '10.0)'); $rf = $rp.IndexOf('TEXT("RESETTLE-' + 'REFUSED: ')
    $ar = $rp.IndexOf('AdoptRotRefit.Remove(E.EntryGuid);'); $el = $rp.IndexOf('E.Location = ' + 'HitLoc;')
    if ((Count $rp 'AdoptRotRefit.Remove(E.EntryGuid);') -ne 1 -or $rb -lt 0 -or $rf -lt $rb -or $ar -lt $rf -or $el -lt $ar) { $fails += 'C12: the move readback (MF1) must precede AdoptRotRefit.Remove, which must precede the entry write' }
    if ($rp -notmatch ('\+\+MoveRefused;\s*RwgResettleGaveUp\.Add\(E\.EntryGuid\);[\s\S]{0,400}?' + 'continue;')) { $fails += 'C12: a refused move must give up and continue without writing the entry' }
}
if ($rwb) {
    $gw = 'if (++V >= NodeShuffleMapMode::RwgResettleVoidGiveUp) { RwgResettleWellGaveUp.Add(E.CorePath); ' + '}'
    if ((Count $rwb $gw) -ne 1 -or -not $rwb.Contains('++Deferred; if (Cands[K].Key < ' + '25000.0) {')) { $fails += 'C12: the well void give-up must exist once, inside the 250 m test (MF3)' }
}
# C13
if (-not $text.rsettle.Contains("mesh='%s' hitZ=%.0f hitNormalZ=%.2f boundsExtent" + 'Cm=(%.0f,%.0f,%.0f) kindsSoFar=%d')) { $fails += 'C13: RWGTERRAINSKIP must carry mesh, hitNormalZ and boundsExtentCm (MF5)' }
if ($rg) {
    if ((Count $rg ('if (Cls == 2) { bSkippedWaterSheet = ' + 'true; }')) -ne 1 -or (Count $rg ('bOutWater = bBelowDepthFloor || IsPointInWater(OutLoc) || bPrimarySkipped' + 'Water;')) -ne 1) { $fails += 'C13: a skipped water sheet must mark the primary settle wet (S2)' }
}
# C14
if (-not $text.place.Contains('noTerrainDefer=%d spawnSettlesWithAnySkip=%d terrainOnlyCapHits' + 'AllCallers=%d')) { $fails += 'C14: PLACECENSUS must carry the renamed counters (S5)' }

# ---------------- RWG P4b / part C (W16..W17) ----------------
# W16
$rtc = 'if (' + $notV + ') { RwgRetryUnbound' + 'WellSatellites(); }'
$iT = $text.subsys.IndexOf($rtc)
if ((Count $text.subsys ('RwgRetryUnbound' + 'WellSatellites();')) -ne 1 -or $iT -lt 0 -or $iA -lt 0 -or $iT -lt $iA -or $iR -lt $iT) { $fails += 'W16: the satellite retry must run once per pass, behind the off-vanilla test, after the re-bind and before ApplyWellRetype' }
$rtb = Get-Body $text.wrwg ('void ' + $cls + 'RwgRetryUnbound' + 'WellSatellites()')
if (-not $rtb) { $fails += 'W16: RwgRetryUnboundWellSatellites body not found' } else {
    if (-not $rtb.Contains('if (' + $isV + ') { return; }')) { $fails += 'W16: the retry must return on vanilla' }
    if (-not $rtb.Contains('if (!bRwgWellRebindDone || !bAdopted' + 'RestoredWells) { return; }')) { $fails += 'W16: the retry must wait for the once-rebind and the restored-well adoption' }
    if ((Count $rtb ('SuppressVanillaWellGroup(E, EWellSuppressPhase::' + 'Apply);')) -ne 1 -or -not $rtb.Contains('if (E.bGroupPlaced || (!E.bUnhidePending && WellGroupHas' + 'SuppressedMember(E)))')) { $fails += 'W16: a re-bound group that owns suppression must be suppressed in the same call' }
    if (-not $rtb.Contains('SpawnedWellSatellites.RemoveAndCopy' + 'Value(OldPath, OurCopy)') -or -not $rtb.Contains('SpawnedWellSatellites.Add(NewPath, ' + 'OurCopy);')) { $fails += 'W16: our spawned copy must be re-keyed with the record path' }
    if ((Count $rtb ('RwgClaimedOriginals.Add(FObjectKey(' + 'Hit));')) -ne 1 -or (Count $rtb ('if (RwgIsClaimedOriginal(Sat)) { ' + 'continue; }')) -ne 1) { $fails += 'W16: RwgClaimedOriginals must be the used set (checked per candidate, added per bind)' }
    if (-not $rtb.Contains('NodeShuffleMapMode::RwgWellRetry' + 'ClaimsPerPass')) { $fails += 'W16: the retry must be bounded per pass' }
    if (-not $rtb.Contains('TEXT("WELLCLAIM-' + 'RETRY: pass=%d groupsPending=%d satsBoundNow=%d satsStillUnbound=%d groupsNowComplete=%d suppressedNow=%d ')) { $fails += 'W16: the WELLCLAIM-RETRY census is missing or reordered' }
}
if ((Count $text.maph ('constexpr int32 RwgWellRetryClaimsPerPass = ' + '32;')) -ne 1) { $fails += 'W16: RwgWellRetryClaimsPerPass must be 32' }
# W17
$ci = 'const bool bClauseImmediate = bLayoutGenerated && RwgLayoutVersion >= NodeShuffleMapMode::RwgLayoutVersion' + 'P2b' + "`n" + '            && bStageReadable && bStageReady && ' + 'bFloor;'
if ((Count $text.mm $ci) -ne 1) { $fails += 'W17: the immediate clause must require a saved P2b+ layout, a Ready stage and the floor, and nothing else' }
if ((Count $text.mm ('const bool bReadyNow = bClauseA || bClauseImmediate || ' + 'bClauseB;')) -ne 1) { $fails += 'W17: bReadyNow must OR the immediate clause' }
if ((Count $text.mm ('RwgReadyBy = bClauseA ? TEXT("stage") : bClauseImmediate ? TEXT("stage-' + 'immediate") : TEXT("savedLayoutStable");')) -ne 1) { $fails += 'W17: RwgReadyBy must name the immediate clause' }
if (-not $text.mm.Contains('TEXT("RWGLAYOUT: apply latched clause=stage-' + 'immediate ')) { $fails += 'W17: the stage-immediate RWGLAYOUT line is missing' }
if ((Count $text.mm ('const bool bClauseA = bStageReadable && bStageReady && bStable && ' + 'bFloor;')) -ne 1) { $fails += 'W17: clause A (new game) must keep its stability term' }

if ($fails.Count -gt 0) { $fails | ForEach-Object { Write-Host "FAIL $_" }; exit 1 }
Write-Host "PASS: RWG P4/P2c lint -- W1..W17 and C1..C14 hold across $($files.Count) loaded files"
exit 0
