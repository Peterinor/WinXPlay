param([switch]$CompileOnly, [string]$Output, [string]$Python)
$ErrorActionPreference = "Stop"
& "$PSScriptRoot/../build.ps1" -Scenario (Split-Path $PSScriptRoot -Leaf) -CompileOnly:$CompileOnly -Output $Output -Python $Python
