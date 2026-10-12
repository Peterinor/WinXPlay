"""Run the compiled ARM reboot tool with CE API substitutes; never reboot the host."""
from pathlib import Path
import re
import struct
import unittest
from arm_support import Arm, ROOT, FACTORY, pefile, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC

EXE = ROOT / 'build/tools/system_ops/system_ops.exe'
CONFIG = b'SYSTEMOPS_V1\r\nREBOOT\r\n'


class Machine(Arm):
    def __init__(self, config=CONFIG, files=None, folder=r'\Hard Disk\AutoLaunch', failure=None, result=50, reset=False, states=None):
        super().__init__(EXE)
        self.folder = folder
        self.files = dict(files or {})
        if config is not None:
            self.files[folder + r'\systemops.txt'] = config
        self.failure, self.result, self.reset = failure, result, reset
        self.error = 0
        self.handles = {}
        self.closed = set()
        self.events = []
        self.reset_calls = []
        self.states = states
        self.registry_handles = {}
        self.hooks = {}
        factory = pefile.PE(str(FACTORY))
        names = {s.ordinal: s.name for s in factory.DIRECTORY_ENTRY_EXPORT.symbols}
        for module in self.pe.DIRECTORY_ENTRY_IMPORT:
            for imp in module.imports:
                address = 0x20018000 + len(self.hooks) * 4
                self.hooks[address] = (imp.name or names[imp.ordinal]).decode()
                self.u.mem_write(imp.address, struct.pack('<I', address))
        self.power_api = 0x20019000
        self.hooks[self.power_api] = 'SetSystemPowerState'
        self.u.hook_add(UC_HOOK_CODE, self.hook)

    def text(self, address):
        result = bytearray()
        while bytes(self.u.mem_read(address, 2)) != b'\0\0':
            result += self.u.mem_read(address, 2)
            address += 2
        return result.decode('utf-16le')

    def hook(self, u, address, size, data):
        name = self.hooks.get(address)
        if name is None:
            return
        args = [u.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]
        def arg(i):
            return args[i] if i < 4 else struct.unpack('<I', u.mem_read(u.reg_read(UC_ARM_REG_SP) + 4 * (i-4), 4))[0]
        a, b, c, d = args
        result = 1
        if name == 'GetTickCount':
            result = 10000
        elif name == 'GetModuleFileNameW':
            path = self.folder + r'\system_ops.exe'
            self.wide(b, path)
            result = len(path)
        elif name == 'GetModuleHandleW':
            assert self.text(a) == 'coredll.dll'
            result = 333
        elif name in ('GetProcAddressW', 'GetProcAddress'):
            assert a == 333 and self.text(b) == 'SetSystemPowerState'
            result = 0 if self.failure == 'api_missing' else self.power_api
            self.error = 127 if not result else 0
        elif name == 'GetLastError':
            result = self.error
        elif name in ('lstrlenW', 'wcslen'):
            result = len(self.text(a))
        elif name in ('lstrcpyW', 'wcscpy'):
            self.wide(a, self.text(b))
            result = a
        elif name == 'RegOpenKeyExW':
            assert c == 0 and d == 0x20019
            key = self.text(b)
            if a == 0x80000002:
                assert key == r'System\CurrentControlSet\Control\Power\State'
                result = 5 if self.failure == 'registry_open' else 2 if self.states is None else 0
                handle = 1000
            else:
                assert a == 1000 and self.registry_handles[a] == 'root'
                result = 0
                handle = 1001 + list(self.states).index(key)
            if not result:
                self.registry_handles[handle] = 'root' if a == 0x80000002 else key
                u.mem_write(arg(4), struct.pack('<I', handle))
        elif name == 'RegEnumKeyExW':
            assert a == 1000
            if self.failure == 'registry_enum':
                result = 5
            elif b >= len(self.states):
                result = 259
            else:
                key = list(self.states)[b]
                self.wide(c, key)
                u.mem_write(d, struct.pack('<I', len(key)))
                result = 0
        elif name == 'RegQueryValueExW':
            assert self.text(b) == 'Flags' and c == 0
            value = self.states[self.registry_handles[a]]
            if value is None:
                result = 2
            elif self.failure == 'registry_query':
                result = 5
            else:
                u.mem_write(d, struct.pack('<I', 1 if self.failure == 'registry_type' else 4))
                u.mem_write(arg(4), struct.pack('<I', value))
                u.mem_write(arg(5), struct.pack('<I', 4))
                result = 0
        elif name == 'RegCloseKey':
            del self.registry_handles[a]
            result = 0
        elif name == 'wsprintfW':
            index = 2
            def substitute(match):
                nonlocal index
                value = arg(index)
                index += 1
                return self.text(value) if match[0] == '%s' else (f'{value:04d}' if match[0] == '%04lu' else str(value))
            value = re.sub(r'%04lu|%lu|%s', substitute, self.text(b))
            self.wide(a, value)
            result = len(value)
        elif name == 'CreateFileW':
            path, disposition = self.text(a), arg(4)
            marker = path.endswith('.once')
            self.events.append(('open', path, disposition))
            if marker and self.failure == 'marker_create':
                result, self.error = 0xffffffff, 5
            elif disposition == 1 and path in self.files:
                result, self.error = 0xffffffff, 80
            elif disposition == 3 and path not in self.files:
                result, self.error = 0xffffffff, 2
            else:
                if disposition == 1:
                    self.files[path] = b''
                result = 100 + len(self.handles)
                self.handles[result] = [path, 0]
        elif name == 'GetFileSize':
            u.mem_write(b, struct.pack('<I', 0))
            result = len(self.files[self.handles[a][0]])
        elif name == 'ReadFile':
            path, offset = self.handles[a]
            blob = self.files[path][offset:offset+c]
            if self.failure == 'config_read':
                blob, result = b'', 0
            if blob:
                u.mem_write(b, blob)
            u.mem_write(d, struct.pack('<I', len(blob)))
            self.handles[a][1] += len(blob)
        elif name == 'WriteFile':
            path, offset = self.handles[a]
            kind = 'marker' if path.endswith('.once') else 'log'
            blob = bytes(u.mem_read(b, c))
            if self.failure == kind + '_write':
                blob, result, self.error = b'', 0, 112
            self.files[path] = self.files[path][:offset] + blob + self.files[path][offset+len(blob):]
            self.handles[a][1] += len(blob)
            u.mem_write(d, struct.pack('<I', len(blob)))
            self.events.append(('write', path))
        elif name in ('FlushFileBuffers', 'CloseHandle'):
            path = self.handles[a][0]
            kind = 'marker' if path.endswith('.once') else 'config' if path.endswith('systemops.txt') else 'log'
            action = 'flush' if name == 'FlushFileBuffers' else 'close'
            self.events.append((action, path))
            result = int(self.failure != kind + '_' + action)
            if result and action == 'close':
                self.closed.add(a)
            if not result:
                self.error = 5
        elif name == 'SetFilePointer':
            assert b == 0 and c == 0 and d == 2
            self.handles[a][1] = result = len(self.files[self.handles[a][0]])
        elif name == 'SetSystemPowerState':
            self.reset_calls.append((self.text(a) if a else 0, b, c))
            self.events.append(('reset',))
            assert set(self.handles) == self.closed, 'All tool file handles must be closed before reset'
            assert not self.registry_handles
            assert c == 0, 'Do not force power transition'
            if a:
                assert b == 0 and self.states[self.text(a)] == 0x800000
            else:
                assert b == 0x800000
            if self.reset:
                u.emu_stop()
                return
            result = self.result
        else:
            raise AssertionError('Unexpected API: ' + name)
        u.reg_write(UC_ARM_REG_R0, result)
        u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))

    def run(self):
        self.u.reg_write(UC_ARM_REG_SP, 0x2001f000)
        self.u.reg_write(UC_ARM_REG_LR, 0x20000000)
        self.u.emu_start(self.exports['RunSystemOps'], 0x20000000, count=1000000)
        if self.reset and self.reset_calls:
            return None
        assert self.u.reg_read(UC_ARM_REG_PC) == 0x20000000, 'Tool did not finish'
        return self.u.reg_read(UC_ARM_REG_R0)

    def log(self):
        return '\n'.join(v.decode('utf-16') for k, v in self.files.items() if re.search(r'system_ops_\d+\.txt$', k) and v)


