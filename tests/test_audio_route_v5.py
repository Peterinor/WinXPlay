"""Execute compiled audio IOCTL wrappers using substituted system I/O."""
import sys
import struct
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from arm_support import Arm,pefile,FACTORY
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_PC,UC_ARM_REG_LR
EXE=ROOT/'build/carplay/vehicle-tests/winxplay_test_av.exe'

class AudioRouteTests(unittest.TestCase):
    def run_wrapper(self,action=None,result=1):
        a=Arm(EXE);core=pefile.PE(str(FACTORY));names={s.ordinal:s.name for s in core.DIRECTORY_ENTRY_EXPORT.symbols}
        calls=[];last_error=[];hooks={};a.u.mem_map(0x30000000,0x10000)
        for module in a.pe.DIRECTORY_ENTRY_IMPORT:
            if module.dll.lower()!=b'coredll.dll':continue
            for imp in module.imports:
                name=imp.name or names.get(imp.ordinal)
                if name in (b'DeviceIoControl',b'SetLastError'):
                    stub=0x30000000+4*len(hooks);hooks[stub]=name;a.u.mem_write(imp.address,struct.pack('<I',stub))
        def hook(u,addr,size,data):
            if addr not in hooks:return
            args=[u.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]
            if hooks[addr]==b'DeviceIoControl':
                tail=struct.unpack('<4I',u.mem_read(u.reg_read(UC_ARM_REG_SP),16))
                calls.append((args[0],args[1],bytes(u.mem_read(args[2],args[3])) if args[3] else b'',tail))
                value=result
            else:last_error.append(args[0]);value=0
            u.reg_write(UC_ARM_REG_R0,value);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
        a.u.hook_add(UC_HOOK_CODE,hook)
        returned=a.call('MrnEsaiUnmute',123) if action is None else a.call('MrnUsbRouteCommand',123,action)
        return returned,calls,last_error

    def test_source_request_and_release_bytes(self):
        for action,code,payload in [(1,4,b'\x09\x01'),(2,1,b'\x09'),(3,2,b'\x09'),(4,4,b'\x09\x00')]:
            self.assertEqual(self.run_wrapper(action),(1,[(123,code,payload,(0,0,0,0))],[]))

    def test_request_failure_preserved(self):
        for action in (1,2,3,4,None):self.assertEqual(self.run_wrapper(action,0)[0],0)

    def test_invalid_action_cannot_send(self):
        for action in (0,5,0xffffffff):self.assertEqual(self.run_wrapper(action),(0,[],[87]))

    def test_unmute_has_no_guessed_payload(self):
        self.assertEqual(self.run_wrapper(),(1,[(123,8,b'',(0,0,0,0))],[]))

    def test_arm_ce_and_new_imports_exist_in_factory(self):
        pe=pefile.PE(str(EXE));self.assertEqual(pe.FILE_HEADER.Machine,0x1c2)
        self.assertEqual(pe.OPTIONAL_HEADER.Subsystem,9)
        core=pefile.PE(str(FACTORY));names={s.ordinal:s.name for s in core.DIRECTORY_ENTRY_EXPORT.symbols};known=set(names.values())
        found=set()
        for module in pe.DIRECTORY_ENTRY_IMPORT:
            if module.dll.lower()!=b'coredll.dll':continue
            for imp in module.imports:
                name=imp.name or names.get(imp.ordinal);self.assertIn(name,known);found.add(name)
        self.assertTrue({b'DeviceIoControl',b'WaitForSingleObject'}<=found)
        self.assertNotIn(b'CreateProcessW',found)
        self.assertNotIn(b'RegSetValueExW',found)

    def test_audio_tool_has_no_process_helper_dependency(self):
        data=EXE.read_bytes()
        self.assertNotIn('process_ops.exe'.encode('utf-16le'),data)
        self.assertNotIn(b'run_processops',data)
        self.assertIn(b'av-test-v8-channel-pattern',data)

if __name__=='__main__':unittest.main()
