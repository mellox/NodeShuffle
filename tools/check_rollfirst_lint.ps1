# check_rollfirst_lint.ps1 -- ns-rollfirst-1 (2026-11-14): "Every resource first." After a roll the K nearest active entries of every
# resource (by position) load before anything else; then the existing nearest-first order. LOADS every file the packet touched:
# NodeShuffleRollFirst.cpp, NodeShuffleMapModeProvisionalScan.cpp, NodeShuffleMapModeSea.cpp, NodeShuffleMapModeProvisional.cpp/.h,
# NodeShuffleScanDiag.cpp, NodeShuffleSubsystem.cpp/.h, NodeShuffleMapMode.h, NodeShuffleBuildMarkers.h, NodeShuffle.cpp,
# NodeShuffleMcpDescribe.cpp, docs/DIAGNOSTICS.md. Every pin is anchored on its function body or on text unique to its site.
#   R1  console variable: name, default 3, one clamped reader (0..16), the clamp constant.
#   R2  K = 0 restores the old order: the reorder clears its lists BEFORE the K test and returns before touching Sorted; the loaded-entry
#       collection runs only while K > 0; the sea pass maps Step to I unchanged when the priority list is empty.
#   R3  tier and rank: a bucket per rank emitted rank-first (each bucket in Sorted's nearest-first order), then the rest in order; the
#       per-resource top-K insertion keeps ascending distance and truncates to K; no sort call anywhere in the new file.
#   R4  pick population: an actorless entry the provisional path cannot load is passed over before it is offered; loaded entries are
#       offered with no Sorted position (never priority); the can-load test names every set-aside reason.
#   R5  sea pass: the priority entries are walked first in the SAME loop with the SAME body and the SAME Left; stale (guid, index) pairs are
#       dropped; the per-pass bound stays 64.
#   R6  no entry reaches a spawn attempt without PassOk (which holds the sea-done test); the new file never writes the candidate list.
#   R7  the four new tags, each once, with their field lists (hand-counted arity: specifiers = fields).
#   R8  SCANCOVER: in the GetClosetClusters hook after SCANFILTER-PICK and before the diagnostics gate; never called from the spawn pass.
#   R9  build marker rollfirst-1 is the newest and on its own line; scanfilter-4 stays listed; no marker literal in the new file.
#   R10 the console variable has a manifest row (GNsMcpCvars).
#   R11 no legend (comment) in the new file and no DIAGNOSTICS.md text carries a field=value token of a new line's own fields.
#   R12 the log-only sea-done helper is the candidate scan's expression, term for term; the roll time is noted right after the roll mark.
#   R13 touched files stay under 500 lines; the reorder and the per-pass report walk no Layout.
#   R15 (round 2 review N2) supersession is read before the latch is re-armed; an empty build closes the latch; PASS is
#       diagnostics-gated and the DONE latch closes outside that gate.
#   R14 (round 2) the DONE latch starts closed; the per-pass report keeps its K/latch guard and its Left gate. R3 also pins the top-K
#       guard that keeps T.Last() off an empty array; R5 pins SeaFirstN to the validated list; R4 counts SetAsideOk's tests.
# Mutants run in-file. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    rf    = 'NodeShuffle\Private\NodeShuffleRollFirst.cpp'
    pscan = 'NodeShuffle\Private\NodeShuffleMapModeProvisionalScan.cpp'
    sea   = 'NodeShuffle\Private\NodeShuffleMapModeSea.cpp'
    prov  = 'NodeShuffle\Private\NodeShuffleMapModeProvisional.cpp'
    provh = 'NodeShuffle\Private\NodeShuffleMapModeProvisional.h'
    sdiag = 'NodeShuffle\Private\NodeShuffleScanDiag.cpp'
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    subh  = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    maph  = 'NodeShuffle\Public\NodeShuffleMapMode.h'
    mark  = 'NodeShuffle\Private\NodeShuffleBuildMarkers.h'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
    desc  = 'NodeShuffle\Private\NodeShuffleMcpDescribe.cpp'
    sfr   = 'NodeShuffle\Private\NodeShuffleScanFilterRows.cpp'
}
$text = [ordered]@{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = ((Get-Content -Raw -Encoding utf8 -Path $p) -replace "`r`n", "`n")
}
$docPath = Join-Path $root 'docs\DIAGNOSTICS.md'
if (-not (Test-Path $docPath)) { Write-Host "FAIL(load): pinned file 'docs/DIAGNOSTICS.md' does not exist"; exit 2 }
$text['doc'] = ((Get-Content -Raw -Encoding utf8 -Path $docPath) -replace "`r`n", "`n")

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

