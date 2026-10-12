"""Read a bounded, audio/CarPlay-filtered syslog from the already paired phone.

Pairing secrets stay in memory except temporary TLS files, which are always removed.
No phone settings or files are modified.
"""
import socket,struct,plistlib,ssl,tempfile,time,re,json
from collections import deque
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives.serialization import Encoding,PublicFormat
from streaming_integration import exact,BUILD

def mux(message):
    s=socket.create_connection(('127.0.0.1',27015),4)
    body=plistlib.dumps(dict(message,ClientVersionString='MRN diagnostics',ProgName='WinXPlay',kLibUSBMuxVersion=3))
    s.sendall(struct.pack('<IIII',len(body)+16,1,8,1)+body)
    n,version,kind,tag=struct.unpack('<IIII',exact(s,16));assert 16<=n<=4*1024*1024
    return s,plistlib.loads(exact(s,n-16))
def connect(device,port):
    s,r=mux(dict(MessageType='Connect',DeviceID=device,PortNumber=socket.htons(port)))
    assert r['Number']==0,r
    return s
def request(s,**fields):
    body=plistlib.dumps(fields);s.sendall(struct.pack('>I',len(body))+body)
    n=struct.unpack('>I',exact(s,4))[0];assert n<=4*1024*1024
    r=plistlib.loads(exact(s,n));assert 'Error' not in r,r
    return r
def main():
    s,r=mux(dict(MessageType='ListDevices'));s.close()
    device=next(d for d in r['DeviceList'] if d['Properties']['ConnectionType']=='USB')
    s,r=mux(dict(MessageType='ReadPairRecord',PairRecordID=device['Properties']['SerialNumber']));s.close()
    record=plistlib.loads(r['PairRecordData'])
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT);context.check_hostname=False;context.verify_mode=ssl.CERT_NONE;context.set_ciphers('DEFAULT:@SECLEVEL=0')
    with tempfile.TemporaryDirectory(prefix='mrn-tls-') as directory:
        cert=Path(directory)/'certificate.pem';key=Path(directory)/'private.pem'
        cert.write_bytes(record['RootCertificate']);key.write_bytes(record['RootPrivateKey']);context.load_cert_chain(cert,key)
    def secure(s):
        s=context.wrap_socket(s)
        actual=x509.load_der_x509_certificate(s.getpeercert(binary_form=True)).public_key()
        expected=x509.load_pem_x509_certificate(record['DeviceCertificate']).public_key()
        assert actual.public_bytes(Encoding.DER,PublicFormat.SubjectPublicKeyInfo)==expected.public_bytes(Encoding.DER,PublicFormat.SubjectPublicKeyInfo),'Pair record peer mismatch'
        return s
    control=connect(device['DeviceID'],62078)
    r=request(control,Request='StartSession',HostID=record['HostID'],SystemBUID=record['SystemBUID'],Label='WinXPlay')
    assert r['EnableSessionSSL'];control=secure(control)
    r=request(control,Request='StartService',Service='com.apple.syslog_relay')
    stream=connect(device['DeviceID'],r['Port'])
    if r.get('EnableServiceSSL'):stream=secure(stream)
    stream.settimeout(1);end=time.monotonic()+35;pending=b'';lines=deque(maxlen=4000)
    # Only diagnostic lines from the audio/CarPlay processes are retained.
    while time.monotonic()<end:
        try:data=stream.recv(65536)
        except socket.timeout:continue
        if not data:break
        pending+=data
        while b'\n' in pending:
            line,pending=pending.split(b'\n',1);line=line.decode('utf-8','replace').replace('\0','')
            if 'RunningBoard' not in line and (re.search(r' (airplayd|carkitd|accessoryd)[(\[]',line,re.I) or ('audiomxd' in line and re.search(r'(CarPlay|AirPlay|route|routing|supported.*format)',line,re.I))):
                lines.append(re.sub(r'^(\w+\s+\d+\s+[\d:]+)\s+\S+',r'\1 iPhone',line))
        pending=pending[-65536:]
    stream.close();control.close()
    (BUILD/'phone-audio-diagnostics.log').write_text('\n'.join(lines),encoding='utf-8')
    print(json.dumps({'filtered_lines':len(lines),'path':str(BUILD/'phone-audio-diagnostics.log')}))
if __name__=='__main__':main()
