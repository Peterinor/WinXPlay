# 电脑端 WinCE 模拟器检查

这些程序用于 WinCE 模拟器客体，不生成车机 USB 操作包。

| 目录 | 用途 | 生成程序 |
| --- | --- | --- |
| emulator_smoke | 检查基础 CE API | CE_ARM_SMOKE.exe |
| hmi_loader_probe | 比较原厂与补丁 HMI 的加载结果 | HMI_LOADER_PROBE.exe |

在项目根目录执行 `./emulator/build.ps1`，共编译两个 EXE，分别位于 `build/emulator/<目录>/`。可加 `-Scenario emulator_smoke` 或 `-Scenario hmi_loader_probe` 只编译其中一项。编译不会自动启动模拟器。
