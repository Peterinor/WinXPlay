param([switch]$Test,[switch]$CheckArm,[string]$OutputDirectory,[switch]$NoLogs)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
Assert-WinXPlayEnvironment @('CARPLAY_VC','CARPLAY_SDK')
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskVc = $env:CARPLAY_VC
$taskSdk = $env:CARPLAY_SDK
$env:PATH = "$taskVc/bin/amd64;$taskVc/../Common7/IDE;" + $env:PATH
$env:INCLUDE = "$taskVc/include;$taskSdk/Include"
$env:LIB = "$taskVc/lib/amd64;$taskSdk/Lib/x64"
$taskBuild = if($OutputDirectory){[IO.Path]::GetFullPath($OutputDirectory)}else{Join-Path $taskRoot '../build/carplay/windows-x64'}
if($NoLogs -and !$OutputDirectory){$taskBuild+='-nologs'}
New-Item -ItemType Directory -Force $taskBuild | Out-Null
# Source encoding is prepared during migration; builds do not rewrite source files.
Push-Location $taskBuild
try {
    $taskCommon = @('/nologo','/EHsc','/MT','/W4','/O2','/DUNICODE','/D_UNICODE','/DNOMINMAX','/D_WIN32_WINNT=0x0600','/D_CRT_SECURE_NO_WARNINGS')
    if($NoLogs){$taskCommon+='/DWINXPLAY_ENABLE_LOGS=0'}
    $taskSystemMedia=& "$PSScriptRoot/build-system-media.ps1"
    $taskCommon+='/DCARPLAY_HAS_MF'
    $taskMediaSources=@($taskSystemMedia.Objects)+@($taskSystemMedia.Adapter)
    $taskMediaLibs=$taskSystemMedia.Libs
    $taskSources = @("$taskRoot/core/protocol.cpp","$taskRoot/core/plist.cpp","$taskRoot/core/link.cpp","$taskRoot/core/auth.cpp","$taskRoot/platform/windows/crypto.cpp","$taskRoot/platform/windows/platform.cpp","$taskRoot/platform/common/lockdown.cpp")
    # Distinct /Fo prevents core/auth.cpp and windows/auth.cpp object-name collision.
    & "$taskVc/bin/amd64/cl.exe" @taskCommon /c "$taskRoot/platform/windows/auth.cpp" '/Fowindows_auth.obj'
    if ($LASTEXITCODE -ne 0) { throw 'Native accessory signer build failed' }
    $taskSources += 'windows_auth.obj'
    $taskSources += @("$taskRoot/core/identification.cpp","$taskRoot/core/input.cpp")
    $taskSources += "$taskRoot/core/buffered_audio.cpp"
    $taskSources += @("$taskRoot/core/airinfo.cpp","$taskRoot/platform/windows/aircrypto.cpp","$taskRoot/core/pairing.cpp","$taskRoot/platform/windows/live_media.cpp","$taskRoot/core/receiver.cpp","$taskRoot/platform/windows/usb.cpp")
    $taskSources += @("$taskRoot/platform/common/receiver_context.cpp", "$taskRoot/devices/generic/profile.cpp")
    $taskSources += $taskMediaSources
    & "$taskVc/bin/amd64/cl.exe" @taskCommon @taskSources "$taskRoot/platform/windows/main.cpp" '/FeWinXPlay.exe' /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib ws2_32.lib winmm.lib shell32.lib crypt32.lib setupapi.lib iphlpapi.lib advapi32.lib @taskMediaLibs
    if ($LASTEXITCODE -ne 0) { throw 'Desktop build failed' }
    & $env:MRN_PYTHON "$PSScriptRoot/stage-windows-runtime.py" --output $taskBuild
    if ($LASTEXITCODE -ne 0) { throw 'Windows runtime preparation failed' }
    if ($Test) {
        & "$taskVc/bin/amd64/cl.exe" @taskCommon @taskSources "$taskRoot/tests/windows/test.cpp" '/Fecarplay_tests.exe' /link ws2_32.lib user32.lib gdi32.lib winmm.lib crypt32.lib setupapi.lib iphlpapi.lib advapi32.lib @taskMediaLibs
        if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
        & './carplay_tests.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Native tests failed' }
        & "$taskVc/bin/amd64/cl.exe" @taskCommon @taskSources "$taskRoot/tests/windows/input_host.cpp" '/Feinput_host.exe' /link ws2_32.lib user32.lib gdi32.lib winmm.lib crypt32.lib setupapi.lib iphlpapi.lib advapi32.lib @taskMediaLibs
        if ($LASTEXITCODE -ne 0) { throw 'Input integration host build failed' }

    }
    if ($CheckArm) {
        $taskCeSdk = $env:CARPLAY_CE_SDK
        $taskCeInclude = "$taskCeSdk/Include/Armv4i"
        $env:INCLUDE = "$taskVc/ce/include;$taskCeInclude"
        & "$taskVc/ce/bin/x86_arm/cl.exe" /nologo /c /EHsc /W4 /O1 /D_WIN32_WCE=0x500 /DUNDER_CE /DARM /D_ARM_ /D_CRT_SECURE_NO_WARNINGS "/Fo$taskBuild/arm_protocol.obj" "$taskRoot/core/protocol.cpp"
        if ($LASTEXITCODE -ne 0) { throw 'ARM protocol compile failed' }
        & "$taskVc/ce/bin/x86_arm/cl.exe" /nologo /c /EHsc /W4 /O1 /D_WIN32_WCE=0x500 /DUNDER_CE /DARM /D_ARM_ /D_CRT_SECURE_NO_WARNINGS "/Fo$taskBuild/arm_plist.obj" "$taskRoot/core/plist.cpp"
        if ($LASTEXITCODE -ne 0) { throw 'ARM plist compile failed' }
        & "$taskVc/ce/bin/x86_arm/cl.exe" /nologo /c /EHsc /W4 /O1 /D_WIN32_WCE=0x500 /DUNDER_CE /DARM /D_ARM_ /D_CRT_SECURE_NO_WARNINGS "/Fo$taskBuild/arm_link.obj" "$taskRoot/core/link.cpp"
        if ($LASTEXITCODE -ne 0) { throw 'ARM link engine compile failed' }
        & "$taskVc/ce/bin/x86_arm/cl.exe" /nologo /c /EHsc /W4 /O1 /D_WIN32_WCE=0x500 /DUNDER_CE /DARM /D_ARM_ /D_CRT_SECURE_NO_WARNINGS "/Fo$taskBuild/arm_auth.obj" "$taskRoot/core/auth.cpp"
        if ($LASTEXITCODE -ne 0) { throw 'ARM authentication compile failed' }
        foreach($taskPortable in @('identification','airinfo','input')) {
            & "$taskVc/ce/bin/x86_arm/cl.exe" /nologo /c /EHsc /W4 /O1 /D_WIN32_WCE=0x500 /DUNDER_CE /DARM /D_ARM_ /D_CRT_SECURE_NO_WARNINGS "/Fo$taskBuild/arm_$taskPortable.obj" "$taskRoot/core/$taskPortable.cpp"
            if ($LASTEXITCODE -ne 0) { throw "ARM $taskPortable compile failed" }
        }
    }
} finally { Pop-Location }
