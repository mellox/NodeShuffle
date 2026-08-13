# T68 (ns-t68-release-config, 2026-08-11) -- THE RELEASE-CONFIG LINT.
#
# T68 deleted seven user-facing switches and hard-wired what each of them decided. Every one of those
# hard-wirings is a value that USED to be a variable, which is exactly the shape that gets quietly
# re-introduced as a "safer" config read six months later, and the shape that gets half-reverted.
# This script pins the seven, plus the two new mechanisms T68 added.
#
# WHAT IS PINNED, AND WHY EACH ONE IS A LINT RATHER THAN A COMMENT:
#   H1  The five deleted panel properties are ABSENT from the config registration AND from the struct
#       mirror. A leftover in either place is a row that persists to NodeShuffle.cfg and reads nothing,
#       or a struct field nothing fills -- the two halves must go together (SYMMETRY).
#       AND: a deleted option's NAME must not survive as a tooltip fragment anywhere on the panel. A pin
#       satisfied by leftover prose is the pin failing silently.
#   H2  Both compatibility-notice call sites pass the LITERAL true. One literal and one config read is
#       the asymmetry this rule exists to catch.
#   H3  The re-roll polarity is hard-wired ON, as a constant, in the roll.
#   H4  The checkbox<->CVar coupling: one resolver, precedence by SetBy, the checkbox as fallback, and
#       the arm pass reading the resolver rather than the raw CVar.
#   H5  The row sort runs at BOTH populators, and at each one it runs AFTER the row work (the add path
#       withdraws by position -- a sort inside that loop would remove the wrong row).
#   H6  The retired T23 pair stays retired: no emitter, no key, no lint script.
#   H8  ADDED BY THE F4 ROUND: the generic label, its Satisfactory Plus tooltip opening, and a
#       SYMMETRY pin that no TEXT() literal anywhere still carries the old label.
#   H1c ADDED BY THE COLD REVIEW (F3): the deleted identifiers must not survive as CONTRACT TEXT in the
#       public header or the apply path either. H1b guarded the panel; the next author reads the .h.
#       (Scoped re-pass R3: Public\NodeShuffle.h -- the .h that carried F3's defect -- is now in the set.)
#
# Exit 0 = all nine hold. Exit 1 = at least one does not. Exit 2 = a pinned FILE is missing, i.e. the
# suite could not even load what it claims to check (a suite that silently skips a file is the vacuous
# pass this repo has shipped twice).

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'

$files = @{
    cfg    = 'NodeShuffle\Private\NodeShuffleConfig.cpp'
    cfgh   = 'NodeShuffle\Public\NodeShuffleConfig.h'
    main   = 'NodeShuffle\Private\NodeShuffle.cpp'
    subsys = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    roll   = 'NodeShuffle\Private\NodeShuffleWellRelocateRoll.cpp'
    prot   = 'NodeShuffle\Private\NodeShuffleForeignProtectConfig.cpp'
    unhide = 'NodeShuffle\Private\NodeShuffleWellUnhide.cpp'
    apply  = 'NodeShuffle\Private\NodeShuffleWellRelocateApply.cpp'
    # SCOPED RE-PASS R3: Public\NodeShuffle.h is THE FILE H1c WAS WRITTEN FOR -- F3's defect #1 was
    # a TODO(pre-release) in it -- and it was in no key, so the pin could not reach the file whose
    # defect created it. That is this repo's named vacuous-pass class, occurring inside the artifact
    # written to prevent it. Adding it immediately surfaced a SEVENTH stale mention at :338.
    pubh   = 'NodeShuffle\Public\NodeShuffle.h'
    # T71 (2026-08-12): the three well files whose gates were hard-wired, plus the subsystem header
    # that declares them. H1c must reach them or its stale-mention scan is blind exactly where T71
    # deleted contract text.
    wellroll = 'NodeShuffle\Private\NodeShuffleWellRoll.cpp'
    retype   = 'NodeShuffle\Private\NodeShuffleWellRetype.cpp'
    sweep    = 'NodeShuffle\Private\NodeShuffleWellSweep.cpp'
    subsysh  = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
}

# EVERY pinned file is loaded up front and a missing one is a HARD exit, never a skipped check.
$text = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) {
        Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist -- this suite cannot check what it cannot read."
        exit 2
    }
    $text[$k] = Get-Content -Raw -Path $p
}

