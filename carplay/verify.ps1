param(
    [ValidateSet('Windows','CeHost','CeMrn','CeGeneric','All')][string]$Target='Windows',
    [switch]$SkipBuild,
    [string]$Python=$env:MRN_PYTHON
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../scripts/environment.ps1"
if (!$Python) { $Python=$env:MRN_PYTHON }
& $Python "$PSScriptRoot/tests/check_boundaries.py"
if($LASTEXITCODE -ne 0){throw 'Architecture boundary check failed'}
& "$PSScriptRoot/scripts/verify-contracts.ps1"
if($Target -eq 'Windows' -or $Target -eq 'All') {
    & "$PSScriptRoot/scripts/verify-windows.ps1" -SkipBuild:$SkipBuild -Python $Python
}
if($Target -ne 'Windows') {
    if($Target -eq 'All'){
        foreach($taskTarget in @('CeHost','CeMrn','CeGeneric')){
            & "$PSScriptRoot/scripts/verify-wince.ps1" -Target $taskTarget -SkipBuild:$SkipBuild -Python $Python
        }
    }else{
        & "$PSScriptRoot/scripts/verify-wince.ps1" -Target $Target -SkipBuild:$SkipBuild -Python $Python
    }
}
