# check_scanfilter_lint.ps1 -- ns-scanfilter-1 (2026-11-14): per-resource SHOW/HIDE switches for modded resources in the scanner wheel.
# Handoff: _team/test-logs/2026-11-14-nodeshuffle-scanfilter-1-handoff.md
# LOADS every file the packet touched: NodeShuffleScanFilter.cpp/.h, NodeShuffle.cpp, NodeShuffleBuildMarkers.h, NodeShuffleConfig.cpp/.h,
# NodeShuffleSubsystem.cpp/.h, NodeShuffleMcpDescribe.cpp, and the lint it edited (mcpcontract); ns-scanfilter-4 adds
# NodeShuffleScanFilterRows.cpp, NodeShuffleScanFilterInternal.h, NodeShuffleForeignProtectConfig.cpp, NodeShuffle.h and the veto requirement.
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
#   S15 (ns-scanfilter-4) ONE LIST: the switch is a field of the protection rows; every row the scanner side creates is ticked;
#       the latch and the veto conjunct are byte-identical to 9b4837d; Protected/Resource locked, Show live; the carry-over runs
#       once (hidden, non-resettable flag) and empties the hidden retired list; no duplicate rows; the starting-value rule;
#       a sighted resource's row is left to the protection pass; the protection pass never writes through the template's field
#   S14 (ns-scanfilter-3) PICK: the GetClosetClusters hook (NodeShuffleScanDiag.cpp, LOADED) sets the closest-pick flag right before the
#       original and clears it by ON_SCOPE_EXIT right after; SCANPING is kept; the IsNodeScannable hook runs the game first and only
#       ever overrides to true; ShouldAnswerScannable needs the flag, a snapshot for the pick world and an ON own-path row
#   S13 (ns-scanfilter-2) ADD: inside the snapshot branch, after the removal, ON rows the list lacks are appended in row order from
#       GAddOrder (weak classes resolved by Tick, ON + modded only, the only GAddOrder.Add); no duplicates; existing rows never rewritten
#   S12 the knowledge pass (UnlockModdedScannerKnowledge + ProvideKAPIMinerInfo) is byte-identical to the parent commit 187df97
#   S16 (ns-scanfilter-4 round 3, "One box, follows game"; LOADS NodeShuffleScanFilterState.cpp) only a PLAYER-SET row adds, removes or
#       answers the pick; an AUTO row is kept by the hook unless All Minable; touch detection runs before the mirror; the mirror writes
#       only an AUTO row's box, only to AutoShowValue, only when known, never Protected; carried rows and (after a lost record file)
#       existing rows are PLAYER-SET; a deleted row's record goes (pruned on the rows before the adds); the record is written by Tick after the config save, never by a hook
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
    sdiag = 'NodeShuffle\Private\NodeShuffleScanDiag.cpp'   # ns-scanfilter-3: the GetClosetClusters hook
    sfr   = 'NodeShuffle\Private\NodeShuffleScanFilterRows.cpp'      # ns-scanfilter-4: the row pass, carry-over, registration
    sfi   = 'NodeShuffle\Private\NodeShuffleScanFilterInternal.h'    # ns-scanfilter-4: shared state
    prot  = 'NodeShuffle\Private\NodeShuffleForeignProtectConfig.cpp' # ns-scanfilter-4: the protection rows' writer and latch
    nsh   = 'NodeShuffle\Public\NodeShuffle.h'                       # ns-scanfilter-4: the three entry points
    veto  = 'NodeShuffleVetoKBFL\Private\NodeShuffleDestroyerVetoRequirement.cpp' # ns-scanfilter-4: the consumption conjunct
    sfs   = 'NodeShuffle\Private\NodeShuffleScanFilterState.cpp'     # ns-scanfilter-4 round 3: AUTO / PLAYER-SET row states, the record file
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
$marker2   = '2026-11-14-' + 'scanfilter-2'
$marker3   = '2026-11-14-' + 'scanfilter-3'
$marker4   = '2026-11-14-' + 'scanfilter-4'
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
$noRowRule = 'bShown = DefaultShownForPath(Info.Path); // the hook''s no-row rule; an AUTO row gets the same rule'
$autoRule  = 'bShown = DefaultShownForPath(Info.Path); // AUTO row (round 3): the game''s entry stays, except All Minable'
$autoFind  = 'else if (GAutoRow.Contains(Info.Path))'
$snapMode  = 'if (bPlayerSet) { GRowShown.Add(Path, bShow); } else { GAutoRow.Add(Path); }'
$touchMirror = "        C.BecamePlayerSet = DetectTouches(Shows);`n        C.Mirrored = MirrorAutoRows(WorldContext, Shows, C.MirrorUnknown);"
$stateIo   = @(('FlushRowState' + 'RecordIfDirty'), ('SaveStringTo' + 'File'), ('UpdateRow' + 'States'), ('NoteCarried' + 'Row'), ('ResolveMissing' + 'RecordOnce'))
$keepShown = 'if (bShown) { Kept.Add(Entry); continue; }'
$commitOut = 'if (bRemovedAny || bAddedAny) { InOutList = MoveTemp(Kept); }'
$newRowDef = 'const bool bDefaultShown = AutoShowValue(World, Path, &Basis, nullptr);'
$newRowSet = 'if (!AppendRow(Rows, Tpl, bTplReadable, Path, bDefaultShown, Withdrawn)) { continue; }'
$wantA     = 'for (const FString& P : Managed) { Wanted.Add(P); }'
$wantB     = 'for (const FString& P : GSeenNoRow) { if (IsModdedPath(P) && !Managed.Contains(P)) { Wanted.Add(P); } }'
$tickCall  = 'ScanFilterTick(Config.Enabled);'
$cfgWrite  = @(('Mark' + 'Dirty'), ('MarkConfiguration' + 'Dirty'), ('FlushPending' + 'Saves'), ('GetConfigurationRoot' + 'Section'), ('AddNew' + 'Element'), ('->Value' + ' ='), ('FFile' + 'Helper'), ('IFile' + 'Manager'), ('Save' + 'Config'), ('Load' + 'Object'), ('Try' + 'Load'), ('ResolveC' + 'lass'))
$unlockTok = @(('UnlockScannable' + 'Resource'), ('FScannable' + 'ResourcePair'), ('GetUnlock' + 'Subsystem'), ('AFGUnlock' + 'Subsystem'), ('(Save' + 'Game'), ('SaveGame' + 'Subsystem'))
$orderBad  = @(('.Insert' + '('), ('RemoveAt' + 'Swap'), ('RemoveAll' + 'Swap'), ('.Sort' + '('), ('Algo::' + 'Sort'))
$knowHash  = '4E858636E0CDF8AAD25DF652BEEEAACAFF22BD2F54F7C51D4FA82C983AEC1CC1' # measured at 187df97 and at this packet (identical)
$oldKnow   = 'Shuffled modded resources are registered with the resource ' + 'scanner so scanners recognize them'
$fmts = @('TEXT("SCANFILTER-CALL full call=%d snapshotForThisWorld=%d sizeIn=%d sizeOut=%d hiddenByRow=%d hiddenByNoRowDefault=%d moddedSeen=%d vanillaSeen=%d rowsInSnapshot=%d removed=[%s] addedByRow=%d added=[%s]"),',
          'TEXT("SCANFILTER-CALL addedSetChanged call=%d addedByRow=%d added=[%s]"),',
          'TEXT("SCANFILTER-CALL census callsSinceLastLine=%d callsTotal=%d ',
          'TEXT("SCANFILTER-ROWS %s rowsTotal=%d rowsInSnapshot=%d shown=%d hidden=%d malformed=%d blankPath=%d addedThisPass=%d perMount:%s onRowsClassNotFound=%d onRowsFoundNotAddable=%d classNotFound=[%s] deferredToProtectionSync=%d autoRows=%d playerSetRows=%d becamePlayerSetThisPass=%d mirroredThisPass=%d mirrorValueUnknown=%d"),',
          'TEXT("SCANFILTER-STATE playerSet resource=%s value=%d recordedBefore=%d: ',
          'TEXT("SCANFILTER-STATE mirrored resource=%s old=%d new=%d basis=%s"),',
          'TEXT("SCANFILTER-STATE fileMissing path=%s"),',
          'TEXT("SCANFILTER-STATE fileUnreadable path=%s linesRead=%d: ',
          'TEXT("SCANFILTER-STATE fileRead path=%s records=%d playerSet=%d malformedLines=%d"),',
          'TEXT("SCANFILTER-STATE noRecordFile carriedThisPass=%d rowsBefore=%d rowsMadePlayerSet=%d rowsLeftAuto=%d"),',
          'TEXT("SCANFILTER-STATE recordDropped resource=%s wasPlayerSet=%d: no row has this path now"),',
          'TEXT("SCANFILTER-STATE recordDropped resource=%s wasPlayerSet=%d: the protection side created this row again"),',
          'TEXT("SCANFILTER-STATE fileWritten path=%s records=%d playerSet=%d"),',
          'TEXT("SCANFILTER-STATE fileWriteFailed path=%s: ',
          'TEXT("SCANFILTER-STATE carryOverPass recordsKept=%d recordsForgotten=%d: ',
          'TEXT("SCANFILTER-ROWS emptied: the list held %d row(s)',
          'TEXT("SCANFILTER-ROWADD resource=%s name=''%s'' nameFromItemName=%d mount=%s startsShown=%d source=%s startBasis=%s"),',
          'TEXT("SCANFILTER-CARRY done retiredRowsRead=%d retiredRowsUnreadable=%d valuesCopied=%d rowsCreated=%d rowsNotCreatedOverCap=%d rowsWithdrawn=%d otherRowsLeftAuto=%d retiredRowsEmptied=%d gameListRead=%d"),',
          'TEXT("SCANFILTER-CARRY none flagReachable=%d flagAlreadySet=%d retiredRowsPresent=%d: ')
