param([string]$OutputDirectory,[switch]$SkipBuild)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskBuild=if($OutputDirectory){[IO.Path]::GetFullPath($OutputDirectory)}else{Join-Path $taskRoot '../build/carplay/windows-system-media'}
if(!$SkipBuild){ & "$PSScriptRoot/build-windows.ps1" -OutputDirectory $taskBuild }
$taskVc=$env:CARPLAY_VC
$taskSdk=$env:CARPLAY_SDK
$taskSdkLib=(Get-WinXPlayModernSdk).Library
$env:PATH="$taskVc/bin/amd64;$taskVc/../Common7/IDE;"+$env:PATH
$env:INCLUDE="$taskVc/include;$taskSdk/Include"
$env:LIB="$taskVc/lib/amd64;$taskSdk/Lib/x64"
$taskLibs=@(); foreach($taskLib in @('d3d11','dxgi','mfplat','mfuuid','wmcodecdspuuid')){$taskLibs+="$taskSdkLib/$taskLib.lib"}
Push-Location $taskBuild
try {
    & "$taskVc/bin/amd64/cl.exe" /nologo /EHsc /MT /O2 /DUNICODE /D_UNICODE "$taskRoot/tests/windows/system_media_test.cpp" mf_video.obj /Fesystem_media_test.exe /link user32.lib ole32.lib winmm.lib @taskLibs
    if($LASTEXITCODE -ne 0){throw 'System media test build failed'}
    $taskFfmpeg=$env:CARPLAY_FFMPEG
    foreach($taskCase in @(@(1280,720),@(1920,1080))){
        $taskW=$taskCase[0]; $taskH=$taskCase[1]; $taskFile="test-${taskW}.h264"
        & $taskFfmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc2=size=${taskW}x${taskH}:rate=30" -frames:v 180 -c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p -x264-params 'aud=1:keyint=30:bframes=0' -f h264 $taskFile
        if($LASTEXITCODE -ne 0){throw 'Video fixture generation failed'}
        foreach($taskDelay in @(0,33)){
            & ./system_media_test.exe $taskFile "result-${taskW}-${taskDelay}.json" $taskW $taskH 180 $taskDelay
            Get-Content "result-${taskW}-${taskDelay}.json"
            if($LASTEXITCODE -ne 0){throw "System video integration failed: $taskW / $taskDelay"}
        }
    }
}finally{Pop-Location}
