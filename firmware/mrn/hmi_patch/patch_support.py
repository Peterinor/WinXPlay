"""Exact-F3.21 patch constants and byte-identical restoration."""
import hashlib, struct, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts'))
from external_assets import oem_path
SOURCE = oem_path('ENTHMI2.EXE')
EXPECTED = '65774c87ded13f1d5d11e086934203ee4965197e2e0ed0cf3546fec2d982320e'
BASE = 0x10000
def sha(data): return hashlib.sha256(data).hexdigest()
def align(value, alignment): return (value + alignment - 1) // alignment * alignment
def branch(source, target):
    delta = target - source - 8
    assert delta % 4 == 0 and -(1 << 25) <= delta < (1 << 25)
    return struct.pack('<I', 0xea000000 | ((delta >> 2) & 0xffffff))

def restore(patched,manifest):
    if sha(patched)!=manifest['patched_sha256']: raise ValueError('Patched hash mismatch')
    result=bytearray(patched[:manifest['original_size']])
    for c in manifest['changes_in_original']:
        off=c['offset'];new=bytes.fromhex(c['new']);old=bytes.fromhex(c['old'])
        assert result[off:off+len(new)]==new
        result[off:off+len(old)]=old
    if sha(result)!=manifest['original_sha256']: raise ValueError('Restoration hash mismatch')
    return bytes(result)
