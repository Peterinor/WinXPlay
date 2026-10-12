"""Verify every current flat A/B USB pair, including nested runtime payloads."""
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from arm_digest import digest
from diagnostic_catalog import SCENARIOS
from runtime_package import production_data

PAIRS={Path(item['main']['executable']).stem:item['main']['executable'] for item in SCENARIOS.values()}
PAIRS.update({'file_ops':'file_ops.exe','system_ops':'system_ops.exe','firmware_install':'file_ops.exe','winxplay_bridge_update':'file_ops.exe','winxplay_install':'file_ops.exe',
              'winxplay_usb':'WinXPlay.exe',**{f'winxplay_test_{x}':f'winxplay_test_{x}.exe' for x in ('av','connect','input')}})

class UsbPackageTests(unittest.TestCase):
    def test_all_pairs_have_correct_launch_format_checksum_and_path(self):
        self.assertEqual(len(PAIRS),14)
        for name,exe in PAIRS.items():
            with self.subTest(package=name):
                root=ROOT/'build/packages'/name;a=root/'AutoLaunch';b=root/'AUTOLAUNCH_PROD'
                self.assertEqual({p.name for p in root.iterdir() if p.is_dir()}, {'AutoLaunch','AUTOLAUNCH_PROD'})
                self.assertFalse(root.with_name(name+'_prod').exists())
                binary=(a/exe).read_bytes()
                self.assertEqual(binary,(b/exe).read_bytes())
                self.assertFalse((a/'AutoLaunch_prod.txt').exists())
                self.assertFalse((b/'Autolaunch.txt').exists())
                self.assertEqual((a/'Autolaunch.txt').read_bytes(),
                    (digest(binary)+'|0|\\Hard Disk\\AutoLaunch\\'+exe+'|').encode('ascii'))
                # Independently count set bits, not using the production writer.
                checksum=sum(bin(byte).count('1') for byte in binary)&0xffffffff
                self.assertEqual((b/'AutoLaunch_prod.txt').read_bytes(),
                    f'{checksum:08X},\\Hard Disk\\AUTOLAUNCH_PROD\\{exe},\r\n'.encode('ascii'))

    def test_runtime_payloads_match_and_no_archive_or_source_snapshots(self):
        forbidden={'.zip','.cpp','.h','.py','.ps1','.obj','.lib'}
        for name in PAIRS:
            with self.subTest(package=name):
                root=ROOT/'build/packages'/name;a=root/'AutoLaunch';b=root/'AUTOLAUNCH_PROD'
                af={p.relative_to(a).as_posix() for p in a.rglob('*') if p.is_file()}-{'Autolaunch.txt'}
                bf={p.relative_to(b).as_posix() for p in b.rglob('*') if p.is_file()}-{'AutoLaunch_prod.txt'}
                self.assertEqual(af,bf)
                for folder in (a,b):
                    self.assertEqual((folder/'LICENSE').read_bytes(), (ROOT/'LICENSE').read_bytes())
                    notices=(folder/'THIRD_PARTY.txt').read_bytes()
                    self.assertEqual((ROOT/'THIRD_PARTY.md').read_bytes(), notices)
                for rel in af:
                    self.assertNotIn(Path(rel).suffix.lower(),forbidden)
                    if Path(rel).suffix.lower() not in ('.txt','.md','.ini','.xml','.plist'):
                        self.assertEqual((a/rel).read_bytes(),(b/rel).read_bytes(),rel)
                    if Path(rel).suffix.lower() in ('.txt','.ini','.xml','.plist'):
                        self.assertNotIn(b'\\Hard Disk\\AutoLaunch\\',(b/rel).read_bytes(),rel)

    def test_b_install_sources_exist_and_recipes_parse_on_actual_arm_code(self):
        from test_file_ops_policy import Tests
        for name in ('firmware_install','winxplay_install','winxplay_bridge_update'):
            for scheme,usb in (('AutoLaunch', 'AutoLaunch'),('AUTOLAUNCH_PROD','AUTOLAUNCH_PROD')):
                with self.subTest(package=name+scheme):
                    folder=ROOT/'build/packages'/name/scheme
                    recipe=(folder/'fileops.txt').read_bytes()
                    self.assertEqual(Tests().parse(recipe)[0],1)
                    prefix='\\Hard Disk\\'+usb+'\\'
                    for line in recipe.decode('utf-8-sig').splitlines():
                        fields=line.split('|')
                        if fields[0] not in ('COPY','INSTALL'):continue
                        self.assertTrue(fields[1].startswith(prefix))
                        source=fields[1][len(prefix):].replace('\\','/')
                        self.assertTrue((folder/source).is_file(),source)

    def test_text_conversion_handles_unicode_without_mutating_executables(self):
        text='COPY|\\Hard Disk\\AutoLaunch\\payload\\demo.exe|\\NAND_USER\\demo.exe\nAutolaunch.txt'
        expected=text.replace('AutoLaunch\\','AUTOLAUNCH_PROD\\').replace('Autolaunch.txt','AutoLaunch_prod.txt')
        for encoding in ('utf-8','utf-8-sig','utf-16'):
            self.assertEqual(production_data('fileops.txt',text.encode(encoding)).decode(encoding),expected)
        binary=b'AutoLaunch\\'+bytes(range(256))
        self.assertEqual(production_data('demo.exe',binary),binary)
        self.assertEqual(production_data('identity.pk8',binary),binary)

    def test_winxplay_touch_update_keeps_private_receiver_identity(self):
        import plistlib,json
        no_logs=json.loads((ROOT/'build/validation/winxplay_install-package.json').read_text()).get('logs_enabled') is False
        for scheme in ('AutoLaunch','AUTOLAUNCH_PROD'):
            folder=ROOT/'build/packages/winxplay_install'/scheme
            settings=plistlib.loads((folder/'payload/carplay.plist').read_bytes())
            self.assertEqual(settings['CARPLAY_TOUCH'],'1')
            for recipe in ('fileops.txt','update-bridge-fileops.txt','update-usb-fileops.txt'):
                text=(folder/recipe).read_text('ascii')
                if no_logs and recipe=='update-bridge-fileops.txt':self.assertNotIn('carplay.plist',text)
                else:self.assertIn('carplay.plist',text)
                self.assertNotIn('receiver-identity.dat',text)
                self.assertNotIn('phone-pair.plist',text)
            self.assertFalse((folder/'payload/receiver-identity.dat').exists())

    def test_winxplay_nologs_package_uses_all_three_release_binaries(self):
        import json,hashlib
        record=json.loads((ROOT/'build/validation/winxplay_install-package.json').read_text())
        if record.get('logs_enabled') is not False:self.skipTest('Debug package selected')
        for scheme in ('AutoLaunch','AUTOLAUNCH_PROD'):
            folder=ROOT/'build/packages/winxplay_install'/scheme
            for name in ('WinXPlay.exe','WinXPlayUsbMux.dll','WinXPlayBridge.dll'):
                build='build/firmware/ab_bridge-nologs' if name=='WinXPlayBridge.dll' else 'build/carplay/ce-wm5-arm-mrn-nologs'
                self.assertEqual((folder/'payload'/name).read_bytes(),(ROOT/build/name).read_bytes())
                self.assertIn(name,(folder/'update-bridge-fileops.txt').read_text())
            for name in record['files']:
                if scheme=='AutoLaunch':self.assertEqual(hashlib.sha256((folder/name).read_bytes()).hexdigest(),record['files'][name])

    def test_winxplay_collects_original_logs_using_fileops(self):
        from test_file_ops_policy import Tests
        for scheme in ('AutoLaunch','AUTOLAUNCH_PROD'):
            recipe=(ROOT/'build/packages/winxplay_install'/scheme/'collect-fileops.txt').read_bytes()
            self.assertEqual(Tests().parse(recipe)[0],1)
            lines=recipe.decode('ascii').splitlines()
            self.assertEqual(lines[0],'MRN_FILEOPS_V2')
            names=[]
            for line in lines[1:]:
                operation,source,target=line.split('|')
                self.assertEqual(operation,'COPY')
                self.assertEqual(source,'\\NAND_USER\\WinXPlay\\'+target)
                self.assertTrue(target.endswith(('.log','.plist')))
                self.assertNotIn('phone-pair',target)
                names.append(target)
            self.assertEqual(len(names),len(set(names)))
            self.assertTrue({'connection-trace.log','connection-trace.previous.log',
                'crash.log','usb-driver.log','A_BRIDGE.log','session-usb.plist',
                'video-backend.plist','audio-output.plist','last-error.plist'} <= set(names))
            self.assertNotIn('WinXPlay-diagnostics.txt',names)
            self.assertEqual((ROOT/'build/packages/winxplay_install'/scheme/'collect-bridge-fileops.txt').read_text().splitlines(),
                ['MRN_FILEOPS_V2',r'COPY|\NAND_USER\WinXPlay\A_BRIDGE.log|A_BRIDGE.log'])

if __name__=='__main__':unittest.main()
