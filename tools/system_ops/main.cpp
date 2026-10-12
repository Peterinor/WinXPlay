#include <windows.h>
#include <pm.h>

// Only WinCE APIs; USB startup files are generated separately by the packager.
static HANDLE g_log=INVALID_HANDLE_VALUE;
static BOOL g_logOK=TRUE;
static WCHAR g_logPath[MAX_PATH];
static DWORD g_start;
static char g_config[4097];

static BOOL WriteAll(HANDLE file,const void* data,DWORD size){
    DWORD written=0;
    return WriteFile(file,data,size,&written,0)&&written==size;
}
static void Note(const WCHAR* message,DWORD value){
    WCHAR line[MAX_PATH+100];wsprintfW(line,L"[%lu] %s value=%lu\r\n",GetTickCount(),message,value);
    if(!WriteAll(g_log,line,lstrlenW(line)*sizeof(WCHAR))||!FlushFileBuffers(g_log))g_logOK=FALSE;
}
static DWORD Finish(DWORD code){
    if(g_log!=INVALID_HANDLE_VALUE){
        if(!CloseHandle(g_log)&&!code)code=22;
        g_log=INVALID_HANDLE_VALUE;
    }
    return code;
}
static BOOL Equal(const char* p,DWORD length,const char* value){
    DWORD i=0;while(value[i]){if(i>=length||p[i]!=value[i])return FALSE;++i;}
    return i==length;
}
extern "C" __declspec(dllexport) BOOL ParseSystemConfig(const char* data,DWORD length){
    if(!data||!length||length>4096)return FALSE;
    DWORD p=0,lines=0;
    if(length>=3&&(BYTE)data[0]==0xef&&(BYTE)data[1]==0xbb&&(BYTE)data[2]==0xbf)p=3;
    while(p<length){
        DWORD first=p;while(p<length&&data[p]!='\r'&&data[p]!='\n'){
            if(!data[p])return FALSE;++p;
        }
        DWORD last=p;while(first<last&&(data[first]==' '||data[first]=='\t'))++first;
        while(last>first&&(data[last-1]==' '||data[last-1]=='\t'))--last;
        while(p<length&&(data[p]=='\r'||data[p]=='\n'))++p;
        if(first==last||data[first]=='#')continue;
        if(lines==0){if(!Equal(data+first,last-first,"SYSTEMOPS_V1"))return FALSE;}
        else if(lines==1){if(!Equal(data+first,last-first,"REBOOT"))return FALSE;}
        else return FALSE;
        ++lines;
    }
    return lines==2;
}
typedef DWORD (WINAPI *SetPowerState)(LPCWSTR,DWORD,DWORD);

// Discover the platform's name using the standard CE power-state registry schema.
// Some OEM power managers omit RESET from their numeric-hint mapping.
static BOOL FindResetState(WCHAR* name){
    name[0]=0;HKEY root=0;
    LONG error=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"System\\CurrentControlSet\\Control\\Power\\State",0,KEY_READ,&root);
    if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND){Note(L"POWER_STATE_REGISTRY_ABSENT_USING_HINT",error);return TRUE;}
    if(error!=ERROR_SUCCESS){Note(L"POWER_STATE_REGISTRY_OPEN_ERROR",error);return FALSE;}
    BOOL ok=TRUE;DWORD matches=0;
    for(DWORD index=0;;++index){
        WCHAR child[MAX_PATH];DWORD length=MAX_PATH;
        error=RegEnumKeyExW(root,index,child,&length,0,0,0,0);
        if(error==ERROR_NO_MORE_ITEMS)break;
        if(error!=ERROR_SUCCESS){Note(L"POWER_STATE_ENUM_ERROR",error);ok=FALSE;break;}
        HKEY state=0;error=RegOpenKeyExW(root,child,0,KEY_READ,&state);
        if(error!=ERROR_SUCCESS){Note(L"POWER_STATE_OPEN_ERROR",error);ok=FALSE;break;}
        DWORD type=0,flags=0,size=sizeof(flags);
        error=RegQueryValueExW(state,L"Flags",0,&type,(BYTE*)&flags,&size);
        LONG closed=RegCloseKey(state);
        if(closed!=ERROR_SUCCESS){Note(L"POWER_STATE_CLOSE_ERROR",closed);ok=FALSE;break;}
        if(error==ERROR_FILE_NOT_FOUND)continue;
        if(error!=ERROR_SUCCESS||type!=REG_DWORD||size!=sizeof(flags)){
            Note(L"POWER_STATE_FLAGS_INVALID",error);ok=FALSE;break;
        }
        if(flags==POWER_STATE_RESET){
            ++matches;lstrcpyW(name,child);
            Note(L"RESET_STATE_NAME_FOLLOWS",flags);Note(name,flags);
        }
    }
    if(RegCloseKey(root)!=ERROR_SUCCESS)ok=FALSE;
    if(matches>1){Note(L"RESET_STATE_AMBIGUOUS_NO_REBOOT",matches);ok=FALSE;}
    if(!matches)Note(L"RESET_STATE_NOT_REGISTERED_USING_HINT",0);
    return ok;
}

