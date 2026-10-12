param(
    [ValidateSet('Windows','CeHost','CeMrn','CeGeneric')][string]$Target='Windows',
    [switch]$Test, [switch]$CheckArm,
    [switch]$CryptoOnly, [switch]$SelftestOnly,
    [string]$OutputDirectory,
    [string]$Python=$env:MRN_PYTHON
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../scripts/environment.ps1"
if (!$Python) { $Python=$env:MRN_PYTHON }
if($Target -eq 'Windows') {
    if($CryptoOnly -or $SelftestOnly){throw 'CryptoOnly and SelftestOnly apply to CE targets only.'}
    if($Test){
        if($OutputDirectory){throw 'Test with custom OutputDirectory requires explicitly generated fixtures; use the platform build script.'}
        & $Python "$PSScriptRoot/tests/windows/fixtures.py"
        if($LASTEXITCODE -ne 0){throw 'Fixture generation failed'}
    }
    & "$PSScriptRoot/scripts/build-windows.ps1" -Test:$Test -CheckArm:$CheckArm -OutputDirectory $OutputDirectory
} else {
    if($Test -or $CheckArm -or $OutputDirectory){throw 'CE targets use verify.ps1 and their isolated output profiles; Windows-only build parameters were supplied.'}
    if($CryptoOnly -and $SelftestOnly){throw 'Select CryptoOnly or SelftestOnly, not both.'}
    & "$PSScriptRoot/scripts/build-wince.ps1" -Desktop:($Target -eq 'CeHost') -Generic:($Target -eq 'CeGeneric') -CryptoOnly:$CryptoOnly -SelftestOnly:$SelftestOnly
}
