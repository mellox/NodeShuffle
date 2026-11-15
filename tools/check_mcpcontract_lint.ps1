# MCPCONTRACT lint (2026-11-09, packet mcp-contract-1; handoff _team/test-logs/2026-11-09-nodeshuffle-mcp-contract-handoff.md).
# Pins NodeShuffle's side of the MCP <-> mod contract v1 (_team/mcp-mod-contract-v1.md, owned by the MCP orchestrator):
#   M1 SIGNATURES: the two exact UFUNCTION declarations, the reader friend, and no third contract function (tier-2 writes are out of scope)
#   M2 ISOLATION: nothing under Source/, the Build.cs or the .uplugin names the MCP mod (needle built by concatenation, case-insensitive)
#   M3 READ-ONLY: in NodeShuffleMcp*.cpp / NodeShuffleMcpCommon.h CODE (comments and string literals stripped): no load, trace, sweep,
#      overlap, spawn, const_cast, mutable, config/save write, GetMapMode call (it latches a mutable member) or config read; no assignment,
#      increment or container mutation through Sub.; every subsystem reference is const; McpSnapshot forwards *this as const
#   M4 LOGGING: exactly one UE_LOG per contract function, Verbose, and the snapshot one sits in McpSnapshot after the reader returns
#   M5 SIZE CAP: 64 KB measured in UTF-8 bytes; near and summary each re-serialize until under it; the manifest checks it too
#   C1 CONFIG COMPLETENESS: every key registered on the config ROOT section anywhere in Source/ (AddBool/AddInt literals and
#      Root->SectionProperties.Add with a literal or a resolved TCHAR constant) has exactly one manifest row, and no row is a phantom
#   C2 the re-roll trigger RerollNow carries touchesSave true
#   C3 CONSOLE COMPLETENESS: every NodeShuffle.* console command and console variable registered in Source/ has exactly one manifest row,
#      no phantom rows, and every registration declaration resolved to a literal name
#   R  REVIEW ROUND 1 + CONTRACT REVISION 2 (P1..P8 of _team/test-logs/2026-11-09-nodeshuffle-mcp-contract-review.md): row shape with
#      saveEffect; GOLDEN (touchesSave, saveEffect) per key and command; touchesSave false <=> saveEffect none; every ErrorJson call
#      carries a known errorKind; NotReady carries errorKind; no pointee calls beyond the allowed reads; water-grid symmetry for commands
#      whose handler traces the ground without suppressing grid learning; the envelope on Describe/Help/NotReady/ErrorJson
#   C4 MARKERS: every NodeShuffle.cpp "packet marker (" line and the LOADED tag appear in NodeShuffleBuildMarkers.h, same order, and this
#      packet's marker is the newest; McpDescribe's buildMarker reads the header's newest entry, never a date literal
#   F  CONTRACT REVISION 4 (R4.4, finite numbers only): near skips a Layout entry with a non-finite location component inside its Layout
#      loop before the distance math, emits the skip count, help names it; a non-finite pawn location is refused as no-player
#   L  LASTROLL (2026-11-10, packet mcp-lastroll-1; handoff _team/test-logs/2026-11-10-nodeshuffle-mcp-lastroll-handoff.md): the member is
#      plain (no property macro, not mutable, no reflected struct); MCP files never write it or bind it non-const; roll code only STORES
#      into it (no read beyond the sequence copy); the one reset is RollLayout's first statement and precedes every store; each family's
#      stores sit in the census's own function between its UE_LOG and the next anchor; NAME CHAIN: every name= token of the census line
#      maps reader field -> struct member -> store -> the very argument the line prints (declared omissions/derivations only); the
#      lastRoll section is dispatched and named in help, the manifest's snapshotArgs and the unknown-section error text.
#      One family is held today: ROLLDENSITY.
#   E  EFFECT REGRADE (2026-11-10, packet mcp-regrade-1; handoff _team/test-logs/2026-11-10-nodeshuffle-mcp-regrade-handoff.md): GOLDEN
#      effect (read/write) per console command, so a command the audit traced to a member write, a lazy store load or an object load
#      cannot drift back to read (the MCP's console_run runs read rows on the user's real save); and neither manifest table's notes may
#      cite the const-signature phrase (needle concatenated) as grading evidence.
# Every forbidden needle is BUILT BY CONCATENATION so this file cannot satisfy or trip its own checks. Mutants run in-file.
# Exit 0 = all hold; 1 = a rule or mutant fails; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$pin = [ordered]@{
    hdr   = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    desc  = 'NodeShuffle\Private\NodeShuffleMcpDescribe.cpp'
    snap  = 'NodeShuffle\Private\NodeShuffleMcpSnapshot.cpp'
    com   = 'NodeShuffle\Private\NodeShuffleMcpCommon.h'
    mark  = 'NodeShuffle\Private\NodeShuffleBuildMarkers.h'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
    cfg   = 'NodeShuffle\Private\NodeShuffleConfig.cpp'
    sg    = 'NodeShuffle\Private\NodeShuffleRwgSeedGas.cpp'
    lr    = 'NodeShuffle\Private\NodeShuffleMcpLastRoll.cpp'
    lrh   = 'NodeShuffle\Public\NodeShuffleLastRoll.h'
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    dens  = 'NodeShuffle\Private\NodeShuffleRwgDensityBudget.cpp'
}
function Read-Norm([string]$p) { return ((Get-Content -Raw -Encoding utf8 -Path $p) -replace "`r`n", "`n") }
foreach ($k in $pin.Keys) { if (-not (Test-Path (Join-Path $src $pin[$k]))) { Write-Host "FAIL(load): pinned file '$($pin[$k])' does not exist"; exit 2 } }
# Every source file under Source/ is loaded (keyed by its relative path), plus the two module/plugin descriptors.
$text = [ordered]@{}
Get-ChildItem -Path $src -Recurse -File -Include *.cpp, *.h, *.cs | Sort-Object FullName | ForEach-Object {
    $text[$_.FullName.Substring($src.Length + 1)] = Read-Norm $_.FullName
}
$upl = Join-Path $root 'NodeShuffle.uplugin'
if (-not (Test-Path $upl)) { Write-Host 'FAIL(load): NodeShuffle.uplugin does not exist'; exit 2 }
$text['..\NodeShuffle.uplugin'] = Read-Norm $upl

# CODE ONLY: one left-to-right alternation, so a quote inside a comment or a // inside a string is classified by whichever starts first.
$codeRx = [regex]'//[^\n]*|/\*[\s\S]*?\*/|"(?:[^"\\\n]|\\.)*"|''(?:[^''\\\n]|\\.)*'''
function Code-Only([string]$t) {
    return $codeRx.Replace($t, [System.Text.RegularExpressions.MatchEvaluator]{ param($m)
        $v = $m.Value
        if ($v.StartsWith('/')) { return ' ' }
        if ($v.StartsWith('"')) { return '""' }
        return $v })
}
$code = [ordered]@{}
foreach ($k in $text.Keys) { $code[$k] = Code-Only $text[$k] }

function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }
function Ordered([string]$t, [string[]]$needles) {
    $at = 0
    foreach ($n in $needles) { $i = $t.IndexOf($n, $at); if ($i -lt 0) { return $false }; $at = $i + $n.Length }
    return $true
}
function Set-Diff([string[]]$a, [string[]]$b) { return @($a | Where-Object { $b -notcontains $_ }) }

