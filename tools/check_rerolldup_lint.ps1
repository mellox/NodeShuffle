# REROLLDUP lint (2026-11-01, packet rwg-rerolldup-1; handoff _team/test-logs/2026-11-01-nodeshuffle-rerolldup-handoff.md).
# A re-roll carried every occupied spawned entry under its miner AND re-dealt the same original from the pool rebuild, so each
# carried entry was counted twice (pool 973 -> 1031 with 58 carried). This lint pins the fix in RollLayout and the TWINS census in
# NodeShuffle.DumpLayout. LOADS: NodeShuffleSubsystem.cpp (RollLayout), NodeShuffleLayoutDump.cpp (TWINS), NodeShuffle.cpp (marker).
#   U1 (I1) a carried entry (by EntryGuid) never re-enters the pool: the only NewLayout.Add in the rebuild loop is guarded by the
#      carried flag; the guid set is filled from CarriedEntries; the augments' dedupe set carries the carried paths explicitly
#   U2 (I2) any OTHER entry whose original path equals a carried path is dropped (path compare, not guid), counted and logged
#   U3 (I3) carried hide records are written after BOTH OriginalNodeRecord resets, through the conversion's own helper, deduped by
#      path, before the RemoveAll and the carried append; the conversion uses the same helper; no count shortcut
#   U4 (I4) composition kept: the counts, purity multiset and VanillaCount++ after the guarded Add carry no carried-flag guard;
#      the zombie predicate skips carried entries
#   U5 (I5) fresh extras = config minus carried extras, fed to the extras GenerateNewLocations call; carried extras are the
#      carried entries with no original resource; carried locations stay in the avoid set both generation calls read
#   U6 REROLLPOOL-CARRY census: exact format + arguments, re-roll only, ungated, before the Rolled layout line; each token once
#   U7 marker rwg-rerolldup-1 once on its own line in NodeShuffle.cpp and nowhere in the packet sources
#   U8 LAYOUTDUMP-TWINS / -TWIN: exact formats, after the CHAT line, active+path predicate, shared means two or more; the
#      per-entry E line carries no path field
# Every forbidden needle is BUILT BY CONCATENATION. Mutants run in-file. Exit 0 = all hold; 1 = a rule or mutant fails; 2 = missing file.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    dump  = 'NodeShuffle\Private\NodeShuffleLayoutDump.cpp'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
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

