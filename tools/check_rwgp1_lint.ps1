# RWG P1 (2026-10-14) -- THE MAP-MODE / STORE-ISOLATION LINT.
#
# WHAT P1 PROMISES, AND WHAT EACH PIN STOPS:
#   R1  Off-vanilla roll is OFF: the parked constant is false, exactly once. P2 flips it -- deliberately,
#       by changing THIS pin in the same commit. A silent flip is the one-line shortcut this forbids.
#   R2  RefreshTick returns into RwgObserveTick off vanilla BEFORE the starter-node capture and BEFORE its
#       IsWorldReadyForRoll() call. Presence is not enough; this is an ORDER inside one function body.
#   R3  Store isolation, both stores, both directions (SYMMETRY): each Ensure*Loaded body tests the map mode
#       BEFORE its load latch is set and before any file path is built; each Flush*IfDirty body tests it
#       BEFORE its dirty check and before SaveStringToFile. A guard on one store only is the half-fix.
#   R4  The vanilla and RWG package literals live once, in NodeShuffleMapMode.h, and GetMapMode compares
#       with Equals (no fuzzy StartsWith/Contains on the package that would hedge a miss into "vanilla").
#   R5  IsWorldReadyForReroll ANDs the RWG latch off vanilla (spec B4), at its single site.
#   R6  The R1 lines exist with their tags at line-initial position inside TEXT(" and the late-node gate
#       still calls IsWorldReadyForReroll (the one site R5 relies on).
#   R7  No format string in the new files carries a legend: a `(...` parenthetical holding name=value
#       would collide with the field it explains (this repo has shipped that trap five times).
# RWG P2a (2026-10-17, _team/nodeshuffle-rwg-p2-spec.md §1/§3/Addendum 2) adds:
#   R8  Stage readability: RwgReadGeneratorState returns on the progress-stage STRING, and FindGeneratorFields
#       returns stage readability -- P1 returned the absent EPWGeneratorState hit, so the stage was dead code.
#   R9  Readiness v2 has no quiescence-only path: the ready predicate ANDs stage-readable, stage-Ready, stable
#       and floor; the stable counter advances only in the stage-Ready branch and the first Ready tick zeroes
#       it; the P1 quiescence constant and counter are gone.
#   R10 RWGWAIT names the refusing clause: its census format string ends in the gatedBy field.
#   R11 RWGHEIGHTAPI: the accepted RWG build is one code constant; each decorated export name lives once, in
#       the HeightApi file only; TryBind refuses on version BEFORE it refuses on exports and allows calls only
#       after both; no RWG call precedes the callsAllowed guard in RwgLogHeightApi; no library is linked.
# RWG P2b (2026-10-17, P2 spec §2 + "R2 measurements": roll ON for mode=rwg, placement v2, apply-on-load) changes R1
# (pinned TRUE now, deliberately), R2 (RefreshTick falls through on RWG once RwgPipelineMayRun allows), R9 (clause A is
# the v2 predicate; clause B applies a SAVED layout only), R10, and adds:
#   R12 RwgPipelineMayRun: the map/roll/ready guard is its FIRST test; the legacy-layout check sits after it and
#       re-rolls through RollLayout(Seed, true) only after the RWGLAYOUT legacy line.
#   R13 RwgLayoutVersion is stamped at ONE site, inside the RWG-only test in RollLayout; nothing else assigns it.
#   R14 The RWG deal rejects outside then resolves Z then water-rejects, all BEFORE the spacing scan; no cave spots
#       off vanilla (GenerateNewLocations nulls the cave output first); the volume test decides water.
#   R15 Spawn: on RWG the settle block asks RwgResolveAtSpawn first and RaycastSettle only when the API was not
#       used; the ApplyLayout loop defers RWG actor creation to the capped, nearest-first RwgSpawnPass.
#       RWG P2c (2026-10-18) CHANGED THIS PIN DELIBERATELY: the P2b rule "a trace miss or a far hit spawns at the API Z" was
#       measured wrong (63 % of spawns floated or sank). R15 now pins resolver v3: the terrain-only RaycastSettle runs, a
#       failed settle returns false (the caller defers or redeals), and the ONLY `return true` follows the settled-on-terrain
#       flag write -- so no path can hand the caller the API Z as a final position.
#   R16 Cave learning, the vanilla depth floor and the redeal cave branch are gated to vanilla at their sites.
# RWG P2b-2 (2026-10-17 cold review fixes): R9 clause B also needs t >= RwgApplyFallbackSeconds and the saved-records
#   floor (MF1), and its counter resets on any change (M16); R15 pins the API agreement test BEFORE RaycastSettle (MF2);
#   R16 also pins rock pairing/hiding to vanilla (M17) and IsPointInWater's off-vanilla cached route (SF3).
#   R17 The byte-signature gate refuses after the export refusal and before calls are allowed; the ground query
#       makes no RWG call unless the binding is usable.
#
# Needles that name forbidden or pinned literals are BUILT BY CONCATENATION so this file cannot match itself.
# Exit 0 = all hold. Exit 1 = at least one does not. Exit 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
if ($args.Count -gt 0) { $src = $args[0] } # RWG P2c: mutant runs point this at a scratch copy