extern "C" __declspec(dllexport) DWORD RunSystemOps(){
    g_start=GetTickCount();g_logOK=TRUE;
    WCHAR base[MAX_PATH],configPath[MAX_PATH],markerPath[MAX_PATH];
    DWORD length=GetModuleFileNameW(0,base,MAX_PATH),last=0;
    if(!length||length>=MAX_PATH)return 11;
    for(DWORD i=0;i<length;++i)if(base[i]==L'\\')last=i+1;
    if(!last||last>200)return 11;
    base[last]=0;
    wsprintfW(configPath,L"%ssystemops.txt",base);
    wsprintfW(markerPath,L"%ssystem_ops.reboot.once",base);
    for(DWORD n=1;n<=9999;++n){
        wsprintfW(g_logPath,L"%ssystem_ops_%04lu.txt",base,n);
        g_log=CreateFileW(g_logPath,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);
        if(g_log!=INVALID_HANDLE_VALUE)break;
        DWORD error=GetLastError();if(error!=ERROR_FILE_EXISTS&&error!=ERROR_ALREADY_EXISTS)return 12;
    }
    if(g_log==INVALID_HANDLE_VALUE)return 12;
    WORD bom=0xfeff;if(!WriteAll(g_log,&bom,sizeof(bom)))g_logOK=FALSE;
    Note(L"START SYSTEM_OPS_V1_1_UPTIME",g_start);
    HANDLE config=CreateFileW(configPath,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    if(config==INVALID_HANDLE_VALUE){Note(L"CONFIG_OPEN_ERROR",GetLastError());return Finish(18);}
    DWORD high=0,size=GetFileSize(config,&high),read=0;
    BOOL valid=!high&&size<=4096&&ReadFile(config,g_config,size,&read,0)&&read==size;
    if(!CloseHandle(config))valid=FALSE;
    if(!valid||!ParseSystemConfig(g_config,size)){Note(L"CONFIG_INVALID_NO_REBOOT",0);return Finish(18);}
    HMODULE core=GetModuleHandleW(L"coredll.dll");
    SetPowerState reset=core?(SetPowerState)GetProcAddress(core,L"SetSystemPowerState"):0;
    if(!reset){Note(L"STANDARD_RESET_API_UNAVAILABLE",GetLastError());return Finish(20);}
    if(!g_logOK)return Finish(22);
    // CREATE_NEW makes even an incomplete marker block a repeat request.
    HANDLE marker=CreateFileW(markerPath,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);
    if(marker==INVALID_HANDLE_VALUE){
        DWORD error=GetLastError();
        BOOL exists=error==ERROR_FILE_EXISTS||error==ERROR_ALREADY_EXISTS;
        Note(exists?L"REBOOT_SKIPPED_ONCE_MARKER_EXISTS":L"ONCE_MARKER_CREATE_ERROR",error);
        return Finish(g_logOK?(exists?0:21):22);
    }
    // Keep the marker even if state discovery fails, so startup cannot retry forever.
    WCHAR record[160];wsprintfW(record,L"SYSTEMOPS_V1 REBOOT attempted; uptime=%lu\r\n",g_start);
    BOOL saved=WriteAll(marker,record,lstrlenW(record)*sizeof(WCHAR));
    if(!FlushFileBuffers(marker))saved=FALSE;
    if(!CloseHandle(marker))saved=FALSE;
    if(!saved){Note(L"ONCE_MARKER_SAVE_ERROR_NO_REBOOT",GetLastError());return Finish(21);}
    Note(L"ONCE_MARKER_SAVED",0);
    WCHAR resetState[MAX_PATH];
    if(!FindResetState(resetState))return Finish(25);
    Note(resetState[0]?L"RESET_REQUEST_USES_REGISTERED_NAME":L"RESET_REQUEST_USES_NUMERIC_HINT",0);
    Note(L"REQUEST_SetSystemPowerState_POWER_STATE_RESET",POWER_STATE_RESET);
    if(!g_logOK)return Finish(22);
    // Close our files before the power manager starts shutting devices down.
    if(!CloseHandle(g_log)){g_log=INVALID_HANDLE_VALUE;return 22;}
    g_log=INVALID_HANDLE_VALUE;
    DWORD result=reset(resetState[0]?resetState:NULL,resetState[0]?0:POWER_STATE_RESET,0);
    // A return value is not evidence that the device actually restarted.
    g_log=CreateFileW(g_logPath,GENERIC_WRITE,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    if(g_log!=INVALID_HANDLE_VALUE){
        if(SetFilePointer(g_log,0,0,FILE_END)==INVALID_SET_FILE_POINTER)g_logOK=FALSE;
        else Note(result==ERROR_SUCCESS?L"RESET_API_RETURNED_SUCCESS_REBOOT_NOT_CONFIRMED":L"RESET_API_RETURNED_ERROR",result);
    }
    return Finish(result==ERROR_SUCCESS?24:23);
}

extern "C" void SystemOpsEntry(){
    HANDLE single=CreateMutexW(0,FALSE,L"SYSTEM_OPS_REBOOT_SINGLETON_V1");
    if(!single||GetLastError()==ERROR_ALREADY_EXISTS){if(single)CloseHandle(single);ExitProcess(10);}
    DWORD code=RunSystemOps();CloseHandle(single);ExitProcess(code);
}
