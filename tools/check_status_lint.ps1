# STATUS lint (2026-11-15, packet ns-status-1; handoff _team/test-logs/2026-11-15-nodeshuffle-status-1-handoff.md).
# The first-shuffle status on Random World Generator worlds: chat lines, NSSTATUS log line, host panel, test command. DISPLAY ONLY.
# LOADS every file the packet touched for the status and pins each rule on context unique to its site. Every needle naming a forbidden
# literal is BUILT BY CONCATENATION so this file can neither satisfy nor trip its own checks. Mutants run in-file.
#   T1  ONCE-ONLY CHAT: the done/posted flags start false and are never reset to false anywhere; S0 is dropped once S1 was entered;
#       a line is marked done only on a posted, abandoned or dropped result.
#   T2  S5 ONE-SHOT LATCH: set only on the first Left == 0 seen after the first roll; never reset; the copy call precedes the Left gate.
#   T3  FIRST-ROLL SESSION FLAG: set at one site (StatusNoteFirstRoll, RWG only) called right after the first RollLayout.
#   T4  IsStartReady for display: the status accessor never runs the getter bind and its one caller is NodeShuffleStatus.cpp.
#   T5  NO RWG SYMBOL in the status files (no RWG class name, no raw export resolve).
#   T6  READ-ONLY toward the shuffle: the status files assign no readiness, roll or layout member.
#   T7  StatusNoteGate sits on the line before the readiness return gate; RefreshTick calls StatusTick in both places; files <= 500 lines.
#   T8  NSSTATUS format: tag first, no field twice, legend prose only; the two chat lines verbatim.
#   T9  Marker 2026-11-15-status-1 is the newest header entry, once in NodeShuffle.cpp, absent from the status files.
#   T10 PANEL: NativePaint draws nothing while the owning controller shows the cursor (early return, never SetVisibility); the widget is a
#       UPROPERTY; EndPlay removes it; it is created only for a LOCAL player controller; Build.cs carries UMG/Slate/SlateCore privately.
#   T11 TEST COMMAND: display only -- no chat call, no stage/latch/chat/layout write; assigns only the test flag; '(test)' title; never
#       reads the map mode; registered like NodeShuffle.DumpLayout.
#   T12 SETTING: ShowStatusPanel default true in the mirror and the panel; read only by the panel file; tick period constant matches.
#   T13 (cold review) the stage function shows nothing off RWG, past the cap, on a saved layout or after the latch; the first-roll flag
#       waits for a roll that produced a layout.
# Exit 0 = all hold; 1 = a rule or mutant fails; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$files = [ordered]@{
    st    = 'Source\NodeShuffle\Private\NodeShuffleStatus.cpp'
    pn    = 'Source\NodeShuffle\Private\NodeShuffleStatusPanel.cpp'
    wg    = 'Source\NodeShuffle\Private\NodeShuffleStatusWidget.cpp'
    wgh   = 'Source\NodeShuffle\Private\NodeShuffleStatusWidget.h'
    bcs   = 'Source\NodeShuffle\NodeShuffle.Build.cs'
    cfg   = 'Source\NodeShuffle\Private\NodeShuffleConfig.cpp'
    cfgh  = 'Source\NodeShuffle\Public\NodeShuffleConfig.h'
    sth   = 'Source\NodeShuffle\Private\NodeShuffleStatus.h'
    sub   = 'Source\NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    subh  = 'Source\NodeShuffle\Public\NodeShuffleSubsystem.h'
    mmc   = 'Source\NodeShuffle\Private\NodeShuffleMapMode.cpp'
    mmh   = 'Source\NodeShuffle\Public\NodeShuffleMapMode.h'
    hapi  = 'Source\NodeShuffle\Private\NodeShuffleMapModeHeightApi.cpp'
    rf    = 'Source\NodeShuffle\Private\NodeShuffleRollFirst.cpp'
    mk    = 'Source\NodeShuffle\Private\NodeShuffleBuildMarkers.h'
    nsmod = 'Source\NodeShuffle\Private\NodeShuffle.cpp'
    doc   = 'docs\DIAGNOSTICS.md'
}
$T = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $root $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $T[$k] = (Get-Content -Raw -Path $p) -replace "`r`n", "`n"
}
$all = @{}
foreach ($f in Get-ChildItem -Path (Join-Path $root 'Source\NodeShuffle') -Recurse -Include *.cpp, *.h) { $all[$f.FullName] = (Get-Content -Raw -Path $f.FullName) -replace "`r`n", "`n" }
function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }
function Ordered([string]$t, [string[]]$needles) { $at = -1; foreach ($n in $needles) { $i = $t.IndexOf($n, $at + 1); if ($i -lt 0) { return $false }; $at = $i }; return $true }
function Span([string]$t, [string]$a, [string]$b) { $i = $t.IndexOf($a); if ($i -lt 0) { return '' }; $j = $t.IndexOf($b, $i + $a.Length); if ($j -lt 0) { return '' }; return $t.Substring($i, $j - $i) }
$marker = '2026-11-15-' + 'status-1'
$statusKeys = @('st', 'sth', 'pn', 'wg', 'wgh')

