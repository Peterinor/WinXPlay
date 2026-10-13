# 文档索引

所有操作路径以项目根目录为基准；本地根目录名称可以是 apps。

| 主题 | 文档 |
| --- | --- |
| 项目边界和目录归属 | [项目结构](project-layout.md) |
| 工具链、外部输入和构建命令 | [构建](build.md) |
| 当前功能和实测限制 | [兼容性](compatibility.md) |
| 自动检查、ARM 回归和实机验收 | [测试](testing.md) |
| 发布物与源码对应关系 | [发布](release.md) |
| 下一步工作 | [路线图](roadmap.md) |
| Windows 运行与操作 | [Windows](guides/windows.md) |
| 508 MRN 使用取舍、USB 带宽与卡顿提示 | [MRN 使用提示](guides/mrn-usage-notes.md) |
| MRN 部署、更新与恢复 | [MRN 安装](guides/mrn-install.md) |
| 故障定位与日志 | [故障排查](guides/troubleshooting.md) |
| MRN 接口约束和适配结论 | [MRN 设备说明](devices/mrn.md) |
| MRN 驱动与原厂接口依据 | [触摸](devices/mrn/input.md)、[音频](devices/mrn/audio.md)、[图形](devices/mrn/graphics.md)、[USB](devices/mrn/usb.md) |
| 接收端架构、代码和决策 | [架构](../carplay/docs/architecture.md)、[代码布局](../carplay/docs/code-layout.md)、[决策](../carplay/docs/decisions.md) |
| 输入消息与按键 | [输入契约](../carplay/docs/input.md) |
| MRN 专属诊断入口 | [AV、连接、输入工具](../carplay/tools/mrn/README.md) |

## 维护规则

每个主题只维护一份当前结论。README 提供入口；使用说明写操作；架构文档写职责和约束；兼容性表写验证边界。日期用于标识验证批次，不把逐日调试过程追加到产品 README。

研究过程、失败尝试、原厂反汇编与完整日志归研究工作区。进入本项目的结论必须说明适用设备/固件、实际行为及尚未验证的部分。公开文档不依赖研究工作区的路径；可公开且必要的证据应另行脱敏、审核后随版本提供。

源码整理、环境检查等工作的执行清单和批次报告也归研究工作区，不进入产品文档索引。对使用者长期有效的依赖、操作步骤和验证边界分别维护在构建、测试和兼容性文档中；面向使用者的版本变化记入 CHANGELOG。