$title = 'Show Modded Resources In ' + 'Scanner (Per Resource)' # retired title: must appear nowhere
$mtitle = "Protect Other Mods' Nodes (Per Resource)" # the one list
$latchHash = '8AA17E58455F6A8C0BE67C9DFE3945560270DA8922096E6C7AE6C33FA555B207' # measured at 9b4837d and at this packet (identical)
$consHash  = '71D42A42455D4EFD28B685B5822D76FDB792492D7B6149DF2873B4D74862CCB9' # IsForeignResourceProtectedByConfig, same
$plain = @('resource to the list of resources it hands to the resource scanner, including a resource you ', 'have not researched. OFF: NodeShuffle takes it out of that list. When you scan for an ON ',
           'found. Nothing is written to your save: the switches change only what NodeShuffle tells the ',
           'In multiplayer only the host''s game applies it. These switches apply to every world on ', # ns-scanfilter-4: the merged list's tooltip
           'SHOW IN SCANNER takes effect in a running game: reopen the scanner. UNTIL YOU CHANGE A ROW''S ',
           'BOX, NodeShuffle leaves that resource''s scanner entry to the game and only keeps the box ',
           'are kept out of the list NodeShuffle hands the scanner. ONCE YOU CHANGE A BOX, it stays as you set it, except a change made to a brand-new row in the few seconds before NodeShuffle first checks it. ON: NodeShuffle adds the ',
           'ticked and following the game''s scanner list again, the next time NodeShuffle sees that ',
           'this install, not to one save. A box carried over from the old scanner list counts as changed, ') # round 3: the two row states