# Concatenated needles (never spelled whole in this file).
$mcpModName   = 'Satisfactory' + 'MCP'
$thirdFn      = 'Mcp' + 'Action'
$sigDescribe  = 'UFUNCTION() FString ' + 'McpDescribe() const;'
$sigSnapshot  = 'UFUNCTION() FString ' + 'McpSnapshot(const FString& ArgsJson);'
$friendDecl   = 'friend struct ' + 'FNodeShuffleMcpReader;'
$trampoline   = 'FNodeShuffleMcpReader::Snapshot(' + 'static_cast<const ANodeShuffleSubsystem&>(*this), ArgsJson, Branch);'
$capDecl      = 'static constexpr int32 MaxOutputBytes = ' + '64 * 1024;'
$capLoop      = 'if (Bytes <= MaxOutputBytes) ' + '{ break; }'
$capShrink    = 'Keep = FMath::Min(Keep - 1, ' + 'static_cast<int32>(static_cast<int64>(Keep) * MaxOutputBytes / Bytes));'
$capDesc      = 'return Utf8Bytes(Out) <= MaxOutputBytes ' + '? Out : ErrorJson('
$utf8Pin      = 'const FTCHARToUTF8 Utf8(*Text, ' + 'Text.Len());'
$logVerbose   = 'UE_LOG(LogNodeShuffle, ' + 'Verbose, '
$newestExpr   = 'PacketMarkers[' + 'NodeShuffleBuildMarkers::PacketMarkerCount - 1]'
$forbidden = @(
    ('Load' + 'Object'), ('Load' + 'Class'), ('Static' + 'Load'), ('Try' + 'Load'), ('Load' + 'Synchronous'), ('Load' + 'Package'),
    ('Line' + 'Trace'), ('Sweep' + 'Single'), ('Sweep' + 'Multi'), ('Overlap' + 'Multi'), ('Overlap' + 'Any'), ('Spawn' + 'Actor'),
    ('const' + '_cast'), ('mut' + 'able'), ('GetMap' + 'Mode('), ('GetActive' + 'Config('), ('Save' + 'Config'), ('Mark' + 'Dirty'),
    ('Flush' + 'PendingSaves'), ('ClearReroll' + 'NowFlag'), ('this' + '->'), ('Get' + 'MutableDefault'), ('Destroy' + 'Actor')
)
$pointeeRx = @(
    ('Sub\s*\.\s*GetWorld\(\)\s*->(?!\s*GetPlayerController' + 'Iterator\()'),
    ('\(\s*\*\s*Slot\s*\)\s*' + '->'),
    ('\bLiveNode\s*->(?!\s*GetClass\(\)' + ')'),
    ('\bFoundPawn\s*->(?!\s*GetActorLocation\(\)' + ')')
)
# F: finite-number guards in near (contract Revision 4, R4.4).
$nearHead = 'FString FNodeShuffleMcpReader::' + 'Near('
$finLoop  = 'for (int32 I = 0; I < Sub.Layout.' + 'Num(); ++I)'
$finGuard = 'if (!FMath::IsFinite(E.' + 'Location.X) || !FMath::IsFinite(E.Location.Y) || !FMath::IsFinite(E.Location.Z)) { ++NonFiniteSkipped; continue; }'
$finDx    = 'const double Dx = E.' + 'Location.X - XCm;'
$finEmit  = 'Root->SetNumberField(TEXT("nonFinite' + 'Skipped"), NonFiniteSkipped);'
$finHelp  = 'nonFinite' + 'Skipped ('
$finPawn  = 'if (!FMath::IsFinite(P.' + 'X) || !FMath::IsFinite(P.Y))'
$finPawnRet = 'return ErrorJson(TEXT("no-player"), FString::Printf(TEXT("player %d: the pawn location has a non-' + 'finite X or Y component"), PlayerIndex)); }'
$finPawnRetC = 'return ErrorJson(TEXT(""), FString::Printf(TEXT(""), PlayerIndex)); }' # code-only shape of $finPawnRet (string literals blanked by Code-Only)
$errKinds = 'bad-args|no-world|no-player|not-ready|unsupported|too-large'
# GOLDEN (touchesSave/saveEffect) from the review's R2.1 table. Any difference in the manifest is RED.
$golden = @{
    'Enabled' = 'true/structural'; 'RerollNow' = 'true/structural'; 'ActivePercent' = 'true/structural'; 'MinNodesPerResource' = 'true/structural'
    'MinNodesPerModdedResource' = 'true/structural'; 'RandomizePurity' = 'true/structural'; 'IncludeModdedNodes' = 'true/structural'
    'SpawnRadiusMeters' = 'true/structural'; 'UnlockModdedKnowledge' = 'true/structural'; 'ProtectOtherModsNodes' = 'true/structural'
    'ProtectedForeignResources' = 'true/structural'; 'SeedOverride' = 'false/none'; 'NewNodeCount' = 'false/none'; 'EnableStarterNodes' = 'false/none'
    'StarterNodeRadiusMeters' = 'false/none'; 'RwgDensityBudget' = 'false/none'; 'ModdedVanillaCountsOnRwg' = 'false/none'
    'ModdedVanillaCountRows' = 'false/none'; 'EnableDiagnostics' = 'false/none'; 'ScannerResourceVisibility' = 'false/none' # ns-scanfilter-1
    'ScannerSwitchesMerged' = 'false/none' # ns-scanfilter-4: hidden carry-over flag
    'NodeShuffle.SeedHere' = 'true/structural'; 'NodeShuffle.Here' = 'true/structural'; 'NodeShuffle.PointAtHere' = 'true/structural'
    'NodeShuffle.WellProbe' = 'true/structural'; 'NodeShuffle.AuditPlacements' = 'true/structural'; 'NodeShuffle.DumpLayout' = 'false/none'
    'NodeShuffle.ProbeNearestNode' = 'false/none'; 'NodeShuffle.NearestCave' = 'false/none'; 'NodeShuffle.DumpExtractors' = 'false/none'
    'NodeShuffle.DumpWells' = 'false/none'; 'NodeShuffle.WhereCaveNodes' = 'true/structural'; 'NodeShuffle.ChatProbe' = 'false/none' # ns-rocks-1
}
# E (mcp-regrade-1): GOLDEN effect per console command, from the audit in the regrade handoff. read = the tool may run it on a real save.
$effectGolden = @{
    'NodeShuffle.Here' = 'write'; 'NodeShuffle.NearestCave' = 'write'; 'NodeShuffle.SeedHere' = 'write'; 'NodeShuffle.AuditPlacements' = 'write'
    'NodeShuffle.DumpExtractors' = 'write'; 'NodeShuffle.DumpLayout' = 'write'; 'NodeShuffle.ProbeNearestNode' = 'write'
    'NodeShuffle.PointAtHere' = 'write'; 'NodeShuffle.DumpWells' = 'read'; 'NodeShuffle.WellProbe' = 'write'; 'NodeShuffle.WhereCaveNodes' = 'write'; 'NodeShuffle.ChatProbe' = 'write' # ns-rocks-1
}
$constPhrase = 'const ' + 'handler'
# E (cold review regrade-1): every golden READ row is pinned at its SOURCE too -- each file its handler reaches in this module.
$readSites = @{ 'NodeShuffle.DumpWells' = @('NodeShuffle\Private\NodeShuffleWellDump.cpp', 'NodeShuffle\Private\NodeShuffleWellCensus.cpp') }
$readExtra = @(('ANodeShuffle' + 'Subsystem'), ('Raycast' + 'GroundAt('), ('Ensure' + 'CaveStoreLoaded'), ('Ensure' + 'WaterGridLoaded'),
    ('IsKnown' + 'WaterCell'), ('Set' + 'Timer'), ('SaveString' + 'ToFile'), ('FFile' + 'Helper'), ('Register' + 'ManagedNode'),
    ('Reset' + 'ManagedNodes'), ('PostChat' + 'Notice'))
# P7: commands whose handler traces the ground. Here's handler is the LogHereCensus body inside the subsystem file.
$gridSites = [ordered]@{
    'NodeShuffle.Here' = @('NodeShuffle\Private\NodeShuffleSubsystem.cpp', 'void ANodeShuffleSubsystem::LogHereCensus() const')
    'NodeShuffle.PointAtHere' = @('NodeShuffle\Private\NodeShufflePointAtHere.cpp', '')
    'NodeShuffle.WellProbe' = @('NodeShuffle\Private\NodeShuffleWellMemberProbe.cpp', '')
    # mcp-regrade-1 (coordinator overrule): ProbeNearestNode stays write (engine-side query effects unmeasured; rule E pins it, mutant E4);
    # a ground trace added to its file without grid suppression must also turn it touchesSave true like its three siblings.
    'NodeShuffle.ProbeNearestNode' = @('NodeShuffle\Private\NodeShuffleNodeProbe.cpp', '')
}
$groundCall = 'RaycastGround' + 'At('
$gridSuppress = 'bWaterGridLearning' + 'Suppressed'
function FnBody([string]$t, [string]$head) {
    $i = $t.IndexOf($head); if ($i -lt 0) { return '' }
    $j = $t.IndexOf("`n}`n", $i); if ($j -lt 0) { return '' }
    return $t.Substring($i, $j - $i)
}
# L: lastRoll (mcp-lastroll-1). Reflection/mutable needles concatenated.
$lrMember    = "`n    FNodeShuffleLastRoll " + 'LastRoll;' + "`n"
$lrReflRx    = '\b(?:' + 'UPROP' + 'ERTY|' + 'USTR' + 'UCT|' + 'GENERATED' + '_BODY|' + 'UCL' + 'ASS)\b'
$lrMutRx     = '\b' + 'mut' + 'able\b'
$lrBegin     = 'LastRoll.' + 'BeginRoll(Seed, bIsReroll);'
$lrRollSig   = 'void ANodeShuffleSubsystem::' + 'RollLayout(int32 Seed, bool bIsReroll)'
$lrDensSig   = 'void ANodeShuffleSubsystem::' + 'RwgDensityBudgetEnd()'
$lrDensFmt   = 'TEXT("ROLL' + 'DENSITY: '
$lrDensNext  = 'UE_LOG(LogNodeShuffle, Display, TEXT("ROLLDENSITY-' + 'LEGEND: '
$lrStoreRx   = '\bLastRoll\.(Density)\.(\w+) = ([^;]+);' # REROLLPOOL-CARRY is not held (rerolldup U6 adjacency; see the handoff)
$lrSeqStore  = '.CapturedInSeq = LastRoll.' + 'RollSeq;'
$lrEndCallRx = '(?<!void )(?<!::)\bRwgDensityBudgetEnd\(\);'
$lrMcpWriteRx = @('\bLastRoll\s*(?:\.\s*\w+\s*)+(?:=(?!=)|\+=|-=|\*=|/=|\+\+|--)', '\b[LDC]\s*\.\s*\w+(?:\s*\.\s*\w+)*\s*(?:=(?!=)|\+=|-=|\*=|/=|\+\+|--)',
    '(?:\+\+|--)\s*[LDC]\s*\.', '(?<!const )\bFNodeShuffleLastRoll(?:Density)?\s*[&*]', ('\b' + 'Begin' + 'Roll\s*\('))
