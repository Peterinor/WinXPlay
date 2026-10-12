param()
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/video-queue-tests'
$taskVc=$env:CARPLAY_VC
$env:PATH="$taskVc/bin/amd64;$taskVc/../Common7/IDE;"+$env:PATH
$env:INCLUDE="$taskVc/include"
$env:LIB="$taskVc/lib/amd64;$env:CARPLAY_SDK/Lib/x64"
New-Item -ItemType Directory -Force $taskOut | Out-Null
Push-Location $taskOut
try {
 & "$taskVc/bin/amd64/cl.exe" /nologo /EHsc /MT /O2 /Fevideo_queue_tests.exe "$taskRoot/tests/native/video_queue_tests.cpp" "$taskRoot/core/protocol.cpp" "$taskRoot/tools/mrn/av_clip.cpp"
 if($LASTEXITCODE -ne 0){throw 'Queue test compile failed'}
 & ./video_queue_tests.exe "$taskRoot/../build/carplay/vehicle-tests/video.avt" "$taskOut/queued.h264"
 if($LASTEXITCODE -ne 0){throw 'Queue test failed'}
 & $env:CARPLAY_FFMPEG -hide_banner -loglevel error -y -i "$taskOut/queued.h264" -f rawvideo -pix_fmt rgb24 "$taskOut/decoded.rgb"
 if($LASTEXITCODE -ne 0){throw 'Independent H264 decode failed'}
 if((Get-Item "$taskOut/decoded.rgb").Length -ne 180*800*480*3){throw 'Decoded frame count mismatch'}
 Write-Output 'PASS: all 180 queued H264 frames independently decode; FFmpeg is test-only.'
}finally{Pop-Location}
