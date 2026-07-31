# Captures the DEPLOYED NodeShuffle DLL's import table as a sorted symbol SET.
#
# WHY THIS EXISTS, alongside check_imports.ps1 rather than instead of it. They answer different
# questions and only one of them is about safety:
#
#   check_imports.ps1  -- "does every import RESOLVE?" That is the load-killer question (an unresolved
#                         import 127-kills the module at boot) and it must keep gating every build.
#                         But across packets it can only be compared by COUNT, and a count is not a
#                         set: one symbol added and one dropped reads as "delta zero".
#   this script         -- "WHICH symbols?" Storing the set makes the next packet's delta a real diff,
#                         so every new engine entry point has to be named rather than inferred from an
#                         unchanged total.
#
# The standing lesson on this machine is that the import table is MEASURED, never predicted -- the
# inference has been falsified twice (2026-07-26 "virtual => no import surface"; Packet H0's
# AFGResourceNodeFrackingSatellite::GetCore, which has an INLINE BODY in the header and is imported
# anyway). This file is what turns "we measured it" into something the NEXT packet can check.
#
# Usage (after a build, with the mod DEPLOYED):
#   pwsh -File tools\capture-imports-baseline.ps1                       # rewrite the baseline
#   pwsh -File tools\capture-imports-baseline.ps1 -OutFile $env:TEMP\imports-now.txt
#   git diff --no-index tools\imports-baseline.txt $env:TEMP\imports-now.txt
#
# -Marker should name the build marker in NodeShuffle.cpp that the capture was taken at, so the
# baseline is always traceable to a specific DLL.

param(
    [string]$ModName = "NodeShuffle",
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\Satisfactory",
    [string]$OutFile = "$PSScriptRoot\imports-baseline.txt",
    [string]$Marker  = "(unspecified -- pass -Marker with the value in NodeShuffle.cpp)"
)

$ErrorActionPreference = "Stop"

$dumpbin = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $dumpbin) { Write-Error "dumpbin.exe not found under Visual Studio tools."; exit 2 }

$modDll = "$GameDir\FactoryGame\Mods\$ModName\Binaries\Win64\FactoryGameSteam-$ModName-Win64-Shipping.dll"
if (-not (Test-Path $modDll)) { Write-Error "Mod DLL not found (build + deploy first): $modDll"; exit 2 }

# Same two parse patterns check_imports.ps1 uses, deliberately -- if the two scripts ever disagree
# about what counts as an import row, their numbers stop being comparable and this file loses its point.
$rows = New-Object System.Collections.Generic.List[string]
$currentDll = $null
foreach ($line in (& $dumpbin /nologo /imports $modDll)) {
    if ($line -match '^\s+(\S+\.dll)\s*$') { $currentDll = $Matches[1] }
    elseif ($currentDll -and $line -match '^\s+[0-9A-Fa-f]+\s+(\?\S+|\w[\w@]+)\s*$') { $rows.Add("$currentDll`t$($Matches[1])") }
}
if ($rows.Count -eq 0) { Write-Error "Parsed 0 imports - dumpbin output format may have changed."; exit 1 }

$sorted = $rows | Sort-Object
$perDll = $sorted | ForEach-Object { $_.Split("`t")[0] } | Group-Object | Sort-Object Name
# The subtotal check_imports.ps1 actually verifies. Reported separately so the two tools' numbers can be
# reconciled at a glance instead of looking like a discrepancy.
$gameCount = @($sorted | Where-Object { $_ -like 'FactoryGameSteam-*' }).Count

$header = @(
    "# $ModName -- IMPORT SYMBOL BASELINE (generated; do not hand-edit)",
    "#",
    "# Regenerate / diff: see tools\capture-imports-baseline.ps1, which also explains why this exists",
    "# alongside check_imports.ps1 (that one asks 'does every import RESOLVE?'; this one records WHICH).",
    "#",
    "# CAPTURED AT MARKER : $Marker",
    "# CAPTURED ON        : $(Get-Date -Format 'yyyy-MM-dd')",
    "# SOURCE DLL         : $modDll",
    "#",
    "# TOTAL IMPORTS                  : $($sorted.Count)",
    "# ... of which FactoryGameSteam-* : $gameCount   <-- THIS is the number check_imports.ps1 reports.",
    "#     The remainder are CRT / KERNEL32 imports, which check_imports.ps1 does not verify (they are",
    "#     guaranteed by the platform, not by the game build) but which are recorded here anyway: a new",
    "#     one is still a new entry point and should be noticed rather than silently absorbed.",
    "#",
    "# PER-MODULE COUNTS:"
) + ($perDll | ForEach-Object { "#   {0,-46} {1,4}" -f $_.Name, $_.Count }) + @(
    "#",
    "# FORMAT: <source dll><TAB><decorated symbol>, sorted. Everything below this line is data.",
    "# -----------------------------------------------------------------------------------------"
)

Set-Content -Path $OutFile -Value ($header + $sorted) -Encoding utf8
Write-Host ("Wrote {0}" -f $OutFile)
Write-Host ("  {0} symbols total, {1} from FactoryGameSteam-* modules (marker: {2})" -f $sorted.Count, $gameCount, $Marker)