$fails = @()

# ---- H1: the five deleted properties are gone from BOTH the registration and the struct mirror ----
# Matched on the KEY as it would be registered / declared, not on loose prose, so the comments that
# record the deletion (which necessarily name them) do not trip it.
# T71 (2026-08-12) EXTENDS THIS LIST rather than starting a parallel one: H1 asks exactly the T71
# question too (is a deleted key still registered / still mirrored), and a second suite asking it in a
# second file is how two pins drift apart. The T71-specific pins -- the hard-wire anchors and the doc
# sync -- live in tools/check_t71_lint.ps1 because they have no T68 analogue.
$deleted = @('EnableExperimentalFeatures', 'CommitWellsAtRoll', 'ShowCompatibilityNotices',
             'AllowVanillaDisappear', 'RerollRelocatedWells',
             'ShuffleResourceWells', 'RelocateResourceWells')
foreach ($d in $deleted) {
    if ($text.cfg -match ('Add(Bool|Int)\(TEXT\("' + $d + '"\)')) {
        $fails += "FAIL(H1): $($files.cfg) still REGISTERS the panel property '$d' -- T68 deleted it; a re-registered key persists to NodeShuffle.cfg and is read by nothing."
    }
    if ($text.cfgh -match ('(bool|int32)\s+' + $d + '\s*\{')) {
        $fails += "FAIL(H1): $($files.cfgh) still DECLARES the struct mirror field '$d' -- the schema and the mirror must be deleted together, or FillConfigurationStruct fills a field nothing writes."
    }
}
# H1b: NO DELETED OPTION'S DISPLAY NAME MAY SURVIVE IN PANEL COPY. This is the "a pin must never be
# satisfied by a leftover tooltip fragment" rule, applied in the one direction that can actually happen:
# a tooltip that still tells the player to use an option that is no longer on the page.
$deadCopy = @("Re-roll Wells That Have Already Moved", "Remove A Moved Well At The Re-roll Itself",
              "Remove A Moved Well Immediately", "Allow Vanilla Nodes To Disappear",
              "Show Compatibility Notices In Chat", "Enable Experimental Features",
              "Shuffle Resource Wells", "Relocate Resource Wells")
foreach ($phrase in $deadCopy) {
    foreach ($ln in (Get-Content -Path (Join-Path $src $files.cfg))) {
        if ($ln -match 'TEXT\("' -and $ln -like "*$phrase*") {
            $fails += "FAIL(H1b): $($files.cfg) still shows the player the deleted option name '$phrase' in panel copy -- the option is gone, so the sentence points at nothing."
            break
        }
    }
}

# ---- H1c (ADDED BY THE T68 COLD REVIEW, F3): THE PIN MUST REACH THE FILE THE NEXT AUTHOR READS. ----
# H1b above scans NodeShuffleConfig.cpp ONLY. The review found FIVE surviving comments naming deleted
# switches as live behaviour -- one a TODO(pre-release) in the PUBLIC header NodeShuffle.h, i.e. that
# function's contract text -- and NOT ONE was reachable by any pin, because every tombstone lives in the
# .cpp and the person who will get this wrong is reading the .h. That is
# lessons-file-the-rule-where-the-author-works, and a pin that cannot reach the file it guards is this
# repo's known vacuous-pass class. The identifier scan is widened to the four files carrying contract
# text about the deleted switches -- FIVE files since the scoped re-pass (R3 added Public\NodeShuffle.h,
# which is the one whose defect motivated the whole half).
#
# COMMENTS ARE IN SCOPE HERE, unlike H4's non-comment scan, and that is the point: A HEADER COMMENT IS
# CONTRACT TEXT, NOT A TOMBSTONE. The exemption is therefore explicit and narrow -- a line may name a
# deleted identifier only if it also carries the "T68" marker, which a dated deletion note has and a
# stale contract comment does not.
# THE IDENTIFIERS ARE THE DELETED THINGS' FULL, UNAMBIGUOUS NAMES. A bare 'ProtectForeignNodes' also
# matches the veto module's LIVE parameter `bProtectForeignNodes` (its ABI, not a deleted switch), and a
# bare 'AutoAllowExtractors' matches the live `LogAutoAllowExtractorsState`. A pin that fires on live
# code gets deleted rather than fixed, which is how a suite dies -- so each entry names the CVar or the
# accessor exactly.
$deadIdents = @('EnableExperimentalFeatures', 'CommitWellsAtRoll', 'RerollRelocatedWells',
                'AllowVanillaDisappear', 'ShowCompatibilityNotices',
                'ShuffleResourceWells', 'RelocateResourceWells',
                'NodeShuffle.AutoAllowExtractors', 'GNodeShuffleAutoAllowExtractors',
                'NodeShuffle.ProtectForeignNodes', 'IsForeignNodeProtectionEnabled',
                'TODO(pre-release)')
