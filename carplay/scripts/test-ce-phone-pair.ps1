param([switch]$BuildOnly)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/ce-phone-pair-test'
$taskVc=$env:CARPLAY_VC
$taskSdk=$env:CARPLAY_CE_SDK
New-Item -ItemType Directory -Force $taskOut | Out-Null
$env:PATH="$taskVc/bin;$taskVc/../Common7/IDE;"+$env:PATH
$env:INCLUDE="$taskVc/ce/include;$taskSdk/Include/Armv4i"
$env:LIB="$taskVc/ce/lib/armv4i;$taskSdk/Lib/ARMV4I"
$taskFlags=@('/nologo','/c','/O2','/MT','/GS-','/EHsc','/Zc:wchar_t-','/DUNICODE','/D_UNICODE','/DNOMINMAX','/D_WIN32_WCE=0x500','/DUNDER_CE','/DARM','/D_ARM_','/DWIN32_PLATFORM_PSPC','/DWINXPLAY_WOLFSSL','/DWOLFSSL_USER_SETTINGS',"/I$taskRoot/platform/common","/I$env:CARPLAY_WOLFSSL")
Push-Location $taskOut
try {
 $taskObjects=@()
 foreach($taskFile in @('tests/wince/phone_pair_smoke.cpp','platform/common/phone_pair.cpp','platform/common/phone_identity_wolfssl.cpp','platform/common/crypto_wolfssl.cpp','platform/wince/platform.cpp','core/protocol.cpp','core/plist.cpp')){
  $taskObj=([IO.Path]::GetFileNameWithoutExtension($taskFile))+'.obj';$taskObjects+=$taskObj
  & "$taskVc/ce/bin/x86_arm/cl.exe" @taskFlags "/Fo$taskObj" "$taskRoot/$taskFile"
  if($LASTEXITCODE -ne 0){throw "Compile failed: $taskFile"}
 }
 & "$taskVc/ce/bin/x86_arm/link.exe" /nologo /OUT:MRN_PHONE_PAIR_SMOKE.exe /SUBSYSTEM:WINDOWSCE,5.00 /MACHINE:THUMB /STACK:262144,4096 @taskObjects "$taskRoot/../build/carplay/ce-wm5-arm-mrn/mrn_wolfssl.lib" coredll.lib corelibc.lib crypt32.lib ws2.lib
 if($LASTEXITCODE -ne 0){throw 'Phone pairing test link failed'}
}finally{Pop-Location}
if(!$BuildOnly){
 & (Get-WinXPlayPowerShell32) -NoProfile -File "$taskRoot/tests/wince/phone_pair_emulator.ps1"
 if($LASTEXITCODE -ne 0){throw 'Phone pairing emulator test failed'}
}
