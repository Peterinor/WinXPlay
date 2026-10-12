"""Run F3.21's actual RegisterDevice kernel wrapper and Device Manager stub."""
import struct
import unittest
import arm_support as base
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_PC,UC_ARM_REG_LR

class FactoryTests(unittest.TestCase):
    def test_register_and_deregister_are_1060_stubs_but_activation_has_real_entries(self):
        folder=base.FACTORY.parent
        m=base.Arm(folder/'devmgr.dll')
        core=base.pefile.PE(str(folder/'k.coredll.dll'))
        start=core.OPTIONAL_HEADER.ImageBase
        m.u.mem_map(start,(core.OPTIONAL_HEADER.SizeOfImage+4095)&~4095)
        m.u.mem_write(start,core.get_memory_mapped_image())
        # StartDeviceManager obtains this table from its literal at 0xeff82c4c.
        table=struct.unpack('<I',m.pe.get_data(0x2c4c,4))[0]
        self.assertEqual(table,0xeff81098)
        slots=struct.unpack('<14I',m.pe.get_data(table-m.pe.OPTIONAL_HEADER.ImageBase,56))
        self.assertEqual(slots[2:4],(0xeff82d7c,0xeff82d7c))
        self.assertEqual((slots[8],slots[12]),(0xeff82de0,0xeff82d94))
        self.assertEqual(m.pe.get_data(0x2d7c,20).hex(),'04e02de5240400e3c23c00eb0000a0e304f09de4')
        m.u.mem_write(0xefe7d428,struct.pack('<I',table))
        errors=[]
        def hook(u,address,size,data):
            if address==0xeff92094: # Original SetLastError import thunk.
                errors.append(u.reg_read(UC_ARM_REG_R0))
                u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
        m.u.hook_add(UC_HOOK_CODE,hook)
        for name in ('RegisterDevice','DeregisterDevice'):
            export=next(s for s in core.DIRECTORY_ENTRY_EXPORT.symbols if s.name==name.encode())
            m.exports[name]=start+export.address
            self.assertEqual(m.call(name,0,0,0,0),0)
        self.assertEqual(errors,[1060,1060])

if __name__=='__main__':unittest.main()
