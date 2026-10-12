# MRN 图形渲染与显示交接

适用范围见 [MRN 设备说明](../mrn.md)。基础结论来自 F3.21 ENTHMI2、DDRAW_IPU、媒体模块的指令及注册配置核对；当前实现分别在 CE 视频后端与 HMI 进程内桥接中。

## 三种状态必须分开

1. WinCE HWND 的前台、可见及 Z 序，用于窗口和消息管理。
2. 原厂 LayerManager / DirectDraw 表面的显隐和提交，用于硬件显示层协调。
3. 解码器和渲染器收到样本、完成绘制，以及屏幕实际可见的像素。

原厂主界面的 Hmi 图层编号为 0，采用 DirectDraw 表面并通过 EGL 绘制/提交。隐藏原厂 HWND 或反复把 WinXPlay 置顶，都不能替代原厂图层显隐流程。进程和窗口消失也不等于已显示像素被清除，返回原车需要恢复正确的表面并重绘。

F3.21 默认 LayerManager 配置为 `DdrawUpdateMethod=1`、`UseDDrawZOrder=0`；这不关闭 DDRAW_IPU 内部的 Overlay 排序。驱动存在特定双图层、32bpp/local-alpha 条件下的排序分支，但没有活动 Overlay 状态就不能据此判断一次遮挡的原因，更不能认为 32bpp 永久最高优先级。

## HMI 进程内的显示契约

以下为原厂 EXE 首选基址 `0x00010000` 下的 VA，只用于理解当前版本定位；运行时必须换算并校验，不能由外部进程直接调用。

| 原厂位置 | 作用 | 当前使用约束 |
| --- | --- | --- |
| `0x02113368` | 图层管理实例的全局引用 | 取实际实例，不创建第二个管理器 |
| 实例 `+0x0C`、`+0x58` | 共享表、递归互斥锁 | 先核对归属和状态，再在原厂 UI 线程获取锁 |
| `0x01DF3DC0` → `0x01DF4BD0` | LayerSetVisibility 包装及实现 | 只修改 Hmi 图层 0，保存原可见状态 |
| `0x01DF3DB8` → `0x01DF44DC` | 提交配置变化 | setter 之后必须提交，不能只写字段 |
| `0x02042038` | 原厂公共窗口过程 | 特定 CE 模块查询失败时用于候选基址定位，仍需完整指令检查 |

[display.h](../../../firmware/mrn/bridge/display.h)核验 setter、commit、锁创建指令及重定位引用；拒绝不匹配版本、错误线程和外部窗口。CE HMODULE 不是可直接当成 PE 基址的指针，分段映射的 AllocationBase 也不一定是整个映像起点。

桥接在 UI 线程零等待取锁，忙时通过定时器重试，不无限阻塞原厂 UI。保存管理器、共享表、表面标识及原可见值，只隐藏/恢复图层 0；如果期间实例或表面被替换，不把旧状态恢复到新对象。提交后观察变更标志，并请求目标窗口重绘。

`LAYER_SUBMITTED` 只代表已提交，`LAYER_STATE_PROCESSED` 只代表共享状态已处理，均不是实际像素成功证明。其他图层、倒车、休眠及异常退出下的恢复需单独验收。

## 实时视频管线

当前默认路径为：解密并组帧的 H.264 → 自有 DirectShow 内存源 → 原厂 H.264 解码滤镜 → 系统视频渲染器。图构建和媒体线程实现在平台层 [video_dshow.cpp](../../../carplay/platform/wince/video_dshow.cpp)，滤镜标识由 [MRN profile](../../../carplay/devices/mrn/profile.cpp) 提供；Generic 不选择这些滤镜。

| 组件 | 当前固定选择 |
| --- | --- |
| H.264 解码滤镜 | `{3D581965-7C6E-4FE9-894F-50FC821BAE68}` |
| 系统视频渲染器 | `{70E102B0-5556-11CE-97C0-00AA0055595A}` |
| 视频窗口 | IVideoWindow 的 owner 和 message drain 指向接收窗口，位置按客户区更新 |

图线程必须处理渲染器创建的窗口消息。退出时唤醒等待中的源流、停止图、隐藏渲染窗口并解除 owner/message drain，不能只结束应用 UI 消息循环。

[视频队列](../../../carplay/platform/common/video_queue.h)接收完整 Annex-B access unit，缓存 SPS/PPS，等待 IDR；上限为单样本 2 MiB、总量 4 MiB、8 个样本。不能无限积压，也不能丢失参考帧后仍按完整 GOP 继续消费。当前没有完整恢复手机 PTS，因此不能由名义 30fps 推导精确呈现时钟或长期音画同步。

原厂 USB 文件播放器通过 RenderFile 建立自己的图，不能靠把实时流不断写成 MP4 替代此内存源；其名为 SETSURFACE 的已分析命令最终为空分支，不是给第三方提供的外部表面接口。

当前接收端不直接调用 VPU 私有接口；构建不依赖 NXP BSP。原厂组件或 VPU 驱动存在、滤镜成功创建、样本被接收均不足以证明硬件加速、零拷贝或实际帧率。有限素材曾实际可见；IQualProp 绘制计数也曾与可见画面不一致，必须结合物理观察。

## 验证与维护

[HMI 层测试](../../../tests/test_hmi_layer.py)核对编译桥接及指定原厂 setter/commit 指令，覆盖锁、归属、重定位和表面替换；OS/显示完成由替身提供。[队列测试](../../../carplay/tests/native/video_queue_tests.cpp)覆盖媒体缓冲策略。它们均不能证明原厂 Overlay 已在实际屏幕隐藏或恢复。

固件补丁必须通过原件哈希检查，构建和更新流程见[桥接说明](../../../firmware/mrn/README.md)。移植时先重新建立模块定位、图层类型和恢复契约，不复制 F3.21 地址到其他固件，也不把这些地址下沉到通用窗口工具。