$lrSq = 'NodeShuffleRwgDensityBudget::LandSamplesPerCellSide * NodeShuffleRwgDensityBudget::LandSamplesPerCellSide'
$lrDensDerived = @{ 'landSamplesPerCell' = @($lrSq, ('TEXT("landSamplesPerCell"), ' + $lrSq)); 'cellLinesOmitted' = @('Printable - Printed', 'TEXT("cellLinesOmitted"), D.CellLinesPrintable - D.CellLinesPrinted') }
function LrBlock([string]$t, [string]$head) { # anonymous-namespace helper: from its head to its four-space closing brace
    $i = $t.IndexOf($head); if ($i -lt 0) { return '' }
    $j = $t.IndexOf("`n    }`n", $i); if ($j -lt 0) { return '' }
    return $t.Substring($i, $j - $i)
}
function LrCensus([string]$t, [string]$fmtHead) { # @(name= tokens, printed arguments) of the UE_LOG whose format starts with fmtHead
    $m = [regex]::Match($t, [regex]::Escape($fmtHead) + '([^"]*)"\),\s*([\s\S]*?)\);')
    if (-not $m.Success) { return $null }
    $names = @([regex]::Matches($m.Groups[1].Value, '(\w+)=%') | ForEach-Object { $_.Groups[1].Value })
    $argv = @($m.Groups[2].Value -split ',' | ForEach-Object { (($_ -replace '\s+', ' ').Trim()) -replace ' \? 1 : 0$', '' })
    return ,@($names, $argv)
}
function LrStores([string]$body, [string]$fam, [ref]$errs) {
    $map = @{}
    foreach ($m in [regex]::Matches($body, $lrStoreRx)) {
        if ($m.Groups[1].Value -ne $fam) { continue }
        $mem = $m.Groups[2].Value
        if ($map.ContainsKey($mem)) { $errs.Value += "(L) $fam.$mem is stored twice" }
        $map[$mem] = ($m.Groups[3].Value -replace '\s+', ' ').Trim()
    }
    return $map
}
function LrChain([string]$fam, $census, [string]$reader, [string]$var, [hashtable]$stores, [string[]]$omitted, [hashtable]$derived) {
    $f = @()
    if ($null -eq $census) { return ,@("(L) $fam : census line not found") }
    if ($reader -eq '') { return ,@("(L) $fam : reader helper not found") }
    $names = $census[0]; $argv = $census[1]
    if ($names.Count -eq 0 -or $names.Count -ne $argv.Count) { return ,@("(L) $fam : $($names.Count) name= tokens but $($argv.Count) printed arguments") }
    $rmap = @{}
    foreach ($m in [regex]::Matches($reader, 'TEXT\("(\w+)"\), ' + $var + '\.(\w+)\b(?!\s*-)')) { $rmap[$m.Groups[1].Value] = $m.Groups[2].Value }
    $rnames = @([regex]::Matches($reader, 'TEXT\("(\w+)"\)') | ForEach-Object { $_.Groups[1].Value })
    for ($i = 0; $i -lt $names.Count; $i++) {
        $n = $names[$i]; $a = $argv[$i]
        if ($omitted -contains $n) { if ($rnames -contains $n) { $f += "(L) $fam : $n is declared not captured but the reader serves it" }; continue }
        if ($derived.ContainsKey($n)) {
            if ($a -ne $derived[$n][0]) { $f += "(L) $fam : $n prints '$a', the reader's derivation assumes '$($derived[$n][0])'" }
            if ($reader.IndexOf($derived[$n][1]) -lt 0) { $f += "(L) $fam : the reader's derivation of $n changed" }
            continue
        }
        if (-not $rmap.ContainsKey($n)) { $f += "(L) $fam : the reader does not serve $n from a member"; continue }
        $mem = $rmap[$n]
        if (-not $stores.ContainsKey($mem)) { $f += "(L) $fam : member $mem (served as $n) has no store"; continue }
        if ($stores[$mem] -ne $a) { $f += "(L) $fam : $n prints '$a' but $mem stores '$($stores[$mem])'" }
    }
    foreach ($rn in $rnames) { if ($names -notcontains $rn -and @('captured', 'nonFiniteOmitted') -notcontains $rn) { $f += "(L) $fam : the reader serves $rn, which the census line does not print" } }
    foreach ($mem in $stores.Keys) { if ($mem -ne 'CapturedInSeq' -and -not [regex]::IsMatch($reader, '\b' + $var + '\.' + $mem + '\b')) { $f += "(L) $fam : member $mem is stored but never served" } }
    return ,$f
}
$writeRx = @(
    '\bSub\s*\.\s*\w+(?:\s*(?:\.|->)\s*\w+)*\s*(?:\[[^\]]*\])?\s*(?:=(?!=)|\+=|-=|\*=|/=|\|=|&=|\+\+|--)',
    '(?:\+\+|--)\s*Sub\s*\.',
    '\bSub\s*\.\s*\w+\s*\.\s*(?:Add|AddUnique|Emplace|Remove|RemoveAt|RemoveAll|Empty|Reset|Append|Sort|StableSort|FindOrAdd|Insert|Pop|Push|Init|SetNum|Shrink|Compact)\s*\('
)

