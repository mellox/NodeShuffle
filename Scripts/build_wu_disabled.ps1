# Builds a mod with WeaponUpgrades' .uplugin disabled for the duration of the build.
#
# WHY: PackagePlugin compiles ALL mods discovered in the starter project's Mods\ tree, and
# WeaponUpgrades' AccessTransformer friend trips a UHT "unused friend access transformer"
# false-flag -> C2248 across the shared target (memory: weaponupgrades-uht-unused-friend).
# The satisfactory-mod playbook requires this wrapper to live COMMITTED in the repo rather
# than be re-created per session. Restore happens in `finally`, so a failed build cannot
# strand WeaponUpgrades disabled.
#
# UHT GUARD (packet ns-uht-guard-1, 2026-11-10). On 2026-11-09 20:51 a build of this wrapper
# shipped a DLL in which two new UFUNCTIONs had no reflection: UBT runs the header tool for a
# module only when a reflected header is newer than the module's UHT Timestamp (or that file
# is missing), every build of a target re-stamps every module, and the robocopy sync inside
# build_mod.ps1 preserves repo mtimes. So, BEFORE the child build:
#   - refuse (exit 4, touching nothing) if the build lock exists or the game is running;
#   - set LastWriteTime = now on every repo Source\ header containing the GENERATED_BODY
#     macro (mtime only; content and git status unchanged), so the synced copies are newer
#     than any stamp. The stamp delete was NOT chosen: it would happen before the sync and
#     before at_grants_check's pre gate, which reads stamp presence (uht_rerun_reasons).
# AFTER a child exit 0 (the DLL is ALREADY deployed by then), two gates:
#   a. the alpakit log was written by this run and carries both header-tool lines,
#      anchored at line start;
#   b. tools\check_reflection_thunks.ps1 finds every thunk derived from the source headers
#      in the deployed main DLL.
# A failing gate does not restore anything; it exits with its own code (table below) and
# says the deployed DLL must be treated as defective.
#
# EXIT CODES. Own: 4 refused (lock present or game running, nothing touched); 20 the child
# returned no exit code; 21 header-tool evidence gate failed; 22 reflection-thunk gate failed;
# 23 both gates failed; 25 the lever found no header to touch. Any other value is passed
# through from build_mod.ps1 (1 build failed, 2 game running, 3 sync, 4 lock/UBT busy,
# 16 AT pre gate refused, 17 deployed but AT post gate failed). Gates run only after child 0.
#
# Test seams: -LockFile is used ONLY for this wrapper's own self-heal and refusal; it is never
# passed to build_mod.ps1, which always uses the real lock. -EvidenceSelfTest <log> runs only
# the header-tool evidence content check on that file and exits (0 pass / 21 fail) before any
# self-heal, touch or build.
param(
    [string]$ModName = "NodeShuffle",
    [string]$BuildScript = "C:\Users\mello\.claude\skills\satisfactory-modding\scripts\build_mod.ps1",
    [string]$WuUplugin = "C:\Claude\Projects\SatisfactoryModLoader\Mods\WeaponUpgrades\WeaponUpgrades.uplugin",
    [string]$LockFile = "C:\Claude\Projects\_team\BUILD-LOCK.txt",
    [string]$AlpakitLog = (Join-Path $env:TEMP ('alpakit_' + $ModName + '.log')),
    [string]$DeployedDir = "C:\Program Files (x86)\Steam\steamapps\common\Satisfactory\FactoryGame\Mods\NodeShuffle\Binaries\Win64",
    [string]$EvidenceSelfTest = ""
)
$ErrorActionPreference = "Stop"
$wrapperStart = Get-Date
$exit = 1
$renamed = $false
$repoRoot = Split-Path -Parent $PSScriptRoot
$UhtTarget = 'FactoryGame' + 'Steam'

