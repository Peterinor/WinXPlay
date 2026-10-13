// Standard CE registry registration, used only after a known driver fallback.
static BOOL registryInstallMatch(LPCWSTR path,LPCWSTR match){
    HKEY key=0;DWORD disposition=0;LONG result;
    registration.fallback=1;registration.stage=USB_REG_CLIENT_KEY;
    result=RegCreateKeyExW(HKEY_LOCAL_MACHINE,clientKey,0,0,0,0,0,&key,&disposition);
    if(result!=ERROR_SUCCESS){registration.error=result;return FALSE;}RegCloseKey(key);
    registration.stage=USB_REG_MATCH_KEY;
    result=RegCreateKeyExW(HKEY_LOCAL_MACHINE,match,0,0,0,0,0,&key,&disposition);
    if(result!=ERROR_SUCCESS){registration.error=result;return FALSE;}
    registration.stage=USB_REG_DLL_VALUE;
    DWORD size=(lstrlenW(path)+1)*sizeof(WCHAR);
    result=RegSetValueExW(key,L"DLL",0,REG_SZ,(const BYTE*)path,size);
    if(result==ERROR_SUCCESS){
        registration.stage=USB_REG_VERIFY;
        WCHAR readback[MAX_PATH]={0};DWORD actual=sizeof(readback),type=0;
        result=RegQueryValueExW(key,L"DLL",0,&type,(LPBYTE)readback,&actual);
        if(result==ERROR_SUCCESS&&(type!=REG_SZ||actual!=size||memcmp(readback,path,size)!=0))
            result=ERROR_INVALID_DATA;
    }
    RegCloseKey(key);registration.error=result;
    return result==ERROR_SUCCESS;
}
static BOOL registryInstall(LPCWSTR path){
    return registryInstallMatch(path,matchKey) && registryInstallMatch(path,bootstrapKey);
}
static BOOL registryUninstall(DWORD &error){
    LONG a=RegDeleteKeyW(HKEY_LOCAL_MACHINE,matchKey);
    LONG c=RegDeleteKeyW(HKEY_LOCAL_MACHINE,bootstrapKey);
    LONG b=RegDeleteKeyW(HKEY_LOCAL_MACHINE,clientKey);
    BOOL ok=(a==0||a==ERROR_FILE_NOT_FOUND)&&(b==0||b==ERROR_FILE_NOT_FOUND)&&(c==0||c==ERROR_FILE_NOT_FOUND);
    error=ok?0:(a?a:(c?c:b));return ok;
}
