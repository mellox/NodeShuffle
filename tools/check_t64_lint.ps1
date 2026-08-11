# ns-t64-rock-hide-and-audit -- THE LINT FOR THE ONE-LINE EDITS THAT PUT THE ROCK LAG BACK.
#
# T64's contract is one sentence: the moment an original's AFGNodeMeshActor is paired into
# MeshActorCache, a steady-hidden original's rock is CAPTURED IF NEEDED AND THEN HIDDEN, at BOTH add
# sites, and the sweep that finds those pairings can see originals deriving directly from
# AFGResourceNodeBase. Every clause of that sentence is held up by a handful of lines, and each has an
# obvious-looking one-line edit that breaks it SILENTLY: nothing crashes, nothing logs, and the rock
# lag measured in scratchpad/solid-hide-latency.md (delays up to 1535 s, 212 of 220 rocks still drawn
# when their pairing resolved) comes straight back with the counters still reading zero.
#
# THERE IS NO UNIT TEST HERE TO GO GREEN -- the failure is world-partition streaming inside a shipping
# game, and the runtime instrument is the ROCKHIDE-CENSUS line, which only speaks after someone has
# already played. This lint is the part that speaks at edit time.
#
# THE EDITS IT PINS, each a genuine one-liner someone would write under pressure:
#   1. DROP THE HIDE CALL AT EITHER CACHE-ADD SITE. One site alone leaves the other route's population
#      lagging exactly as before, and the census would still print (with a smaller number) so the
#      regression looks like a quiet world rather than a missing branch.
#   2. NARROW THE FORWARD-LINK ITERATOR BACK TO AFGResourceNode. That is the pre-T64 shape and it is
#      the T43 defect: a class deriving directly from AFGResourceNodeBase is not in that iterator's
#      type at all, so its rock can only ever be paired by the engine back-link.
#   3. DROP THE FRACKING EXCLUSION from that now-wider iterator. Widening the type is what makes this
#      possible: fracking members were previously excluded by BOTH the iterator's type and the test,
#      and now only by the test.
#   4. MOVE THE HIDE ABOVE THE CAPTURE, or delete the capture call. The author's ruling is "check if
#      captured, capture if needed, then hide"; reversing it hands the capture step a donor this mod
#      just hid, and the only thing that would have made that safe is an unmeasured code comment.
#   5. HIDE WITHOUT THE STEADY-RECORD IDENTITY TEST. Dropping the weak-pointer comparison turns a
#      stale path key from a previous instance into a licence to hide a stranger's rock.
#   6. RE-BREAK THE ROUTE ATTRIBUTION by dropping the per-pass carry map the backstop consults. That
#      is what made `route=backstop` structurally unable to fire before this packet, so a zero there
#      was an artifact of ordering and not a measurement.
#
# IT READS THE FILES ON DISK, NOT `git show` -- same reason as check_t54/t59/t61_lint.ps1: a git-backed
# read goes vacuous on exactly the uncommitted tree it exists to check.
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
# MUTATION TEST (in memory, no files written):  pwsh -File tools\check_t64_lint.ps1 -MutationTest

