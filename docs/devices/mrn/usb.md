# MRN USB 设备与网络接入

适用范围见 [MRN 设备说明](../mrn.md)。本页基于 F3.21 USB 主机、设备管理器和 NCM 驱动行为及当前适配实现。USB 枚举、控制通道、手机信任、配件认证和媒体网络是独立阶段。

## 通路与职责

| 通路 | 实现 | 用途 |
| --- | --- | --- |
| Apple USBMUX 接口 `FF/FE/02` | 自有 WinXPlayUsbMux.dll、bulk 管道、流设备 `CMX1:` | USBMUX、Lockdown、手机信任及控制服务 |
| Apple CDC-NCM 接口 `02/0D/00` | 原厂 usbcdcncm.dll、NDIS、IPv6 | 媒体网络，不由 CMX 代替 |
| Apple 整机引导匹配 | 自有驱动检查实时配置描述符 | 当前配置缺少所需接口时准备配置切换 |

Apple VID 为 `0x05AC`（十进制 1452）。USB 配置值随手机和模式变化，不能硬编码某个序号等于 CarPlay。驱动选择同时包含 USBMUX 与 NCM 的实时配置，完成后把 NCM 接口留给 OS；既有 `bConfigurationValue` 只作为已保存偏好，不在每次启动时强制覆写。

CE7 主机扩展了 WM5 SDK 的 USB 结构。遍历配置与接口时必须按主机返回的 `dwCount` 前进，校验长度和对齐，只访问公共前缀，不能使用旧 SDK 的 `sizeof` 作为数组步长。实现见[描述符处理](../../../carplay/platform/wince/usb_driver/descriptors.h)。

## 两类注册问题不能混为一谈

### USB 客户端匹配注册

F3.21 的 USBD `RegisterClientDriverID` 与 `RegisterClientSettings` 存在直接返回 FALSE 的空实现。[设备注册策略](../../../carplay/devices/mrn/usb_registration.h)仅在 MRN USB DLL 构建中启用，只在两者都匹配已知 ARM/Thumb 空实现指令时采用受限注册表回退；一般失败、缺失导出或拒绝访问不能触发绕过。

自有键位于 `HKLM\Drivers\USB`：

```text
ClientDrivers\WinXPlayUsbMux
LoadClients\1452\Default\255_254_2\WinXPlayUsbMux
LoadClients\1452\Default\Default\WinXPlayUsbMux
```

第二项匹配 USBMUX 接口，第三项用于整机引导。写入 DLL 路径后必须读回验证。匹配项存在只代表注册准备完成，不代表 USBDeviceAttach 已执行。

### CMX 流设备激活

F3.21 设备管理器的 RegisterDevice / DeregisterDevice 入口存在返回 1060 的空实现，ActivateDeviceEx / DeactivateDevice 则有独立实现。当前[驱动](../../../carplay/platform/wince/usb_driver/driver.cpp)使用后者激活/释放 CMX。

配置键为 `HKLM\Drivers\WinXPlay\UsbMuxStream`，设置 DLL、`Prefix=CMX`、`Index=1`、`Flags=0`。ActivateDeviceEx 第四参数传给 CMX_Init 的第二参数，是当前驱动实例内的临时上下文，不是注册表路径；CMX_Init 必须验证同步激活时的 pending context。不把指针写入注册表或设置为 BuiltIn 自动启动输入。

## 原厂 NCM 匹配约束

原厂规则与新增 Apple 规则分别为：

```text
HKLM\Drivers\USB\LoadClients\65535_65535\0_0_0\2_13_0\USBCDCNCM
HKLM\Drivers\USB\LoadClients\1452\0_0_0\2_13_0\USBCDCNCM
```

DLL 均为 `usbcdcncm.dll`。不能把中间组改成 `Default`：该原厂驱动检查匹配所得 USB_DRIVER_SETTINGS 的设备类别，而不仅检查实时接口描述符，拒绝通配值 `0xFFFFFFFF`。保留 `0_0_0` 是已采用的兼容条件。

[usb_configuration.h](../../../carplay/devices/mrn/usb_configuration.h)先验证原厂 DLL 和规则，限定新增路径，保留冲突值，写入后读回，失败时回滚自身写入。不改原厂 DLL、不覆盖原厂规则或 Comm/IP 配置。注册变化需要重新枚举，不能据此报告 NCM 网卡已经就绪。

这一匹配修正已有指定 MRN 上出现 USBCDCNCM1、IPv6 链路本地地址及媒体 SETUP/RECORD 的记录，支持采用该规则；不代表所有手机、长时会话或物理音视频已验收。

## 会话与诊断边界

1. 确认自有驱动注册及真实 ATTACH、bulk 管道、CMX 激活；文件存在或 LoadDriver 成功不够。
2. 确认手机信任、Lockdown TLS 和配件认证；配件身份不是手机配对记录。
3. 确认 NDIS 的 NCM 网卡、非回环 IPv6 地址及实际网络通路；只有 `ASYNCMAC1` 和 `::1` 不算就绪。
4. 网络未就绪时继续处理控制流和必要的信任会话恢复，不能反复启动 CarKit 来替代网卡准备；媒体端点在地址可用后发布。
5. 最后分别验证媒体协议、解码/输出和真实画面声音，不能把手机“正在连接”视为成功投屏。

单 USB 口设备先部署至内部 `\NAND_USER\WinXPlay` 再接手机，步骤见[MRN 安装](../../guides/mrn-install.md)。部署用 U 盘的 AutoLaunch 启动链与手机 USBMUX/NCM 是不同用途。

回归入口：[注册](../../../tests/test_usbmux_registration.py)、[挂接](../../../tests/test_usbmux_attach.py)、[bulk 生命周期](../../../tests/test_usbmux_bulk.py)、[Apple 配置](../../../carplay/tests/wince/apple_usb_configuration_test.cpp)。测试使用编译代码和 API 替身，不代替真实插拔、主机控制器、驱动绑定或 IPv6 验收。
