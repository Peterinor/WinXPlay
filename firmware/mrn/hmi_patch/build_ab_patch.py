"""Exact F3.21, reversible WinXPlay input hooks. Writes a NEW EXE, never deploys."""
import json, os, struct, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'scripts'))
from patch_support import SOURCE, EXPECTED, BASE, ROOT, align, branch, sha, restore
from external_assets import oem_path
import pefile

OUT=ROOT/'build/firmware/ab_bridge'
BAND=0x1dd2388
WINDOW=0x2042038
DLL_PATH=r'\NAND_USER\WinXPlay\WinXPlayBridge.dll'

def build():
    original=SOURCE.read_bytes()
    if sha(original)!=EXPECTED: raise ValueError('Exact original F3.21 required; do not stack on BAND-patched EXE')
    pe=pefile.PE(data=original)
    assert pe.OPTIONAL_HEADER.ImageBase==BASE
    assert pe.get_data(BAND-BASE,4)==bytes.fromhex('f04f2de9')
    assert pe.get_data(WINDOW-BASE,4)==bytes.fromhex('0dc0a0e1')
    assert not pe.OPTIONAL_HEADER.DATA_DIRECTORY[4].VirtualAddress
    assert pe.get_overlay_data_start_offset() is None
    header=pe.sections[-1].get_file_offset()+40
    assert header+80<=pe.OPTIONAL_HEADER.SizeOfHeaders and not any(original[header:header+80])
    OUT.mkdir(parents=True,exist_ok=True)
    sa,fa=pe.OPTIONAL_HEADER.SectionAlignment,pe.OPTIONAL_HEADER.FileAlignment
    cr=align(pe.OPTIONAL_HEADER.SizeOfImage,sa);dr=cr+sa
    data=bytearray(0x800)
    for off,text in [(0x40,DLL_PATH),(0x140,'MrnBridgeStart')]:
        encoded=(text+'\0').encode('utf-16le');assert off+len(encoded)<=0x200
        data[off:off+len(encoded)]=encoded
    descriptors=b''.join(d.struct.__pack__() for d in pe.DIRECTORY_ENTRY_IMPORT)
    data[0x200:0x200+len(descriptors)]=descriptors
    pos=0x200+len(descriptors);assert pos+40<=0x600
    # These two ordinal imports are pinned to the supplied original COREDLL.
    core=pefile.PE(str(oem_path('coredll.dll')))
    names={s.ordinal:s.name for s in core.DIRECTORY_ENTRY_EXPORT.symbols}
    assert names[528]==b'LoadLibraryW' and names[530]==b'GetProcAddressW'
    data[0x600:0x60c]=struct.pack('<III',0x80000210,0x80000212,0)
    data[0x610:0x61c]=data[0x600:0x60c]
    data[0x620:0x62c]=b'COREDLL.dll\0'
    data[pos:pos+20]=struct.pack('<IIIII',dr+0x600,0,0,dr+0x620,dr+0x610)
    asm=(ROOT/'firmware/mrn/hmi_patch/hook.s.in').read_text()
    for k,v in dict(CODE_RVA=cr,STATE_RVA=dr,PATH_RVA=dr+0x40,EXPORT_RVA=dr+0x140,
                    LOAD_IAT_RVA=dr+0x610,PROC_IAT_RVA=dr+0x614).items():asm=asm.replace('@'+k+'@',hex(v))
    (OUT/'hook.s').write_text(asm,encoding='ascii')
    vc=Path(os.environ['CARPLAY_VC']);env=dict(os.environ)
    env['PATH']=str(vc/'bin')+';'+str(vc.parent/'Common7/IDE')+';'+env['PATH']
    subprocess.run([str(vc/'ce/bin/x86_arm/armasm.exe'),'-32','-arch','5T',str(OUT/'hook.s'),str(OUT/'hook.obj')],check=True,env=env)
    exports=['HookStart','Worker','TrampolineBranch','WindowHook','WindowBranch']
    subprocess.run([str(vc/'ce/bin/x86_arm/link.exe'),'/nologo','/DLL','/NOENTRY','/NODEFAULTLIB','/MACHINE:ARM',
                    '/OUT:'+str(OUT/'hook.dll'),*[f'/EXPORT:{s}' for s in exports],str(OUT/'hook.obj')],check=True,env=env)
    payload=pefile.PE(str(OUT/'hook.dll'));section=next(s for s in payload.sections if s.Name.rstrip(b'\0')==b'.text')
    assert not getattr(payload,'DIRECTORY_ENTRY_IMPORT',None) and not getattr(payload,'DIRECTORY_ENTRY_BASERELOC',None)
    symbols={s.name.decode():s.address-section.VirtualAddress for s in payload.DIRECTORY_ENTRY_EXPORT.symbols}
    code=bytearray(section.get_data()[:section.Misc_VirtualSize]);assert len(code)<=sa and symbols['HookStart']==0
    for sym,dest in [('TrampolineBranch',BAND+4),('WindowBranch',WINDOW+4)]:
        off=symbols[sym];code[off:off+4]=branch(BASE+cr+off,dest)
    result=bytearray(original)
    for address,sym in [(BAND,'HookStart'),(WINDOW,'WindowHook')]:
        off=pe.get_offset_from_rva(address-BASE);result[off:off+4]=branch(address,BASE+cr+symbols[sym])
    rc=align(len(result),fa);result.extend(bytes(rc-len(result)))
    nc=align(len(code),fa);result.extend(code);result.extend(bytes(nc-len(code)))
    rd=len(result);nd=align(len(data),fa);result.extend(data);result.extend(bytes(nd-len(data)))
    for i,(name,rva,size,raw,rawsize,flags) in enumerate([
        (b'.abcode',cr,len(code),rc,nc,0x60000020),(b'.abdata',dr,len(data),rd,nd,0xc0000040)]):
        result[header+i*40:header+(i+1)*40]=struct.pack('<8sIIIIIIHHI',name,size,rva,rawsize,raw,0,0,0,0,flags)
    def field(obj,name,value):struct.pack_into('<I',result,obj.get_field_absolute_offset(name),value)
    struct.pack_into('<H',result,pe.FILE_HEADER.get_field_absolute_offset('NumberOfSections'),len(pe.sections)+2)
    field(pe.OPTIONAL_HEADER,'SizeOfImage',align(dr+len(data),sa))
    field(pe.OPTIONAL_HEADER,'SizeOfCode',pe.OPTIONAL_HEADER.SizeOfCode+nc)
    field(pe.OPTIONAL_HEADER,'SizeOfInitializedData',pe.OPTIONAL_HEADER.SizeOfInitializedData+nd)
    field(pe.OPTIONAL_HEADER.DATA_DIRECTORY[1],'VirtualAddress',dr+0x200)
    field(pe.OPTIONAL_HEADER.DATA_DIRECTORY[1],'Size',len(descriptors)+40)
    field(pe.OPTIONAL_HEADER,'CheckSum',pefile.PE(data=result).generate_checksum())
    changes=[];start=None
    for i,(a,b) in enumerate(zip(original,result)):
        if a!=b and start is None:start=i
        if a==b and start is not None:
            changes.append(dict(offset=start,old=original[start:i].hex(),new=result[start:i].hex()));start=None
    if start is not None:changes.append(dict(offset=start,old=original[start:].hex(),new=result[start:len(original)].hex()))
    manifest=dict(original_sha256=sha(original),patched_sha256=sha(result),original_size=len(original),
        patched_size=len(result),changes_in_original=changes,code_rva=cr,data_rva=dr,symbols=symbols,
        band_va=BAND,window_va=WINDOW,load_iat_rva=dr+0x610,proc_iat_rva=dr+0x614,
        behavior='BAND starts/toggles WinXPlay via optional worker-loaded bridge; only owned input is consumed.',
        limitations=['No vehicle/CE loader test','Runtime main-window/common-WndProc correspondence unverified',
                     'OS window handoff is not a verified physical layer handoff; no raw CAN mapping or firmware deployment'])
    assert restore(bytes(result),manifest)==original
    (OUT/'ENTHMI2.WINXPLAY.EXE').write_bytes(result)
    (OUT/'patch-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print('Patched copy and byte-identical rollback manifest:',OUT)
    return manifest

if __name__=='__main__':build()
