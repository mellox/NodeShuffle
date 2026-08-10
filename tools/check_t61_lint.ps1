# ns-t61-observe-always -- THE LINT FOR THE ONE-LINE EDITS THAT TURN OBSERVE MODE BACK INTO ENFORCE.
#
# T61's contract is a single sentence: with NodeShuffle.DestroyerVeto=0 the KBFL requirement hook is
# ARMED and MEASURING, and IsRequirementMet returns TRUE for every target class -- managed nodes
# included -- so the world behaves exactly as if the hook were absent. Every part of that sentence is
# held up by a handful of lines, and each of those lines has an obvious-looking one-line edit that
# breaks it SILENTLY: nothing crashes, nothing logs, and a player who never opted in starts having
# another mod's behaviour vetoed on their save.
#
# THERE IS NO UNIT TEST HERE TO GO GREEN -- the failure is a live requirement evaluation inside a
# third-party mod's KBFL chain, and the runtime instrument is the VETOCENSUS line's mode field, which
# only speaks after someone has already played. This lint is the part that speaks at edit time.
#
# THE EDITS IT PINS, each a genuine one-liner someone would write under pressure:
#   1. NEUTER OR NARROW THE OBSERVE GUARD in the requirement (delete it, `&&` another term onto it, or
#      make it conditional on the target being foreign). The guard is the invariant.
#   2. PUT A VETO RETURN ABOVE THE GUARD. Ordering is the whole mechanism: any `return false` that can
#      be reached before the guard is a veto that fires in observe mode.
#   3. RESTORE THE MASTER-GATE EARLY RETURN in ArmDestroyerVetoIfEnabled. That is the pre-T61 shape --
#      the hook simply never arms -- and it takes the census, the config rows and the notice with it.
#   4. LATCH PROTECTION WITHOUT THE MODE TERM. `!bObserveOnly &&` is what makes "protection is off
#      whenever the master gate is off" true by construction rather than by agreement.
#   5. MODE-GATE THE CENSUS TIMERS. An observing world's census IS the deliverable; a `bObserveOnly`
#      early-out in that block reproduces exactly the blindness T61 removed.
#   6. DROP THE MODE FIELD FROM THE CENSUS. Then no log distinguishes an observing world from an
#      enforcing one, and every later measurement is uninterpretable.
#
# IT READS THE FILES ON DISK, NOT `git show` -- same reason as check_t54_lint.ps1 / check_t59_lint.ps1:
# a git-backed read goes vacuous on exactly the uncommitted tree it exists to check.
#
# IT STRIPS `//` COMMENTS BEFORE MATCHING, preserving line numbers by blanking rather than removing --
# this file's own rationale, and the pinned files' comments, legitimately DISCUSS the forbidden forms.
#
# NON-VACUITY IS CHECKED FOR EVERY HALF: each ban is paired with an assertion that the thing being
# protected still EXISTS, so a rename or a deletion cannot turn a ban into a silent pass.
#
# SILENT ON PASS. Exit 0 = every pin holds. Exit 1 = at least one does not, one line each naming
# file+pattern+why.
#
# MUTATION TEST (in memory, no files written):  pwsh -File tools\check_t61_lint.ps1 -MutationTest

param([switch]$MutationTest)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$reqFile  = 'NodeShuffleDestroyerVetoRequirement.cpp'
$armFile  = 'NodeShuffleVetoKBFL.cpp'
$mainFile = 'NodeShuffle.cpp'
$reqPath  = Join-Path $root ('Source\NodeShuffleVetoKBFL\Private\' + $reqFile)
$armPath  = Join-Path $root ('Source\NodeShuffleVetoKBFL\Private\' + $armFile)
$mainPath = Join-Path $root ('Source\NodeShuffle\Private\' + $mainFile)

function Remove-LineComments([string]$text) {
    ($text -split "`n" | ForEach-Object { $_ -replace '//.*$', '' }) -join "`n"
}

function Get-LineNumber([string[]]$lines, [string]$pattern) {
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match $pattern) { return $i + 1 }
    }
    return -1
}

function Get-AllLineNumbers([string[]]$lines, [string]$pattern) {
    $out = @()
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match $pattern) { $out += ($i + 1) }
    }
    return ,$out
}

