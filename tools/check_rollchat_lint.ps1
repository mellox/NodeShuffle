# check_rollchat_lint.ps1 -- ns-rollchat-1 (2026-11-14): the post-shuffle chat breakdown (round 2: two-column fixed-width table,
# one row per chat message, paced drain). Handoff: _team/test-logs/2026-11-14-nodeshuffle-rollchat-1-handoff.md
#
# LOADS every file the packet touched: NodeShuffleRollChat.cpp, NodeShuffleSubsystem.cpp/.h, NodeShuffle.cpp, NodeShuffleBuildMarkers.h,
# NodeShuffleMapModePlace.cpp (the deal-pass file, to prove no call lives there), and the three lints it edited
# (check_rerollwipe_lint, check_mcpcontract_lint, check_rwg020_lint).
#   RC1  marker: listed in the markers header (newest until ns-rocks-1), once on its own line in NodeShuffle.cpp, absent from the packet's source files
#   RC2  ONE post-shuffle point: exactly one queue call, the last statement of RollLayout after EmitRollCensus and the REROLLAUDIT call;
#        no call in the deal-pass file; one definition, one declaration
#   RC3  not on a plain load: rows are appended only in RollChatQueueBreakdown; the row member is not UPROPERTY (never saved); the
#        first-apply note posts nothing and queues nothing; the queue function never posts
#   RC4  reconcile: EmitRollCensus stashes its `active` figure; the breakdown reads it once, resets it, compares NodeTotal to it, and the
#        Total row prints NodeTotal; the wells self-comparison is gone (review F2)
#   RC5  order: alphabetical by display name, case-insensitive, class-path tie-break; the count is not a sort key
#   RC6  one emitter: posts only through PostChatNotice (tag ROLLCHAT), one row per call; no chat struct, no broadcast
#   RC7  tick site beside the other notice emitters; the first-apply note after LogLayoutSummary; EndPlay clears the drain (teardown)
#   RC8  diagnostics: ROLLCHAT-QUEUED (lines, messages, linesPerMessage, count-column digit figure), ROLLCHAT-LINE delimited, ROLLCHAT-DONE and its Warning variant; no field=value legend
#   RC9  RollChat.cpp under 500 lines; display name through UFGItemDescriptor::GetItemName
#   RC10 the edited lints: rerollwipe R11 pins the queue call after the audit; mcpcontract C4 names this marker; rwg020 G8 its note
#   RC11 (r4) the COUNT-FIRST composer: one row builder; gap 2*M-2 after the header word, 8+2*(M-digits) after every count; M floored
#        at 3 from the Total's digits; the old name-first fixed-width composer and its width variables are forbidden; computed
#        from this table; header, (no resource) and Total rows go through it; the old pipe/three-column, grouped/numbered and wells
#        shapes are absent
#   RC12 paced drain: one one-shot timer per row (never looping), one post per timer fire, no loop in the step; index advances after acceptance
#   RC13 drop-on-new-roll: the queue function first clears any table still draining; the clear stops the timer, logs the drop, resets
#   RC14 (r3, review F2) interleave gate: while RollChatIsDraining() the pending and T61 emitters are NOT called (deferred, not
#        dropped); the helper reads our own armed flag, not a timer query
#   RC16 (ns-quietchat-1) the shared emitter (NodeShuffleObserveNotice.cpp, LOADED) passes the first player controller as the
#        broadcast instigator ONLY under bPassFirstPlayerAsInstigator and never on a dedicated server; the take-to-broadcast block is
#        pinned CONTIGUOUS (review F6); PENDING (NodeShufflePendingNotice.cpp, LOADED) and T61 pass no fourth argument (review F4) (default false in NodeShuffle.h, LOADED); otherwise nullptr;
#        the EMITTED line prints instigatorRequested and instigatorPassed; ROLLCHAT is the caller that sets the flag (RC6)
#   RC17 (ns-quietchat-1 round 2) table lines are composed into RollChatLines, then packed in order RollChatLinesPerMessage (10) at
#        a time, joined by a line break, into RollChatMessages; the drain posts one MESSAGE per fire and logs every line it carried
#        (ROLLCHAT-LINE, delimited, with line and message index); the joined body is never logged
#   RC15 (r3, review F4) duplicate display names get the short class name appended inside RollChatTableRows, before the rows are composed
#   r3 also: no repeat-character helper and no timer-active query anywhere in the packet files (zero-new-import rule, needles by
#        concatenation, in RC11); the armed flag is set on every arm and cleared on fire and on clear (RC12); one DONE-family line
#        on a give-up (RC8, review F5); the written-never-read totals member is gone (RC4, review F8)
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
    emit  = 'NodeShuffle\Private\NodeShuffleObserveNotice.cpp'   # ns-quietchat-1
    modh  = 'NodeShuffle\Public\NodeShuffle.h'                   # ns-quietchat-1
    pend  = 'NodeShuffle\Private\NodeShufflePendingNotice.cpp'   # ns-quietchat-1 review F4
}
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

