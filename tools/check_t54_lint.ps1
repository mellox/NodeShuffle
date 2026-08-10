# ns-t54-immediate-hide -- THE LINT FOR THE TWO ONE-LINE SHORTCUTS.
#
# T54 ships an OPPOSITE-POLARITY TEST PAIR: [NodeShuffle][TEST] T54-A asserts that no well the roll
# marked as moving is still standing at its vanilla origin while nothing blocked hiding it, and T54-B
# asserts the exact complement over the same number. The pair's whole value is that it cannot be
# satisfied without origins actually being hidden.
#
# THERE ARE EXACTLY TWO LINES SOMEONE WOULD WRITE UNDER PRESSURE TO TURN T54-A GREEN WITHOUT DOING THE
# WORK, and they are different in kind, which is why both halves are checked:
#
#   1. ASSIGN A VERDICT A LITERAL. `VerdictA = TEXT("PASS")` retires the measurement outright. Same
#      shortcut check_t23_writers.ps1 forbids for the T23 pair, applied to T54's own file.
#
#   2. RECORD A BLOCKED REASON WITHOUT RUNNING THE ARM. T54-A's denominator is
#      candidates MINUS blocked, and an entry is "blocked" purely because the immediate-hide arm wrote a
#      reason code for it into WellImmediateHideReasonThisPass. A write from anywhere else inflates the
#      subtracted population and shrinks the denominator toward zero -- which reads as PASS, and at the
#      limit as VACUOUS, while nothing in the world changed. This is WORSE than a fake pass: it also
#      makes the pair's hole detector (entries neither suppressed nor blocked-with-a-reason) report zero
#      holes, so the one instrument that would catch the lie is silenced by the same line.
#
# READS ARE DELIBERATELY ALLOWED. NodeShuffleWellImmediateHide.cpp calls .Find() on that map -- that IS
# the pair consuming what the arm measured, and forbidding it would forbid the feature. Only WRITES are
# forbidden outside the arm's own file.
#
# IT READS THE FILES ON DISK, NOT `git show`. docs/TECH-DEBT.md's gate-placement lesson is that a check
# reading through `git show` sees only committed content and silently passes an uncommitted tree unless
# it is handed a `git stash create` object. This packet is code-complete and UNCOMMITTED by design, so a
# git-backed read would have gone vacuous on exactly the tree it exists to check. A plain file read has
# no such failure mode and needs no stash.
#
# SILENT ON PASS. Exit 0 = both censuses hold. Exit 1 = one does not, with one line naming file+pattern.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source\NodeShuffle'

# The sanctioned sites, by file. Named here AND in the code that owns them; moving a site changes both.
$pairFile     = 'NodeShuffleWellImmediateHide.cpp'   # EmitWellImmediateHideTestPair
$allowedWrite = 'NodeShuffleWellRelocateApply.cpp'   # ApplyWellRelocation's immediate-hide arm

$fail = $false

