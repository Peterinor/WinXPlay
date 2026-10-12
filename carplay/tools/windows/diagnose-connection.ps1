# Read-only Windows connection checks. Uses Windows PowerShell 5.1 built-ins.
# Does not install drivers, change registry/network settings, or read private keys.
param([string]$OutputDirectory = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../../../scripts/environment.ps1"
$taskLines = New-Object 'System.Collections.Generic.List[string]'
function Add-Line([string]$Text) { $taskLines.Add($Text) }
function Check-Section([string]$Name, [scriptblock]$Check) {
    Add-Line ''
    Add-Line ('=== ' + $Name + ' ===')
    try { & $Check } catch { Add-Line ('CHECK ERROR (not proof of missing component): ' + $_.Exception.Message) }
}
Add-Line 'WinXPlay - Windows connection report v1'
Add-Line ('Captured: ' + (Get-Date -Format o))
Add-Line 'Keep iPhone unlocked and connected. Start CarPlay and attempt connection BEFORE this check.'
Add-Line 'No system settings changed. USB serial numbers, pairing records and private key contents are not collected.'

Check-Section 'Windows' {
    $taskOs = Get-CimInstance Win32_OperatingSystem
    Add-Line ($taskOs.Caption + '; version=' + $taskOs.Version + '; build=' + $taskOs.BuildNumber + '; ' + $taskOs.OSArchitecture)
}
Check-Section 'Apple services' {
    $taskServices = @(Get-Service | Where-Object { $_.Name -match 'Apple.*Mobile|AppleMobile' -or $_.DisplayName -match 'Apple.*Mobile' })
    foreach ($taskService in $taskServices) { Add-Line ($taskService.Name + ': ' + $taskService.Status) }
    if (!$taskServices.Count) { Add-Line 'No Apple Mobile service matched by name; check the service port below as well.' }
}
Check-Section 'Apple device service port used by this receiver' {
    $taskClient = New-Object System.Net.Sockets.TcpClient
    $taskWait = $null
    try {
        $taskConnect = $taskClient.BeginConnect('127.0.0.1', 27015, $null, $null)
        $taskWait = $taskConnect.AsyncWaitHandle
        if (!$taskWait.WaitOne(1500)) { throw 'localhost:27015 did not respond within 1.5 seconds' }
        $taskClient.EndConnect($taskConnect)
        Add-Line 'localhost:27015 REACHABLE (TCP only; this does not verify pairing or CarPlay).'
    } finally {
        $taskClient.Close()
        if ($taskWait) { $taskWait.Close() }
    }
}
Check-Section 'Present Apple USB devices and driver status' {
    $taskApple = @(Get-CimInstance Win32_PnPEntity -Filter "PNPDeviceID LIKE 'USB%VID_05AC%'" | Where-Object { $_.Present })
    Add-Line ('Present Apple USB nodes: ' + $taskApple.Count)
    foreach ($taskDevice in $taskApple) {
        # Keep only VID/PID/interface portion; omit per-device serial/instance suffix.
        $taskHardware = ($taskDevice.PNPDeviceID -split '\\')[1]
        Add-Line ('USB=' + $taskHardware + '; class=' + $taskDevice.PNPClass + '; driver=' + $taskDevice.Service + '; status=' + $taskDevice.Status + '; problemCode=' + $taskDevice.ConfigManagerErrorCode)
        if ($taskDevice.Service -eq 'usbccgp') {
            $taskDeviceKey = 'HKLM:\SYSTEM\CurrentControlSet\Enum\' + $taskDevice.PNPDeviceID
            $taskProperties = Get-ItemProperty -LiteralPath $taskDeviceKey
            if ($taskProperties.Driver) {
                $taskDriverKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\' + $taskProperties.Driver
                $taskGroup = (Get-ItemProperty -LiteralPath $taskDriverKey).EnumeratorClass
                if ($null -eq $taskGroup) { Add-Line '  EnumeratorClass: not set (diagnostic only; not automatically an error)' }
                else { Add-Line ('  EnumeratorClass: ' + (($taskGroup | ForEach-Object { '{0:X2}' -f $_ }) -join ' ')) }
            }
        }
    }
}
Check-Section 'System NCM driver registration' {
    $taskNcmServiceKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\UsbNcm'
    $taskNcmRegistered = Test-Path -LiteralPath $taskNcmServiceKey
    Add-Line ('UsbNcm service registered=' + $taskNcmRegistered)
    if ($taskNcmRegistered) {
        $taskDriverPath = [Environment]::ExpandEnvironmentVariables([string](Get-ItemProperty -LiteralPath $taskNcmServiceKey).ImagePath)
        if ($taskDriverPath.StartsWith('\SystemRoot\', [StringComparison]::OrdinalIgnoreCase)) {
            $taskDriverPath = Join-Path $env:SystemRoot $taskDriverPath.Substring(12)
        } elseif ($taskDriverPath.StartsWith('\??\')) {
            $taskDriverPath = $taskDriverPath.Substring(4)
        } elseif ($taskDriverPath -and ![IO.Path]::IsPathRooted($taskDriverPath)) {
            $taskDriverPath = Join-Path $env:SystemRoot $taskDriverPath
        }
        if ([Environment]::Is64BitOperatingSystem -and ![Environment]::Is64BitProcess) {
            $taskSystem32Prefix = Join-Path $env:SystemRoot 'System32\'
            if ($taskDriverPath.StartsWith($taskSystem32Prefix, [StringComparison]::OrdinalIgnoreCase)) {
                $taskDriverPath = Join-Path (Join-Path $env:SystemRoot 'Sysnative') $taskDriverPath.Substring($taskSystem32Prefix.Length)
            }
        }
        Add-Line ('Registered NCM driver file exists=' + [bool]($taskDriverPath -and (Test-Path -LiteralPath $taskDriverPath -PathType Leaf)))
    }
    Add-Line 'File presence alone does not establish device binding or a usable connection.'
}
Check-Section 'NCM adapters and IPv6 (same driver selection as the receiver)' {
    $taskNcm = @(Get-CimInstance Win32_NetworkAdapter -Filter "ServiceName = 'UsbNcm'" | Where-Object { $_.GUID })
    $taskUsable = 0
    Add-Line ('NCM adapter records: ' + $taskNcm.Count)
    foreach ($taskNcmDevice in $taskNcm) {
        $taskAdapters = @(Get-NetAdapter -IncludeHidden | Where-Object { ([string]$_.InterfaceGuid).Trim('{}') -ieq ([string]$taskNcmDevice.GUID).Trim('{}') })
        foreach ($taskAdapter in $taskAdapters) {
            $taskAddresses = @(Get-NetIPAddress -InterfaceIndex $taskAdapter.ifIndex | Where-Object { $_.AddressFamily -eq 'IPv6' })
            $taskLocal = @($taskAddresses | Where-Object { ([System.Net.IPAddress]::Parse($_.IPAddress)).IsIPv6LinkLocal })
            $taskAppleBound = $taskNcmDevice.PNPDeviceID -like 'USB\VID_05AC*'
            Add-Line ('ifIndex=' + $taskAdapter.ifIndex + '; status=' + $taskAdapter.Status + '; Apple USB=' + $taskAppleBound + '; IPv6 link-local count=' + $taskLocal.Count)
            foreach ($taskAddress in $taskLocal) { Add-Line ('  IPv6 address state=' + $taskAddress.AddressState) }
            if ($taskAdapter.Status -eq 'Up' -and $taskLocal.Count -gt 0) { $taskUsable++ }
        }
    }
    Add-Line ('Up NCM adapters with link-local IPv6: ' + $taskUsable)
    if (!$taskUsable) { Add-Line 'NO NCM MEDIA PATH FOUND. If CarPlay has already attempted a connection, investigate USB mode/interface grouping/driver binding/IPv6.' }
    else { Add-Line 'NCM address prerequisite found; this does NOT prove iPhone reachability or successful streaming.' }
}
Check-Section 'Runtime file locations from this shell or EXE directory defaults' {
    $taskRuntime = Join-Path $PSScriptRoot '../../../build/carplay/windows-x64'
    $taskCryptoPath = if($env:CARPLAY_CRYPTO){$env:CARPLAY_CRYPTO}else{Join-Path $taskRuntime 'libcrypto-3-x64.dll'}
    $taskIdentityPath = if($env:CARPLAY_IDENTITY){$env:CARPLAY_IDENTITY}else{Join-Path $taskRuntime 'identity'}
    Add-Line ('OpenSSL file exists=' + [bool]($taskCryptoPath -and (Test-Path -LiteralPath $taskCryptoPath -PathType Leaf)))
    Add-Line 'Media: Windows Media Foundation / D3D11 / waveOut (no external media runtime)'
    Add-Line ('Identity certificate exists=' + [bool]($taskIdentityPath -and (Test-Path -LiteralPath (Join-Path $taskIdentityPath 'certificate.p7b') -PathType Leaf)))
    Add-Line ('Identity key exists=' + [bool]($taskIdentityPath -and (Test-Path -LiteralPath (Join-Path $taskIdentityPath 'identity.pk8') -PathType Leaf)))
    Add-Line 'No DLL/executable was loaded and no credential contents were read. Custom launchers may use different paths.'
}
Add-Line ''
Add-Line 'Next evidence: this report plus the receiver auto-connect.plist (latest connection error).'
$taskReportName = 'carplay-connection-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.txt'
try {
    $taskReportPath = Join-Path $OutputDirectory $taskReportName
    $taskLines | Out-File -LiteralPath $taskReportPath -Encoding utf8
} catch {
    $taskReportPath = Join-Path ([IO.Path]::GetTempPath()) $taskReportName
    $taskLines | Out-File -LiteralPath $taskReportPath -Encoding utf8
}
$taskLines | ForEach-Object { Write-Output $_ }
Write-Output ("`nReport saved: " + $taskReportPath)
