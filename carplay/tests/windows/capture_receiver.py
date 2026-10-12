"""Capture only our native receiver's client rendering for visual verification."""
import ctypes as c,ctypes.wintypes as w,sys
from pathlib import Path
from PIL import Image
u=c.WinDLL('user32');g=c.WinDLL('gdi32');u.SetProcessDPIAware()
u.FindWindowW.argtypes=[w.LPCWSTR,w.LPCWSTR];u.FindWindowW.restype=w.HWND
u.GetClientRect.argtypes=[w.HWND,c.POINTER(w.RECT)]
u.GetDC.argtypes=[w.HWND];u.GetDC.restype=w.HDC
u.SendMessageW.argtypes=[w.HWND,w.UINT,c.c_size_t,c.c_ssize_t];u.SendMessageW.restype=c.c_ssize_t
u.ReleaseDC.argtypes=[w.HWND,w.HDC]
g.CreateCompatibleDC.argtypes=[w.HDC];g.CreateCompatibleDC.restype=w.HDC
g.CreateCompatibleBitmap.argtypes=[w.HDC,c.c_int,c.c_int];g.CreateCompatibleBitmap.restype=w.HBITMAP
g.SelectObject.argtypes=[w.HDC,w.HGDIOBJ];g.SelectObject.restype=w.HGDIOBJ
g.GetDIBits.argtypes=[w.HDC,w.HBITMAP,w.UINT,w.UINT,c.c_void_p,c.c_void_p,w.UINT]
g.DeleteObject.argtypes=[w.HGDIOBJ];g.DeleteDC.argtypes=[w.HDC]
h=u.FindWindowW('MRN_NATIVE_CARPLAY',None);assert h,'Receiver window missing'
r=w.RECT();u.GetClientRect(h,c.byref(r));width,height=r.right,r.bottom
dc=u.GetDC(h);mem=g.CreateCompatibleDC(dc);bmp=g.CreateCompatibleBitmap(dc,width,height);old=g.SelectObject(mem,bmp)
u.PrintWindow.argtypes=[w.HWND,w.HDC,w.UINT];assert u.PrintWindow(h,mem,1);g.SelectObject(mem,old)
import struct
info=c.create_string_buffer(struct.pack('<IiiHHIIiiII',40,width,-height,1,32,0,width*height*4,0,0,0,0));pixels=c.create_string_buffer(width*height*4)
assert g.GetDIBits(mem,bmp,0,height,pixels,info,0)==height
out=Path(__file__).resolve().parents[2]/'../build/carplay/windows-x64/receiver-capture.png';Image.frombytes('RGB',(width,height),pixels.raw,'raw','BGRX').save(out)
g.DeleteObject(bmp);g.DeleteDC(mem);u.ReleaseDC(h,dc);print(out)
