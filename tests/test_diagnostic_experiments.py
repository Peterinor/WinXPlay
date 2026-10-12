"""Apply existing ARM behavior tests to relocated and newly rebuilt experiments.

Historical package/EXE comparisons remain in the original test modules. These
checks select the new binaries without executing any native CE application.
"""
from pathlib import Path
import os
import unittest
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from diagnostic_catalog import CATALOG, binary
SELECTED=os.environ.get('MRN_DIAGNOSTIC_TEST_TOOL')


def run_checks(owner,case,names):
    result=unittest.TestResult()
    for name in names:case(name).run(result)
    owner.assertFalse(result.errors+result.failures,result.errors+result.failures)
    owner.assertEqual(result.testsRun,len(names))


class ExperimentTests(unittest.TestCase):
    def test_both_usb_directories_reach_log_creation_in_real_entries(self):
        from test_diagnostics import DiagnosticMachine
        selected=[SELECTED] if SELECTED else [t for t in CATALOG if CATALOG[t]['category']=='experiment']
        names=set(selected)
        for tool in selected:names.update(CATALOG[tool]['dependencies'])
        for tool in names:
            for folder in ('AutoLaunch','AUTOLAUNCH_PROD','AUTOLAUNCH_PROD_invalid'):
                with self.subTest(tool=tool,folder=folder):
                    machine=DiagnosticMachine(tool,path='\\Hard Disk\\'+folder+'\\'+CATALOG[tool]['executable'],stop_at_log=True)
                    code,_=machine.run()
                    expected=True  # Generic tools run from arbitrary writable directories.
                    self.assertEqual(bool(machine.opened_paths),expected)
                    self.assertEqual(code==0,expected)

    @unittest.skipIf(SELECTED not in (None,'window-operations'),'different selected tool')
    def test_window_operations_configuration_targeting_and_restore_gates(self):
        from test_window_ops import WindowOpsTests
        run_checks(self,WindowOpsTests,unittest.defaultTestLoader.getTestCaseNames(WindowOpsTests))

    @unittest.skipIf(SELECTED not in (None,'hmi-termination-experiment'),'different selected tool')
    def test_configuration_driven_process_operations(self):
        from test_processops import ProcessOpsTests
        run_checks(self,ProcessOpsTests,unittest.defaultTestLoader.getTestCaseNames(ProcessOpsTests))

    def test_new_experiment_imports_are_available_on_factory_system(self):
        import json
        from arm_support import pefile,FACTORY
        catalog=CATALOG
        selected=[SELECTED] if SELECTED else [t for t in catalog if catalog[t]['category']=='experiment']
        names=set(selected)
        for tool in selected:names.update(catalog[tool]['dependencies'])
        factory=pefile.PE(str(FACTORY));exports={s.ordinal:s.name for s in factory.DIRECTORY_ENTRY_EXPORT.symbols}
        exported_names=set(exports.values())
        for tool in names:
            pe=pefile.PE(str(binary(tool)));resolved=set()
            for module in pe.DIRECTORY_ENTRY_IMPORT:
                self.assertEqual(module.dll.lower(),b'coredll.dll')
                for symbol in module.imports:
                    name=symbol.name or exports[symbol.ordinal]
                    self.assertIn(name,exported_names);resolved.add(name)
            forbidden={b'RegSetValueExW',b'WriteProcessMemory',b'DeviceIoControl'}
            self.assertFalse(forbidden&resolved)
            if tool!='hmi-termination-experiment':self.assertNotIn(b'OpenProcess',resolved)


if __name__=='__main__':unittest.main()
