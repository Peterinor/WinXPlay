// Included inside namespace cp. No heap allocations or application locks on
// the exception path. Always continue exception search; never resume a fault.
static HANDLE crashFile=INVALID_HANDLE_VALUE;
void initializeCrashDiagnostics(const wchar_t *directory) {
    WCHAR path[MAX_PATH];
    if(!directory || lstrlenW(directory)>MAX_PATH-16)return;
    lstrcpyW(path,directory);lstrcatW(path,L"\\crash.log");
    crashFile=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_ALWAYS,0,0);
}
LONG diagnosticException(EXCEPTION_POINTERS *exception,const wchar_t *boundary) {
    DWORD saved=GetLastError();
    if(crashFile!=INVALID_HANDLE_VALUE && exception && exception->ExceptionRecord){
        EXCEPTION_RECORD *r=exception->ExceptionRecord;
        MEMORYSTATUS memory={0};memory.dwLength=sizeof(memory);GlobalMemoryStatus(&memory);
        WCHAR wideLine[384];char line[768];DWORD written=0;
        wsprintfW(wideLine,L"%lu pid=%lu tid=%lu EXCEPTION boundary=%.48s code=%08lx address=%08lx flags=%08lx p0=%08lx p1=%08lx free_phys=%lu free_virtual=%lu\r\n",
            GetTickCount(),GetCurrentProcessId(),GetCurrentThreadId(),boundary,
            r->ExceptionCode,(DWORD)r->ExceptionAddress,r->ExceptionFlags,
            r->NumberParameters>0?(DWORD)r->ExceptionInformation[0]:0,
            r->NumberParameters>1?(DWORD)r->ExceptionInformation[1]:0,
            memory.dwAvailPhys,memory.dwAvailVirtual);
        int count=WideCharToMultiByte(CP_UTF8,0,wideLine,-1,line,sizeof(line),0,0);
        if(count>1 && GetFileSize(crashFile,0)<128*1024 &&
           SetFilePointer(crashFile,0,0,FILE_END)!=INVALID_SET_FILE_POINTER){
            WriteFile(crashFile,line,count-1,&written,0);FlushFileBuffers(crashFile);
        }
    }
    SetLastError(saved);return EXCEPTION_CONTINUE_SEARCH;
}
