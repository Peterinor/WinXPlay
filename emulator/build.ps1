param([ValidateSet('all','emulator_smoke','hmi_loader_probe')][string]$Scenario='all')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../scripts/environment.ps1"
$taskRoot = (Resolve-Path "$PSScriptRoot/..").Path
. "$taskRoot/scripts/wince-toolchain.ps1"
if ($Scenario -in @('all','emulator_smoke')) {
    Build-WinceExe -Sources "$PSScriptRoot/emulator_smoke/main.cpp" -Entry SmokeEntry -Name CE_ARM_SMOKE -OutputDirectory "$taskRoot/build/emulator/emulator_smoke"
}
if ($Scenario -in @('all','hmi_loader_probe')) {
    Build-WinceExe -Sources "$PSScriptRoot/hmi_loader_probe/main.cpp" -Entry HmiProbeEntry -Name HMI_LOADER_PROBE -OutputDirectory "$taskRoot/build/emulator/hmi_loader_probe"
}
