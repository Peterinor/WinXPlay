param([string]$Root=(Split-Path (Split-Path $PSScriptRoot -Parent) -Parent),
      [string]$ReportPrefix='emulator',
      [string[]]$Modes=@('rgb','cancel','rgb60','cancel60','audio','audio-probe','audio-probe-cancel','invalid-probe','invalid-background','delayed-device0','invalid-device','invalid-delay','invalid-duration','missing-decoder','invalid-config'))
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
 # Each invocation gets new remote folders, preserving previous logs.
 $taskBatch=[DateTime]::UtcNow.ToString('yyyyMMddHHmmss')
 $taskRemote="\MRN_AV_$taskBatch"
 Write-Output "Remote test directory: $taskRemote"
 $taskRemote | Set-Content -Encoding UTF8 "$taskOut/emulator-remote-path.txt"
 foreach($taskName in @('winxplay_test_av.exe','video.avt','rgb.avt')){
  Write-Output "Deploy $taskName"
  $taskFiles.SendFile("$taskOut/$taskName","$taskRemote\$taskName",$true,$false)
 }
 $taskRunIndex=0
 foreach($taskMode in $Modes){
  ++$taskRunIndex
  Write-Output "Run $taskMode"
  $taskConfigMode=if($taskMode -in @('audio','delayed-device0','invalid-device','invalid-delay')){'audio'}elseif($taskMode -eq 'invalid-config'){'invalid'}else{'video'}
  $taskExtra=if($taskMode -eq 'delayed-device0'){'<key>start_delay_seconds</key><integer>2</integer><key>wave_device</key><integer>0</integer>'}elseif($taskMode -eq 'invalid-device'){'<key>wave_device</key><integer>9999</integer>'}elseif($taskMode -eq 'invalid-delay'){'<key>start_delay_seconds</key><integer>61</integer>'}else{''}
  if($taskMode -eq 'invalid-duration'){$taskExtra='<key>playback_seconds</key><integer>61</integer>'}
  if($taskMode -in @('audio-probe','audio-probe-cancel','invalid-probe')){
   $taskConfigMode='audio'
   $taskSeconds=if($taskMode -eq 'audio-probe'){22}elseif($taskMode -eq 'invalid-probe'){-8}else{8}
   $taskExtra="<key>audio_probe_seconds</key><integer>$taskSeconds</integer><key>wave_device</key><integer>0</integer><key>background</key><true/>"
  }
  if($taskMode -eq 'invalid-background'){$taskExtra='<key>background</key><true/>'}
  if($taskMode -in @('route-unavailable','route-invalid-device','route-missing-helper')){
   $taskConfigMode='audio'
   $taskRouteDevice=if($taskMode -eq 'route-invalid-device'){1}else{0}
   $taskPrepare=if($taskMode -eq 'route-missing-helper'){'<true/>'}else{'<false/>'}
   $taskExtra="<key>usb_route_test</key><true/><key>run_processops</key>$taskPrepare<key>wave_device</key><integer>$taskRouteDevice</integer><key>background</key><true/>"
  }
  $taskConfig="<?xml version=`"1.0`"?><plist version=`"1.0`"><dict><key>mode</key><string>$taskConfigMode</string>$taskExtra</dict></plist>"
  $taskConfigPath=Join-Path $taskOut "$ReportPrefix-$taskMode-config.plist"
  [IO.File]::WriteAllText($taskConfigPath,$taskConfig,[Text.Encoding]::UTF8)
  $taskFiles.SendFile($taskConfigPath,"$taskRemote\test.plist",$true,$false)
  $taskArgs=if($taskMode -eq 'rgb'){'--selftest'}elseif($taskMode -eq 'cancel'){'--selftest-cancel'}elseif($taskMode -eq 'rgb60'){'--selftest-long'}elseif($taskMode -eq 'cancel60'){'--selftest-long-cancel'}else{''}
  if($taskMode -eq 'audio-probe-cancel'){$taskArgs='--selftest-audio-probe-cancel'}
  $taskProcess=$taskDevice.GetRemoteProcess()
  if(!$taskProcess.Start("$taskRemote\winxplay_test_av.exe",$taskArgs)){throw "Launch failed: $taskMode"}
  # The legacy ARM emulator can advance its guest clock much slower than host
  # wall time. This host harness budget does not change the guest's 60 s clip.
  $taskDeadline=[DateTime]::UtcNow.AddSeconds($(if($taskMode -in @('rgb60','audio-probe')){900}else{240}))
  while(!$taskProcess.HasExited() -and [DateTime]::UtcNow -lt $taskDeadline){Start-Sleep -Milliseconds 200}
  if(!$taskProcess.HasExited()){
   $taskRemoteLog='avtest-{0:0000}.log' -f $taskRunIndex
   $taskFiles.ReceiveFile("$taskRemote\$taskRemoteLog","$taskOut/$ReportPrefix-$taskMode-timeout.log",$true)
   throw "Emulator timed out: $taskMode"
  }
  $taskCode=$taskProcess.GetExitCode();$taskProcess.Close()
  foreach($taskExt in @('log','plist')){
   $taskRemoteLog='avtest-{0:0000}.{1}' -f $taskRunIndex,$taskExt
   $taskFiles.ReceiveFile("$taskRemote\$taskRemoteLog","$taskOut/$ReportPrefix-$taskMode.$taskExt",$true)
  }
  $taskResults+=@{mode=$taskMode;exit_code=$taskCode}
  Write-Output "$taskMode exit=$taskCode"
  $taskExpected=if($taskMode -in @('route-unavailable','route-invalid-device','route-missing-helper','missing-decoder','invalid-config','invalid-device','invalid-delay','invalid-duration','invalid-probe','invalid-background')){1}else{0}
  if($taskCode -ne $taskExpected){throw "Unexpected exit code: $taskMode ($taskCode)"}
 }
 $taskResults | ConvertTo-Json | Set-Content -Encoding UTF8 "$taskOut/$ReportPrefix-results.json"
}finally{$taskDevice.Disconnect()}
