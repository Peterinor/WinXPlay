# 构建与外部依赖

在项目根目录执行命令。输出统一位于 `build/`；脚本、配置样例和必需源码不放入 build。

## 配置

PowerShell 构建入口加载 `scripts/environment.ps1`，可读取本地 `.local/settings.ps1`；该文件不提交。参照 [配置样例](../examples/settings.ps1) 设置环境变量，也可以在当前终端直接设置。Python 脚本使用当前进程的环境变量；独立运行前执行 `. ./scripts/environment.ps1`。

| 变量 | 用途 |
| --- | --- |
| CARPLAY_VC / MRN_CE_VC | VS2008 VC 根目录，需包含所选目标的 x64 或 ARM 编译器 |
| CARPLAY_SDK | 桌面旧 Windows SDK 根目录，用于现有公共构建 |
| CARPLAY_CE_SDK / MRN_CE_SDK | 当前 ARM 构建使用的 WM5 PocketPC SDK 根目录 |
| CARPLAY_MODERN_SDK | Windows 10/11 SDK 根目录，支持标准 Lib/版本/um/x64 和离线包 um/x64 布局 |
| CARPLAY_MODERN_SDK_VERSION | 可选，固定 Include 下的 SDK 版本；未指定时选择具有媒体头文件的最高版本 |
| CARPLAY_CLANG | 可用的 x64 clang-cl.exe，用于 Windows 系统媒体实现 |
| CARPLAY_WOLFSSL | wolfSSL 5.9.1-stable 固定源码目录，版本见第三方声明 |
| CARPLAY_CRYPTO / CARPLAY_SSL | OpenSSL 3 x64 DLL 来源；Windows 构建自动复制至 EXE 目录，运行时可选覆盖 |
| CARPLAY_FFMPEG | 开发测试素材生成器 ffmpeg.exe，接收端运行不使用 FFmpeg |
| MRN_PYTHON | Python 3.10 或以上解释器；默认使用 PATH 中的 python |
| CARPLAY_IDENTITY | 用户自行准备的身份来源目录，含 certificate.p7b、identity.pk8；Windows 构建和本地接收端打包使用 |
| WINXPLAY_OEM_ROOT | 可选原厂输入目录；仅设备补丁、方案 A 打包及部分 ARM 验证需要 |
| WINXPLAY_CORECON_DLL | 可选，Microsoft.Smartdevice.Connectivity.dll 完整路径；模拟器工具默认检查系统 Common Files 的标准安装位置 |
| WINXPLAY_EMULATOR_DEVICE | CoreCon 设备显示名；默认 USA Windows Mobile 5.0 Pocket PC R2 Emulator，可按已安装语言和设备修改 |

Python 开发依赖见 [requirements-dev.txt](../requirements-dev.txt)。专有 SDK、工具链、模拟器 ROM 和 OEM 文件由使用者另行准备，不随源码分发。当前 ARM 产物采用 WM5 SDK；这不是对所有 CE6/WEC7 设备的兼容承诺。

开发、构建和测试工具按以下用途准备，不属于接收端运行依赖，也不随运行包分发：

| 工具或开发包 | 用途 |
| --- | --- |
| Python | 执行构建辅助、打包和测试脚本 |
| FFmpeg | 生成、处理合成音视频测试素材 |
| cryptography | 合成认证、TLS、身份材料校验及协议测试 |
| pefile | PE 检查和 DLL 依赖解析 |
| unicorn | ARM 指令回归和启动摘要计算 |
| Pillow | 界面截图分析 |
| Microsoft 工具链、SDK、CoreCon | 编译、链接及模拟器部署测试 |

这些工具及开发包适用各自上游许可。Python 包版本由 requirements-dev.txt 固定；合成测试身份不等于可用的真实配件身份。

## 证书与私钥位置

在被忽略的 `.local/settings.ps1` 中设置来源目录，例如：

```powershell
$env:CARPLAY_IDENTITY = 'C:/private/winxplay/identity'
```

