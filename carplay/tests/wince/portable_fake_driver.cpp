// Emulator-only stub: never touches USB registration or hardware.
#include <windows.h>
#include <string>
extern "C" __declspec(dllexport) BOOL USBInstallDriver(LPCWSTR path){
    std::wstring file(path),folder=file.substr(0,file.find_last_of(L'\\'));
    if(GetFileAttributesW((folder+L"\\driver-fail").c_str())!=INVALID_FILE_ATTRIBUTES)return FALSE;
    HANDLE h=CreateFileW((folder+L"\\driver-called").c_str(),GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
    if(h==INVALID_HANDLE_VALUE)return FALSE;
    DWORD n=0;BOOL ok=WriteFile(h,path,(wcslen(path)+1)*sizeof(wchar_t),&n,0);CloseHandle(h);return ok;
}
BOOL WINAPI DllMain(HANDLE,DWORD,LPVOID){return TRUE;}
