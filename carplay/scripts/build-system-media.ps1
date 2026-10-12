# Compile the private modern-SDK objects in the caller's output directory.
# The application and shared code continue to use the existing C++03 toolchain.
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
Assert-WinXPlayEnvironment @('CARPLAY_VC','CARPLAY_MODERN_SDK','CARPLAY_CLANG')
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskVc=$env:CARPLAY_VC
$taskModern=$env:CARPLAY_MODERN_SDK
$taskClang=$env:CARPLAY_CLANG
$taskSdkLayout=Get-WinXPlayModernSdk
$taskSdkInclude=$taskSdkLayout.Include
$taskSdkLib=$taskSdkLayout.Library
$taskOldInclude=$env:INCLUDE
try {
    $env:INCLUDE="$taskSdkInclude/um;$taskSdkInclude/shared;$taskVc/include"
    foreach($taskUnit in @('mf_video','mf_audio')) {
        & $taskClang /nologo /c /O2 /MT /GS- /GR- /DUNICODE /D_UNICODE /DNOMINMAX /DNDEBUG /D_WIN32_WINNT=0x0A00 "/Fo$taskUnit.obj" "$taskRoot/platform/windows/$taskUnit.cpp" | Out-Host
        if($LASTEXITCODE -ne 0){throw "System media compile failed: $taskUnit"}
    }
}finally{$env:INCLUDE=$taskOldInclude}
$taskLibs=@();foreach($taskLib in @('d3d11','dxgi','mfplat','mfuuid','wmcodecdspuuid')){
    $taskLibrary=Join-Path $taskSdkLib "$taskLib.lib"
    if(!(Test-Path -LiteralPath $taskLibrary)){throw "Missing Windows SDK library: $taskLibrary"}
    $taskLibs+=$taskLibrary
}
$taskLibs+='ole32.lib'
@{Objects=@('mf_video.obj','mf_audio.obj'); Adapter="$taskRoot/platform/windows/mf_video_adapter.cpp"; Libs=$taskLibs}
