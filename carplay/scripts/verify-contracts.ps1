param([string]$Vc=$env:CARPLAY_VC)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
if(!$Vc){$Vc=$env:CARPLAY_VC}
$env:PATH="$Vc/bin/amd64;$Vc/../Common7/IDE;"+$env:PATH
$env:INCLUDE="$Vc/include"
$taskSdk=$env:CARPLAY_SDK
# The Windows C++ runtime itself needs the Kernel32 import library. No SDK
# headers are visible to the core build and no application platform code links.
$env:LIB="$Vc/lib/amd64;$taskSdk/Lib/x64"
$taskOut=Join-Path $taskRoot '../build/carplay/contracts'
New-Item -ItemType Directory -Force $taskOut | Out-Null
Push-Location $taskOut
try {
    $taskSources=@('protocol','plist','link','auth','identification','airinfo','input','pairing','buffered_audio','receiver') | ForEach-Object {"$taskRoot/core/$_.cpp"}
    # Only C++ runtime headers: proves contracts/core do not silently depend
    # on Windows headers, a selected device or a media/crypto implementation.
    & "$Vc/bin/amd64/cl.exe" /nologo /EHsc /MT /W4 /O2 @taskSources "$taskRoot/tests/native/contracts.cpp" /Fecontracts.exe
    if($LASTEXITCODE -ne 0){throw 'Portable contract build failed'}
    & ./contracts.exe
    if($LASTEXITCODE -ne 0){throw 'Portable contract tests failed'}
} finally {Pop-Location}