function Invoke-McpChecks($Txt, $Cod) {
    $f = @()
    $h = $Txt[$pin.hdr]
    # ---- M1
    if ((Count $h $sigDescribe) -ne 1) { $f += '(M1) McpDescribe declaration missing or not exact' }
    if ((Count $h $sigSnapshot) -ne 1) { $f += '(M1) McpSnapshot declaration missing or not exact' }
    if ((Count $h $friendDecl) -ne 1) { $f += '(M1) FNodeShuffleMcpReader friend missing' }
    if ((Count $Cod[$pin.hdr] $thirdFn) -ne 0) { $f += '(M1) a third contract function is declared; tier-2 writes are out of scope' }
    if (-not (Ordered $h @($sigDescribe, $sigSnapshot, $friendDecl, '};'))) { $f += '(M1) declarations out of place' }
    if ((Count $Txt[$pin.desc] 'FString ANodeShuffleSubsystem::McpDescribe() const') -ne 1) { $f += '(M1) McpDescribe definition missing' }
    if ((Count $Txt[$pin.snap] 'FString ANodeShuffleSubsystem::McpSnapshot(const FString& ArgsJson)') -ne 1) { $f += '(M1) McpSnapshot definition missing' }
    # ---- M2
    foreach ($k in $Txt.Keys) { if ($Txt[$k].IndexOf($mcpModName, [StringComparison]::OrdinalIgnoreCase) -ge 0) { $f += "(M2) $k names the MCP mod" } }
    # ---- M3
    foreach ($k in @($pin.desc, $pin.snap, $pin.com, $pin.lr)) {
        $c = $Cod[$k]
        foreach ($n in $forbidden) { if ((Count $c $n) -ne 0) { $f += "(M3) forbidden call or keyword in $k : $n" } }
        foreach ($rx in $writeRx) { if ([regex]::IsMatch($c, $rx)) { $f += "(M3) write through Sub. in $k : $([regex]::Match($c, $rx).Value)" } }
        if ([regex]::IsMatch($c, '(?<!const )ANodeShuffleSubsystem\s*&')) { $f += "(M3) non-const subsystem reference in $k" }
        if ([regex]::IsMatch($c, 'ANodeShuffleSubsystem\s*\*')) { $f += "(M3) subsystem pointer in $k" }
    }
    $snapC = $Cod[$pin.snap]
    if ((Count $snapC $trampoline) -ne 1) { $f += '(M3) McpSnapshot must forward *this as a const reference to the reader' }
    $sb = $snapC.IndexOf('FString ANodeShuffleSubsystem::McpSnapshot(')
    if ($sb -ge 0 -and ([regex]::Matches($snapC.Substring($sb), '\*this')).Count -ne 1) { $f += '(M3) McpSnapshot body may use *this only in the forwarding call' }
    # ---- M4
    foreach ($k in @($pin.desc, $pin.snap)) {
        $c = $Cod[$k]
        $nLog = Count $c 'UE_LOG('
        if ($nLog -ne 1) { $f += "(M4) exactly one UE_LOG expected in $k, found $nLog" }
        if ((Count $c $logVerbose) -ne 1) { $f += "(M4) the UE_LOG in $k must be Verbose on LogNodeShuffle" }
    }
    if ((Count $Cod[$pin.com] 'UE_LOG(') -ne 0) { $f += '(M4) no UE_LOG in the common header' }
    if ((Count $Cod[$pin.lr] 'UE_LOG(') -ne 0) { $f += '(M4) no UE_LOG in the lastRoll reader (McpSnapshot logs its branch)' }
    if (-not (Ordered $snapC @('FString ANodeShuffleSubsystem::McpSnapshot(', $trampoline, $logVerbose, 'return Out;'))) { $f += '(M4) the snapshot log line must follow the reader call inside McpSnapshot' }
    if (-not (Ordered $Cod[$pin.desc] @('FString ANodeShuffleSubsystem::McpDescribe() const', 'FNodeShuffleMcpReader::Describe();', $logVerbose))) { $f += '(M4) the describe log line must sit in McpDescribe' }
    # ---- M5
    if ((Count $Txt[$pin.com] $capDecl) -ne 1) { $f += '(M5) the 64 KB cap constant is missing or changed' }
    if ((Count $snapC $capLoop) -ne 2 -or (Count $snapC $capShrink) -ne 2) { $f += '(M5) near and summary must each re-serialize and drop the tail until under the cap' }
    if ((Count $Txt[$pin.snap] $utf8Pin) -ne 1) { $f += '(M5) the cap must be measured in UTF-8 bytes' }
    if ((Count $Txt[$pin.desc] $capDesc) -ne 1) { $f += '(M5) the manifest must be checked against the cap' }
    # ---- R (review round 1 + Revision 2)
    foreach ($k in @($pin.desc, $pin.snap, $pin.lr)) { foreach ($rx in $pointeeRx) { if ([regex]::IsMatch($Cod[$k], $rx)) { $f += "(R) pointee call in $k : $([regex]::Match($Cod[$k], $rx).Value)" } } }
    $d = $Txt[$pin.desc]
    $loose = ([regex]::Matches($d, '\{ TEXT\("([\w.]+)"\), TEXT\("[\w-]+"\), (?:true|false),')).Count
    $strict = @([regex]::Matches($d, '\{ TEXT\("([\w.]+)"\), TEXT\("(tick|roll|world-load|inert|never|read|write)"\), (true|false), TEXT\("(none|gameplay|structural)"\),'))
    if ($strict.Count -ne $loose) { $f += "(R) row shape: $loose rows, $($strict.Count) carry a valid saveEffect" }
    $seen = @{}
    foreach ($m in $strict) {
        $name = $m.Groups[1].Value; $grade = $m.Groups[3].Value + '/' + $m.Groups[4].Value; $seen[$name] = 1
        if (($m.Groups[3].Value -eq 'false') -ne ($m.Groups[4].Value -eq 'none')) { $f += "(R) $name : touchesSave false must pair with saveEffect none and only with it" }
        if (-not $golden.ContainsKey($name)) { $f += "(R) $name has no golden grade in this lint" } elseif ($golden[$name] -ne $grade) { $f += "(R) $name graded $grade, golden $($golden[$name])" }
    }
    foreach ($g in $golden.Keys) { if (-not $seen.ContainsKey($g)) { $f += "(R) golden row $g missing from the manifest" } }
    # ---- E (mcp-regrade-1): golden effect per console row; no note cites the const-signature phrase
    $eSeen = @{}
    foreach ($m in [regex]::Matches($d, '\{ TEXT\("(NodeShuffle\.\w+)"\), TEXT\("(read|write)"\), (?:true|false),')) {
        $n = $m.Groups[1].Value; $eSeen[$n] = 1
        if (-not $effectGolden.ContainsKey($n)) { $f += "(E) $n has no golden effect in this lint" }
        elseif ($effectGolden[$n] -ne $m.Groups[2].Value) { $f += "(E) $n effect $($m.Groups[2].Value), golden $($effectGolden[$n])" }
    }
    foreach ($g in $effectGolden.Keys) { if (-not $eSeen.ContainsKey($g)) { $f += "(E) golden effect row $g missing from the manifest" } }
    foreach ($rn in $readSites.Keys) {
        if ($effectGolden[$rn] -ne 'read') { $f += "(E) $rn has a read-site pin but is not golden read"; continue }
        foreach ($rf in $readSites[$rn]) {
            if (-not $Cod.Contains($rf)) { $f += "(E) read-site file $rf is not loaded"; continue }
            foreach ($n in ($forbidden + $readExtra)) { if ((Count $Cod[$rf] $n) -ne 0) { $f += "(E) read row $rn reaches '$n' in $rf" } }
        }
    }
    foreach ($g in $effectGolden.Keys) { if ($effectGolden[$g] -eq 'read' -and -not $readSites.ContainsKey($g)) { $f += "(E) golden read row $g has no source pin" } }
    foreach ($tbl in @('GNsMcpConfigKeys', 'GNsMcpConsole')) {
        $blk = [regex]::Match($d, $tbl + '\[\] = \{([\s\S]*?)\n    \};')
        if (-not $blk.Success) { $f += "(E) manifest table $tbl not found" }
        elseif ($blk.Groups[1].Value.IndexOf($constPhrase, [StringComparison]::OrdinalIgnoreCase) -ge 0) { $f += "(E) a note in $tbl cites the const-signature phrase as grading evidence" }
    }
    foreach ($k in @($pin.desc, $pin.snap, $pin.lr)) {
        $calls =([regex]::Matches($Cod[$k], '(?<!::)\bErrorJson\(')).Count
        $good = ([regex]::Matches($Txt[$k], '(?<!::)\bErrorJson\(TEXT\("(?:' + $errKinds + ')"\),')).Count
        if ($calls -ne $good) { $f += "(R) $k : $calls ErrorJson calls, $good with a known errorKind" }
    }
    if ((FnBody $Txt[$pin.snap] 'bool FNodeShuffleMcpReader::NotReady(').IndexOf('TEXT("errorKind")') -lt 0) { $f += '(R) NotReady must carry errorKind' }
    foreach ($pair in @(@($pin.desc, 'FString FNodeShuffleMcpReader::Describe()'), @($pin.snap, 'FString FNodeShuffleMcpReader::Help()'),
                        @($pin.snap, 'bool FNodeShuffleMcpReader::NotReady('), @($pin.snap, 'FString FNodeShuffleMcpReader::ErrorJson('))) {
        if ((FnBody $Cod[$pair[0]] $pair[1]).IndexOf('AddEnvelope(Root);') -lt 0) { $f += "(R) envelope missing in $($pair[1])" }
    }
    foreach ($cmd in $gridSites.Keys) {
        $file = $gridSites[$cmd][0]; $head = $gridSites[$cmd][1]
        if (-not $Txt.Contains($file)) { $f += "(R) water-grid site file missing: $file"; continue }
        $body = if ($head -eq '') { $Cod[$file] } else { FnBody $Cod[$file] $head }
        if ($body -eq '') { $f += "(R) water-grid site body not found for $cmd"; continue }
        if ($body.Contains($groundCall) -and -not $body.Contains($gridSuppress)) {
            if ((Count $d ('{ TEXT("' + $cmd + '"), TEXT("write"), true,')) -ne 1) { $f += "(R) $cmd traces the ground (water grid) and must be write / touchesSave true" }
        }
    }
    # ---- C1
    $reg = @()
    foreach ($k in $Txt.Keys) {
        if (-not $k.EndsWith('.cpp')) { continue }
        $t = $Txt[$k]
        $lit = @([regex]::Matches($t, '\bAdd(?:Bool|Int)\(TEXT\("(\w+)"\)') | ForEach-Object { $_.Groups[1].Value })
        if ($lit.Count -ne ([regex]::Matches($Cod[$k], '\bAdd(?:Bool|Int)\(')).Count) { $f += "(C1) an AddBool/AddInt call in $k has no literal key" }
        $reg += $lit
        foreach ($m in [regex]::Matches($t, 'Root->SectionProperties\.Add\((TEXT\("(\w+)"\)|(\w+))\s*,')) {
            if ($m.Groups[2].Success) { $reg += $m.Groups[2].Value; continue }
            $id = $m.Groups[3].Value
            $def = [regex]::Match($t, 'const TCHAR\* const ' + $id + ' = TEXT\("(\w+)"\)')
            if ($def.Success) { $reg += $def.Groups[1].Value }
            elseif (-not ($id -eq 'Key' -and $t.Contains('const auto AddBool = [&](const TCHAR* Key'))) { $f += "(C1) unresolved root key '$id' in $k" }
        }
    }
    $rows = @([regex]::Matches($Txt[$pin.desc], '\{ TEXT\("(\w+)"\), TEXT\("(tick|roll|world-load|inert|never)"\), (true|false),') | ForEach-Object { $_.Groups[1].Value })
    foreach ($k in (Set-Diff $reg $rows)) { $f += "(C1) registered config key missing from the manifest: $k" }
    foreach ($k in (Set-Diff $rows $reg)) { $f += "(C1) manifest config row is not a registered key: $k" }
    foreach ($k in ($rows | Group-Object | Where-Object { $_.Count -gt 1 })) { $f += "(C1) duplicate manifest row: $($k.Name)" }
    if ($reg.Count -lt 19) { $f += "(C1) only $($reg.Count) registered keys found; the extractor lost its population" }
    # ---- C2
    if ((Count $Txt[$pin.desc] '{ TEXT("RerollNow"), TEXT("tick"), true,') -ne 1) { $f += '(C2) RerollNow must be live tick with touchesSave true' }
    # ---- C3
    $cmds = @(); $vars = @()
    foreach ($k in $Txt.Keys) {
        $t = $Txt[$k]; $c = $Cod[$k]
        $cm = @([regex]::Matches($t, '\bFAutoConsoleCommand\w*\s+\w+\(\s*TEXT\("([\w.]+)"\)') | ForEach-Object { $_.Groups[1].Value })
        $vm = @([regex]::Matches($t, '\b(?:FAutoConsoleVariableRef|FAutoConsoleVariable|TAutoConsoleVariable<[^>]+>)\s+\w+\(\s*TEXT\("([\w.]+)"\)') | ForEach-Object { $_.Groups[1].Value })
        if ($cm.Count -ne ([regex]::Matches($c, '\bFAutoConsoleCommand\w*\s+\w+\(')).Count) { $f += "(C3) a console command declaration in $k has no literal name" }
        if ($vm.Count -ne ([regex]::Matches($c, '\b(?:FAutoConsoleVariableRef|FAutoConsoleVariable|TAutoConsoleVariable<[^>]+>)\s+\w+\(')).Count) { $f += "(C3) a console variable declaration in $k has no literal name" }
        if ([regex]::IsMatch($c, '\bRegisterConsole(?:Command|Variable\w*)\s*\(')) { $f += "(C3) $k registers through IConsoleManager; extend this lint to read it" }
        $cmds += $cm; $vars += $vm
    }
    $cmds = @($cmds | Where-Object { $_.StartsWith('NodeShuffle.') }); $vars = @($vars | Where-Object { $_.StartsWith('NodeShuffle.') })
    $crow = @([regex]::Matches($Txt[$pin.desc], '\{ TEXT\("(NodeShuffle\.\w+)"\), TEXT\("(read|write)"\), (true|false),') | ForEach-Object { $_.Groups[1].Value })
    $cvBlock = [regex]::Match($Txt[$pin.desc], 'GNsMcpCvars\[\] = \{([\s\S]*?)\};')
    $vrow = @(if ($cvBlock.Success) { [regex]::Matches($cvBlock.Groups[1].Value, 'TEXT\("(NodeShuffle\.\w+)"\)') | ForEach-Object { $_.Groups[1].Value } })
    foreach ($k in (Set-Diff $cmds $crow)) { $f += "(C3) console command missing from the manifest: $k" }
    foreach ($k in (Set-Diff $crow $cmds)) { $f += "(C3) manifest console row is not a registered command: $k" }
    foreach ($k in (Set-Diff $vars $vrow)) { $f += "(C3) console variable missing from the manifest: $k" }
    foreach ($k in (Set-Diff $vrow $vars)) { $f += "(C3) manifest cvar row is not a registered variable: $k" }
    if ($cmds.Count -lt 11 -or $vars.Count -lt 6) { $f += "(C3) extractor found $($cmds.Count) commands / $($vars.Count) variables; it lost its population" }
    # ---- C4
    $nsm = $Txt[$pin.nsmod]
    $lineTags = @([regex]::Matches($nsm, '(?m)^    UE_LOG\(LogNodeShuffle, Display, TEXT\("===== NodeShuffle packet marker \(([\w-]+)\)====="\)\);') | ForEach-Object { $_.Groups[1].Value })
    if ($lineTags.Count -ne (Count $nsm 'packet marker (')) { $f += '(C4) a packet marker in NodeShuffle.cpp is not in the established own-line shape' }
    $loaded = [regex]::Match($nsm, 'TEXT\("===== NodeShuffle [\d.]+ LOADED \(([\w-]+)\)====="\)')
    $mh = $Txt[$pin.mark]
    $arr = [regex]::Match($mh, 'PacketMarkers\[\] = \{([\s\S]*?)\};')
    $hdrTags = @(if ($arr.Success) { [regex]::Matches($arr.Groups[1].Value, 'TEXT\("([\w-]+)"\)') | ForEach-Object { $_.Groups[1].Value } })
    $hdrLoaded = [regex]::Match($mh, 'LoadedLineTag = TEXT\("([\w-]+)"\)')
    if (($lineTags -join '|') -ne ($hdrTags -join '|')) { $f += "(C4) markers header does not list NodeShuffle.cpp's packet markers in order (cpp: $($lineTags -join ','); header: $($hdrTags -join ','))" }
    if (-not $loaded.Success -or -not $hdrLoaded.Success -or $loaded.Groups[1].Value -ne $hdrLoaded.Groups[1].Value) { $f += '(C4) LOADED tag differs between NodeShuffle.cpp and the markers header' }
    # rwg-0-2-0-2: no longer the newest; pinned present (check_rwg020_lint G6). ns-rollchat-1 bumped the newest marker.
    # ns-rollchat-1: no longer the newest; pinned present (check_rollchat_lint RC1). ns-rocks-1 bumped the newest marker.
    # ns-rocks-1: no longer the newest; pinned present (check_rocks_lint K1). ns-quietchat-1 bumped the newest marker.
    # ns-quietchat-1: no longer the newest; pinned present (check_scanfilter_lint S9). ns-scanfilter-1 bumped the newest marker.
    # ns-scanfilter-1: no longer the newest; pinned present (check_scanfilter_lint S9). ns-scanfilter-2 bumped the newest marker.
    # ns-scanfilter-2: no longer the newest; pinned present (check_scanfilter_lint S9). ns-scanfilter-3 bumped the newest marker.
    # ns-scanfilter-3: no longer the newest; pinned present (check_scanfilter_lint S9). ns-scanfilter-4 bumped the newest marker.
    # ns-scanfilter-4: no longer the newest; pinned present (check_scanfilter_lint S9). ns-rollfirst-1 bumped the newest marker.
    # ns-rollfirst-1: no longer the newest; pinned present (check_rollfirst_lint R9). rwg-0-2-1-1 bumped the newest marker.
    # rwg-0-2-1-1: no longer the newest; pinned present (check_rwg020_lint G9). ns-status-1 bumped the newest marker.
    $own = '2026-11-15-' + 'status-1' # McpDescribe's buildMarker reads the header's newest entry
    if ($hdrTags.Count -eq 0 -or $hdrTags[-1] -ne $own -or (Count $nsm $own) -ne 1) { $f += '(C4) this packet marker must be the newest, once, on its own line' }
    if ((Count $Txt[$pin.desc] $newestExpr) -ne 1 -or [regex]::IsMatch($Txt[$pin.desc], 'TEXT\("20\d\d-\d\d-\d\d-')) { $f += '(C4) buildMarker must come from the markers header (newest entry), never a literal in Describe' }
    # ---- F (contract R4.4: finite numbers only)
    $nearC = FnBody $Cod[$pin.snap] $nearHead
    if ((Count $snapC $finGuard) -ne 1 -or -not (Ordered $nearC @($finLoop, $finGuard, $finDx))) { $f += '(F) near must skip a Layout entry with a non-finite location component inside its Layout loop, before the distance math' }
    $nearT = FnBody $Txt[$pin.snap] $nearHead
    if ((Count $Txt[$pin.snap] $finEmit) -ne 1 -or (Count $nearT $finEmit) -ne 1) { $f += '(F) near must emit the non-finite skip count' }
    if ((Count (FnBody $Txt[$pin.snap] 'FString FNodeShuffleMcpReader::Help()') $finHelp) -ne 1) { $f += '(F) help must name the non-finite skip count' }
    if ((Count $nearC $finPawn) -ne 1 -or -not (Ordered $nearC @('FoundPawn->GetActorLocation();', $finPawn, $finPawnRetC, 'XCm = P.X;'))) { $f += '(F) near must refuse a pawn location with a non-finite X or Y before using it as the point' }
    # ---- L (mcp-lastroll-1)
    if ((Count $h $lrMember) -ne 1) { $f += '(L) the subsystem must hold exactly one plain member line: FNodeShuffleLastRoll LastRoll;' }
    if ([regex]::IsMatch($Cod[$pin.hdr], $lrReflRx + '[^;{}]*FNodeShuffleLastRoll\s+LastRoll\b') -or [regex]::IsMatch($Cod[$pin.hdr], $lrMutRx + '\s+FNodeShuffleLastRoll\b')) { $f += '(L) the LastRoll member must carry no property macro and must not be mutable' }
    if ([regex]::IsMatch($Cod[$pin.lrh], $lrReflRx) -or [regex]::IsMatch($Cod[$pin.lrh], $lrMutRx)) { $f += '(L) NodeShuffleLastRoll.h must stay plain C++: no reflection macro, no mutable' }
    if (-not (Ordered $h @('#include "NodeShuffleLastRoll.h"', '#include "NodeShuffleSubsystem.generated.h"'))) { $f += '(L) the subsystem header must include NodeShuffleLastRoll.h before its generated header' }
    $mcpKeys = @($pin.desc, $pin.snap, $pin.com, $pin.lr)
    foreach ($k in $mcpKeys) { foreach ($rx in $lrMcpWriteRx) { if ([regex]::IsMatch($Cod[$k], $rx)) { $f += "(L) MCP file $k writes LastRoll or binds it non-const: $([regex]::Match($Cod[$k], $rx).Value)" } } }
    $stores = 0; $refs = 0; $begins = 0; $endCalls = 0; $dAll = 0
    foreach ($k in $Cod.Keys) {
        if ($mcpKeys -contains $k -or $k -eq $pin.lrh) { continue }
        $c = $Cod[$k]
        $sm = @([regex]::Matches($c, $lrStoreRx))
        $stores += $sm.Count; $dAll += @($sm | Where-Object { $_.Groups[1].Value -eq 'Density' }).Count
        $refs += ([regex]::Matches($c, '\bLastRoll\s*\.')).Count; $begins += (Count $c $lrBegin); $endCalls += ([regex]::Matches($c, $lrEndCallRx)).Count
    }
    if ($begins -ne 1) { $f += "(L) exactly one LastRoll reset expected in roll code, found $begins" }
    if ($refs -ne $stores + 2) { $f += "(L) roll code may only store into LastRoll: $refs references, $stores stores (2 others allowed: the reset and the sequence copy)" }
    $rollT = FnBody $Txt[$pin.sub] $lrRollSig
    if (-not $rollT.StartsWith($lrRollSig + "`n{`n    " + $lrBegin)) { $f += '(L) the LastRoll reset must be the first statement of RollLayout' }
    if ($endCalls -ne 1 -or ([regex]::Matches((FnBody $Cod[$pin.sub] $lrRollSig), $lrEndCallRx)).Count -ne 1) { $f += '(L) RwgDensityBudgetEnd must be called exactly once, from RollLayout (so after the reset)' }
    $errs = @()
    $densT = FnBody $Txt[$pin.dens] $lrDensSig
    $ci = $densT.IndexOf($lrDensFmt); $ni = $densT.IndexOf($lrDensNext)
    $dIn = @([regex]::Matches($densT, $lrStoreRx) | Where-Object { $_.Groups[1].Value -eq 'Density' })
    if ($ci -lt 0 -or $ni -lt 0 -or $dIn.Count -eq 0 -or $dIn.Count -ne $dAll -or @($dIn | Where-Object { $_.Index -lt $ci -or $_.Index -gt $ni }).Count -gt 0) { $f += "(L) every Density store must sit in RwgDensityBudgetEnd between the ROLLDENSITY line and its legend line ($($dIn.Count) there, $dAll in all)" }
    if ((Count $densT ('LastRoll.Density' + $lrSeqStore)) -ne 1) { $f += '(L) the Density stores must record the roll sequence once' }
    $dMap = LrStores $densT 'Density' ([ref]$errs)
    $f += $errs
    if ($dMap['CellLinesPrintable'] -ne 'Printable') { $f += '(L) CellLinesPrintable must copy the Printable local (the cellLinesOmitted derivation reads it)' }
    $lrT = $Txt[$pin.lr]
    $f += LrChain 'ROLLDENSITY' (LrCensus $densT $lrDensFmt) (LrBlock $lrT 'void NsMcpDensityFields(') 'D' $dMap @('cellsAtCap') $lrDensDerived
    if ((FnBody $Cod[$pin.lr] 'FString FNodeShuffleMcpReader::LastRoll(').IndexOf('AddEnvelope(Root);') -lt 0) { $f += '(L) envelope missing in LastRoll' }
    if ((Count $Txt[$pin.snap] 'if (Section.Equals(TEXT("lastRoll"), ESearchCase::IgnoreCase)) { OutBranch = TEXT("lastRoll"); return LastRoll(Sub, OutBranch); }') -ne 1) { $f += '(L) the lastRoll section must be dispatched' }
    if ((Count (FnBody $Txt[$pin.snap] 'FString FNodeShuffleMcpReader::Help()') 'Sections->SetStringField(TEXT("lastRoll"),') -ne 1) { $f += '(L) help must describe the lastRoll section' }
    if ((Count $Txt[$pin.desc] 'TEXT("help | near | summary | lastRoll (') -ne 1) { $f += "(L) the manifest's snapshotArgs must list lastRoll" }
    if ((Count $Txt[$pin.snap] 'is not help, near, summary or lastRoll') -ne 1) { $f += '(L) the unknown-section error must list lastRoll' }
    return ,$f
}

