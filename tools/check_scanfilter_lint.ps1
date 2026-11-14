# check_scanfilter_lint.ps1 -- ns-scanfilter-1 (2026-11-14): per-resource SHOW/HIDE switches for modded resources in the scanner wheel.
# Handoff: _team/test-logs/2026-11-14-nodeshuffle-scanfilter-1-handoff.md
# LOADS every file the packet touched: NodeShuffleScanFilter.cpp/.h, NodeShuffle.cpp, NodeShuffleBuildMarkers.h, NodeShuffleConfig.cpp/.h,
# NodeShuffleSubsystem.cpp/.h, NodeShuffleMcpDescribe.cpp, and the lint it edited (mcpcontract).
#   S1  the hook: original first (Scope(Self)), the body's result overrides only when something was removed; SCANFILTER-HOOK logged
#   S2  filterable = modded only: the predicate (not /Game/, not the game's native module) and the vanilla keep-branch before any row lookup
#   S3  defaults: All Minable mount starts hidden, every other mod shown; a new row is created with that same rule
#   S4  a modded class with no row yet gets the same rule in the very call that sees it, and is recorded for a row
#   S5  original order: the hook only appends to Kept, never inserts, sorts or swap-removes
#   S6  the hook body (FilterScannableResources span) does no config walk, no config write and no disk IO
#   S7  no new code path unlocks a scanner resource or touches save data (new files + the hook in NodeShuffle.cpp)
#   S8  rows come from the UNION of the managed set (a) and the classes the hook saw (b); (b) is kept for the world session;
#       the periodic pass runs from RefreshTick BEFORE the Enabled early-out
#   S9  marker 2026-11-14-scanfilter-1: newest in the markers header, once in NodeShuffle.cpp, absent from the new files;
#       quietchat-1 stays listed; mcpcontract C4 names this marker
#   S10 config: registration right after UnlockModdedKnowledge, editable in a running world (false x4, no true), struct mirror,
#       required plain-words sentences, the old false knowledge sentence gone, the MCP manifest row
#   S11 diagnostics: SCANFILTER-CALL full (first 20) + census (5 s), SCANFILTER-ROWS loaded/changed + emptied, SCANFILTER-ROWADD
#   S12 the knowledge pass (UnlockModdedScannerKnowledge + ProvideKAPIMinerInfo) is byte-identical to the parent commit 187df97
# Forbidden needles are built by concatenation. Mutants run in-file. Exit 0 = all hold and every mutant is caught; 1 = failure; 2 = missing file.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    sf    = 'NodeShuffle\Private\NodeShuffleScanFilter.cpp'
    sfh   = 'NodeShuffle\Private\NodeShuffleScanFilter.h'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
    mark  = 'NodeShuffle\Private\NodeShuffleBuildMarkers.h'
    cfgc  = 'NodeShuffle\Private\NodeShuffleConfig.cpp'
    cfgh  = 'NodeShuffle\Public\NodeShuffleConfig.h'
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    subh  = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    desc  = 'NodeShuffle\Private\NodeShuffleMcpDescribe.cpp'
}
$lintFiles = [ordered]@{ lmcp = 'check_mcpcontract_lint.ps1' }
$text = [ordered]@{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = ((Get-Content -Raw -Encoding utf8 -Path $p) -replace "`r`n", "`n")
}
foreach ($k in $lintFiles.Keys) {
    $p = Join-Path $PSScriptRoot $lintFiles[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned lint '$($lintFiles[$k])' does not exist"; exit 2 }
    $text[$k] = ((Get-Content -Raw -Encoding utf8 -Path $p) -replace "`r`n", "`n")
}
function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }
function Ordered([string]$t, [string[]]$needles) {
    $at = 0
    foreach ($n in $needles) { $i = $t.IndexOf($n, $at); if ($i -lt 0) { return $false }; $at = $i + $n.Length }
    return $true
}
function Span([string]$t, [string]$a, [string]$b) {
    if ((Count $t $a) -ne 1) { return '' }
    $i = $t.IndexOf($a); $j = $t.IndexOf($b, $i + $a.Length)
    if ($j -lt 0) { return '' }
    return $t.Substring($i, $j - $i)
}
function Sha([string]$s) { return [Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($s))) }

