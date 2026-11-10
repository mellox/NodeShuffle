<#
check_reflection_thunks.ps1 -- does a built NodeShuffle DLL export the UHT-generated
native thunk for every UFUNCTION declared on an exported class? (packet ns-uht-guard-1,
2026-11-10; ported from PrimalFauna's tools\check_reflection_thunks.ps1, not called at runtime)

Background: UBT skips the header tool when every reflected header looks older than the
module's UHT Timestamp. A new UFUNCTION then compiles as a plain C++ method, the build
reports success, and reflection cannot find it (shipped 2026-11-09 20:51, DLL A25DF22E).
The plain method's export is NOT evidence; the thunk is. UHT emits one static thunk per
native UFUNCTION, and on a class declared with the module export macro the shipping DLL's
export table carries its decorated name: '?' + 'exec' + Fn + '@' + Class + '@@' ...

EXPECTED LIST COMES FROM THE SOURCE HEADERS, never from generated files: a stale
generated file would list the same stale set the DLL was built from and pass vacuously.
Per module folder under -SourceRoot, the export macro is <FOLDERNAME upper-cased>_API.
For each class declared with that macro, every UFUNCTION declared in it (except
BlueprintImplementableEvent, which has no native thunk) expects one class-qualified thunk.

SCOPE: only exported classes are visible to this probe (the thunk reaches the export table
only through the macro). Every UFUNCTION on a non-exported class is printed as a
NOT-PROVABLE line -- never checked, never passed.

Failure cases: any expected thunk missing; dumpbin missing or nonzero exit; the export
table parse yields no rows; a class with expected thunks has no export at all in the DLL
(the probe could not see the class); the main module expects zero thunks (a derivation
regression must not shrink the gate to nothing). A non-main module expecting zero is
reported as a fact. A non-main module expecting more than zero must be given its own DLL
with -ModuleDll Module=path, or it fails (it is never checked against the main DLL).

Runs under Windows PowerShell 5.1 and PowerShell 7. ASCII only.
Output: NSTHUNK lines. Exit 0 = every expected thunk present; 1 = any failure.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Dll,
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    [string]$MainModule = 'NodeShuffle',
    [string[]]$ModuleDll = @(),
    [string]$Dumpbin = ''
)
$ErrorActionPreference = 'Stop'
$fails = New-Object System.Collections.Generic.List[string]
function Out-Line([string]$s) { Write-Output $s }

# ---- derive expected thunks from headers -------------------------------------
$expected = @{}      # module -> list of 'Class::Fn'
$notProvable = New-Object System.Collections.Generic.List[string]
if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) {
    $fails.Add("NSTHUNK FAIL derive: source root '$SourceRoot' does not exist")
} else {
    $macro = 'UFUNC' + 'TION('
    foreach ($modDir in @(Get-ChildItem -LiteralPath $SourceRoot -Directory)) {
        $mod = $modDir.Name
        $api = $mod.ToUpperInvariant() + '_API'
        $list = New-Object System.Collections.Generic.List[string]
        $headers = @(Get-ChildItem -LiteralPath $modDir.FullName -Recurse -File -Filter '*.h' | Where-Object { $_.Extension -eq '.h' })
        $skipped = 0
        foreach ($f in $headers) {
            $t = [System.IO.File]::ReadAllText($f.FullName) -replace "`r`n", "`n"
            $t = [regex]::Replace($t, '/\*.*?\*/', ' ', 'Singleline')
            $t = [regex]::Replace($t, '//[^\n]*', '')
            foreach ($m in [regex]::Matches($t, '(?m)^[ \t]*' + [regex]::Escape($macro))) {
                $i = $m.Index + $m.Length; $depth = 1
                while ($i -lt $t.Length -and $depth -gt 0) {
                    if ($t[$i] -eq '(') { $depth++ } elseif ($t[$i] -eq ')') { $depth-- }
                    $i++
                }
                if ($depth -ne 0) { $fails.Add("NSTHUNK FAIL derive: unbalanced specifier list in $($f.Name)"); continue }
                $spec = $t.Substring($m.Index + $m.Length, $i - 1 - ($m.Index + $m.Length))
                if ($spec -match '\bBlueprintImplementableEvent\b') { $skipped++; continue }
                $rest = $t.Substring($i)
                $p = $rest.IndexOf('(')
                if ($p -lt 0) { $fails.Add("NSTHUNK FAIL derive: no declaration after a UFUNCTION in $($f.Name)"); continue }
                $id = [regex]::Match($rest.Substring(0, $p), '([A-Za-z_][A-Za-z0-9_]*)\s*$')
                if (-not $id.Success) { $fails.Add("NSTHUNK FAIL derive: no function name after a UFUNCTION in $($f.Name)"); continue }
                $decls = [regex]::Matches($t.Substring(0, $m.Index), '(?m)^[ \t]*(?:class|struct)[ \t]+(' + $api + '[ \t]+)?([A-Za-z_][A-Za-z0-9_]*)[^;\n]*$')
                if ($decls.Count -eq 0) { $fails.Add("NSTHUNK FAIL derive: no owning class for $($id.Groups[1].Value) in $($f.Name)"); continue }
                $owner = $decls[$decls.Count - 1]
                $qual = $owner.Groups[2].Value + '::' + $id.Groups[1].Value
                if ($owner.Groups[1].Success) { $list.Add($qual) } else { $notProvable.Add("$mod $qual") }
            }
        }
        $expected[$mod] = $list
        Out-Line ("NSTHUNK derive module={0} macro={1} headers={2} expectedThunks={3} skippedImplementableEvent={4}" -f $mod, $api, $headers.Count, $list.Count, $skipped)
    }
    if (-not $expected.ContainsKey($MainModule)) { $fails.Add("NSTHUNK FAIL derive: main module folder '$MainModule' not found under $SourceRoot") }
}
foreach ($u in $notProvable) {
    Out-Line "NSTHUNK NOT-PROVABLE $u -- declared on a class without the module export macro; this probe cannot see its thunk (not checked, not passed)"
}
Out-Line 'NSTHUNK SCOPE only UFUNCTIONs on classes declared with the module export macro are visible to this probe; non-exported classes are outside what it can see'

