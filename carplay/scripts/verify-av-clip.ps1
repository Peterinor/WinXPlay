param()
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/vehicle-tests'
$taskVc=$env:CARPLAY_VC
$taskSdk=$env:CARPLAY_SDK
$env:PATH="$taskVc/bin;$taskVc/../Common7/IDE;"+$env:PATH
$env:INCLUDE="$taskVc/include"
$env:LIB="$taskVc/lib/amd64;$taskSdk/Lib/x64"
$taskTests=Join-Path $taskOut 'native-tests'
New-Item -ItemType Directory -Force $taskTests | Out-Null
Push-Location $taskTests
try {
 & "$taskVc/bin/amd64/cl.exe" /nologo /EHsc /MT /O2 /Feav_clip_tests.exe "$taskRoot/tests/native/av_clip_tests.cpp" "$taskRoot/tools/mrn/av_clip.cpp" "$taskRoot/core/protocol.cpp"
 if($LASTEXITCODE -ne 0){throw 'Fixture tests build failed'}
 & ./av_clip_tests.exe "$taskOut/video.avt" "$taskOut/rgb.avt"
 if($LASTEXITCODE -ne 0){throw 'Fixture contract tests failed'}
}finally{Pop-Location}
