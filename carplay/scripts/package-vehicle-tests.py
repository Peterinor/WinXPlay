"""Publish the three field-test entry points; no developer fixtures or identities."""
from pathlib import Path
import argparse
import json
import plistlib
import sys

APP = Path(__file__).resolve().parents[1]
ROOT = APP.parent
sys.path.insert(0, str(ROOT / 'scripts'))
from arm_digest import digest
from runtime_package import publish_existing_variants


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=ROOT/'build/packages')
    parser.add_argument('--target', choices=('all','av','connect','input'), default='all')
    parser.add_argument('--av-mode', choices=('play','microphone'), default='play')
    args = parser.parse_args()
    out = args.output.resolve()
    programs = {
        'av': ('winxplay_test_av.exe', {'mode': args.av_mode, 'delay_seconds': 10}),
        'connect': ('winxplay_test_connect.exe', {'mode': 'inspect'}),
        'input': ('winxplay_test_input.exe', {'mode': 'standard', 'delay_seconds': 10, 'seconds': 60}),
    }
    manifest = {'platform': 'wince-arm', 'vehicle_tested': False, 'programs': {}}
    for group, (name, config) in programs.items():
        if args.target not in ('all',group):
            continue
        binary = (APP / '../build/carplay/vehicle-tests' / name).read_bytes()
        base = out / Path(name).stem / 'AutoLaunch'
        base.mkdir(parents=True, exist_ok=True)
        (base / name).write_bytes(binary)
        (base / 'suite.plist').write_bytes(plistlib.dumps(config))
        launch = digest(binary) + '|0|\\Hard Disk\\AutoLaunch\\' + name + '|'
        (base / 'Autolaunch.txt').write_text(launch, encoding='ascii')
        if group == 'connect':
            (base / 'network.plist').write_bytes(plistlib.dumps({'seconds': 30}))
            (base / 'WinXPlayUsbMux.dll').write_bytes((APP / '../build/carplay/ce-wm5-arm-mrn/WinXPlayUsbMux.dll').read_bytes())
        if group == 'av':
            (base / 'video.avt').write_bytes((APP / '../build/carplay/vehicle-tests/video.avt').read_bytes())
            (base / 'test.plist').write_bytes(plistlib.dumps({'mode': 'full', 'wave_device': 1}))
        examples = base
        modes = {'av': ['inspect', 'instantiate', 'play', 'voice-pcm', 'microphone'],
                 'connect': ['inspect', 'usb-register', 'phone', 'pair', 'session'],
                 'input': ['standard', 'bridge']}[group]
        for mode in modes:
            example = dict(config, mode=mode)
            if group == 'av':
                example = {'mode': mode, 'delay_seconds': 10}
                if mode == 'voice-pcm':
                    example['seconds'] = 20
                if mode == 'microphone':
                    example['seconds'] = 6
            (examples / ('suite-' + mode + '.example.plist')).write_bytes(plistlib.dumps(example))
        if group == 'av':
            for playback in ('full', 'audio', 'video', 'sync'):
                (examples / ('playback-' + playback + '.example.plist')).write_bytes(plistlib.dumps({'mode': playback, 'wave_device': 1}))
            # Retire only the known obsolete example from existing A/B packages.
            for variant in ('AutoLaunch','AUTOLAUNCH_PROD'):
                old = out / Path(name).stem / variant / 'audio-usb-route-test.example.plist'
                if old.exists():
                    old.unlink()
        manifest['programs'][group] = {'executable': name, 'default_mode': config['mode']}
        assert (base / name).read_bytes() == binary
        assert (base / 'Autolaunch.txt').read_text('ascii') == launch
        (base / '使用说明.md').write_bytes((APP / 'tools/mrn/README.md').read_bytes())
        publish_existing_variants(base, name)
    manifest['startup_schemes']=['A','B']
    record=ROOT/'build/validation/package-vehicle-tests.json'
    record.parent.mkdir(parents=True, exist_ok=True)
    if args.target!='all' and record.exists():
        previous=json.loads(record.read_text(encoding='utf-8'))
        previous.setdefault('programs',{}).update(manifest['programs'])
        previous['startup_schemes']=manifest['startup_schemes']
        manifest=previous
    record.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(str(out))


if __name__ == '__main__':
    main()
