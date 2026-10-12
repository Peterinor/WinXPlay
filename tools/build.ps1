param([Alias('Tool')][string]$Scenario, [switch]$List, [switch]$CompileOnly, [string]$Output, [string]$Python)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../scripts/environment.ps1"
if (!$Python) { $Python=$env:MRN_PYTHON }
$taskRoot = (Resolve-Path "$PSScriptRoot/..").Path
if ($List -or !$Scenario) {
    [pscustomobject]@{scenario='file_ops'; title='File operations'; exe_count=1; start_exe='file_ops.exe'}
    [pscustomobject]@{scenario='system_ops'; title='System operations'; exe_count=1; start_exe='system_ops.exe'}
    & "$taskRoot/scripts/build-scenarios.ps1" -List
    return
}
if ($Scenario -eq 'all' -and $Output) { throw '-Output requires one tool.' }
if ($Scenario -in @('all','file_ops')) {
    & "$PSScriptRoot/file_ops/build.ps1" -CompileOnly:$CompileOnly -Output $Output -Python $Python
    if ($Scenario -eq 'file_ops') { return }
}
if ($Scenario -in @('all','system_ops')) {
    & "$PSScriptRoot/system_ops/build.ps1" -CompileOnly:$CompileOnly -Output $Output -Python $Python
    if ($Scenario -eq 'system_ops') { return }
}
& "$taskRoot/scripts/build-scenarios.ps1" -Scenario $Scenario -CompileOnly:$CompileOnly -Output $Output -Python $Python
