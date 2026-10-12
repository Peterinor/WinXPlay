# system_ops：WinCE 系统操作

当前只有 `REBOOT`，使用标准 WinCE `SetSystemPowerState` 请求系统重启。v1.1 先通过标准注册表接口枚举 `HKLM\System\CurrentControlSet\Control\Power\State`，查找唯一 `Flags=POWER_STATE_RESET` 的状态，再调用 `SetSystemPowerState(状态名, 0, 0)`；未注册对应状态时使用 `(NULL, POWER_STATE_RESET, 0)`。没有 MRN 私有消息、寄存器操作或终止原厂进程的步骤，也不写死状态名称。程序从自身目录读取配置并写日志，不依赖固定 U 盘路径。

## 使用

包内 `systemops.txt` 已配置为：

```text
SYSTEMOPS_V1
REBOOT
```

从 `build/packages/system_ops/` 选择 `AutoLaunch` 或 `AUTOLAUNCH_PROD`，把所选文件夹复制到 U 盘根目录；一次只选一套。启动器执行 EXE 后立即请求重启，不需要点击窗口。使用前结束正在进行的文件替换或升级。

重启前，工具在自身目录创建并保存 `system_ops.reboot.once`。只要该文件存在，再次运行就记录跳过，不重复请求重启。**下一次需要再次重启时，手动删除这个文件。** 即使请求失败，标记也保留，避免开机反复尝试。不要把删除标记的操作放入开机自动执行的配置。包内不包含标记文件，重新复制程序不会主动删除旧标记。

若要在文件操作结束后重启，把 `system_ops.exe` 和 `systemops.txt` 放在同一个目录，并在 `fileops.txt` 最后一行添加（方案 A）：

```text
RUN|\Hard Disk\AutoLaunch\system_ops.exe
```

这时保留 file_ops 的 U 盘启动文件，只复制上述两个运行文件，不用 system_ops 包里的启动文件覆盖它。方案 B 的路径使用 `\Hard Disk\AUTOLAUNCH_PROD\system_ops.exe`。file_ops 是失败后继续执行，前面复制失败也会运行最后这一行；RUN 不等待重启工具退出。它请求的是整个 WinCE 系统重启，而不是只重启 HMI；不等于整车断电冷启动，也不保证外围控制器一同复位。

## 日志

同目录 `system_ops_0001.txt` 等 UTF-16 日志递增编号。每条记录前的毫秒数为系统运行时间。

| 标记 | 含义 |
| --- | --- |
| `ONCE_MARKER_SAVED` | 一次性标记已写入、刷新并关闭 |
| `RESET_STATE_NAME_FOLLOWS` | 下一行记录从系统注册表发现的重启状态名称 |
| `RESET_REQUEST_USES_REGISTERED_NAME` | 按发现的名称调用标准电源接口 |
| `RESET_REQUEST_USES_NUMERIC_HINT` | 没有对应注册状态，使用标准 RESET 参数 |
| `REQUEST_SetSystemPowerState_POWER_STATE_RESET` | 即将调用标准系统接口；不是重启成功证明 |
| `REBOOT_SKIPPED_ONCE_MARKER_EXISTS` | 本轮跳过，没有再次请求重启 |
| `RESET_API_RETURNED_ERROR` | 接口返回错误，value 是其返回的系统错误码 |
| `RESET_API_RETURNED_SUCCESS_REBOOT_NOT_CONFIRMED` | 接口返回 0，但工具仍在执行，不能确认重启 |

工具先刷新、关闭日志及标记文件，再调用系统接口。若调用完成了系统重启，通常不会留下返回记录；仅凭日志在请求处结束，仍不能排除卡住或掉电。需结合实际开机画面和下一次日志的系统运行时间确认。

退出码：0＝标记存在而跳过；10＝已有实例；11＝程序路径无效；12＝无法创建日志；18＝配置无效或读取失败；20＝标准接口不可用；21＝标记无法保存；22＝日志无法保存或关闭；23＝重启接口返回错误；24＝接口返回 0 但重启未确认；25＝注册表读取失败、状态格式无效或存在多个 RESET 状态而无法确定。实际重启通常不会返回退出码。错误不会触发其他底层重启方式。

只接受一个 `REBOOT`，支持空行、`#` 注释、ASCII/UTF-8 BOM，配置最多 4096 字节。整份配置检查完成后才操作。标记或日志不能可靠写入时，不调用重启接口。

## 实现依据与验证边界

- [微软 SetSystemPowerState 文档](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms920754(v=msdn.10)) 支持按名称或 `POWER_STATE_RESET` 指定状态；本工具按系统注册的名称请求，不使用 `POWER_FORCE`。
- F3.21 提取的 `coredll.dll` 导出 `SetSystemPowerState`（ordinal 1582），`pm.dll` 导出 `PmSetSystemPowerState`。这证明入口存在，不证明该平台允许应用程序请求 RESET。
- MRN F3.21 的电源管理不能通过数值 RESET 参数选择对应状态；注册表中的 `LocalReboot` 使用 `Flags=0x800000`。工具枚举该状态后按名称请求，配置仍为 REBOOT，代码不硬编码 MRN 状态名。
- v1.1 方案 B 在 MRN F3.21 上有按 `LocalReboot` 请求并实际重启的验证记录。该结论限定于该版本和设备，不覆盖其他 WinCE 设备、整车冷断电或外围控制器复位；新构建仍需验收。
- 标准电源接口失败时保留错误码并退出，不改用直接硬件复位或其他底层接口绕过电源管理。

离线测试执行编译后的 ARM 代码，用替身模拟 WinCE API 和文件系统，覆盖重复启动、失败路径、日志/标记落盘及标准接口参数；不是完整 WinCE 模拟器。一次性跳过分支目前仅有离线验证。

## 构建

```powershell
./tools/system_ops/build.ps1
# 或统一构建入口
./tools/build.ps1 -Scenario system_ops
```

支持 `-CompileOnly`、`-Python` 和 `-Output`。构建产物在 `build/tools/system_ops/`；默认验证并生成 `build/packages/system_ops/AutoLaunch/` 与 `build/packages/system_ops/AUTOLAUNCH_PROD/`，不带日期目录、ZIP 或源码。`package.py --scheme A/B` 可只生成其中一套。A/B 启动文件是 MRN 适配层，EXE 本身只调用 WinCE 系统 API。
