"""Real-device test: startup, bind-failure retry, pause/resume, recovery and reconnect.

Requires the configured iPhone. Leaves the receiver open after success.
"""
import ctypes as c
from ctypes import wintypes as w
import json, os, plistlib, socket, subprocess, time
from pathlib import Path

root = Path(__file__).resolve().parents[2]
build = root / '../build/carplay/windows-x64'
u = c.WinDLL('user32')
u.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]
u.FindWindowW.restype = w.HWND
u.PostMessageW.argtypes = [w.HWND, w.UINT, c.c_size_t, c.c_ssize_t]
assert not u.FindWindowW('MRN_NATIVE_CARPLAY', None), 'Close existing receiver first'

def read(name):
    try:
        return plistlib.loads((build / name).read_bytes())
    except (OSError, ValueError):
        return {}

def wait(predicate, seconds=30):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(.2)
    raise AssertionError('Timed out; state=' + repr(read('auto-connect.plist')))

env = os.environ.copy()
# Synthetic/offline check: do not default to a real accessory identity.
blocker = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
blocker.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
blocker.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
blocker.bind(('::', 7000))
blocker.listen(1)
started = time.time()
process = subprocess.Popen([str(build / 'WinXPlay.exe')], env=env)
h = None
try:
    wait(lambda: u.FindWindowW('MRN_NATIVE_CARPLAY', None))
    h = u.FindWindowW('MRN_NATIVE_CARPLAY', None)
    state_file = build / 'auto-connect.plist'
    wait(lambda: state_file.exists() and state_file.stat().st_mtime >= started
         and read(state_file.name).get('attempts', 0) >= 2
         and read(state_file.name).get('retry_pending'))
    failed = read(state_file.name)
    assert 'bind' in failed['status'], failed
    u.PostMessageW(h, 0x111, 110, 0)
    wait(lambda: not read(state_file.name).get('automatic', True))
    attempts = read(state_file.name)['attempts']
    time.sleep(4)
    assert read(state_file.name)['attempts'] == attempts
    u.PostMessageW(h, 0x111, 110, 0)
    wait(lambda: read(state_file.name).get('attempts', 0) > attempts)
    blocker.close()
    media = build / 'streaming-diagnostics.plist'
    def receiving():
        state = read(media.name)
        return (media.exists() and media.stat().st_mtime >= started
                and not state.get('session_ended') and state.get('video_frames', 0) > 20
                and state.get('audio_played_bytes', 0) > 0)
    wait(receiving, 60)
    recovered = read(media.name)
    attempts = read(state_file.name)['attempts']
    u.PostMessageW(h, 0x111, 109, 0)
    wait(lambda: read(state_file.name).get('attempts', 0) > attempts)
    time.sleep(2)
    wait(receiving, 60)
    result = dict(passed=True, default_startup=True, bind_failure_retry=failed['attempts'],
                  pause_stops_retries=True, resume=True, recovered_video_frames=recovered['video_frames'],
                  recovered_audio_bytes=recovered['audio_played_bytes'], manual_reconnect=True,
                  final_media=read(media.name))
    (build / 'auto-connect-ui-results.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
except BaseException:
    if h:
        u.PostMessageW(h, 0x10, 0, 0)
    raise
finally:
    blocker.close()
