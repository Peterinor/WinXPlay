"""Package one standalone diagnostic and its declared helpers. Never runs CE code."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys
from runtime_package import publish
from diagnostic_catalog import CATALOG, SCENARIOS, binary, main_tool

ROOT=Path(__file__).resolve().parents[1]


def sha(data):return hashlib.sha256(data).hexdigest()


def members(tool):
    ordered=[]
    def visit(name):
        for dependency in CATALOG[name]['dependencies']:visit(dependency)
        if name not in ordered:ordered.append(name)
    visit(tool)
    return ordered


def verify_build(tool):
    path=binary(tool)
    record=json.loads(path.with_suffix('.build.json').read_text(encoding='utf-8-sig'))
    if record['tool']!=tool or sha(path.read_bytes())!=record['sha256']:
        raise RuntimeError(f'Binary/build record mismatch: {tool}; rebuild this tool')
    for relative,expected in record['source_sha256'].items():
        if sha((ROOT/relative).read_bytes())!=expected:
            raise RuntimeError(f'Source changed since build: {relative}; rebuild {tool}')
    for dependency,expected in record['dependency_sha256'].items():
        if sha(binary(dependency).read_bytes())!=expected:
            raise RuntimeError(f'Helper changed since build: {dependency}; rebuild {tool}')
    return record


def package(tool,output=None,scheme="both"):
    tool=main_tool(tool)
    item=CATALOG[tool]
    if item['category'] not in ('basic','experiment','support'):
        raise ValueError('USB packaging supports basic diagnostics and explicit HMI/storage experiments. Helpers and emulator targets are build-only.')
    output=(output or ROOT/'build/packages'/Path(item['executable']).stem).resolve()
    records={name:verify_build(name) for name in members(tool)}
    if item['category'] in ('basic','support'):
        env=dict(os.environ,MRN_DIAGNOSTIC_TEST_TOOL=tool)
        test=subprocess.run([sys.executable,str(ROOT/'tests/test_diagnostics.py'),'-v'],capture_output=True,env=env)
    else:
        env=dict(os.environ,MRN_DIAGNOSTIC_TEST_TOOL=tool)
        test=subprocess.run([sys.executable,str(ROOT/'tests/test_diagnostic_experiments.py'),'-v'],capture_output=True,env=env)
    if test.returncode:raise RuntimeError((test.stdout+test.stderr).decode('utf8',errors='replace'))
    # Recheck after tests so a concurrent rebuild cannot silently mix helper versions.
    for name,record in records.items():
        if verify_build(name)!=record:raise RuntimeError(f'Build changed during packaging: {name}')
    payload={'AutoLaunch/'+CATALOG[name]['executable']:binary(name).read_bytes() for name in members(tool)}
    for config_file in item.get('config_files',[]):
        payload['AutoLaunch/'+config_file]=(item['source_dir']/config_file).read_bytes()
    if item.get('options'):
        config=f"[diagnostic]\r\ndelay_ms={item['delay_ms']}\r\nduration_ms={item.get('duration_ms',20000)}\r\n".encode('ascii')
        if item.get('window_option'):config+=b'show_test_window=0\r\n'
        payload['AutoLaunch/'+Path(item['executable']).stem+'.ini']=config
    scenario=SCENARIOS[item['scenario']]
    instructions=f"# {scenario['title']}\n\n本包共 {len(members(tool))} 个 EXE，启动入口只有 `{item['executable']}`。其他 EXE 由它自动调用，无须单独启动。\n\n将本目录内的文件复制到 U 盘 AutoLaunch 中，完整保留所需运行文件。\n\n{item['description']}。\n\n本次重新构建只完成离线验证，未执行车机或完整 WinCE 客体测试。保留实测日志，旧版本的实车结论不自动适用于本包。\n\n"
    if item['category'] in ('basic','support'):
        instructions+='日志位于 AutoLaunch，名称为本工具 EXE 名加递增编号。DIAGNOSTIC_COMPLETE 仅表示程序逻辑完成，不证明物理屏幕效果。\n\n'
        if item.get('options'):
            instructions+='同名 INI 控制 delay_ms（0–30000）和 duration_ms（1000–60000）；缺少文件使用默认值，存在但无效则停止执行。修改参数无需更改启动摘要。\n\n'
        else:instructions+='启动检查不读取配置、无弹窗、不等待交互，写入入口记录后退出。\n\n'
        if tool=='window-observer':instructions+='默认 show_test_window=0 只观察前台窗口，不创建窗口。设为 1 才显示输入测试窗口，只接收发给自身的消息，不监听所有按键，不隐藏原厂 HMI，画面可能被覆盖。\n\n'
        if tool=='system-information':instructions+='查询完成即退出，duration_ms 不延长运行。\n\n'
    else:
        instructions+=(item['source_dir']/item['instructions_file']).read_text(encoding='utf-8')+'\n\n'
    instructions+='固定目录反复更新程序和默认配置，不生成 ZIP 或源码副本。每次只使用一种启动方案。\n'
    files={name.removeprefix('AutoLaunch/'):data for name,data in payload.items()
           if name!='AutoLaunch/Autolaunch.txt'}
    files['使用说明.md']=instructions.encode('utf8')
    for name,record in records.items():
        if sha(files[CATALOG[name]['executable']])!=record['sha256']:
            raise RuntimeError(f'Binary changed during packaging: {name}')
    publish(output,files,item['executable'],scheme)
    validation=ROOT/'build/validation'/(Path(item['executable']).stem+'-packaging.txt')
    validation.parent.mkdir(parents=True,exist_ok=True)
    validation.write_bytes(test.stdout+test.stderr)
    return {'directory':str(output),'vehicle_tested':False,'schemes':['A','B'] if scheme=='both' else [scheme]}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scenario','--tool',dest='scenario',required=True,help='Scenario folder: '+', '.join(SCENARIOS))
    parser.add_argument('--output',type=Path)
    parser.add_argument('--scheme',choices=('A','B','both'),default='both')
    args=parser.parse_args()
    print(json.dumps(package(args.scenario,args.output,args.scheme),ensure_ascii=True))


if __name__=='__main__':main()