# Header-tool evidence: both phrases must START a line (optional leading whitespace or one
# [bracketed] prefix), so a mention inside other text does not count.
function Get-UhtEvidence([string[]]$Lines) {
    $pre = '^\s*(\[[^\]]*\]\s*)?'
    $r = @{ Parsing = $null; Reflection = $null; Total = $null }
    $pParse = $pre + [regex]::Escape('Parsing headers for ' + $UhtTarget) + '\b'
    $pRefl  = $pre + [regex]::Escape('Reflection code generated for ' + $UhtTarget) + '\b'
    $pTotal = $pre + 'Total of \d+ written\b'
    foreach ($l in $Lines) {
        $s = "$l"
        if (-not $r.Parsing    -and $s -match $pParse) { $r.Parsing = $s.Trim() }
        if (-not $r.Reflection -and $s -match $pRefl)  { $r.Reflection = $s.Trim() }
        if (-not $r.Total      -and $s -match $pTotal) { $r.Total = $s.Trim() }
    }
    return $r
}
function Write-UhtEvidence($ev) {
    foreach ($k in @('Parsing', 'Reflection', 'Total')) {
        if ($ev[$k]) { Write-Host ("NSBUILD UHT-LOG {0} line found: {1}" -f $k, $ev[$k]) }
        else { Write-Host ("NSBUILD UHT-LOG {0} line not found" -f $k) }
    }
}

if ($EvidenceSelfTest) {
    if (-not (Test-Path -LiteralPath $EvidenceSelfTest -PathType Leaf)) { Write-Host "NSBUILD EVIDENCE-SELFTEST file not found: $EvidenceSelfTest"; exit 21 }
    $ev = Get-UhtEvidence @(Get-Content -LiteralPath $EvidenceSelfTest -Encoding UTF8)
    Write-UhtEvidence $ev
    if ($ev.Parsing -and $ev.Reflection) { Write-Host "NSBUILD EVIDENCE-SELFTEST content PASS"; exit 0 }
    Write-Host "NSBUILD EVIDENCE-SELFTEST content FAIL"; exit 21
}

# Self-heal (retro-approved 2026-08-11, ledger x3): a KILLED build (tool timeout, TaskStop) never
# runs the finally below, stranding two things a FAILED build cannot strand. Heal both at START:
# 1. A leftover .disabled_for_build from a killed prior run (WeaponUpgrades stays disabled for every
#    later session until someone notices -- happened twice in one hour on 2026-08-10).
$strandedWu = "$WuUplugin.disabled_for_build"
if ((Test-Path $strandedWu) -and -not (Test-Path $WuUplugin)) {
    Rename-Item $strandedWu (Split-Path $WuUplugin -Leaf)
    Write-Host "SELF-HEAL: restored stranded WeaponUpgrades.uplugin from a killed prior build"
}
# 2. A build lock whose owner PID is provably dead (never steal a live one -- the cross-session
#    serialization contract in _team/BUILD-COORDINATION.md depends on it).
if (Test-Path $LockFile) {
    $lockText = Get-Content $LockFile -Raw
    if ($lockText -match 'PID (\d+)') {
        $lockPid = [int]$Matches[1]
        if (-not (Get-Process -Id $lockPid -ErrorAction SilentlyContinue)) {
            [System.IO.File]::Delete($LockFile)
            Write-Host "SELF-HEAL: cleared stale build lock (owner PID $lockPid is dead)"
        }
    }
}

# Refusal BEFORE the lever: build_mod.ps1 would refuse anyway, and a refused build must not
# leave headers touched.
$lockPresent = Test-Path -LiteralPath $LockFile
$gameProcs = @(Get-Process -Name 'FactoryGameSteam-Win64-Shipping' -ErrorAction SilentlyContinue)
if ($lockPresent -or $gameProcs.Count -gt 0) {
    Write-Host ("NSBUILD REFUSE lockFilePresent={0} gameProcessCount={1} lockPath={2} -- no header touched, no build started" -f $lockPresent, $gameProcs.Count, $LockFile)
    exit 4
}

# LEVER: mtime-only touch of every reflected repo header.
$gbMacro = 'GENERATED_' + 'BODY'
$touchSet = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Source') -Recurse -File -Filter '*.h' |
              Where-Object { $_.Extension -eq '.h' -and [System.IO.File]::ReadAllText($_.FullName).Contains($gbMacro) })
if ($touchSet.Count -eq 0) {
    Write-Host "NSBUILD UHT-TOUCH headers=0 under $(Join-Path $repoRoot 'Source') -- nothing to touch; refusing to build without the lever"
    exit 25
}
$touchTime = Get-Date
foreach ($h in $touchSet) { [System.IO.File]::SetLastWriteTime($h.FullName, $touchTime) }
Write-Host ("NSBUILD UHT-TOUCH headers={0} setTo={1} names={2}" -f $touchSet.Count, $touchTime.ToString('s'), (($touchSet | ForEach-Object { $_.Name }) -join ','))