$marker    = '2026-11-14-' + 'scanfilter-1'
$prevMark  = '2026-11-14-' + 'quietchat-1'
$hookHead  = 'bool FilterScannableResources(const AFGResourceScanner* Scanner, TArray<TSubclassOf<UFGResourceDescriptor>>& InOutList)' + "`n    {"
$tickHead  = 'void Tick(UObject* WorldContext, const TArray<FString>& ManagedModdedPaths, bool bModEnabled)' + "`n    {"
$subscribe = 'const FDelegateHandle ScanFilterHook = SUBSCRIBE_UOBJECT_METHOD(AFGResourceScanner, GetScannableResources,'
$runOrig   = 'TArray<TSubclassOf<UFGResourceDescriptor>> List = Scope(Self);'
$override  = 'if (NodeShuffleScanFilter::FilterScannableResources(Self, List)) { Scope.Override(List); }'
$predicate = 'return !ClassPath.IsEmpty() && !ClassPath.StartsWith(BaseGameContentRoot) && !ClassPath.StartsWith(BaseGameScriptRoot);'
$vanKeep   = 'if (!Info.bModded) { ++F.Vanilla; Kept.Add(Entry); continue; } // vanilla: never hidden, row or no row'
$rowFind   = 'if (const bool* RowShown = GRowShown.Find(Info.Path))'
$defRule   = 'return !ClassPath.StartsWith(AllMinableMountRoot);'
$amConst   = 'const TCHAR* const AllMinableMountRoot = TEXT("/AllMinable/"); // user decision 2026-11-14'
$noRowSeen = 'GSeenNoRow.Add(Info.Path); // source (b): the periodic pass gives it a row'
$noRowRule = 'bShown = DefaultShownForPath(Info.Path); // the same rule a new row is created with'
$keepShown = 'if (bShown) { Kept.Add(Entry); continue; }'
$commitOut = 'if (bRemovedAny) { InOutList = MoveTemp(Kept); }'
$newRowDef = 'const bool bDefaultShown = DefaultShownForPath(Path);'
$newRowSet = 'R.Show->Value = bDefaultShown;'
$wantA     = 'for (const FString& P : Managed) { Wanted.Add(P); }'
$wantB     = 'for (const FString& P : GSeenNoRow) { if (IsModdedPath(P) && !Managed.Contains(P)) { Wanted.Add(P); } }'
$tickCall  = 'ScanFilterTick(Config.Enabled);'
$cfgWrite  = @(('Mark' + 'Dirty'), ('MarkConfiguration' + 'Dirty'), ('FlushPending' + 'Saves'), ('GetConfigurationRoot' + 'Section'), ('AddNew' + 'Element'), ('->Value' + ' ='), ('FFile' + 'Helper'), ('IFile' + 'Manager'), ('Save' + 'Config'), ('Load' + 'Object'), ('Try' + 'Load'), ('ResolveC' + 'lass'))
$unlockTok = @(('UnlockScannable' + 'Resource'), ('FScannable' + 'ResourcePair'), ('GetUnlock' + 'Subsystem'), ('AFGUnlock' + 'Subsystem'), ('(Save' + 'Game'), ('SaveGame' + 'Subsystem'))
$orderBad  = @(('.Insert' + '('), ('RemoveAt' + 'Swap'), ('RemoveAll' + 'Swap'), ('.Sort' + '('), ('Algo::' + 'Sort'))
$knowHash  = '4E858636E0CDF8AAD25DF652BEEEAACAFF22BD2F54F7C51D4FA82C983AEC1CC1' # measured at 187df97 and at this packet (identical)
$oldKnow   = 'Shuffled modded resources are registered with the resource ' + 'scanner so scanners recognize them'
$fmts = @('TEXT("SCANFILTER-CALL full call=%d snapshotForThisWorld=%d sizeIn=%d sizeOut=%d hiddenByRow=%d hiddenByNoRowDefault=%d moddedSeen=%d vanillaSeen=%d rowsInSnapshot=%d removed=[%s]"),',
          'TEXT("SCANFILTER-CALL census callsSinceLastLine=%d callsTotal=%d ',
          'TEXT("SCANFILTER-ROWS %s rowsTotal=%d rowsInSnapshot=%d shown=%d hidden=%d malformed=%d blankPath=%d addedThisPass=%d perMount:%s"),',
          'TEXT("SCANFILTER-ROWS emptied: the list held %d row(s)',
          'TEXT("SCANFILTER-ROWADD resource=%s name=''%s'' nameFromItemName=%d mount=%s startsShown=%d source=%s"),')
$title = 'Show Modded Resources In ' + 'Scanner (Per Resource)'
$plain = @('A switch only hides or shows; it never makes a resource ',
           'A switch can only remove a resource from the scanner''s list, never add one: a resource that is not on the list stays absent even when its switch is ON.',
           'These switches apply to every world on this install, not to one save.')