# Returns an array of failure strings. Empty array = every pin holds.
function Get-T61Failures([string]$reqText, [string]$armText, [string]$mainText) {
    $fails = @()
    $req  = Remove-LineComments $reqText
    $arm  = Remove-LineComments $armText
    $main = Remove-LineComments $mainText
    $reqL  = $req  -split "`n"
    $armL  = $arm  -split "`n"
    $mainL = $main -split "`n"

    # ---- HALF 1: the observe guard exists, is EXACTLY the mode test, and precedes every veto return ----
    $guardLine = Get-LineNumber $reqL 'if\s*\(\s*GNodeShuffleVetoObserveOnly\s*\)'
    if ($guardLine -eq -1) {
        $fails += "FAIL: $reqFile has no bare /if (GNodeShuffleVetoObserveOnly)/ guard -- the observe-only invariant is gone or has been narrowed with an extra term. While that guard is the sole gate, an observing world returns true for EVERY target; any additional conjunct makes some target class vetoable with the master gate off."
    } else {
        $window = ($reqL[($guardLine - 1)..([Math]::Min($reqL.Count - 1, $guardLine + 20))]) -join "`n"
        if ($window -notmatch 'return\s+true\s*;') {
            $fails += "FAIL: $reqFile line $guardLine has the observe guard but no /return true;/ within 20 lines of it -- the guard must RETURN the un-hooked result, not merely log."
        }
        foreach ($ln in (Get-AllLineNumbers $reqL 'return\s+false\s*;')) {
            if ($ln -lt $guardLine) {
                $fails += "FAIL: $reqFile line $ln can return false BEFORE the observe guard on line $guardLine -- that is a veto that fires while the master gate is off. Every veto return must sit below the guard."
            }
        }
    }
    # NON-VACUITY: the function being protected, and the latch's single writer.
    if ($req -notmatch 'IsRequirementMet_Implementation') {
        $fails += "FAIL: $reqFile no longer defines IsRequirementMet_Implementation -- half 1 is vacuous; the pinned invariant lives in that function."
    }
    # The DECLARATION's initialiser is not a write for this purpose -- it is the file-static's default,
    # which every world init overwrites before anything can be judged. Only assignments count.
    $writes = @((Get-AllLineNumbers $reqL 'GNodeShuffleVetoObserveOnly\s*=') |
        Where-Object { $reqL[$_ - 1] -notmatch '^\s*static\s' })
    if ($writes.Count -ne 1) {
        $fails += "FAIL: $reqFile has $($writes.Count) writes to GNodeShuffleVetoObserveOnly (expected exactly 1, in ResetSessionCounters). The mode is latched ONCE per world; a second writer means one KBFL sweep can be split across two modes."
    }
    # COLD REVIEW F3: the write COUNT is not the pin -- the assigned VALUE is. `= false;` is still
    # exactly one write, passes the count above, and makes every world enforce on a default install.
    if ($req -notmatch 'GNodeShuffleVetoObserveOnly\s*=\s*bObserveOnly\s*;') {
        $fails += "FAIL: $reqFile does not assign GNodeShuffleVetoObserveOnly from the bObserveOnly PARAMETER -- a constant on that line makes every world run one mode regardless of the master gate, while the write-count pin above still passes."
    }

    # ---- HALF 2: the census names the mode, from this run's latch ----
    if ($req -notmatch 'VETOCENSUS') {
        $fails += "FAIL: $reqFile no longer emits a VETOCENSUS line -- half 2 is vacuous and an observing world produces no measurement at all."
    }
    if ($req -notmatch 'mode=%s') {
        $fails += "FAIL: $reqFile's census no longer carries a /mode=%s/ field -- nothing in the log then distinguishes an observing world from an enforcing one, and every later reading of that log is uninterpretable."
    }
    foreach ($word in @('observing', 'enforcing')) {
        if ($req -notmatch $word) {
            $fails += "FAIL: $reqFile no longer contains the mode word '$word' -- the census can no longer print both states, so the field above is decorative."
        }
    }

    # ---- HALF 3: the arm call is UNCONDITIONAL on the master gate ----
    $obsDecl = Get-LineNumber $mainL 'const\s+bool\s+bObserveOnly\s*=\s*\(\s*GNodeShuffleDestroyerVeto\s*==\s*0\s*\)'
    if ($obsDecl -eq -1) {
        $fails += "FAIL: $mainFile no longer derives bObserveOnly from /GNodeShuffleDestroyerVeto == 0/ -- the master gate must select the MODE, not whether the hook arms."
    }
    foreach ($ln in (Get-AllLineNumbers $mainL 'GNodeShuffleDestroyerVeto\s*==\s*0')) {
        if ($ln -eq $obsDecl) { continue }
        $to = [Math]::Min($mainL.Count - 1, $ln + 11)
        $w  = ($mainL[($ln - 1)..$to]) -join "`n"
        if ($w -match 'return\s*;') {
            $fails += "FAIL: $mainFile line $ln tests the master gate and returns within 12 lines -- that is the pre-T61 shape where the hook never arms, and it silently removes the census, the per-resource rows and the chat notice."
        }
    }
    # COLD REVIEW F4: the ban above only inspects lines that name the CVar. The same blindness is one
    # line away spelled with the variable that is right there -- `if (bObserveOnly) { return; }` inside
    # ArmDestroyerVetoIfEnabled restores the pre-T61 shape and passed every earlier pin.
    $armFnLine = Get-LineNumber $mainL 'void\s+FNodeShuffleModule::ArmDestroyerVetoIfEnabled'
    if ($armFnLine -eq -1) {
        $fails += "FAIL: $mainFile no longer defines ArmDestroyerVetoIfEnabled -- the early-return ban below is vacuous."
    } else {
        for ($i = $armFnLine; $i -lt [Math]::Min($mainL.Count, $armFnLine + 60); $i++) {
            if ($mainL[$i] -match 'bObserveOnly' -and $mainL[$i] -match 'return\s*;') {
                $fails += "FAIL: $mainFile line $($i + 1) returns early on bObserveOnly inside ArmDestroyerVetoIfEnabled -- the mode must never decide WHETHER to arm."
            }
        }
    }
    $armCalls = Get-AllLineNumbers $mainL 'GNodeShuffleKBFLVetoArmFn\s*\(\s*World\s*,\s*bObserveOnly\s*,\s*bProtectForeignNodes\s*\)'
    if ($armCalls.Count -ne 1) {
        $fails += "FAIL: $mainFile has $($armCalls.Count) calls of the form /GNodeShuffleKBFLVetoArmFn(World, bObserveOnly, bProtectForeignNodes)/ (expected exactly 1) -- the arm pass must be invoked once per world init and must receive both latched policy booleans."
    }

    # ---- HALF 4: protection can never be latched on while the mode is observing ----
    if ($main -notmatch '!\s*bObserveOnly\s*&&\s*IsForeignNodeProtectionEnabled\s*\(\s*\)') {
        $fails += "FAIL: $mainFile no longer computes the protection latch as /!bObserveOnly && IsForeignNodeProtectionEnabled()/ -- without the mode term, NodeShuffle.ProtectForeignNodes could latch ON in a world whose master gate is off, and foreign nodes would be vetoed on a default install."
    }
    # COLD REVIEW F5: half 4 pinned only the MAIN module's expression, so the two-module split could be
    # restored in the arm file with one identifier -- protection would latch ON in an observing world
    # and the import runtime test 7 expects to be gone would silently come back.
    if ($arm -match 'IsForeignNodeProtectionEnabled') {
        $fails += "FAIL: $armFile references IsForeignNodeProtectionEnabled -- T61 moved BOTH policy reads into the module that owns the CVars; reading it here re-splits the policy across two modules and re-adds an import the DLL check expects to be gone."
    }
    if ($arm -notmatch 'const\s+bool\s+bProtectForeign\s*=\s*bProtectForeignNodes\s*;') {
        $fails += "FAIL: $armFile no longer takes its protection latch straight from the caller's parameter."
    }
    if ($arm -notmatch 'ResetSessionCounters\s*\(\s*bProtectForeign\s*,\s*bObserveOnly\s*\)') {
        $fails += "FAIL: $armFile no longer passes both latched booleans into ResetSessionCounters -- the requirement then evaluates against a stale or defaulted mode."
    }

    # ---- HALF 5: the census timers are scheduled in BOTH modes ----
    $timerLines = Get-AllLineNumbers $armL 'GetTimerManager\(\)\.SetTimer'
    if ($timerLines.Count -lt 2) {
        $fails += "FAIL: $armFile schedules $($timerLines.Count) census timer(s) (expected 2) -- half 5 is vacuous and the per-world census may never be emitted."
    } else {
        $from = [Math]::Max(0, $timerLines[0] - 9)
        $to   = [Math]::Min($armL.Count - 1, $timerLines[-1] + 4)
        $w = ($armL[$from..$to]) -join "`n"
        if ($w -match 'bObserveOnly') {
            $fails += "FAIL: $armFile mentions bObserveOnly inside the census-timer block (lines $($from + 1)..$($to + 1)) -- mode-gating the census is exactly the blindness T61 removed: an observing world's census IS the deliverable."
        }
    }

    return ,$fails
}

