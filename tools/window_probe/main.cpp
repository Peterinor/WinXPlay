#include "../../common/diagnostic_support.h"

static DWORD g_start=0,g_duration=0,g_inputs=0;
static BOOL g_failed=FALSE;
static HWND g_last=0;
static BOOL g_firstForeground=TRUE;
static void Foreground() {
    HWND w=GetForegroundWindow();
    if(g_firstForeground||w!=g_last){g_firstForeground=FALSE;g_last=w;DWORD pid=0,tid=w?GetWindowThreadProcessId(w,&pid):0;
        DiagValues(L"FOREGROUND_HWND_PID_TID",(DWORD)w,pid,tid);
        DiagValues(L"FOREGROUND_VISIBLE",w?IsWindowVisible(w):0);
    }
}
static LRESULT CALLBACK InputWindow(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if((message>=WM_KEYFIRST && message<=WM_KEYLAST)||(message>=WM_MOUSEFIRST && message<=WM_MOUSELAST)){
        if(g_inputs<256)DiagValues(L"INPUT_MESSAGE_WPARAM_LPARAM",message,(DWORD)wp,(DWORD)lp);
        if(g_inputs==256)DiagLog(L"INPUT_LOG_LIMIT_REACHED");
        if(g_inputs<0xffffffff)++g_inputs;
    }
    if(message==WM_PAINT){
        PAINTSTRUCT paint;HDC dc=BeginPaint(window,&paint);RECT r;GetClientRect(window,&r);
        if(!dc)g_failed=TRUE;
        else{
            FillRect(dc,&r,(HBRUSH)GetStockObject(WHITE_BRUSH));SetBkMode(dc,TRANSPARENT);
            WCHAR text[160];wsprintfW(text,L"MRN WINDOW OBSERVER\nInput window\nMessages: %lu\nAuto close; OEM HMI is still running",g_inputs);
            DrawTextW(dc,text,-1,&r,DT_CENTER|DT_WORDBREAK);
        }
        EndPaint(window,&paint);return 0;
    }
    if(message==WM_TIMER){
        Foreground();
        if(GetTickCount()-g_start>=g_duration||!DiagLogOK())PostMessageW(window,WM_CLOSE,0,0);
        else InvalidateRect(window,0,FALSE);
        return 0;
    }
    if(message==WM_CLOSE){KillTimer(window,1);DestroyWindow(window);return 0;}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
static BOOL ObserveWindow(const DiagnosticOptions* options) {
    g_start=GetTickCount();g_duration=options->durationMs;
    DiagLog(L"NO_FOREIGN_WINDOW_TEXT_QUERY_NO_HMI_HIDE_OR_TERMINATE");
    DiagValues(L"INITIAL_FOREGROUND",(DWORD)GetForegroundWindow());
    Foreground();
    if(!options->showTestWindow){
        DiagLog(L"PASSIVE_WINDOW_OBSERVATION_NO_TEST_WINDOW");
        do{Sleep(250);Foreground();}while(GetTickCount()-g_start<g_duration && DiagLogOK());
        return DiagLogOK();
    }
    DiagLog(L"TEST_WINDOW_REQUESTED_BY_CONFIGURATION");
    WNDCLASSW wc={0};wc.hInstance=GetModuleHandleW(0);wc.lpfnWndProc=InputWindow;wc.lpszClassName=L"MRNWindowObserverV3";
    if(!RegisterClassW(&wc)){DiagValues(L"REGISTER_CLASS_ERROR",GetLastError());return FALSE;}
    HWND w=CreateWindowExW(0,wc.lpszClassName,L"MRN WINDOW OBSERVER",WS_POPUP|WS_VISIBLE,8,40,460,180,0,0,wc.hInstance,0);
    if(!w){DiagValues(L"CREATE_WINDOW_ERROR",GetLastError());return FALSE;}
    if(!SetTimer(w,1,250,0)){DiagValues(L"TIMER_ERROR",GetLastError());DestroyWindow(w);return FALSE;}
    DiagValues(L"INPUT_WINDOW_HWND_FOREGROUND_REQUEST",(DWORD)w,SetForegroundWindow(w));
    DiagLog(L"ONLY_MESSAGES_DELIVERED_TO_OUR_WINDOW_NOT_GLOBAL_INPUT");UpdateWindow(w);
    MSG msg;int result;
    while((result=GetMessageW(&msg,0,0,0))>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    if(result<0){DiagValues(L"GET_MESSAGE_ERROR",GetLastError());if(IsWindow(w))DestroyWindow(w);return FALSE;}
    DiagValues(L"INPUT_COUNT",g_inputs);return !g_failed&&DiagLogOK();
}

extern "C" void WindowObserverEntry() {
    RunDiagnostic(L"window_probe",ObserveWindow,TRUE,10000,TRUE);
}
