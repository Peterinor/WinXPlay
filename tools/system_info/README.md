# 采集系统信息与存储容量

确认程序是否从 U 盘启动，并采集系统、进程及实际枚举到的存储容量。无窗口，采集完退出。查看 EXE 同目录的 system_info_*.txt，包含入口时间、系统信息、容量与最终结果。

## 构建并打包

在项目根目录执行：

```powershell
./tools/system_info/build.ps1
```

一次生成 **1 个操作包，内含 1 个 EXE**。U 盘启动入口只有 `system_info.exe`。包内 `Autolaunch.txt` 已配置好，不需要选择其他 EXE。

- `system_info.exe`：一次采集启动记录、系统、进程和存储容量信息。

软件包固定在 `build/packages/<程序名>/`，目录内直接放 EXE、配置和说明，不生成 ZIP 或源码副本。重复打包更新同一目录和默认配置。默认将其中的文件复制到 U 盘 `AutoLaunch` 中；需要 B 方案时运行 `tools/package.py --scenario system_info --scheme B`，将生成文件复制到 U 盘 `AUTOLAUNCH_PROD` 中，两套方案只选一种。配置和日志跟随 EXE。

只编译不打包可加 `-CompileOnly`；指定项目 Python 可加 `-Python <python.exe 路径>`；指定新的软件包目录可加 `-Output <目录>`。打包会执行离线检查，失败则不交付软件包。

## 源码

`main.cpp` 是本场景入口，`scenario.json` 只描述本场景主程序及配套程序。`build.ps1` 调用上一级共享脚本。EXE 文件名保留，以兼容原有安装路径与日志协议。

离线检查覆盖 API 调用和日志处理；实际存储枚举、容量及 U 盘启动需在目标设备上核验。

下文如用 AutoLaunch 举例，选择方案 B 时对应 AUTOLAUNCH_PROD；新包的两种入口均需实车核验。


默认构建打包同时生成AutoLaunch、AUTOLAUNCH_PROD 两个子目录：`build/packages/system_info/AutoLaunch/` 是方案 A，复制到 U 盘 `AutoLaunch`；同目录下的 `AUTOLAUNCH_PROD/` 是方案 B，复制到 `AUTOLAUNCH_PROD`。每次只选一套，修改配置无需重新生成启动校验。

使用时将所选的 `AutoLaunch` 或 `AUTOLAUNCH_PROD` 文件夹整体复制到 U 盘根目录，每次只选一套；不需要自行创建或重命名文件夹。
