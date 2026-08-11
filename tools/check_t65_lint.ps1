# ns-t65-panel-and-copy -- THE LINT FOR THE ONE-LINE EDITS THAT UNDO THE PANEL REWORK.
#
# T65's contract is three sentences. (1) In a protection row the TICK BOX comes first and the resource
# label after it, which in C++ is nothing but the order of two SectionProperties.Add calls. (2) The row's
# label is "<MountRoot>: <Name>" and the MOUNT SEGMENT IS NEVER DROPPED -- that segment is the only thing
# that tells a vanilla asset from an SF+/mod asset of the same name, and a notice that trimmed it away is
# what made asset identity ambiguous in the T62/T63 era. (3) The full asset path REMAINS the stored key
# and the row's tooltip, and an existing NodeShuffle.cfg round-trips unchanged -- with a startup census
# line that measures the reload rather than asserting it.
#
# Each clause is held up by a handful of lines and each has an obvious-looking one-line edit that breaks
# it SILENTLY: nothing crashes, nothing logs, and the panel merely looks slightly different or the label
# merely looks slightly shorter -- while the identity information a player needs to tell two mods' ores
# apart is gone.
#
# THE EDITS IT PINS, each a genuine one-liner someone would write under pressure:
#   1. MOVE THE Resource Add BACK ABOVE THE Protected Add (or reorder while "tidying"). The tick box
#      drifts right with the text width again, which is the exact complaint the rework answers, and
#      nothing anywhere reports that the order changed.
#   2. RETURN THE BARE LEAF FROM THE LABEL FUNCTION -- delete the mount concatenation, "it's noisy".
#      That is the T62/T63 ambiguity restored, and every label still looks perfectly reasonable.
#   3. DROP THE FULL PATH FROM THE ROW TOOLTIP. The trimmed label then has no companion surface but the
#      text box, and the next edit that hides the text box loses the identity entirely.
#   4. STOP WRITING THE PATH AS THE STORED KEY (e.g. write the label instead, so the .cfg "reads
#      nicely"). Every existing row stops matching, silently, and every resource quietly re-protects.
#   5. DELETE THE ROUND-TRIP CENSUS, or its fallback-count denominator. A reload that dropped rows then
#      looks exactly like a reload that had none.
#   6. RESTORE THE ORIGIN CLAIM in the chat notice ("... are not part of the base game's content"), which
#      is FALSE for vanilla resources carried by another mod's node actors -- the population that was
#      actually measured on 2026-08-11.
#   7. LET THE NOTICE'S QUOTED LIST TITLE DRIFT FROM THE PANEL'S ACTUAL TITLE. The message then tells the
#      player to look for a heading that does not exist.
#   8. DROP THE T60 ARCHETYPE-INSTANCING GUARD while restructuring the row loop. That check is the only
#      thing standing between "rows have their own tick boxes" and "untick one, they all move", and it
#      cannot be proven from headers -- so a rework that quietly removes it removes the measurement too.
#   9. RENAME A T60CENSUS FIELD while adding T65's. The population lens: a field that changes meaning or
#      name silently invalidates every earlier log reading.
#
# IT READS THE FILES ON DISK, NOT `git show` -- same reason as check_t54/t59/t61/t64_lint.ps1: a
# git-backed read goes vacuous on exactly the uncommitted tree it exists to check.
#
# IT STRIPS `//` COMMENTS BEFORE MATCHING, preserving line numbers by blanking rather than removing --
# this file's own rationale, and the pinned files' comments, legitimately DISCUSS the forbidden forms
# (the notice file's comment quotes the false claim in full in order to explain why it was removed).
#
# NON-VACUITY IS CHECKED FOR EVERY HALF: each ban is paired with an assertion that the thing being
# protected still EXISTS, so a rename or a deletion cannot turn a ban into a silent pass.
#
# SILENT ON PASS. Exit 0 = every pin holds. Exit 1 = at least one does not, one line each naming
# file+pattern+why.
#
# MUTATION TEST (in memory, no files written):  pwsh -File tools\check_t65_lint.ps1 -MutationTest

