param([string]$BuildDirectory='')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
if (!$BuildDirectory) { $BuildDirectory=Join-Path $taskRoot '../build/carplay/windows-x64' }
if(!(Test-Path -LiteralPath (Join-Path $BuildDirectory 'receiver_context.obj'))){
 throw 'Build the Windows receiver first, or pass -BuildDirectory with its compiled objects.'
}
$taskVc=$env:CARPLAY_VC
$taskSdk=$env:CARPLAY_SDK
$taskSdkLib=(Get-WinXPlayModernSdk).Library
$taskSavedPath=$env:PATH; $taskSavedInclude=$env:INCLUDE; $taskSavedLib=$env:LIB
try {
 $env:PATH="$taskVc/bin;$taskVc/../Common7/IDE;"+$env:PATH
 $env:INCLUDE="$taskVc/include;$taskSdk/Include"
 $env:LIB="$taskVc/lib/amd64;$taskSdk/Lib/x64"
 Push-Location $BuildDirectory
 try {
  $taskFlags=@('/nologo','/EHsc','/MT','/O2','/c','/DUNICODE','/D_UNICODE','/DNOMINMAX','/D_WIN32_WINNT=0x0600','/DCARPLAY_HAS_MF')
  & "$taskVc/bin/amd64/cl.exe" @taskFlags /DreceiverAddresses=testReceiverAddresses /Fonetwork_session_lockdown.obj "$taskRoot/platform/common/lockdown.cpp"
  if($LASTEXITCODE -ne 0){throw 'CarKit test build failed'}
  & "$taskVc/bin/amd64/cl.exe" @taskFlags /Fonetwork_session_host.obj "$taskRoot/tests/windows/network_session_host.cpp"
  if($LASTEXITCODE -ne 0){throw 'Network session host build failed'}
  $taskObjects=@('aircrypto','airinfo','auth','buffered_audio','crypto','identification','input','link','live_media','mf_audio','mf_video_adapter','mf_video','pairing','platform','plist','profile','protocol','receiver_context','receiver','usb','windows_auth','network_session_lockdown','network_session_host') | ForEach-Object {"$_.obj"}
  $taskLibs=@('d3d11','dxgi','mfplat','mfuuid','wmcodecdspuuid') | ForEach-Object {"$taskSdkLib/$_.lib"}
  & "$taskVc/bin/amd64/link.exe" /nologo /OUT:network_session_host.exe @taskObjects ws2_32.lib user32.lib gdi32.lib winmm.lib crypt32.lib setupapi.lib iphlpapi.lib advapi32.lib ole32.lib @taskLibs
  if($LASTEXITCODE -ne 0){throw 'Network session host link failed'}
 } finally {Pop-Location}
 & $env:MRN_PYTHON "$taskRoot/tests/windows/network_session_integration.py" $BuildDirectory
 if($LASTEXITCODE -ne 0){throw 'Network session regression failed'}
} finally {
 $env:PATH=$taskSavedPath; $env:INCLUDE=$taskSavedInclude; $env:LIB=$taskSavedLib
}
