"""Exercise compiled ARM DELETE/MOVE with an in-memory filesystem, not a vehicle."""
import struct
import re
import unittest
import test_file_ops_policy as base
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP


class TransferMachine(base.Arm):
    def __init__(self, files, failure=None):
        super().__init__(base.OPS)
        self.files=dict(files);self.failure=failure;self.calls=[];self.handles={};self.error=0
        self.hooks={};self.deleted=False
        core=base.pefile.PE(str(base.FACTORY));names={s.ordinal:s.name for s in core.DIRECTORY_ENTRY_EXPORT.symbols}
        for module in self.pe.DIRECTORY_ENTRY_IMPORT:
            for imp in module.imports:
                address=0x20018000+4*len(self.hooks)
                self.hooks[address]=(imp.name or names[imp.ordinal]).decode()
                self.u.mem_write(imp.address,struct.pack('<I',address))
        self.u.hook_add(UC_HOOK_CODE,self.hook)

    def text(self,p):
        out=bytearray()
        while bytes(self.u.mem_read(p,2))!=b'\0\0':out+=self.u.mem_read(p,2);p+=2
        return out.decode('utf-16le')

    def hook(self,u,address,size,data):
        name=self.hooks.get(address)
        if name is None:return
        a,b,c,d=[u.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]
        self.calls.append(name);result=1
        if name=='GetFileAttributesW':
            path=self.text(a)
            if self.deleted and path=='source' and self.failure=='postcheck':result=0xffffffff;self.error=5
            elif path not in self.files:result=0xffffffff;self.error=2
            else:result=16 if self.files[path] is None else 32
        elif name=='GetLastError':result=self.error
        elif name=='CopyFileW':
            source,target=self.text(a),self.text(b)
            assert c==1,'Destination must never be overwritten'
            result=int(target not in self.files and self.failure!='copy')
            if result:self.files[target]=self.files[source] if self.failure!='mismatch' else b'different'
        elif name=='CreateFileW':
            path=self.text(a)
            if self.failure=='open' or path not in self.files:result=0xffffffff
            else:
                result=100+len(self.handles);self.handles[result]=[path,0]
        elif name=='GetFileSize':
            u.mem_write(b,struct.pack('<I',0));result=len(self.files[self.handles[a][0]])
        elif name=='ReadFile':
            path,offset=self.handles[a];chunk=self.files[path][offset:offset+c]
            # Inject payload-read failure after configuration has been loaded.
            if self.failure=='read' and not path.lower().endswith('\\fileops.txt'):result=0;chunk=b''
            u.mem_write(b,chunk or b'\0');u.mem_write(d,struct.pack('<I',len(chunk)))
            self.handles[a][1]+=len(chunk)
        elif name=='CloseHandle':result=int(self.failure!='close')
        elif name=='DeleteFileW':
            path=self.text(a);result=int(self.failure!='delete')
            if result:del self.files[path];self.deleted=True
        else:raise AssertionError('Unexpected API '+name)
        u.reg_write(UC_ARM_REG_R0,result);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))

    def transfer(self):
        self.wide(0x20001000,'source');self.wide(0x20002000,'destination')
        return self.call('TransferThenRemove',0x20001000,0x20002000)


