# WinXPlay USB 连接修复

先尝试正常启动 WinXPlay。如果 Windows 已识别 iPhone，但 WinXPlay 一直无法建立 USB 网络，可以使用本工具。它只处理特定 USB 接口分组配置问题，不是所有连接故障的通用修复。

1. 退出 WinXPlay，连接并解锁一台 iPhone；其他 Apple USB 设备先拔下。
2. 双击程序旁的 **USB连接修复.cmd**，窗口自动检查状态，不修改系统。
3. 如需修复，点击 **修复 USB 连接**，在 Windows 提示中允许管理员操作。
4. 等待结果提示，重新拔插并解锁 iPhone，再启动 WinXPlay。手机上的“信任”提示需要本人确认。
5. 如需撤销，连接同一设备、使用相同 USB 端口，打开工具点击 **恢复原设置**。

不需要安装 Python、打开命令行或记住参数。移动程序目录时保留 `tools/usb-repair/` 中的脚本和模块。

工具只修改当前 Apple 组合设备驱动实例的 `EnumeratorClass`，然后请求 Windows 重启这个设备。它不下载驱动、不替换 Apple 驱动、不自动重启电脑；如果 Windows 返回需要重启的状态，窗口会提示保存工作后自行重启。Windows 不提供 Microsoft UsbNcm 支持时，工具会说明原因并停止修复。

修改前自动保存原值，按设备和驱动实例分别保存到 `%ProgramData%\WinXPlay\usb-repair\`；重复修复不会覆盖原备份。设备重启日志也保存在这里。换 USB 端口或驱动重装可能改变实例，工具不会把其他实例的备份套用到当前设备。旧开发脚本的备份不自动导入。

设置保存成功不等于 CarPlay 已连接成功。设备重启失败时设置可能已经保存，请按窗口提示重新拔插手机。连接仍有问题时，检查 Apple 设备服务、数据线、手机授权及 WinXPlay 诊断信息，不要反复修复已就绪的分组设置。

开发者仍可执行 `repair-usb-ncm.ps1` 只读检查，或在管理员 PowerShell 使用 `-Apply` / `-Restore`；普通使用建议通过中文窗口操作。

设备重启调用和返回码依据 Microsoft 的 [PnPUtil 命令说明](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax)及[返回值说明](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-return-values)。本工具不把设置完成当作硬件验收。
