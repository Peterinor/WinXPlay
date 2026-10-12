$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../../scripts/environment.ps1"
$taskRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$taskOut=Join-Path $taskRoot '../build/carplay/ce-bridge-test'
Import-WinXPlayCoreCon
$taskStore=New-Object Microsoft.SmartDevice.Connectivity.DatastoreManager(1033)
$taskDevice=@($taskStore.GetPlatforms() | ForEach-Object {$_.GetDevices()} | Where-Object {$_.Name -eq $env:WINXPLAY_EMULATOR_DEVICE})[0]
$taskDevice.Connect()
try {
 $taskFiles=$taskDevice.GetFileDeployer();$taskRemote='\NAND_USER\WinXPlay'
 $taskExe='WinXPlay.exe'
 $taskRepo=Split-Path $taskRoot -Parent
 $taskFiles.SendFile("$taskRepo/build/firmware/ab_bridge/WinXPlayBridge.dll","$taskRemote\WinXPlayBridge.dll",$true,$false)
 $taskFiles.SendFile("$taskRoot/../build/carplay/ce-wm5-arm-mrn/WinXPlay.exe","$taskRemote\$taskExe",$true,$false)
 $taskFiles.SendFile("$taskOut/WINXPLAY_BRIDGE_SMOKE.exe","$taskRemote\WINXPLAY_BRIDGE_SMOKE.exe",$true,$false)
 foreach($taskMode in @('','existing')){
 $taskProcess=$taskDevice.GetRemoteProcess()
 if(!$taskProcess.Start("$taskRemote\WINXPLAY_BRIDGE_SMOKE.exe",$taskMode)){throw 'Bridge smoke launch failed'}
 $taskUntil=[DateTime]::UtcNow.AddSeconds(180)
 while(!$taskProcess.HasExited() -and [DateTime]::UtcNow -lt $taskUntil){Start-Sleep -Milliseconds 200}
 if(!$taskProcess.HasExited()){throw "Bridge smoke timed out in $taskRemote"}
 $taskCode=$taskProcess.GetExitCode();$taskProcess.Close()
 foreach($taskFile in @('bridge-smoke.plist','auto-connect.plist')){$taskFiles.ReceiveFile("$taskRemote\$taskFile","$taskOut/$taskFile",$true)}
 Get-Content "$taskOut/bridge-smoke.plist"
 if($taskCode -ne 0){throw "Bridge smoke failed ($taskMode): $taskCode"}
 $taskLabel=if($taskMode){$taskMode}else{'new'}
 Copy-Item "$taskOut/bridge-smoke.plist" "$taskOut/bridge-smoke-$taskLabel.plist" -Force
 Start-Sleep -Milliseconds 300
 }
}finally{$taskDevice.Disconnect()}
