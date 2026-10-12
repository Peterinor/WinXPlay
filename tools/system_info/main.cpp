#include "../../common/diagnostic_support.h"
#include <tlhelp32.h>

static void Processes() {
    HMODULE m=LoadLibraryW(L"toolhelp.dll");
    if(!m){DiagValues(L"OPTIONAL_TOOLHELP_UNAVAILABLE",GetLastError());return;}
    typedef HANDLE(WINAPI *Snap)(DWORD,DWORD);
    typedef BOOL(WINAPI *Step)(HANDLE,LPPROCESSENTRY32);
    typedef BOOL(WINAPI *Close)(HANDLE);
    Snap snap=(Snap)GetProcAddress(m,L"CreateToolhelp32Snapshot");
    Step first=(Step)GetProcAddress(m,L"Process32First"),next=(Step)GetProcAddress(m,L"Process32Next");
    Close close=(Close)GetProcAddress(m,L"CloseToolhelp32Snapshot");
    if(snap&&first&&next&&close){
        HANDLE h=snap(TH32CS_SNAPPROCESS,0);
        if(h && h!=INVALID_HANDLE_VALUE){
            PROCESSENTRY32 p={0};p.dwSize=sizeof(p);BOOL more=first(h,&p);DWORD count=0;
            while(more && count++<256 && DiagLogOK()){
                p.szExeFile[MAX_PATH-1]=0;DiagLog(p.szExeFile);
                DiagValues(L"PROCESS_PID_PARENT_THREADS",p.th32ProcessID,p.th32ParentProcessID,p.cntThreads);
                p.dwSize=sizeof(p);more=next(h,&p);
            }
            DiagValues(L"PROCESS_ENUM_STOP_MORE",more);close(h);
        }else DiagValues(L"OPTIONAL_SNAPSHOT_FAILED",GetLastError());
    }else DiagLog(L"OPTIONAL_TOOLHELP_EXPORTS_MISSING");
    FreeLibrary(m);
}
static BOOL ReadCapacity(const WCHAR* path) {
    DiagLog(path);ULARGE_INTEGER available,total,freeBytes;
    if(GetDiskFreeSpaceExW(path,&available,&total,&freeBytes)){
        DiagValues(L"TOTAL_LOW_HIGH",total.LowPart,total.HighPart);
        DiagValues(L"FREE_LOW_HIGH",freeBytes.LowPart,freeBytes.HighPart);
        return TRUE;
    }else DiagValues(L"VOLUME_QUERY_ERROR",GetLastError());
    return FALSE;
}
static BOOL ReadVolumes() {
    BOOL ok=ReadCapacity(L"\\");
    WIN32_FIND_DATA data;HANDLE search=FindFirstFileW(L"\\*",&data);
    if(search==INVALID_HANDLE_VALUE){DiagValues(L"VOLUME_ENUM_ERROR",GetLastError());return FALSE;}
    DWORD count=0;BOOL more=TRUE;
    while(more&&count++<256&&DiagLogOK()){
        data.cFileName[MAX_PATH-1]=0;
        // CE mount points carry FILE_ATTRIBUTE_TEMPORARY. Ordinary directories
        // must not be reported as additional disks sharing the same capacity.
        if((data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_TEMPORARY))==
                (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_TEMPORARY)){
            WCHAR path[MAX_PATH];if(lstrlenW(data.cFileName)<MAX_PATH-2){
                wsprintfW(path,L"\\%s",data.cFileName);if(!ReadCapacity(path))ok=FALSE;
            }
        }
        more=FindNextFileW(search,&data);
    }
    DWORD error=GetLastError();FindClose(search);
    DiagLog(L"STORAGE_READ_ONLY_MOUNT_POINT_ENUMERATION");
    return ok&&!more&&error==ERROR_NO_MORE_FILES&&DiagLogOK();
}

static BOOL ReadSystemInformation(const DiagnosticOptions*) {
    OSVERSIONINFO os={0};os.dwOSVersionInfoSize=sizeof(os);BOOL ok=GetVersionExW(&os);
    if(ok)DiagValues(L"OS_MAJOR_MINOR_BUILD",os.dwMajorVersion,os.dwMinorVersion,os.dwBuildNumber);
    else DiagValues(L"OS_QUERY_ERROR",GetLastError());
    SYSTEM_INFO info={0};GetSystemInfo(&info);
    DiagValues(L"CPU_ARCH_TYPE_LEVEL",info.wProcessorArchitecture,info.dwProcessorType,info.wProcessorLevel);
    MEMORYSTATUS memory={0};memory.dwLength=sizeof(memory);GlobalMemoryStatus(&memory);
    DiagValues(L"MEMORY_TOTAL_AVAILABLE_LOAD",memory.dwTotalPhys,memory.dwAvailPhys,memory.dwMemoryLoad);
    DiagValues(L"SCREEN_WIDTH_HEIGHT",GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN));
    Processes();
    DiagLog(L"SYSTEM_INFORMATION_COLLECTED");
    BOOL storageOK=DiagLogOK()?ReadVolumes():FALSE;
    return ok&&storageOK&&DiagLogOK();
}
extern "C" void SystemInformationEntry() {
    RunDiagnostic(L"system_info",ReadSystemInformation,TRUE,0);
}