$marker     = '2026-11-14-' + 'rollchat-1'
$queueCall  = '    RollChatQueueBreakdown(bIsReroll, ActiveCount); // ns-rollchat-1: post-shuffle chat breakdown; after every deal pass and the census, once per roll'
$auditLine  = '    RerollWipeEmitListAudit(bIsReroll, AuditNotYieldedBeforeWipe);'
$censusCall = '    EmitRollCensus(Seed, bIsReroll, PoolSize, TargetActive, VanillaCount, NewLocations.Num(),'
$stash      = '    RollChatCensusActiveEntries = ActiveEntries; // ns-rollchat-1'
$reconcile  = '    const bool bNodesMatch = CensusActive >= 0 && NodeTotal == CensusActive;'
$nodeTotal  = '    const int32 NodeTotal = NodesNamed + NodesNoResource;'
$readOnce   = '    RollChatCensusActiveEntries = -1;'
$sortName   = 'const int32 ByName = A.Name.Compare(B.Name, ESearchCase::IgnoreCase);'
$sortTie    = 'return A.ClassPath.Compare(B.ClassPath, ESearchCase::CaseSensitive) < 0;'
$summaryFmt = 'TEXT("trigger %s | lines %d messages %d linesPerMessage %d resources %d | nodeTotal %d nodesNoResource %d censusActive %d nodesReconciled %d rolledLineActive %d | wellLayoutEntries %d | countColumnDigits %d | nameFallbacks %d"),'
$rowsDecl   = '    TArray<FString> RollChatMessages;'
$linesDecl  = '    TArray<FString> RollChatLines;'
$perMsg     = '    constexpr int32 RollChatLinesPerMessage = 10;'
$linesUpTo  = '        return FMath::Min(MessagesSent * RollChatLinesPerMessage, LineCount);'
$packBlock  = '    for (int32 First = 0; First < RollChatLines.Num(); First += RollChatLinesPerMessage)' + "`n" +
              '    {' + "`n" +
              '        const int32 End = FMath::Min(First + RollChatLinesPerMessage, RollChatLines.Num());' + "`n" +
              '        FString Message;' + "`n" +
              '        for (int32 i = First; i < End; ++i)' + "`n" +
              '        {' + "`n" +
              '            if (i > First) { Message += TEXT("\n"); }' + "`n" +
              '            Message += RollChatLines[i];' + "`n" +
              '        }' + "`n" +
              '        RollChatMessages.Add(Message);' + "`n" +
              '    }'
$lineLog    = '        const int32 FirstLine = RollChatNext * RollChatLinesPerMessage;' + "`n" +
              '        const int32 EndLine = RollChatLinesInFirstMessages(RollChatNext + 1, RollChatLines.Num());' + "`n" +
              '        for (int32 i = FirstLine; i < EndLine; ++i)' + "`n" +
              '        {' + "`n" +
              '            UE_LOG(LogNodeShuffle, Display, TEXT("ROLLCHAT-LINE %d/%d in message %d/%d: |%s|"),' + "`n" +
              '                i + 1, RollChatLines.Num(), RollChatNext + 1, RollChatMessages.Num(), *RollChatLines[i]);' + "`n" +
              '        }'
$rowBody    = "        const int32 Gap = bHeader ? (2 * M - 2) : (8 + 2 * (M - Count.Len()));`n        FString Row = Count;`n        for (int32 i = 0; i < Gap; ++i) { Row.AppendChar(TEXT(' ')); }`n        Row += Label;`n        return Row;"
$hdrRow     = 'RollChatLines.Add(RollChatCountRow(TEXT("Count"), TEXT("Resource"), CountDigitsM, true));'
$totalRow   = 'RollChatLines.Add(RollChatCountRow(FString::FromInt(NodeTotal), TEXT("Total"), CountDigitsM, false));'
$noResRow   = 'RollChatLines.Add(RollChatCountRow(FString::FromInt(NodesNoResource), NoResourceLabel, CountDigitsM, false));'
$resRow     = 'RollChatLines.Add(RollChatCountRow(FString::FromInt(R.Nodes), R.Name, CountDigitsM, false));'
$mLine      = 'const int32 CountDigitsM = FMath::Max(3, FString::FromInt(NodeTotal).Len());'
$rowSig     = 'FString RollChatCountRow(const FString& Count, const FString& Label, int32 M, bool bHeader)'
$oldComposer = @(('RollChat' + 'FixedRow'), ('Name' + 'Width'), ('Count' + 'Width'), ('line' + 'Length'))
$opening    = 'RollChatLines.Add(FString::Printf(TEXT("New layout (%s), counted at the shuffle:"), Trigger));'
$postCall   = 'FNodeShuffleModule::PostChatNotice(World, RollChatMessages[RollChatNext], TEXT("ROLLCHAT"), /*bPassFirstPlayerAsInstigator=*/true)'
$armOnce    = 'World->GetTimerManager().SetTimer(RollChatDrainTimer, this, &ANodeShuffleSubsystem::RollChatDrainStep,' + "`n" + '        RollChatRowGapSeconds, false);'
$armNext    = 'World->GetTimerManager().SetTimer(RollChatDrainTimer, this, &ANodeShuffleSubsystem::RollChatDrainStep,' + "`n" + '            RollChatRowGapSeconds, false);'
$supersede  = '    RollChatClear(TEXT("superseded by a new roll"));'
$teardown   = '    RollChatClear(TEXT("world teardown"));'
$bcast      = 'Chat->' + 'BroadcastChat' + 'Message(Msg, '
$instBlock  = '    APlayerController* Instigator = nullptr;' + "`n" +
              '    if (bPassFirstPlayerAsInstigator && World->GetNetMode() != NM_DedicatedServer)' + "`n" +
              '    {' + "`n" +
              '        FConstPlayerControllerIterator PcIt = World->GetPlayerControllerIterator();' + "`n" +
              '        if (PcIt) { Instigator = PcIt->Get(); }' + "`n" +
              '    }' + "`n" +
              '    ' + $bcast + 'Instigator);'
