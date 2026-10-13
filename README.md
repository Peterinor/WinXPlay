# WinXPlay

**面向 Windows 与 WinCE 的有线 CarPlay 接收端。**

WinXPlay 在电脑或适配的车机上接收 iPhone 的 CarPlay 画面与音频，并将触摸、鼠标、键盘和控制器操作传回手机。项目包含共享协议核心、平台音视频后端、设备适配，以及部署和诊断工具。

Windows 与 WinCE 共用协议核心，各自实现操作系统相关的网络、窗口、媒体和 USB 能力。具体设备的硬件能力、驱动差异和原有系统集成由设备适配层处理；支持 WinCE 平台不等于适用于所有 WinCE 设备。

项目目前处于实验性开发阶段，适合希望自行构建、验证或移植接收端的开发者。实际支持范围见[兼容性说明](docs/compatibility.md)。

[开始构建](docs/build.md) · [Windows 使用](docs/guides/windows.md) · [文档索引](docs/README.md) · [变更记录](CHANGELOG.md)

## 项目提供什么

- **有线连接与会话管理**：USB 设备发现、手机信任与配对、配件认证、媒体网络准备、断线后的重连处理，以及可区分连接阶段的状态界面。
- **视频与音频接收**：共享媒体组帧与会话逻辑，通过平台后端完成解码、显示和音频输入输出。可用格式及实际效果取决于平台实现和设备能力。
- **多种输入方式**：触摸、鼠标、方向键和旋钮导航，以及首页、返回、媒体控制等消息。电话和 Siri 控制接口已有实现，实际通话与语音能力仍需验证。
- **部署与诊断工具**：文件操作、进程和窗口管理、系统信息采集，以及音视频、连接、输入三个独立诊断入口。构建与打包入口统一，部署方式由具体目标决定。

## 平台与当前状态

| 平台 | 当前实现 | 支持边界 |
| --- | --- | --- |
| Windows x64 | 有线接收、系统视频解码与显示、PCM/AAC 音频、桌面输入、连接界面和 USB 修复工具 | 有指定环境下的 iPhone 视频、输入和音频实测记录；合成回归覆盖更多协议场景 |
| WinCE ARM | CE 系统与窗口实现、网络与 USB 接入代码、wave 音频接口，以及 GDI/DirectShow 相关媒体实现 | 通用构建用于移植起点，没有开箱可用的视频后端；视频滤镜、音频路由、USB 驱动兼容和物理输入仍需按设备确认或适配 |

当前范围是**单手机、有线、主屏接收**。无线 CarPlay、多手机并发和仪表副屏尚未实现。WinCE 已接入普通 AAC 系统 DMO 解码，MRN 启动自检实车通过，但尚未观察到手机选择 AAC 音乐流；不声明缓冲 AAC。Siri/通话质量、回声消除和长时稳定性仍有待完成的验证项。

具体设备的适用固件、集成方式和实测结果分别记录在设备文档中，不作为 WinCE 平台的通用能力声明。现有设备适配入口见 [MRN 设备说明](docs/devices/mrn.md)。

**标致 508 MRN 使用者请先阅读[使用提示与技术取舍](docs/guides/mrn-usage-notes.md)**：当前默认保留 USB 画面和触控，关闭 CarPlay 音频输入、输出，声音走原车蓝牙。动画卡顿及麦克风回声、录音副作用等限制尚未全部解决。

软件接口存在、离线回归通过和物理效果通过是不同状态。各能力的具体限制统一维护在[兼容性与验证边界](docs/compatibility.md)，开发优先级见[路线图](docs/roadmap.md)。

## 开始使用

### 先准备运行条件

真实连接需要 iPhone、可用的 USB 数据通路，以及使用者自行准备的配件认证身份。身份由匹配的证书和私钥组成，与手机点击“信任”后产生的配对记录不同；项目不提供真实身份或配对材料。

- **Windows**：目标电脑需要 Apple 设备驱动/服务、USB/NCM 通路及系统媒体组件。构建输出会准备 OpenSSL DLL；提供身份来源后，会一并准备运行身份目录。
- **WinCE**：目标设备需要可用的 USB/网络通路与媒体能力。先确认对应设备适配的支持范围；新设备还需完成后端选择、驱动兼容和实际输入输出验证。

