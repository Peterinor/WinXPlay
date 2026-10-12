"""Prepare only the two-file HMI/bridge COPY recipe; never deploy to a vehicle."""
import hashlib
import json
from pathlib import Path
import shutil
import sys

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/packages/firmware'
sys.path.insert(0,str(ROOT/'scripts'))
from arm_digest import digest
from runtime_package import publish_existing_variants

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    verification=json.loads((OUT/'verification.json').read_text(encoding='utf-8'))
    names=['ENTHMI2.WINXPLAY.EXE','WinXPlayBridge.dll']
    assert verification['passed']
    for name in names:assert sha(OUT/name)==verification['binaries'][name],name
    tool=ROOT/'build/tools/file_ops/file_ops.exe'
    record=json.loads(tool.with_suffix('.build.json').read_text(encoding='utf-8-sig'))
    assert sha(tool)==record['sha256'],'Rebuild the file manager'
    for name,expected in record['source_sha256'].items():
        assert sha(ROOT/name)==expected,'Rebuild the file manager: '+name
    folder=ROOT/'build/packages/firmware_install/AutoLaunch'
    payload=folder/'payload';payload.mkdir(parents=True,exist_ok=True)
    for name in names:shutil.copyfile(OUT/name,payload/name)
    shutil.copyfile(tool,folder/'file_ops.exe')
    (folder/'Autolaunch.txt').write_text(
        digest(tool.read_bytes())+r'|0|\Hard Disk\AutoLaunch\file_ops.exe|',encoding='ascii')
    recipe=(
        'MRN_FILEOPS_V2\r\n'
        'COPY|\\Hard Disk\\AutoLaunch\\payload\\WinXPlayBridge.dll|\\NAND_USER\\WinXPlay\\WinXPlayBridge.dll\r\n'
        'COPY|\\Hard Disk\\AutoLaunch\\payload\\ENTHMI2.WINXPLAY.EXE|\\Windows\\entHMI2.exe\r\n'
    ).encode('ascii')
    # Validate the actual deployed ARM parser, not a second Python grammar.
    sys.path.insert(0,str(ROOT/'tests'))
    from test_file_ops_policy import Tests
    assert Tests().parse(recipe)==(1,2),'COPY recipe rejected by file manager'
    (folder/'fileops.txt').write_bytes(recipe)
    (folder/'复制说明.txt').write_text('''HMI 与 AB Bridge 独立复制包

将本目录内的文件及 payload 文件夹复制到 U盘 AutoLaunch 中，使用本包的 fileops.txt 和 Autolaunch.txt。
两条 COPY 按顺序执行：先桥接 DLL，再 HMI；目标存在就覆盖，不备份。
WinXPlayBridge.dll → \\NAND_USER\\WinXPlay\\WinXPlayBridge.dll
ENTHMI2.WINXPLAY.EXE → \\Windows\\entHMI2.exe（复制时自动使用目标文件名）。

此包只复制以上两个文件，不复制 WinXPlay 主程序、配置或证书，不自动启动、结束进程或重启。
请在原厂 HMI 已退出且桥接 DLL 未被占用时执行。正在运行的 HMI 会被工具拦截；系统只读或 ROM 文件可能无法覆盖。
结果在 U盘 AutoLaunch\\FILEOPS_编号\\RESULT.txt，ALL_JOBS_COMPLETE 表示全部操作及内容核对均完成；单项失败记录日志并继续下一项，FILEOPS_SUCCESS_FAILED 汇总成功和失败数量。
复制完成后移除本复制包，再重新启动 HMI／车机加载新文件，避免下次启动重复覆盖。
这是 F3.21 文件替换包，不是刷机包；复制成功不代表重启后仍保留，需要实车确认。
WinXPlay 主程序及配套文件仍须事先放在 \\NAND_USER\\WinXPlay。
''',encoding='utf-8-sig')
    files=['file_ops.exe','Autolaunch.txt','fileops.txt',
           *['payload/'+name for name in names],'复制说明.txt']
    manifest={'arm_recipe_validated':True,'vehicle_deployed':False,'files':{n:sha(folder/n) for n in files}}
    record=ROOT/'build/validation/firmware-copy-manifest.json'
    record.parent.mkdir(parents=True,exist_ok=True)
    record.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    publish_existing_variants(folder,'file_ops.exe',files)
    print(str(folder)+'; '+str(folder.parent/'AUTOLAUNCH_PROD'))

if __name__=='__main__':main()
