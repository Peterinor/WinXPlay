"""Exercise the real CarKit loop and plist state against an independent TLS/iAP2 peer."""
import json, socket, struct, sys, time
from pathlib import Path
import tls_integration as t
t.build=Path(sys.argv[1]).resolve()
host=t.build/'network_session_host.exe'
observed={}

def parameters(body):
    fields={}
    while body:
        n,kind=struct.unpack_from('>HH',body)
        assert 4<=n<=len(body)
        fields[kind]=body[4:n];body=body[n:]
    return fields

def controls(sock, closed=False):
    # Finish identification, then announce availability while no address exists.
    sock.sendall(t.link(0x40,11,101,10,t.csm(0x1d00)))
    h,msg,body=t.receive_csm(sock)
    assert msg==0x1d01
    fields=parameters(body)
    observed['serial']=fields[3].rstrip(b'\0').decode()
    assert observed['serial'].startswith('WXP-')
    sock.sendall(t.link(0x40,12,h[5],10,t.csm(0x1d02)))
    sock.sendall(t.link(0x40,13,h[5],10,t.csm(0x4300)))
    began=time.monotonic(); sock.settimeout(.25); started=False
    # Read full frames; only the initial header byte may time out.
    while time.monotonic()-began<8:
        try: first=sock.recv(1)
        except socket.timeout:
            if closed and time.monotonic()-began>1.5: return
            continue
        assert first, 'CarKit closed while waiting for its first network address'
        sock.settimeout(2)
        header=first+t.exact(sock,8); size=struct.unpack_from('>H',header,2)[0]
        body=t.exact(sock,size-9);sock.settimeout(.25)
        assert sum(header)%256==0
        if not body: continue
        assert sum(body)%256==0
        message=struct.unpack_from('>H',body,4)[0]
        sock.sendall(t.link(0x40,13,header[5],0))
        if message==0x4301:
            observed['device_id']=parameters(body[6:-1])[3].rstrip(b'\0').decode()
            assert not closed and time.monotonic()-began>=1.5
            started=True;break
    assert started, 'No start message after the network became ready'

results=[]
r=t.scenario('mfi_ok',host,controls)['native_result']
assert r['test_lookups']==3 and not r['test_waiting'] and r['carplay_start_queued'],r
assert 'Missing plist key' not in r.get('error',''),r
assert observed['device_id']==r['test_device_id'] and observed['serial']==r['test_serial']
results.append({'scenario':'actual_first_wait_then_network_ready','passed':True})
r=t.scenario('mfi_ok',host,lambda s:controls(s,True),{'WINXPLAY_WAIT_TEST':'closed'})['native_result']
assert r['test_lookups']>=2 and r['test_waiting'] and not r.get('carplay_start_queued'),r
assert r['failed_stage']=='receiver_network_address',r
results.append({'scenario':'peer_closed_while_waiting_retains_retry_gate','passed':True})
r=t.scenario('service_refused',host)['native_result']
assert not r['test_waiting'] and r['test_lookups']==0,r
results.append({'scenario':'early_service_failure_has_network_state','passed':True})
r=t.scenario('trust_only',host,extra_env={'WINXPLAY_WAIT_TEST':'trust_only'})['native_result']
assert r['lockdown_tls'] and not r['carkit_service_opened'],r
results.append({'scenario':'restore_trust_without_restarting_carplay','passed':True})
(t.build/'network-session-results.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
