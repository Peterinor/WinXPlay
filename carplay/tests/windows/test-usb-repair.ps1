# Offline regression: every device, registry and restart operation is replaced by a double.
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../../..").Path
$testRoot=Join-Path $projectRoot ('build/validation/usb-repair-tests-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$repairModule=Import-Module "$projectRoot/carplay/tools/windows/usb-ncm-repair.psm1" -Force -PassThru
& $repairModule {
    param($testRoot)
    function script:Reset-Fake {
        $script:fake=@{Admin=$true;Count=1;Driver=$true;Exists=$false;Value=$null;Kind='Binary';Writes=0;Restarts=0;Code=0;Queries=0;Swap=$false}
        $script:casePath=Join-Path $testRoot ([guid]::NewGuid().ToString('N'))
    }
    function script:Assert-Fake($condition,$message){if(!$condition){throw $message}}
    function script:Expect-Failure([scriptblock]$body){
        $rejected=$false
        try {& $body | Out-Null} catch {$rejected=$true}
        Assert-Fake $rejected 'Expected operation rejection'
        Assert-Fake ($script:fake.Writes -eq 0) 'Rejected operation wrote registry'
    }
    function script:Test-WinXPlayAdministrator {return $script:fake.Admin}
    function script:Get-PnpDevice {
        param([switch]$PresentOnly)
        $script:fake.Queries++
        for($i=0;$i -lt $script:fake.Count;$i++){
            $suffix=if($script:fake.Swap -and $script:fake.Queries -gt 1){'changed'}else{'test'+$i}
            [pscustomobject]@{InstanceId=('USB\VID_05AC&PID_1234\'+$suffix);Class='USBDevice'}
        }
    }
    function script:Get-ItemProperty {
        param($LiteralPath)
        if($LiteralPath -like '*\Enum\*'){return [pscustomobject]@{Service='usbccgp';Driver='{88bae032-5a81-49f0-bc3d-a4ff138216d6}\0001'}}
        if($script:fake.Exists){return [pscustomobject]@{EnumeratorClass=$script:fake.Value}}
        return [pscustomobject]@{}
    }
    function script:Get-Item {
        param($LiteralPath)
        $key=New-Object psobject
        $key | Add-Member ScriptMethod GetValueKind {param($Name); return $script:fake.Kind}
        return $key
    }
    function script:Test-Path {
        param($LiteralPath,$PathType)
        if($LiteralPath -like 'HKLM:*'){return $script:fake.Driver}
        return Microsoft.PowerShell.Management\Test-Path @PSBoundParameters
    }
    function script:New-ItemProperty {
        param($LiteralPath,$Name,$PropertyType,$Value,[switch]$Force)
        Assert-Fake ($LiteralPath -like 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\*') 'Wrong registry scope'
        Assert-Fake ($Name -eq 'EnumeratorClass') 'Wrong registry value'
        $script:fake.Writes++; $script:fake.Exists=$true; $script:fake.Value=[byte[]]$Value; $script:fake.Kind=$PropertyType
    }
    function script:Remove-ItemProperty {
        param($LiteralPath,$Name,$ErrorAction)
        $script:fake.Writes++; $script:fake.Exists=$false; $script:fake.Value=$null
    }
    function script:Restart-WinXPlayUsbDevice {
        param($Instance,$Log)
        Assert-Fake ($Instance -like 'USB\VID_05AC*') 'Wrong restart target'
        $script:fake.Restarts++
        return $script:fake.Code
    }
    $cases=0
    Reset-Fake
    $status=Get-WinXPlayUsbStatus -StateDirectory $script:casePath
    Assert-Fake (!$status.Applied -and !$status.HasBackup -and $script:fake.Writes -eq 0) 'Read-only check changed state'
    $cases++
    Invoke-WinXPlayUsbRepair -StateDirectory $script:casePath | Out-Null
    $before=[IO.File]::ReadAllBytes($status.Backup)
    Assert-Fake ($script:fake.Writes -eq 1 -and $script:fake.Restarts -eq 1) 'Apply failed'
    Assert-Fake (!(Import-Clixml $status.Backup).Exists) 'Absent original not backed up correctly'
    $cases++
    Invoke-WinXPlayUsbRepair -StateDirectory $script:casePath | Out-Null
    Assert-Fake ($script:fake.Writes -eq 1 -and $script:fake.Restarts -eq 1) 'Repeated repair changed state'
    Assert-Fake ([Convert]::ToBase64String($before) -eq [Convert]::ToBase64String([IO.File]::ReadAllBytes($status.Backup))) 'Original backup overwritten'
    $cases++
    Invoke-WinXPlayUsbRepair -Restore -StateDirectory $script:casePath | Out-Null
    Assert-Fake (!$script:fake.Exists) 'Restore failed to remove originally absent setting'
    $cases++
    Reset-Fake
    $script:fake.Exists=$true; $script:fake.Value=[byte[]](9,8,7)
    Invoke-WinXPlayUsbRepair -StateDirectory $script:casePath | Out-Null
    Invoke-WinXPlayUsbRepair -Restore -StateDirectory $script:casePath | Out-Null
    Assert-Fake ([BitConverter]::ToString($script:fake.Value) -eq '09-08-07') 'Existing original not restored'
    $cases++
    foreach($count in @(0,2)){
        Reset-Fake; $script:fake.Count=$count
        Expect-Failure {Invoke-WinXPlayUsbRepair -StateDirectory $script:casePath}
        $cases++
    }
    Reset-Fake; $script:fake.Admin=$false
    Expect-Failure {Invoke-WinXPlayUsbRepair -StateDirectory $script:casePath}; $cases++
    Reset-Fake; $script:fake.Driver=$false
    Expect-Failure {Invoke-WinXPlayUsbRepair -StateDirectory $script:casePath}; $cases++
    Reset-Fake
    Expect-Failure {Invoke-WinXPlayUsbRepair -ExpectedTarget 'wrong' -StateDirectory $script:casePath}; $cases++
    Reset-Fake; $script:fake.Exists=$true; $script:fake.Value=123; $script:fake.Kind='DWord'
    Expect-Failure {Invoke-WinXPlayUsbRepair -StateDirectory $script:casePath}; $cases++
    Reset-Fake; $script:fake.Swap=$true
    Expect-Failure {Invoke-WinXPlayUsbRepair -StateDirectory $script:casePath}; $cases++
    Reset-Fake
    Expect-Failure {Invoke-WinXPlayUsbRepair -Restore -StateDirectory $script:casePath}; $cases++
    Reset-Fake
    $status=Get-WinXPlayUsbStatus -StateDirectory $script:casePath
    New-Item -ItemType Directory -Path $script:casePath | Out-Null
    [pscustomobject]@{Version=1;Key='wrong';Instance='wrong';Exists=$false} | Export-Clixml $status.Backup
    Expect-Failure {Invoke-WinXPlayUsbRepair -Restore -StateDirectory $script:casePath}; $cases++
    Reset-Fake; $script:fake.Code=3010
    $message=Invoke-WinXPlayUsbRepair -StateDirectory $script:casePath
    Assert-Fake ($message -match '重启电脑' -and $script:fake.Writes -eq 1) 'Reboot required not reported'; $cases++
    Reset-Fake; $script:fake.Code=5
    $message=Invoke-WinXPlayUsbRepair -StateDirectory $script:casePath
    Assert-Fake ($message -match '手动拔插' -and $script:fake.Writes -eq 1) 'Restart failure lost saved-state notice'; $cases++
    [ordered]@{passed=$true;cases=$cases;real_registry_changed=$false;real_device_restarted=$false} | ConvertTo-Json
} $testRoot
