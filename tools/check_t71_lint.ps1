# check_t71_lint.ps1 -- T71 (ns-t71-wells-always-on, 2026-08-12)
#
# WHAT T71 DID: deleted the two resource-well config options (ShuffleResourceWells,
# RelocateResourceWells) and hard-wired both behaviours ON. The author's ruling was "make them a natural
# part of the shuffle", overriding the T68 audit's keep-both #7.
#
# WHAT THIS SUITE PINS, AND WHY EACH PIN IS HERE RATHER THAN IN check_t68_lint.ps1:
#   * H1  -- the two HARD-WIRE CONSTANTS still exist and are still `true`. A revert of either (to false,
#            or back to a config read) must fail. T68 has no analogue: it never introduced a named
#            constant, so its pins cannot see this.
#   * H2  -- the OFF BRANCHES stay deleted. Each is pinned by the FUNCTION it lived in, not by a bare
#            fragment: the 2026-08-11 lint rule (x3) is that a trailing-fragment pin is satisfiable by a
#            SIBLING declaration sharing the fragment, so every pin below anchors on text unique to its
#            site.
#   * H3  -- the four call sites pass NO gate argument. This is the half that catches a partial revert:
#            someone restoring the parameter but not the config field leaves a build that compiles.
#   * H4  -- DOC SYNC. The panel-row count, the README's group list, and the surviving unbounded-absence
#            warning. A player-facing claim that outlived its option is exactly what T71 was asked to
#            preserve, so its ABSENCE is a failure, not a cleanup.
#   * H5  -- the deleted display names do not survive in any player-facing text.
#
# Deleted-key/mirror/stale-mention scanning is NOT duplicated here -- check_t68_lint.ps1's H1/H1b/H1c
# were EXTENDED with T71's two identifiers and five more files instead, because asking one question in
# two suites is how two pins drift apart.
#
# SILENT ON PASS except the final summary line.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'