$files = [ordered]@{
    mapcpp  = 'NodeShuffle\Private\NodeShuffleMapMode.cpp'
    r1cpp   = 'NodeShuffle\Private\NodeShuffleMapModeR1.cpp'
    maph    = 'NodeShuffle\Public\NodeShuffleMapMode.h'
    subsys  = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    subsysh = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    late    = 'NodeShuffle\Private\NodeShuffleLateNodes.cpp'
    hapi    = 'NodeShuffle\Private\NodeShuffleMapModeHeightApi.cpp'
    place   = 'NodeShuffle\Private\NodeShuffleMapModePlace.cpp'
}
$text = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) {
        Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist -- this suite cannot check what it cannot read."
        exit 2
    }
    $text[$k] = Get-Content -Raw -Path $p
}
$fails = @()

# Body of one function, anchored on its full signature line (unique to its site), up to the next
# top-level "}\n" at column 0.
function Get-Body([string]$t, [string]$sig) {
    $i = $t.IndexOf($sig)
    if ($i -lt 0) { return $null }
    $m = [regex]::Match($t.Substring($i), "(?m)^\}\r?$")
    if (-not $m.Success) { return $null }
    return $t.Substring($i, $m.Index)
}
$cls = 'ANodeShuffle' + 'Subsystem::'
$notVanilla = 'GetMapMode() != ENodeShuffleMapMode::' + 'Vanilla'

# R1
$rollConst = 'constexpr bool bRwgRoll' + 'Enabled = true;'
$n = ([regex]::Matches($text.maph, [regex]::Escape($rollConst))).Count
if ($n -ne 1) { $fails += "R1: the off-vanilla roll constant must appear exactly once as true in NodeShuffleMapMode.h (P2b; found $n)" }
foreach ($k in @('mapcpp','r1cpp','subsys','subsysh','hapi','place')) {
    if ($text[$k] -match ('bRwgRoll' + 'Enabled\s*=')) { $fails += "R1: '$($files[$k])' assigns the roll constant; it lives only in NodeShuffleMapMode.h" }
}

# R2
$tick = Get-Body $text.subsys ('void ' + $cls + 'RefreshTick()')
if (-not $tick) { $fails += 'R2: RefreshTick body not found' } else {
    $obs  = $tick.IndexOf('RwgObserve' + 'Tick();')
    $cap  = $tick.IndexOf('if (!bPlayerStart' + 'Captured && !bLayoutGenerated)')
    $roll = $tick.IndexOf('IsWorldReady' + 'ForRoll()')
    $guard = $tick.IndexOf($notVanilla)
    if ($obs -lt 0 -or $guard -lt 0) { $fails += 'R2: RefreshTick lacks the off-vanilla observe branch' }
    elseif ($cap -lt 0 -or $roll -lt 0) { $fails += 'R2: RefreshTick lost the starter capture or the roll gate (pins cannot order what is missing)' }
    elseif (-not ($guard -lt $obs -and $obs -lt $cap -and $cap -lt $roll)) { $fails += 'R2: the observe branch must precede the starter capture and the IsWorldReadyForRoll gate' }
    else {
        $after = $tick.Substring($obs, 80)
        if ($after -notmatch ('^RwgObserveTick\(\);\s*if \(!RwgPipeline' + 'MayRun\(\)\) \{ return; \}')) { $fails += 'R2: RwgObserveTick() must be followed immediately by the RwgPipelineMayRun return gate' }
    }
}

