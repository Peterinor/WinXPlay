"""Assemble local deployment packages; never writes to a USB drive or vehicle."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT/'build/packages'
TOOLS = ['file_ops', 'process_ops', 'system_info', 'system_ops', 'window_ops',
         'window_probe', 'startup_probe', 'winxplay_test_av', 'winxplay_test_connect', 'winxplay_test_input']
FIRMWARE = ['firmware', 'firmware_install', 'winxplay_bridge_update']
RECEIVER = ['winxplay_install', 'winxplay_usb']
GROUPS = {'Tools': TOOLS, 'Firmware': FIRMWARE, 'Receiver': RECEIVER,
          'All': TOOLS + FIRMWARE + RECEIVER}


def write_inventory(output, generated, target, no_logs=False):
    """Only this successful run is marked regenerated; older folders are not verified."""
    inventory = {'target': target, 'complete': True, 'vehicle_deployed': False,
                 'receiver_logs_enabled': not no_logs if target in ('Receiver', 'All') else None,
                 'packages': {}}
    lines = ['# 本地部署包', '',
             f'最近成功执行：{target}，本轮生成 {len(generated)} 个包。下面只将本轮产物标记为已生成。', '',
             '所有 U 盘包均有 `AutoLaunch`（方案 A）与 `AUTOLAUNCH_PROD`（方案 B），每次只选一套。',
             '`firmware` 是补丁文件目录，不是刷机包。打包不会修改实际 U 盘或车机。', '',
             '接收端安装包和直接运行包含私有身份；固件包含原厂衍生文件。本目录不是公开 Release。', '',
             '重复打包更新默认配置，请另存自己的配置。file_ops 默认导出 cmd.exe；process_ops 和 window_ops 需先填写目标；system_ops 默认请求重启。', '',
             '| 目录 | 本轮状态 | 内容 |', '| --- | --- | --- |']
    for name in GROUPS['All']:
        folder = output/name
        if name in generated:
            if name == 'firmware':
                required = ['ENTHMI2.WINXPLAY.EXE', 'WinXPlayBridge.dll', 'manifest.json', 'LICENSE', 'THIRD_PARTY.txt']
            else:
                required = [s+'/'+f for s, launch in [('AutoLaunch', 'Autolaunch.txt'),
                            ('AUTOLAUNCH_PROD', 'AutoLaunch_prod.txt')]
                            for f in (launch, 'LICENSE', 'THIRD_PARTY.txt')]
            for relative in required:
                if not (folder/relative).is_file():
                    raise RuntimeError(f'Incomplete package: {name}/{relative}')
            inventory['packages'][name] = {'files': {
                p.relative_to(folder).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(folder.rglob('*')) if p.is_file()}}
            status = '已生成并检查必需文件'
        else:
            status = '已有目录，未在本轮验证' if folder.is_dir() else '未生成'
        purpose = ('私有身份运行包' if name in RECEIVER else
                   'HMI 补丁及桥接文件' if name == 'firmware' else
                   'HMI 与桥接复制包' if name == 'firmware_install' else
                   '主程序与桥接更新包（带日志）' if name == 'winxplay_bridge_update' else
                   '单项测试包' if name.startswith('winxplay_test_') else '通用工具包')
        lines.append(f'| `{name}` | {status} | {purpose} |')
    (output/'README.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    (output/'inventory.json').write_text(json.dumps(inventory, ensure_ascii=False, indent=2), encoding='utf-8')


def preflight(args):
    """Find missing inputs before overwriting any existing package."""
    required = []
    if args.target in ('Tools', 'All'):
        required += [ROOT/f'build/tools/{name}/{name}.exe' for name in TOOLS if not name.startswith('winxplay_test_')]
        required += [ROOT/'build/tools/window_ops/window_restore.exe']
        required += [ROOT/f'build/carplay/vehicle-tests/winxplay_test_{x}.exe' for x in ('av', 'connect', 'input')]
        required += [ROOT/'build/carplay/vehicle-tests/video.avt', ROOT/'build/carplay/ce-wm5-arm-mrn/WinXPlayUsbMux.dll']
    if args.target in ('Firmware', 'All'):
        required += [ROOT/'build/firmware/ab_bridge'/x for x in
                     ('ENTHMI2.WINXPLAY.EXE', 'WinXPlayBridge.dll', 'verification.json', 'layer-verification.json',
                      'patch-manifest.json', 'verification.log', 'layer-tests.log')]
        required += [ROOT/'build/carplay/ce-wm5-arm-mrn/WinXPlay.exe', ROOT/'build/tools/file_ops/file_ops.exe']
    if args.target in ('Receiver', 'All'):
        if args.identity is None:
            raise RuntimeError('Set CARPLAY_IDENTITY or pass -Identity for local receiver packages.')
        required += [args.identity/x for x in ('certificate.p7b', 'identity.pk8')]
        suffix = '-nologs' if args.no_logs else ''
        required += [ROOT/f'build/carplay/ce-wm5-arm-mrn{suffix}'/x for x in ('WinXPlay.exe', 'WinXPlayUsbMux.dll')]
        bridge = ROOT/('build/firmware/ab_bridge-nologs' if args.no_logs else 'build/packages/firmware')
        if args.target != 'All' or args.no_logs:
            required += [bridge/x for x in ('WinXPlayBridge.dll', 'verification.json')]
        required += [ROOT/'build/tools/file_ops/file_ops.exe']
    missing = [str(p) for p in required if not p.is_file()]
    if missing:
        raise RuntimeError('Build and verify the missing inputs first (docs/build.md):\n'+'\n'.join(missing))
    from external_assets import require_oem
    require_oem('SSW_AUTOLAUNCHER.DLL')
    if args.target != 'Receiver':
        require_oem('coredll.dll')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=GROUPS, default='Tools')
    parser.add_argument('--identity', type=Path)
    parser.add_argument('--no-logs', action='store_true')
    args = parser.parse_args()
    if args.no_logs and args.target not in ('Receiver', 'All'):
        parser.error('--no-logs applies to Receiver or All only')
    status = ROOT/'build/validation/package-all.json'
    status.parent.mkdir(parents=True, exist_ok=True)
    record = {'target': args.target, 'complete': False, 'completed_steps': []}
    def save():
        status.write_text(json.dumps(record, indent=2), encoding='utf-8')
    def run(script, *options):
        print('Packaging: '+script, flush=True)
        subprocess.run([sys.executable, str(ROOT/script), *options], check=True)
        record['completed_steps'].append(script + (' --direct-run' if '--direct-run' in options else ''))
        save()
    save()
    preflight(args)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    if args.target in ('Tools', 'All'):
        run('scripts/package-tools.py')
    if args.target in ('Firmware', 'All'):
        run('firmware/mrn/package.py')
        run('firmware/mrn/hmi_patch/package-copy.py')
    if args.target in ('Receiver', 'All'):
        options = ['--identity', str(args.identity)] + (['--no-logs'] if args.no_logs else [])
        run('carplay/scripts/package-usb-test.py', *options)
        run('carplay/scripts/package-usb-test.py', *options, '--direct-run')
    write_inventory(OUTPUT, GROUPS[args.target], args.target, args.no_logs)
    record['complete'] = True
    record['packages'] = GROUPS[args.target]
    save()
    print(f'Complete: {len(record["packages"])} packages -> {OUTPUT}')


if __name__ == '__main__':
    main()
