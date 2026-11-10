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
#   C4 MARKERS: every NodeShuffle.cpp "packet marker (" line and the LOADED tag appear in NodeShuffleBuildMarkers.h, same order, and this
#      packet's marker is the newest; McpDescribe's buildMarker reads the header's newest entry, never a date literal
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
$capLoop      = 'if (Utf8Bytes(Out) <= MaxOutputBytes) ' + '{ break; }'
$capDesc      = 'return Utf8Bytes(Out) <= MaxOutputBytes ' + '? Out : ErrorJson('
$utf8Pin      = 'const FTCHARToUTF8 Utf8(*Text, ' + 'Text.Len());'
$logVerbose   = 'UE_LOG(LogNodeShuffle, ' + 'Verbose, '
$newestExpr   = 'PacketMarkers[' + 'NodeShuffleBuildMarkers::PacketMarkerCount - 1]'
$forbidden = @(
    ('Load' + 'Object'), ('Load' + 'Class'), ('Static' + 'Load'), ('Try' + 'Load'), ('Load' + 'Synchronous'), ('Load' + 'Package'),
    ('Line' + 'Trace'), ('Sweep' + 'Single'), ('Sweep' + 'Multi'), ('Overlap' + 'Multi'), ('Overlap' + 'Any'), ('Spawn' + 'Actor'),
    ('const' + '_cast'), ('mut' + 'able'), ('GetMap' + 'Mode('), ('GetActive' + 'Config('), ('Save' + 'Config'), ('Mark' + 'Dirty'),
    ('Flush' + 'PendingSaves'), ('ClearReroll' + 'NowFlag'), ('this' + '->'), ('Get' + 'MutableDefault')
)
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
    foreach ($k in @($pin.desc, $pin.snap, $pin.com)) {
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
    if (-not (Ordered $snapC @('FString ANodeShuffleSubsystem::McpSnapshot(', $trampoline, $logVerbose, 'return Out;'))) { $f += '(M4) the snapshot log line must follow the reader call inside McpSnapshot' }
    if (-not (Ordered $Cod[$pin.desc] @('FString ANodeShuffleSubsystem::McpDescribe() const', 'FNodeShuffleMcpReader::Describe();', $logVerbose))) { $f += '(M4) the describe log line must sit in McpDescribe' }
    # ---- M5
    if ((Count $Txt[$pin.com] $capDecl) -ne 1) { $f += '(M5) the 64 KB cap constant is missing or changed' }
    if ((Count $snapC $capLoop) -ne 2 -or (Count $snapC '--Keep;') -ne 2) { $f += '(M5) near and summary must each re-serialize and drop the tail until under the cap' }
    if ((Count $Txt[$pin.snap] $utf8Pin) -ne 1) { $f += '(M5) the cap must be measured in UTF-8 bytes' }
    if ((Count $Txt[$pin.desc] $capDesc) -ne 1) { $f += '(M5) the manifest must be checked against the cap' }
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
    $own = '2026-11-09-' + 'mcp-contract-1'
    if ($hdrTags.Count -eq 0 -or $hdrTags[-1] -ne $own -or (Count $nsm $own) -ne 1) { $f += '(C4) this packet marker must be the newest, once, on its own line' }
    if ((Count $Txt[$pin.desc] $newestExpr) -ne 1 -or [regex]::IsMatch($Txt[$pin.desc], 'TEXT\("20\d\d-\d\d-\d\d-')) { $f += '(C4) buildMarker must come from the markers header (newest entry), never a literal in Describe' }
    return ,$f
}

$base = Invoke-McpChecks $text $code
if ($base.Count -gt 0) { $base | ForEach-Object { Write-Host "FAIL$_" }; Write-Host "MCPCONTRACT lint: FAIL -- baseline $($base.Count) problem(s)"; exit 1 }
Write-Host "BASELINE GREEN: M1..M5, C1..C4 hold across $($text.Count) loaded files"

$H = $pin.hdr; $D = $pin.desc; $S = $pin.snap; $CMN = $pin.com; $MK = $pin.mark; $NS = $pin.nsmod; $CF = $pin.cfg; $SG = $pin.sg
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
    @{ Name = 'N19 near stops re-serializing under the cap'; Key = $S; Expect = 'M5'; Find = ($capLoop + "`n        if (Keep == 0) { OutBranch += TEXT("" error: over cap with no entries"")"); Repl = ('break;' + "`n        if (Keep == 0) { OutBranch += TEXT("" error: over cap with no entries"")") }
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
    @{ Name = 'N34 buildMarker hard-coded in Describe'; Key = $D; Expect = 'C4'; Find = ('NodeShuffleBuildMarkers::' + $newestExpr); Repl = ('TEXT("' + '2026-11-09-' + 'mcp-contract-1' + '")') }
    @{ Name = 'N33 the uplugin gains an MCP plugin dependency'; Key = '..\NodeShuffle.uplugin'; Expect = 'M2'; Find = '"Plugins": ['; Repl = ('"Plugins": [ { "Name": "' + $mcpModName + '" },') }
)
$bad = 0
foreach ($m in $mutants) {
    if (-not $text.Contains($m.Key)) { Write-Host "MUTANT-SETUP FAIL $($m.Name): no loaded file $($m.Key)"; $bad++; continue }
    if ((Count $text[$m.Key] $m.Find) -ne 1) { Write-Host "MUTANT-SETUP FAIL $($m.Name): find text not unique in $($m.Key)"; $bad++; continue }
    $t2 = [ordered]@{}; foreach ($k in $text.Keys) { $t2[$k] = $text[$k] }
    $c2 = [ordered]@{}; foreach ($k in $code.Keys) { $c2[$k] = $code[$k] }
    $t2[$m.Key] = $t2[$m.Key].Replace($m.Find, $m.Repl)
    $c2[$m.Key] = Code-Only $t2[$m.Key]
    $r = Invoke-McpChecks $t2 $c2
    $hits = @($r | Where-Object { $_.StartsWith("($($m.Expect))") })
    if ($hits.Count -gt 0) { Write-Host "RED  (ok) $($m.Name)" }
    else { Write-Host "GREEN (BAD) $($m.Name): expected $($m.Expect), got: $($r -join ' | ')"; $bad++ }
}
if ($bad -gt 0) { Write-Host "FAIL: $bad mutant(s) not caught"; exit 1 }
Write-Host "PASS: MCPCONTRACT lint -- M1..M5, C1..C4 hold across $($text.Count) loaded files; mutants caught $($mutants.Count)/$($mutants.Count)."
exit 0
