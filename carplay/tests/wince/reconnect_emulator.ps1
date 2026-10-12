param([string]$Installed='False')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../../scripts/environment.ps1"
$taskRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/ce-reconnect-test'
Import-WinXPlayCoreCon
$taskStore=New-Object Microsoft.SmartDevice.Connectivity.DatastoreManager(1033)
$taskDevice=@($taskStore.GetPlatforms() | ForEach-Object {$_.GetDevices()} | Where-Object {$_.Name -eq $env:WINXPLAY_EMULATOR_DEVICE})[0]
$taskDevice.Connect()
try {
 $taskFiles=$taskDevice.GetFileDeployer();$taskRemote='\MRN_RECONNECT_'+[DateTime]::UtcNow.ToString('yyyyMMddHHmmss')
 $taskExe='WinXPlay.exe'
 if($Installed -eq 'True'){$taskRemote='\NAND_USER\WinXPlay';$taskExe='WinXPlay.exe'}
 $taskFiles.SendFile("$taskRoot/../build/carplay/ce-wm5-arm-mrn/WinXPlay.exe","$taskRemote\$taskExe",$true,$false)
 $taskFiles.SendFile("$taskOut/MRN_RECONNECT_SMOKE.exe","$taskRemote\MRN_RECONNECT_SMOKE.exe",$true,$false)
 $taskProcess=$taskDevice.GetRemoteProcess()
 if(!$taskProcess.Start("$taskRemote\MRN_RECONNECT_SMOKE.exe",'')){throw 'Reconnect smoke launch failed'}
 $taskUntil=[DateTime]::UtcNow.AddSeconds(180)
 while(!$taskProcess.HasExited() -and [DateTime]::UtcNow -lt $taskUntil){Start-Sleep -Milliseconds 200}
 if(!$taskProcess.HasExited()){throw "Reconnect smoke timed out in $taskRemote"}
 $taskCode=$taskProcess.GetExitCode();$taskProcess.Close()
 foreach($taskFile in @('reconnect-smoke.plist','auto-connect.plist')){$taskFiles.ReceiveFile("$taskRemote\$taskFile","$taskOut/$taskFile",$true)}
 Get-Content "$taskOut/reconnect-smoke.plist"
 if($taskCode -ne 0){throw "Reconnect smoke failed: $taskCode"}
}finally{$taskDevice.Disconnect()}
