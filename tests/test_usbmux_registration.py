"""Execute production ARM driver registration with isolated registry API substitutes."""
from pathlib import Path
import struct, unittest
import arm_support as base
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC

DLL=base.ROOT/'build/carplay/ce-wm5-arm-mrn/WinXPlayUsbMux.dll'
PATH=r'\NAND_USER\WinXPlay\WinXPlayUsbMux.dll'
CLIENT=r'Drivers\USB\ClientDrivers\WinXPlayUsbMux'
MATCH=r'Drivers\USB\LoadClients\1452\Default\255_254_2\WinXPlayUsbMux'
BOOTSTRAP=r'Drivers\USB\LoadClients\1452\Default\Default\WinXPlayUsbMux'
STUB=bytes.fromhex('0000a0e31eff2fe1')

class Machine(base.Arm):
    def __init__(self,mode='stub',failure=None,dll=DLL):
        super().__init__(dll);self.mode=mode;self.failure=failure;self.error=0
        self.hooks={};self.keys={};self.handles={};self.writes=[];self.next_handle=10
        core=base.pefile.PE(str(base.FACTORY));names={s.ordinal:s.name for s in core.DIRECTORY_ENTRY_EXPORT.symbols}
        for module in self.pe.DIRECTORY_ENTRY_IMPORT:
            for imp in module.imports:
                address=self.add((imp.name or names[imp.ordinal]).decode())
                self.u.mem_write(imp.address,struct.pack('<I',address))
        for name in ('RegisterClientDriverID','RegisterClientSettings','UnRegisterClientDriverID','UnRegisterClientSettings'):
            self.add(name,mode=='stub')
        self.u.hook_add(UC_HOOK_CODE,self.hook)
    def add(self,name,stub=False):
        address=0x20014000+16*len(self.hooks);self.hooks[address]=name
        self.u.mem_write(address,STUB if stub else bytes.fromhex('0100a0e31eff2fe1'));return address
    def text(self,p):
        b=bytearray()
        while bytes(self.u.mem_read(p,2))!=b'\0\0':b+=self.u.mem_read(p,2);p+=2
        return b.decode('utf-16le')
    def hook(self,u,address,size,data):
        name=self.hooks.get(address)
        if not name:return
        regs=[u.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]
        def arg(i):return regs[i] if i<4 else struct.unpack('<I',u.mem_read(u.reg_read(UC_ARM_REG_SP)+4*(i-4),4))[0]
        a,b,c,d=regs;result=1
        def put(p,v):u.mem_write(p,struct.pack('<I',v))
        if name=='LoadLibraryW':result=0 if self.failure=='load' else 111;self.error=126 if not result else 0
        elif name in ('GetProcAddressW','GetProcAddress'):
            wanted=self.text(b);result=next((p for p,n in self.hooks.items() if n==wanted),0)
            if self.failure=='exports':result=0;self.error=127
        elif name=='SetLastError':self.error=a
        elif name=='GetLastError':result=self.error
        elif name=='FreeLibrary':self.error=999  # Verify cleanup cannot clobber the reported failure.
        elif name=='lstrlenW':result=len(self.text(a))
        elif name=='memset':u.mem_write(a,bytes([b&255])*c);result=a
        elif name=='memcpy':u.mem_write(a,bytes(u.mem_read(b,c)));result=a
        elif name=='memcmp':result=0 if u.mem_read(a,c)==u.mem_read(b,c) else 1
        elif name in ('RegisterClientDriverID','RegisterClientSettings','UnRegisterClientDriverID','UnRegisterClientSettings'):
            if self.mode=='stub':return # Execute original firmware's exact return-FALSE instructions.
            result=0 if self.mode=='denied' else 1;self.error=5 if not result else 0
            if name=='RegisterClientSettings':
                assert self.text(a)==PATH and self.text(b)=='WinXPlayUsbMux' and c==0
                assert struct.unpack('<10I',u.mem_read(d,40))in ((40,1452,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,255,254,2),(40,1452,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff))
        elif name=='RegCreateKeyExW':
            key=self.text(b);assert a==0x80000002 and key in (CLIENT,MATCH,BOOTSTRAP)
            if self.failure==('client' if key==CLIENT else 'match'):result=5
            else:
                self.keys.setdefault(key,{})
                self.next_handle+=1;self.handles[self.next_handle]=key
                put(arg(7),self.next_handle);put(arg(8),1);result=0
        elif name=='RegSetValueExW':
            key=self.handles[a];assert key in (MATCH,BOOTSTRAP) and self.text(b)=='DLL' and d==1
            result=5 if self.failure=='set' else 0
            if result==0:
                value=bytes(u.mem_read(arg(4),arg(5)));self.keys[key]['DLL']=value;self.writes.append((key,value))
        elif name=='RegQueryValueExW':
            value=self.keys[self.handles[a]]['DLL'];put(d,1);put(arg(5),len(value))
            u.mem_write(arg(4),value if self.failure!='verify' else b'X'+value[1:]);result=0
        elif name=='RegCloseKey':result=0
        elif name=='RegDeleteKeyW':
            key=self.text(b);assert key in (CLIENT,MATCH,BOOTSTRAP)
            result=0 if key in self.keys else 2;self.keys.pop(key,None)
        else:raise AssertionError('Unexpected API '+name)
        u.reg_write(UC_ARM_REG_R0,result);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
    def install(self):
        self.wide(0x20001000,PATH);ok=self.call('USBInstallDriver',0x20001000)
        error=self.error
        assert self.call('USBGetRegistrationStatus',0x20002000,32)==1
        return ok,error,struct.unpack('<8I',self.u.mem_read(0x20002000,32))