身份配置见[证书与私钥位置](docs/build.md#证书与私钥位置)。桌面运行见 [Windows 使用说明](docs/guides/windows.md)，设备相关的使用和部署入口见[文档索引](docs/README.md)。

### 从源码构建

以下命令在仓库根目录的 PowerShell 中执行，仓库所在文件夹名称不限。

先使用 Python 3.10 或以上版本检查源码结构；这一步不需要手机、身份材料或专有 SDK：

```powershell
python scripts/check-project.py
```

按[构建指南](docs/build.md)准备所选目标的工具链，将 [examples/settings.ps1](examples/settings.ps1) 复制到 `.local/settings.ps1`，填写本机路径。现有构建使用 VS2008；Windows 媒体实现另需现代 Windows SDK 和 clang-cl，CE 使用 WM5 PocketPC SDK 与 wolfSSL。开发和测试所需的 Python 包可安装到自己的开发环境：

```powershell
python -m pip install -r requirements-dev.txt
```

选择需要的目标构建：

```powershell
# Windows 接收端
./build.ps1 -Target Windows

# WinCE 通用构建（供开发与移植，不是可直接部署到任意设备的完整接收端）
./build.ps1 -Target CeGeneric

# 查看或编译通用诊断工具
./build.ps1 -Target Tools -List
./build.ps1 -Target Tools
```

| 产物 | 默认位置 |
| --- | --- |
| Windows 程序 | `build/carplay/windows-x64/WinXPlay.exe` |
| WinCE 通用构建 | `build/carplay/ce-wm5-arm-generic/` |
| 通用工具 | `build/tools/` |
| 本地部署包 | `build/packages/` |

Windows 构建完成后可直接双击 EXE，无需启动脚本；移动到另一台电脑时保留同目录的 DLL、身份和配套工具。系统驱动与服务仍需在目标电脑上可用。

设备专属构建目标和安装步骤见对应设备文档。**编译与打包是两个步骤**：`build.ps1` 不会自动生成全部部署包；需要部署包时，按[打包指南](docs/build.md#打包)准备相应产物、验证结果和外部输入，再使用 `package.ps1`。打包只生成本地文件。

## 代码如何组织

协议核心保持 C++03，并通过最小能力接口使用传输、媒体和密码实现。代码按以下职责组织：

| 层次 | 负责什么 | 主要位置 |
| --- | --- | --- |
| 共享核心 | 协议、认证、配对、会话、媒体组帧和输入消息 | `carplay/core`、`carplay/interfaces` |
| 平台实现 | Windows/WinCE 的系统 API、窗口、网络、媒体接口与 USB 接入 | `carplay/platform` |
| 设备适配 | 设备参数、硬件及驱动差异、专属音频路由等兼容行为 | `carplay/devices` |
| 系统集成 | 特定设备原有界面的桥接、补丁及切换逻辑 | `firmware` |

例如，调用 WinCE 的 wave 音频接口属于平台实现；选择某台设备的输出通道、切换原有音源属于设备适配。使用窗口消息属于平台能力；从原有界面接管触摸、按键和显示层属于具体设备的系统集成。

```text
.
├─ carplay/
│  ├─ core/          协议、认证、配对、会话、媒体组帧与输入
│  ├─ interfaces/    平台能力契约
│  ├─ platform/      Windows、WinCE 及两者共用的系统实现
│  ├─ devices/       通用配置与具体设备适配
│  ├─ tools/         Windows USB 工具与 MRN 专属诊断
│  └─ tests/         接收端回归、合成对端与模拟器检查
├─ firmware/         设备专属 HMI 桥接与补丁生成器
├─ tools/            文件、进程、窗口及系统工具
├─ emulator/         WinCE 模拟器测试应用
├─ common/           通用诊断应用共享代码
├─ scripts/          共用构建、打包与检查逻辑
├─ tests/            工具及跨模块回归
├─ examples/         本地配置与任务示例
└─ docs/             使用、构建、架构与设备接口文档
```

`.local/` 保存个人环境配置，`build/` 保存可生成的产物，均不进入版本控制。源码可独立检出，构建不隐式读取仓库之外的研究目录；所需 SDK、原厂文件和身份通过明确配置提供。

维护代码可从[接收端架构](carplay/docs/architecture.md)和[代码布局](carplay/docs/code-layout.md)开始。设备适配采用的接口结论、代码位置和验证边界随源码维护在 `docs/devices/`，具体硬件和原有系统的集成细节在对应文档中展开。

## 验证与参与开发

配置好开发环境后，可运行 Windows 验证入口，其中包含架构边界检查、核心契约测试和 Windows 合成回归：

```powershell
./carplay/verify.ps1 -Target Windows
```

合成回归使用生成的身份和媒体，不需要真实配件身份；它覆盖协议、认证、媒体、输入及部分界面行为。CE 的工具、USB 和桥接另有编译后 ARM 回归，使用 API 替身检查边界条件。具体前提、入口与实机验收方法见[测试指南](docs/testing.md)。

欢迎贡献问题修复、设备适配、可复现测试和文档改进。报告问题时说明平台、固件、手机系统、程序版本、复现步骤及实际观察，并提供最小脱敏日志。协议或认证材料相关问题按[安全说明](SECURITY.md)处理，不公开上传身份、配对记录或完整私有运行目录。开发约定见 [CONTRIBUTING.md](CONTRIBUTING.md)。

常用入口：[故障排查](docs/guides/troubleshooting.md) · [诊断工具](carplay/tools/mrn/README.md) · [通用工具](tools/README.md) · [发布规则](docs/release.md)

## 许可与来源

除另有声明的文件外，项目自有代码采用 **GPL-3.0-or-later**，完整条款见 [LICENSE](LICENSE)。运行依赖与代码、协议来源见 [THIRD_PARTY.md](THIRD_PARTY.md)，第三方内容适用各自声明。

源码不包含专有 SDK、原厂固件、原车字库或真实身份材料。本地部署包可能包含使用者提供的身份或原厂衍生文件，应按[发布规则](docs/release.md)单独检查，不能直接作为公开发布包上传。
