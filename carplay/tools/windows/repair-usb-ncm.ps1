param([switch]$Apply,[switch]$Restore,[switch]$Interactive,[string]$ExpectedTarget)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'usb-ncm-repair.psm1') -Force
if($Apply -and $Restore){throw '请选择修复或恢复其中一项。'}
if($Interactive){Add-Type -AssemblyName System.Windows.Forms; Add-Type -AssemblyName System.Drawing}
if($Apply -or $Restore){
    try {
        $result=Invoke-WinXPlayUsbRepair -Restore:$Restore -ExpectedTarget $ExpectedTarget
        if($Interactive){[Windows.Forms.MessageBox]::Show($result,'WinXPlay USB 连接修复','OK','Information') | Out-Null}
        else {$result}
        exit 0
    }catch{
        if($Interactive){[Windows.Forms.MessageBox]::Show($_.Exception.Message,'操作未完成','OK','Error') | Out-Null}
        else {Write-Error $_}
        exit 1
    }
}
if(!$Interactive){Get-WinXPlayUsbStatus; return}

[Windows.Forms.Application]::EnableVisualStyles()
$form=New-Object Windows.Forms.Form
$form.Text='WinXPlay · USB 连接修复'
$form.ClientSize=New-Object Drawing.Size(620,390)
$form.StartPosition='CenterScreen'
$form.FormBorderStyle='FixedDialog'
$form.MaximizeBox=$false
$form.Font=New-Object Drawing.Font('Microsoft YaHei UI',10)
$intro=New-Object Windows.Forms.Label
$intro.SetBounds(24,20,570,74)
$intro.Text="用于已识别 iPhone、但 WinXPlay 无法建立 USB 网络的情况。`r`n先尝试正常连接；需要修复时，请退出 WinXPlay，只连接一台已解锁的 iPhone。"
$form.Controls.Add($intro)
$statusBox=New-Object Windows.Forms.TextBox
$statusBox.SetBounds(24,108,570,138)
$statusBox.Multiline=$true
$statusBox.ReadOnly=$true
$statusBox.ScrollBars='Vertical'
$statusBox.TabStop=$false
$form.Controls.Add($statusBox)
$checkButton=New-Object Windows.Forms.Button
$checkButton.Text='检查状态'
$checkButton.SetBounds(24,262,170,38)
$repairButton=New-Object Windows.Forms.Button
$repairButton.Text='修复 USB 连接'
$repairButton.SetBounds(224,262,170,38)
$restoreButton=New-Object Windows.Forms.Button
$restoreButton.Text='恢复原设置'
$restoreButton.SetBounds(424,262,170,38)
$form.Controls.AddRange(@($checkButton,$repairButton,$restoreButton))
$note=New-Object Windows.Forms.Label
$note.SetBounds(24,316,570,64)
$note.Text="检查不会修改系统。修复和恢复需要 Windows 管理员授权，会短暂断开此设备。备份按设备保存在电脑上，移动程序目录不会丢失。"
$form.Controls.Add($note)
$script:deviceStatus=$null
$script:busy=$false
function Update-RepairStatus {
    $repairButton.Enabled=$false; $restoreButton.Enabled=$false; $script:deviceStatus=$null
    try {
        $script:deviceStatus=Get-WinXPlayUsbStatus
        $summary=if($script:deviceStatus.Applied){'USB 分组设置已就绪，无须重复修复。'}else{'此设备尚未应用 USB 分组修复。仅在连接故障时使用。'}
        if(!$script:deviceStatus.DriverAvailable){$summary+="`r`nWindows 缺少 Microsoft UsbNcm 驱动支持，当前不能执行修复。"}
        $summary+="`r`n原设置备份："+$(if($script:deviceStatus.HasBackup){'已保存，可恢复。'}else{'尚无备份。'})
        $summary+="`r`n这项检查不代表手机已成功连接 CarPlay。"
        $statusBox.Text=$summary
        $repairButton.Enabled=$script:deviceStatus.DriverAvailable -and !$script:deviceStatus.Applied
        $restoreButton.Enabled=$script:deviceStatus.HasBackup
    }catch {$statusBox.Text=$_.Exception.Message}
}
function Start-RepairAction([string]$Action) {
    if(!$script:deviceStatus -or $script:busy){return}
    $script:busy=$true
    $repairButton.Enabled=$false; $restoreButton.Enabled=$false; $checkButton.Enabled=$false
    $statusBox.Text='等待 Windows 管理员授权及操作结果。取消授权不会修改设置。'
    try {
        $shell=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
        $arguments='-NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File "'+$PSCommandPath+'" -Interactive -'+$Action+' -ExpectedTarget '+$script:deviceStatus.Target.Id
        $child=Start-Process -FilePath $shell -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -PassThru
        try {
            while(!$child.HasExited){[Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 100}
            $actionExit=$child.ExitCode
        } finally {$child.Dispose()}
        Update-RepairStatus
        if($actionExit -ne 0){$statusBox.Text="上次操作未完成，请查看错误提示后重试。`r`n"+$statusBox.Text}
    }catch {$statusBox.Text='操作未执行或管理员授权已取消。'+"`r`n"+$_.Exception.Message}
    finally {$script:busy=$false; $checkButton.Enabled=$true}
}
$checkButton.Add_Click({Update-RepairStatus})
$repairButton.Add_Click({Start-RepairAction 'Apply'})
$restoreButton.Add_Click({Start-RepairAction 'Restore'})
$form.Add_Shown({Update-RepairStatus})
$form.Add_FormClosing({param($sender,$eventArgs); if($script:busy){$eventArgs.Cancel=$true}})
[void]$form.ShowDialog()
$form.Dispose()
