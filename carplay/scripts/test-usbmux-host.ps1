$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/usbmux-host-tests'
$taskVc=$env:CARPLAY_VC
$taskSdk=$env:CARPLAY_SDK
New-Item -ItemType Directory -Force $taskOut | Out-Null
$env:PATH="$taskVc/bin;$taskVc/../Common7/IDE;"+$env:PATH
$env:INCLUDE="$taskVc/include;$taskSdk/Include"
$env:LIB="$taskVc/lib/amd64;$taskSdk/Lib/x64"
Push-Location $taskOut
try {
 & "$taskVc/bin/amd64/cl.exe" /nologo /EHsc /MT /O2 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0600 /DNOMINMAX "$taskRoot/tests/wince/usbmux_host.cpp" "$taskRoot/core/protocol.cpp" "$taskRoot/core/plist.cpp" /Feusbmux_tests.exe /link ws2_32.lib advapi32.lib user32.lib
 if($LASTEXITCODE -ne 0){throw 'USBMUX test build failed'}
 & ./usbmux_tests.exe
 if($LASTEXITCODE -ne 0){throw 'USBMUX runtime test failed'}
}finally{Pop-Location}
