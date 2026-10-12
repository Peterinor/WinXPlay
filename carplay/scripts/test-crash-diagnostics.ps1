$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/ce-host-x64'
$taskVc=$env:CARPLAY_VC
$taskSdk=$env:CARPLAY_SDK
$taskSavedPath=$env:PATH; $taskSavedInclude=$env:INCLUDE; $taskSavedLib=$env:LIB
try {
 $env:PATH="$taskVc/bin;$taskVc/../Common7/IDE;"+$env:PATH
 $env:INCLUDE="$taskVc/include;$taskSdk/Include"
 $env:LIB="$taskVc/lib/amd64;$taskSdk/Lib/x64"
 New-Item -ItemType Directory -Force $taskOut | Out-Null
 & "$taskVc/bin/amd64/cl.exe" /nologo /MT /O2 /DUNICODE /D_UNICODE "/Fo$taskOut/crash_diagnostics_test.obj" "/Fe$taskOut/crash_diagnostics_test.exe" "$taskRoot/tests/wince/crash_diagnostics_test.cpp" /link user32.lib
 if($LASTEXITCODE -ne 0){throw 'Crash diagnostics test compile failed'}
 & "$taskOut/crash_diagnostics_test.exe"
 if($LASTEXITCODE -ne 0){throw 'Crash diagnostics test failed'}
} finally {
 $env:PATH=$taskSavedPath; $env:INCLUDE=$taskSavedInclude; $env:LIB=$taskSavedLib
}