class SequenceMachine(TransferMachine):
    """Run the real executable entry, including parsing and dispatch, with CE API substitutes."""
    def __init__(self, config, files, failure=None, folder='AutoLaunch'):
        self.folder=folder
        initial={r'\Windows':None,r'\NAND_USER':None,r'\Hard Disk':None,
                 '\\Hard Disk\\'+folder:None,'\\Hard Disk\\'+folder+'\\fileops.txt':config}
        initial.update(files)
        super().__init__(initial,failure)
        self.tick=1000;self.events=[];self.exit_code=None;self.mutex=800
        self.live_mutexes={};self.closed_handles=[]
        self.waits=[]
        self.u.mem_map(0xffffc000,0x1000);self.u.mem_write(0xffffc80c,struct.pack('<I',100))

    def hook(self,u,address,size,data):
        name=self.hooks.get(address)
        if name is None:return
        a,b,c,d=[u.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]
        def arg(i):return (a,b,c,d)[i] if i<4 else struct.unpack('<I',u.mem_read(u.reg_read(UC_ARM_REG_SP)+4*(i-4),4))[0]
        result=1;self.calls.append(name)
        if name=='GetTickCount':result=self.tick
        elif name=='Sleep':self.tick+=a
        elif name=='CreateMutexW':
            self.mutex+=1;result=self.mutex;self.error=0;self.live_mutexes[result]=self.text(c)
            if self.failure=='legacy_guard' and self.text(c)=='MRN_BAND_TEST_SINGLETON_V1':self.error=183
        elif name=='CloseHandle':
            self.closed_handles.append(a);self.live_mutexes.pop(a,None);result=int(self.failure!='close')
        elif name=='CreateProcessW':
            path=self.text(a)
            assert not any(n in self.live_mutexes.values() for n in ('MRN_BAND_TEST_SINGLETON_V1','MRN_B_DISPLAY_RECOVERY_V1'))
            assert all(arg(i)==0 for i in range(1,9))
            self.events.append(('RUN',path,None,self.tick))
            result=int(self.failure!='launch')
            if result:u.mem_write(arg(9),struct.pack('<IIII',701,702,703,704))
            else:self.error=193
        elif name=='CreateThread':result=77  # UI/guard scheduling is not simulated here.
        elif name in ('InitializeCriticalSection','EnterCriticalSection','LeaveCriticalSection'):pass
        elif name=='WaitForSingleObject':
            assert a==77 and b==7000
            self.waits.append((a,b))
            self.tick+=b;result=258
        elif name=='GetModuleFileNameW':
            path='\\Hard Disk\\'+self.folder+'\\file_ops.exe';self.wide(b,path);result=len(path)
        elif name in ('GetCurrentProcess','GetCurrentProcessId'):result=0x42 if name=='GetCurrentProcess' else 100
        elif name=='TerminateProcess':
            assert a==0x42;self.exit_code=b;u.emu_stop();return
        elif name=='LoadLibraryW':result=333
        elif name in ('GetProcAddress','GetProcAddressW'):
            result=0x20019000+4*len(self.hooks);self.hooks[result]=self.text(b)
        elif name=='CreateToolhelp32Snapshot':result=901
        elif name=='Process32First':self.wide(b+36,'idle.exe')
        elif name=='Process32Next':result=0;self.error=18
        elif name in ('CloseToolhelp32Snapshot','FreeLibrary','FlushFileBuffers'):pass
        elif name in ('lstrlenW','wcslen'):result=len(self.text(a))
        elif name in ('lstrcpyW','wcscpy'):self.wide(a,self.text(b));result=a
        elif name=='memset':u.mem_write(a,bytes([b&255])*c);result=a
        elif name=='wsprintfW':
            fmt=self.text(b);index=2
            def substitute(match):
                nonlocal index
                value=arg(index);index+=1
                return self.text(value) if match[0]=='%s' else (f'{value:04d}' if match[0]=='%04lu' else str(value))
            value=re.sub(r'%04lu|%lu|%s',substitute,fmt);self.wide(a,value);result=len(value)
        elif name=='CreateDirectoryW':
            path=self.text(a);result=int(path not in self.files)
            if result:self.files[path]=None
            else:self.error=183
        elif name=='CreateFileW':
            path=self.text(a);disposition=arg(4)
            if disposition==1 and path not in self.files:self.files[path]=b''
            elif disposition==1 or path not in self.files:self.error=2;result=0xffffffff
            if result!=0xffffffff:
                result=100+len(self.handles);self.handles[result]=[path,0]
        elif name=='WriteFile':
            path,offset=self.handles[a];blob=bytes(u.mem_read(b,c));self.files[path]+=blob
            self.handles[a][1]+=c;u.mem_write(d,struct.pack('<I',c))
        elif name=='CopyFileW':
            source,target=self.text(a),self.text(b);assert c in (0,1)
            self.events.append(('COPY',source,target,self.tick))
            result=int(source in self.files and self.files[source] is not None and
                       (target not in self.files or (not c and self.files[target] is not None)) and self.failure!='copy')
            if result:self.files[target]=self.files[source]
            else:self.error=2 if source not in self.files else 80
        elif name=='MoveFileW':
            source,target=self.text(a),self.text(b);self.events.append(('RENAME',source,target,self.tick))
            result=int(source in self.files and target not in self.files)
            if result:self.files[target]=self.files.pop(source)
        elif name=='DeleteFileW':
            path=self.text(a);self.events.append(('DELETE',path,None,self.tick));result=int(path in self.files and self.failure!='delete')
            if result:del self.files[path]
            else:self.error=5
        else:
            self.calls.pop();return super().hook(u,address,size,data)
        u.reg_write(UC_ARM_REG_R0,result);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))

    def run(self):
        self.u.reg_write(UC_ARM_REG_SP,0x2001f000)
        self.u.emu_start(self.pe.OPTIONAL_HEADER.ImageBase+self.pe.OPTIONAL_HEADER.AddressOfEntryPoint,0,count=30000000)
        assert self.exit_code is not None,'Entry did not exit'
        return self.exit_code


