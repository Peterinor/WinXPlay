# MRN 设备适配

这里仅保留设备参数、选择和已证实的固件差异。普通 WinCE 系统调用及其处理流程放在平台层，不能因为最早在 MRN 上验证就视为 MRN 私有实现。接口依据见 [MRN 设备说明](../../../docs/devices/mrn.md)。

| 文件 | 责任 |
| --- | --- |
| profile.cpp | 分辨率、帧率、后端与滤镜 ID、音频输入/输出开关、AAC DMO 选择、显示名称和字体 |
| wince.cpp | 部署互斥约束、音频后端选择与能力说明 |
| audio_route.cpp / audio_route.h | AVV0 私有 TTS 音源申请/释放，设备号及已验证缓冲参数 |
| usb_registration.h | F3.21 USB 注册 API 的已知空实现检测 |

共享实现：

- [WinCE 启动](../../platform/wince/portable.cpp)：身份/配对、USB 注册与 NCM 准备。
- [NCM 配置](../../platform/wince/usb_configuration.h)、[驱动加载检查](../../platform/wince/usb_loader_probe.h)、[网络诊断](../../platform/wince/network_diagnostics.h)和[注册表注册](../../platform/wince/usb_driver/registry_registration.h)。
- [waveOut 混音](../../platform/wince/wave_mixer.cpp)、[音频重试](../../platform/wince/audio_recovery.cpp)和 [PCM 队列](../../platform/common/pcm_queue.h)。

原厂触摸和显示交接位于 [firmware/mrn](../../../firmware/mrn/README.md)，实时视频在平台层。新增设备差异须有具体依据；不预先为普通 API 调用增加一套适配接口。