# The four new formats and their field lists (R7, R11). The tag text is built by concatenation so this file is not a copy of it.
$fmts = [ordered]@{
    build = @{ Tag = ('ROLLFIRST' + '-BUILD: '); Fields = @('generation','build','perResource','resources','picks','wanted','resourcesShortOfK','passedOver','listEntries','buildMs','secondsSinceRoll','supersededOpenGeneration') }
    pass  = @{ Tag = ('ROLLFIRST' + '-PASS: ');  Fields = @('pass','generation','wanted','withActor','prioSeaPending','seaCheckedFirst','prioCandidates','prioAttempted','prioSpawnedThisPass','prioFailedThisPass','setAside','resources','resourcesAtK','secondsSinceRoll') }
    done  = @{ Tag = ('ROLLFIRST' + '-DONE: ');  Fields = @('pass','generation','secondsSinceRoll','wanted','withActor','resources','resourcesAtK','resourcesShortOfK','setAside','perResource') }
    cover = @{ Tag = ('SCAN' + 'COVER: ');       Fields = @('resource','dealtActive','withActor','withoutActor','nearestWithoutActorM','nearestWithoutActorSeaDone','nearestWithoutActorQueuePos','clustersForResource','secondsSinceRoll') }
}

function Invoke-RollFirstChecks($T) {
    $f = @()
    $rf = $T.rf
    $reord = Span $rf 'void ANodeShuffleSubsystem::RwgRollFirstReorder(' 'void ANodeShuffleSubsystem::RwgRollFirstEmitPass('
    $emit  = Span $rf 'void ANodeShuffleSubsystem::RwgRollFirstEmitPass(' 'void ANodeShuffleSubsystem::EmitScanCover('
    $canl  = Span $rf 'bool ANodeShuffleSubsystem::RwgRollFirstCanLoad(' 'bool ANodeShuffleSubsystem::RwgRollFirstSeaDone('
    $seaD  = Span $rf 'bool ANodeShuffleSubsystem::RwgRollFirstSeaDone(' 'void ANodeShuffleSubsystem::RwgRollFirstNoteRoll()'
    $build = Span $T.pscan '    if (Why)' '    // Phase 1:'
    $passOk = Span $T.pscan '    const auto PassOk = [&](int32 I)' '    // Build decision.'
    $seaFn = Span $T.sea 'void ANodeShuffleSubsystem::RwgSeaRedealPass()' 'for (int32 I = 0; I < WellLayout.Num() && Left > '
    $spawnFn = Span $T.prov 'void ANodeShuffleSubsystem::RwgProvisionalSpawnPass(' 'void ANodeShuffleSubsystem::RwgProvisionalEmitCensus('
    $hook = Span $T.sdiag 'SUBSCRIBE_UOBJECT_METHOD(AFGResourceScanner, GetClosetClusters,' 'void ANodeShuffleSubsystem::EmitScanPing('
    if ($reord -eq '' -or $emit -eq '' -or $canl -eq '' -or $seaD -eq '' -or $build -eq '' -or $passOk -eq '' -or $seaFn -eq '' -or $spawnFn -eq '' -or $hook -eq '') { $f += '(R0) a pinned span is missing or duplicated'; return $f }
    # R1
    if ((Count $rf ("TEXT(`"NodeShuffle.RwgFirstPerResource`"),`n        3,")) -ne 1) { $f += '(R1) NodeShuffle.RwgFirstPerResource must default to 3' }
    if ((Count $rf 'return FMath::Clamp(CVarNodeShuffleRwgFirstPerResource.GetValueOnGameThread(), 0, RwgFirstPerResourceMax);') -ne 1 -or (Count $rf 'CVarNodeShuffleRwgFirstPerResource.GetValueOnGameThread()') -ne 1) { $f += '(R1) one clamped reader 0..max' }
    if ((Count $T.provh 'constexpr int32 RwgFirstPerResourceMax = 16;') -ne 1 -or (Count $T.provh '    int32 RwgFirstPerResource();') -ne 1) { $f += '(R1) the clamp constant 16 and the reader declaration' }
    if ((Count $rf 'Values are clamped to 0..16.') -ne 1 -or (Count $rf 'Default 3. 0: off') -ne 1) { $f += '(R1) the help text must state the default, off value and clamp' }
    # R2
    if (-not (Ordered $reord @('    RwgRollFirstList.Reset();', '    RwgRollFirstPicks.Reset();', '    if (K <= 0) { return; }', '    Sorted = MoveTemp(Out);'))) { $f += '(R2) the lists are cleared before the K test, and K = 0 returns before Sorted is replaced' }
    $preGuard = $reord.Substring(0, [Math]::Max(0, $reord.IndexOf('    if (K <= 0) { return; }')))
    if ($preGuard.Contains('Sorted.') -or $preGuard.Contains('Sorted =') -or (Count $reord 'Sorted = ') -ne 1) { $f += '(R2) nothing touches Sorted before the K test; Sorted is assigned once' }
    if ((Count $build '        const bool bRfOn = NodeShuffleMapMode::RwgFirstPerResource() > 0;') -ne 1 -or (Count $build '} else if (bRfOn && Layout[I].bIsNewNode && Layout[I].bActive && IsValid(SpawnedNodes.FindRef(Layout[I].EntryGuid))) { RfLoaded.Emplace(') -ne 1) { $f += '(R2) the loaded collection runs only while K > 0, in the build walk' }
    if ((Count $seaFn '        const int32 I = (Step < SeaFirstN) ? SeaFirst[Step] : Step - SeaFirstN;') -ne 1) { $f += '(R2) the sea pass maps Step to the layout index unchanged once the priority steps are spent' }
    # R3
    if (-not (Ordered $reord @('    for (int32 P = 0; P < Sorted.Num(); ++P) { if (RankOfPos[P] >= 0) { Buckets[RankOfPos[P]].Add(P); } }', '    for (int32 J = 0; J < K; ++J) { for (const int32 P : Buckets[J]) { Out.Add(Sorted[P]); RwgRollFirstList.Emplace(', '    for (int32 P = 0; P < Sorted.Num(); ++P) { if (RankOfPos[P] < 0) { Out.Add(Sorted[P]); } }', '    Sorted = MoveTemp(Out);'))) { $f += '(R3) priority tier first, rank before distance (buckets in nearest-first order), then the rest in order' }
    if ((Count $reord '            if (S.SortedPos >= 0) { RankOfPos[S.SortedPos] = J; }') -ne 1 -or (Count $reord '        while (At > 0 && T[At - 1].D > D) { --At; }') -ne 1 -or (Count $reord '        if (T.Num() > K) { T.SetNum(K); }') -ne 1 -or (Count $reord '        if (T.Num() >= K && D >= T.Last().D) { return; }') -ne 1) { $f += '(R3) the per-resource top K keeps ascending distance, truncates to K, and ranks only actorless picks' }
    if ((Count $rf ('.' + 'Sort(')) -ne 0 -or (Count $rf ('Algo::' + 'Sort')) -ne 0) { $f += '(R3) no sort call in the new file' }
    # R4
    if (-not (Ordered $reord @('        if (!RwgRollFirstCanLoad(Layout[Sorted[P].Value])) { ++PassedOver; continue; }', '        Offer(Sorted[P].Key, Sorted[P].Value, P);', '    for (const TPair<double, int32>& L : Loaded) { Offer(L.Key, L.Value, -1); }'))) { $f += '(R4) set-aside entries are passed over before the offer; loaded entries hold a slot with no Sorted position' }
    foreach ($n in @('if (!E.bIsNewNode || !E.bActive || E.bPinned) { return false; }', 'DormantThisSession.Contains(E.EntryGuid)) { return false; }', 'SpawnParkedThisSession.Contains(E.EntryGuid)) { return false; }', 'if (RwgProvRefused.Contains(E.EntryGuid)) { return false; }', 'if (E.bRayCasted) { return false; }', 'if (RwgSeaDealFailed.Contains(E.EntryGuid)) { return false; }', 'return !RwgProvisionalAtWetSpot(E);')) {
        if ((Count $canl $n) -ne 1) { $f += "(R4) the can-load test must hold: $n" }
    }
    # R5
    if (-not (Ordered $seaFn @('    int32 Left = NodeShuffleMapMode::RwgSeaRedealPerPass;', '    TArray<int32> SeaFirst;', '    for (const TPair<FGuid, int32>& G : RwgRollFirstList) { if (Layout.IsValidIndex(G.Value) && Layout[G.Value].EntryGuid == G.Key) { SeaFirst.Add(G.Value); } }', '    const int32 SeaFirstN = SeaFirst.Num();', '    for (int32 Step = 0; Step < SeaFirstN + Layout.Num() && Left > 0; ++Step)', '        const int32 I = (Step < SeaFirstN) ? SeaFirst[Step] : Step - SeaFirstN;', '        FNodeShuffleEntry& E = Layout[I];', '        if (!E.bIsNewNode || !E.bActive || (E.bRwgSeaChecked && (E.bRwgVoidChecked || !bVoidOn)) || RwgSeaDealFailed.Contains(E.EntryGuid)) { continue; }', '        --Left; ++Checked;'))) { $f += '(R5) priority first, same loop, same skip, same Left, stale pairs dropped' }
    if ((Count $seaFn 'for (int32 Step = ') -ne 1 -or (Count $seaFn ('for (int32 I = 0; I < Layout.Num() && Left > ' + '0; ++I)')) -ne 0 -or (Count $seaFn '        --Left; ++Checked;') -ne 1) { $f += '(R5) exactly one node loop in the sea pass' }
    if ((Count $T.maph ('constexpr int32 RwgSeaRedealPerPass = ' + '64;')) -ne 1) { $f += '(R5) the sea bound stays 64' }
    # R6
    if ((Count $T.pscan 'OutCands.Add(') -ne 2 -or (Count $T.pscan 'if (PassOk(F.Value)) { OutCands.Add(F.Value); }') -ne 1 -or (Count $T.pscan '        if (PassOk(I)) { OutCands.Add(I); }') -ne 1) { $f += '(R6) every candidate passes PassOk' }
    if (-not (Ordered $passOk @('        const bool bSeaDone = bSeaPassIdle || (E.bRwgSeaChecked && (E.bRwgVoidChecked || !bVoidMaybe));', '        if (!bSeaDone) { ++SeaPending; return false; }', '        return true;'))) { $f += '(R6) PassOk refuses an entry that is not sea-done' }
    if ((Count $spawnFn 'Cands.Add(') -ne 0 -or (Count $spawnFn 'Cands.Insert(') -ne 0 -or (Count $spawnFn 'Cands.Emplace(') -ne 0) { $f += '(R6) the spawn pass adds no candidate of its own' }
    foreach ($w in @('RwgProvCandList.Emplace(', 'RwgProvCandList.Add(', 'RwgProvCandList.Insert(', 'RwgProvCandList.Reset(', 'RwgProvCandList[Q] =', 'OutCands')) { if ((Count $rf $w) -ne 0) { $f += "(R6) the new file must not write the candidate list ($w)" } }
    # R7
    foreach ($k in $fmts.Keys) {
        $tag = $fmts[$k].Tag
        $m = [regex]::Matches($rf, 'TEXT\("' + [regex]::Escape($tag) + '([^"]*)"\)')
        if ($m.Count -ne 1) { $f += "(R7) $k tag must appear once as a TEXT literal"; continue }
        $names = @([regex]::Matches($m[0].Groups[1].Value, '(?<![A-Za-z0-9])([A-Za-z][A-Za-z0-9]*)=') | ForEach-Object { $_.Groups[1].Value })
        $specs = ([regex]::Matches($m[0].Groups[1].Value, '%[-+ #0]*\d*(?:\.\d+)?[dsfiu]')).Count
        if (($names -join ',') -ne ($fmts[$k].Fields -join ',') -or $specs -ne $fmts[$k].Fields.Count) { $f += "(R7) $k fields or specifier count changed" }
        if ((Count $T.doc ($tag.TrimEnd(' ', ':'))) -lt 1) { $f += "(R7) $k tag must have a DIAGNOSTICS.md entry" }
    }
    # R8
    if (-not (Ordered $hook @('NodeShuffleScanFilter::EmitPickLine(Self, Closest.Num());', '        if (IsValid(Self) && Self->GetWorld()) { for (TActorIterator<ANodeShuffleSubsystem> It(Self->GetWorld()); It; ++It) { It->EmitScanCover(Self); break; } }', 'if (!FNodeShuffleModule::AreDiagnosticsEnabled() || !IsValid(Self) || !Self->GetWorld()) { return; }'))) { $f += '(R8) SCANCOVER is ungated, after the pick line, before the diagnostics gate' }
    foreach ($k in @('prov', 'pscan', 'sea')) { if ((Count $T[$k] 'EmitScanCover(') -ne 0) { $f += "(R8) SCANCOVER must not run from $k" } }
    # R9
    $hdrTags = @([regex]::Matches($T.mark, 'TEXT\("(20\d\d-\d\d-\d\d-[\w-]+)"\),') | ForEach-Object { $_.Groups[1].Value })
    $own = '2026-11-14-' + 'rollfirst-1'; $prev = '2026-11-14-' + 'scanfilter-4'
    # rwg-0-2-1-1 (2026-11-15): rollfirst-1 is no longer the newest; it stays listed (the newest is pinned by check_rwg020_lint G9 and check_mcpcontract C4).
    if ($hdrTags.Count -eq 0 -or $hdrTags -notcontains $own -or $hdrTags -notcontains $prev) { $f += '(R9) rollfirst-1 and scanfilter-4 stay listed' }
    if ((Count $T.nsmod ('packet marker (' + $own + ')=====')) -ne 1 -or (Count $T.nsmod ('packet marker (' + $prev + ')=====')) -ne 1) { $f += '(R9) both markers once on their own lines in NodeShuffle.cpp' }
    if ((Count $rf $own) -ne 0) { $f += '(R9) no marker literal inside the new file' }
    # R10
    $cv = [regex]::Match($T.desc, 'GNsMcpCvars\[\] = \{([\s\S]*?)\};')
    if (-not $cv.Success -or (Count $cv.Groups[1].Value 'TEXT("NodeShuffle.RwgFirstPerResource")') -ne 1) { $f += '(R10) the console variable needs its manifest row' }
    # R11
    $noLit = [regex]::Replace($rf, 'TEXT\("(?:[^"\\]|\\.)*"\)', 'TEXT()')
    $docRows = (($T.doc -split "`n") | Where-Object { $_.Contains('ROLLFIRST') -or $_.Contains('SCAN' + 'COVER') }) -join "`n"
    foreach ($k in $fmts.Keys) { foreach ($fld in $fmts[$k].Fields) {
        if ([regex]::IsMatch($noLit, '(?<![A-Za-z0-9])' + $fld + '=')) { $f += "(R11) a comment in the new file shows a $fld token in field form" }
        if ([regex]::IsMatch($docRows, '(?<![A-Za-z0-9])' + $fld + '=')) { $f += "(R11) DIAGNOSTICS.md shows a $fld token in field form" }
    } }
    # R12
    foreach ($n in @('const bool bVoidMaybe = !(bRwgVoidCheckDone && bRwgKillboxDone) || RwgVoidPassTermsOn();', 'const bool bSeaPassIdle = bRwgSeaCheckDone && !bRwgSeaUsable;', 'const bool bSeaDone = bSeaPassIdle || (E.bRwgSeaChecked && (E.bRwgVoidChecked || !bVoidMaybe));')) {
        if ((Count $seaD $n) -ne 1 -or (Count $T.pscan $n) -ne 1) { $f += "(R12) the log-only sea-done helper must match the candidate scan: $n" }
    }
    if ((Count $T.sub "    RwgProvCandMarkDirty(bIsReroll ? TEXT(`"reroll`") : TEXT(`"roll`")); // rwg-farspawn-1f C3: the provisional candidate list is rebuilt next pass`n    RwgRollFirstNoteRoll();") -ne 1 -or (Count $T.sub 'RwgRollFirstNoteRoll()') -ne 1) { $f += '(R12) the roll time is noted once, right after the roll mark in RollLayout' }
    # R13
    foreach ($k in @('rf', 'pscan', 'sea', 'prov', 'sdiag', 'sfr')) { $lines = ($T[$k] -split "`n").Count; if ($lines -gt 501) { $f += "(R13) $k is over 500 lines ($lines)" } }
    foreach ($sp in @($reord, $emit)) { if ($sp.Contains('for (int32 I = 0; I < Layout.Num()') -or $sp.Contains('for (const FNodeShuffleEntry& E : Layout)')) { $f += '(R13) the reorder and the per-pass report walk no Layout' } }
    # R14 (round 2, review F5)
    if ((Count $emit '    if (RwgRollFirstK <= 0 || bRwgRollFirstDoneLogged) { return; }') -ne 1 -or (Count $emit '    if (Left > 0)') -ne 1 -or (Count $T.subh '    bool bRwgRollFirstDoneLogged = true;') -ne 1) { $f += '(R14) DONE latch starts closed; K guard and Left gate in the per-pass report' }
    # R15 (round 2 review, N2)
    if (-not (Ordered $reord @('    const bool bSuperseded = !bRwgRollFirstDoneLogged;', '    if (Wanted > 0) { ++RwgRollFirstGeneration; bRwgRollFirstDoneLogged = false; }', '    else { bRwgRollFirstDoneLogged = true; }', '    if (Wanted > 0 || bSuperseded || FNodeShuffleModule::AreDiagnosticsEnabled())'))) { $f += '(R15) supersession is read before the latch is re-armed; an empty build closes the latch' }
    if (-not (Ordered $emit @('    if (Left > 0)', ('        if (Ns' + 'Diag(ENsDiag::Status))'), ('            UE_LOG(LogNodeShuffle, Display, TEXT("ROLLFIRST' + '-PASS: '), '        return;', '    bRwgRollFirstDoneLogged = true;'))) { $f += '(R15) PASS prints only with diagnostics on; the DONE latch closes outside that gate' }
    $sao = Span $T.pscan '    const auto SetAsideOk = [this](const FNodeShuffleEntry& E)' '    const auto NearestSq ='; if ($sao -eq '' -or (Count $sao ('return ' + 'false;')) -ne 4) { $f += '(R4) SetAsideOk changed: mirror it in RwgRollFirstCanLoad' }
    return $f
}

$fails = @(Invoke-RollFirstChecks $text)
$prioLoop = '    for (int32 J = 0; J < K; ++J) { for (const int32 P : Buckets[J]) { Out.Add(Sorted[P]); RwgRollFirstList.Emplace(Layout[Sorted[P].Value].EntryGuid, Sorted[P].Value); } }'
$mutants = @(
    @{ Name = 'Q1 priority tier removed from the order'; Key = 'rf'; Expect = 'R3'; Find = '    Sorted = MoveTemp(Out);'; Repl = '    Out.Reset();' }
    @{ Name = 'Q2 rank-before-distance swapped (distance only)'; Key = 'rf'; Expect = 'R3'; Find = $prioLoop; Repl = '    for (int32 P = 0; P < Sorted.Num(); ++P) { if (RankOfPos[P] >= 0) { Out.Add(Sorted[P]); RwgRollFirstList.Emplace(Layout[Sorted[P].Value].EntryGuid, Sorted[P].Value); } }' }
    @{ Name = 'Q3 rest emitted before the priority tier'; Key = 'rf'; Expect = 'R3'; Find = ($prioLoop + "`n" + '    for (int32 P = 0; P < Sorted.Num(); ++P) { if (RankOfPos[P] < 0) { Out.Add(Sorted[P]); } }'); Repl = ('    for (int32 P = 0; P < Sorted.Num(); ++P) { if (RankOfPos[P] < 0) { Out.Add(Sorted[P]); } }' + "`n" + $prioLoop) }
    @{ Name = 'Q4 sea priority removed'; Key = 'sea'; Expect = 'R5'; Find = '{ SeaFirst.Add(G.Value); }'; Repl = '{ }' }
    @{ Name = 'Q5 sea priority stops spending the bound'; Key = 'sea'; Expect = 'R5'; Find = 'Step < SeaFirstN + Layout.Num() && Left > 0; ++Step)'; Repl = 'Step < SeaFirstN + Layout.Num(); ++Step)' }
    @{ Name = 'Q6 sea bound raised'; Key = 'maph'; Expect = 'R5'; Find = ('constexpr int32 RwgSeaRedealPerPass = ' + '64;'); Repl = ('constexpr int32 RwgSeaRedealPerPass = ' + '128;') }
    @{ Name = 'Q7 sea priority as a second loop with its own bound'; Key = 'sea'; Expect = 'R5'; Find = '    TArray<int32> SeaFirst;'; Repl = ("    for (int32 I = 0; I < Layout.Num() && Left > " + "0; ++I) { }`n    TArray<int32> SeaFirst;") }
    @{ Name = 'Q8 sea guid check dropped'; Key = 'sea'; Expect = 'R5'; Find = 'if (Layout.IsValidIndex(G.Value) && Layout[G.Value].EntryGuid == G.Key) { SeaFirst.Add(G.Value); }'; Repl = 'SeaFirst.Add(G.Value);' }
    @{ Name = 'Q9 K=0 leaves a stale priority list'; Key = 'rf'; Expect = 'R2'; Find = "    RwgRollFirstList.Reset();`n    RwgRollFirstPicks.Reset();"; Repl = '    RwgRollFirstPicks.Reset();' }
    @{ Name = 'Q10 K=0 guard dropped'; Key = 'rf'; Expect = 'R2'; Find = '    if (K <= 0) { return; } // off'; Repl = '    // off' }
    @{ Name = 'Q11 loaded collection runs with K=0'; Key = 'pscan'; Expect = 'R2'; Find = '} else if (bRfOn && Layout[I].bIsNewNode'; Repl = '} else if (Layout[I].bIsNewNode' }
    @{ Name = 'Q12 un-sea-checked entry reaches the candidates (PassOk bypassed)'; Key = 'pscan'; Expect = 'R6'; Find = '        if (PassOk(I)) { OutCands.Add(I); }'; Repl = '        OutCands.Add(I);' }
    @{ Name = 'Q13 sea-done test dropped from PassOk'; Key = 'pscan'; Expect = 'R6'; Find = '        if (!bSeaDone) { ++SeaPending; return false; }'; Repl = '' }
    @{ Name = 'Q14 new file writes the candidate list'; Key = 'rf'; Expect = 'R6'; Find = '    Sorted = MoveTemp(Out);'; Repl = "    Sorted = MoveTemp(Out);`n    RwgProvCandList.Reset();" }
    @{ Name = 'Q15 refused entries picked'; Key = 'rf'; Expect = 'R4'; Find = '    if (RwgProvRefused.Contains(E.EntryGuid)) { return false; }'; Repl = '' }
    @{ Name = 'Q16 set-aside entries offered'; Key = 'rf'; Expect = 'R4'; Find = '{ ++PassedOver; continue; }'; Repl = '{ ++PassedOver; }' }
    @{ Name = 'Q17 SCANCOVER tag renamed'; Key = 'rf'; Expect = 'R7'; Find = ('TEXT("SCAN' + 'COVER: resource='); Repl = 'TEXT("SCANCOV: resource=' }
    @{ Name = 'Q18 PASS tag renamed'; Key = 'rf'; Expect = 'R7'; Find = ('TEXT("ROLLFIRST' + '-PASS: '); Repl = 'TEXT("ROLLFIRSTPASS: ' }
    @{ Name = 'Q19 DONE tag renamed'; Key = 'rf'; Expect = 'R7'; Find = ('TEXT("ROLLFIRST' + '-DONE: '); Repl = 'TEXT("ROLLFIRSTDONE: ' }
    @{ Name = 'Q20 BUILD tag renamed'; Key = 'rf'; Expect = 'R7'; Find = ('TEXT("ROLLFIRST' + '-BUILD: '); Repl = 'TEXT("ROLLFIRSTBUILD: ' }
    @{ Name = 'Q21 DONE drops setAside'; Key = 'rf'; Expect = 'R7'; Find = ' resourcesShortOfK=%d setAside=%d perResource=%d"),'; Repl = ' resourcesShortOfK=%d perResource=%d"),' }
    @{ Name = 'Q22 SCANCOVER behind the diagnostics gate'; Key = 'sdiag'; Expect = 'R8'; Find = '        if (IsValid(Self) && Self->GetWorld()) { for (TActorIterator<ANodeShuffleSubsystem> It(Self->GetWorld()); It; ++It) { It->EmitScanCover(Self); break; } }'; Repl = '' }
    @{ Name = 'Q23 marker dropped from the header'; Key = 'mark'; Expect = 'R9'; Find = ('        TEXT("2026-11-14-' + 'rollfirst-1"),'); Repl = '' }
    @{ Name = 'Q24 manifest row dropped'; Key = 'desc'; Expect = 'R10'; Find = 'TEXT("NodeShuffle.RwgFirstPerResource"),'; Repl = '' }
    @{ Name = 'Q25 legend shows a field token'; Key = 'rf'; Expect = 'R11'; Find = '// Legend (prose only): wanted counts'; Repl = ('// Legend (prose only): wanted' + '=3 counts') }
    @{ Name = 'Q26 sea-done helper drifts'; Key = 'rf'; Expect = 'R12'; Find = '    const bool bSeaPassIdle = bRwgSeaCheckDone && !bRwgSeaUsable;'; Repl = '    const bool bSeaPassIdle = bRwgSeaCheckDone;' }
    @{ Name = 'Q27 default changed'; Key = 'rf'; Expect = 'R1'; Find = "TEXT(`"NodeShuffle.RwgFirstPerResource`"),`n        3,"; Repl = "TEXT(`"NodeShuffle.RwgFirstPerResource`"),`n        8," }
    @{ Name = 'Q28 Layout walk in the per-pass report'; Key = 'rf'; Expect = 'R13'; Find = '    // State of the priority set now.'; Repl = '    for (const FNodeShuffleEntry& E : Layout) { }' }
    @{ Name = 'Q29 roll time not noted'; Key = 'sub'; Expect = 'R12'; Find = '    RwgRollFirstNoteRoll();'; Repl = '' }
    @{ Name = 'Q30 sort call in the new file'; Key = 'rf'; Expect = 'R3'; Find = '    Sorted = MoveTemp(Out);'; Repl = ("    Out" + ".Sort();`n    Sorted = MoveTemp(Out);") }
    @{ Name = 'Q31 SeaFirstN counts stale pairs'; Key = 'sea'; Expect = 'R5'; Find = '    const int32 SeaFirstN = SeaFirst.Num();'; Repl = '    const int32 SeaFirstN = RwgRollFirstList.Num();' }
    @{ Name = 'Q32 top-K guard dropped'; Key = 'rf'; Expect = 'R3'; Find = '        if (T.Num() >= K && D >= T.Last().D) { return; }'; Repl = '        if (D >= T.Last().D) { return; }' }
    @{ Name = 'Q33 DONE latch starts open'; Key = 'subh'; Expect = 'R14'; Find = '    bool bRwgRollFirstDoneLogged = true;'; Repl = '    bool bRwgRollFirstDoneLogged = false;' }
    @{ Name = 'Q34 empty build leaves the DONE latch open'; Key = 'rf'; Expect = 'R15'; Find = '    else { bRwgRollFirstDoneLogged = true; }'; Repl = '' }
    @{ Name = 'Q35 PASS ungated again'; Key = 'rf'; Expect = 'R15'; Find = ('        if (Ns' + 'Diag(ENsDiag::Status))'); Repl = '        if (true)' }
    @{ Name = 'Q36 supersession read after the re-arm'; Key = 'rf'; Expect = 'R15'; Find = ('    const bool bSuperseded = !bRwgRollFirstDoneLogged;' + ' // a generation was still open: no ROLLFIRST-DONE yet and no earlier build replaced it' + "`n" + '    if (Wanted > 0) { ++RwgRollFirstGeneration; bRwgRollFirstDoneLogged = false; } // a new generation: DONE re-armed'); Repl = ('    if (Wanted > 0) { ++RwgRollFirstGeneration; bRwgRollFirstDoneLogged = false; } // a new generation: DONE re-armed' + "`n" + '    const bool bSuperseded = !bRwgRollFirstDoneLogged;') }
)
$mutantFails = @()
$caught = 0
foreach ($m in $mutants) {
    $mut = [ordered]@{}
    foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = @(Invoke-RollFirstChecks $mut)
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ }
    else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "ROLLFIRST LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: ROLLFIRST lint -- R1..R15 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
