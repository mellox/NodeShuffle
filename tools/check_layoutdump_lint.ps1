# rwg-layout-dump-1 lint (2026-10-29): NodeShuffle.DumpLayout (LAYOUTDUMP-*) + tools/layout_spacing.py + its fixture.
# THE TWO-OWNER TRAP this lint exists for: the C++ format strings and the Python parser drift silently to zero parses. So the
# field ORDER of every line class is pinned across THREE surfaces at once -- the C++ TEXT("LAYOUTDUMP-<C>: ...") format, the .py
# FIELDS tuple and the fixture's first line of that class -- and the .py is RUN against temp copies of the fixture.
# Pins (all structural; every forbidden needle is BUILT BY CONCATENATION so this file cannot match itself):
#   L1  END control: the END format carries countsMatch, and bMatch compares all four printed counts against their sources.
#   L2  No trace in the dump file: no ground-raycast, settle, line-trace or sweep call (P3: the dump must not teach the water grid).
#   L3  The completion chat line: exactly one PostChatNotice call, tagged LAYOUTDUMP.
#   L4  Field order per class: C++ format == .py FIELDS == fixture line; every field has exactly one printf spec; UE_LOG arity == specs.
#   L5  The .py refuses an incomplete block: fixture minus END -> nonzero; fixture + a truncated second block -> nonzero;
#       intact fixture -> zero (positive control) and --selftest -> zero.
#   L6  Legend lines carry no name=value token and no build-marker string.
#   L7  Dated TODO at the registration and at the header declaration.
#   L8  Registration lives in the dump file only (NodeShuffle.cpp untouched); the RWG early return precedes the first Layout read.
#   L9  Dealable fraction comes from the shared helper (RwgDensityLandSample); the slope test is never restated in the dump file.
#   L10 (review F2) The grid is gated on all three grader Done flags: the AND expression, bGrid built from it, the grid loop and
#       Cols keyed on bGrid (never on bApi alone), BEGIN prints gradesDone, and both latch masks carry the Done bits 8/16/32.
#   L11 Fixture mirrors source CHARACTER FOR CHARACTER: every fixture line of a class matches its C++ format with each printf spec
#       read as one non-space token, and the fixture's LEGEND lines equal the C++ legend literals in order.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$files = [ordered]@{
    dump  = 'Source\NodeShuffle\Private\NodeShuffleLayoutDump.cpp'
    subh  = 'Source\NodeShuffle\Public\NodeShuffleSubsystem.h'
    nsmod = 'Source\NodeShuffle\Private\NodeShuffle.cpp'
    py    = 'tools\layout_spacing.py'
    fx    = 'tools\fixtures\layoutdump_fixture.log'
    diffpy = 'tools\layout_diff.py'
    diffx = 'tools\fixtures\layoutdiff_fixture.log'
}
$classes = @('BEGIN', 'E', 'W', 'O', 'G', 'END')
function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }
function Fmt-Fields([string]$fmt) { return @([regex]::Matches($fmt, '(?<![A-Za-z0-9])([A-Za-z][A-Za-z0-9]*)=') | ForEach-Object { $_.Groups[1].Value }) }
function Specs([string]$fmt) { return ([regex]::Matches($fmt, '%[-+ #0]*\d*(?:\.\d+)?[dsfiu]')).Count }
function Read-Text([string]$p) { return ((Get-Content -Raw -Encoding utf8 -Path $p) -replace "`r`n", "`n") }
function Log-Args([string]$t, [string]$tag) {
    # Depth-aware top-level comma count of the UE_LOG call whose format starts with the tag (same helper as check_rwgtrace_lint.ps1).
    $k = $t.IndexOf('TEXT("' + $tag); if ($k -lt 0) { return -1 }
    $u = $t.LastIndexOf('UE_LOG(', $k); if ($u -lt 0) { return -1 }
    $depth = 0; $commas = 0; $i = $u + 7; $inS = $false; $inC = $false
    while ($i -lt $t.Length) {
        $c = $t[$i]
        if ($inS) { if ($c -eq '\') { $i++ } elseif ($c -eq '"') { $inS = $false } }
        elseif ($inC) { if ($c -eq '\') { $i++ } elseif ($c -eq "'") { $inC = $false } }
        elseif ($c -eq '"') { $inS = $true } elseif ($c -eq "'") { $inC = $true }
        elseif ('([{'.Contains($c)) { $depth++ }
        elseif (')]}'.Contains($c)) { if ($depth -eq 0) { return $commas + 1 - 3 }; $depth-- }
        elseif ($c -eq ',' -and $depth -eq 0) { $commas++ }
        $i++
    }
    return -1
}
function Run-Py([string]$pyText, [string]$fxText, [string[]]$pyArgs) {
    # Writes the (possibly mutated) script and log to a fresh temp dir and returns the exit code. -I: isolated mode.
    $d = Join-Path ([IO.Path]::GetTempPath()) ('layoutdump-lint-' + [guid]::NewGuid().ToString('N'))
    [void](New-Item -ItemType Directory -Path $d)
    [void](New-Item -ItemType Directory -Path (Join-Path $d 'fixtures'))
    try {
        [IO.File]::WriteAllText((Join-Path $d 'layout_spacing.py'), $pyText)
        [IO.File]::WriteAllText((Join-Path $d 'fixtures\layoutdump_fixture.log'), $fxText)
        $a = @('-I', (Join-Path $d 'layout_spacing.py')) + ($pyArgs | ForEach-Object { $_ -replace '\{D\}', $d })
        & python @a *> $null
        return $LASTEXITCODE
    } finally { [IO.Directory]::Delete($d, $true) }
}

$text = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $root $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = Read-Text $p
}

function Invoke-LayoutDumpChecks($T, [bool]$RunPy) {
    $f = @()
    $D = $T.dump
    # L1
    if ((Count $D 'TEXT("LAYOUTDUMP-END: ') -ne 1 -or (Count $D ' countsMatch=%d ') -ne 1) { $f += '(L1) exactly one END format carrying countsMatch' }
    $ctl = 'const bool bMatch = EntriesPrinted == Layout.Num() && WellsPrinted == WellLayout.Num() && OriginalsPrinted == OriginalNodeRecord.Num() && GridPrinted == GridExpected;'
    if ((Count $D $ctl) -ne 1 -or (Count $D 'bMatch ? 1 : 0') -ne 1) { $f += '(L1) the END control must compare all four printed counts and print bMatch' }
    # L2
    foreach ($n in @(('Raycast' + 'GroundAt('), ('Raycast' + 'Settle('), ('Line' + 'Trace'), ('Sweep' + 'Single'), ('Sweep' + 'Multi'))) {
        if ((Count $D $n) -ne 0) { $f += "(L2) the dump file must make no trace call ($n)" }
    }
    # L3
    if ((Count $D ('PostChat' + 'Notice(')) -ne 1 -or (Count $D 'FNodeShuffleModule::PostChatNotice(GetWorld(), Body, TEXT("LAYOUTDUMP"));') -ne 1) { $f += '(L3) exactly one PostChatNotice(GetWorld(), Body, TEXT("LAYOUTDUMP")) call' }
    # L4
    foreach ($c in $classes) {
        $m = [regex]::Matches($D, 'TEXT\("LAYOUTDUMP-' + $c + ': ([^"]*)"\)')
        if ($m.Count -ne 1) { $f += "(L4) LAYOUTDUMP-${c}: expected one C++ format literal, found $($m.Count)"; continue }
        $fmt = $m[0].Groups[1].Value
        $cf = Fmt-Fields $fmt
        $sf = [regex]::Matches($fmt, '(?<![A-Za-z0-9])[A-Za-z][A-Za-z0-9]*=%').Count # fields printed through a spec (BEGIN's map=rwg is a literal)
        if ((Specs $fmt) -ne $sf -or ($cf.Count - $sf) -gt 1) { $f += "(L4) LAYOUTDUMP-${c}: $(Specs $fmt) printf specs for $sf spec fields of $($cf.Count)" }
        $na = Log-Args $D ('LAYOUTDUMP-' + $c + ': ')
        if ($na -ne (Specs $fmt)) { $f += "(L4) LAYOUTDUMP-${c}: UE_LOG passes $na args for $(Specs $fmt) specs" }
        $pm = [regex]::Match($T.py, "(?s)'" + $c + "': \(([^)]*)\)")
        $pf = if ($pm.Success) { @([regex]::Matches($pm.Groups[1].Value, "'([A-Za-z][A-Za-z0-9]*)'") | ForEach-Object { $_.Groups[1].Value }) } else { @() }
        $fls = [regex]::Matches($T.fx, '(?m)^\[[^\]]*\]\[\s*\d+\]LogNodeShuffle: Display: LAYOUTDUMP-' + $c + ': ([^\n]*)$')
        if (($cf -join ',') -ne ($pf -join ',')) { $f += "(L4) LAYOUTDUMP-${c}: C++ fields [$($cf -join ',')] != .py FIELDS [$($pf -join ',')]" }
        if ($fls.Count -lt 1) { $f += "(L4) LAYOUTDUMP-${c}: no fixture line" }
        $lit = @([regex]::Split($fmt, '%[-+ #0]*\d*(?:\.\d+)?[dsfiu]') | ForEach-Object { [regex]::Escape($_) })
        $rxFmt = '^' + ($lit -join '\S+') + '$'
        foreach ($fl in $fls) {
            $ff = Fmt-Fields $fl.Groups[1].Value
            if (($cf -join ',') -ne ($ff -join ',')) { $f += "(L4) LAYOUTDUMP-${c}: C++ fields [$($cf -join ',')] != fixture fields [$($ff -join ',')]" }
            if ($fl.Groups[1].Value -notmatch $rxFmt) { $f += "(L11) LAYOUTDUMP-${c}: a fixture line does not match the C++ format character for character" }
        }
    }
    # L5
    if ($RunPy) {
        $noEnd = (($T.fx -split "`n") | Where-Object { $_ -notmatch 'Display: LAYOUTDUMP-END: ' }) -join "`n"
        $trunc = $T.fx + "`n" + $noEnd
        $out = '{D}\out.md'
        if ((Run-Py $T.py $T.fx @('--log', '{D}\fixtures\layoutdump_fixture.log', '--out', $out)) -ne 0) { $f += '(L5) positive control: the intact fixture must analyse with exit 0' }
        if ((Run-Py $T.py $T.fx @('--selftest')) -ne 0) { $f += '(L5) --selftest must exit 0' }
        if ((Run-Py $T.py $noEnd @('--log', '{D}\fixtures\layoutdump_fixture.log', '--out', $out)) -eq 0) { $f += '(L5) a fixture with END deleted was accepted' }
        if ((Run-Py $T.py $trunc @('--log', '{D}\fixtures\layoutdump_fixture.log', '--out', $out)) -eq 0) { $f += '(L5) a truncated LAST block was accepted (fell back to an earlier one)' }
        # L12 (rwg-layout-diff-1): the diff tool and its fixture are RUN in-tree (it imports layout_spacing.py by path)
        & python -I (Join-Path $root 'tools\layout_diff.py') --selftest *> $null
        if ($LASTEXITCODE -ne 0) { $f += '(L12) layout_diff.py --selftest must exit 0' }
    }
    # L6
    $leg = [regex]::Matches($D, 'TEXT\("LAYOUTDUMP-LEGEND: ([^"]*)"\)')
    if ($leg.Count -lt 1) { $f += '(L6) no legend lines' }
    foreach ($l in $leg) {
        if ($l.Groups[1].Value -match '[A-Za-z0-9]=') { $f += '(L6) a legend line carries a name=value token' }
        if ($l.Groups[1].Value.Contains('LOAD' + 'ED (') -or $l.Groups[1].Value -match ('rwg-' + 'layout-dump')) { $f += '(L6) a legend line carries a build-marker string' }
    }
    # L7
    $todo = 'TODO(2026-10-29, rwg-layout-dump-1): diagnostic command -- gate behind EnableDiagnostics or strip before release.'
    if ((Count $D ("// $todo`nstatic FAutoConsoleCommandWithWorldAndArgs GNodeShuffleDumpLayoutCmd(")) -ne 1) { $f += '(L7) dated TODO directly above the registration' }
    if ((Count $T.subh 'TODO(2026-10-29, rwg-layout-dump-1): diagnostic, gate or strip.') -ne 1 -or (Count $T.subh "    void DumpLayout();`n};") -ne 1) { $f += '(L7) header declaration with its dated TODO, last in the class' }
    # L8
    if ((Count $D 'TEXT("NodeShuffle.DumpLayout")') -ne 1 -or (Count $D 'It->DumpLayout();') -ne 1 -or (Count $T.nsmod 'DumpLayout') -ne 0) { $f += '(L8) registered once in the dump file, absent from NodeShuffle.cpp' }
    $iMode = $D.IndexOf('if (Mode != ENodeShuffleMapMode::Rwg)'); $iLay = $D.IndexOf('Layout)', $D.IndexOf('void ANodeShuffleSubsystem::DumpLayout()'))
    if ($iMode -lt 0 -or $iLay -lt 0 -or $iMode -gt $iLay) { $f += '(L8) the RWG early return must precede the first Layout read' }
    # L9
    if ((Count $D 'const bool bDeal = RwgDensityLandSample(Q, X, Y);') -ne 1) { $f += '(L9) dealable must come from RwgDensityLandSample' }
    if ((Count $D ('RwgApiSlope' + 'TooSteep(')) -ne 0) { $f += '(L9) the slope test must not be restated in the dump file' }
    # L10 (needles concatenated so this file cannot satisfy them)
    $gate = 'const bool bGradesDone = bRwgSeaCheckDone' + ' && bRwgVoidCheckDone' + ' && bRwgKillboxDone;'
    if ((Count $D $gate) -ne 1) { $f += '(L10) bGradesDone must AND all three grader Done flags' }
    if ((Count $D ('const bool bGrid = bApi' + ' && bGradesDone;')) -ne 1) { $f += '(L10) bGrid must be bApi AND bGradesDone' }
    if ((Count $D ('if (b' + 'Grid)')) -ne 1 -or (Count $D ('if (b' + 'Api)')) -ne 0) { $f += '(L10) the grid loop must be gated on bGrid, never on bApi alone' }
    if ((Count $D ('const int32 Cols = b' + 'Grid ? ')) -ne 1) { $f += '(L10) Cols (and so gridCells) must be zero unless bGrid' }
    if ((Count $D (' gradesDone=%d"),')) -ne 1 -or (Count $D ('H, bApi ? 1 : 0, b' + 'GradesDone ? 1 : 0);')) -ne 1) { $f += '(L10) BEGIN must print gradesDone from bGradesDone' }
    $done = ' | (bRwgSeaCheckDone ? 8 : 0) | (bRwgVoidCheckDone ? 16 : 0) | (bRwgKillboxDone ? 32 : 0);'
    if ((Count $D ('const int32 LatchMaskBefore = (bRwgSeaUsable ? 1 : 0) | (bRwgVoidUsable ? 2 : 0) | (bRwgKillboxKnown ? 4 : 0)' + $done)) -ne 1 -or
        (Count $D ('const int32 LatchMaskAfter = (bRwgSeaUsable ? 1 : 0) | (bRwgVoidUsable ? 2 : 0) | (bRwgKillboxKnown ? 4 : 0)' + $done)) -ne 1) { $f += '(L10) both latch masks must carry the Done bits 8/16/32' }
    # L11 legend mirror
    $cl = @([regex]::Matches($D, 'TEXT\("LAYOUTDUMP-LEGEND: ([^"]*)"\)') | ForEach-Object { $_.Groups[1].Value })
    $fxl = @([regex]::Matches($T.fx, '(?m)^\[[^\]]*\]\[\s*\d+\]LogNodeShuffle: Display: LAYOUTDUMP-LEGEND: ([^\n]*)$') | ForEach-Object { $_.Groups[1].Value })
    if (($cl -join "`n") -ne ($fxl -join "`n")) { $f += "(L11) fixture LEGEND lines ($($fxl.Count)) differ from the C++ legend literals ($($cl.Count))" }
    return $f
}

$fails = Invoke-LayoutDumpChecks $text $true
$mutants = @(
    @{ Name = 'M1 END control drops the grid term'; Key = 'dump'; Expect = 'L1'; Find = ' && GridPrinted == GridExpected;'; Repl = ';' }
    @{ Name = 'M2 a ground trace added'; Key = 'dump'; Expect = 'L2'; Find = '    const double GridMs ='; Repl = ('    Raycast' + 'GroundAt(FVector::ZeroVector);' + "`n    const double GridMs =") }
    @{ Name = 'M3 chat line removed'; Key = 'dump'; Expect = 'L3'; Find = 'const bool bPosted = FNodeShuffleModule::PostChatNotice(GetWorld(), Body, TEXT("LAYOUTDUMP"));'; Repl = 'const bool bPosted = false;' }
    @{ Name = 'M4 C++ E fields swapped'; Key = 'dump'; Expect = 'L4'; Find = 'cave=%d rayCasted=%d live=%d'; Repl = 'rayCasted=%d cave=%d live=%d' }
    @{ Name = 'M5 .py G field renamed'; Key = 'py'; Expect = 'L4'; Find = "'landFrac', 'dealableFrac'"; Repl = "'landFrac', 'dealFrac'" }
    @{ Name = 'M6 fixture G line loses a field'; Key = 'fx'; Expect = 'L4'; Find = 'landFrac=0.5000 dealableFrac=0.5000'; Repl = 'dealableFrac=0.5000' }
    @{ Name = 'M7 END loses an argument'; Key = 'dump'; Expect = 'L4'; Find = 'DealableNotLand, LatchMaskBefore, LatchMaskAfter'; Repl = 'LatchMaskBefore, LatchMaskAfter' }
    @{ Name = 'M8 incomplete-block refusal disabled'; Key = 'py'; Expect = 'L5'; Find = "    if cur is not None:`n        raise DumpError(f'the last"; Repl = "    if False:`n        raise DumpError(f'the last" }
    @{ Name = 'M9 legend exemplifies a field'; Key = 'dump'; Expect = 'L6'; Find = 'countsMatch is one only when'; Repl = 'countsMatch=1 only when' }
    @{ Name = 'M10 registration TODO dropped'; Key = 'dump'; Expect = 'L7'; Find = "release.`nstatic FAutoConsoleCommandWithWorldAndArgs"; Repl = "release.`n`nstatic FAutoConsoleCommandWithWorldAndArgs" }
    @{ Name = 'M11 command also registered in NodeShuffle.cpp'; Key = 'nsmod'; Expect = 'L8'; Find = 'LOADED ('; Repl = 'DumpLayout LOADED (' }
    @{ Name = 'M12 slope restated'; Key = 'dump'; Expect = 'L9'; Find = 'const bool bDeal = RwgDensityLandSample(Q, X, Y);'; Repl = ('double RR = 0.0; const bool bDeal = bLand && !RwgApiSlope' + 'TooSteep(Q, X, Y, RR);') }
    @{ Name = 'M13 fixture stays parseable but .py FIELDS lose BEGIN order'; Key = 'py'; Expect = 'L4'; Find = "'dumpId', 'map', 'seed'"; Repl = "'dumpId', 'seed', 'map'" }
    # Review fixes (F2/F3/F6) -- anchored on context unique to the E / BEGIN line (W also prints a pinned field).
    @{ Name = 'M14 E format drops pinned'; Key = 'dump'; Expect = 'L4'; Find = 'newNode=%d pinned=%d"'; Repl = 'newNode=%d"' }
    @{ Name = 'M15 BEGIN format drops gradesDone'; Key = 'dump'; Expect = 'L4'; Find = 'apiUsable=%d gradesDone=%d"'; Repl = 'apiUsable=%d"' }
    @{ Name = 'M16 grid gate reduced to bApi'; Key = 'dump'; Expect = 'L10'; Find = ('const bool bGrid = bApi' + ' && bGradesDone;'); Repl = 'const bool bGrid = bApi;' }
    @{ Name = 'M17 grader AND loses the killbox term'; Key = 'dump'; Expect = 'L10'; Find = (' && bRwgVoidCheckDone' + ' && bRwgKillboxDone;'); Repl = ' && bRwgVoidCheckDone;' }
    @{ Name = 'M18 grid loop keyed on bApi again'; Key = 'dump'; Expect = 'L10'; Find = ('if (b' + 'Grid)'); Repl = ('if (b' + 'Api)') }
    @{ Name = 'M19 latch masks lose the Done bits'; Key = 'dump'; Expect = 'L10'; Find = ' | (bRwgKillboxDone ? 32 : 0);'; Repl = ';' }
    @{ Name = 'M20 fixture E line drifts from the format (extra literal)'; Key = 'fx'; Expect = 'L11'; Find = 'LAYOUTDUMP-E: i=0 guid='; Repl = 'LAYOUTDUMP-E: i=0 entry guid=' }
    @{ Name = 'M21 fixture legend drifts from source'; Key = 'fx'; Expect = 'L11'; Find = 'one line per layout entry'; Repl = 'one line per entry' }
    @{ Name = 'M22 .py drops the pinned filter from originals-all'; Key = 'py'; Expect = 'L5'; Find = "[(e['x'], e['y']) for e in E if not e['newNode'] and e['pinned']]"; Repl = "[(e['x'], e['y']) for e in E if not e['newNode']]" }
    @{ Name = 'M22b .py drops the pinned filter from player-findable'; Key = 'py'; Expect = 'L5'; Find = "or (not e['newNode'] and e['pinned'])]"; Repl = "or (not e['newNode'])]" }
    @{ Name = 'M23 fixture latch mask changes across the grid'; Key = 'fx'; Expect = 'L5'; Find = 'latchMaskAfter=63 '; Repl = 'latchMaskAfter=3 ' }
    @{ Name = 'M24 fixture dealableNotLand nonzero'; Key = 'fx'; Expect = 'L5'; Find = 'dealableNotLand=0 '; Repl = 'dealableNotLand=2 ' }
    @{ Name = 'M25 fixture BEGIN says the graders were not done'; Key = 'fx'; Expect = 'L5'; Find = ' apiUsable=1 gradesDone=1'; Repl = ' apiUsable=1 gradesDone=0' }
)
$mutantFails = @()
$caught = 0
foreach ($m in $mutants) {
    $mut = @{}
    foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = Invoke-LayoutDumpChecks $mut ($m.Expect -eq 'L5')
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ }
    else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "LAYOUTDUMP LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: LAYOUTDUMP lint -- L1..L12 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