$files = @{
    cfgh     = 'NodeShuffle\Public\NodeShuffleConfig.h'
    cfg      = 'NodeShuffle\Private\NodeShuffleConfig.cpp'
    subsys   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    subsysh  = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    wellroll = 'NodeShuffle\Private\NodeShuffleWellRoll.cpp'
    retype   = 'NodeShuffle\Private\NodeShuffleWellRetype.cpp'
    relroll  = 'NodeShuffle\Private\NodeShuffleWellRelocateRoll.cpp'
    relapply = 'NodeShuffle\Private\NodeShuffleWellRelocateApply.cpp'
    sweep    = 'NodeShuffle\Private\NodeShuffleWellSweep.cpp'
    # T71 COLD REVIEW F4 (ledgered defect, x4): a green suite says NOTHING about a file it never
    # reads. WellClaim.cpp was edited by T71 and loaded by no suite at all; the review round then
    # edited WellUnhide.cpp (F1) and WellImmediateHide.cpp (F2/F7), so all three are loaded here and
    # each carries at least one pin below. Adding a file without adding a pin is the same defect.
    claim    = 'NodeShuffle\Private\NodeShuffleWellClaim.cpp'
    unhide   = 'NodeShuffle\Private\NodeShuffleWellUnhide.cpp'
    imhide   = 'NodeShuffle\Private\NodeShuffleWellImmediateHide.cpp'
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
# The docs are pinned too and live outside Source\.
$docs = @{
    readme    = Join-Path $root 'README.md'
    changelog = Join-Path $root 'CHANGELOG.md'
    uplugin   = Join-Path $root 'NodeShuffle.uplugin'
}
foreach ($k in $docs.Keys) {
    if (-not (Test-Path $docs[$k])) {
        Write-Host "FAIL(load): pinned doc '$($docs[$k])' does not exist."
        exit 2
    }
    $text[$k] = Get-Content -Raw -Path $docs[$k]
}

function Invoke-T71Checks([hashtable]$T) {
    $f = @()

    # ---- H1: THE TWO HARD-WIRE CONSTANTS. Anchored on the full declaration including `= true`, so a
    # flip to false fails, and on the struct header file so a move elsewhere fails too.
    if ($T.cfgh -notmatch 'static constexpr bool bWellShuffleHardWiredOn\s*=\s*true;') {
        $f += "FAIL(H1): NodeShuffleConfig.h no longer declares /static constexpr bool bWellShuffleHardWiredOn = true;/ -- T71 hard-wired the in-place well retype ON deliberately. A false, a rename, or a return to a config read silently restores the opt-in behaviour the author removed."
    }
    if ($T.cfgh -notmatch 'static constexpr bool bWellRelocationHardWiredOn\s*=\s*true;') {
        $f += "FAIL(H1): NodeShuffleConfig.h no longer declares /static constexpr bool bWellRelocationHardWiredOn = true;/ -- T71 hard-wired well RELOCATION ON deliberately. This is the gate that carried the unbounded-absence warning; turning it off without the author's ruling reverts the packet."
    }
    # H1b: neither constant may be re-declared as a UPROPERTY -- that is the exact half-revert that puts
    # a row back on the panel while the code still reads the constant.
    if ($T.cfgh -match 'UPROPERTY\([^)]*\)\s*[\r\n\s]*bool\s+(ShuffleResourceWells|RelocateResourceWells)') {
        $f += "FAIL(H1b): NodeShuffleConfig.h re-declares a deleted well toggle as a UPROPERTY -- T71 removed both from the struct mirror."
    }

    # ---- H2: THE OFF BRANCHES STAY DELETED, each anchored on its own function.
    # RollWellLayout: the deleted branch was `if (!Config.ShuffleResourceWells)`. The static_assert that
    # replaced it is the positive anchor; a config read reappearing in this file is the negative one.
    if ($T.wellroll -notmatch 'static_assert\(FNodeShuffleConfigStruct::bWellShuffleHardWiredOn') {
        $f += "FAIL(H2): NodeShuffleWellRoll.cpp lost its static_assert on bWellShuffleHardWiredOn -- that assert is what stands where RollWellLayout's OFF gate used to, and it is the tripwire if the constant is ever flipped."
    }
    if ($T.wellroll -match 'Config\.ShuffleResourceWells|\.RelocateResourceWells') {
        $f += "FAIL(H2): NodeShuffleWellRoll.cpp reads a deleted well toggle -- both fields are gone from FNodeShuffleConfigStruct."
    }
    # ApplyWellRetype: pinned by its DEFINITION line, which is unique to this file.
    if ($T.retype -notmatch 'void ANodeShuffleSubsystem::ApplyWellRetype\(\)') {
        $f += "FAIL(H2): NodeShuffleWellRetype.cpp no longer defines ApplyWellRetype() with NO parameter -- T71 removed the bWellShuffleEnabled gate. A restored parameter means the OFF branch is coming back with it."
    }
    if ($T.retype -match "'Shuffle Resource Wells' is OFF") {
        $f += "FAIL(H2): NodeShuffleWellRetype.cpp still emits the WELLH1 'is OFF' line -- that state cannot occur, so the line would be a log claim no run can produce."
    }
    # RollWellRelocation.
    if ($T.relroll -notmatch 'void ANodeShuffleSubsystem::RollWellRelocation\(int32 Seed, bool bIsReroll\)') {
        $f += "FAIL(H2): NodeShuffleWellRelocateRoll.cpp no longer defines RollWellRelocation(int32, bool) with NO gate parameter -- T71 removed it."
    }
    if ($T.relroll -notmatch 'static_assert\(FNodeShuffleConfigStruct::bWellRelocationHardWiredOn') {
        $f += "FAIL(H2): NodeShuffleWellRelocateRoll.cpp lost its static_assert on bWellRelocationHardWiredOn."
    }
    if ($T.relroll -match "'Relocate Resource Wells' is OFF") {
        $f += "FAIL(H2): NodeShuffleWellRelocateRoll.cpp still emits the WELLH2-ROLL 'is OFF' line -- unreachable since T71."
    }
    # ApplyWellRelocation: the bOn conjunction must be the constexpr form built from BOTH constants.
    # Anchored on `constexpr bool bOn` plus both names, because that pairing exists nowhere else.
    if ($T.relapply -notmatch 'constexpr bool bOn = FNodeShuffleConfigStruct::bWellShuffleHardWiredOn') {
        $f += "FAIL(H2): NodeShuffleWellRelocateApply.cpp's bOn is no longer the constexpr conjunction of the two hard-wired constants -- the retype-AND-relocate requirement is the one thing that must stay visible at this site (ns-review-h2 F1)."
    }
    if ($T.relapply -notmatch 'void ANodeShuffleSubsystem::ApplyWellRelocation\(float SpawnRadiusCm\)') {
        $f += "FAIL(H2): NodeShuffleWellRelocateApply.cpp no longer defines ApplyWellRelocation(float) -- T71 removed both gate parameters."
    }
    # FinishWellRollTeardown's sweep gate: the TWO-TERM form is load-bearing (ns-review-h5 F1) and must
    # not collapse to a bare literal.
    if ($T.sweep -notmatch 'constexpr bool bSweepOn = FNodeShuffleConfigStruct::bWellShuffleHardWiredOn') {
        $f += "FAIL(H2): NodeShuffleWellSweep.cpp's bSweepOn is no longer the two-term conjunction of the hard-wired constants -- h5 F1 found that the relocation term ALONE was never the correct gate, and a bare `true` erases that finding."
    }
    if ($T.sweep -notmatch 'void ANodeShuffleSubsystem::FinishWellRollTeardown\(\)') {
        $f += "FAIL(H2): NodeShuffleWellSweep.cpp no longer defines FinishWellRollTeardown() with no parameter."
    }

    # ---- H3: THE CALL SITES PASS NO GATE ARGUMENT. This is the partial-revert detector.
    if ($T.subsys -notmatch '(?m)^\s*ApplyWellRetype\(\);') {
        $f += "FAIL(H3): NodeShuffleSubsystem.cpp no longer calls ApplyWellRetype() with no argument."
    }
    if ($T.subsys -notmatch '(?m)^\s*ApplyWellRelocation\(SpawnRadiusCm\);') {
        $f += "FAIL(H3): NodeShuffleSubsystem.cpp no longer calls ApplyWellRelocation(SpawnRadiusCm) with no gate arguments."
    }
    if ($T.subsys -notmatch '(?m)^\s*RollWellRelocation\(Seed, bIsReroll\);') {
        $f += "FAIL(H3): NodeShuffleSubsystem.cpp no longer calls RollWellRelocation(Seed, bIsReroll) with no gate argument."
    }
    if ($T.subsysh -match 'void ApplyWellRetype\(bool|void ApplyWellRelocation\(bool|void RollWellRelocation\(int32 Seed, bool bIsReroll, bool|void FinishWellRollTeardown\(bool') {
        $f += "FAIL(H3): NodeShuffleSubsystem.h still declares a well function with a gate parameter -- T71 removed all four."
    }

    # ---- H4: DOC SYNC. The surviving player-facing claims are pinned by PRESENCE. T71's whole parity
    # duty was that the unbounded-absence warning must not vanish with the tooltip that carried it, so
    # its disappearance is a FAILURE of this packet, not a tidy-up.
    if ($T.uplugin -notmatch 'absent for an unbounded time') {
        $f += "FAIL(H4): NodeShuffle.uplugin's Description no longer carries the unbounded-absence warning. It moved there from the deleted relocation tooltip precisely so it would survive T71; deleting it makes the mod page silent about the one behaviour a player cannot opt out of."
    }
    if ($T.readme -notmatch 'always on since 1\.4\.0') {
        $f += "FAIL(H4): README.md's Resource wells heading no longer says the wells feature is always on -- it is the section a player is sent to now that there is no tooltip."
    }
    # COLD REVIEW F8 (VERBATIM anchor): 'not bounded' was satisfied by the PRE-EXISTING Known-behaviour
    # section, so deleting T71's new Resource-wells prose left H4 green. Anchored on text unique to the
    # new section instead; the old sentence keeps its own pin below so both sections stay covered.
    # SCOPED RE-PASS R2: 'Enabled' is tested at exactly one site (NodeShuffleSubsystem.cpp:261); the
    # seven SML hooks registered in StartupModule never read it, so "stops the mod acting at all" was
    # false and would have sent a bug reporter hunting a state the mod does not have.
    if ($T.readme -match 'stops the mod acting at all') {
        $f += "FAIL(H4): README.md again claims turning Enabled off 'stops the mod acting at all' -- the build-gun and scanner hooks are installed in StartupModule and never read Enabled."
    }
    if ($T.readme -notmatch 'genuinely absent from the world for an \*\*unbounded\*\* time') {
        $f += "FAIL(H4): README.md's NEW Resource wells section no longer carries the unbounded-absence sentence -- that section is where a player is sent now that the tooltip is gone."
    }
    if ($T.readme -notmatch 'not bounded') {
        $f += "FAIL(H4): README.md no longer states that the absence of a relocating well is not time-bounded."
    }
    if ($T.readme -match 'in four groups') {
        $f += "FAIL(H4): README.md still describes FOUR config groups -- the resource-wells group was deleted by T71, leaving three."
    }
    if ($T.changelog -notmatch 'no setting that turns this off') {
        $f += "FAIL(H4): CHANGELOG.md's 1.4.0 entry no longer states plainly that wells cannot be switched off. The entry's voice is unsoftened by design; a behaviour change a player cannot opt out of must be said outright."
    }
    # H4b: the panel row count claim has to agree with the number of Add* registrations actually made.
    # A count asserted in prose and never re-measured is this repo's named stale-figure class.
    $rows = ([regex]::Matches($T.cfg, 'Add(Bool|Int)\(TEXT\("')).Count
    if ($rows -ne 15) {
        $f += "FAIL(H4b): NodeShuffleConfig.cpp registers $rows panel rows, not the 15 that T71's docs, TECH-DEBT table and CHANGELOG all claim. Fix the count or fix the docs -- do not leave them disagreeing."
    }

    # ---- H5: no deleted display name survives in player-facing text (panel copy, README table, uplugin).
    foreach ($phrase in @('Shuffle Resource Wells (In Place)', 'Relocate Resource Wells (EXPERIMENTAL)')) {
        foreach ($k in @('cfg', 'uplugin')) {
            foreach ($ln in ($T[$k] -split "`r?`n")) {
                if ($ln -like "*$phrase*" -and $ln -notmatch 'T71') {
                    $f += "FAIL(H5): $k shows the deleted option name '$phrase' outside a T71 deletion note -- the option is gone, so the sentence points at nothing."
                    break
                }
            }
        }
    }
    # ---- H7 (COLD REVIEW F4, VERBATIM): the TickOff legend must keep reporting its own predicate. ----
    if ($T.claim -notmatch 'OFF\(bWellLastApplyRelocationOn=0 -- UNEXPECTED since T71 hard-wired the gate ON\)') {
        $f += "FAIL(H7): NodeShuffleWellClaim.cpp's TickOff legend no longer reports its own predicate -- T71 rewrote it away from 'relocation disabled', a config state the panel does not have. Restoring that wording re-asserts a cause no run can produce."
    }
    # ---- H8 (COLD REVIEW F1, AUTHORED PINS): the stranded census must not re-assert the dead cause. ----
    # The review supplied the replacement SENTENCES but no pin; these are mine. Both directions are
    # pinned -- the dead phrasing must be ABSENT and the predicate-reporting phrasing PRESENT -- because
    # an absence pin alone is satisfied by deleting the line altogether.
    if ($T.unhide -match 'relocation switched off while the original was already removed') {
        $f += "FAIL(H8): NodeShuffleWellUnhide.cpp's WELLH2-STRANDED Warning again states 'relocation switched off' as a CAUSE -- no 1.4.0 build permits that and no panel row offers it. The bucket is decided by !E.bRelocate alone."
    }
    if ($T.unhide -notmatch 'no longer marked as relocating while its origin was already') {
        $f += "FAIL(H8): NodeShuffleWellUnhide.cpp's WELLH2-STRANDED Warning no longer reports the predicate it tested -- the F1 replacement text is gone."
    }
    if ($T.unhide -match 'nobody is working on it because relocation is switched off') {
        $f += "FAIL(H8): NodeShuffleWellUnhide.cpp's Display split again names a switched-off relocation -- same dead cause, quieter line."
    }
    # SCOPED RE-PASS R1: H8's first four pins all read LOG STRINGS and the predicate; none read comment
    # prose, so a full revert of F1's REASONING passed 27/27 while the classification-order contract a
    # maintainer actually reads still named the dead dual-cause. These two pin the prose.
    if ($T.unhide -match 'relocation is off for this pass') {
        $f += "FAIL(H8): NodeShuffleWellUnhide.cpp's classification-order contract again says the nobody-is-working bucket means relocation is off FOR THIS PASS -- that half of the cause died with the toggle, and this comment is what a maintainer reads before touching the buckets."
    }
    if ($T.unhide -match 'relocation switched off after a roll-time hide') {
        $f += "FAIL(H8): NodeShuffleWellUnhide.cpp again names 'relocation switched off after a roll-time hide' as an entry route -- T68 deleted the roll-time hide arm and T71 deleted the toggle, so the bucket has ONE route (a re-roll clearing bRelocate), not two."
    }
    if ($T.unhide -match '!bWellLastApplyRelocationOn \|\| !E\.bRelocate') {
        $f += "FAIL(H8): NodeShuffleWellUnhide.cpp's NotWorked predicate carries the dead disjunct again -- bWellLastApplyRelocationOn is a compile-time true since T71, so a two-term test tells the reader this bucket has two causes when it has one."
    }
    # ---- H9 (COLD REVIEW F2/F7, AUTHORED PINS). ----
    if ($T.imhide -match 'with it off this') {
        $f += "FAIL(H9): NodeShuffleWellImmediateHide.cpp's T54 pair legend again describes a relocation-off state -- it cannot occur, and this is the one line a reader uses to judge whether the pair still means anything."
    }
    if ($T.imhide -notmatch 'hard-wired ON since T71, so this pair is measured on every') {
        $f += "FAIL(H9): NodeShuffleWellImmediateHide.cpp's T54 pair legend lost the F2 replacement -- a VACUOUS verdict must be explained as an empty SAVE population, never as a setting."
    }
    if ($T.imhide -match 'T23-A stays red') {
        $f += "FAIL(H9): NodeShuffleWellImmediateHide.cpp again points a reader at the T23 pair, which T68 retired with CommitWellsAtRoll -- the instrument does not exist."
    }
    # Anchored on the FIELD's own format fragment, which existed nowhere else in the file. The first
    # draft of this pin matched the bare phrase 'relocation switched ' and fired on an unrelated design
    # comment 30 lines away -- a pin that cannot tell its own site from prose is one someone deletes.
    if ($T.relapply -match '\(1 = off\): %d') {
        $f += "FAIL(H9): NodeShuffleWellRelocateApply.cpp's RESTORE ARMED line again prints the permanently-invariant relocation-off field (F7) -- a constant that reads as a measurement invites a reader to treat it as evidence."
    }

    # ================================================================================================
    # H6 -- NEGATIVE CONTROLS MUST BE ANCHORED PAST THE TAG BOUNDARY. (Chip-sourced item, folded in
    # 2026-08-12; the flag came from a subagent chip, not from the user.)
    # ================================================================================================
    # MEASURED, not assumed: `WELLH2-ROLL` is a STRICT PREFIX of the live tag family. Source\ holds 9
    # emit sites spelling `WELLH2-ROLL:` and 22 further prose mentions, plus the DELETED tag
    # `WELLH2-ROLLHIDE` (T68 removed the roll-time hide arm; only its tombstone comment survives).
    # THE DIRECTION OF THE VACUITY IS **ALWAYS-MATCH**: a negative control written on the short form to
    # prove the deleted arm absent hits all 31 live occurrences and can never report absence, so it
    # either fires a permanent false FAIL or -- if someone "fixes" it by inverting the sense -- passes
    # while seeing only living tags. Both outcomes are the pin failing to distinguish.
    # THE FIX IS THE TERMINATOR: the deleted tag is pinned as `WELLH2-ROLLHIDE` followed by a character
    # the LIVE tags cannot produce there. Live spellings are `WELLH2-ROLL:` and `WELLH2-ROLL ` -- neither
    # can extend into `ROLLHIDE` -- so `WELLH2-ROLLHIDE[:\s]` matches the dead tag and nothing else.
    $rollhideEmit = [regex]'WELLH2-ROLLHIDE[:\s]'
    foreach ($k in @('relroll', 'relapply', 'sweep', 'subsys')) {
        foreach ($ln in ($T[$k] -split "`r?`n")) {
            if ($rollhideEmit.IsMatch($ln) -and $ln -notmatch 'T68|T71') {
                $f += "FAIL(H6): $k names the DELETED log tag WELLH2-ROLLHIDE outside a T68/T71 tombstone -- the roll-time hide arm was removed with CommitWellsAtRoll; a re-added emit would print a tag no consumer expects."
            }
        }
    }
    # H6-POSITIVE-CONTROL / non-vacuity: the scan must be able to SEE this tag family at all. Anchored
    # with a negative lookahead so it matches the LIVE tag and is not itself satisfied by the dead one.
    if ($T.sweep -notmatch 'WELLH2-ROLL(?![A-Za-z-])') {
        $f += "FAIL(H6p): the positive control failed -- no live WELLH2-ROLL tag is visible in NodeShuffleWellSweep.cpp, so H6's negative control is scanning text that no longer contains this family and would pass by finding nothing."
    }
    return $f
}

$fails = Invoke-T71Checks $text

# ================================================================================================
# MUTATION SUITE -- every pin above must be shown to FAIL on a revert of the thing it guards.
# A pin that has never been seen red is a pin nobody has tested.
# ================================================================================================
$mutants = @(
    @{ Name = 'M1  flip the retype hard-wire to false';        Key = 'cfgh';
       Find = 'static constexpr bool bWellShuffleHardWiredOn = true;';
       Repl = 'static constexpr bool bWellShuffleHardWiredOn = false;'; Expect = 'H1' },
    @{ Name = 'M2  flip the relocation hard-wire to false';    Key = 'cfgh';
       Find = 'static constexpr bool bWellRelocationHardWiredOn = true;';
       Repl = 'static constexpr bool bWellRelocationHardWiredOn = false;'; Expect = 'H1' },
    @{ Name = 'M3  re-add ShuffleResourceWells as a UPROPERTY'; Key = 'cfgh';
       Find = '    static constexpr bool bWellShuffleHardWiredOn = true;';
       Repl = "    UPROPERTY(BlueprintReadWrite)`n    bool ShuffleResourceWells{false};"; Expect = 'H1' },
    @{ Name = 'M4  re-add RelocateResourceWells as a UPROPERTY'; Key = 'cfgh';
       Find = '    static constexpr bool bWellRelocationHardWiredOn = true;';
       Repl = "    UPROPERTY(BlueprintReadWrite)`n    bool RelocateResourceWells{false};"; Expect = 'H1' },
    @{ Name = 'M5  restore the RollWellLayout OFF gate';       Key = 'wellroll';
       Find = 'static_assert(FNodeShuffleConfigStruct::bWellShuffleHardWiredOn';
       Repl = 'if (!Config.ShuffleResourceWells) { return; } static_assert(false && FNodeShuffleConfigStruct_removed'; Expect = 'H2' },
    @{ Name = 'M6  restore ApplyWellRetype''s gate parameter'; Key = 'retype';
       Find = 'void ANodeShuffleSubsystem::ApplyWellRetype()';
       Repl = 'void ANodeShuffleSubsystem::ApplyWellRetype(bool bWellShuffleEnabled)'; Expect = 'H2' },
    @{ Name = 'M7  restore RollWellRelocation''s gate parameter'; Key = 'relroll';
       Find = 'void ANodeShuffleSubsystem::RollWellRelocation(int32 Seed, bool bIsReroll)';
       Repl = 'void ANodeShuffleSubsystem::RollWellRelocation(int32 Seed, bool bIsReroll, bool bRelocationEnabled)'; Expect = 'H2' },
    @{ Name = 'M8  collapse ApplyWellRelocation''s bOn to a literal'; Key = 'relapply';
       Find = 'constexpr bool bOn = FNodeShuffleConfigStruct::bWellShuffleHardWiredOn';
       Repl = 'constexpr bool bOn = true; constexpr bool bUnusedOn = FNodeShuffleConfigStruct_removed'; Expect = 'H2' },
    @{ Name = 'M9  collapse the post-roll sweep gate to a literal'; Key = 'sweep';
       Find = 'constexpr bool bSweepOn = FNodeShuffleConfigStruct::bWellShuffleHardWiredOn';
       Repl = 'constexpr bool bSweepOn = true; constexpr bool bUnusedSweep = FNodeShuffleConfigStruct_removed'; Expect = 'H2' },
    @{ Name = 'M10 restore ApplyWellRelocation''s gate parameters'; Key = 'relapply';
       Find = 'void ANodeShuffleSubsystem::ApplyWellRelocation(float SpawnRadiusCm)';
       Repl = 'void ANodeShuffleSubsystem::ApplyWellRelocation(bool bWellShuffleEnabled, bool bRelocationEnabled, float SpawnRadiusCm)'; Expect = 'H2' },
    @{ Name = 'M11 restore the gate argument at the retype call site'; Key = 'subsys';
       Find = '    ApplyWellRetype();';
       Repl = '    ApplyWellRetype(Config.ShuffleResourceWells);'; Expect = 'H3' },
    @{ Name = 'M12 restore the gate arguments at the relocation call site'; Key = 'subsys';
       Find = '    ApplyWellRelocation(SpawnRadiusCm);';
       Repl = '    ApplyWellRelocation(Config.ShuffleResourceWells, Config.RelocateResourceWells, SpawnRadiusCm);'; Expect = 'H3' },
    @{ Name = 'M13 restore a gate parameter in the subsystem header'; Key = 'subsysh';
       Find = '    void ApplyWellRetype();';
       Repl = '    void ApplyWellRetype(bool bWellShuffleEnabled);'; Expect = 'H3' },
    @{ Name = 'M14 drop the unbounded-absence warning from the mod Description'; Key = 'uplugin';
       Find = 'absent for an unbounded time'; Repl = 'placed promptly'; Expect = 'H4' },
    @{ Name = 'M15 revert the README to the four-group panel'; Key = 'readme';
       Find = 'in three groups'; Repl = 'in four groups'; Expect = 'H4' },
    @{ Name = 'M16 soften the CHANGELOG''s cannot-be-turned-off sentence'; Key = 'changelog';
       Find = 'no setting that turns this off'; Repl = 'a setting for advanced users'; Expect = 'H4' },
    @{ Name = 'M17 re-register a deleted well row (row count + display name)'; Key = 'cfg';
       Find = '    AddBool(TEXT("EnableDiagnostics"), false,';
       Repl = "    AddBool(TEXT(`"ShuffleResourceWells`"), false, TEXT(`"Shuffle Resource Wells (In Place)`"), TEXT(`"x`"));`n    AddBool(TEXT(`"EnableDiagnostics`"), false,"; Expect = 'H4b' },
    @{ Name = 'M18 drop the README not-bounded statement'; Key = 'readme';
       Find = '**not bounded**'; Repl = '**short**'; Expect = 'H4' },
    # M19 IS THE CHIP'S MUTANT: reintroduce the deleted tag as a real emit. It must be CAUGHT, which is
    # what proves the terminator-anchored control distinguishes it from the 31 live WELLH2-ROLL strings.
    @{ Name = 'M19 reintroduce the deleted WELLH2-ROLLHIDE emit'; Key = 'sweep';
       Find = '    SweepOrphanedWellActors(TEXT("post-roll"), bSweepOn);';
       Repl = "    UE_LOG(LogNodeShuffle, Display, TEXT(`"WELLH2-ROLLHIDE: armed %d`"), 0);`n    SweepOrphanedWellActors(TEXT(`"post-roll`"), bSweepOn);"; Expect = 'H6' },
    # M20 proves the POSITIVE control is load-bearing: strip the live tag family from the scanned file
    # and H6p must go red, so a future refactor that renames the family cannot leave H6 scanning nothing.
    @{ Name = 'M20 remove the live WELLH2-ROLL tag family from the sweep'; Key = 'sweep';
       Find = 'WELLH2-ROLL: withdrew the placement claim'; Repl = 'WELLH2X: withdrew the placement claim'; Expect = 'H6p' },
    # ---- COLD REVIEW ROUND MUTANTS. M21 is the review's own (F4, verbatim intent): the exact "tidy"
    # it predicted -- restoring the pre-T71 TickOff wording -- must now go red. M22-M26 cover the F1/F2/F7
    # fixes this round authored; per the ledgered rule, a revert of each must fail a suite.
    @{ Name = 'M21 restore the pre-T71 TickOff legend in WellClaim.cpp'; Key = 'claim';
       Find = 'OFF(bWellLastApplyRelocationOn=0 -- UNEXPECTED since T71 hard-wired the gate ON)';
       Repl = 'OFF(relocation disabled -- the apply gate skips this entry)'; Expect = 'H7' },
    @{ Name = 'M22 restore the false CAUSE in the WELLH2-STRANDED Warning'; Key = 'unhide';
       Find = 'no longer marked as relocating while its origin was already';
       Repl = 'relocation switched off while the original was already removed'; Expect = 'H8' },
    @{ Name = 'M23 restore the false cause in the stranded Display split'; Key = 'unhide';
       Find = 'nobody is working on it: this entry is no longer marked as relocating';
       Repl = 'nobody is working on it because relocation is switched off for this'; Expect = 'H8' },
    @{ Name = 'M24 re-add the dead disjunct to the NotWorked predicate'; Key = 'unhide';
       Find = 'else if (!E.bRelocate)';
       Repl = 'else if (!bWellLastApplyRelocationOn || !E.bRelocate)'; Expect = 'H8' },
    @{ Name = 'M25 restore the VACUOUS-by-construction clause on the T54 pair'; Key = 'imhide';
       Find = 'hard-wired ON since T71, so this pair is measured on every';
       Repl = 'measured only while well relocation is switched on for the pass; with it off this'; Expect = 'H9' },
    @{ Name = 'M26 re-point the banner at the retired T23 pair'; Key = 'imhide';
       Find = 'THE T23 PAIR (T23-A / T23-B) IS GONE';
       Repl = 'T23-A stays red with that toggle off and T23-B stays green; the T23 PAIR'; Expect = 'H9' },
    # M27 mutates the FORMAT FRAGMENT, which is what a real revert of F7 restores. An earlier draft
    # mutated only the surrounding prose and went uncaught once the pin was re-anchored -- the mutant
    # must reproduce the revert, not merely disturb text near it.
    @{ Name = 'M27 restore the permanently-invariant relocation-off field (F7)'; Key = 'relapply';
       Find = 'longer marked as relocating (1 = not relocating): %d';
       Repl = 'off for this pass (1 = off): %d, or this entry no longer marked as relocating (1 = not relocating): %d'; Expect = 'H9' },
    # SCOPED RE-PASS mutants: a revert of F1's REASONING (not just its strings) must now go red.
    @{ Name = 'M28 restore the dead dual-cause in the classification-order contract'; Key = 'unhide';
       Find = 'the entry is no longer marked as relocating, so';
       Repl = 'relocation is off for this pass or for this entry, so'; Expect = 'H8' },
    @{ Name = 'M29 restore the roll-time-hide entry route'; Key = 'unhide';
       Find = 'ONE state reaches that outcome with no';
       Repl = 'Two states reach the same player outcome and set no failure flag: relocation switched off after a roll-time hide -- no'; Expect = 'H8' },
    @{ Name = 'M30 restore the false README Enabled claim'; Key = 'readme';
       Find = 'stops the mod rolling, moving or maintaining anything further';
       Repl = 'stops the mod acting at all'; Expect = 'H4' }
)

$mutantFails = @()
$caughtCount = 0
foreach ($m in $mutants) {
    $mutated = @{}
    foreach ($k in $text.Keys) { $mutated[$k] = $text[$k] }
    if (-not $mutated[$m.Key].Contains($m.Find)) {
        $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- its anchor text is absent from '$($m.Key)'. A mutant that does not mutate is a vacuous pass."
        continue
    }
    $mutated[$m.Key] = $mutated[$m.Key].Replace($m.Find, $m.Repl)
    $result = Invoke-T71Checks $mutated
    $hit = $result | Where-Object { $_ -like "*($($m.Expect))*" }
    if ($hit) { $caughtCount++ }
    else {
        $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin -- that pin cannot see the revert it exists to prevent."
    }
}

$allFails = @($fails) + @($mutantFails)
if ($allFails.Count -gt 0) {
    foreach ($x in $allFails) { Write-Host $x }
    Write-Host "T71 LINT: FAIL -- $($allFails.Count) problem(s). Mutants caught $caughtCount/$($mutants.Count)."
    exit 1
}
Write-Host "T71 LINT: PASS -- all pins hold; mutants caught $caughtCount/$($mutants.Count)."
exit 0