# ---- dumpbin -------------------------------------------------------------------
if (-not $Dumpbin) {
    $cand = @(Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe' -ErrorAction SilentlyContinue | Sort-Object FullName -Descending)
    if ($cand.Count -gt 0) { $Dumpbin = $cand[0].FullName }
}
if (-not $Dumpbin -or -not (Test-Path -LiteralPath $Dumpbin -PathType Leaf)) { $fails.Add('NSTHUNK FAIL dumpbin.exe not found -- no verdict possible') }

function Get-ExportNames([string]$Path) {
    $names = New-Object System.Collections.Generic.List[string]
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { $fails.Add("NSTHUNK FAIL DLL not found: $Path"); return ,$names }
    if (-not $Dumpbin -or -not (Test-Path -LiteralPath $Dumpbin -PathType Leaf)) { return ,$names }
    $global:LASTEXITCODE = $null
    $raw = @(& $Dumpbin /nologo /exports $Path)
    $rc = $LASTEXITCODE
    if ($null -eq $rc -or $rc -ne 0) { $fails.Add("NSTHUNK FAIL dumpbin exit '$rc' on $Path -- no verdict possible"); return ,$names }
    foreach ($line in $raw) {
        $mm = [regex]::Match("$line", '^\s+\d+\s+[0-9A-Fa-f]+\s+(?:[0-9A-Fa-f]{8}\s+)?(\S+)')
        if ($mm.Success) { $names.Add($mm.Groups[1].Value) }
    }
    $script:DllLine = ("NSTHUNK dll={0} bytes={1} exportRows={2}" -f $Path, (Get-Item -LiteralPath $Path).Length, $names.Count)
    if ($names.Count -eq 0) { $fails.Add("NSTHUNK FAIL export table parse saw no rows in $Path -- the probe saw nothing, no verdict") }
    return ,$names
}

$dllFor = @{}
$dllFor[$MainModule] = $Dll
foreach ($pair in @($ModuleDll | ForEach-Object { $_ -split ',' } | Where-Object { $_ })) {
    $kv = $pair -split '=', 2
    if ($kv.Count -eq 2) { $dllFor[$kv[0].Trim()] = $kv[1].Trim() }
}

$totalExpected = 0; $totalFound = 0
foreach ($mod in @($expected.Keys | Sort-Object)) {
    $list = $expected[$mod]
    if ($list.Count -eq 0) {
        if ($mod -eq $MainModule) {
            $fails.Add("NSTHUNK FAIL main module $mod expects zero thunks from its headers -- an empty list is never a pass")
        } else {
            Out-Line "NSTHUNK MODULE $mod expected=0 -- no UFUNCTION on an exported class in this module; nothing to check here (a stated fact, not a failure)"
        }
        continue
    }
    if (-not $dllFor.ContainsKey($mod)) {
        $fails.Add("NSTHUNK FAIL module $mod expects $($list.Count) thunk(s) but no DLL was supplied for it (-ModuleDll $mod=path); not checked against the main DLL")
        continue
    }
    $script:DllLine = $null
    $exports = Get-ExportNames $dllFor[$mod]
    if ($script:DllLine) { Out-Line $script:DllLine }
    $found = 0
    $classSeen = @{}
    foreach ($q in $list) {
        $cls, $fn = $q -split '::', 2
        if (-not $classSeen.ContainsKey($cls)) {
            $classTag = '@' + $cls + '@@'
            $classSeen[$cls] = @($exports | Where-Object { $_.Contains($classTag) }).Count
            Out-Line ("NSTHUNK class {0} exportsQualifiedByClass={1}" -f $cls, $classSeen[$cls])
            if ($classSeen[$cls] -eq 0) { $fails.Add("NSTHUNK FAIL class $cls has no export at all in $($dllFor[$mod]) -- the probe cannot see this class, no verdict on its thunks") }
        }
        $needle = '?' + 'ex' + 'ec' + $fn + '@' + $cls + '@@'
        $plain  = '?' + $fn + '@' + $cls + '@@'
        $k = @($exports | Where-Object { $_.StartsWith($needle, [System.StringComparison]::Ordinal) }).Count
        $kp = @($exports | Where-Object { $_.StartsWith($plain, [System.StringComparison]::Ordinal) }).Count
        Out-Line ("NSTHUNK function {0} thunkExports={1} plainMethodExports={2}" -f $q, $k, $kp)
        if ($k -ge 1) { $found++ } else { $fails.Add("NSTHUNK FAIL MISSING $q -- no class-qualified native-thunk export in $($dllFor[$mod])") }
    }
    $totalExpected += $list.Count; $totalFound += $found
    Out-Line ("NSTHUNK MODULE {0} expected={1} found={2}" -f $mod, $list.Count, $found)
}
Out-Line ("NSTHUNK SUMMARY expected={0} found={1} failures={2}" -f $totalExpected, $totalFound, $fails.Count)
foreach ($x in $fails) { Out-Line $x }
if ($fails.Count -eq 0) { exit 0 } else { exit 1 }
