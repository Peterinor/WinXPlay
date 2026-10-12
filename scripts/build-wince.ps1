param([ValidateSet('all','tools')][string]$Target='all')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/environment.ps1"
$taskRoot = Split-Path $PSScriptRoot -Parent
& "$taskRoot/tools/build.ps1" -Scenario all -CompileOnly
