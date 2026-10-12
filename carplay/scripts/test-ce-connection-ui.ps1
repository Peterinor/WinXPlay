param([string]$FontPath='')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
if(!(Test-Path -LiteralPath $FontPath)){throw 'Provide the locally inspected OEM lanting.TTF with -FontPath; it is not bundled.'}
$taskVc=$env:CARPLAY_VC
$taskSdk=$env:CARPLAY_SDK
$taskOut=Join-Path $taskRoot '../build/carplay/connection-ui'
New-Item -ItemType Directory -Force $taskOut | Out-Null
$env:PATH="$taskVc/bin;$taskVc/../Common7/IDE;"+$env:PATH
$env:INCLUDE="$taskVc/include;$taskSdk/Include"
$env:LIB="$taskVc/lib/amd64;$taskSdk/Lib/x64"
Push-Location $taskOut
try {
 foreach($taskProfile in @('mrn','generic')) {
 $taskProfileFlags=@()
 if($taskProfile -eq 'generic'){$taskProfileFlags+='/DWXUI_GENERIC_TEST'}
 & "$taskVc/bin/amd64/cl.exe" /nologo /EHsc /MT /O2 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0600 /DNOMINMAX @taskProfileFlags "$taskRoot/tests/wince/connection_ui_host.cpp" "$taskRoot/devices/$taskProfile/profile.cpp" "/Feconnection_ui_tests_$taskProfile.exe" /link user32.lib gdi32.lib
 if($LASTEXITCODE -ne 0){throw 'Connection UI test build failed'}
 & "./connection_ui_tests_$taskProfile.exe" $FontPath
 if($LASTEXITCODE -ne 0){throw 'Connection UI test failed'}
 }
}finally{Pop-Location}
