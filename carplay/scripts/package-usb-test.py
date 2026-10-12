"""Prepare runtime files with separate FILEOPS deploy and launch recipes."""
from pathlib import Path
import argparse
import hashlib
import json
import plistlib
import shutil
import sys
from usb_test_assets import private_material

ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT.parent
sys.path.insert(0,str(REPO/'scripts'))
from arm_digest import digest
from runtime_package import publish_existing_variants, license_payload


def package_notices(out):
    for name, data in license_payload().items():
        (out / name).write_bytes(data)


def record_path(out):
    target=REPO/'build/validation'/(out.parent.name+'-package.json')
    target.parent.mkdir(parents=True,exist_ok=True)
    return target


def no_logs_instructions(direct):
    return '''WinXPlay：无日志运行版

已关闭运行日志、崩溃日志和诊断快照；不会自动删除以前的日志。
配对、接收端身份、配置保存及屏幕提示仍然正常工作。FILEOPS 的复制结果保留。
桥接已禁用系统触摸注入，由 HMI 内的 DLL 向 WinXPlay 投递标准窗口输入消息，主程序不含 MRN 私有接收协议。
''' + ('''本包从 U盘直接运行。程序运行时不能拔掉 U盘；单 USB 口日常使用请用内部安装版。
''' if direct else '''首次部署使用默认 fileops.txt（包含认证材料）。已有安装更新时，用 update-bridge-fileops.txt 替换 fileops.txt，只覆盖主程序、桥接 DLL 和 USB DLL，保留配置和配对。
先退出 WinXPlay 和原厂 HMI、拔下手机，再执行复制；确认 ALL_JOBS_COMPLETE 后重启车机，使已加载的 DLL 一并更新。不需要覆盖 HMI EXE。
''')