try {
    if (Test-Path $WuUplugin) {
        Rename-Item $WuUplugin "WeaponUpgrades.uplugin.disabled_for_build"
        $renamed = $true
        Write-Host "WeaponUpgrades.uplugin disabled for this build"
    }
    $global:LASTEXITCODE = $null
    & powershell -File $BuildScript -ModName $ModName
    $childExit = $LASTEXITCODE
    if ($null -eq $childExit -or "$childExit" -eq '') {
        Write-Host "NSBUILD CHILD-EXIT none -- the build script returned no exit code; treating as failure (exit 20)"
        $exit = 20
    } else {
        $exit = [int]$childExit
        Write-Host "NSBUILD CHILD-EXIT code=$exit"
    }
}
finally {
    if ($renamed) {
        $disabled = "$WuUplugin.disabled_for_build"
        if (Test-Path $disabled) {
            Rename-Item $disabled "WeaponUpgrades.uplugin"
            Write-Host "WeaponUpgrades.uplugin restored"
        }
    }
}

if ($exit -eq 0) {
    $evidenceOk = $false
    $thunkOk = $false
    $mainDll = Join-Path $DeployedDir ('FactoryGameSteam-' + $ModName + '-Win64-Shipping.dll')
    $vetoDll = Join-Path $DeployedDir ('FactoryGameSteam-' + $ModName + 'VetoKBFL-Win64-Shipping.dll')
    # Gate a: header-tool evidence from THIS run.
    try {
        if (-not (Test-Path -LiteralPath $AlpakitLog -PathType Leaf)) {
            Write-Host "NSBUILD GATE-A FAIL alpakit log not found: $AlpakitLog"
        } else {
            $logTime = (Get-Item -LiteralPath $AlpakitLog).LastWriteTime
            $fresh = $logTime -ge $wrapperStart
            Write-Host ("NSBUILD GATE-A log={0} logWritten={1} wrapperStart={2} writtenThisRun={3}" -f $AlpakitLog, $logTime.ToString('s'), $wrapperStart.ToString('s'), $fresh)
            $ev = Get-UhtEvidence @(Get-Content -LiteralPath $AlpakitLog -Encoding UTF8)
            Write-UhtEvidence $ev
            $evidenceOk = [bool]($fresh -and $ev.Parsing -and $ev.Reflection)
            Write-Host ("NSBUILD GATE-A {0}" -f $(if ($evidenceOk) { 'PASS' } else { 'FAIL' }))
        }
    } catch { Write-Host "NSBUILD GATE-A FAIL could not run: $($_.Exception.Message)" }
    # Gate b: reflection thunks derived from the source headers, in the deployed DLL.
    try {
        $thunkScript = Join-Path $repoRoot 'tools\check_reflection_thunks.ps1'
        $global:LASTEXITCODE = $null
        $thunkOut = @(& $thunkScript -Dll $mainDll -SourceRoot (Join-Path $repoRoot 'Source') -MainModule $ModName -ModuleDll ($ModName + 'VetoKBFL=' + $vetoDll))
        $thunkExit = $LASTEXITCODE
        $thunkOut | ForEach-Object { Write-Host "$_" }
        if ($null -eq $thunkExit -or "$thunkExit" -eq '') { Write-Host "NSBUILD GATE-B FAIL the thunk check returned no exit code" }
        elseif ([int]$thunkExit -ne 0) { Write-Host "NSBUILD GATE-B FAIL thunk check exit $thunkExit" }
        else { $thunkOk = $true; Write-Host "NSBUILD GATE-B PASS" }
    } catch { Write-Host "NSBUILD GATE-B FAIL could not run: $($_.Exception.Message)" }

    if (-not $evidenceOk -and -not $thunkOk) { $exit = 23 }
    elseif (-not $evidenceOk) { $exit = 21 }
    elseif (-not $thunkOk) { $exit = 22 }
    if ($exit -ne 0) {
        Write-Host ("NSBUILD DEFECTIVE-DEPLOY exit {0}: the build reported success and ALREADY DEPLOYED {1}, but a post-build reflection gate failed. Treat the DEPLOYED DLL as defective and restore the last reviewed park before anyone launches the game. This wrapper restores nothing." -f $exit, $mainDll)
    } else {
        Write-Host "NSBUILD GATES PASS header-tool evidence and reflection thunks"
    }
} else {
    Write-Host "NSBUILD GATES not run: child exit $exit (post-build gates run only after child exit 0)"
}
exit $exit
