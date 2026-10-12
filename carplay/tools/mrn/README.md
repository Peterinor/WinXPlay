# MRN 专用诊断工具

本页适用于 MRN F3.21 的 AV、连接与输入测试。工具包含原厂音源、滤镜和界面桥接操作，使用前先确认 [设备范围](../../../docs/devices/mrn.md)。

## 默认 AV 流程：音频、视频、同步

复制 `build/packages/winxplay_test_av/AutoLaunch` 或 `AUTOLAUNCH_PROD` 整个文件夹到 U 盘根目录，二选一即可。不需要修改配置、不需要手机，也不需要按原车语音键或切换音源。测试保留原厂 AV 服务运行，停用旧的杀进程、重启及其他音频测试启动项。

程序启动后等待 **10 秒**，自动按顺序执行：

| 阶段 | 正常情况下的内容 | 观察项目 |
| --- | --- | --- |
| 1. 音频 | 自动申请 TTS，等待 2 秒；20 秒声道对照：先静音 2 秒，再做两轮“双声道 1 声 → 仅左 2 声 → 仅右 3 声” | 是否有声、是否分左右、原音源是否仍在 |
| 2. 视频 | 间隔 2 秒后，播放约 6 秒 800×480 / 30 fps 视频；含运动图案、闪白和帧号；不申请音频通道 | 是否看见画面，是否流畅；原音源可能继续响 |
| 3. 同步 | 间隔 2 秒，重新申请 TTS 并等待 2 秒；同时播放约 6 秒视频和双声道短音 | 每秒闪白是否与短音对齐，有无明显延迟/漂移 |

正常总时长约 50 秒，加上媒体组件初始化和清理时间；建议从程序启动起等约 65 秒后取日志。完成后关闭窗口并退出，没有额外 30 秒倒计时。若某阶段失败，会记录原因并继续下一阶段；用户取消、音频资源清理失败或底层调用阻塞时，不能保证执行完后续阶段。点击测试窗口或 ESC 会取消整轮；运行时无需点击。

声道阶段的听音顺序如下，每组占 3 秒，短音间隔 0.5 秒，其余时间静音；通道在两轮之间保持申请，不重新打开：

| 相对 PCM 开始 | 输出 |
| --- | --- |
| 0–2 秒 | 静音，用于给输出链留出启动时间 |
| 2 秒 | 双声道同响 1 声 |
| 5、5.5 秒 | 仅左 2 声 |
| 8、8.5、9 秒 | 仅右 3 声 |
| 11 秒 | 双声道同响 1 声 |
| 14、14.5 秒 | 仅左 2 声 |
| 17、17.5、18 秒 | 仅右 3 声 |
| 20 秒 | 本阶段结束并释放通道 |

预期两轮均听到 **1—2—3**，共 12 声。请记录每组是否出现、来自哪些扬声器；不要只报总数。如果两轮固定缺同一组，更支持通道选择问题；如果只有第一轮缺声、第二轮完整，更支持启动时序问题，仍需结合日志判断。同步阶段仍是每秒 1 声，共约 6 秒，不使用这个 1—2—3 模式。

日志为 EXE 同目录的 `avtest-0001.log`、`avtest-0001.plist`，再次运行编号递增；`suite-result.plist` 是最近一次入口结果。日志用 PHASE_BEGIN/PHASE_END 区分阶段，plist 的 phases 数组保存每阶段缓冲数、播放位置、视频完成/帧统计、启动时刻和通道释放结果。阶段结束即保存一次报告。请同时反馈实际画面、左右声道和声画是否对齐；日志的 API 完成不等于物理画面/同步成功。

所有带声音的实车模式都使用 **waveOut 设备 1 + TTS 通道自动申请/释放**，10 个独立 100 ms 缓冲持续补充。默认 `suite.plist` 为 `mode=play, delay_seconds=10`，`test.plist` 为 `mode=full, wave_device=1`；设备 0 配置会被拒绝。不使用 USB/ESAI 私有解除静音方式，不修改音量、不终止原厂进程。原厂显示层仍可能盖住普通窗口；请求测试窗口置顶不等于取得显示层控制权。

TTS 使用 `AVV0: / 0x33 / 单字节 4` 申请与 `0x34 / 单字节 4` 释放。当前两轮声道对照仍待实车验证，已有出声记录不能证明左右声道正确分离。图像与 PCM 按同一 6 秒素材时间线制作，记录各自启动时刻，但尚未以音频硬件时钟驱动视频。测试用于测量可见声画差异，不保证精确同步；完整接收链与长期音画同步仍需独立验收。

保留 `voice-pcm` 单独基线模式：将 `suite-voice-pcm.example.plist` 复制为 `suite.plist`，等待 10 秒后自动申请通道并播放约 20 秒双短音，日志仍为 `voice_pcm_编号.txt`。该模式忽略 `test.plist`。如只测一项，将 `suite-play.example.plist` 复制为 `suite.plist`，再将 `playback-audio`、`playback-video` 或 `playback-sync.example.plist` 对应示例复制为 `test.plist`；恢复完整测试用 `playback-full.example.plist`。

