"""Independent synthetic peer: SRP, pair verify, MFiSAP, encrypted RTSP/video/audio.

This exercises the real native receiver and Windows output, not an iPhone.
"""
import hashlib, hmac, json, math, os, plistlib, re, socket, struct, subprocess, time, sys, tempfile
from pathlib import Path
from cryptography.hazmat.primitives.asymmetric import ed25519, x25519, ec, utils
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography import x509

ROOT=Path(__file__).resolve().parents[2]; BUILD=Path(os.environ.get('CARPLAY_TEST_BUILD',ROOT/'../build/carplay/windows-x64'))
H=lambda x:hashlib.sha512(x).digest()
def hk(k,s,i):return hmac.new(hmac.new(s.encode(),k,hashlib.sha512).digest(),i.encode()+b'\1',hashlib.sha512).digest()[:32]
def raw(k):return k.public_key().public_bytes(serialization.Encoding.Raw,serialization.PublicFormat.Raw)
def tlv(**fields):
    out=b''
    for k,v in fields.items():
        for p in range(0,len(v),255):out+=bytes([int(k[1:]),len(v[p:p+255])])+v[p:p+255]
    return out
def parse(b):
    out={}
    while b:
        t,n=b[:2];assert len(b)>=2+n;out[t]=out.get(t,b'')+b[2:2+n];b=b[2+n:]
    assert 7 not in out, f'Pairing error {out}'
    return out
def nonce(label):return bytes(4)+label.encode()
def exact(s,n):
    b=b''
    while len(b)<n:
        x=s.recv(n-len(b));assert x,'Receiver closed connection';b+=x
    return b
