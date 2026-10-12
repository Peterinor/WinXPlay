# Shared compiler settings for WinCE application binaries (not a BSP build).
function Build-WinceExe {
    param(
        [Parameter(Mandatory=$true)][string[]]$Sources,
        [Parameter(Mandatory=$true)][string]$Entry,
        [Parameter(Mandatory=$true)][string]$Name,
        [Parameter(Mandatory=$true)][string]$OutputDirectory
    )
    $ErrorActionPreference = 'Stop'
    . "$PSScriptRoot/environment.ps1"
    Assert-WinXPlayEnvironment @('MRN_CE_VC','MRN_CE_SDK')
    $taskVc = if ($env:MRN_CE_VC) { $env:MRN_CE_VC } else { $env:CARPLAY_VC }
    $taskSdk = if ($env:MRN_CE_SDK) { $env:MRN_CE_SDK } else { $env:CARPLAY_CE_SDK }
    $taskCl = Join-Path $taskVc 'ce\bin\x86_arm\cl.exe'
    $taskLink = Join-Path $taskVc 'ce\bin\x86_arm\link.exe'
    foreach ($required in @($taskCl, $taskLink, "$taskSdk\Lib\ARMV4I\coredll.lib")) {
        if (!(Test-Path -LiteralPath $required)) { throw "Missing WinCE build dependency: $required" }
    }
    $priorPath = $env:PATH
    try {
        $taskIde = Join-Path (Split-Path $taskVc -Parent) 'Common7\IDE'
        $env:PATH = "$taskVc\bin;$taskIde;" + $priorPath
        New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
        $objects = @()
        $index = 0
        foreach ($source in $Sources) {
            $source = (Resolve-Path -LiteralPath $source).Path
            $sourceDir = Split-Path $source -Parent
            foreach ($header in Get-ChildItem -LiteralPath $sourceDir -Filter '*.h' -File) {
                [IO.File]::WriteAllText((Join-Path $OutputDirectory $header.Name), [IO.File]::ReadAllText($header.FullName), [Text.UTF8Encoding]::new($true))
            }
            $stem = [IO.Path]::GetFileNameWithoutExtension($source)
            $temporary = Join-Path $OutputDirectory "$Name-$index-$stem.cpp"
            # Old compiler needs a BOM; never rewrite original source files.
            [IO.File]::WriteAllText($temporary, [IO.File]::ReadAllText($source), [Text.UTF8Encoding]::new($true))
            $object = Join-Path $OutputDirectory "$Name-$index-$stem.obj"
            & $taskCl /nologo /c /O1 /GS- /GR- /Zl /DUNICODE /D_UNICODE /D_WIN32_WCE=0x500 /DUNDER_CE /DWIN32_PLATFORM_PSPC /DARM /D_ARM_ /I "$taskSdk\Include\Armv4i" /I "$taskVc\ce\include" /I $sourceDir /I $OutputDirectory "/Fo$object" $temporary
            if ($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
            $objects += $object
            $index++
        }
        & $taskLink /nologo /NODEFAULTLIB "/ENTRY:$Entry" /SUBSYSTEM:WINDOWSCE,5.00 /MACHINE:THUMB "/OUT:$OutputDirectory\$Name.exe" @objects "$taskSdk\Lib\ARMV4I\coredll.lib"
        if ($LASTEXITCODE -ne 0) { throw "Link failed: $Name" }
    } finally { $env:PATH = $priorPath }
}
