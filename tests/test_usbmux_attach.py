"""Execute the built ARM DLL's attach paths with fake CE/USB APIs (no physical USB)."""
import struct, unittest
from test_usbmux_registration import Machine, PATH
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC

class AttachMachine(Machine):
    def __init__(self, failure=None):
        super().__init__(); self.attach_failure=failure; self.log=bytearray(); self.closed=[]
        self.registry_trace={};self.stream_values={};self.activations=[];self.init_context=0
        self.init_done=self.add('init_done');self.deinit_done=self.add('deinit_done')
        # CE's inline GetCurrentProcessId reads the shared KData page.
        self.u.mem_map(0xffffc000,4096);self.put(0xffffc80c,42)
        self.api=0x20004000; self.iface=0x20005000; self.desc=0x20006000
        self.endpoints=0x20007000; self.accepted=0x20009000; self.pipe_count=0
        # WM5 USB_FUNCS/USB_INTERFACE/USB_DEVICE layout used by the production build.
        for slot,name in [(3,'usb_notify'),(13,'usb_info'),(23,'usb_pipe'),(26,'usb_close')]:
            self.put(self.api+4*slot,self.add(name))
        self.put(self.api,168)
        self.put(self.iface,24)
        self.u.mem_write(self.iface+4,bytes([9,4,1,0,2,255,254,2,0]))
        self.put(self.iface+20,self.endpoints)
        self.put(self.desc,32)
        self.u.mem_write(self.desc+12,struct.pack('<HH',1452,0x12a8))
        for i,address in enumerate((0x81,2)):
            p=self.endpoints+16*i; self.put(p,16)
            self.u.mem_write(p+4,struct.pack('<BBBBHB',7,5,address,2,512,0))
    def configurations(self, configs, active=0, ce7=True):
        stride=32 if ce7 else 28; interface_stride=28 if ce7 else 24
        self.put(self.desc+24,0x2000a000)
        self.u.mem_write(self.desc+21,bytes([len(configs)]))
        for i,(value,interfaces) in enumerate(configs):
            p=0x2000a000+i*stride;self.put(p,stride)
            self.u.mem_write(p+4,bytes([9,2,0,0,len(interfaces),value,0,0x80,250]))
            self.put(p+20,len(interfaces));self.put(p+24,0x2000b000+i*0x100)
            for j,(cls,sub,proto) in enumerate(interfaces):
                q=0x2000b000+i*0x100+j*interface_stride;self.put(q,interface_stride)
                self.u.mem_write(q+4,bytes([9,4,j,0,0,cls,sub,proto,0]))
        self.put(self.desc+28,0x2000a000+active*stride)
    def put(self,p,v):self.u.mem_write(p,struct.pack('<I',v))
    def hook(self,u,address,size,data):
        name=self.hooks.get(address)
        if not name:return
        a,b,c,d=[u.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]
        result=1
        def arg(i):return (a,b,c,d)[i] if i<4 else struct.unpack('<I',u.mem_read(u.reg_read(UC_ARM_REG_SP)+(i-4)*4,4))[0]
        if name=='RegCreateKeyExW':
            assert a==0x80000002 and self.text(b)==r'Drivers\WinXPlay\UsbMuxStream'
            result=5 if self.attach_failure=='stream_key' else 0
            if not result:self.put(arg(7),778);self.put(arg(8),1)
        elif name=='RegSetValueExW' and a==778:
            key=self.text(b);assert key in ('DLL','Prefix','Index','Flags') and c==0
            result=5 if self.attach_failure=='stream_value' else 0
            if not result:self.stream_values[key]=(d,bytes(u.mem_read(arg(4),arg(5))))
        elif name=='RegOpenKeyExW':
            assert a==0x80000002 and self.text(b)==r'Drivers\USB\ClientDrivers\WinXPlayUsbMux\Diagnostics'
            assert c==0 and d==2
            result=5 if self.attach_failure in ('registry_trace','all_trace') else 0
            if not result:self.put(arg(4),777)
            self.error=999
        elif name=='RegSetValueExW':
            assert a==777 and c==0 and d==1
            assert arg(5)<=320
            result=5 if self.attach_failure=='registry_write' else 0
            if not result:self.registry_trace[self.text(b)]=bytes(u.mem_read(arg(4),arg(5))).decode('utf-16le').rstrip('\0')
            self.error=999
        elif name=='GetModuleFileNameW':self.wide(b,PATH);result=len(PATH)
        elif name=='GetCurrentProcessId':result=42
        elif name=='GetTickCount':result=1000
        elif name=='CreateFileW':
            assert self.text(a)==r'\NAND_USER\WinXPlay\usb-driver.log'
            result=0xffffffff if self.attach_failure in ('trace','all_trace') else 123;self.error=5
        elif name=='GetFileSize':result=len(self.log)
        elif name=='SetFilePointer':result=len(self.log)
        elif name=='WriteFile':self.log.extend(u.mem_read(b,c));self.put(d,c);self.error=999
        elif name=='CloseHandle':self.error=999
        elif name=='usb_info':result=self.desc
        elif name=='usb_pipe':
            self.pipe_count+=1
            result=0 if self.attach_failure=='pipe' and self.pipe_count==2 else self.pipe_count
            self.error=31 if not result else 0
        elif name=='usb_close':self.closed.append(a)
        elif name=='LocalAlloc':result=0x20008000;u.mem_write(result,b'\0'*b)
        elif name in ('InitializeCriticalSection','DeleteCriticalSection','LocalFree'):pass
        elif name in ('InterlockedIncrement','InterlockedDecrement'):
            result=(struct.unpack('<I',u.mem_read(a,4))[0]+(1 if name.endswith('Increment') else -1))&0xffffffff
            self.put(a,result)
        elif name=='InterlockedCompareExchange':
            result=struct.unpack('<I',u.mem_read(a,4))[0]
            if result==c:self.put(a,b)
        elif name=='InterlockedExchange':
            result=struct.unpack('<I',u.mem_read(a,4))[0];self.put(a,b)
        elif name=='ActivateDeviceEx':
            assert self.text(a)==r'Drivers\WinXPlay\UsbMuxStream' and b==c==0
            assert self.stream_values=={'DLL':(1,(PATH+'\0').encode('utf-16le')),
                'Prefix':(1,'CMX\0'.encode('utf-16le')),'Index':(4,struct.pack('<I',1)),'Flags':(4,bytes(4))}
            self.activations.append(d)
            if self.attach_failure=='register':result=0;self.error=31
            else:
                self.activation_return=u.reg_read(UC_ARM_REG_LR)
                self.wide(0x2000e000,r'Drivers\Active\200')
                u.reg_write(UC_ARM_REG_R0,0x2000e000)
                u.reg_write(UC_ARM_REG_R1,0 if self.attach_failure=='init_context' else d)
                u.reg_write(UC_ARM_REG_LR,self.init_done)
                u.reg_write(UC_ARM_REG_PC,self.exports['CMX_Init']);return
        elif name=='init_done':
            self.init_context=a;u.reg_write(UC_ARM_REG_R0,456 if a else 0)
            u.reg_write(UC_ARM_REG_PC,self.activation_return);return
        elif name=='usb_notify':
            self.notify_callback=b;self.notify_context=c
            result=0 if self.attach_failure=='notify' else 1;self.error=31 if not result else 0
        elif name=='DeactivateDevice':
            assert a==456 and self.init_context
            self.closed.append(('device',a));self.deactivation_return=u.reg_read(UC_ARM_REG_LR)
            u.reg_write(UC_ARM_REG_R0,self.init_context);u.reg_write(UC_ARM_REG_LR,self.deinit_done)
            u.reg_write(UC_ARM_REG_PC,self.exports['CMX_Deinit']);return
        elif name=='deinit_done':
            self.init_context=0;u.reg_write(UC_ARM_REG_R0,1)
            u.reg_write(UC_ARM_REG_PC,self.deactivation_return);return
        else:return super().hook(u,address,size,data)
        u.reg_write(UC_ARM_REG_R0,result);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
    def attach(self,iface=None):
        self.put(self.accepted,99)
        self.u.mem_write(0x2001f000,struct.pack('<III',self.accepted,0,0))
        result=self.call('USBDeviceAttach',99,self.api,self.iface if iface is None else iface,0)
        return result,struct.unpack('<I',self.u.mem_read(self.accepted,4))[0]