$emitSig    = 'bool FNodeShuffleModule::PostChatNotice(UWorld* World, const FString& Body, const TCHAR* SourceTag,' + "`n" + '    bool bPassFirstPlayerAsInstigator)'
$emitDecl   = '    static bool PostChatNotice(class UWorld* World, const FString& Body, const TCHAR* SourceTag,' + "`n" + '        bool bPassFirstPlayerAsInstigator = false);'
$emitLog    = 'bodyChars=%d instigatorRequested=%d instigatorPassed=%d netMode=%d)"),' + "`n" + '        SourceTag, (int32)EFGChatMessageType::CMT_CustomMessage, Body.Len(),' + "`n" + '        bPassFirstPlayerAsInstigator ? 1 : 0, Instigator ? 1 : 0, (int32)World->GetNetMode());'
$guard      = '    if (bRollChatTimerArmed) { return; }'
$isDraining = 'return RollChatNext < RollChatMessages.Num() && bRollChatTimerArmed;'
$giveUp     = '        { const FString Why = FString::Printf(TEXT("emitter refused %d time(s)"), RollChatAttempts); RollChatClear(*Why); return; }'
$dupAppend  = '            Rows[i].Name += FString::Printf(TEXT(" (%s)"), *(Rows[i].ClassPath.FindLastChar(TEXT(''.''), Dot) ? Rows[i].ClassPath.Mid(Dot + 1) : Rows[i].ClassPath));'
$noChrN     = 'Chr' + 'N('
$noTimerQ   = 'IsTimer' + 'Active('
$totalsMember = 'bRollChatTotals' + 'Match'
$gateSeq    = @('const bool bRollChatDraining = RollChatIsDraining();', 'if (!bRollChatDraining)', 'EmitPendingNoticeIfReady(/*bNoticesEnabled=*/true);',
                'FNodeShuffleModule::TickForeignNoticeEmitter(GetWorld(), /*bNoticesEnabled=*/true);', 'TickRollChatEmitter();')

