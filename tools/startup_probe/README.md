# 确认 U 盘上的程序能否启动

只记录程序入口、开机时间和完成状态，然后退出。不弹窗、不读取配置，不查询系统、进程或存储容量。适合先验证最基本的 USB 程序执行链路。

在项目根目录执行：

```powershell
./tools/startup_probe/build.ps1
```

生成 **1 个 USB 包，内含 1 个 EXE：startup_probe.exe**。脚本会编译、执行离线检查并生成启动摘要，结束时显示包的位置。

软件包固定在 `build/packages/<程序名>/`，目录内直接放 EXE、配置和说明，不生成 ZIP 或源码副本。重复打包更新同一目录和默认配置。默认将其中的文件复制到 U 盘 `AutoLaunch` 中；需要 B 方案时运行 `tools/package.py --scenario startup_probe --scheme B`，将生成文件复制到 U 盘 `AUTOLAUNCH_PROD` 中，两套方案只选一种。配置和日志跟随 EXE。

执行后查看 U 盘 `AutoLaunch/MRN_USB_STARTUP_CHECK_0001.txt`；如果已有该日志，会使用新的递增编号。出现 `USB_ENTRY_REACHED_NO_WINDOW_NO_CONFIGURATION` 说明程序已经到达入口，`DIAGNOSTIC_COMPLETE` 说明记录流程完成。没有弹窗是正常行为；没有新日志不能单凭这一点区分“没有启动”和“日志写入失败”。

实际 U 盘启动需在目标设备上验证。只编译可加 `-CompileOnly`，指定 Python 可加 `-Python <路径>`。

下文如用 AutoLaunch 举例，选择方案 B 时对应 AUTOLAUNCH_PROD；新包的两种入口均需实车核验。


默认构建打包同时生成AutoLaunch、AUTOLAUNCH_PROD 两个子目录：`build/packages/startup_probe/AutoLaunch/` 是方案 A，复制到 U 盘 `AutoLaunch`；同目录下的 `AUTOLAUNCH_PROD/` 是方案 B，复制到 `AUTOLAUNCH_PROD`。每次只选一套，修改配置无需重新生成启动校验。

使用时将所选的 `AutoLaunch` 或 `AUTOLAUNCH_PROD` 文件夹整体复制到 U 盘根目录，每次只选一套；不需要自行创建或重命名文件夹。
