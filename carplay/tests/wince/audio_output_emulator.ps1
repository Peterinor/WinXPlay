$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../../scripts/environment.ps1"
$taskRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/ce-audio-output-test'
Import-WinXPlayCoreCon
$taskStore=New-Object Microsoft.SmartDevice.Connectivity.DatastoreManager(1033)
$taskDevice=@($taskStore.GetPlatforms() | ForEach-Object {$_.GetDevices()} | Where-Object {$_.Name -eq $env:WINXPLAY_EMULATOR_DEVICE})[0]
$taskDevice.Connect()
try {
 $taskFiles=$taskDevice.GetFileDeployer();$taskRemote='\MRN_AUDIO_'+[DateTime]::UtcNow.ToString('yyyyMMddHHmmss')
 $taskFiles.SendFile("$taskOut/MRN_AUDIO_OUTPUT_SMOKE.exe","$taskRemote\MRN_AUDIO_OUTPUT_SMOKE.exe",$true,$false)
 $taskProcess=$taskDevice.GetRemoteProcess()
 if(!$taskProcess.Start("$taskRemote\MRN_AUDIO_OUTPUT_SMOKE.exe",'')){throw 'Audio smoke launch failed'}
 $taskUntil=[DateTime]::UtcNow.AddSeconds(240)
 while(!$taskProcess.HasExited() -and [DateTime]::UtcNow -lt $taskUntil){Start-Sleep -Milliseconds 200}
 if(!$taskProcess.HasExited()){throw "Audio smoke timed out in $taskRemote"}
 $taskCode=$taskProcess.GetExitCode();$taskProcess.Close()
 $taskFiles.ReceiveFile("$taskRemote\audio-output-smoke.plist","$taskOut/audio-output-smoke.plist",$true)
 Get-Content "$taskOut/audio-output-smoke.plist"
 if($taskCode -ne 0){throw "Audio smoke failed: $taskCode"}
 foreach($taskFile in @('audio-recovery.plist','audio-recovery-report.plist')){
  $taskFiles.ReceiveFile("$taskRemote\$taskFile","$taskOut/$taskFile",$true)
 }
}finally{$taskDevice.Disconnect()}
