# ns-t67-panel-copy-and-menu-stamp -- THE LINT FOR T67's ONE-LINE UNDOs.
#
# T67 is four small contracts, and every one of them has an obvious-looking one-line edit that breaks it
# SILENTLY -- nothing crashes, nothing logs, and the panel merely reads slightly differently:
#
#   1. THE TWO LABEL SLOTS CARRY DIFFERENT PARTS. SML's section widget renders the literal format
#      `{HeaderText} ({DisplayName})` (measured in Widget_CP_Section_Base.uasset, 2026-08-11). T65
#      stamped ONE string into BOTH, which is why every row printed as `X (X)`. The obvious "tidy" is to
#      make the two assignments use the same variable again -- and the panel then doubles again with
#      nothing anywhere saying so.
#   2. THE BASE GAME'S MOUNT ROOT RENDERS AS "Satisfactory" (item D, user-approved 2026-08-11), and the
#      chat notice's legend QUOTES that word. Delete the mapping and the legend names a string the list
#      never prints; delete the legend's word and it names a string the list stopped printing.
#   3. THE MAIN-MENU STAMP PASS EXISTS AND IS LABEL-ONLY. SML greys the protection list out in the pause
#      menu, so the main menu is the only editable surface -- and the population pass runs in-world only.
#      Two opposite one-liners break it: deleting the lifecycle hook (labels vanish from the only place
#      a player can edit them), or "while we are here" adding a MarkDirty/save to that pass, which would
#      rewrite NodeShuffle.cfg on every boot from a context that has seen no sightings.
#   4. THE FOUR "TRIES TO REMOVE" SURFACES STAY REGRADED. What the hook observes is a REQUIREMENT
#      EVALUATION, never a destroy (docs/TECH-DEBT.md T67). Re-introducing the removal wording in any of
#      the four is a copy edit anyone might make while "making it read better".
#   Plus two carried items: the T65 review's M2 fix (b) diskRowsUnticked field, and alternative E (the
#   notice reads a MEASURED parse flag instead of re-deriving it by searching the label for ": ").
#
# IT READS THE FILES ON DISK, NOT `git show` -- same reason as check_t54/t59/t61/t64/t65/t66_lint.ps1: a
# git-backed read goes vacuous on exactly the uncommitted tree it exists to check.
#
# IT STRIPS `//` COMMENTS BEFORE MATCHING, preserving line numbers by blanking rather than removing --
# the pinned files' comments legitimately QUOTE the forbidden wording in order to explain why it was
# removed, and a ban that matched its own rationale would be unfixable.
#
# EVERY PIN IS ANCHORED ON A FUNCTION NAME OR AN ADJACENT TOKEN, never on a bare fragment that another
# occurrence could satisfy (the M4/M12/M16 history in check_t65/t66_lint.ps1).
#
# NON-VACUITY IS CHECKED FOR EVERY HALF: each ban is paired with an assertion that the thing being
# protected still EXISTS, so a rename or a deletion cannot turn a ban into a silent pass.
#
# SILENT ON PASS. Exit 0 = every pin holds. Exit 1 = at least one does not, one line each.
#
# MUTATION TEST (in memory, no files written):  pwsh -File tools\check_t67_lint.ps1 -MutationTest

