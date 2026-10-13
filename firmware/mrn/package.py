"""Package the matching standard-window receiver and HMI DLL; no device writes."""
from pathlib import Path
import argparse
import hashlib
import json
import sys
import runpy

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from runtime_package import publish, production_data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-logs', action='store_true', help='Package the verified no-log receiver and bridge')
    args = parser.parse_args()
    if not args.no_logs:
        runpy.run_path(str(Path(__file__).parent / 'hmi_patch/package-artifacts.py'), run_name='__main__')
    suffix = '-nologs' if args.no_logs else ''
    bridge = ROOT / ('build/firmware/ab_bridge' + suffix)
    receiver = ROOT / ('build/carplay/ce-wm5-arm-mrn' + suffix) / 'WinXPlay.exe'
    tool = ROOT / 'build/tools/file_ops/file_ops.exe'
    verification = json.loads((bridge / 'verification.json').read_text(encoding='utf-8'))
    layers = json.loads((bridge / 'layer-verification.json').read_text(encoding='utf-8'))
    dll = (bridge / 'WinXPlayBridge.dll').read_bytes()
    sha = lambda data: hashlib.sha256(data).hexdigest()
    assert verification['passed'] and verification['binaries']['WinXPlayBridge.dll'] == sha(dll)
    assert layers['passed'] and layers['bridge_sha256'] == sha(dll)
    assert verification['tests_run'] >= 16 and layers['tests_run'] >= 9
    assert verification['logs_enabled'] == (not args.no_logs)
    if args.no_logs:
        assert layers['logs_enabled'] is False
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

本轮变更：MRN 默认关闭 CarPlay 音频输出和麦克风输入，保留画面、触控及按键。默认不声明 PCM/AAC、不运行 AAC 自检、不申请 TTS 或录音音源；音频请使用手机/原车蓝牙。通用 WinCE 和 Windows 默认值保持开启。
本包默认音频输出、麦克风输入都关闭。运行配置可以独立覆盖默认值：在 NAND_USER/WinXPlay/carplay.plist 现有 dict 内加入 CARPLAY_AUDIO_OUTPUT、CARPLAY_MICROPHONE，值使用 string 类型的 0 或 1。两项均为 0 时只保留画面和触控，音频需自行保持原车蓝牙连接；不会自动切蓝牙。不要用只有这两项的新文件覆盖其他现有配置。修改后重新启动 WinXPlay 并重连手机。CARPLAY_AAC 设为字符串 0 可关闭 AAC 对照 PCM。
本轮测试：保持原车蓝牙连接并播放音乐，进入 CarPlay 滚动/切换页面，核对画面、触控及蓝牙声音。回收完整 FILEOPS 日志。若现有 carplay.plist 显式将上述开关设为字符串 1，需改为 0 或移除对应项，设备默认关闭才会生效；部署包保留现有配置。

本包一起更新 WinXPlay.exe 和 WinXPlayBridge.dll，保留麦克风上行和 MODE/SEEK 映射。根据 FILEOPS_0002 实车日志，补上方向盘实际拨轮编号 0x34（SWRC_AUDIO_SCROLL）的切歌映射，同时保留 0x36 分支和完整原始按键日志。
需要车机已有加载 MrnBridgeStart/MrnOnMessage 的 HMI 钩子。本次不修改 HMI EXE、USB 驱动、认证、配对或配置。

本次开机尚未按过 BAND 时，DLL 尚未加载，可以先更新再首次按 BAND，无需额外重启。
如果已经按过 BAND，需要先退出持有 DLL 的原厂 HMI 及 WinXPlay，再更新；否则应安排重新启动以加载新 DLL。
程序不会自动关闭进程或重启；两项均成功且内容核对通过后再测试。
使用 AutoLaunch 或 AUTOLAUNCH_PROD 其中一种目录，不能同时放两套。

