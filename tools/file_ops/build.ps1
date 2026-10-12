param([switch]$CompileOnly, [string]$Output, [string]$Python)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
if (!$Python) { $Python=$env:MRN_PYTHON }
$taskRoot = (Resolve-Path "$PSScriptRoot/../..").Path
. "$taskRoot/scripts/wince-toolchain.ps1"
Build-WinceExe -Sources "$PSScriptRoot/main.cpp" -Entry FileOpsEntry -Name file_ops -OutputDirectory "$taskRoot/build/tools/file_ops"
$binary = Join-Path $taskRoot 'build/tools/file_ops/file_ops.exe'
$inputs = @{}
foreach ($relative in @('tools/file_ops/main.cpp','tools/file_ops/config_policy.h','tools/file_ops/progress_window.h','tools/file_ops/build.ps1','scripts/build-wince.ps1','scripts/wince-toolchain.ps1')) {
    $inputs[$relative] = (Get-FileHash -LiteralPath (Join-Path $taskRoot $relative) -Algorithm SHA256).Hash.ToLowerInvariant()
}
[ordered]@{version='2.9'; execution_order='sequential_continue_on_error'; sha256=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant(); source_sha256=$inputs} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath ([IO.Path]::ChangeExtension($binary,'.build.json')) -Encoding UTF8
if (!$CompileOnly) {
    if (!$Python) { $Python = if ($env:MRN_PYTHON) { $env:MRN_PYTHON } else { 'python' } }
    if (!$Output) { $Output = Join-Path $taskRoot 'build/packages/file_ops' }
    & $Python "$PSScriptRoot/package.py" --output $Output
    if ($LASTEXITCODE -ne 0) { throw 'File manager package validation failed.' }
    Write-Host "USB package: $Output"
}