function Invoke-Checks($T) {
    $f = @()
    $qb    = Span $T.chat 'void ANodeShuffleSubsystem::RollChatQueueBreakdown(bool bIsReroll, int32 RolledLineActive)' 'void ANodeShuffleSubsystem::TickRollChatEmitter()'
    $tick  = Span $T.chat 'void ANodeShuffleSubsystem::TickRollChatEmitter()' 'void ANodeShuffleSubsystem::RollChatDrainStep()'
    $step  = Span $T.chat 'void ANodeShuffleSubsystem::RollChatDrainStep()' 'void ANodeShuffleSubsystem::RollChatNoteFirstApply() const'
    $clear = Span $T.chat 'void ANodeShuffleSubsystem::RollChatClear(const TCHAR* Why)' 'void ANodeShuffleSubsystem::RollChatQueueBreakdown('
    $note  = Span $T.chat 'void ANodeShuffleSubsystem::RollChatNoteFirstApply() const' "`n}`n"
    $helper = Span $T.chat 'bool ANodeShuffleSubsystem::RollChatIsDraining() const' "`n}`n"
    $rowFn = Span $T.chat $rowSig "`n    }`n"
    # RC1
    $hdrTags = @([regex]::Matches($T.mark, 'TEXT\("(20\d\d-\d\d-\d\d-[\w-]+)"\),') | ForEach-Object { $_.Groups[1].Value })
    if ($hdrTags -notcontains $marker) { $f += '(RC1) marker must stay listed in the markers header (ns-rocks-1 is newer)' }
    if ((Count $T.nsmod ('packet marker (' + $marker + ')=====')) -ne 1) { $f += '(RC1) marker once on its own line in NodeShuffle.cpp' }
    if ((Count $T.chat $marker) -ne 0 -or (Count $T.sub $marker) -ne 0 -or (Count $T.subh $marker) -ne 0) { $f += '(RC1) no marker inside the packet source files' }
    # RC2
    $roll = Span $T.sub 'void ANodeShuffleSubsystem::RollLayout(int32 Seed, bool bIsReroll)' 'void ANodeShuffleSubsystem::EmitRollCensus('
    $callTok = 'RollChatQueue' + 'Breakdown('
    if ($roll -eq '') { $f += '(RC2) RollLayout span missing' }
    elseif ((Count $roll $callTok) -ne 1 -or -not (Ordered $roll @($censusCall, $auditLine, ($queueCall + "`n}`n")))) { $f += '(RC2) one queue call, the last statement of RollLayout, after EmitRollCensus and the audit' }
    if ((Count $T.sub $callTok) -ne 1 -or (Count $T.place $callTok) -ne 0 -or (Count $T.chat $callTok) -ne 1 -or (Count $T.subh $callTok) -ne 1) { $f += '(RC2) the queue call exists once (RollLayout); never in the deal-pass file; one definition, one declaration' }
    if ((Count $T.subh 'void RollChatQueueBreakdown(bool bIsReroll, int32 RolledLineActive);') -ne 1) { $f += '(RC2) the queue declaration' }
    # RC3
    foreach ($q in @('RollChatLines.Add(', 'RollChatMessages.Add(')) { if ($qb -eq '' -or (Count $T.chat $q) -ne (Count $qb $q)) { $f += '(RC3) lines and messages are appended only in RollChatQueueBreakdown' } }
    if ((Count $T.sub 'RollChatMessages') -ne 0 -or (Count $T.sub 'RollChatLines') -ne 0 -or (Count $T.sub ('RollChat' + 'Rows')) -ne 0) { $f += '(RC3) Subsystem.cpp touches no table member' }
    if ((Count $T.subh $rowsDecl) -ne 1 -or (Count $T.subh $linesDecl) -ne 1 -or [regex]::IsMatch($T.subh, 'UPROPERTY[^\n]*\n?\s*TArray<FString> RollChat(Messages|Lines)')) { $f += '(RC3) the line and message members are plain (not UPROPERTY, never saved)' }
    if ($note -eq '' -or (Count $note ('PostChat' + 'Notice')) -ne 0 -or (Count $note '.Add(') -ne 0) { $f += '(RC3) the first-apply note posts nothing and queues nothing' }
    if ($qb -eq '' -or (Count $qb ('PostChat' + 'Notice')) -ne 0) { $f += '(RC3) the queue function never posts (posting waits for the tick gates)' }
    # RC4
    $census = Span $T.sub 'void ANodeShuffleSubsystem::EmitRollCensus(' 'void ANodeShuffleSubsystem::GenerateNewLocations('
    if ($census -eq '' -or -not (Ordered $census @('++ActiveEntries;', $stash, 'TEXT("ROLLCENSUS: seed=%d reroll=%d '))) { $f += '(RC4) EmitRollCensus stashes its active figure after counting and before printing it' }
    if ((Count $T.sub 'RollChatCensusActiveEntries') -ne 1) { $f += '(RC4) only the stash writes the census figure in Subsystem.cpp' }
    if ($qb -eq '' -or -not (Ordered $qb @('const int32 CensusActive = RollChatCensusActiveEntries;', $readOnce, $nodeTotal, $reconcile, $totalRow))) { $f += '(RC4) reconcile: read the stash once, reset it, NodeTotal compared and printed in the Total row' }
    if ((Count $T.subh 'mutable int32 RollChatCensusActiveEntries = -1;') -ne 1) { $f += '(RC4) the stash member declaration' }
    if ((Count $T.chat $totalsMember) -ne 0 -or (Count $T.subh $totalsMember) -ne 0) { $f += '(RC4) the written-never-read totals member stays gone (review F8)' }
    if ((Count $T.chat ('wells' + 'Reconciled')) -ne 0 -or (Count $T.chat ('bWells' + 'Match')) -ne 0) { $f += '(RC4) the wells self-comparison must stay gone (review F2)' }
    # RC5
    $rows = Span $T.chat 'TArray<FRollChatRow> RollChatTableRows(' 'FString RollChatCountRow('
    if ($rows -eq '' -or -not (Ordered $rows @('Rows.StableSort(', $sortName, 'if (ByName != 0) { return ByName < 0; }', $sortTie))) { $f += '(RC5) alphabetical stable sort on the display name, class-path tie-break' }
    if ($rows -ne '' -and ((Count $rows ('A.' + 'Nodes')) -ne 0 -or (Count $rows ('B.' + 'Nodes')) -ne 0)) { $f += '(RC5) the count must not be a sort key' }
    # RC6
    foreach ($bad in @(('FChatMessage' + 'Struct'), ('BroadcastChat' + 'Message'), ('AddChatMessage' + 'ToReceived'), ('AFGChat' + 'Manager'))) {
        if ((Count $T.chat $bad) -ne 0) { $f += '(RC6) RollChat.cpp must post only through the shared emitter (found a direct chat use)' }
    }
    if ((Count $T.chat ('PostChat' + 'Notice(')) -ne 1 -or (Count $step $postCall) -ne 1) { $f += '(RC6) one PostChatNotice call, in the drain step, one row, tag ROLLCHAT' }
    # RC7
    if ((Count $T.sub 'TickRollChatEmitter();') -ne 1 -or -not (Ordered $T.sub @('FNodeShuffleModule::TickForeignNoticeEmitter(GetWorld(), /*bNoticesEnabled=*/true);', 'TickRollChatEmitter();'))) { $f += '(RC7) one tick call, after the foreign-notice emitter' }
    if ((Count $T.sub 'RollChatNoteFirstApply();') -ne 1 -or -not (Ordered $T.sub @('            LogLayoutSummary();', '            RollChatNoteFirstApply();'))) { $f += '(RC7) the first-apply note follows LogLayoutSummary' }
    $endPlay = Span $T.sub 'void ANodeShuffleSubsystem::EndPlay(' 'Super::EndPlay(EndPlayReason);'
    if ($endPlay -eq '' -or (Count $endPlay $teardown) -ne 1) { $f += '(RC7) EndPlay clears the drain before Super::EndPlay (teardown)' }
    # RC8
    foreach ($fmt in @('TEXT("ROLLCHAT-QUEUED: %s")', 'TEXT("ROLLCHAT-LINE %d/%d in message %d/%d: |%s|")', 'TEXT("ROLLCHAT-DONE: %d of %d message(s) sent, %d of %d line(s) sent, in %.1f s, %d refusal(s) | %s")',
        'TEXT("ROLLCHAT-DONE: table dropped part-way (%s) -- %d of %d message(s) sent, %d of %d line(s) sent, %d message(s) dropped | %s")', $summaryFmt)) {
        if ((Count $T.chat $fmt) -ne 1) { $f += "(RC8) diagnostic format missing or duplicated: $fmt" }
    }
    if ((Count $T.chat 'TEXT("ROLLCHAT-DONE') -ne 2 -or (Count $step $giveUp) -ne 1) { $f += '(RC8) a give-up logs exactly one DONE-family line: the clear, called with the refusal count (review F5)' }
    foreach ($old in @(('ROLLCHAT-' + 'BODY'), ('row' + '(s)'), ('| rows ' + '%d'))) { if ((Count $T.chat $old) -ne 0) { $f += '(RC8) a pre-packing log word is back (one row per message is no longer true)' } }
    $sm = [regex]::Match($T.chat, 'RollChatSummary = FString::Printf\(\s*TEXT\("([^"]*)"\)')
    if (-not $sm.Success -or $sm.Groups[1].Value.Contains('=')) { $f += '(RC8) the summary format prints fields without field=value legend syntax' }
    # RC9
    if (($T.chat -split "`n").Count -ge 500) { $f += '(RC9) NodeShuffleRollChat.cpp must stay under 500 lines' }
    if ((Count $T.chat 'UFGItemDescriptor::GetItemName(TSubclassOf<UFGItemDescriptor>(Cls)).ToString()') -ne 1) { $f += '(RC9) display name through GetItemName' }
    # RC10
    if (-not $T.lwipe.Contains('RollChatQueue' + 'Breakdown(bIsReroll, ActiveCount); // ns-rollchat-1')) { $f += '(RC10) check_rerollwipe R11 must pin the queue call as the statement after the audit' }
    if (-not $T.lmcp.Contains('# ns-rollchat-1: no longer the newest; pinned present (check_rollchat_lint RC1)')) { $f += '(RC10) check_mcpcontract C4 must carry the no-longer-newest note for this marker' }
    if (-not $T.l020.Contains("-not `$T.lmcp.Contains('# rwg-0-2-0-2: no longer the newest; pinned present')")) { $f += '(RC10) check_rwg020 G8 must anchor on the no-longer-newest note' }
    # RC11
    if ((Count $T.chat $rowBody) -ne 1 -or (Count $T.chat $rowSig) -ne 1) { $f += '(RC11) the one count-first row builder: count, then 2*M-2 (header) or 8+2*(M-digits) spaces, then the label' }
    foreach ($old in $oldComposer) { if ((Count $T.chat $old) -ne 0) { $f += '(RC11) the old name-first fixed-width composer or its width fields are back' } }
    if ($rowFn -eq '' -or (Count $T.chat 'AppendChar(') -ne (Count $rowFn 'AppendChar(') -or (Count $T.chat 'RollChatCountRow(') -ne 5) { $f += '(RC11) the row layout lives in RollChatCountRow only (one definition, four callers)' }
    foreach ($k in @('chat', 'sub', 'subh')) { if ((Count $T[$k] $noChrN) -ne 0 -or (Count $T[$k] $noTimerQ) -ne 0) { $f += "(RC11) zero-new-import rule: no repeat-character helper and no timer-active query ($k)" } }
    if ($qb -eq '' -or -not (Ordered $qb @($mLine, $opening, $hdrRow, $resRow, $noResRow, $totalRow))) { $f += '(RC11) M computed from the Total before the rows; opening, header, resource, (no resource), Total in order' }
    if ($qb -ne '' -and ((Count $qb 'RollChatLines.Add(') -ne 5 -or (Count $qb 'RollChatLines.Add(RollChatCountRow(') -ne 4)) { $f += '(RC11) every table row goes through the count-first builder (only the opening line does not)' }
    foreach ($old in @(('" ' + '| "'), ('%s ' + '| %d'), ('Resource ' + '| Nodes'), ('RollChatNamesPer' + 'Message'), ('RollChatAppend' + 'Groups'), ('%d' + '/%d: "), Label'), ('TEXT("' + 'Wells'), ('Well' + 'Counts'), "`t")) {
        if ((Count $T.chat $old) -ne 0) { $f += "(RC11) an old shape is back (pipe/three-column, grouped/numbered, wells column, or a tab)" }
    }
    # RC12
    if ((Count $T.chat 'constexpr float RollChatRowGapSeconds = 0.3f;') -ne 1) { $f += '(RC12) the row gap constant' }
    if ($tick -eq '' -or (Count $tick $armOnce) -ne 1 -or (Count $tick ('PostChat' + 'Notice')) -ne 0) { $f += '(RC12) the tick only arms the one-shot drain timer; it never posts' }
    if ($step -eq '' -or (Count $step $armNext) -ne 1 -or -not (Ordered $step @(('if (!' + $postCall + ')'), 'return;', 'ROLLCHAT-LINE', '++RollChatNext;', $armNext))) { $f += '(RC12) the step posts one row, advances only after acceptance, then re-arms one gap later' }
    if ($step -ne '' -and (Count $step '++RollChatNext;') -ne 1) { $f += '(RC12) the step posts one row, advances only after acceptance, then re-arms one gap later' }
    if ($step -ne '' -and ((Count $step 'while (') -ne 0 -or (Count $step 'for (') -ne 1 -or (Count $lineLog 'for (') -ne 1)) { $f += '(RC12) one post per timer fire: the only loop in the drain step is the per-line log loop (RC17)' }
    if ((Count $T.chat ('RollChatRowGapSeconds, ' + 'true')) -ne 0) { $f += '(RC12) the drain timer must never loop' }
    if ((Count $T.subh '    FTimerHandle RollChatDrainTimer;') -ne 1) { $f += '(RC12) the drain timer handle' }
    if ($step -eq '' -or -not (Ordered $step @('++RollChatAttempts;', 'if (RollChatAttempts >= RollChatMaxEmitAttempts)', 'RollChatClear('))) { $f += '(RC12) a refusal is counted and the budget drops the table' }
    if ($tick -eq '' -or -not (Ordered $tick @($guard, $armOnce, 'bRollChatTimerArmed = true;'))) { $f += '(RC12) the tick never re-arms a running drain' }
    if ($step -eq '' -or -not (Ordered $step @('bRollChatTimerArmed = false;', ('PostChat' + 'Notice('), $armNext, 'bRollChatTimerArmed = true;'))) { $f += '(RC12) the armed flag clears when the one-shot fires and is set on each re-arm' }
    if ($clear -eq '' -or (Count $clear 'bRollChatTimerArmed = false;') -ne 1) { $f += '(RC12) the clear drops the armed flag' }
    if ((Count $T.chat 'bRollChatTimerArmed = true;') -ne 2) { $f += '(RC12) the flag is set at exactly the two arm sites' }
    # RC13
    if ($qb -eq '' -or -not (Ordered $qb @($supersede, 'RollChatLines.Add('))) { $f += '(RC13) a new roll first drops any table still draining' }
    if ($clear -eq '' -or -not (Ordered $clear @('ClearTimer(RollChatDrainTimer);', 'table dropped part-way', 'RollChatMessages.Reset();', 'RollChatLines.Reset();', 'RollChatNext = 0;'))) { $f += '(RC13) the clear stops the timer, logs the drop, resets the rows and index' }
    # RC16 (ns-quietchat-1)
    $emitFn = Span $T.emit $emitSig 'void FNodeShuffleModule::ResetForeignNoticeState()'
    if ($emitFn -eq '' -or -not (Ordered $emitFn @($instBlock, $emitLog)) -or (Count $emitFn $bcast) -ne 1 -or (Count $emitFn ($bcast + 'nullptr')) -ne 0) { $f += '(RC16) the emitter passes the first player controller only under the flag (nullptr otherwise) and logs requested/passed' }
    if ((Count $T.modh $emitDecl) -ne 1) { $f += '(RC16) the flag defaults to false in the declaration (the other four callers keep nullptr)' }
    if ((Count $T.chat '/*bPassFirstPlayerAsInstigator=*/true') -ne 1) { $f += '(RC16) only the ROLLCHAT call in RollChat.cpp sets the flag' }
    if ((Count $T.pend 'FNodeShuffleModule::PostChatNotice(World, Body, TEXT("PENDING"));') -ne 1 -or (Count $T.emit 'if (PostChatNotice(World, BuildForeignNoticeBody(Unannounced), TEXT("T61")))') -ne 1) { $f += '(RC16) the PENDING and T61 calls pass no fourth argument (they keep nullptr)' }
    # RC17 (ns-quietchat-1 round 2) packing: lines composed, then packed in order L at a time joined by a line break; the drain
    #      posts the MESSAGE and logs every line it carried, once, delimited
    if ((Count $T.chat $perMsg) -ne 1 -or (Count $T.chat $linesUpTo) -ne 1) { $f += '(RC17) the packing constant (10) and the lines-carried helper' }
    if ($qb -eq '' -or (Count $qb $packBlock) -ne 1 -or -not (Ordered $qb @($totalRow, $packBlock, '    RollChatNext = 0;'))) { $f += '(RC17) every line is packed, in order, after the Total line is composed' }
    if ($step -eq '' -or (Count $step $lineLog) -ne 1 -or -not (Ordered $step @(('if (!' + $postCall + ')'), $lineLog, '++RollChatNext;'))) { $f += '(RC17) each accepted message logs every line it carried, once, delimited' }
    if ($step -ne '' -and ((Count $step 'RollChatMessages[RollChatNext]') -ne 1)) { $f += '(RC17) the drain step reads the message only to post it (the joined body is never logged)' }
    # RC14
    if ((Count $T.sub $gateSeq[0]) -ne 1 -or -not (Ordered $T.sub $gateSeq) -or (Count $T.sub $gateSeq[2]) -ne 1 -or (Count $T.sub $gateSeq[3]) -ne 1) { $f += '(RC14) the pending and T61 emitters are called only when no table is draining' }
    if ($helper -eq '' -or (Count $helper $isDraining) -ne 1 -or (Count $T.subh 'bool RollChatIsDraining() const;') -ne 1) { $f += '(RC14) RollChatIsDraining reads rows-unsent AND the armed flag' }
    # RC15
    $rows15 = Span $T.chat 'TArray<FRollChatRow> RollChatTableRows(' 'FString RollChatCountRow('
    if ($rows15 -eq '' -or -not (Ordered $rows15 @('Rows.StableSort(', 'TArray<bool> Dup; Dup.Init(false, Rows.Num());', $dupAppend, 'return Rows;'))) { $f += '(RC15) duplicate names get the class suffix after the sort, before the rows are returned' }
    return ,$f
}