$base = Invoke-McpChecks $text $code
if ($base.Count -gt 0) { $base | ForEach-Object { Write-Host "FAIL$_" }; Write-Host "MCPCONTRACT lint: FAIL -- baseline $($base.Count) problem(s)"; exit 1 }
Write-Host "BASELINE GREEN: M1..M5, R, E, C1..C4, F, L hold across $($text.Count) loaded files"

$H = $pin.hdr; $D = $pin.desc; $S = $pin.snap; $CMN = $pin.com; $MK = $pin.mark; $NS = $pin.nsmod; $CF = $pin.cfg; $SG = $pin.sg
$LRC = $pin.lr; $LRH = $pin.lrh; $SU = $pin.sub; $DN = $pin.dens
$BC = 'NodeShuffle\NodeShuffle.Build.cs'; $WD = 'NodeShuffle\Private\NodeShuffleWellDump.cpp'
$mutants = @(
    @{ Name = 'N1 McpDescribe loses const'; Key = $H; Expect = 'M1'; Find = $sigDescribe; Repl = 'UFUNCTION() FString McpDescribe();' }
    @{ Name = 'N2 McpSnapshot takes the args by value'; Key = $H; Expect = 'M1'; Find = $sigSnapshot; Repl = 'UFUNCTION() FString McpSnapshot(FString ArgsJson);' }
    @{ Name = 'N3 a write action is declared'; Key = $H; Expect = 'M1'; Find = $friendDecl; Repl = ($friendDecl + "`n    UFUNCTION() FString " + $thirdFn + '(const FString& Name, const FString& ArgsJson);') }
    @{ Name = 'N4 reader friend dropped'; Key = $H; Expect = 'M1'; Find = ("    " + $friendDecl + "`n"); Repl = '' }
    @{ Name = 'N5 a source file includes an MCP header'; Key = $S; Expect = 'M2'; Find = '#include "NodeShuffleMcpCommon.h"'; Repl = ('#include "NodeShuffleMcpCommon.h"' + "`n#include `"" + $mcpModName + '/Public/McpTools.h"') }
    @{ Name = 'N6 Build.cs gains the MCP module'; Key = $BC; Expect = 'M2'; Find = '"Json"'; Repl = ('"Json", "' + $mcpModName + '"') }
    @{ Name = 'N7 snapshot loads a class'; Key = $S; Expect = 'M3'; Find = 'O->SetStringField(TEXT("nodeClass"), ShortObjectName(E.NodeClassPath));'; Repl = ('O->SetStringField(TEXT("nodeClass"), ' + 'Load' + 'Class<UObject>(nullptr, *E.NodeClassPath)->GetName());') }
    @{ Name = 'N8 snapshot traces'; Key = $S; Expect = 'M3'; Find = 'const FVector P = FoundPawn->GetActorLocation();'; Repl = ('const FVector P = FoundPawn->GetActorLocation(); FHitResult Hit; Sub.GetWorld()->' + 'Line' + 'TraceSingleByChannel(Hit, P, P, ECC_WorldStatic);') }
    @{ Name = 'N9 const_cast to reach a member'; Key = $S; Expect = 'M3'; Find = 'int32 SpawnedValid = 0;'; Repl = ('int32 SpawnedValid = 0; ' + 'const' + '_cast<ANodeShuffleSubsystem&>(Sub);') }
    @{ Name = 'N10 summary calls the latching map-mode getter'; Key = $S; Expect = 'M3'; Find = 'NsMcpMapModeName(Sub.MapMode)'; Repl = ('NsMcpMapModeName(Sub.' + 'GetMap' + 'Mode())') }
    @{ Name = 'N11 member assignment through Sub.'; Key = $S; Expect = 'M3'; Find = 'int32 ModdedOrigin = 0;'; Repl = 'int32 ModdedOrigin = 0; Sub.LateNodeScansCompleted = 0;' }
    @{ Name = 'N12 container mutation through Sub.'; Key = $S; Expect = 'M3'; Find = 'int32 ModdedOrigin = 0;'; Repl = 'int32 ModdedOrigin = 0; Sub.Layout.Empty();' }
    @{ Name = 'N13 a reader takes the subsystem non-const'; Key = $CMN; Expect = 'M3'; Find = 'static FString Summary(const ANodeShuffleSubsystem& Sub, FString& OutBranch);'; Repl = 'static FString Summary(ANodeShuffleSubsystem& Sub, FString& OutBranch);' }
    @{ Name = 'N14 trampoline forwards a mutable *this'; Key = $S; Expect = 'M3'; Find = $trampoline; Repl = 'FNodeShuffleMcpReader::Snapshot(*this, ArgsJson, Branch);' }
    @{ Name = 'N15 snapshot reads config'; Key = $S; Expect = 'M3'; Find = 'int32 ModdedOrigin = 0;'; Repl = ('int32 ModdedOrigin = 0; const bool bX = FNodeShuffleConfigStruct::' + 'GetActive' + 'Config(nullptr).Enabled;') }
    @{ Name = 'N16 snapshot log raised to Display'; Key = $S; Expect = 'M4'; Find = $logVerbose; Repl = 'UE_LOG(LogNodeShuffle, Display, ' }
    @{ Name = 'N17 a second log line in near'; Key = $S; Expect = 'M4'; Find = 'const double RadiusCm = RadiusM * 100.0;'; Repl = ('const double RadiusCm = RadiusM * 100.0; ' + $logVerbose + 'TEXT("x"));') }
    @{ Name = 'N18 cap raised tenfold'; Key = $CMN; Expect = 'M5'; Find = $capDecl; Repl = 'static constexpr int32 MaxOutputBytes = 640 * 1024;' }
    @{ Name = 'N19 near stops re-serializing under the cap'; Key = $S; Expect = 'M5'; Find = ($capLoop + "`n        if (Keep == 0) { OutBranch += TEXT("" error: over cap with no rows"")"); Repl = ('break;' + "`n        if (Keep == 0) { OutBranch += TEXT("" error: over cap with no rows"")") }
    @{ Name = 'N20 config key dropped from the manifest'; Key = $D; Expect = 'C1'; Find = '{ TEXT("EnableDiagnostics"), TEXT("tick"), false,'; Repl = '{ TEXT("EnableDiagnosticz"), TEXT("tick"), false,' }
    @{ Name = 'N21 new config key registered'; Key = $CF; Expect = 'C1'; Find = '    AddBool(TEXT("EnableDiagnostics"), false,'; Repl = ("    AddBool(TEXT(""NewKeyX""), false, TEXT(""x""), TEXT(""x""));`n" + '    AddBool(TEXT("EnableDiagnostics"), false,') }
    @{ Name = 'N22 new root array registered through a constant'; Key = $SG; Expect = 'C1'; Find = 'Root->SectionProperties.Add(RowsKey, Rows);'; Repl = 'Root->SectionProperties.Add(RowsKey, Rows); Root->SectionProperties.Add(ExtraKey, Rows);' }
    @{ Name = 'N23 re-roll trigger marked save-safe'; Key = $D; Expect = 'C2'; Find = '{ TEXT("RerollNow"), TEXT("tick"), true,'; Repl = '{ TEXT("RerollNow"), TEXT("tick"), false,' }
    @{ Name = 'N24 new console command registered'; Key = $WD; Expect = 'C3'; Find = 'static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleDumpWellsCmd('; Repl = ("static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleNewCmd(TEXT(""NodeShuffle.NewCmdX""), TEXT(""x""), {});`n" + 'static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleDumpWellsCmd(') }
    @{ Name = 'N25 console row dropped from the manifest'; Key = $D; Expect = 'C3'; Find = '{ TEXT("NodeShuffle.DumpWells"), TEXT("read"), false,'; Repl = '{ TEXT("NodeShuffle.DumpWellz"), TEXT("read"), false,' }
    @{ Name = 'N26 new console variable registered'; Key = $WD; Expect = 'C3'; Find = 'static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleDumpWellsCmd('; Repl = ("static TAutoConsoleVariable<int32> CVarX(TEXT(""NodeShuffle.NewVarX""), 0, TEXT(""x""));`n" + 'static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleDumpWellsCmd(') }
    @{ Name = 'N27 command registered with a computed name'; Key = $WD; Expect = 'C3'; Find = 'static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleDumpWellsCmd('; Repl = ("static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleNewCmd(*Name, TEXT(""x""), {});`n" + 'static FAutoConsoleCommandWithWorldAndArgs GNodeShuffleDumpWellsCmd(') }
    @{ Name = 'N28 new packet marker line not in the header'; Key = $NS; Expect = 'C4'; Find = '    UE_LOG(LogNodeShuffle, Display, TEXT("===== NodeShuffle packet marker (2026-11-01-rwg-thoriumclass-2)====="));'; Repl = ('    UE_LOG(LogNodeShuffle, Display, TEXT("===== NodeShuffle packet marker (2026-11-01-rwg-thoriumclass-2)====="));' + "`n" + '    UE_LOG(LogNodeShuffle, Display, TEXT("===== NodeShuffle packet marker (2099-01-01-x-1)====="));') }
    @{ Name = 'N29 header marker dropped'; Key = $MK; Expect = 'C4'; Find = "        TEXT(""2026-11-01-rwg-density-1""),`n"; Repl = '' }
    @{ Name = 'N30 LOADED tag bumped in the cpp only'; Key = $NS; Expect = 'C4'; Find = 'LOADED (2026-10-31-rwg-farspawn-1f)'; Repl = 'LOADED (2026-10-31-rwg-farspawn-1g)' }
    @{ Name = 'N31 header order swapped'; Key = $MK; Expect = 'C4'; Find = "        TEXT(""2026-11-01-rwg-logflood-1""),`n        TEXT(""2026-11-01-rwg-rerolldup-1""),"; Repl = "        TEXT(""2026-11-01-rwg-rerolldup-1""),`n        TEXT(""2026-11-01-rwg-logflood-1""),"}
    @{ Name = 'N32 marker in an unanchored shape'; Key = $NS; Expect = 'C4'; Find = '    UE_LOG(LogNodeShuffle, Display, TEXT("===== NodeShuffle packet marker (2026-11-01-rwg-density-1)====="));'; Repl = '    UE_LOG(LogNodeShuffle, Log, TEXT("===== NodeShuffle packet marker (2026-11-01-rwg-density-1)====="));' }
    @{ Name = 'N34 buildMarker hard-coded in Describe'; Key = $D; Expect = 'C4'; Find = ('NodeShuffleBuildMarkers::' + $newestExpr); Repl = ('TEXT("' + '2026-11-10-' + 'mcp-lastroll-1' + '")') }
    @{ Name = 'X1 ActivePercent graded save-safe'; Key = $D; Expect = 'R'; Find = '{ TEXT("ActivePercent"), TEXT("roll"), true, TEXT("structural"),'; Repl = '{ TEXT("ActivePercent"), TEXT("roll"), false, TEXT("none"),' }
    @{ Name = 'X3 pointee write through a SpawnedNodes slot'; Key = $S; Expect = 'R'; Find = 'if (Slot && IsValid(*Slot)) { ++LiveEntries; }'; Repl = ('if (Slot && IsValid(*Slot)) { ++LiveEntries; (*Slot)' + '->SetActorHiddenInGame(true); }') }
    @{ Name = 'X4 SeedHere graded read / save-safe'; Key = $D; Expect = 'R'; Find = '{ TEXT("NodeShuffle.SeedHere"), TEXT("write"), true, TEXT("structural"),'; Repl = '{ TEXT("NodeShuffle.SeedHere"), TEXT("read"), false, TEXT("none"),' }
    @{ Name = 'X5 world pointee destroys an actor'; Key = $S; Expect = 'M3'; Find = 'int32 ModdedOrigin = 0;'; Repl = ('int32 ModdedOrigin = 0; Sub.GetWorld()->' + 'Destroy' + 'Actor(nullptr);') }
    @{ Name = 'X6 Here back to save-safe'; Key = $D; Expect = 'R'; Find = '{ TEXT("NodeShuffle.Here"), TEXT("write"), true, TEXT("structural"),'; Repl = '{ TEXT("NodeShuffle.Here"), TEXT("write"), false, TEXT("none"),' }
    @{ Name = 'X7 errorKind dropped from NotReady'; Key = $S; Expect = 'R'; Find = "    Root->SetStringField(TEXT(""errorKind""), Kind);`n    Root->SetStringField(TEXT(""error""), Reason);"; Repl = '    Root->SetStringField(TEXT("error"), Reason);' }
    @{ Name = 'X8 unknown errorKind'; Key = $S; Expect = 'R'; Find = 'return ErrorJson(TEXT("bad-args"), TEXT("radiusM must be greater than 0"));'; Repl = 'return ErrorJson(TEXT("oops"), TEXT("radiusM must be greater than 0"));' }
    @{ Name = 'X9 RerollNow graded gameplay'; Key = $D; Expect = 'R'; Find = '{ TEXT("RerollNow"), TEXT("tick"), true, TEXT("structural"),'; Repl = '{ TEXT("RerollNow"), TEXT("tick"), true, TEXT("gameplay"),' }
    @{ Name = 'X10 envelope dropped from ErrorJson'; Key = $S; Expect = 'R'; Find = "    AddEnvelope(Root);`n    Root->SetStringField(TEXT(""errorKind""), Kind);`n    Root->SetStringField(TEXT(""error""), Message);"; Repl = "    Root->SetStringField(TEXT(""errorKind""), Kind);`n    Root->SetStringField(TEXT(""error""), Message);" }
    @{ Name = 'X11 PointAtHere back to read (water-grid symmetry)'; Key = $D; Expect = 'R'; Find = '{ TEXT("NodeShuffle.PointAtHere"), TEXT("write"), true,'; Repl = '{ TEXT("NodeShuffle.PointAtHere"), TEXT("read"), true,' }
    @{ Name = 'X12 pawn pointee call beyond its location'; Key = $S; Expect = 'R'; Find = 'const FVector P = FoundPawn->GetActorLocation();'; Repl = ('const FVector P = FoundPawn->GetActorLocation(); FoundPawn' + '->GetName();') }
    @{ Name = 'N33 the uplugin gains an MCP plugin dependency'; Key = '..\NodeShuffle.uplugin'; Expect = 'M2'; Find = '"Plugins": ['; Repl = ('"Plugins": [ { "Name": "' + $mcpModName + '" },') }
    @{ Name = 'F1 near drops the finite guard'; Key = $S; Expect = 'F'; Find = $finGuard; Repl = '' }
    @{ Name = 'F2 finite guard moved after the distance math'; Key = $S; Expect = 'F'; Find = ($finGuard + "`n        " + $finDx); Repl = ($finDx + "`n        " + $finGuard) }
    @{ Name = 'F3 nonFiniteSkipped not emitted'; Key = $S; Expect = 'F'; Find = $finEmit; Repl = '' }
    @{ Name = 'F4 help loses the nonFiniteSkipped clause'; Key = $S; Expect = 'F'; Find = $finHelp; Repl = 'returned (' }
    @{ Name = 'F5 pawn-location finite guard disabled'; Key = $S; Expect = 'F'; Find = $finPawn; Repl = 'if (false)' }
    @{ Name = 'F6 pawn guard body gutted'; Key = $S; Expect = 'F'; Find = $finPawnRet; Repl = '}' }
    @{ Name = 'LR1 a density store deleted'; Key = $DN; Expect = 'L'; Find = 'LastRoll.Density.Accepted = S.AcceptedTotal; '; Repl = '' }
    @{ Name = 'LR2 a density store copies the wrong argument'; Key = $DN; Expect = 'L'; Find = 'LastRoll.Density.RejectedWater = S.RejWater;'; Repl = 'LastRoll.Density.RejectedWater = S.RejVoid;' }
    @{ Name = 'LR3 reset moved after a store'; Key = $SU; Expect = 'L'; Find = ('    ' + $lrBegin + ' // mcp-lastroll-1'); Repl = '    // mcp-lastroll-1'; Find2 = 'RwgDensityBudgetEnd(); }'; Repl2 = ('RwgDensityBudgetEnd(); ' + $lrBegin + ' }') }
    @{ Name = 'LR4 member made mutable'; Key = $H; Expect = 'L'; Find = $lrMember; Repl = ("`n    " + 'mut' + 'able FNodeShuffleLastRoll LastRoll;' + "`n") }
    @{ Name = 'LR5 member gains a property macro'; Key = $H; Expect = 'L'; Find = $lrMember; Repl = ("`n    " + 'UPROP' + 'ERTY() FNodeShuffleLastRoll LastRoll;' + "`n") }
    @{ Name = 'LR6 reader writes the member'; Key = $LRC; Expect = 'L'; Find = 'const bool bCaptured = L.RollSeq > 0;'; Repl = 'const bool bCaptured = L.RollSeq > 0; Sub.LastRoll.RollSeq = 0;' }
    @{ Name = 'LR7 reader binds the member non-const'; Key = $LRC; Expect = 'L'; Find = 'const FNodeShuffleLastRoll& L = Sub.LastRoll;'; Repl = 'FNodeShuffleLastRoll& L = Sub.LastRoll;' }
    @{ Name = 'LR8 struct becomes reflected'; Key = $LRH; Expect = 'L'; Find = "struct FNodeShuffleLastRoll`n{"; Repl = ('USTR' + 'UCT()' + "`nstruct FNodeShuffleLastRoll`n{") }
    @{ Name = 'LR9 roll code reads the member'; Key = $SU; Expect = 'L'; Find = '    RwgProvCandMarkDirty(bIsReroll'; Repl = "    if (LastRoll.RollSeq > 5) { return; }`n    RwgProvCandMarkDirty(bIsReroll" }
    @{ Name = 'LR10 density store after the legend line'; Key = $DN; Expect = 'L'; Find = '    RwgDensityState.Reset(); // the late-node deal'; Repl = "    LastRoll.Density.Draws = S.Draws;`n    RwgDensityState.Reset(); // the late-node deal" }
    @{ Name = 'LR18 density store in another function'; Key = $SU; Expect = 'L'; Find = 'RwgDensityBudgetEnd(); }'; Repl = 'RwgDensityBudgetEnd(); LastRoll.Density.Calls = 0; }' }
    @{ Name = 'LR11 reader field renamed'; Key = $LRC; Expect = 'L'; Find = 'TEXT("draws"), D.Draws'; Repl = 'TEXT("drawCount"), D.Draws' }
    @{ Name = 'LR12 reader serves the uncopied cellsAtCap'; Key = $LRC; Expect = 'L'; Find = 'O->SetNumberField(TEXT("draws"), D.Draws);'; Repl = 'O->SetNumberField(TEXT("draws"), D.Draws); O->SetNumberField(TEXT("cellsAtCap"), D.Draws);' }
    @{ Name = 'LR13 reader logs'; Key = $LRC; Expect = 'M4'; Find = 'const FNodeShuffleLastRoll& L = Sub.LastRoll;'; Repl = ('const FNodeShuffleLastRoll& L = Sub.LastRoll; ' + $logVerbose + 'TEXT("x"));') }
    @{ Name = 'LR14 help loses lastRoll'; Key = $S; Expect = 'L'; Find = 'Sections->SetStringField(TEXT("lastRoll"),'; Repl = 'Sections->SetStringField(TEXT("lastRollX"),' }
    @{ Name = 'LR15 manifest loses lastRoll'; Key = $D; Expect = 'L'; Find = 'help | near | summary | lastRoll ('; Repl = 'help | near | summary (' }
    @{ Name = 'LR16 error text loses lastRoll'; Key = $S; Expect = 'L'; Find = 'is not help, near, summary or lastRoll'; Repl = 'is not help, near or summary' }
    @{ Name = 'LR17 dispatch dropped'; Key = $S; Expect = 'L'; Find = 'return LastRoll(Sub, OutBranch); }'; Repl = 'return Help(); }' }
    @{ Name = 'LR19 density sequence copy dropped'; Key = $DN; Expect = 'L'; Find = ('LastRoll.Density' + $lrSeqStore); Repl = '' }
    @{ Name = 'E1 WhereCaveNodes back to read (lazy cave-store load)'; Key = $D; Expect = 'E'; Find = '{ TEXT("NodeShuffle.WhereCaveNodes"), TEXT("write"), true,'; Repl = '{ TEXT("NodeShuffle.WhereCaveNodes"), TEXT("read"), true,' }
    @{ Name = 'E2 NearestCave back to read (map-mode latch)'; Key = $D; Expect = 'E'; Find = '{ TEXT("NodeShuffle.NearestCave"), TEXT("write"), false,'; Repl = '{ TEXT("NodeShuffle.NearestCave"), TEXT("read"), false,' }
    @{ Name = 'E3 DumpExtractors back to read (object loads)'; Key = $D; Expect = 'E'; Find = '{ TEXT("NodeShuffle.DumpExtractors"), TEXT("write"), false,'; Repl = '{ TEXT("NodeShuffle.DumpExtractors"), TEXT("read"), false,' }
    @{ Name = 'E4 ProbeNearestNode loosened to read (unmeasured engine queries)'; Key = $D; Expect = 'E'; Find = '{ TEXT("NodeShuffle.ProbeNearestNode"), TEXT("write"), false,'; Repl = '{ TEXT("NodeShuffle.ProbeNearestNode"), TEXT("read"), false,' }
    @{ Name = 'E5 a console note cites the const-signature phrase again'; Key = $D; Expect = 'E'; Find = 'Logs a census of every loaded resource well.'; Repl = ('Logs a census of every loaded resource well (' + $constPhrase + ').') }
    @{ Name = 'E6 a config note cites the phrase in upper case'; Key = $D; Expect = 'E'; Find = 'Read only by RollLayout (the new-location quota).'; Repl = ('Read only by RollLayout (the new-location quota; ' + $constPhrase.ToUpper() + ').') }
    @{ Name = 'E7 ProbeNearestNode gains a ground trace (grid learning) while touchesSave false'; Key = 'NodeShuffle\Private\NodeShuffleNodeProbe.cpp'; Expect = 'R'; Find = 'RunTotallyInsideProbe(W, E.Location, Subject, Inside);'; Repl = ('FVector GL; FRotator GR; bool bWet = false; ' + $groundCall + 'E.Location, E.Location.Z, nullptr, nullptr, GL, GR, bWet); RunTotallyInsideProbe(W, E.Location, Subject, Inside);') }
    @{ Name = 'E8 DumpWells census reaches the subsystem'; Key = 'NodeShuffle\Private\NodeShuffleWellCensus.cpp'; Expect = 'E'; Find = 'Out.IteratorCores = Cores.Num();'; Repl = ('Out.IteratorCores = Cores.Num(); for (TActorIterator<' + 'ANodeShuffle' + 'Subsystem> SIt(World); SIt; ++SIt) { SIt->' + 'GetMap' + 'Mode(); }') }
    @{ Name = 'E9 DumpWells handler traces'; Key = 'NodeShuffle\Private\NodeShuffleWellDump.cpp'; Expect = 'E'; Find = 'CollectWellCensus(World, C);'; Repl = ('CollectWellCensus(World, C); FHitResult H; World->' + 'Line' + 'TraceSingleByChannel(H, FVector::ZeroVector, FVector::UpVector, ECC_WorldStatic);') }
    @{ Name = 'X13 AuditPlacements back to save-safe (water-grid loader)'; Key = $D; Expect = 'R'; Find = '{ TEXT("NodeShuffle.AuditPlacements"), TEXT("write"), true, TEXT("structural"),'; Repl = '{ TEXT("NodeShuffle.AuditPlacements"), TEXT("write"), false, TEXT("none"),' }
    @{ Name = 'X14 WhereCaveNodes back to save-safe (cave-store loader)'; Key = $D; Expect = 'R'; Find = '{ TEXT("NodeShuffle.WhereCaveNodes"), TEXT("write"), true, TEXT("structural"),'; Repl = '{ TEXT("NodeShuffle.WhereCaveNodes"), TEXT("write"), false, TEXT("none"),' }
    @{ Name = 'LR20 reader loads a class';Key = $LRC; Expect = 'M3'; Find = 'const bool bCaptured = L.RollSeq > 0;'; Repl = ('const bool bCaptured = L.RollSeq > 0; ' + 'Load' + 'Class<UObject>(nullptr, TEXT("x"));') }
)
$bad = 0
foreach ($m in $mutants) {
    if (-not $text.Contains($m.Key)) { Write-Host "MUTANT-SETUP FAIL $($m.Name): no loaded file $($m.Key)"; $bad++; continue }
    if ((Count $text[$m.Key] $m.Find) -ne 1) { Write-Host "MUTANT-SETUP FAIL $($m.Name): find text not unique in $($m.Key)"; $bad++; continue }
    $t2 = [ordered]@{}; foreach ($k in $text.Keys) { $t2[$k] = $text[$k] }
    $c2 = [ordered]@{}; foreach ($k in $code.Keys) { $c2[$k] = $code[$k] }
    $t2[$m.Key] = $t2[$m.Key].Replace($m.Find, $m.Repl)
    if ($m.ContainsKey('Find2')) { # optional second edit in the same file (a MOVE = delete here + insert there)
        if ((Count $t2[$m.Key] $m.Find2) -ne 1) { Write-Host "MUTANT-SETUP FAIL $($m.Name): second find text not unique"; $bad++; continue }
        $t2[$m.Key] = $t2[$m.Key].Replace($m.Find2, $m.Repl2)
    }
    $c2[$m.Key] = Code-Only $t2[$m.Key]
    $r = Invoke-McpChecks $t2 $c2
    $hits = @($r | Where-Object { $_.StartsWith("($($m.Expect))") })
    if ($hits.Count -gt 0) { Write-Host "RED  (ok) $($m.Name)" }
    else { Write-Host "GREEN (BAD) $($m.Name): expected $($m.Expect), got: $($r -join ' | ')"; $bad++ }
}
# E10 (SELF-AUTHORED, coder, after the regrade-1 cold review; not reviewer text): a golden read row with no $readSites entry must
# go red. The mutation is to THIS lint's own table (no source edit can reach it), so it runs outside the source-mutant loop: drop the
# DumpWells read-site pin, run the checks on the unmutated tree, require an (E) hit, then restore the pin.
$lintMutants = 1
$savedSites = $readSites[[string]('NodeShuffle.' + 'DumpWells')]
$readSites.Remove('NodeShuffle.' + 'DumpWells')
$r = Invoke-McpChecks $text $code
$readSites['NodeShuffle.' + 'DumpWells'] = $savedSites
if (@($r | Where-Object { $_.StartsWith('(E)') -and $_.Contains('has no source pin') }).Count -gt 0) { Write-Host 'RED  (ok) E10 golden read row loses its source pin (self-authored, lint-table mutant)' }
else { Write-Host "GREEN (BAD) E10 golden read row loses its source pin: got: $($r -join ' | ')"; $bad++ }
if ($bad -gt 0) { Write-Host "FAIL: $bad mutant(s) not caught"; exit 1 }
Write-Host "PASS: MCPCONTRACT lint -- M1..M5, R, E, C1..C4, F, L hold across $($text.Count) loaded files; mutants caught $($mutants.Count + $lintMutants)/$($mutants.Count + $lintMutants)."
exit 0
