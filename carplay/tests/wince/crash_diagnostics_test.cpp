// Execute the production exception filter with a real SEH exception on Windows.
#include <windows.h>
#include <cstdio>
#include <cstring>
namespace cp {
#include "../../platform/wince/crash_diagnostics.inc"
}
static void fault(){
    __try{RaiseException(0xe1234567,0,0,0);}
    __except(cp::diagnosticException(GetExceptionInformation(),L"test_delivery")){}
}
static bool propagates(){
    __try{fault();return false;}
    __except(GetExceptionCode()==0xe1234567?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
}
int main(){
    WCHAR temp[MAX_PATH],folder[MAX_PATH],log[MAX_PATH];
    if(!GetTempPathW(MAX_PATH,temp) || !GetTempFileNameW(temp,L"wxp",0,folder))return 1;
    DeleteFileW(folder);if(!CreateDirectoryW(folder,0))return 2;
    cp::initializeCrashDiagnostics(folder);
    if(!propagates())return 3;
    SetLastError(1234);if(cp::diagnosticException(0,L"null")!=EXCEPTION_CONTINUE_SEARCH || GetLastError()!=1234)return 4;
    CloseHandle(cp::crashFile);cp::crashFile=INVALID_HANDLE_VALUE;
    lstrcpyW(log,folder);lstrcatW(log,L"\\crash.log");
    HANDLE f=CreateFileW(log,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
    char data[2048]={0};DWORD size=0;
    if(f==INVALID_HANDLE_VALUE || !ReadFile(f,data,sizeof(data)-1,&size,0))return 5;
    CloseHandle(f);DeleteFileW(log);RemoveDirectoryW(folder);
    if(!strstr(data,"boundary=test_delivery") || !strstr(data,"code=e1234567") ||
       !strstr(data,"free_phys=") || !strstr(data,"free_virtual="))return 6;
    puts("Crash filter: real exception recorded, propagated to outer handler, null input safe, last-error preserved.");
    return 0;
}