# ---- HALF 1: neither T54 verdict may be assigned a literal ----
# Matches the DIRECT form only (`VerdictA = TEXT(...)`). The shipped code assigns from a ternary whose
# CONDITION is the measured population, so it does not match -- which is the whole distinction being
# enforced: a verdict computed from a measurement is fine, a verdict handed a constant is not.
$verdictPattern = 'Verdict[AB]\s*=\s*TEXT\s*\('
$pairPath = Join-Path $src ('Private\' + $pairFile)
foreach ($hit in @(Select-String -Path $pairPath -Pattern $verdictPattern)) {
    Write-Host "FAIL: $pairFile line $($hit.LineNumber) assigns a T54 verdict a literal, matching /$verdictPattern/ -- both verdicts must be computed from the measured population."
    $fail = $true
}

# NON-VACUITY for half 1 (check_t23_writers.ps1 cold review F8: a lint half that matches nothing passes
# silently and enforces nothing). Both verdicts must still EXIST in that file for the check above to be
# checking anything at all.
foreach ($name in @('VerdictA', 'VerdictB')) {
    if (@(Select-String -Path $pairPath -Pattern ([regex]::Escape($name))).Count -eq 0) {
        Write-Host "FAIL: $pairFile names no $name at all -- this check has gone vacuous. The T54 pair must exist for its literal-assignment ban to mean anything."
        $fail = $true
    }
}

# ---- HALF 2: the blocked-reason map may only be WRITTEN by the arm that measures ----
# Every mutating form TMap offers that this code could plausibly use, plus whole-map assignment. .Find
# and .Contains are absent on purpose: they are reads, and the pair depends on them.
$writePattern = 'WellImmediateHideReasonThisPass\s*(\.\s*(Add|Emplace|Append|Reset|Empty|Remove|FindOrAdd)\s*\(|=[^=])'
$writeHits = @(Select-String -Path (Join-Path $src '*\*.cpp') -Pattern $writePattern)
if ($writeHits.Count -eq 0) {
    Write-Host "FAIL: no write to WellImmediateHideReasonThisPass matched at all, against /$writePattern/ -- this check has gone vacuous. The immediate-hide arm must record why it declined, or the T54 pair's denominator is unmeasured."
    $fail = $true
}
foreach ($hit in $writeHits) {
    $file = Split-Path -Leaf $hit.Path
    if ($file -ne $allowedWrite) {
        Write-Host "FAIL: WellImmediateHideReasonThisPass is written in $file line $($hit.LineNumber), matching /$writePattern/ -- only $allowedWrite may write it, and only beneath the decision it records. A write elsewhere inflates the blocked population, shrinks T54-A's denominator toward a vacuous pass, and silences the pair's own hole detector."
        $fail = $true
    }
}

# ---- HALF 3: reason code 3 must stay INSIDE T54-A's denominator (cold review F2) ----
# THE SHORTCUT THIS CATCHES IS A CLASSIFICATION, NOT A WRITE, which is why halves 1 and 2 cannot see it:
# nothing writes the map illegitimately. Folding reason 3 -- "the suppression ran and took no member" --
# into Blocked subtracts it from the denominator, and T54-A then reads PASS while a live vanilla well is
# standing in front of the player. Deleting one `!= 3` is the whole shortcut.
#
# TWO INDEPENDENT ANCHORS, because either alone is brittle: the PREDICATE that performs the split, and
# the PROSE that reports its count. Someone who removes the split almost certainly removes one of the
# two, and removing both is no longer a one-line shortcut. Neither anchor is a value this code prints,
# so no legend/grep collision is introduced by checking for them.
#
# THE DENOMINATOR EXPRESSION IS ANCHORED TOO (scoped re-review, D). Both anchors above survive the
# one-line reintroduction `Eligible = Candidates - Blocked - TookNothing;`, which is the most natural
# shortcut of all ("we know why those didn't hide") and restores F2's false-PASS exactly. So the
# denominator is pinned to subtracting Blocked AND NOTHING ELSE. The `!= 3` branch body is pinned to
# `++Blocked` for the mirror evasion: swapping the two branch bodies also evades a bare `!= 3` anchor.
$splitAnchors = @{
    'the reason-3 split predicate'              = '\*\s*Reason\s*!=\s*3'
    'the take-no-member count in the pair line' = 'take no member at all'
    'the reason-3 branch still bucketing into Blocked' = '!=\s*3\s*\)\s*\{\s*\+\+Blocked'
    'the denominator subtracting Blocked and nothing else' = 'Eligible\s*=\s*Candidates\s*-\s*Blocked\s*;'
}
foreach ($name in $splitAnchors.Keys) {
    $pat = $splitAnchors[$name]
    if (@(Select-String -Path $pairPath -Pattern $pat).Count -eq 0) {
        Write-Host "FAIL: $pairFile no longer contains $name, matching /$pat/ -- reason code 3 must stay INSIDE T54-A's denominator. Folding it into the blocked bucket lets T54-A pass while a vanilla well is still standing."
        $fail = $true
    }
}

if ($fail) { exit 1 }
exit 0