param([switch]$MutationTest)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$subFile  = 'NodeShuffleSubsystem.cpp'
$hideFile = 'NodeShuffleRockStreamInHide.cpp'
$subPath  = Join-Path $root ('Source\NodeShuffle\Private\' + $subFile)
$hidePath = Join-Path $root ('Source\NodeShuffle\Private\' + $hideFile)

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
function Get-T64Failures([string]$subText, [string]$hideText) {
    $fails = @()
    $sub  = Remove-LineComments $subText
    $hide = Remove-LineComments $hideText
    $subL  = $sub  -split "`n"
    $hideL = $hide -split "`n"

    # ---- HALF 1: the hide runs at BOTH MeshActorCache.Add sites, and AFTER the Add at each ----
    # NOT wrapped in @(): Get-AllLineNumbers returns `,$out`, and @() around that yields an array whose
    # single element is the inner array -- which makes every comparison below compare against the whole
    # list and the pin silently stops meaning anything. Assignment already collects the inner array.
    $addLines  = Get-AllLineNumbers $subL 'MeshActorCache\.Add\s*\('
    $hideCalls = Get-AllLineNumbers $subL 'TryHideStreamedRockForSteadyOriginal\s*\('
    if ($addLines.Count -ne 2) {
        $fails += "FAIL: $subFile has $($addLines.Count) /MeshActorCache.Add(/ site(s), expected exactly 2 (back-link sweep and forward-link sweep). The pin below is written against those two sites; a third one would be an unguarded pairing whose rock nothing hides."
    }
    if ($hideCalls.Count -lt 2) {
        $fails += "FAIL: $subFile calls TryHideStreamedRockForSteadyOriginal $($hideCalls.Count) time(s), expected one per cache-add site. A missing call leaves that route's originals lagging exactly as they did before T64, while the census still prints a smaller non-zero number and reads like a quiet world."
    }
    foreach ($add in $addLines) {
        $near = @($hideCalls | Where-Object { $_ -gt $add -and $_ -le ($add + 8) })
        if ($near.Count -eq 0) {
            $fails += "FAIL: $subFile line $add adds a pairing to MeshActorCache with no TryHideStreamedRockForSteadyOriginal call within the following 8 lines. The hide must run AT the add and AFTER it -- the capture step inside reads the pairing back out of this very cache."
        }
    }

    # ---- HALF 2: the forward-link sweep iterates the BASE class, and still excludes fracking ----
    # SCOPED TO RebuildMeshActorCache ON PURPOSE. NodeShuffleSubsystem.cpp has other
    # TActorIterator<AFGResourceNodeBase> sweeps (the VanillaNodeCache refresh is one), and an unscoped
    # match on them is a pin that passes no matter what this function's iterator is narrowed back to --
    # measured, not predicted: the first run of this lint's mutation test reported M3 and M4 as MISSED
    # for exactly that reason.
    $rebuildStart = Get-LineNumber $subL 'ANodeShuffleSubsystem::RebuildMeshActorCache'
    $rebuildEnd   = if ($rebuildStart -eq -1) { -1 } else { [Math]::Min($subL.Count, $rebuildStart + 200) }
    $iterLine = -1
    if ($rebuildStart -ne -1) {
        foreach ($ln in (Get-AllLineNumbers $subL 'TActorIterator<\s*AFGResourceNodeBase\s*>\s*NodeIt\s*\(\s*GetWorld\(\)\s*\)')) {
            if ($ln -ge $rebuildStart -and $ln -le $rebuildEnd) { $iterLine = $ln; break }
        }
    }
    if ($iterLine -eq -1) {
        $fails += "FAIL: $subFile has no /TActorIterator<AFGResourceNodeBase>/ sweep -- the forward-link pairing has been narrowed back to AFGResourceNode. That is the T43 defect: a class deriving directly from the base is not in that iterator's type at all, so its rock can only ever be paired by the engine back-link, which is unset for most level nodes in a modded world."
    } else {
        $window = ($subL[($iterLine - 1)..([Math]::Min($subL.Count - 1, $iterLine + 8))]) -join "`n"
        if ($window -notmatch 'IsFrackingActor\s*\(') {
            $fails += "FAIL: $subFile line $iterLine widened the sweep to AFGResourceNodeBase but has no IsFrackingActor test within 8 lines. Widening the type is what makes that test load-bearing: fracking members used to be excluded by the iterator's type AND the test, and are now excluded by the test alone."
        }
    }
    # NON-VACUITY: the function the two halves above live in.
    if ($sub -notmatch 'ANodeShuffleSubsystem::RebuildMeshActorCache') {
        $fails += "FAIL: $subFile no longer defines RebuildMeshActorCache -- halves 1 and 2 are vacuous; both pinned invariants live in that function."
    }

    # ---- HALF 3: capture BEFORE hide, and the steady-record identity test, in the helper ----
    $capLine  = Get-LineNumber $hideL 'CaptureOriginalVisualIfNeeded\s*\('
    $hideLine = Get-LineNumber $hideL 'SetActorHiddenInGame\s*\(\s*true\s*\)'
    if ($capLine -eq -1) {
        $fails += "FAIL: $hideFile no longer calls CaptureOriginalVisualIfNeeded -- the author's ruling is 'check if captured, capture if needed, then hide', and without the call a resource whose only donor is this just-streamed rock loses it the moment this route hides it."
    }
    if ($hideLine -eq -1) {
        $fails += "FAIL: $hideFile no longer calls /SetActorHiddenInGame(true)/ -- half 3 is vacuous and the whole route hides nothing."
    }
    if ($capLine -ne -1 -and $hideLine -ne -1 -and $capLine -gt $hideLine) {
        $fails += "FAIL: $hideFile hides on line $hideLine BEFORE capturing on line $capLine. Order is the whole mechanism here: capture's first source reads the paired mesh actor's static mesh component, and the only thing that would make reading a hidden donor safe is an unmeasured code comment this packet deliberately does not rest on."
    }
    if ($hide -notmatch 'SteadyHiddenOriginals\.Find\s*\(') {
        $fails += "FAIL: $hideFile no longer looks the node up in SteadyHiddenOriginals -- this route would then hide the rock of any hidden node it is handed, including one this mod never suppressed."
    }
    if ($hide -notmatch 'Steady->Get\(\)\s*!=\s*Node') {
        $fails += "FAIL: $hideFile no longer compares the steady record's weak pointer against the node it was handed. The map is keyed by path, and a path key left behind by a previous instance would otherwise be a licence to hide a stranger's rock."
    }
    if ($hide -notmatch 'ANodeShuffleSubsystem::TryHideStreamedRockForSteadyOriginal') {
        $fails += "FAIL: $hideFile no longer defines TryHideStreamedRockForSteadyOriginal -- half 3 is vacuous; every pin above it names that function."
    }

    # ---- HALF 4: the route-attribution carry map survives to the backstop ----
    $carryDecl = Get-LineNumber $subL 'TMap<FString,\s*FNodeShuffleMeshHideWatch>\s+WatchReportedThisPass'
    if ($carryDecl -eq -1) {
        $fails += "FAIL: $subFile no longer declares WatchReportedThisPass -- the watch sweep drops a record in the same pass its rock becomes findable, so the backstop's attribution loop can never match it. That is what made /route=backstop/ read 0 across 220 reports: an artifact of ordering, not a measurement."
    } else {
        $uses = Get-AllLineNumbers $subL 'WatchReportedThisPass'
        if (@($uses | Where-Object { $_ -gt $carryDecl }).Count -lt 2) {
            $fails += "FAIL: $subFile declares WatchReportedThisPass but does not both fill it and read it later -- a carry map nothing reads restores exactly the blind spot it was added to remove."
        }
    }
    if ($sub -notmatch 'ROCKHIDE-CENSUS') {
        $fails += "FAIL: $subFile no longer emits a ROCKHIDE-CENSUS line -- nothing then reports how many rocks each of the three routes hid, or the denominators those counts belong to, and every later reading of the log is uninterpretable."
    }
    if ($sub -notmatch 'hiddenByStreamInRouteThisPass') {
        $fails += "FAIL: $subFile's MESHHIDE-LATENCY record line no longer carries the stream-in-route field. Without it a rock this route darkened moments earlier in the same pass reads as a rock that arrived hidden, which nothing measured."
    }

    return ,$fails
}

$subRaw  = Get-Content -Raw $subPath
$hideRaw = Get-Content -Raw $hidePath

if (-not $MutationTest) {
    $f = Get-T64Failures $subRaw $hideRaw
    if ($f.Count -gt 0) { $f | ForEach-Object { Write-Host $_ }; exit 1 }
    exit 0
}

# ---- mutation mode: prove each pin actually catches its edit ----
$control = Get-T64Failures $subRaw $hideRaw
Write-Host ("CONTROL (unmutated tree): {0}" -f $(if ($control.Count -eq 0) { 'PASS (0 failures) -- as required' } else { "UNEXPECTEDLY FAILING with $($control.Count):`n  " + ($control -join "`n  ") }))

function Replace-First([string]$text, [string]$find, [string]$repl) {
    $i = $text.IndexOf($find)
    if ($i -lt 0) { return $null }
    return $text.Substring(0, $i) + $repl + $text.Substring($i + $find.Length)
}

# No mutant embeds a line ending in its Find string: the tree is CRLF and a `\n` would silently turn
# every such mutant into "NOT APPLIED", i.e. a test that proves nothing (check_t59_lint's note).
$mutants = @(
    @{ Name = 'M1 drop the hide at the back-link cache-add site'; File = 'sub';
       Find = '            TryHideStreamedRockForSteadyOriginal(Paired, *It);'; Repl = '            ' },
    @{ Name = 'M2 drop the hide at the forward-link cache-add site'; File = 'sub';
       Find = '            TryHideStreamedRockForSteadyOriginal(Node, MA);'; Repl = '            ' },
    @{ Name = 'M3 narrow the forward-link iterator back to AFGResourceNode'; File = 'sub';
       Find = 'for (TActorIterator<AFGResourceNodeBase> NodeIt(GetWorld()); NodeIt; ++NodeIt)';
       Repl = 'for (TActorIterator<AFGResourceNode> NodeIt(GetWorld()); NodeIt; ++NodeIt)' },
    @{ Name = 'M4 drop the fracking exclusion from the widened iterator'; File = 'sub';
       Find = 'if (!IsValid(Node) || Node->HasAnyFlags(RF_Transient) || IsFrackingActor(Node))';
       Repl = 'if (!IsValid(Node) || Node->HasAnyFlags(RF_Transient))' },
    @{ Name = 'M5 delete the capture call so the hide runs alone'; File = 'hide';
       Find = 'const bool bCapturePending = CaptureOriginalVisualIfNeeded(Node);';
       Repl = 'const bool bCapturePending = false;' },
    @{ Name = 'M6 drop the steady-record identity comparison'; File = 'hide';
       Find = 'if (!Steady || Steady->Get() != Node)'; Repl = 'if (!Steady)' },
    @{ Name = 'M7 drop the steady lookup entirely'; File = 'hide';
       Find = 'SteadyHiddenOriginals.Find(Node->GetPathName());'; Repl = 'nullptr;' },
    @{ Name = 'M8 re-break route attribution by not carrying the reported records'; File = 'sub';
       Find = '            WatchReportedThisPass.Add(Pair.Key, Pair.Value);'; Repl = '            ' },
    @{ Name = 'M9 drop the stream-in field from the latency record line'; File = 'sub';
       Find = 'TEXT("hiddenByStreamInRouteThisPass=%d (1 = this mod'; Repl = 'TEXT("(1 = this mod' },
    @{ Name = 'M10 drop the route census line'; File = 'sub';
       Find = 'TEXT("ROCKHIDE-CENSUS hide-pass %d.'; Repl = 'TEXT("hide-pass %d.' }
)

$missed = 0
foreach ($m in $mutants) {
    $s = $subRaw; $h = $hideRaw
    switch ($m.File) {
        'sub'  { $s = Replace-First $subRaw  $m.Find $m.Repl; $applied = $s }
        'hide' { $h = Replace-First $hideRaw $m.Find $m.Repl; $applied = $h }
    }
    if ($null -eq $applied) {
        Write-Host ("{0}: NOT APPLIED -- the text it edits was not found. The mutation test is stale, which means it is proving nothing." -f $m.Name)
        $missed++
        continue
    }
    $f = Get-T64Failures $s $h
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
