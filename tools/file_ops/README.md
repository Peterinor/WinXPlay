# U 盘文件管理工具 v2.9

一个独立程序 `file_ops.exe`，读取同目录的 `fileops.txt` 执行文件操作。无需安装 B 显示程序，默认显示实时操作日志窗口。新包默认只把 `\Windows\cmd.exe` 导出到 U 盘。

## 构建与使用

在项目根目录运行：

```powershell
./tools/file_ops/build.ps1
```

脚本编译、执行离线检查并更新固定目录 `build/packages/file_ops/`。打开就是 EXE、配置、使用说明；没有日期、版本、平台、USB包等中间目录，不生成 ZIP 或源码副本。重复构建更新默认配置。只编译可加 `-CompileOnly`；`-Output` 可指定固定输出目录，`-Python` 指定 Python。

默认方案 A：把目录内文件复制到 U 盘 `AutoLaunch` 中。需要方案 B 时运行 `package.py --scheme B`，将生成文件复制到 U 盘 `AUTOLAUNCH_PROD` 中；两种方案只选一种。配置和日志跟随 EXE。以下以方案 A 举例，方案 B 的运行路径会在打包时转换。编辑 `fileops.txt`；同目录的 `*.example.txt` 是可选示例，不会自动运行。修改配置无需重算 EXE 启动摘要。需要从 U 盘提供文件时，自己创建 `AutoLaunch/payload` 并放入待复制文件。

## 操作指令

配置首行使用 `FILEOPS_V2`，每行一个操作：

| 操作 | 写法 | 行为 |
| --- | --- | --- |
| COPY | `COPY|源绝对路径|目标绝对路径或导出文件名` | 按目标复制并逐字节核对；仅文件名则导出到本轮 FILEOPS 目录；目标存在时直接覆盖，不备份（工具 2.4 起） |
| INSTALL | `INSTALL|源绝对路径|目标绝对路径` | 安装或替换文件，保留旧版备份 |
| WATCH | `WATCH|源绝对路径|导出文件名` | 本行采集约三分钟，每五秒保存一个新快照；结束后才执行下一行 |
| DELETE | `DELETE|文件绝对路径` | 先复制到本轮目录并逐字节核对，再删除源文件 |
| MOVE | `MOVE|源绝对路径|目标绝对路径` | 复制、逐字节核对后删除源文件，支持跨卷移动 |
| RUN | `RUN|EXE绝对路径` | 创建独立进程，不等待程序退出，不复制文件（工具 2.3 起支持） |

复制和启动是独立操作。只启动已经部署好的程序时，配置可以只有：

```text
MRN_FILEOPS_V2
RUN|\NAND_USER\WinXPlay\WinXPlay.exe
```

RUN 当前不接受命令行参数，支持路径中的空格。执行前释放本工具持有的 B/恢复程序保护锁，以免拦住目标程序启动。系统创建进程后记录 PID、关闭本工具持有的进程和线程句柄，目标程序独立运行。创建失败则记录系统错误并继续下一项；创建成功不代表目标程序已初始化成功或画面已显示。重复启动行为由目标程序自己处理。

例如删除自建目录中的旧日志：

```text
MRN_FILEOPS_V2
DELETE|\NAND_USER\MRN_TEST\old.log
```

例如将 U 盘文件移入自建应用目录：

```text
MRN_FILEOPS_V2
MOVE|\Hard Disk\AutoLaunch\payload\input.bin|\NAND_USER\MRN_TEST\input.bin
```

旧 `MRN_FILEOPS_V1` 配置继续支持 COPY / INSTALL / WATCH 并保持原有路径范围与操作顺序规则；任意操作顺序、COPY 绝对目标、DELETE / MOVE 及系统文件操作使用 V2 首行。配置为 ASCII 或带 BOM 的 UTF-8，支持空行及以 `#` 开头的注释。

v2.8 将 V1/V2 的任务上限统一提高到 **1024 条**，配置文件上限提高到 **512 KiB（524288 字节）**，替代旧版 12 条/8 KiB 限制。空行、注释、首行格式标识不占任务条数，但计入文件大小。整份配置及目标保护检查完成后才执行第一项；超限或末尾有无效操作时不会先执行前面的任务。单行最多 479 字符，现有路径长度和字符规则保持不变。

## 按行顺序执行

先检查整份配置的语法、路径及本轮输出保护，再逐行执行。v2.9 起，单项操作失败就记录日志、跳过并继续下一项，最后汇总成功和失败数量，不需要增加配置。源文件是否存在在执行到该行时检查，允许前一步创建后一步需要的文件。V2 允许 COPY 后接 DELETE/INSTALL，也允许多次引用同一路径，不会按操作类型重排。配置检查不通过时不执行任务；日志无法继续写入时终止执行。

