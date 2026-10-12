# 观察前台窗口与本窗口输入

排查前台窗口变化。默认不弹窗；需要测试本窗口输入时，将同名 INI 的 show_test_window 改为 1。只记录发给自身窗口的输入，原厂 HMI 仍可能覆盖画面。查看 AutoLaunch/MRN_WINDOW_OBSERVER_*.txt。

## 构建并打包

在项目根目录执行：

```powershell
./tools/window_probe/build.ps1
```

一次生成 **1 个操作包，内含 1 个 EXE**。U 盘启动入口只有 `window_probe.exe`。包内 `Autolaunch.txt` 已配置好，不需要选择其他 EXE。

- `window_probe.exe`：观察前台窗口；可选显示测试窗口记录输入。

软件包固定在 `build/packages/<程序名>/`，目录内直接放 EXE、配置和说明，不生成 ZIP 或源码副本。重复打包更新同一目录和默认配置。默认将其中的文件复制到 U 盘 `AutoLaunch` 中；需要 B 方案时运行 `tools/package.py --scenario window_probe --scheme B`，将生成文件复制到 U 盘 `AUTOLAUNCH_PROD` 中，两套方案只选一种。配置和日志跟随 EXE。

只编译不打包可加 `-CompileOnly`；指定项目 Python 可加 `-Python <python.exe 路径>`；指定新的软件包目录可加 `-Output <目录>`。打包会执行离线检查，失败则不交付软件包。

## 源码

`main.cpp` 是本场景入口，`scenario.json` 只描述本场景主程序及配套程序。`build.ps1` 调用上一级共享脚本。EXE 文件名保留，以兼容原有安装路径与日志协议。

离线检查不能证明物理显示、真实输入和恢复效果，部署包需在目标设备上核验。

下文如用 AutoLaunch 举例，选择方案 B 时对应 AUTOLAUNCH_PROD；新包的两种入口均需实车核验。


默认构建打包同时生成AutoLaunch、AUTOLAUNCH_PROD 两个子目录：`build/packages/window_probe/AutoLaunch/` 是方案 A，复制到 U 盘 `AutoLaunch`；同目录下的 `AUTOLAUNCH_PROD/` 是方案 B，复制到 `AUTOLAUNCH_PROD`。每次只选一套，修改配置无需重新生成启动校验。

使用时将所选的 `AutoLaunch` 或 `AUTOLAUNCH_PROD` 文件夹整体复制到 U 盘根目录，每次只选一套；不需要自行创建或重命名文件夹。
