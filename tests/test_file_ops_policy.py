"""File-operation policy, recipe parsing and rollback checks on compiled ARM code."""
import struct
import unittest
from arm_support import (Arm, OPS, ROOT, FACTORY, pefile, UC_HOOK_CODE,
                         UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_PC, UC_ARM_REG_LR)

class Tests(unittest.TestCase):
    def test_paths_reject_escape_and_system_writes(self):
        a=Arm(OPS)
        cases=[(r'\NAND_USER\MRN_TEST\app.exe',1,1),
               (r'\Windows\entHMI2.exe',1,0),(r'\NAND_USER\other\x.exe',1,0),
               (r'\NAND_USER\MRN_TEST\..\x.exe',1,0),(r'\NAND_USER\MRN_TEST\x.exe.',1,0),
               (r'\NAND_USER\MRN_TEST\x.exe ',1,0),(r'\NAND_USER\MRN_TEST\x:stream',1,0),
               (r'\NAND_USER\MRN_TEST\\x.exe',1,0),(r'\NAND_USER\MRN_TEST\x/evil',1,0),
               ('operation.log',2,1),('../x',2,0),(r'..\x',2,0),('x|y',2,0),
               (r'\Windows\cmd.exe',0,1),(r'\NAND_RES\x',0,1)]
        for path,kind,expected in cases:
            a.wide(0x20001000,path);self.assertEqual(a.call('SafePath',0x20001000,kind),expected,path)
    def parse(self,text):
        a=Arm(OPS)
        # Separate large input/output arenas; neither may overlap the stack or hooks.
        a.u.mem_map(0x21000000,0x200000);a.u.mem_map(0x21200000,0x100000)
        a.u.mem_write(0x21200000,text+b'\0\0\0\0')
        guard=0x21000000+1024*(4+220*2*2)
        a.u.mem_write(guard,b'JOB_BUFFER_END')
        ok=a.call('ParseFileConfig',0x21200000,0x21000000,0x20003000,
                  instruction_limit=30000000)
        self.assertEqual(bytes(a.u.mem_read(guard,14)),b'JOB_BUFFER_END')
        return ok,struct.unpack('<I',a.u.mem_read(0x20003000,4))[0]

    def test_expanded_job_limit_and_overflow_guard(self):
        lines=[br'COPY|\Windows\cmd.exe|'+str(i).encode()+b'.exe' for i in range(1025)]
        self.assertEqual(self.parse(b'FILEOPS_V2\n'+b'\n'.join(lines[:1024])),(1,1024))
        self.assertEqual(self.parse(b'FILEOPS_V2\n'+b'\n'.join(lines)),(0,1024))
    def test_config_validation_before_any_job_execution(self):
        install=br'INSTALL|\Hard Disk\AutoLaunch\payload\B.exe|\NAND_USER\MRN_TEST\B.exe'
        watch=br'WATCH|\NAND_USER\MRN_TEST\B.log|B.log'
        good=b'MRN_FILEOPS_V1\r\n# test\r\n'+install+b'\r\n'+watch+b'\r\n'
        self.assertEqual(self.parse(good),(1,2));self.assertEqual(self.parse(b'\xef\xbb\xbf'+good),(1,2))
        bad=[b'',b'OTHER\n'+install,b'MRN_FILEOPS_V1\n',b'MRN_FILEOPS_V1\n'+watch+b'\n'+install,
             b'MRN_FILEOPS_V1\n'+watch+b'\n'+watch,good.replace(b'INSTALL|',b'DELETE|'),
             good.replace(br'\NAND_USER\MRN_TEST\B.exe',br'\Windows\entHMI2.exe'),
             good.replace(b'B.log\r\n',b'B.log|EXTRA\r\n'),good.replace(b'# test',b'# \xff')]
        for text in bad:self.assertEqual(self.parse(text)[0],0,text)
        self.assertEqual(self.parse(b'MRN_FILEOPS_V1\n'+b'\n'.join(br'COPY|\Windows\cmd.exe|'+str(i).encode()+b'.exe' for i in range(13))),(1,13))
    def test_rename_commit_and_rollback_failures(self):
        core=pefile.PE(str(FACTORY));names={s.ordinal:s.name for s in core.DIRECTORY_ENTRY_EXPORT.symbols}
        for failures,existed,expected in [(set(),1,0),({1},1,1),({2},1,2),({2,3},1,3),(set(),0,0),({1},0,2)]:
            a=Arm(OPS);stub=0x2001e000
            imp=next(i for d in a.pe.DIRECTORY_ENTRY_IMPORT for i in d.imports if (i.name or names.get(i.ordinal))==b'MoveFileW')
            a.u.mem_write(imp.address,struct.pack('<I',stub));calls=[]
            for addr,text in [(0x20001000,'target'),(0x20002000,'staged'),(0x20003000,'backup')]:a.wide(addr,text)
            state={'staged':'new'}
            if existed:state['target']='old'
            def wide(ptr):
                out=bytearray()
                while bytes(a.u.mem_read(ptr,2))!=b'\0\0':out+=a.u.mem_read(ptr,2);ptr+=2
                return out.decode('utf-16le')
            def hook(u,addr,size,data):
                if addr!=stub:return
                src,dst=wide(u.reg_read(UC_ARM_REG_R0)),wide(u.reg_read(UC_ARM_REG_R1));calls.append((src,dst))
                ok=len(calls) not in failures and src in state and dst not in state
                if ok:state[dst]=state.pop(src)
                u.reg_write(UC_ARM_REG_R0,int(ok));u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
            a.u.hook_add(UC_HOOK_CODE,hook)
            self.assertEqual(a.call('ReplaceStaged',0x20001000,0x20002000,0x20003000,existed),expected)
            if expected==0:self.assertEqual(state['target'],'new')
            if existed and expected in (1,2):self.assertEqual(state['target'],'old')
            if expected==3:self.assertEqual(state['backup'],'old')
    def test_file_ops_imports(self):
        core=pefile.PE(str(FACTORY));names={s.ordinal:s.name for s in core.DIRECTORY_ENTRY_EXPORT.symbols};known=set(names.values())
        for path in (OPS,):
            pe=pefile.PE(str(path));resolved=set()
            for d in pe.DIRECTORY_ENTRY_IMPORT:
                self.assertEqual(d.dll.lower(),b'coredll.dll')
                for i in d.imports:
                    n=i.name or names[i.ordinal];self.assertIn(n,known);resolved.add(n)
            self.assertFalse({b'DeviceIoControl',b'RegSetValueExW',b'SuspendThread'}&resolved)

if __name__=='__main__':unittest.main()
