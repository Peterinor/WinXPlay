// Independent standard WinCE progress window, following file_ops' UI pattern.
// The capture thread never performs drawing or waits for window messages.
#pragma once
namespace microphone_ui {
static CRITICAL_SECTION lock;
static HANDLE thread=0,ready=0;
static HWND window=0,previous=0;
static std::wstring text;
static LONG cancelled=0,paints=0,destroyed=0;
static bool done=false,foregroundAccepted=false;
static DWORD finishTick=0,error=0;
static HFONT font=0;

static void setStatus(const std::wstring& value){
    EnterCriticalSection(&lock);text=value;LeaveCriticalSection(&lock);
}
static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_CLOSE||(m==WM_KEYUP&&w==VK_ESCAPE)){
        // Let waveIn cleanup complete before closing the window.
        InterlockedExchange(&cancelled,1);return 0;
    }
    if(m==WM_TIMER&&w==1){
        EnterCriticalSection(&lock);bool expired=done&&DWORD(GetTickCount()-finishTick)>=5000;
        LeaveCriticalSection(&lock);
        if(expired){ShowWindow(h,SW_HIDE);DestroyWindow(h);}
        else InvalidateRect(h,0,FALSE);
        return 0;
    }
    if(m==WM_PAINT){
        EnterCriticalSection(&lock);std::wstring snapshot=text;bool finished=done;DWORD tick=finishTick;
        LeaveCriticalSection(&lock);
        PAINTSTRUCT p;HDC dc=BeginPaint(h,&p);RECT r;GetClientRect(h,&r);
        FillRect(dc,&r,(HBRUSH)GetStockObject(BLACK_BRUSH));
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
        HGDIOBJ old=SelectObject(dc,font?font:GetStockObject(SYSTEM_FONT));
        r.left+=12;r.right-=12;r.top+=12;r.bottom-=12;
        std::wstring message=L"WinXPlay microphone test\nSpeak numbers / pause / speak again.\nNo automatic playback.\n\n"+snapshot;
        if(finished){wchar_t footer[80];DWORD seconds=(GetTickCount()-tick)/1000;
            wsprintfW(footer,L"\n\nFinished - closes in %lu s",seconds<5?5-seconds:0);message+=footer;}
        DrawTextW(dc,message.c_str(),-1,&r,DT_WORDBREAK|DT_NOPREFIX);
        SelectObject(dc,old);EndPaint(h,&p);InterlockedIncrement(&paints);return 0;
    }
    if(m==WM_DESTROY){InterlockedExchange(&destroyed,1);KillTimer(h,1);PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static DWORD WINAPI run(LPVOID){
    HINSTANCE instance=GetModuleHandleW(0);WNDCLASSW wc={0};wc.hInstance=instance;
    wc.lpfnWndProc=procedure;wc.lpszClassName=L"winxplay_microphone_test";
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){error=GetLastError();SetEvent(ready);return 1;}
    previous=GetForegroundWindow();
    window=CreateWindowExW(WS_EX_TOPMOST,wc.lpszClassName,L"Microphone test",WS_POPUP|WS_VISIBLE,
        0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),0,0,instance,0);
    if(!window){error=GetLastError();SetEvent(ready);return 2;}
    LOGFONTW lf={0};lf.lfHeight=-22;lstrcpyW(lf.lfFaceName,L"Tahoma");font=CreateFontIndirectW(&lf);
    ShowWindow(window,SW_SHOW);foregroundAccepted=SetForegroundWindow(window)!=FALSE;UpdateWindow(window);
    if(!SetTimer(window,1,250,0)){error=GetLastError();ShowWindow(window,SW_HIDE);DestroyWindow(window);}
    SetEvent(ready);
    if(!error){MSG m;while(GetMessageW(&m,0,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}}
    if(font){DeleteObject(font);font=0;}
    if(previous&&IsWindow(previous)){SetForegroundWindow(previous);InvalidateRect(previous,0,TRUE);}
    return error?3:0;
}
static void initialize(){
    InitializeCriticalSection(&lock);cancelled=paints=destroyed=0;done=false;error=0;
    foregroundAccepted=false;window=previous=0;thread=ready=0;text.clear();
}
static bool start(){
    ready=CreateEventW(0,TRUE,FALSE,0);if(!ready){error=GetLastError();return false;}
    thread=CreateThread(0,0,run,0,0,0);if(!thread){error=GetLastError();return false;}
    DWORD waited=WaitForSingleObject(ready,5000);
    if(waited!=WAIT_OBJECT_0){error=waited==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError();return false;}
    return !error&&window!=0;
}
static bool stop(){
    EnterCriticalSection(&lock);done=true;finishTick=GetTickCount();LeaveCriticalSection(&lock);
    bool exited=!thread||WaitForSingleObject(thread,7000)==WAIT_OBJECT_0;
    // If a UI API is stuck, keep its shared state valid until process exit.
    if(exited){if(thread)CloseHandle(thread);if(ready)CloseHandle(ready);DeleteCriticalSection(&lock);thread=ready=0;}
    return exited;
}
}
