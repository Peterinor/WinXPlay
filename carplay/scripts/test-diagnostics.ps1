param([switch]$NoLogs)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/diagnostics-tests'
$taskVc=$env:CARPLAY_VC
$taskSdk=$env:CARPLAY_SDK
if($NoLogs){$taskOut+='-nologs'}
$taskFlags=@();if($NoLogs){$taskFlags+='/DWINXPLAY_ENABLE_LOGS=0'}
New-Item -ItemType Directory -Force $taskOut | Out-Null
$env:PATH="$taskVc/bin;$taskVc/../Common7/IDE;"+$env:PATH
$env:INCLUDE="$taskVc/include;$taskSdk/Include"
$env:LIB="$taskVc/lib/amd64;$taskSdk/Lib/x64"
Push-Location $taskOut
try {
 & "$taskVc/bin/amd64/cl.exe" @taskFlags /nologo /EHsc /MT /O2 /DUNICODE /D_UNICODE /DWINXPLAY_DIAGNOSTICS_TEST /D_WIN32_WINNT=0x0600 /DNOMINMAX "$taskRoot/tests/wince/diagnostics_host.cpp" /Fediagnostics_tests.exe /link ws2_32.lib user32.lib
 if($LASTEXITCODE -ne 0){throw 'Diagnostic test build failed'}
 & ./diagnostics_tests.exe
 if($LASTEXITCODE -ne 0){throw 'Diagnostic runtime test failed'}
}finally{Pop-Location}