该目录包含 `certificate.p7b`（DER 编码的 PKCS#7 配件证书）和 `identity.pk8`（DER 编码的未加密 PKCS#8 私钥），两者必须匹配。这里只配置路径，不把证书或私钥正文写入脚本。配置样例见 [settings.ps1](../examples/settings.ps1)。

- Windows：执行 `./build.ps1 -Target Windows` 后，身份复制到 `build/carplay/windows-x64/identity/`，OpenSSL DLL 位于 EXE 同目录。无日志或自定义输出使用各自的输出目录。程序可直接双击启动，布局见 [Windows 使用说明](guides/windows.md)。
- MRN：编译只生成程序；`./package.ps1 -Target Receiver` 或 `All` 在打包时复制身份。安装包位于各启动方案的 `payload/identity/`，部署后为 `\NAND_USER\WinXPlay\identity\`；U 盘直接运行包位于 EXE 同级 `identity/`。

WinCE 接收端同样从运行目录的 `identity/` 读取身份，但 Generic 不采用 MRN 的安装目录或 U 盘部署流程。

Windows 与 MRN 打包共用身份准备逻辑：验证证书和私钥匹配，并生成 `certificate.der`。不自动复制手机配对记录。Windows 未配置身份来源时仍可编译，也可以手动准备运行目录；配置了来源但文件缺失或不匹配则报错。重新构建会更新已配置来源对应的身份，未配置来源不会删除已有身份。所有包含真实身份的输出都属于本地私有文件。

## 编译

```powershell
./build.ps1 -Target Windows
./build.ps1 -Target CeHost
./build.ps1 -Target CeMrn
./build.ps1 -Target CeGeneric
./build.ps1 -Target Tools -List
./build.ps1 -Target Tools
./build.ps1 -Target Emulator
./build.ps1 -Target Firmware
```

| 接收端目标 | 平台与设备选择 |
| --- | --- |
| Windows | Windows 系统媒体与通用设备配置 |
| CeHost | 桌面上的 CE/wolfSSL 路径验证，采用通用配置，不加载 MRN 字体或设备策略 |
| CeGeneric | CE 系统 API、标准 wave 与通用配置；没有已配置的视频解码后端，不启用 MRN USB 回退或安装流程 |
| CeMrn | CE 平台加 MRN 设备配置、DirectShow 滤镜、TTS 输出及 USB/启动兼容 |

Tools 默认只编译，不向车机安装，不要求打包所用原厂文件。单模块构建入口仍可使用。Firmware 默认只编译桥接 DLL；需要生成受版本和哈希限定的 HMI 补丁时，显式执行 `./firmware/mrn/build.ps1 -WithPatch`。

Windows 默认输出 `build/carplay/windows-x64/WinXPlay.exe`；MRN 为 `build/carplay/ce-wm5-arm-mrn/`；通用工具为 `build/tools/工具名/`；桥接 DLL 为 `build/firmware/ab_bridge/`。`-NoLogs` 构建使用独立的 `-nologs` 输出，不覆盖带日志版本。

## 原厂输入

`scripts/external_assets.py` 从 `WINXPLAY_OEM_ROOT/文件名` 读取输入；也支持单文件覆盖，例如 `WINXPLAY_OEM_COREDLL_DLL`、`WINXPLAY_OEM_ENTHMI2_EXE` 和 `WINXPLAY_OEM_SSW_AUTOLAUNCHER_DLL`。未配置时只检查被忽略的 `.local/oem/`，不搜索父目录。

| 输入 | 使用范围 |
| --- | --- |
| SSW_AUTOLAUNCHER.DLL | F3.21 方案 A 启动摘要；执行原摘要算法，不假设等于标准 SHA-256 |
| coredll.dll | 指定设备的导入序号核对及 ARM API 模拟；不是完整系统模拟 |
| ENTHMI2.EXE | F3.21 HMI 补丁输入，构建器校验完整 SHA-256 |

不复制原厂文件进入源码。补丁只生成新文件及还原清单，不就地改写输入。字体绘制检查需显式提供 `-FontPath`，字库不随项目分发。

## 打包

以下统一打包目标面向 MRN 的 A/B 启动与部署约定，通用工具也使用该设备的包格式。它们不是通用 WinCE 安装器；Windows 运行目录由 Windows 构建准备，CeGeneric 的部署由移植者按目标设备实现。

编译与打包是两个步骤：`build.ps1` 不会自动生成全部部署包。在项目根目录使用统一入口：

```powershell
./package.ps1 -Target Tools
./package.ps1 -Target Firmware
./package.ps1 -Target Receiver -NoLogs
./package.ps1 -Target All -NoLogs
```

| 目标 | 输出 |
| --- | --- |
| Tools（默认） | 7 个通用工具包及 AV、连接、输入 3 个测试包 |
| Firmware | firmware、firmware_install、winxplay_bridge_update |
| Receiver | winxplay_install、winxplay_usb；身份来自 CARPLAY_IDENTITY 或 -Identity |
| All | 上述全部 15 个目录 |

输出统一在 `build/packages/`，其中 14 个包同时生成 A/B 两种启动目录；`firmware` 是补丁文件目录。`README.md` 和 `inventory.json` 记录本轮生成清单及文件 SHA-256；旧目录不会被标记为本轮已验证。`build/validation/package-all.json` 记录执行状态，任何子步骤失败都会停止，不会报告全部成功。

统一入口只打包已经编译并通过对应校验的产物。首次完整准备可按顺序执行：

```powershell
. ./scripts/environment.ps1
./build.ps1 -Target Tools
./build.ps1 -Target CeMrn
./carplay/scripts/build-vehicle-tests.ps1
& $env:MRN_PYTHON ./carplay/tests/wince/av_fixtures.py
./firmware/mrn/build.ps1 -WithPatch
& $env:MRN_PYTHON ./tests/test_ab_interaction.py *> ./build/firmware/ab_bridge/verification.log
& $env:MRN_PYTHON ./tests/test_hmi_layer.py *> ./build/firmware/ab_bridge/layer-tests.log
```

两项桥接验证必须成功；打包器核对其 JSON 结果与当前二进制哈希。要沿用无日志接收端安装包，再准备无日志产物：

```powershell
./carplay/scripts/build-wince.ps1 -NoLogs
./firmware/mrn/build.ps1 -NoLogs
$env:WINXPLAY_TEST_NO_LOGS='1'
try {
    & $env:MRN_PYTHON ./tests/test_ab_interaction.py *> ./build/firmware/ab_bridge-nologs/verification.log
} finally {
    Remove-Item Env:WINXPLAY_TEST_NO_LOGS
}
./package.ps1 -Target All -NoLogs
```

`-NoLogs` 只选择两个接收端部署包中的无日志主程序、USB DLL 和桥接 DLL；测试工具、固件诊断包及 `winxplay_bridge_update` 保留日志。省略该参数则选择带日志接收端；缺少选定版本会明确报错，不会回退到另一版本。打包不写实际 U 盘或车机，不从项目上层的历史 build 复制旧包。

各模块入口仍可单独使用，例如只生成方案 B 或一个测试包：

```powershell
. ./scripts/environment.ps1
& $env:MRN_PYTHON tools/file_ops/package.py --scheme B
& $env:MRN_PYTHON tools/package.py --scenario window_probe --scheme B
& $env:MRN_PYTHON carplay/scripts/package-vehicle-tests.py --target av
```

方案 A 和默认双方案打包需要原厂摘要输入。方案 B 的启动校验不需要该摘要 DLL，但工具包的编译后 API 回归仍可能需要 coredll.dll。不要为了让打包成功而跳过已有校验。

接收端安装包的 `package-usb-test.py` 会按显式 `--identity` 参数复制真实身份，生成的是本地私有部署包，不是可直接上传的公开 Release。用 `--help` 查看参数，参见 [发布规则](release.md)。

重复打包更新程序及默认配置，请另存自己的任务配置。file_ops 默认导出一份 cmd.exe；process_ops 和 window_ops 默认需填写目标后才执行；system_ops 默认请求重启。具体操作以包内说明为准。
