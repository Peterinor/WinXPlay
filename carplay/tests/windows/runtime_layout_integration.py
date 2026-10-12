"""Run relocated binaries without runtime environment variables or launch scripts."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import tempfile
from auth_integration import identity, verify_raw
import tls_integration

BUILD = Path(__file__).resolve().parents[2]/'../build/carplay/windows-x64'


def main():
    env = {k: v for k, v in os.environ.items()
           if k not in ('CARPLAY_CRYPTO', 'CARPLAY_SSL', 'CARPLAY_IDENTITY')}
    env['PATH'] = str(Path(os.environ['SystemRoot'])/'System32')
    with tempfile.TemporaryDirectory(prefix='portable runtime ', dir=BUILD) as temp:
        directory = Path(temp)
        for name in ('carplay_tests.exe', 'WinXPlay.exe'):
            shutil.copyfile(BUILD/name, directory/name)
        for dll in BUILD.glob('*.dll'):
            shutil.copyfile(dll, directory/dll.name)
        local = directory/'identity'
        local.mkdir()
        certificate = identity(local)
        digest = os.urandom(32)
        def sign(environment, ok=True):
            result = subprocess.run([str(directory/'carplay_tests.exe'), '--auth-sign', '-', digest.hex()],
                                    cwd=directory.parent, env=environment, capture_output=True, timeout=10)
            assert result.returncode == (0 if ok else 2), result.stdout
            return bytes.fromhex(result.stdout.decode().strip()) if ok else None
        verify_raw(certificate, digest, sign(env))
        alternate = directory/'alternate'
        alternate.mkdir()
        override_certificate = identity(alternate)
        verify_raw(override_certificate, digest, sign(dict(env, CARPLAY_IDENTITY=str(alternate))))
        sign(dict(env, CARPLAY_CRYPTO=str(directory/'missing.dll')), ok=False)
        prior = dict(os.environ)
        try:
            os.environ.clear()
            os.environ.update(env)
            tls_integration.scenario('mfi_ok', executable=directory/'carplay_tests.exe')
        finally:
            os.environ.clear()
            os.environ.update(prior)
        # Smoke creates a hidden UI without connecting to a physical phone.
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        result = subprocess.run([str(directory/'WinXPlay.exe'), '--ui-smoke'], cwd=directory.parent,
                                env=env, startupinfo=startup, timeout=25)
        assert result.returncode == 0 and (directory/'ui-smoke.bmp').is_file()
    report = {'passed': True, 'relocated_runtime': True, 'runtime_environment_required': False,
              'cwd_independent': True, 'local_identity_signature_verified': True,
              'environment_override_verified': True, 'local_tls_verified': True,
              'hidden_ui_smoke': True, 'physical_iphone_verified': False}
    (BUILD/'runtime-layout-results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
