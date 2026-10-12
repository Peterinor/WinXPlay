$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../../scripts/environment.ps1"
$taskRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/ce-portable-test'
& $env:MRN_PYTHON "$taskRoot/tests/wince/emulator_fixtures.py"
if($LASTEXITCODE -ne 0){throw 'Synthetic identity generation failed'}
Import-WinXPlayCoreCon
$taskStore=New-Object Microsoft.SmartDevice.Connectivity.DatastoreManager(1033)
$taskDevice=@($taskStore.GetPlatforms() | ForEach-Object {$_.GetDevices()} | Where-Object {$_.Name -eq $env:WINXPLAY_EMULATOR_DEVICE})[0]
$taskDevice.Connect()
try {
 $taskFiles=$taskDevice.GetFileDeployer();$taskRemote='\MRN_PORTABLE_'+[DateTime]::UtcNow.ToString('yyyyMMddHHmmss')
 foreach($taskFile in @('MRN_PORTABLE_SMOKE.exe','WinXPlayUsbMux.dll')){$taskFiles.SendFile("$taskOut/$taskFile","$taskRemote\$taskFile",$true,$false)}
 foreach($taskFile in @('certificate.p7b','certificate.der','identity.pk8')){
  $taskFiles.SendFile("$taskRoot/../build/carplay/ce-wm5-arm-mrn/emulator-fixtures/$taskFile","$taskRemote\identity\$taskFile",$true,$false)
 }
 $taskFiles.SendFile("$taskRoot/../build/carplay/ce-wm5-arm-mrn/emulator-fixtures/mismatched.pk8","$taskRemote\mismatched.pk8",$true,$false)
 $taskProcess=$taskDevice.GetRemoteProcess()
 if(!$taskProcess.Start("$taskRemote\MRN_PORTABLE_SMOKE.exe",'')){throw 'Portable smoke launch failed'}
 $taskUntil=[DateTime]::UtcNow.AddSeconds(240)
 while(!$taskProcess.HasExited() -and [DateTime]::UtcNow -lt $taskUntil){Start-Sleep -Milliseconds 200}
 if(!$taskProcess.HasExited()){throw "Portable smoke timed out in $taskRemote"}
 $taskCode=$taskProcess.GetExitCode();$taskProcess.Close()
 $taskFiles.ReceiveFile("$taskRemote\portable-smoke.plist","$taskOut/portable-smoke.plist",$true)
 Get-Content "$taskOut/portable-smoke.plist"
 if($taskCode -ne 0){throw "Portable smoke failed: $taskCode"}
}finally{$taskDevice.Disconnect()}
