"""执行原始 F3.21 摘要函数，避免把标准 SHA-256 错当作启动器校验。"""
from pathlib import Path
from external_assets import require_oem
import hashlib
import pefile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC



def digest(data):
    """仅模拟 memcpy 和栈检查，其余摘要计算执行固件中的 ARM 指令。"""
    binary = require_oem('SSW_AUTOLAUNCHER.DLL').read_bytes()
    if hashlib.sha256(binary).hexdigest() != 'eaf53d4c3beb7d8cf65aecd671a767b035602285aef339d733103b2af3ec2ec1':
        raise ValueError('Unsupported AutoLaunch DLL; the exact MRN F3.21 digest implementation is required')
    pe = pefile.PE(data=binary)
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0x10000000, 0x10000)
    uc.mem_write(0x10000000, pe.get_memory_mapped_image())
    if len(data) > 16 * 1024 * 1024:
        raise ValueError('Digest input exceeds 16 MiB test limit')
    uc.mem_map(0x20000000, max(0x100000, (len(data) + 0x10000 + 0xfff) & ~0xfff))
    uc.mem_map(0x30000000, 0x100000)
    if data:
        uc.mem_write(0x20010000, data)

    def hook(machine, addr, size, unused):
        if addr == 0x10005090:
            dst = machine.reg_read(UC_ARM_REG_R0)
            src = machine.reg_read(UC_ARM_REG_R1)
            count = machine.reg_read(UC_ARM_REG_R2)
            if count:
                machine.mem_write(dst, bytes(machine.mem_read(src, count)))
            machine.reg_write(UC_ARM_REG_PC, machine.reg_read(UC_ARM_REG_LR))
        elif addr == 0x10004dd4:
            machine.reg_write(UC_ARM_REG_PC, machine.reg_read(UC_ARM_REG_LR))
    # Only intercept the two substituted routines, not every SHA instruction.
    uc.hook_add(UC_HOOK_CODE, hook, begin=0x10005090, end=0x10005090)
    uc.hook_add(UC_HOOK_CODE, hook, begin=0x10004dd4, end=0x10004dd4)
    def call(address, *args):
        for reg, value in zip([UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2], args):
            uc.reg_write(reg, value)
        uc.reg_write(UC_ARM_REG_SP, 0x300ff000)
        uc.reg_write(UC_ARM_REG_LR, 0x30000000)
        uc.emu_start(address, 0x30000000, timeout=30_000_000, count=50_000_000)
        if uc.reg_read(UC_ARM_REG_PC) != 0x30000000:
            raise RuntimeError('Digest emulation did not return')
    call(0x100020b8, 0x20000000)
    # The firmware update routine is incremental. Bound each emulator call so
    # full receivers, as well as small launch helpers, fit the instruction limit.
    for offset in range(0, len(data), 65536):
        call(0x1000495c, 0x20000000, 0x20010000 + offset, min(65536, len(data)-offset))
    call(0x10004a1c, 0x20000000, 0x20001000)
    return bytes(uc.mem_read(0x20001000, 32)).hex()
