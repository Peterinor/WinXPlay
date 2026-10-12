$ErrorActionPreference='Stop'

function Test-WinXPlayAdministrator {
    return ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-WinXPlayUsbTarget {
    $phones=@(Get-PnpDevice -PresentOnly | Where-Object {
        $_.InstanceId -like 'USB\VID_05AC*' -and $_.InstanceId -notmatch '&MI_' -and $_.Class -eq 'USBDevice'
    })
    if($phones.Count -eq 0){throw '未找到可修复的 Apple USB 设备。请连接并解锁一台 iPhone；若仍无法识别，请先检查 Apple 驱动和数据线。'}
    if($phones.Count -ne 1){throw '检测到多台 Apple USB 设备。请只保留需要修复的一台 iPhone，然后重新检查。'}
    $device=Get-ItemProperty -LiteralPath ('HKLM:\SYSTEM\CurrentControlSet\Enum\'+$phones[0].InstanceId)
    if($device.Service -ne 'usbccgp'){throw '当前设备未使用预期的 Windows USB 组合驱动，不能应用此项修复。'}
    if($device.Driver -notmatch '^\{88bae032-5a81-49f0-bc3d-a4ff138216d6\}\\\d+$'){throw '当前驱动配置不在此修复工具的支持范围内。'}
    $key='HKLM:\SYSTEM\CurrentControlSet\Control\Class\'+$device.Driver
    $sha=[Security.Cryptography.SHA256]::Create()
    try {$id=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($phones[0].InstanceId+'|'+$key)))).Replace('-','').ToLowerInvariant()}
    finally {$sha.Dispose()}
    [pscustomobject]@{Instance=$phones[0].InstanceId;Key=$key;Id=$id}
}

