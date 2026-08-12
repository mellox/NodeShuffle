# ns-t59-pack-namespace -- THE LINT FOR THE ONE-LINE EDITS THAT REINTRODUCE CROSS-SESSION PACK CHURN.
#
# T59's fix is structural and small, which is exactly why it is easy to undo by accident. The generated
# SF+ allow-list pack lives in ONE per-INSTALL directory; its documents are namespaced per SESSION
# ("s-<slug>--auto-allow-....cdo.yml") and a pass may delete ONLY files carrying its own prefix. The
# defect this replaced (docs/TECH-DEBT.md T59, measured over three boots) was a whole-directory delete
# driven by a per-save input: loading save B deleted the documents save A needed.
#
# THERE IS NO UNIT TEST HERE TO GO GREEN -- the failure is a FILE DELETE in a live install, and the
# runtime instrument is PACKCHURN's `crossNamespaceRemoved`, which only speaks after someone has already
# played two saves. This lint is the part that speaks at edit time.
#
# THE EDITS IT PINS, each a genuine one-liner someone would write under pressure:
#   1. RE-WIDEN THE SANITIZER TO EMIT '-'. The document prefix separator is "--" and the delete step
#      selects by that prefix, so a '-' inside a slug lets one session's selector match another
#      session's files ("A" would select "A--B"'s). Adding `|| C == TEXT('-')` is the whole regression.
#   2. RESTORE THE WHOLE-DIRECTORY DELETE IN THE PASS. `DeleteDirectory(*PackDir, ..., Tree=true)` is
#      sanctioned in exactly ONE place -- the NodeShuffle.AutoAllowExtractors=0 rollback lever, whose
#      contract IS "remove everything, every session" -- and that site sits BEFORE the identity gate.
#      Any tree delete after the gate is the original defect back.
#   3. DELETE BY AN UNANCHORED LISTING. The per-file delete iterates PriorOwnDocNames, and a name only
#      enters that set under a StartsWith(DocPrefix) test. Swapping the loop to the full directory
#      listing (AllDocFilesBefore) is one identifier and deletes every playthrough's documents.
#   4. DROP THE PREFIX FROM THE WRITTEN FILENAME. Documents then land in the legacy, unowned name space
#      -- and the next pass's prefix-scoped clear cannot remove them, so they accumulate forever.
#   5. DELETE THE REGRESSION DETECTOR. crossNamespaceRemoved / otherNamespaceDocsBefore are what make
#      1-4 visible in one log line instead of another three-boot study; removing them is silent.
#
# IT READS THE FILES ON DISK, NOT `git show` -- same reason as check_t54_lint.ps1: a git-backed read goes
# vacuous on exactly the uncommitted tree it exists to check.
#
# IT STRIPS `//` COMMENTS BEFORE MATCHING, preserving line numbers by blanking rather than removing.
# This file's comments legitimately DISCUSS the forbidden forms ("what used to happen here was
# DeleteDirectory(PackDir, Tree=true)"), and a lint that fires on its own rationale would be deleted
# within a week. Known limitation: a `//` inside a string literal would be blanked too; no such literal
# exists in the pinned file, and the vacuity checks below would fire if one ever swallowed an anchor.
#
# NON-VACUITY IS CHECKED FOR EVERY HALF (check_t23_writers.ps1 cold review F8): each ban is paired with
# an assertion that the thing being protected still EXISTS, so a rename or a deletion cannot turn a ban
# into a silent pass.
#
# SILENT ON PASS. Exit 0 = every pin holds. Exit 1 = at least one does not, one line each naming
# file+pattern+why.
#
# MUTATION TEST (in memory, no files written):  pwsh -File tools\check_t59_lint.ps1 -MutationTest
# It applies each of the five edits above to an in-memory copy of the real file, re-runs the same checks,
# and reports CAUGHT/MISSED per mutant plus a positive control on the unmutated text. It is a diagnostic
# mode and prints on success by design; the default mode stays silent.

