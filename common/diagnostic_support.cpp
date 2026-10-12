#include "diagnostic_support.h"

static HANDLE g_log=INVALID_HANDLE_VALUE;
static BOOL g_ok=TRUE;
static DWORD g_start=0;
static BOOL Write(const WCHAR* s) {
    DWORD bytes=lstrlenW(s)*2,written=0;
    return WriteFile(g_log,s,bytes,&written,0)&&written==bytes;
}
void DiagLog(const WCHAR* text) {
    WCHAR prefix[32];wsprintfW(prefix,L"[%lu] ",GetTickCount()-g_start);
    if(!Write(prefix)||!Write(text)||!Write(L"\r\n"))g_ok=FALSE;
}
void DiagValues(const WCHAR* label,DWORD a,DWORD b,DWORD c) {
    WCHAR text[200];wsprintfW(text,L"%s a=%lu b=%lu c=%lu",label,a,b,c);DiagLog(text);
}
BOOL DiagLogOK(){return g_ok;}
static DWORD WINAPI Deadline(LPVOID){Sleep(120000);ExitProcess(124);return 0;}

static BOOL Equal(const char* a,const char* b) {
    while(*a && *a==*b){++a;++b;}return *a==*b;
}
static char* Trim(char* s) {
    while(*s==' '||*s=='\t')++s;
    char* end=s;while(*end)++end;
    while(end>s && (end[-1]==' '||end[-1]=='\t'))--end;
    *end=0;return s;
}
static BOOL Number(const char* s,DWORD limit,DWORD* out) {
    if(!*s)return FALSE;DWORD n=0;
    while(*s){if(*s<'0'||*s>'9')return FALSE;DWORD d=*s++-'0';
        if(n>limit/10 || (n==limit/10 && d>limit%10))return FALSE;n=n*10+d;}
    *out=n;return TRUE;
}
extern "C" __declspec(dllexport) BOOL ParseDiagnosticOptions(const BYTE* bytes,DWORD length,DiagnosticOptions* out,BOOL allowTestWindow) {
    if(!bytes||!out||!length||length>1024)return FALSE;
    DWORD pos=0,seen=0;BOOL section=FALSE;
    DiagnosticOptions cfg=*out;
    if(length>=3 && bytes[0]==0xef && bytes[1]==0xbb && bytes[2]==0xbf)pos=3;
    while(pos<length) {
        char line[128];DWORD n=0;
        while(pos<length && bytes[pos]!='\r' && bytes[pos]!='\n') {
            BYTE c=bytes[pos++];
            if((c<32 && c!='\t')||c>126||n>=127)return FALSE;
            line[n++]=(char)c;
        }
        line[n]=0;
        if(pos<length && bytes[pos]=='\r')++pos;
        if(pos<length && bytes[pos]=='\n')++pos;
        char* s=Trim(line);if(!*s||*s=='#'||*s==';')continue;
        if(Equal(s,"[diagnostic]")){if(section)return FALSE;section=TRUE;continue;}
        if(!section)return FALSE;
        char* value=s;while(*value && *value!='=')++value;
        if(!*value)return FALSE;*value++=0;s=Trim(s);value=Trim(value);
        DWORD bit=0;
        if(Equal(s,"delay_ms")) {
            bit=1;if(!Number(value,30000,&cfg.delayMs))return FALSE;
        } else if(Equal(s,"duration_ms")) {
            bit=2;if(!Number(value,60000,&cfg.durationMs)||cfg.durationMs<1000)return FALSE;
        } else if(Equal(s,"show_test_window") && allowTestWindow) {
            bit=4;if(!Number(value,1,&cfg.showTestWindow))return FALSE;
        } else return FALSE;
        if(seen&bit)return FALSE;seen|=bit;
    }
    if(!section)return FALSE;
    *out=cfg;return TRUE;
}

void RunDiagnostic(const WCHAR* name,DiagnosticFunction function,BOOL readOptions,DWORD defaultDelayMs,BOOL allowTestWindow) {
    g_start=GetTickCount();
    WCHAR path[MAX_PATH];wsprintfW(path,L"%s_V2_SINGLETON",name);
    HANDLE singleton=CreateMutexW(0,FALSE,path);
    if(!singleton||GetLastError()==ERROR_ALREADY_EXISTS)ExitProcess(10);
    HANDLE guard=CreateThread(0,0,Deadline,0,0,0);
    if(!guard)ExitProcess(11);CloseHandle(guard);
    WCHAR self[MAX_PATH];DWORD n=GetModuleFileNameW(0,self,MAX_PATH);
    if(!n||n>=MAX_PATH)ExitProcess(12);
    WCHAR folder[MAX_PATH];DWORD last=0;
    for(DWORD i=0;i<n;++i)if(self[i]==L'\\')last=i+1;
    if(!last||last>170)ExitProcess(12);
    for(DWORD j=0;j<last;++j)folder[j]=self[j];folder[last]=0;
    for(DWORD serial=1;serial<=9999;++serial){
        wsprintfW(path,L"%s%s_%04lu.txt",folder,name,serial);
        g_log=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);
        if(g_log!=INVALID_HANDLE_VALUE)break;
        DWORD error=GetLastError();if(error!=ERROR_FILE_EXISTS && error!=ERROR_ALREADY_EXISTS)ExitProcess(13);
    }
    if(g_log==INVALID_HANDLE_VALUE)ExitProcess(14);
    if(!Write(L"\xfeff"))g_ok=FALSE;
    DiagLog(L"START WINCE_DIAGNOSTIC_TOOLS_V4");DiagLog(name);
    DiagValues(L"PID_OS_UPTIME",GetCurrentProcessId(),g_start);
    DiagnosticOptions cfg={defaultDelayMs,20000,0};BOOL valid=TRUE;
    if(readOptions && g_ok){
        wsprintfW(path,L"%s%s.ini",folder,name);
        HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
        if(file!=INVALID_HANDLE_VALUE){
            BYTE bytes[1024];DWORD high=0,size=GetFileSize(file,&high),got=0;
            valid=!high && size && size<=sizeof(bytes) && ReadFile(file,bytes,size,&got,0) && got==size
                  && ParseDiagnosticOptions(bytes,size,&cfg,allowTestWindow);
            CloseHandle(file);
        }else{
            DWORD error=GetLastError();
            valid=error==ERROR_FILE_NOT_FOUND;
            DiagLog(valid?L"OPTIONS_ABSENT_USING_DEFAULTS":L"OPTIONS_OPEN_FAILED");
            if(!valid)DiagValues(L"OPTIONS_ERROR",error);
        }
    }
    BOOL passed=FALSE;
    if(!valid)DiagLog(L"OPTIONS_INVALID_NO_DIAGNOSTIC_EXECUTED");
    else if(g_ok){
        DiagValues(L"DELAY_DURATION",cfg.delayMs,cfg.durationMs);
        if(allowTestWindow)DiagValues(L"SHOW_TEST_WINDOW",cfg.showTestWindow);
        if(!FlushFileBuffers(g_log))g_ok=FALSE;
        if(g_ok){
            if(cfg.delayMs)Sleep(cfg.delayMs);
            DiagLog(L"DIAGNOSTIC_BEGIN");
            if(g_ok)passed=function(&cfg);
            DiagLog(passed?L"DIAGNOSTIC_COMPLETE":L"DIAGNOSTIC_FAILED");
        }
    }
    DiagLog(L"END_REQUESTED");
    if(!FlushFileBuffers(g_log))g_ok=FALSE;
    CloseHandle(g_log);CloseHandle(singleton);
    ExitProcess(valid&&passed&&g_ok?0:1);
}
