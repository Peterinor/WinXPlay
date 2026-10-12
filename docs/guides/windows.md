# Windows 使用说明

先按 [构建指南](../build.md) 配置并构建 Windows 目标。运行需要 OpenSSL 3 x64、Apple 设备服务、可用的 USB/NCM 通路及用户自己的配件认证身份。

构建完成后直接双击 `build/carplay/windows-x64/WinXPlay.exe`。不需要启动脚本，也不需要在运行机器上设置这三项路径环境变量。程序按 EXE 所在目录查找依赖，不依赖当前工作目录：

```text
build/carplay/windows-x64/
├─ WinXPlay.exe
├─ libcrypto-3-x64.dll
├─ libssl-3-x64.dll
├─ USB连接修复.cmd
├─ USB连接修复说明.md
├─ tools/usb-repair/
├─ identity/
│  ├─ certificate.p7b
│  ├─ identity.pk8
│  └─ certificate.der
├─ LICENSE
└─ THIRD_PARTY.txt
```

`certificate.p7b` 是配件证书，`identity.pk8` 是配套私钥；构建时校验两者匹配，并生成 `certificate.der`。Windows 实际读取前两份，DER 文件与 CE 布局保持一致。证书来源由本地 `.local/settings.ps1` 中的 `CARPLAY_IDENTITY` 指定，完整配置见 [证书与私钥位置](../build.md#证书与私钥位置)。源码不附带真实身份。

Windows 构建会复制配置的 OpenSSL DLL 及发现的相邻非系统依赖；设置了 `CARPLAY_IDENTITY` 时同时复制身份到输出目录。未配置身份时仍可构建，已有运行身份不会被删除；首次连接前需自行把匹配文件放入 `identity/`。缺少 OpenSSL DLL 或配置了无效身份时，构建明确报错，不会声称运行文件已准备好。

运行时显式设置的 `CARPLAY_CRYPTO`、`CARPLAY_SSL`、`CARPLAY_IDENTITY` 优先于本地默认路径，便于调试。旧启动脚本已经移除；直接双击不读取 `.local/settings.ps1`，而是使用已经复制好的运行文件。

移动程序时保留上述目录结构和附加依赖 DLL；OBJ、测试素材和构建中间文件不是运行必需项。系统媒体组件及 Apple 设备驱动、服务仍需在目标 Windows 上可用，复制 DLL 不会安装这些系统依赖。含 `identity/` 的输出是本地私有运行目录，不直接用于公开 Release。身份文件存在及本地签名验证通过均不代表手机已接受认证。

界面显示插线、手机授权、网络准备、等待视频和重连等阶段。手机可能要求解锁和信任；撤销信任后的配对恢复由接收流程处理。F11 切换全屏，右键打开设置与诊断入口。视频显示保持宽高比，黑边不触发触摸。

常用输入：方向键导航、滚轮旋钮、Enter 确认、Home 首页、Backspace 返回、空格播放/暂停；F6 接听、F7 挂断、F8 按下/松开 Siri、F9 昼夜、F5 请求关键帧。手机决定实际业务状态，发送指令不代表通话已接通。详细行为见 [输入契约](../../carplay/docs/input.md)。

音频默认使用 Windows 后端；iPhone 直接蓝牙连接原车是另一条路径，不应把它当作接收端音频验收。麦克风只在手机请求相应流后开启；全双工与回声消除尚未验收。

离线检查连接条件：`./carplay/tools/windows/diagnose-connection.ps1`。检测报告不包含私钥，也不证明已建立媒体会话。

## USB 网络修复

Windows 已识别 iPhone、但 WinXPlay 无法建立 USB 网络时，先退出 WinXPlay，再双击 EXE 同目录的 **USB连接修复.cmd**。中文窗口提供“检查状态”“修复 USB 连接”“恢复原设置”；检查不修改系统，执行修复或恢复时才请求管理员权限。不需要 Python 或命令行参数。

窗口会说明未连接、多设备、驱动不支持、已经配置、需要重启等状态。原设置按设备备份到 `%ProgramData%\WinXPlay\usb-repair\`。恢复使用同一设备和驱动实例；移动程序文件夹不会丢失备份。修复完成后重新拔插并解锁手机，再启动 WinXPlay。详见 [USB 修复说明](../../carplay/tools/windows/usb-repair.md)。
