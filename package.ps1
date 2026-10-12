param(
    [ValidateSet('Tools','Firmware','Receiver','All')]
    [string]$Target='Tools',
    [switch]$NoLogs,
    [string]$Identity
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/scripts/environment.ps1"
if (!$Identity) { $Identity=$env:CARPLAY_IDENTITY }
$packageArguments=@("$PSScriptRoot/scripts/package_all.py", '--target', $Target)
if ($NoLogs) { $packageArguments+='--no-logs' }
if ($Identity) { $packageArguments+=@('--identity', $Identity) }
& $env:MRN_PYTHON @packageArguments
if ($LASTEXITCODE -ne 0) { throw 'Packaging failed. See the error above; the full set is not complete.' }
