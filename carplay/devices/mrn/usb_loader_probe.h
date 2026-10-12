// Probe our DLL with the loader API used by F3.21 USBD. Does not attach a device.
#ifndef WINXPLAY_USB_LOADER_PROBE_H
#define WINXPLAY_USB_LOADER_PROBE_H
namespace cp {
struct UsbLoaderProbe {
    DWORD error,exportError,releaseError;
    bool available,loaded,attachExport;
    UsbLoaderProbe():error(0),exportError(0),releaseError(0),available(false),loaded(false),attachExport(false){}
};
static UsbLoaderProbe probeUsbDriverLoader(const wchar_t* path){
    UsbLoaderProbe result;DWORD saved=GetLastError();
    HMODULE core=GetModuleHandleW(L"coredll.dll");
    typedef HMODULE (WINAPI *DriverLoader)(LPCWSTR);
    DriverLoader load=core?(DriverLoader)GetProcAddress(core,L"LoadDriver"):0;
    result.available=load!=0;
    if(!load)result.error=ERROR_PROC_NOT_FOUND;
    else{
        SetLastError(0);HMODULE driver=load(path);
        result.loaded=driver!=0;result.error=driver?0:GetLastError();
        if(driver){
            SetLastError(0);
            result.attachExport=GetProcAddress(driver,L"USBDeviceAttach")!=0;
            result.exportError=result.attachExport?0:GetLastError();
            SetLastError(0);
            if(!FreeLibrary(driver))result.releaseError=GetLastError();
        }
    }
    SetLastError(saved);return result;
}
}
#endif
