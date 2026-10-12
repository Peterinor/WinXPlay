"""Shared ARM execution helpers for local WinCE checks, with OS test doubles."""
from pathlib import Path
import sys,struct
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import arm_digest,pefile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
OPS=ROOT/'build/tools/file_ops/file_ops.exe'
from external_assets import oem_path
FACTORY=oem_path('coredll.dll')

class Arm:
    def __init__(self,path):
        self.pe=pefile.PE(str(path));base=self.pe.OPTIONAL_HEADER.ImageBase
        self.u=Uc(UC_ARCH_ARM,UC_MODE_ARM);self.u.mem_map(base,(self.pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095)
        self.u.mem_write(base,self.pe.get_memory_mapped_image());self.u.mem_map(0x20000000,0x20000)
        self.exports={s.name.decode():base+s.address for s in self.pe.DIRECTORY_ENTRY_EXPORT.symbols}
    def call(self,name,*args,instruction_limit=300000):
        for r,v in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],args):self.u.reg_write(r,v)
        self.u.reg_write(UC_ARM_REG_SP,0x2001f000);self.u.reg_write(UC_ARM_REG_LR,0x20000000)
        self.u.emu_start(self.exports[name],0x20000000,count=instruction_limit)
        assert self.u.reg_read(UC_ARM_REG_PC)==0x20000000
        return self.u.reg_read(UC_ARM_REG_R0)
    def wide(self,address,value):self.u.mem_write(address,(value+'\0').encode('utf-16le'))
