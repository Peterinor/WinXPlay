"""Execute the actual ARM parser and window selection with WinCE API doubles.

This does not emulate display hardware or cross-process recovery scheduling.
"""
import itertools
import struct
import unittest
from test_diagnostics import DiagnosticMachine
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_LR, UC_ARM_REG_PC


class WindowMachine(DiagnosticMachine):
    def __init__(self, tool='window-operations', **kwargs):
        super().__init__(tool, **kwargs)
        self.windows = []
        self.pending_enum = None

    def enum_next(self, u):
        callback, context, return_address, index = self.pending_enum
        if index == len(self.windows):
            u.reg_write(UC_ARM_REG_R0, 1)
            u.reg_write(UC_ARM_REG_PC, return_address)
            self.pending_enum = None
            return
        self.pending_enum = (callback, context, return_address, index + 1)
        u.reg_write(UC_ARM_REG_R0, self.windows[index][0])
        u.reg_write(UC_ARM_REG_R1, context)
        u.reg_write(UC_ARM_REG_LR, 0x3000f000)
        u.reg_write(UC_ARM_REG_PC, callback)

    def hook(self, u, addr, size, data):
        if addr == 0x3000f000:
            self.enum_next(u)
            return
        name = self.hooks.get(addr)
        a, b, c, d = [u.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]
        if name in ('memcpy', 'memmove'):
            u.mem_write(a, bytes(u.mem_read(b, c)))
            result = a
        elif name == 'MultiByteToWideChar':
            from unicorn.arm_const import UC_ARM_REG_SP
            dest, capacity = struct.unpack('<II', u.mem_read(u.reg_read(UC_ARM_REG_SP), 8))
            try:
                value = bytes(u.mem_read(c, d)).decode('utf-8').encode('utf-16le')
                result = len(value) // 2
                if result > capacity:
                    result = 0
                else:
                    u.mem_write(dest, value)
            except UnicodeDecodeError:
                result = 0
        elif name == 'EnumWindows':
            self.calls.append((name, (a, b)))
            self.pending_enum = (a, b, u.reg_read(UC_ARM_REG_LR), 0)
            self.enum_next(u)
            return
        elif name == 'IsWindow':
            result = int(any(w[0] == a for w in self.windows))
        elif name in ('GetWindowTextW', 'GetClassNameW', 'GetWindowThreadProcessId'):
            window = next((w for w in self.windows if w[0] == a), (0, '', '', 0, 0))
            if name == 'GetWindowThreadProcessId':
                u.mem_write(b, struct.pack('<I', window[3]))
                result = window[4]
            else:
                value = window[1 if name == 'GetWindowTextW' else 2][:c-1]
                self.wide(b, value)
                result = len(value)
        else:
            return super().hook(u, addr, size, data)
        u.reg_write(UC_ARM_REG_R0, result)
        u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))


