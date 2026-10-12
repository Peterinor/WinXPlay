"""Run the shipped ARM voice test with substituted WinCE calls, not a vehicle emulator."""
import re
import struct
import unittest
from arm_support import Arm, FACTORY, ROOT, pefile
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_PC, UC_ARM_REG_LR


class VoiceRun:
    def __init__(self, failure=None):
        self.arm = Arm(ROOT/'build/carplay/vehicle-tests/winxplay_test_av.exe')
        self.u = self.arm.u
        self.failure = failure
        self.tick = 1000
        self.calls = []
        self.log = bytearray()
        self.pending = []
        self.prepared = set()
        self.initial = []
        self.played = 0
        self.next_done = None
        self.started = False
        self.hooks = {}
        names = {s.ordinal: s.name for s in pefile.PE(str(FACTORY)).DIRECTORY_ENTRY_EXPORT.symbols}
        self.u.mem_map(0x30000000, 0x10000)
        for module in self.arm.pe.DIRECTORY_ENTRY_IMPORT:
            if module.dll.lower() != b'coredll.dll':
                continue
            for imp in module.imports:
                stub = 0x30000000 + 4*len(self.hooks)
                self.hooks[stub] = (imp.name or names[imp.ordinal]).decode()
                self.put(imp.address, stub)
        self.u.hook_add(UC_HOOK_CODE, self.hook, begin=0x30000000, end=0x3000ffff)

    def put(self, address, value):
        self.u.mem_write(address, struct.pack('<I', value))

    def get(self, address):
        return struct.unpack('<I', self.u.mem_read(address, 4))[0]

    def wide(self, address):
        out = bytearray()
        while True:
            pair = self.u.mem_read(address, 2)
            if pair == b'\0\0':
                return out.decode('utf-16le')
            out.extend(pair)
            address += 2

    def hook(self, u, address, size, data):
        if address not in self.hooks:
            return
        name = self.hooks[address]
        args = [u.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]
        args += list(struct.unpack('<12I', u.mem_read(u.reg_read(UC_ARM_REG_SP), 48)))
        a, b, c, d = args[:4]
        self.calls.append((name, args))
        result = 0
        if name == 'memset':
            u.mem_write(a, bytes([b & 255])*c)
            result = a
        elif name == 'GetTickCount':
            result = self.tick
        elif name == 'Sleep':
            self.tick += a
            if self.failure != 'stall' and self.next_done is not None:
                while self.pending and self.tick >= self.next_done:
                    header = self.pending.pop(0)
                    self.put(header+16, 3)  # DONE|PREPARED
                    self.played += 4800
                    self.next_done += 100
        elif name == 'GetModuleFileNameW':
            path = '\\Hard Disk\\AutoLaunch\\winxplay_test_av.exe'
            self.arm.wide(b, path)
            result = len(path)
        elif name == 'wsprintfW':
            fmt = self.wide(b)
            values = iter(args[2:])
            def replace(match):
                value = next(values)
                if match.group(2) == 's':
                    return self.wide(value)
                width = int(match.group(1) or 0)
                return str(value).zfill(width)
            text = re.sub(r'%(\d*)(?:l)?([us])', replace, fmt)
            self.arm.wide(a, text)
            result = len(text)
        elif name == 'lstrlenW':
            result = len(self.wide(a))
        elif name == 'CreateFileW':
            path = self.wide(a)
            if path == 'AVV0:':
                self.av_open = args[:7]
                result = 0xffffffff if self.failure == 'av-open' else 22
            else:
                result = 11
        elif name == 'WriteFile':
            self.log.extend(u.mem_read(b, c))
            self.put(d, c)
            result = 1
        elif name == 'FlushFileBuffers':
            result = int(not (self.failure == 'log' and b'T\x00T\x00S\x00_\x00C' in self.log))
        elif name in ('CloseHandle',):
            result = 1
        elif name == 'GetLastError':
            result = 5
        elif name == 'waveOutGetNumDevs':
            result = 1 if self.failure == 'missing-device' else 2
        elif name == 'waveOutGetDevCaps':
            self.arm.wide(b+8, 'SSI Audio')
        elif name == 'DeviceIoControl':
            assert a == 22 and b in (0x33, 0x34)
            assert d == 1 and bytes(u.mem_read(c, d)) == b'\x04'
            assert args[4:8] == [0, 0, 0, 0]
            result = int(self.failure != 'request' if b == 0x33 else self.failure != 'release')
        elif name == 'waveOutOpen':
            assert b == 1
            assert struct.unpack('<HHIIHHH', u.mem_read(c, 18)) == (1, 2, 48000, 192000, 4, 16, 0)
            assert args[3:6] == [0, 0, 0]
            self.put(a, 33)
            result = 1 if self.failure == 'wave-open' else 0
        elif name == 'waveOutGetVolume':
            self.put(b, 0x80008000)
        elif name == 'waveOutPrepareHeader':
            assert c == 32
            if self.failure == 'prepare' and len(self.prepared) == 4:
                result = 1
            else:
                self.prepared.add(b)
                self.put(b+16, 2)
        elif name == 'waveOutWrite':
            assert b in self.prepared and b not in self.pending
            assert self.get(b+4) == 19200
            if self.failure == 'write' and len(self.initial) == 4:
                result = 1
            elif self.failure == 'refill' and self.started:
                result = 1
            else:
                if not self.started:
                    self.initial.append(b)
                self.pending.append(b)
                self.put(b+16, 18)
                if self.next_done is None and len(self.pending) == 6:
                    self.next_done = self.tick+100
                if len(self.initial) == 10:
                    self.started = True
        elif name == 'waveOutGetPosition':
            self.put(b, 2)
            self.put(b+4, self.played)
        elif name == 'waveOutReset':
            for header in self.pending:
                self.put(header+16, 3)
            self.pending.clear()
        elif name == 'waveOutUnprepareHeader':
            assert b not in self.pending
            self.prepared.remove(b)
        elif name == 'waveOutClose':
            assert not self.pending and not self.prepared
        else:
            raise AssertionError('Unexpected API: '+name)
        u.reg_write(UC_ARM_REG_R0, result)
        u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))

    def run(self, delay=0, seconds=2):
        self.result = self.arm.call('VoicePcmTest', delay, seconds, instruction_limit=15000000)
        self.text = self.log.decode('utf-16le')
        return self

    def requests(self):
        return [args[1] for name, args in self.calls if name == 'DeviceIoControl']


