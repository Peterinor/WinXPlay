$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/ce-host-x64'
$taskVc=$env:CARPLAY_VC; $taskSdk=$env:CARPLAY_SDK
$taskOldPath=$env:PATH; $taskOldInclude=$env:INCLUDE; $taskOldLib=$env:LIB
try {
 $env:PATH="$taskVc/bin;$taskVc/../Common7/IDE;"+$env:PATH
 $env:INCLUDE="$taskVc/include;$taskSdk/Include"; $env:LIB="$taskVc/lib/amd64;$taskSdk/Lib/x64"
 New-Item -ItemType Directory -Force $taskOut | Out-Null
 Push-Location $taskOut
 try {
  & "$taskVc/bin/amd64/cl.exe" /nologo /EHsc /MT /O2 /DUNICODE /D_UNICODE /Femrn_microphone_test.exe "$taskRoot/tests/wince/mrn_microphone_test.cpp" "$taskRoot/core/protocol.cpp" "$taskRoot/core/plist.cpp" /link user32.lib
  if($LASTEXITCODE -ne 0){throw 'Microphone test build failed'}
  & ./mrn_microphone_test.exe
  if($LASTEXITCODE -ne 0){throw 'Microphone test failed'}
 }finally{Pop-Location}
}finally{$env:PATH=$taskOldPath;$env:INCLUDE=$taskOldInclude;$env:LIB=$taskOldLib}
