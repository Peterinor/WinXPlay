$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/ce-host-x64'
$taskVc=$env:CARPLAY_VC
$taskSdk=$env:CARPLAY_SDK
New-Item -ItemType Directory -Force $taskOut | Out-Null
$taskSavedPath=$env:PATH; $taskSavedInclude=$env:INCLUDE; $taskSavedLib=$env:LIB
try {
 $env:PATH="$taskVc/bin;$taskVc/../Common7/IDE;"+$env:PATH
 $env:INCLUDE="$taskVc/include;$taskSdk/Include"
 $env:LIB="$taskVc/lib/amd64;$taskSdk/Lib/x64"
 foreach($taskTest in @('apple_usb_configuration_test','apple_ncm_binding_test')){
 & "$taskVc/bin/amd64/cl.exe" /nologo /EHsc /MT /O2 /DUNICODE /D_UNICODE "/Fo$taskOut/$taskTest.obj" "/Fe$taskOut/$taskTest.exe" "$taskRoot/tests/wince/$taskTest.cpp" /link user32.lib
 if($LASTEXITCODE -ne 0){throw 'Apple USB configuration test compile failed'}
 & "$taskOut/$taskTest.exe"
 if($LASTEXITCODE -ne 0){throw 'Apple USB configuration test failed'}
 }
} finally {
 $env:PATH=$taskSavedPath; $env:INCLUDE=$taskSavedInclude; $env:LIB=$taskSavedLib
}
