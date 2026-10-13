// Test-only NCM prerequisites for the WM5 emulator, which has no NCM driver.
// Exercise production startup without changing the emulator's USB registry.
// Registry failure/rollback paths are covered by apple_ncm_binding_test.cpp.
#include "../../platform/common/native_crypto.h"
#include "../../platform/common/local_identity.h"
#include <cstring>
namespace {
const wchar_t *ncmOriginal=L"Drivers\\USB\\LoadClients\\65535_65535\\0_0_0\\2_13_0\\USBCDCNCM";
const wchar_t *ncmTarget=L"Drivers\\USB\\LoadClients\\1452\\0_0_0\\2_13_0\\USBCDCNCM";
bool ncmRegistered=false;
HKEY configKey=(HKEY)0x12340001,originalKey=(HKEY)0x12340002,targetKey=(HKEY)0x12340003;
bool synthetic(HKEY key){return key==configKey || key==originalKey || key==targetKey;}
DWORD testAttributes(LPCWSTR path){
    if(!wcscmp(path,L"\\Windows\\usbcdcncm.dll"))return FILE_ATTRIBUTE_NORMAL;
    return GetFileAttributesW(path);
}
LONG testOpen(HKEY root,LPCWSTR path,DWORD options,REGSAM access,PHKEY key){
    if(root==HKEY_LOCAL_MACHINE){
        if(!wcscmp(path,L"Drivers\\USB\\LoadClients\\1452\\Default\\Default")){*key=configKey;return 0;}
        if(!wcscmp(path,ncmOriginal)){*key=originalKey;return 0;}
    }
    return RegOpenKeyExW(root,path,options,access,key);
}
LONG testCreate(HKEY root,LPCWSTR path,DWORD reserved,LPWSTR cls,DWORD options,
        REGSAM access,LPSECURITY_ATTRIBUTES security,PHKEY key,LPDWORD disposition){
    if(root==HKEY_LOCAL_MACHINE && !wcscmp(path,ncmTarget)){
        *key=targetKey;*disposition=ncmRegistered?REG_OPENED_EXISTING_KEY:REG_CREATED_NEW_KEY;return 0;
    }
    return RegCreateKeyExW(root,path,reserved,cls,options,access,security,key,disposition);
}
LONG testQuery(HKEY key,LPCWSTR name,LPDWORD reserved,LPDWORD type,LPBYTE data,LPDWORD size){
    if(!synthetic(key))return RegQueryValueExW(key,name,reserved,type,data,size);
    if(key==configKey || (key==targetKey && !ncmRegistered))return ERROR_FILE_NOT_FOUND;
    if(wcscmp(name,L"DLL"))return ERROR_FILE_NOT_FOUND;
    const WCHAR dll[]=L"usbcdcncm.dll";if(*size<sizeof(dll))return ERROR_MORE_DATA;
    *type=REG_SZ;*size=sizeof(dll);memcpy(data,dll,sizeof(dll));return 0;
}
LONG testSet(HKEY key,LPCWSTR name,DWORD reserved,DWORD type,const BYTE *data,DWORD size){
    if(key!=targetKey)return RegSetValueExW(key,name,reserved,type,data,size);
    if(wcscmp(name,L"DLL") || type!=REG_SZ || size!=sizeof(L"usbcdcncm.dll") ||
        memcmp(data,L"usbcdcncm.dll",size))return ERROR_INVALID_DATA;
    ncmRegistered=true;return 0;
}
LONG testClose(HKEY key){return synthetic(key)?ERROR_SUCCESS:RegCloseKey(key);}
}
#define GetFileAttributesW testAttributes
#define RegOpenKeyExW testOpen
#define RegCreateKeyExW testCreate
#define RegQueryValueExW testQuery
#define RegSetValueExW testSet
#define RegCloseKey testClose
#include "../../platform/wince/portable.cpp"