# THE EXEMPTION IS A 6-LINE WINDOW, NOT THE LINE ITSELF. A tombstone is a PARAGRAPH: the dated "T68"
# marker sits on its first line and the identifier it retires can be named five or six lines further
# down (the two deletion notes in NodeShuffleConfig.cpp are 4 and 5 lines from their marker -- measured,
# which is why the window is 6 and not 3). A per-line test fired on every deletion note this packet
# deliberately wrote, and a pin that cannot tell a tombstone from a stale contract comment is a pin
# someone deletes rather than fixes. Six lines is a comment paragraph; it is not a licence to leave a
# stale contract sentence somewhere near a T68 note, because H1c's whole population is FOUR files whose
# T68 notes are all in one block each.
# T71: the marker is now 'T68' OR 'T71'. A T71 deletion note is a tombstone by exactly the same
# argument -- dated, deliberate, and naming what it retired -- and leaving the window T68-only would have
# made every T71 note fail H1c, which is the shape that gets a suite deleted rather than fixed.
function Test-T68Window([string[]]$Lines, [int]$Index) {
    $lo = [Math]::Max(0, $Index - 6)
    $hi = [Math]::Min($Lines.Count - 1, $Index + 6)
    for ($w = $lo; $w -le $hi; $w++) { if ($Lines[$w] -match 'T68|T71') { return $true } }
    return $false
}
# T71: the five files T71 edited are added -- a green suite says nothing about a file it never reads
# (2026-08-11 lint rule, x3).
# T71 COLD REVIEW F4: 'unhide' was in $files but omitted from THIS scan loop -- loaded and never read,
# which is the ledgered defect wearing its quietest hat.
foreach ($k in @('cfg', 'cfgh', 'main', 'apply', 'pubh', 'roll', 'subsys', 'wellroll', 'retype', 'sweep', 'subsysh', 'unhide')) {
    $h1cLines = @(Get-Content -Path (Join-Path $src $files[$k]))
    for ($i = 0; $i -lt $h1cLines.Count; $i++) {
        if (Test-T68Window $h1cLines $i) { continue }
        foreach ($ident in $deadIdents) {
            if ($h1cLines[$i].Contains($ident)) {
                $fails += "FAIL(H1c): $($files[$k]) line $($i + 1) names the deleted '$ident' outside any T68 deletion note -- a contract comment describing a switch this packet removed will send the next author to re-add it."
            }
        }
    }
}
# NON-VACUITY, TWO WAYS. (a) H1c must be able to SEE a deleted identifier at all -- if none of them
# appears anywhere in the four files, the scan is reading the wrong paths and would pass by finding
# nothing. (b) The window exemption must actually be exempting something, or it is not being exercised.
$h1cSawIdent = $false
$h1cSawExempt = $false
foreach ($k in @('cfg', 'cfgh', 'main', 'apply', 'pubh')) {
    $ls = @(Get-Content -Path (Join-Path $src $files[$k]))
    for ($i = 0; $i -lt $ls.Count; $i++) {
        foreach ($ident in $deadIdents) {
            if ($ls[$i].Contains($ident)) {
                $h1cSawIdent = $true
                if (Test-T68Window $ls $i) { $h1cSawExempt = $true }
            }
        }
    }
}
if (-not $h1cSawIdent) {
    $fails += "FAIL(H1c): not one deleted identifier appears anywhere in the five scanned files -- this half would pass by seeing nothing, which is the vacuous pass it exists to prevent."
}
if (-not $h1cSawExempt) {
    $fails += "FAIL(H1c): the T68-window exemption never fired -- the deletion notes this packet wrote should each trip it, so the window logic is not doing what it claims."
}

