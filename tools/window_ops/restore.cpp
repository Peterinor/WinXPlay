#include "window_support.h"

static HWND g_target=0;
static WindowOptions g_options;
static DWORD g_pid=0,g_tid=0;
static HANDLE g_hidden=0,g_restoring=0,g_done=0,g_cancel=0,g_armed=0,g_hideReturned=0;
static volatile DWORD g_restoreTick=0,g_restoreVisible=0,g_restoreForeground=0,g_attempts=0;

static BOOL RestoreNow() {
    if(!Identity(g_target,g_pid,g_tid))return FALSE;
    // SW_SHOW's return value describes previous visibility, not success.
    ShowWindow(g_target,SW_SHOW);
    g_restoreVisible=IsWindowVisible(g_target);
    g_restoreForeground=SetForegroundWindow(g_target);
    return g_restoreVisible;
}
static DWORD WINAPI RestoreThread(LPVOID) {
    // Arm before the main thread is allowed to issue SW_HIDE. No file IO in
    // this time-critical recovery path: USB stalls cannot delay the timer.
    SetEvent(g_armed);
    WaitForSingleObject(g_cancel,g_options.durationMs);
    g_restoreTick=GetTickCount()-g_begin;
    SetEvent(g_restoring);
    for(DWORD i=0;i<10;++i){++g_attempts;BOOL ok=RestoreNow();
        if(ok && WaitForSingleObject(g_hideReturned,0)==WAIT_OBJECT_0)break;
        Sleep(250);
    }
    SetEvent(g_done);return 0;
}
extern "C" void WindowRestoreEntry() {
    g_begin=GetTickCount();DWORD args[6];
    if(!WindowOpsParseArguments(GetCommandLineW(),args))ExitProcess(20);
    g_target=(HWND)args[0];g_pid=args[1];g_tid=args[2];DWORD parent=args[3],run=args[4];
    WCHAR folder[MAX_PATH],path[MAX_PATH];if(!Folder(folder))ExitProcess(21);
    wsprintfW(path,L"%swindow_restore_%04lu.txt",folder,run);
    if(!OpenLog(path))ExitProcess(22);
    Note(L"START WINDOW_RESTORE");Data(L"TARGET_HWND_PID_TID",args[0],g_pid,g_tid);
    if(!ReadOptions(folder,&g_options) || g_options.durationMs!=args[5]){Note(L"ABORT_CONFIG_MISMATCH_NO_HIDE");ExitProcess(30);}
    g_hidden=EventHandle(parent,run,L"HIDDEN",TRUE);g_restoring=EventHandle(parent,run,L"RESTORING",TRUE);
    g_done=EventHandle(parent,run,L"DONE",TRUE);g_cancel=EventHandle(parent,run,L"CANCEL",TRUE);
    if(!g_hidden||!g_restoring||!g_done||!g_cancel){Note(L"ABORT_EVENTS_NOT_READY_NO_HIDE");ExitProcess(23);}
    if(WindowOpsFindTarget(&g_options)!=g_target || !Identity(g_target,g_pid,g_tid) || !IsTargetProcess(g_pid,g_options.processName)){
        Note(L"ABORT_TARGET_IDENTITY_NO_HIDE");SetEvent(g_done);ExitProcess(24);
    }
    if(!IsWindowVisible(g_target) || GetForegroundWindow()!=g_target){
        Note(L"ABORT_TARGET_NOT_VISIBLE_FOREGROUND_NO_HIDE");SetEvent(g_done);ExitProcess(25);
    }
    g_armed=CreateEventW(0,TRUE,FALSE,0);g_hideReturned=CreateEventW(0,TRUE,FALSE,0);
    if(!g_armed||!g_hideReturned){Note(L"ABORT_RECOVERY_EVENTS_NO_HIDE");SetEvent(g_done);ExitProcess(26);}
    Note(L"PRECHECK_PASS; recovery thread will start before SW_HIDE");
    FlushFileBuffers(g_log);
    if(!g_logOK || WaitForSingleObject(g_cancel,0)==WAIT_OBJECT_0){SetEvent(g_done);ExitProcess(27);}
    HANDLE thread=CreateThread(0,0,RestoreThread,0,0,0);
    if(!thread){Note(L"ABORT_RECOVERY_THREAD_NO_HIDE");SetEvent(g_done);ExitProcess(28);}
    DWORD armed=WaitForSingleObject(g_armed,1000);
    DWORD hideBegin=GetTickCount()-g_begin,hideEnd=0,priorVisible=0,hidden=0;
    if(Identity(g_target,g_pid,g_tid) && MatchesWindow(g_target,&g_options) && IsWindowVisible(g_target) && GetForegroundWindow()==g_target && armed==WAIT_OBJECT_0 && WaitForSingleObject(g_restoring,0)!=WAIT_OBJECT_0 && WaitForSingleObject(g_cancel,0)!=WAIT_OBJECT_0){
        priorVisible=ShowWindow(g_target,SW_HIDE);
        hideEnd=GetTickCount()-g_begin;hidden=!IsWindowVisible(g_target);
        SetEvent(g_hideReturned);
        // If SW_HIDE returned late, repeat restoration instead of announcing a
        // fresh hide after the recovery thread has already run.
        if(WaitForSingleObject(g_restoring,0)==WAIT_OBJECT_0 || WaitForSingleObject(g_cancel,0)==WAIT_OBJECT_0)RestoreNow();
        else if(hidden)SetEvent(g_hidden);
        else SetEvent(g_cancel);
    }else{SetEvent(g_hideReturned);SetEvent(g_cancel);}
    DWORD waited=WaitForSingleObject(thread,g_options.durationMs+5000);
    if(waited!=WAIT_OBJECT_0){SetEvent(g_restoring);RestoreNow();}
    Data(L"HIDE_BEGIN_END_MS",hideBegin,hideEnd);Data(L"SHOW_HIDE_PREVIOUS_VISIBLE_AND_HIDDEN",priorVisible,hidden);
    Data(L"RESTORE_TICK_ATTEMPTS",g_restoreTick,g_attempts);
    Data(L"RESTORE_VISIBLE_FOREGROUND",g_restoreVisible,g_restoreForeground);
    Data(L"RESTORE_THREAD_WAIT",waited);Note(L"END_RESTORE_REQUESTED");FlushFileBuffers(g_log);
    SetEvent(g_done);CloseHandle(thread);CloseHandle(g_log);ExitProcess(g_restoreVisible?0:29);
}
