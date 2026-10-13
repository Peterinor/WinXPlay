# 接收端架构

本文描述当前实现及明确的剩余边界；产品验证状态集中在 [兼容性表](../../docs/compatibility.md)。

## 分层

共享 `core/` 负责协议、认证顺序、配对状态、媒体组帧及输入控制；`interfaces/` 提供 transport、session_runtime、media、crypto_provider 四组最小契约。核心公共头不暴露 HWND、SOCKET、COM 或 OEM 路径。

`platform/windows/` 使用 Media Foundation、D3D11 与 wave；`platform/wince/` 使用 CE 系统接口、DirectShow 与 wave。`platform/common/` 是已证明共用的 Windows 家族代码，仍包含网络/线程装配、Lockdown 启动流程、连接界面与 wolfSSL 实现，不宣称它可直接移植到任意 OS。

`devices/` 提供默认参数和必要的设备行为，例如 MRN USB 注册兼容、音源和字体配置。原厂 HMI 的桥接位于项目级 firmware/mrn，协议核心不感知固件地址。

分层按 `CarPlay → platform（WinCE / Windows / 后续 Linux）→ device / firmware` 组织。没有证据表明设备存在差异时，普通系统 API 的调用和处理流程直接归平台层，不添加设备回调或策略接口。Linux 暂无实现。

WinCE 的身份/配对准备、USB 注册、NCM 规则检查与读回/回滚、驱动加载检查、网络地址校验和窗口初始化都在平台层。MRN 与 Generic 使用同一个启动入口；仍按 `CARPLAY_PORTABLE` 决定是否准备 USB。当前网络地址发现要求 IPv6 链路本地地址，属于现有媒体网络流程，与设备型号无关。

音频的 waveOut 线程、缓冲提交、共享混音和失败重试在 `platform/wince/`，纯 PCM 转换及队列在 `platform/common/`。MRN 仅选择设备 1、缓冲和等待参数，并实现 AVV0 私有音源申请/释放；Generic 继续选择标准 wave 后端。DirectShow 的图构建留在平台层，分辨率、帧率和滤镜标识留在设备 profile。

`devices/wince.h` 只负责实际设备选择和部署约束。USB 注册中已经证实的 F3.21 空实现检测留在设备层，注册表操作本身由平台执行。NCM 使用已有 usbcdcncm 驱动和已验证的匹配规则；共享实现不等于任意固件均已通过实机验证。

`CARPLAY_CE_DSHOW` 只选择 DirectShow 媒体路径，不控制音源、USB 或部署策略。USB DLL 的 `WINXPLAY_DEVICE_MRN` 单独选择 F3.21 注册 API 空实现的兼容策略；通用构建只调用系统注册 API。ActivateDeviceEx、描述符长度检查和 bulk 传输属于 CE API/USB 通用实现，不因最初在 MRN 上验证就全部归为设备代码。

## 装配与生命周期

应用入口创建具体后端，ReceiverServices 借用它们，ReceiverContext 必须比 Receiver 活得更久。会话持有工厂创建的媒体对象；接口明确约定提交数据是被消费还是被复制。平台对象和原生句柄留在平台侧，不建立动态插件框架。

连接从设备发现、手机信任、配件认证、网络准备进入媒体会话。断线关闭本次资源并按现有重试策略重新连接；等待手机信任和等待网络有不同的超时语义。用户退出取消后续重试。

窗口可见性与手机会话分开管理。返回原车隐藏窗口并释放输入，不等于断开手机或完成原车音源交接。每个新会话的自动 Home 操作仅在媒体和事件通道就绪、延时满足且用户尚未操作时尝试一次，实际目的页由手机决定。

## 媒体与能力

Windows 视频由 Media Foundation 解码、D3D11 呈现，AAC 由系统组件解码，PCM 交给 waveOut。FFmpeg 只生成开发素材。WinCE 平台提供 wave、GDI 与 DirectShow 实时内存源实现；是否选用 DirectShow、使用哪些滤镜由设备目标决定。Generic 没有已配置的视频解码后端；MRN 选择自己的解码滤镜与 TTS 音频输出。PCM 在平台入口完成必要的字节序转换；支持麦克风的配置随手机请求开启采集，流结束后关闭。

能力由实际媒体后端提供。CE 不声明 AAC；不把已有消息接口表述为已获得真实触屏或车载话筒。

DirectShow 视频队列有界，缓存 SPS/PPS 并等待 IDR；当前尚未完整恢复手机 PTS，不能宣称精确音画同步或完整定时呈现。WinCE 通用音频恢复对设备可恢复错误采用退避重开并丢弃过期数据；驱动调用自身永久阻塞不在该恢复保证范围内。

## 后续约束

显示、音频和输入归属分别处理；优先事件必须取消残留输入。持续时间戳、长时资源释放、倒车抢占和休眠恢复需要独立实现/验收，不能由窗口状态或媒体提交计数推导成功。
