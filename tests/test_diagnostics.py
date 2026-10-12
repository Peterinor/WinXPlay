"""Execute compiled WinCE ARM code. API substitutes are not a vehicle test."""
from pathlib import Path
import os
import json
import re
import struct
import unittest
import sys
from arm_support import Arm, pefile, FACTORY
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from diagnostic_catalog import CATALOG, binary
BASIC={name:t for name,t in CATALOG.items() if t['category']=='basic'}
SELECTED=os.environ.get('MRN_DIAGNOSTIC_TEST_TOOL')
TOOLS=[SELECTED] if SELECTED else list(BASIC)
def executable(tool):
    return binary(tool)


class DiagnosticMachine(Arm):
    def __init__(self, tool, config=None, path=None, write_fail=False, config_error=2, flush_fail=False, capacity_fail=False, stop_at_log=False):
        super().__init__(executable(tool))
        self.tool=tool;self.capacity_fail=capacity_fail
        self.name=Path(CATALOG[tool]['executable']).stem
        self.config_error=config_error;self.error=0;self.flush_fail=flush_fail
        path=path or '\\Hard Disk\\AutoLaunch\\'+CATALOG[tool]['executable']
        self.directory=path.rsplit('\\',1)[0]+'\\'
        self.stop_at_log=stop_at_log;self.opened_paths=[]
        core=pefile.PE(str(FACTORY))
        by_ordinal={s.ordinal:s.name for s in core.DIRECTORY_ENTRY_EXPORT.symbols}
        self.u.mem_map(0x30000000,0x10000)
        # SDK GetCurrentProcessId is an inline read of CE's shared KData page.
        self.u.mem_map(0xffffc000,0x1000)
        self.u.mem_write(0xffffc80c,struct.pack('<I',100))
        self.hooks={}
        for desc in self.pe.DIRECTORY_ENTRY_IMPORT:
            for imp in desc.imports:
                stub=0x30000000+4*len(self.hooks)
                self.hooks[stub]=(imp.name or by_ordinal[imp.ordinal]).decode()
                self.u.mem_write(imp.address,struct.pack('<I',stub))
        self.config=config;self.path=path;self.write_fail=write_fail
        self.calls=[];self.log=bytearray();self.tick=6589;self.exited=None
        self.u.hook_add(UC_HOOK_CODE,self.hook)

    def string(self,ptr):
        out=bytearray()
        while bytes(self.u.mem_read(ptr,2))!=b'\0\0':
            out+=self.u.mem_read(ptr,2);ptr+=2
        return out.decode('utf-16le')

    def hook(self,u,addr,size,data):
        name=self.hooks.get(addr)
        if name is None:return
        regs=[u.reg_read(r) for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3]]
        a,b,c,d=regs
        stack=u.reg_read(UC_ARM_REG_SP)
        def arg(i):return regs[i] if i<4 else struct.unpack('<I',u.mem_read(stack+4*(i-4),4))[0]
        self.calls.append((name,tuple(regs)))
        result=1
        if name=='GetTickCount':result=self.tick
        elif name=='Sleep':self.tick+=a
        elif name=='GetLastError':result=self.error
        elif name=='CreateMutexW':result=40
        elif name=='CreateThread':result=41  # Guard scheduling is outside this test.
        elif name=='GetModuleFileNameW':self.wide(b,self.path);result=len(self.path)
        elif name=='GetCommandLineW':
            result=0x20010000;self.wide(result,'1 2 3 4 1 5000' if self.tool=='window-restore-helper' else '1 2 3 4 1')
        elif name in ('wcsicmp','_wcsicmp','lstrcmpiW'):result=int(self.string(a).lower()!=self.string(b).lower())
        elif name in ('wcslen','lstrlenW'):result=len(self.string(a))
        elif name=='CreateFileW':
            p=self.string(a)
            self.opened_paths.append(p)
            if self.stop_at_log:
                assert p.startswith(self.directory) and p.endswith('.txt'),p
                self.exited=0;u.emu_stop();return
            if p.endswith('.ini'):
                assert p==self.directory+self.name+'.ini'
                result=43 if self.config is not None else 0xffffffff
                self.error=0 if self.config is not None else self.config_error
            else:
                assert p==self.directory+self.name+'_0001.txt',p
                assert arg(4)==1  # CREATE_NEW, no log overwrites.
                result=42
        elif name=='GetFileSize':u.mem_write(b,struct.pack('<I',0));result=len(self.config)
        elif name=='ReadFile':u.mem_write(b,self.config);u.mem_write(d,struct.pack('<I',len(self.config)))
        elif name=='WriteFile':
            if not self.write_fail:self.log.extend(u.mem_read(b,c))
            u.mem_write(d,struct.pack('<I',0 if self.write_fail else c));result=int(not self.write_fail)
        elif name=='wsprintfW':
            fmt=self.string(b);index=2
            def replace(m):
                nonlocal index
                value=arg(index);index+=1
                if m.group(0)=='%s':return self.string(value)
                return f'{value:04d}' if m.group(0)=='%04lu' else str(value)
            formatted=re.sub(r'%04lu|%lu|%s',replace,fmt)
            self.wide(a,formatted);result=len(formatted)
        elif name=='FlushFileBuffers':result=int(not self.flush_fail)
        elif name in ('CloseHandle','SetForegroundWindow','UpdateWindow','SetTimer','RegisterClassW'):pass
        elif name in ('GetCurrentProcess','GetCurrentProcessId','GetModuleHandleW'):result=100
        elif name=='TerminateProcess':
            assert a==0x42,'CE SDK current-process pseudohandle only'
            self.exited=b;u.emu_stop();return
        elif name=='GetForegroundWindow':result=0
        elif name=='GetVersionExW':u.mem_write(a+4,struct.pack('<III',7,0,1))
        elif name=='GetSystemInfo':pass
        elif name=='GlobalMemoryStatus':pass
        elif name=='memset':u.mem_write(a,bytes([b&255])*c);result=a
        elif name=='GetSystemMetrics':result=800
        elif name=='LoadLibraryW':result=0  # Optional Toolhelp unavailable.
        elif name=='FindFirstFileW':
            self.volume_index=0
            u.mem_write(b,bytes(592));u.mem_write(b,struct.pack('<I',0x10));self.wide(b+44,'Windows');result=81
        elif name=='FindNextFileW':
            self.volume_index+=1
            if self.volume_index==1:
                u.mem_write(b,bytes(592));u.mem_write(b,struct.pack('<I',0x110));self.wide(b+44,'Storage Card')
            else:self.error=18;result=0
        elif name=='FindClose':pass
        elif name=='GetDiskFreeSpaceExW':
            for ptr in (b,c,d):u.mem_write(ptr,struct.pack('<Q',1024*1024))
            result=int(not self.capacity_fail)
        elif name=='CreateWindowExW':result=55
        elif name=='GetMessageW':result=0
        else:raise AssertionError(f'Unhandled API {name}')
        u.reg_write(UC_ARM_REG_R0,result)
        u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))

    def run(self,instruction_limit=1000000):
        self.u.reg_write(UC_ARM_REG_SP,0x2001f000)
        self.u.emu_start(self.pe.OPTIONAL_HEADER.ImageBase+self.pe.OPTIONAL_HEADER.AddressOfEntryPoint,0,count=instruction_limit)
        assert self.exited is not None,'Diagnostic did not exit'
        return self.exited,self.log.decode('utf-16le')