function Invoke-Checks($T) {
    $f = @()
    $hook = Span $T.sf $hookHead '// ---- ns-scanfilter-3: THE CLOSEST-PICK ANSWER.'
    $tick = Span $T.sfr $tickHead '// ---- THE FIELD AND THE RETIRED ROWS ----'
    $reg  = Span $T.sfr 'void RegisterConfigRows(UConfigPropertySection* Root,' '// Source (a): distinct modded'
    $sfAll = $T.sf + $T.sfr + $T.sfs
    $append = Span $T.sfr 'bool AppendRow(' 'void CaptureStartContext('
    $carry = Span $T.sfr 'bool RunCarryOverOnce(' 'bool AutoShowValue(const UObject* WorldContext'
    $start = Span $T.sfr 'bool AutoShowValue(const UObject* WorldContext' '// ---- THE PERIODIC PASS ----'
    $touch = Span $T.sfs 'int32 DetectTouches(' 'int32 MirrorAutoRows('
    $mirror = Span $T.sfs 'int32 MirrorAutoRows(' 'void EnsureRowStateRecordLoaded()'
    $load = Span $T.sfs 'void EnsureRowStateRecordLoaded()' 'void NoteCarriedRow('
    $noted = Span $T.sfs 'void NoteCarriedRow(' 'void ResolveMissingRecordOnce('
    $resolve = Span $T.sfs 'void ResolveMissingRecordOnce(' 'FRowStateCounts UpdateRowStates('
    $update = Span $T.sfs 'FRowStateCounts UpdateRowStates(' 'int32 DropRecordsWithoutRows('
    $drop = Span $T.sfs 'int32 DropRecordsWithoutRows(' 'bool InGameNameIfLoaded('
    $refresh = Span $T.sub 'void ANodeShuffleSubsystem::RefreshTick()' 'RwgObserveTick();'
    $nsHook = Span $T.nsmod $subscribe 'SCANFILTER-HOOK target='
    # S1
    if ($nsHook -eq '' -or -not (Ordered $nsHook @('[](auto& Scope, const AFGResourceScanner* Self)', $runOrig, $override))) { $f += '(S1) the hook runs the original first and overrides only with the filtered list' }
    if ((Count $T.nsmod 'TEXT("SCANFILTER-HOOK target=AFGResourceScanner::GetScannableResources handlerRegistered=%d"),') -ne 1 -or (Count $T.nsmod 'ScanFilterHook.IsValid() ? 1 : 0);') -ne 1) { $f += '(S1) SCANFILTER-HOOK logs whether the hook was installed' }
    if ($hook -eq '' -or (Count $hook $commitOut) -ne 1 -or (Count $hook 'bRemovedAny = true;') -ne 1 -or (Count $hook 'return bRemovedAny || bAddedAny;') -ne 1) { $f += '(S1) the body returns the filtered list and says whether it removed anything' }
    if ($hook -eq '' -or (Count $hook 'F.bSnapshot = World != nullptr && GSnapWorld.IsValid() && GSnapWorld.Get() == World;') -ne 1 -or -not (Ordered $hook @('if (F.bSnapshot)', $commitOut))) { $f += '(S1) no snapshot for the caller''s world leaves the list untouched' }
    # S2
    if ((Count $T.sf $predicate) -ne 1 -or (Count $T.sf 'const TCHAR* const BaseGameContentRoot = TEXT("/Game/");') -ne 1 -or (Count $T.sf 'const TCHAR* const BaseGameScriptRoot = TEXT("/Script/FactoryGame.");') -ne 1) { $f += '(S2) the modded predicate and the base-game roots' }
    if ($hook -eq '' -or (Count $hook $vanKeep) -ne 1 -or -not (Ordered $hook @($vanKeep, $rowFind))) { $f += '(S2) vanilla is kept before any row lookup' }
    if ((Count $T.sf 'Info.bModded = IsModdedPath(Info.Path);') -ne 1) { $f += '(S2) the class cache classifies with the modded predicate' }
    # S3
    if ((Count $T.sf $defRule) -ne 1 -or (Count $T.sf $amConst) -ne 1) { $f += '(S3) default: All Minable hidden, every other mod shown' }
    if ($tick -eq '' -or -not (Ordered $tick @($newRowDef, $newRowSet)) -or $append -eq '' -or (Count $append 'R.Show->Value = bShow;') -ne 1) { $f += '(S3) a new row is created with the starting-value rule' }
    # S4
    if ($hook -eq '' -or -not (Ordered $hook @($rowFind, 'else', $noRowSeen, $noRowRule))) { $f += '(S4) no-row modded classes get the default rule in the same call and are recorded for a row' }
    # S5
    if ($hook -eq '' -or (Count $hook 'Kept.Add(Entry);') -ne 3 -or (Count $hook $keepShown) -ne 1) { $f += '(S5) Kept is built by appending in the original order' }
    foreach ($n in $orderBad) { if ($hook -ne '' -and (Count $hook $n) -ne 0) { $f += "(S5) the hook must not reorder the list ($n)" } }
    # S6
    foreach ($n in $cfgWrite) { if ($hook -ne '' -and (Count $hook $n) -ne 0) { $f += "(S6) the hook body must not walk or write config or touch disk ($n)" } }
    if ($hook -ne '' -and ([regex]::Matches($hook, 'UE_LOG\(')).Count -ne 0) { $f += '(S6) the hook logs only through EmitCallLine / EmitCallCensus' }
    # S7
    foreach ($k in @('sf', 'sfh', 'sfi', 'sfs')) { foreach ($n in $unlockTok) { if ((Count $T[$k] $n) -ne 0) { $f += "(S7) no unlock or save access in the new files ($k $n)" } } }
    foreach ($n in @(('UnlockScannable' + 'Resource'), ('(Save' + 'Game'), ('SaveGame' + 'Subsystem'))) { if ((Count $T.sfr $n) -ne 0) { $f += "(S7) no unlock or save access in the rows file ($n)" } }
    if ((Count $T.sfr ('GetUnlock' + 'Subsystem()')) -ne 1 -or (Count $T.sfr 'Unlocks->') -ne 2 -or (Count $T.sfr 'Unlocks->GetScannableResources()') -ne 1 -or (Count $T.sfr 'Unlocks->GetScannableResourcePairs()') -ne 1) { $f += '(S7) the rows file only READS the scannable list, at one site' }
    foreach ($n in $unlockTok) { if ($nsHook -ne '' -and (Count $nsHook $n) -ne 0) { $f += "(S7) no unlock or save access in the hook ($n)" } }
    # S8
    if ($tick -eq '' -or -not (Ordered $tick @($wantA, $wantB, $newRowSet))) { $f += '(S8) rows come from the union of the managed set and the classes the hook saw' }
    if ((Count $sfAll 'GSeenNoRow.Reset();') -ne 1 -or (Count $sfAll 'GSeenNoRow.Empty(') -ne 0) { $f += '(S8) the hook''s seen set is cleared only per world' }
    if ($refresh -eq '' -or -not (Ordered $refresh @('FNodeShuffleConfigStruct::GetActiveConfig(this);', $tickCall, 'if (!Config.Enabled)'))) { $f += '(S8) the periodic pass runs from RefreshTick before the Enabled early-out' }
    if ((Count $T.subh '    void ScanFilterTick(bool bModEnabled);') -ne 1 -or (Count $T.sfr 'if (!E.bActive || !NodeShuffleScanFilter::IsModdedPath(E.AssignedResourceClassPath)) { continue; }') -ne 1) { $f += '(S8) the managed set is active modded layout entries through the same predicate' }
    # S9
    $hdrTags = @([regex]::Matches($T.mark, 'TEXT\("(20\d\d-\d\d-\d\d-[\w-]+)"\),') | ForEach-Object { $_.Groups[1].Value })
    # ns-rollfirst-1 (2026-11-14): scanfilter-4 is no longer the newest; it stays listed (the newest is pinned by check_rollfirst_lint R9 and check_mcpcontract C4).
    if ($hdrTags.Count -eq 0 -or $hdrTags -notcontains $marker4 -or $hdrTags -notcontains $marker -or $hdrTags -notcontains $marker2 -or $hdrTags -notcontains $marker3) { $f += '(S9) scanfilter-4, -1, -2 and -3 stay listed' }
    if ((Count $T.nsmod ('packet marker (' + $marker4 + ')=====')) -ne 1) { $f += '(S9) scanfilter-4 once on its own line in NodeShuffle.cpp' }
    if ((Count $T.nsmod ('packet marker (' + $marker3 + ')=====')) -ne 1) { $f += '(S9) scanfilter-3 once on its own line in NodeShuffle.cpp' }
    if ((Count $T.nsmod ('packet marker (' + $marker2 + ')=====')) -ne 1) { $f += '(S9) scanfilter-2 once on its own line in NodeShuffle.cpp' }
    if ($hdrTags -notcontains $prevMark) { $f += '(S9) quietchat-1 stays listed in the markers header' }
    if ((Count $T.nsmod ('packet marker (' + $marker + ')=====')) -ne 1 -or (Count $T.nsmod ('packet marker (' + $prevMark + ')=====')) -ne 1) { $f += '(S9) both markers once on their own lines in NodeShuffle.cpp' }
    foreach ($k in @('sf', 'sfh', 'sfr', 'sfi', 'sfs')) { if ((Count $T[$k] $marker) -ne 0 -or (Count $T[$k] $marker2) -ne 0 -or (Count $T[$k] $marker3) -ne 0 -or (Count $T[$k] $marker4) -ne 0) { $f += "(S9) no marker inside the new file ($k)" } }
    if (-not $T.lmcp.Contains("`$own = '2026-11-20-' + 'finalflag-1'")) { $f += '(S9) check_mcpcontract C4 must name finalflag-1 as the newest (ns-finalflag-1 rotated it from quietlog-1)' }
    # S10
    $cfgKnow = Span $T.cfgc '    AddBool(TEXT("UnlockModdedKnowledge"), true,' 'AddBool(TEXT("ProtectOtherModsNodes"), true,'
    if ($cfgKnow -eq '' -or (Count $cfgKnow 'NodeShuffleScanFilter::RegisterConfigRows(Root, ArrayClass, SectionClass, StringClass, BoolClass);') -ne 1) { $f += '(S10) the rows are registered right after the knowledge option' }
    if ($reg -eq '' -or (Count $reg 'bRequiresWorldReload = true') -ne 0 -or (Count $reg 'Root->SectionProperties.Add(RetiredRowsKey, Retired);') -ne 1) { $f += '(S10) the retired list is registered under its old key' }
    if ((Count $T.sfr 'const TCHAR* const RetiredRowsKey = TEXT("ScannerResourceVisibility");') -ne 1 -or (Count $T.cfgh 'TArray<FNodeShuffleScannerVisibilityRow> ScannerResourceVisibility;') -ne 1 -or (Count $T.cfgh 'struct NODESHUFFLE_API FNodeShuffleScannerVisibilityRow') -ne 1) { $f += '(S10) the config key and its struct mirror' }
    foreach ($s in $plain) { if ((Count $T.cfgc $s) -ne 1) { $f += "(S10) the list text must say: $s" } }
    if ((Count $T.cfgc $oldKnow) -ne 0) { $f += '(S10) the knowledge tooltip''s old first sentence is gone' }
    if ((Count $T.desc '{ TEXT("ScannerResourceVisibility"), TEXT("tick"), false, TEXT("none"),') -ne 1 -or (Count $T.desc '{ TEXT("ScannerSwitchesMerged"), TEXT("tick"), false, TEXT("none"),') -ne 1 -or (Count $T.desc 'Array of rows {Protected, ShowInScanner, Resource}; the live, touchesSave and saveEffect columns describe Protected.') -ne 1) { $f += '(S10) MCP manifest rows' }
    if ((Count $T.cfgc ('Rows->DisplayName = FText::FromString(TEXT("' + $mtitle + '"));')) -ne 1 -or (Count $T.cfgc ("use 'Show in scanner' in '" + $mtitle + "'.")) -ne 1 -or (Count ($sfAll + $T.cfgc) $title) -ne 0) { $f += '(S10) the knowledge tooltip quotes the list title exactly' }
    foreach ($o in @(('and so does a ' + 'deleted row'), ('not while it ' + 'is paused'), ('Reset empties ' + 'the list'), ('on a dedicated ' + 'server none is'))) { if ((Count ($sfAll + $T.cfgc) $o) -ne 0) { $f += "(S10) unmeasured tooltip clause is gone ($o)" } }
    foreach ($o in @(('A new row starts ON for a ' + 'modded resource'), ('with its starting ' + 'scanner value'), ('Starting' + 'ShowValue'))) { if ((Count ($sfAll + $T.cfgc + $T.prot + $T.desc) $o) -ne 0) { $f += "(S10) the retired starting-value rule is gone ($o)" } }
    $oldRes = 'A resource you have not ' + 'researched will not appear'; if ((Count ($sfAll + $T.cfgc) $oldRes) -ne 0) { $f += '(S10) the false research sentence is gone' }
    # S11
    if ($tick -eq '' -or (Count $tick 'Now - GLastCallLineTime >= CallCensusPeriodSeconds && (FNodeShuffleModule::AreDiagnosticsEnabled() || !SameFigures(GLatest, GLastCensus))) { EmitCallCensus(Now); }') -ne 1) { $f += '(S11) the tick-side census is gated like the hook-side one' }
    if ((Count $T.sfr 'if ((RefusedOverCap > 0 || Withdrawn > 0) && (RefusedOverCap != GLastRefused || Withdrawn != GLastWithdrawn))') -ne 1 -or (Count $T.sfr 'GLastRefused = RefusedOverCap; GLastWithdrawn = Withdrawn;') -ne 1) { $f += '(S11) the notAdded warning is latched on its figures' } # round 2, F5 pin: coder-written
    foreach ($x in $fmts) { if ((Count $sfAll $x) -ne 1) { $f += "(S11) diagnostic format: $x" } }
    if ((Count $T.prot 'TEXT("SCANFILTER-PROTROW resource=%s showFieldWritten=%d showStartsOn=%d startBasis=%s"),') -ne 1) { $f += '(S11) diagnostic format: SCANFILTER-PROTROW' }
    if ((Count $T.sfh 'constexpr int32 FullCallLines = 20;') -ne 1 -or (Count $T.sfh 'constexpr double CallCensusPeriodSeconds = 5.0;') -ne 1) { $f += '(S11) 20 full lines, then a census at most every 5 s' }
    if ($hook -eq '' -or -not (Ordered $hook @('const bool bFullLine = GCallsTotal <= FullCallLines;', 'EmitCallLine(F, RemovedNames, AddedNames);', 'else if (Now - GLastCallLineTime >= CallCensusPeriodSeconds && (FNodeShuffleModule::AreDiagnosticsEnabled() || !SameFigures(GLatest, GLastCensus)))', 'EmitCallCensus(Now);'))) { $f += '(S11) the call lines are emitted from the hook' }
    if ($tick -eq '' -or (Count $tick 'if (GLastRowCount > 0 && Rows->Values.Num() == 0)') -ne 1) { $f += '(S11) an emptied list is measured and logged' }
    foreach ($o in @(('A switch only hides' + ' or shows'), ('never add' + ' one: a resource'), ('never makes the resource ' + 'scannable'), ('To hide modded resources from the ' + 'scanner instead'), ('stays in the list the resource ' + 'scanner offers'), ('edit it and the row ' + 'matches nothing'), ('the list the resource ' + 'scanner offers'))) { if ((Count $sfAll $o) + (Count $T.cfgc $o) -ne 0) { $f += "(S10) remove-only wording is gone ($o)" } }
    if ((Count $T.sf 'latestAddedByRow=%d"),') -ne 1) { $f += '(S11) census carries latestAddedByRow' }
    if ((Count $T.sfr 'ON: NodeShuffle adds this resource to the list it hands the resource scanner, even if') -ne 1 -or (Count $T.sfr 'Has no effect on a base-game resource.') -ne 1) { $f += '(S10) row switch text' }
    if ((Count $sfAll ('the path you ' + 'typed')) -ne 0) { $f += '(S10) the Resource field no longer claims the typed path is used' }
    if ((Count $hook 'if (F.bSnapshot && AddedNames != GLastAddedNames) { if (!bFullLine) { EmitAddedSetChanged(AddedNames, F.AddedByRow); } GLastAddedNames = AddedNames; }') -ne 1) { $f += '(S11) a changed added set is named after the full lines' }
    # S13
    if ($hook -eq '' -or -not (Ordered $hook @('Present.Contains(AddCls)) { continue; }', 'const bool* AddRow = GRowShown.Find(AddInfo.Path);', 'if (!AddInfo.bModded || AddRow == nullptr || !*AddRow) { continue; }', 'Present.Add(AddCls);'))) { $f += '(S13) the add step uses the removal''s key: own path modded with an ON row' }
    $addLoop = 'for (const TWeakObjectPtr<UClass>& Weak : GAddOrder)'
    if ($hook -eq '' -or -not (Ordered $hook @('if (F.bSnapshot)', 'Present.Add(Cls);', $vanKeep, $keepShown, 'Present.Remove(Cls);', $addLoop, 'UClass* AddCls = Weak.Get();', 'if (!AddCls || Present.Contains(AddCls)) { continue; }', 'Present.Add(AddCls);', 'Kept.Add(TSubclassOf<UFGResourceDescriptor>(AddCls));', '++F.AddedByRow;', $commitOut))) { $f += '(S13) the add step appends absent ON-row classes inside the snapshot branch, after the removal, without duplicates' }
    if ((Count $sfAll 'GAddOrder.Add(') -ne 1 -or (Count $T.sf 'TArray<TWeakObjectPtr<UClass>> GAddOrder;') -ne 1 -or (Count $sfAll 'GAddOrder.Reset();') -ne 2) { $f += '(S13) GAddOrder is weak, filled only by Tick, cleared per pass and per world' }
    if ($tick -eq '' -or -not (Ordered $tick @('GAddOrder.Reset();', 'if (!IsModdedPath(Path)) { continue; }', 'const bool bShow = R.Show->Value;', 'if (bShow && bPlayerSet)', 'UClass* RowCls = FSoftClassPath(Path).ResolveClass();', '&& !RowCls->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) { GAddOrder.Add(RowCls); }', 'else if (RowCls) { ++OnFoundNotAddable; }'))) { $f += '(S13) only ON, modded, resolved descriptor rows enter GAddOrder' }
    $vw = '->Value' + ' ='
    if ($tick -eq '' -or $append -eq '' -or $carry -eq '' -or (Count $tick $vw) -ne 0 -or (Count $append $vw) -ne 3 -or (Count $carry $vw) -ne 2 -or (Count $T.sfr.Substring(0, [Math]::Max(0, $T.sfr.IndexOf('// ---- THE FIELD AND THE RETIRED ROWS ----'))) $vw) -ne 5 -or (Count $T.sfr 'Rows->AddNewElement()') -ne 1) { $f += '(S13) the row pass writes values only into rows it has just created (and once, in the carry-over); round 3: its only other write is the mirror (S16)' }
    # S15 (ns-scanfilter-4) ONE LIST
    $latch = Span $T.prot 'void FNodeShuffleModule::LatchForeignResourceOptOutsFromConfig(UObject* WorldContext)' '// ---- T67 ITEM C'
    $cons  = Span $T.prot 'bool FNodeShuffleModule::IsForeignResourceProtectedByConfig(' 'void FNodeShuffleModule::GetSeenForeignResources('
    if ($latch -eq '' -or (Sha $latch) -ne $latchHash -or $cons -eq '' -or (Sha $cons) -ne $consHash) { $f += '(S15) the protection latch and its consumption predicate are byte-identical to 9b4837d' }
    if ((Count $T.veto "bResourceProtectedByConfig = bNodeClassIsVanilla`n                    || FNodeShuffleModule::IsForeignResourceProtectedByConfig(ForeignResourcePath);") -ne 1 -or (Count $T.veto 'ShowInScanner') -ne 0) { $f += '(S15) the veto conjunct is unchanged and never reads the switch' }
    if ($append -eq '' -or (Count $append 'R.Protected->Value = true;') -ne 1 -or (Count $T.sfr 'Protected->Value') -ne 1) { $f += '(S15) a row the scanner side creates is ticked, and nothing else in the rows file writes Protected' }
    $t60 = Span $T.cfgc 'TEXT("ProtectedForeignResources"));' 'Root->SectionProperties.Add(TEXT("ProtectedForeignResources"), Rows);'
    if ($t60 -eq '' -or (Count $t60 'Rows->bAllowUserReset = false;') -ne 1) { $f += '(S15) the per-resource list cannot be Reset by the player' }
    if ($t60 -eq '' -or (Count $t60 'Rows->bRequiresWorldReload = false;') -ne 1 -or (Count $t60 'RowTemplate->bRequiresWorldReload = false;') -ne 1 -or (Count $t60 'ProtectedProp->bRequiresWorldReload = true; // F10') -ne 1 -or (Count $t60 'ResourceProp->bRequiresWorldReload = true; // F10') -ne 1 -or -not (Ordered $t60 @('RowTemplate->SectionProperties.Add(TEXT("Protected"), ProtectedProp);', 'NodeShuffleScanFilter::AddShowFieldToRowTemplate(RowTemplate, BoolClass);', 'RowTemplate->SectionProperties.Add(TEXT("Resource"), ResourceProp);'))) { $f += '(S15) Protected and Resource locked in a running world, the array and row not, Show between them' }
    $showF = Span $T.sfr 'void AddShowFieldToRowTemplate(' 'void RegisterConfigRows('
    if ($showF -eq '' -or (Count $showF 'Show->bRequiresWorldReload = false;') -ne 1 -or (Count $showF 'bRequiresWorldReload = true') -ne 0) { $f += '(S15) Show in scanner is editable in a running world' }
    if ($carry -eq '' -or -not (Ordered $carry @('if (!Flag || Flag->Value)', 'return false;', 'ByPath.Find(Path)', 'AppendRow(', 'NoteCarriedRow(Path, OldShow->Value);', 'Retired->Values.Empty();', 'Flag->Value = true;', 'return true;')) -or (Count $carry 'Flag->Value = true;') -ne 1) { $f += '(S15) the carry-over runs only while the flag is false, copies onto existing rows first, empties the retired list and sets the flag' }
    if ($reg -eq '' -or (Count $reg 'Retired->bHidden = true;') -ne 1 -or (Count $reg 'Flag->bHidden = true;') -ne 1 -or (Count $reg 'Flag->bAllowUserReset = false;') -ne 1 -or (Count $reg 'Root->SectionProperties.Add(MergedFlagKey, Flag);') -ne 1) { $f += '(S15) the retired list and the flag are hidden; the flag cannot be reset' }
    if ($tick -eq '' -or -not (Ordered $tick @('const bool bCarried = RunCarryOverOnce(', 'if (Existing.Contains(Path)) { continue; }', 'if (FNodeShuffleModule::IsForeignRowPendingFromProtectionSync(Path))', 'if (++DeferPasses <= MaxDeferPasses) { ++Deferred; continue; }', 'if (Rows->Values.Num() >= MaxRows)', $newRowSet))) { $f += '(S15) no duplicate row; a sighted resource is left to the protection pass' }
    if ($start -eq '' -or -not (Ordered $start @('bool bOn = false;', 'bool bKnown = true;', 'if (IsModdedPath(Path) && !DefaultShownForPath(Path)) { Basis = TEXT("allMinable"); }', 'GStartWorld.Get() != World) { bKnown = false; Basis = TEXT("noContextForThisWorld"); }', 'else if (!GStartGameListRead) { bKnown = false; Basis = TEXT("gameListUnread"); }', 'else if (GStartGameList.Contains(Path)) { bOn = true; Basis = TEXT("gameScannableList"); }', 'return bOn;')) -or (Count $start 'bOn = true') -ne 1) { $f += '(S15) the AUTO value: OFF for All Minable, else ON exactly when the read scannable list contains the path; unknown while unread' }
    if ((Count $T.prot 'const bool bShowWritten = ShowProp != nullptr && ShowProp != TemplateShow;') -ne 1 -or (Count $T.prot 'if (bShowWritten) { ShowProp->Value = NodeShuffleScanFilter::AutoShowValue(WorldContext, Pair.Key, &ShowBasis, nullptr); }') -ne 1 -or (Count $T.prot 'if (ExistingByPath.Contains(Pair.Key)) { continue; }') -ne 1) { $f += '(S15) the protection pass gives a new row the same starting value and never writes through the template' }
    if ((Count $T.nsh 'static bool IsForeignRowPendingFromProtectionSync(const FString& ResourceClassPath);') -ne 1 -or (Count $T.prot 'return GNodeShuffleLastSyncedRevision != GNodeShuffleSeenForeignRevision && GNodeShuffleSeenForeignResources.Contains(ResourceClassPath);') -ne 1) { $f += '(S15) the scanner side defers only while a protection pass is pending for a sighted resource' }
    if ($tick -eq '' -or -not (Ordered $tick @('CaptureStartContext(World);', 'if (CarryFlag && !CarryFlag->Value && !GStartGameListRead)', 'GSnapWorld.Reset();', 'return;', 'const bool bCarried = RunCarryOverOnce('))) { $f += '(S15) the carry-over and the snapshot wait for the scannable list' }
    if ($tick -eq '' -or -not (Ordered $tick @('if (!AppendRow(Rows, Tpl, bTplReadable, Path, bDefaultShown, Withdrawn)) { continue; }', '++Added;', 'if (Added > 0 || bCarried) { FNodeShuffleModule::SortResourceRows(Rows, TEXT("scanner-add")); }', 'Rows->MarkDirty();'))) { $f += '(S15) the scanner side sorts after every append, before the save' }
    # S16 (round 3) AUTO / PLAYER-SET
    if ($tick -eq '' -or -not (Ordered $tick @('const bool bShow = R.Show->Value;', 'const bool bPlayerSet = IsRowPlayerSet(Path);', $snapMode, 'if (bShow && bPlayerSet)', 'GAddOrder.Add(RowCls)')) -or (Count $sfAll 'GRowShown.Add(') -ne 1 -or (Count $sfAll 'GAutoRow.Add(') -ne 1) { $f += '(S16) only a PLAYER-SET row enters GRowShown and GAddOrder; an AUTO row enters GAutoRow only' }
    if ($hook -eq '' -or -not (Ordered $hook @($rowFind, $autoFind, $autoRule, 'else', $noRowSeen, $noRowRule)) -or (Count $hook $autoRule) -ne 1 -or (Count $hook 'GSeenNoRow.Add(') -ne 1 -or (Count $hook 'GAutoRow') -ne 1) { $f += '(S16) the hook keeps an AUTO row''s entry unless All Minable, and adds nothing for it' }
    $answer16 = Span $T.sf 'bool ShouldAnswerScannable(UClass* ResourceClass, bool bGameAnswer)' 'void EmitPickLine('
    if ($answer16 -eq '' -or (Count $answer16 'GAutoRow') -ne 0) { $f += '(S16) the pick answer never reads the AUTO set' }
    if ($touch -eq '' -or -not (Ordered $touch @('if (!Rec || Rec->bPlayerSet) { continue; }', 'if (Row.Show->Value == Rec->bLastValue) { continue; }', 'Rec->bPlayerSet = true;')) -or (Count $T.sfs $touchMirror) -ne 1) { $f += '(S16) touch detection runs before the mirror and makes a changed box PLAYER-SET' }
    if ($mirror -eq '' -or -not (Ordered $mirror @('if (Rec && Rec->bPlayerSet) { continue; }', 'const bool bMirror = AutoShowValue(WorldContext, Row.Path, &Basis, &bKnown);', 'if (!bKnown) { ++OutUnknown; continue; }', 'if (Row.Show->Value == bMirror) { continue; }', 'Row.Show->Value = bMirror;')) -or (Count $T.sfs ('->Value' + ' = ')) -ne 1 -or (Count $mirror 'Row.Show->Value = bMirror;') -ne 1 -or (Count $T.sfs ('Protected' + '->')) -ne 0) { $f += '(S16) the mirror writes only an AUTO row''s box, only to the known AUTO value, and nothing else in the state file writes a value' }
    if ($noted -eq '' -or (Count $noted 'Rec.bPlayerSet = true;') -ne 1) { $f += '(S16) a carried-over row is recorded PLAYER-SET' }
    if ($resolve -eq '' -or -not (Ordered $resolve @('if (GMissingResolved || !GRecordsLoaded) { return; }', 'GRecordsDirty = true;', 'if (bCarriedThisPass) { ++LeftAuto; continue; }', 'NewRec.bPlayerSet = true;')) -or $load -eq '' -or (Count $load 'GMissingResolved = true;') -ne 1 -or -not (Ordered $load @('Lines[0].TrimStartAndEnd() != RecordHeader', 'return;', 'GMissingResolved = true;'))) { $f += '(S16) a missing or unreadable record makes every existing row PLAYER-SET (except on the carry-over pass) and a fresh file is written' }
    if ($tick -eq '' -or -not (Ordered $tick @('EnsureRowStateRecordLoaded();', 'const bool bCarried = RunCarryOverOnce(', 'ResolveMissingRecordOnce(Existing, bCarried);', 'DropRecordsWithoutRows(Existing);', 'if (Existing.Contains(Path)) { continue; }', 'const FRowStateCounts States = UpdateRowStates(World, Shows);', 'if (Added > 0 || bCarried || States.Mirrored > 0)', 'Manager->FlushPendingSaves();', 'FlushRowStateRecordIfDirty();', 'GRowShown.Reset();'))) { $f += '(S16) Tick order: load, carry-over, missing-file decision before the adds, states, config save (also for a mirror write), record write, snapshot' }
    if ($update -eq '' -or $drop -eq '' -or -not (Ordered $drop @('if (RowsBeforeAdds.Contains(It.Key())) { continue; }', 'It.RemoveCurrent();')) -or (Count $T.sfs 'TEXT("NodeShuffle_ScannerRows.txt")') -ne 1) { $f += '(S16) a record whose path has no row is dropped; the record file name' }
    foreach ($n in $stateIo) { foreach ($s in @($hook, $answer16, $nsHook)) { if ($s -ne '' -and (Count $s $n) -ne 0) { $f += "(S16) no record-file call from a hook ($n)" } }; if ((Count ($T.sf + $T.sdiag + $T.nsmod) $n) -ne 0) { $f += "(S16) no record-file call in the hook files ($n)" } }
    if ($resolve -eq '' -or $noted -eq '' -or -not (Ordered $resolve @('if (bCarriedThisPass)', 'if (GNotedByCarry.Contains(It.Key())) { continue; }', 'It.RemoveCurrent();', 'if (GMissingResolved || !GRecordsLoaded) { return; }')) -or (Count $noted 'GNotedByCarry.Add(Path);') -ne 1) { $f += '(S16) on the carry-over pass every record the carry-over did not note is forgotten (a record file left from another config)' }
    if ((Count $T.sfr 'if (!ReadRow(Element, R) || (bTplReadable && R.Show == Tpl.Show)) { continue; }') -ne 1) { $f += '(S16) the mirror never writes through the template''s box' }
    if ((Count $T.sfr 'EnsureRowStateRecordLoaded();') -ne 1 -or -not (Ordered $T.sfr @('EnsureRowStateRecordLoaded();', 'ApplyPendingRowStateForgets();', 'const bool bCarried = RunCarryOverOnce('))) { $f += '(S16) protection-created rows are forgotten before the carry-over' }
    if ((Count $T.sfs 'if (Trailer != Read.Num())') -ne 1) { $f += '(S16) a truncated record file is treated as lost' }
    foreach ($o in @(('All Minable is kept out of ' + 'that list'), ('are kept out of ' + 'that list'))) { if ((Count ($sfAll + $T.cfgc) $o) -ne 0) { $f += '(S10) All Minable sentence names the scanner''s list' } }
    if ((Count $T.prot 'NodeShuffleScanFilter::ForgetRowState(Pair.Key); // review r2 F1') -ne 1) { $f += '(S16) a row the protection side creates forgets its old record' }
    foreach ($o in @(('and so does every box if ' + 'NodeShuffle''s record'), ('is ' + 'lost.\n\n'))) { if ((Count $T.cfgc $o) -ne 0) { $f += '(S10) the record-file sentence names its exceptions' } }
    if ((Count $T.sfr ('FlushRowState' + 'RecordIfDirty();')) -ne 1) { $f += '(S16) the record is written from Tick only' }
    if ((Count $T.sfr 'Once you change the box, it stays as you set it (a value carried over from the old scanner list counts as changed).') -ne 1) { $f += '(S10) row switch text: the two states' }
    # S14
    $diagHook = Span $T.sdiag 'SUBSCRIBE_UOBJECT_METHOD(AFGResourceScanner, GetClosetClusters,' 'void ANodeShuffleSubsystem::EmitScanPing('
    if ($diagHook -eq '' -or -not (Ordered $diagHook @('NodeShuffleScanFilter::BeginClosestPick(Self);', 'ON_SCOPE_EXIT { NodeShuffleScanFilter::EndClosestPick(); };', 'Scope(Self, Closest);', '}', 'NodeShuffleScanFilter::EmitPickLine(Self, Closest.Num());', 'if (!FNodeShuffleModule::AreDiagnosticsEnabled() || !IsValid(Self) || !Self->GetWorld()) { return; }', 'It->EmitScanPing(Self, Closest); return; }')) -or (Count $diagHook 'BeginClosestPick(') -ne 1 -or (Count $diagHook 'Scope(Self, Closest);') -ne 1) { $f += '(S14) the pick flag brackets exactly the original call; SCANPING kept' }
    if ((Count $T.sdiag '#include "Misc/ScopeExit.h"') -ne 1) { $f += '(S14) ON_SCOPE_EXIT include' }
    $endPick = Span $T.sf 'void EndClosestPick()' 'bool ShouldAnswerScannable('
    if ($endPick -eq '' -or (Count $endPick 'GInClosestPick = false;') -ne 1) { $f += '(S14) EndClosestPick clears the flag' }
    $answer = Span $T.sf 'bool ShouldAnswerScannable(UClass* ResourceClass, bool bGameAnswer)' 'void EmitPickLine('
    if ($answer -eq '' -or -not (Ordered $answer @('if (!GInClosestPick) { return false; }', 'if (bGameAnswer) { ++GPickGameYes; return false; }', 'if (!ResourceClass || PickWorld == nullptr || !GSnapWorld.IsValid() || GSnapWorld.Get() != PickWorld) { return false; }', 'const bool* PickRow = GRowShown.Find(PickInfo.Path);', 'if (!PickInfo.bModded || PickRow == nullptr || !*PickRow) { return false; }', '++GPickChangedYes;', 'return true;')) -or (Count $answer 'return true;') -ne 1) { $f += '(S14) a false becomes true only inside the pick, with a snapshot and an ON own-path row' }
    foreach ($n in $cfgWrite) { if ($answer -ne '' -and (Count $answer $n) -ne 0) { $f += "(S14) the pick answer must not walk or write config or touch disk ($n)" } }
    $pickHook = Span $T.nsmod 'SUBSCRIBE_UOBJECT_METHOD(AFGUnlockSubsystem, IsNodeScannable,' 'SCANFILTER-PICKHOOK target='
    if ($pickHook -eq '' -or -not (Ordered $pickHook @('[](auto& Scope, AFGUnlockSubsystem* Self, FScannableResourcePair Pair)', 'const bool bGameAnswer = Scope(Self, Pair);', 'if (NodeShuffleScanFilter::ShouldAnswerScannable(Pair.ResourceDescriptor.Get(), bGameAnswer)) { Scope.Override(true); }')) -or (Count $pickHook ('Override(' + 'false)')) -ne 0) { $f += '(S14) IsNodeScannable: game first, override only to true' }
    if ((Count $T.nsmod 'TEXT("SCANFILTER-PICKHOOK target=AFGUnlockSubsystem::IsNodeScannable handlerRegistered=%d"),') -ne 1 -or (Count $T.sf 'TEXT("SCANFILTER-PICK resource=%s scannableQuestions=%d gameAnsweredYes=%d changedToYes=%d clustersReturned=%d snapshotForThisWorld=%d"),') -ne 1) { $f += '(S14) PICK and PICKHOOK lines' }
    $unlockCall = @(('UnlockScannable' + 'Resource'), ('GetUnlock' + 'Subsystem'), ('(Save' + 'Game'), ('SaveGame' + 'Subsystem')) # the pick hook must name the unlock subsystem and the pair type; these it may not
    foreach ($n in $unlockCall) { if (($pickHook -ne '' -and (Count $pickHook $n) -ne 0) -or ($diagHook -ne '' -and (Count $diagHook $n) -ne 0)) { $f += "(S7) no unlock or save access in the pick hooks ($n)" } }
    if ((Count $T.sdiag 'SUBSCRIBE_UOBJECT_METHOD(AFGUnlockSubsystem, GetScannableResources') + (Count $T.nsmod 'SUBSCRIBE_UOBJECT_METHOD(AFGUnlockSubsystem, GetScannableResources') -ne 0) { $f += '(S7) the unlock subsystem getter is not hooked' }
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
    @{ Name = 'M6 new row ignores the default';       Key = 'sfr';   Expect = 'S3';  Find = $newRowSet; Repl = 'if (!AppendRow(Rows, Tpl, bTplReadable, Path, true, Withdrawn)) { continue; }' }
    @{ Name = 'M7 no-row left unfiltered';            Key = 'sf';    Expect = 'S4';  Find = $noRowRule; Repl = 'bShown = true;' }
    @{ Name = 'M8 hook writes config';                Key = 'sf';    Expect = 'S6';  Find = "        ++GCallsSinceLine;`n        FCallFigures F;"; Repl = ("        ++GCallsSinceLine; if (UConfigManager* Cm = nullptr) { Cm->" + 'FlushPending' + "Saves(); }`n        FCallFigures F;") }
    @{ Name = 'M9 unlock call added';                 Key = 'sfr';    Expect = 'S7';  Find = "        GSnapWorld = World;`n"; Repl = ("        GSnapWorld = World; // Unlocks->" + 'UnlockScannable' + "Resource(Pair);`n") }
    @{ Name = 'M10 rows from the managed set only';   Key = 'sfr';    Expect = 'S8';  Find = $wantB; Repl = '' }
    @{ Name = 'M11 order not preserved';              Key = 'sf';    Expect = 'S5';  Find = $keepShown; Repl = ('if (bShown) { Kept' + '.Insert(Entry, 0); continue; }') }
    @{ Name = 'M12 tick after the Enabled early-out'; Key = 'sub';   Expect = 'S8';  Find = "    $tickCall`n"; Repl = '' }
    @{ Name = 'M13 seen set drained per pass';        Key = 'sfr';    Expect = 'S8';  Find = "        GSnapWorld = World;`n"; Repl = "        GSnapWorld = World; GSeenNoRow.Empty();`n" }
    @{ Name = 'M14 marker not newest';                Key = 'mark';  Expect = 'S9';  Find = ('        TEXT("' + $marker + '"),'); Repl = '' }
    @{ Name = 'M15 retired list registered locked';   Key = 'sfr';   Expect = 'S10'; Find = 'Retired->bRequiresWorldReload = false;'; Repl = 'Retired->bRequiresWorldReload = true;' }
    @{ Name = 'M16 research sentence dropped';        Key = 'cfgc';    Expect = 'S10'; Find = $plain[1]; Repl = '' }
    @{ Name = 'M17 old knowledge sentence back';      Key = 'cfgc';  Expect = 'S10'; Find = 'TEXT("ON: after a world loads,'; Repl = ('TEXT("' + $oldKnow + '. ON: after a world loads,') }
    @{ Name = 'M18 hook logs directly';               Key = 'sf';    Expect = 'S6';  Find = "        GLatest = F;`n"; Repl = "        GLatest = F; UE_LOG(LogNodeShuffle, Display, TEXT(`"x`"));`n" }
    @{ Name = 'M19 knowledge pass edited';            Key = 'sub';   Expect = 'S12'; Find = 'if (!Config.UnlockModdedKnowledge) { return true; }'; Repl = 'if (!Config.UnlockModdedKnowledge) { return false; }' }
    @{ Name = 'M20 hook installed log dropped';       Key = 'nsmod'; Expect = 'S1';  Find = 'ScanFilterHook.IsValid() ? 1 : 0);'; Repl = '1);' }
    @{ Name = 'M22 knowledge tooltip quotes the retired title'; Key = 'cfgc'; Expect = 'S10'; Find = ("use 'Show in scanner' in '" + $mtitle + "'."); Repl = ("use '" + $title + "'.") }
    @{ Name = 'M23 census made unconditional';         Key = 'sf';    Expect = 'S11'; Find = 'else if (Now - GLastCallLineTime >= CallCensusPeriodSeconds && (FNodeShuffleModule::AreDiagnosticsEnabled() || !SameFigures(GLatest, GLastCensus)))'; Repl = 'else if (Now - GLastCallLineTime >= CallCensusPeriodSeconds)' } # round 2, coder-written
    @{ Name = 'M24 notAdded latch removed';           Key = 'sfr';    Expect = 'S11'; Find = 'if ((RefusedOverCap > 0 || Withdrawn > 0) && (RefusedOverCap != GLastRefused || Withdrawn != GLastWithdrawn))'; Repl = 'if (RefusedOverCap > 0 || Withdrawn > 0)' } # round 2, coder-written
    @{ Name = 'M25 remove-only wording back';         Key = 'cfgc';    Expect = 'S10'; Find = 'have not researched. OFF: NodeShuffle takes it out of that list.'; Repl = ('have not researched. A switch only hides' + ' or shows.') } # scanfilter-2 (was round 2's old-sentence mutant)
    @{ Name = 'M37 add ignores an OFF own-path row'; Key = 'sf'; Expect = 'S13'; Find = 'if (!AddInfo.bModded || AddRow == nullptr || !*AddRow) { continue; }'; Repl = 'if (!AddInfo.bModded) { continue; }' }
    @{ Name = 'M39 add allowed with no own-path row'; Key = 'sf'; Expect = 'S13'; Find = 'if (!AddInfo.bModded || AddRow == nullptr || !*AddRow) { continue; }'; Repl = 'if (!AddInfo.bModded || (AddRow && !*AddRow)) { continue; }' }
    @{ Name = 'M38 added set never named late'; Key = 'sf'; Expect = 'S11'; Find = 'if (F.bSnapshot && AddedNames != GLastAddedNames)'; Repl = 'if (false && AddedNames != GLastAddedNames)' }
    @{ Name = 'M27 add path removed';                 Key = 'sf';    Expect = 'S13'; Find = 'Kept.Add(TSubclassOf<UFGResourceDescriptor>(AddCls));'; Repl = '' }
    @{ Name = 'M28 OFF rows added';                   Key = 'sfr';    Expect = 'S13'; Find = "            if (bShow && bPlayerSet)`n            {`n                UClass* RowCls"; Repl = "            if (true)`n            {`n                UClass* RowCls" }
    @{ Name = 'M29 no-row resource added';            Key = 'sf';    Expect = 'S13'; Find = 'GSeenNoRow.Add(Info.Path); // source (b)'; Repl = 'GSeenNoRow.Add(Info.Path); GAddOrder.Add(Cls); // source (b)' }
    @{ Name = 'M30 duplicates possible';              Key = 'sf';    Expect = 'S13'; Find = 'if (!AddCls || Present.Contains(AddCls)) { continue; }'; Repl = 'if (!AddCls) { continue; }' }
    @{ Name = 'M31 add with no snapshot';             Key = 'sf';    Expect = 'S13'; Find = "        if (F.bSnapshot)`n        {"; Repl = "        if (true)`n        {" }
    @{ Name = 'M32 class resolved in the hook';       Key = 'sf';    Expect = 'S6';  Find = '                UClass* AddCls = Weak.Get();'; Repl = ('                UClass* AddCls = Weak.Get(); if (!AddCls) { AddCls = FSoftClassPath(Info.Path).' + 'ResolveC' + 'lass(); }') }
    @{ Name = 'M33 base-game resource addable';       Key = 'sfr';    Expect = 'S13'; Find = 'if (!IsModdedPath(Path)) { continue; }'; Repl = '' }
    @{ Name = 'M34 unlock call in the add step';      Key = 'sf';    Expect = 'S7';  Find = '                ++F.AddedByRow;'; Repl = ('                ++F.AddedByRow; // Unlocks->' + 'UnlockScannable' + 'Resource(Pair);') }
    @{ Name = 'M35 existing rows rewritten';          Key = 'sfr';   Expect = 'S13'; Find = '            Existing.Add(Path);' + "`n" + '            if (bFirstPassThisWorld)'; Repl = '            Existing.Add(Path); R.Show->Value = true;' + "`n" + '            if (bFirstPassThisWorld)' }
    @{ Name = 'M36 marker not newest (scanfilter-2)'; Key = 'mark';  Expect = 'S9';  Find = ('        TEXT("' + $marker2 + '"),'); Repl = '' }
    @{ Name = 'M50 pick block braces removed'; Key = 'sdiag'; Expect = 'S14'; Find = "            Scope(Self, Closest); // the scanner's own pick first; this hook only reads what it returned`n        }"; Repl = "            Scope(Self, Closest); // the scanner's own pick first; this hook only reads what it returned" }
    @{ Name = 'M40 pick flag never cleared';          Key = 'sf';    Expect = 'S14'; Find = "        GInClosestPick = false;`n        GPickWorld.Reset();"; Repl = "        GPickWorld.Reset();" }
    @{ Name = 'M41 flag set outside the original';    Key = 'sdiag'; Expect = 'S14'; Find = "NodeShuffleScanFilter::BeginClosestPick(Self);`n            ON_SCOPE_EXIT { NodeShuffleScanFilter::EndClosestPick(); };`n            Scope(Self, Closest);"; Repl = "ON_SCOPE_EXIT { NodeShuffleScanFilter::EndClosestPick(); };`n            Scope(Self, Closest); NodeShuffleScanFilter::BeginClosestPick(Self);" }
    @{ Name = 'M42 override without the flag';        Key = 'sf';    Expect = 'S14'; Find = 'if (!GInClosestPick) { return false; }'; Repl = 'if (!GInClosestPick) { }' }
    @{ Name = 'M43 override without a snapshot';      Key = 'sf';    Expect = 'S14'; Find = 'if (!ResourceClass || PickWorld == nullptr || !GSnapWorld.IsValid() || GSnapWorld.Get() != PickWorld) { return false; }'; Repl = 'if (!ResourceClass) { return false; }' }
    @{ Name = 'M44 override for an OFF row';          Key = 'sf';    Expect = 'S14'; Find = 'if (!PickInfo.bModded || PickRow == nullptr || !*PickRow) { return false; }'; Repl = 'if (!PickInfo.bModded || PickRow == nullptr) { return false; }' }
    @{ Name = 'M45 override with no own-path row';    Key = 'sf';    Expect = 'S14'; Find = 'if (!PickInfo.bModded || PickRow == nullptr || !*PickRow) { return false; }'; Repl = 'if (!PickInfo.bModded || (PickRow && !*PickRow)) { return false; }' }
    @{ Name = 'M46 a true turned to false';           Key = 'nsmod'; Expect = 'S14'; Find = '{ Scope.Override(true); }' + "`n    });`n    UE_LOG(LogNodeShuffle, Display, TEXT(`"SCANFILTER-PICKHOOK"; Repl = ('{ Scope.Override(true); } else if (bGameAnswer) { Scope.Override(' + 'false); }' + "`n    });`n    UE_LOG(LogNodeShuffle, Display, TEXT(`"SCANFILTER-PICKHOOK") }
    @{ Name = 'M47 unlock call in the pick answer';   Key = 'sf';    Expect = 'S7';  Find = '        ++GPickChangedYes;'; Repl = ('        ++GPickChangedYes; // Unlocks->' + 'UnlockScannable' + 'Resource(Pair);') }
    @{ Name = 'M48 SCANPING removed';                 Key = 'sdiag'; Expect = 'S14'; Find = 'It->EmitScanPing(Self, Closest); return; }'; Repl = 'return; }' }
    @{ Name = 'M49 marker not newest (scanfilter-3)'; Key = 'mark';  Expect = 'S9';  Find = ('        TEXT("' + $marker3 + '"),'); Repl = '' }
    @{ Name = 'M26 tick census made unconditional'; Key = 'sfr'; Expect = 'S11'; Find = 'CallCensusPeriodSeconds && (FNodeShuffleModule::AreDiagnosticsEnabled() || !SameFigures(GLatest, GLastCensus))) { EmitCallCensus(Now); }'; Repl = 'CallCensusPeriodSeconds) { EmitCallCensus(Now); }' }
    @{ Name = 'M51 scanner-created row unticked';      Key = 'sfr';   Expect = 'S15'; Find = 'R.Protected->Value = true;'; Repl = 'R.Protected->Value = false;' }
    @{ Name = 'M52 latch opts out an OFF scanner row'; Key = 'prot';  Expect = 'S15'; Find = '        if (!ProtectedProp->Value)' + "`n" + '        {' + "`n" + '            ++UntickedRows;'; Repl = '        if (!ProtectedProp->Value || !NodeShuffleFindShowField(Element)->Value)' + "`n" + '        {' + "`n" + '            ++UntickedRows;' }
    @{ Name = 'M53 Protected editable in a running world'; Key = 'cfgc'; Expect = 'S15'; Find = 'ProtectedProp->bRequiresWorldReload = true; // F10'; Repl = 'ProtectedProp->bRequiresWorldReload = false;' }
    @{ Name = 'M54 Show in scanner locked';            Key = 'sfr';   Expect = 'S15'; Find = 'Show->bRequiresWorldReload = false;'; Repl = 'Show->bRequiresWorldReload = true;' }
    @{ Name = 'M55 carry-over runs again on a later load'; Key = 'sfr'; Expect = 'S15'; Find = 'if (!Flag || Flag->Value)'; Repl = 'if (!Flag)' }
    @{ Name = 'M56 carry-over never sets its flag';    Key = 'sfr';   Expect = 'S15'; Find = '            Flag->Value = true;' + "`n"; Repl = '' }
    @{ Name = 'M57 retired list not emptied';          Key = 'sfr';   Expect = 'S15'; Find = '            if (Retired) { Retired->Values.Empty(); }'; Repl = '' }
    @{ Name = 'M58 duplicate row from the row pass';   Key = 'sfr';   Expect = 'S15'; Find = '            if (Existing.Contains(Path)) { continue; }'; Repl = '' }
    @{ Name = 'M59 duplicate row from the carry-over'; Key = 'sfr';   Expect = 'S15'; Find = 'if (TArray<int32>* Indices = ByPath.Find(Path))'; Repl = 'if (TArray<int32>* Indices = nullptr)' }
    @{ Name = 'M60 All Minable AUTO box kept ON';       Key = 'sfr';   Expect = 'S15'; Find = '        if (IsModdedPath(Path) && !DefaultShownForPath(Path)) { Basis = TEXT("allMinable"); }' + "`n" + '        else if'; Repl = '        if' }
    @{ Name = 'M61 unlisted AUTO box kept ON';         Key = 'sfr';   Expect = 'S15'; Find = '        bool bOn = false;'; Repl = '        bool bOn = true;' }
    @{ Name = 'M74 AUTO row added by the hook';        Key = 'sfr';   Expect = 'S16'; Find = 'if (bShow && bPlayerSet)'; Repl = 'if (bShow)' }
    @{ Name = 'M75 AUTO non-All-Minable row removed';  Key = 'sf';    Expect = 'S16'; Find = $autoRule; Repl = 'bShown = false;' }
    @{ Name = 'M76 AUTO All Minable row not removed';  Key = 'sf';    Expect = 'S16'; Find = $autoRule; Repl = 'bShown = true;' }
    @{ Name = 'M77 pick override for an AUTO row';     Key = 'sf';    Expect = 'S16'; Find = 'if (!PickInfo.bModded || PickRow == nullptr || !*PickRow) { return false; }'; Repl = 'if (!PickInfo.bModded || ((PickRow == nullptr || !*PickRow) && !GAutoRow.Contains(PickInfo.Path))) { return false; }' }
    @{ Name = 'M78 AUTO rows in the player map';       Key = 'sfr';   Expect = 'S16'; Find = $snapMode; Repl = 'GRowShown.Add(Path, bShow); if (!bPlayerSet) { GAutoRow.Add(Path); }' }
    @{ Name = 'M79 mirror writes a PLAYER-SET row';    Key = 'sfs';   Expect = 'S16'; Find = 'if (Rec && Rec->bPlayerSet) { continue; } // a PLAYER-SET row is never written'; Repl = '' }
    @{ Name = 'M80 mirror writes Protected';           Key = 'sfs';   Expect = 'S16'; Find = '                Row.Show->Value = bMirror;'; Repl = ('                Row.Show->Value = bMirror; Row.' + 'Protected->Value = bMirror;') }
    @{ Name = 'M81 touch detection skipped';           Key = 'sfs';   Expect = 'S16'; Find = "                Rec->bPlayerSet = true;`n"; Repl = '' }
    @{ Name = 'M82 mirror before touch detection';     Key = 'sfs';   Expect = 'S16'; Find = $touchMirror; Repl = "        C.Mirrored = MirrorAutoRows(WorldContext, Shows, C.MirrorUnknown);`n        C.BecamePlayerSet = DetectTouches(Shows);" }
    @{ Name = 'M83 carried-over rows left AUTO';       Key = 'sfr';   Expect = 'S15'; Find = '                NoteCarriedRow(Path, OldShow->Value);'; Repl = '' }
    @{ Name = 'M84 carried-over record not player-set'; Key = 'sfs';  Expect = 'S16'; Find = '        Rec.bPlayerSet = true;'; Repl = '' }
    @{ Name = 'M85 missing file treated as all-AUTO';  Key = 'sfs';   Expect = 'S16'; Find = '            NewRec.bPlayerSet = true;'; Repl = '' }
    @{ Name = 'M86 missing file decided after the adds'; Key = 'sfr'; Expect = 'S16'; Find = "        ResolveMissingRecordOnce(Existing, bCarried);`n"; Repl = '' }
    @{ Name = 'M87 record written from a hook';        Key = 'sf';    Expect = 'S16'; Find = "        GLatest = F;`n"; Repl = ("        GLatest = F; " + 'FlushRowState' + "RecordIfDirty();`n") }
    @{ Name = 'M88 deleted row''s record kept';        Key = 'sfs';   Expect = 'S16'; Find = "            It.RemoveCurrent();`n            GRecordsDirty = true;`n            ++Dropped;"; Repl = "            GRecordsDirty = true;`n            ++Dropped;" }
    @{ Name = 'M91 record pruned after the adds';     Key = 'sfr';   Expect = 'S16'; Find = "        DropRecordsWithoutRows(Existing);`n"; Repl = '' }
    @{ Name = 'M92 stale record survives a carry-over'; Key = 'sfs'; Expect = 'S16'; Find = 'if (bCarriedThisPass) // the config'; Repl = 'if (false) // the config' }
    @{ Name = 'M93 mirror writes through the template'; Key = 'sfr'; Expect = 'S16'; Find = 'if (!ReadRow(Element, R) || (bTplReadable && R.Show == Tpl.Show)) { continue; }'; Repl = 'if (!ReadRow(Element, R)) { continue; }' }
    @{ Name = 'M94 protection re-creation keeps the record'; Key = 'sfr'; Expect = 'S16'; Find = "        ApplyPendingRowStateForgets(); // review r2 F1"; Repl = "        // review r2 F1" }
    @{ Name = 'M95 truncated record read as whole'; Key = 'sfs'; Expect = 'S16'; Find = 'if (Trailer != Read.Num())'; Repl = 'if (false)' }
    @{ Name = 'M96 protection re-creation not forgotten'; Key = 'prot'; Expect = 'S16'; Find = '        NodeShuffleScanFilter::ForgetRowState(Pair.Key); // review r2 F1: a re-created row starts AUTO'; Repl = '' }
    @{ Name = 'M89 mirror write never saved';          Key = 'sfr';   Expect = 'S16'; Find = 'if (Added > 0 || bCarried || States.Mirrored > 0)'; Repl = 'if (Added > 0 || bCarried)' }
    @{ Name = 'M90 mirror while the list is unread';   Key = 'sfs';   Expect = 'S16'; Find = 'if (!bKnown) { ++OutUnknown; continue; }'; Repl = '' }
    @{ Name = 'M62 retired list shown';                Key = 'sfr';   Expect = 'S15'; Find = '        Retired->bHidden = true;'; Repl = '' }
    @{ Name = 'M63 carry-over flag resettable';        Key = 'sfr';   Expect = 'S15'; Find = '        Flag->bAllowUserReset = false;'; Repl = '' }
    @{ Name = 'M64 scanner side pre-empts a sighted row'; Key = 'sfr'; Expect = 'S15'; Find = '                if (++DeferPasses <= MaxDeferPasses) { ++Deferred; continue; }'; Repl = '' }
    @{ Name = 'M70 deferral never ends';                Key = 'prot';  Expect = 'S15'; Find = 'return GNodeShuffleLastSyncedRevision != GNodeShuffleSeenForeignRevision && GNodeShuffleSeenForeignResources.Contains(ResourceClassPath);'; Repl = 'return GNodeShuffleSeenForeignResources.Contains(ResourceClassPath);' }
    @{ Name = 'M71 carry-over with the list unread';    Key = 'sfr';   Expect = 'S15'; Find = 'if (CarryFlag && !CarryFlag->Value && !GStartGameListRead)'; Repl = 'if (false)' }
    @{ Name = 'M65 protection pass writes through the template'; Key = 'prot'; Expect = 'S15'; Find = 'ShowProp != nullptr && ShowProp != TemplateShow;'; Repl = 'ShowProp != nullptr;' }
    @{ Name = 'M66 veto reads the switch';             Key = 'veto';  Expect = 'S15'; Find = '|| FNodeShuffleModule::IsForeignResourceProtectedByConfig(ForeignResourcePath);'; Repl = '|| FNodeShuffleModule::IsForeignResourceProtectedByConfig(ForeignResourcePath); // ShowInScanner' }
    @{ Name = 'M67 marker not newest (scanfilter-4)'; Key = 'mark';  Expect = 'S9';  Find = ('        TEXT("' + $marker4 + '"),'); Repl = '' }
    @{ Name = 'M68 unlock write in the rows file';     Key = 'sfr';   Expect = 'S7';  Find = '            GStartGameListRead = true;'; Repl = ('            GStartGameListRead = true; // Unlocks->' + 'UnlockScannable' + 'Resource(Pair);') }
    @{ Name = 'M69 existing switch rewritten by the protection pass'; Key = 'sfr'; Expect = 'S13'; Find = '            const bool bShow = R.Show->Value;'; Repl = '            const bool bShow = R.Show->Value; R.Show->Value = bShow;' }
    @{ Name = 'M72 list resettable'; Key = 'cfgc'; Expect = 'S15'; Find = '        Rows->bAllowUserReset = false;'; Repl = '' }
    @{ Name = 'M73 scanner side never sorts'; Key = 'sfr'; Expect = 'S15'; Find = 'if (Added > 0 || bCarried) { FNodeShuffleModule::SortResourceRows(Rows, TEXT("scanner-add")); }'; Repl = '' }
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

if ($fails.Count -eq 0) { Write-Host "PASS check_scanfilter_lint: S1-S16 hold; $caught/$($mutants.Count) mutants caught"; exit 0 }
Write-Host "check_scanfilter_lint: $($fails.Count) failure(s); $caught/$($mutants.Count) mutants caught"
exit 1
