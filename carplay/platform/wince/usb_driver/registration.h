// Included by driver.cpp after contract.h and usbdi.h.
static WinXPlayUsbRegistration registration;
static const WCHAR clientId[]=L"WinXPlayUsbMux";
static const WCHAR clientKey[]=L"Drivers\\USB\\ClientDrivers\\WinXPlayUsbMux";
// Apple VID only; wildcard product/release and device class; exact ff/fe/02 interface.
static const WCHAR matchKey[]=L"Drivers\\USB\\LoadClients\\1452\\Default\\255_254_2\\WinXPlayUsbMux";
static const WCHAR bootstrapKey[]=L"Drivers\\USB\\LoadClients\\1452\\Default\\Default\\WinXPlayUsbMux";
#include "../../../devices/usb_registration.h"
static USB_DRIVER_SETTINGS bootstrapSettings(){
    USB_DRIVER_SETTINGS s=settings();
    s.dwInterfaceClass=s.dwInterfaceSubClass=s.dwInterfaceProtocol=USB_NO_INFO;
    return s;
}
extern "C" BOOL USBGetRegistrationStatus(WinXPlayUsbRegistration* out,DWORD size){
    if(!out||size!=sizeof(*out))return FALSE;*out=registration;return TRUE;
}
extern "C" BOOL USBInstallDriver(LPCWSTR path){
    memset(&registration,0,sizeof(registration));registration.size=sizeof(registration);
    registration.stage=USB_REG_LOAD;
    if(!path||path[0]!=L'\\'||lstrlenW(path)>=MAX_PATH){registration.error=ERROR_INVALID_PARAMETER;SetLastError(registration.error);return FALSE;}
    HMODULE usbd=LoadLibraryW(L"USBD.dll");
    if(!usbd){registration.error=GetLastError();return FALSE;}
    registration.stage=USB_REG_EXPORTS;
    LPREGISTER_CLIENT_DRIVER_ID id=(LPREGISTER_CLIENT_DRIVER_ID)GetProcAddress(usbd,L"RegisterClientDriverID");
    LPREGISTER_CLIENT_SETTINGS reg=(LPREGISTER_CLIENT_SETTINGS)GetProcAddress(usbd,L"RegisterClientSettings");
    BOOL ok=FALSE;
    if(!id||!reg)registration.error=ERROR_PROC_NOT_FOUND;
    else {
        USB_DRIVER_SETTINGS s=settings();
        registration.stage=USB_REG_SETTINGS;
        SetLastError(0);registration.idResult=id(clientId);registration.idError=GetLastError();
        SetLastError(0);registration.settingsResult=reg(path,clientId,0,&s);registration.settingsError=GetLastError();
        ok=registration.settingsResult!=0;
        if(ok){
            USB_DRIVER_SETTINGS bootstrap=bootstrapSettings();
            SetLastError(0);registration.settingsResult=reg(path,clientId,0,&bootstrap);
            registration.settingsError=GetLastError();ok=registration.settingsResult!=0;
        }
        registration.error=ok?0:registration.settingsError;
        // A build-selected device policy may handle known firmware stubs.
        // Generic builds never bypass failed system registration.
        if(!ok&&deviceUsbRegistrationFallback((FARPROC)id,(FARPROC)reg))ok=registryInstall(path);
    }
    if(ok){registration.stage=USB_REG_DONE;registration.error=0;}
    FreeLibrary(usbd);SetLastError(registration.error);return ok;
}
extern "C" BOOL USBUnInstallDriver(){
    HMODULE usbd=LoadLibraryW(L"USBD.dll");if(!usbd)return FALSE;
    LPUN_REGISTER_CLIENT_DRIVER_ID id=(LPUN_REGISTER_CLIENT_DRIVER_ID)GetProcAddress(usbd,L"UnRegisterClientDriverID");
    LPUN_REGISTER_CLIENT_SETTINGS reg=(LPUN_REGISTER_CLIENT_SETTINGS)GetProcAddress(usbd,L"UnRegisterClientSettings");
    BOOL ok=FALSE;DWORD error=ERROR_PROC_NOT_FOUND;
    if(id&&reg){
        if(deviceUsbRegistrationFallback((FARPROC)id,(FARPROC)reg)){
            ok=registryUninstall(error);
        }else{USB_DRIVER_SETTINGS s=settings(),b=bootstrapSettings();
            BOOL a=reg(clientId,0,&s),c=reg(clientId,0,&b);ok=a&&c&&id(clientId);error=ok?0:GetLastError();}
    }
    FreeLibrary(usbd);SetLastError(error);return ok;
}