class DiagnosticsTests(unittest.TestCase):
    def test_production_startup_reads_config_and_writes_log_beside_executable(self):
        for tool in TOOLS:
            with self.subTest(tool=tool):
                config=b'[diagnostic]\ndelay_ms=0\nduration_ms=1000\n'
                machine=DiagnosticMachine(tool,config=config,path='\\Hard Disk\\AUTOLAUNCH_PROD\\'+CATALOG[tool]['executable'])
                code,log=machine.run()
                self.assertEqual(code,0)
                self.assertIn('DIAGNOSTIC_COMPLETE',log)
                self.assertTrue(machine.opened_paths)
                self.assertTrue(all(p.startswith('\\Hard Disk\\AUTOLAUNCH_PROD\\') for p in machine.opened_paths))

    def parse(self,data,tool=None):
        arm=Arm(executable(tool or TOOLS[0]))
        arm.u.mem_write(0x20001000,data or b'\0')
        arm.u.mem_write(0x20003000,struct.pack('<III',10000,20000,0))
        result=arm.call('ParseDiagnosticOptions',0x20001000,len(data),0x20003000,int((tool or TOOLS[0])=='window-observer'))
        out=struct.unpack('<III',arm.u.mem_read(0x20003000,12))
        if not result:self.assertEqual(out,(10000,20000,0))
        return result,out

    def test_parameter_bounds_and_no_runtime_tool_selector(self):
        for tool in TOOLS:
            self.assertEqual(self.parse(b'[diagnostic]\ndelay_ms=0\nduration_ms=1000',tool),(1,(0,1000,0)))
            self.assertEqual(self.parse(b'\xef\xbb\xbf[diagnostic]\ndelay_ms=30000\nduration_ms=60000',tool),(1,(30000,60000,0)))
        good=b'[diagnostic]\n'
        for data in [b'',b'[diagnostics]',b'delay_ms=0',good+b'case=system_info',
                     good+b'tool=system-information',good+b'delay_ms=-1',good+b'delay_ms=30001',
                     good+b'duration_ms=999',good+b'duration_ms=60001',good+b'duration_ms=42949672960',
                     good+b'delay_ms=0\ndelay_ms=1',good+b'[diagnostic]',good+b'unknown=1',
                     good+b'\0',good+b'#\xff',good+b'#'+b'a'*128]:
            self.assertEqual(self.parse(data)[0],0,data)
        maximum=good+b'\n'*(1024-len(good))
        self.assertEqual(self.parse(maximum)[0],1)
        self.assertEqual(self.parse(maximum+b'\n')[0],0)

    def test_each_executable_has_only_its_fixed_behavior(self):
        markers={'usb-startup-check':'USB_ENTRY_REACHED','system-information':'OS_MAJOR_MINOR_BUILD',
                 'window-observer':'PASSIVE_WINDOW_OBSERVATION_NO_TEST_WINDOW'}
        for tool in TOOLS:
            machine=DiagnosticMachine(tool,b'[diagnostic]\ndelay_ms=0\nduration_ms=1000')
            code,log=machine.run();self.assertEqual(code,0,log)
            self.assertIn(markers[tool],log);self.assertIn(machine.name,log)
            self.assertIn('DIAGNOSTIC_COMPLETE',log)
            names={n for n,_ in machine.calls}
            self.assertNotIn('CreateWindowExW',names)
            self.assertEqual('GetDiskFreeSpaceExW' in names,tool=='system-information')
            self.assertEqual('LoadLibraryW' in names,tool=='system-information')
            if tool=='usb-startup-check':
                self.assertNotIn('ReadFile',names);self.assertNotIn('Sleep',names)
                self.assertEqual(sum(n=='CreateFileW' for n,_ in machine.calls),1)

    @unittest.skipIf(SELECTED not in (None,'window-observer'),'different selected tool')
    def test_window_visibility_is_explicit_and_invalid_values_do_not_run(self):
        for value in (0,1):
            config=f'[diagnostic]\ndelay_ms=0\nduration_ms=1000\nshow_test_window={value}'.encode()
            machine=DiagnosticMachine('window-observer',config)
            code,log=machine.run();self.assertEqual(code,0,log)
            self.assertEqual('CreateWindowExW' in {n for n,_ in machine.calls},bool(value))
            self.assertIn('ONLY_MESSAGES_DELIVERED' if value else 'PASSIVE_WINDOW_OBSERVATION',log)
        for value in ('2','-1','true','1\nshow_test_window=0'):
            machine=DiagnosticMachine('window-observer',f'[diagnostic]\nshow_test_window={value}'.encode())
            self.assertEqual(machine.run()[0],1)
            self.assertNotIn('CreateWindowExW',{n for n,_ in machine.calls})

    @unittest.skipIf(SELECTED not in (None,'system-information'),'different selected tool')
    def test_system_summary_includes_storage_and_preserves_partial_failure(self):
        machine=DiagnosticMachine('system-information',capacity_fail=True)
        code,log=machine.run();self.assertEqual(code,1)
        self.assertIn('OS_MAJOR_MINOR_BUILD',log)
        self.assertIn('SYSTEM_INFORMATION_COLLECTED',log)
        self.assertIn('VOLUME_QUERY_ERROR',log)
        self.assertIn('DIAGNOSTIC_FAILED',log)
        config=b'[diagnostic]\nshow_test_window=1'
        self.assertEqual(self.parse(config,'system-information')[0],0)

    def test_optional_missing_parameters_use_defaults(self):
        for tool in TOOLS:
            code,log=DiagnosticMachine(tool).run();self.assertEqual(code,0,log)
            if BASIC[tool]['options']:
                self.assertIn('OPTIONS_ABSENT_USING_DEFAULTS',log)
                self.assertIn(f"DELAY_DURATION a={BASIC[tool]['delay_ms']} b=20000",log)

    def test_invalid_unreadable_configuration_and_failed_log_prevent_work(self):
        work={'CreateWindowExW','GetDiskFreeSpaceExW','LoadLibraryW','GetForegroundWindow'}
        for tool in TOOLS:
            scenarios=[dict(write_fail=True),dict(flush_fail=True)]
            if BASIC[tool]['options']:
                scenarios += [dict(config=b'[diagnostic]\ncase=input_window'),dict(config_error=5)]
            for kwargs in scenarios:
                machine=DiagnosticMachine(tool,**kwargs);code,log=machine.run()
                self.assertEqual(code,1,(tool,kwargs,log))
                self.assertNotIn('DIAGNOSTIC_COMPLETE',log)
                self.assertFalse(work & {n for n,_ in machine.calls})

    def test_runs_from_other_writable_locations(self):
        for tool in TOOLS:
            machine=DiagnosticMachine(tool,path='\\Windows\\'+BASIC[tool]['executable'])
            self.assertEqual(machine.run()[0],0)
            self.assertTrue(all(p.startswith('\\Windows\\') for p in machine.opened_paths))

    def test_factory_imports_and_no_foreign_process_operations(self):
        core=pefile.PE(str(FACTORY));by_ord={s.ordinal:s.name for s in core.DIRECTORY_ENTRY_EXPORT.symbols}
        names=set(by_ord.values())
        for tool in TOOLS:
            resolved=set();pe=pefile.PE(str(executable(tool)))
            for desc in pe.DIRECTORY_ENTRY_IMPORT:
                self.assertEqual(desc.dll.lower(),b'coredll.dll')
                for imp in desc.imports:
                    name=imp.name or by_ord[imp.ordinal];self.assertIn(name,names);resolved.add(name)
            self.assertFalse({b'OpenProcess',b'CreateProcessW',b'ShowWindow',b'DeviceIoControl',
                              b'RegSetValueExW',b'GetWindowTextW',b'CopyFileW',b'MoveFileW',b'DeleteFileW'}&resolved)
            self.assertEqual(b'CreateWindowExW' in resolved,tool=='window-observer')
            self.assertEqual(b'GetDiskFreeSpaceExW' in resolved,tool=='system-information')


if __name__=='__main__':unittest.main()
