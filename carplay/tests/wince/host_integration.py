"""Independent peers exercise the actual CE crypto/TLS/protocol build on Windows.
Host media/USB adapters are substituted; this does not validate physical CE drivers.
"""
import sys, os, json, subprocess
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'windows'))
import auth_integration as auth, integration, streaming_integration as streaming
from cryptography.hazmat.primitives import serialization
build=Path(__file__).resolve().parents[2]/'../build/carplay'/'ce-host-x64'
auth.BUILD=integration.build=streaming.BUILD=build
original=auth.identity
def identity(path,curve=None):
    cert=original(path,curve)
    (path/'certificate.der').write_bytes(cert.public_bytes(serialization.Encoding.DER))
    return cert
auth.identity=identity
import tls_integration as tls
tls.build=build
subprocess.run([str(build/'MRN_CARPLAY_SELFTEST.exe')],check=True,timeout=30)
auth.run_tests()
results=[integration.scenario(k) for k in ('ok','bad_tag','refused')]
results += [tls.scenario(k) for k in ('ok','wrong_pin','service_refused','mfi_ok','mfi_reject','mfi_early_accept')]
(build/'ce-protocol-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
os.environ['CARPLAY_TEST_EXECUTABLE']=str(build/'WinXPlay.exe')
streaming.run()
print('CE host protocol/crypto/TLS/PCM/H264 tests passed; CE hardware unverified.')