本版保留触摸自动恢复，并修正短暂卡顿误取消点击：单次 100 毫秒响应探测超时不会取消手势，连续无响应达到已有 2 秒期限才暂停；投递失败仍立即取消。
逐秒音视频报告改为后台写盘，音频播放线程不再重复写逐秒快照。触摸日志每秒批量写入；新增音频缺数据填静音与补缓冲间隔统计。
本轮继续移走 DirectShow 渲染窗口线程内的周期报告写盘；48 kHz 音频跳过无需执行的插值运算，保持样本、采样率和缓冲策略不变。MEDIA_RX 新增 audio_process_ms/audio_process_max_ms，观察音频处理是否拖住接收。
蓝牙对照后的更新：单路音乐改为整块读取 PCM 队列，多路继续混音；补充音频解密、队列锁等待、填缓冲、waveOutWrite 分段耗时。当前仍是 TTS 输出路线，本更新不代表音乐音质或实车卡顿已验收。
本包启用调试日志，桥接日志为 NAND_USER/WinXPlay/A_BRIDGE.log；不再产生 B_INTERACTION.log。
桥接记录 TOUCH_SOURCE、TOUCH_FORWARD，主程序记录 TOUCH_WINDOW；只记录按下/释放，避免移动事件刷日志。
新增：手机请求录音时，主程序申请录音机源 0x10，等待 2 秒后以设备 0 / 22050 Hz / 单声道 PCM16 采集，转换至手机协商格式上传；会话结束释放。等待阶段不阻塞接收循环。该组合独立录音已实测，接入主程序后的电话/Siri 和回声仍需本轮实测。
CarPlay 前台时，MODE（页面切换图标）发送 CarPlay Home 控制；普通 SEEK 上/下发送下一首/上一首，仅转换 action=0 为一次完整点击。中控切歌已实车确认。方向盘日志中的 72 条拨轮输入全部为 0x34（十进制 52），此前未映射导致不切歌；本版对 0x34/0x36 使用有符号步进：正数下一首，负数上一首，每步一次，零步不发送，单条最多 16 次。物理拨动方向与正负号的对应仍待实车核对；0x32/0x33/0x35/0x37 保持原厂处理。后台仍走原厂操作，BAND 用途不变。不猜测长按和快进/快退，Home 的实际界面行为由 iPhone 决定。
RAW_KEY / RAW_STEP 日志现在也记录未映射事件；以十进制保留编号、动作或有符号步数、目标虚拟键。例 RAW_STEP id delta virtual 54 -1 177 表示 0x36 负一步、上一首；virtual=0 表示未映射。桥接首次按 BAND 启动后才开始记录。
取日志时把 collect-fileops.txt 复制为 fileops.txt，再运行文件工具。保留完整 FILEOPS_编号 目录。
本次方向盘测试：BAND 进入 CarPlay → 播放音乐 → 方向盘下一首操作一次、等待 2 秒、上一首操作一次 → 核对中控切歌 → BAND 返回原厂并重复方向盘操作 → 回收 A_BRIDGE.log。分别记录是否有效以及方向是否相反。即使无效，新增日志也能辨认遗漏的事件，避免重复猜测。
本轮默认不测试 CarPlay 麦克风回传。下述旧音频实现仍保留在代码中，只有显式重新开启对应开关才会使用。
窗口隐藏时手机会话保持。日志或模拟器通过不能替代实车画面、触摸和原厂恢复验证。本包未部署到车机或实际 U 盘。
'''
    if args.no_logs:
        instructions = '''# WinXPlay 无日志更新

WinXPlay.exe 和 WinXPlayBridge.dll 均使用 WINXPLAY_ENABLE_LOGS=0 编译，关闭运行诊断日志及逐秒报告写盘。已有日志文件不会删除；文件更新工具仍保留操作结果，方便核对复制是否成功。
MRN 默认关闭 CarPlay 音频输出和麦克风输入，保留画面、触控及按键。音频请自行连接原车蓝牙，本包不会自动切换蓝牙。
若现有 carplay.plist 中 CARPLAY_AUDIO_OUTPUT 或 CARPLAY_MICROPHONE 显式设为字符串 1，请改为字符串 0 或移除对应项，设备默认关闭才会生效。本包保留现有配置。

本包一起更新 WinXPlay.exe 和 WinXPlayBridge.dll，需要车机已有 HMI 桥接钩子，不修改 HMI EXE、USB 驱动、认证或配对。
本次开机尚未按过 BAND 时，可以先更新再首次按 BAND，无需额外重启。若已加载旧 DLL，需要退出持有 DLL 的原厂 HMI 及 WinXPlay，或重新启动车机，才能加载新版本。
使用 AutoLaunch 或 AUTOLAUNCH_PROD 其中一种目录，不能同时放两套。确认两项文件均复制成功后进入 CarPlay。
collect-fileops.txt 仅供收集设备上已有的旧日志，本版不会生成新的运行诊断日志。
本包已通过编译和桥接模拟测试，仍需实车确认画面与触控。本包未部署到车机或实际 U 盘。
'''
    files = {'file_ops.exe': tool.read_bytes(), 'fileops.txt': recipe,
             'payload/WinXPlay.exe': binary, 'payload/WinXPlayBridge.dll': dll,
             '使用说明.md': instructions.encode('utf-8')}
    names = ['A_BRIDGE.log', 'connection-trace.log', 'connection-trace.previous.log',
             'streaming-diagnostics.plist', 'auto-connect.plist', 'last-error.plist',
             'video-backend.plist', 'audio-output.plist', 'audio-recovery.plist', 'audio-input.plist', 'crash.log']
    files['collect-fileops.txt'] = ('FILEOPS_V2\n' + ''.join(
        'COPY|\\NAND_USER\\WinXPlay\\' + name + '|' + name + '\n' for name in names)).encode('ascii')
    output = ROOT / 'build/packages/winxplay_bridge_update'
    publish(output, files, 'file_ops.exe')
    record = {'date': '2026-10-12', 'logs_enabled': not args.no_logs, 'vehicle_deployed': False,
              'files': {p.relative_to(output).as_posix(): sha(p.read_bytes())
                        for folder in ('AutoLaunch', 'AUTOLAUNCH_PROD')
                        for p in (output / folder).rglob('*') if p.is_file()}}
    (output / 'manifest.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
    print(output)


if __name__ == '__main__':
    main()
