# WinXPlay：HMI 内的标准窗口桥接

`WinXPlayBridge.dll` 由原厂 HMI 加载。WinXPlay 不链接或加载桥接 DLL，也不编入 MRN 客户端、握手、心跳或私有输入协议。

桥接维护前先阅读[触摸与按键接口](../../docs/devices/mrn/input.md)和[图形与显示交接](../../docs/devices/mrn/graphics.md)：前者说明原厂驱动的定向投递与手势归属，后者说明必须在 HMI 进程内完成的图层操作及版本限制。完整设备依据见 [MRN 设备说明](../../docs/devices/mrn.md)。

## 目录

```text
mrn/
├─ bridge/          # DLL：输入转发、显示层交接和界面切换
├─ hmi_patch/       # 原厂 EXE 补丁生成器、汇编模板及补丁打包
├─ build.ps1        # 构建 DLL；-WithPatch 同时生成 HMI 补丁
├─ package.py       # 输出已验证的补丁文件及配套更新包
└─ README.md
```

早期 BAND 补丁模板保留在外部研究工作区。其他设备在 `firmware/` 下与 `mrn/` 并列。

## 当前职责

- DLL：BAND 启动/寻找 WinXPlay、核对窗口所属进程、原厂图层隐藏/恢复、窗口切换、触摸坐标转换和白名单按键转发。
- WinXPlay：处理标准窗口消息及 CarPlay 消息。窗口隐藏时取消输入，但保留手机会话和后台自动连接。CarPlay 请求返回车机时隐藏自身窗口；明确退出才关闭连接。
- 固件钩子 ABI：`MrnBridgeStart` 返回 `MrnOnMessage`。已有兼容钩子的 HMI 更新桥接 DLL 时无需重新打补丁。

## 输入和切换

原厂 UI 线程将属于 WinXPlay 的事件排入 DLL 工作窗口；消息内的代次只用于 DLL 内部过滤旧事件。工作线程转换屏幕坐标到 WinXPlay 客户区，向其主窗口投递 `WM_LBUTTONDOWN/MOUSEMOVE/LBUTTONUP`、`WM_KEYDOWN/UP`，保留按键重复/释放参数。所有向 WinXPlay 投递的输入与取消消息由同一工作线程排序。

BAND 等原厂已有手势和白名单按键释放后才激活。返回时停止输入转发、发送 `WM_CANCELMODE`、隐藏 WinXPlay 并恢复原厂；未结束的手势不改投原厂。DLL 使用标准 `WM_SHOWWINDOW` 通知主程序显隐状态。WinXPlay 取消输入时也释放自己持有的鼠标捕获。

DLL 每 250ms 检查进程与窗口，使用 100ms 的 `WM_NULL / SendMessageTimeout` 检查响应；不要求应用发送心跳。窗口隐藏/消失或进程确认退出时恢复原厂。等待新窗口最多 8 秒；超时恢复原厂，不结束接收端进程。输入投递失败或响应超时停止本次转发并取消输入，BAND 重新进入后恢复；不因此中断手机连接。响应检查成功不等于每条投递消息都已处理。

只有原厂目标窗口的触摸和白名单标准键会被转发。音量/静音、其他原厂消息、倒车及电源相关消息继续原流程；仍不猜测 CAN 原始键值。不调用系统 SendInput，不向其他应用注入触摸。真实图层调用沿用原实现与固件指令核验。

## 构建和更新

```powershell
./firmware/mrn/build.ps1 -WithPatch
./carplay/scripts/build-wince.ps1
python tests/test_ab_interaction.py *> build/firmware/ab_bridge/verification.log
python tests/test_hmi_layer.py *> build/firmware/ab_bridge/layer-tests.log
python firmware/mrn/package.py
```

通用 WinCE 可使用主程序构建脚本的 `-Generic`；同样不包含 MRN 输入客户端。两份构建脚本仍支持 `-NoLogs`，分别输出至原目录的 `-nologs` 版本。

`WinXPlay.exe` 和 `WinXPlayBridge.dll` 必须使用配套版本。采用私有握手的旧 DLL 与当前主程序不兼容。桥接更新包只更新这两个文件，保留 USB 驱动、认证、配对和配置；不覆盖 HMI、不自动终止进程或重启。已加载 DLL 需要退出原厂 HMI 后才能替换，更新后重启车机加载新文件。

`package.py` 将已验证的 HMI/DLL 文件发布到 `build/packages/firmware/`，将主程序/DLL 更新包发布到 `build/packages/winxplay_bridge_update/`。首次安装 HMI 钩子所用的独立复制包，可在前者准备好后执行 `python firmware/mrn/hmi_patch/package-copy.py` 生成。

## 验证边界

ARM 指令测试使用系统 API 替身，覆盖标准消息参数、坐标转换、BAND 重入、旧代次丢弃、手势隔离、失败取消和退出恢复。原厂图层测试仍执行被核验的 setter/commit 指令。真实 Windows 窗口到加密 CarPlay 输入的测试另行运行。验证方法见 [测试指南](../../docs/testing.md)，实际支持边界见 [兼容性](../../docs/compatibility.md)。

标准 PostMessage 是定向消息投递，不等于完整系统触摸注入。跨进程窗口 API、WinCE 前台规则及 OEM 图层的实际表现仍需使用配套版本实车验证；其他窗口可操作不能证明 WinXPlay 的输入和显示交接正确。

原厂输入通过 WINXPLAY_OEM_ROOT 或单文件环境变量配置，见 [构建指南](../../docs/build.md)。普通 build.ps1 只编译桥接 DLL；-WithPatch 另外生成受哈希限定的 HMI 补丁。补丁输出含原厂代码，仅供本地部署，不能直接当作公开 Release。
