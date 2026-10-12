"""Run the production ARM stream driver against controlled asynchronous USB completions."""
import struct, unittest
from test_usbmux_attach import AttachMachine
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC

class BulkMachine(AttachMachine):
    def __init__(self):
        super().__init__()
        self.u.mem_map(0x21000000,65536)
        self.issued=[];self.aborted=[];self.finished=[];self.freed=[];self.events={}
        self.transfers={};self.next_transfer=700;self.ready=False;self.status_error=0
        for slot,name in [(24,'bulk_abort_pipe'),(29,'bulk_issue'),(33,'bulk_status'),
                          (35,'bulk_abort'),(36,'bulk_close')]:self.put(self.api+slot*4,self.add(name))
        assert self.attach()==(1,1)
        self.context=self.call('CMX_Open',self.init_context,0,0)
        self.exports['notify']=self.notify_callback
    def hook(self,u,address,size,data):
        name=self.hooks.get(address)
        if not name:return
        regs=[u.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]
        def arg(i):return regs[i] if i<4 else struct.unpack('<I',u.mem_read(u.reg_read(UC_ARM_REG_SP)+(i-4)*4,4))[0]
        a,b,c,d=regs;result=1
        if name=='LocalAlloc' and b==65536:result=0x21000000
        elif name=='LocalFree':self.freed.append(a)
        elif name in ('EnterCriticalSection','LeaveCriticalSection'):pass
        elif name=='CreateEventW':result=10000+len(self.events);self.events[result]=False
        elif name=='EventModify':self.events[a]=b==3
        elif name=='ResetEvent':self.events[a]=False
        elif name=='WaitForSingleObject':result=0 if self.events[a] else 258
        elif name=='bulk_issue':
            self.next_transfer+=1;result=self.next_transfer
            record={'pipe':a,'event':c,'flags':d,'size':arg(4),'buffer':arg(5),'actual':0}
            self.transfers[result]=record;self.issued.append(result)
            if a==2:record['actual']=arg(4);self.events[c]=True
        elif name=='bulk_status':
            self.put(b,self.transfers[a]['actual']);self.put(c,self.status_error)
        elif name=='bulk_abort':self.aborted.append(a);self.events[self.transfers[a]['event']]=True
        elif name=='bulk_close':self.finished.append(a)
        elif name=='bulk_abort_pipe':pass
        else:return super().hook(u,address,size,data)
        u.reg_write(UC_ARM_REG_R0,result);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
    def read(self,size=65536):return self.call('CMX_Read',self.context,0x20010000,size)
    def complete(self,payload):
        t=self.transfers[self.issued[-1]]
        self.u.mem_write(t['buffer'],payload);t['actual']=len(payload);self.events[t['event']]=True

class BulkTests(unittest.TestCase):
    def test_idle_polls_keep_one_transfer_then_receive_delayed_reply(self):
        m=BulkMachine()
        for _ in range(8):self.assertEqual(m.read(),0xffffffff);self.assertEqual(m.error,1460)
        self.assertEqual(len(m.issued),1);self.assertEqual(m.aborted,[]);self.assertEqual(m.finished,[])
        payload=bytes.fromhex('0000000600000024faceface000000010001f27e')
        m.complete(payload);self.assertEqual(m.read(),len(payload))
        self.assertEqual(bytes(m.u.mem_read(0x20010000,len(payload))),payload)
        self.assertEqual(m.finished,m.issued);self.assertEqual(m.aborted,[])
    def test_transfer_completion_between_polls_is_not_discarded(self):
        m=BulkMachine();m.read();m.complete(b'late response')
        self.assertEqual(m.read(),13);self.assertEqual(len(m.issued),1)
        m.read();self.assertEqual(len(m.issued),2)
    def test_small_caller_buffer_preserves_remainder_without_resubmit(self):
        m=BulkMachine();m.read(3);m.complete(b'abcdefgh')
        for expected in (b'abc',b'def',b'gh'):
            self.assertEqual(m.read(3),len(expected))
            self.assertEqual(bytes(m.u.mem_read(0x20010000,len(expected))),expected)
        self.assertEqual(len(m.issued),1)
    def test_close_cancels_pending_before_freeing_buffer_and_reopen_is_clean(self):
        m=BulkMachine();m.read();m.call('CMX_Close',m.context)
        self.assertEqual(m.aborted,m.issued);self.assertEqual(m.finished,m.issued)
        self.assertIn(0x21000000,m.freed)
        self.assertEqual(m.call('CMX_Open',m.context,0,0),m.context)
        m.read();self.assertEqual(len(m.issued),2)
    def test_detach_cleans_pending_and_rejects_further_reads(self):
        m=BulkMachine();m.read();m.u.mem_write(0x2001f000,bytes(16))
        self.assertEqual(m.call('notify',m.context,1,0,0),1)
        self.assertEqual(m.finished,m.issued);self.assertIn(0x21000000,m.freed)
        self.assertEqual(m.read(),0xffffffff);self.assertEqual(len(m.issued),1)
        m.call('CMX_Close',m.context);self.assertEqual(len(m.finished),1)
    def test_completion_error_is_not_delivered_as_payload(self):
        m=BulkMachine();m.read();m.complete(b'bad');m.status_error=4
        self.assertEqual(m.read(),0xffffffff);self.assertEqual(m.error,1167)
        self.assertEqual(m.finished,m.issued)
    def test_zero_length_and_oversize_reads_do_not_submit(self):
        m=BulkMachine();self.assertEqual(m.read(0),0)
        self.assertEqual(m.read(65537),0xffffffff);self.assertEqual(m.error,87)
        self.assertEqual(m.issued,[])
    def test_short_usb_packet_and_output_count(self):
        m=BulkMachine();m.read();m.complete(bytes(36));self.assertEqual(m.read(),36)
        t=m.transfers[m.issued[0]]
        self.assertEqual(t['size'],65536);self.assertNotEqual(t['buffer'],0x20010000)
        self.assertEqual(m.call('CMX_Write',m.context,0x20010000,17),17)
        self.assertIn(b'BULK_TX requested actual error 00000011 00000011 00000000',m.log)

if __name__=='__main__':unittest.main()