param([switch]$MutationTest)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cfgFile    = 'NodeShuffleConfig.cpp'
$protFile   = 'NodeShuffleForeignProtectConfig.cpp'
$noticeFile = 'NodeShuffleObserveNotice.cpp'
$rootFile   = 'NodeShuffleRootInstance.cpp'
$auditFile  = 'NodeShuffleAuditPlacements.cpp'
$cfgPath    = Join-Path $root ('Source\NodeShuffle\Private\' + $cfgFile)
$protPath   = Join-Path $root ('Source\NodeShuffle\Private\' + $protFile)
$noticePath = Join-Path $root ('Source\NodeShuffle\Private\' + $noticeFile)
$rootPath   = Join-Path $root ('Source\NodeShuffle\Private\' + $rootFile)
$auditPath  = Join-Path $root ('Source\NodeShuffle\Private\' + $auditFile)

function Remove-LineComments([string]$text) {
    ($text -split "`n" | ForEach-Object { $_ -replace '//.*$', '' }) -join "`n"
}

# Returns an array of failure strings. Empty array = every pin holds.
function Get-T67Failures([string]$cfgText, [string]$protText, [string]$noticeText,
                         [string]$rootText, [string]$auditText) {
    $fails = @()
    $cfg    = Remove-LineComments $cfgText
    $prot   = Remove-LineComments $protText
    $notice = Remove-LineComments $noticeText
    $rootm  = Remove-LineComments $rootText
    $audit  = Remove-LineComments $auditText

    # ---- HALF 1: the two label slots take DIFFERENT sources ---------------------------------------
    if ($prot -notmatch 'void NodeShuffleStampRowLabel\(UConfigPropertySection\* Section, const FString& NameLabel,') {
        $fails += "FAIL: $protFile no longer declares NodeShuffleStampRowLabel taking a NameLabel -- half 1 is vacuous; every pin below names that function's two slot writes."
    }
    if ($prot -notmatch 'Section->DisplayName = FText::FromString\(MountLabel\);') {
        $fails += "FAIL: $protFile's stamp helper no longer writes the MOUNT label into Section->DisplayName. SML renders a section header as the literal format `{HeaderText} ({DisplayName})`, so the two properties are the two slots of ONE string: putting the same value in both is what made every row print as ``X (X)`` before T67, and it re-appears silently."
    }
    if ($prot -notmatch 'RowWidget->HeaderText = FText::FromString\(NameLabel\);') {
        $fails += "FAIL: $protFile's stamp helper no longer writes the NAME label into RowWidget->HeaderText. Same format, other slot: an empty or duplicated header slot renders as `` (X)`` or ``X (X)``, which is the exact defect T67 fixed."
    }
    # The derivation must still produce the two parts separately, or there is nothing to put in two slots.
    if ($prot -notmatch 'static void NodeShuffleDeriveForeignResourceLabelParts') {
        $fails += "FAIL: $protFile no longer defines NodeShuffleDeriveForeignResourceLabelParts. The panel needs the mount and the name as SEPARATE values; re-composing them into one string and splitting it again at the widget is the re-derivation alternative E exists to forbid."
    }
    if ($prot -notmatch 'OutMountLabel = MountRoot;') {
        $fails += "FAIL: $protFile's parts derivation no longer returns the mount root as OutMountLabel -- the bracket slot then carries whatever it carried last, and the row's mount segment (T65's load-bearing half) is gone from the panel."
    }

    # ---- HALF 2: item D -- the base game's mount root renders as "Satisfactory" ---------------------
    if ($prot -notmatch 'MountSegment == TEXT\("Game"\)\) \? FString\(TEXT\("Satisfactory"\)\) : MountSegment') {
        $fails += "FAIL: $protFile no longer maps the mount segment ""Game"" to ""Satisfactory"" (T67 item D, user-approved 2026-08-11). The mapping is deliberately in ONE place so the panel, the chat notice and the logs cannot disagree about what a base-game row is called."
    }
    if ($notice -notmatch '\\"Satisfactory\\" is the base game') {
        $fails += "FAIL: $noticeFile's colon legend no longer names ""Satisfactory"" as the base game's own content. The legend exists to explain the label the list actually prints; naming a different string than the derivation produces is a false claim in player-facing copy."
    }
    if ($notice -match 'TEXT\("that resource''s asset lives in - \\"Game\\"') {
        $fails += "FAIL: $noticeFile's colon legend has the pre-T67 ""Game"" wording back, which names a string the label derivation no longer produces for any row."
    }

    # ---- HALF 3: the main-menu stamp pass, and its label-only boundary ------------------------------
    if ($prot -notmatch 'void FNodeShuffleModule::StampForeignResourceRowLabelsFromConfig') {
        $fails += "FAIL: $protFile no longer defines StampForeignResourceRowLabelsFromConfig -- half 3 is vacuous. Without it the ONLY code that ever labels a protection row is the in-world population pass, and SML makes the main menu the only place the list is editable, so the editable surface is the unlabelled one."
    } else {
        # The label-only boundary. A save from this pass would rewrite NodeShuffle.cfg on every boot from
        # a context that has seen no sightings. Measured by looking only INSIDE this function.
        $fnStart = $prot.IndexOf('void FNodeShuffleModule::StampForeignResourceRowLabelsFromConfig')
        $fnEnd   = $prot.IndexOf('void FNodeShuffleModule::SyncForeignResourceRowsToConfig')
        # COLD REVIEW F4: an unresolvable window is a FAILURE, never a skip. The first draft had no else,
        # so declaring the sync function above the stamp definition -- the idiom this very file already
        # uses for NodeShuffleDeriveForeignResourceLabelParts -- made both pins below vanish silently,
        # and a stamp pass that saved the config on every boot shipped green (measured by the reviewer).
        if ($fnStart -lt 0 -or $fnEnd -le $fnStart) {
            $fails += "FAIL: $protFile's StampForeignResourceRowLabelsFromConfig body window could not be computed (start=$fnStart end=$fnEnd), so the label-only boundary pin and the T67MENUSTAMP census pin were SKIPPED. A lint that silently checks nothing is worse than no lint; re-anchor the window before trusting this suite."
        }
        else {
            $fnBody = $prot.Substring($fnStart, $fnEnd - $fnStart)
            if ($fnBody -match 'MarkDirty|MarkConfigurationDirty|FlushPendingSaves|AddNewElement|->Value\s*=') {
                $fails += "FAIL: $protFile's StampForeignResourceRowLabelsFromConfig now writes a value, adds a row, or saves. It runs from the game instance module's POST_INITIALIZATION -- in the MAIN MENU, where no sighting has ever happened -- so anything it writes is written from an empty registry, and anything it saves rewrites the player's NodeShuffle.cfg on every boot."
            }
            if ($fnBody -notmatch 'T67MENUSTAMP: rowsWalked %d rowsStamped %d') {
                $fails += "FAIL: $protFile's main-menu stamp pass no longer emits its T67MENUSTAMP census. The pass is otherwise invisible: with no census, ""the labels are missing"" and ""the pass never ran"" read identically in a log."
            }
            # COLD REVIEW F2, same class as the census below: the format text proves the FIELDS exist,
            # this proves the call PASSES them. The reviewer's mutant dropping Fallbacks while leaving
            # five %d in the format MISSED before this pin existed.
            if ($fnBody -notmatch 'RowsWalked, Stamped, Malformed, BlankPaths, Fallbacks\);') {
                $fails += "FAIL: $protFile's T67MENUSTAMP census no longer passes all five counts as arguments. The format string still carries five %d, so the missing one reads an argument that was never supplied."
            }
        }
    }
    if ($rootm -notmatch 'void URootInstance_NodeShuffle::DispatchLifecycleEvent') {
        $fails += "FAIL: $rootFile no longer overrides DispatchLifecycleEvent -- nothing calls the main-menu stamp pass, so the pass exists and never runs."
    }
    if ($rootm -notmatch 'Phase != ELifecyclePhase::POST_INITIALIZATION') {
        $fails += "FAIL: $rootFile's lifecycle override no longer gates on POST_INITIALIZATION. SML registers this module's configuration during INITIALIZATION (UGameInstanceModule::DispatchLifecycleEvent -> RegisterDefaultContent -> RegisterModConfiguration, which loads the .cfg inline), so an earlier phase walks a configuration that does not exist yet and stamps nothing."
    }
    if ($rootm -notmatch 'FNodeShuffleModule::StampForeignResourceRowLabelsFromConfig\(this\)') {
        $fails += "FAIL: $rootFile no longer calls StampForeignResourceRowLabelsFromConfig from the lifecycle hook -- the main-menu rows go back to showing the element template's fallback text."
    }
    if ($rootm -notmatch 'Super::DispatchLifecycleEvent\(Phase\);') {
        $fails += "FAIL: $rootFile's override no longer calls Super::DispatchLifecycleEvent. The base class is what REGISTERS this mod's configuration; skipping it leaves the mod with no config panel at all."
    }

    # ---- HALF 4: the four regraded copy surfaces ----------------------------------------------------
    if ($cfg -match 'tries to remove|try to remove|stops that removal') {
        $fails += "FAIL: $cfgFile has removal wording back in shipped panel copy. What the KBFL hook observes is a REQUIREMENT EVALUATION -- T58 measured an asset evaluating that requirement on nodes it never destroys -- so ""tries to remove"" asserts an intent this code cannot see and ""stops that removal"" asserts an outcome inside the other mod's handler (docs/TECH-DEBT.md T67)."
    }
    if ($notice -match 'steps in to stop that removal') {
        $fails += "FAIL: $noticeFile's enforcing branch has ""steps in to stop that removal"" back. Same regrade as the panel: this branch knows which answer its world gives at the check, not what the other mod does afterwards."
    }
    # Non-vacuity for both bans: the replacement wording must actually be present, or a wholesale
    # deletion of the sentences would pass the bans while telling the player nothing.
    if ($cfg -notmatch "another mod's node handler has checked nodes of that resource") {
        $fails += "FAIL: $cfgFile no longer states what actually puts a resource in the list -- that another mod's node handler CHECKED nodes of it. Passing the removal-wording ban by deleting the sentence leaves the list unexplained."
    }
    if ($notice -notmatch 'NodeShuffle answers that check for that resource') {
        $fails += "FAIL: $noticeFile's enforcing branch no longer states what this world does at the check. Same vacuity trap as above."
    }
    # COLD REVIEW F5. The three bans above are a PHRASE BLACKLIST, and a blacklist cannot protect a
    # claim that can be re-worded ("deletes them", "clears them"). Three of this packet's own changed
    # strings were reverted by the reviewer to a now-false claim with all seven suites green. These are
    # POSITIVE pins on the corrected wording, one per surface.
    if ($prot -notmatch 'label, the name in brackets says which content the asset comes from') {
        $fails += "FAIL: $protFile's row tooltip no longer describes the BRACKET form of the label. T67 removed the colon from the row label; a tooltip that still says ""the name before the colon"" points a player at punctuation the row does not have."
    }
    if ($cfg -notmatch 'is a trimmed form of it: the name in brackets says which content the asset comes') {
        $fails += "FAIL: $cfgFile's TEMPLATE tooltip no longer describes the bracket form. Same false instruction as above, on the fallback every unstamped row shows."
    }
    if ($cfg -notmatch 'when another mod''s node handler checks this resource''s nodes, NodeShuffle ') {
        $fails += "FAIL: $cfgFile's Protected property tooltip no longer states the regraded claim -- that the handler CHECKS and NodeShuffle answers. This is the fourth of T67's four surfaces and the blacklist above cannot protect it: any re-worded deletion claim ('deletes them', 'clears them') passes the ban."
    }

    # ---- HALF 5: carried items -- M2b's diskRowsUnticked, and alternative E -------------------------
    if ($prot -notmatch 'diskRowsUnticked %d') {
        $fails += "FAIL: $protFile's T65ROUNDTRIP line no longer carries diskRowsUnticked (T65 review M2 fix b). The tick is the ONLY per-row state a player sets, so without it a reload that silently reset every tick to the default reads exactly like a save that had no unticks."
    }
    if ($prot -notmatch '\+\+RowsUntickedOnEntry') {
        $fails += "FAIL: $protFile never increments RowsUntickedOnEntry -- diskRowsUnticked would print a constant 0, which is the 'zero without a denominator' failure wearing the field name of a real measurement."
    }
    if ($notice -match 'DisplayName\.Contains\(TEXT\(": "\)\)') {
        $fails += "FAIL: $noticeFile re-derives the mount-parse outcome by searching the label for "": "" (alternative E, undone). That asks a different question than the derivation answered: a resource name legitimately containing that sequence answers yes without a mount ever having been parsed."
    }
    if ($notice -notmatch 'Items\[i\]->bLabelCarriesMount') {
        $fails += "FAIL: $noticeFile's colon-legend gate no longer reads the MEASURED bLabelCarriesMount flag carried on each queued item -- the legend is either always printed or re-derived from text."
    }

    # ---- HALF 6: the audit's self-exclusion is no longer silent ------------------------------------
    if ($audit -notmatch '\+\+PlacementAuditCounts\[AB_SelfActorExcluded\]') {
        $fails += "FAIL: $auditFile never increments AB_SelfActorExcluded. The T66 audit-site self-ignore fix was SILENT -- no field said whether it had run at all -- and this counter is the only evidence of the population it applied to."
    }
    if ($audit -notmatch 'SELF-EXCLUSION: of the ') {
        $fails += "FAIL: $auditFile's AUDITCENSUS no longer prints the SELF-EXCLUSION field, so the counter above is incremented and never read -- which is the same as not having measured it."
    }
    # The format text above proves the FIELD exists; this proves the census UE_LOG actually PASSES its
    # argument. Anchored on the two arguments that immediately precede it, so only that call's argument
    # list can satisfy it. Same hole t66's cold review F5 found on the NODEPROBE side.
    if ($audit -notmatch 'PlacementAuditQueries, PlacementAuditOverlaps,\s*\r?\n\s*PlacementAuditCounts\[AB_SelfActorExcluded\]\);') {
        $fails += "FAIL: $auditFile's AUDITCENSUS no longer passes PlacementAuditCounts[AB_SelfActorExcluded] as the final argument of its census UE_LOG. The SELF-EXCLUSION %d then reads an argument that was never supplied -- undefined behaviour in a UE format string, on a 22-argument line."
    }
    # COLD REVIEW F1: the census must not describe reading 4 as an exclusion. It takes the actor as its
    # SUBJECT, which counts the first hit BEFORE withholding it, so a TOTALLY INSIDE count is not a
    # reading taken with the node out of the trace. Positive pin, because a blacklist cannot cover every
    # way of re-asserting it.
    if ($audit -notmatch 'to the containment instrument as its SUBJECT') {
        $fails += "FAIL: $auditFile's SELF-EXCLUSION sentence no longer distinguishes the containment instrument's SUBJECT parameter from the ignore-list parameters of the settle probe and the enclosure gate. Collapsing the two makes the census assert that the TOTALLY INSIDE count was taken with the node withheld from the trace -- it was not, its first hit is classified and counted -- which is a wrong architectural model produced by a log line."
    }

    return ,$fails
}

$cfgRaw    = Get-Content -Raw $cfgPath
$protRaw   = Get-Content -Raw $protPath
$noticeRaw = Get-Content -Raw $noticePath
$rootRaw   = Get-Content -Raw $rootPath
$auditRaw  = Get-Content -Raw $auditPath

if (-not $MutationTest) {
    $f = Get-T67Failures $cfgRaw $protRaw $noticeRaw $rootRaw $auditRaw
    if ($f.Count -gt 0) { $f | ForEach-Object { Write-Host $_ }; exit 1 }
    exit 0
}

# ---- mutation mode: prove each pin actually catches its edit ----
$control = Get-T67Failures $cfgRaw $protRaw $noticeRaw $rootRaw $auditRaw
Write-Host ("CONTROL (unmutated tree): {0}" -f $(if ($control.Count -eq 0) { 'PASS (0 failures) -- as required' } else { "UNEXPECTEDLY FAILING with $($control.Count):`n  " + ($control -join "`n  ") }))

function Replace-First([string]$text, [string]$find, [string]$repl) {
    $i = $text.IndexOf($find)
    if ($i -lt 0) { return $null }
    return $text.Substring(0, $i) + $repl + $text.Substring($i + $find.Length)
}

# No mutant embeds a line ending in its Find string: the tree is CRLF and a `\n` would silently turn
# every such mutant into "NOT APPLIED", i.e. a test that proves nothing (check_t59_lint's note).
$mutants = @(
    @{ Name = 'M1 put the SAME string back in both label slots (the X (X) regression)'; File = 'prot';
       Find = '        Section->DisplayName = FText::FromString(MountLabel);';
       Repl = '        Section->DisplayName = FText::FromString(NameLabel);' },
    @{ Name = 'M2 stamp the header slot from the mount too'; File = 'prot';
       Find = '            RowWidget->HeaderText = FText::FromString(NameLabel);';
       Repl = '            RowWidget->HeaderText = FText::FromString(MountLabel);' },
    @{ Name = 'M3 drop the Game -> Satisfactory mount rendering (item D)'; File = 'prot';
       Find = '    const FString MountRoot = (MountSegment == TEXT("Game")) ? FString(TEXT("Satisfactory")) : MountSegment;';
       Repl = '    const FString MountRoot = MountSegment;' },
    @{ Name = 'M4 let the notice legend name "Game" again while the label says Satisfactory'; File = 'notice';
       Find = 'TEXT("that resource''s asset comes from - \"Satisfactory\" is the base game''s own.\n");';
       Repl = 'TEXT("that resource''s asset lives in - \"Game\" is the base game''s own content folder.\n");' },
    @{ Name = 'M5 delete the POST_INITIALIZATION gate from the lifecycle hook'; File = 'rootm';
       Find = '    if (Phase != ELifecyclePhase::POST_INITIALIZATION)';
       Repl = '    if (false)' },
    @{ Name = 'M6 stop calling the main-menu stamp pass from the lifecycle hook'; File = 'rootm';
       Find = '    FNodeShuffleModule::StampForeignResourceRowLabelsFromConfig(this);';
       Repl = '    ;' },
    @{ Name = 'M7 make the main-menu pass save the config "so it sticks"'; File = 'prot';
       Find = '        NodeShuffleStampRowLabel(Cast<UConfigPropertySection>(Element), NameLabel, MountLabel, Path);';
       Repl = '        NodeShuffleStampRowLabel(Cast<UConfigPropertySection>(Element), NameLabel, MountLabel, Path); Rows->MarkDirty();' },
    @{ Name = 'M8 restore the removal wording in the panel tooltip'; File = 'cfg';
       Find = 'TEXT("another mod''s node handler has checked nodes of that resource at least once, so the ")';
       Repl = 'TEXT("another mod''s cleanup tries to remove nodes of that resource at least once, so the ")' },
    @{ Name = 'M9 restore the removal wording in the chat notice'; File = 'notice';
       Find = 'Body += TEXT("\nWhile a row stays ticked, NodeShuffle answers that check for that resource\n")';
       Repl = 'Body += TEXT("\nWhile a row stays ticked, NodeShuffle steps in to stop that removal for\n")' },
    @{ Name = 'M10 delete the diskRowsUnticked field (T65 review M2 fix b)'; File = 'prot';
       Find = 'TEXT("T65ROUNDTRIP first-sync: diskRowsRead %d diskRowsWithPathKey %d diskRowsUnticked %d ")';
       Repl = 'TEXT("T65ROUNDTRIP first-sync: diskRowsRead %d diskRowsWithPathKey %d ")' },
    @{ Name = 'M11 re-derive the mount-parse flag from the label text (undo alternative E)'; File = 'notice';
       Find = '            if (Items[i]->bLabelCarriesMount) { bAnyLabelCarriesMount = true; break; }';
       Repl = '            if (Items[i]->DisplayName.Contains(TEXT(": "))) { bAnyLabelCarriesMount = true; break; }' },
    @{ Name = 'M12 drop the audit self-exclusion counter (the fix goes silent again)'; File = 'audit';
       Find = '        if (LiveActor != nullptr) { ++PlacementAuditCounts[AB_SelfActorExcluded]; }';
       Repl = '        ' },
    @{ Name = 'M13 drop the SELF-EXCLUSION sentence from the audit census'; File = 'audit';
       Find = 'TEXT("queries and %d overlaps issued by the containment instrument. SELF-EXCLUSION: of the ")';
       Repl = 'TEXT("queries and %d overlaps issued by the containment instrument. Of the ")' },
    @{ Name = 'M14 delete the main-menu pass census line'; File = 'prot';
       Find = 'TEXT("T67MENUSTAMP: rowsWalked %d rowsStamped %d rowsMalformedShape %d rowsBlankPath %d ")';
       Repl = 'TEXT("menu stamp: walked %d stamped %d malformed %d blank %d ")' },
    @{ Name = 'M15 drop the mount half of the parts derivation'; File = 'prot';
       Find = '    OutMountLabel = MountRoot;'; Repl = '    ' },
    # ---- COLD REVIEW ROUND (F1/F2/F4/F5). M16 is the reviewer's verbatim mutant; its Find was checked
    # for ambiguity on this tree first (1 occurrence of "PlacementAuditOverlaps," in the file) so the
    # two-line-tail fallback the review offered was not needed. M17-M22 exercise the other new pins.
    @{ Name = 'M16 drop the AB_SelfActorExcluded ARGUMENT while leaving its %d in the census format'; File = 'audit';
       Find = 'PlacementAuditOverlaps,'; Repl = 'PlacementAuditOverlaps);  //' },
    @{ Name = 'M17 drop an argument from the T67MENUSTAMP census while leaving its %d'; File = 'prot';
       Find = '        RowsWalked, Stamped, Malformed, BlankPaths, Fallbacks);';
       Repl = '        RowsWalked, Stamped, Malformed, BlankPaths);' },
    # M18 is the F4 vacuity itself: the file's own forward-declaration idiom, applied to the sync
    # function, moves $fnEnd above $fnStart. Before the else-branch existed this silently disabled BOTH
    # body pins; it must now fail loudly.
    @{ Name = 'M18 forward-declare the sync function above the stamp definition (F4: window inverts)'; File = 'prot';
       Find = 'void FNodeShuffleModule::StampForeignResourceRowLabelsFromConfig(UObject* WorldContext)';
       Repl = 'void FNodeShuffleModule::SyncForeignResourceRowsToConfig(UObject*); void FNodeShuffleModule::StampForeignResourceRowLabelsFromConfig(UObject* WorldContext)' },
    @{ Name = 'M19 revert the row tooltip to the pre-T67 colon wording (F5)'; File = 'prot';
       Find = 'TEXT("label, the name in brackets says which content the asset comes from."),';
       Repl = 'TEXT("label, the name before the colon is the content folder it comes from."),' },
    @{ Name = 'M20 revert the TEMPLATE tooltip to the colon wording (F5)'; File = 'cfg';
       Find = 'TEXT("is a trimmed form of it: the name in brackets says which content the asset comes ")';
       Repl = 'TEXT("is a trimmed form of it: the name before the colon is the content folder it comes ")' },
    # M21 is the one a phrase blacklist cannot catch: a RE-WORDED deletion claim on the fourth surface.
    @{ Name = 'M21 re-assert a deletion in the Protected tooltip, re-worded past the blacklist (F5)'; File = 'cfg';
       Find = 'TEXT("Ticked: when another mod''s node handler checks this resource''s nodes, NodeShuffle ")';
       Repl = 'TEXT("Ticked: NodeShuffle keeps this resource''s nodes alive when another mod deletes ")' },
    @{ Name = 'M22 collapse the census SUBJECT/ignore distinction back into one claim (F1)'; File = 'audit';
       Find = 'TEXT("IGNORED actor, and to the containment instrument as its SUBJECT -- which is a different ")';
       Repl = 'TEXT("IGNORED actor, and to the containment instrument too -- one mechanism, so ")' }
)

$missed = 0
foreach ($m in $mutants) {
    $c = $cfgRaw; $p = $protRaw; $n = $noticeRaw; $r = $rootRaw; $a = $auditRaw; $applied = $null
    switch ($m.File) {
        'cfg'    { $c = Replace-First $cfgRaw    $m.Find $m.Repl; $applied = $c }
        'prot'   { $p = Replace-First $protRaw   $m.Find $m.Repl; $applied = $p }
        'notice' { $n = Replace-First $noticeRaw $m.Find $m.Repl; $applied = $n }
        'rootm'  { $r = Replace-First $rootRaw   $m.Find $m.Repl; $applied = $r }
        'audit'  { $a = Replace-First $auditRaw  $m.Find $m.Repl; $applied = $a }
    }
    if ($null -eq $applied) {
        Write-Host ("{0}: NOT APPLIED -- the text it edits was not found. The mutation test is stale, which means it is proving nothing." -f $m.Name)
        $missed++
        continue
    }
    $f = Get-T67Failures $c $p $n $r $a
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
