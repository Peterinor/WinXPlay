# 测试

## 无硬件结构检查

```powershell
python scripts/check-project.py
python carplay/tests/check_boundaries.py
python carplay/tests/wince/test_network_echo.py
```

检查本地文档链接、产品/研究边界、Python 语法、源码许可标记中的异常字符及核心头文件依赖。平台边界检查还禁止直接包含具体设备目录，以及混入 MRN 路径、滤镜 ID 或 NCM 注册项。不读取真实身份，不连接手机，不启动车机。

## 桌面合成回归

```powershell
./carplay/scripts/verify-contracts.ps1
./carplay/verify.ps1 -Target Windows
```

完整 Windows 验证需要编译器、两个 SDK、clang-cl、OpenSSL、FFmpeg 和 Python 开发依赖。测试生成合成证书和媒体，不需要真实配件身份。它会运行本机测试程序和隐藏窗口，但不会代替真实 iPhone 验收。

Windows 回归还会把 EXE、依赖 DLL 和合成身份复制到临时目录，清除三个运行路径环境变量，从另一工作目录验证本地签名、TLS 及隐藏窗口启动，并检查显式环境变量仍能覆盖默认位置。结果为 `build/carplay/windows-x64/runtime-layout-results.json`；此项不使用真实私钥。

`carplay/tests/windows/test-usb-repair.ps1` 使用设备、注册表及重启操作的替身，检查 USB 修复的备份、重复执行、恢复、设备变化和失败提示。可单独运行，也纳入 Windows 验证入口；不会修改本机注册表或重启真实设备。管理员授权流程和手机重新连接仍需实际环境验收。

`carplay/tests/windows/test-usb-repair-ui.ps1` 在 Windows PowerShell 5.1 中执行完整窗口入口，保留真实参数绑定、模块导入及按钮事件，仅用测试替身替换设备状态和提权启动。它检查开关参数与控件变量无冲突，并验证两个操作按钮的调用参数；可用 `-ToolDirectory` 指向分发目录中的 `tools/usb-repair`，验证实际交付脚本。测试不会显示可见窗口或触发真实提权。

## 编译后 ARM 回归

工具与跨模块测试在 `tests/`，使用 Unicorn 执行实际编译产物并模拟 OS API。部分测试使用外部 coredll.dll 的导入序号表，先按构建指南配置。

```powershell
./build.ps1 -Target Tools
python tests/test_file_manager.py
python tests/test_system_ops.py
python tests/test_diagnostics.py
python tests/test_window_ops.py
```

USB 驱动回归先构建 CeMrn 与 CeGeneric，再运行 `test_usbmux_registration.py`、`test_usbmux_attach.py` 和 `test_usbmux_bulk.py`。注册回归分别检查 MRN 的受限兼容回退及 Generic 不修改设备专属注册项的行为。补丁回归需要显式生成补丁，随后运行 `test_ab_interaction.py` 和 `test_hmi_layer.py`；结果写入 `build/firmware/ab_bridge/`。

`./carplay/scripts/test-mrn-audio.ps1` 在桌面编译并运行实际 MRN 混音与恢复实现，用替身替换 AVV0/wave 设备。覆盖六种采样率、共享输出、多缓冲、失败清理、退避重试、PCM 字节序和暂停/播放位置恢复，不打开真实音频设备。CE 版本的音频和 DirectShow 测试仍由对应模拟器脚本单独运行。

完整打包的准备顺序与日志保存位置见 [构建指南](build.md#打包)。生成全部 15 个目录后，执行 `python tests/test_usb_startup_packages.py`，检查 14 组 A/B 启动格式、摘要、运行文件一致性、许可文件、安装源文件和 ARM 配置解析。该检查需要完整本地部署包及原厂摘要输入，不能用三个抽样包代替。

产品回归覆盖本项目代码的行为与接口；原厂 PM/USBD 指令、hive 或录音机代码的研究验证在项目外保存。完整发现式运行前应准备对应构建和外部输入；默认结构检查不会把缺少这些条件误报为已通过硬件测试。

## 模拟器和实机

模拟器部署脚本在 `carplay/tests/wince/`，需要 Microsoft CoreCon 与用户自己的 WM5 模拟器环境。显式调用才会启动或部署客体程序。实车检查使用 [三个诊断入口](../carplay/tools/mrn/README.md)，每轮记录包哈希；物理效果由现场观察与日志分别确认。