class SystemOpsTests(unittest.TestCase):
    def test_shipped_configuration_requests_one_standard_reset(self):
        m = Machine(config=(ROOT / 'tools/system_ops/systemops.txt').read_bytes())
        self.assertEqual(m.run(), 23)
        self.assertEqual(m.reset_calls, [(0, 0x800000, 0)])

    def test_config_validates_complete_recipe_before_reboot(self):
        for text in (b'', b'REBOOT', b'SYSTEMOPS_V1\n', CONFIG + b'REBOOT\n', CONFIG + b'UNKNOWN\n',
                     b'SYSTEMOPS_V1\nREBOOT|10\n', CONFIG + b'\0', b'x' * 4097):
            m = Machine(config=text)
            self.assertEqual(m.run(), 18, text[:60])
            self.assertEqual(m.reset_calls, [])
            self.assertFalse(any(k.endswith('.once') for k in m.files))
        m = Machine(config=b'\xef\xbb\xbf# header\r\nSYSTEMOPS_V1\n\t REBOOT \t\n# end\n')
        self.assertEqual(m.run(), 23)
        self.assertEqual(len(m.reset_calls), 1)

    def test_missing_config_api_and_read_failure_never_attempt_reset(self):
        for config, failure, code in [(None, None, 18), (CONFIG, 'api_missing', 20), (CONFIG, 'config_read', 18)]:
            m = Machine(config=config, failure=failure)
            self.assertEqual(m.run(), code)
            self.assertEqual(m.reset_calls, [])
            self.assertFalse(any(k.endswith('.once') for k in m.files))

    def test_marker_and_log_are_flushed_and_closed_before_standard_request(self):
        m = Machine(reset=True)
        self.assertIsNone(m.run())
        self.assertEqual(m.reset_calls, [(0, 0x800000, 0)])
        marker = m.folder + r'\system_ops.reboot.once'
        self.assertIn(('flush', marker), m.events)
        self.assertIn(('close', marker), m.events)
        self.assertIn('REQUEST_SetSystemPowerState_POWER_STATE_RESET', m.log())
        self.assertNotIn('CONFIRMED', m.log())

    def test_marker_blocks_request_after_restart_and_rearming_is_explicit(self):
        first = Machine(reset=True)
        first.run()
        second = Machine(files=first.files)
        self.assertEqual(second.run(), 0)
        self.assertEqual(second.reset_calls, [])
        self.assertIn('REBOOT_SKIPPED_ONCE_MARKER_EXISTS', second.log())
        self.assertIn(second.folder + r'\system_ops_0002.txt', second.files)
        del second.files[second.folder + r'\system_ops.reboot.once']
        third = Machine(files=second.files)
        self.assertEqual(third.run(), 23)
        self.assertEqual(len(third.reset_calls), 1)

    def test_failed_or_zero_return_does_not_claim_reboot_or_retry(self):
        for result, code, marker in [(5, 23, 'RESET_API_RETURNED_ERROR value=5'),
                                     (0, 24, 'RESET_API_RETURNED_SUCCESS_REBOOT_NOT_CONFIRMED')]:
            m = Machine(result=result)
            self.assertEqual(m.run(), code)
            self.assertIn(marker, m.log())
            again = Machine(files=m.files)
            self.assertEqual(again.run(), 0)
            self.assertEqual(again.reset_calls, [])

    def test_marker_or_log_failure_prevents_request(self):
        for failure in ('marker_create', 'marker_write', 'marker_flush', 'marker_close',
                        'log_write', 'log_flush', 'log_close'):
            m = Machine(failure=failure)
            self.assertNotEqual(m.run(), 0, failure)
            self.assertEqual(m.reset_calls, [], failure)

    def test_even_empty_existing_marker_blocks_repeat(self):
        m = Machine(files={r'\Hard Disk\AutoLaunch\system_ops.reboot.once': b''})
        self.assertEqual(m.run(), 0)
        self.assertEqual(m.reset_calls, [])

    def test_portable_paths_including_production_and_internal_directory(self):
        for folder in (r'\Hard Disk\AUTOLAUNCH_PROD', r'\NAND_USER\My Tools'):
            m = Machine(folder=folder)
            self.assertEqual(m.run(), 23)
            self.assertIn(folder + r'\system_ops.reboot.once', m.files)
            self.assertTrue(all(path.startswith(folder + '\\') for path in m.files))

    def test_factory_exports_cover_imports_and_standard_power_api(self):
        core = pefile.PE(str(FACTORY))
        by_ordinal = {s.ordinal: s.name for s in core.DIRECTORY_ENTRY_EXPORT.symbols}
        by_name = set(by_ordinal.values())
        self.assertIn(b'SetSystemPowerState', by_name)
        image = pefile.PE(str(EXE))
        self.assertEqual(image.FILE_HEADER.Machine, 0x1c2)
        self.assertEqual(image.OPTIONAL_HEADER.Subsystem, 9)
        for module in image.DIRECTORY_ENTRY_IMPORT:
            self.assertEqual(module.dll.lower(), b'coredll.dll')
            for imp in module.imports:
                self.assertTrue(imp.name in by_name if imp.name else imp.ordinal in by_ordinal)

    def test_named_reset_is_discovered_without_hardcoding_platform_name(self):
        for name in ('LocalReboot', 'reset', 'Another Reset State', 'R' * 255):
            m = Machine(states={'On': 0x10000, name: 0x800000, 'Unclassified': None})
            self.assertEqual(m.run(), 23)
            self.assertEqual(m.reset_calls, [(name, 0, 0)])
            self.assertIn('RESET_REQUEST_USES_REGISTERED_NAME', m.log())
            self.assertIn(name, m.log())

    def test_registry_failure_or_ambiguous_reset_does_not_guess(self):
        for failure in ('registry_open', 'registry_enum', 'registry_query', 'registry_type'):
            m = Machine(states={'reset': 0x800000}, failure=failure)
            self.assertEqual(m.run(), 25, failure)
            self.assertEqual(m.reset_calls, [])
        m = Machine(states={'warm': 0x800000, 'cold': 0x800000})
        self.assertEqual(m.run(), 25)
        self.assertEqual(m.reset_calls, [])
        self.assertIn('RESET_STATE_AMBIGUOUS_NO_REBOOT', m.log())

    def test_no_registered_reset_retains_standard_numeric_hint(self):
        m = Machine(states={'on': 0x10000})
        self.assertEqual(m.run(), 23)
        self.assertEqual(m.reset_calls, [(0, 0x800000, 0)])




if __name__ == '__main__':
    unittest.main()
