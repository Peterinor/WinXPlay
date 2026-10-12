param(
    [Alias('Tool')][string]$Scenario,
    [switch]$List,
    [switch]$CompileOnly,
    [string]$Output,
    [string]$Python
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/environment.ps1"
$taskRoot = (Resolve-Path "$PSScriptRoot/..").Path
. "$taskRoot/scripts/wince-toolchain.ps1"
$sourceRoot = Join-Path $taskRoot "tools"
$sharedHeaders = "$taskRoot/common"
$scenarios = @(Get-ChildItem -LiteralPath $sourceRoot -Directory | ForEach-Object {
    $manifest = Join-Path $_.FullName 'scenario.json'
    if (Test-Path -LiteralPath $manifest) {
        Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json
    }
})
if ($List -or !$Scenario) {
    $scenarios | Select-Object scenario,title,@{Name='exe_count';Expression={1 + @($_.companions).Count}},@{Name='start_exe';Expression={$_.main.executable}}
    return
}
if ($Scenario -eq 'all') { $selected = $scenarios }
else { $selected = @($scenarios | Where-Object { $_.scenario -eq $Scenario -or $_.main.tool -eq $Scenario }) }
if (!$selected.Count) { throw "Unknown scenario '$Scenario'. Use -List or run a scenario folder's build.ps1." }
if ($Output -and ($selected.Count -ne 1 -or $CompileOnly)) { throw '-Output requires one scenario and packaging enabled.' }
if (!$Python) { $Python = if ($env:MRN_PYTHON) { $env:MRN_PYTHON } else { 'python' } }

foreach ($scenarioItem in $selected) {
    $folder = Join-Path $sourceRoot $scenarioItem.scenario
    $outputDirectory = Join-Path $taskRoot "build/tools/$($scenarioItem.scenario)"
    $programs = @($scenarioItem.companions) + @($scenarioItem.main)
    $built = @{}
    foreach ($item in $programs) {
        New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
        $sources = @($item.sources | ForEach-Object { Join-Path $folder $_ })
        $name = [IO.Path]::GetFileNameWithoutExtension($item.executable)
        Build-WinceExe -Sources $sources -Entry $item.entry -Name $name -OutputDirectory $outputDirectory
        $binary = Join-Path $outputDirectory $item.executable
        $built[$item.tool] = $binary
        $inputs = @{}
        $sourceInputs = @($sources) + @(Get-ChildItem -LiteralPath $folder -Filter '*.h' -File | ForEach-Object FullName)
        $sourceInputs += @(Get-ChildItem -LiteralPath $sharedHeaders -Filter '*.h' -File | ForEach-Object FullName)
        $sourceInputs += @("$folder/scenario.json", "$folder/build.ps1", "$sourceRoot/build.ps1", "$PSCommandPath", "$taskRoot/scripts/wince-toolchain.ps1")
        foreach ($source in $sourceInputs) {
            $absolute = (Resolve-Path -LiteralPath $source).Path
            $relative = $absolute.Substring($taskRoot.Length + 1).Replace('\','/')
            $inputs[$relative] = (Get-FileHash -LiteralPath $absolute -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        $dependencies = @{}
        foreach ($dependency in $item.dependencies) {
            $dependencies[$dependency] = (Get-FileHash -LiteralPath $built[$dependency] -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        [ordered]@{scenario=$scenarioItem.scenario; tool=$item.tool; executable=$item.executable; sha256=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant(); source_sha256=$inputs; dependency_sha256=$dependencies; vehicle_tested=$false} |
            ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputDirectory "$name.build.json") -Encoding UTF8
    }
    Write-Host "Built $($scenarioItem.scenario): $($programs.Count) EXE -> $outputDirectory"
    if (!$CompileOnly) {
        $packageOutput = $Output
        if (!$packageOutput) {
            $packageOutput = Join-Path $taskRoot ("build/packages/" + [IO.Path]::GetFileNameWithoutExtension($scenarioItem.main.executable))
        }
        & $Python "$taskRoot/scripts/package_diagnostics.py" --scenario $scenarioItem.scenario --output $packageOutput
        if ($LASTEXITCODE -ne 0) { throw "Package validation failed: $($scenarioItem.scenario)" }
        Write-Host "USB package: $packageOutput"
    }
}
