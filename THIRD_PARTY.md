# 许可与第三方来源

除另有明确声明的文件外，本项目自有代码统一采用 GPL-3.0-or-later，完整条款见根目录 LICENSE。

该声明不替代第三方许可，也不授权分发原厂固件、SDK、字库、配件身份或手机配对材料。各来源的 GPL `only` / `or-later` 范围仍须按固定版本原声明核实，不能只靠一份 GPL 全文推断。

| 来源 | 固定版本/用法 | 当前记录 |
| --- | --- | --- |
| [DiPlay](https://github.com/shihabal3amri/DiPlay/tree/4ce354d5c4393b1e770ea5772898c95947f759e8) | 4ce354d5c4393b1e770ea5772898c95947f759e8；C++ 协议报文布局与状态流程参考/改写 | 来源记录为 GPL-3.0；保留来源及贡献声明，发布前核查具体派生范围；不包含其 APK、Android 资源或认证身份 |
| [libusbmuxd](https://github.com/libimobiledevice/libusbmuxd) | USBMUX 协议参考，未链接或分发其库；Windows 通过 Apple 设备服务的 localhost:27015 通信 | 保留协议来源记录，不列为编译或运行库依赖 |
| [wolfSSL/wolfCrypt](https://github.com/wolfSSL/wolfssl/tree/1d363f3adceba9d1478230ede476a37b0dcdef24) | 5.9.1-stable，1d363f3adceba9d1478230ede476a37b0dcdef24；CE 和 CeHost 静态链接 | 开源 GPLv3；源码外置，通过 CARPLAY_WOLFSSL 配置；项目配置在 carplay/platform/common/user_settings.h |
| [OpenSSL 3](https://www.openssl.org/) | Windows 动态链接；本地构建从显式配置的位置复制 DLL 到运行目录 | Apache-2.0；实际发布时记录准确版本及再分发材料 |

根目录 LICENSE 与本文件是项目许可和来源声明入口，运行包复制为 LICENSE 与 THIRD_PARTY.txt。它们不替代实际分发依赖要求附带的许可、版权和 NOTICE 文件。tools、firmware、emulator、common 随本项目采用自有代码许可；发布前仍需逐文件核查来源，不能把目录位置当作第三方代码可重新许可的依据。
