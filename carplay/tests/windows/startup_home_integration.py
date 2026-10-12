"""Actual receiver + encrypted RTSP/HID, synthetic video sink; no real iPhone."""
import json,os,plistlib,select,socket,struct,subprocess,tempfile,time
from pathlib import Path
from input_integration import Events,user
from streaming_integration import Peer,hk,BUILD
from auth_integration import identity
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

def quiet(event,seconds=.65):
    assert not event.pending
    assert not select.select([event.s],[],[],seconds)[0],'Unexpected repeated/early Home command'

def scenario(mode):
    with tempfile.TemporaryDirectory(prefix='winxplay-home-') as directory:
        root=Path(directory);identity(root)
        env=os.environ.copy();env.update(CARPLAY_IDENTITY=directory,
            CARPLAY_PAIRING_STORE=str(root/'pair.dat'),CARPLAY_REPORT=str(root/'report.plist'),
            CARPLAY_CONTROLS='0' if mode=='disabled' else '1',CARPLAY_TOUCH='1')
        listener=BUILD/'input-listener.plist';listener.unlink(missing_ok=True)
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
        process=subprocess.Popen([str(BUILD/'input_host.exe'),'--startup-home'],env=env,startupinfo=startup)
        peer=None;video=None;event=None;hwnd=None
        try:
            until=time.monotonic()+12
            while not listener.exists():
                assert process.poll() is None and time.monotonic()<until
                time.sleep(.03)
            hwnd=plistlib.loads(listener.read_bytes())['window']
            peer=Peer(17001);peer.pair();peer.mfi();session=peer.setup({})
            if mode!='event_late':
                event=Events(session['eventPort'],peer.shared)
                assert event.read()['type']=='setNightMode'
                quiet(event,.55) # Event established alone must not send Home.
            if mode=='user_started':
                user.SendMessageW(hwnd,0x100,13,0);user.SendMessageW(hwnd,0x101,13,0)
                assert [event.read()['hidReport'][0] for _ in range(2)]==[1,0]
            streams=peer.setup({'streams':[{'type':110,'streamConnectionID':1234}]})['streams']
            video=socket.create_connection(('127.0.0.1',streams[0]['dataPort']))
            if mode!='record_late':peer.request('/stream',method='RECORD')
            if event:quiet(event,.6) # Socket/RECORD alone is not first video.
            cipher=ChaCha20Poly1305(hk(peer.shared,'DataStream-Salt1234','DataStream-Output-Encryption-Key'))
            def frame(counter):
                body=b'\x00\x00\x00\x02\x65\x80';head=bytearray(128);struct.pack_into('<I',head,0,len(body)+16)
                video.sendall(head+cipher.encrypt(bytes(4)+struct.pack('<Q',counter),body,bytes(head)))
            frame(0)
            if mode=='record_late':
                quiet(event,.75);peer.request('/stream',method='RECORD')
            if mode=='event_late':
                time.sleep(.75);event=Events(session['eventPort'],peer.shared)
                assert event.read()['type']=='setNightMode'
            if mode not in ('user_started','disabled'):
                down,up=event.read(),event.read()
                assert down=={'type':'hidSendReport','uuid':'2a2a2a2b','hidReport':b'\x02\0\0\0'}
                assert up=={'type':'hidSendReport','uuid':'2a2a2a2b','hidReport':bytes(4)}
            quiet(event,.85)
            peer.request('/stream',method='RECORD');frame(1);quiet(event,.7)
        finally:
            for s in (video,event.s if event else None,peer.s if peer else None):
                if s:s.close()
            if process.poll() is None:
                if hwnd:user.SendMessageW(hwnd,0x10,0,0)
                try:process.wait(timeout=8)
                except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=5)

if __name__=='__main__':
    cases=['normal','user_started','disabled','record_late','event_late','reconnect']
    for case in cases:
        scenario(case);print('PASS',case,flush=True)
    result={'passed':True,'cases':cases,'physical_iphone':False,'dashboard_verified':False}
    (BUILD/'startup-home-integration.json').write_text(json.dumps(result,indent=2))
