param([switch]$NoLogs, [switch]$WithPatch)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/environment.ps1"
Assert-WinXPlayEnvironment @('CARPLAY_VC','CARPLAY_CE_SDK')
$taskRoot=(Resolve-Path "$PSScriptRoot/../..").Path
. "$taskRoot/scripts/wince-toolchain.ps1"
$taskOut="$taskRoot/build/firmware/ab_bridge"
if($NoLogs){$taskOut+='-nologs'}
$taskLogFlags=@();if($NoLogs){$taskLogFlags+='/DWINXPLAY_ENABLE_LOGS=0'}
New-Item -ItemType Directory -Force $taskOut | Out-Null
$taskVc=$env:CARPLAY_VC
$taskSdk=$env:CARPLAY_CE_SDK
$priorPath=$env:PATH
try {
 $env:PATH="$taskVc/bin;$taskVc/../Common7/IDE;"+$env:PATH
 & "$taskVc/ce/bin/x86_arm/cl.exe" @taskLogFlags /nologo /c /O1 /GS- /GR- /Zl /DUNICODE /D_UNICODE /D_WIN32_WCE=0x500 /DUNDER_CE /DARM /D_ARM_ /DWIN32_PLATFORM_PSPC /I "$taskSdk/Include/Armv4i" /I "$taskVc/ce/include" "/Fo$taskOut/bridge.obj" "$PSScriptRoot/bridge/bridge.cpp"
 if($LASTEXITCODE -ne 0){throw 'Bridge compile failed'}
 & "$taskVc/ce/bin/x86_arm/link.exe" /nologo /DLL /NODEFAULTLIB /ENTRY:DllMain /MACHINE:THUMB /SUBSYSTEM:WINDOWSCE,5.00 "/OUT:$taskOut/WinXPlayBridge.dll" "$taskOut/bridge.obj" "$taskSdk/Lib/ARMV4I/coredll.lib"
 if($LASTEXITCODE -ne 0){throw 'Bridge link failed'}
}finally{$env:PATH=$priorPath}
if ($WithPatch) {
    & $env:MRN_PYTHON "$PSScriptRoot/hmi_patch/build_ab_patch.py"
    if($LASTEXITCODE -ne 0){throw 'HMI patch build failed'}
}
