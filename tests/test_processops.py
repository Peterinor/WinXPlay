"""Run the built ARM process tool with OS doubles; never terminate host processes."""
from pathlib import Path
import struct
import unittest
from test_diagnostics import DiagnosticMachine
from arm_support import Arm
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_LR, UC_ARM_REG_PC

ROOT=Path(__file__).resolve().parents[1]
EXE=ROOT/'build/tools/process_ops/process_ops.exe'
HEADER=b'MRN_PROCESSOPS_V1\n'
DEFAULT=HEADER+b'KILL|10|entHMI2.exe|20|1\nKILL|15|entMedia.exe|20|1\n'

class ProcessMachine(DiagnosticMachine):
    def __init__(self, config=DEFAULT, processes=None, failure=None, **kwargs):
        super().__init__('hmi-termination-experiment',config=config,**kwargs)
        self.start=self.tick;self.failure=failure;self.dynamic={};self.snapshots=0
        self.processes=processes if processes is not None else {201:('entHMI2.exe',0),202:('entMedia.exe',0)}
        self.kills=[];self.opens=[];self.items=[];self.index=0;self.finished=set()

    def hook(self,u,addr,size,data):
        name=self.hooks.get(addr)
        if name is None:return
        a,b,c,d=[u.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]
        result=1
        if name=='CreateFileW':
            path=self.string(a)
            if path.endswith('processops.txt'):
                self.opened_paths.append(path);result=43 if self.config is not None else 0xffffffff
            else:return super().hook(u,addr,size,data)
        elif name=='LoadLibraryW':result=0 if self.failure=='no_toolhelp' else 101
        elif name in ('GetProcAddress','GetProcAddressW'):
            symbol=self.string(b)
            if symbol not in self.dynamic:
                stub=0x30008000+4*len(self.dynamic);self.dynamic[symbol]=stub;self.hooks[stub]=symbol
            result=self.dynamic[symbol]
        elif name=='CreateToolhelp32Snapshot':
            self.snapshots+=1
            if self.failure=='snapshot':self.error=5;result=0xffffffff
            else:
                self.items=[(pid,item[0]) for pid,item in self.processes.items() if item[1]<=self.tick-self.start and pid not in self.finished]
                if self.failure=='identity_change' and self.snapshots==2:self.items=[(301,'entHMI2.exe')]
                self.index=0;result=102
        elif name in ('Process32First','Process32Next'):
            if name=='Process32Next':self.index+=1
            if self.index>=len(self.items):self.error=18;result=0
            else:
                pid,exe=self.items[self.index]
                u.mem_write(b+8,struct.pack('<I',pid));self.wide(b+36,'\\Windows\\'+exe)
        elif name=='OpenProcess':
            self.opens.append(c);result=0 if self.failure=='open' else c+1000
            self.error=5 if not result else 0
        elif name=='WaitForSingleObject':
            assert b==0, 'No blocking wait may gate other jobs'
            pid=a-1000
            delayed=self.failure=='busy_delayed' and pid==201 and self.tick-self.start>=64000
            if delayed:self.finished.add(pid)
            result=0 if pid in self.finished else 258
        elif name=='TerminateProcess' and a!=0x42:
            pid=a-1000;self.kills.append((pid,self.tick-self.start))
            busy=self.failure in ('busy_delayed','busy_forever') and pid==201
            result=int(self.failure!='terminate' and not busy)
            if result and self.failure!='timeout':self.finished.add(pid)
            else:self.error=170 if busy else 5
        elif name in ('FreeLibrary','CloseToolhelp32Snapshot'):pass
        else:return super().hook(u,addr,size,data)
        u.reg_write(UC_ARM_REG_R0,result);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))

