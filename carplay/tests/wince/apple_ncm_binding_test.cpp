// Registry and filesystem substitutes: never changes the development computer.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
static const wchar_t *original=L"Drivers\\USB\\LoadClients\\65535_65535\\0_0_0\\2_13_0\\USBCDCNCM";
static const wchar_t *target=L"Drivers\\USB\\LoadClients\\1452\\0_0_0\\2_13_0\\USBCDCNCM";
static int failure;static unsigned writes,creates,deletes;static bool exists,hasValue;
static std::wstring value;
static void check(bool ok){if(!ok){fprintf(stderr,"NCM assertion scenario %d\n",failure);exit(1);}}
static DWORD attrs(LPCWSTR name){check(!wcscmp(name,L"\\Windows\\usbcdcncm.dll"));return failure==1?INVALID_FILE_ATTRIBUTES:0;}
static DWORD error(){return ERROR_FILE_NOT_FOUND;}
static LONG open(HKEY root,LPCWSTR name,DWORD,REGSAM,PHKEY key){
    check(root==HKEY_LOCAL_MACHINE && !wcscmp(name,original));*key=(HKEY)1;
    return failure==2?ERROR_FILE_NOT_FOUND:0;
}
static LONG create(HKEY root,LPCWSTR name,DWORD,LPWSTR,DWORD,REGSAM,LPSECURITY_ATTRIBUTES,PHKEY key,LPDWORD disposition){
    check(root==HKEY_LOCAL_MACHINE && !wcscmp(name,target));++creates;
    if(failure==3)return ERROR_ACCESS_DENIED;
    *key=(HKEY)2;*disposition=exists?REG_OPENED_EXISTING_KEY:REG_CREATED_NEW_KEY;exists=true;return 0;
}
static LONG query(HKEY key,LPCWSTR name,LPDWORD,LPDWORD type,LPBYTE data,LPDWORD size){
    check(!wcscmp(name,L"DLL"));std::wstring text;
    if(key==(HKEY)1)text=failure==4?L"another.dll":L"usbcdcncm.dll";
    else{check(key==(HKEY)2);if(!hasValue)return ERROR_FILE_NOT_FOUND;text=value;}
    if(key==(HKEY)2 && writes && (failure==6 || failure==7))text=L"bad.dll";
    *type=failure==8 && key==(HKEY)1?REG_DWORD:REG_SZ;
    DWORD n=DWORD((text.size()+1)*sizeof(wchar_t));check(*size>=n);*size=n;
    memcpy(data,text.c_str(),n);return 0;
}
static LONG set(HKEY key,LPCWSTR name,DWORD,DWORD type,const BYTE *data,DWORD size){
    check(key==(HKEY)2 && !wcscmp(name,L"DLL") && type==REG_SZ);
    check(size==sizeof(L"usbcdcncm.dll") && !wcscmp((LPCWSTR)data,L"usbcdcncm.dll"));++writes;
    if(failure==5)return ERROR_ACCESS_DENIED;
    hasValue=true;value=(LPCWSTR)data;return 0;
}
static LONG close(HKEY key){check(key==(HKEY)1 || key==(HKEY)2);return 0;}
static LONG delKey(HKEY root,LPCWSTR name){check(root==HKEY_LOCAL_MACHINE && !wcscmp(name,target));++deletes;exists=hasValue=false;return 0;}
static LONG delValue(HKEY key,LPCWSTR name){check(key==(HKEY)2 && !wcscmp(name,L"DLL"));++deletes;hasValue=false;return 0;}
#define GetFileAttributesW attrs
#define GetLastError error
#define RegOpenKeyExW open
#define RegCreateKeyExW create
#define RegQueryValueExW query
#define RegSetValueExW set
#define RegCloseKey close
#define RegDeleteKeyW delKey
#define RegDeleteValueW delValue
#include "../../devices/mrn/usb_configuration.h"
static cp::AppleNcmBinding run(int mode,bool key=false,const wchar_t *prior=0){
    failure=mode;writes=creates=deletes=0;exists=key;hasValue=prior!=0;value=prior?prior:L"";
    cp::AppleNcmBinding result;cp::prepareAppleNcmBinding(result);return result;
}
int main(){
    cp::AppleNcmBinding r=run(0);check(!r.error && r.changed && exists && writes==1 && creates==1);
    r=run(0,true,L"usbcdcncm.dll");check(!r.error && !r.changed && !writes);
    r=run(0,true,L"other.dll");check(r.error==ERROR_ALREADY_EXISTS && !writes && value==L"other.dll");
    for(int i=1;i<=8;++i){
        r=run(i,i==7);check(r.error!=0 && !r.changed);
        if(i==1 || i==2 || i==4 || i==8)check(!creates && !writes);
        if(i==5 || i==6)check(deletes==1 && !exists);
        if(i==7)check(deletes==1 && exists && !hasValue);
    }
    puts("11 NCM registration scenarios passed: Apple interface scope, idempotency, conflicts, errors and rollback.");
}