class ProgressMachine(SequenceMachine):
    """Exercise compiled window callbacks; GDI/CE APIs are substitutes, not a screen test."""
    def __init__(self, config, files, failure=None):
        super().__init__(config,files,failure)
        self.ui_entry=None;self.windows=[];self.screen_text=[];self.edit_text='';self.destroyed=[]

    def hook(self,u,address,size,data):
        name=self.hooks.get(address)
        if name is None:return
        a,b,c,d=[u.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]
        result=1
        if name=='CreateThread':
            if self.ui_entry is None:
                self.ui_entry=c
                if self.failure=='ui_thread':
                    u.reg_write(UC_ARM_REG_R0,0);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR));return
            return super().hook(u,address,size,data)
        if name=='GetModuleHandleW':result=self.pe.OPTIONAL_HEADER.ImageBase
        elif name=='RegisterClassW':self.exports['WindowProc']=struct.unpack('<I',u.mem_read(a+4,4))[0]
        elif name=='GetSystemMetrics':result=800 if a==0 else 480
        elif name=='CreateWindowExW':
            self.windows.append(self.text(b));result=500+len(self.windows)
        elif name=='CreateFontIndirectW':result=0  # Exercise stock font fallback.
        elif name=='GetMessageW':result=0
        elif name in ('ShowWindow','SetForegroundWindow','UpdateWindow','SetTimer','KillTimer',
                      'PostQuitMessage','EnableWindow','InvalidateRect','SendMessageW',
                      'FillRect','SetBkMode','SetTextColor','SelectObject','EndPaint','DefWindowProcW'):pass
        elif name=='SetWindowTextW':self.edit_text=self.text(b)
        elif name=='BeginPaint':result=700
        elif name=='GetClientRect':u.mem_write(b,struct.pack('<iiii',0,0,800,480))
        elif name=='GetStockObject':result=701
        elif name=='DrawTextW':self.screen_text.append(self.text(b))
        elif name=='DestroyWindow':self.destroyed.append(a)
        else:return super().hook(u,address,size,data)
        u.reg_write(UC_ARM_REG_R0,result);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))

    def display(self):
        self.exports['UiThread']=self.ui_entry
        self.call('UiThread',0)
        self.call('WindowProc',501,0x113,1,0)  # WM_TIMER refreshes the accumulated log.
        self.call('WindowProc',501,0xF,0,0)  # WM_PAINT renders status/paths.