class ProcessOpsTests(unittest.TestCase):
    def parse(self,data):
        a=Arm(EXE);a.u.mem_map(0x21000000,0x100000);a.u.mem_map(0x21100000,0x100000)
        a.u.mem_write(0x21000000,data or b'\0')
        guard=0x21100000+1024*140
        a.u.mem_write(guard,b'END_OF_JOBS')
        ok=a.call('ParseProcessConfig',0x21000000,len(data),0x21100000,0x20004000,
                  instruction_limit=30000000)
        self.assertEqual(bytes(a.u.mem_read(guard,11)),b'END_OF_JOBS')
        count=struct.unpack('<I',a.u.mem_read(0x20004000,4))[0]
        jobs=[]
        for i in range(count):
            raw=bytes(a.u.mem_read(0x21100000+i*140,140))
            jobs.append((*struct.unpack('<III',raw[:12]),raw[12:].decode('utf-16le').split('\0')[0]))
        return ok,jobs

    def test_config_bom_crlf_and_same_time_order(self):
        ok,jobs=self.parse(b'\xef\xbb\xbf'+DEFAULT.replace(b'\n',b'\r\n'))
        self.assertEqual(ok,1);self.assertEqual(jobs,[(10000,20000,1000,'entHMI2.exe'),(15000,20000,1000,'entMedia.exe')])
        self.assertEqual(self.parse(HEADER+b'KILL|0|a.exe|0|1\nKILL|0|b.EXE|120|30')[0],1)

    def test_invalid_config_rejected_before_any_kill(self):
        bad=[b'',HEADER,DEFAULT+b'KILL|1|later.exe|0|1',DEFAULT+b'garbage',
             DEFAULT.replace(b'entHMI2.exe',b'*.exe'),DEFAULT.replace(b'entHMI2.exe',b'\\Windows\\entHMI2.exe'),
             DEFAULT.replace(b'|10|',b'|-1|'),DEFAULT.replace(b'|10|',b'|4294967296|'),
             DEFAULT.replace(b'|10|',b'|3601|'),DEFAULT.replace(b'|20|1',b'|121|1'),
             DEFAULT.replace(b'|20|1',b'|20|0'),DEFAULT.replace(b'|20|1',b'|20|31'),
             DEFAULT.replace(b'entHMI2.exe',b'entHMI2.exe|x'),DEFAULT+b'\x00',
             DEFAULT+b'# \xff',HEADER+b'KILL|0|a.exe|0|1\n'*1025]
        for config in bad:
            with self.subTest(config=config):
                self.assertEqual(self.parse(config),(0,[]))
                m=ProcessMachine(config=config);code,log=m.run()
                self.assertEqual(code,16,log);self.assertEqual(m.kills,[])
        m=ProcessMachine(config=None);self.assertEqual(m.run()[0],15);self.assertEqual(m.kills,[])

    def test_expanded_job_and_byte_boundaries(self):
        config=HEADER+b'KILL|0|a.exe|0|1\n'*1024
        ok,jobs=self.parse(config)
        self.assertEqual(ok,1);self.assertEqual(len(jobs),1024)
        self.assertEqual(self.parse(config+b'KILL|0|a.exe|0|1\n'),(0,[]))
        one=HEADER+b'KILL|0|a.exe|0|1\n'
        maximum=one+b'\n'*(524288-len(one))
        self.assertEqual(self.parse(maximum),(1,[(0,0,1000,'a.exe')]))
        self.assertEqual(self.parse(maximum+b'\n'),(0,[]))
        m=ProcessMachine(config=maximum,processes={201:('a.exe',0)})
        self.assertEqual(m.run(instruction_limit=30000000)[0],0)
        self.assertEqual(m.kills,[(201,0)])
        bad=ProcessMachine(config=maximum+b'\n')
        code,log=bad.run()
        self.assertEqual(code,16);self.assertEqual(bad.kills,[])
        self.assertIn('CONFIG_TOO_LARGE_NO_KILL',log)

    def test_large_plan_runs_all_jobs_and_late_error_prevents_all_kills(self):
        operations=[f'KILL|0|target_{i}.exe|0|1\n'.encode() for i in range(40)]
        config=HEADER+(b'#'+b'x'*200+b'\n')*45+b''.join(operations)
        self.assertGreater(len(config),8192)
        processes={201+i:(f'target_{i}.exe',0) for i in range(40)}
        m=ProcessMachine(config=config,processes=processes)
        code,log=m.run(instruction_limit=30000000)
        self.assertEqual(code,0,log)
        self.assertEqual([pid for pid,tick in m.kills],list(range(201,241)))
        self.assertIn('PROCESSOPS_FINISHED jobs=40 failures=0',log)
        bad=ProcessMachine(config=config+b'INVALID\n',processes=processes)
        self.assertEqual(bad.run(instruction_limit=30000000)[0],16)
        self.assertEqual(bad.kills,[])

    def test_absolute_times_and_serial_exit_confirmation(self):
        m=ProcessMachine();code,log=m.run()
        self.assertEqual(code,0,log);self.assertEqual(m.kills,[(201,10000),(202,15000)])
        self.assertEqual(log.count('TARGET_EXIT_CONFIRMED'),2);self.assertIn('PROCESSOPS_COMPLETE',log)
        self.assertFalse(any(name in ('FindWindowW','ShowWindow','DeviceIoControl') for name,args in m.calls))

    def test_late_target_does_not_delay_other_job(self):
        m=ProcessMachine(processes={201:('ENTHMI2.EXE',18000),202:('entMedia.exe',0)})
        self.assertEqual(m.run()[0],0);self.assertEqual(m.kills,[(202,15000),(201,18000)])

    def test_missing_target_expires_without_stopping_other_jobs(self):
        m=ProcessMachine(processes={202:('entMedia.exe',0)})
        code,log=m.run();self.assertEqual(code,23,log)
        self.assertEqual(m.tick-m.start,30000);self.assertEqual(m.kills,[(202,15000)])
        m=ProcessMachine(config=HEADER+b'KILL|0|absent.exe|0|1')
        self.assertEqual(m.run()[0],23);self.assertEqual(m.tick-m.start,0)

    def test_ambiguous_self_and_changed_identity_only_fail_own_job(self):
        cases=[dict(processes={201:('entHMI2.exe',0),203:('ENTHMI2.EXE',0),202:('entMedia.exe',0)}),
               dict(processes={100:('entHMI2.exe',0),202:('entMedia.exe',0)}),
               dict(failure='identity_change')]
        for options in cases:
            m=ProcessMachine(**options);code,log=m.run()
            self.assertEqual(code,23,log);self.assertEqual(m.kills,[(202,15000)])

    def test_failures_do_not_report_all_success(self):
        for failure in ('snapshot','open','terminate','timeout'):
            with self.subTest(failure=failure):
                m=ProcessMachine(failure=failure);code,log=m.run()
                self.assertEqual(code,23,log)
                self.assertIn('JOB_START job=2',log)
                self.assertNotIn('TARGET_EXIT_CONFIRMED',log);self.assertNotIn('PROCESSOPS_COMPLETE',log)
        m=ProcessMachine(failure='no_toolhelp');self.assertEqual(m.run()[0],18)
        for options in [dict(write_fail=True),dict(flush_fail=True)]:
            m=ProcessMachine(**options);self.assertNotEqual(m.run()[0],0);self.assertEqual(m.kills,[])

    def test_busy_delayed_exit_keeps_handle_and_media_runs_on_time(self):
        m=ProcessMachine(failure='busy_delayed');code,log=m.run()
        self.assertEqual(code,0,log)
        self.assertIn((202,15000),m.kills)
        self.assertEqual([t for pid,t in m.kills if pid==201],list(range(10000,30001,1000)))
        self.assertEqual(m.opens,[201,202])
        self.assertEqual(log.count('TARGET_EXIT_CONFIRMED'),2)
        self.assertIn('[64000] JOB_END job=1',log)
        self.assertIn('accepted=0 error=170',log)

    def test_busy_forever_does_not_block_media_or_claim_hmi_exit(self):
        m=ProcessMachine(failure='busy_forever');code,log=m.run()
        self.assertEqual(code,23,log);self.assertIn((202,15000),m.kills)
        self.assertEqual(log.count('TARGET_EXIT_CONFIRMED'),1)
        self.assertIn('EXIT_NOT_CONFIRMED_TIMEOUT',log)
        self.assertNotIn('PROCESSOPS_COMPLETE',log)

    def test_neutral_header_and_non_mrn_directory(self):
        for header in (b'PROCESSOPS_V1\n', b'WINCE_PROCESSOPS_V1\n'):
            m=ProcessMachine(config=header+b'KILL|0|example.exe|0|1\n',
                processes={201:('example.exe',0)},path=r'\Storage Card\Tools\process_ops.exe')
            self.assertEqual(m.run()[0],0)
            self.assertEqual(m.kills,[(201,0)])
            self.assertTrue(all(p.startswith(m.directory) for p in m.opened_paths))

    def test_each_directory_and_each_invocation_executes_plan(self):
        for folder in ('AutoLaunch','AUTOLAUNCH_PROD'):
            for _ in range(2):
                m=ProcessMachine(path='\\Hard Disk\\'+folder+'\\process_ops.exe')
                self.assertEqual(m.run()[0],0);self.assertEqual(len(m.kills),2)
                self.assertTrue(all(p.startswith(m.directory) for p in m.opened_paths))

if __name__=='__main__':unittest.main()
