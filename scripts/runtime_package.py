"""Publish flat A/B runtime directories; never write to the vehicle or USB."""
from pathlib import Path
import re
from arm_digest import digest

STARTUP_FILES = {'Autolaunch.txt', 'AutoLaunch_prod.txt'}
ROOT = Path(__file__).resolve().parents[1]


def license_payload():
    """Copy the authoritative project license and dependency notices."""
    return {'LICENSE': (ROOT / 'LICENSE').read_bytes(),
            'THIRD_PARTY.txt': (ROOT / 'THIRD_PARTY.md').read_bytes()}


def production_data(name, data):
    # Convert text recipes and guides, never EXE/DLL or identity material.
    if Path(name).name in ('LICENSE', 'THIRD_PARTY.txt'):
        return data
    suffix = Path(name).suffix.lower()
    if suffix not in ('.txt', '.ini', '.md', '.xml', '.plist'):
        return data
    if suffix == '.plist' and data.startswith(b'bplist'):
        return data
    encoding = 'utf-16' if data.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'
    bom = data.startswith(b'\xef\xbb\xbf')
    text = data.decode(encoding)
    text = text.replace('Autolaunch.txt', 'AutoLaunch_prod.txt')
    text = re.sub(r'\bAutoLaunch\b', 'AUTOLAUNCH_PROD', text)
    return text.encode(encoding if encoding == 'utf-16' or bom else 'utf-8')


def publish(output, files, executable, scheme='both'):
    output = Path(output).resolve()
    selected = ('A', 'B') if scheme == 'both' else (scheme,)
    for variant in selected:
        if variant not in ('A', 'B'):
            raise ValueError('Unknown startup scheme')
        _publish_scheme(output/('AutoLaunch' if variant=='A' else 'AUTOLAUNCH_PROD'), files, executable, variant)
    return output


def _publish_scheme(output, files, executable, scheme):
    if scheme not in ('A', 'B'):
        raise ValueError('Unknown startup scheme')
    payload = {name: data for name, data in files.items() if name not in STARTUP_FILES}
    for name, data in license_payload().items():
        payload.setdefault(name, data)
    for name in payload:
        path = Path(name)
        if path.is_absolute() or '..' in path.parts or path.suffix.lower() in ('.zip', '.cpp', '.h', '.py', '.ps1', '.obj', '.lib'):
            raise ValueError('Only runtime files belong in this package: ' + name)
        if not (output/path).resolve().is_relative_to(output):
            raise ValueError('Package path escapes output: ' + name)
    binary = payload[executable]
    filename = 'Autolaunch.txt' if scheme == 'A' else 'AutoLaunch_prod.txt'
    opposite = 'AutoLaunch_prod.txt' if scheme == 'A' else 'Autolaunch.txt'
    if (output/opposite).exists():
        raise ValueError('Do not mix A and B in one directory: ' + str(output))
    if scheme == 'B':
        payload = {name: production_data(name, data) for name, data in payload.items()}
        checksum = sum(byte.bit_count() for byte in binary) & 0xffffffff
        payload[filename] = f'{checksum:08X},\\Hard Disk\\AUTOLAUNCH_PROD\\{executable},\r\n'.encode('ascii')
    else:
        payload[filename] = (digest(binary) + '|0|\\Hard Disk\\AutoLaunch\\' + executable + '|').encode('ascii')
    # Per-package instructions identify the correct destination folder.
    if '使用说明.md' in payload:
        folder = 'AutoLaunch' if scheme == 'A' else 'AUTOLAUNCH_PROD'
        header = f'本次启动方案 {scheme}：将本目录内文件复制到 U 盘 `{folder}` 中。\n\n'
        body = payload['使用说明.md'].decode('utf-8-sig')
        body = re.sub(r'^本次启动方案 [AB]：[^\n]*\n\n', '', body)
        payload['使用说明.md'] = (header + body).encode('utf-8')
    output.mkdir(parents=True, exist_ok=True)
    for name, data in payload.items():
        path = output/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        if path.read_bytes() != data:
            raise IOError('Package readback failed: ' + name)
    return output


def publish_existing_variants(output, executable, names=None):
    """Finalize AutoLaunch and publish AUTOLAUNCH_PROD under the same package root."""
    output = Path(output).resolve()
    if output.name != 'AutoLaunch':
        raise ValueError('Expected the package AutoLaunch directory: ' + str(output))
    names = names or [p.relative_to(output).as_posix() for p in output.rglob('*') if p.is_file()]
    files = {name: (output/name).read_bytes() for name in names if name not in STARTUP_FILES}
    return publish(output.parent, files, executable, 'both')
