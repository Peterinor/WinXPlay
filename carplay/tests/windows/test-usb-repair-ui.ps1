# Execute the full entry point, including its typed parameters. Replace only the modal loop.
param([string]$ToolDirectory)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../../..").Path
if(!$ToolDirectory){$ToolDirectory=Join-Path $projectRoot 'carplay/tools/windows'}
$testRoot=Join-Path $projectRoot ('build/validation/usb-repair-ui-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $ToolDirectory 'usb-ncm-repair.psm1') -Destination $testRoot
$source=Get-Content -LiteralPath (Join-Path $ToolDirectory 'repair-usb-ncm.ps1') -Raw -Encoding UTF8
$harness=@'
function Get-WinXPlayUsbStatus {
    [pscustomobject]@{Target=[pscustomobject]@{Id='synthetic'};Applied=$false;HasBackup=$true;DriverAvailable=$true}
}
$script:launches=New-Object 'System.Collections.Generic.List[string]'
function Start-Process {
    param($FilePath,$ArgumentList,$Verb,$WindowStyle,[switch]$PassThru)
    if($Verb -ne 'RunAs' -or $WindowStyle -ne 'Hidden'){throw 'Unexpected elevation request'}
    $script:launches.Add($ArgumentList)
    $child=[pscustomobject]@{HasExited=$true;ExitCode=0}
    $child | Add-Member ScriptMethod Dispose {}
    return $child
}
if($Restore -isnot [System.Management.Automation.SwitchParameter]){throw 'Restore parameter type changed'}
if($Apply -isnot [System.Management.Automation.SwitchParameter]){throw 'Apply parameter type changed'}
$form.Opacity=0
$form.ShowInTaskbar=$false
try {
    $form.Show()
    [Windows.Forms.Application]::DoEvents()
    $buttons=@($form.Controls | Where-Object {$_ -is [Windows.Forms.Button]})
    if($buttons.Count -ne 3){throw 'Expected three repair controls'}
    $applyControl=$buttons | Where-Object {$_.Text -eq '修复 USB 连接'}
    $restoreControl=$buttons | Where-Object {$_.Text -eq '恢复原设置'}
    if(!$applyControl.Enabled -or !$restoreControl.Enabled){throw 'Expected enabled actions with synthetic device and backup'}
    $applyControl.PerformClick()
    $restoreControl.PerformClick()
    if($script:launches.Count -ne 2){throw 'Action buttons did not dispatch both operations'}
    if($script:launches[0] -notmatch ' -Apply -ExpectedTarget synthetic$'){throw 'Wrong repair arguments'}
    if($script:launches[1] -notmatch ' -Restore -ExpectedTarget synthetic$'){throw 'Wrong restore arguments'}
    foreach($arguments in $script:launches){
        if(!$arguments.Contains('-File "'+$PSCommandPath+'"')){throw 'Elevated child would use the wrong entry point'}
    }
    [ordered]@{passed=$true;full_entry_point=$true;typed_switches_preserved=$true;action_buttons=2;real_elevation=$false;real_registry_changed=$false} | ConvertTo-Json
} finally {$form.Hide()}
'@
if(!$source.Contains('[void]$form.ShowDialog()')){throw 'Cannot locate modal UI loop'}
$source.Replace('[void]$form.ShowDialog()',$harness) | Set-Content -LiteralPath (Join-Path $testRoot 'repair-usb-ncm.ps1') -Encoding UTF8
& (Join-Path $testRoot 'repair-usb-ncm.ps1') -Interactive
