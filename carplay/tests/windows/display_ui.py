"""Validate fullscreen and Esc restore at the user's current resolution."""
import ctypes as c,ctypes.wintypes as w,time,json,plistlib,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2];build=root/'../build/carplay/windows-x64'
u=c.WinDLL('user32');u.SetProcessDPIAware()
u.FindWindowW.argtypes=[w.LPCWSTR,w.LPCWSTR];u.FindWindowW.restype=w.HWND
u.GetDlgItem.argtypes=[w.HWND,c.c_int];u.GetDlgItem.restype=w.HWND
u.GetWindow.argtypes=[w.HWND,w.UINT];u.GetWindow.restype=w.HWND
u.SendMessageW.argtypes=[w.HWND,w.UINT,c.c_size_t,c.c_ssize_t];u.SendMessageW.restype=c.c_ssize_t
u.PostMessageW.argtypes=[w.HWND,w.UINT,c.c_size_t,c.c_ssize_t]
u.IsWindowVisible.argtypes=[w.HWND]
u.GetWindowRect.argtypes=[w.HWND,c.POINTER(w.RECT)]
u.GetWindowLongPtrW.argtypes=[w.HWND,c.c_int];u.GetWindowLongPtrW.restype=c.c_ssize_t
u.MonitorFromWindow.argtypes=[w.HWND,w.DWORD];u.MonitorFromWindow.restype=w.HANDLE
class Monitor(c.Structure):_fields_=[('size',w.DWORD),('monitor',w.RECT),('work',w.RECT),('flags',w.DWORD)]
u.GetMonitorInfoW.argtypes=[w.HANDLE,c.POINTER(Monitor)]
h=u.FindWindowW('MRN_NATIVE_CARPLAY',None);assert h
def rect():
 r=w.RECT();assert u.GetWindowRect(h,c.byref(r));return(r.left,r.top,r.right,r.bottom)
def wait(predicate,seconds=10):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  if predicate():return
  time.sleep(.1)
 raise AssertionError('Window transition timed out')
def stats():
 try:return plistlib.loads((build/'streaming-diagnostics.plist').read_bytes())
 except Exception:return {}
assert not u.GetWindow(h,5), 'Projection window must not contain toolbar controls'
wait(lambda:stats().get('video_frames',0)>40 and stats().get('audio_played_bytes',0)>0,45)
u.PostMessageW(h,0x100,0x1b,0)
wait(lambda:u.GetWindowLongPtrW(h,-16)&0x00cf0000!=0)
before=rect();style=u.GetWindowLongPtrW(h,-16)
try:
 assert u.PostMessageW(h,0x100,0x7a,0)
 wait(lambda:u.GetWindowLongPtrW(h,-16)&0x00cf0000==0)
 assert not u.GetWindow(h,5)
 assert u.GetWindowLongPtrW(h,-16)&0x00cf0000==0
 m=Monitor();m.size=c.sizeof(m);assert u.GetMonitorInfoW(u.MonitorFromWindow(h,2),c.byref(m))
 assert rect()==(m.monitor.left,m.monitor.top,m.monitor.right,m.monitor.bottom),(rect(),m.monitor)
 subprocess.run([sys.executable,str(Path(__file__).with_name('capture_receiver.py'))],check=True,capture_output=True)
 (build/'fullscreen-capture.png').write_bytes((build/'receiver-capture.png').read_bytes())
finally:
 u.PostMessageW(h,0x100,0x1b,0)
wait(lambda:u.GetWindowLongPtrW(h,-16)==style)
assert rect()==before,(before,rect())
assert u.GetWindowLongPtrW(h,-16)==style
assert not u.GetWindow(h,5)
result={'passed':True,'physical_media':stats(),'no_toolbar':True,'fullscreen_monitor_bounds':True,'F11':True,'Esc_restores_window':True}
(build/'display-ui-results.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