$fails = Invoke-Checks $text
foreach ($x in $fails) { Write-Host "FAIL$x" }

$mutants = @(
    @{ Name = 'M1 queue call removed';                 Key = 'sub';   Expect = 'RC2';  Find = $queueCall; Repl = '' }
    @{ Name = 'M2 queue call per pass';                Key = 'sub';   Expect = 'RC2';  Find = $censusCall; Repl = ('    RollChatQueue' + 'Breakdown(bIsReroll, 0);' + "`n" + $censusCall) }
    @{ Name = 'M3 queue call in the deal-pass file';   Key = 'place'; Expect = 'RC2';  Find = '    RwgBuildWaterVolumeCache();'; Repl = ('    RwgBuildWaterVolumeCache(); RollChatQueue' + 'Breakdown(true, 0);') }
    @{ Name = 'M4 rows saved';                         Key = 'subh';  Expect = 'RC3';  Find = $rowsDecl; Repl = ('    UPROPERTY(Save' + 'Game)' + "`n" + $rowsDecl) }
    @{ Name = 'M5 first-apply note posts';             Key = 'chat';  Expect = 'RC3';  Find = '    // Reached on the first apply'; Repl = ('    FNodeShuffleModule::PostChat' + 'Notice(nullptr, FString(), TEXT("X"));' + "`n    // Reached on the first apply") }
    @{ Name = 'M6 census stash removed';               Key = 'sub';   Expect = 'RC4';  Find = $stash; Repl = '   ' }
    @{ Name = 'M7 reconcile widened';                  Key = 'chat';  Expect = 'RC4';  Find = $reconcile; Repl = '    const bool bNodesMatch = CensusActive >= 0;' }
    @{ Name = 'M8 sorted by count';                    Key = 'chat';  Expect = 'RC5';  Find = $sortName; Repl = ('const int32 ByName = B.' + 'Nodes - A.' + 'Nodes;') }
    @{ Name = 'M9 direct broadcast';                   Key = 'chat';  Expect = 'RC6';  Find = '        ++RollChatAttempts;'; Repl = ('        ++RollChatAttempts; // ' + 'BroadcastChat' + 'Message') }
    @{ Name = 'M10 legend syntax in summary';          Key = 'chat';  Expect = 'RC8';  Find = 'censusActive %d nodesReconciled'; Repl = ('censusActive' + '=%d nodesReconciled') }
    @{ Name = 'M11 marker dropped from header';        Key = 'mark';  Expect = 'RC1';  Find = ('        TEXT("' + $marker + '"),'); Repl = '' }
    @{ Name = 'M12 tick call removed';                 Key = 'sub';   Expect = 'RC7';  Find = '        TickRollChatEmitter();'; Repl = '       ' }
    @{ Name = 'M13 rerollwipe lint unpins the call';   Key = 'lwipe'; Expect = 'RC10'; Find = ('RollChatQueue' + 'Breakdown(bIsReroll, ActiveCount); // ns-rollchat-1'); Repl = 'X' }
    @{ Name = 'M14 mcpcontract note dropped';          Key = 'lmcp';  Expect = 'RC10'; Find = '# ns-rollchat-1: no longer the newest; pinned present'; Repl = '' } # ns-rocks-1 (2026-11-14): newest moved on
    @{ Name = 'M15 teardown clear removed';            Key = 'sub';   Expect = 'RC7';  Find = $teardown; Repl = '   ' }
    @{ Name = 'M16 several rows per fire';             Key = 'chat';  Expect = 'RC12'; Find = ('    if (!' + $postCall + ')'); Repl = ('    for (int32 k = 0; k < 1; ++k) {}' + "`n    if (!" + $postCall + ')') }
    @{ Name = 'M17 looping timer';                     Key = 'chat';  Expect = 'RC12'; Find = $armOnce; Repl = $armOnce.Replace('false);', ('tr' + 'ue);')) }
    @{ Name = 'M18 no supersede on a new roll';        Key = 'chat';  Expect = 'RC13'; Find = $supersede; Repl = '   ' }
    @{ Name = 'M19 header row not composed';           Key = 'chat';  Expect = 'RC11'; Find = $hdrRow; Repl = 'RollChatLines.Add(FString(TEXT("Count      Resource")));' }
    @{ Name = 'M20 pipe row format back';              Key = 'chat';  Expect = 'RC11'; Find = '    // THE ONE ROW FORMAT'; Repl = ('    // TEXT("%s ' + '| %d | %d")' + "`n    // THE ONE ROW FORMAT") }
    @{ Name = 'M21 M not floored at 3';                Key = 'chat';  Expect = 'RC11'; Find = 'FMath::Max(3, FString::FromInt(NodeTotal).Len())'; Repl = 'FString::FromInt(NodeTotal).Len()' }
    @{ Name = 'M22 label before count';                Key = 'chat';  Expect = 'RC11'; Find = '        FString Row = Count;'; Repl = '        FString Row = Label;' }
    @{ Name = 'M35 header gap changed';                Key = 'chat';  Expect = 'RC11'; Find = '(2 * M - 2)'; Repl = '(2 * M)' }
    @{ Name = 'M36 row gap not doubled per digit';     Key = 'chat';  Expect = 'RC11'; Find = '(8 + 2 * (M - Count.Len()))'; Repl = '(8 + (M - Count.Len()))' }
    @{ Name = 'M37 old fixed-width composer back';     Key = 'chat';  Expect = 'RC11'; Find = '        Row += Label;'; Repl = ('        Row += Label; // RollChat' + 'FixedRow') }
    @{ Name = 'M38 tab in a composed row';             Key = 'chat';  Expect = 'RC11'; Find = '        Row += Label;'; Repl = "        Row += Label; Row.AppendChar(TEXT('`t'));" }
    @{ Name = 'M23 wells column back';                 Key = 'chat';  Expect = 'RC11'; Find = $hdrRow; Repl = ($hdrRow + ' // TEXT("' + 'Wells")') }
    @{ Name = 'M24 BODY not delimited';                Key = 'chat';  Expect = 'RC8';  Find = 'ROLLCHAT-LINE %d/%d in message %d/%d: |%s|'; Repl = 'ROLLCHAT-LINE %d/%d in message %d/%d: %s' }
    @{ Name = 'M26 refusal budget removed';            Key = 'chat';  Expect = 'RC12'; Find = 'if (RollChatAttempts >= RollChatMaxEmitAttempts)'; Repl = 'if (false)' }
    @{ Name = 'M27 running-drain guard removed';       Key = 'chat';  Expect = 'RC12'; Find = $guard; Repl = '   ' }
    @{ Name = 'M28 repeat-character helper back';      Key = 'chat';  Expect = 'RC11'; Find = '        FString Row = Count;'; Repl = ('        FString Row = Count; // FString::' + $noChrN + '1, TEXT(' + "' '" + '))') }
    @{ Name = 'M29 helper queries the timer';          Key = 'chat';  Expect = 'RC14'; Find = $isDraining; Repl = ('return RollChatNext < RollChatMessages.Num() && GetWorld()->GetTimerManager().' + $noTimerQ + 'RollChatDrainTimer);') }
    @{ Name = 'M30 interleave gate bypassed';          Key = 'sub';   Expect = 'RC14'; Find = 'if (!bRollChatDraining)'; Repl = 'if (true)' }
    @{ Name = 'M31 flag not cleared on fire';          Key = 'chat';  Expect = 'RC12'; Find = '    bRollChatTimerArmed = false; // the one-shot has fired'; Repl = '    // the one-shot has fired' }
    @{ Name = 'M32 give-up logs two DONE lines';       Key = 'chat';  Expect = 'RC8';  Find = $giveUp; Repl = ('        { UE_LOG(LogNodeShuffle, Warning, TEXT("ROLLCHAT-' + 'DONE: x")); }' + "`n" + $giveUp) }
    @{ Name = 'M33 duplicate suffix removed';          Key = 'chat';  Expect = 'RC15'; Find = $dupAppend; Repl = '' }
    @{ Name = 'M34 totals member back';                Key = 'subh';  Expect = 'RC4';  Find = '    bool bRollChatTimerArmed = false;'; Repl = ('    bool ' + $totalsMember + ' = true;' + "`n" + '    bool bRollChatTimerArmed = false;') }
    @{ Name = 'M39 ROLLCHAT call drops the flag';      Key = 'chat';  Expect = 'RC6';  Find = ', /*bPassFirstPlayerAsInstigator=*/true)'; Repl = ')' }
    @{ Name = 'M40 controller passed unconditionally'; Key = 'emit';  Expect = 'RC16'; Find = "    if (bPassFirstPlayerAsInstigator && World->GetNetMode() != NM_DedicatedServer)`n    {`n        FConstPlayerControllerIterator"; Repl = "    {`n        FConstPlayerControllerIterator" }
    @{ Name = 'M41 flag ignored, nullptr passed';      Key = 'emit';  Expect = 'RC16'; Find = ($bcast + 'Instigator);'); Repl = ($bcast + 'nullptr);') }
    @{ Name = 'M43 T61 sets the flag'; Key = 'emit'; Expect = 'RC16'; Find = 'TEXT("T61")))'; Repl = 'TEXT("T61"), true))' }
    @{ Name = 'M44 dedicated-server test removed';     Key = 'emit';  Expect = 'RC16'; Find = 'if (bPassFirstPlayerAsInstigator && World->GetNetMode() != NM_DedicatedServer)'; Repl = 'if (bPassFirstPlayerAsInstigator)' }
    @{ Name = 'M45 pointer cleared before broadcast';  Key = 'emit';  Expect = 'RC16'; Find = ("        if (PcIt) { Instigator = PcIt->Get(); }`n    }`n"); Repl = ("        if (PcIt) { Instigator = PcIt->Get(); }`n    }`n    Instigator = nullptr;`n") }
    @{ Name = 'M46 netMode field dropped';             Key = 'emit';  Expect = 'RC16'; Find = ' netMode=%d)"),'; Repl = ')"),' }
    @{ Name = 'M42 flag defaults to true';             Key = 'modh';  Expect = 'RC16'; Find = 'bool bPassFirstPlayerAsInstigator = false);'; Repl = 'bool bPassFirstPlayerAsInstigator = true);' }
    @{ Name = 'M47 one line per message';              Key = 'chat';  Expect = 'RC17'; Find = $perMsg; Repl = '    constexpr int32 RollChatLinesPerMessage = 1;' }
    @{ Name = 'M48 join string changed';               Key = 'chat';  Expect = 'RC17'; Find = 'Message += TEXT("\n");'; Repl = 'Message += TEXT(" ");' }
    @{ Name = 'M49 last partial group dropped';        Key = 'chat';  Expect = 'RC17'; Find = 'First < RollChatLines.Num(); First +='; Repl = 'First + RollChatLinesPerMessage <= RollChatLines.Num(); First +=' }
    @{ Name = 'M50 first group skips the opening line'; Key = 'chat'; Expect = 'RC17'; Find = 'for (int32 First = 0; First <'; Repl = 'for (int32 First = 1; First <' }
    @{ Name = 'M51 drain posts a single line';         Key = 'chat';  Expect = 'RC6';  Find = 'PostChatNotice(World, RollChatMessages[RollChatNext]'; Repl = 'PostChatNotice(World, RollChatLines[RollChatNext]' }
    @{ Name = 'M52 per-line log removed';              Key = 'chat';  Expect = 'RC17'; Find = $lineLog; Repl = '' }
    @{ Name = 'M53 lines member saved';                Key = 'subh';  Expect = 'RC3';  Find = $linesDecl; Repl = ('    UPROPERTY(Save' + 'Game)' + "`n" + $linesDecl) }
    @{ Name = 'M25 index advances before acceptance';  Key = 'chat';  Expect = 'RC12'; Find = ('    if (!' + $postCall + ')'); Repl = ('    ++RollChatNext;' + "`n    if (!" + $postCall + ')') }
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

if ($fails.Count -eq 0) { Write-Host "PASS check_rollchat_lint: RC1-RC17 hold; $caught/$($mutants.Count) mutants caught"; exit 0 }
Write-Host "check_rollchat_lint: $($fails.Count) failure(s); $caught/$($mutants.Count) mutants caught"
exit 1
