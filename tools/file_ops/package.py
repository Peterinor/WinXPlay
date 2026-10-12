"""Package the standalone USB file manager without installing or launching it."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys

APP = Path(__file__).resolve().parent
ROOT = APP.parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from runtime_package import publish


def sha(data):
    return hashlib.sha256(data).hexdigest()


def package(output, scheme="both"):
    output = (output or ROOT/'build/packages'/'file_ops').resolve()
    exe = ROOT / 'build/tools/file_ops/file_ops.exe'
    record_path = exe.with_suffix('.build.json')
    record = json.loads(record_path.read_text(encoding='utf-8-sig'))
    binary = exe.read_bytes()
    if sha(binary) != record['sha256']:
        raise ValueError('Binary changed since build; run file_ops/build.ps1')
    for relative, expected in record['source_sha256'].items():
        if sha((ROOT / relative).read_bytes()) != expected:
            raise ValueError('Source changed since build: ' + relative)
    result = subprocess.run([sys.executable, str(ROOT / 'tests/test_file_manager.py'), '-v'],
                            capture_output=True, env=dict(os.environ, PYTHONIOENCODING='utf-8'))
    if result.returncode:
        raise RuntimeError((result.stdout + result.stderr).decode('utf-8', errors='replace'))
    files = {'file_ops.exe': binary,
             'fileops.txt': (APP / 'examples/copy.txt').read_bytes(),
             '使用说明.md': (APP / 'README.md').read_bytes()}
    for p in (APP / 'examples').glob('*.txt'):
        files[p.stem + '.example.txt'] = p.read_bytes()
    if exe.read_bytes() != binary or json.loads(record_path.read_text(encoding='utf-8-sig')) != record:
        raise ValueError('Build changed during packaging')
    for relative, expected in record['source_sha256'].items():
        if sha((ROOT / relative).read_bytes()) != expected:
            raise ValueError('Source changed during packaging: ' + relative)
    # Validate every provided recipe through the actual ARM parser.
    sys.path.insert(0, str(ROOT / 'tests'))
    from test_file_ops_policy import Tests
    for name, data in files.items():
        if name.endswith('.txt'):
            for recipe in (data, data.replace(b'AutoLaunch\\', b'AUTOLAUNCH_PROD\\')):
                if not Tests().parse(recipe)[0]:
                    raise ValueError('Invalid shipped configuration: ' + name)
    publish(output, files, 'file_ops.exe', scheme)
    validation = ROOT / 'build/validation/file_ops-packaging.txt'
    validation.parent.mkdir(parents=True, exist_ok=True)
    validation.write_bytes(result.stdout + result.stderr)
    return {'directory': str(output), 'vehicle_tested': False, 'schemes': ['A','B'] if scheme=='both' else [scheme]}



if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=None)
    parser.add_argument('--scheme', choices=('A', 'B', 'both'), default='both')
    args = parser.parse_args()
    print(json.dumps(package(args.output, args.scheme), ensure_ascii=True))
