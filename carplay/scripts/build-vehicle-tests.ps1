param([ValidateSet('all','av','connect','input')][string]$Target='all')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/vehicle-tests'
$taskVc=$env:CARPLAY_VC
$taskSdk=$env:CARPLAY_CE_SDK
New-Item -ItemType Directory -Force $taskOut | Out-Null
$env:PATH="$taskVc/bin;$taskVc/../Common7/IDE;"+$env:PATH
$env:INCLUDE="$taskVc/ce/include;$taskSdk/Include/Armv4i"
$env:LIB="$taskVc/ce/lib/armv4i;$taskSdk/Lib/ARMV4I"
$taskFlags=@('/nologo','/c','/O2','/MT','/GS-','/EHsc','/Zc:wchar_t-','/DUNICODE','/D_UNICODE','/DNOMINMAX','/D_WIN32_WCE=0x500','/DUNDER_CE','/DARM','/D_ARM_','/DWIN32_PLATFORM_PSPC','/DWINXPLAY_WOLFSSL')
Push-Location $taskOut
try {
 foreach($taskGroup in @('av','input')) {
  if($Target -ne 'all' -and $Target -ne $taskGroup){continue}
  $taskSources=@('platform/wince/platform.cpp','core/protocol.cpp','core/plist.cpp')
  if($taskGroup -eq 'av'){$taskSources+=@('tools/mrn/av_test.cpp','tools/mrn/av_clip.cpp','tools/mrn/media_probe.cpp','tools/mrn/voice_pcm_test.cpp','tools/mrn/microphone_test.cpp','tools/mrn/tts_pcm_contract.cpp','tools/mrn/suite_av.cpp')}
  else{$taskSources+=@('tools/mrn/suite_input.cpp')}
  $taskObjects=@()
  foreach($taskSource in $taskSources){
   $taskObj=$taskGroup+'_'+[IO.Path]::GetFileNameWithoutExtension($taskSource)+'.obj';$taskObjects+=$taskObj
   $taskExtra=@()
   & "$taskVc/ce/bin/x86_arm/cl.exe" @taskFlags @taskExtra "/Fo$taskObj" "$taskRoot/$taskSource"
   if($LASTEXITCODE -ne 0){throw "Compile failed: $taskSource"}
  }
  $taskName=if($taskGroup -eq 'av'){'winxplay_test_av.exe'}else{'winxplay_test_input.exe'}
  $taskLibs=@('coredll.lib','corelibc.lib','crypt32.lib','ws2.lib')
  if($taskGroup -eq 'av'){$taskLibs+=@('strmbase.lib','strmiids.lib','ole32.lib','uuid.lib')}
  & "$taskVc/ce/bin/x86_arm/link.exe" /nologo "/OUT:$taskName" /SUBSYSTEM:WINDOWSCE,5.00 /MACHINE:THUMB /STACK:262144,4096 @taskObjects @taskLibs
  if($LASTEXITCODE -ne 0){throw "Link failed: $taskName"}
 }
}finally{Pop-Location}
if($Target -in @('all','connect')){& "$PSScriptRoot/build-wince.ps1" -VehicleTests;if($LASTEXITCODE -ne 0){throw 'Connection test build failed'}}
Get-ChildItem -LiteralPath $taskOut -Filter '*.exe' | Select-Object Name,Length
