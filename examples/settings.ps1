# Copy to .local/settings.ps1 and set paths for the targets you use.
# This example is not loaded automatically. Never put real keys in this file.
$env:CARPLAY_VC = 'C:/toolchains/VS2008/VC'
$env:CARPLAY_SDK = 'C:/toolchains/WindowsSDK/v6.0A'
$env:CARPLAY_CE_SDK = 'C:/toolchains/WM5/PocketPC'
$env:CARPLAY_MODERN_SDK = 'C:/toolchains/WindowsSDK10/c'
# $env:CARPLAY_MODERN_SDK_VERSION = '10.0.26100.0'
$env:CARPLAY_CLANG = 'C:/toolchains/llvm/bin/clang-cl.exe'
$env:CARPLAY_WOLFSSL = 'C:/dependencies/wolfssl-5.9.1'
$env:CARPLAY_FFMPEG = 'C:/tools/ffmpeg/bin/ffmpeg.exe'
$env:CARPLAY_CRYPTO = 'C:/dependencies/openssl/libcrypto-3-x64.dll'
$env:CARPLAY_SSL = 'C:/dependencies/openssl/libssl-3-x64.dll'
# Windows builds and CE packaging copy this private identity into the runtime output.
# Directory must contain matching certificate.p7b and identity.pk8; never commit them.
# $env:CARPLAY_IDENTITY = 'C:/private/winxplay/identity'
# $env:WINXPLAY_OEM_ROOT = 'C:/private/mrn-f321'
# $env:MRN_PYTHON = 'C:/Python/python.exe'
# $env:WINXPLAY_EMULATOR = 'C:/toolchains/wm5-emulator'
# $env:WINXPLAY_CORECON_DLL = 'C:/toolchains/CoreCon/Microsoft.Smartdevice.Connectivity.dll'
# $env:WINXPLAY_EMULATOR_DEVICE = 'USA Windows Mobile 5.0 Pocket PC R2 Emulator'