# R3
$stores = @(
    @{ fn = 'EnsureWaterGridLoaded() const'; latch = 'bWaterGridLoaded = true;'; io = 'FPaths::Combine(' },
    @{ fn = 'EnsureCaveStoreLoaded() const'; latch = 'bCaveStoreLoaded = true;'; io = 'FPaths::Combine(' },
    @{ fn = 'FlushWaterGridIfDirty() const'; latch = 'if (!bWaterGridDirty) { return; }'; io = 'SaveStringToFile(' },
    @{ fn = 'FlushCaveStoreIfDirty() const'; latch = 'if (!bCaveStoreDirty) { return; }'; io = 'SaveStringToFile(' }
)
foreach ($s in $stores) {
    $b = Get-Body $text.subsys ('void ' + $cls + $s.fn)
    if (-not $b) { $fails += "R3: body of $($s.fn) not found"; continue }
    $g = $b.IndexOf($notVanilla)
    $l = $b.IndexOf($s.latch)
    $io = $b.IndexOf($s.io)
    if ($g -lt 0) { $fails += "R3: $($s.fn) has no map-mode guard" }
    elseif ($l -lt 0 -or $io -lt 0) { $fails += "R3: $($s.fn) lost its latch/dirty check or its file I/O (pin anchors gone)" }
    elseif (-not ($g -lt $l -and $g -lt $io)) { $fails += "R3: $($s.fn) tests the map mode AFTER its latch/dirty check or file I/O" }
}
$gen = Get-Body $text.subsys ('void ' + $cls + 'WriteGeneratedLocationsJson(const TArray<FVector>& Locations) const')
if (-not $gen -or $gen.IndexOf($notVanilla) -lt 0 -or $gen.IndexOf($notVanilla) -gt $gen.IndexOf('SaveStringToFile(')) {
    $fails += 'R3: WriteGeneratedLocationsJson must test the map mode before writing'
}
# R3b: each off-vanilla guard block must END in `return;` directly before the vanilla path (R3 pins order, not the return).
$tails = [ordered]@{
    'EnsureWaterGridLoaded() const' = 'return;\s*\}\s*bWaterGridLoaded = true;\s*// 1\. The user'
    'EnsureCaveStoreLoaded() const' = 'return;\s*\}\s*bCaveStoreLoaded = true;\s*// 1\. The user'
    'FlushWaterGridIfDirty() const' = 'WaterGridNewSamples = 0;\s*return;\s*\}\s*if \(!bWaterGridDirty\) \{ return; \}'
    'FlushCaveStoreIfDirty() const' = 'CaveStoreNewRecords = 0;\s*return;\s*\}\s*if \(!bCaveStoreDirty\) \{ return; \}'
}
foreach ($fn in $tails.Keys) {
    $b = Get-Body $text.subsys ('void ' + $cls + $fn)
    if (-not $b -or $b -notmatch $tails[$fn]) { $fails += "R3b: $fn guard block does not end in return; before the vanilla path" }
}

# R4
$van = '/Game/FactoryGame/Map/GameLevel01/' + 'Persistent_Level'
$rwg = '/RandomWorldGenerator/Maps/ProceduralWorldLevel/' + 'ProceduralWorldLevel'
foreach ($lit in @($van, $rwg)) {
    $inH = ([regex]::Matches($text.maph, [regex]::Escape($lit))).Count
    $elsewhere = 0
    foreach ($k in @('mapcpp','r1cpp','subsys','subsysh','late','hapi','place')) { $elsewhere += ([regex]::Matches($text[$k], [regex]::Escape($lit))).Count }
    if ($inH -ne 1 -or $elsewhere -ne 0) { $fails += "R4: package literal '$lit' must appear once, in NodeShuffleMapMode.h only (h=$inH elsewhere=$elsewhere)" }
}
$gm = Get-Body $text.mapcpp ('ENodeShuffleMapMode ' + $cls + 'GetMapMode() const')
if (-not $gm) { $fails += 'R4: GetMapMode body not found' } else {
    if (([regex]::Matches($gm, 'MapPackageName\.Equals\(')).Count -ne 2) { $fails += 'R4: GetMapMode must classify with exactly two Equals comparisons' }
    if ($gm -match ('MapPackageName\.(Starts' + 'With|Con' + 'tains|Ends' + 'With)')) { $fails += 'R4: GetMapMode uses a fuzzy package match' }
}
if ($gm -and $gm -notmatch ('Equals\(NodeShuffleMapMode::VanillaPackage\(\), ESearchCase::IgnoreCase\)\)\s*\{\s*MapMode = ENodeShuffleMapMode::' + 'Vanilla;')) { $fails += 'R4b: the vanilla package literal no longer classifies as Vanilla' }

# R5
$rr = Get-Body $text.subsys ('bool ' + $cls + 'IsWorldReadyForReroll() const')
if (-not $rr -or $rr.IndexOf($notVanilla + ' && !bRwg' + 'Ready') -lt 0) { $fails += 'R5: IsWorldReadyForReroll lacks the off-vanilla readiness AND' }

