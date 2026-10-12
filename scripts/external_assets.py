"""Paths to user-supplied OEM inputs; no proprietary data is bundled."""
from pathlib import Path
import os

ROOT = Path(__file__).resolve().parents[1]

def oem_path(name):
    key = 'WINXPLAY_OEM_' + name.upper().replace('.', '_')
    return Path(os.environ.get(key, str(Path(os.environ.get('WINXPLAY_OEM_ROOT', str(ROOT / '.local/oem'))) / name)))

def require_oem(name):
    path = oem_path(name)
    if not path.is_file():
        raise FileNotFoundError(f'Missing optional OEM input {name}. Set WINXPLAY_OEM_ROOT or WINXPLAY_OEM_{name.upper().replace(".", "_")}; see docs/build.md.')
    return path