class AttachTests(unittest.TestCase):
    def test_activation_calls_real_init_with_bus_context_and_rejects_uninvited_init(self):
        m=AttachMachine();self.assertEqual(m.call('CMX_Init',0x20001000,0xdeadbeef),0)
        self.assertEqual(m.error,87)
        self.assertEqual(m.attach(),(1,1));self.assertEqual(m.activations,[m.init_context])
        self.assertIn(b'CMX_INIT pid context',m.log)
        self.assertEqual(m.call('CMX_Init',0x20001000,m.init_context),0)
    def test_stream_registry_and_bad_init_failures_close_pipes_without_activation_leak(self):
        for failure in ('stream_key','stream_value','init_context'):
            m=AttachMachine(failure);self.assertEqual(m.attach(),(0,0))
            self.assertIn(1,m.closed);self.assertIn(2,m.closed);self.assertEqual(m.init_context,0)
            self.assertNotIn(('device',456),m.closed)
            m.attach_failure=None;self.assertEqual(m.attach(),(1,1))
    def test_driver_uses_activate_and_deactivate_not_unimplemented_register_apis(self):
        from test_usbmux_registration import DLL,base
        p=base.pefile.PE(str(DLL));core=base.pefile.PE(str(base.FACTORY))
        names={s.ordinal:s.name for s in core.DIRECTORY_ENTRY_EXPORT.symbols}
        imports={i.name or names.get(i.ordinal) for d in p.DIRECTORY_ENTRY_IMPORT for i in d.imports}
        self.assertTrue({b'ActivateDeviceEx',b'DeactivateDevice'}<=imports)
        self.assertTrue({b'RegisterDevice',b'DeregisterDevice'}.isdisjoint(imports))
    def test_null_interface_rejected_and_accepted_cleared(self):
        m=AttachMachine();self.assertEqual(m.attach(0),(0,0));self.assertIn(b'REJECT_BOOTSTRAP_NO_PHONE_INTERFACES',m.log)
    def test_wrong_interface_and_missing_endpoints(self):
        m=AttachMachine();m.u.mem_write(m.iface+9,b'\x02')
        self.assertEqual(m.attach(),(0,0));self.assertIn(b'REJECT_DESCRIPTOR',m.log)
        m=AttachMachine();m.u.mem_write(m.iface+8,b'\x00')
        self.assertEqual(m.attach(),(0,0));self.assertIn(b'REJECT_ENDPOINTS',m.log)
    def test_pipe_failure_closes_other_pipe_and_preserves_logged_error(self):
        m=AttachMachine('pipe');self.assertEqual(m.attach(),(0,0));self.assertEqual(m.closed,[1])
        self.assertIn(b'PIPE_OUT handle error 00000000 0000001f',m.log)
    def test_register_and_notification_failure_cleanup(self):
        for failure in ('register','notify'):
            m=AttachMachine(failure);self.assertEqual(m.attach(),(0,0))
            self.assertIn(1,m.closed);self.assertIn(2,m.closed)
            self.assertEqual(('device',456) in m.closed,failure=='notify')
            self.assertIn(b'REJECT_REGISTRATION_OR_NOTIFY',m.log)
    def test_success_and_unwritable_trace_do_not_change_acceptance(self):
        m=AttachMachine();self.assertEqual(m.attach(),(1,1));self.assertIn(b'ACCEPTED',m.log)
        m=AttachMachine('trace');self.assertEqual(m.attach(),(1,1));self.assertEqual(m.log,b'')
    def test_trace_cap_does_not_prevent_attach(self):
        m=AttachMachine();m.log=bytearray(b'x'*(128*1024))
        self.assertEqual(m.attach(),(1,1));self.assertEqual(len(m.log),128*1024)
        self.assertIn('ATTACH',m.registry_trace)
    def test_registry_records_attach_when_file_is_unwritable(self):
        m=AttachMachine('trace');self.assertEqual(m.attach(),(1,1))
        self.assertEqual(m.log,b'')
        self.assertIn('ATTACH',m.registry_trace)
        self.assertIn('ACCEPTED',m.registry_trace)
    def test_optional_registry_failures_do_not_break_connection(self):
        for failure in ('registry_trace','registry_write','all_trace'):
            m=AttachMachine(failure);self.assertEqual(m.attach(),(1,1))
            self.assertEqual(m.registry_trace,{})
    def test_actual_configuration_logged_before_pipe_failure(self):
        m=AttachMachine('pipe');config=0x2000a000
        m.put(m.desc+28,config);m.put(config,28)
        m.u.mem_write(config+4,bytes([9,2,0,0,1,5,0,0x80,250]))
        # No interface-array traversal on this early failure path.
        m.put(config+20,1)
        self.assertEqual(m.attach(),(0,0))
        self.assertIn('00000005 00000001 00000001',m.registry_trace['ACTIVE_CONFIG'])
        self.assertIn('00000000 0000001f',m.registry_trace['PIPE_OUT'])

