$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/report-worker-host'
$taskVc=$env:CARPLAY_VC
$taskSdk=$env:CARPLAY_SDK
$taskSavedPath=$env:PATH; $taskSavedInclude=$env:INCLUDE; $taskSavedLib=$env:LIB
try {
 $env:PATH="$taskVc/bin;$taskVc/../Common7/IDE;"+$env:PATH
 $env:INCLUDE="$taskVc/include;$taskSdk/Include"
 $env:LIB="$taskVc/lib/amd64;$taskSdk/Lib/x64"
 New-Item -ItemType Directory -Force $taskOut | Out-Null
 Push-Location $taskOut
 try {
  & "$taskVc/bin/amd64/cl.exe" /nologo /EHsc /MT /O2 /DUNICODE /D_UNICODE /Fereport_worker_test.exe "$taskRoot/tests/wince/report_worker_test.cpp" "$taskRoot/core/protocol.cpp" "$taskRoot/core/plist.cpp"
  if($LASTEXITCODE -ne 0){throw 'Report worker test compile failed'}
  & ./report_worker_test.exe
  if($LASTEXITCODE -ne 0){throw 'Report worker test failed'}
 } finally {Pop-Location}
} finally {
 $env:PATH=$taskSavedPath; $env:INCLUDE=$taskSavedInclude; $env:LIB=$taskSavedLib
}
