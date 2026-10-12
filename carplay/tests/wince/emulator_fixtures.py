"""Synthetic accessory credentials for the isolated emulator; never phone credentials."""
from pathlib import Path
import sys
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'windows'))
from auth_integration import identity
folder=Path(__file__).resolve().parents[2]/'../build/carplay'/'ce-wm5-arm-mrn'/'emulator-fixtures'
folder.mkdir(parents=True,exist_ok=True)
cert=identity(folder,critical_extension=True)
(folder/'certificate.der').write_bytes(cert.public_bytes(serialization.Encoding.DER))
(folder/'mismatched.pk8').write_bytes(ec.generate_private_key(ec.SECP256R1()).private_bytes(
    serialization.Encoding.DER,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
