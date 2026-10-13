"""Compiled ARM checks with explicit OS doubles; not a full CE or vehicle test."""
import sys,struct,json,unittest,os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
sys.path.insert(0,str(ROOT/'firmware/mrn/hmi_patch'))
from external_assets import oem_path
import arm_digest,pefile
from build_ab_patch import OUT,BASE,BAND,WINDOW,SOURCE
from patch_support import restore
NO_LOGS=os.environ.get('WINXPLAY_TEST_NO_LOGS')=='1'
REPORT_OUT=OUT.with_name(OUT.name+'-nologs') if NO_LOGS else OUT
def artifact_path(path):
    return REPORT_OUT/path.name if NO_LOGS and path.name=='WinXPlayBridge.dll' else path
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *

REGS=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5,
      UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_R12]
MEM=0x30000000;STOP=MEM+0xff00;SP=MEM+0xe000
CORE=pefile.PE(str(oem_path('coredll.dll')))
NAMES={s.ordinal:(s.name or b'').decode() for s in CORE.DIRECTORY_ENTRY_EXPORT.symbols}

class Machine:
    def __init__(self,path,slide=0):
        path=artifact_path(path)
        self.pe=pefile.PE(str(path));self.u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
        self.base=self.pe.OPTIONAL_HEADER.ImageBase+slide;self.slide=slide
        self.u.mem_map(self.base,(self.pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095)
        self.u.mem_write(self.base,self.pe.get_memory_mapped_image());self.u.mem_map(MEM,0x10000)
        # WM5 SDK's GetCurrentProcessId macro reads the CE KData page directly.
        self.u.mem_map(0xffffc000,0x1000);self.write(0xffffc80c,55)
        self.exports={s.name.decode():self.base+s.address for s in getattr(getattr(self.pe,'DIRECTORY_ENTRY_EXPORT',None),'symbols',[])}
        self.apis={};self.callbacks={};self.points={};self.now=1000;self.messages=[]
        for dll in self.pe.DIRECTORY_ENTRY_IMPORT:
            if dll.dll.lower()!=b'coredll.dll':continue
            for imp in dll.imports:
                name=imp.name.decode() if imp.name else NAMES[imp.ordinal]
                address=MEM+0x100+4*len(self.apis);self.apis[address]=name
                self.write(imp.address+slide,address)
        self.u.hook_add(UC_HOOK_CODE,self.hook)
    def write(self,p,v):self.u.mem_write(p,struct.pack('<I',v&0xffffffff))
    def read(self,p):return struct.unpack('<I',self.u.mem_read(p,4))[0]
    def wide(self,p):
        b=bytearray()
        while bytes(self.u.mem_read(p+len(b),2))!=b'\0\0':b+=self.u.mem_read(p+len(b),2)
        return b.decode('utf-16le')
    def ret(self,value=1):
        self.u.reg_write(UC_ARM_REG_R0,value&0xffffffff)
        self.u.reg_write(UC_ARM_REG_PC,self.u.reg_read(UC_ARM_REG_LR))
    def hook(self,u,pc,size,_):
        args=[u.reg_read(r) for r in REGS[:4]]
        if pc in self.points:self.points[pc](*args);return
        if pc not in self.apis:return
        name=self.apis[pc]
        if name not in self.callbacks:raise AssertionError('Unexpected API: '+name)
        value=self.callbacks[name](*args)
        if value is not None:self.ret(value)
    def call(self,entry,*args):
        if isinstance(entry,str):entry=self.exports[entry]
        values=list(args)+[0xa000+i for i in range(len(args),13)]
        for r,v in zip(REGS,values):self.u.reg_write(r,v)
        self.u.reg_write(UC_ARM_REG_SP,SP);self.u.reg_write(UC_ARM_REG_LR,STOP)
        try:self.u.emu_start(entry,STOP,count=200000)
        except Exception as exc:
            raise RuntimeError('entry=%x PC=%x LR=%x'%(entry,self.u.reg_read(UC_ARM_REG_PC),self.u.reg_read(UC_ARM_REG_LR))) from exc
        assert self.u.reg_read(UC_ARM_REG_PC)==STOP
        return self.u.reg_read(UC_ARM_REG_R0)

class PatchTests(unittest.TestCase):
    def machine(self,slide=0):
        m=Machine(OUT/'ENTHMI2.WINXPLAY.EXE',slide)
        f=json.loads((OUT/'patch-manifest.json').read_text());state=m.base+f['data_rva']
        worker=m.base+f['code_rva']+f['symbols']['Worker'];m.state=state;m.worker=worker
        m.original=[];m.threads=[];m.loads=[];m.mirrored=[];m.closed=[]
        def original(*args):
            sp=m.u.reg_read(UC_ARM_REG_SP);assert sp==SP-64-36
            m.original.append(args)
            for i,r in enumerate(REGS[4:12]):m.u.reg_write(r,m.read(sp+4*i))
            m.u.reg_write(UC_ARM_REG_R0,0x4567);m.u.reg_write(UC_ARM_REG_SP,sp+36)
            m.u.reg_write(UC_ARM_REG_PC,m.read(sp+32))
        def window(*args):
            assert m.u.reg_read(UC_ARM_REG_SP)==SP
            assert m.u.reg_read(UC_ARM_REG_R12)==SP
            m.original.append(args);m.ret(0x8910)
        def create(a,b,c,d):
            assert [a,b,c,d]==[0,0,worker,0]
            assert m.u.reg_read(UC_ARM_REG_SP)%8==0
            m.threads.append(c);return 9
        def load(p,*_):m.loads.append(m.wide(p));return 12
        start=MEM+0x900;mirror=MEM+0x904
        def start_call(*_):m.ret(mirror)
        def mirror_call(*args):
            m.mirrored.append(args)
            for r in [UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R12]:m.u.reg_write(r,0xbad)
            m.ret(getattr(m,"consume",0))
        def proc(handle,name,*_):assert handle==12 and m.wide(name)=='MrnBridgeStart';return start
        m.points={BAND+4+slide:original,WINDOW+4+slide:window,start:start_call,mirror:mirror_call}
        m.callbacks={'GetTickCount':lambda *_:m.now,'CreateThread':create,'CloseHandle':lambda h,*_:m.closed.append(h) or 1,
                     'LoadLibraryW':load,'GetProcAddressW':proc}
        return m
    def test_band_and_optional_dll(self):
        m=self.machine()
        for key in range(0x4a):
            if key!=3:self.assertEqual(m.call(BAND,key,0),0x4567)
        self.assertFalse(m.threads)
        self.assertEqual(m.call(BAND,3,0),0);self.assertEqual(len(m.threads),1)
        m.call(BAND,3,0);self.assertEqual(len(m.threads),1)
        m.call(m.worker);self.assertEqual(m.read(m.state),0)
        self.assertEqual(m.loads,[r'\NAND_USER\WinXPlay\WinXPlayBridge.dll'])
        m.now+=500;m.call(BAND,3,0);m.call(m.worker);self.assertEqual(len(m.loads),1)
        m=self.machine();m.callbacks['LoadLibraryW']=lambda *_:0
        m.call(BAND,3,0);m.call(m.worker);self.assertEqual(m.read(m.state+16),0)
        self.assertEqual(m.call(WINDOW,77,0x201,1,123),0x8910);self.assertFalse(m.mirrored)
    def test_window_original_arguments_and_relocation(self):
        for slide in (0,0x1000000):
            m=self.machine(slide);m.call(BAND+slide,3,0);m.call(m.worker)
            for msg in (0x201,0x200,0x202,0xf,0x8001,0x8002):
                values=[77,msg,1,0x123456]
                self.assertEqual(m.call(WINDOW+slide,*values),0x8910)
                self.assertEqual(m.original[-1],tuple(values));self.assertEqual(m.mirrored[-1],tuple(values))
                self.assertEqual([m.u.reg_read(r) for r in REGS[4:12]],[0xa000+i for i in range(4,12)])
    def test_worker_creation_and_missing_export_recover(self):
        m=self.machine();m.callbacks['CreateThread']=lambda *_:0
        m.call(BAND,3,0);self.assertEqual(m.read(m.state),0)
        m=self.machine();m.callbacks['GetProcAddressW']=lambda *_:0
        m.call(BAND,3,0);m.call(m.worker)
        self.assertEqual(m.read(m.state),0);self.assertEqual(m.read(m.state+16),0)
    def test_exact_rollback_and_only_two_code_edits(self):
        f=json.loads((OUT/'patch-manifest.json').read_text());data=(OUT/'ENTHMI2.WINXPLAY.EXE').read_bytes()
        old=SOURCE.read_bytes();self.assertEqual(restore(data,f),old)
        pe=pefile.PE(data=old);patched=pefile.PE(data=data)
        for sec in pe.sections:
            if not sec.Characteristics&0x20000000:continue
            a=sec.PointerToRawData;b=a+sec.SizeOfRawData
            changed={i for i in range(a,b) if old[i]!=data[i]}
            allowed={pe.get_offset_from_rva(addr-BASE)+i for addr in (BAND,WINDOW) for i in range(4)}
            self.assertTrue(changed<=allowed)
        self.assertEqual(patched.OPTIONAL_HEADER.CheckSum,patched.generate_checksum())
        for a,b in zip(pe.DIRECTORY_ENTRY_IMPORT,patched.DIRECTORY_ENTRY_IMPORT):self.assertEqual(a.struct.__pack__(),b.struct.__pack__())

    def test_consumed_message_skips_original_and_keeps_registers(self):
        m=self.machine();m.call(BAND,3,0);m.call(m.worker);m.consume=1
        before=len(m.original)
        self.assertEqual(m.call(WINDOW,77,0x201,1,123),0)
        self.assertEqual(len(m.original),before)
        self.assertEqual([m.u.reg_read(r) for r in REGS[4:12]],[0xa000+i for i in range(4,12)])
    def test_band_failure_restores_original_arguments(self):
        m=self.machine();m.callbacks['CreateThread']=lambda *_:0
        self.assertEqual(m.call(BAND,3,0,123,456),0x4567)
        self.assertEqual(m.original[-1],(3,0,123,456))
        m=self.machine();m.callbacks['LoadLibraryW']=lambda *_:0
        m.call(BAND,3,0);m.call(m.worker);m.now+=500
        self.assertEqual(m.call(BAND,3,0,123,456),0x4567)
        self.assertEqual(m.original[-1],(3,0,123,456))


ATTACH=0x8518;REGISTER=0x8510;ACK=0x8511;READY=0x8512;ACTIVE=0x8513
HEARTBEAT=0x8514;PONG=0x8515;STOPMSG=0x8516;BYE=0x8517;ACTIVE_ACK=0x8519;RETURN=0x851a
DOWN=0x8520;MOVE=0x8521;UP=0x8522;KEY=0x8523

def common(m):
    m.windows={101:55,303:55};m.titles={303:'DPCA_HMI_WINEA7:0',101:'WinXPlayBridgeV2'}
    m.postOk=True;m.alive=True;m.workers=[];m.shows=[];m.created=[]
    m.hit=0;m.foreground=303;m.capture=0;m.parents={};m.injected=[];m.injectOk=True;m.disabled=set();m.invisible=set()
    def exchange(p,v,*_):old=m.read(p);m.write(p,v);return old
    def compare(p,v,c,*_):
        old=m.read(p)
        if old==c:m.write(p,v)
        return old
    def increment(p,*_):v=m.read(p)+1;m.write(p,v);return v
    def decrement(p,*_):v=m.read(p)-1;m.write(p,v);return v
    def pid(h,p,*_):m.write(p,m.windows.get(h,0));return int(h in m.windows)
    def title(h,p,n,*_):s=m.titles.get(h,'')[:n-1];m.u.mem_write(p,(s+'\0').encode('utf-16le'));return len(s)
    def find(c,t,*_):
        name=m.wide(c or t)
        return {'DPCA_HMI_WINEA7:0':303,'WinXPlayBridgeV2':101,'WinXPlay':202 if 202 in m.windows else 0}.get(name,0)
    def post(*args):m.messages.append(args);return int(m.postOk)
    def create(path,args,*_):
        assert m.wide(path)==r'\NAND_USER\WinXPlay\WinXPlay.exe' and args==0
        m.created.append(path)
        if not getattr(m,'processOk',True):return 0
        pi=m.read(m.u.reg_read(UC_ARM_REG_SP)+20)
        for i,v in enumerate([500,501,66,67]):m.write(pi+i*4,v)
        m.windows[202]=66;return 1
    def halt(*_):m.u.reg_write(UC_ARM_REG_PC,STOP);return None
    def inject(n,p,size,*_):
        assert n==1 and size==28
        m.injected.append(struct.unpack('<7I',bytes(m.u.mem_read(p,size))))
        return int(m.injectOk)
    def foreground(h,*_):m.foreground=h;return 1
    m.callbacks={'InterlockedExchange':exchange,'InterlockedCompareExchange':compare,
        'GetModuleHandleW':lambda *_:0,
        'InterlockedIncrement':increment,'InterlockedDecrement':decrement,'GetTickCount':lambda *_:m.now,
        'CreateThread':lambda a,b,c,d:m.workers.append(c) or 99,'CloseHandle':lambda *_:1,
        'CreateFileW':lambda *_:0xffffffff,'GetLastError':lambda *_:3,'FlushFileBuffers':lambda *_:1,
        'RegisterClassW':lambda *_:1,'CreateWindowExW':lambda *_:101,'SetTimer':lambda *_:73,
        'KillTimer':lambda *_:1,'GetMessageW':halt,'CreateProcessW':create,'FindWindowW':find,
        'IsWindow':lambda h,*_:int(h in m.windows),'GetWindowThreadProcessId':pid,'GetWindowTextW':title,
        'PostMessageW':post,'OpenProcess':lambda *_:500,'WaitForSingleObject':lambda *_:getattr(m,'waitResult',258 if m.alive else 0),
        'GetExitCodeProcess':lambda h,p,*_:m.write(p,0xc0000005) or 1,
        'DefWindowProcW':lambda *_:0,'ShowWindow':lambda h,n,*_:m.shows.append((h,n)) or 1,
        'SetForegroundWindow':foreground,'SetWindowPos':lambda *_:1,'InvalidateRect':lambda *_:1,
        'WindowFromPoint':lambda *_:m.hit,'GetForegroundWindow':lambda *_:m.foreground,
        'GetCapture':lambda *_:m.capture,'GetParent':lambda h,*_:m.parents.get(h,0),
        'GetWindowLongW':lambda h,*_:0x40000000 if h in m.parents else 0,
        'IsWindowVisible':lambda h,*_:int(h in m.windows and h not in m.invisible),
        'IsWindowEnabled':lambda h,*_:int(h in m.windows and h not in m.disabled),
        'GetSystemMetrics':lambda n,*_:800 if n==0 else 480,'SendInput':inject,
        '__rt_sdiv':lambda divisor,dividend,*_:dividend//divisor,
        'ClientToScreen':lambda *_:1,'ScreenToClient':lambda *_:1,
        'memset':lambda p,v,n,*_:m.u.mem_write(p,bytes([v])*n) or p,
        'xxx_wsprintfW':lambda *_:0,'wsprintfW':lambda *_:0}
    return m

class BridgeTests(unittest.TestCase):
    def test_steering_track_signed_steps_and_oem_passthrough(self):
        m=self.server()
        self.assertEqual(m.call('MrnOnMessage',303,0x8000,0x36,1),0)
        self.ready(m)
        for delta,count,vk in ((1,1,0xb0),(2,2,0xb0),(-1,1,0xb1),(-3,3,0xb1),
                               (0,0,0),(32767,16,0xb0),(-32768,16,0xb1)):
            m.messages.clear()
            self.assertEqual(m.call('MrnOnMessage',303,0x8000,0x36,delta & 0xffffffff),1)
            self.drain(m)
            self.assertEqual([v[1:] for v in m.messages if v[0]==202],
                             [(0x100,vk,0),(0x101,vk,0)]*count)
        # Low 16 bits follow the OEM SXTH behavior even without sign extension.
        m.messages.clear();m.call('MrnOnMessage',303,0x8000,0x36,0xffff);self.drain(m)
        self.assertEqual([v[1:] for v in m.messages if v[0]==202],[(0x100,0xb1,0),(0x101,0xb1,0)])
        m.call('MrnBridgeStart');self.tick(m);m.messages.clear()
        self.assertEqual(m.call('MrnOnMessage',303,0x8000,0x36,1),0)
        self.drain(m);self.assertFalse(any(v[0]==202 for v in m.messages))

    def test_unmapped_raw_inputs_are_traced_without_being_consumed(self):
        m=self.server();self.ready(m)
        for raw in (0x32,0x33,0x35,0x37,0x2b,0x49,0x99):
            m.messages.clear()
            self.assertEqual(m.call('MrnOnMessage',303,0x8000,raw,0xffffffff),0)
            self.drain(m)
            self.assertFalse(any(v[0]==202 for v in m.messages))
            self.assertEqual((101,0x852b,raw,0xffffffff) in m.messages,not NO_LOGS)
        m.messages.clear()
        self.assertEqual(m.call('MrnOnMessage',101,0x8000,0x36,1),0)
        self.assertFalse(m.messages)

    def test_track_steps_stop_after_post_failure(self):
        m=self.server();self.ready(m);m.messages.clear();m.postOk=False
        self.assertEqual(m.call('MrnOnMessage',303,0x8000,0x36,3),1)
        m.postOk=True;self.drain(m)
        self.assertFalse(any(v[0]==202 for v in m.messages))

    def test_vehicle_audio_scroll_steps_in_carplay_and_factory(self):
        # First five RAW_STEP messages from vehicle FILEOPS_0002/A_BRIDGE.log:
        # 135655/+1, 147804/+1, 149935/+1, 151384/-1, 151930/-1, raw ID 52.
        m=self.server()
        for delta in (1,-1):
            self.assertEqual(m.call('MrnOnMessage',303,0x8000,0x34,delta & 0xffffffff),0)
        self.ready(m)
        for delta in (1,1,1,-1,-1,0,2,-2):
            m.messages.clear()
            self.assertEqual(m.call('MrnOnMessage',303,0x8000,0x34,delta & 0xffffffff),1)
            self.drain(m)
            vk=0xb0 if delta>0 else 0xb1
            self.assertEqual([v[1:] for v in m.messages if v[0]==202],
                             [(0x100,vk,0),(0x101,vk,0)]*abs(delta))
        m.call('MrnBridgeStart');self.tick(m);m.messages.clear()
        for delta in (1,-1):
            self.assertEqual(m.call('MrnOnMessage',303,0x8000,0x34,delta & 0xffffffff),0)
        self.drain(m);self.assertFalse(any(v[0]==202 for v in m.messages))

    def test_raw_mode_and_media_keys_only_in_carplay(self):
        m=self.server()
        for raw in (1,0x27,0x10,0x11,0x1e,0x1f):
            self.assertEqual(m.call('MrnOnMessage',303,0x8000,raw,0),0)
        self.ready(m)
        for raw,vk in ((1,0x24),(0x27,0x24),(0x11,0xb0),(0x1e,0xb0),(0x10,0xb1),(0x1f,0xb1)):
            m.messages.clear();self.assertEqual(m.call('MrnOnMessage',303,0x8000,raw,0),1);self.drain(m)
            self.assertEqual([v[1:] for v in m.messages if v[0]==202],[(0x100,vk,0),(0x101,vk,0)])
            for action in (1,2,3,4):
                m.messages.clear();self.assertEqual(m.call('MrnOnMessage',303,0x8000,raw,action),1);self.drain(m)
                self.assertFalse(any(v[0]==202 for v in m.messages))
        for raw in (3,0x14,0x1c,0x1d,0x2b):
            self.assertEqual(m.call('MrnOnMessage',303,0x8000,raw,0),0)

    def server(self):
        m=common(Machine(OUT/'WinXPlayBridge.dll'))
        m.callbacks['SendMessageTimeout']=lambda *_:int(getattr(m,'responsive',True))
        def show(h,n,*_):
            m.shows.append((h,n))
            if n:m.invisible.discard(h)
            else:m.invisible.add(h)
            return 1
        m.callbacks['ShowWindow']=show
        m.call('MrnBridgeStart');m.call(m.workers[0]);return m
    def send(self,m,msg,wp=0,lp=0):return m.call('MrnControlMessage',101,msg,wp,lp)
    def tick(self,m):self.send(m,0x113,73)
    def ready(self,m):
        self.tick(m)
        return [v for v in m.messages if v[1]==0x851b][-1][2]
    def drain(self,m):
        pending=[v for v in m.messages if v[0]==101 and DOWN<=v[1]<=KEY+1]
        m.messages=[v for v in m.messages if v not in pending]
        for _,msg,wp,lp in pending:self.send(m,msg,wp,lp)
    def test_standard_mouse_and_key_messages_with_coordinate_translation(self):
        m=self.server();self.ready(m);m.messages.clear()
        def convert(h,p,*_):
            self.assertEqual(h,202);m.write(p,m.read(p)-10);m.write(p+4,m.read(p+4)-20);return 1
        m.callbacks['ScreenToClient']=convert
        for msg,wp in [(0x201,1),(0x200,1),(0x202,0)]:
            self.assertEqual(m.call('MrnOnMessage',303,msg,wp,(60<<16)|80),1)
        for msg,lp in [(0x100,1),(0x100,0x40000001),(0x101,0xc0000001)]:
            self.assertEqual(m.call('MrnOnMessage',303,msg,13,lp),1)
        self.drain(m)
        events=[v for v in m.messages if v[0]==202]
        self.assertEqual(events,[(202,0x201,1,(40<<16)|70),(202,0x200,1,(40<<16)|70),
            (202,0x202,0,(40<<16)|70),(202,0x100,13,1),(202,0x100,13,0x40000001),(202,0x101,13,0xc0000001)])
        self.assertTrue(all(v[1]<0x400 for v in events))
    def test_band_return_and_reentry_reuse_process_without_private_handshake(self):
        m=self.server();self.ready(m);self.assertEqual(len(m.created),1)
        m.call('MrnBridgeStart');self.tick(m)
        self.assertIn((202,0),m.shows);self.assertIn((202,0x1f,0,0),m.messages)
        self.assertTrue(any(v[1]==0x851c for v in m.messages))
        m.call('MrnBridgeStart');self.tick(m);self.assertEqual(len(m.created),1)
        self.assertNotIn(202,m.invisible)
        self.assertFalse(any(v[0]==202 and v[1]>=0x400 for v in m.messages))
    def test_hidden_or_exited_target_restores_factory_but_failed_wait_does_not(self):
        for kind in ('hidden','exit','failed_wait','wrong_pid'):
            m=self.server();self.ready(m);m.messages.clear()
            if kind=='hidden':m.invisible.add(202)
            if kind=='exit':m.alive=False
            if kind=='failed_wait':m.waitResult=0xffffffff
            if kind=='wrong_pid':m.windows[202]=999
            self.tick(m)
            self.assertEqual(any(v[1]==0x851c for v in m.messages),kind!='failed_wait')
            if kind=='wrong_pid':self.assertFalse(any(v[0]==202 for v in m.messages))
    def test_oem_gesture_finishes_before_activation_and_return_quarantines_remainder(self):
        m=self.server()
        self.assertEqual(m.call('MrnOnMessage',303,0x201,1,0),0)
        self.tick(m);self.assertFalse(any(v[1]==0x851b for v in m.messages))
        self.assertEqual(m.call('MrnOnMessage',303,0x202,0,0),0)
        self.tick(m);self.assertTrue(any(v[1]==0x851b for v in m.messages))
        self.assertEqual(m.call('MrnOnMessage',303,0x201,1,0),1);self.drain(m)
        m.call('MrnBridgeStart');self.tick(m);m.messages.clear()
        self.assertEqual(m.call('MrnOnMessage',303,0x202,0,0),1)
        self.drain(m);self.assertFalse(any(v[0]==202 for v in m.messages))
        self.assertEqual(m.call('MrnOnMessage',303,0x201,1,0),0)
    def test_stale_worker_input_is_rejected_after_new_band_session(self):
        m=self.server();old=self.ready(m)
        m.call('MrnBridgeStart');self.tick(m);m.call('MrnBridgeStart');self.tick(m);m.messages.clear()
        self.send(m,DOWN,(old<<16)|1,0)
        self.assertFalse(m.messages)
    def test_unresponsive_target_disables_input_without_disconnect_or_switch(self):
        m=self.server();self.ready(m);m.messages.clear();m.responsive=False;m.now+=2000;self.tick(m)
        self.assertIn((202,0x1f,0,0),m.messages)
        self.assertFalse(any(v[1]==0x851c for v in m.messages))
        m.messages.clear();self.assertEqual(m.call('MrnOnMessage',303,0x201,1,0),1)
        self.drain(m);self.assertFalse(any(v[0]==202 for v in m.messages))
    def test_failed_forwarding_cancels_partial_input(self):
        m=self.server();self.ready(m);m.messages.clear()
        m.call('MrnOnMessage',303,0x201,1,0);m.postOk=False;self.drain(m)
        self.assertIn((202,0x1f,0,0),m.messages)
    def test_short_probe_misses_preserve_the_entire_click(self):
        m=self.server();self.ready(m);m.messages.clear()
        m.call('MrnOnMessage',303,0x201,1,123);self.drain(m)
        m.responsive=False
        for _ in range(4):m.now+=250;self.tick(m)
        m.call('MrnOnMessage',303,0x202,0,123);self.drain(m)
        m.responsive=True;self.tick(m)
        self.assertEqual([v[1] for v in m.messages if v[0]==202],[0x201,0x202])
    def test_sustained_timeout_recovers_without_cancel_storm_or_band(self):
        m=self.server();old=self.ready(m);m.messages.clear()
        m.responsive=False;m.now+=2000
        for _ in range(4):self.tick(m)
        self.assertEqual(m.messages.count((202,0x1f,0,0)),1)
        m.responsive=True;self.tick(m);m.messages.clear()
        self.send(m,DOWN,(old<<16)|1,0) # queued before the failure
        self.assertFalse(any(v[0]==202 for v in m.messages))
        for msg,wp in [(0x201,1),(0x202,0)]:m.call('MrnOnMessage',303,msg,wp,123)
        self.drain(m)
        self.assertEqual([v[1] for v in m.messages if v[0]==202],[0x201,0x202])
        self.assertEqual(len(m.created),1)
    def test_recovery_waits_for_held_touch_and_key_release(self):
        m=self.server();self.ready(m);m.messages.clear()
        m.call('MrnOnMessage',303,0x201,1,0);m.call('MrnOnMessage',303,0x100,13,1)
        self.drain(m);m.responsive=False;m.now+=2000;self.tick(m);m.messages.clear()
        m.responsive=True;self.tick(m)
        m.call('MrnOnMessage',303,0x200,1,12);m.call('MrnOnMessage',303,0x202,0,12)
        self.drain(m);self.tick(m)
        self.assertFalse(any(v[0]==202 for v in m.messages))
        m.call('MrnOnMessage',303,0x101,13,0xc0000001);self.drain(m);self.tick(m)
        m.messages.clear()
        for msg,wp in [(0x201,1),(0x202,0)]:m.call('MrnOnMessage',303,msg,wp,25)
        self.drain(m)
        self.assertEqual([v[1] for v in m.messages if v[0]==202],[0x201,0x202])
    def test_expired_heartbeat_recovers_on_next_healthy_tick(self):
        m=self.server();self.ready(m);m.messages.clear();m.now+=2100
        m.call('MrnOnMessage',303,0x201,1,0);self.tick(m)
        m.call('MrnOnMessage',303,0x202,0,0);self.drain(m);self.tick(m)
        self.assertEqual(m.messages.count((202,0x1f,0,0)),1)
        m.messages.clear()
        for msg,wp in [(0x201,1),(0x202,0)]:m.call('MrnOnMessage',303,msg,wp,25)
        self.drain(m)
        self.assertEqual([v[1] for v in m.messages if v[0]==202],[0x201,0x202])
    def test_recovery_preserves_pending_layer_handoff_and_requires_cancel_success(self):
        m=self.server();activation=self.ready(m);m.messages.clear()
        m.postOk=False;m.responsive=False;m.now+=2000;self.tick(m)
        m.responsive=True;self.tick(m)
        m.call('MrnOnMessage',303,0x201,1,0);m.call('MrnOnMessage',303,0x202,0,0)
        self.drain(m)
        self.assertFalse(any(v[0]==202 and v[1]==0x201 for v in m.messages))
        m.postOk=True;self.tick(m)
        m.call('MrnOnMessage',303,0x851b,activation,101)
        self.assertIn(303,m.invisible)
        m.messages.clear()
        for msg,wp in [(0x201,1),(0x202,0)]:m.call('MrnOnMessage',303,msg,wp,25)
        self.drain(m)
        self.assertEqual([v[1] for v in m.messages if v[0]==202],[0x201,0x202])
    def test_start_timeout_does_not_kill_child_and_other_oem_messages_pass_through(self):
        m=self.server();m.responsive=False;self.tick(m);m.now+=8001;self.tick(m)
        self.assertTrue(any(v[1]==0x851c for v in m.messages))
        for msg,wp in [(0x8001,0),(0x10,0),(0x100,0xae),(0x101,0xad)]:
            self.assertEqual(m.call('MrnOnMessage',303,msg,wp,0),0)
        self.assertNotIn('TerminateProcess',m.apis.values())
    def test_only_factory_window_is_intercepted_and_oem_mode_keeps_input(self):
        m=self.server();m.windows[404]=55;m.titles[404]='Other'
        for msg in (0x201,0x200,0x202):self.assertEqual(m.call('MrnOnMessage',404,msg,1,0),0)
        for msg in (0x201,0x200,0x202):self.assertEqual(m.call('MrnOnMessage',303,msg,1,0),0)
    def test_factory_imports_and_no_system_injection(self):
        m=self.server()
        self.assertFalse({'SendInput','mouse_event','TerminateProcess'} & set(m.apis.values()))
        for dll in m.pe.DIRECTORY_ENTRY_IMPORT:
            self.assertEqual(dll.dll.lower(),b'coredll.dll')
            for imp in dll.imports:
                self.assertIn(imp.name.decode() if imp.name else NAMES[imp.ordinal],NAMES.values())

if __name__=='__main__':
    suite=unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result=unittest.TextTestRunner(verbosity=2).run(suite)
    import hashlib
    report={'scope':'Compiled ARM hooks/standard-message bridge with OS doubles; no physical HMI or firmware deployment',
        'passed':result.wasSuccessful(),'logs_enabled':not NO_LOGS,'tests_run':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),
        'binaries':{name:hashlib.sha256(artifact_path(OUT/name).read_bytes()).hexdigest() for name in
                    ('ENTHMI2.WINXPLAY.EXE','WinXPlayBridge.dll')}}
    (REPORT_OUT/'verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    sys.exit(0 if result.wasSuccessful() else 1)
