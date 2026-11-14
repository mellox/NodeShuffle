# check_rollchat_lint.ps1 -- ns-rollchat-1 (2026-11-14): the post-shuffle chat breakdown.
# Handoff: _team/test-logs/2026-11-14-nodeshuffle-rollchat-1-handoff.md
#
# LOADS every file the packet touched: NodeShuffleRollChat.cpp, NodeShuffleSubsystem.cpp/.h, NodeShuffle.cpp,
# NodeShuffleBuildMarkers.h, and NodeShuffleMapModePlace.cpp (the deal-pass file, to prove no call lives there).
#   RC1 marker: newest in the markers header, once on its own line in NodeShuffle.cpp, absent from the packet's source files
#   RC2 ONE post-shuffle point: exactly one queue call, inside RollLayout, after EmitRollCensus and the REROLLAUDIT call, as
#       RollLayout's last statement; no call in the deal-pass file (GenerateRwgLocations) nor anywhere else
#   RC3 not on a plain load: the queue is written only by RollChatQueueBreakdown; the queue member is not UPROPERTY (never saved);
#       the first-apply note posts nothing and writes no queue
#   RC4 reconcile: EmitRollCensus stashes its `active` figure after its counting loop and before the ROLLCENSUS line; the breakdown
#       compares named+no-resource against it, reads it once (reset to -1), and the wells total against WellLayout.Num()
#   RC5 order: alphabetical by display name, case-insensitive, class-path tie-break; the count is not a sort key
#   RC6 one emitter: the file posts only through PostChatNotice (tag ROLLCHAT); it builds no chat struct and calls no broadcast
#   RC7 tick site beside the other notice emitters; the first-apply note after LogLayoutSummary
#   RC8 diagnostics: ROLLCHAT-QUEUED, ROLLCHAT-BODY, the outcome line and its two Warning variants; no field=value legend in the summary
#   RC10 the edited lints (also LOADED): rerollwipe R11 pins the queue call after the audit; mcpcontract C4 names this marker;
#        rwg020 G8 anchors on its no-longer-newest note
#   RC9 RollChat.cpp under 500 lines; display name through UFGItemDescriptor::GetItemName
# Mutants run in-file. Exit 0 = all hold and every mutant is caught; 1 = a check failed or a mutant survived; 2 = a pinned file is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    chat  = 'NodeShuffle\Private\NodeShuffleRollChat.cpp'
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    subh  = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
    mark  = 'NodeShuffle\Private\NodeShuffleBuildMarkers.h'
    place = 'NodeShuffle\Private\NodeShuffleMapModePlace.cpp'
}
# The three existing lints this packet edited, loaded so their new pins are pinned here too (RC10).
$lintFiles = [ordered]@{
    lwipe = 'check_rerollwipe_lint.ps1'
    lmcp  = 'check_mcpcontract_lint.ps1'
    l020  = 'check_rwg020_lint.ps1'
}
$text = [ordered]@{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = ((Get-Content -Raw -Encoding utf8 -Path $p) -replace "`r`n", "`n")
}
foreach ($k in $lintFiles.Keys) {
    $p = Join-Path $PSScriptRoot $lintFiles[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned lint '$($lintFiles[$k])' does not exist"; exit 2 }
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

$marker    = '2026-11-14-' + 'rollchat-1'
$queueCall = '    RollChatQueueBreakdown(bIsReroll, ActiveCount); // ns-rollchat-1: post-shuffle chat breakdown; after every deal pass and the census, once per roll'
$auditLine = '    RerollWipeEmitListAudit(bIsReroll, AuditNotYieldedBeforeWipe);'
$censusCall = '    EmitRollCensus(Seed, bIsReroll, PoolSize, TargetActive, VanillaCount, NewLocations.Num(),'
$stash     = '    RollChatCensusActiveEntries = ActiveEntries; // ns-rollchat-1'
$reconcile = '    const bool bNodesMatch = CensusActive >= 0 && NodesNamed + NodesNoResource == CensusActive;'
$wellsRec  = '    const bool bWellsMatch = WellsNamed + WellsNoResource == WellLayout.Num();'
$readOnce  = '    RollChatCensusActiveEntries = -1;'
$sortName  = 'const int32 ByName = A.Name.Compare(B.Name, ESearchCase::IgnoreCase);'
$sortTie   = 'return A.ClassPath.Compare(B.ClassPath, ESearchCase::CaseSensitive) < 0;'
$summaryFmt = 'TEXT("trigger %s | nodeResources %d nodeTotal %d nodesNoResource %d censusActive %d nodesReconciled %d rolledLineActive %d | wellResources %d wellTotal %d wellsNoResource %d wellLayoutEntries %d wellsReconciled %d | nameFallbacks %d"),'
$queueDecl = '    TArray<FString> RollChatPendingBodies;'

function Invoke-Checks($T) {
    $f = @()
    # RC1
    $hdrTags = @([regex]::Matches($T.mark, 'TEXT\("(20\d\d-\d\d-\d\d-[\w-]+)"\),') | ForEach-Object { $_.Groups[1].Value })
    if ($hdrTags.Count -eq 0 -or $hdrTags[-1] -ne $marker) { $f += '(RC1) marker must be the newest entry in the markers header' }
    if ((Count $T.nsmod ('packet marker (' + $marker + ')=====')) -ne 1) { $f += '(RC1) marker once on its own line in NodeShuffle.cpp' }
    if ((Count $T.chat $marker) -ne 0 -or (Count $T.sub $marker) -ne 0 -or (Count $T.subh $marker) -ne 0) { $f += '(RC1) no marker inside the packet source files' }
    # RC2
    $roll = Span $T.sub 'void ANodeShuffleSubsystem::RollLayout(int32 Seed, bool bIsReroll)' 'void ANodeShuffleSubsystem::EmitRollCensus('
    $callTok = 'RollChatQueue' + 'Breakdown('
    if ($roll -eq '') { $f += '(RC2) RollLayout span missing' }
    elseif ((Count $roll $callTok) -ne 1 -or -not (Ordered $roll @($censusCall, $auditLine, ($queueCall + "`n}`n")))) { $f += '(RC2) one queue call, the last statement of RollLayout, after EmitRollCensus and the audit' }
    if ((Count $T.sub $callTok) -ne 1 -or (Count $T.place $callTok) -ne 0 -or (Count $T.chat $callTok) -ne 1 -or (Count $T.subh $callTok) -ne 1) { $f += '(RC2) the queue call exists once (RollLayout); never in the deal-pass file; one definition, one declaration' }
    # RC3
    $qWrite = 'RollChatPendingBodies = '
    if ((Count $T.chat $qWrite) -ne 1 -or (Count $T.sub $qWrite) -ne 0 -or (Count $T.sub 'RollChatPendingBodies') -ne 0) { $f += '(RC3) the queue is written only in RollChatQueueBreakdown' }
    if ((Count $T.subh $queueDecl) -ne 1 -or [regex]::IsMatch($T.subh, 'UPROPERTY[^\n]*\n?\s*TArray<FString> RollChatPendingBodies')) { $f += '(RC3) the queue member is plain (not UPROPERTY, never saved)' }
    $note = Span $T.chat 'void ANodeShuffleSubsystem::RollChatNoteFirstApply() const' "`n}`n"
    if ($note -eq '' -or (Count $note ('PostChat' + 'Notice')) -ne 0 -or (Count $note 'RollChatPendingBodies.Add') -ne 0) { $f += '(RC3) the first-apply note posts nothing and queues nothing' }
    $qb = Span $T.chat 'void ANodeShuffleSubsystem::RollChatQueueBreakdown(bool bIsReroll, int32 RolledLineActive)' 'void ANodeShuffleSubsystem::TickRollChatEmitter()'
    if ($qb -eq '' -or (Count $qb ('PostChat' + 'Notice')) -ne 0) { $f += '(RC3) the queue function never posts (posting waits for the tick gates)' }
    # RC4
    $census = Span $T.sub 'void ANodeShuffleSubsystem::EmitRollCensus(' 'void ANodeShuffleSubsystem::GenerateNewLocations('
    if ($census -eq '' -or -not (Ordered $census @('++ActiveEntries;', $stash, 'TEXT("ROLLCENSUS: seed=%d reroll=%d '))) { $f += '(RC4) EmitRollCensus stashes its active figure after counting and before printing it' }
    if ((Count $T.sub 'RollChatCensusActiveEntries') -ne 1) { $f += '(RC4) only the stash writes the census figure in Subsystem.cpp' }
    if ($qb -eq '' -or -not (Ordered $qb @('const int32 CensusActive = RollChatCensusActiveEntries;', $readOnce, $reconcile, $wellsRec))) { $f += '(RC4) reconcile: read the stash once, reset it, compare nodes and wells' }
    if ((Count $T.subh 'mutable int32 RollChatCensusActiveEntries = -1;') -ne 1) { $f += '(RC4) the stash member declaration' }
    # RC5
    $rows = Span $T.chat 'TArray<FRollChatRow> RollChatRows(' 'void RollChatAppendGroups('
    if ($rows -eq '' -or -not (Ordered $rows @('Rows.StableSort(', $sortName, 'if (ByName != 0) { return ByName < 0; }', $sortTie))) { $f += '(RC5) alphabetical stable sort on the display name, class-path tie-break' }
    if ($rows -ne '' -and ((Count $rows ('A.' + 'Count')) -ne 0 -or (Count $rows ('B.' + 'Count')) -ne 0)) { $f += '(RC5) the count must not be a sort key' }
    # RC6
    foreach ($bad in @(('FChatMessage' + 'Struct'), ('BroadcastChat' + 'Message'), ('AddChatMessage' + 'ToReceived'), ('AFGChat' + 'Manager'))) {
        if ((Count $T.chat $bad) -ne 0) { $f += "(RC6) RollChat.cpp must post only through the shared emitter (found a direct chat use)" }
    }
    if ((Count $T.chat 'FNodeShuffleModule::PostChatNotice(World, RollChatPendingBodies[0], TEXT("ROLLCHAT"))') -ne 1) { $f += '(RC6) one PostChatNotice call with tag ROLLCHAT' }
    # RC7
    if ((Count $T.sub 'TickRollChatEmitter();') -ne 1 -or -not (Ordered $T.sub @('FNodeShuffleModule::TickForeignNoticeEmitter(GetWorld(), /*bNoticesEnabled=*/true);', 'TickRollChatEmitter();'))) { $f += '(RC7) one tick call, after the foreign-notice emitter' }
    if ((Count $T.sub 'RollChatNoteFirstApply();') -ne 1 -or -not (Ordered $T.sub @('            LogLayoutSummary();', '            RollChatNoteFirstApply();'))) { $f += '(RC7) the first-apply note follows LogLayoutSummary' }
    # RC8
    foreach ($fmt in @('TEXT("ROLLCHAT-QUEUED: %d message(s) composed | %s")', 'TEXT("ROLLCHAT-BODY %d/%d: %s")', 'TEXT("ROLLCHAT: composed %d, emitter accepted %d, attempts %d | %s")',
        'TEXT("ROLLCHAT: TOTALS DIFFER -- composed %d, emitter accepted %d, attempts %d | %s")', 'TEXT("ROLLCHAT: EMITTER REFUSED -- composed %d, emitter accepted %d, %d dropped after %d attempts | %s")', $summaryFmt)) {
        if ((Count $T.chat $fmt) -ne 1) { $f += "(RC8) diagnostic format missing or duplicated: $fmt" }
    }
    $sm = [regex]::Match($T.chat, 'RollChatSummary = FString::Printf\(\s*TEXT\("([^"]*)"\)')
    if (-not $sm.Success -or $sm.Groups[1].Value.Contains('=')) { $f += '(RC8) the summary format prints fields without field=value legend syntax' }
    if ((Count $T.chat 'FString Body = FString::Printf(TEXT("%s %d/%d: "), Label, i / RollChatNamesPerMessage + 1, Groups);') -ne 1) { $f += '(RC8) list messages are numbered (Label k/n)' }
    if ((Count $T.subh 'void RollChatQueueBreakdown(bool bIsReroll, int32 RolledLineActive);') -ne 1) { $f += '(RC2) the queue declaration' }
    # RC10 the edited lints carry this packet's pins
    if (-not $T.lwipe.Contains('RollChatQueue' + 'Breakdown(bIsReroll, ActiveCount); // ns-rollchat-1')) { $f += '(RC10) check_rerollwipe R11 must pin the queue call as the statement after the audit' }
    if (-not $T.lmcp.Contains("`$own = '2026-11-14-' + 'rollchat-1'")) { $f += '(RC10) check_mcpcontract C4 must name this packet marker as the newest' }
    if (-not $T.l020.Contains("-not `$T.lmcp.Contains('# rwg-0-2-0-2: no longer the newest; pinned present')")) { $f += '(RC10) check_rwg020 G8 must anchor on the no-longer-newest note' }
    # RC9
    if (($T.chat -split "`n").Count -ge 500) { $f += '(RC9) NodeShuffleRollChat.cpp must stay under 500 lines' }
    if ((Count $T.chat 'UFGItemDescriptor::GetItemName(TSubclassOf<UFGItemDescriptor>(Cls)).ToString()') -ne 1) { $f += '(RC9) display name through GetItemName' }
    return ,$f
}

$fails = Invoke-Checks $text
foreach ($x in $fails) { Write-Host "FAIL$x" }

# Mutants: each must trip the named rule.
$mutants = @(
    @{ Name = 'M1 queue call removed';                 Key = 'sub';   Expect = 'RC2'; Find = $queueCall; Repl = '' }
    @{ Name = 'M2 queue call per pass (after census)'; Key = 'sub';   Expect = 'RC2'; Find = $censusCall; Repl = ('    RollChatQueue' + 'Breakdown(bIsReroll);' + "`n" + $censusCall) }
    @{ Name = 'M3 queue call in the deal-pass file';   Key = 'place'; Expect = 'RC2'; Find = '    RwgBuildWaterVolumeCache();'; Repl = ('    RwgBuildWaterVolumeCache(); RollChatQueue' + 'Breakdown(true);') }
    @{ Name = 'M4 queue saved';                        Key = 'subh';  Expect = 'RC3'; Find = $queueDecl; Repl = ('    UPROPERTY(Save' + 'Game)' + "`n" + $queueDecl) }
    @{ Name = 'M5 first-apply note posts';             Key = 'chat';  Expect = 'RC3'; Find = '    // Reached once per session'; Repl = ('    FNodeShuffleModule::PostChat' + 'Notice(nullptr, FString(), TEXT("X"));' + "`n    // Reached once per session") }
    @{ Name = 'M6 census stash removed';               Key = 'sub';   Expect = 'RC4'; Find = $stash; Repl = '   ' }
    @{ Name = 'M7 reconcile widened';                  Key = 'chat';  Expect = 'RC4'; Find = $reconcile; Repl = '    const bool bNodesMatch = CensusActive >= 0;' }
    @{ Name = 'M8 sorted by count';                    Key = 'chat';  Expect = 'RC5'; Find = $sortName; Repl = ('const int32 ByName = B.' + 'Count - A.' + 'Count;') }
    @{ Name = 'M9 direct broadcast';                   Key = 'chat';  Expect = 'RC6'; Find = '    ++RollChatAttempts;'; Repl = ('    ++RollChatAttempts; // ' + 'BroadcastChat' + 'Message') }
    @{ Name = 'M10 legend syntax in summary';          Key = 'chat';  Expect = 'RC8'; Find = 'censusActive %d nodesReconciled'; Repl = ('censusActive' + '=%d nodesReconciled') }
    @{ Name = 'M11 marker not newest';                 Key = 'mark';  Expect = 'RC1'; Find = ('        TEXT("' + $marker + '"),'); Repl = '' }
    @{ Name = 'M13 rerollwipe lint unpins the call';   Key = 'lwipe'; Expect = 'RC10'; Find = ('RollChatQueue' + 'Breakdown(bIsReroll, ActiveCount); // ns-rollchat-1'); Repl = 'X' }
    @{ Name = 'M14 mcpcontract newest reverted';       Key = 'lmcp';  Expect = 'RC10'; Find = "'2026-11-14-' + 'rollchat-1'"; Repl = "'2026-11-13-' + 'rwg-0-2-0-2'" }
    @{ Name = 'M15 groups unnumbered';                 Key = 'chat';  Expect = 'RC8'; Find = 'TEXT("%s %d/%d: "), Label, i / RollChatNamesPerMessage + 1, Groups)'; Repl = 'TEXT("%s: "), Label)' }
    @{ Name = 'M12 tick call removed';                 Key = 'sub';   Expect = 'RC7'; Find = '        TickRollChatEmitter();'; Repl = '       ' }
)
$caught = 0
foreach ($m in $mutants) {
    $copy = [ordered]@{}; foreach ($k in $text.Keys) { $copy[$k] = $text[$k] }
    if (-not $copy[$m.Key].Contains($m.Find)) { Write-Host "FAIL(mutant) $($m.Name): anchor not found"; $fails += 'mutant-anchor'; continue }
    $copy[$m.Key] = $copy[$m.Key].Replace($m.Find, $m.Repl)
    $mf = Invoke-Checks $copy
    if (@($mf | Where-Object { $_.StartsWith('(' + $m.Expect + ')') }).Count -gt 0) { $caught++ }
    else { Write-Host "FAIL(mutant) $($m.Name) survived (expected $($m.Expect))"; $fails += 'mutant-survived' }
}

if ($fails.Count -eq 0) { Write-Host "PASS check_rollchat_lint: RC1-RC10 hold; $caught/$($mutants.Count) mutants caught"; exit 0 }
Write-Host "check_rollchat_lint: $($fails.Count) failure(s); $caught/$($mutants.Count) mutants caught"
exit 1
