"""Package the matching standard-window receiver and HMI DLL; no device writes."""
from pathlib import Path
import hashlib
import json
import sys
import runpy

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from runtime_package import publish, production_data


def main():
    runpy.run_path(str(Path(__file__).parent / 'hmi_patch/package-artifacts.py'), run_name='__main__')
    bridge = ROOT / 'build/firmware/ab_bridge'
    receiver = ROOT / 'build/carplay/ce-wm5-arm-mrn/WinXPlay.exe'
    tool = ROOT / 'build/tools/file_ops/file_ops.exe'
    verification = json.loads((bridge / 'verification.json').read_text(encoding='utf-8'))
    layers = json.loads((bridge / 'layer-verification.json').read_text(encoding='utf-8'))
    dll = (bridge / 'WinXPlayBridge.dll').read_bytes()
    sha = lambda data: hashlib.sha256(data).hexdigest()
    assert verification['passed'] and verification['binaries']['WinXPlayBridge.dll'] == sha(dll)
    assert layers['passed'] and layers['bridge_sha256'] == sha(dll)
    binary = receiver.read_bytes()
    assert b'MrnClient' not in binary and 'WinXPlayBridgeV2'.encode('utf-16le') not in binary
    recipe = (r'''FILEOPS_V2
COPY|\Hard Disk\AutoLaunch\payload\WinXPlay.exe|\NAND_USER\WinXPlay\WinXPlay.exe
COPY|\Hard Disk\AutoLaunch\payload\WinXPlayBridge.dll|\NAND_USER\WinXPlay\WinXPlayBridge.dll
''').encode('ascii')
    sys.path.insert(0, str(ROOT / 'tests'))
    from test_file_ops_policy import Tests
    assert Tests().parse(recipe) == (1, 2)
    assert Tests().parse(production_data('fileops.txt', recipe)) == (1, 2)
    instructions = '''# WinXPlay 标准窗口桥接配套更新

本包一起更新 WinXPlay.exe 和 WinXPlayBridge.dll；旧 DLL 与新主程序不兼容，不能只换一个。
需要车机已有加载 MrnBridgeStart/MrnOnMessage 的 HMI 钩子。本次不修改 HMI EXE、USB 驱动、认证、配对或配置。

先退出 WinXPlay 和原厂 HMI，确认 DLL 未被占用，再使用 U 盘启动文件工具执行两条 COPY。
按当前文件工具 COPY 规则覆盖目标。程序不会自动关闭进程或重启；两项均成功且内容核对通过后重启车机。
使用 AutoLaunch 或 AUTOLAUNCH_PROD 其中一种目录，不能同时放两套。

本包启用调试日志，桥接日志为 NAND_USER/WinXPlay/A_BRIDGE.log；不再产生 B_INTERACTION.log。
测试步骤：BAND 进入 → 点击/拖动/按键 → BAND 返回 → 再次进入 → CarPlay 返回原车 → 再次进入 → 正常退出程序。
窗口隐藏时手机会话保持。日志或模拟器通过不能替代实车画面、触摸和原厂恢复验证。本包未部署到车机或实际 U 盘。
'''
    files = {'file_ops.exe': tool.read_bytes(), 'fileops.txt': recipe,
             'payload/WinXPlay.exe': binary, 'payload/WinXPlayBridge.dll': dll,
             '使用说明.md': instructions.encode('utf-8')}
    output = ROOT / 'build/packages/winxplay_bridge_update'
    publish(output, files, 'file_ops.exe')
    record = {'date': '2026-10-12', 'logs_enabled': True, 'vehicle_deployed': False,
              'files': {p.relative_to(output).as_posix(): sha(p.read_bytes())
                        for folder in ('AutoLaunch', 'AUTOLAUNCH_PROD')
                        for p in (output / folder).rglob('*') if p.is_file()}}
    (output / 'manifest.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
    print(output)


if __name__ == '__main__':
    main()
