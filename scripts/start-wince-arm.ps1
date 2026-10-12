param([string]$EnvironmentRoot = $env:WINXPLAY_EMULATOR)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/environment.ps1"
if (!$EnvironmentRoot) { $EnvironmentRoot=$env:WINXPLAY_EMULATOR }
if (!$EnvironmentRoot) { throw 'Set WINXPLAY_EMULATOR or pass -EnvironmentRoot; see docs/build.md.' }

# 启动真实 Windows Mobile 5 / CE ARM 镜像；此环境不模拟 MRN 硬件。
$emulatorExe = Join-Path $EnvironmentRoot 'runtime\DeviceEmulator.exe'
$romImage = Join-Path $EnvironmentRoot 'images\PPC_USA_WM5.bin'
$sharedDirectory = Join-Path $EnvironmentRoot 'shared'
$stateFile = Join-Path $EnvironmentRoot 'state\wm5-arm.dess'
foreach ($requiredFile in @($emulatorExe, $romImage)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "缺少模拟器文件：$requiredFile"
    }
}
# 避免两个实例同时写入同一个保存状态文件。
$existing = Get-Process -Name DeviceEmulator -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq $emulatorExe }
if ($existing) { throw '本环境的模拟器已经运行，请使用现有窗口。' }
New-Item -ItemType Directory -Path $sharedDirectory, (Split-Path -Parent $stateFile) -Force | Out-Null
# 显式指定 ROM 会冷启动；恢复已保存状态时仅传 /s。
if (Test-Path -LiteralPath $stateFile -PathType Leaf) {
    $arguments = '/s "{0}" /vmname "MRN-App-Test-WM5-ARM"' -f $stateFile
} else {
    $arguments = '"{0}" /memsize 128 /cpucore ARMv5 /sharedfolder "{1}" /s "{2}" /vmname "MRN-App-Test-WM5-ARM" /z' -f $romImage, $sharedDirectory, $stateFile
}
# 用户使用的交互式模拟器需要显示窗口；不启用网络和主机串口。
Start-Process -FilePath $emulatorExe -ArgumentList $arguments -PassThru