例如先备份 a，再用 c 替换 a：

```text
MRN_FILEOPS_V2
MOVE|\Windows\a.dll|\Hard Disk\AutoLaunch\payload\a.dll.bak
COPY|\Hard Disk\AutoLaunch\payload\c.dll|\Windows\a.dll
```

MOVE 失败也会继续尝试 COPY，各行没有隐含依赖。MOVE 成功而 COPY 失败时备份仍保留，a 可能不存在或留下复制未完成的文件；不会自动撤销前面成功的操作。COPY 直接覆盖已有目标，不备份；MOVE 不覆盖目标。需要替换并保留备份时使用 INSTALL。

WATCH 同样遵守行顺序，每行等待约三分钟，不与后续行同时采集。单次采样失败会在本行重试；整行没有一个有效快照则记录失败并继续下一项。两行 WATCH 因此总计约六分钟。退出保护检测持续四分钟没有进展的步骤，不再限制整份任务必须在四分钟内完成。

## 操作范围与恢复

V2 接受文件系统中的绝对文件路径，包括 `\Windows`、`\NAND_RES`、`\NAND_USER`、U 盘和其他已挂载卷。INSTALL 的源与目标也不限定在自建目录，替换系统文件使用 INSTALL。目标的首层目录/卷必须已经存在，允许创建其下的子目录。

只支持单个文件，不递归操作目录，不接受通配符、相对路径、`..`、设备句柄或原始分区地址。工具正在使用的 `file_ops.exe`、`fileops.txt`、`Autolaunch.txt` 及本轮 FILEOPS 日志/备份目录不允许改动；其他旧 FILEOPS 目录中的单个文件可以操作。

MOVE 目标已存在就失败，不覆盖它。程序名匹配正在运行的进程，或无法取得进程清单时，拒绝删除和移动；B/恢复程序在运行时也拒绝写操作。对其他程序的检查不是持续文件锁，操作期间应停止修改这些文件的程序。

DELETE 的备份为本轮 `FILEOPS_编号/deleted-任务编号.bak`，日志记录原路径和备份路径。确认结果前保留该目录；下一轮可从该备份路径按原目标使用 INSTALL 恢复。INSTALL 替换原文件时，旧文件留在目标旁的 `.bak-开机时间-任务编号` 路径，日志记录它的位置。

MOVE 是“复制—核对—删除”，不是断电原子操作。复制或核对失败不会删除源文件；删除失败时可能两份文件都在，记录失败并结束本项，继续下一项，不自动删除目标或重试。删除后的存在性核验失败也不会声称成功。

## 系统文件替换与热更新

例如将已准备好的文件替换到系统目录（示例文件名必须改成你的实际目标）：

```text
MRN_FILEOPS_V2
INSTALL|\Hard Disk\AutoLaunch\payload\replacement.dll|\Windows\replacement.dll
```

此工具操作的是车机文件系统，不调用刷写接口，不修改原始 NAND/NOR 分区。是否可写、是否持久保留、重启后是否被原厂恢复，取决于目标卷及文件属性，须在车机上验证。

ROM 内置、只读或使用中的文件可能拒绝复制、改名或删除，工具不会自动清除属性或绕过系统限制。日志记录文件属性、失败阶段以及相应文件 API 错误。运行中 EXE 会被进程检查拦住；DLL 没有完整的跨进程卸载协调，程序不会主动卸载它。落盘内容核对成功不代表运行中的进程已加载新版；程序/服务重载或重启属于后续步骤，不自动执行。

## 进度窗口

窗口由独立线程刷新，文件复制、校验或 WATCH 等待不会阻塞窗口的消息循环。顶部显示当前任务序号、操作名称和源/目标路径；中间显示最近 48 条日志并自动滚动，完整日志仍写入 RESULT.txt。大文件复制目前显示执行阶段，不显示字节百分比。

任务完成或失败后显示结果及退出码，倒计时 5 秒后自动关闭窗口并退出（界面计时器每 250 ms 检查），也可点击 Close 提前关闭。执行中 Close 不会中断文件替换；无须触摸即可自动结束。创建界面失败时仍保留原有文件操作与日志。原有 V1/V2 配置不需要修改。

倒计时到期后先隐藏、销毁窗口，再结束消息循环。主线程等待最多 7 秒（5 秒倒计时加 2 秒清理余量），随后记录结果并退出；界面卡住时会记录未确认，保留进程退出兜底。

