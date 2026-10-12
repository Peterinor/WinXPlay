# 接收端配置

构建工具链和身份文件路径见 [构建指南](../../docs/build.md)。本页描述已使用的运行选项；不将内部编译宏或测试开关列为用户功能。

| 选项 | 行为 |
| --- | --- |
| CARPLAY_MICROPHONE | 默认 1；设为 0 禁用麦克风能力，上行仍由手机请求控制 |
| CARPLAY_TOUCH | 默认 1；设为 0 禁用触摸能力 |
| CARPLAY_CONTROLS | 默认 1；设为 0 禁用控制器能力，与触摸独立 |
| CARPLAY_WAVE_IN | -1 使用系统默认话筒；可指定枚举得到的设备编号 |
| CARPLAY_WAVE_OUT | 仅标准 wave 输出后端读取设备编号；MRN TTS 后端固定使用经验证的设备 1，不读取此选项 |
| CARPLAY_IDENTITY | 配件身份目录，与手机配对记录不同；Windows 未设置时读取 EXE 同级 identity 目录 |
| CARPLAY_CRYPTO / CARPLAY_SSL | Windows OpenSSL 3 x64 DLL 路径；未设置时使用 EXE 同级 libcrypto-3-x64.dll / libssl-3-x64.dll |

Windows 通过右键设置管理显示参数，默认协商 1280×720；MRN 默认 800×480、最高 30fps。这些是配置值，不是测得帧率。视频保持宽高比，输入坐标使用实际画面区域。

Windows 使用系统媒体后端，旧 FFmpeg 后端选项不再选择运行解码器。CE 的设备后端配置独立；不把 Windows 设置直接照搬到车机。

手机配对记录、设置和日志是运行数据。清理配对、修改音源或更新 DLL 前，应按相应使用说明操作；不要通过删除整个运行目录来排查一般连接问题。