# R6
foreach ($tag in @('MAPMODE','RWGWAIT','RWGREADY','RWGPROBE','RWGGEN','RWGOBSERVE')) {
    if ($text.mapcpp -notmatch ('TEXT\("' + $tag + '[: ]')) { $fails += "R6: '$tag' line missing from NodeShuffleMapMode.cpp" }
}
foreach ($tag in @('RWGCOLLISION','RWGWATER','RWGWATER2','RWGROCK2','RWGSTAGE')) {
    if ($text.r1cpp -notmatch ('TEXT\("' + $tag + '[: ]')) { $fails += "R6: '$tag' line missing from NodeShuffleMapModeR1.cpp" }
}
if ($text.hapi -notmatch ('TEXT\("' + 'RWGHEIGHTAPI[: ]')) { $fails += "R6: 'RWGHEIGHTAPI' line missing from NodeShuffleMapModeHeightApi.cpp" }
if ($text.late.IndexOf('!IsWorldReady' + 'ForReroll()') -lt 0) { $fails += 'R6: the late-node first-scan gate no longer calls IsWorldReadyForReroll (R5 would not reach it)' }

# R7: inside every TEXT("...") of the new files, no parenthetical contains name=value.
foreach ($k in @('mapcpp','r1cpp','hapi','place')) {
    foreach ($m in [regex]::Matches($text[$k], 'TEXT\("((?:[^"\\]|\\.)*)"\)')) {
        $fmt = $m.Groups[1].Value
        foreach ($p in [regex]::Matches($fmt, '\(([^()]*)\)')) {
            if ($p.Groups[1].Value -match '[A-Za-z]=') { $fails += "R7: '$($files[$k])' format string has name=value inside a parenthetical: ($($p.Groups[1].Value))" }
        }
    }
}

# R8
$rgs = Get-Body $text.mapcpp ('bool ' + $cls + 'RwgReadGeneratorState(FString& OutGenState, FString& OutProgressStage) const')
if (-not $rgs -or $rgs -notmatch ('return FindGeneratorFields\(Gen, OutGenState, OutProgressStage\) && !OutProgress' + 'Stage\.IsEmpty\(\);')) {
    $fails += 'R8: RwgReadGeneratorState must return readability keyed on the progress-stage string'
}
$fgf = $text.mapcpp.IndexOf('bool FindGeneratorFields(UObject* Obj, FString& OutGen, FString& OutStage)')
$fgfEnd = if ($fgf -ge 0) { $text.mapcpp.IndexOf('APawn* NodeShuffleMapMode::FirstPlayerPawn', $fgf) } else { -1 }
if ($fgf -lt 0 -or $fgfEnd -lt 0) { $fails += 'R8: FindGeneratorFields not found' } else {
    $fgfBody = $text.mapcpp.Substring($fgf, $fgfEnd - $fgf)
    if ($fgfBody -notmatch ('return !Out' + 'Stage\.IsEmpty\(\);') -or $fgfBody -match ('return b' + 'Gen;')) {
        $fails += 'R8: FindGeneratorFields must return stage readability, not the EPWGeneratorState hit'
    }
}

