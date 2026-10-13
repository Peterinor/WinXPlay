"""Package the normal MRN receiver with passive playback diagnostics."""
from pathlib import Path
import hashlib
import json
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from runtime_package import publish


def main():
    exe = ROOT / 'build/carplay/ce-wm5-arm-mrn/WinXPlay.exe'
    fileops = ROOT / 'build/tools/file_ops/file_ops.exe'
    for path in (exe, fileops):
        if not path.is_file():
            raise RuntimeError('Build required: ' + str(path))
    target = r'\NAND_USER\WinXPlay'
    source = r'\Hard Disk\AutoLaunch\payload'
    files = {'file_ops.exe': fileops.read_bytes(), 'payload/WinXPlay.exe': exe.read_bytes()}
    files['fileops.txt'] = (f'MRN_FILEOPS_V2\nINSTALL|{source}\\WinXPlay.exe|{target}\\WinXPlay.exe\n').encode('ascii')
    files['run-fileops.txt'] = f'MRN_FILEOPS_V2\nRUN|{target}\\WinXPlay.exe\n'.encode('ascii')
    names = ['connection-trace.log', 'connection-trace.previous.log', 'streaming-diagnostics.plist',
             'video-backend.plist', 'audio-output.plist', 'audio-recovery.plist']
    files['collect-fileops.txt'] = ('MRN_FILEOPS_V2\n' + ''.join(
        f'COPY|{target}\\{name}|{name}\n' for name in names)).encode('ascii')
    files['使用说明.txt'] = (ROOT / 'carplay/docs/media-logging.md').read_bytes()
    output = ROOT / 'build/packages/winxplay_logging_update'
    publish(output, files, 'file_ops.exe', 'both')
    manifest = {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}
    validation = ROOT / 'build/validation/logging-update-package.json'
    validation.parent.mkdir(parents=True, exist_ok=True)
    validation.write_text(json.dumps({'files': manifest, 'vehicle_tested': False}, indent=2), encoding='utf-8')
    print(output)


if __name__ == '__main__':
    main()