日常只发布三个测试 EXE。各自链接所需实现，不启动旧的测试 EXE。开发回归使用的模拟驱动、合成配对记录和故障注入用例留在 tests 中，不打进实车包。

| 名称 | 范围 |
| --- | --- |
| winxplay_test_av.exe | 媒体组件检查、音视频播放、麦克风录音、MRN 音源实验 |
| winxplay_test_connect.exe | USB、网络、手机识别和配对、正常接收会话及重连 |
| winxplay_test_input.exe | 标准输入、适配器消息、MRN 原厂桥接输入 |

本套件针对 MRN F3.21，包含设备专属音源、滤镜与 HMI 桥接操作。使用 WinCE API 的检查项不意味着整套工具适用于任意 CE 设备；移植时需重新确认硬件、固件及操作契约。当前只构建 WinCE ARM 二进制。

## 构建与打包

在工程根目录执行 `./carplay/scripts/build-vehicle-tests.ps1`。可用 `-Target av`、`connect`、`input` 单独编译。输出位于 build/carplay/vehicle-tests。打包前用项目 Python 执行 carplay/tests/wince/av_fixtures.py 生成并校验测试素材。

使用项目 Python 执行 `carplay/scripts/package-vehicle-tests.py`。固定输出为 `build/packages/winxplay_test_av/`、`winxplay_test_connect/`、`winxplay_test_input/`。每个包包含 `AutoLaunch` 和 `AUTOLAUNCH_PROD` 两套目录，目录内为 EXE、配置和必要运行文件，不附 ZIP 或源码。选择一个测试包的一套目录，整体复制到 U 盘根目录；不能把多个测试包的启动文件互相覆盖后当成同时启动。重复打包更新同一目录和默认配置。程序也能复制到可写内部目录后用文件工具 RUN 启动。打包不会改写实际 U 盘。

## 选择测试

每个 EXE 读取同目录 suite.plist 的 mode；直接启动时也可用命令行模式覆盖。delay_seconds 范围 0–120。同目录 suite-模式名.example.plist 配置复制为 suite.plist 使用，每次启动选择一个入口模式；AV 的 play/full 会自动依次执行音频、视频、同步三阶段。

### 麦克风录音测试

`winxplay_test_av.exe` 的 `microphone` 模式测试 WinCE `waveIn` 输入。使用已核对的 F3.21 录音机音源申请接口，不需要连接 iPhone 或打电话，不结束原厂程序，不修改固件。

将 `suite-microphone.example.plist` 复制为 `suite.plist`；也可以打包时使用 `--target av --av-mode microphone` 直接生成默认录音包。使用独立窗口类 `winxplay_microphone_test`、`WS_EX_TOPMOST` 和独立 UI 消息线程。启动等待 10 秒后自动显示并录音，无需 BAND，可以与未进行音频采集的 WinXPlay 共存，不注册 WinXPlay 窗口类、不参与 A/B 桥接握手。旧版 display 配置会被记录为忽略。UI 每 250ms 刷新，即使录音 API 阻塞也不阻塞窗口消息线程。

枚举设备后，仅对实车已发现的 waveIn 设备 0 测三段，每段默认 6 秒，均为 PCM16 单声道：

1. `baseline-16k`：16000 Hz，不改音源。
2. `baseline-22k`：22050 Hz，不改音源。
3. `dictaphone-22k`：向 AVV0: 发送源 0x10 的可用、选择请求；等待 2 秒，再以 22050 Hz 录音。

持续说“1、2、3”，停顿后再说。默认从程序启动到退出约 35 秒，另加 API 耗时。结束后按原厂顺序发送不可用、取消选择请求，记录返回值，然后显示 5 秒倒计时并退出、恢复之前的前台窗口。申请失败则跳过第三段，仍尝试释放；取消和异常也进入释放流程。2 秒等待不代表已收到路由激活确认，释放请求成功也不等于已核实原音源恢复。

此模式是独占录音实验：测试期间保持原车录音机、语音识别、电话和 WinXPlay 麦克风采集空闲，不要同时启动 A、B 两套包。当前未实现原厂会话归属查询，不保证与另一录音会话共用源 0x10。无需手动切换音源或按原厂语音键。该音源申请流程仍待有效人声实测，不能仅据代码存在认定录音问题已解决。

EXE 同目录生成递增的 `microphone-1/`、`microphone-2/` 等**结果目录**，不覆盖旧记录：

