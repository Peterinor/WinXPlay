"""Real hidden Win32 window -> shared input -> encrypted event socket.
No iPhone, physical touch hardware or real microphone is used.
"""
import ctypes, json, os, plistlib, re, socket, struct, subprocess, tempfile, time
from pathlib import Path
from streaming_integration import Peer, hk, exact, BUILD
from auth_integration import identity
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

user=ctypes.WinDLL('user32',use_last_error=True)
user.SendMessageW.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_size_t,ctypes.c_ssize_t]
user.SendMessageW.restype=ctypes.c_ssize_t
class Rect(ctypes.Structure):_fields_=[(k,ctypes.c_long) for k in ('left','top','right','bottom')]
user.GetClientRect.argtypes=[ctypes.c_void_p,ctypes.POINTER(Rect)]

class Events:
    def __init__(self,port,secret):
        self.s=socket.create_connection(('127.0.0.1',port));self.s.settimeout(3)
        self.key=ChaCha20Poly1305(hk(secret,'Events-Salt','Events-Write-Encryption-Key'))
        self.counter=0;self.pending=b''
    def read(self):
        while True:
            if b'\r\n\r\n' in self.pending:
                head,body=self.pending.split(b'\r\n\r\n',1)
                size=int(re.search(br'Content-Length: (\d+)',head,re.I)[1])
                if len(body)>=size:
                    self.pending=body[size:];return plistlib.loads(body[:size])
            size=exact(self.s,2);n=struct.unpack('<H',size)[0]
            self.pending+=self.key.decrypt(bytes(4)+struct.pack('<Q',self.counter),exact(self.s,n+16),size)
            self.counter+=1

