# 接收端配置

构建工具链和身份文件路径见 [构建指南](../../docs/build.md)。本页描述已使用的运行选项；不将内部编译宏或测试开关列为用户功能。

| 选项 | 行为 |
| --- | --- |
| CARPLAY_AUDIO_OUTPUT | 默认取设备的 `audio.output`（平台默认开启）；0 禁用 CarPlay 音频输出能力及播放设备 |
| CARPLAY_MICROPHONE | 默认取设备的 `audio.input`（平台默认开启）；0 禁用麦克风能力及采集设备，上行仍由手机请求控制 |
| CARPLAY_AAC | WinCE 默认 1；设备选定的系统 DMO 通过解码自检后才声明普通 AAC；0 保留 PCM。输出关闭时不自检、不声明 AAC |
| CARPLAY_TOUCH | 默认 1；设为 0 禁用触摸能力 |
| CARPLAY_CONTROLS | 默认 1；设为 0 禁用控制器能力，与触摸独立 |
| CARPLAY_WAVE_IN | -1 使用系统默认话筒；可指定枚举得到的设备编号 |
| CARPLAY_WAVE_OUT | 仅标准 wave 输出后端读取设备编号；MRN TTS 后端固定使用经验证的设备 1，不读取此选项 |
| CARPLAY_IDENTITY | 配件身份目录，与手机配对记录不同；Windows 未设置时读取 EXE 同级 identity 目录 |
| CARPLAY_CRYPTO / CARPLAY_SSL | Windows OpenSSL 3 x64 DLL 路径；未设置时使用 EXE 同级 libcrypto-3-x64.dll / libssl-3-x64.dll |

Windows 通过右键设置管理显示参数，默认协商 1280×720；MRN 默认 800×480、最高 30fps。这些是配置值，不是测得帧率。视频保持宽高比，输入坐标使用实际画面区域。

帧率按设备定义在 `DeviceProfile.maxFPS`：MRN 使用 [MRN 配置](../devices/mrn/profile.cpp)，Windows 和 Generic CE 使用 [通用配置](../devices/generic/profile.cpp)，当前均默认 30。该值统一用于向手机声明 `maxFPS`、WinCE DirectShow 的媒体类型与样本时长、Windows MF/D3D11 的帧率参数与样本时间。修改某设备的配置只影响对应构建；这是构建时配置，修改后需要重新编译、更新程序并重新连接手机，不是运行时设置项。

当前程序接受 1～60 的整数作为配置范围；这是程序的输入校验范围，不代表已验证 iPhone 支持其中每个协商值，也不代表设备能达到该帧率。低于 30 的配置是否被手机接受，需要用持续动画时的接收日志确认。会话报告 `video_max_fps`、每秒日志 `max_fps` 和视频后台报告记录配置值，不能当作实测 FPS。

Windows 使用系统媒体后端，旧 FFmpeg 后端选项不再选择运行解码器。CE 的设备后端配置独立；不把 Windows 设置直接照搬到车机。

手机配对记录、设置和日志是运行数据。清理配对、修改音源或更新 DLL 前，应按相应使用说明操作；不要通过删除整个运行目录来排查一般连接问题。

## 设备音频开关

`DeviceProfile.audio` 的平台默认值为输入、输出都开启；MRN 在 `devices/mrn/profile.cpp` 中显式声明这两个独立开关和已核对的 AAC DMO CLSID。系统 COM、解码、线程和队列逻辑仍在 WinCE 平台层。当前 MRN 两路默认关闭，保留画面和触控，音频交由手机/原车蓝牙；通用 WinCE 与 Windows 默认仍开启。

WinCE 可在 EXE 同目录的 `carplay.plist` 中覆盖默认值，值必须是字符串 `"0"` / `"1"`。下面两项应合并进现有 `<dict>`，不要覆盖身份路径等其他配置：

```xml
<key>CARPLAY_AUDIO_OUTPUT</key><string>0</string>
<key>CARPLAY_MICROPHONE</key><string>0</string>
```

两个都为 0 时不声明音频格式、不打开播放或采集设备，保留视频和触控。只关闭其中一个只影响对应方向。蓝牙配对和原车音源选择仍由原车及手机管理，这两个开关不会自动连接或切换蓝牙。修改后退出并重新启动 WinXPlay、重新连接手机，使新会话重新协商；不支持会话内热切换。

AAC 自检只解码短静音素材，不申请音源、不播放。通过后声明普通 AAC-LC 音乐流；不声明缓冲 AAC。`AAC_PROBE`、会话报告的 `aac_offered` 和每路 `MEDIA_AUDIO codec` 区分“可用”“已声明”和“手机实际选用”；性能改善及音质仍需实车验证。

## 正常播放性能日志

正常 CarPlay 会话的每秒帧数、处理耗时与队列统计，见 [正常播放日志](media-logging.md)。
