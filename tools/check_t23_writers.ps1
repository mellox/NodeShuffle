# ns-t23-rollhide -- THE LINT FOR THE ONE-LINE SHORTCUT.
#
# T23 ships an OPPOSITE-POLARITY TEST PAIR: [NodeShuffle][TEST] T23-A asserts that at least one well
# entry is suppressed-and-unplaced (red before roll-time removal lands, green after) and T23-B asserts
# the exact complement. The pair's whole value is that it cannot be satisfied without the world actually
# changing.
#
# THERE IS EXACTLY ONE LINE SOMEONE WOULD WRITE UNDER PRESSURE TO TURN T23-A GREEN WITHOUT DOING THE
# WORK: set bSuppressedByUs to true somewhere other than the site that performs the hide. That is worse
# than a fake pass -- it fabricates an un-hide obligation for a member nobody ever touched, so the
# restore path would un-hide a well that was never hidden, and WELLH2-STRANDED would report a
# suppression that does not exist. This script forbids it by name.
#
# The mirror is checked too, for symmetry: the flag is cleared in exactly one place (the un-hide), and a
# second clearer would silently drop a real obligation -- the failure in the opposite direction.
#
# Exit 0 = the writer census in NodeShuffleSubsystem.h is still true. Exit 1 = it is not.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source\NodeShuffle'

# The sanctioned sites, by file. Named here AND in the field's declaration comment; if you move a site,
# both have to change, which is the point.
$allowedSetTrue = 'NodeShuffleWellRelocateApply.cpp'   # SuppressVanillaWellGroup's HideOne
$allowedClear   = 'NodeShuffleWellUnhide.cpp'          # UnhideWellMember

$fail = $false

# Any assignment of the flag to true. Matches `= true`, `=true`, and the `{ true }` brace form.
$setPattern = 'bSuppressedByUs\s*(=\s*true|\{\s*true\s*\})'
foreach ($hit in (Select-String -Path (Join-Path $src '*\*.cpp') -Pattern $setPattern)) {
    $file = Split-Path -Leaf $hit.Path
    if ($file -ne $allowedSetTrue) {
        Write-Host "FAIL: bSuppressedByUs is set true in $file line $($hit.LineNumber) -- only $allowedSetTrue may do that, and only beneath the hide it records."
        $fail = $true
    }
}

# The clearer. The whole-record reset (`Rec = FNodeShuffleWellSuppressionRecord()`) is the sanctioned
# form and lives in the un-hide; a per-field `= false` anywhere is the smell.
$clearPattern = 'bSuppressedByUs\s*=\s*false'
foreach ($hit in (Select-String -Path (Join-Path $src '*\*.cpp') -Pattern $clearPattern)) {
    $file = Split-Path -Leaf $hit.Path
    if ($file -ne $allowedClear) {
        Write-Host "FAIL: bSuppressedByUs is cleared in $file line $($hit.LineNumber) -- only $allowedClear may discharge the restore obligation."
        $fail = $true
    }
}

# The test pair itself must not be weakened into always-true. A verdict computed from anything other than
# the measured population is the second shortcut.
foreach ($hit in (Select-String -Path (Join-Path $src 'Private\NodeShuffleWellUnhide.cpp') -Pattern 'VerdictA\s*=\s*TEXT|VerdictB\s*=\s*TEXT')) {
    Write-Host "FAIL: a T23 verdict is assigned a literal at line $($hit.LineNumber) -- both verdicts must be computed from the measured population."
    $fail = $true
}

if ($fail) { exit 1 }
Write-Host "OK: the bSuppressedByUs writer census holds (one setter, one clearer) and neither T23 verdict is a literal."
exit 0
