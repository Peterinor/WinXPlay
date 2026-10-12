param([string]$Root=(Split-Path (Split-Path $PSScriptRoot -Parent) -Parent))
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../../scripts/environment.ps1"
Import-WinXPlayCoreCon
$taskStore=New-Object Microsoft.SmartDevice.Connectivity.DatastoreManager(1033)
$taskDevice=@($taskStore.GetPlatforms() | ForEach-Object {$_.GetDevices()} | Where-Object {$_.Name -eq $env:WINXPLAY_EMULATOR_DEVICE})[0]
$taskDevice.Connect()
$taskOut=Join-Path $Root '../build/carplay/vehicle-tests'
$taskResults=@()
try {
 $taskFiles=$taskDevice.GetFileDeployer()
 $taskRemote='\MRN_NET_'+[DateTime]::UtcNow.ToString('yyyyMMddHHmmss')
 $taskFiles.SendFile("$taskOut/winxplay_test_connect.exe","$taskRemote\winxplay_test_connect.exe",$true,$false)
 $taskIndex=0
 foreach($taskCase in @(
  @{name='inspect';body='<key>seconds</key><integer>1</integer>';args='';expected=0},
  @{name='loopback';body='';args='--selftest';expected=0},
  @{name='invalid-seconds';body='<key>seconds</key><integer>-1</integer>';args='';expected=1},
  @{name='invalid-peer';body='<key>peer_address</key><string>::1</string><key>adapter_name</key><string>missing</string>';args='';expected=1},
  @{name='missing-adapter';body='<key>seconds</key><integer>1</integer><key>adapter_name</key><string>missing</string><key>peer_address</key><string>fe80::1234</string>';args='';expected=4}
 )) {
  ++$taskIndex
  $taskConfig='<?xml version="1.0"?><plist version="1.0"><dict>'+$taskCase.body+'</dict></plist>'
  [IO.File]::WriteAllText("$taskOut/emulator-config.plist",$taskConfig,(New-Object Text.UTF8Encoding($false)))
  $taskFiles.SendFile("$taskOut/emulator-config.plist","$taskRemote\network.plist",$true,$false)
  $taskProcess=$taskDevice.GetRemoteProcess()
  if(!$taskProcess.Start("$taskRemote\winxplay_test_connect.exe",$taskCase.args)){throw 'Network test launch failed'}
  $taskDeadline=[DateTime]::UtcNow.AddSeconds(20)
  while(!$taskProcess.HasExited() -and [DateTime]::UtcNow -lt $taskDeadline){Start-Sleep -Milliseconds 150}
  if(!$taskProcess.HasExited()){throw "Emulator timeout; preserved logs at $taskRemote"}
  $taskCode=$taskProcess.GetExitCode();$taskProcess.Close()
  foreach($taskExt in @('log','plist')) {
   $taskName='nettest-{0:0000}.{1}' -f $taskIndex,$taskExt
   $taskFiles.ReceiveFile("$taskRemote\$taskName","$taskOut/emulator-$($taskCase.name).$taskExt",$true)
  }
  $taskResults+=@{name=$taskCase.name;exit_code=$taskCode;expected=$taskCase.expected}
  Write-Output "$($taskCase.name): exit=$taskCode expected=$($taskCase.expected)"
  if($taskCode -ne $taskCase.expected){throw 'Unexpected network test exit code'}
 }
 $taskResults | ConvertTo-Json | Set-Content -Encoding UTF8 "$taskOut/emulator-results.json"
}finally{$taskDevice.Disconnect()}
