"""Exercise native pinned TLS and CarKit/iAP2 bring-up against an independent peer."""
from pathlib import Path
import socket, ssl, struct, subprocess, threading, tempfile, plistlib, json, datetime, os
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
from integration import exact, request, send, build
from auth_integration import identity as accessory_identity, verify_raw
from cryptography.hazmat.primitives.serialization import pkcs7

def identity():
    key=rsa.generate_private_key(public_exponent=65537,key_size=2048)
    name=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'Synthetic native TLS test')])
    cert=(x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
          .serial_number(x509.random_serial_number()).not_valid_before(datetime.datetime(2025,1,1))
          .not_valid_after(datetime.datetime(2030,1,1)).sign(key,hashes.SHA256()))
    return key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,
                             serialization.NoEncryption()),cert.public_bytes(serialization.Encoding.PEM)

def framed(sock,value):
    data=plistlib.dumps(value)
    sock.sendall(struct.pack('>I',len(data))+data)

def read_framed(sock):
    return plistlib.loads(exact(sock,struct.unpack('>I',exact(sock,4))[0]))

def link(control,seq,ack,session,body=b''):
    header=bytearray(struct.pack('>HHBBBB',0xff5a,9+len(body)+(1 if body else 0),control,seq,ack,session))
    header.append((-sum(header))&255)
    return bytes(header)+body+(bytes([(-sum(body))&255]) if body else b'')

def csm(message, body=b''):
    return struct.pack('>HHH',0x4040,6+len(body),message)+body

def receive_csm(sock):
    for _ in range(10):
        header=exact(sock,9);size=struct.unpack_from('>H',header,2)[0]
        assert 9<=size<=65535 and sum(header)%256==0
        body=exact(sock,size-9)
        if not body: continue
        assert sum(body)%256==0 and header[7]==10
        body=body[:-1];magic,n,message=struct.unpack_from('>HHH',body)
        assert magic==0x4040 and n==len(body)
        return header,message,body[6:]
    raise AssertionError('No control response')

