param(
    [ValidateSet('CeHost','CeMrn','CeGeneric')][string]$Target='CeHost',
    [switch]$SkipBuild,
    [string]$Python=$env:MRN_PYTHON
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
if (!$Python) { $Python=$env:MRN_PYTHON }
$taskRoot=Split-Path $PSScriptRoot -Parent
if(!$SkipBuild){ & "$taskRoot/build.ps1" -Target $Target -Python $Python }
if($Target -eq 'CeHost'){
    & $Python "$taskRoot/tests/windows/fixtures.py"
    if($LASTEXITCODE -ne 0){throw 'Fixture generation failed'}
    $taskBuild=Join-Path $taskRoot '../build/carplay/ce-host-x64'
    New-Item -ItemType Directory -Force "$taskBuild/fixtures" | Out-Null
    Copy-Item "$taskRoot/../build/carplay/windows-x64/fixtures/*" "$taskBuild/fixtures" -Force
    Push-Location $taskBuild
    try { & ./carplay_tests.exe; if($LASTEXITCODE -ne 0){throw 'CE-host native tests failed'} } finally {Pop-Location}
    & $Python "$taskRoot/tests/wince/host_integration.py"
    if($LASTEXITCODE -ne 0){throw 'CE-host integration failed'}
} else {
    $taskProfile=if($Target -eq 'CeMrn'){'ce-wm5-arm-mrn'}else{'ce-wm5-arm-generic'}
    & $Python "$taskRoot/tests/wince/audit_binaries.py" --build-dir "$taskRoot/../build/carplay/$taskProfile"
    if($LASTEXITCODE -ne 0){throw 'CE binary audit failed'}
    if($Target -eq 'CeMrn'){
        & (Get-WinXPlayPowerShell32) -NoProfile -ExecutionPolicy Bypass -File "$taskRoot/tests/wince/emulator-selftest.ps1" -Root $taskRoot
        if($LASTEXITCODE -ne 0){throw 'ARM emulator tests failed'}
    }
}
Write-Output "Unified $Target validation passed. Physical CE hardware remains unverified."