此版本使用标准 WinCE 顶层窗口，不隐藏或结束原厂 HMI。MRN 原厂图层曾遮挡普通窗口，实际能否看到进度窗口仍需实车确认；请求置顶不等于已取得硬件显示层。

## 查看结果

日志在 `AutoLaunch/FILEOPS_编号/RESULT.txt`。新包标识为 `START WINCE_FILEOPS_V2_9_PORTABLE`。

- `CONFIG_LIMIT_JOBS_BYTES a=1024 b=524288`：本版任务数和配置字节数上限。
- `CONFIG_TOO_LARGE_NO_INTERNAL_WRITES`：配置文件超过大小上限，未执行配置中的任何任务。

- `UI_THREAD_EXIT_CONFIRMED`、`UI_WM_DESTROY_CONFIRMED`：界面线程已结束、窗口收到销毁消息；不代表物理屏幕一定已刷新。
- `UI_THREAD_EXIT_NOT_CONFIRMED_FALLBACK`、`UI_WM_DESTROY_NOT_CONFIRMED`：未确认正常清理，不能当成窗口已经正常关闭。
- `PROCESS_EXIT_REQUESTED`：即将调用进程退出；进程真正消失仍需外部观测。`END_FILEOPS_REQUESTED` 仅表示文件任务结束，后面还有结果窗口倒计时。

- `DELETE_PASS_BACKUP_RETAINED`：删除完成、备份保留。
- `MOVE_PASS_BYTES_VERIFIED_SOURCE_ABSENT`：移动目标已核对，源文件不存在。
- `RUN_PROCESS_CREATED_PID`：系统创建进程成功，附进程 ID；`RUN_CREATE_PROCESS_ERROR`：创建失败，附系统错误码。
- `JOB_BEGIN_INDEX_OPERATION`、`JOB_COMPLETE_INDEX_OPERATION`：本行开始、完成（任务序号从 1 开始，不含注释和空行）。
- `JOB_FAILED_CONTINUE_INDEX_OPERATION`：本行失败，继续下一项。
- `FILEOPS_SUCCESS_FAILED a=成功数 b=失败数`：任务结果汇总；`ALL_JOBS_COMPLETE` 表示全部成功，`FILEOPS_FINISHED_WITH_FAILURES` 表示执行完毕但有失败项，退出码为 23。
- `INSTALL_PHASE_FINISHED_WITH_FAILURES`：连续 INSTALL 中存在失败项，不记录安装全部成功标记。`FILEOPS_ABORTED_LOG_ERROR` 表示日志写入失败导致中止，日志设备故障时该标记也可能无法落盘。
- `TRANSFER_REMOVE_STATUS`：0 成功；1 源不存在或不是文件；2 目标已存在或无法确认；3 复制失败；4 核对失败；5 删除源失败；6 无法确认源已消失。

新功能通过编译和离线 ARM API 替身测试，尚未实车验证。只有明确配置 RUN 才启动程序，复制指令不会自动启动程序。

## 运行目录与配置兼容

当前程序名为 file_ops.exe，支持从任意可写目录运行，fileops.txt 和 FILEOPS_编号 均位于 EXE 同目录。自身 EXE、当前配置、本轮结果目录按实际路径保护。MRN 的 U 盘摘要文件只属于设备启动适配，不是程序自身的启动依赖。

通用配置使用 FILEOPS_V2；WINCE_FILEOPS_V2、MRN_FILEOPS_V1/V2 继续兼容。只有旧 MRN 配置保留原来的 B/恢复助手互斥约定，通用配置不检查这些 MRN 互斥量。WinXPlay 的部署配置继续使用旧格式，保留已有部署协调。

验证跨重启持久性时，先执行 COPY/INSTALL 和 RUN，并检查目标程序日志；真实冷启动后仅导出或读取目标文件进行比对，不先重新复制。RUN 只证明创建进程，程序是否实际执行还需检查目标日志。


默认构建打包同时生成AutoLaunch、AUTOLAUNCH_PROD 两个子目录：`build/packages/file_ops/AutoLaunch/` 是方案 A，复制到 U 盘 `AutoLaunch`；同目录下的 `AUTOLAUNCH_PROD/` 是方案 B，复制到 `AUTOLAUNCH_PROD`。每次只选一套，修改配置无需重新生成启动校验。

使用时将所选的 `AutoLaunch` 或 `AUTOLAUNCH_PROD` 文件夹整体复制到 U 盘根目录，每次只选一套；不需要自行创建或重命名文件夹。
