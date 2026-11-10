# check_uhtguard_lint.ps1 -- pins for the UHT guard in Scripts\build_wu_disabled.ps1 and for
# tools\check_reflection_thunks.ps1 (packet ns-uht-guard-1, 2026-11-10).
#
# Pins (each reports as "(Pn) ..."):
#   P1 lock/game refusal precedes the header touch and exits 4.
#   P2 the header touch happens exactly once, before the child build call, keyed on the
#      generated-body macro; zero headers refuses (exit 25).
#   P3 the child call is preceded by a reset of the global exit code and a null/empty child
#      exit becomes exit 20.
#   P4 both header-tool phrases are required, anchored at line start, and the log must be
#      written by this run (behaviour: -EvidenceSelfTest on synthetic logs).
#   P5 the thunk script derives from SOURCE headers (never generated files), class-qualifies
#      its needle, fails on dumpbin failure, and fails when the main module expects zero
#      (behaviour: good park PASS, defective park FAIL naming both, zero-expected tree FAIL).
#   P6 the post-build gates run only inside the child-exit-0 branch, after the finally.
#   P7 the WeaponUpgrades restore stays in finally; the disable stays inside try.
#   P8 the thunk-gate call is preceded by a reset of the global exit code; null is a failure.
#   P9 gate failures map to 21/22/23 and print the defective-deploy line.
# Text pins read files with full-line '#' comments and block comments removed. Every
# searched-for token is built by CONCATENATION so this file cannot satisfy its own pins.
# Mutants run in-file against the loaded text; each must turn its named pin red.
# Exit 0 = all pins hold and every mutant is caught; 1 = a pin or mutant fails; 2 = LOAD.
param(
    [string]$Repo = (Split-Path -Parent $PSScriptRoot),
    [string]$GoodDll = 'C:\Claude\Projects\_archive\NodeShuffle-reviewed-binaries\2026-11-09-mcp-contract-1-uht\Binaries\Win64\FactoryGameSteam-NodeShuffle-Win64-Shipping.dll',
    [string]$BadDll  = 'C:\Claude\Projects\_archive\NodeShuffle-reviewed-binaries\2026-11-09-mcp-contract-1\Binaries\Win64\FactoryGameSteam-NodeShuffle-Win64-Shipping.dll'
)
$ErrorActionPreference = 'Stop'

function Strip([string]$raw) {
    $raw = $raw -replace "`r`n", "`n"
    $raw = [regex]::Replace($raw, '(?s)<#.*?#>', '')
    return ((($raw -split "`n") | ForEach-Object { if ($_ -match '^\s*#') { '' } else { $_ } }) -join "`n")
}
$paths = @{ W = (Join-Path $Repo 'Scripts\build_wu_disabled.ps1'); T = (Join-Path $Repo 'tools\check_reflection_thunks.ps1') }
$text = @{}
foreach ($k in @('W', 'T')) {
    if (-not (Test-Path -LiteralPath $paths[$k])) { Write-Host "UHTGUARD LOAD FAIL missing $($paths[$k])"; exit 2 }
    $raw = [System.IO.File]::ReadAllText($paths[$k])
    if ([string]::IsNullOrWhiteSpace($raw)) { Write-Host "UHTGUARD LOAD FAIL empty $($paths[$k])"; exit 2 }
    $text[$k] = Strip $raw
}
foreach ($p in @($GoodDll, $BadDll)) {
    if (-not (Test-Path -LiteralPath $p)) { Write-Host "UHTGUARD LOAD FAIL missing $p"; exit 2 }
}

