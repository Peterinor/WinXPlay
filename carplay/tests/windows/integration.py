"""Independent fragmented Windows usbmux/Lockdown peer and artifact validation."""
from pathlib import Path
import socket, struct, threading, subprocess, plistlib, json

build = Path(__file__).resolve().parents[2] / '../build/carplay/windows-x64'

def exact(sock, count):
    data = b''
    while len(data) < count:
        block = sock.recv(count-len(data))
        if not block: raise EOFError('native peer closed early')
        data += block
    return data

def request(sock):
    length, version, kind, tag = struct.unpack('<IIII', exact(sock,16))
    assert version == 1 and kind == 8 and 16 <= length < 1024*1024
    return tag, plistlib.loads(exact(sock,length-16))

def send(sock, tag, value, malformed=False):
    body = plistlib.dumps(value,fmt=plistlib.FMT_BINARY)
    data = struct.pack('<IIII',len(body)+16,1,8,tag+(1 if malformed else 0))+body
    # Force native recvExact to cope with split headers and split plist data.
    for pos in range(0,len(data),3): sock.sendall(data[pos:pos+3])

def scenario(kind):
    server = socket.socket()
    server.bind(('127.0.0.1',0)); server.listen(); server.settimeout(8)
    errors=[]
    def peer():
        try:
            with server.accept()[0] as sock:
                sock.settimeout(5)
                tag, value = request(sock)
                assert value['MessageType']=='ListDevices'
                send(sock,tag,{'DeviceList':[{'DeviceID':17,'Properties':{
                    'SerialNumber':'SYNTHETIC-NONPRIVATE','ConnectionType':'USB'}}]},kind=='bad_tag')
            if kind=='bad_tag': return
            with server.accept()[0] as sock:
                sock.settimeout(5)
                tag,value=request(sock)
                assert value['MessageType']=='Connect' and value['DeviceID']==17
                assert value['PortNumber']==socket.htons(62078)
                send(sock,tag,{'Number':5 if kind=='refused' else 0})
                if kind=='refused': return
                size=struct.unpack('>I',exact(sock,4))[0]
                value=plistlib.loads(exact(sock,size))
                assert value['Request']=='GetValue' and value['Key']=='ProductVersion'
                response=plistlib.dumps({'Value':'test-ios'})
                data=struct.pack('>I',len(response))+response
                for pos in range(0,len(data),5): sock.sendall(data[pos:pos+5])
        except Exception as exc:
            # An intentionally wrong tag may be rejected before the peer finishes sending.
            if not (kind=='bad_tag' and isinstance(exc,(ConnectionResetError,BrokenPipeError))): errors.append(repr(exc))
        finally: server.close()
    port=server.getsockname()[1]
    thread=threading.Thread(target=peer,daemon=True);thread.start()
    result=subprocess.run([str(build/'carplay_tests.exe'),'--mux',str(port)],cwd=build,
                          capture_output=True,text=True,timeout=15)
    thread.join(10)
    assert not thread.is_alive() and not errors,errors
    assert result.returncode==(0 if kind=='ok' else 2),(kind,result.stdout,result.stderr)
    return {'scenario':kind,'passed':True,'native_output':result.stdout.strip()}

if __name__ == '__main__':
    results=[scenario(k) for k in ('ok','bad_tag','refused')]
    assert plistlib.loads((build/'fixtures/native.bplist').read_bytes()) == plistlib.loads((build/'fixtures/independent.bplist').read_bytes())
    results.append({'scenario':'C++ binary plist decoded independently by Python','passed':True})
    (build/'integration-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(json.dumps(results,indent=2))