class BootstrapTests(unittest.TestCase):
    def test_initial_ptp_configuration_gets_control_stream_without_bulk_pipes(self):
        for ce7 in (False,True):
            m=AttachMachine();m.configurations([(1,[(6,1,1)]),(2,[(1,1,0)])],ce7=ce7)
            self.assertEqual(m.attach(0),(1,1));self.assertEqual(m.pipe_count,0)
            self.assertIn('00000001 00000000 00000004',m.registry_trace['BOOTSTRAP'])
    def test_selects_actual_value_not_fixed_five_and_reads_ce7_stride(self):
        for ce7 in (False,True):
            m=AttachMachine();m.configurations([(1,[(6,1,1)]),(7,[(255,254,2),(2,13,0)])],ce7=ce7)
            self.assertEqual(m.attach(0),(1,1))
            self.assertIn('00000001 00000007 00000004',m.registry_trace['BOOTSTRAP'])
    def test_ready_configuration_defers_to_os_interface_drivers(self):
        m=AttachMachine();m.configurations([(1,[(6,1,1)]),(6,[(255,254,2),(2,13,0)])],active=1)
        self.assertEqual(m.attach(0),(0,0));self.assertEqual(m.pipe_count,0)
        self.assertIn('BOOTSTRAP_READY_DEFER_TO_INTERFACES',m.registry_trace)
        self.assertEqual(m.attach(),(1,1))
        self.assertIn('00000006 00000001 00000001',m.registry_trace['ACCEPTED'])
    def test_foreign_vendor_and_apple_nonphone_not_claimed(self):
        m=AttachMachine();m.configurations([(1,[(6,1,1)])]);m.u.mem_write(m.desc+12,b'\x34\x12')
        self.assertEqual(m.attach(0),(0,0))
        m=AttachMachine();m.configurations([(1,[(3,1,1)])])
        self.assertEqual(m.attach(0),(0,0))
    def test_invalid_host_structure_stride_not_traversed(self):
        for bad in (0,12,27,8192):
            m=AttachMachine();m.configurations([(1,[(6,1,1)])]);m.put(0x2000a000,bad)
            self.assertEqual(m.attach(0),(0,0));self.assertIn('CONFIG_INVALID',m.registry_trace)
    def test_bootstrap_registration_failure_has_no_null_pipe_cleanup(self):
        for failure in ('register','notify'):
            m=AttachMachine(failure);m.configurations([(1,[(6,1,1)])])
            self.assertEqual(m.attach(0),(0,0));self.assertNotIn(0,m.closed)

