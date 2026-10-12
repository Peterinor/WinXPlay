"""Test native MFi signer against independent synthetic X.509 identities."""
from pathlib import Path
import datetime, json, os, subprocess, tempfile
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.hazmat.primitives.serialization import pkcs7
from cryptography.x509.oid import NameOID
BUILD = Path(__file__).resolve().parents[2] / '../build/carplay/windows-x64'

def identity(directory, curve=None, critical_extension=False):
    key = ec.generate_private_key(curve or ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'Synthetic accessory')])
    builder = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(datetime.datetime(2025,1,1)).not_valid_after(datetime.datetime(2030,1,1)))
    if critical_extension:
        builder=builder.add_extension(x509.UnrecognizedExtension(x509.ObjectIdentifier('1.2.840.113635.100.6.36'),b'\x04\x02\x00\x01'),critical=True)
    cert=builder.sign(key, hashes.SHA256())
    (directory/'identity.pk8').write_bytes(key.private_bytes(serialization.Encoding.DER,
         serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    (directory/'certificate.p7b').write_bytes(pkcs7.serialize_certificates([cert], serialization.Encoding.DER))
    return cert

def verify_raw(cert, digest, raw):
    assert len(raw) == 64
    der = utils.encode_dss_signature(int.from_bytes(raw[:32], 'big'), int.from_bytes(raw[32:], 'big'))
    cert.public_key().verify(der, digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))

def run_tests():
    results = []
    with tempfile.TemporaryDirectory(prefix='synthetic-auth-', dir=BUILD) as temp:
        path = Path(temp); cert = identity(path)
        good_key = (path/'identity.pk8').read_bytes()
        good_cert = (path/'certificate.p7b').read_bytes()
        def run(digest, ok):
            result = subprocess.run([str(BUILD/'carplay_tests.exe'), '--auth-sign', str(path), digest.hex()],
                                    capture_output=True, timeout=5)
            assert result.returncode == (0 if ok else 2), result.stdout
            return bytes.fromhex(result.stdout.decode().strip()) if ok else None
        for _ in range(16):
            digest = os.urandom(32); verify_raw(cert, digest, run(digest, True))
        results.append(dict(scenario='16_native_signatures_verified_independently', passed=True))
        run(b'bad-length', False)
        results.append(dict(scenario='invalid_challenge_size_rejected', passed=True))
        identity(path); (path/'certificate.p7b').write_bytes(good_cert)
        run(os.urandom(32), False)
        results.append(dict(scenario='mismatched_key_rejected', passed=True))
        (path/'identity.pk8').write_bytes(good_key)
        (path/'certificate.p7b').write_bytes(good_cert + b'trailing')
        run(os.urandom(32), False)
        results.append(dict(scenario='trailing_certificate_data_rejected', passed=True))
        (path/'certificate.p7b').write_bytes(good_cert)
        (path/'identity.pk8').write_bytes(good_key + b'trailing')
        run(os.urandom(32), False)
        results.append(dict(scenario='trailing_private_key_data_rejected', passed=True))
        identity(path, ec.SECP384R1()); run(os.urandom(32), False)
        results.append(dict(scenario='unsupported_curve_rejected', passed=True))
        (path/'identity.pk8').write_bytes(b'broken'); run(os.urandom(32), False)
        results.append(dict(scenario='malformed_private_key_rejected', passed=True))
    (BUILD/'auth-integration-results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print(json.dumps(results, indent=2))

if __name__ == '__main__': run_tests()
