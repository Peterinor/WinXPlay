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
 Push-Location $taskOut
 try {
  & "$taskVc/bin/amd64/cl.exe" /nologo /EHsc /MT /O2 /DUNICODE /D_UNICODE /Femrn_audio_test.exe "$taskRoot/tests/wince/mrn_audio_test.cpp" "$taskRoot/core/protocol.cpp" "$taskRoot/core/plist.cpp" /link user32.lib
  if($LASTEXITCODE -ne 0){throw 'MRN audio test compile failed'}
  & ./mrn_audio_test.exe
  if($LASTEXITCODE -ne 0){throw 'MRN audio test failed'}
  & "$taskVc/bin/amd64/cl.exe" /nologo /EHsc /MT /O2 /DUNICODE /D_UNICODE /DWINXPLAY_AUDIO_RECOVERY_HOST_TEST /Feaudio_recovery_test.exe "$taskRoot/tests/wince/audio_output_smoke.cpp" "$taskRoot/platform/wince/audio_recovery.cpp" "$taskRoot/platform/wince/audio.cpp" "$taskRoot/core/protocol.cpp" "$taskRoot/core/plist.cpp"
  if($LASTEXITCODE -ne 0){throw 'MRN audio recovery test compile failed'}
  & ./audio_recovery_test.exe
  if($LASTEXITCODE -ne 0){throw 'MRN audio recovery test failed'}
 } finally {Pop-Location}
} finally {
 $env:PATH=$taskSavedPath; $env:INCLUDE=$taskSavedInclude; $env:LIB=$taskSavedLib
}
