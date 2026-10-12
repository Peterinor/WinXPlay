// Compile on desktop; every registry call is substituted, never touches the host.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
static void check(bool ok){if(!ok){fprintf(stderr,"Registry test assertion failed\n");exit(1);}}
static DWORD value,reads,writes,closes;
static int failure;
static LONG fakeOpen(HKEY root,LPCWSTR path,DWORD reserved,REGSAM access,PHKEY key){
    check(root==HKEY_LOCAL_MACHINE && reserved==0 && access==KEY_QUERY_VALUE);
    check(wcscmp(path,L"Drivers\\USB\\LoadClients\\1452\\Default\\Default")==0);
    if(failure==1)return ERROR_FILE_NOT_FOUND;
    if(failure==2)return ERROR_ACCESS_DENIED;
    *key=(HKEY)123;return ERROR_SUCCESS;
}
static LONG fakeQuery(HKEY key,LPCWSTR name,LPDWORD reserved,LPDWORD type,LPBYTE data,LPDWORD size){
    check(key==(HKEY)123 && !reserved && wcscmp(name,L"bConfigurationValue")==0 && *size==4);
    ++reads;
    if(failure==3 && reads==1)return ERROR_FILE_NOT_FOUND;
    if(failure==8 && reads==2)return ERROR_ACCESS_DENIED;
    *type=(failure==4 && reads==1)?REG_SZ:REG_DWORD;
    *size=(failure==5 && reads==1)?2:4;
    *(DWORD*)data=(failure==7 && reads==2)?2:value;
    return ERROR_SUCCESS;
}
static LONG fakeSet(HKEY key,LPCWSTR name,DWORD reserved,DWORD type,const BYTE* data,DWORD size){
    check(key==(HKEY)123 && !reserved && wcscmp(name,L"bConfigurationValue")==0 && type==REG_DWORD && size==4);
    ++writes;
    if(failure==6)return ERROR_ACCESS_DENIED;
    value=*(const DWORD*)data;return ERROR_SUCCESS;
}
static LONG fakeClose(HKEY key){check(key==(HKEY)123);++closes;return ERROR_SUCCESS;}
#define RegOpenKeyExW fakeOpen
#define RegQueryValueExW fakeQuery
#define RegSetValueExW fakeSet
#define RegCloseKey fakeClose
#include "../../devices/mrn/usb_configuration.h"
static int loaderFailure;static DWORD loaderError;static unsigned loaderCalls,loaderReleases;
static HMODULE fakeModule(LPCWSTR name){check(wcscmp(name,L"coredll.dll")==0);return loaderFailure==1?0:(HMODULE)11;}
static HMODULE WINAPI fakeLoadDriver(LPCWSTR path){
    check(wcscmp(path,L"\\NAND_USER\\WinXPlay\\WinXPlayUsbMux.dll")==0);++loaderCalls;
    if(loaderFailure==3){loaderError=193;return 0;}return (HMODULE)22;
}
static FARPROC fakeProc(HMODULE module,LPCWSTR name){
    if(module==(HMODULE)11){check(wcscmp(name,L"LoadDriver")==0);return loaderFailure==2?0:(FARPROC)fakeLoadDriver;}
    check(module==(HMODULE)22 && wcscmp(name,L"USBDeviceAttach")==0);
    if(loaderFailure==4){loaderError=ERROR_PROC_NOT_FOUND;return 0;}return (FARPROC)fakeLoadDriver;
}
static BOOL fakeFree(HMODULE module){check(module==(HMODULE)22);++loaderReleases;loaderError=5;return loaderFailure!=5;}
static DWORD fakeGetError(){return loaderError;}
static void fakeSetError(DWORD error){loaderError=error;}
#define GetModuleHandleW fakeModule
#define GetProcAddress fakeProc
#define FreeLibrary fakeFree
#define GetLastError fakeGetError
#define SetLastError fakeSetError
#include "../../devices/mrn/usb_loader_probe.h"
static cp::AppleUsbConfiguration run(int fail,DWORD initial=2){
    failure=fail;value=initial;reads=writes=closes=0;
    cp::AppleUsbConfiguration result;cp::prepareAppleUsbConfiguration(result);
    check(closes==((fail==1 || fail==2)?0:1));return result;
}
int main(){
    cp::AppleUsbConfiguration r;
    for(DWORD initial=1;initial<=7;++initial){
        r=run(0,initial);check(!r.error && r.previousKnown && r.previous==initial &&
            r.readback==initial && value==initial && !writes && !r.changed);
    }
    for(int fail=1;fail<=5;++fail){
        r=run(fail);check(writes==0 && value==2);
        if(fail==3)check(!r.error && !r.previousKnown);else check(r.error!=0);
    }
    puts("12 Apple configuration inspection scenarios passed; startup never forces a configuration.");
    for(loaderFailure=0;loaderFailure<=5;++loaderFailure){
        loaderError=1234;loaderCalls=loaderReleases=0;
        cp::UsbLoaderProbe p=cp::probeUsbDriverLoader(L"\\NAND_USER\\WinXPlay\\WinXPlayUsbMux.dll");
        check(loaderError==1234);
        check(loaderCalls==((loaderFailure==1 || loaderFailure==2)?0:1));
        check(loaderReleases==((loaderFailure>=1 && loaderFailure<=3)?0:1));
        if(loaderFailure==0)check(p.loaded && p.attachExport && !p.error && !p.exportError && !p.releaseError);
        if(loaderFailure==1 || loaderFailure==2)check(!p.available && p.error==ERROR_PROC_NOT_FOUND);
        if(loaderFailure==3)check(p.available && !p.loaded && p.error==193);
        if(loaderFailure==4)check(p.loaded && !p.attachExport && p.exportError==ERROR_PROC_NOT_FOUND);
        if(loaderFailure==5)check(p.loaded && p.attachExport && p.releaseError==5);
    }
    puts("6 isolated LoadDriver probe scenarios passed; no host DLL loading performed.");
    return 0;
}