# ---- anchors (unique context) ----
$fillGuid    = '        RerollCarriedGuids.Add(C.EntryGuid);'
$fillPath    = '        if (!C.VanillaNodePath.IsEmpty()) { RerollCarriedPaths.Add(C.VanillaNodePath); }'
$fillExtra   = '        if (C.OriginalResourceClassPath.IsEmpty()) { ++RerollCarriedExtras; } else { ++RerollCarriedWithOriginal; }'
$carriedFlag = '            const bool bCarriedOld = RerollCarriedGuids.Contains(Old.EntryGuid);'
$dupePred    = '            if (!bCarriedOld && !Old.VanillaNodePath.IsEmpty() && RerollCarriedPaths.Contains(Old.VanillaNodePath))'
$dupeCount   = '                ++RerollPathDupesDropped;'
$dupeFmt     = 'TEXT("REROLLPOOL-PATHDUPE dropped path=''%s'' res=''%s'' guid=%s active=%d pinned=%d newNode=%d"),'
$guardedAdd  = '            if (!bCarriedOld) { NewLayout.Add(E); } // rwg-rerolldup-1 I1'
$zombiePred  = '            if (!bCarriedOld && !Old.bPinned && !IsValid(Live) && !Old.OriginalResourceClassPath.StartsWith(TEXT("/Game/"))'
$twinCompose = '                if (bPathAlreadyCounted) { ++RerollCarriedSamePathComposition; continue; }'
$augAppend   = '        ExistingPaths.Append(RerollCarriedPaths);'
$quotaLine   = '    const int32 RerollNewQuotaEffective = FMath::Max(0, Config.NewNodeCount - RerollCarriedExtras);'
$quotaCall   = '        GenerateNewLocations(Rng, VanillaLocations, AvoidLocations, RerollNewQuotaEffective, NewLocations,'
$avoidLoop   = "    for (const FNodeShuffleEntry& E : CarriedEntries)`n    {`n        AvoidLocations.Add(E.Location);`n        ++RerollCarriedAvoidAdded;`n    }"
$relocAvoid  = '            TArray<FVector> RelocAvoid = AvoidLocations;'
$convReset   = "    OriginalNodeRecord.Reset();`n    {`n        // Count the unoccupied originals"
$convRec     = '                const FNodeShuffleSuppressedOriginal Rec = NsMakeSuppressedOriginalRecord(E);'
$hideAdd     = '            OriginalNodeRecord.Add(NsMakeSuppressedOriginalRecord(C));'
$hideCount   = '            ++RerollHideRecordsForCarried;'
$hideDedupe  = "            CarriedRecordPaths.Add(C.VanillaNodePath, &bPathRecorded);`n            if (bPathRecorded) { ++RerollHideRecordPathDupes; continue; }"
$removeAll   = '    NewLayout.RemoveAll([](const FNodeShuffleEntry& E){ return !E.bIsNewNode && !E.bPinned; });'
$appendCarry = '    NewLayout.Append(CarriedEntries);'
$helperSig   = 'static FNodeShuffleSuppressedOriginal NsMakeSuppressedOriginalRecord(const FNodeShuffleEntry& E)'
$censusFmt   = 'TEXT("REROLLPOOL-CARRY carried=%d carriedWithOriginal=%d carriedExtras=%d excludedFromPool=%d pathDupesDropped=%d carriedSamePathComposition=%d poolPathRepeats=%d hideRecordsForCarried=%d hideRecordPathDupes=%d carriedOrigLiveOccupied=%d carriedAvoidAdded=%d newQuotaEffective=%d newQuotaConfig=%d poolAfter=%d"),'
$censusArgs  = "            CarriedEntries.Num(), RerollCarriedWithOriginal, RerollCarriedExtras, RerollExcludedFromPool, RerollPathDupesDropped,`n            RerollCarriedSamePathComposition, RerollPoolPathRepeats, RerollHideRecordsForCarried, RerollHideRecordPathDupes,`n            RerollCarriedOrigLiveOccupied, RerollCarriedAvoidAdded, RerollNewQuotaEffective, Config.NewNodeCount, PoolSize);"
$rolledLine  = 'TEXT("Rolled layout: seed %d, pool %d (vanilla %d, new %d), active %d, pinned %d"),'
$marker      = '2026-11-01-rwg-' + 'rerolldup-1'
$twinsFmt    = 'TEXT("LAYOUTDUMP-TWINS dumpId=%d pathsOnMultipleActiveEntries=%d entriesOnSharedPaths=%d pinnedOnSharedPaths=%d activeWithPath=%d"),'
$twinsArgs   = '        DumpId, SharedPaths.Num(), EntriesOnShared, PinnedOnShared, ActiveWithPath);'
$twinFmt     = 'TEXT("LAYOUTDUMP-TWIN dumpId=%d path=%s entries=%d pinned=%d guids=%s"),'
$twinPred    = '        if (!E.bActive || E.VanillaNodePath.IsEmpty()) { continue; }'
$twinShared  = '        if (KV.Value.Num() < 2) { continue; }'
$chatLine    = 'TEXT("LAYOUTDUMP-CHAT: dumpId=%d posted=%d"), DumpId, bPosted ? 1 : 0);'