# R9
$ot = Get-Body $text.mapcpp ('void ' + $cls + 'RwgObserveTick()')
if (-not $ot) { $fails += 'R9: RwgObserveTick body not found' } else {
    if ($ot -notmatch ('const bool bClauseA = bStageReadable && bStageReady && bStable && ' + 'bFloor;')) { $fails += 'R9: clause A must AND stage-readable, stage-Ready, stable and floor' }
    if ($ot -notmatch ('const bool bClauseB = bLayout' + 'Generated && T >= NodeShuffleMapMode::RwgApplyFallbackSeconds\s*&& RwgStableAnyStage >= NodeShuffleMapMode::RwgApplyStableTicks && Nodes >= Saved' + 'Floor;')) { $fails += 'R9: clause B must require a saved layout, the world-time floor, the any-stage stable count and the saved-records floor' }
    if ($ot -notmatch ('const int32 SavedFloor = FMath::Max\(NodeShuffleMapMode::RwgMinNodes, OriginalNode' + 'Record\.Num\(\) / 2\);')) { $fails += 'R9: the saved-layout floor must be max(RwgMinNodes, half the hide records)' }
    # RWG P4b part C (2026-10-18, rwg-p2c-2): changed DELIBERATELY -- a saved P2b+ layout may latch at the first Ready read
    # (bClauseImmediate; its shape is pinned by check_rwgp4_lint.ps1 W17). A new game still needs clause A.
    if ($ot -notmatch ('const bool bReadyNow = bClauseA \|\| bClauseImmediate \|\| ' + 'bClauseB;')) { $fails += 'R9: the latch must be clause A, the saved-layout immediate clause or clause B, nothing else' }
    # RWG P4 (2026-10-18): P2b re-review S2, verbatim.
    if ($ot -notmatch ('const bool bFloor = bLayoutGenerated \? \(RwgAnyLoadedNodesAtFloor\(\) && Nodes >= Saved' + 'Floor\) : IsWorldReadyForRoll\(\);')) { $fails += 'R9: on a saved layout the clause-A floor must also require the saved-records floor' }
    if ($ot -notmatch ('RwgStableAnyStage = bLayout' + 'Generated \? \(\(Delta == 0\) \? RwgStableAnyStage \+ 1 : 0\) : 0;')) { $fails += 'R9: the clause-B counter must count only on a save holding a layout and reset on any count change' }
    if ($ot -notmatch ('else if \(bStageReady\)\s*\{\s*RwgStableSinceReady = \(Delta == 0\) \? RwgStable' + 'SinceReady \+ 1 : 0;')) { $fails += 'R9: the stable counter must advance only in the stage-Ready branch' }
    if ($ot -notmatch ('RwgFirstReadyT = T;\s*RwgStable' + 'SinceReady = 0;')) { $fails += 'R9: the first Ready tick must zero the stable counter' }
    if (([regex]::Matches($ot, 'RwgStableSinceReady\s*=\s*\(')).Count -ne 1) { $fails += 'R9: the stable counter is advanced at more than one site' }
}
foreach ($k in @('mapcpp','r1cpp','subsysh','maph')) {
    if ($text[$k] -match ('RwgStableTicks' + 'WithGenerator|\bRwgStable' + 'Ticks\b')) { $fails += "R9: '$($files[$k])' still carries the P1 quiescence counter or constant" }
}
if ($text.mapcpp -match ('by=quies' + 'cence|TEXT\("quies' + 'cence"\)')) { $fails += 'R9: a quiescence readiness branch is back in NodeShuffleMapMode.cpp' }

# R10
if ($text.mapcpp -notmatch ('TEXT\("RWGWAIT: t=%\.0fs [^"]* gated' + 'By=%s savedLayout=%d stableAnyStage=%d/%d"\)')) { $fails += 'R10: the RWGWAIT census format must carry gatedBy then the clause-B fields' }

