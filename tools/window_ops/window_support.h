#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include "window_config.h"

static HANDLE g_log=INVALID_HANDLE_VALUE;
static DWORD g_begin=0;
static BOOL g_logOK=TRUE;
static void Note(const WCHAR* text) {
    if(g_log==INVALID_HANDLE_VALUE)return;
    WCHAR prefix[32];wsprintfW(prefix,L"[%lu] ",GetTickCount()-g_begin);
    DWORD n=0,size=lstrlenW(prefix)*2;
    if(!WriteFile(g_log,prefix,size,&n,0)||n!=size)g_logOK=FALSE;
    size=lstrlenW(text)*2;
    if(!WriteFile(g_log,text,size,&n,0)||n!=size)g_logOK=FALSE;
    if(!WriteFile(g_log,L"\r\n",4,&n,0)||n!=4)g_logOK=FALSE;
}
static void Data(const WCHAR* name,DWORD a,DWORD b=0,DWORD c=0) {
    WCHAR line[200];wsprintfW(line,L"%s a=%lu b=%lu c=%lu",name,a,b,c);Note(line);
}
static BOOL Folder(WCHAR* path) {
    DWORD n=GetModuleFileNameW(0,path,MAX_PATH),last=0;
    if(!n||n>=MAX_PATH)return FALSE;
    for(DWORD i=0;i<n;++i)if(path[i]==L'\\')last=i+1;
    if(!last || last>MAX_PATH-80)return FALSE;path[last]=0;return TRUE;
}
static BOOL OpenLog(const WCHAR* path) {
    g_log=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);
    if(g_log==INVALID_HANDLE_VALUE)return FALSE;
    WCHAR bom=0xfeff;DWORD n=0;
    return WriteFile(g_log,&bom,2,&n,0)&&n==2;
}
static HANDLE EventHandle(DWORD parent,DWORD run,const WCHAR* kind,BOOL mustExist) {
    WCHAR name[100];wsprintfW(name,L"WINDOW_OPS_%lu_%lu_%s",parent,run,kind);
    HANDLE h=CreateEventW(0,TRUE,FALSE,name);DWORD error=GetLastError();
    if(!h)return 0;
    if((mustExist && error!=ERROR_ALREADY_EXISTS)||(!mustExist && error==ERROR_ALREADY_EXISTS)){CloseHandle(h);return 0;}
    return h;
}
static BOOL Identity(HWND window,DWORD pid,DWORD tid) {
    DWORD current=0;
    return window && IsWindow(window) && GetWindowThreadProcessId(window,&current)==tid && current==pid;
}
static BOOL IsTargetProcess(DWORD pid,const WCHAR* expected) {
    if(!*expected)return TRUE;
    HMODULE m=LoadLibraryW(L"toolhelp.dll");if(!m)return FALSE;
    typedef HANDLE(WINAPI *Snap)(DWORD,DWORD);
    typedef BOOL(WINAPI *Step)(HANDLE,LPPROCESSENTRY32);
    typedef BOOL(WINAPI *Close)(HANDLE);
    Snap snap=(Snap)GetProcAddress(m,L"CreateToolhelp32Snapshot");
    Step first=(Step)GetProcAddress(m,L"Process32First"),next=(Step)GetProcAddress(m,L"Process32Next");
    Close close=(Close)GetProcAddress(m,L"CloseToolhelp32Snapshot");
    BOOL found=FALSE;
    if(snap&&first&&next&&close){HANDLE h=snap(TH32CS_SNAPPROCESS,0);
        if(h && h!=INVALID_HANDLE_VALUE){PROCESSENTRY32 p={0};p.dwSize=sizeof(p);BOOL ok=first(h,&p);DWORD n=0;
            while(ok && n++<256){
                if(p.th32ProcessID==pid){p.szExeFile[MAX_PATH-1]=0;const WCHAR* leaf=p.szExeFile;
                    for(const WCHAR* s=leaf;*s;++s)if(*s==L'\\')leaf=s+1;
                    found=!lstrcmpiW(leaf,expected);break;}
                p.dwSize=sizeof(p);ok=next(h,&p);
            }close(h);
        }
    }FreeLibrary(m);return found;
}
// Strict unsigned decimal parser for six fields: window/process identity, invocation and duration.
extern "C" __declspec(dllexport) DWORD WindowOpsParseArguments(const WCHAR* text,DWORD* out) {
    for(DWORD i=0;i<6;++i){while(*text==L' '||*text==L'\t')++text;
        if(*text<L'0'||*text>L'9')return 0;
        DWORD n=0;while(*text>=L'0'&&*text<=L'9'){DWORD digit=*text-L'0';
            if(n>429496729UL || (n==429496729UL && digit>5))return 0;
            n=n*10+digit;++text;
        }if(!n)return 0;out[i]=n;
        if(*text && *text!=L' ' && *text!=L'\t')return 0;
    }while(*text==L' '||*text==L'\t')++text;return *text==0 && out[5]>=1000 && out[5]<=60000;
}
// Gate main-window display: never show it after restoration has begun.
extern "C" __declspec(dllexport) DWORD WindowOpsMayShow(DWORD hidden,DWORD restoring,DWORD shown) {
    return hidden && !restoring && !shown;
}

static BOOL MatchesWindow(HWND window,const WindowOptions* options) {
    WCHAR title[256],cls[256];
    if(!GetWindowTextW(window,title,256)||!EqualText(title,options->title))return FALSE;
    if(*options->className && (!GetClassNameW(window,cls,256)||!EqualText(cls,options->className)))return FALSE;
    return TRUE;
}
struct WindowSearch {const WindowOptions* options;HWND found;DWORD count;};
static BOOL CALLBACK FindCandidate(HWND window,LPARAM data) {
    WindowSearch* search=(WindowSearch*)data;
    if(!MatchesWindow(window,search->options))return TRUE;
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(!pid || !IsTargetProcess(pid,search->options->processName))return TRUE;
    search->found=window;++search->count;return TRUE;
}
extern "C" __declspec(dllexport) HWND WindowOpsFindTarget(const WindowOptions* options) {
    WindowSearch search={options,0,0};
    if(!EnumWindows(FindCandidate,(LPARAM)&search)||search.count!=1)return 0;
    return search.found;
}
