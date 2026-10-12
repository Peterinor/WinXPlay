param([switch]$BuildOnly)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/ce-bridge-test'
$taskVc=$env:CARPLAY_VC
$taskSdk=$env:CARPLAY_CE_SDK
New-Item -ItemType Directory -Force $taskOut | Out-Null
$env:PATH="$taskVc/bin;$taskVc/../Common7/IDE;"+$env:PATH
$env:INCLUDE="$taskVc/ce/include;$taskSdk/Include/Armv4i"
$env:LIB="$taskVc/ce/lib/armv4i;$taskSdk/Lib/ARMV4I"
$taskFlags=@('/nologo','/c','/O2','/MT','/GS-','/EHsc','/Zc:wchar_t-','/DUNICODE','/D_UNICODE','/DNOMINMAX','/D_WIN32_WCE=0x500','/DUNDER_CE','/DARM','/D_ARM_','/DWIN32_PLATFORM_PSPC')
Push-Location $taskOut
try {
 $taskObjects=@()
 foreach($taskFile in @('tests/wince/bridge_smoke.cpp','platform/wince/platform.cpp','core/protocol.cpp','core/plist.cpp')){
  $taskObj=([IO.Path]::GetFileNameWithoutExtension($taskFile))+'.obj';$taskObjects+=$taskObj
  & "$taskVc/ce/bin/x86_arm/cl.exe" @taskFlags "/Fo$taskObj" "$taskRoot/$taskFile"
  if($LASTEXITCODE -ne 0){throw "Compile failed: $taskFile"}
 }
 & "$taskVc/ce/bin/x86_arm/link.exe" /nologo /OUT:WINXPLAY_BRIDGE_SMOKE.exe /SUBSYSTEM:WINDOWSCE,5.00 /MACHINE:THUMB /STACK:262144,4096 @taskObjects coredll.lib corelibc.lib crypt32.lib ws2.lib
 if($LASTEXITCODE -ne 0){throw 'Bridge test link failed'}
}finally{Pop-Location}
if(!$BuildOnly){
 & (Get-WinXPlayPowerShell32) -NoProfile -File "$taskRoot/tests/wince/bridge_emulator.ps1"
 if($LASTEXITCODE -ne 0){throw 'Bridge emulator test failed'}
}
