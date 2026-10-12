"""Validate and package the CE reboot tool; never execute it on the host/device."""
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


def package(output=None, scheme='both'):
    output = (output or ROOT / 'build/packages/system_ops').resolve()
    exe = ROOT / 'build/tools/system_ops/system_ops.exe'
    binary = exe.read_bytes()
    record = json.loads(exe.with_suffix('.build.json').read_text(encoding='utf-8-sig'))
    def verify():
        if hashlib.sha256(exe.read_bytes()).hexdigest() != record['sha256']:
            raise ValueError('Binary differs from recorded build')
        for name, expected in record['source_sha256'].items():
            if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != expected:
                raise ValueError('Rebuild changed source: ' + name)
    verify()
    result = subprocess.run([sys.executable, str(ROOT / 'tests/test_system_ops.py'), '-v'],
                            capture_output=True, env=dict(os.environ, PYTHONIOENCODING='utf-8'))
    if result.returncode:
        raise RuntimeError((result.stdout + result.stderr).decode('utf-8', errors='replace'))
    verify()
    publish(output, {'system_ops.exe': binary, 'systemops.txt': (APP / 'systemops.txt').read_bytes(),
                     '使用说明.md': (APP / 'README.md').read_bytes()}, 'system_ops.exe', scheme)
    validation = ROOT / 'build/validation/system_ops-packaging.txt'
    validation.parent.mkdir(parents=True, exist_ok=True)
    validation.write_bytes(result.stdout + result.stderr)
    print(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--scheme', choices=('A', 'B', 'both'), default='both')
    args = parser.parse_args()
    package(args.output, args.scheme)
