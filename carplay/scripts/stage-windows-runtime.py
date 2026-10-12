"""Prepare a local Windows runtime beside the EXE, including configured private identity."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import sys
import pefile
from usb_test_assets import private_material

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts'))
from runtime_package import license_payload


def stage(output):
    output = output.resolve()
    if not (output/'WinXPlay.exe').is_file():
        raise RuntimeError('Build WinXPlay.exe before preparing its runtime')
    sources = {}
    for key, name in [('CARPLAY_CRYPTO', 'libcrypto-3-x64.dll'), ('CARPLAY_SSL', 'libssl-3-x64.dll')]:
        path = Path(os.environ.get(key, str(output/name))).resolve()
        if not path.is_file():
            raise RuntimeError(f'Set {key} to the required OpenSSL 3 x64 DLL: {name}')
        if path.name.lower() != name.lower():
            raise RuntimeError(f'{key} must point to {name}, so dependent DLL names remain consistent')
        sources[name] = path
    # Resolve additional non-system imports from the supplied DLL directories.
    pending = list(sources)
    system = Path(os.environ['SystemRoot'])/'System32'
    while pending:
        name = pending.pop()
        source = sources[name]
        with pefile.PE(str(source)) as pe:
            if pe.FILE_HEADER.Machine != 0x8664:
                raise RuntimeError('Runtime DLL must be x64: '+name)
            imports = [item.dll.decode('ascii') for item in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', [])]
        for dependency in imports:
            key = dependency.lower()
            if key in sources or key.startswith(('api-ms-win-', 'ext-ms-win-')):
                continue
            candidate = source.parent/dependency
            if candidate.is_file():
                sources[key] = candidate
                pending.append(key)
            elif not (system/dependency).is_file():
                raise RuntimeError(f'Missing dependency of {name}: {dependency}')
    identity = os.environ.get('CARPLAY_IDENTITY')
    if identity:
        private_material(output, Path(identity))
    for name, source in sources.items():
        target = output/name
        if target.resolve() != source.resolve():
            shutil.copyfile(source, target)
    for name, data in license_payload().items():
        (output/name).write_bytes(data)
    files = ['WinXPlay.exe'] + list(sources) + ['LICENSE', 'THIRD_PARTY.txt']
    support = ROOT/'carplay/tools/windows'
    for source, relative in [('usb-repair.cmd', 'USB连接修复.cmd'),
                             ('usb-repair.md', 'USB连接修复说明.md'),
                             ('repair-usb-ncm.ps1', 'tools/usb-repair/repair-usb-ncm.ps1'),
                             ('usb-ncm-repair.psm1', 'tools/usb-repair/usb-ncm-repair.psm1')]:
        destination = output/relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(support/source, destination)
        files.append(relative)
    identity_present = all((output/'identity'/n).is_file() for n in ('certificate.p7b', 'identity.pk8'))
    if identity_present:
        files += ['identity/'+p.name for p in sorted((output/'identity').iterdir()) if p.is_file()]
    record = {'private': identity_present, 'identity_present': identity_present,
              'files': {name: hashlib.sha256((output/name).read_bytes()).hexdigest() for name in files}}
    (output/'runtime-assets.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
    print(f'Windows runtime ready: {len(sources)} DLLs; identity '+('present' if identity_present else 'not configured'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    stage(parser.parse_args().output)