def direct_package(args,out,folder,build):
    """Same receiver and assets, launched in place from USB; no FILEOPS step."""
    if (folder/'fileops.txt').exists() or (folder/'payload').exists():
        raise RuntimeError('Use a separate output folder for the direct-run package')
    for name in ('WinXPlay.exe','WinXPlayUsbMux.dll'):
        shutil.copyfile(build/name,folder/name)
    private_material(folder,args.identity,args.pair_record)
    settings={'CARPLAY_PORTABLE':'1','CARPLAY_WAVE_OUT':'1','CARPLAY_TOUCH':'1'}
    (folder/'carplay.plist').write_bytes(plistlib.dumps(settings))
    checksum=digest((folder/'WinXPlay.exe').read_bytes())
    (folder/'Autolaunch.txt').write_bytes(
        f'{checksum}|0|\\Hard Disk\\AutoLaunch\\WinXPlay.exe|'.encode('ascii'))
    instructions=r'''WinXPlay：U盘直接启动，检查 USB 驱动加载

1. 将本目录内全部运行文件复制到 U盘 AutoLaunch 目录，使用本包的 Autolaunch.txt。
2. 确保内部存储的旧 WinXPlay 已退出，插入车机并触发 AutoLaunch。
3. 本包直接启动 U盘上的 WinXPlay.exe，不需要 FILEOPS 复制、RUN 或安装。
4. 本轮 LoadDriver 加载检查不需要连接 iPhone。等待至少 35 秒，再正常结束程序；不要在程序运行期间拔出 U盘。
5. 日志直接位于 U盘 AutoLaunch 目录，无需 collect：
   portable-startup.plist：usb_diagnostics_version 应为 5，重点看 usb_driver_loader_probe；usb_stream_activation 应为 ActivateDeviceEx。
   usb-connection.plist、usb-driver.log、auto-connect.plist：后续 USB 状态。
   没连接 iPhone 时，界面仍显示 CMX1 open error=55 是预期现象。

程序、DLL、carplay.plist 和 identity 文件夹需保持本包相对位置。
本包仍执行现有 USB 准备流程：按实际手机描述符选择配置，并把 WinXPlayUsbMux 驱动路径登记为 \Hard Disk\AutoLaunch\WinXPlayUsbMux.dll；不会复制程序到内部存储。结束本轮后如需使用内部安装版，重新启动 \NAND_USER\WinXPlay\WinXPlay.exe，由它重新登记内部 DLL 路径。
LoadDriver 检查在接收端进程执行，不能代替 USB 主机实际接入、CMX1 创建和 CarPlay 测试。
证书与私钥已包含，不要把 identity 文件夹作为日志分享。
'''
    (out/'使用说明.txt').write_text(no_logs_instructions(True) if args.no_logs else instructions,encoding='utf-8-sig')
    package_notices(out)
    names=['WinXPlay.exe','WinXPlayUsbMux.dll','carplay.plist','Autolaunch.txt',
           'identity/certificate.p7b','identity/certificate.der','identity/identity.pk8']
    if args.pair_record:names.append('phone-pair.plist')
    files=[folder/n for n in names]+[out/n for n in ('使用说明.txt','LICENSE','THIRD_PARTY.txt')]
    manifest={p.relative_to(out).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    record_path(out).write_text(json.dumps({'private':True,'mode':'direct_usb','logs_enabled':not args.no_logs,
        'entry':r'\Hard Disk\AutoLaunch\WinXPlay.exe','logs':None if args.no_logs else r'\Hard Disk\AutoLaunch','files':manifest},indent=2),encoding='utf-8')
    for name,value in manifest.items():assert hashlib.sha256((out/name).read_bytes()).hexdigest()==value
    assert (folder/'Autolaunch.txt').read_text('ascii')==f'{checksum}|0|\\Hard Disk\\AutoLaunch\\WinXPlay.exe|'
    publish_existing_variants(out,'WinXPlay.exe' if args.direct_run else 'file_ops.exe',list(manifest))
    print(str(out)+'; '+str(out.parent/'AUTOLAUNCH_PROD'))

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path)
    parser.add_argument('--direct-run',action='store_true',help='Launch WinXPlay directly from USB; logs stay beside EXE')
    parser.add_argument('--identity',type=Path,required=True)
    parser.add_argument('--pair-record',type=Path)
    parser.add_argument('--no-logs',action='store_true',help='Package the verified -NoLogs builds of EXE and both DLLs')
    args=parser.parse_args()
    out=(args.output or REPO/'build/packages'/('winxplay_usb' if args.direct_run else 'winxplay_install')).resolve()/'AutoLaunch'
    repo=ROOT.parent
    # The user keeps test deliverables in build/packages (ignored by source control).
    if (out==repo or repo in out.parents) and repo/'build/packages' not in out.parents:
        raise RuntimeError('Keep generated private packages under build/packages or outside the repository')
    if args.pair_record and not args.pair_record.is_file():raise RuntimeError('Requested trusted phone pairing file is missing')
    for name in ('certificate.p7b','identity.pk8'):
        if not (args.identity/name).is_file():raise RuntimeError('Required identity material is missing: '+name)
    folder=out
    folder.mkdir(parents=True,exist_ok=True)
    build=REPO/'build/carplay'/('ce-wm5-arm-mrn-nologs' if args.no_logs else 'ce-wm5-arm-mrn')
    if args.direct_run:
        direct_package(args,out,folder,build)
        return
    payload_dir=folder/'payload'
    payload_dir.mkdir(exist_ok=True)
    shutil.copyfile(build/'WinXPlay.exe',payload_dir/'WinXPlay.exe')
    # Retire only the former generated payload name, not device files or logs.
    for old_name in ('MRN_BAND_TEST.exe','MRNCarPlayCE.exe','MRNUsbMux.dll'):
        old_entry=payload_dir/old_name
        if old_entry.is_file():old_entry.unlink()
    shutil.copyfile(build/'WinXPlayUsbMux.dll',payload_dir/'WinXPlayUsbMux.dll')
    bridge=REPO/('build/firmware/ab_bridge-nologs/WinXPlayBridge.dll' if args.no_logs else 'build/packages/firmware/WinXPlayBridge.dll')
    verification=json.loads((bridge.parent/'verification.json').read_text(encoding='utf-8'))
    if not verification['passed'] or verification['binaries']['WinXPlayBridge.dll']!=hashlib.sha256(bridge.read_bytes()).hexdigest():
        raise RuntimeError('Build and verify the current HMI bridge before packaging')
    if args.no_logs and verification.get('logs_enabled') is not False:
        raise RuntimeError('NoLogs bridge verification is required')
    shutil.copyfile(bridge,payload_dir/'WinXPlayBridge.dll')
    fileops=REPO/'build/tools/file_ops/file_ops.exe'
    if not fileops.is_file():raise RuntimeError('Build the existing file manager with -CompileOnly first')
    shutil.copyfile(fileops,folder/'file_ops.exe')
    private_material(payload_dir,args.identity,args.pair_record)
    settings_path=payload_dir/'carplay.plist'
    previous=settings_path if settings_path.exists() else folder/'carplay.plist'
    settings=plistlib.loads(previous.read_bytes()) if previous.exists() else {'CARPLAY_WAVE_OUT':'-1'}
    settings['CARPLAY_PORTABLE']='1'
    settings['CARPLAY_WAVE_OUT']='1'
    settings['CARPLAY_TOUCH']='1'
    settings.pop('CARPLAY_RUN_FROM_TEMP',None)
    settings_path.write_bytes(plistlib.dumps(settings))
    target=r'\NAND_USER\WinXPlay'
    # Direct replacement without backups; verify each copy. EXE goes last.
    install_files=['identity/certificate.p7b','identity/certificate.der','identity/identity.pk8',
                   'carplay.plist','WinXPlayUsbMux.dll','WinXPlayBridge.dll']
    if args.pair_record:install_files.append('phone-pair.plist')
    install_files.append('WinXPlay.exe')
    operations=['MRN_FILEOPS_V2']
    for name in install_files:
        native=name.replace('/','\\')
        operations.append(f'COPY|\\Hard Disk\\AutoLaunch\\payload\\{native}|{target}\\{native}')
    (folder/'fileops.txt').write_text('\n'.join(operations)+'\n',encoding='ascii')
    (folder/'run-fileops.txt').write_text(f'MRN_FILEOPS_V2\nRUN|{target}\\WinXPlay.exe\n',encoding='ascii')
    (folder/'update-usb-fileops.txt').write_text('MRN_FILEOPS_V2\n'+''.join(
        f'COPY|\\Hard Disk\\AutoLaunch\\payload\\{name}|{target}\\{name}\n'
        for name in ('WinXPlayUsbMux.dll','carplay.plist','WinXPlay.exe')),encoding='ascii')
    (folder/'update-bridge-fileops.txt').write_text('MRN_FILEOPS_V2\n'+''.join(
        f'COPY|\\Hard Disk\\AutoLaunch\\payload\\{name}|{target}\\{name}\n'
        for name in (('WinXPlayBridge.dll','WinXPlayUsbMux.dll','WinXPlay.exe') if args.no_logs else
                     ('WinXPlayBridge.dll','WinXPlayUsbMux.dll','carplay.plist','WinXPlay.exe'))),encoding='ascii')
    # FILEOPS owns collection and continues after an individual COPY fails.
    # Collect complete original logs; receiver code only records its own events.
    logs=['connection-trace.log','connection-trace.previous.log','crash.log',
          'portable-startup.plist','startup-error.plist','auto-connect.plist',
          'connection-stage.plist','last-error.plist','usb-connection.plist','usb-driver.log',
          'A_BRIDGE.log','session-usb.plist','media-backends.plist',
          'streaming-diagnostics.plist','video-backend.plist','audio-output.plist','audio-recovery.plist',
          'audio-input.plist','host-events.plist']
    (folder/'collect-fileops.txt').write_text('MRN_FILEOPS_V2\n'+''.join(
        f'COPY|{target}\\{name}|{name}\n' for name in logs),encoding='ascii')
    (folder/'collect-bridge-fileops.txt').write_text('MRN_FILEOPS_V2\n'+''.join(
        f'COPY|{target}\\{name}|{name}\n' for name in ('A_BRIDGE.log',)),encoding='ascii')
    # Remove only this script's superseded direct-run files, after preparing payload.
    for name in ('WinXPlay.exe','WinXPlayUsbMux.dll','carplay.plist',
                 'identity/certificate.p7b','identity/certificate.der','identity/identity.pk8'):
        old=folder/name
        if old.is_file():old.unlink()
    old_identity=folder/'identity'
    if old_identity.is_dir() and not any(old_identity.iterdir()):old_identity.rmdir()
    checksum=digest((folder/'file_ops.exe').read_bytes())
    (folder/'Autolaunch.txt').write_bytes(
        f'{checksum}|0|\\Hard Disk\\AutoLaunch\\file_ops.exe|'.encode('ascii'))
    instructions=r'''WinXPlay：运行文件与诊断采集

部署
1. 将 AutoLaunch 目录内容复制到 U盘 AutoLaunch。默认 fileops.txt 复制运行文件到 \NAND_USER\WinXPlay；证书、私钥已包含，手机配对记录由程序保存和复用，不随更新覆盖。
2. 已有正式 BAND 补丁的本次更新：用 update-bridge-fileops.txt 替换 fileops.txt，更新 WinXPlay.exe、WinXPlayBridge.dll、WinXPlayUsbMux.dll 和 carplay.plist（恢复触摸开关）。退出 WinXPlay 和原厂 HMI，拔下 iPhone 后复制；确认每一项成功及 ALL_JOBS_COMPLETE，再重启车机。不必重复覆盖 HMI。若 USB DLL 仍被系统占用，不能把复制失败当成更新成功。
3. 复制和执行独立。重启后按 BAND 启动；无 BAND 补丁时可单独使用 run-fileops.txt 启动内部程序。U盘上的程序正在运行时不要拔盘；内部程序运行不依赖 U盘。

本轮一次完成的操作（约三分钟）
1. 启动内部 WinXPlay，换接已解锁的 iPhone；手机出现提示就确认。等待约 60 秒。
2. 按 BAND 返回原车，再按 BAND 回到 WinXPlay。记录屏幕是否真正切换，以及有无投屏画面。
3. 拔下手机约 10 秒，再插回并解锁，等待约 60 秒。记录是否再次出现“信任”，以及画面是否自动恢复。
4. 完成测试后拔手机，换接 U盘采集。不需要等待定时汇总，也不要为了采集重启 WinXPlay/HMI；重启会重建部分原始日志。
5. 采集时将 collect-fileops.txt 内容替换 fileops.txt，再触发 AutoLaunch。把本轮整个 AutoLaunch/FILEOPS_编号 目录取回即可。

记录内容
WinXPlay 负责记录各自的运行日志，不做日志汇总或采集。日志覆盖启动版本、连接重试历史、USB 枚举和驱动、配对记录读写/复用/手机响应、TLS 和 CarKit、IPv6、视频输出、音频状态、A/B 界面和原厂图层交接。
采集配置使用现有 COPY 指令，逐份复制完整原始日志。FILEOPS 2.9 会记录单项失败并继续后续操作；某阶段尚未到达、未生成对应日志时，查看 RESULT.txt 中的具体复制结果，不影响其他文件采集。采集过程不合并、不截取日志内容。不同日志可能属于不同次运行，按 PID/系统启动毫秒和 RUN_BEGIN 对照。
connection-trace.log 跨程序启动追加，最多保留当前和上一份各约 512KB；记录历史可区分接手机时真正的错误与拔手机取日志之后的 CMX1 error=55。
不采集 identity 文件夹及 phone-pair 配对记录，不记录证书/密钥正文。host_tag 是主机身份的摘要，仅用于判断重连是否更换身份。
日志可确认软件和驱动步骤；物理屏幕是否显示、扬声器是否出声，仍需你观察。未到达的协议阶段不能当作通过。

现有补充配置
collect-bridge-fileops.txt 仅采集 A_BRIDGE.log。
update-usb-fileops.txt 仅更新 USB 驱动和主程序；当前这轮不需要切换到它。
BAND 只切换界面，连接及自动重连在后台继续。退出程序与按 BAND 返回不是同一操作。
'''
    (out/'使用说明.txt').write_text(no_logs_instructions(False) if args.no_logs else instructions,encoding='utf-8-sig')
    package_notices(out)
    runtime_files=['file_ops.exe','Autolaunch.txt','fileops.txt','run-fileops.txt','update-usb-fileops.txt','update-bridge-fileops.txt','collect-fileops.txt','collect-bridge-fileops.txt']
    files=[folder/name for name in runtime_files]+[payload_dir/name for name in install_files]+[out/name for name in ('使用说明.txt','LICENSE','THIRD_PARTY.txt')]
    manifest={p.relative_to(out).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
              for p in files}
    record_path(out).write_text(json.dumps({'private':True,'logs_enabled':not args.no_logs,'entry':'file_ops.exe','receiver_entry':target+r'\WinXPlay.exe','files':manifest},indent=2),encoding='utf-8')
    for name,file_digest in manifest.items():assert hashlib.sha256((out/name).read_bytes()).hexdigest()==file_digest
    assert (folder/'Autolaunch.txt').read_text('ascii')==f'{checksum}|0|\\Hard Disk\\AutoLaunch\\file_ops.exe|'
    publish_existing_variants(out,'WinXPlay.exe' if args.direct_run else 'file_ops.exe',list(manifest))
    print(str(out)+'; '+str(out.parent/'AUTOLAUNCH_PROD'))

if __name__=='__main__':main()