function Invoke-RerollDupChecks($T) {
    $f = @()
    $s = $T.sub
    # U1
    if ((Count $s $fillGuid) -ne 1 -or (Count $s $carriedFlag) -ne 1 -or (Count $s $guardedAdd) -ne 1) { $f += '(U1) carried guid set, carried flag or the guarded pool Add is missing' }
    if (-not (Ordered $s @('CarriedEntries.Add(Kept);', $fillGuid, 'for (const FNodeShuffleEntry& Old : Layout)', $carriedFlag, $guardedAdd, 'Re-roll pool (redesign-6)'))) { $f += '(U1) order: carry loop -> guid set -> rebuild loop -> carried flag -> guarded Add' }
    $loop = Span $s $carriedFlag 'Re-roll pool (redesign-6)'
    if (-not $loop -or (Count $loop ('NewLayout.' + 'Add(')) -ne 1) { $f += '(U1) the rebuild loop must hold exactly one NewLayout Add, the guarded one' }
    if ((Count $s $augAppend) -ne 1 -or -not (Ordered $s @('TSet<FString> ExistingPaths;', $augAppend, 'Non-solid augment', 'Full re-scan augment'))) { $f += '(U1) the augments dedupe set must carry the carried paths before both augments' }
    # U2
    if ((Count $s $fillPath) -ne 1 -or (Count $s $dupePred) -ne 1 -or (Count $s $dupeFmt) -ne 1) { $f += '(U2) carried path set, path-dupe predicate or its Display line missing' }
    if (-not (Ordered $s @($carriedFlag, $dupePred, $dupeCount, $dupeFmt, '                continue;', $guardedAdd))) { $f += '(U2) path dupe must count, log and continue before the pool Add' }
    # U3
    if ((Count $s $helperSig) -ne 1 -or (Count $s $convRec) -ne 1 -or (Count $s $hideAdd) -ne 1 -or (Count $s $hideCount) -ne 1 -or (Count $s $hideDedupe) -ne 1) { $f += '(U3) helper, conversion helper use, carried record write, its count or its path dedupe missing' }
    if ((Count $s ('FNodeShuffleSuppressedOriginal ' + 'Rec;')) -ne 1) { $f += '(U3) the record field set must be built in the helper only (no second inline copy)' }
    $hb = Span $s $helperSig 'void ANodeShuffleSubsystem::RollLayout('
    foreach ($fld in @('Rec.VanillaNodePath = E.VanillaNodePath;', 'Rec.Location = E.Location;', 'Rec.TrueLocation = !E.OriginalTrueLocation.IsNearlyZero() ? E.OriginalTrueLocation : E.Location;', 'Rec.bModdedOrigin = !E.OriginalResourceClassPath.StartsWith(TEXT("/Game/"));', 'Rec.OriginalResourceClassPath = E.OriginalResourceClassPath;', 'if (!E.OriginalTrueLocation.IsNearlyZero()) { Rec.OriginalNodeClassPath = E.NodeClassPath; }')) {
        if (-not $hb -or (Count $hb $fld) -ne 1) { $f += "(U3) helper field missing: $fld" }
    }
    if (-not (Ordered $s @('        OriginalNodeRecord.Reset();', $convReset, $convRec, $hideDedupe, $hideAdd, $hideCount, $removeAll, $appendCarry))) { $f += '(U3) carried records must be written after both resets and before the RemoveAll and the carried append' }
    if ((Count $s ('RerollHideRecordsForCarried = ' + 'CarriedEntries')) -ne 0 -or (Count $s ('RerollHideRecordsForCarried = ' + 'Reroll')) -ne 0) { $f += '(U3) hide-record count must be a counter, never a derived formula' }
    # U4
    $comp = Span $s $guardedAdd '            VanillaCount++;'
    if (-not $comp -or (Count $comp 'bCarriedOld') -ne 1 -or (Count $comp 'VanillaResourceCounts.FindOrAdd(Old.OriginalResourceClassPath)++;') -ne 1 -or (Count $comp 'PurityDeckSource.Add(Old.OriginalPurity);') -ne 1 -or (Count $comp 'VanillaLocations.Add(Old.Location);') -ne 1) { $f += '(U4) composition after the guarded Add must count carried originals (no carried-flag guard)' }
    if ((Count $s $zombiePred) -ne 1 -or (Count $s $twinCompose) -ne 1) { $f += '(U4) zombie predicate must skip carried; carried twins count once' }
    # U5
    if ((Count $s $fillExtra) -ne 1 -or (Count $s $quotaLine) -ne 1 -or (Count $s $quotaCall) -ne 1) { $f += '(U5) carried-extras count, effective quota or its use at the extras call missing' }
    if ((Count $s ('FMath::Max(0, Config.' + 'NewNodeCount)')) -ne 0) { $f += '(U5) the raw config quota must not reach generation' }
    if ((Count $s $avoidLoop) -ne 1 -or -not (Ordered $s @($avoidLoop, $quotaCall, $relocAvoid))) { $f += '(U5) carried locations must enter the avoid set before both generation calls' }
    # U6
    if ((Count $s $censusFmt) -ne 1 -or (Count $s ($censusFmt + "`n" + $censusArgs)) -ne 1) { $f += '(U6) census format or argument list changed' }
    $ci = $s.IndexOf($censusFmt); $ri = $s.IndexOf($rolledLine)
    if ($ci -lt 0 -or $ri -lt $ci -or ($ri - $ci) -gt 1200) { $f += '(U6) census must sit directly before the Rolled layout line' }
    else {
        $pre = $s.Substring([Math]::Max(0, $ci - 400), $ri - [Math]::Max(0, $ci - 400))
        if ($pre.Contains('AreDiagnostics' + 'Enabled')) { $f += '(U6) census must be ungated' }
        if (-not $pre.Contains("    if (bIsReroll)`n    {`n        UE_LOG(LogNodeShuffle, Display,`n            " + $censusFmt)) { $f += '(U6) census must be re-roll only' }
    }
    foreach ($tok in @('carried=', 'carriedExtras=', 'excludedFromPool=', 'pathDupesDropped=', 'hideRecordsForCarried=', 'hideRecordPathDupes=', 'carriedAvoidAdded=', 'newQuotaEffective=', 'newQuotaConfig=', 'poolAfter=')) {
        if ((Count $s (' ' + $tok)) -ne 1) { $f += "(U6) census token echoed or missing: $tok" }
    }
    # U7
    if ((Count $T.nsmod ('    UE_LOG(LogNodeShuffle, Display, TEXT("===== NodeShuffle packet marker (' + $marker + ')====="));')) -ne 1 -or (Count $T.nsmod $marker) -ne 1) { $f += '(U7) marker missing or not on its own line' }
    if ((Count $s $marker) -ne 0 -or (Count $T.dump $marker) -ne 0) { $f += '(U7) marker must not appear in the packet sources' }
    # U8
    $d = $T.dump
    if ((Count $d $twinsFmt) -ne 1 -or (Count $d ($twinsFmt + "`n" + $twinsArgs)) -ne 1 -or (Count $d $twinFmt) -ne 1) { $f += '(U8) TWINS/TWIN format or arguments changed' }
    if (-not (Ordered $d @($chatLine, $twinPred, $twinShared, $twinsFmt, $twinFmt))) { $f += '(U8) twins must follow CHAT; predicate active+path; shared means two or more' }
    if ((Count $d $twinPred) -ne 1 -or (Count $d $twinShared) -ne 1) { $f += '(U8) twins predicates missing' }
    $eFmt = [regex]::Match($d, 'TEXT\("LAYOUTDUMP-E: [^"]*"\)').Value
    if (-not $eFmt -or $eFmt.Contains(' path=')) { $f += '(U8) the per-entry E line must carry no path field' }
    return ,$f
}

