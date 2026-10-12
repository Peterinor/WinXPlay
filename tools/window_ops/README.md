# window_ops：按名称临时隐藏并恢复窗口

指定顶层窗口标题，延迟执行隐藏，到期由独立助手恢复。代码不包含 MRN 窗口名或进程名，运行目录不限于 U 盘。

```ini
[window_ops]
window_title=DPCA_HMI_WINEA7:0
window_class=
process_name=entHMI2.exe
delay_ms=10000
duration_ms=5000
show_test_window=0
```

上面是这台 MRN 的使用示例；其他 WinCE 设备填写自己的窗口标题即可。配置文件为 EXE 同目录的 `window_ops.ini`，支持 UTF-8（可带 BOM）及 UTF-16LE BOM。默认包的标题留空，需填写后运行；空标题、缺少文件、重复或未知字段都会拒绝操作。

- window_title：必填，精确匹配窗口标题，区分大小写，不是窗口类名。最长 127 个 UTF-16 代码单元。
- window_class：可选，用于进一步限定窗口类；最长 127 个 UTF-16 代码单元。
- process_name：可选，所属进程文件名，不带路径或通配符，大小写不敏感；最长 63 个 UTF-16 代码单元。指定后必须能核验进程身份，否则退出。
- delay_ms：从工具入口算起，0–60000，默认 10000。
- duration_ms：恢复线程等待时间，1000–60000，默认 5000；不保证屏幕实际隐藏时长精确到毫秒。
- show_test_window：0 不显示测试画面；1 在目标隐藏期间显示蓝底黄字测试画面，默认 0。

只操作精确匹配的唯一顶层窗口；没有匹配、多个匹配、目标不可见或不是当前前台时不隐藏。不会结束目标进程，也不支持永久隐藏。每次启动执行一次配置，不再使用 MRN_HIDE_ONCE.bin 标记。

window_ops.exe 自动启动同目录的 window_restore.exe。助手先准备恢复线程，再发出隐藏请求；双方通过具名事件协调，恢复前核对窗口句柄、PID、TID。窗口在隐藏期间更改标题不妨碍按原身份恢复。主工具仍有超时恢复分支；助手失败或恢复未确认会记录失败。系统调用阻塞、两个进程都被结束及设备断电等情况不保证恢复时限。

日志在 EXE 同目录：window_ops_编号.txt、window_restore_编号.txt，递增且不覆盖。TARGET_HWND_PID_TID 为选中目标；SHOW_HIDE_PREVIOUS_VISIBLE_AND_HIDDEN、RESTORE_VISIBLE_FOREGROUND、HELPER_WAIT_QUERY_EXIT 记录执行结果；日志不代替实际画面观察。

```powershell
./tools/window_ops/build.ps1
```

源码在 tools/window_ops，编译结果在 build/tools/window_ops，软件包固定在 build/packages/window_ops。把所选方案目录内全部文件复制到 U 盘 AutoLaunch 中，保留助手和配置，只启动 window_ops.exe。不生成 ZIP、源码副本或日期版本子目录。需要 AUTOLAUNCH_PROD 时运行 tools/package.py --scenario window_ops --scheme B。

配置解析和按名称枚举选择有 ARM 代码与 API 替身测试覆盖；当前实现尚未完成实车验证。WinXPlay 正式 A/B 切换使用自己的桥接逻辑，不依赖这个工具。


默认构建打包同时生成AutoLaunch、AUTOLAUNCH_PROD 两个子目录：`build/packages/window_ops/AutoLaunch/` 是方案 A，复制到 U 盘 `AutoLaunch`；同目录下的 `AUTOLAUNCH_PROD/` 是方案 B，复制到 `AUTOLAUNCH_PROD`。每次只选一套，修改配置无需重新生成启动校验。

使用时将所选的 `AutoLaunch` 或 `AUTOLAUNCH_PROD` 文件夹整体复制到 U 盘根目录，每次只选一套；不需要自行创建或重命名文件夹。
