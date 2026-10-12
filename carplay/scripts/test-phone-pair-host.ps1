# Run shared pairing logic with real Windows encrypted storage and synthetic phone responses.
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskOut=Join-Path $taskRoot ('../build/carplay/phone-pair-host/'+[DateTime]::UtcNow.ToString('yyyyMMddHHmmss'))
$taskVc=$env:CARPLAY_VC
$taskSdk=$env:CARPLAY_SDK
New-Item -ItemType Directory -Force $taskOut | Out-Null
$env:PATH="$taskVc/bin;$taskVc/../Common7/IDE;"+$env:PATH
$env:INCLUDE="$taskVc/include;$taskSdk/Include"
$env:LIB="$taskVc/lib/amd64;$taskSdk/Lib/x64"
& python "$taskRoot/tests/wince/phone_pair_validate.py" --output $taskOut --prepare
if($LASTEXITCODE -ne 0){throw 'Synthetic fixture generation failed'}
Push-Location $taskOut
try {
 $taskObjects=@()
 foreach($taskFile in @('tests/wince/phone_pair_smoke.cpp','platform/common/phone_pair.cpp','platform/common/phone_identity_wolfssl.cpp','platform/common/crypto_wolfssl.cpp','platform/wince/platform.cpp','core/protocol.cpp','core/plist.cpp')){
  $taskObj=([IO.Path]::GetFileNameWithoutExtension($taskFile))+'.obj';$taskObjects+=$taskObj
  & "$taskVc/bin/amd64/cl.exe" /nologo /c /EHsc /MT /O2 /DUNICODE /D_UNICODE /DWINXPLAY_DIAGNOSTICS_TEST /D_WIN32_WINNT=0x0600 /DNOMINMAX /DWINXPLAY_WOLFSSL /DWOLFSSL_USER_SETTINGS "/I$taskRoot/platform/common" "/I$env:CARPLAY_WOLFSSL" "/Fo$taskObj" "$taskRoot/$taskFile"
  if($LASTEXITCODE -ne 0){throw "Compile failed: $taskFile"}
 }
 & "$taskVc/bin/amd64/link.exe" /nologo /OUT:phone_pair_tests.exe /SUBSYSTEM:WINDOWS @taskObjects "$taskRoot/../build/carplay/ce-host-x64/mrn_wolfssl.lib" ws2_32.lib user32.lib advapi32.lib crypt32.lib winmm.lib
 if($LASTEXITCODE -ne 0){throw 'Pairing host link failed'}
 $taskProcess=Start-Process -FilePath (Join-Path $taskOut 'phone_pair_tests.exe') -WindowStyle Hidden -PassThru
 if(!$taskProcess.WaitForExit(60000)){throw 'Synthetic pairing test timed out'}
 if($taskProcess.ExitCode -ne 0){Get-Content phone-pair-smoke.plist;throw 'Synthetic pairing failed'}
 Get-Content phone-pair-smoke.plist
 & python "$taskRoot/tests/wince/phone_pair_validate.py" --output $taskOut
 if($LASTEXITCODE -ne 0){throw 'Pairing certificate verification failed'}
}finally{Pop-Location}