param([switch]$MutationTest)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source\NodeShuffle'
$passFile = 'NodeShuffleAutoAllowExtractors.cpp'
$passPath = Join-Path $src ('Private\' + $passFile)

# Blank out // comments, keeping line count and numbering intact.
function Remove-LineComments([string]$text) {
    ($text -split "`n" | ForEach-Object { $_ -replace '//.*$', '' }) -join "`n"
}

function Get-LineNumber([string[]]$lines, [string]$pattern) {
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match $pattern) { return $i + 1 }
    }
    return -1
}

# Returns an array of failure strings. Empty array = every pin holds.
function Get-T59Failures([string]$rawText, [bool]$checkCrossFile) {
    $fails = @()
    $code  = Remove-LineComments $rawText
    $lines = $code -split "`n"

    # ---- HALF 1: the sanitizer must never emit the namespace separator character ----
    foreach ($pat in @("TEXT\(\s*'-'\s*\)", "==\s*'-'")) {
        $ln = Get-LineNumber $lines $pat
        if ($ln -ne -1) {
            $fails += "FAIL: $passFile line $ln contains a hyphen CHARACTER literal, matching /$pat/ -- the session slug charset must stay [A-Za-z0-9_]. A '-' inside a slug makes one session's 's-<slug>--' selector a prefix of another's, and this pass then deletes another playthrough's documents."
        }
    }
    # NON-VACUITY: the sanitizer and its keep-set must still exist for that ban to mean anything.
    foreach ($anchor in @('MakeSessionSlug', "C == TEXT\('_'\)")) {
        if ($code -notmatch $anchor) {
            $fails += "FAIL: $passFile no longer contains /$anchor/ -- the slug sanitizer has gone, so the hyphen ban above is vacuous. Session identity must still be folded to a filesystem-safe charset."
        }
    }
    if ($code -notmatch 'TEXT\("s-%s--"\)') {
        $fails += "FAIL: $passFile no longer builds the document prefix as /TEXT(""s-%s--"")/ -- the prefix form is what the delete step selects on and what tells a namespaced document from a legacy one. Changing it silently orphans every existing document."
    }

    # ---- HALF 2 (REWRITTEN BY T68, 2026-08-11): ZERO directory-tree deletes. ----
    # T59's contract was "exactly one tree delete, and only in the CVar=0 rollback lever". T68 deleted
    # NodeShuffle.AutoAllowExtractors and that lever with it (audit §5.13), so the sanctioned site no
    # longer exists and the SAFE count is now ZERO. THE INVARIANT THIS HALF PROTECTS IS UNCHANGED and is
    # in fact stronger: a tree delete anywhere in this file removes every playthrough's documents, which
    # is the T59 defect. The old pins are not weakened -- their exception is retired with its feature.
    $dirDeleteLines = @()
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match 'DeleteDirectory\s*\(') { $dirDeleteLines += ($i + 1) }
    }
    $gateLine = Get-LineNumber $lines 'MakeSessionSlug\s*\(\s*SessionName\s*\)'
    if ($dirDeleteLines.Count -ne 0) {
        $fails += "FAIL: $passFile has $($dirDeleteLines.Count) DeleteDirectory call(s) (lines $($dirDeleteLines -join ', ')) -- T68 removed the only sanctioned one with the AutoAllowExtractors CVar, so a tree delete here now removes every playthrough's documents with nothing to roll it back."
    }
    # NON-VACUITY: half 2 is a count of zero, which a deleted FILE would also satisfy. The identity gate
    # must still be visible, or this half is asserting nothing about a file it can no longer see.
    if ($gateLine -eq -1) {
        $fails += "FAIL: $passFile no longer contains the identity gate /MakeSessionSlug(SessionName)/ -- half 2 has gone vacuous."
    }
    # T68: and the deleted CVar must not be resurrected here by name.
    if (($lines -join "`n") -match 'GNodeShuffleAutoAllowExtractors') {
        $fails += "FAIL: $passFile references GNodeShuffleAutoAllowExtractors -- T68 deleted that console variable and the pass now always runs."
    }

    # ---- HALF 3: every FILE delete is selected by this session's prefix ----
    $fileDeleteLines = @()
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '\.\s*Delete\s*\(') { $fileDeleteLines += ($i + 1) }
    }
    if ($fileDeleteLines.Count -eq 0) {
        $fails += "FAIL: $passFile performs no per-file delete at all -- this check has gone vacuous, and the pass can no longer replace its own stale documents."
    }
    foreach ($ln in $fileDeleteLines) {
        $from = [Math]::Max(0, $ln - 11)
        $window = ($lines[$from..($ln - 1)]) -join "`n"
        if ($window -notmatch 'for\s*\(\s*const\s+FString&\s+\w+\s*:\s*PriorOwnDocNames\s*\)') {
            $fails += "FAIL: $passFile line $ln deletes a file that was NOT selected from PriorOwnDocNames (no such loop header in the 10 lines above it) -- every delete must iterate the prefix-filtered set. Deleting from an unfiltered directory listing removes other playthroughs' documents."
        }
    }
    # The set itself may only be filled under the prefix test -- one Add, immediately under StartsWith.
    $addLines = @()
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match 'PriorOwnDocNames\s*\.\s*Add\s*\(') { $addLines += ($i + 1) }
    }
    if ($addLines.Count -ne 1) {
        $fails += "FAIL: $passFile has $($addLines.Count) writes to PriorOwnDocNames (expected exactly 1) -- that set is the delete list, and every entry must come from the single prefix test. More than one filling site, or none, breaks the guarantee that only this session's files can be deleted."
    }
    foreach ($ln in $addLines) {
        $from = [Math]::Max(0, $ln - 4)
        $window = ($lines[$from..($ln - 1)]) -join "`n"
        if ($window -notmatch 'StartsWith\s*\(\s*DocPrefix\s*,') {
            $fails += "FAIL: $passFile line $ln adds to the delete list without a StartsWith(DocPrefix, ...) test in the 3 lines above it -- a name that did not prove it belongs to this session must never reach the delete loop."
        }
    }

    # ---- HALF 4: written document names carry the session prefix ----
    foreach ($pat in @('TEXT\("%sauto-allow-%s\.cdo\.yml"\)\s*,\s*\*DocPrefix',
                       'TEXT\("%sauto-allow-%s-%s\.cdo\.yml"\)\s*,\s*\*DocPrefix')) {
        if ($code -notmatch $pat) {
            $fails += "FAIL: $passFile no longer writes a document name matching /$pat/ -- both the vanilla and the mod-owned filename forms must be prefixed with DocPrefix. An unprefixed document lands in the legacy name space, which no session owns and no pass can ever clear."
        }
    }
    $ln = Get-LineNumber $lines 'TEXT\("auto-allow-'
    if ($ln -ne -1) {
        $fails += "FAIL: $passFile line $ln writes a document name beginning literally 'auto-allow-' with no session prefix -- that is the pre-T59, un-namespaced form. It accumulates forever because the prefix-scoped clear cannot select it."
    }

    # ---- HALF 5: the runtime regression detector must survive ----
    foreach ($anchor in @('crossNamespaceRemoved', 'otherNamespaceDocsBefore', 'ForeignDocNamesBefore', 'AUTOALLOW PACKCHURN')) {
        if ($code -notmatch [regex]::Escape($anchor)) {
            $fails += "FAIL: $passFile no longer contains '$anchor' -- PACKCHURN's cross-name-space half is the only runtime instrument that shows a regression to the T59 defect, and its zero needs its denominator on the same line. Removing either is silent."
        }
    }

    # ---- HALF 6: the pack path must stay private to this file, or a single-file lint is not enough ----
    if ($checkCrossFile) {
        $refs = @(Select-String -Path (Join-Path $src '*\*.cpp'), (Join-Path $src '*\*.h') -Pattern 'GetAutoAllowPackDir' -ErrorAction SilentlyContinue)
        $files = @($refs | ForEach-Object { Split-Path -Leaf $_.Path } | Sort-Object -Unique)
        if ($files.Count -eq 0) {
            $fails += "FAIL: GetAutoAllowPackDir is referenced nowhere under Source\NodeShuffle -- this lint scans one file on the strength of that symbol being file-local, so its disappearance makes every half above scope-blind."
        } elseif ($files.Count -gt 1 -or $files[0] -ne $passFile) {
            $fails += "FAIL: the generated pack directory is reached from $($files -join ', ') -- it must stay private to $passFile. Once another file can compute that path, a delete can live outside everything this lint reads."
        }
    }

    return $fails
}

