"""Local private USB runtime assets."""
from pathlib import Path
import plistlib
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.serialization import pkcs7
ROOT=Path(__file__).resolve().parents[1]

def private_material(destination,identity,pair_record=None):
    destination.mkdir(parents=True,exist_ok=True)
    if pair_record:
        record=plistlib.loads(pair_record.read_bytes())
        if not record.get('DeviceUDID'):raise RuntimeError('Cached pairing record has no DeviceUDID')
        for field in ('RootPrivateKey','RootCertificate','DeviceCertificate','HostID','SystemBUID'):
            if not record.get(field):raise RuntimeError('Incomplete trusted pairing record')
        (destination/'phone-pair.plist').write_bytes(plistlib.dumps(record))
    cert_bytes=(identity/'certificate.p7b').read_bytes()
    key_bytes=(identity/'identity.pk8').read_bytes()
    key=serialization.load_der_private_key(key_bytes,password=None)
    encoding=serialization.Encoding.DER;fmt=serialization.PublicFormat.SubjectPublicKeyInfo
    public=key.public_key().public_bytes(encoding,fmt)
    certs=[c for c in pkcs7.load_der_pkcs7_certificates(cert_bytes) if c.public_key().public_bytes(encoding,fmt)==public]
    if len(certs)!=1:raise RuntimeError('Accessory certificate/key mismatch')
    folder=destination/'identity';folder.mkdir(exist_ok=True)
    (folder/'certificate.p7b').write_bytes(cert_bytes)
    (folder/'identity.pk8').write_bytes(key_bytes)
    (folder/'certificate.der').write_bytes(certs[0].public_bytes(encoding))
