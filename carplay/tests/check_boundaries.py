"""Check the architectural boundary, not the implementation of protocol methods."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
PORTABLE = {ROOT / 'core', ROOT / 'interfaces'}
FORBIDDEN = re.compile(
    r'\b(?:HANDLE|HWND|HDC|SOCKET|CRITICAL_SECTION|UNDER_CE|WINXPLAY_WOLFSSL|'
    r'GetTickCount|CreateThread|EnterCriticalSection|LocalAuth|LiveVideo|LiveAudio)\b')
checked = []
for folder in sorted(PORTABLE):
    for path in sorted(folder.glob('*')):
        if path.suffix not in ('.h', '.cpp'):
            continue
        text = path.read_text(encoding='utf-8-sig')
        code = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
        assert not FORBIDDEN.search(code), f'Platform token leaked into {path}'
        for quoted, system in re.findall(r'#\s*include\s*(?:"([^"]+)"|<([^>]+)>)', code):
            if quoted:
                dependency = (path.parent / quoted).resolve()
                assert dependency.is_file(), dependency
                assert dependency.parent in PORTABLE, f'Nonportable include: {path}: {quoted}'
            else:
                assert system in {
                    'vector', 'string', 'map', 'stdexcept', 'deque', 'algorithm',
                    'sstream', 'memory', 'cstring', 'cstdlib', 'set', 'limits',
                    'cmath', 'cctype',
                }, f'Nonstandard header: {path}: {system}'
        checked.append(path.relative_to(ROOT).as_posix())

# Platform code may use the device contract, but cannot select a concrete car.
for path in (ROOT / 'platform').rglob('*'):
    if path.suffix not in ('.h', '.cpp', '.inc'):
        continue
    text = path.read_text(encoding='utf-8-sig')
    for quoted in re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.M):
        dependency = (path.parent / quoted).resolve()
        if dependency.is_relative_to(ROOT / 'devices'):
            assert dependency.parent == ROOT / 'devices', f'Concrete device include: {path}: {quoted}'
    assert 'WINXPLAY_DEVICE_MRN' not in text, f'Device selection leaked into platform: {path}'
    for token in ('NAND_USER', 'NAND_RES', 'AVV0:', 'USBCDCNCM', '3D581965-7C6E-4FE9-894F-50FC821BAE68'):
        assert token not in text, f'OEM configuration leaked into platform: {path}: {token}'

# Every source-level local include must resolve, including files moved out of CE.
for path in ROOT.rglob('*'):
    if not path.is_file() or path.suffix not in ('.h', '.cpp', '.inc'):
        continue
    if any(part in ('out', 'dist') for part in path.relative_to(ROOT).parts):
        continue
    text = path.read_text(encoding='utf-8-sig')
    for quoted in re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.M):
        dependency = (path.parent / quoted).resolve()
        assert dependency.is_file(), f'Missing include: {path}: {quoted}'
        # Integration tests may include the sibling HMI bridge's public policy.
        # Production receiver sources stay within the receiver module; core's
        # stricter whitelist above is unchanged.
        boundary = ROOT.parent if path.is_relative_to(ROOT / 'tests') else ROOT
        assert boundary in dependency.parents, f'External local include: {path}: {quoted}'

print(json.dumps({'portable_files_checked': len(checked), 'passed': True, 'files': checked}, indent=2))
