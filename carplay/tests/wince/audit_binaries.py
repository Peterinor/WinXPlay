"""Audit PE architecture, CE subsystem, imports and required driver exports."""
from pathlib import Path
import struct, json, hashlib
import argparse
parser=argparse.ArgumentParser()
parser.add_argument('--build-dir',type=Path,default=Path(__file__).resolve().parents[2]/'../build/carplay'/'ce-wm5-arm-mrn')
parser.add_argument('--expected-count',type=int,default=3)
args=parser.parse_args()
root=args.build_dir
reports=[]
# Renaming to WinXPlay deliberately preserves historical binaries. Audit the
# current build, without deleting old outputs or counting them as new artifacts.
legacy={'MRNCarPlayCE.exe','MRNUsbMux.dll','MRN_MEDIA_PROBE.exe'}
paths=[p for p in list(root.glob('*.exe'))+list(root.glob('*.dll')) if p.name not in legacy]
assert {'WinXPlay.exe','WinXPlayUsbMux.dll'} <= {p.name for p in paths}
for path in paths:
    b=path.read_bytes()
    def u16(o):return struct.unpack_from('<H',b,o)[0]
    def u32(o):return struct.unpack_from('<I',b,o)[0]
    pe=u32(0x3c);assert b[pe:pe+4]==b'PE\0\0'
    optional=pe+24;assert u16(optional)==0x10b
    assert u16(pe+4)==0x1c2 and u16(optional+68)==9
    sections=optional+u16(pe+20)
    def offset(rva):
        for i in range(u16(pe+6)):
            start=sections+i*40
            size,virtual,raw_size,raw=struct.unpack_from('<IIII',b,start+8)
            if virtual<=rva<virtual+max(size,raw_size):return raw+rva-virtual
        raise AssertionError(f'Unmapped RVA {rva:x}')
    def string(rva):
        p=offset(rva);return b[p:b.index(0,p)].decode('ascii')
    imports=[];table=offset(u32(optional+104))
    while u32(table+12):
        imports.append(string(u32(table+12)));table+=20
    assert set(x.lower() for x in imports)<={'coredll.dll','crypt32.dll','ws2.dll','ole32.dll'}
    if path.name=='WinXPlayUsbMux.dll':
        exp=offset(u32(optional+96));names=offset(u32(exp+32))
        exports={string(u32(names+i*4)) for i in range(u32(exp+24))}
        assert {'USBDeviceAttach','USBInstallDriver','USBUnInstallDriver','CMX_Init','CMX_Read','CMX_Write','CMX_IOControl'}<=exports
    reports.append(dict(file=path.name,bytes=len(b),machine='ARM/Thumb',subsystem='Windows CE',imports=imports,sha256=hashlib.sha256(b).hexdigest()))
assert len(reports)==args.expected_count
(root/'pe-audit.json').write_text(json.dumps(reports,indent=2),encoding='utf-8')
print(json.dumps(reports,indent=2))