function Invoke-Checks($T) {
    $f = @()
    $hook = Span $T.sf $hookHead '// ---- THE PERIODIC PASS ----'
    $tick = Span $T.sf $tickHead '// ---- THE ROWS ----'
    $reg  = Span $T.sf 'void RegisterConfigRows(UConfigPropertySection* Root,' "`n    }`n}"
    $refresh = Span $T.sub 'void ANodeShuffleSubsystem::RefreshTick()' 'RwgObserveTick();'
    $nsHook = Span $T.nsmod $subscribe 'SCANFILTER-HOOK target='
    # S1
    if ($nsHook -eq '' -or -not (Ordered $nsHook @('[](auto& Scope, const AFGResourceScanner* Self)', $runOrig, $override))) { $f += '(S1) the hook runs the original first and overrides only with the filtered list' }
    if ((Count $T.nsmod 'TEXT("SCANFILTER-HOOK target=AFGResourceScanner::GetScannableResources handlerRegistered=%d"),') -ne 1 -or (Count $T.nsmod 'ScanFilterHook.IsValid() ? 1 : 0);') -ne 1) { $f += '(S1) SCANFILTER-HOOK logs whether the hook was installed' }
    if ($hook -eq '' -or (Count $hook $commitOut) -ne 1 -or (Count $hook 'bRemovedAny = true;') -ne 1 -or (Count $hook 'return bRemovedAny;') -ne 1) { $f += '(S1) the body returns the filtered list and says whether it removed anything' }
    if ($hook -eq '' -or (Count $hook 'F.bSnapshot = World != nullptr && GSnapWorld.IsValid() && GSnapWorld.Get() == World;') -ne 1 -or -not (Ordered $hook @('if (F.bSnapshot)', $commitOut))) { $f += '(S1) no snapshot for the caller''s world leaves the list untouched' }
    # S2
    if ((Count $T.sf $predicate) -ne 1 -or (Count $T.sf 'const TCHAR* const BaseGameContentRoot = TEXT("/Game/");') -ne 1 -or (Count $T.sf 'const TCHAR* const BaseGameScriptRoot = TEXT("/Script/FactoryGame.");') -ne 1) { $f += '(S2) the modded predicate and the base-game roots' }
    if ($hook -eq '' -or (Count $hook $vanKeep) -ne 1 -or -not (Ordered $hook @($vanKeep, $rowFind))) { $f += '(S2) vanilla is kept before any row lookup' }
    if ((Count $T.sf 'Info.bModded = IsModdedPath(Info.Path);') -ne 1) { $f += '(S2) the class cache classifies with the modded predicate' }
    # S3
    if ((Count $T.sf $defRule) -ne 1 -or (Count $T.sf $amConst) -ne 1) { $f += '(S3) default: All Minable hidden, every other mod shown' }
    if ($tick -eq '' -or -not (Ordered $tick @($newRowDef, $newRowSet))) { $f += '(S3) a new row is created with the default rule' }
    # S4
    if ($hook -eq '' -or -not (Ordered $hook @($rowFind, 'else', $noRowSeen, $noRowRule))) { $f += '(S4) no-row modded classes get the default rule in the same call and are recorded for a row' }
    # S5
    if ($hook -eq '' -or (Count $hook 'Kept.Add(Entry);') -ne 3 -or (Count $hook $keepShown) -ne 1) { $f += '(S5) Kept is built by appending in the original order' }
    foreach ($n in $orderBad) { if ($hook -ne '' -and (Count $hook $n) -ne 0) { $f += "(S5) the hook must not reorder the list ($n)" } }
    # S6
    foreach ($n in $cfgWrite) { if ($hook -ne '' -and (Count $hook $n) -ne 0) { $f += "(S6) the hook body must not walk or write config or touch disk ($n)" } }
    if ($hook -ne '' -and ([regex]::Matches($hook, 'UE_LOG\(')).Count -ne 0) { $f += '(S6) the hook logs only through EmitCallLine / EmitCallCensus' }
    # S7
    foreach ($k in @('sf', 'sfh')) { foreach ($n in $unlockTok) { if ((Count $T[$k] $n) -ne 0) { $f += "(S7) no unlock or save access in the new files ($k $n)" } } }
    foreach ($n in $unlockTok) { if ($nsHook -ne '' -and (Count $nsHook $n) -ne 0) { $f += "(S7) no unlock or save access in the hook ($n)" } }
    # S8
    if ($tick -eq '' -or -not (Ordered $tick @($wantA, $wantB, 'Rows->AddNewElement()'))) { $f += '(S8) rows come from the union of the managed set and the classes the hook saw' }
    if ((Count $T.sf 'GSeenNoRow.Reset();') -ne 1 -or (Count $T.sf 'GSeenNoRow.Empty(') -ne 0) { $f += '(S8) the hook''s seen set is cleared only per world' }
    if ($refresh -eq '' -or -not (Ordered $refresh @('FNodeShuffleConfigStruct::GetActiveConfig(this);', $tickCall, 'if (!Config.Enabled)'))) { $f += '(S8) the periodic pass runs from RefreshTick before the Enabled early-out' }
    if ((Count $T.subh '    void ScanFilterTick(bool bModEnabled);') -ne 1 -or (Count $T.sf 'if (!E.bActive || !NodeShuffleScanFilter::IsModdedPath(E.AssignedResourceClassPath)) { continue; }') -ne 1) { $f += '(S8) the managed set is active modded layout entries through the same predicate' }
    # S9
    $hdrTags = @([regex]::Matches($T.mark, 'TEXT\("(20\d\d-\d\d-\d\d-[\w-]+)"\),') | ForEach-Object { $_.Groups[1].Value })
    if ($hdrTags.Count -eq 0 -or $hdrTags[-1] -ne $marker) { $f += '(S9) marker must be the newest entry in the markers header' }
    if ($hdrTags -notcontains $prevMark) { $f += '(S9) quietchat-1 stays listed in the markers header' }
    if ((Count $T.nsmod ('packet marker (' + $marker + ')=====')) -ne 1 -or (Count $T.nsmod ('packet marker (' + $prevMark + ')=====')) -ne 1) { $f += '(S9) both markers once on their own lines in NodeShuffle.cpp' }
    foreach ($k in @('sf', 'sfh')) { if ((Count $T[$k] $marker) -ne 0) { $f += "(S9) no marker inside the new file ($k)" } }
    if (-not $T.lmcp.Contains("`$own = '2026-11-14-' + 'scanfilter-1'")) { $f += '(S9) check_mcpcontract C4 must name this marker as the newest' }
    # S10
    $cfgKnow = Span $T.cfgc '    AddBool(TEXT("UnlockModdedKnowledge"), true,' 'AddBool(TEXT("ProtectOtherModsNodes"), true,'
    if ($cfgKnow -eq '' -or (Count $cfgKnow 'NodeShuffleScanFilter::RegisterConfigRows(Root, ArrayClass, SectionClass, StringClass, BoolClass);') -ne 1) { $f += '(S10) the rows are registered right after the knowledge option' }
    if ($reg -eq '' -or (Count $reg 'bRequiresWorldReload = false;') -ne 4 -or (Count $reg 'bRequiresWorldReload = true') -ne 0 -or (Count $reg 'Root->SectionProperties.Add(RowsKey, Rows);') -ne 1) { $f += '(S10) the list, its row and every field are editable in a running world' }
    if ((Count $T.sf 'const TCHAR* const RowsKey = TEXT("ScannerResourceVisibility");') -ne 1 -or (Count $T.cfgh 'TArray<FNodeShuffleScannerVisibilityRow> ScannerResourceVisibility;') -ne 1 -or (Count $T.cfgh 'struct NODESHUFFLE_API FNodeShuffleScannerVisibilityRow') -ne 1) { $f += '(S10) the config key and its struct mirror' }
    foreach ($s in $plain) { if ($reg -eq '' -or (Count $reg $s) -ne 1) { $f += "(S10) the list text must say: $s" } }
    if ((Count $T.cfgc $oldKnow) -ne 0) { $f += '(S10) the knowledge tooltip''s old first sentence is gone' }
    if ((Count $T.desc '{ TEXT("ScannerResourceVisibility"), TEXT("tick"), false, TEXT("none"),') -ne 1) { $f += '(S10) MCP manifest row' }
    if ((Count $T.sf ('Rows->DisplayName = FText::FromString(TEXT("' + $title + '"));')) -ne 1 -or (Count $T.cfgc ("use '" + $title + "'.")) -ne 1) { $f += '(S10) the knowledge tooltip quotes the list title exactly' }
    $oldRes = 'A resource you have not ' + 'researched will not appear'; if ((Count $T.sf $oldRes) -ne 0) { $f += '(S10) the false research sentence is gone' }
    # S11
    if ((Count $T.sf 'if ((RefusedOverCap > 0 || Withdrawn > 0) && (RefusedOverCap != GLastRefused || Withdrawn != GLastWithdrawn))') -ne 1 -or (Count $T.sf 'GLastRefused = RefusedOverCap; GLastWithdrawn = Withdrawn;') -ne 1) { $f += '(S11) the notAdded warning is latched on its figures' } # round 2, F5 pin: coder-written
    foreach ($x in $fmts) { if ((Count $T.sf $x) -ne 1) { $f += "(S11) diagnostic format: $x" } }
    if ((Count $T.sfh 'constexpr int32 FullCallLines = 20;') -ne 1 -or (Count $T.sfh 'constexpr double CallCensusPeriodSeconds = 5.0;') -ne 1) { $f += '(S11) 20 full lines, then a census at most every 5 s' }
    if ($hook -eq '' -or -not (Ordered $hook @('const bool bFullLine = GCallsTotal <= FullCallLines;', 'EmitCallLine(F, RemovedNames);', 'else if (Now - GLastCallLineTime >= CallCensusPeriodSeconds && (FNodeShuffleModule::AreDiagnosticsEnabled() || !SameFigures(GLatest, GLastCensus)))', 'EmitCallCensus(Now);'))) { $f += '(S11) the call lines are emitted from the hook' }
    if ($tick -eq '' -or (Count $tick 'if (GLastRowCount > 0 && Rows->Values.Num() == 0)') -ne 1) { $f += '(S11) an emptied list is measured and logged' }
    # S12
    $know = Span $T.sub 'bool ANodeShuffleSubsystem::UnlockModdedScannerKnowledge()' 'bool ANodeShuffleSubsystem::IsEligibleVanillaNode('
    if ($know -eq '' -or (Sha $know) -ne $knowHash) { $f += '(S12) the knowledge pass must stay byte-identical' }
    return ,$f
}