param([switch]$MutationTest)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cfgFile    = 'NodeShuffleConfig.cpp'
$protFile   = 'NodeShuffleForeignProtectConfig.cpp'
$noticeFile = 'NodeShuffleObserveNotice.cpp'
$cfgPath    = Join-Path $root ('Source\NodeShuffle\Private\' + $cfgFile)
$protPath   = Join-Path $root ('Source\NodeShuffle\Private\' + $protFile)
$noticePath = Join-Path $root ('Source\NodeShuffle\Private\' + $noticeFile)

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
function Get-T65Failures([string]$cfgText, [string]$protText, [string]$noticeText) {
    $fails = @()
    $cfg    = Remove-LineComments $cfgText
    $prot   = Remove-LineComments $protText
    $notice = Remove-LineComments $noticeText
    $cfgL    = $cfg    -split "`n"
    $protL   = $prot   -split "`n"
    $noticeL = $notice -split "`n"

    # ---- HALF 1: the tick box is declared BEFORE the resource string ----------------------------
    $protAdd = Get-LineNumber $cfgL 'RowTemplate->SectionProperties\.Add\(\s*TEXT\("Protected"\)'
    $resAdd  = Get-LineNumber $cfgL 'RowTemplate->SectionProperties\.Add\(\s*TEXT\("Resource"\)'
    if ($protAdd -eq -1) {
        $fails += "FAIL: $cfgFile no longer adds a /Protected/ property to the row template -- half 1 is vacuous and the row has no tick box at all."
    }
    if ($resAdd -eq -1) {
        $fails += "FAIL: $cfgFile no longer adds a /Resource/ property to the row template -- half 1 is vacuous and the row has no identity field."
    }
    if ($protAdd -ne -1 -and $resAdd -ne -1 -and $protAdd -gt $resAdd) {
        $fails += "FAIL: $cfgFile adds Resource (line $resAdd) BEFORE Protected (line $protAdd). SectionProperties is a TMap iterated in insertion order and the row widget is horizontal, so this order IS the on-screen field order: the tick box would render after the label again and drift right with the text width, which is the exact complaint the T65 rework answers. Nothing logs when this changes."
    }
    # The row must stay horizontal, or 'first' means nothing on screen.
    if ($cfg -notmatch 'ECP_SectionWidgetType::CPS_Horizontal') {
        $fails += "FAIL: $cfgFile no longer sets the row template to CPS_Horizontal -- in a vertical row the checkbox-first ordering pinned above stops describing anything a player sees."
    }

    # ---- HALF 2: the label carries the MOUNT SEGMENT ---------------------------------------------
    $labelFn = Get-LineNumber $protL 'FString NodeShuffleMakeForeignResourceLabel'
    if ($labelFn -eq -1) {
        $fails += "FAIL: $protFile no longer defines NodeShuffleMakeForeignResourceLabel -- half 2 is vacuous; every pin below names that function."
    } else {
        # T65 cold review S10: the label function occupies ~70 lines, so a 70-line window false-FAILs
        # after ~5 added comment lines. 120 keeps the pin anchored on the function, not on its size.
        $end = [Math]::Min($protL.Count, $labelFn + 120)
        $window = ($protL[($labelFn - 1)..($end - 1)]) -join "`n"
        if ($window -notmatch 'MountRoot') {
            $fails += "FAIL: $protFile's label function no longer derives a MountRoot -- the label is back to a bare descriptor leaf. That is the T62/T63 ambiguity: a vanilla asset and a mod's same-named asset render identically, and the label still looks perfectly reasonable while carrying strictly less identity."
        }
        if ($window -notmatch 'MountRoot\s*\+\s*TEXT\("\s*:\s*"\)') {
            $fails += "FAIL: $protFile's label function no longer concatenates the mount root with a colon separator. Whatever it returns now, the row label has stopped being ""<Mod>: <Resource>"" and the T61 notice's colon legend describes text that is not there."
        }
        if ($window -notmatch 'return ResourceClassPath;') {
            $fails += "FAIL: $protFile's label function no longer falls back to the FULL PATH on a parse failure. A trimmed-looking label that could not actually be parsed is indistinguishable from one that was."
        }
        if ($window -notmatch 'bOutParsed') {
            $fails += "FAIL: $protFile's label function no longer reports whether the parse succeeded -- the fallback count on T65ROUNDTRIP/T65LABEL then has nothing to count and reads 0 forever."
        }
    }

    # ---- HALF 3: the full path stays the KEY, and becomes the row TOOLTIP -------------------------
    if ($prot -notmatch 'ResourceProp->Value\s*=\s*Pair\.Key;') {
        $fails += "FAIL: $protFile no longer writes the resource PATH (Pair.Key) into the row's Resource value. The stored key is the whole compatibility contract: an existing NodeShuffle.cfg's rows are matched by that exact string, so writing anything else silently orphans every row a player already has."
    }
    if ($prot -notmatch 'Section->Tooltip\s*=') {
        $fails += "FAIL: $protFile no longer stamps the row section's Tooltip. The visible label is trimmed by T65, so the tooltip is where the full path went; without it the identity survives only in the text box, and the next edit that hides the text box loses it entirely."
    }
    if ($prot -notmatch 'NodeShuffleStampRowLabel\(\s*Section,\s*RowLabel,\s*Path\s*\)') {
        $fails += "FAIL: $protFile no longer passes the full Path to NodeShuffleStampRowLabel at the DISK-ROW site. Rows read back from NodeShuffle.cfg are exactly the ones the player already has, and they would get a trimmed label with no tooltip behind it."
    }
    if ($prot -notmatch 'NodeShuffleStampRowLabel\(\s*Section,\s*Pair\.Value\.DisplayName,\s*Pair\.Key\s*\)') {
        $fails += "FAIL: $protFile no longer passes the full path to NodeShuffleStampRowLabel at the NEW-ROW site. Newly added rows would carry a trimmed label with no tooltip, while reloaded rows carry both -- the two sync sites drifting apart is precisely what one shared stamp helper exists to prevent."
    }

    # ---- HALF 4: the round-trip census, with its denominator --------------------------------------
    if ($prot -notmatch 'T65ROUNDTRIP') {
        $fails += "FAIL: $protFile no longer emits a T65ROUNDTRIP line -- nothing then measures that an existing config's rows survived the reload, and a load that dropped every row looks exactly like a load that had none."
    }
    foreach ($field in @('diskRowsRead', 'diskRowsWithPathKey', 'labelFallbacksToFullPath')) {
        if ($prot -notmatch $field) {
            $fails += "FAIL: $protFile's T65ROUNDTRIP line no longer carries the '$field' count. Each of these is one half of a ratio: a fallback count without its denominator, or a key count without the row count it is drawn from, is evidence of nothing."
        }
    }
    if ($prot -notmatch 'T65LABEL') {
        $fails += "FAIL: $protFile no longer emits a T65LABEL line -- the derived label text itself is then visible only on screen, so a wrong trim can never be read out of a log."
    }

    # ---- HALF 5: the notice copy claims only what was measured ------------------------------------
    if ($notice -match 'not part of the base game') {
        $fails += "FAIL: $noticeFile has the origin claim back in shipped copy. It asserts the listed RESOURCES are modded, and the hook's own predicate tests the NODE CLASS -- on 2026-08-11 it fired for vanilla NitrogenGas and LiquidOil carried by RefinedPower well actors, making the sentence false for a population this code demonstrably produces."
    }
    if ($notice -notmatch 'FString BuildForeignNoticeBody') {
        $fails += "FAIL: $noticeFile no longer defines BuildForeignNoticeBody -- half 5 is vacuous; every pin here is about the text that function builds."
    }
    if ($notice -notmatch "Those nodes' own classes are not the base game's") {
        $fails += "FAIL: $noticeFile no longer states the one thing that IS measured about every listed item -- that the NODE classes are not base-game classes (bNodeClassIsVanilla false, i.e. the actor class path is outside /Game/). Without it the message names resources and explains nothing about why they are listed."
    }
    # The notice QUOTES the panel title. Both must say the same thing.
    $cfgTitle = ($cfgL | Where-Object { $_ -match 'Rows->DisplayName\s*=\s*FText::FromString' }) -join ''
    if ($cfgTitle -notmatch "Protect Other Mods' Nodes") {
        $fails += "FAIL: $cfgFile's protection-list title is no longer ""Protect Other Mods' Nodes"". The old ""...Resources"" wording asserts the resource belongs to another mod, which is the same false claim T65 removed from the chat notice; and the notice quotes this title verbatim, so any change here must be made in both files."
    }
    if ($notice -notmatch "Protect Other Mods' Nodes \(Per") {
        $fails += "FAIL: $noticeFile no longer quotes the panel title as ""Protect Other Mods' Nodes (Per Resource)"". The message tells the player exactly which heading to open; a drifted quote sends them looking for a heading that does not exist."
    }

    # ---- HALF 6: T60's archetype-instancing guard and census fields survive the rework -------------
    # TWO sites, counted -- NOT one -notmatch. Cold review F8's whole point was that the guard runs over
    # EVERY row (including the Deserialize-created ones, which is every row on every boot) as well as on
    # the row being added; a single -notmatch stays green while either site alone is deleted, because the
    # other still matches. Measured, not predicted: the first run of this lint's mutation test reported
    # M8 as MISSED for exactly that reason.
    $guardSites = Get-AllLineNumbers $protL 'ResourceProp\s*==\s*TemplateResource\s*\|\|\s*ProtectedProp\s*==\s*TemplateProtected'
    if ($guardSites.Count -lt 2) {
        $fails += "FAIL: $protFile has $($guardSites.Count) pointer-identity comparison(s) against the element template, expected 2 (the every-row scan and the new-row withdrawal). That check is the ONLY evidence that AddNewElement really instances per-row subobjects -- it cannot be proven from headers -- and without both sites 'untick one row, they all move' ships as a silent behaviour."
    }
    if ($prot -notmatch 'RemoveElementAtIndex') {
        $fails += "FAIL: $protFile no longer withdraws a row when the shared-template guard fires. Writing through a shared value object puts a resource path on the TEMPLATE itself, which every later row then clones -- the failure mode the guard was added to stop, not merely to report."
    }
    # The phrase is matched on ONE source line: the Error's format string is split across TEXT() literals
    # and a longer phrase spanning the break would never match anything (it did not, on the first run).
    if ($prot -notmatch 'value object with the element template') {
        $fails += "FAIL: $protFile no longer carries the shared-subobject Error line. The guard can then fire with nothing to read afterwards, which is the same as not having measured it."
    }
    # The two T65 counters must actually be incremented; a census field whose counter nothing bumps
    # reads 0 forever and is indistinguishable from a clean run.
    foreach ($counter in @('LabelsDerivedThisPass', 'LabelFallbacksThisPass')) {
        if ($prot -notmatch ('\+\+' + $counter)) {
            $fails += "FAIL: $protFile never increments $counter -- the T65ROUNDTRIP line would print a constant 0 for it, which is the 'zero without a denominator' failure wearing the field name of a real measurement."
        }
    }
    foreach ($field in @('rowsInFileAtLatch', 'rowsLoadedFromDisk', 'optOutsActingThisWorld')) {
        if ($prot -notmatch $field) {
            $fails += "FAIL: $protFile no longer carries the T60CENSUS field '$field'. T65 must not redefine or rename any T60 field: every earlier log reading is keyed on these names, and a silent rename makes past and present logs incomparable without anything announcing it."
        }
    }

    return ,$fails
}

$cfgRaw    = Get-Content -Raw $cfgPath
$protRaw   = Get-Content -Raw $protPath
$noticeRaw = Get-Content -Raw $noticePath

if (-not $MutationTest) {
    $f = Get-T65Failures $cfgRaw $protRaw $noticeRaw
    if ($f.Count -gt 0) { $f | ForEach-Object { Write-Host $_ }; exit 1 }
    exit 0
}

# ---- mutation mode: prove each pin actually catches its edit ----
$control = Get-T65Failures $cfgRaw $protRaw $noticeRaw
Write-Host ("CONTROL (unmutated tree): {0}" -f $(if ($control.Count -eq 0) { 'PASS (0 failures) -- as required' } else { "UNEXPECTEDLY FAILING with $($control.Count):`n  " + ($control -join "`n  ") }))

function Replace-First([string]$text, [string]$find, [string]$repl) {
    $i = $text.IndexOf($find)
    if ($i -lt 0) { return $null }
    return $text.Substring(0, $i) + $repl + $text.Substring($i + $find.Length)
}

# No mutant embeds a line ending in its Find string: the tree is CRLF and a `\n` would silently turn
# every such mutant into "NOT APPLIED", i.e. a test that proves nothing (check_t59_lint's note).
$mutants = @(
    @{ Name = 'M1 put the Resource field back before the Protected field'; File = 'cfg';
       Find = 'RowTemplate->SectionProperties.Add(TEXT("Protected"), ProtectedProp);';
       Repl = 'RowTemplate->SectionProperties.Add(TEXT("Resource"), ResourceProp);' },
    @{ Name = 'M2 return the bare leaf from the label function'; File = 'prot';
       Find = '    return MountRoot + TEXT(": ") + Leaf;'; Repl = '    return Leaf;' },
    @{ Name = 'M3 drop the full path from the row tooltip'; File = 'prot';
       Find = '            Section->Tooltip = FText::FromString(FString::Printf(';
       Repl = '            const bool bTooltipSkipped = true; (void)(' },
    @{ Name = 'M4 stop writing the path as the stored key'; File = 'prot';
       Find = '        ResourceProp->Value = Pair.Key;'; Repl = '        ResourceProp->Value = Pair.Value.DisplayName;' },
    @{ Name = 'M5 delete the round-trip census line'; File = 'prot';
       Find = 'TEXT("T65ROUNDTRIP first-sync: diskRowsRead %d diskRowsWithPathKey %d diskRowsMalformedShape ")';
       Repl = 'TEXT("first-sync: rows %d keyed %d malformed ")' },
    @{ Name = 'M6 restore the origin claim in the notice'; File = 'notice';
       Find = 'TEXT("Those nodes'' own classes are not the base game''s; the resources they carry can be\n")';
       Repl = 'TEXT("These are not part of the base game''s content, and the resources they carry are\n")' },
    @{ Name = 'M7 let the notice''s quoted title drift from the panel title'; File = 'notice';
       Find = 'Body += TEXT("\nEach one has been added, TICKED, to \"Protect Other Mods'' Nodes (Per\n")';
       Repl = 'Body += TEXT("\nEach one has been added, TICKED, to \"Protect Other Mods'' Stuff (Per\n")' },
    @{ Name = 'M8 drop the archetype-instancing pointer-identity guard'; File = 'prot';
       Find = '        if (bTemplateReadable && (ResourceProp == TemplateResource || ProtectedProp == TemplateProtected))';
       Repl = '        if (false)' },
    @{ Name = 'M9 rename a T60CENSUS field while adding T65''s'; File = 'prot';
       Find = 'TEXT("T60CENSUS latch: rowsInFileAtLatch %d untickedRows %d malformedRows %d blankRows %d ")';
       Repl = 'TEXT("T60CENSUS latch: rowsAtLatch %d untickedRows %d malformedRows %d blankRows %d ")' },
    @{ Name = 'M10 delete the fallback tally at the disk-row site (the census keeps its field)'; File = 'prot';
       Find = '        if (!bRowLabelParsed) { ++LabelFallbacksThisPass; }'; Repl = '        ' },
    @{ Name = 'M11 make the row template vertical so field order stops being visible'; File = 'cfg';
       Find = 'RowWidget->WidgetType = ECP_SectionWidgetType::CPS_Horizontal;';
       Repl = 'RowWidget->WidgetType = ECP_SectionWidgetType::CPS_Vertical;' },
    @{ Name = 'M12 drop the full path at the disk-row stamp site'; File = 'prot';
       Find = '        NodeShuffleStampRowLabel(Section, RowLabel, Path);';
       Repl = '        NodeShuffleStampRowLabel(Section, RowLabel, FString());' },
    # M13 exercises the ORDER pin itself, which M1 does not: M1 deletes the Protected add and is caught
    # by the vacuity half. This one leaves both adds in place and simply registers Resource FIRST -- the
    # actual regression (a "tidy the declarations" edit), with nothing missing afterwards.
    @{ Name = 'M13 register Resource before Protected, leaving both adds present'; File = 'cfg';
       Find = '        UConfigPropertyBool* ProtectedProp = NewObject<UConfigPropertyBool>(RowTemplate, BoolClass,';
       Repl = '        RowTemplate->SectionProperties.Add(TEXT("Resource"), ResourceProp); UConfigPropertyBool* ProtectedProp = NewObject<UConfigPropertyBool>(RowTemplate, BoolClass,' }
)

$missed = 0
foreach ($m in $mutants) {
    $c = $cfgRaw; $p = $protRaw; $n = $noticeRaw; $applied = $null
    switch ($m.File) {
        'cfg'    { $c = Replace-First $cfgRaw    $m.Find $m.Repl; $applied = $c }
        'prot'   { $p = Replace-First $protRaw   $m.Find $m.Repl; $applied = $p }
        'notice' { $n = Replace-First $noticeRaw $m.Find $m.Repl; $applied = $n }
    }
    if ($null -eq $applied) {
        Write-Host ("{0}: NOT APPLIED -- the text it edits was not found. The mutation test is stale, which means it is proving nothing." -f $m.Name)
        $missed++
        continue
    }
    $f = Get-T65Failures $c $p $n
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