class TransitionMachine(AttachMachine):
    def __init__(self,failure=None):
        super().__init__();self.transition_failure=failure;self.events={};self.resets=[];self.vendors=[]
        self.pins=0;self.unpins=0;self.selected=None;self.worker=None;self.freed=0
        for slot,name in [(14,'usb_vendor'),(33,'usb_status'),(35,'usb_abort'),(36,'usb_close_transfer'),(39,'usb_disable')]:
            self.put(self.api+4*slot,self.add(name))
        self.add('LoadDriver')
    def hook(self,u,address,size,data):
        name=self.hooks.get(address)
        if not name:return
        a,b,c,d=[u.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]
        def arg(i):return (a,b,c,d)[i] if i<4 else struct.unpack('<I',u.mem_read(u.reg_read(UC_ARM_REG_SP)+(i-4)*4,4))[0]
        result=1
        if name=='RegOpenKeyExW' and self.text(b)==r'Drivers\USB\LoadClients\1452\Default\Default':
            assert a==0x80000002 and d==3
            self.put(arg(4),888);result=0
        elif name=='RegSetValueExW' and a==888:
            assert self.text(b)=='bConfigurationValue' and d==4 and arg(5)==4
            result=5 if self.transition_failure=='registry' else 0
            if not result:self.selected=struct.unpack('<I',u.mem_read(arg(4),4))[0]
        elif name=='RegQueryValueExW' and a==888:
            assert self.text(b)=='bConfigurationValue'
            self.put(d,4);self.put(arg(5),4);self.put(arg(4),self.selected);result=0
        elif name in ('EnterCriticalSection','LeaveCriticalSection'):pass
        elif name=='InterlockedExchange':
            result=struct.unpack('<I',u.mem_read(a,4))[0];self.put(a,b)
        elif name=='InterlockedCompareExchange':
            result=struct.unpack('<I',u.mem_read(a,4))[0]
            if result==c:self.put(a,b)
        elif name=='GetModuleHandleW':result=222
        elif name=='LoadDriver':
            assert self.text(a)==PATH
            result=0 if self.transition_failure=='pin' else 444
            if result:self.pins+=1
        elif name=='FreeLibrary' and a==444:self.unpins+=1
        elif name=='CreateEventW':result=10000+len(self.events);self.events[result]=False
        elif name=='EventModify':
            assert b in (2,3);self.events[a]=b==3
        elif name=='SetEvent':self.events[a]=True
        elif name=='ResetEvent':self.events[a]=False
        elif name=='WaitForSingleObject':
            result=0 if self.events[a] else 258
            if self.transition_failure=='timeout' and len(self.events)>1:result=258
        elif name=='CreateThread':
            if self.transition_failure=='thread':result=0
            else:self.exports['worker']=c;self.worker=d;result=999
        elif name=='usb_vendor':
            req=bytes(u.mem_read(arg(4),8));self.vendors.append(req)
            assert req==bytes.fromhex('c052000004000100')
            u.mem_write(arg(5),bytes([1 if self.transition_failure=='reply' else 0]))
            self.events[c]=True;result=900
        elif name=='usb_status':self.put(b,1);self.put(c,0)
        elif name in ('usb_abort','usb_close_transfer'):pass
        elif name=='usb_disable':
            self.resets.append((a,b,c));assert (a,b,c)==(99,1,0)
            result=0 if self.transition_failure=='reset' else 1
        elif name=='usb_notify':
            self.exports['notify']=b;self.notify_context=c
        elif name=='LocalFree':self.freed+=1
        elif name=='FreeLibraryAndExitThread':
            assert a==444;self.unpins+=1
            u.reg_write(UC_ARM_REG_R0,0);u.reg_write(UC_ARM_REG_PC,0x20000000);return
        else:return super().hook(u,address,size,data)
        u.reg_write(UC_ARM_REG_R0,result);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
    def start(self,with_target=False):
        configs=[(1,[(6,1,1)])]
        if with_target:configs.append((7,[(255,254,2),(2,13,0)]))
        self.configurations(configs)
        assert self.attach(0)==(1,1)
        self.context=self.call('CMX_Open',0x20008000,0,0)
        assert self.context
        self.u.mem_write(0x2001f000,bytes(16))
        return self.call('CMX_IOControl',self.context,0x2220d4,0,0)
    def close_and_work(self):
        self.call('CMX_Close',self.context)
        if self.worker:self.call('worker',self.worker)