$raw = Get-Content -Raw -Path $passPath

if (-not $MutationTest) {
    $fails = Get-T59Failures $raw $true
    foreach ($f in $fails) { Write-Host $f }
    if ($fails.Count -gt 0) { exit 1 }
    exit 0
}

# ---------------- MUTATION TEST (in memory; nothing is written to disk) ----------------
# Each mutant is the one-line edit named in the header. A mutant that is not CAUGHT means the pin for it
# does not work -- report it and exit nonzero, because an uncaught mutant is a lint that lies.
$control = Get-T59Failures $raw $true
Write-Host ("CONTROL (unmutated tree): {0}" -f $(if ($control.Count -eq 0) { 'PASS (0 failures) -- as required' } else { "UNEXPECTEDLY FAILING with $($control.Count):`n  " + ($control -join "`n  ") }))

# FIRST-OCCURRENCE replace. Two mutants must hit ONE specific site (the delete loop) while the same text
# legitimately appears later in the census; a blanket .Replace would mutate the census too and the
# mutant would then be proving something other than what it names. Line endings are never embedded in a
# Find string for the same reason a `git show` read is refused elsewhere in tools\: the file is LF today
# and a CRLF checkout must not silently turn every mutant into "NOT APPLIED".
function Replace-First([string]$text, [string]$find, [string]$repl) {
    $i = $text.IndexOf($find)
    if ($i -lt 0) { return $null }
    return $text.Substring(0, $i) + $repl + $text.Substring($i + $find.Length)
}

