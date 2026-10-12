param([string]$Root=(Split-Path (Split-Path $PSScriptRoot -Parent) -Parent))
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../../scripts/environment.ps1"
Import-WinXPlayCoreCon
$ds=New-Object Microsoft.SmartDevice.Connectivity.DatastoreManager(1033)
$d=@($ds.GetPlatforms() | ForEach-Object {$_.GetDevices()} | Where-Object {$_.Name -eq $env:WINXPLAY_EMULATOR_DEVICE})[0]
$d.Connect()
try {
 $files=$d.GetFileDeployer(); $files.SendFile("$Root\..\build\carplay\ce-wm5-arm-mrn\MRN_CARPLAY_SELFTEST.exe",'\MRN_CARPLAY_SELFTEST.exe',$true,$false)
 $p=$d.GetRemoteProcess();if(!$p.Start('\MRN_CARPLAY_SELFTEST.exe','')){throw 'ARM selftest launch failed'}
 $deadline=[DateTime]::UtcNow.AddSeconds(45)
 while(!$p.HasExited() -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 250}
 if(!$p.HasExited()){throw 'ARM selftest timed out'}
 $code=$p.GetExitCode();$p.Close()
 $files.ReceiveFile('\ce-selftest.plist',"$Root\..\build\carplay\ce-wm5-arm-mrn\emulator-selftest.plist",$true)
 Write-Output "ARM emulator selftest exit: $code"
 if($code -ne 0){throw 'ARM selftest failed'}
 & $env:MRN_PYTHON "$Root/tests/wince/emulator_fixtures.py"
 if($LASTEXITCODE -ne 0){throw 'Synthetic emulator fixture generation failed'}
 $files.SendFile("$Root\..\build\carplay\ce-wm5-arm-mrn\WinXPlay.exe",'\MRN_CE_TEST\WinXPlay.exe',$true,$false)
 foreach($name in @('certificate.p7b','certificate.der','identity.pk8')){
  $files.SendFile("$Root\..\build\carplay\ce-wm5-arm-mrn\emulator-fixtures\$name","\MRN_CE_TEST\identity\$name",$true,$false)
 }
 $p=$d.GetRemoteProcess();if(!$p.Start('\MRN_CE_TEST\WinXPlay.exe','--serve-test 17123 2 800 480')){throw 'ARM receiver launch failed'}
 $deadline=[DateTime]::UtcNow.AddSeconds(20)
 while(!$p.HasExited() -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 250}
 if(!$p.HasExited()){throw 'ARM receiver startup timed out'}
 $code=$p.GetExitCode();$p.Close()
 if($code -ne 0){$files.ReceiveFile('\MRN_CE_TEST\startup-error.plist',"$Root\..\build\carplay\ce-wm5-arm-mrn\emulator-startup-error.plist",$true);throw 'ARM receiver startup failed'}
 $files.ReceiveFile('\MRN_CE_TEST\receiver-listener.plist',"$Root\..\build\carplay\ce-wm5-arm-mrn\emulator-receiver-listener.plist",$true)
 Write-Output "ARM receiver listener exit: $code"
 $files.SendFile("$Root\..\build\carplay\vehicle-tests\winxplay_test_av.exe",'\MRN_CE_TEST\winxplay_test_av.exe',$true,$false)
 $p=$d.GetRemoteProcess();if(!$p.Start('\MRN_CE_TEST\winxplay_test_av.exe','inspect')){throw 'Media probe launch failed'}
 $deadline=[DateTime]::UtcNow.AddSeconds(20)
 while(!$p.HasExited() -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 250}
 if(!$p.HasExited()){throw 'Media probe timed out'}
 $code=$p.GetExitCode();$p.Close()
 $files.ReceiveFile('\MRN_CE_TEST\media-probe.plist',"$Root\..\build\carplay\ce-wm5-arm-mrn\emulator-media-probe.plist",$true)
 if($code -ne 0){throw 'Media probe failed'}
 Write-Output "ARM media probe exit: $code"
} finally {$d.Disconnect()}