class RegistrationTests(unittest.TestCase):

    def test_generic_ce_never_uses_firmware_registry_fallback(self):
        dll=base.ROOT/'build/carplay/ce-wm5-arm-generic/WinXPlayUsbMux.dll'
        for mode,expected in [('normal',1),('denied',0),('stub',0)]:
            with self.subTest(mode=mode):
                m=Machine(mode,dll=dll);ok,error,r=m.install()
                self.assertEqual(ok,expected)
                self.assertEqual(r[7],0)
                self.assertEqual(m.keys,{})
                self.assertEqual(m.writes,[])
                if mode=='stub':
                    self.assertEqual(m.call('USBUnInstallDriver'),0)
                    self.assertEqual(m.keys,{})

    def test_firmware_stubs_register_exact_own_keys_and_uninstall(self):
        m=Machine();ok,error,r=m.install()
        self.assertEqual((ok,error,r[1],r[7]),(1,0,8,1))
        self.assertEqual(set(m.keys),{CLIENT,MATCH,BOOTSTRAP})
        for key in (MATCH,BOOTSTRAP):self.assertEqual(m.keys[key]['DLL'],(PATH+'\0').encode('utf-16le'))
        self.assertEqual(m.install()[0],1)
        self.assertEqual(m.call('USBUnInstallDriver'),1);self.assertEqual(m.keys,{})
    def test_normal_api_success_and_denial_do_not_fallback(self):
        for mode,expected in [('normal',1),('denied',0)]:
            m=Machine(mode);ok,error,r=m.install()
            self.assertEqual(ok,expected);self.assertEqual(m.keys,{})
            self.assertEqual(r[7],0);self.assertEqual(error,0 if expected else 5)
    def test_each_failure_reports_stage_and_preserves_error(self):
        for failure,stage,error in [('load',1,126),('exports',2,127),('client',4,5),('match',5,5),('set',6,5),('verify',7,13)]:
            with self.subTest(failure=failure):
                m=Machine(failure=failure);ok,e,r=m.install()
                self.assertEqual((ok,e,r[1],r[2]),(0,error,stage,error))

if __name__=='__main__':unittest.main()
