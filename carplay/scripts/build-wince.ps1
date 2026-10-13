param([switch]$Desktop, [switch]$CryptoOnly, [switch]$SelftestOnly, [switch]$Generic, [switch]$VehicleTests, [switch]$NoLogs)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
Assert-WinXPlayEnvironment @('CARPLAY_VC','CARPLAY_WOLFSSL')
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskCe=Join-Path $taskRoot 'platform/wince'
$taskSharedCommon=Join-Path $taskRoot 'platform/common'
$taskDevice=if($Generic -or $Desktop){'generic'}else{'mrn'}
$taskDeviceRoot=Join-Path $taskRoot "devices/$taskDevice"
$taskVc=$env:CARPLAY_VC
$taskSdk=if($Desktop){$env:CARPLAY_SDK}else{$env:CARPLAY_CE_SDK}
$taskWolf=$env:CARPLAY_WOLFSSL
$taskOut=Join-Path $taskRoot $(if($Desktop){'../build/carplay/ce-host-x64'}elseif($Generic){'../build/carplay/ce-wm5-arm-generic'}else{'../build/carplay/ce-wm5-arm-mrn'})
if($NoLogs){$taskOut+='-nologs'}
New-Item -ItemType Directory -Force $taskOut | Out-Null
$env:PATH="$taskVc/bin;$taskVc/../Common7/IDE;"+$env:PATH
if($Desktop){
 $taskCl="$taskVc/bin/amd64/cl.exe"; $taskLink="$taskVc/bin/amd64/link.exe"; $taskLib="$taskVc/bin/amd64/lib.exe"
 $env:INCLUDE="$taskVc/include;$taskSdk/Include"; $env:LIB="$taskVc/lib/amd64;$taskSdk/Lib/x64"
 $taskFlags=@('/DWIN32','/D_WIN32_WINNT=0x0600')
}else{
 $taskCl="$taskVc/ce/bin/x86_arm/cl.exe"; $taskLink="$taskVc/ce/bin/x86_arm/link.exe"; $taskLib="$taskVc/ce/bin/x86_arm/lib.exe"
 $env:INCLUDE="$taskVc/ce/include;$taskSdk/Include/Armv4i"; $env:LIB="$taskVc/ce/lib/armv4i;$taskSdk/Lib/ARMV4I"
 $taskFlags=@('/D_WIN32_WCE=0x500','/DUNDER_CE','/DARM','/D_ARM_','/DWIN32_PLATFORM_PSPC')
}
$taskFlags+=@('/nologo','/c','/O2','/MT','/GS-','/DUNICODE','/D_UNICODE','/DNOMINMAX','/DWOLFSSL_USER_SETTINGS','/D_CRT_SECURE_NO_WARNINGS',"/I$taskCe","/I$taskSharedCommon","/I$taskWolf")
if($NoLogs){$taskFlags+='/DWINXPLAY_ENABLE_LOGS=0'}
$taskCrypto=@('aes','asn','chacha','chacha20_poly1305','coding','curve25519','dh','ecc','ed25519','error','fe_low_mem','ge_low_mem','misc','hash','hmac','tfm','kdf','logging','memory','md5','poly1305','pwdbased','random','rsa','sha','sha256','sha512','signature','wc_port','wc_encrypt','wolfmath')
# Source encoding is prepared during migration; builds do not rewrite source files.
Push-Location $taskOut
try {
 $taskObjects=@()
 foreach($stem in $taskCrypto){
  $source="$taskWolf/wolfcrypt/src/$stem.c"; $object="wolf_$stem.obj"; $taskObjects+=$object
  if(!(Test-Path $object) -or (Get-Item $object).LastWriteTime -lt (Get-Item "$taskSharedCommon/user_settings.h").LastWriteTime){
   & $taskCl @taskFlags "/Fo$object" $source
   if($LASTEXITCODE -ne 0){throw "wolfCrypt compile failed: $stem"}
  }
 }
 foreach($stem in @('internal','io','keys','ssl','tls','tls13','wolfio')){
  $source="$taskWolf/src/$stem.c"
  if(!(Test-Path $source)){continue}
  $object="tls_$stem.obj"; $taskObjects+=$object
  if(!(Test-Path $object) -or (Get-Item $object).LastWriteTime -lt (Get-Item "$taskSharedCommon/user_settings.h").LastWriteTime){
   & $taskCl @taskFlags "/Fo$object" $source
   if($LASTEXITCODE -ne 0){throw "wolfSSL compile failed: $stem"}
  }
 }
 & $taskLib /nologo '/OUT:mrn_wolfssl.lib' @taskObjects
 if($LASTEXITCODE -ne 0){throw 'Crypto archive failed'}
 if(!$CryptoOnly){
  $taskAppFlags=$taskFlags+@('/EHsc','/DWINXPLAY_WOLFSSL')
  if($Desktop -and !$SelftestOnly){$taskAppFlags+='/DCARPLAY_HAS_MF'}
  if($taskDevice -eq 'mrn'){$taskAppFlags+=@('/DCARPLAY_CE_DSHOW','/DWINXPLAY_DEVICE_MRN','/Zc:wchar_t-')}
  if(!$Desktop){$taskAppFlags+='/DCARPLAY_CE_AAC'}
  $taskCore=Join-Path $taskRoot 'core'
  $taskAppObjects=@()
  foreach($stem in @('protocol','plist','link','auth','identification','airinfo','input')){
   $object="core_$stem.obj"; $taskAppObjects+=$object
   & $taskCl @taskAppFlags "/Fo$object" "$taskCore/$stem.cpp"
   if($LASTEXITCODE -ne 0){throw "Core compile failed: $stem"}
  }
  foreach($stem in @('crypto','platform','frame','audio_wave','presenter_gdi','selftest')){
   $source= switch($stem){
    'crypto' {"$taskSharedCommon/crypto_wolfssl.cpp"}
    'frame' {"$taskSharedCommon/frame.cpp"}
    'selftest' {"$taskRoot/tests/wince/selftest.cpp"}
    default {"$taskCe/$stem.cpp"}
   }
   $object="ce_$stem.obj"; $taskAppObjects+=$object
   & $taskCl @taskAppFlags "/Fo$object" $source
   if($LASTEXITCODE -ne 0){throw "CE compile failed: $stem"}
  }
  $taskLinkFlags=if($Desktop){@('/SUBSYSTEM:WINDOWS','user32.lib','gdi32.lib','advapi32.lib','crypt32.lib','ws2_32.lib','winmm.lib')}else{@('/SUBSYSTEM:WINDOWSCE,5.00','/MACHINE:THUMB','/STACK:262144,4096','coredll.lib','corelibc.lib','crypt32.lib','ws2.lib')}
  & $taskLink /nologo '/OUT:MRN_CARPLAY_SELFTEST.exe' @taskAppObjects 'mrn_wolfssl.lib' @taskLinkFlags
  if($LASTEXITCODE -ne 0){throw 'CE link failed'}
  if(!$SelftestOnly){
   $taskAppObjects=$taskAppObjects | Where-Object {$_ -ne 'ce_selftest.obj'}
   $taskShared=Join-Path $taskRoot 'platform/windows'
   foreach($stem in @('pairing','receiver','buffered_audio','lockdown','receiver_context','profile')){
    $source=if($stem -eq 'lockdown' -or $stem -eq 'receiver_context'){"$taskSharedCommon/$stem.cpp"}elseif($stem -eq 'profile'){"$taskDeviceRoot/profile.cpp"}else{"$taskRoot/core/$stem.cpp"}
    $object="shared_$stem.obj"; $taskAppObjects+=$object
    & $taskCl @taskAppFlags "/Fo$object" $source
    if($LASTEXITCODE -ne 0){throw "Shared adapter compile failed: $stem"}
   }
   $taskNative=if($Desktop){@('auth','main')}else{@('auth','audio','video','backend_registry','device_wince','usbmux','main','phone_pair','phone_identity_wolfssl','portable','wave_mixer','audio_recovery')}
   if(!$Desktop){$taskNative+='audio_aac';$taskLinkFlags+=@('ole32.lib','uuid.lib','dmoguids.lib')}
   if(!$Desktop -and !$Generic){$taskNative+=@('video_dshow','audio_route','audio_input');$taskLinkFlags+=@('strmbase.lib','strmiids.lib','ole32.lib','uuid.lib')}
   foreach($stem in $taskNative){
    $source=if($stem -eq 'auth'){"$taskSharedCommon/identity_wolfssl.cpp"}elseif($stem -eq 'phone_pair' -or $stem -eq 'phone_identity_wolfssl'){"$taskSharedCommon/$stem.cpp"}elseif($stem -eq 'device_wince'){"$taskDeviceRoot/wince.cpp"}elseif($stem -eq 'audio_route' -or $stem -eq 'audio_input'){"$taskDeviceRoot/$stem.cpp"}else{"$taskCe/$stem.cpp"}
    $object="ce_$stem.obj"; $taskAppObjects+=$object
    & $taskCl @taskAppFlags "/Fo$object" $source
    if($LASTEXITCODE -ne 0){throw "Native backend compile failed: $stem"}
   }
   if($Desktop){
    $taskSystemMedia=& "$PSScriptRoot/build-system-media.ps1"
    $taskAppObjects+=$taskSystemMedia.Objects
    $taskLinkFlags+=$taskSystemMedia.Libs
    & $taskCl @taskAppFlags '/Fosystem_video_adapter.obj' $taskSystemMedia.Adapter
    if($LASTEXITCODE -ne 0){throw 'Host system video adapter compile failed'}
    $taskAppObjects+='system_video_adapter.obj'
    & $taskCl @taskAppFlags '/Foshared_media.obj' "$taskShared/live_media.cpp"
    if($LASTEXITCODE -ne 0){throw 'Host media adapter compile failed'}
    $taskAppObjects+='shared_media.obj'
    & $taskCl @taskAppFlags '/Foshared_usb.obj' "$taskShared/usb.cpp"
    if($LASTEXITCODE -ne 0){throw 'Host USB adapter compile failed'}
    $taskAppObjects+='shared_usb.obj';$taskLinkFlags+=@('setupapi.lib','iphlpapi.lib')
   }
   & $taskLink /nologo '/OUT:WinXPlay.exe' @taskAppObjects 'mrn_wolfssl.lib' @taskLinkFlags
   if($LASTEXITCODE -ne 0){throw 'Receiver link failed'}
   if($VehicleTests -and !$Desktop -and !$Generic){
    $taskSuiteOut=Join-Path $taskRoot '../build/carplay/vehicle-tests'
    New-Item -ItemType Directory -Force $taskSuiteOut | Out-Null
    $taskSuiteObjects=@($taskAppObjects | Where-Object {$_ -ne 'ce_main.obj'})
    foreach($taskSuite in @(
     @{source='platform/wince/main.cpp';object='suite_receiver.obj';entry='/DWINXPLAY_CONNECTION_TEST'},
     @{source='tools/mrn/network_test.cpp';object='suite_network.obj';entry='/DWINXPLAY_TEST_SUITE'},
     @{source='tools/mrn/suite_connect.cpp';object='suite_connect.obj';entry='/DWINXPLAY_TEST_SUITE'}
    )){
     & $taskCl @taskAppFlags $taskSuite.entry ("/Fo"+$taskSuite.object) (Join-Path $taskRoot $taskSuite.source)
     if($LASTEXITCODE -ne 0){throw "Suite compile failed: $($taskSuite.source)"}
     $taskSuiteObjects+=$taskSuite.object
    }
    & $taskLink /nologo "/OUT:$taskSuiteOut/winxplay_test_connect.exe" @taskSuiteObjects 'mrn_wolfssl.lib' @taskLinkFlags
    if($LASTEXITCODE -ne 0){throw 'Connection suite link failed'}
   }
   if($Desktop){
    $taskTestObjects=$taskAppObjects | Where-Object {$_ -ne 'ce_main.obj'}
    & $taskCl @taskAppFlags '/Fohost_test.obj' "$taskRoot/tests/windows/test.cpp"
    if($LASTEXITCODE -ne 0){throw 'Host protocol tests compile failed'}
    & $taskLink /nologo /SUBSYSTEM:CONSOLE /OUT:carplay_tests.exe @taskTestObjects host_test.obj mrn_wolfssl.lib user32.lib gdi32.lib advapi32.lib crypt32.lib ws2_32.lib winmm.lib setupapi.lib iphlpapi.lib @($taskSystemMedia.Libs)
    if($LASTEXITCODE -ne 0){throw 'Host protocol tests link failed'}
   }
   if(!$Desktop){
    $taskUsbFlags=@($taskFlags)
    if($taskDevice -eq 'mrn'){$taskUsbFlags+='/DWINXPLAY_DEVICE_MRN'}
    & $taskCl @taskUsbFlags '/Fousb_driver.obj' "$taskCe/usb_driver/driver.cpp"
    if($LASTEXITCODE -ne 0){throw 'USB client compile failed'}
    & $taskLink /nologo /DLL /MACHINE:THUMB /SUBSYSTEM:WINDOWSCE,5.00 "/DEF:$taskCe/usb_driver/driver.def" /OUT:WinXPlayUsbMux.dll /NODEFAULTLIB:OLDNAMES usb_driver.obj coredll.lib corelibc.lib
    if($LASTEXITCODE -ne 0){throw 'USB client link failed'}
   }
  }
 }
}finally{Pop-Location}
