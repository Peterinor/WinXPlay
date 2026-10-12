// Inspect the existing preference; the USB bootstrap selects from live descriptors.
// Included after Windows declarations; kept here, not in the generic USB driver.
#ifndef WINXPLAY_MRN_USB_CONFIGURATION_H
#define WINXPLAY_MRN_USB_CONFIGURATION_H
namespace cp {
// Reuse the MRN's installed NCM miniport, only for Apple CDC/NCM interfaces.
// Keep the OEM rule and its Comm/IP configuration unchanged.
struct AppleNcmBinding {
    DWORD error,rollbackError;bool changed;const char *stage;
    AppleNcmBinding():error(0),rollbackError(0),changed(false),stage("driver_file"){}
};
static void prepareAppleNcmBinding(AppleNcmBinding &r){
    const WCHAR *original=L"Drivers\\USB\\LoadClients\\65535_65535\\0_0_0\\2_13_0\\USBCDCNCM";
    // F3.21 USBDeviceAttach checks the matched USB_DRIVER_SETTINGS, not only
    // the live descriptors: DeviceClass must be 0 (with InterfaceClass 2) or 2.
    // Default produces USB_NO_INFO and the OEM driver rejects it. Preserve
    // the OEM 0_0_0 middle group; narrow only the vendor to Apple.
    const WCHAR *target=L"Drivers\\USB\\LoadClients\\1452\\0_0_0\\2_13_0\\USBCDCNCM";
    const WCHAR *dll=L"usbcdcncm.dll";
    if(GetFileAttributesW(L"\\Windows\\usbcdcncm.dll")==INVALID_FILE_ATTRIBUTES){r.error=GetLastError();return;}
    HKEY key=0;WCHAR value[MAX_PATH]={0};DWORD size=sizeof(value),type=0,disposition=0;
    r.stage="verify_oem_rule";
    r.error=RegOpenKeyExW(HKEY_LOCAL_MACHINE,original,0,KEY_QUERY_VALUE,&key);
    if(r.error)return;
    r.error=RegQueryValueExW(key,L"DLL",0,&type,(BYTE*)value,&size);RegCloseKey(key);
    if(!r.error && (type!=REG_SZ || size<sizeof(WCHAR) || size>sizeof(value) ||
        size%sizeof(WCHAR) || value[size/sizeof(WCHAR)-1] || lstrcmpiW(value,dll)))r.error=ERROR_INVALID_DATA;
    if(r.error)return;
    r.stage="apple_ncm_rule";
    r.error=RegCreateKeyExW(HKEY_LOCAL_MACHINE,target,0,0,0,KEY_QUERY_VALUE|KEY_SET_VALUE,0,&key,&disposition);
    if(r.error)return;
    size=sizeof(value);type=0;
    r.error=RegQueryValueExW(key,L"DLL",0,&type,(BYTE*)value,&size);
    if(!r.error){
        if(type!=REG_SZ || size<sizeof(WCHAR) || size>sizeof(value) || size%sizeof(WCHAR) ||
           value[size/sizeof(WCHAR)-1] || lstrcmpiW(value,dll))r.error=ERROR_ALREADY_EXISTS;
        RegCloseKey(key);r.stage=r.error?"conflicting_rule":"already_registered";return;
    }
    if(r.error!=ERROR_FILE_NOT_FOUND){RegCloseKey(key);return;}
    const DWORD expected=(lstrlenW(dll)+1)*sizeof(WCHAR);
    r.stage="write_apple_ncm_rule";
    r.error=RegSetValueExW(key,L"DLL",0,REG_SZ,(const BYTE*)dll,expected);
    bool written=r.error==0;
    if(written){
        r.stage="verify_apple_ncm_rule";size=sizeof(value);type=0;
        r.error=RegQueryValueExW(key,L"DLL",0,&type,(BYTE*)value,&size);
        if(!r.error && (type!=REG_SZ || size!=expected || memcmp(value,dll,expected)))r.error=ERROR_INVALID_DATA;
    }
    if(r.error && written && disposition!=REG_CREATED_NEW_KEY)
        r.rollbackError=RegDeleteValueW(key,L"DLL");
    RegCloseKey(key);
    if(r.error && disposition==REG_CREATED_NEW_KEY)r.rollbackError=RegDeleteKeyW(HKEY_LOCAL_MACHINE,target);
    if(!r.error){r.changed=true;r.stage="registered_reenumeration_required";}
}
struct AppleUsbConfiguration {
    DWORD requested, previous, readback, error, rollbackError;
    bool previousKnown, changed;
    const char* stage;
    AppleUsbConfiguration():requested(0),previous(0),readback(0),error(0),rollbackError(0),
        previousKnown(false),changed(false),stage("open") {}
};
static void prepareAppleUsbConfiguration(AppleUsbConfiguration& result) {
    // Open only the existing Apple rule. Never create a generic USB/class rule.
    HKEY key=0;
    result.error=RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"Drivers\\USB\\LoadClients\\1452\\Default\\Default",0,
        KEY_QUERY_VALUE,&key);
    if(result.error!=ERROR_SUCCESS)return;
    DWORD type=0,size=sizeof(DWORD);
    result.stage="read_previous";
    result.error=RegQueryValueExW(key,L"bConfigurationValue",0,&type,
        (BYTE*)&result.previous,&size);
    if(result.error==ERROR_FILE_NOT_FOUND){result.error=0;RegCloseKey(key);result.stage="automatic";return;}
    if(result.error==ERROR_SUCCESS && (type!=REG_DWORD || size!=sizeof(DWORD)))
        result.error=ERROR_INVALID_DATA;
    if(result.error==ERROR_SUCCESS){
        result.previousKnown=true;
        result.requested=result.readback=result.previous;
    }
    RegCloseKey(key);
    if(result.error==ERROR_SUCCESS)result.stage="preserved_for_bootstrap";
    // No startup overwrite: a previous descriptor-selected value can be reused.
}
}
#endif
