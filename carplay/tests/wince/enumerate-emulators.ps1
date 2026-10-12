$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../../scripts/environment.ps1"
Import-WinXPlayCoreCon
$ds=New-Object Microsoft.SmartDevice.Connectivity.DatastoreManager(1033)
foreach($p in $ds.GetPlatforms()){ foreach($d in $p.GetDevices()){ [pscustomobject]@{Platform=$p.Name;Device=$d.Name;Emulator=$d.IsEmulator()} }}
