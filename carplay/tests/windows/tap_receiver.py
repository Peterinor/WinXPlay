"""Send a test tap through our receiver's normal window input path."""
import ctypes as c,ctypes.wintypes as w,time,sys
import plistlib
from pathlib import Path
u=c.WinDLL('user32');u.SetProcessDPIAware()
u.FindWindowW.argtypes=[w.LPCWSTR,w.LPCWSTR];u.FindWindowW.restype=w.HWND
u.GetClientRect.argtypes=[w.HWND,c.POINTER(w.RECT)]
u.SendMessageW.argtypes=[w.HWND,w.UINT,c.c_size_t,c.c_ssize_t]
h=u.FindWindowW('MRN_NATIVE_CARPLAY',None);assert h
state=plistlib.loads((Path(__file__).resolve().parents[2]/'../build/carplay/windows-x64/streaming-diagnostics.plist').read_bytes());width,height=state['video_width'],state['video_height']
u.GetWindowLongPtrW.argtypes=[w.HWND,c.c_int];u.GetWindowLongPtrW.restype=c.c_ssize_t
full=u.GetWindowLongPtrW(h,-16)&0x00cf0000==0
r=w.RECT();u.GetClientRect(h,c.byref(r));available_width=r.right;available_height=r.bottom
scale=min(available_width/width,available_height/height);pw,ph=int(width*scale),int(height*scale)
x=int((r.right-pw)/2+pw*float(sys.argv[1]));y=int((available_height-ph)/2+ph*float(sys.argv[2]));lp=x+(y<<16)
u.SendMessageW(h,0x201,1,lp);time.sleep(.08);u.SendMessageW(h,0x202,0,lp)
