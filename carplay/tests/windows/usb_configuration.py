"""WinUSB descriptor/CarPlay transition diagnostic. Never installs a driver."""
import ctypes as c, ctypes.wintypes as w, json, sys, uuid
from pathlib import Path
class GUID(c.Structure):
    _fields_=[('data',c.c_ubyte*16)]
class Interface(c.Structure):
    _fields_=[('size',w.DWORD),('guid',GUID),('flags',w.DWORD),('reserved',c.c_size_t)]
class Setup(c.Structure):
    _pack_=1
    _fields_=[('type',c.c_ubyte),('request',c.c_ubyte),('value',c.c_ushort),('index',c.c_ushort),('length',c.c_ushort)]
class Overlapped(c.Structure):
    _fields_=[('internal',c.c_size_t),('internalHigh',c.c_size_t),('offset',w.DWORD),('offsetHigh',w.DWORD),('event',w.HANDLE)]
s=c.WinDLL('setupapi',use_last_error=True);k=c.WinDLL('kernel32',use_last_error=True);u=c.WinDLL('winusb',use_last_error=True)
s.SetupDiGetClassDevsW.argtypes=[c.POINTER(GUID),w.LPCWSTR,w.HWND,w.DWORD];s.SetupDiGetClassDevsW.restype=w.HANDLE
s.SetupDiEnumDeviceInterfaces.argtypes=[w.HANDLE,c.c_void_p,c.POINTER(GUID),w.DWORD,c.POINTER(Interface)]
s.SetupDiGetDeviceInterfaceDetailW.argtypes=[w.HANDLE,c.POINTER(Interface),c.c_void_p,w.DWORD,c.POINTER(w.DWORD),c.c_void_p]
s.SetupDiDestroyDeviceInfoList.argtypes=[w.HANDLE]
k.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,c.c_void_p,w.DWORD,w.DWORD,w.HANDLE];k.CreateFileW.restype=w.HANDLE
k.CloseHandle.argtypes=[w.HANDLE]
k.CreateEventW.argtypes=[c.c_void_p,w.BOOL,w.BOOL,w.LPCWSTR];k.CreateEventW.restype=w.HANDLE
k.DeviceIoControl.argtypes=[w.HANDLE,w.DWORD,c.c_void_p,w.DWORD,c.c_void_p,w.DWORD,c.POINTER(w.DWORD),c.POINTER(Overlapped)]
k.GetOverlappedResult.argtypes=[w.HANDLE,c.POINTER(Overlapped),c.POINTER(w.DWORD),w.BOOL]
k.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD]
k.CancelIo.argtypes=[w.HANDLE]
u.WinUsb_Initialize.argtypes=[w.HANDLE,c.POINTER(c.c_void_p)]
u.WinUsb_Free.argtypes=[c.c_void_p]
u.WinUsb_ControlTransfer.argtypes=[c.c_void_p,Setup,c.c_void_p,w.ULONG,c.POINTER(w.ULONG),c.c_void_p]
legacy='--legacy' in sys.argv
g=GUID();c.memmove(c.byref(g),uuid.UUID('f0b32be3-6678-4879-9230-e43845d805ee' if legacy else '664be590-54bd-4964-8a8c-6cd1314f6dc2').bytes_le,16)
h=s.SetupDiGetClassDevsW(c.byref(g),None,None,0x12)
reports=[]
try:
 for index in range(16):
    item=Interface();item.size=c.sizeof(item)
    if not s.SetupDiEnumDeviceInterfaces(h,None,c.byref(g),index,c.byref(item)):break
    needed=w.DWORD();s.SetupDiGetDeviceInterfaceDetailW(h,c.byref(item),None,0,c.byref(needed),None)
    buf=c.create_string_buffer(needed.value);c.cast(buf,c.POINTER(w.DWORD))[0]=8
    if not s.SetupDiGetDeviceInterfaceDetailW(h,c.byref(item),buf,len(buf),None,None):raise c.WinError(c.get_last_error())
    path=c.wstring_at(c.addressof(buf)+4)
    handle=k.CreateFileW(path,0xc0000000,3,None,3,0x40000000,None)
    if handle==c.c_void_p(-1).value:
        handle=k.CreateFileW(path+'\\0000',0xc0000000,3,None,3,0x40000000,None)
    if handle==c.c_void_p(-1).value:reports.append({'open_error':c.get_last_error()});continue
    usb=c.c_void_p()
    try:
        if not legacy and not u.WinUsb_Initialize(handle,c.byref(usb)):reports.append({'init_error':c.get_last_error()});continue
        def control(t,r,v,i,n):
            if legacy:
                packet=c.create_string_buffer(c.string_at(c.byref(Setup(t,r,v,i,n)),8)+bytes(n),8+n)
                overlap=Overlapped();overlap.event=k.CreateEventW(None,True,False,None);done=w.DWORD()
                try:
                    ok=k.DeviceIoControl(handle,0x2200a0,packet,len(packet),packet,len(packet),None,c.byref(overlap))
                    if not ok and c.get_last_error()!=997:raise c.WinError(c.get_last_error())
                    if k.WaitForSingleObject(overlap.event,3000)!=0:
                        k.CancelIo(handle);k.GetOverlappedResult(handle,c.byref(overlap),c.byref(done),True);raise TimeoutError('USB control timeout')
                    if not k.GetOverlappedResult(handle,c.byref(overlap),c.byref(done),False):raise c.WinError(c.get_last_error())
                    return packet.raw[8:done.value]
                finally:k.CloseHandle(overlap.event)
            out=c.create_string_buffer(n);done=w.ULONG()
            ok=u.WinUsb_ControlTransfer(usb,Setup(t,r,v,i,n),out,n,c.byref(done),None)
            if not ok:raise c.WinError(c.get_last_error())
            return out.raw[:done.value]
        device=control(0x80,6,0x100,0,18)
        report={'vid':int.from_bytes(device[8:10],'little'),'pid':int.from_bytes(device[10:12],'little'),'configs':[]}
        for cfg in range(device[17]):
            try:
                header=control(0x80,6,0x200|cfg,0,9)
                data=control(0x80,6,0x200|cfg,0,int.from_bytes(header[2:4],'little'))
                report['configs'].append({'index':cfg,'descriptor_hex':data.hex()})
            except OSError as e:report['configs'].append({'index':cfg,'error':str(e)})
        if '--enable-carplay' in sys.argv:
            report['carplay_transition_response']=control(0xc0,0x52,0,4,1).hex()
        reports.append(report)
    finally:
        if usb:u.WinUsb_Free(usb)
        k.CloseHandle(handle)
finally:s.SetupDiDestroyDeviceInfoList(h)
output=Path(__file__).resolve().parents[2]/'../build/carplay/windows-x64/usb-configuration.json'
output.write_text(json.dumps(reports,indent=2),encoding='utf-8')
print(json.dumps(reports,indent=2))
