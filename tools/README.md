# 通用 WinCE 工具

源码目录、EXE 名和软件包目录使用相同名称。平台属于构建目标；不把 MRN 或 WinCE 写入通用工具名称。未来 Linux 实现沿用工具名，按平台拆分实现与构建目标；目前仅支持 WinCE ARM。

| 目录 | 功能 | 程序 |
| --- | --- | --- |
| [file_ops](file_ops/README.md) | 文件复制、安装、移动、删除、观察和启动 | file_ops.exe |
| [process_ops](process_ops/README.md) | 按配置时间结束指定进程 | process_ops.exe |
| [system_ops](system_ops/README.md) | 通过标准 WinCE 电源接口请求一次系统重启 | system_ops.exe |
| [system_info](system_info/README.md) | 系统、进程与存储信息 | system_info.exe |
| [window_probe](window_probe/README.md) | 前台窗口观察与自身窗口输入 | window_probe.exe |
| [window_ops](window_ops/README.md) | 按名称临时隐藏窗口并恢复，含助手 | window_ops.exe |
| [startup_probe](startup_probe/README.md) | 记录启动入口后退出 | startup_probe.exe |

在项目根目录执行：

```powershell
./tools/build.ps1 -List
./tools/process_ops/build.ps1
./tools/build.ps1 -Scenario all
```

单工具入口或批量入口默认编译、离线验证并打包；加 `-CompileOnly` 只编译。使用 `-Python <python.exe>` 指定已有 Python 环境。

编译文件在 `build/tools/<程序名>/`，可用包同时生成两套：`build/packages/<程序名>/AutoLaunch/` 和 `build/packages/<程序名>/AUTOLAUNCH_PROD/`。包直接放 EXE、配置和说明，不生成 ZIP、源码副本或日期版本子目录。A 包复制到 U 盘 AutoLaunch，B 包复制到 AUTOLAUNCH_PROD，每次只选一套。此启动适配属于 MRN，工具本身从 EXE 所在目录读取配置和写日志。

默认同时生成 A/B；如只需方案 B，五个场景工具使用 `tools/package.py --scenario <程序名> --scheme B`；file_ops、system_ops 使用各自目录中的 `package.py --scheme B`。每次只使用一种方案。

通用日志与参数支持在 `common`。场景共享编译器是 `scripts/build-scenarios.ps1`；file_ops、system_ops 保留自己的构建配方。按名称隐藏和恢复窗口使用 window_ops。

使用时将所选的 `AutoLaunch` 或 `AUTOLAUNCH_PROD` 文件夹整体复制到 U 盘根目录，每次只选一套；不需要自行创建或重命名文件夹。