class WindowOpsTests(unittest.TestCase):
    def parse(self, text, tool='window-operations'):
        machine = WindowMachine(tool)
        machine.wide(0x20001000, text)
        machine.u.mem_write(0x20003000, b'\xa5' * 652)
        ok = machine.call('WindowOpsParseConfig', 0x20001000, 0x20003000)
        if not ok:
            self.assertEqual(bytes(machine.u.mem_read(0x20003000, 652)), b'\xa5' * 652)
        return machine, ok

    def test_configuration_bounds_unicode_and_no_partial_publication(self):
        prefix = '[window_ops]\nwindow_title=车机窗口\n'
        for tool in ('window-operations', 'window-restore-helper'):
            machine, ok = self.parse(prefix, tool)
            self.assertEqual(ok, 1)
            self.assertEqual(machine.string(0x20003000), '车机窗口')
            self.assertEqual(struct.unpack('<III', machine.u.mem_read(0x20003000+640, 12)), (10000, 5000, 0))
        for fields in ('delay_ms=0\nduration_ms=1000\nshow_test_window=1',
                       'delay_ms=60000\nduration_ms=60000\nwindow_class=Class\nprocess_name=test.exe'):
            self.assertEqual(self.parse(prefix+fields)[1], 1)
        for text in ('', '[window_ops]', '[window_ops]\nwindow_title=', '[other]\nwindow_title=x',
                     prefix+'window_title=y', prefix+'duration_ms=999', prefix+'duration_ms=60001',
                     prefix+'duration_ms=42949672950', prefix+'delay_ms=-1', prefix+'delay_ms=60001',
                     prefix+'show_test_window=2', prefix+'action=kill', prefix+'process_name=*.exe',
                     prefix+'process_name=\\Windows\\a.exe', prefix+'[window_ops]',
                     '[window_ops]\nwindow_title='+'x'*128):
            with self.subTest(text=text):
                self.assertEqual(self.parse(text)[1], 0)

    def test_unique_exact_target_class_filter_and_ambiguous_refusal(self):
        machine, ok = self.parse('[window_ops]\nwindow_title=Target\n')
        self.assertEqual(ok, 1)
        window = (51, 'Target', 'ClassA', 101, 201)
        cases = [([], 0), ([window], 51), ([(52, 'Target extra', 'ClassA', 101, 202)], 0),
                 ([(52, 'target', 'ClassA', 101, 202)], 0),
                 ([window, (52, 'Target', 'ClassB', 102, 202)], 0)]
        for windows, expected in cases:
            machine.windows = windows
            self.assertEqual(machine.call('WindowOpsFindTarget', 0x20003000), expected)
        machine, _ = self.parse('[window_ops]\nwindow_title=Target\nwindow_class=ClassB\n')
        machine.windows = [window, (52, 'Target', 'ClassB', 102, 202)]
        self.assertEqual(machine.call('WindowOpsFindTarget', 0x20003000), 52)
        machine, _ = self.parse('[window_ops]\nwindow_title=Target\nprocess_name=test.exe\n')
        machine.windows = [window]
        # Toolhelp unavailable: an unverified process filter must not be ignored.
        self.assertEqual(machine.call('WindowOpsFindTarget', 0x20003000), 0)

    def test_helper_arguments_and_display_restore_gate(self):
        machine = WindowMachine('window-restore-helper')
        for text, valid in [('1 2 3 4 1 1000', 1), ('4294967295 2 3 4 1 60000', 1),
                            ('1 2 3 4 1', 0), ('0 2 3 4 1 5000', 0),
                            ('1 2 3 4 1 999', 0), ('1 2 3 4 1 60001', 0),
                            ('1 2 3 4 1 5000 7', 0), ('4294967296 2 3 4 1 5000', 0)]:
            machine.wide(0x20001000, text)
            self.assertEqual(machine.call('WindowOpsParseArguments', 0x20001000, 0x20002000), valid, text)
        for hidden, restoring, shown in itertools.product((0, 1), repeat=3):
            self.assertEqual(machine.call('WindowOpsMayShow', hidden, restoring, shown),
                             int(hidden and not restoring and not shown))

    def test_missing_or_invalid_config_exits_without_window_operations(self):
        for config in (None, b'[window_ops]\nwindow_title=\n', b'[window_ops]\nwindow_title=x\nduration_ms=0'):
            machine = WindowMachine(config=config, path='\\Storage Card\\Tools\\window_ops.exe')
            code, log = machine.run()
            self.assertEqual(code, 30)
            self.assertIn('ABORT_INVALID_CONFIG_NO_HIDE', log)
            self.assertNotIn('CreateProcessW', [name for name, _ in machine.calls])
            self.assertNotIn('ShowWindow', [name for name, _ in machine.calls])

    def test_utf8_and_utf16_configs_reach_target_lookup_without_a_match(self):
        text = '[window_ops]\nwindow_title=窗口\ndelay_ms=0\n'
        for config in (text.encode('utf-8'), b'\xef\xbb\xbf'+text.encode('utf-8'),
                       b'\xff\xfe'+text.encode('utf-16le')):
            machine = WindowMachine(config=config)
            code, log = machine.run()
            self.assertEqual(code, 14)
            self.assertIn('ABORT_TARGET_NOT_EXACT_VISIBLE_FOREGROUND_NO_HIDE', log)
            self.assertIn('EnumWindows', [name for name, _ in machine.calls])
            self.assertNotIn('CreateProcessW', [name for name, _ in machine.calls])


if __name__ == '__main__':
    unittest.main()