# ---- fixtures for behaviour pins ----------------------------------------------------
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('uhtguard-lint-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $tmp | Out-Null
$ph1 = 'Parsing headers for ' + 'FactoryGame' + 'Steam'
$ph2 = 'Reflection code generated for ' + 'FactoryGame' + 'Steam' + ' in 2.1 seconds'
Set-Content -LiteralPath (Join-Path $tmp 'good.log') -Value @('x', $ph1, 'Total of 3 written', $ph2, 'y')
Set-Content -LiteralPath (Join-Path $tmp 'mid.log')  -Value @('x', ('note: ' + $ph1), ('see ' + $ph2), 'y')
Set-Content -LiteralPath (Join-Path $tmp 'one.log')  -Value @('x', $ph1, 'y')
$zeroSrc = Join-Path $tmp 'zeroSrc\NodeShuffle\Public'
New-Item -ItemType Directory -Force $zeroSrc | Out-Null
Set-Content -LiteralPath (Join-Path $zeroSrc 'X.h') -Value @('UCLASS()', ('class NODESHUFFLE' + '_API AX : public AActor'), '{', ('    GENERATED_' + 'BODY()'), '};')
# Fixed synthetic tree matching the parks' source (2 UFUNCTIONs), so a legitimate new
# UFUNCTION in the live Source cannot turn this lint red; live Source is the wrapper's job.
$fixSrc = Join-Path $tmp 'fixSrc\NodeShuffle\Public'
New-Item -ItemType Directory -Force $fixSrc | Out-Null
Set-Content -LiteralPath (Join-Path $fixSrc 'NodeShuffleSubsystem.h') -Value @('UCLASS()', ('class NODESHUFFLE' + '_API ANodeShuffleSubsystem : public AModSubsystem'), '{', ('    GENERATED_' + 'BODY()'), 'public:', ('    UFUNC' + 'TION() FString McpDescribe() const;'), ('    UFUNC' + 'TION() FString McpSnapshot(const FString& ArgsJson);'), '};')
$behaviourCache = @{}
$runN = 0
function Run-Script([string]$body, [string[]]$argv) {
    $script:runN++
    $f = Join-Path $tmp ("run{0}.ps1" -f $script:runN)
    [System.IO.File]::WriteAllText($f, $body)
    $global:LASTEXITCODE = $null
    $out = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $f @argv | ForEach-Object { "$_" })
    return @{ Code = $LASTEXITCODE; Out = $out }
}
function Behaviour-W([string]$W) {
    $key = 'W' + $W.GetHashCode() + $W.Length
    if ($behaviourCache.ContainsKey($key)) { return $behaviourCache[$key] }
    $r = New-Object System.Collections.Generic.List[string]
    $exp = @(@('good.log', 0), @('mid.log', 21), @('one.log', 21))
    foreach ($e in $exp) {
        $x = Run-Script $W @('-EvidenceSelfTest', (Join-Path $tmp $e[0]))
        if ($x.Code -ne $e[1]) { $r.Add("(P4) evidence self-test on $($e[0]) exited '$($x.Code)', expected $($e[1])") }
    }
    $behaviourCache[$key] = $r; return $r
}
function Behaviour-T([string]$T) {
    $key = 'T' + $T.GetHashCode() + $T.Length
    if ($behaviourCache.ContainsKey($key)) { return $behaviourCache[$key] }
    $r = New-Object System.Collections.Generic.List[string]
    $src = Join-Path $tmp 'fixSrc'
    $g = Run-Script $T @('-Dll', $GoodDll, '-SourceRoot', $src)
    if ($g.Code -ne 0) { $r.Add("(P5) good park DLL exited '$($g.Code)', expected 0") }
    $b = Run-Script $T @('-Dll', $BadDll, '-SourceRoot', $src)
    $miss = @($b.Out | Where-Object { $_ -match '^NSTHUNK FAIL MISSING ANodeShuffleSubsystem::(McpDescribe|McpSnapshot) ' }).Count
    if ($b.Code -ne 1 -or $miss -ne 2) { $r.Add("(P5) defective park DLL exited '$($b.Code)' with $miss MISSING lines, expected 1 and 2") }
    $z = Run-Script $T @('-Dll', $GoodDll, '-SourceRoot', (Join-Path $tmp 'zeroSrc'))
    if ($z.Code -ne 1) { $r.Add("(P5) zero-expected main module exited '$($z.Code)', expected 1") }
    $behaviourCache[$key] = $r; return $r
}

function Cnt([string]$s, [string]$n) { ([regex]::Matches($s, [regex]::Escape($n))).Count }
function Idx([string]$s, [string]$n) { $s.IndexOf($n, [System.StringComparison]::Ordinal) }
function RIdx([string]$s, [string]$pat) { $m = [regex]::Match($s, $pat); if ($m.Success) { $m.Index } else { -1 } }