# ---- H8 (F4, author's decision 2026-08-11): THE LABEL, ITS TOOLTIP OPENING, AND EVERY QUOTE OF IT ----
# The author replaced "Protect Other Mods' Nodes From Removal" with the generic "Protect Other Mods'
# Nodes" and moved the concrete case into the tooltip, because the cold review graded the old label as
# an outcome claim T67 established this feature may not make. Three things are pinned:
#   (a) the label is exactly the generic string;
#   (b) the tooltip still OPENS with the concrete case naming Satisfactory Plus -- the half of the
#       trade carrying the meaning the generic label gave up, and the sentence most likely to be
#       trimmed later by an editor who does not know why it is there;
#   (c) SYMMETRY: the OLD label appears in NO TEXT() literal ANYWHERE. It was quoted in four other
#       places (the CVar help, the protection-list tooltip, the chat notice, the veto module's log
#       line); a label quoted differently in two places is this workspace's most-repeated defect
#       class, so the scan runs over every pinned file, not just the panel.
if ($text.cfg -notmatch [regex]::Escape('TEXT("Protect Other Mods'' Nodes"),')) {
    $fails += "FAIL(H8a): $($files.cfg) no longer registers the checkbox label as the generic ""Protect Other Mods' Nodes"" -- the author chose that wording on 2026-08-11 precisely because the old one asserted an outcome (T67)."
}
if ($text.cfg -notmatch [regex]::Escape('Satisfactory Plus is the known case')) {
    $fails += "FAIL(H8b): $($files.cfg) no longer opens the checkbox tooltip with the concrete Satisfactory Plus case -- that sentence is the half of the F4 trade that carries the meaning the generic label gave up."
}
foreach ($k in $text.Keys) {
    foreach ($ln in ($text[$k] -split "`r?`n")) {
        if ($ln -match 'TEXT\(' -and $ln -match 'Nodes From Removal') {
            $fails += "FAIL(H8c): $($files[$k]) still ships the OLD label ""...Nodes From Removal"" in a TEXT() literal -- every quote site must render the label the panel actually shows."
        }
    }
}

# ---- H2: both notice call sites pass the literal true ----
foreach ($site in @('EmitPendingNoticeIfReady\(/\*bNoticesEnabled=\*/true\)',
                    'TickForeignNoticeEmitter\(GetWorld\(\), /\*bNoticesEnabled=\*/true\)')) {
    if ($text.subsys -notmatch $site) {
        $fails += "FAIL(H2): $($files.subsys) no longer passes the literal true at the notice call site /$site/ -- both sites must be hard-wired, and hard-wired the SAME way; one literal and one config read is how the two notices come to disagree."
    }
}
if ($text.subsys -match 'Config\.ShowCompatibilityNotices') {
    $fails += "FAIL(H2): $($files.subsys) reads Config.ShowCompatibilityNotices -- that property was deleted; the struct field does not exist and the read would not compile, but it must not come back as a re-added key either."
}

# ---- H3: the re-roll polarity is a hard-wired ON constant ----
if ($text.roll -notmatch 'static\s+constexpr\s+bool\s+bRerollRelocated\s*=\s*true\s*;') {
    $fails += "FAIL(H3): $($files.roll) no longer hard-wires /static constexpr bool bRerollRelocated = true/ -- T68 fixed the polarity ON deliberately (a re-roll re-rolls moved wells too); a config read or a false here silently restores the pre-T68 behaviour."
}
# CODE FORM ONLY -- the struct-member access. The comments that RECORD the deletion necessarily name
# both keys, and a pin that fired on its own rationale would be deleted rather than fixed.
if ($text.roll -match '\.RerollRelocatedWells|\.CommitWellsAtRoll') {
    $fails += "FAIL(H3): $($files.roll) reads a deleted well toggle -- both RerollRelocatedWells and CommitWellsAtRoll are gone."
}

