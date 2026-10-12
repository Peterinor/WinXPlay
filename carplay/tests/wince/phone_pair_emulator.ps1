$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../../scripts/environment.ps1"
$taskRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/ce-phone-pair-test'
$taskPython=$env:MRN_PYTHON
& $taskPython "$taskRoot/tests/wince/phone_pair_validate.py" --prepare
if($LASTEXITCODE -ne 0){throw 'Synthetic phone key generation failed'}
Import-WinXPlayCoreCon
$taskStore=New-Object Microsoft.SmartDevice.Connectivity.DatastoreManager(1033)
$taskDevice=@($taskStore.GetPlatforms() | ForEach-Object {$_.GetDevices()} | Where-Object {$_.Name -eq $env:WINXPLAY_EMULATOR_DEVICE})[0]
$taskDevice.Connect()
try {
 $taskFiles=$taskDevice.GetFileDeployer();$taskRemote='\MRN_PAIR_'+[DateTime]::UtcNow.ToString('yyyyMMddHHmmss')
 foreach($taskFile in @('MRN_PHONE_PAIR_SMOKE.exe','device-public.pem')){$taskFiles.SendFile("$taskOut/$taskFile","$taskRemote\$taskFile",$true,$false)}
 $taskProcess=$taskDevice.GetRemoteProcess()
 if(!$taskProcess.Start("$taskRemote\MRN_PHONE_PAIR_SMOKE.exe",'')){throw 'Phone pairing launch failed'}
 $taskUntil=[DateTime]::UtcNow.AddSeconds(600)
 while(!$taskProcess.HasExited() -and [DateTime]::UtcNow -lt $taskUntil){Start-Sleep -Milliseconds 200}
 if(!$taskProcess.HasExited()){throw "Phone pairing timed out in $taskRemote"}
 $taskCode=$taskProcess.GetExitCode();$taskProcess.Close()
 $taskFiles.ReceiveFile("$taskRemote\phone-pair-smoke.plist","$taskOut/phone-pair-smoke.plist",$true)
 Get-Content "$taskOut/phone-pair-smoke.plist"
 if($taskCode -ne 0){throw "Phone pairing smoke failed: $taskCode"}
 foreach($taskFile in @('generated-pair.plist','pair-request.plist')){$taskFiles.ReceiveFile("$taskRemote\$taskFile","$taskOut/$taskFile",$true)}
}finally{$taskDevice.Disconnect()}
& $taskPython "$taskRoot/tests/wince/phone_pair_validate.py"
if($LASTEXITCODE -ne 0){throw 'Independent pairing certificate validation failed'}