- `capture.log`：设备、格式、每个初始化/清理 API 的返回码，以及每秒字节数和左右通道峰值。
- `report.plist`：设备能力、各格式的打开结果、采集字节、缓冲数、每声道非零/削波样本数、峰值和平均绝对幅度。
- 另记 `ui_start_error`、`foreground_accepted`、`paint_count`、`ui_thread_exited`、`window_destroyed`、`route_requests`、`route_release_requests_ok`、`signal_captures`；capture.log 包含 `MICROPHONE_V4`、`WINDOW_READY`、音源请求和录音进展。不生成桥接状态，不覆盖内部 B_INTERACTION.log。
- `阶段-device-0-RATEhz-1ch.wav`：各阶段独立保存，直接在电脑上回听。零数据时也可能存在只有头部的 WAV。

格式查询失败后仍尝试实际打开，以兼容查询实现不完整的旧驱动。设备号来自 `waveInGetNumDevs`，**不能把播放设备 1 当成录音设备 1**。如果驱动未释放缓冲区，则保留内存并停止后续尝试，不对它继续复用或强制结束线程。个别驱动 API 本身若不返回，测试也不能保证定时退出；日志在调用前后落盘用于定位。

`scan_completed` 表示三段尝试及清理均完成；`capture_completed` 需要采到数据；`signal_detected` 只表示非零采样，噪声也会满足。全零 PCM 明确记为 ALL ZERO，不能当作有声音。退出码 0 需要至少一段有非零采样且整轮完成，2 表示无信号或未完成，1 表示异常/文件错误。`voice_verified` 始终为 false，须结合 WAV 回听。这个测试不验证 CarPlay 音频上传、全双工通话和回声消除。录音文件保存在本地，不自动上传、不自动回放。

### AV

- play：默认入口，读取 `test.plist`。full 为完整三阶段；audio / video / sync 为单项。音频统一使用设备 1，自动申请 TTS。
- voice-pcm：独立的已出声基线测试，使用设备 1；该模式忽略 `test.plist`。
- microphone：录音设备枚举、格式尝试、采样电平和 WAV 保存，读取 suite.plist 的 seconds（默认 6，范围 1–30）。
- inspect：只检查媒体组件和设备，输出 media-probe.plist。
- instantiate：额外尝试创建指定媒体组件。

AV 的 --selftest 等命令是开发模拟器检查，不代表物理音视频成功。avtest-编号.log/plist 保留每轮记录。

### 连接

- inspect：网络及自有 USB 控制设备状态检查，读取 network.plist；不注册驱动、不配对。
- usb-register：复用正式程序的便携启动/USB 注册流程，需要同目录真实 identity 和配置以及 WinXPlayUsbMux.dll。注册会改变自有 USB 驱动配置。
- phone：打开现有 USB 通路并枚举手机；可能申请 USB 模式切换，需要重新枚举后重试。
- pair：枚举并执行正常手机配对，手机可能要求解锁和信任。配对密钥不写入诊断日志。
- session：直接复用正式接收端实现，在当前进程中连接、接收和自动重连；并非启动另一个测试 EXE。按正式程序方式提供身份、设置和驱动，关闭窗口结束会话。音视频可能随会话启用。

phone/pair/session/usb-register 前应结束正在运行的 WinXPlay，不能并发占用 USB 或同名窗口。发布包不附带身份和配对秘密；可把测试 EXE 复制到已有、已停止运行的 WinXPlay 内部目录复用配置。真实手机连接通过生产链路验证，合成配对和模拟驱动只留给开发回归。

结果包括 connect-编号.log、nettest-编号.log/plist、phone-test.plist，session 使用正式程序同名诊断文件。准备运行新一轮前先收集上一轮文件。

### 输入

- standard：显示独立测试窗口，记录自身收到的标准鼠标/键盘以及 WinXPlay 适配消息。
- bridge：创建初始隐藏的 WinXPlay 类窗口，按 BAND 后由已安装的新版 WinXPlayBridge.dll 找到并显示。只接收标准窗口消息，不进行私有握手；期间不能运行正式 WinXPlay。不改原厂窗口名，不杀原厂程序，不自行安装桥接补丁。
- seconds 为 1–300，默认 60；ESC 或超时关闭窗口并退出，DLL 检测到窗口消失后恢复原厂。窗口被原厂显示层覆盖时日志仍可采集，创建窗口不等于屏幕可见。

input-编号.log 区分 windows-input、adapter-input 和 window-state；后者包括 WM_SHOWWINDOW 和 WM_CANCELMODE。触摸坐标按实际收到的消息记录；没有消息不能推断驱动没有产生触摸，也不把 TSD 缓存当成新触摸事件。窗口消息记录不等于物理显示层恢复成功。

## 结果边界

suite-result.plist 是最近一轮入口结果，physical_effect_verified 始终为 false，实车听音、画面和操作反馈另行确认。completed 仅说明该模式按接口返回完成；不等于成功投屏、扬声器可听见或触摸已抵达手机。完整会话沿用原程序日志判断，不只看退出码。