# ---- H4: the checkbox <-> CVar coupling ----
if ($text.main -notmatch 'static\s+bool\s+NodeShuffleResolveDestroyerVetoRequested') {
    $fails += "FAIL(H4): $($files.main) no longer defines the single resolver NodeShuffleResolveDestroyerVetoRequested -- with two copies of the precedence question, the latch's readers can disagree."
}
if ($text.main -notmatch 'ECVF_SetByMask\)\s*!=\s*ECVF_SetByConstructor') {
    $fails += "FAIL(H4): $($files.main) no longer decides the console override by SetBy priority -- comparing the CVar's VALUE to its default cannot tell an untouched variable from a deliberate 'set it to 0', so a player who typed 0 would be overridden by the checkbox."
}
if ($text.main -notmatch 'GetActiveConfig\(WorldContext\)\.ProtectOtherModsNodes') {
    $fails += "FAIL(H4): $($files.main) no longer falls back to the ProtectOtherModsNodes checkbox -- the persisted setting is the source of truth whenever the CVar was never set."
}
if ($text.main -notmatch 'const\s+bool\s+bObserveOnly\s*=\s*\(\s*bVetoRequested\s*==\s*false\s*\)') {
    $fails += "FAIL(H4): $($files.main) no longer derives bObserveOnly from the RESOLVED gate -- reading the raw CVar again would ignore the checkbox entirely."
}
if ($text.cfg -notmatch 'AddBool\(TEXT\("ProtectOtherModsNodes"\), true,') {
    $fails += "FAIL(H4): $($files.cfg) no longer registers ProtectOtherModsNodes with default TRUE -- the default is the behaviour change T68 shipped and the changelog claims."
}
if ($text.cfgh -notmatch 'bool\s+ProtectOtherModsNodes\{true\}') {
    $fails += "FAIL(H4): $($files.cfgh) struct mirror default for ProtectOtherModsNodes is not true -- the schema default and the mirror default must agree, or an unfilled struct silently disables protection."
}
# The deleted CVar must not return in any module.
# NON-COMMENT LINES ONLY, for the same reason as H3 above: the T68 tombstone comment names the deleted
# accessor on purpose.
foreach ($k in @('main','cfg')) {
    foreach ($ln in ($text[$k] -split "`r?`n")) {
        if ($ln -match '^\s*(//|\*|/\*)') { continue }
        if ($ln -match 'IsForeignNodeProtectionEnabled\s*\(') {
            $fails += "FAIL(H4): $($files[$k]) calls IsForeignNodeProtectionEnabled -- T68 deleted NodeShuffle.ProtectForeignNodes and that accessor with it."
            break
        }
    }
}

