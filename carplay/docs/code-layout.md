# 代码布局

| 目录/文件 | 当前职责 |
| --- | --- |
| core/protocol、plist、link | 报文和基础连接协议 |
| core/auth、identification、airinfo | 认证流程、识别与能力声明 |
| core/receiver、pairing、buffered_audio、input | 会话、配对、缓冲音频与输入状态 |
| interfaces | 传输、运行环境、媒体、密码及配对存储契约 |
| platform/common/receiver_context | 两平台共用的具体装配 |
| platform/common/lockdown、phone_pair | USB/TLS 启动及手机信任编排 |
| platform/common/connection_* | 共用连接界面、文字与字体选择 |
| platform/windows | 桌面 USB 服务、系统媒体、密码和窗口实现 |
| platform/wince | CE 系统、音视频、窗口与自有 USB 驱动 |
| devices/profile.h、devices/wince.h | 设备参数及 CE 构建时装配契约 |
| devices/generic、devices/mrn | 通用默认实现与 MRN 启动、滤镜选择、音频及 USB 注册兼容 |
| tools/mrn | MRN 专用 AV、连接、输入三个诊断入口，含原厂路由与桥接实验 |
| tests/native、windows、wince | 核心契约、合成对端、平台回归与模拟器部署 |

依赖由应用入口装配；core/interfaces 只引用公共契约和允许的标准头。`tests/check_boundaries.py` 检查这一点。common 中的 native_*、legacy_media 等仍是平台私有过渡实现，不能把它们当公共 SDK。

构建脚本保持在模块 scripts，项目级共用工具在根目录 scripts。所有产物进入根目录 build/carplay。只在实际实现需要时增加目录或接口，不预建空插件、空后端或承诺未存在的文件。
