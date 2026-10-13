#pragma once
#include "policy.h"
// Only called on bridge worker, never from the original A input hook.
static HANDLE mrnLog=INVALID_HANDLE_VALUE;
#ifndef WINXPLAY_ENABLE_LOGS
#define WINXPLAY_ENABLE_LOGS 1
#endif
#if WINXPLAY_ENABLE_LOGS
static DWORD mrnLogBytes=0;
static WCHAR mrnPending[4096];
static DWORD mrnPendingChars=0,mrnFlushedAt=0;
static void MrnFlushLog(BOOL force=FALSE) {
    DWORD now=GetTickCount();
    if(mrnLog==INVALID_HANDLE_VALUE || !mrnPendingChars || (!force && DWORD(now-mrnFlushedAt)<1000))return;
    DWORD written=0;
    WriteFile(mrnLog,mrnPending,mrnPendingChars*2,&written,0);
    mrnLogBytes+=written;mrnPendingChars=0;mrnFlushedAt=now;
    if(force)FlushFileBuffers(mrnLog);
}
static void MrnLog(const WCHAR *tag,DWORD a=0,DWORD b=0,DWORD c=0,BOOL signedB=FALSE) {
    if(mrnLog==INVALID_HANDLE_VALUE || mrnLogBytes>=256*1024)return;
    WCHAR line[220];
    int n=wsprintfW(line,signedB?L"[%lu] %s %lu %ld %lu\r\n":L"[%lu] %s %lu %lu %lu\r\n",GetTickCount(),tag,a,b,c);
    // Timestamp on arrival; batch disk writes away from per-event forwarding.
    if(n>0 && n<220 && mrnPendingChars+DWORD(n)<=4096 &&
       mrnLogBytes+(mrnPendingChars+DWORD(n))*2<=256*1024) {
        CopyMemory(mrnPending+mrnPendingChars,line,n*2);mrnPendingChars+=n;
    }
}
static void MrnOpenLog(const WCHAR *stem) {
    WCHAR path[MAX_PATH];wsprintfW(path,MRN_INSTALL L"%s.log",stem);
    mrnLog=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,0,0);
    mrnLogBytes=0;mrnPendingChars=0;mrnFlushedAt=GetTickCount();
    if(mrnLog!=INVALID_HANDLE_VALUE){DWORD n=0;WORD bom=0xfeff;WriteFile(mrnLog,&bom,2,&n,0);mrnLogBytes=n;}
}
#else
static void MrnLog(const WCHAR *,DWORD=0,DWORD=0,DWORD=0,BOOL=FALSE){}
static void MrnOpenLog(const WCHAR *){}
static void MrnFlushLog(BOOL=FALSE){}
#endif