class VoicePcmTests(unittest.TestCase):
    def test_autonomous_request_queue_completion_release(self):
        run = VoiceRun().run(delay=30)
        self.assertEqual(run.result, 0)
        self.assertEqual(run.requests(), [0x33, 0x34])
        names = [name for name, args in run.calls]
        self.assertLess(names.index('DeviceIoControl'), names.index('waveOutOpen'))
        self.assertLess(names.index('waveOutClose'), len(names)-1-names[::-1].index('DeviceIoControl'))
        writes = [i for i, name in enumerate(names) if name == 'waveOutWrite']
        self.assertEqual(writes[9]-writes[0], 9)  # no sleep/log between initial writes
        self.assertEqual(len(set(run.initial)), 10)
        buffers = [run.get(h) for h in run.initial]
        self.assertEqual(len(set(buffers)), 10)
        self.assertGreater(run.played, 48000)
        self.assertIn('DRAIN_PENDING a=0', run.text)
        self.assertIn('END_PHYSICAL_SOUND_REQUIRES_USER_CONFIRMATION a=0', run.text)
        self.assertGreaterEqual(run.tick, 34000)
        # Only blocks 0 and 2 contain a low-level tone; channels match.
        for index, pointer in enumerate(buffers):
            samples = struct.unpack('<9600h', run.u.mem_read(pointer, 19200))
            self.assertEqual(samples[::2], samples[1::2])
            self.assertLessEqual(max(map(abs, samples)), 1200)
            self.assertEqual(any(samples), index in (0, 2))

    def test_failed_acquisition_never_opens_pcm(self):
        for failure in ('missing-device', 'av-open', 'request'):
            with self.subTest(failure=failure):
                run = VoiceRun(failure).run()
                self.assertEqual(run.result, 23)
                self.assertNotIn('waveOutOpen', [n for n, a in run.calls])
                self.assertNotIn(0x34, run.requests())

    def test_failure_after_request_releases_and_cleans_up(self):
        for failure in ('wave-open', 'prepare', 'write', 'refill', 'stall', 'release', 'log'):
            with self.subTest(failure=failure):
                run = VoiceRun(failure).run()
                self.assertEqual(run.result, 22 if failure == 'log' else 23)
                self.assertEqual(run.requests(), [0x33, 0x34])
                self.assertFalse(run.pending)
                self.assertFalse(run.prepared)
                self.assertLess(run.tick, 10000)

    def test_invalid_duration_has_no_side_effect(self):
        for delay, seconds in ((121, 20), (0, 0), (0, 61)):
            run = VoiceRun().run(delay, seconds)
            self.assertEqual(run.result, 18)
            self.assertFalse(run.calls)


if __name__ == '__main__':
    unittest.main()