$fails = Invoke-Checks $text
foreach ($x in $fails) { Write-Host "FAIL$x" }

$mutants = @(
    @{ Name = 'M1 filter removed (original returned)'; Key = 'nsmod'; Expect = 'S1';  Find = $override; Repl = 'NodeShuffleScanFilter::FilterScannableResources(Self, List);' }
    @{ Name = 'M2 vanilla filterable';               Key = 'sf';    Expect = 'S2';  Find = $vanKeep; Repl = 'if (!Info.bModded) { ++F.Vanilla; }' }
    @{ Name = 'M3 predicate widened to vanilla';     Key = 'sf';    Expect = 'S2';  Find = $predicate; Repl = 'return !ClassPath.IsEmpty();' }
    @{ Name = 'M4 default inverted for All Minable';  Key = 'sf';    Expect = 'S3';  Find = $defRule; Repl = 'return true;' }
    @{ Name = 'M5 default inverted for other mods';   Key = 'sf';    Expect = 'S3';  Find = $defRule; Repl = 'return ClassPath.StartsWith(AllMinableMountRoot) && false;' }
    @{ Name = 'M6 new row ignores the default';       Key = 'sf';    Expect = 'S3';  Find = $newRowSet; Repl = 'R.Show->Value = true;' }
    @{ Name = 'M7 no-row left unfiltered';            Key = 'sf';    Expect = 'S4';  Find = $noRowRule; Repl = 'bShown = true;' }
    @{ Name = 'M8 hook writes config';                Key = 'sf';    Expect = 'S6';  Find = "        ++GCallsSinceLine;`n        FCallFigures F;"; Repl = ("        ++GCallsSinceLine; if (UConfigManager* Cm = nullptr) { Cm->" + 'FlushPending' + "Saves(); }`n        FCallFigures F;") }
    @{ Name = 'M9 unlock call added';                 Key = 'sf';    Expect = 'S7';  Find = "        GSnapWorld = World;`n"; Repl = ("        GSnapWorld = World; // Unlocks->" + 'UnlockScannable' + "Resource(Pair);`n") }
    @{ Name = 'M10 rows from the managed set only';   Key = 'sf';    Expect = 'S8';  Find = $wantB; Repl = '' }
    @{ Name = 'M11 order not preserved';              Key = 'sf';    Expect = 'S5';  Find = $keepShown; Repl = ('if (bShown) { Kept' + '.Insert(Entry, 0); continue; }') }
    @{ Name = 'M12 tick after the Enabled early-out'; Key = 'sub';   Expect = 'S8';  Find = "    $tickCall`n"; Repl = '' }
    @{ Name = 'M13 seen set drained per pass';        Key = 'sf';    Expect = 'S8';  Find = "        GSnapWorld = World;`n"; Repl = "        GSnapWorld = World; GSeenNoRow.Empty();`n" }
    @{ Name = 'M14 marker not newest';                Key = 'mark';  Expect = 'S9';  Find = ('        TEXT("' + $marker + '"),'); Repl = '' }
    @{ Name = 'M15 list greyed in a running world';   Key = 'sf';    Expect = 'S10'; Find = 'Rows->bRequiresWorldReload = false;'; Repl = 'Rows->bRequiresWorldReload = true;' }
    @{ Name = 'M16 research sentence dropped';        Key = 'sf';    Expect = 'S10'; Find = $plain[1]; Repl = '' }
    @{ Name = 'M17 old knowledge sentence back';      Key = 'cfgc';  Expect = 'S10'; Find = 'TEXT("ON: after a world loads,'; Repl = ('TEXT("' + $oldKnow + '. ON: after a world loads,') }
    @{ Name = 'M18 hook logs directly';               Key = 'sf';    Expect = 'S6';  Find = "        GLatest = F;`n"; Repl = "        GLatest = F; UE_LOG(LogNodeShuffle, Display, TEXT(`"x`"));`n" }
    @{ Name = 'M19 knowledge pass edited';            Key = 'sub';   Expect = 'S12'; Find = 'if (!Config.UnlockModdedKnowledge) { return true; }'; Repl = 'if (!Config.UnlockModdedKnowledge) { return false; }' }
    @{ Name = 'M20 hook installed log dropped';       Key = 'nsmod'; Expect = 'S1';  Find = 'ScanFilterHook.IsValid() ? 1 : 0);'; Repl = '1);' }
    @{ Name = 'M22 list title drifts from the quote';  Key = 'sf';    Expect = 'S10'; Find = ('TEXT("' + $title + '")'); Repl = 'TEXT("Scanner Visibility")' }
    @{ Name = 'M23 census made unconditional';         Key = 'sf';    Expect = 'S11'; Find = 'else if (Now - GLastCallLineTime >= CallCensusPeriodSeconds && (FNodeShuffleModule::AreDiagnosticsEnabled() || !SameFigures(GLatest, GLastCensus)))'; Repl = 'else if (Now - GLastCallLineTime >= CallCensusPeriodSeconds)' } # round 2, coder-written
    @{ Name = 'M24 notAdded latch removed';           Key = 'sf';    Expect = 'S11'; Find = 'if ((RefusedOverCap > 0 || Withdrawn > 0) && (RefusedOverCap != GLastRefused || Withdrawn != GLastWithdrawn))'; Repl = 'if (RefusedOverCap > 0 || Withdrawn > 0)' } # round 2, coder-written
    @{ Name = 'M25 old research sentence back';       Key = 'sf';    Expect = 'S10'; Find = 'TEXT("scannable. A switch can only remove'; Repl = ('TEXT("scannable. A resource you have not ' + 'researched will not appear. A switch can only remove') } # round 2, coder-written
    @{ Name = 'M21 census period widened';            Key = 'sfh';   Expect = 'S11'; Find = 'constexpr double CallCensusPeriodSeconds = 5.0;'; Repl = 'constexpr double CallCensusPeriodSeconds = 60.0;' }
)
$caught = 0
foreach ($m in $mutants) {
    $copy = [ordered]@{}; foreach ($k in $text.Keys) { $copy[$k] = $text[$k] }
    if (-not $copy[$m.Key].Contains($m.Find)) { Write-Host "FAIL(mutant) $($m.Name): anchor not found"; $fails += 'mutant-anchor'; continue }
    $copy[$m.Key] = $copy[$m.Key].Replace($m.Find, $m.Repl)
    $mf = Invoke-Checks $copy
    if (@($mf | Where-Object { $_.StartsWith('(' + $m.Expect + ')') }).Count -gt 0) { $caught++ }
    else { Write-Host "FAIL(mutant) $($m.Name) survived (expected $($m.Expect))"; $fails += 'mutant-survived' }
}

if ($fails.Count -eq 0) { Write-Host "PASS check_scanfilter_lint: S1-S12 hold; $caught/$($mutants.Count) mutants caught"; exit 0 }
Write-Host "check_scanfilter_lint: $($fails.Count) failure(s); $caught/$($mutants.Count) mutants caught"
exit 1