class FileManagerTests(unittest.TestCase):
    def test_expanded_job_boundary(self):
        base.Tests().test_expanded_job_limit_and_overflow_guard()

    def test_large_recipe_executes_in_order_and_rejects_late_error_before_copy(self):
        source=r'\Windows\cmd.exe'
        lines=[f'COPY|{source}|copy-{i}.exe'.encode() for i in range(100)]
        # Cross both old limits: 100 tasks plus over 8 KiB of valid comments.
        config=b'FILEOPS_V2\n'+(b'#'+b'x'*450+b'\n')*20+b'\n'.join(lines)+b'\n'
        self.assertGreater(len(config),8192)
        m=SequenceMachine(config,{source:b'exe'})
        self.assertEqual(m.run(),0)
        self.assertEqual([e[2] for e in m.events],
                         [r'\Hard Disk\AutoLaunch\FILEOPS_0001\copy-'+str(i)+'.exe' for i in range(100)])
        bad=SequenceMachine(config+b'UNKNOWN|invalid\n',{source:b'exe'})
        self.assertEqual(bad.run(),18)
        self.assertEqual(bad.events,[])

    def test_config_byte_limit_checked_before_jobs(self):
        source=r'\Windows\cmd.exe'
        head=b'FILEOPS_V2\nCOPY|\\Windows\\cmd.exe|cmd.exe\n'
        remaining=512*1024-len(head)
        comments=(b'#'+b'x'*477+b'\n')*(remaining//479)
        # Empty lines pad the exact byte boundary without any oversized line.
        config=head+comments+b'\n'*(remaining-len(comments))
        self.assertEqual(len(config),524288)
        m=SequenceMachine(config,{source:b'exe'})
        self.assertEqual(m.run(),0)
        self.assertEqual(len(m.events),1)
        bad=SequenceMachine(config+b'\n',{source:b'exe'})
        self.assertEqual(bad.run(),18)
        self.assertEqual(bad.events,[])
        log=bad.files[r'\Hard Disk\AutoLaunch\FILEOPS_0001\RESULT.txt'].decode('utf-16le')
        self.assertIn('CONFIG_TOO_LARGE_NO_INTERNAL_WRITES',log)

    def test_progress_window_shows_success_failure_and_latest_log(self):
        config=b'MRN_FILEOPS_V2\nCOPY|\\Windows\\cmd.exe|cmd.exe\n'
        for failure,code,marker in [(None,0,'ALL_JOBS_COMPLETE'),('copy',23,'JOB_FAILED_CONTINUE_INDEX_OPERATION')]:
            m=ProgressMachine(config,{r'\Windows\cmd.exe':b'exe'},failure)
            self.assertEqual(m.run(),code)
            m.tick-=7000  # Replay the UI at completion, before the close deadline.
            m.display()
            self.assertEqual(m.windows,['MRN_FILEOPS_PROGRESS_V25','EDIT','BUTTON'])
            self.assertIn(marker,m.edit_text)
            self.assertTrue(any(('ALL COMPLETE' if code==0 else 'FINISHED WITH ERRORS') in t for t in m.screen_text))
            self.assertIn(r'\Windows\cmd.exe',m.screen_text)
            m.call('WindowProc',501,0x111,100,0)
            self.assertEqual(m.destroyed,[501])

    def test_finished_window_hides_and_destroys_at_deadline(self):
        m=ProgressMachine(b'FILEOPS_V2\nCOPY|\\Windows\\cmd.exe|cmd.exe\n',
                          {r'\Windows\cmd.exe':b'exe'})
        self.assertEqual(m.run(),0)
        m.tick-=7000
        m.display()
        self.assertEqual(m.destroyed,[])
        m.tick+=4999
        m.call('WindowProc',501,0x113,1,0)
        self.assertEqual(m.destroyed,[])
        m.tick+=1
        m.call('WindowProc',501,0x113,1,0)
        self.assertEqual(m.destroyed,[501])
        log=m.files[r'\Hard Disk\AutoLaunch\FILEOPS_0001\RESULT.txt'].decode('utf-16le')
        # This harness does not schedule the UI during the entry execution;
        # the timeout must be reported honestly, never as successful cleanup.
        self.assertIn('UI_THREAD_EXIT_NOT_CONFIRMED_FALLBACK',log)
        self.assertIn('UI_WM_DESTROY_NOT_CONFIRMED',log)
        self.assertIn('PROCESS_EXIT_REQUESTED',log)

    def test_ui_thread_failure_does_not_prevent_copy(self):
        m=ProgressMachine(b'MRN_FILEOPS_V2\nCOPY|\\Windows\\cmd.exe|cmd.exe\n',
                          {r'\Windows\cmd.exe':b'exe'},'ui_thread')
        self.assertEqual(m.run(),0)
        self.assertEqual(m.waits,[])
        self.assertEqual(m.files[r'\Hard Disk\AutoLaunch\FILEOPS_0001\cmd.exe'],b'exe')
        self.assertIn('UI_THREAD_FAILED_FILE_LOG_ONLY',m.files[r'\Hard Disk\AutoLaunch\FILEOPS_0001\RESULT.txt'].decode('utf-16le'))

    def test_portable_tool_and_configuration_are_protected(self):
        folder=r'\Hard Disk\Tools'
        for leaf in ('file_ops.exe','fileops.txt'):
            config=('FILEOPS_V2\nDELETE|'+folder+'\\'+leaf+'\n').encode()
            m=SequenceMachine(config,{},folder='Tools')
            self.assertEqual(m.run(),18)
            self.assertFalse(m.events)
            self.assertIn('CONFIG_REJECTED_NO_INTERNAL_WRITES',m.files[folder+r'\FILEOPS_0001\RESULT.txt'].decode('utf-16le'))

    def test_production_entry_exports_and_replaces_in_configuration_order(self):
        folder=r'\Hard Disk\AUTOLAUNCH_PROD'
        config=('MRN_FILEOPS_V2\nMOVE|\\NAND_USER\\old.exe|\\NAND_USER\\backup.exe\n'
                'COPY|'+folder+'\\payload\\new.exe|\\NAND_USER\\old.exe\n'
                'COPY|\\NAND_USER\\backup.exe|backup.exe\n').encode('ascii')
        machine=SequenceMachine(config,{r'\NAND_USER\old.exe':b'old',folder+r'\payload\new.exe':b'new'},folder='AUTOLAUNCH_PROD')
        self.assertEqual(machine.run(),0)
        self.assertEqual(machine.files[r'\NAND_USER\old.exe'],b'new')
        self.assertEqual(machine.files[folder+r'\FILEOPS_0001\backup.exe'],b'old')
        self.assertIn('ALL_JOBS_COMPLETE',machine.files[folder+r'\FILEOPS_0001\RESULT.txt'].decode('utf-16le'))
        self.assertFalse(any('\\AutoLaunch\\' in p for p in machine.files))
        portable=SequenceMachine(b'WINCE_FILEOPS_V2\nCOPY|\\Windows\\cmd.exe|cmd.exe\n',{r'\Windows\cmd.exe':b'exe'},folder='Tools')
        self.assertEqual(portable.run(),0)
        self.assertEqual(portable.files[r'\Hard Disk\Tools\FILEOPS_0001\cmd.exe'],b'exe')

    def test_production_control_files_protected_and_legacy_install_supported(self):
        folder=b'\\Hard Disk\\AUTOLAUNCH_PROD\\'
        for leaf in (b'file_ops.exe',b'fileops.txt',b'AutoLaunch_prod.txt'):
            self.assertEqual(self.parse([b'DELETE|'+folder+leaf])[0],0)
        self.assertEqual(self.parse([b'INSTALL|'+folder+br'payload\new.exe|\NAND_USER\MRN_TEST\app.exe'],b'MRN_FILEOPS_V1'),(1,1))

    def test_existing_copy_install_watch_policy_and_rollback(self):
        for name in ['test_paths_reject_escape_and_system_writes',
                     'test_config_validation_before_any_job_execution',
                     'test_rename_commit_and_rollback_failures']:
            getattr(base.Tests(),name)()

    def test_file_manager_imports_exist_in_factory_coredll(self):
        core=base.pefile.PE(str(base.FACTORY));names={s.ordinal:s.name for s in core.DIRECTORY_ENTRY_EXPORT.symbols}
        known=set(names.values());resolved=set()
        for module in base.pefile.PE(str(base.OPS)).DIRECTORY_ENTRY_IMPORT:
            self.assertEqual(module.dll.lower(),b'coredll.dll')
            for imp in module.imports:
                name=imp.name or names[imp.ordinal];self.assertIn(name,known);resolved.add(name)
        self.assertIn(b'DeleteFileW',resolved)
        # CE's ExitProcess inlines TerminateProcess on the current-process handle.
        self.assertFalse({b'RemoveDirectoryW',b'DeviceIoControl',b'RegSetValueExW',b'OpenProcess'}&resolved)

    def parse(self,lines,header=b'MRN_FILEOPS_V2'):
        return base.Tests().parse(header+b'\n'+b'\n'.join(lines))

    def test_delete_and_move_configuration_and_v1_compatibility(self):
        delete=br'DELETE|\NAND_USER\MRN_TEST\old.log'
        move=br'MOVE|\Hard Disk\AutoLaunch\payload\a.bin|\NAND_USER\MRN_TEST\b.bin'
        export=br'COPY|\Windows\cmd.exe|cmd.exe'
        self.assertEqual(self.parse([delete,move,export]),(1,3))
        self.assertEqual(self.parse([export],b'MRN_FILEOPS_V1'),(1,1))
        for bad in [[delete+b'|'],[delete+b'|target'],[move+b'|extra'],[b'DELETE|'],
                    [b'MOVE|source']]:
            self.assertEqual(self.parse(bad)[0],0,bad)
        self.assertEqual(self.parse([export,delete]),(1,2))
        self.assertEqual(self.parse([delete,delete]),(1,2))  # Existence is checked when reached.
        for new in [delete,move]:self.assertEqual(self.parse([new],b'MRN_FILEOPS_V1')[0],0)

    def test_mutation_paths_allow_system_files_but_protect_own_control_files(self):
        good=br'\NAND_USER\MRN_TEST\a.bin'
        for path in [br'\Hard Disk\AutoLaunch\file_ops.exe',br'\Hard Disk\AutoLaunch\fileops.txt',
                     br'\Hard Disk\AutoLaunch\Autolaunch.txt',
                     br'\NAND_USER\MRN_TEST\..\a.bin',br'\NAND_USER\MRN_TEST\*.bin',
                     br'\NAND_USER\MRN_TEST\a.bin.',br'\NAND_USER\MRN_TEST\a.bin:stream']:
            for line in [b'DELETE|'+path,b'MOVE|'+path+b'|'+good,b'MOVE|'+good+b'|'+path]:
                self.assertEqual(self.parse([line])[0],0,line)
        self.assertEqual(self.parse([b'MOVE|'+good+b'|'+good.lower()])[0],0)
        self.assertEqual(self.parse([b'MOVE|'+good+br'|\Hard Disk\AutoLaunch\payload\saved.bin']),(1,1))
        for path in [br'\Windows\a.bin',br'\NAND_RES\a.bin',br'\NAND_USER\other\a.bin',br'\Storage Card\a.bin']:
            for line in [b'DELETE|'+path,b'MOVE|'+path+b'|'+good,b'MOVE|'+good+b'|'+path,
                         b'INSTALL|'+good+b'|'+path,b'COPY|'+path+b'|export.bin']:
                self.assertEqual(self.parse([line]),(1,1),line)

    def test_active_run_protection_does_not_block_previous_runs(self):
        a=base.Arm(base.OPS);folder=r'\Hard Disk\AutoLaunch\FILEOPS_0001'
        a.wide(0x20002000,folder)
        for path,result in [(folder,0),(folder+r'\RESULT.txt',0),(folder.lower()+r'\deleted-0.bak',0),
                            (folder+'0'+r'\RESULT.txt',1),(r'\Windows\driver.dll',1),
                            (r'\Hard Disk\AutoLaunch\FILEOPS_0002\RESULT.txt',1)]:
            a.wide(0x20001000,path)
            self.assertEqual(a.call('MutationPathAllowed',0x20001000,0x20002000),result)

    def test_transfer_verifies_all_bytes_before_removing_source(self):
        for data in [b'',bytes(range(256))*9]:
            m=TransferMachine({'source':data});self.assertEqual(m.transfer(),0)
            self.assertEqual(m.files,{'destination':data})
            self.assertLess(m.calls.index('CopyFileW'),m.calls.index('DeleteFileW'))
            self.assertLess(m.calls.index('CloseHandle'),m.calls.index('DeleteFileW'))

    def test_failures_preserve_original_and_do_not_overwrite_destination(self):
        for files,failure,status in [({},None,1),({'source':None},None,1),
                ({'source':b'original','destination':b'existing'},None,2),
                ({'source':b'original','destination':None},None,2),
                ({'source':b'original'},'copy',3),({'source':b'original'},'mismatch',4),
                ({'source':b'original'},'open',4),({'source':b'original'},'read',4),
                ({'source':b'original'},'close',4),({'source':b'original'},'delete',5)]:
            with self.subTest(files=files,failure=failure):
                m=TransferMachine(files,failure);self.assertEqual(m.transfer(),status)
                if 'source' in files:self.assertEqual(m.files['source'],files['source'])
                if 'destination' in files:self.assertEqual(m.files['destination'],files['destination'])
                if status<5:self.assertNotIn('DeleteFileW',m.calls)
        m=TransferMachine({'source':b'original'},'postcheck')
        self.assertEqual(m.transfer(),6);self.assertEqual(m.files['destination'],b'original')

    def scenario(self,lines,files,failure=None):
        machine=SequenceMachine(('MRN_FILEOPS_V2\n'+'\n'.join(lines)).encode('ascii'),files,failure)
        result=machine.run()
        return machine,result

    def test_entry_move_backup_then_copy_replacement_in_exact_order(self):
        a,b,c=r'\Windows\a.dll',r'\Windows\a.dll.bak',r'\Hard Disk\c.dll'
        m,result=self.scenario([f'MOVE|{a}|{b}',f'COPY|{c}|{a}'],{a:b'old',c:b'new'})
        self.assertEqual(result,0);self.assertEqual(m.files[a],b'new');self.assertEqual(m.files[b],b'old')
        self.assertEqual([(e[0],e[1],e[2]) for e in m.events],[('COPY',a,b),('DELETE',a,None),('COPY',c,a)])

    def test_entry_defers_install_source_until_previous_move_creates_it(self):
        seed,source,target=r'\Hard Disk\seed.dll',r'\Hard Disk\prepared.dll',r'\Windows\target.dll'
        m,result=self.scenario([f'MOVE|{seed}|{source}',f'INSTALL|{source}|{target}',f'COPY|{target}|export.dll'],
                               {seed:b'new',target:b'old'})
        self.assertEqual(result,0);self.assertEqual(m.files[target],b'new')
        self.assertEqual(m.files[r'\Hard Disk\AutoLaunch\FILEOPS_0001\export.dll'],b'new')
        self.assertTrue(any(p.startswith(target+'.bak-') and data==b'old' for p,data in m.files.items()))

    def test_entry_copy_before_delete_and_repeated_target_is_valid(self):
        a,c=r'\Windows\a.dll',r'\Hard Disk\c.dll'
        m,result=self.scenario([f'COPY|{a}|saved.dll',f'DELETE|{a}',f'COPY|{c}|{a}'],{a:b'old',c:b'new'})
        self.assertEqual(result,0);self.assertEqual(m.files[a],b'new')
        self.assertEqual(m.files[r'\Hard Disk\AutoLaunch\FILEOPS_0001\saved.dll'],b'old')

    def test_entry_failed_first_or_second_step_continues_later_steps(self):
        a,b,c=r'\Windows\a.dll',r'\Windows\a.dll.bak',r'\Hard Disk\c.dll'
        lines=[f'MOVE|{a}|{b}',f'COPY|{c}|{a}',f'DELETE|{c}']
        m,result=self.scenario(lines,{a:b'old',b:b'prior-backup',c:b'new'})
        self.assertNotEqual(result,0);self.assertEqual(m.files[a],b'new');self.assertNotIn(c,m.files)
        self.assertEqual(m.files[b],b'prior-backup')
        log=m.files[r'\Hard Disk\AutoLaunch\FILEOPS_0001\RESULT.txt'].decode('utf-16le')
        self.assertIn('JOB_FAILED_CONTINUE_INDEX_OPERATION a=1 b=5',log)
        self.assertIn('FILEOPS_SUCCESS_FAILED a=2 b=1',log)
        self.assertIn('FILEOPS_FINISHED_WITH_FAILURES',log)
        self.assertNotIn('ALL_JOBS_COMPLETE',log)
        m,result=self.scenario(lines,{a:b'old'})
        self.assertNotEqual(result,0);self.assertNotIn(a,m.files);self.assertEqual(m.files[b],b'old')
        log=m.files[r'\Hard Disk\AutoLaunch\FILEOPS_0001\RESULT.txt'].decode('utf-16le')
        self.assertIn('JOB_BEGIN_INDEX_OPERATION a=3 b=4',log)
        self.assertIn('FILEOPS_SUCCESS_FAILED a=1 b=2',log)

    def test_missing_delete_and_install_do_not_block_copy_or_run(self):
        source,exe=r'\Windows\cmd.exe',r'\NAND_USER\app.exe'
        m,result=self.scenario([r'DELETE|\NAND_USER\missing.log',
                               r'INSTALL|\Hard Disk\missing.exe|\NAND_USER\missing.exe',
                               f'COPY|{source}|{exe}',f'RUN|{exe}'],{source:b'exe'})
        self.assertEqual(result,23)
        self.assertEqual(m.files[exe],b'exe')
        self.assertIn(('RUN',exe,None),[e[:3] for e in m.events])
        log=m.files[r'\Hard Disk\AutoLaunch\FILEOPS_0001\RESULT.txt'].decode('utf-16le')
        self.assertIn('FILEOPS_SUCCESS_FAILED a=2 b=2',log)
        self.assertNotIn('INSTALL_PHASE_DONE_BAND_MAY_BE_PRESSED',log)

    def test_failed_install_batch_never_reports_success_at_its_last_job(self):
        source=r'\Hard Disk\seed.exe'
        m,result=self.scenario([r'INSTALL|\Hard Disk\missing.exe|\NAND_USER\a.exe',
                               f'INSTALL|{source}|\\NAND_USER\\b.exe'],{source:b'exe'})
        self.assertEqual(result,23);self.assertEqual(m.files[r'\NAND_USER\b.exe'],b'exe')
        log=m.files[r'\Hard Disk\AutoLaunch\FILEOPS_0001\RESULT.txt'].decode('utf-16le')
        self.assertIn('INSTALL_PHASE_FINISHED_WITH_FAILURES',log)
        self.assertNotIn('INSTALL_PHASE_DONE_BAND_MAY_BE_PRESSED',log)

    def test_blocked_guard_is_released_and_each_later_mutation_rechecks_it(self):
        source=r'\Windows\cmd.exe'
        m,result=self.scenario([f'COPY|{source}|\\NAND_USER\\a.exe',
                               f'COPY|{source}|\\NAND_USER\\b.exe',f'COPY|{source}|export.exe'],
                               {source:b'exe'},'legacy_guard')
        self.assertEqual(result,23)
        self.assertNotIn(r'\NAND_USER\a.exe',m.files);self.assertNotIn(r'\NAND_USER\b.exe',m.files)
        self.assertEqual(m.files[r'\Hard Disk\AutoLaunch\FILEOPS_0001\export.exe'],b'exe')
        log=m.files[r'\Hard Disk\AutoLaunch\FILEOPS_0001\RESULT.txt'].decode('utf-16le')
        self.assertEqual(log.count('FILE_CHANGE_BLOCKED_B_OR_HELPER_ACTIVE'),2)

    def test_entry_watch_finishes_before_next_line_and_no_samples_is_failure(self):
        log,c=r'\Windows\sample.log',r'\Hard Disk\c.dll'
        lines=[f'WATCH|{log}|sample.log',f'COPY|{c}|after.dll']
        m,result=self.scenario(lines,{log:b'sample',c:b'new'})
        self.assertEqual(result,0)
        later=next(e for e in m.events if e[1]==c);self.assertGreaterEqual(later[3],181000)
        m,result=self.scenario(lines,{c:b'new'})
        self.assertNotEqual(result,0);self.assertTrue(any(e[1]==c for e in m.events))
        self.assertEqual(m.files[r'\Hard Disk\AutoLaunch\FILEOPS_0001\after.dll'],b'new')

    def test_entry_validates_all_syntax_and_active_output_before_any_job(self):
        a,b=r'\Windows\a.dll',r'\Windows\backup.dll'
        for second in ['UNKNOWN|x|y',r'COPY|\Windows\a.dll|\Hard Disk\AutoLaunch\FILEOPS_0001\RESULT.txt']:
            m,result=self.scenario([f'MOVE|{a}|{b}',second],{a:b'old'})
            self.assertNotEqual(result,0);self.assertEqual(m.events,[]);self.assertEqual(m.files[a],b'old')

    def test_entry_absolute_copy_replaces_existing_target(self):
        a,c=r'\Windows\a.dll',r'\Hard Disk\c.dll'
        m,result=self.scenario([f'COPY|{c}|{a}',f'DELETE|{c}'],{a:b'old',c:b'new'})
        self.assertEqual(result,0);self.assertEqual(m.files[a],b'new');self.assertNotIn(c,m.files)

    def test_copy_replaces_or_creates_without_backup(self):
        source,target=r'\Hard Disk\new.exe',r'\NAND_USER\MRN_TEST\app.exe'
        for existing in ({},{target:b'old'}):
            m,result=self.scenario([f'COPY|{source}|{target}'],{source:b'new',**existing})
            self.assertEqual(result,0);self.assertEqual(m.files[target],b'new')
            self.assertEqual([e[:3] for e in m.events],[('COPY',source,target)])
            self.assertFalse(any('.bak' in p or '.new-' in p for p in m.files))
            self.assertIn('BYTE_COMPARE_PASS',m.files[r'\Hard Disk\AutoLaunch\FILEOPS_0001\RESULT.txt'].decode('utf-16le'))

    def test_run_standalone_creates_process_and_closes_handles(self):
        exe=r'\NAND_USER\My App\app.exe'
        m,result=self.scenario([f'RUN|{exe}'],{exe:b'executable'})
        self.assertEqual(result,0);self.assertEqual([e[:3] for e in m.events],[('RUN',exe,None)])
        self.assertIn(701,m.closed_handles);self.assertIn(702,m.closed_handles)
        log=m.files[r'\Hard Disk\AutoLaunch\FILEOPS_0001\RESULT.txt'].decode('utf-16le')
        self.assertIn('RUN_PROCESS_CREATED_PID a=703',log)
        self.assertEqual(m.waits,[(77,7000)])  # UI lifetime only, never child handles 701/702.

    def test_run_releases_copy_guard_and_continues_after_copy_failure(self):
        source,exe=r'\Hard Disk\new.exe',r'\NAND_USER\app.exe'
        lines=[f'COPY|{source}|{exe}',f'RUN|{exe}']
        m,result=self.scenario(lines,{source:b'new'})
        self.assertEqual(result,0);self.assertEqual([e[0] for e in m.events],['COPY','RUN'])
        m,result=self.scenario(lines,{source:b'new',exe:b'old'},'copy')
        self.assertNotEqual(result,0);self.assertIn('CreateProcessW',m.calls)
        self.assertEqual(m.files[exe],b'old')

    def test_run_invalid_missing_and_launch_failure(self):
        exe=r'\NAND_USER\app.exe'
        for bad in ('RUN|relative.exe',r'RUN|\NAND_USER\..\a.exe',r'RUN|\NAND_USER\a.dll',
                    f'RUN|{exe}|',f'RUN|{exe}|arg'):
            self.assertEqual(self.parse([bad.encode()])[0],0,bad)
        self.assertEqual(self.parse([f'RUN|{exe}'.encode()],b'MRN_FILEOPS_V1')[0],0)
        for files,failure in (({},None),({exe:None},None),({exe:b'bad'},'launch')):
            m,result=self.scenario([f'RUN|{exe}',f'COPY|{exe}|later.exe'],files,failure)
            self.assertNotEqual(result,0);self.assertTrue(any(e[0]=='COPY' for e in m.events))
            if failure is None:self.assertNotIn('CreateProcessW',m.calls)

    def test_copy_failure_continues_and_protected_paths_are_rejected(self):
        source,target=r'\Hard Disk\new.exe',r'\NAND_USER\old.exe'
        for failure in ('copy','read'):
            m,result=self.scenario([f'COPY|{source}|{target}',f'DELETE|{source}'],
                                   {source:b'new',target:b'old'},failure)
            self.assertNotEqual(result,0);self.assertEqual(m.files[source],b'new')
            self.assertFalse(any(e[0]=='DELETE' for e in m.events))
            # DELETE was attempted too, but its backup suffers the same injected failure.
            log=m.files[r'\Hard Disk\AutoLaunch\FILEOPS_0001\RESULT.txt'].decode('utf-16le')
            self.assertIn('JOB_BEGIN_INDEX_OPERATION a=2 b=4',log)
            self.assertIn('FILEOPS_SUCCESS_FAILED a=0 b=2',log)
        for path in (source,r'\Hard Disk\AutoLaunch\fileops.txt',r'\NAND_USER\..\bad.exe'):
            self.assertEqual(self.parse([f'COPY|{source}|{path}'.encode()])[0],0)
        self.assertEqual(self.parse([f'COPY|{source}|{target}'.encode()],b'MRN_FILEOPS_V1')[0],0)
        self.assertEqual(self.parse([f'OVERWRITE|{source}|{target}'.encode()])[0],0)
        m,result=self.scenario([f'COPY|{source}|\\Hard Disk\\AutoLaunch\\FILEOPS_0001\\RESULT.txt'],{source:b'new'})
        self.assertNotEqual(result,0);self.assertEqual(m.events,[])


if __name__=='__main__':unittest.main()