# ---- H5: the sort runs at BOTH populators, and last at each ----
$sortCalls = @([regex]::Matches($text.prot, 'NodeShuffleSortForeignResourceRows\(Rows,\s*TEXT\("([^"]+)"\)\)') | ForEach-Object { $_.Groups[1].Value })
foreach ($expected in @('menu-stamp', 'in-world-sync')) {
    if ($sortCalls -notcontains $expected) {
        $fails += "FAIL(H5): $($files.prot) does not sort the rows at the '$expected' call site -- BOTH populators must sort, or the in-world panel and the main-menu panel show the same file in different orders."
    }
}
if ($text.prot -notmatch 'Rows->Values\.Sort\(') {
    $fails += "FAIL(H5): $($files.prot) no longer sorts Rows->Values at all -- the helper has gone vacuous and the call sites above prove nothing."
}
# ORDER WITHIN THE SYNC PASS: the sort must come AFTER the add loop's positional withdrawal.
$protLines   = Get-Content -Path (Join-Path $src $files.prot)
$withdrawLn  = -1; $syncSortLn = -1
for ($i = 0; $i -lt $protLines.Count; $i++) {
    if ($protLines[$i] -match 'RemoveElementAtIndex\(Rows->Values\.Num\(\) - 1\)') { $withdrawLn = $i + 1 }
    if ($protLines[$i] -match "NodeShuffleSortForeignResourceRows\(Rows, TEXT\(`"in-world-sync`"\)\)") { $syncSortLn = $i + 1 }
}
if ($withdrawLn -eq -1 -or $syncSortLn -eq -1) {
    $fails += "FAIL(H5): $($files.prot) is missing either the positional withdrawal or the in-world sort call -- the ordering check below cannot run, so this half is vacuous."
} elseif ($syncSortLn -lt $withdrawLn) {
    $fails += "FAIL(H5): $($files.prot) sorts at line $syncSortLn, BEFORE the positional withdrawal at line $withdrawLn -- RemoveElementAtIndex(Num()-1) then names a different row and the sort deletes the wrong one."
}
foreach ($f in @('rowsUnreadable %d', 'rowsWithBlankPath %d')) {
    if ($text.prot -notmatch [regex]::Escape($f)) {
        $fails += "FAIL(H5/F5): $($files.prot) no longer prints '$f' -- the cold review split an unreadable ELEMENT from a blank PATH because one name over two causes is what makes a reader stop looking; re-merging them undoes that."
    }
}
if ($text.prot -notmatch 'T68ROWSORT: callSite %s rowsInArray %d rowsChangedPosition %d') {
    $fails += "FAIL(H5): $($files.prot) no longer emits the T68ROWSORT census with its three measured counts -- a reorder nobody can see in the log is a reorder nobody can debug."
}

# ---- H6: the T23 pair stays retired ----
foreach ($k in @('unhide','apply')) {
    if ($text[$k] -match 'EmitWellRollHideTestPair\(') {
        $fails += "FAIL(H6): $($files[$k]) calls or defines EmitWellRollHideTestPair -- the T23 opposite-polarity pair was retired WITH its feature; re-adding it against a deleted toggle makes it read VACUOUS forever, which is a vacuous pass."
    }
}
if (Test-Path (Join-Path $root 'tools\check_t23_writers.ps1')) {
    $fails += "FAIL(H6): tools/check_t23_writers.ps1 is back -- it lints a test pair that no longer exists."
}

# ------------------------------------------------------------------------------------------------
# MUTATION TEST. Every pin above must FAIL on a revert of the change it protects. A pin that survives
# its own mutant is decorative.
# ------------------------------------------------------------------------------------------------
# SCOPED RE-PASS R4: THE GRADER MUST USE THE PIN'S OWN EXEMPTION, NOT A RE-IMPLEMENTATION OF IT.
# The 'cfgh' arm re-implemented H1c's window as a PER-LINE `if ($ln -match 'T68') { continue }`. A mutant
# inserted six lines from a T68 marker would then be reported CAUGHT while the real pin skipped it --
# latent today (verified: M12's anchor has no marker within the window) and silently wrong the moment
# anyone adds a T68 note near it. One helper, calling Test-T68Window, so the grade cannot drift from the
# pin it grades.
function Test-H1cOnText([string]$Text) {
    $ls = @($Text -split "`r?`n")
    for ($i = 0; $i -lt $ls.Count; $i++) {
        if (Test-T68Window $ls $i) { continue }
        foreach ($ident in $deadIdents) { if ($ls[$i].Contains($ident)) { return $true } }
    }
    return $false
}

function Replace-First([string]$Text, [string]$Find, [string]$Repl) {
    $i = $Text.IndexOf($Find)
    if ($i -lt 0) { return $null }
    return $Text.Substring(0, $i) + $Repl + $Text.Substring($i + $Find.Length)
}

# No Find string ends at a line boundary: the tree is CRLF and a trailing `\n` would silently turn a
# mutant into "NOT APPLIED" -- a test that proves nothing (check_t59_lint's note, same trap).
$mutants = @(
    @{ Name = 'M1 re-register the dead experimental flag'; Key = 'cfg';
       Find = '    AddBool(TEXT("ProtectOtherModsNodes"), true,';
       Repl = '    AddBool(TEXT("EnableExperimentalFeatures"), false, TEXT("x"), TEXT("y")); AddBool(TEXT("ProtectOtherModsNodes"), true,' },
    @{ Name = 'M2 restore the struct mirror field for a deleted key'; Key = 'cfgh';
       Find = '    bool ProtectOtherModsNodes{true};';
       Repl = '    bool CommitWellsAtRoll{false};' },
    @{ Name = 'M3 leave a deleted option named in panel copy'; Key = 'cfg';
       Find = 'TEXT("Master switch. When off, NodeShuffle does nothing';
       Repl = 'TEXT("See Re-roll Wells That Have Already Moved. Master switch. When off, NodeShuffle does nothing' },
    @{ Name = 'M4 make one notice site a config read again'; Key = 'subsys';
       Find = 'EmitPendingNoticeIfReady(/*bNoticesEnabled=*/true)';
       Repl = 'EmitPendingNoticeIfReady(Config.ShowCompatibilityNotices)' },
    @{ Name = 'M5 flip the hard-wired re-roll polarity'; Key = 'roll';
       Find = 'static constexpr bool bRerollRelocated = true;';
       Repl = 'static constexpr bool bRerollRelocated = false;' },
    @{ Name = 'M6 decide the console override by VALUE instead of SetBy'; Key = 'main';
       Find = '(Var->GetFlags() & ECVF_SetByMask) != ECVF_SetByConstructor';
       Repl = 'Var->GetInt() != 0' },
    @{ Name = 'M7 drop the checkbox fallback (console-only gate again)'; Key = 'main';
       Find = '    return FNodeShuffleConfigStruct::GetActiveConfig(WorldContext).ProtectOtherModsNodes;';
       Repl = '    return false;' },
    @{ Name = 'M8 default the new checkbox OFF'; Key = 'cfg';
       Find = '    AddBool(TEXT("ProtectOtherModsNodes"), true,';
       Repl = '    AddBool(TEXT("ProtectOtherModsNodes"), false,' },
    @{ Name = 'M9 sort at one populator only'; Key = 'prot';
       Find = 'NodeShuffleSortForeignResourceRows(Rows, TEXT("menu-stamp"));';
       Repl = '// sort removed' },
    @{ Name = 'M10 hollow out the sort helper'; Key = 'prot';
       Find = '    Rows->Values.Sort(';
       Repl = '    if (false) Rows->Values.SortStable(' },
    @{ Name = 'M11 bring the retired T23 pair back'; Key = 'apply';
       Find = '    EmitWellStrandedCensus();';
       Repl = '    EmitWellStrandedCensus(); EmitWellRollHideTestPair(false);' },
    # ADDED BY THE COLD REVIEW ROUND. M12 reproduces the EXACT defect F3 found -- a stale contract
    # comment in a file H1b never scanned. M13 re-merges the two census populations F5 split.
    # RE-POINTED BY THE SCOPED RE-PASS (R3). M12 used to insert into NodeShuffleConfig.h -- also a public
    # header, so the handoff's sentence was not false, but it DID NOT REPRODUCE F3'S SITE and no mutant
    # covered the file that carried the defect. It now lands in Public\NodeShuffle.h, on the contract
    # comment of the very function whose gate F3 found stale.
    @{ Name = 'M12 (F3) stale pre-release TODO in Public\NodeShuffle.h, F3 defect #1 site'; Key = 'pubh';
       # ANCHOR CHOICE (the one authored detail in this round -- the re-pass specified the FILE, not
       # the line). It must sit OUTSIDE any T68 window, or the mutant is exempted by the pin's own
       # rule and reported SURVIVED for a reason that is not a pin weakness. MEASURED: this file's
       # T68 markers are at 297 / 338 / 422, and ResetSeenForeignResources' declaration at :359 is a
       # public contract comment more than 6 lines from all three. That is R5's documented
       # limitation being navigated, not evaded -- a mutant planted inside a tombstone tests nothing.
       Find = '    static void ResetSeenForeignResources();';
       Repl = '    // Gated by NodeShuffle.AutoAllowExtractors; TODO(pre-release): move behind EnableExperimentalFeatures.' + [Environment]::NewLine + '    static void ResetSeenForeignResources();' },
    @{ Name = 'M13 (F5) re-conflate the two census populations'; Key = 'prot';
       Find = 'rowsUnreadable %d '; Repl = 'rowsWithNoReadablePath %d ' },
    # F4 ROUND. M14 reverts the author's label decision. M15 is the SYMMETRY failure the review named
    # -- the label changed in the panel but not at one of the four sites that quote it.
    @{ Name = 'M14 (F4) revert the label to the outcome-asserting wording'; Key = 'cfg';
       Find = 'TEXT("Protect Other Mods'' Nodes"),';
       Repl = 'TEXT("Protect Other Mods'' Nodes From Removal"),' },
    @{ Name = 'M15 (F4/SYMMETRY) leave one quote site on the old label'; Key = 'main';
       Find = 'checkbox ''Protect Other Mods'' Nodes'' ';
       Repl = 'checkbox ''Protect Other Mods'' Nodes From Removal'' ' }
)

# Each mutant is graded by RE-RUNNING the matching pin against the mutated text, in-memory. Nothing on
# disk is touched.
$missed = 0
foreach ($m in $mutants) {
    $mutated = Replace-First $text[$m.Key] $m.Find $m.Repl
    if ($null -eq $mutated) {
        Write-Host "FAIL(mutation): '$($m.Name)' NOT APPLIED -- its anchor text is not present in $($files[$m.Key]). An unapplied mutant grades nothing."
        $missed++
        continue
    }
    $caught = $false
    switch ($m.Key) {
        'cfg' {
            # H8 re-run on the mutated text.
            if ($mutated -notmatch [regex]::Escape('TEXT("Protect Other Mods'' Nodes"),')) { $caught = $true }
            if ($mutated -notmatch [regex]::Escape('Satisfactory Plus is the known case')) { $caught = $true }
            foreach ($ln in ($mutated -split "`r?`n")) {
                if ($ln -match 'TEXT\(' -and $ln -match 'Nodes From Removal') { $caught = $true }
            }
            foreach ($d in $deleted) { if ($mutated -match ('Add(Bool|Int)\(TEXT\("' + $d + '"\)')) { $caught = $true } }
            foreach ($phrase in $deadCopy) {
                foreach ($ln in ($mutated -split "`r?`n")) {
                    if ($ln -match 'TEXT\("' -and $ln -like "*$phrase*") { $caught = $true; break }
                }
            }
            if ($mutated -notmatch 'AddBool\(TEXT\("ProtectOtherModsNodes"\), true,') { $caught = $true }
        }
        'cfgh' {
            foreach ($d in $deleted) { if ($mutated -match ('(bool|int32)\s+' + $d + '\s*\{')) { $caught = $true } }
            if ($mutated -notmatch 'bool\s+ProtectOtherModsNodes\{true\}') { $caught = $true }
            if (Test-H1cOnText $mutated) { $caught = $true }
        }
        'pubh' {
            if (Test-H1cOnText $mutated) { $caught = $true }
        }
        'subsys' {
            if ($mutated -notmatch 'EmitPendingNoticeIfReady\(/\*bNoticesEnabled=\*/true\)') { $caught = $true }
            if ($mutated -match 'Config\.ShowCompatibilityNotices') { $caught = $true }
        }
        'roll' {
            if ($mutated -notmatch 'static\s+constexpr\s+bool\s+bRerollRelocated\s*=\s*true\s*;') { $caught = $true }
        }
        'main' {
            foreach ($ln in ($mutated -split "`r?`n")) {
                if ($ln -match 'TEXT\(' -and $ln -match 'Nodes From Removal') { $caught = $true }
            }
            if ($mutated -notmatch 'ECVF_SetByMask\)\s*!=\s*ECVF_SetByConstructor') { $caught = $true }
            if ($mutated -notmatch 'GetActiveConfig\(WorldContext\)\.ProtectOtherModsNodes') { $caught = $true }
        }
        'prot' {
            $calls = @([regex]::Matches($mutated, 'NodeShuffleSortForeignResourceRows\(Rows,\s*TEXT\("([^"]+)"\)\)') | ForEach-Object { $_.Groups[1].Value })
            foreach ($e in @('menu-stamp', 'in-world-sync')) { if ($calls -notcontains $e) { $caught = $true } }
            if ($mutated -notmatch 'Rows->Values\.Sort\(') { $caught = $true }
            foreach ($f in @('rowsUnreadable %d', 'rowsWithBlankPath %d')) {
                if ($mutated -notmatch [regex]::Escape($f)) { $caught = $true }
            }
        }
        'apply' {
            if ($mutated -match 'EmitWellRollHideTestPair\(') { $caught = $true }
        }
    }
    if (-not $caught) {
        Write-Host "FAIL(mutation): '$($m.Name)' SURVIVED -- the pin that should have caught it is decorative."
        $missed++
    }
}

foreach ($f in $fails) { Write-Host $f }
if ($fails.Count -gt 0 -or $missed -gt 0) {
    Write-Host "T68 lint: $($fails.Count) pin failure(s), $missed mutation failure(s)."
    exit 1
}
Write-Host "OK: T68 -- 5 deleted properties absent from schema AND mirror AND panel copy; both notice sites hard-wired true; re-roll polarity constant ON; one veto resolver with SetBy precedence and a checkbox fallback defaulting ON; rows sorted at both populators, after the positional withdrawal; T23 pair stays retired. $($mutants.Count)/$($mutants.Count) mutants caught."
exit 0