function Invoke-StatusChecks($T, $all) {
    $f = @()
    $chat = Span $T.st 'void ANodeShuffleSubsystem::StatusChatStep(UWorld* World)' "`n}`n"
    # T1
    if ((Count $T.subh '    bool bStsChatS0Done = false, bStsChatS1Done = false;') -ne 1 -or (Count $T.subh '    bool bStsChatS0Posted = false, bStsChatS1Posted = false;') -ne 1) { $f += '(T1) the once-only chat flags must start false' }
    foreach ($k in $all.Keys) { if ((Split-Path -Leaf $k) -eq 'NodeShuffleSubsystem.h') { continue }; foreach ($n in @(('bStsChatS0Done = ' + 'false'), ('bStsChatS1Done = ' + 'false'), ('bStsChatS0Wanted = ' + 'false'), ('bStsChatS1Wanted = ' + 'false'))) { if ($all[$k].Contains($n) -and -not $all[$k].Contains('    bool ' + $n)) { $f += "(T1) a once-only chat flag is reset in $k" } } }
    if ($chat -eq '' -or (Count $chat 'if (bStsChatS1Wanted && bStsChatS0Wanted && !bStsChatS0Done) { bStsChatS0Done = true;') -ne 1) { $f += '(T1) S0 must be dropped once S1 was entered' }
    if ((Count $chat 'if (bPosted || StsChatAttempts >= ChatMaxAttempts)') -ne 1 -or (Count $chat '{ bStsChatS1Done = true; bStsChatS1Posted = bPosted; } else { bStsChatS0Done = true; bStsChatS0Posted = bPosted; }') -ne 1) { $f += '(T1) a line is done only on posted or abandoned, and posted records the call result' }
    if (-not (Ordered $chat @('if (!PC || !Pawn)', 'if (StsChatHold < 0) { StsChatHold = 0;', 'FNodeShuffleModule::PostChatNotice(World,'))) { $f += '(T1) chat waits for a pawn and one more tick before posting' }
    # T2
    $rfn = Span $T.st 'void ANodeShuffleSubsystem::StatusNoteRollFirst(' "`n}`n"
    if ($rfn -eq '' -or (Count $rfn 'if (!bStsFirstRollThisSession || bStsDoneLatched) { return; }') -ne 1 -or (Count $rfn 'if (Left == 0) { bStsDoneLatched = true; StsDoneRealT = FPlatformTime::Seconds(); }') -ne 1) { $f += '(T2) the S5 latch is set once, on the first Left == 0 after the first roll' }
    foreach ($k in $all.Keys) { if ((Split-Path -Leaf $k) -eq 'NodeShuffleSubsystem.h') { continue }; if ($all[$k].Contains('bStsDoneLatched = ' + 'false') -and -not $all[$k].Contains('    bool bStsDoneLatched = ' + 'false;')) { $f += "(T2) the S5 latch is reset in $k" } }
    if ((Count $T.subh '    bool bStsDoneLatched = false;') -ne 1) { $f += '(T2) the S5 latch starts false' }
    $emit = Span $T.rf 'void ANodeShuffleSubsystem::RwgRollFirstEmitPass(' 'void ANodeShuffleSubsystem::EmitScanCover('
    if (-not (Ordered $emit @('int32 AtK = 0;', '    StatusNoteRollFirst(AtK, Left, RwgRollFirstResources);', '    if (Left > 0)', '    bRwgRollFirstDoneLogged = true;'))) { $f += '(T2) the roll-first copy runs after AtK and before the Left gate' }
    # T3
    if ((Count $T.sub ("RollLayout(Seed, false);`n        StatusNoteFirstRoll();")) -ne 1 -or (Count $T.sub 'StatusNoteFirstRoll();') -ne 1) { $f += '(T3) the first-roll flag call sits right after the first RollLayout, once' }
    $set = 0; foreach ($k in $all.Keys) { $set += Count $all[$k] ('bStsFirstRollThisSession = ' + 'true;') }
    if ($set -ne 1 -or (Count $T.st 'if (bStsFirstRollThisSession || GetMapMode() != ENodeShuffleMapMode::Rwg) { return; }') -ne 1) { $f += '(T3) the first-roll session flag is set at one site, RWG only' }
    # T4
    $acc = Span $T.hapi 'int32 NodeShuffleMapMode::RwgStartReadyForStatus(UWorld* World)' "`n}"
    if ($acc -eq '' -or $acc.Contains('TryBind' + 'ReadyGetters(') -or (Count $acc 'if (!R.bTried || !R.Fn[2]) { return -1; }') -ne 1) { $f += '(T4) the status accessor must not run the getter bind' }
    foreach ($k in $all.Keys) {
        $c = Count $all[$k] ('RwgStartReady' + 'ForStatus(')
        $leaf = Split-Path -Leaf $k
        $ok = @{ 'NodeShuffleStatus.cpp' = 1; 'NodeShuffleMapMode.h' = 1; 'NodeShuffleMapModeHeightApi.cpp' = 1 }
        if ($c -gt 0 -and (-not $ok.ContainsKey($leaf) -or $ok[$leaf] -ne $c)) { $f += "(T4) the IsStartReady status accessor is referenced from $leaf ($c)" }
    }
    # T5
    foreach ($k in $statusKeys) { foreach ($n in @(('APW' + 'WorldGenerator'), ('Procedural' + 'World'), ('GetProc' + 'Address'), ('FPW' + 'Progress'))) { if ($T[$k].Contains($n)) { $f += "(T5) RWG symbol '$n' in $k" } } }
    # T6
    foreach ($k in $statusKeys) {
        foreach ($n in @('bRwgReady', 'RwgStableSinceReady', 'bRwgStageReadySeen', 'bLayoutGenerated', 'bRwgUnreadableWarned', 'RwgPrevNodeCount', 'bRwgRollFirstDoneLogged', 'RwgRollFirstGeneration')) {
            if ([regex]::IsMatch($T[$k], '\b' + $n + '\s*(=[^=]|\+\+|--|\+=|-=)')) { $f += "(T6) $k assigns $n" }
        }
        if ([regex]::IsMatch($T[$k], '\bLayout\s*(\.|\[[^\]]*\]\.)\s*(Add|Remove|Reset|Empty|Insert|SetNum|Emplace)\w*\(') -or [regex]::IsMatch($T[$k], '\bE\.\w+\s*=[^=]')) { $f += "(T6) $k mutates Layout" }
    }
    # T7
    if ((Count $T.mmc ("        StatusNoteGate(bStageReadable, bStageReady, bFloor, GatedBy, Stage);")) -ne 1 -or -not ($T.mmc -match 'StatusNoteGate\(bStageReadable, bStageReady, bFloor, GatedBy, Stage\);[^\n]*\n        if \(!bReadyNow\)')) { $f += '(T7) StatusNoteGate must be the line right before the readiness return gate' }
    if ((Count $T.sub 'StatusTick(/*bEnabled=*/false);') -ne 1 -or (Count $T.sub "RwgObserveTick();`n        StatusTick(/*bEnabled=*/true);") -ne 1) { $f += '(T7) RefreshTick calls StatusTick in the disabled branch and right after RwgObserveTick' }
    foreach ($k in @('st', 'sth', 'mmc', 'rf', 'pn', 'wg', 'wgh')) { $n = ($T[$k] -split "`n").Count; if ($n -gt 501) { $f += "(T7) $k is over 500 lines ($n)" } }
    # T8
    $fmt = [regex]::Match($T.st, 'TEXT\("(NSSTATUS: [^"]*)"\)')
    if (-not $fmt.Success) { $f += '(T8) NSSTATUS format not found' } else {
        $names = @([regex]::Matches($fmt.Groups[1].Value, '(\w+)=') | ForEach-Object { $_.Groups[1].Value })
        if ($names.Count -ne @($names | Select-Object -Unique).Count) { $f += '(T8) a field repeats in NSSTATUS' }
        foreach ($need in @('stage', 'stageReadable', 'stageReady', 'startReady', 'stableSinceReady', 'gatedBy', 'floorMet', 'chatS0Posted', 'chatS1Posted', 'netMode', 'localPC', 'widgetAdded')) { if ($names -notcontains $need) { $f += "(T8) NSSTATUS lacks $need" } }
    }
    $leg = Span $T.st '// Legend (prose only' 'UE_LOG(LogNodeShuffle, Display, TEXT("NSSTATUS: '
    if ($leg -eq '' -or [regex]::IsMatch($leg, '\w=\S')) { $f += '(T8) the NSSTATUS legend must be prose (no field=value)' }
    if ((Count $T.st ('TEXT("Node Shuffle: waiting for the world generator to report it is ready. Your nodes are shuffled after ' + 'that.")')) -ne 1 -or (Count $T.st ('TEXT("Node Shuffle: world ready. Waiting until no new nodes appear for a minute, then shuffling. The game may pause briefly when it ' + 'shuffles.")')) -ne 1) { $f += '(T8) the two chat lines must be the briefed text, verbatim' }
    $chatLits = @([regex]::Matches($T.st, 'TEXT\("Node Shuffle: [^"]*"\)') | ForEach-Object { $_.Value })
    if ($chatLits.Count -ne 2 -or @($chatLits | Where-Object { $_.Contains('few ' + 'seconds') }).Count -ne 0) { $f += '(T8) exactly two chat lines, and none for Soon' }
    # T9
    $arr = [regex]::Match($T.mk, 'PacketMarkers\[\] = \{([\s\S]*?)\};')
    $tags = @(if ($arr.Success) { [regex]::Matches($arr.Groups[1].Value, 'TEXT\("([\w-]+)"\)') | ForEach-Object { $_.Groups[1].Value } })
    if ($tags.Count -eq 0 -or $tags[-1] -ne $marker -or @($tags | Where-Object { $_ -eq $marker }).Count -ne 1 -or (Count $T.nsmod ('packet marker (' + $marker + ')=====')) -ne 1) { $f += '(T9) marker status-1 must be the newest header entry, once, and once in NodeShuffle.cpp' }
    foreach ($k in $statusKeys) { if ($T[$k].Contains($marker)) { $f += "(T9) marker literal in $k" } }
    # T10
    $paint = Span $T.wg 'int32 UNodeShuffleStatusWidget::NativePaint(' "`n}"
    if (-not (Ordered $paint @('const APlayerController* PC = GetOwningPlayer();', 'if (PC && PC->bShowMouseCursor) { return Layer; }', 'FSlateDrawElement::MakeLines(', 'FSlateDrawElement::MakeText('))) { $f += '(T10) NativePaint must return early while the cursor shows, before drawing' }
    foreach ($k in $statusKeys) { if ($T[$k].Contains('SetVisibility' + '(')) { $f += "(T10) $k hides by visibility" } }
    if ((Count $T.subh "    UPROPERTY(Transient)`n    TObjectPtr<class UNodeShuffleStatusWidget> StatusWidget = nullptr;") -ne 1) { $f += '(T10) the widget must be held by a transient UPROPERTY' }
    if (-not (Ordered (Span $T.sub 'void ANodeShuffleSubsystem::EndPlay(' 'Super::EndPlay(EndPlayReason);') @('StatusRemovePanel(TEXT("endPlay"));'))) { $f += '(T10) EndPlay must remove the panel before Super::EndPlay' }
    $rm = Span $T.pn 'void ANodeShuffleSubsystem::StatusRemovePanel(' "`n}"
    if (-not (Ordered $rm @('StatusWidget->RemoveFromParent();', 'StatusWidget = nullptr;'))) { $f += '(T10) removal calls RemoveFromParent and clears the pointer' }
    $en = Span $T.pn 'void ANodeShuffleSubsystem::StatusEnsurePanel()' "`n}"
    if (-not (Ordered $en @('if (Pc && Pc->IsLocalController()) { LocalPc = Pc; break; }', 'if (!LocalPc)', 'CreateWidget<UNodeShuffleStatusWidget>(LocalPc, UNodeShuffleStatusWidget::StaticClass());', 'StatusWidget->AddToViewport('))) { $f += '(T10) the widget is created only for a local player controller' }
    $priv = Span $T.bcs 'PrivateDependencyModuleNames.AddRange(' '});'
    if ((Count $priv '"UMG", "Slate", "SlateCore",') -ne 1 -or (Count (Span $T.bcs 'PublicDependencyModuleNames.AddRange(' '});') 'UMG') -ne 0) { $f += '(T10) UMG/Slate/SlateCore must be private dependencies' }
    # T11
    $tst = Span $T.pn 'void ANodeShuffleSubsystem::StatusRunPanelTest(' "`n}"
    if ($tst -eq '' -or $tst.Contains('PostChat' + 'Notice') -or $tst.Contains('GetMap' + 'Mode') -or (Count $tst 'StatusWidget->Title = TEXT("Node Shuffle (test)");') -ne 1) { $f += '(T11) the test command posts no chat, never reads the map mode and titles the panel (test)' }
    $assign = @([regex]::Matches(($tst -replace '//[^\n]*', ''), '(?m)^\s*([A-Za-z_][\w>.-]*)\s*(=[^=]|\+\+|\+=)') | ForEach-Object { $_.Groups[1].Value })
    foreach ($a in $assign) { if (@('bStsTestActive', 'StatusWidget->Title', 'StatusWidget->Body', 'StatusWidget->Detail', 'EStage S', 'bool bOff', 'FString Body, Detail') -notcontains $a -and $a -notmatch '^(EStage|bool|FString)$') { $f += "(T11) the test command assigns $a" } }
    if (-not $T.pn.Contains('static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleStatusPanelTestCmd(') -or -not $T.pn.Contains('for (TActorIterator<ANodeShuffleSubsystem> It(World); It; ++It)')) { $f += '(T11) registered like NodeShuffle.DumpLayout' }
    # T12
    if ((Count $T.cfgh '    bool ShowStatusPanel{true};') -ne 1 -or (Count $T.cfg 'AddBool(TEXT("ShowStatusPanel"), true,') -ne 1) { $f += '(T12) ShowStatusPanel defaults ON in the mirror and the panel' }
    foreach ($k in $all.Keys) { $leaf = Split-Path -Leaf $k; if (@('NodeShuffleStatusPanel.cpp', 'NodeShuffleConfig.h') -notcontains $leaf -and $all[$k].Contains('.ShowStatus' + 'Panel')) { $f += "(T12) the panel setting is read in $leaf" } }
    if ((Count $T.sub 'constexpr float TickIntervalSeconds = 5.0f;') -ne 1 -or (Count $T.sth 'constexpr int32 StatusTickSeconds = 5;') -ne 1) { $f += '(T12) the status tick constant must match RefreshTick (5 s)' }
    # T13 (cold review): the stage function shows nothing off RWG, past the cap, on a saved layout or after the readiness latch
    $cs = Span $T.st 'uint8 ANodeShuffleSubsystem::StatusComputeStage(bool bEnabled)' "`n}`n"
    if ($cs -eq '' -or (Count $cs 'if (!bEnabled || bStsDisplayEnded || GetMapMode() != ENodeShuffleMapMode::Rwg) { return static_cast<uint8>(EStage::None); }') -ne 1 -or (Count $cs 'if (bLayoutGenerated || !bStsGateNoted || bRwgReady) { return static_cast<uint8>(EStage::None); }') -ne 1 -or (Count $cs 'if (Now - StsFirstRollRealT >= DisplayCapSeconds) { bStsDisplayEnded = true; bStsCapReached = true;') -ne 1) { $f += '(T13) the stage function shows nothing off RWG, past the cap, on a saved layout or after the latch' }
    $fr = Span $T.st 'void ANodeShuffleSubsystem::StatusNoteFirstRoll()' "`n}`n"
    if (-not (Ordered $fr @('if (bStsFirstRollThisSession || GetMapMode() != ENodeShuffleMapMode::Rwg) { return; }', '    if (!bLayoutGenerated) { return; }', ('bStsFirstRollThisSession = ' + 'true;')))) { $f += '(T13) the first-roll flag waits for a roll that produced a layout' }
    return $f
}

