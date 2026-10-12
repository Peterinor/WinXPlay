// Included by platform.cpp, inside namespace cp. Bounded event logging only.
#ifdef UNDER_CE
#include "crash_diagnostics.inc"
#endif
void diagnosticEvent(const char *event,const std::string &detail) {
    DWORD saved=GetLastError();HANDLE mutex=0,f=INVALID_HANDLE_VALUE;bool locked=false;
    try {
        mutex=CreateMutexW(0,FALSE,L"WinXPlayDiagnosticTrace");
        DWORD wait=mutex?WaitForSingleObject(mutex,1000):WAIT_FAILED;
        locked=wait==WAIT_OBJECT_0 || wait==WAIT_ABANDONED;
        if(locked){
            std::wstring path=executableDirectory()+L"\\connection-trace.log";
            f=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,0,OPEN_ALWAYS,0,0);
            if(f!=INVALID_HANDLE_VALUE && GetFileSize(f,0)>=512*1024){
                CloseHandle(f);f=INVALID_HANDLE_VALUE;
                std::wstring previous=executableDirectory()+L"\\connection-trace.previous.log";
                DeleteFileW(previous.c_str());
                // If rotation fails, preserve the existing log instead of truncating it.
                if(MoveFileW(path.c_str(),previous.c_str()))
                    f=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_NEW,0,0);
            }
            if(f!=INVALID_HANDLE_VALUE){
                std::string safe=detail.substr(0,1024);
                for(size_t i=0;i<safe.size();++i)if((unsigned char)safe[i]<32)safe[i]=' ';
                std::ostringstream s;s<<GetTickCount()<<" pid="<<GetCurrentProcessId()<<" tid="<<GetCurrentThreadId()
                    <<" "<<event<<" "<<safe<<"\r\n";
                std::string line=s.str();DWORD written=0;
                if(SetFilePointer(f,0,0,FILE_END)!=INVALID_SET_FILE_POINTER)
                    WriteFile(f,line.data(),line.size(),&written,0);
            }
        }
    }catch(...){}
    if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);
    if(locked)ReleaseMutex(mutex);
    if(mutex)CloseHandle(mutex);
    SetLastError(saved);
}
