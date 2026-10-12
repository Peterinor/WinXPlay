// UI has its own message loop; file operations never call into the UI thread.
static CRITICAL_SECTION g_uiLock;
static BOOL g_uiEnabled=FALSE,g_uiDone=FALSE;
static volatile BOOL g_uiDestroyed=FALSE;
static DWORD g_uiFinishTick=0,g_uiRevision=0,g_uiPainted=0;
static const DWORD g_uiCloseSeconds=5;
static WCHAR g_uiTitle[160]=L"FileOps v2.9 - Starting";
static WCHAR g_uiSource[MAX_PATH]=L"",g_uiTarget[MAX_PATH]=L"";
static WCHAR g_uiLines[48][360];
static DWORD g_uiHead=0,g_uiCount=0;
static HWND g_uiWindow=0,g_uiEdit=0,g_uiButton=0;
static HFONT g_uiFont=0;

static void UiAppend(const WCHAR* prefix,const WCHAR* text){
    if(!g_uiEnabled)return;
    EnterCriticalSection(&g_uiLock);
    WCHAR* dest=g_uiLines[g_uiHead];DWORD used=0;
    const WCHAR* parts[]={prefix,text};
    for(int p=0;p<2;++p)for(DWORD i=0;parts[p][i]&&used<359;++i)dest[used++]=parts[p][i];
    dest[used]=0;g_uiHead=(g_uiHead+1)%48;if(g_uiCount<48)++g_uiCount;
    ++g_uiRevision;LeaveCriticalSection(&g_uiLock);
}
static const WCHAR* UiOperation(DWORD op){
    switch(op){case OP_COPY:return L"COPY";case OP_INSTALL:return L"INSTALL";
    case OP_WATCH:return L"WATCH";case OP_DELETE:return L"DELETE";
    case OP_MOVE:return L"MOVE";case OP_RUN:return L"RUN";}return L"UNKNOWN";
}
static void UiJob(DWORD index,DWORD count,const FileJob& job){
    if(!g_uiEnabled)return;
    EnterCriticalSection(&g_uiLock);
    wsprintfW(g_uiTitle,L"FileOps v2.9 - %lu / %lu - %s",index,count,UiOperation(job.operation));
    lstrcpyW(g_uiSource,job.source);lstrcpyW(g_uiTarget,job.target);
    ++g_uiRevision;LeaveCriticalSection(&g_uiLock);
}
static void UiRefresh(){
    static WCHAR snapshot[48*362+1];
    BOOL changed=FALSE;
    EnterCriticalSection(&g_uiLock);
    if(g_uiPainted!=g_uiRevision){
        DWORD used=0,start=(g_uiHead+48-g_uiCount)%48;
        for(DWORD i=0;i<g_uiCount;++i){
            const WCHAR* line=g_uiLines[(start+i)%48];
            for(DWORD j=0;line[j];++j)snapshot[used++]=line[j];
            snapshot[used++]=L'\r';snapshot[used++]=L'\n';
        }
        snapshot[used]=0;g_uiPainted=g_uiRevision;changed=TRUE;
    }
    BOOL done=g_uiDone;LeaveCriticalSection(&g_uiLock);
    if(changed){SetWindowTextW(g_uiEdit,snapshot);SendMessageW(g_uiEdit,WM_VSCROLL,SB_BOTTOM,0);}
    EnableWindow(g_uiButton,done);
    InvalidateRect(g_uiWindow,0,FALSE);
}
static LRESULT CALLBACK FileOpsWindowProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
    if(msg==WM_TIMER){
        EnterCriticalSection(&g_uiLock);
        BOOL expired=g_uiDone && DWORD(GetTickCount()-g_uiFinishTick)>=g_uiCloseSeconds*1000;
        LeaveCriticalSection(&g_uiLock);
        if(expired){ShowWindow(hwnd,SW_HIDE);DestroyWindow(hwnd);}
        else UiRefresh();
        return 0;
    }
    if(msg==WM_CLOSE||(msg==WM_COMMAND&&LOWORD(w)==100)){
        EnterCriticalSection(&g_uiLock);BOOL done=g_uiDone;LeaveCriticalSection(&g_uiLock);
        if(done){ShowWindow(hwnd,SW_HIDE);DestroyWindow(hwnd);}return 0;
    }
    if(msg==WM_DESTROY){g_uiDestroyed=TRUE;KillTimer(hwnd,1);PostQuitMessage(0);return 0;}
    if(msg==WM_PAINT){
        WCHAR title[160],source[MAX_PATH],target[MAX_PATH],footer[160];BOOL done;DWORD finish;
        EnterCriticalSection(&g_uiLock);
        lstrcpyW(title,g_uiTitle);lstrcpyW(source,g_uiSource);lstrcpyW(target,g_uiTarget);
        done=g_uiDone;finish=g_uiFinishTick;
        LeaveCriticalSection(&g_uiLock);
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT rect;GetClientRect(hwnd,&rect);
        FillRect(dc,&rect,(HBRUSH)GetStockObject(BLACK_BRUSH));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
        HGDIOBJ old=SelectObject(dc,g_uiFont?g_uiFont:GetStockObject(SYSTEM_FONT));
        RECT line={12,8,rect.right-12,40};DrawTextW(dc,title,-1,&line,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
        line.top=42;line.bottom=83;DrawTextW(dc,source,-1,&line,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
        line.top=85;line.bottom=126;DrawTextW(dc,target,-1,&line,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
        if(done){DWORD elapsed=(GetTickCount()-finish)/1000;wsprintfW(footer,L"Finished - closes in %lu s. Full log: RESULT.txt",elapsed<g_uiCloseSeconds?g_uiCloseSeconds-elapsed:0);}
        else wsprintfW(footer,L"Running - elapsed %lu s. File operations continue independently.",(GetTickCount()-g_begin)/1000);
        line.top=rect.bottom-42;line.bottom=rect.bottom-4;line.right=rect.right-130;
        DrawTextW(dc,footer,-1,&line,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
        SelectObject(dc,old);EndPaint(hwnd,&ps);return 0;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
static DWORD WINAPI FileOpsUiThread(LPVOID){
    HINSTANCE instance=GetModuleHandleW(0);WNDCLASSW cls={0};
    cls.lpfnWndProc=FileOpsWindowProc;cls.hInstance=instance;cls.lpszClassName=L"MRN_FILEOPS_PROGRESS_V25";
    if(!RegisterClassW(&cls))return 1;
    int width=GetSystemMetrics(SM_CXSCREEN),height=GetSystemMetrics(SM_CYSCREEN);
    HWND window=CreateWindowExW(WS_EX_TOPMOST,cls.lpszClassName,L"MRN File Operations",WS_POPUP|WS_VISIBLE,
        0,0,width,height,0,0,instance,0);
    if(!window)return 2;g_uiWindow=window;
    g_uiEdit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,
        12,132,width-24,height-182,window,0,instance,0);
    g_uiButton=CreateWindowExW(0,L"BUTTON",L"Close",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        width-116,height-42,104,34,window,(HMENU)100,instance,0);
    if(!g_uiEdit||!g_uiButton){DestroyWindow(window);return 3;}
    LOGFONTW font={0};font.lfHeight=-18;lstrcpyW(font.lfFaceName,L"Tahoma");g_uiFont=CreateFontIndirectW(&font);
    SendMessageW(g_uiEdit,EM_SETLIMITTEXT,48*362,0);
    if(g_uiFont)SendMessageW(g_uiEdit,WM_SETFONT,(WPARAM)g_uiFont,TRUE);
    EnableWindow(g_uiButton,FALSE);ShowWindow(window,SW_SHOW);SetForegroundWindow(window);UpdateWindow(window);
    if(!SetTimer(window,1,250,0)){DestroyWindow(window);return 4;}
    MSG message;while(GetMessageW(&message,0,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    if(g_uiFont)DeleteObject(g_uiFont);
    return 0;
}
static HANDLE UiStart(){
    InitializeCriticalSection(&g_uiLock);g_uiEnabled=TRUE;
    HANDLE thread=CreateThread(0,0,FileOpsUiThread,0,0,0);
    if(!thread)g_uiEnabled=FALSE;
    return thread;
}
static HANDLE g_uiThread=0;
static void FinishFileOps(DWORD code){
    if(g_uiEnabled){
        EnterCriticalSection(&g_uiLock);
        wsprintfW(g_uiTitle,code?L"FileOps v2.9 - FINISHED WITH ERRORS (code %lu)":L"FileOps v2.9 - ALL COMPLETE (code %lu)",code);
        g_uiDone=TRUE;g_uiFinishTick=GetTickCount();++g_uiRevision;
        LeaveCriticalSection(&g_uiLock);
        // The UI timer hides/destroys its own window at 5s. Allow the message
        // loop to finish before process exit; never destroy across threads.
        if(g_uiThread){
            DWORD waited=WaitForSingleObject(g_uiThread,(g_uiCloseSeconds+2)*1000);
            Note(waited==WAIT_OBJECT_0?L"UI_THREAD_EXIT_CONFIRMED":L"UI_THREAD_EXIT_NOT_CONFIRMED_FALLBACK");
            Note(g_uiDestroyed?L"UI_WM_DESTROY_CONFIRMED":L"UI_WM_DESTROY_NOT_CONFIRMED");
            CloseHandle(g_uiThread);g_uiThread=0;
        }
    }
    Note(L"PROCESS_EXIT_REQUESTED");
    if(g_log!=INVALID_HANDLE_VALUE){FlushFileBuffers(g_log);CloseHandle(g_log);}
    ExitProcess(code);
}