def scenario(kind, executable=None, after_auth=None, extra_env=None):
    private,cert=identity()
    _,wrong=identity()
    record={'HostID':'SYNTHETIC','SystemBUID':'SYNTHETIC','RootPrivateKey':private,
            'RootCertificate':cert,'DeviceCertificate':wrong if kind=='wrong_pin' else cert}
    listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(10)
    port=listener.getsockname()[1];errors=[]
    with tempfile.TemporaryDirectory(prefix='synthetic-tls-',dir=build) as temp:
        temp=Path(temp);(temp/'cert.pem').write_bytes(cert);(temp/'key.pem').write_bytes(private)
        env=os.environ.copy();env.pop('CARPLAY_IDENTITY',None)
        if extra_env: env.update(extra_env)
        if kind.startswith('mfi_'):
            accessory=accessory_identity(temp)
            env['CARPLAY_IDENTITY']=str(temp)
        context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(temp/'cert.pem',temp/'key.pem')
        def peer():
            try:
                with listener.accept()[0] as sock:
                    sock.settimeout(5)
                    tag,value=request(sock);assert value['MessageType']=='ReadPairRecord'
                    send(sock,tag,{'PairRecordData':plistlib.dumps(record)})
                with listener.accept()[0] as raw:
                    raw.settimeout(5)
                    tag,value=request(raw);assert value['MessageType']=='Connect' and value['PortNumber']==socket.htons(62078)
                    send(raw,tag,{'Number':0})
                    value=read_framed(raw);assert value['Request']=='StartSession'
                    framed(raw,{'EnableSessionSSL':True,'SessionID':'synthetic-session'})
                    try: secured=context.wrap_socket(raw,server_side=True)
                    except ssl.SSLError:
                        if kind=='wrong_pin': return
                        raise
                    with secured:
                        if kind=='trust_only':
                            # The native process closes immediately after TLS;
                            # Winsock may report EOF or an aborted/reset socket.
                            try: remaining=secured.recv(1)
                            except (ConnectionAbortedError,ConnectionResetError): remaining=b''
                            assert remaining==b'', 'Trust restoration opened a service unexpectedly'
                            return
                        value=read_framed(secured);assert value['Service']=='com.apple.carkit.service'
                        if kind=='service_refused':
                            framed(secured,{'Error':'InvalidService'});return
                        framed(secured,{'Port':12345,'EnableServiceSSL':False})
                        with listener.accept()[0] as service:
                            service.settimeout(5)
                            tag,value=request(service);assert value['PortNumber']==socket.htons(12345)
                            send(service,tag,{'Number':0})
                            assert exact(service,6)==bytes.fromhex('ff550200ee10')
                            header=exact(service,9);size=struct.unpack_from('>H',header,2)[0]
                            body=exact(service,size-9)
                            assert sum(header)%256==0 and sum(body)%256==0 and header[4]==0x80
                            syn=link(0xc0,7,99,0,bytes.fromhex('011effff0fa001f404030a0001'))
                            identification=link(0x40,8,99,10,csm(0xaa00 if kind.startswith('mfi_') else 0x1d00))
                            for byte in syn+identification: service.sendall(bytes([byte]))
                            if kind.startswith('mfi_'):
                                h,message,body=receive_csm(service)
                                assert message==0xaa01 and h[5]==100 and h[6]==8
                                size,param=struct.unpack_from('>HH',body);assert size==len(body) and param==0
                                certificates=pkcs7.load_der_pkcs7_certificates(body[4:])
                                assert len(certificates)==1 and certificates[0].fingerprint(hashes.SHA256())==accessory.fingerprint(hashes.SHA256())
                                if kind=='mfi_early_accept':
                                    service.sendall(link(0x40,9,100,10,csm(0xaa05)));return
                                digest=os.urandom(32)
                                challenge=struct.pack('>HH',36,0)+digest
                                service.sendall(link(0x40,9,100,10,csm(0xaa02,challenge)))
                                h,message,body=receive_csm(service)
                                assert message==0xaa03 and h[5]==101 and h[6]==9
                                size,param=struct.unpack_from('>HH',body);assert size==68 and param==0
                                verify_raw(accessory,digest,body[4:])
                                service.sendall(link(0x40,10,101,10,csm(0xaa04 if kind=='mfi_reject' else 0xaa05)))
                                if kind=='mfi_ok':
                                    ack=exact(service,9);assert ack[6]==10
                                    if after_auth: after_auth(service)
                                return
                            acks=exact(service,18)
                            assert acks[6]==7 and acks[15]==8
            except Exception as exc:
                if not (kind=='wrong_pin' and isinstance(exc,(EOFError,ConnectionResetError,BrokenPipeError))): errors.append(repr(exc))
            finally: listener.close()
        thread=threading.Thread(target=peer,daemon=True);thread.start()
        result=subprocess.run([str(executable or build/'carplay_tests.exe'),'--carkit',str(port)],cwd=build,
                              capture_output=True,timeout=30,env=env)
        thread.join(10)
        assert not thread.is_alive() and not errors,(kind,errors,result.stdout,result.stderr)
        report=plistlib.loads(result.stdout)
        assert result.returncode==(0 if kind in ('ok','mfi_ok','trust_only') else 2),(kind,report,result.stderr)
        if kind=='wrong_pin': assert report['failed_stage']=='lockdown_tls'
        if kind=='service_refused': assert report['error']=='Lockdown: InvalidService'
        if kind=='ok': assert report['iap2_link_ready'] and report['iap2_identification_requested']
        if kind=='mfi_ok': assert report['mfi_authentication_accepted'] and report['mfi_challenge_answered']
        if kind in ('mfi_reject','mfi_early_accept'):
            assert report['failed_stage']=='mfi_authentication' and not report.get('mfi_authentication_accepted',False)
        return {'scenario':kind,'passed':True,'native_result':report}

if __name__ == '__main__':
    results=[scenario(k) for k in ('ok','wrong_pin','service_refused','mfi_ok','mfi_reject','mfi_early_accept')]
    (build/'tls-integration-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(json.dumps(results,indent=2))
