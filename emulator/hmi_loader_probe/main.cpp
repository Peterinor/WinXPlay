#include <windows.h>

static HANDLE logFile = INVALID_HANDLE_VALUE;
static void Text(const char* text) {
    if (logFile == INVALID_HANDLE_VALUE) return;
    DWORD n=0,written=0;while(text[n])++n;
    WriteFile(logFile,text,n,&written,0);
}
static void Number(const char* label,DWORD n) {
    char b[16];int i=15;b[i]=0;
    do { b[--i]=char('0'+n%10);n/=10; } while(n && i>0);
    Text(label);Text(b+i);Text("\r\n");
}
static void Try(const char* label,const WCHAR* path) {
    Text(label);Text("\r\n");
    DWORD attr=GetFileAttributesW(path);Number("attributes=",attr);
    PROCESS_INFORMATION pi={0};
    SetLastError(0);
    BOOL ok=CreateProcessW(path,0,0,0,FALSE,0,0,0,0,&pi);
    DWORD error=GetLastError();
    Number("CreateProcess_success=",ok);Number("GetLastError=",error);
    if(ok) {
        DWORD wait=WaitForSingleObject(pi.hProcess,5000);
        DWORD code=0;GetExitCodeProcess(pi.hProcess,&code);
        Number("wait_5s=",wait);Number("process_exit_code=",code);
        if(wait==WAIT_TIMEOUT) {
            // Only terminate the child that this diagnostic itself created.
            Number("terminate_test_child=",TerminateProcess(pi.hProcess,0));
        }
        CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    }
}
extern "C" void HmiProbeEntry() {
    logFile=CreateFileW(L"\\Storage Card\\HMI_LOAD_TEST.log",GENERIC_WRITE,FILE_SHARE_READ,
                      0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    if(logFile==INVALID_HANDLE_VALUE) ExitProcess(1);
    Text("HMI loader comparison - real guest CreateProcessW; no API stubs\r\n");
    OSVERSIONINFO os={0};os.dwOSVersionInfoSize=sizeof(os);GetVersionEx(&os);
    Number("OS_major=",os.dwMajorVersion);Number("OS_minor=",os.dwMinorVersion);Number("OS_build=",os.dwBuildNumber);
    MEMORYSTATUS memory;memory.dwLength=sizeof(memory);GlobalMemoryStatus(&memory);
    Number("physical_memory_total=",memory.dwTotalPhys);Number("physical_memory_available=",memory.dwAvailPhys);
    Try("ORIGINAL HMI",L"\\Storage Card\\HMI_TEST\\HMI_ORIGINAL.exe");
    Try("PATCHED HMI",L"\\Storage Card\\HMI_TEST\\HMI_PATCHED.exe");
    CloseHandle(logFile);logFile=INVALID_HANDLE_VALUE;
    MessageBoxW(0,L"Original and patched HMI load attempts finished.\nSee HMI_LOAD_TEST.log on Storage Card.",
                L"HMI loader comparison",MB_OK);
    ExitProcess(0);
}
