#pragma once
#include <windows.h>

struct WindowOptions {
    WCHAR title[128], className[128], processName[64];
    DWORD delayMs, durationMs, showTestWindow;
};
static BOOL EqualText(const WCHAR* a,const WCHAR* b) {
    while(*a && *a==*b){++a;++b;}return *a==*b;
}
static BOOL CopyText(WCHAR* to,const WCHAR* from,DWORD capacity) {
    DWORD n=0;while(from[n]){if(n+1>=capacity)return FALSE;to[n]=from[n];++n;}to[n]=0;return TRUE;
}
static BOOL Number(const WCHAR* s,DWORD low,DWORD high,DWORD* out) {
    if(!*s)return FALSE;DWORD n=0;
    for(;*s;++s){if(*s<L'0'||*s>L'9')return FALSE;DWORD d=*s-L'0';if(n>(high-d)/10 || d>high)return FALSE;n=n*10+d;}
    if(n<low||n>high)return FALSE;*out=n;return TRUE;
}
// Parse completely before publishing options; callers never act on partial input.
extern "C" __declspec(dllexport) BOOL WindowOpsParseConfig(const WCHAR* text,WindowOptions* out) {
    WindowOptions value={0};value.delayMs=10000;value.durationMs=5000;
    DWORD mask=0,total=0;BOOL section=FALSE;
    while(*text){
        WCHAR line[256];DWORD n=0;
        while(*text && *text!=L'\n' && *text!=L'\r'){
            if(n>=255 || ++total>2048)return FALSE;line[n++]=*text++;
        }
        while(*text==L'\n'||*text==L'\r'){if(++total>2048)return FALSE;++text;}
        line[n]=0;WCHAR* s=line;while(*s==L' '||*s==L'\t')++s;
        while(n && (line[n-1]==L' '||line[n-1]==L'\t'))line[--n]=0;
        if(!*s||*s==L'#'||*s==L';')continue;
        if(!section){if(!EqualText(s,L"[window_ops]"))return FALSE;section=TRUE;continue;}
        WCHAR* eq=s;while(*eq && *eq!=L'=')++eq;if(!*eq)return FALSE;*eq++=0;
        WCHAR* end=eq-1;while(end>s && (end[-1]==L' '||end[-1]==L'\t'))*--end=0;
        while(*eq==L' '||*eq==L'\t')++eq;
        DWORD bit=0;
        if(EqualText(s,L"window_title")){bit=1;if(!*eq||!CopyText(value.title,eq,128))return FALSE;}
        else if(EqualText(s,L"window_class")){bit=2;if(!CopyText(value.className,eq,128))return FALSE;}
        else if(EqualText(s,L"process_name")){
            bit=4;if(!CopyText(value.processName,eq,64))return FALSE;
            for(const WCHAR* p=eq;*p;++p)if(*p==L'\\'||*p==L'/'||*p==L':'||*p==L'*'||*p==L'?')return FALSE;
        }
        else if(EqualText(s,L"delay_ms")){bit=8;if(!Number(eq,0,60000,&value.delayMs))return FALSE;}
        else if(EqualText(s,L"duration_ms")){bit=16;if(!Number(eq,1000,60000,&value.durationMs))return FALSE;}
        else if(EqualText(s,L"show_test_window")){bit=32;if(!Number(eq,0,1,&value.showTestWindow))return FALSE;}
        else return FALSE;
        if(mask&bit)return FALSE;mask|=bit;
    }
    if(!section||!(mask&1))return FALSE;
    *out=value;return TRUE;
}
static BOOL ReadOptions(const WCHAR* folder,WindowOptions* out) {
    WCHAR path[MAX_PATH];wsprintfW(path,L"%swindow_ops.ini",folder);
    HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    if(h==INVALID_HANDLE_VALUE)return FALSE;
    DWORD high=0,size=GetFileSize(h,&high),read=0;BYTE bytes[4096];WCHAR text[4096];
    BOOL ok=!high && size>0 && size<=4094 && ReadFile(h,bytes,size,&read,0) && read==size;
    CloseHandle(h);if(!ok)return FALSE;
    DWORD count=0;
    if(size>=2 && bytes[0]==0xff && bytes[1]==0xfe){
        if(size%2)return FALSE;
        for(DWORD i=2;i<size;i+=2)text[count++]=(WCHAR)(bytes[i]|(bytes[i+1]<<8));
    }else{
        DWORD skip=size>=3 && bytes[0]==0xef && bytes[1]==0xbb && bytes[2]==0xbf?3:0;
        int n=MultiByteToWideChar(CP_UTF8,0,(const char*)bytes+skip,size-skip,text,4095);
        if(n<=0)return FALSE;count=(DWORD)n;
    }
    for(DWORD j=0;j<count;++j)if(!text[j])return FALSE;
    text[count]=0;return WindowOpsParseConfig(text,out);
}
