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