def run():
    listener=BUILD/'input-listener.plist';listener.unlink(missing_ok=True)
    closed=BUILD/'input-microphone-closed.flag';closed.unlink(missing_ok=True)
    result={'physical_iPhone':False,'real_microphone':False,'passed':False}
    with tempfile.TemporaryDirectory(prefix='winxplay-input-') as folder:
        identity(Path(folder));env=os.environ.copy();env['CARPLAY_IDENTITY']=folder
        env['CARPLAY_PAIRING_STORE']=str(Path(folder)/'pair.dat')
        env['CARPLAY_REPORT']=str(BUILD/'input-streaming.plist')
        for setting in ('CARPLAY_MICROPHONE','CARPLAY_TOUCH','CARPLAY_CONTROLS'):env[setting]='1'
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
        process=subprocess.Popen([str(BUILD/'input_host.exe')],env=env,startupinfo=startup)
        event=None;mic=None;peer=None
        try:
            deadline=time.monotonic()+12
            while not listener.exists():
                assert process.poll() is None and time.monotonic()<deadline,'Input host startup failed'
                time.sleep(.05)
            h=plistlib.loads(listener.read_bytes())['window'];send=lambda m,w=0,l=0:user.SendMessageW(h,m,w,l)
            peer=Peer(17001);peer.pair();peer.mfi();session=peer.setup({})
            info=plistlib.loads(peer.request('/info',method='GET'))
            assert info['deviceID']==peer.receiver_id.decode() and info['bluetoothIDs']==[info['deviceID']]
            assert [x['uuid'] for x in info['hidDevices']]==['2a2a2a2a','2a2a2a2b','2a2a2a2c','2a2a2a2d']
            assert all(x['displayUUID']==info['displays'][0]['uuid'] for x in info['hidDevices'])
            event=Events(session['eventPort'],peer.shared)
            assert event.read()=={'type':'setNightMode','params':{'nightMode':False}}
            duplicate=socket.create_connection(('127.0.0.1',session['eventPort']));duplicate.settimeout(3)
            assert duplicate.recv(1)==b'';duplicate.close()
            # Actual client geometry: click outside letterbox is ignored, centre maps to midpoint.
            r=Rect();assert user.GetClientRect(h,ctypes.byref(r))
            centre=(r.right//2)|((r.bottom//2)<<16)
            send(0x201,1,0);send(0x202,0,0)
            send(0x201,1,centre);send(0x202,0,centre)
            down,up=event.read(),event.read()
            assert down['uuid']=='2a2a2a2a' and down['hidReport'][1]==1 and up['hidReport'][1]==0
            x,y=struct.unpack_from('<HH',down['hidReport'],2);assert abs(x-400)<=2 and abs(y-240)<=2
            # Capture loss releases; a subsequent move cannot start another press.
            send(0x201,1,centre);assert event.read()['hidReport'][1]==1
            send(0x215);release=[event.read() for _ in range(4)];assert release[0]['hidReport'][1]==0
            send(0x200,1,centre)
            # Keyboard, relative wheel, media and telephony, Siri press/release.
            send(0x100,13);send(0x101,13)
            assert [event.read()['hidReport'][0] for _ in range(2)]==[1,0]
            send(0x20a,120<<16);assert event.read()['hidReport'][3]==255;assert event.read()['hidReport'][3]==0
            send(0x100,32);send(0x101,32)
            assert [(v['uuid'],v['hidReport'][0]) for v in (event.read(),event.read())]==[('2a2a2a2c',3),('2a2a2a2c',0)]
            assert send(0x319,0,11<<16)==1
            assert event.read()['hidReport']==b'\x04';assert event.read()['hidReport']==b'\x00'
            # MRN MODE emits standard Home; SEEK emits standard media keys.
            send(0x100,0x24);send(0x101,0x24)
            home,neutral=event.read(),event.read()
            assert home['uuid']=='2a2a2a2b' and home['hidReport'][0]==2 and neutral['hidReport'][0]==0
            for key,index in ((0xb0,4),(0xb1,5),(0xb2,2),(0xb3,3)):
                send(0x100,key);send(0x101,key)
                pressed,released=event.read(),event.read()
                assert pressed['uuid']=='2a2a2a2c' and pressed['hidReport']==bytes([index]) and released['hidReport']==b'\0'
            for key,index in ((0x75,1),(0x76,4)):
                send(0x100,key);send(0x101,key)
                assert event.read()['hidReport']==bytes([index]);assert event.read()['hidReport']==b'\0'
            send(0x100,0x77);send(0x101,0x77)
            assert [event.read()['params']['siriAction'] for _ in range(2)]==[2,3]
            # OEM pointer-free events support two contacts even without a touch driver.
            send(0x8000+20,0x100,0x80008000);send(0x8000+20,0x101,0xffffffff)
            first,second=event.read(),event.read();assert first['hidReport'][1]==1
            assert second['hidReport'][1]==second['hidReport'][7]==1
            send(0x8000+24);assert event.read()['hidReport'][1]==0
            for _ in range(3):event.read()
            send(0x8000+23,1);assert event.read()['params']['nightMode'] is True
            send(0x8000+25);assert event.read()['type']=='forceKeyFrame'
            result['window_to_encrypted_input']=True
            # A synthetic microphone exercises the real negotiation/encryption/UDP path.
            mic=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);mic.bind(('127.0.0.1',0));mic.settimeout(3)
            peer.setup({'streams':[{'type':100,'streamConnectionID':3456,'audioFormat':16,
                'audioType':'telephony','dataPort':mic.getsockname()[1],'framesPerPacket':320}]})
            key=ChaCha20Poly1305(hk(peer.shared,'DataStream-Salt3456','DataStream-Input-Encryption-Key'))
            for i in range(3):
                packet=mic.recv(4096);header=packet[:12];counter=packet[-8:]
                pcm=key.decrypt(bytes(4)+counter,packet[12:-8],header[4:])
                assert pcm==b'\x12\x34'*320
                assert struct.unpack_from('>H',header,2)[0]==i and struct.unpack_from('>I',header,4)[0]==i*320
            peer.request('/stream',plistlib.dumps({'streams':[{'type':100}]},fmt=plistlib.FMT_BINARY),'TEARDOWN')
            assert closed.exists(),'Microphone not released by stream teardown'
            result['synthetic_microphone_uplink_and_teardown']=True
            # Authenticated phone requests are dispatched to the actual host UI handler.
            host=BUILD/'host-events.plist'
            for command in ({'type':'modesChanged','params':{'resources':[]}},
                            {'type':'requestUI','params':{'url':'car:'}}):
                host.unlink(missing_ok=True)
                peer.request('/command',plistlib.dumps(command,fmt=plistlib.FMT_BINARY))
                deadline=time.monotonic()+3
                while not host.exists() and time.monotonic()<deadline:time.sleep(.02)
                report=plistlib.loads(host.read_bytes())
                assert report['type']==command['type']
                if command['type']=='requestUI':assert report['host_ui_requested'] is True
            result['phone_commands_to_host']=True
        finally:
            for s in (event.s if event else None,mic,peer.s if peer else None):
                if s:s.close()
            if process.poll() is None:
                if listener.exists():user.SendMessageW(plistlib.loads(listener.read_bytes())['window'],0x10,0,0)
                try:process.wait(timeout=8)
                except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=5)
            (BUILD/'input-integration.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        subprocess.run([str(BUILD/'input_host.exe'),'--microphone-unavailable'],env=env,
                       startupinfo=startup,check=True,timeout=10)
        report=plistlib.loads((BUILD/'audio-input.plist').read_bytes())
        assert report['stage']=='closed' and report['device_failures']==2 and not report['device_open']
        result['unavailable_microphone_retry_and_release']=True
        result['passed']=True
        (BUILD/'input-integration.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))
if __name__=='__main__':run()
