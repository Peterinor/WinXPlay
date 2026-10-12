param(
    [ValidateSet('Windows','CeHost','CeMrn','CeGeneric','Tools','Emulator','Firmware')]
    [string]$Target='Windows',
    [switch]$List,
    [switch]$Test
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/scripts/environment.ps1"
if ($List -and $Target -ne 'Tools') { throw '-List is only available for Tools.' }
if ($Test -and $Target -ne 'Windows') { throw '-Test currently selects the Windows native tests; see docs/testing.md for other targets.' }
switch ($Target) {
    'Tools' { & "$PSScriptRoot/tools/build.ps1" -Scenario all -List:$List -CompileOnly }
    'Emulator' { & "$PSScriptRoot/emulator/build.ps1" }
    'Firmware' { & "$PSScriptRoot/firmware/mrn/build.ps1" }
    default { & "$PSScriptRoot/carplay/build.ps1" -Target $Target -Test:$Test }
}