# R11
$acc = 'constexpr int32 RwgHeightApiAccepted' + 'Build = 705;'
if (([regex]::Matches($text.hapi, [regex]::Escape($acc))).Count -ne 1) { $fails += 'R11: the accepted RWG build constant must appear exactly once in the HeightApi file' }
$syms = @(
    ('?Get@APWWorld' + 'Generator@@SAPEAV1@PEAVUWorld@@@Z'),
    ('?GetField@APWWorld' + 'Generator@@QEBA?AV?$TSharedPtr@$$CBVFPWDensityField@@$00@@XZ'),
    ('?GetSpliced@FPWDensity' + 'Field@@QEBAAEBVFPWSplicedWorld@@XZ'),
    ('?SurfaceHeightCm@FPWDensity' + 'Field@@QEBANNN@Z'),
    ('?IsOutsideWorld@FPWDensity' + 'Field@@QEBA_NNN@Z'),
    ('?WaterLevelCmAt@FPWSpliced' + 'World@@QEBANNNH@Z')
)
foreach ($sym in $syms) {
    $inH = ([regex]::Matches($text.hapi, [regex]::Escape('"' + $sym + '"'))).Count
    $else = 0
    foreach ($k in @('mapcpp','r1cpp','subsys','subsysh','maph','late','place')) { $else += ([regex]::Matches($text[$k], [regex]::Escape($sym))).Count }
    if ($inH -ne 1 -or $else -ne 0) { $fails += "R11: decorated export '$sym' must appear once, as a literal in the HeightApi file only (hapi=$inH elsewhere=$else)" }
}
$tb = $text.hapi.IndexOf('void TryBind(FRwgHeightApi& A)')
if ($tb -lt 0) { $fails += 'R11: TryBind not found' } else {
    $tbb = $text.hapi.Substring($tb)
    $v = $tbb.IndexOf('if (!bVersion) { A.Status = ')
    $e = $tbb.IndexOf('if (A.Required != 6) { A.Status = ')
    $u = $tbb.IndexOf('A.bUsable = ' + 'true;')
    if ($v -lt 0 -or $e -lt 0 -or $u -lt 0 -or -not ($v -lt $e -and $e -lt $u)) { $fails += 'R11: TryBind must refuse on version, then on exports, before allowing calls' }
}
$lh = Get-Body $text.hapi ('void ' + $cls + 'RwgLogHeightApi(bool bFull)')
if (-not $lh) { $fails += 'R11: RwgLogHeightApi body not found' } else {
    $g = $lh.IndexOf('if (!A.bUsable || !Pawn)')
    $fc = [regex]::Match($lh, 'A\.(Get|GetField|GetSpliced|Surface|Outside|Water|GMin|GMax)\(')
    if ($g -lt 0 -or -not $fc.Success -or $fc.Index -lt $g) { $fails += 'R11: an RWG call precedes the callsAllowed guard in RwgLogHeightApi' }
}
if ($text.hapi -match ('#pragma comment\(' + 'lib')) { $fails += 'R11: the HeightApi file links a library; the binding must stay runtime-only' }
# R11b: GetField's hidden return slot is the SECOND argument (x64 MSVC instance method: this RCX, slot RDX; measured by disassembling build 705, 2026-10-17).
$gf = 'using FnGetField = FFieldPtr* (*)(const void* This, FFieldPtr* Return' + 'Slot);'
if (([regex]::Matches($text.hapi, [regex]::Escape($gf))).Count -ne 1) { $fails += 'R11b: FnGetField must take (This, ReturnSlot) in that order, once' }
if ($text.hapi -notmatch ('A\.GetField\(Gen, &Field\);\s*const void\* F = Field\.Get\(\);\s*if \(!' + 'F\)')) { $fails += 'R11b: GetField must be called as (Gen, &Field) and null-checked before use' }
# R11c: RWG calls happen only from the latch or the post-latch window.
$hcalls = ([regex]::Matches(($text.mapcpp + $text.r1cpp + $text.subsys + $text.late), 'RwgLogHeight' + 'Api\(')).Count
$rp = Get-Body $text.r1cpp ('void ' + $cls + 'RwgLogReprobes(bool bFull)')
if (-not $rp -or $rp.IndexOf('RwgLogHeight' + 'Api(bFull);') -lt 0 -or $hcalls -ne 1) { $fails += 'R11c: RwgLogHeightApi must be called once, from RwgLogReprobes only' }
if ($ot) {
    $sr = $ot.IndexOf('const int32 Since' + 'Ready = RwgObserveTicks - RwgReadyTick;')
    $rc = [regex]::Matches($ot, 'RwgLog' + 'Reprobes\(')
    if ($sr -lt 0 -or $rc.Count -ne 1 -or $rc[0].Index -lt $sr) { $fails += 'R11c: RwgObserveTick may call RwgLogReprobes only after the latch' }
}

