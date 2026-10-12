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
 $taskBatch='\WinXPlaySuite_'+[DateTime]::UtcNow.ToString('yyyyMMddHHmmss')
 foreach($taskCase in @(
  @{name='input-window';exe='winxplay_test_input.exe';mode='standard';seconds=1;expected=0},
  @{name='input-bridge-timeout';exe='winxplay_test_input.exe';mode='bridge';seconds=1;expected=0},
  @{name='input-invalid-duration';exe='winxplay_test_input.exe';mode='standard';seconds=0;expected=1},
  @{name='input-invalid-mode';exe='winxplay_test_input.exe';mode='invalid';seconds=1;expected=1},
  @{name='av-inspect';exe='winxplay_test_av.exe';mode='inspect';seconds=1;expected=0},
  @{name='av-invalid-mode';exe='winxplay_test_av.exe';mode='invalid';seconds=1;expected=1},
  @{name='connect-invalid-mode';exe='winxplay_test_connect.exe';mode='invalid';seconds=1;expected=1}
 )) {
  $taskRemote=$taskBatch+'\'+$taskCase.name
  $taskFiles.SendFile((Join-Path $taskOut $taskCase.exe),($taskRemote+'\'+$taskCase.exe),$true,$false)
  $taskXml='<?xml version="1.0"?><plist version="1.0"><dict><key>mode</key><string>'+$taskCase.mode+'</string><key>delay_seconds</key><integer>0</integer><key>seconds</key><integer>'+$taskCase.seconds+'</integer></dict></plist>'
  $taskConfig=Join-Path $taskOut 'suite-emulator-config.plist'
  [IO.File]::WriteAllText($taskConfig,$taskXml,(New-Object Text.UTF8Encoding($false)))
  $taskFiles.SendFile($taskConfig,($taskRemote+'\suite.plist'),$true,$false)
  $taskProcess=$taskDevice.GetRemoteProcess()
  if(!$taskProcess.Start(($taskRemote+'\'+$taskCase.exe),'')){throw "Launch failed: $($taskCase.name)"}
  $taskDeadline=[DateTime]::UtcNow.AddSeconds(90)
  while(!$taskProcess.HasExited() -and [DateTime]::UtcNow -lt $taskDeadline){Start-Sleep -Milliseconds 200}
  if(!$taskProcess.HasExited()){throw "Timeout: $($taskCase.name) logs at $taskRemote"}
  $taskCode=$taskProcess.GetExitCode();$taskProcess.Close()
  $taskFiles.ReceiveFile(($taskRemote+'\suite-result.plist'),(Join-Path $taskOut ($taskCase.name+'-result.plist')),$true)
  if($taskCase.name -in @('input-window','input-bridge-timeout')){
   $taskFiles.ReceiveFile(($taskRemote+'\input-0001.log'),(Join-Path $taskOut ($taskCase.name+'.log')),$true)
  }
  $taskResults+=@{name=$taskCase.name;exit_code=$taskCode;expected=$taskCase.expected}
  Write-Output "$($taskCase.name): exit=$taskCode expected=$($taskCase.expected)"
  if($taskCode -ne $taskCase.expected){throw 'Suite case failed'}
 }
 $taskResults | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $taskOut 'suite-emulator-results.json')
}finally{$taskDevice.Disconnect()}
