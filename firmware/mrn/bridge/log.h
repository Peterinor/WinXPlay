#pragma once
#include "policy.h"
// Only called on bridge worker, never from the original A input hook.
static HANDLE mrnLog=INVALID_HANDLE_VALUE;
#ifndef WINXPLAY_ENABLE_LOGS
#define WINXPLAY_ENABLE_LOGS 1
#endif
#if WINXPLAY_ENABLE_LOGS
static DWORD mrnLogBytes=0;
static void MrnLog(const WCHAR *tag,DWORD a=0,DWORD b=0,DWORD c=0) {
    if(mrnLog==INVALID_HANDLE_VALUE || mrnLogBytes>=256*1024)return;
    WCHAR line[220];
    int n=wsprintfW(line,L"[%lu] %s %lu %lu %lu\r\n",GetTickCount(),tag,a,b,c);
    DWORD written=0;
    if(n>0 && n<220 && mrnLogBytes+DWORD(n*2)<=256*1024) {
        WriteFile(mrnLog,line,n*2,&written,0);mrnLogBytes+=written;
    }
}
static void MrnOpenLog(const WCHAR *stem) {
    WCHAR path[MAX_PATH];wsprintfW(path,MRN_INSTALL L"%s.log",stem);
    mrnLog=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,0,0);
    mrnLogBytes=0;
    if(mrnLog!=INVALID_HANDLE_VALUE){DWORD n=0;WORD bom=0xfeff;WriteFile(mrnLog,&bom,2,&n,0);mrnLogBytes=n;}
}
#else
static void MrnLog(const WCHAR *,DWORD=0,DWORD=0,DWORD=0){}
static void MrnOpenLog(const WCHAR *){}
#endif