$fails = @(Invoke-StatusChecks $T $all)
$N = "`n"
$mutants = @(
    @{ Name = 'U1 S0 chat flag reset on regress'; Key = 'st'; Expect = 'T1'; Find = '    if (Next == EStage::S0) { bStsChatS0Wanted = true; }'; Repl = ('    if (Next == EStage::S0) { bStsChatS0Wanted = true; } else { bStsChatS0Done = ' + 'false; }') }
    @{ Name = 'U2 S0 posts after S1'; Key = 'st'; Expect = 'T1'; Find = 'if (bStsChatS1Wanted && bStsChatS0Wanted && !bStsChatS0Done) { bStsChatS0Done = true;'; Repl = 'if (false) {' }
    @{ Name = 'U3 done set without a result'; Key = 'st'; Expect = 'T1'; Find = 'if (bPosted || StsChatAttempts >= ChatMaxAttempts)'; Repl = 'if (true)' }
    @{ Name = 'U4 no hold tick'; Key = 'st'; Expect = 'T1'; Find = 'if (StsChatHold < 0) { StsChatHold = 0;'; Repl = 'if (false) { StsChatHold = 0;' }
    @{ Name = 'U5 S5 latch follows Left back'; Key = 'st'; Expect = 'T2'; Find = 'if (Left == 0) { bStsDoneLatched = true; StsDoneRealT = FPlatformTime::Seconds(); }'; Repl = ('bStsDoneLatched = (Left == 0); if (Left > 0) { bStsDoneLatched = ' + 'false; }') }
    @{ Name = 'U6 copy after the Left gate'; Key = 'rf'; Expect = 'T2'; Find = ('    StatusNoteRollFirst(AtK, Left, RwgRollFirstResources);'); Repl = '' }
    @{ Name = 'U7 first-roll flag call dropped'; Key = 'sub'; Expect = 'T3'; Find = "RollLayout(Seed, false);`n        StatusNoteFirstRoll();"; Repl = 'RollLayout(Seed, false);' }
    @{ Name = 'U8 first-roll flag on every map'; Key = 'st'; Expect = 'T3'; Find = 'if (bStsFirstRollThisSession || GetMapMode() != ENodeShuffleMapMode::Rwg) { return; }'; Repl = 'if (bStsFirstRollThisSession) { return; }' }
    @{ Name = 'U9 accessor runs the bind'; Key = 'hapi'; Expect = 'T4'; Find = 'if (!R.bTried || !R.Fn[2]) { return -1; }'; Repl = ('if (!R.bTried) { TryBind' + 'ReadyGetters(R); } if (!R.Fn[2]) { return -1; }') }
    @{ Name = 'U10 accessor read by the readiness code'; Key = 'mmc'; Expect = 'T4'; Find = '        if (!bReadyNow)'; Repl = ('        if (NodeShuffleMapMode::RwgStartReady' + 'ForStatus(World) == 1) { } if (!bReadyNow)') }
    @{ Name = 'U11 RWG class named in status'; Key = 'st'; Expect = 'T5'; Find = '#include "Engine/World.h"'; Repl = ('#include "Engine/World.h"' + $N + 'class ' + 'APW' + 'WorldGenerator;') }
    @{ Name = 'U12 status writes the latch'; Key = 'st'; Expect = 'T6'; Find = '    bStsGateNoted = true;'; Repl = ('    bStsGateNoted = true; bRwg' + 'Ready = true;') }
    @{ Name = 'U13 status call moved after the gate'; Key = 'mmc'; Expect = 'T7'; Find = "        StatusNoteGate(bStageReadable, bStageReady, bFloor, GatedBy, Stage);"; Repl = '        // moved' }
    @{ Name = 'U14 status tick before the observe'; Key = 'sub'; Expect = 'T7'; Find = "RwgObserveTick();`n        StatusTick(/*bEnabled=*/true);"; Repl = "StatusTick(/*bEnabled=*/true);`n        RwgObserveTick();" }
    @{ Name = 'U15 field repeated'; Key = 'st'; Expect = 'T8'; Find = ' netMode=%d localPC=%d'; Repl = ' netMode=%d stage=%d' }
    @{ Name = 'U16 legend shows a token'; Key = 'st'; Expect = 'T8'; Find = '// Legend (prose only, docs/DIAGNOSTICS.md): the stage'; Repl = ('// Legend (prose only, docs/DIAGNOSTICS.md): the stage' + '=S1') }
    @{ Name = 'U17 chat copy drifts'; Key = 'st'; Expect = 'T8'; Find = 'then shuffling. The game may pause briefly'; Repl = 'then shuffling in a few seconds. The game may pause briefly' }
    @{ Name = 'U18 marker not newest'; Key = 'mk'; Expect = 'T9'; Find = ('        TEXT("' + $marker + '"),'); Repl = '' }
    @{ Name = 'U19 no cursor early return'; Key = 'wg'; Expect = 'T10'; Find = 'if (PC && PC->bShowMouseCursor) { return Layer; }'; Repl = '' }
    @{ Name = 'U20 hidden by visibility'; Key = 'pn'; Expect = 'T10'; Find = '    StatusWidget->RemoveFromParent();'; Repl = ('    StatusWidget->SetVisibility' + '(ESlateVisibility::Collapsed);') }
    @{ Name = 'U21 EndPlay keeps the panel'; Key = 'sub'; Expect = 'T10'; Find = '    StatusRemovePanel(TEXT("endPlay"));'; Repl = '' }
    @{ Name = 'U22 widget not a UPROPERTY'; Key = 'subh'; Expect = 'T10'; Find = "    UPROPERTY(Transient)`n    TObjectPtr<class UNodeShuffleStatusWidget> StatusWidget"; Repl = '    TObjectPtr<class UNodeShuffleStatusWidget> StatusWidget' }
    @{ Name = 'U23 any controller'; Key = 'pn'; Expect = 'T10'; Find = 'if (Pc && Pc->IsLocalController()) { LocalPc = Pc; break; }'; Repl = 'if (Pc) { LocalPc = Pc; break; }' }
    @{ Name = 'U24 test posts chat'; Key = 'pn'; Expect = 'T11'; Find = '    bStsTestActive = true;'; Repl = ('    bStsTestActive = true; FNodeShuffleModule::PostChat' + 'Notice(GetWorld(), Body, TEXT("T"));') }
    @{ Name = 'U25 test writes a chat flag'; Key = 'pn'; Expect = 'T11'; Find = '    bStsTestActive = true;'; Repl = ("    bStsTestActive = true;`n    bStsChatS1Done = true;") }
    @{ Name = 'U26 test title not marked'; Key = 'pn'; Expect = 'T11'; Find = 'StatusWidget->Title = TEXT("Node Shuffle (test)");'; Repl = 'StatusWidget->Title = TEXT("Node Shuffle");' }
    @{ Name = 'U27 setting default off'; Key = 'cfg'; Expect = 'T12'; Find = 'AddBool(TEXT("ShowStatusPanel"), true,'; Repl = 'AddBool(TEXT("ShowStatusPanel"), false,' }
    @{ Name = 'U29 saved layout shows the status'; Key = 'st'; Expect = 'T13'; Find = 'if (bLayoutGenerated || !bStsGateNoted || bRwgReady)'; Repl = 'if (!bStsGateNoted || bRwgReady)' }
    @{ Name = 'U30 status on every map'; Key = 'st'; Expect = 'T13'; Find = 'if (!bEnabled || bStsDisplayEnded || GetMapMode() != ENodeShuffleMapMode::Rwg)'; Repl = 'if (!bEnabled || bStsDisplayEnded)' }
    @{ Name = 'U31 no display cap'; Key = 'st'; Expect = 'T13'; Find = 'if (Now - StsFirstRollRealT >= DisplayCapSeconds)'; Repl = 'if (false)' }
    @{ Name = 'U32 aborted roll sets the flag'; Key = 'st'; Expect = 'T13'; Find = '    if (!bLayoutGenerated) { return; }'; Repl = '' }
    @{ Name = 'U28 UMG made public'; Key = 'bcs'; Expect = 'T10'; Find = '"FactoryGame", "SML",'; Repl = '"FactoryGame", "SML", "UMG",' }
)
$mutantFails = @()
$caught = 0
foreach ($m in $mutants) {
    $mut = @{}
    foreach ($k in $T.Keys) { $mut[$k] = $T[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $mall = @{}
    foreach ($k in $all.Keys) { $mall[$k] = $all[$k] }
    $full = (Resolve-Path (Join-Path $root $files[$m.Key])).Path
    if ($mall.ContainsKey($full)) { $mall[$full] = $mut[$m.Key] }
    $res = @(Invoke-StatusChecks $mut $mall)
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ }
    else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$out = @($fails) + @($mutantFails)
if ($out.Count -gt 0) {
    foreach ($x in $out) { Write-Host "FAIL: $x" }
    Write-Host "STATUS LINT: FAIL -- $($out.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: STATUS lint -- T1..T13 hold across $($T.Count) pinned files + $($all.Count) scanned sources; mutants caught $caught/$($mutants.Count)."
