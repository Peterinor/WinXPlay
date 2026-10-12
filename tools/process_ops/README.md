# 配置驱动的进程终止工具 process_ops

源码目录为 `tools/process_ops`，入口为 `process_ops.exe`。工具在后台运行，不依赖 HMI 窗口、不显示测试画面，不操作音频。音源申请、PCM 输出由独立音频程序负责。

## 配置

EXE 同目录 `processops.txt`：

```text
PROCESSOPS_V1
# KILL|seconds from tool entry|exe basename|wait seconds|retry seconds
KILL|10|entHMI2.exe|20|1
KILL|15|entMedia.exe|20|1
```

以上示例在本工具启动后第 10 秒开始尝试结束 HMI，第 15 秒开始尝试结束媒体服务。保留 entAV.exe、音频驱动及通信服务。

- 时间以工具入口为零点，不是车辆通电时间；必须按时间递增或相同排列。
- 各行独立调度：后项不等待前项成功或退出确认。使用非阻塞退出检查；系统调用自身耗时仍可能造成短暂调度延迟，不保证硬实时。
- 第四列为每行实际开始后的查找及失败重试窗口；第五列为重试间隔。终止请求返回失败（包括 ERROR_BUSY）时，在窗口内核对身份并重试。返回成功后不重复发送请求。
- 无论终止请求返回成功还是失败，都保留同一进程句柄检查退出；重试窗口结束后可继续观察，直到最后一次请求返回后的 60 秒。确认退出后不继续监视。
- 只接受精确 EXE 文件名，大小写不敏感，不接受路径或通配符。同名多进程时拒绝选择，不会全部结束。
- 单项失败仅结束该项，不跳过其他行。同名多进程、自身或身份变化会结束该项；查找、打开失败可在窗口内重试。观察到期仍未确认退出会记录失败。配置错误、工具基础 API 不可用或日志写入失败仍会停止整个工具。
- 可在后面再次写同一 EXE 名，以处理预计在另一时间出现的进程；不会自动追杀重启的进程。
- 完整配置先校验再执行。缺失、超限或末尾有错误都不会先终止前面配置的进程。v1.3 支持最多 **1024 条操作、512 KiB（524288 字节）**，替代旧版 32 条/8 KiB 限制。空行、注释和首行标识不占任务数，但计入文件大小。单行最多 255 字节，进程名最多 63 字符。
- 时间参数仍为启动后 0–3600 秒、查找及重试窗口 0–120 秒、重试间隔 1–30 秒；等待 0 表示只查找一次。1024 项使用独立的任务状态，后项不会因前项尚未退出而等待。
- 配置使用 ASCII 或 UTF-8 BOM，注释也用 ASCII；支持空行和以 # 开头的注释。编辑配置无需重新计算 EXE 启动摘要。

每次启动工具都会执行配置，旧 `MRN_KILL_ONCE.bin` 不再生效。互斥锁防止同时运行两份工具；不阻止前一次退出后再次运行。不要保留旧版终止工具的启动条目。

## 日志及边界

同目录 `process_ops_0001.txt` 等 UTF-16 日志递增编号，不覆盖旧文件。记录计划、实际时间、目标名称、PID、每次终止结果及退出确认。`TARGET_EXIT_CONFIRMED` 表示该行退出已确认；`PROCESSOPS_FINISHED` 汇总已完成任务和失败数，只有全部成功才出现 `PROCESSOPS_COMPLETE`。全部成功返回 0；有单项失败则等待其他任务完成后返回 23。这不证明后续音源切换或声音输出成功。

本版启动标识为 `START WINCE_PROCESSOPS_V1_3_PORTABLE`。`CONFIG_LIMIT_JOBS_BYTES jobs=1024 bytes=524288` 记录限制；文件超出大小上限记录 `CONFIG_TOO_LARGE_NO_KILL`，随后拒绝执行整份配置。

获取进程句柄后再次核对名称和 PID，再发终止请求；拒绝终止自身。枚举快照不是系统级进程锁，代码不声称能排除全部系统并发变化。不修改文件、注册表或看门狗，不自动重启目标程序。结束原厂界面后需正常重启车机恢复。

## 构建与 U 盘包

```powershell
./tools/process_ops/build.ps1
```

编译目录 `build/tools/process_ops/`，软件包固定在 `build/packages/process_ops/`。包内直接放 EXE、processops.txt、启动配置和说明，不生成 ZIP、源码副本或日期版本子目录。

默认配置只有注释，需填写任务后使用。新配置首行用 `PROCESSOPS_V1`，仍兼容旧 `MRN_PROCESSOPS_V1`。B 启动方案用 `tools/package.py --scenario process_ops --scheme B`；一次只使用一种启动方案。

离线检查执行编译后的 ARM 配置解析和调度代码，系统 API 使用替身；不是完整 WinCE 仿真，也不代表新工具已经实车验证。


默认构建打包同时生成AutoLaunch、AUTOLAUNCH_PROD 两个子目录：`build/packages/process_ops/AutoLaunch/` 是方案 A，复制到 U 盘 `AutoLaunch`；同目录下的 `AUTOLAUNCH_PROD/` 是方案 B，复制到 `AUTOLAUNCH_PROD`。每次只选一套，修改配置无需重新生成启动校验。

使用时将所选的 `AutoLaunch` 或 `AUTOLAUNCH_PROD` 文件夹整体复制到 U 盘根目录，每次只选一套；不需要自行创建或重命名文件夹。