$base = Invoke-RerollDupChecks $text
if ($base.Count -gt 0) { $base | ForEach-Object { Write-Host "FAIL$_" }; Write-Host "REROLLDUP lint: FAIL -- baseline $($base.Count) problem(s)"; exit 1 }
Write-Host 'BASELINE GREEN: U1..U8 hold across 3 loaded files'

$mutants = @(
    @{ Name = 'X1 carried entry re-enters the pool (guard dropped)'; Key = 'sub'; Expect = 'U1'; Find = $guardedAdd; Repl = '            NewLayout.Add(E); // rwg-rerolldup-1 I1' }
    @{ Name = 'X2 guid set never filled'; Key = 'sub'; Expect = 'U1'; Find = ($fillGuid + "`n"); Repl = '' }
    @{ Name = 'X3 augments dedupe set loses carried paths'; Key = 'sub'; Expect = 'U1'; Find = $augAppend; Repl = '        // append removed' }
    @{ Name = 'X4 dedupe only by EntryGuid'; Key = 'sub'; Expect = 'U2'; Find = $dupePred; Repl = '            if (!bCarriedOld && !Old.VanillaNodePath.IsEmpty() && RerollCarriedGuids.Contains(Old.EntryGuid))' }
    @{ Name = 'X5 path-dupe kept in the pool'; Key = 'sub'; Expect = 'U2'; Find = ($dupeCount + "`n"); Repl = '' }
    @{ Name = 'X6 exclude but forget the hide record'; Key = 'sub'; Expect = 'U3'; Find = ($hideAdd + "`n"); Repl = '' }
    @{ Name = 'X7 hide record written before the conversion reset'; Key = 'sub'; Expect = 'U3'; Find = ($hideAdd + "`n"); Repl = ''
       Find2 = $convReset; Repl2 = ("    for (const FNodeShuffleEntry& C : CarriedEntries) { OriginalNodeRecord.Add(NsMakeSuppressedOriginalRecord(C)); }`n" + $convReset) }
    @{ Name = 'X8 hide record count derived'; Key = 'sub'; Expect = 'U3'; Find = $hideCount; Repl = ('            RerollHideRecordsForCarried = ' + 'CarriedEntries.Num();') }
    @{ Name = 'X9 hide records not deduped by path'; Key = 'sub'; Expect = 'U3'; Find = "            if (bPathRecorded) { ++RerollHideRecordPathDupes; continue; }"; Repl = '            (void)bPathRecorded;' }
    @{ Name = 'X10 conversion copies the record inline again'; Key = 'sub'; Expect = 'U3'; Find = $convRec; Repl = ('                FNodeShuffleSuppressedOriginal ' + 'Rec; Rec = NsMakeSuppressedOriginalRecord(E);') }
    @{ Name = 'X11 helper drops TrueLocation'; Key = 'sub'; Expect = 'U3'; Find = '    Rec.TrueLocation = !E.OriginalTrueLocation.IsNearlyZero() ? E.OriginalTrueLocation : E.Location;'; Repl = '    Rec.TrueLocation = E.Location;' }
    @{ Name = 'X12 carried original dropped from composition'; Key = 'sub'; Expect = 'U4'; Find = $guardedAdd; Repl = ($guardedAdd + "`n            if (bCarriedOld) { continue; }") }
    @{ Name = 'X13 zombie GC reaches carried'; Key = 'sub'; Expect = 'U4'; Find = $zombiePred; Repl = '            if (!Old.bPinned && !IsValid(Live) && !Old.OriginalResourceClassPath.StartsWith(TEXT("/Game/"))' }
    @{ Name = 'X14 raw config quota reaches generation'; Key = 'sub'; Expect = 'U5'; Find = $quotaCall; Repl = ('        GenerateNewLocations(Rng, VanillaLocations, AvoidLocations, FMath::Max(0, Config.' + 'NewNodeCount), NewLocations,') }
    @{ Name = 'X15 carried extras not subtracted'; Key = 'sub'; Expect = 'U5'; Find = $quotaLine; Repl = '    const int32 RerollNewQuotaEffective = FMath::Max(0, Config.NewNodeCount - 0);' }
    @{ Name = 'X16 carried location left out of the avoid set'; Key = 'sub'; Expect = 'U5'; Find = "        AvoidLocations.Add(E.Location);`n        ++RerollCarriedAvoidAdded;"; Repl = '        ++RerollCarriedAvoidAdded;' }
    @{ Name = 'X17 census gated'; Key = 'sub'; Expect = 'U6'; Find = "    if (bIsReroll)`n    {`n        UE_LOG(LogNodeShuffle, Display,`n            TEXT(""REROLLPOOL-CARRY"; Repl = ("    if (bIsReroll && FNodeShuffleModule::AreDiagnostics" + "Enabled())`n    {`n        UE_LOG(LogNodeShuffle, Display,`n            TEXT(""REROLLPOOL-CARRY") }
    @{ Name = 'X18 census field dropped'; Key = 'sub'; Expect = 'U6'; Find = ' hideRecordPathDupes=%d'; Repl = '' }
    @{ Name = 'X19 legend echoes a census token'; Key = 'sub'; Expect = 'U6'; Find = '    // named in its comment above; poolAfter is the PoolSize'; Repl = ('    // named in its comment above; pool' + 'After= is the PoolSize') }
    @{ Name = 'X20 marker reverted'; Key = 'nsmod'; Expect = 'U7'; Find = $marker; Repl = ('2026-11-01-rwg-' + 'rerolldup-0') }
    @{ Name = 'X21 twins counts singletons'; Key = 'dump'; Expect = 'U8'; Find = $twinShared; Repl = '        if (KV.Value.Num() < 1) { continue; }' }
    @{ Name = 'X22 twins counts inactive entries'; Key = 'dump'; Expect = 'U8'; Find = $twinPred; Repl = '        if (E.VanillaNodePath.IsEmpty()) { continue; }' }
    @{ Name = 'X23 path added to every E line (the flood)'; Key = 'dump'; Expect = 'U8'; Find = 'pinned=%d"),' + "`n" + '            I, *E.EntryGuid.ToString()'; Repl = 'pinned=%d path=%s"),' + "`n" + '            I, *E.EntryGuid.ToString()' }
)
$bad = 0
foreach ($m in $mutants) {
    $t2 = [ordered]@{}; foreach ($k in $text.Keys) { $t2[$k] = $text[$k] }
    if ((Count $t2[$m.Key] $m.Find) -ne 1) { Write-Host "MUTANT-SETUP FAIL $($m.Name): find text not unique in $($m.Key)"; $bad++; continue }
    $t2[$m.Key] = $t2[$m.Key].Replace($m.Find, $m.Repl)
    if ($m.ContainsKey('Find2')) {
        if ((Count $t2[$m.Key] $m.Find2) -ne 1) { Write-Host "MUTANT-SETUP FAIL $($m.Name): find2 text not unique in $($m.Key)"; $bad++; continue }
        $t2[$m.Key] = $t2[$m.Key].Replace($m.Find2, $m.Repl2)
    }
    $r = Invoke-RerollDupChecks $t2
    $hits = @($r | Where-Object { $_.StartsWith("($($m.Expect))") })
    if ($hits.Count -gt 0) { Write-Host "RED  (ok) $($m.Name)" }
    else { Write-Host "GREEN (BAD) $($m.Name): expected $($m.Expect), got: $($r -join ' | ')"; $bad++ }
}
if ($bad -gt 0) { Write-Host "FAIL: $bad mutant(s) not caught"; exit 1 }
Write-Host "PASS: REROLLDUP lint -- U1..U8 hold; mutants caught $($mutants.Count)/$($mutants.Count)."
exit 0
