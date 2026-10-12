param([switch]$SkipBuild,[string]$Python=$env:MRN_PYTHON)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
if (!$Python) { $Python=$env:MRN_PYTHON }
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskBuild=Join-Path $taskRoot '../build/carplay/windows-x64'
& "$taskRoot/tests/windows/test-usb-repair.ps1"
if(!$?){throw 'USB repair offline regression failed'}
& "$env:SystemRoot/System32/WindowsPowerShell/v1.0/powershell.exe" -NoProfile -ExecutionPolicy Bypass -STA -File "$taskRoot/tests/windows/test-usb-repair-ui.ps1"
if($LASTEXITCODE -ne 0){throw 'USB repair full UI entry regression failed'}
& $Python "$taskRoot/tests/windows/fixtures.py"
if($LASTEXITCODE -ne 0){throw 'Fixture generation failed'}
if(!$SkipBuild){ & "$taskRoot/build.ps1" -Target Windows -Test -CheckArm -Python $Python }
else {
    Push-Location $taskBuild
    try { & ./carplay_tests.exe; if($LASTEXITCODE -ne 0){throw 'Native tests failed'} } finally {Pop-Location}
}
foreach($taskTest in @('runtime_layout_integration.py','auth_integration.py','tls_integration.py','input_integration.py','startup_home_integration.py','streaming_integration.py')){
    & $Python "$taskRoot/tests/windows/$taskTest"
    if($LASTEXITCODE -ne 0){throw "Independent test failed: $taskTest"}
}
foreach($taskMode in @('--fullhd','--aac','--buffered')){
    & $Python "$taskRoot/tests/windows/streaming_integration.py" $taskMode
    if($LASTEXITCODE -ne 0){throw "Streaming test failed: $taskMode"}
}
$taskUiProcess=Start-Process -FilePath "$taskBuild/WinXPlay.exe" -ArgumentList '--ui-smoke' -WindowStyle Hidden -PassThru
if(!$taskUiProcess.WaitForExit(20000)){
    Stop-Process -Id $taskUiProcess.Id -Force
    throw 'UI smoke timed out'
}
if($taskUiProcess.ExitCode -ne 0 -or !(Test-Path "$taskBuild/ui-smoke.bmp")){throw 'UI smoke failed'}
Write-Output 'Unified Windows validation passed. Physical iPhone regression was not performed by this script.'
