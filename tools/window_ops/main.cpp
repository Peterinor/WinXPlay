#include "window_support.h"

static WindowOptions g_options;
static HWND g_target=0,g_test=0;
static DWORD g_targetPid=0,g_targetTid=0,g_startTest=0,g_inputs=0,g_paints=0;
static DWORD g_fillResult=0,g_textResult=0,g_paintDC=0;
static BOOL g_shown=FALSE,g_hidingOwn=FALSE;
static HANDLE g_hidden=0,g_restoring=0,g_done=0,g_cancel=0,g_helper=0;

static void BackupRestore() {
    if(g_cancel)SetEvent(g_cancel);
    if(Identity(g_target,g_targetPid,g_targetTid)) {
        if(!IsWindowVisible(g_target)){Note(L"BACKUP_RESTORE_BEGIN");ShowWindow(g_target,SW_SHOW);}
        SetForegroundWindow(g_target);
        Data(L"BACKUP_TARGET_VISIBLE",IsWindowVisible(g_target));
    }
}
static LRESULT CALLBACK TestWindow(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_PAINT){
        ++g_paints;PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);g_paintDC=dc?1:0;
        HBRUSH brush=CreateSolidBrush(RGB(0,40,160));
        if(dc){g_fillResult=FillRect(dc,&r,brush);SetTextColor(dc,RGB(255,255,0));SetBkMode(dc,TRANSPARENT);
            g_textResult=DrawTextW(dc,L"WINDOW OPERATIONS\n\nTarget hidden temporarily\nAutomatic restore is armed",-1,&r,DT_CENTER|DT_WORDBREAK);}
        if(brush)DeleteObject(brush);EndPaint(h,&ps);return 0;
    }
    if((msg>=0x100 && msg<=0x109)||(msg>=0x200 && msg<=0x20e)||msg==0x8000||msg==0x8001){if(g_inputs++<32)Data(L"INPUT_MSG_WP_LP",msg,(DWORD)wp,(DWORD)lp);}
    if(msg==WM_TIMER){
        BOOL restoring=WaitForSingleObject(g_restoring,0)==WAIT_OBJECT_0;
        BOOL hidden=WaitForSingleObject(g_hidden,0)==WAIT_OBJECT_0;
        if(g_options.showTestWindow && WindowOpsMayShow(hidden,restoring,g_shown)){
            g_shown=TRUE;ShowWindow(h,SW_SHOW);SetForegroundWindow(h);UpdateWindow(h);
            // Recovery can start during painting. Immediately yield again.
            if(WaitForSingleObject(g_restoring,0)==WAIT_OBJECT_0){ShowWindow(h,SW_HIDE);g_hidingOwn=TRUE;}
            else Data(L"TEST_WINDOW_SHOWN_VISIBLE",IsWindowVisible(h));
        }
        if(restoring && !g_hidingOwn){g_hidingOwn=TRUE;ShowWindow(h,SW_HIDE);Note(L"TEST_WINDOW_HIDDEN_FOR_RESTORE");}
        DWORD elapsed=GetTickCount()-g_startTest;
        if(elapsed>=g_options.durationMs+1500 && !restoring){SetEvent(g_cancel);ShowWindow(h,SW_HIDE);BackupRestore();}
        if(WaitForSingleObject(g_done,0)==WAIT_OBJECT_0 || (g_helper && WaitForSingleObject(g_helper,0)==WAIT_OBJECT_0) || elapsed>=g_options.durationMs+7000)
            PostMessageW(h,WM_CLOSE,0,0);
        return 0;
    }
    if(msg==WM_CLOSE){if(g_cancel)SetEvent(g_cancel);KillTimer(h,1);ShowWindow(h,SW_HIDE);DestroyWindow(h);return 0;}
    if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,msg,wp,lp);
}
extern "C" void WindowOpsEntry() {
    g_begin=GetTickCount();HANDLE mutex=CreateMutexW(0,FALSE,L"WINDOW_OPS_SINGLETON");
    DWORD error=GetLastError();if(!mutex||error==ERROR_ALREADY_EXISTS)ExitProcess(10);
    WCHAR folder[MAX_PATH],path[MAX_PATH];if(!Folder(folder))ExitProcess(11);
    DWORD run;
    for(run=1;run<=9999;++run){wsprintfW(path,L"%swindow_ops_%04lu.txt",folder,run);if(OpenLog(path))break;
        error=GetLastError();if(error!=ERROR_FILE_EXISTS&&error!=ERROR_ALREADY_EXISTS)ExitProcess(12);}
    if(run>9999)ExitProcess(13);
    Note(L"START WINDOW_OPS");
    if(!ReadOptions(folder,&g_options)){Note(L"ABORT_INVALID_CONFIG_NO_HIDE");CloseHandle(g_log);ExitProcess(30);}
    Note(g_options.title);Data(L"DELAY_DURATION_TEST",g_options.delayMs,g_options.durationMs,g_options.showTestWindow);
    DWORD elapsed=GetTickCount()-g_begin;if(elapsed<g_options.delayMs)Sleep(g_options.delayMs-elapsed);
    g_target=WindowOpsFindTarget(&g_options);
    g_targetTid=GetWindowThreadProcessId(g_target,&g_targetPid);
    Data(L"TARGET_HWND_PID_TID",(DWORD)g_target,g_targetPid,g_targetTid);
    HWND initialForeground=GetForegroundWindow();DWORD foregroundPid=0;
    DWORD foregroundTid=GetWindowThreadProcessId(initialForeground,&foregroundPid);
    Data(L"INITIAL_FOREGROUND_HWND_PID_TID",(DWORD)initialForeground,foregroundPid,foregroundTid);
    Data(L"INITIAL_TARGET_VISIBLE",g_target?IsWindowVisible(g_target):0);
    if(!Identity(g_target,g_targetPid,g_targetTid)||!IsTargetProcess(g_targetPid,g_options.processName)||!IsWindowVisible(g_target)||GetForegroundWindow()!=g_target){
        Note(L"ABORT_TARGET_NOT_EXACT_VISIBLE_FOREGROUND_NO_HIDE");CloseHandle(g_log);ExitProcess(14);
    }
    RECT original={0};GetWindowRect(g_target,&original);Data(L"TARGET_RECT_LEFT_TOP",original.left,original.top);Data(L"TARGET_RECT_RIGHT_BOTTOM",original.right,original.bottom);
    DWORD parent=GetCurrentProcessId();
    g_hidden=EventHandle(parent,run,L"HIDDEN",FALSE);g_restoring=EventHandle(parent,run,L"RESTORING",FALSE);
    g_done=EventHandle(parent,run,L"DONE",FALSE);g_cancel=EventHandle(parent,run,L"CANCEL",FALSE);
    if(!g_hidden||!g_restoring||!g_done||!g_cancel){Note(L"ABORT_EVENTS_NO_HIDE");ExitProcess(15);}
    WNDCLASSW wc={0};wc.lpfnWndProc=TestWindow;wc.hInstance=GetModuleHandleW(0);wc.lpszClassName=L"WindowOpsTest";
    if(!RegisterClassW(&wc)){Data(L"REGISTER_ERROR",GetLastError());ExitProcess(16);}
    g_test=CreateWindowExW(0,wc.lpszClassName,L"WINDOW OPERATIONS",WS_POPUP,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),0,0,wc.hInstance,0);
    if(!g_test||!SetTimer(g_test,1,100,0)){Note(L"ABORT_TEST_WINDOW_TIMER_NO_HIDE");ExitProcess(17);}
    WCHAR command[150];wsprintfW(command,L"%lu %lu %lu %lu %lu %lu",(DWORD)g_target,g_targetPid,g_targetTid,parent,run,g_options.durationMs);
    wsprintfW(path,L"%swindow_restore.exe",folder);
    PROCESS_INFORMATION child={0};Note(L"START_RESTORE_HELPER");FlushFileBuffers(g_log);
    if(!g_logOK || !CreateProcessW(path,command,0,0,FALSE,0,0,0,0,&child)){Data(L"HELPER_START_ERROR_NO_HIDE",GetLastError());DestroyWindow(g_test);ExitProcess(18);}
    g_helper=child.hProcess;CloseHandle(child.hThread);g_startTest=GetTickCount();
    MSG message;while(GetMessageW(&message,0,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    BackupRestore();DWORD waited=WaitForSingleObject(g_helper,10000),code=0;
    BOOL queried=FALSE;if(waited==WAIT_OBJECT_0)queried=GetExitCodeProcess(g_helper,&code);
    Data(L"HELPER_WAIT_QUERY_EXIT",waited,queried,code);
    Data(L"TEST_SHOWN_PAINTS_INPUTS",g_shown,g_paints,g_inputs);
    Data(L"PAINT_DC_FILL_TEXT",g_paintDC,g_fillResult,g_textResult);
    if(Identity(g_target,g_targetPid,g_targetTid))Data(L"FINAL_TARGET_VISIBLE_FOREGROUND",IsWindowVisible(g_target),GetForegroundWindow()==g_target);
    Note(L"END_HIDE_TEST_REQUESTED");FlushFileBuffers(g_log);CloseHandle(g_log);CloseHandle(g_helper);CloseHandle(mutex);ExitProcess(queried && code==0?0:31);
}
