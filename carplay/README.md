# CarPlay 接收端

Windows 与 WinCE 共用一份协议核心；平台媒体、网络和设备适配分别实现。当前范围为单手机有线主屏接收、音视频与输入控制。无线、仪表副屏和多手机并发未实现。

| 入口 | 内容 |
| --- | --- |
| [项目构建](../docs/build.md) | 环境配置和各目标编译 |
| [Windows 使用](../docs/guides/windows.md) | 启动、设置和按键 |
| [MRN 部署](../docs/guides/mrn-install.md) | 内部安装、更新与日志 |
| [兼容性](../docs/compatibility.md) | 实际验证范围 |
| [架构](docs/architecture.md) | 模块、状态和资源边界 |
| [代码布局](docs/code-layout.md) | 文件职责与依赖规则 |
| [技术决策](docs/decisions.md) | 当前采用的方案及原因 |
| [运行配置](docs/configuration.md) | 功能开关与设备选择 |
| [输入契约](docs/input.md) | 标准按键、触摸与消息 |
| [MRN 诊断工具](tools/mrn/README.md) | 设备专属 AV、连接、输入测试 |

命令从项目根目录执行：

```powershell
./carplay/build.ps1 -Target Windows
# 通用 WinCE 移植起点，没有预配置的视频解码后端
./carplay/build.ps1 -Target CeGeneric
# MRN 设备配置
./carplay/build.ps1 -Target CeMrn
./carplay/verify.ps1 -Target Windows
```

接收端产物位于项目根目录的 `build/carplay/`。Windows 构建后直接双击 `build/carplay/windows-x64/WinXPlay.exe`；依赖 DLL 和已配置的身份随构建准备。真实认证身份与配对记录不在源码中。许可证及第三方来源见项目根目录的 LICENSE、THIRD_PARTY.md。
