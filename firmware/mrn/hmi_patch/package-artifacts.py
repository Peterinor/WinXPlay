"""Package verified HMI integration artifacts as a plain folder; never flash."""
import hashlib,json,shutil,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'scripts'))
from runtime_package import license_payload
BUILD=ROOT/'build/firmware/ab_bridge'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    verification=json.loads((BUILD/'verification.json').read_text(encoding='utf-8'))
    # Standard-window bridge suite, including timeout recovery and patch ABI.
    assert verification['passed'] and verification['tests_run']>=16
    for name,digest in verification['binaries'].items():assert sha(BUILD/name)==digest,name
    layer=json.loads((BUILD/'layer-verification.json').read_text(encoding='utf-8'))
    assert layer['passed'] and layer['tests_run']>=9
    assert layer['bridge_sha256']==sha(BUILD/'WinXPlayBridge.dll')
    target=ROOT/'build/packages/firmware';target.mkdir(parents=True,exist_ok=True)
    names=['ENTHMI2.WINXPLAY.EXE','WinXPlayBridge.dll','patch-manifest.json','verification.json','verification.log',
           'layer-verification.json','layer-tests.log']
    for name in names:shutil.copyfile(BUILD/name,target/name)
    shutil.copyfile(ROOT/'firmware/mrn/README.md',target/'README.md')
    for name,data in license_payload().items():
        (target/name).write_bytes(data)
        names.append(name)
    manifest={'firmware_deployed':False,'flash_package':False,'files':{name:sha(target/name) for name in names+['README.md']}}
    (target/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print(target)
if __name__=='__main__':main()
