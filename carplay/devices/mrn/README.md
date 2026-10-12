# MRN 设备适配

这里保存 MRN F3.21 的设备默认值、启动适配和音频实现。原厂接口及适用边界集中维护在 [MRN 设备说明](../../../docs/devices/mrn.md)，不在代码旁复制另一份研究时间线。

| 文件 | 维护职责 | 接口依据 |
| --- | --- | --- |
| profile.cpp | 分辨率、视频后端与滤镜 ID、显示名称及原车字体 | [设备配置](../../../docs/devices/mrn.md) |
| wince.cpp | 启动准备、安装互斥、窗口策略与音频后端装配 | [设备配置](../../../docs/devices/mrn.md) |
| usb_registration.h | F3.21 注册 API 空实现的受限回退 | [USB 注册与匹配](../../../docs/devices/mrn/usb.md) |
| network_diagnostics.h | F3.21 NCM 注册路径及驱动文件的只读诊断 | [USB 与网络](../../../docs/devices/mrn/usb.md) |
| audio_recovery.cpp | TTS 打开/写入失败后的退避重试、丢弃过期音频和诊断 | [音频输出](../../../docs/devices/mrn/audio.md) |
| portable.cpp / portable.h | 内部运行、身份准备及 USB 启动装配 | [USB 与网络](../../../docs/devices/mrn/usb.md) |
| usb_configuration.h / usb_loader_probe.h | 原厂 NCM 规则兼容、配置检查与驱动加载诊断 | [USB 注册与匹配](../../../docs/devices/mrn/usb.md) |
| audio_output.cpp / audio_pcm.h | TTS 通道、共享 SSI 输出及有界 PCM 队列 | [音频输出](../../../docs/devices/mrn/audio.md) |

触摸和原厂显示交接位于 [firmware/mrn](../../../firmware/mrn/README.md)，实时视频实现位于平台层。新增设备差异应放在相应适配中，不能把 OEM 地址、私有 IOCTL 或原厂窗口名称写入共享协议核心。