class TransitionTests(unittest.TestCase):
    def test_vehicle_four_configurations_bootstrap_activation_and_detach_cleanup(self):
        m=TransitionMachine()
        m.configurations([(1,[(6,1,1)]),(2,[(1,1,0),(1,2,0),(3,0,0),(3,0,0)]),
                          (3,[(6,1,1),(255,254,2)]),
                          (4,[(6,1,1),(255,254,2),(1,1,0),(1,2,0),(3,0,0)])])
        self.assertEqual(m.attach(0),(1,1));self.assertEqual(m.pipe_count,0)
        context=m.init_context
        self.assertEqual(m.call('CMX_Open',context,0,0),context)
        self.assertEqual(m.call('CMX_Close',context),1)
        self.assertEqual(m.call('notify',m.notify_context,1,0,0),1)
        self.assertEqual(m.closed.count(('device',456)),1)
        self.assertEqual(m.freed,1);self.assertEqual(m.init_context,0)
    def test_vendor_request_runs_only_after_stream_closed_and_pins_dll(self):
        m=TransitionMachine();self.assertEqual(m.start(),1)
        self.assertEqual((m.vendors,m.resets),([],[]));self.assertEqual(m.pins,1)
        m.close_and_work();self.assertEqual(len(m.vendors),1);self.assertEqual(m.resets,[(99,1,0)])
        self.assertEqual(m.unpins,1)
    def test_known_configuration_is_written_without_vendor_mode_request(self):
        m=TransitionMachine();self.assertEqual(m.start(True),1);m.close_and_work()
        self.assertEqual(m.selected,7);self.assertEqual(m.vendors,[]);self.assertEqual(len(m.resets),1)
    def test_failed_mode_transfer_or_selection_does_not_reset(self):
        for failure in ('reply','timeout','registry'):
            m=TransitionMachine(failure);self.assertEqual(m.start(failure=='registry'),1);m.close_and_work()
            self.assertEqual(m.resets,[]);self.assertEqual(m.unpins,1)
            self.assertNotEqual(m.call('CMX_Open',m.context,0,0),0)
    def test_failed_worker_setup_releases_pin_and_can_retry(self):
        for failure in ('pin','thread'):
            m=TransitionMachine(failure);self.assertEqual(m.start(),0)
            self.assertEqual(m.pins,m.unpins);self.assertEqual(m.worker,None)
    def test_worker_timeout_before_close_never_touches_usb(self):
        m=TransitionMachine();self.assertEqual(m.start(),1);m.call('worker',m.worker)
        self.assertEqual((m.vendors,m.resets),([],[]));self.assertEqual(m.unpins,1)
    def test_unplug_before_worker_releases_context_and_avoids_hardware_calls(self):
        m=TransitionMachine();self.assertEqual(m.start(),1);m.call('CMX_Close',m.context)
        m.u.mem_write(0x2001f000,bytes(16));m.call('notify',m.notify_context,1,0,0)
        m.call('worker',m.worker)
        self.assertEqual((m.vendors,m.resets),([],[]));self.assertEqual(m.freed,1);self.assertEqual(m.unpins,1)
        self.assertNotIn(0,m.closed)
    def test_missing_reset_api_fails_without_queueing(self):
        m=TransitionMachine();m.put(m.api,156);self.assertEqual(m.start(),0)
        self.assertEqual(m.worker,None)
    def test_reset_failure_allows_later_retry(self):
        m=TransitionMachine('reset');self.assertEqual(m.start(True),1);m.close_and_work()
        self.assertNotEqual(m.call('CMX_Open',m.context,0,0),0)

if __name__=='__main__':unittest.main()
