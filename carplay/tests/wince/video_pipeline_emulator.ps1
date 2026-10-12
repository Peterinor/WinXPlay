$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../../scripts/environment.ps1"
$taskRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/ce-video-test'
Import-WinXPlayCoreCon
$taskStore=New-Object Microsoft.SmartDevice.Connectivity.DatastoreManager(1033)
$taskDevice=@($taskStore.GetPlatforms() | ForEach-Object {$_.GetDevices()} | Where-Object {$_.Name -eq $env:WINXPLAY_EMULATOR_DEVICE})[0]
$taskDevice.Connect()
try {
 $taskFiles=$taskDevice.GetFileDeployer();$taskRemote='\MRN_VIDEO_'+[DateTime]::UtcNow.ToString('yyyyMMddHHmmss')
 $taskFiles.SendFile("$taskOut/MRN_VIDEO_SMOKE.exe","$taskRemote\MRN_VIDEO_SMOKE.exe",$true,$false)
 $taskProcess=$taskDevice.GetRemoteProcess()
 if(!$taskProcess.Start("$taskRemote\MRN_VIDEO_SMOKE.exe",'')){throw 'Smoke launch failed'}
 # ARM emulation can advance the guest clock much slower than host time.
 $taskUntil=[DateTime]::UtcNow.AddSeconds(240)
 while(!$taskProcess.HasExited() -and [DateTime]::UtcNow -lt $taskUntil){Start-Sleep -Milliseconds 200}
 if(!$taskProcess.HasExited()){throw "Smoke timed out in $taskRemote; not a hardware success"}
 $taskCode=$taskProcess.GetExitCode();$taskProcess.Close()
 foreach($taskFile in @('video-pipeline-smoke.plist','video-backend.plist')){
  $taskFiles.ReceiveFile("$taskRemote\$taskFile","$taskOut/$taskFile",$true)
 }
 Get-Content "$taskOut/video-pipeline-smoke.plist"
 if($taskCode -ne 0){throw "Smoke failed: $taskCode"}
}finally{$taskDevice.Disconnect()}