function Invoke-Checks([string]$W, [string]$T) {
    $r = New-Object System.Collections.Generic.List[string]
    $touch    = '[System.IO.File]::SetLast' + 'WriteTime('
    $child    = '& powershell -File $Build' + 'Script -ModName $ModName'
    $refIf    = 'if ($lockPresent -or $gameProcs.Count -gt ' + '0) {'
    $iTouch = Idx $W $touch; $iChild = Idx $W $child; $iRefIf = Idx $W $refIf
    $iTry = RIdx $W '(?m)^try \{'; $iFin = RIdx $W '(?m)^finally \{'
    $iGateIf = RIdx $W ('(?m)^if \(\$exit -eq ' + '0\) \{')
    if ($iTry -lt 0 -or $iFin -lt 0) { $r.Add('(P7) top-level try/finally not found'); return $r }

    # P1
    $iLockTest = Idx $W ('$lockPresent = Test-Path -LiteralPath $Lock' + 'File')
    $iGameTest = Idx $W ("Get-Process -Name 'FactoryGameSteam-Win64-" + "Shipping'")
    if ($iRefIf -lt 0 -or $iLockTest -lt 0 -or $iGameTest -lt 0) { $r.Add('(P1) refusal condition (lock file AND game process) not found') }
    else {
        $blk = $W.Substring($iRefIf, (Idx $W.Substring($iRefIf) "`n}"))
        if (-not ([regex]::IsMatch($blk, '(?m)^\s*exit 4\s*$'))) { $r.Add('(P1) refusal block does not exit 4') }
        if (-not ($iLockTest -lt $iRefIf -and $iGameTest -lt $iRefIf)) { $r.Add('(P1) lock/game measurement must precede the refusal if') }
        if ($iTouch -ge 0 -and $iTouch -lt $iRefIf) { $r.Add('(P1) a header touch precedes the refusal') }
    }
    # P2
    if ((Cnt $W $touch) -ne 1) { $r.Add("(P2) expected exactly one header touch, found $(Cnt $W $touch)") }
    elseif (-not ($iTouch -lt $iTry -and $iTry -lt $iChild -and $iChild -lt $iFin)) { $r.Add('(P2) order must be touch < try < child call < finally') }
    if ((Cnt $W $child) -ne 1) { $r.Add("(P2) expected exactly one child build call, found $(Cnt $W $child)") }
    if ((Cnt $W ("`$gbMacro = 'GENERATED_' + " + "'BODY'")) -ne 1 -or (Cnt $W ('.Contains($gb' + 'Macro)')) -ne 1) { $r.Add('(P2) touch set must be keyed on the generated-body macro') }
    if (-not [regex]::IsMatch($W, '(?s)if \(\$touchSet\.Count -eq 0\) \{[^}]*exit 25')) { $r.Add('(P2) zero headers must refuse with exit 25') }
    # P3
    $reset = '$global:LASTEXIT' + 'CODE = $null'
    if ($iChild -gt $iTry) {
        $pre = $W.Substring($iTry, $iChild - $iTry + 12)
        if (-not [regex]::IsMatch($pre, [regex]::Escape($reset) + '\s*\n\s*' + [regex]::Escape($child.Substring(0, 12)))) { $r.Add('(P3) the global exit-code reset must sit immediately before the child call') }
        $post = $W.Substring($iChild, $iFin - $iChild)
        if (-not ($post.Contains('$childExit = $LASTEXIT' + 'CODE') -and $post.Contains('if ($null -eq $child' + 'Exit -or') -and [regex]::IsMatch($post, '\$exit = 2' + '0\b'))) { $r.Add('(P3) null/empty child exit must become exit 20') }
    } else { $r.Add('(P3) child call not inside try') }
    # P4
    foreach ($n in @(
        ("`$pre = '^\s*(\[[^\]]*\]\s*)?" + "'"),
        ("`$pParse = `$pre + [regex]::Escape('Parsing headers for ' + " + '$UhtTarget)'),
        ("`$pRefl  = `$pre + [regex]::Escape('Reflection code generated for ' + " + '$UhtTarget)'),
        ("`$UhtTarget = 'FactoryGame' + " + "'Steam'"),
        ('$fresh = $logTime -ge $wrapper' + 'Start'),
        ('$evidenceOk = [bool]($fresh -and $ev.Parsing -and $ev.' + 'Reflection)'))) {
        if ((Cnt $W $n) -ne 1) { $r.Add("(P4) missing or duplicated: $n") }
    }
    $iStart = Idx $W ('$wrapperStart = ' + 'Get-Date')
    if ($iStart -lt 0 -or ($iTouch -ge 0 -and $iStart -gt $iTouch)) { $r.Add('(P4) wrapper start time must be taken before the touch') }
    foreach ($x in (Behaviour-W $W)) { $r.Add($x) }
    # P5
    foreach ($bad in @(('.gen' + '.cpp'), ('.gen' + '.h'), ('generated' + '.h'))) {
        if ($T.IndexOf($bad, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) { $r.Add("(P5) thunk script code references a generated file ($bad)") }
    }
    foreach ($n in @(
        ("-Filter '*" + ".h'"),
        ("`$api = `$mod.ToUpperInvariant() + '_" + "API'"),
        ("`$needle = '?' + 'ex' + 'ec' + `$fn + '@' + `$cls + " + "'@@'"),
        ('if ($null -eq $rc -or $rc -ne ' + '0) { $fails.Add('),
        ('if ($mod -eq $Main' + 'Module) {'))) {
        if ((Cnt $T $n) -ne 1) { $r.Add("(P5) thunk script missing or duplicated: $n") }
    }
    foreach ($x in (Behaviour-T $T)) { $r.Add($x) }
    # P6
    if ($iGateIf -lt 0 -or $iGateIf -lt $iFin) { $r.Add('(P6) gates must sit in an if-child-exit-0 block after the finally') }
    else {
        $gate = $W.Substring($iGateIf)
        $iEv = Idx $gate ('Get-UhtEvidence @(Get-Content -LiteralPath $Alpakit' + 'Log')
        $thunkCall = '& $thunkScript -Dll $mainDll -SourceRoot (Join-Path $repoRoot ' + "'Source')"
        $iTh = Idx $gate $thunkCall
        $iElse = RIdx $gate '(?m)^\} else \{'
        if ($iEv -lt 0 -or $iTh -lt 0 -or $iElse -lt 0 -or $iEv -gt $iElse -or $iTh -gt $iElse -or (Cnt $W $thunkCall) -ne 1) { $r.Add('(P6) both gate calls must sit inside the exit-0 branch, once each') }
        # P8
        if ($iTh -ge 0) {
            if (-not [regex]::IsMatch($gate, [regex]::Escape($reset) + '\s*\n\s*\$thunkOut = @\(' + [regex]::Escape($thunkCall))) { $r.Add('(P8) the global exit-code reset must sit immediately before the thunk-gate call') }
            if (-not ($gate.Contains('$thunkExit = $LASTEXIT' + 'CODE') -and $gate.Contains('if ($null -eq $thunk' + 'Exit -or'))) { $r.Add('(P8) a null thunk-gate exit must be a failure') }
        }
        # P9
        foreach ($n in @(('$exit = 2' + '3 }'), ('$exit = 2' + '1 }'), ('$exit = 2' + '2 }'), ('NSBUILD DEFECTIVE' + '-DEPLOY'))) {
            if ((Cnt $gate $n) -ne 1) { $r.Add("(P9) gate section missing: $n") }
        }
    }
    # P7
    $finBody = $W.Substring($iFin, (Idx $W.Substring($iFin) "`n}") + 2)
    if (-not ($finBody.Contains('if ($ren' + 'amed) {') -and $finBody.Contains('Rename-Item $disabled "WeaponUpgrades' + '.uplugin"'))) { $r.Add('(P7) WeaponUpgrades restore is not in finally') }
    $iDis = Idx $W ('Rename-Item $WuUplugin "WeaponUpgrades.uplugin.disabled' + '_for_build"')
    if (-not ($iDis -gt $iTry -and $iDis -lt $iChild)) { $r.Add('(P7) the disable must be inside try, before the child call') }
    return $r
}

$exitCode = 0
try {
    $base = @(Invoke-Checks $text.W $text.T)
    if ($base.Count -gt 0) { foreach ($x in $base) { Write-Host "UHTGUARD BASELINE FAIL $x" }; $exitCode = 1 }
    else {
        Write-Host 'UHTGUARD BASELINE GREEN: P1..P9 hold'
        $mutants = @(
            @{ N = 'W1 refusal no longer exits'; K = 'W'; E = 'P1'; F = ('$gameProcs.Count, $LockFile)' + "`n    exit 4"); R = '$gameProcs.Count, $LockFile)' }
            @{ N = 'W2 a touch before the refusal'; K = 'W'; E = 'P1'; F = '$lockPresent = Test-Path -LiteralPath $LockFile'; R = ("[System.IO.File]::SetLast" + "WriteTime('x', (Get-Date))`n" + '$lockPresent = Test-Path -LiteralPath $LockFile') }
            @{ N = 'W3 game check dropped from the refusal'; K = 'W'; E = 'P1'; F = ('if ($lockPresent -or $gameProcs.Count -gt ' + '0) {'); R = 'if ($lockPresent) {' }
            @{ N = 'W4 touch moved after the child call'; K = 'W'; E = 'P2'; F = 'foreach ($h in $touchSet) { [System.IO.File]::SetLastWriteTime($h.FullName, $touchTime) }'; R = ''
               F2 = ('    $childExit = $LASTEXIT' + 'CODE'); R2 = ('    $childExit = $LASTEXIT' + 'CODE' + "`n    foreach (`$h in `$touchSet) { [System.IO.File]::SetLastWriteTime(`$h.FullName, `$touchTime) }") }
            @{ N = 'W5 exit-code reset before child removed'; K = 'W'; E = 'P3'; F = ('$global:LASTEXITCODE = $null' + "`n    & powershell"); R = '& powershell' }
            @{ N = 'W6 null child exit treated as success'; K = 'W'; E = 'P3'; F = '$exit = 20'; R = '$exit = 0' }
            @{ N = 'W7 parsing phrase unanchored'; K = 'W'; E = 'P4'; F = '$pParse = $pre + '; R = '$pParse = ' }
            @{ N = 'W8 gate needs only the parsing phrase'; K = 'W'; E = 'P4'; F = '($fresh -and $ev.Parsing -and $ev.Reflection)'; R = '($fresh -and $ev.Parsing)' }
            @{ N = 'W9 self-test needs only one phrase'; K = 'W'; E = 'P4'; F = 'if ($ev.Parsing -and $ev.Reflection) {'; R = 'if ($ev.Parsing) {' }
            @{ N = 'W10 log freshness dropped'; K = 'W'; E = 'P4'; F = '$fresh = $logTime -ge $wrapperStart'; R = '$fresh = $true' }
            @{ N = 'W11 gates run on any child exit'; K = 'W'; E = 'P6'; F = 'if ($exit -eq 0) {'; R = 'if ($true) {' }
            @{ N = 'W12 restore removed from finally'; K = 'W'; E = 'P7'; F = 'Rename-Item $disabled "WeaponUpgrades.uplugin"'; R = '' }
            @{ N = 'W13 exit-code reset before thunk gate removed'; K = 'W'; E = 'P8'; F = ('$global:LASTEXITCODE = $null' + "`n        `$thunkOut"); R = '$thunkOut' }
            @{ N = 'W14 thunk failure maps to success'; K = 'W'; E = 'P9'; F = '$exit = 22'; R = '$exit = 0' }
            @{ N = 'T1 derive from generated cpp'; K = 'T'; E = 'P5'; F = "-Filter '*.h' | Where-Object { `$_.Extension -eq '.h' }"; R = ("-Filter ('*.gen' + '.cpp')") }
            @{ N = 'T2 zero-expected main module passes'; K = 'T'; E = 'P5'; F = '            $fails.Add("NSTHUNK FAIL main module'; R = '            Out-Line ("NSTHUNK FAIL main module' }
            @{ N = 'T3 needle not class-qualified'; K = 'T'; E = 'P5'; F = "'ec' + `$fn + '@' + `$cls + '@@'"; R = "'ec' + `$fn + '@'" }
            @{ N = 'T4 dumpbin failure ignored'; K = 'T'; E = 'P5'; F = '$null -eq $rc -or $rc -ne 0'; R = '$false' }
        )
        $bad = 0
        foreach ($m in $mutants) {
            $t2 = @{ W = $text.W; T = $text.T }
            if ((Cnt $t2[$m.K] $m.F) -ne 1) { Write-Host "UHTGUARD MUTANT-SETUP FAIL $($m.N): find text not unique ($(Cnt $t2[$m.K] $m.F))"; $bad++; continue }
            $t2[$m.K] = $t2[$m.K].Replace($m.F, $m.R)
            if ($m.ContainsKey('F2')) {
                if ((Cnt $t2[$m.K] $m.F2) -ne 1) { Write-Host "UHTGUARD MUTANT-SETUP FAIL $($m.N): second find not unique"; $bad++; continue }
                $t2[$m.K] = $t2[$m.K].Replace($m.F2, $m.R2)
            }
            $res = @(Invoke-Checks $t2.W $t2.T)
            if (@($res | Where-Object { $_.StartsWith("($($m.E))") }).Count -gt 0) { Write-Host "UHTGUARD RED  (ok) $($m.N)" }
            else { Write-Host "UHTGUARD GREEN (BAD) $($m.N): expected $($m.E), got: $($res -join ' | ')"; $bad++ }
        }
        if ($bad -gt 0) { Write-Host "UHTGUARD FAIL: $bad of $($mutants.Count) mutant(s) not caught"; $exitCode = 1 }
        else { Write-Host "UHTGUARD PASS: P1..P9 hold; mutants caught $($mutants.Count)/$($mutants.Count)." }
    }
}
finally {
    if ($tmp -and (Test-Path -LiteralPath $tmp)) { [System.IO.Directory]::Delete($tmp, $true) }
}
exit $exitCode