class Peer:
    def __init__(self,port):
        self.s=socket.create_connection(('127.0.0.1',port),5);self.s.settimeout(8)
        self.rx=self.tx=None;self.rc=self.tc=0;self.pending=b'';self.seq=0
    def request(self,path,body=b'',method='POST'):
        self.seq+=1
        b=f'{method} {path} RTSP/1.0\r\nCSeq: {self.seq}\r\nContent-Length: {len(body)}\r\n\r\n'.encode()+body
        if self.tx:
            out=b''
            for i in range(0,len(b),701):
                block=b[i:i+701];size=struct.pack('<H',len(block));out+=size+self.tx.encrypt(bytes(4)+struct.pack('<Q',self.tc),block,size);self.tc+=1
            b=out
        # Fragment header/body deliberately, without depending on native parser helpers.
        for i in range(0,len(b),137):self.s.sendall(b[i:i+137])
        while True:
            if b'\r\n\r\n' in self.pending:
                head,data=self.pending.split(b'\r\n\r\n',1);n=int(re.search(br'Content-Length: (\d+)',head,re.I)[1])
                if len(data)>=n:
                    self.pending=data[n:];assert b'200 OK' in head,head;return data[:n]
            if self.rx:
                size=exact(self.s,2);n=struct.unpack('<H',size)[0];self.pending+=self.rx.decrypt(bytes(4)+struct.pack('<Q',self.rc),exact(self.s,n+16),size);self.rc+=1
            else:self.pending+=self.s.recv(65536)
    def pair(self):
        # RFC 5054 3072-bit SRP group (independent fixed public group).
        N=int('FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7EDEE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F83655D23DCA3AD961C62F356208552BB9ED529077096966D670C354E4ABC9804F1746C08CA18217C32905E462E36CE3BE39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF6955817183995497CEA956AE515D2261898FA051015728E5A8AAAC42DAD33170D04507A33A85521ABDF1CBA64ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE6BF12FFA06D98A0864D87602733EC86A64521F2B18177B200CBBE117577A615D6C770988C0BAD946E208E24FA074E5AB3143DB5BFCE0FD108E4B82D120A93AD2CAFFFFFFFFFFFFFFFF',16)
        pad=lambda n:n.to_bytes(384,'big');integer=lambda b:int.from_bytes(b,'big')
        m=parse(self.request('/pair-setup',tlv(t6=b'\1',t0=b'\0')));salt,B=m[2],m[3]
        a=integer(os.urandom(32));A=pad(pow(5,a,N));x=integer(H(salt+H(b'Pair-Setup:3939')))
        k=integer(H(pad(N)+pad(5)));u=integer(H(A+B));S=pow((integer(B)-k*pow(5,x,N))%N,a+u*x,N);K=H(S.to_bytes((S.bit_length()+7)//8,'big'))
        M=H(bytes(a^b for a,b in zip(H(pad(N)),H(b'\5')))+H(b'Pair-Setup')+salt+A+B+K)
        m=parse(self.request('/pair-setup',tlv(t6=b'\3',t3=A,t4=M)));assert m[4]==H(A+M+K)
        self.id=b'MRN-independent-test';self.ed=ed25519.Ed25519PrivateKey.generate();pub=raw(self.ed)
        key=hk(K,'Pair-Setup-Encrypt-Salt','Pair-Setup-Encrypt-Info');prefix=hk(K,'Pair-Setup-Controller-Sign-Salt','Pair-Setup-Controller-Sign-Info')
        sub=tlv(t1=self.id,t3=pub,t10=self.ed.sign(prefix+self.id+pub));m=parse(self.request('/pair-setup',tlv(t6=b'\5',t5=ChaCha20Poly1305(key).encrypt(nonce('PS-Msg05'),sub,None))))
        sub=parse(ChaCha20Poly1305(key).decrypt(nonce('PS-Msg06'),m[5],None));self.server=sub[3];self.receiver_id=sub[1]
        ed25519.Ed25519PublicKey.from_public_bytes(self.server).verify(sub[10],hk(K,'Pair-Setup-Accessory-Sign-Salt','Pair-Setup-Accessory-Sign-Info')+sub[1]+sub[3])
        ephemeral=x25519.X25519PrivateKey.generate();epub=raw(ephemeral)
        m=parse(self.request('/pair-verify',tlv(t6=b'\1',t3=epub)));self.shared=ephemeral.exchange(x25519.X25519PublicKey.from_public_bytes(m[3]));key=hk(self.shared,'Pair-Verify-Encrypt-Salt','Pair-Verify-Encrypt-Info')
        sub=parse(ChaCha20Poly1305(key).decrypt(nonce('PV-Msg02'),m[5],None));assert sub[1]==self.receiver_id;ed25519.Ed25519PublicKey.from_public_bytes(self.server).verify(sub[10],m[3]+sub[1]+epub)
        sub=tlv(t1=self.id,t10=self.ed.sign(epub+self.id+m[3]));m=parse(self.request('/pair-verify',tlv(t6=b'\3',t5=ChaCha20Poly1305(key).encrypt(nonce('PV-Msg03'),sub,None))));assert m[6]==b'\4'
        self.tx=ChaCha20Poly1305(hk(self.shared,'Control-Salt','Control-Write-Encryption-Key'));self.rx=ChaCha20Poly1305(hk(self.shared,'Control-Salt','Control-Read-Encryption-Key'))
    def mfi(self):
        key=x25519.X25519PrivateKey.generate();pub=raw(key);body=self.request('/auth-setup',b'\1'+pub);shared=key.exchange(x25519.X25519PublicKey.from_public_bytes(body[:32]));n=struct.unpack('>I',body[32:36])[0];cert=body[36:36+n];size=struct.unpack('>I',body[36+n:40+n])[0];sig=body[40+n:];assert len(sig)==size==64
        decrypt=Cipher(algorithms.AES(hashlib.sha1(b'AES-KEY'+shared).digest()[:16]),modes.CTR(hashlib.sha1(b'AES-IV'+shared).digest()[:16])).decryptor();sig=decrypt.update(sig)+decrypt.finalize()
        from cryptography.hazmat.primitives.serialization.pkcs7 import load_der_pkcs7_certificates
        c=load_der_pkcs7_certificates(cert)[0];c.public_key().verify(utils.encode_dss_signature(int.from_bytes(sig[:32],'big'),int.from_bytes(sig[32:],'big')),body[:32]+pub,ec.ECDSA(hashes.SHA256()))
    def setup(self,body):return plistlib.loads(self.request('/stream',plistlib.dumps(body,fmt=plistlib.FMT_BINARY),'SETUP'))

def run():
    from auth_integration import identity
    synthetic_identity=tempfile.TemporaryDirectory(prefix='mrn-stream-test-')
    identity(Path(synthetic_identity.name))
    env=os.environ.copy();env['CARPLAY_IDENTITY']=synthetic_identity.name;env['CARPLAY_PAIRING_STORE']=str(BUILD/'test-peer-identity.dat')
    env['CARPLAY_REPORT']=str(BUILD/'synthetic-streaming.plist')
    # Deliberately stale settings must not resurrect a removed runtime backend.
    env['CARPLAY_FFMPEG']=str(BUILD/'missing-ffmpeg.exe')
    env['CARPLAY_VIDEO_BACKEND']='ffmpeg'
    (BUILD/'synthetic-streaming.plist').unlink(missing_ok=True)
    listener=BUILD/'receiver-listener.plist';listener.unlink(missing_ok=True)
    width,height=(1920,1080) if '--fullhd' in sys.argv else ((1280,720) if '--hd' in sys.argv else (800,480))
    process=subprocess.Popen([os.environ.get('CARPLAY_TEST_EXECUTABLE',str(BUILD/'WinXPlay.exe')),'--serve-test','17000','40',str(width),str(height)],env=env)
    buffered='--buffered' in sys.argv
    aac='--aac' in sys.argv or buffered
    results={'synthetic_peer':True,'physical_iPhone':False,'audio_codec':'AAC-LC' if aac else 'PCM','buffered':buffered}
    try:
        deadline=time.monotonic()+12
        while not listener.exists():
            assert process.poll() is None,f'Native startup exit {process.returncode}';assert time.monotonic()<deadline,'Native listener timed out';time.sleep(.1)
        peer=Peer(17000);peer.pair();results['pair_setup_and_verify']=True;peer.mfi();results['mfi_sap_signature_verified']=True
        info=plistlib.loads(peer.request('/info',method='GET'));assert info['displays'][0]['widthPixels']==width and info['displays'][0]['heightPixels']==height
        hid=info['hidDevices'][0]['hidDescriptor']
        assert hid.count(b'\x26'+struct.pack('<H',width)+b'\x09\x30')==2
        assert hid.count(b'\x26'+struct.pack('<H',height)+b'\x09\x31')==2
        session=peer.setup({'features':['mainBuffered']} if buffered else {})
        audio_options={'type':103 if buffered else (102 if aac else 100),'streamConnectionID':5678,'audioFormat':0x800000 if aac else 32768}
        buffered_key=os.urandom(32)
        if buffered:
            assert 'mainBuffered' in session['enabledFeatures'] and 'mainBufferedInfo' in info
            audio_options.update(shk=buffered_key,ct=4,spf=1024)
        streams=peer.setup({'streams':[{'type':110,'streamConnectionID':1234},audio_options]})['streams']
        peer.request('/stream',method='RECORD')
        v=socket.create_connection(('127.0.0.1',streams[0]['dataPort']));vkey=ChaCha20Poly1305(hk(peer.shared,'DataStream-Salt1234','DataStream-Output-Encryption-Key'))
        fixture=BUILD/'stream-test.h264'
        subprocess.run([os.environ.get('CARPLAY_FFMPEG', 'ffmpeg'),'-hide_banner','-loglevel','error','-f','lavfi','-i',f'testsrc=size={width}x{height}:rate=30','-frames:v','60','-c:v','libx264','-preset','ultrafast','-tune','zerolatency','-pix_fmt','yuv420p','-x264opts','aud=1:repeat-headers=1','-f','h264','-y',str(fixture)],check=True)
        nals=[n for n in re.split(b'\x00\x00\x00?\x01',fixture.read_bytes()) if n]
        frames=[];group=[]
        for n in nals:
            if n[0]&31==9 and group:frames.append(group);group=[]
            group.append(n)
        if group:frames.append(group)
        sps=next(n for n in nals if n[0]&31==7);pps=next(n for n in nals if n[0]&31==8)
        config=b'\1'+sps[1:4]+b'\xff\xe1'+struct.pack('>H',len(sps))+sps+b'\1'+struct.pack('>H',len(pps))+pps
        head=bytearray(128);struct.pack_into('<I',head,0,len(config));head[4]=1;v.sendall(head+config)
        audio=socket.create_connection(('127.0.0.1',streams[1]['dataPort'])) if buffered else socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        akey=ChaCha20Poly1305(buffered_key if buffered else hk(peer.shared,'DataStream-Salt5678','DataStream-Output-Encryption-Key'))
        for i,group in enumerate(frames):
            body=b''.join(struct.pack('>I',len(n))+n for n in group);head=bytearray(128);struct.pack_into('<I',head,0,len(body)+16);v.sendall(head+vkey.encrypt(bytes(4)+struct.pack('<Q',i),body,bytes(head)))
            # 1/30 second low-volume stereo sine, real waveOut playback.
            pcm=b''.join(struct.pack('>hh',int(700*math.sin(2*math.pi*440*(i*1600+j)/48000)),int(700*math.sin(2*math.pi*440*(i*1600+j)/48000))) for j in range(1600))
            header=struct.pack('>BBHII',128,96,i,i*1600,0);counter=struct.pack('<Q',i)
            if not aac:audio.sendto(header+akey.encrypt(bytes(4)+counter,pcm,header[4:])+counter,('127.0.0.1',streams[1]['dataPort']))
            time.sleep(1/30)
        audio_count=len(frames)
        if aac:
            encoded=BUILD/'stream-test.aac'
            subprocess.run([os.environ.get('CARPLAY_FFMPEG', 'ffmpeg'),'-hide_banner','-loglevel','error','-f','lavfi','-i','sine=frequency=440:sample_rate=48000:duration=2','-ac','2','-c:a','aac','-f','adts','-y',str(encoded)],check=True)
            data=encoded.read_bytes();packets=[]
            while data:
                size=((data[3]&3)<<11)|(data[4]<<3)|(data[5]>>5);assert size>=7 and len(data)>=size
                packets.append(data[7:size]);data=data[size:]
            for i,payload in enumerate(packets):
                header=struct.pack('>BBHII',128,96,i,i*1024,0);counter=struct.pack('<Q',i)
                packet=header+akey.encrypt(bytes(4)+counter,payload,header[4:])+counter
                if buffered:
                    framed=struct.pack('>H',len(packet)+2)+packet
                    audio.sendall(framed[:1]);audio.sendall(framed[1:17]);audio.sendall(framed[17:])
                else:
                    audio.sendto(packet,('127.0.0.1',streams[1]['dataPort']))
                    time.sleep(1024/48000)
            audio_count=len(packets)
        if buffered:
            time.sleep(1.2)
            before=plistlib.loads((BUILD/'synthetic-streaming.plist').read_bytes())
            assert before['audio_played_bytes']==0,before
            result=plistlib.loads(peer.request('/stream',plistlib.dumps({'rtpTime':0,'rate':1.0},fmt=plistlib.FMT_BINARY),'SETRATEANCHORTIME'))
            assert result['rate']==1.0 and abs(result['networkTimeSecs']-time.time())<3,result
            time.sleep(1)
            paused=plistlib.loads(peer.request('/stream',plistlib.dumps({'rate':0.0},fmt=plistlib.FMT_BINARY),'SETRATE'))
            assert 0<paused['rtpTime']<len(packets)*1024,paused
            time.sleep(.3)
            anchor=plistlib.loads(peer.request('/stream',method='GETANCHOR'))
            assert anchor['rate']==0 and anchor['rtpTime']==paused['rtpTime'],anchor
            peer.request('/stream',plistlib.dumps({'rtpTime':anchor['rtpTime'],'rate':1.0},fmt=plistlib.FMT_BINARY),'SETRATE')
            time.sleep(2.5)
            feedback=plistlib.loads(peer.request('/feedback'))
            assert feedback['streams'][0]['sampleTime']>70000,feedback
            peer.request('/stream',plistlib.dumps({'streams':[{'type':103}]},fmt=plistlib.FMT_BINARY),'TEARDOWN')
            assert plistlib.loads(peer.request('/info',method='GET'))['name']=='WinXPlay'
            results['pause_resume_and_stream_teardown']=True
        time.sleep(2);v.shutdown(socket.SHUT_WR);time.sleep(2)
        stats=plistlib.loads((BUILD/'synthetic-streaming.plist').read_bytes());results['receiver']=stats
        assert stats['video_frames']>=55,stats
        assert (stats['video_width'],stats['video_height'])==(width,height),stats
        assert stats['audio_packets']==audio_count,stats
        assert stats.get('audio_played_bytes',0)>=300000,stats
        results['passed']=True
    finally:
        if process.poll() is None:process.terminate()
        process.wait(timeout=5)
        synthetic_identity.cleanup()
        name=f'streaming-{width}x{height}-integration.json' if width!=800 else ('streaming-buffered-integration.json' if buffered else ('streaming-aac-integration.json' if aac else 'streaming-integration.json'))
        (BUILD/name).write_text(json.dumps(results,indent=2,ensure_ascii=False),encoding='utf-8')
    print(json.dumps(results,indent=2,ensure_ascii=False))
if __name__=='__main__':run()
