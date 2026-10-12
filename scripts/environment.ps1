# Local configuration is optional and never part of the public source package.
$winxplayProjectRoot = Split-Path $PSScriptRoot -Parent
$winxplaySettings = Join-Path $winxplayProjectRoot '.local/settings.ps1'
if (Test-Path -LiteralPath $winxplaySettings) { . $winxplaySettings }
if (!$env:MRN_PYTHON) { $env:MRN_PYTHON = 'python' }
if (!$env:MRN_CE_VC -and $env:CARPLAY_VC) { $env:MRN_CE_VC = $env:CARPLAY_VC }
if (!$env:CARPLAY_VC -and $env:MRN_CE_VC) { $env:CARPLAY_VC = $env:MRN_CE_VC }
if (!$env:MRN_CE_SDK -and $env:CARPLAY_CE_SDK) { $env:MRN_CE_SDK = $env:CARPLAY_CE_SDK }
if (!$env:CARPLAY_CE_SDK -and $env:MRN_CE_SDK) { $env:CARPLAY_CE_SDK = $env:MRN_CE_SDK }
if (!$env:WINXPLAY_EMULATOR_DEVICE) { $env:WINXPLAY_EMULATOR_DEVICE = 'USA Windows Mobile 5.0 Pocket PC R2 Emulator' }

function Get-WinXPlayPowerShell32 {
    $winxplaySystemDirectory=if([Environment]::Is64BitOperatingSystem){'SysWOW64'}else{'System32'}
    $winxplayShell=Join-Path $env:SystemRoot "$winxplaySystemDirectory/WindowsPowerShell/v1.0/powershell.exe"
    if(!(Test-Path -LiteralPath $winxplayShell -PathType Leaf)){throw 'Windows PowerShell x86 is required for the legacy emulator tools.'}
    return $winxplayShell
}

function Import-WinXPlayCoreCon {
    $winxplayCoreCon=$env:WINXPLAY_CORECON_DLL
    if(!$winxplayCoreCon){
        $winxplayCommonFiles=${env:CommonProgramFiles(x86)}
        if(!$winxplayCommonFiles){$winxplayCommonFiles=$env:CommonProgramFiles}
        if($winxplayCommonFiles){$winxplayCoreCon=Join-Path $winxplayCommonFiles 'Microsoft Shared/CoreCon/1.0/Bin/Microsoft.Smartdevice.Connectivity.dll'}
    }
    if(!$winxplayCoreCon -or !(Test-Path -LiteralPath $winxplayCoreCon -PathType Leaf)){
        throw 'Configure WINXPLAY_CORECON_DLL with the installed Microsoft.Smartdevice.Connectivity.dll path; see docs/build.md.'
    }
    [Reflection.Assembly]::LoadFrom($winxplayCoreCon) | Out-Null
}

function Assert-WinXPlayEnvironment {
    param([string[]]$Names)
    foreach ($winxplayName in $Names) {
        $winxplayValue = [Environment]::GetEnvironmentVariable($winxplayName, 'Process')
        if (!$winxplayValue -or !(Test-Path -LiteralPath $winxplayValue)) {
            throw "Configure $winxplayName with an existing path. See docs/build.md and examples/settings.ps1."
        }
    }
}

function Get-WinXPlayModernSdk {
    Assert-WinXPlayEnvironment @('CARPLAY_MODERN_SDK')
    $taskModern=$env:CARPLAY_MODERN_SDK
$taskSdkVersion=$env:CARPLAY_MODERN_SDK_VERSION
if(!$taskSdkVersion){
    $taskVersions=@(Get-ChildItem -LiteralPath "$taskModern/Include" -Directory | Where-Object {
        $_.Name -match '^\d+\.\d+\.\d+\.\d+$' -and (Test-Path -LiteralPath (Join-Path $_.FullName 'um/mfapi.h'))
    } | Sort-Object {[version]$_.Name} -Descending)
    if(!$taskVersions.Count){throw 'No Windows SDK headers found under CARPLAY_MODERN_SDK/Include.'}
    $taskSdkVersion=$taskVersions[0].Name
}
$taskSdkInclude=Join-Path $taskModern "Include/$taskSdkVersion"
foreach($taskHeader in @('um/mfapi.h','um/d3d11.h','shared/sdkddkver.h')){
    if(!(Test-Path -LiteralPath (Join-Path $taskSdkInclude $taskHeader))){throw "Missing Windows SDK header: $taskSdkInclude/$taskHeader"}
}
$taskSdkLib=Join-Path $taskModern "Lib/$taskSdkVersion/um/x64"
if(!(Test-Path -LiteralPath $taskSdkLib)){$taskSdkLib=Join-Path $taskModern 'um/x64'}
    return @{Include=$taskSdkInclude;Library=$taskSdkLib;Version=$taskSdkVersion}
}
