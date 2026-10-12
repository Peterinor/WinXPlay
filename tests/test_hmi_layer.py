"""Execute the new ARM adapter and original F3.21 setter/commit, with OS doubles.
No physical overlay is emulated. The observer test supplies listener state.
"""
import struct,unittest
from test_ab_interaction import Machine,common,OUT,SOURCE,BASE,MEM,pefile

class LayerTests(unittest.TestCase):
    def machine(self,slide=0):
        m=common(Machine(OUT/'WinXPlayBridge.dll'))
        p=pefile.PE(str(SOURCE));base=BASE+slide
        if slide:p.relocate_image(base)
        m.u.mem_map(base,p.OPTIONAL_HEADER.SizeOfImage)
        m.u.mem_write(base,p.get_memory_mapped_image())
        m.manager=MEM+0x4000;m.shared=MEM+0x5000;m.report=MEM+0x7000
        m.write(0xffffc808,1)
        m.callbacks['GetModuleHandleW']=lambda *_:0xdeadbeef
        def module_info(process,module,out,size):
            self.assertEqual(module,0xdeadbeef);self.assertEqual(size,12)
            m.write(out,base);m.write(out+4,p.OPTIONAL_HEADER.SizeOfImage);m.write(out+8,base+0x1000);return 1
        m.callbacks['GetModuleInformation']=module_info
        m.write(0x2113368+slide,m.manager);m.write(m.manager+0xc,m.shared)
        m.write(m.manager+0x58,401);m.write(m.shared+0x8c,55);m.write(m.shared+0x1c,901)
        m.u.mem_write(m.shared+0x88,b'\1');m.u.mem_write(m.shared+0x24,b'\1')
        m.u.mem_write(m.shared+0x2c,b'\1');m.write(m.shared+0x84,0x400)
        m.write(m.shared+0x564,55);m.write(0x2143650+slide,777)
        m.write(0x21436e8+slide,0)
        m.write(0x20d63a8+slide,MEM+0x7100);m.write(MEM+0x7100,0x12345678)
        m.depth=0;m.maxDepth=0;m.events=[];m.busy=False
        def wait(h,timeout,*_):
            self.assertEqual(h,401)
            if m.busy:return 258
            m.depth+=1;m.maxDepth=max(m.depth,m.maxDepth);return 0
        def release(h,*_):
            self.assertEqual(h,401);self.assertGreater(m.depth,0);m.depth-=1;return 1
        def signal(h,op,*_):self.assertEqual((h,op),(777,3));m.events.append((h,op));return 1
        m.callbacks['WaitForSingleObject']=wait;m.callbacks['ReleaseMutex']=release
        # Only OS thunks and stack-cookie helper are replaced. The original
        # visibility setter and 32-process event-commit loop execute unchanged.
        m.points[0x1da2d4c+slide]=lambda *a:m.ret(wait(*a))
        m.points[0x1df3be8+slide]=lambda *a:m.ret(release(*a))
        m.points[0x1da2d0c+slide]=lambda *a:m.ret(signal(*a))
        m.points[0x1da2ad0+slide]=lambda *_:m.ret(1)
        return m
    def handoff(self,m,hide):
        result=m.call('MrnLayerHandoff',303,hide,m.report)
        self.assertEqual(m.depth,0)
        return result
    def test_actual_setter_commit_hide_and_restore_only_hmi(self):
        m=self.machine()
        other=bytes((i*13)&255 for i in range(0x88,0x550))
        # Preserve layer 0's ownership tail; other layer visible fields are data.
        m.u.mem_write(m.shared+0x90,other[8:])
        before=bytes(m.u.mem_read(m.shared+0x90,len(other)-8))
        self.assertEqual(self.handoff(m,1),1)
        self.assertEqual(bytes(m.u.mem_read(m.shared+0x2c,1)),b'\0')
        self.assertEqual(bytes(m.u.mem_read(m.shared+0x558,1)),b'\1')
        self.assertEqual(m.events,[(777,3)]);self.assertEqual(m.maxDepth,2)
        self.assertEqual(bytes(m.u.mem_read(m.shared+0x90,len(other)-8)),before)
        self.assertEqual(self.handoff(m,0),1)
        self.assertEqual(bytes(m.u.mem_read(m.shared+0x2c,1)),b'\1')
        self.assertEqual(len(m.events),2)
        self.assertEqual(self.handoff(m,0),0)
    def test_duplicate_hide_retains_original_state(self):
        m=self.machine();self.handoff(m,1);self.handoff(m,1);self.handoff(m,0)
        self.assertEqual(bytes(m.u.mem_read(m.shared+0x2c,1)),b'\1')
    def test_initially_hidden_layer_stays_hidden_on_return(self):
        m=self.machine();m.u.mem_write(m.shared+0x2c,b'\0')
        self.handoff(m,1);self.handoff(m,0)
        self.assertEqual(bytes(m.u.mem_read(m.shared+0x2c,1)),b'\0')
    def test_guard_rejects_unknown_firmware(self):
        m=self.machine();m.u.mem_write(0x1df4bd0,b'\0'*4)
        self.assertEqual(self.handoff(m,1),2);self.assertFalse(m.events)
    def test_ce_opaque_handle_and_unmapped_pe_headers(self):
        m=self.machine();m.u.mem_unmap(BASE,0x1000)
        self.assertEqual(self.handoff(m,1),1);self.assertEqual(self.handoff(m,0),1)
    def test_module_information_failure_does_not_touch_layers(self):
        m=self.machine();m.callbacks['GetModuleInformation']=lambda *_:0
        self.assertEqual(self.handoff(m,1),2);self.assertFalse(m.events)
    def fallback(self,m,slide=0,proc_offset=0,section_mapping=True):
        m.callbacks['GetModuleInformation']=lambda *_:0
        m.callbacks['GetLastError']=lambda *_:87
        m.callbacks['GetWindowLongW']=lambda *_:0x2042038+slide+proc_offset
        def query(address,out,size,*_):
            self.assertEqual(size,28)
            allocation=(0x2040000 if section_mapping else BASE)+slide
            m.u.mem_write(out,struct.pack('<7I',address&~4095,allocation,0x20,4096,0x1000,0x20,0x1000000))
            return 28
        m.callbacks['VirtualQuery']=query
    def test_vehicle_psapi_87_uses_window_mapping_and_keeps_code_checks(self):
        for slide in (0,0x4000000):
            m=self.machine(slide);self.fallback(m,slide)
            self.assertEqual(self.handoff(m,1),1);self.assertEqual(self.handoff(m,0),1)
            self.assertEqual(len(m.events),2)
        m=self.machine();self.fallback(m);m.u.mem_write(0x1df4bd0,b'\0'*4)
        self.assertEqual(self.handoff(m,1),2);self.assertFalse(m.events)
    def test_window_mapping_fallback_rejects_proxy_and_failed_query(self):
        m=self.machine();self.fallback(m,proc_offset=4)
        self.assertEqual(self.handoff(m,1),2);self.assertFalse(m.events)
        m=self.machine();self.fallback(m);m.callbacks['VirtualQuery']=lambda *_:0
        self.assertEqual(self.handoff(m,1),2);self.assertFalse(m.events)
    def test_window_mapping_fallback_observer_and_unmapped_headers(self):
        m=self.machine();self.fallback(m);m.u.mem_unmap(BASE,4096)
        self.assertEqual(self.handoff(m,1),1)
        m.u.mem_write(m.shared+0x558,b'\0');m.write(m.shared+0x84,0x20)
        self.assertEqual(m.call('MrnLayerObserve',m.report),9)
    def test_fallback_whole_image_mapping_still_works(self):
        m=self.machine();self.fallback(m,section_mapping=False)
        self.assertEqual(self.handoff(m,1),1);self.assertEqual(self.handoff(m,0),1)
    def test_fallback_aligned_foreign_proc_fails_instruction_validation(self):
        m=self.machine();self.fallback(m,proc_offset=0x10000)
        self.assertEqual(self.handoff(m,1),2);self.assertFalse(m.events)
    def test_fallback_rejects_uncommitted_or_inaccessible_region(self):
        for state,protect in ((0x2000,0x20),(0x1000,1),(0x1000,0x120)):
            m=self.machine();self.fallback(m)
            def query(address,out,size,*_):
                m.u.mem_write(out,struct.pack('<7I',address&~4095,0x2040000,0x20,4096,state,protect,0x1000000))
                return 28
            m.callbacks['VirtualQuery']=query
            self.assertEqual(self.handoff(m,1),2);self.assertFalse(m.events)
    def test_wrong_thread_and_foreign_layer_not_modified(self):
        m=self.machine();m.write(0xffffc808,2)
        self.assertEqual(self.handoff(m,1),4);self.assertFalse(m.events)
        m.write(0xffffc808,1);m.write(m.shared+0x8c,99)
        self.assertEqual(self.handoff(m,1),6);self.assertFalse(m.events)
    def test_busy_lock_is_retryable_without_saving_state(self):
        m=self.machine();m.busy=True
        self.assertEqual(self.handoff(m,1),5);self.assertFalse(m.events)
        m.busy=False;self.assertEqual(self.handoff(m,0),0)
        self.assertEqual(self.handoff(m,1),1)
    def test_replaced_surface_is_not_restored_from_stale_snapshot(self):
        m=self.machine();self.handoff(m,1);m.write(m.shared+0x1c,902)
        self.assertEqual(self.handoff(m,0),8);self.assertEqual(len(m.events),1)
    def test_observer_distinguishes_requested_and_processed_state(self):
        m=self.machine();self.handoff(m,1)
        self.assertEqual(m.call('MrnLayerObserve',m.report),10)
        m.u.mem_write(m.shared+0x558,b'\0');m.write(m.shared+0x84,0x20)
        self.assertEqual(m.call('MrnLayerObserve',m.report),9)
        self.assertEqual(m.depth,0)
    def test_relocated_hmi_uses_relocated_instance_and_code(self):
        m=self.machine(0x4000000)
        self.assertEqual(self.handoff(m,1),1);self.assertEqual(self.handoff(m,0),1)
        self.assertEqual(len(m.events),2)

if __name__=='__main__':
    import hashlib,json,sys
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(LayerTests))
    (OUT/'layer-verification.json').write_text(json.dumps({
        'passed':result.wasSuccessful(),'tests_run':result.testsRun,
        'bridge_sha256':hashlib.sha256((OUT/'WinXPlayBridge.dll').read_bytes()).hexdigest(),
        'original_hmi_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        'physical_display_verified':False,
        'scope':'Original ARM setter/commit; OS calls and listener completion state use doubles.'
    },indent=2),encoding='utf-8')
    sys.exit(0 if result.wasSuccessful() else 1)