# R12
$pm = Get-Body $text.place ('bool ' + $cls + 'RwgPipelineMayRun()')
if (-not $pm) { $fails += 'R12: RwgPipelineMayRun body not found' } else {
    $g1 = $pm.IndexOf('if (GetMapMode() != ENodeShuffleMapMode::Rwg || !NodeShuffleMapMode::bRwgRollEnabled || !bRwg' + 'Ready)')
    $firstIf = $pm.IndexOf('if (')
    $lv = $pm.IndexOf('if (RwgLayoutVersion >= NodeShuffleMapMode::RwgLayout' + 'VersionP2b)')
    $lg = $pm.IndexOf('RWGLAYOUT: legacy layout ' + 'ignored')
    $rr = $pm.IndexOf('RollLayout(Seed, ' + 'true);')
    if ($g1 -lt 0 -or $g1 -ne $firstIf) { $fails += 'R12: the map/roll/ready guard must be the first test in RwgPipelineMayRun' }
    elseif ($lv -lt $g1 -or $lg -lt $lv -or $rr -lt $lg) { $fails += 'R12: the legacy check must follow the guard, and the re-roll must follow the RWGLAYOUT legacy line' }
}
# R13
$stamp = 'if (GetMapMode() == ENodeShuffleMapMode::Rwg) { RwgLayout' + 'Version = NodeShuffleMapMode::RwgLayoutVersionP2b; }'
$assigns = 0
foreach ($k in @('mapcpp','r1cpp','subsys','hapi','place','late')) { $assigns += ([regex]::Matches($text[$k], 'RwgLayout' + 'Version\s*=[^=]')).Count }
$rl = Get-Body $text.subsys ('void ' + $cls + 'RollLayout(int32 Seed, bool bIsReroll)')
if (-not $rl -or ([regex]::Matches($rl, [regex]::Escape($stamp))).Count -ne 1 -or $assigns -ne 1) { $fails += "R13: RwgLayoutVersion must be assigned once, inside the RWG-only test in RollLayout (assignments=$assigns)" }
# R14
$d1 = Get-Body $text.place ('bool ' + $cls + 'RwgDealOneSpot(const NodeShuffleMapMode::FRwgGroundQuery& Q, FRandomStream& Rng, FVector& OutSpot,')
if (-not $d1) { $fails += 'R14: RwgDealOneSpot body not found' } else {
    $o = $d1.IndexOf('if (Q.Outside(X, Y)) { ++Outside; return false; }'); $z = $d1.IndexOf('const double Z = Q.Surface' + 'Z(X, Y);'); $w = $d1.IndexOf('if (bVolume) { ++Water; return ' + 'false; }')
    if ($o -lt 0 -or $z -lt 0 -or $w -lt 0 -or -not ($o -lt $z -and $z -lt $w)) { $fails += 'R14: RwgDealOneSpot must reject outside, then resolve Z, then reject volume water' }
}
$gl = Get-Body $text.place ('bool ' + $cls + 'GenerateRwgLocations(FRandomStream& Rng, const TArray<FVector>& AvoidLocations, int32 Count,')
if (-not $gl -or $gl.IndexOf('RwgDealOne' + 'Spot(') -lt 0 -or $gl.IndexOf('RwgDealOne' + 'Spot(') -gt $gl.IndexOf('DistSquared' + '2D(')) { $fails += 'R14: the RWG deal must call RwgDealOneSpot before its spacing scan' }
$gn = Get-Body $text.subsys ('void ' + $cls + 'GenerateNewLocations(FRandomStream& Rng, const TArray<FVector>& VanillaLocations,')
if (-not $gn -or $gn -notmatch ('if \(' + [regex]::Escape($notVanilla) + '\)\s*\{\s*OutUnderground' + 'Indices = nullptr;') -or $gn.IndexOf('OutUnderground' + 'Indices = nullptr;') -gt $gn.IndexOf('CaveChance')) { $fails += 'R14: GenerateNewLocations must null the cave output off vanilla before any cave share' }
# R15
if ($text.subsys -notmatch ('bool bSettled = \(GetMapMode\(\) == ENodeShuffleMapMode::Rwg\) \? RwgResolveAtSpawn\(Entry, bRwgApiUsed, bWaterNoLand\) : false;\s*if \(!bRwgApiUsed\) \{ bSettled = Raycast' + 'Settle\(Entry, nullptr, nullptr, &bWaterNoLand\); \}')) { $fails += 'R15: the settle block must ask RwgResolveAtSpawn on RWG and RaycastSettle only when the API was not used' }
$ra = Get-Body $text.place ('bool ' + $cls + 'RwgResolveAtSpawn(FNodeShuffleEntry& Entry, bool& bOutApiUsed, bool& bOutWaterNoLand)')
if (-not $ra) { $fails += 'R15: RwgResolveAtSpawn body not found' } else {
    $rs = $ra.IndexOf('const bool bSettled = Raycast' + 'Settle(Entry, nullptr, nullptr, &bOutWaterNoLand, /*bTerrainOnly=*/' + 'true);')
    $nf = $ra.IndexOf('if (!bSettled)'); $fw = $ra.IndexOf('Entry.bRwgSettledOn' + 'Terrain = true;')
    $rt = [regex]::Matches($ra, 'return ' + 'true;')
    if ($rs -lt 0) { $fails += 'R15: resolver v3 must settle through the terrain-only RaycastSettle' }
    elseif ($nf -lt $rs -or $fw -lt $nf) { $fails += 'R15: the failed-settle return must follow the settle and precede the terrain flag write' }
    elseif ($ra.Substring($nf, 160) -notmatch ('\{\s*if \(!bOutWaterNoLand\) \{ \+\+RwgPcNoTerrainDefer; \}\s*return ' + 'false;\s*\}')) { $fails += 'R15: a failed terrain settle must return false (defer / redeal), never spawn at the API Z' }
    elseif ($rt.Count -ne 1 -or $rt[0].Index -lt $fw) { $fails += 'R15: the only return-true in the resolver must follow the settled-on-terrain flag write' }
    if ($ra.Contains('FMath::Abs(HitLoc.Z - Api' + 'Z0)')) { $fails += 'R15: the P2b agreement-then-API-Z rule is back' }
}
$sp = Get-Body $text.place ('void ' + $cls + 'RwgSpawnPass(float SpawnRadiusCm, bool& bChangedWorld)')
if (-not $sp -or $sp.IndexOf('Cands.' + 'Sort(') -lt 0 -or $sp -notmatch ('FMath::Min\(Cands\.Num\(\), NodeShuffleMapMode::RwgMaxSpawn' + 'AttemptsPerPass\)')) { $fails += 'R15: RwgSpawnPass must sort nearest-first and cap attempts at RwgMaxSpawnAttemptsPerPass' }
if ($text.subsys -notmatch ('if \(' + [regex]::Escape($notVanilla) + '\) \{ continue; \}\s*if \(Entry\.bActive && IsLocationNearAnyPlayer\(Entry\.Location, SpawnRadiusCm\)\)')) { $fails += 'R15: the ApplyLayout loop must hand RWG actor creation to RwgSpawnPass before its inline spawn' }
# R16
$gates = @(
    ('if (GetMapMode() == ENodeShuffleMapMode::Vanilla) { ClassifyOriginal' + 'Underground(Node, Rec.VanillaNodePath); }'),
    ('if (GetMapMode() == ENodeShuffleMapMode::Vanilla) { ExpandCave' + 'FloorsBudgeted(); }'),
    ('OutLoc.Z < DeepWaterFloorZ && GetMapMode() == ENodeShuffleMapMode::' + 'Vanilla;'),
    ('Entry.ResourceForm != 2 /* liquid */ && GetMapMode() == ENodeShuffleMapMode::' + 'Vanilla)'),
    ('if (GetMapMode() == ENodeShuffleMapMode::Vanilla) { Orphan' + 'RockCleanup(); }'),
    ('if (GetMapMode() == ENodeShuffleMapMode::Vanilla) { Rebuild' + 'MeshActorCache(); }')
)
foreach ($gt in $gates) { if (([regex]::Matches($text.subsys, [regex]::Escape($gt))).Count -ne 1) { $fails += "R16: vanilla-only gate missing: $gt" } }
if (([regex]::Matches($text.subsys, 'ClassifyOriginal' + 'Underground\(Node')).Count -ne 1) { $fails += 'R16: an ungated ClassifyOriginalUnderground call exists' }
if (([regex]::Matches($text.subsys, [regex]::Escape('&& RwgWaterVolumes.Num() > 0) { return RwgIsPointInWater' + 'Cached(Point); }'))).Count -ne 1) { $fails += 'R16: IsPointInWater must route off vanilla through the cached volume test (vanilla keeps the loop)' }
# R17
if ($tb -ge 0) {
    $tbb2 = $text.hapi.Substring($tb)
    $e2 = $tbb2.IndexOf('if (A.Required != 6) { A.Status = '); $sg = $tbb2.IndexOf('if (bRequiredSigBad) { A.Status = ' + 'TEXT("signatureMismatch"); return; }'); $u2 = $tbb2.IndexOf('A.bUsable = ' + 'true;')
    if ($sg -lt 0 -or -not ($e2 -lt $sg -and $sg -lt $u2)) { $fails += 'R17: the byte-signature refusal must sit between the export refusal and A.bUsable = true' }
}
$gq = $text.hapi.IndexOf('NodeShuffleMapMode::FRwgGroundQuery::FRwgGroundQuery(UWorld* World, UObject* Generator)')
if ($gq -lt 0) { $fails += 'R17: FRwgGroundQuery constructor not found' } else {
    $gqb = $text.hapi.Substring($gq)
    $gg = $gqb.IndexOf('if (!A.bUsable || !World)'); $gc = $gqb.IndexOf('A.Get(' + 'World)')
    if ($gg -lt 0 -or $gc -lt 0 -or $gc -lt $gg) { $fails += 'R17: the ground query calls RWG before checking the binding is usable' }
}
foreach ($tag in @('ROLLCENSUS','PLACECENSUS','RWGLAYOUT','RWGHIDECHECK')) { # RWG P4: P2b re-review S3, verbatim list
    if ($text.place -notmatch ('TEXT\("' + $tag + '[: ]')) { $fails += "R6: '$tag' line missing from NodeShuffleMapModePlace.cpp" }
}

if ($fails.Count -gt 0) {
    $fails | ForEach-Object { Write-Host "FAIL $_" }
    exit 1
}
Write-Host "PASS: RWG P1/P2a/P2b lint -- R1..R17 hold across $($files.Count) loaded files"
exit 0
