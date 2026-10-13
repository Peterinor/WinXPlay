$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/ce-aac-host-test'
$taskVc=$env:CARPLAY_VC
$taskSdk=$env:CARPLAY_SDK
New-Item -ItemType Directory -Force $taskOut | Out-Null
$env:PATH="$taskVc/bin;$taskVc/../Common7/IDE;"+$env:PATH
$env:INCLUDE="$taskVc/include;$taskSdk/Include"
$env:LIB="$taskVc/lib/amd64;$taskSdk/Lib/x64"
Push-Location $taskOut
try {
 & "$taskVc/bin/amd64/cl.exe" /nologo /EHsc /MT /O2 /DUNICODE /D_UNICODE /DNOMINMAX /Feaac_dmo_test.exe "$taskRoot/tests/wince/aac_dmo_test.cpp" "$taskRoot/core/protocol.cpp" "$taskRoot/core/plist.cpp" /link ole32.lib uuid.lib dmoguids.lib
 if($LASTEXITCODE -ne 0){throw 'AAC test compile failed'}
 & ./aac_dmo_test.exe
 if($LASTEXITCODE -ne 0){throw 'AAC test failed'}
}finally{Pop-Location}
