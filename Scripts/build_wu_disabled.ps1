# Builds a mod with WeaponUpgrades' .uplugin disabled for the duration of the build.
#
# WHY: PackagePlugin compiles ALL mods discovered in the starter project's Mods\ tree, and
# WeaponUpgrades' AccessTransformer friend trips a UHT "unused friend access transformer"
# false-flag -> C2248 across the shared target (memory: weaponupgrades-uht-unused-friend).
# The satisfactory-mod playbook requires this wrapper to live COMMITTED in the repo rather
# than be re-created per session. Restore happens in `finally`, so a failed build cannot
# strand WeaponUpgrades disabled.
param(
    [string]$ModName = "NodeShuffle",
    [string]$BuildScript = "C:\Users\mello\.claude\skills\satisfactory-modding\scripts\build_mod.ps1",
    [string]$WuUplugin = "C:\Claude\Projects\SatisfactoryModLoader\Mods\WeaponUpgrades\WeaponUpgrades.uplugin"
)
$ErrorActionPreference = "Stop"
$exit = 1
$renamed = $false

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
$lockFile = "C:\Claude\Projects\_team\BUILD-LOCK.txt"
if (Test-Path $lockFile) {
    $lockText = Get-Content $lockFile -Raw
    if ($lockText -match 'PID (\d+)') {
        $lockPid = [int]$Matches[1]
        if (-not (Get-Process -Id $lockPid -ErrorAction SilentlyContinue)) {
            [System.IO.File]::Delete($lockFile)
            Write-Host "SELF-HEAL: cleared stale build lock (owner PID $lockPid is dead)"
        }
    }
}

try {
    if (Test-Path $WuUplugin) {
        Rename-Item $WuUplugin "WeaponUpgrades.uplugin.disabled_for_build"
        $renamed = $true
        Write-Host "WeaponUpgrades.uplugin disabled for this build"
    }
    & powershell -File $BuildScript -ModName $ModName
    $exit = $LASTEXITCODE
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
exit $exit
