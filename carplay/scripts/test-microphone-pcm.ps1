$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/microphone-tests'
$taskVc=$env:CARPLAY_VC
New-Item -ItemType Directory -Force $taskOut | Out-Null
$env:PATH="$taskVc/bin/amd64;$taskVc/../Common7/IDE;"+$env:PATH
$env:INCLUDE="$taskVc/include"
$env:LIB="$taskVc/lib/amd64;$env:CARPLAY_SDK/Lib/x64"
Push-Location $taskOut
try {
 & "$taskVc/bin/amd64/cl.exe" /nologo /MT /EHsc /Femicrophone_pcm_test.exe /Fomicrophone_pcm_test.obj "$taskRoot/tests/wince/microphone_pcm_test.cpp"
 if($LASTEXITCODE -ne 0){throw 'PCM test build failed'}
 & ./microphone_pcm_test.exe microphone-fixture.wav
 if($LASTEXITCODE -ne 0){throw 'PCM meter and WAV test failed'}
}finally{Pop-Location}