function Get-WinXPlayUsbStatus {
    param([string]$StateDirectory=(Join-Path $env:ProgramData 'WinXPlay/usb-repair'))
    $target=Get-WinXPlayUsbTarget
    $current=Get-ItemProperty -LiteralPath $target.Key
    $present=$null -ne $current.PSObject.Properties['EnumeratorClass']
    $kind=if($present){(Get-Item -LiteralPath $target.Key).GetValueKind('EnumeratorClass').ToString()}else{''}
    [pscustomobject]@{
        Target=$target
        Applied=($present -and $kind -eq 'Binary' -and ([BitConverter]::ToString([byte[]]$current.EnumeratorClass)) -eq '02-00-00')
        Backup=(Join-Path $StateDirectory ($target.Id+'.clixml'))
        HasBackup=(Test-Path -LiteralPath (Join-Path $StateDirectory ($target.Id+'.clixml')) -PathType Leaf)
        DriverAvailable=(Test-Path -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\UsbNcm')
    }
}

function Restart-WinXPlayUsbDevice {
    param([string]$Instance,[string]$Log)
    & "$env:SystemRoot/System32/pnputil.exe" /restart-device $Instance 2>&1 | Out-File -LiteralPath $Log -Encoding utf8
    return $LASTEXITCODE
}

function Invoke-WinXPlayUsbRepairCore {
    param([switch]$Restore,[string]$ExpectedTarget,[string]$StateDirectory=(Join-Path $env:ProgramData 'WinXPlay/usb-repair'))
    if(!(Test-WinXPlayAdministrator)){throw '此操作需要管理员权限。请使用随程序提供的 USB 连接修复窗口。'}
    $status=Get-WinXPlayUsbStatus -StateDirectory $StateDirectory
    $target=$status.Target
    if($ExpectedTarget -and $ExpectedTarget -ne $target.Id){throw '连接的设备或驱动实例已变化。未修改设置，请关闭提示后重新检查。'}
    if(!$Restore -and !$status.DriverAvailable){throw '当前 Windows 缺少 Microsoft UsbNcm 驱动支持；这项配置修复无法代替驱动安装。未修改设置。'}
    if($Restore -and !$status.HasBackup){throw '未找到此设备的原设置备份，无法恢复。'}
    if($status.HasBackup){
        $saved=Import-Clixml -LiteralPath $status.Backup
        if($saved.Key -ne $target.Key -or $saved.Instance -ne $target.Instance -or $saved.Version -ne 1){throw '备份与当前设备不匹配，未修改设置。'}
        if($saved.Exists -and ($saved.Kind -ne 'Binary' -or $saved.Value -isnot [byte[]])){throw '备份中的原设置格式不受支持，未修改设置。'}
    }
    if(!$Restore -and $status.Applied){return '此设备已经采用所需的 USB 分组设置，本次未修改。请重新连接并解锁 iPhone，再启动 WinXPlay。'}
    # Validate the original type before creating a backup or changing the registry.
    if(!$Restore -and !$status.HasBackup){
        $current=Get-ItemProperty -LiteralPath $target.Key
        $exists=$null -ne $current.PSObject.Properties['EnumeratorClass']
        if($exists -and (Get-Item -LiteralPath $target.Key).GetValueKind('EnumeratorClass').ToString() -ne 'Binary'){
            throw '原设置不是预期的二进制格式，未修改设置。'
        }
        New-Item -ItemType Directory -Path $StateDirectory -Force | Out-Null
        $saved=[pscustomobject]@{Version=1;Instance=$target.Instance;Key=$target.Key;Exists=$exists;Kind='Binary';Value=$current.EnumeratorClass}
        $saved | Export-Clixml -LiteralPath $status.Backup -NoClobber
    }
    # Detect unplug/replacement while the backup was being prepared.
    if((Get-WinXPlayUsbTarget).Id -ne $target.Id){throw '设备在准备过程中发生变化，未修改设置，请重新检查。'}
    if($Restore){
        if($saved.Exists){New-ItemProperty -LiteralPath $target.Key -Name EnumeratorClass -PropertyType Binary -Value ([byte[]]$saved.Value) -Force | Out-Null}
        else {Remove-ItemProperty -LiteralPath $target.Key -Name EnumeratorClass -ErrorAction SilentlyContinue}
        $message='已恢复此设备的原 USB 分组设置。'
    }else{
        New-ItemProperty -LiteralPath $target.Key -Name EnumeratorClass -PropertyType Binary -Value ([byte[]](2,0,0)) -Force | Out-Null
        $message='已保存 USB 分组修复设置，原设置已备份。'
    }
    # Verify the write before reporting success. Do not infer a working CarPlay session.
    $actual=Get-ItemProperty -LiteralPath $target.Key
    $hasActual=$null -ne $actual.PSObject.Properties['EnumeratorClass']
    $wantExists=(!$Restore -or $saved.Exists)
    if($hasActual -ne $wantExists){throw '设置写入后核对失败，请使用备份恢复原设置。'}
    if($wantExists){
        $wanted=if($Restore){[byte[]]$saved.Value}else{[byte[]](2,0,0)}
        if((Get-Item -LiteralPath $target.Key).GetValueKind('EnumeratorClass').ToString() -ne 'Binary' -or
            [BitConverter]::ToString([byte[]]$actual.EnumeratorClass) -ne [BitConverter]::ToString($wanted)){
            throw '设置内容核对失败，请使用备份恢复原设置。'
        }
    }
    try {$code=Restart-WinXPlayUsbDevice -Instance $target.Instance -Log (Join-Path $StateDirectory ($target.Id+'-restart.log'))}
    catch {return $message+"`r`n设备重启未完成，请手动拔插 iPhone。详细原因："+$_.Exception.Message}
    if($code -eq 3010){return $message+"`r`nWindows 要求重启电脑后生效，请保存工作后自行重启。工具不会自动重启电脑。"}
    if($code -ne 0){return $message+"`r`n设备重启未完成（返回码 $code），请手动拔插 iPhone。"}
    return $message+"`r`n请重新连接并解锁 iPhone，然后启动 WinXPlay。设置完成不等于 CarPlay 已连接成功。"
}

function Invoke-WinXPlayUsbRepair {
    param([switch]$Restore,[string]$ExpectedTarget,[string]$StateDirectory=(Join-Path $env:ProgramData 'WinXPlay/usb-repair'))
    $mutex=New-Object Threading.Mutex($false,'Global\WinXPlayUsbRepair')
    $acquired=$false
    try {
        try {$acquired=$mutex.WaitOne(0)} catch [Threading.AbandonedMutexException] {$acquired=$true}
        if(!$acquired){throw '另一个 USB 修复操作正在执行，请等待完成后重试。'}
        Invoke-WinXPlayUsbRepairCore -Restore:$Restore -ExpectedTarget $ExpectedTarget -StateDirectory $StateDirectory
    } finally {if($acquired){$mutex.ReleaseMutex()}; $mutex.Dispose()}
}

Export-ModuleMember -Function Get-WinXPlayUsbStatus,Invoke-WinXPlayUsbRepair