$mutants = @(
    @{ Name = 'M1 re-widen the sanitizer to emit a hyphen';
       Find = "|| C == TEXT('_');"; Repl = "|| C == TEXT('_') || C == TEXT('-');" },
    @{ Name = 'M2 restore the whole-directory delete in the regeneration path';
       Find = "    int32 OwnDocsDeleted = 0;"; Repl = "    FM.DeleteDirectory(*PackDir, false, true);`n    int32 OwnDocsDeleted = 0;" },
    @{ Name = 'M3 delete from the unfiltered directory listing (delete loop only)';
       Find = "for (const FString& N : PriorOwnDocNames)"; Repl = "for (const FString& N : AllDocFilesBefore)" },
    @{ Name = 'M4 drop the session prefix from the written filename';
       Find = 'TEXT("%sauto-allow-%s.cdo.yml"), *DocPrefix, '; Repl = 'TEXT("auto-allow-%s.cdo.yml"), ' },
    @{ Name = 'M5 remove the cross-name-space regression counter';
       Find = 'crossNamespaceRemoved %d'; Repl = 'removed %d' }
)

$missed = 0
foreach ($m in $mutants) {
    $mutated = Replace-First $raw $m.Find $m.Repl
    if ($null -eq $mutated) {
        Write-Host ("{0}: NOT APPLIED -- the text it edits was not found. The mutation test is stale, which means it is proving nothing." -f $m.Name)
        $missed++
        continue
    }
    $f = Get-T59Failures $mutated $true
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