$reqRaw  = Get-Content -Raw $reqPath
$armRaw  = Get-Content -Raw $armPath
$mainRaw = Get-Content -Raw $mainPath

if (-not $MutationTest) {
    $f = Get-T61Failures $reqRaw $armRaw $mainRaw
    if ($f.Count -gt 0) { $f | ForEach-Object { Write-Host $_ }; exit 1 }
    exit 0
}

# ---- mutation mode: prove each pin actually catches its edit ----
$control = Get-T61Failures $reqRaw $armRaw $mainRaw
Write-Host ("CONTROL (unmutated tree): {0}" -f $(if ($control.Count -eq 0) { 'PASS (0 failures) -- as required' } else { "UNEXPECTEDLY FAILING with $($control.Count):`n  " + ($control -join "`n  ") }))

function Replace-First([string]$text, [string]$find, [string]$repl) {
    $i = $text.IndexOf($find)
    if ($i -lt 0) { return $null }
    return $text.Substring(0, $i) + $repl + $text.Substring($i + $find.Length)
}

$mutants = @(
    @{ Name = 'M1 narrow the observe guard with an extra term'; File = 'req';
       Find = 'if (GNodeShuffleVetoObserveOnly)'; Repl = 'if (GNodeShuffleVetoObserveOnly && !bManaged)' },
    # No mutant embeds a line ending in its Find string: the tree is CRLF and a `\n` would silently
    # turn every such mutant into "NOT APPLIED", i.e. a test that proves nothing (check_t59_lint's note).
    @{ Name = 'M2 veto managed nodes ABOVE the observe guard'; File = 'req';
       Find = '    GNodeShuffleVetoEvalCount++;'; Repl = '    GNodeShuffleVetoEvalCount++; if (bManaged) { return false; }' },
    @{ Name = 'M3 restore the master-gate early return in the arm entry point'; File = 'main';
       Find = "    const bool bProtectForeignNodes = !bObserveOnly && IsForeignNodeProtectionEnabled();";
       Repl = "    if (GNodeShuffleDestroyerVeto == 0) { return; }`n    const bool bProtectForeignNodes = !bObserveOnly && IsForeignNodeProtectionEnabled();" },
    @{ Name = 'M4 latch protection without the mode term'; File = 'main';
       Find = '!bObserveOnly && IsForeignNodeProtectionEnabled()'; Repl = 'IsForeignNodeProtectionEnabled()' },
    @{ Name = 'M5 mode-gate the census timers'; File = 'arm';
       Find = '        if (World)'; Repl = '        if (World && !bObserveOnly)' },
    @{ Name = 'M6 drop the mode field from the census'; File = 'req';
       Find = 'VETOCENSUS %s: mode=%s'; Repl = 'VETOCENSUS %s:' },
    # The three mutants the cold review's F3/F4/F5 holes were found by. Each passed the pre-review lint.
    @{ Name = 'M7 latch a constant mode'; File = 'req';
       Find = 'GNodeShuffleVetoObserveOnly = bObserveOnly;'; Repl = 'GNodeShuffleVetoObserveOnly = false;' },
    @{ Name = 'M8 early-return on bObserveOnly inside the arm entry point'; File = 'main';
       Find = '    GNodeShuffleVetoObserveOnlyThisWorld = bObserveOnly;';
       Repl = '    if (bObserveOnly) { return; }' },
    @{ Name = 'M9 re-split the policy by re-reading the CVar in the arm file'; File = 'arm';
       Find = 'const bool bProtectForeign = bProtectForeignNodes;';
       Repl = 'const bool bProtectForeign = FNodeShuffleModule::IsForeignNodeProtectionEnabled();' }
)

$missed = 0
foreach ($m in $mutants) {
    $r = $reqRaw; $a = $armRaw; $mn = $mainRaw
    switch ($m.File) {
        'req'  { $r  = Replace-First $reqRaw  $m.Find $m.Repl; $applied = $r }
        'arm'  { $a  = Replace-First $armRaw  $m.Find $m.Repl; $applied = $a }
        'main' { $mn = Replace-First $mainRaw $m.Find $m.Repl; $applied = $mn }
    }
    if ($null -eq $applied) {
        Write-Host ("{0}: NOT APPLIED -- the text it edits was not found. The mutation test is stale, which means it is proving nothing." -f $m.Name)
        $missed++
        continue
    }
    $f = Get-T61Failures $r $a $mn
    $new = @($f | Where-Object { $control -notcontains $_ })
    if ($new.Count -gt 0) {
        Write-Host ("{0}: CAUGHT ({1} new failure(s)) -- first: {2}" -f $m.Name, $new.Count, $new[0])
    } else {
        Write-Host ("{0}: MISSED -- the lint does not detect this edit." -f $m.Name)
        $missed++
    }
}
if ($missed -gt 0 -or $control.Count -gt 0) { exit 1 }
exit 0
