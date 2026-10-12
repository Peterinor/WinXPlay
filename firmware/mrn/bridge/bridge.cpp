#include "policy.h"
#include "log.h"
#include "display.h"
static HINSTANCE module;
static LONG started=0,pendingBand=0,liveToken=0,liveBeat=0,transition=0;
static LONG physicalDown=0,heldKeys=0,deliveryFault=0,wantFactory=1;
static HWND control=0,factory=0,target=0;
static HANDLE child=0;
static DWORD childPid=0,requestedAt=0,generation=0,phase=0;
static DWORD contactOwner=0,keyOwner[256]={0}; // OEM UI thread: A / B / quarantine
static BOOL layerHide=FALSE,layerSubmitted=FALSE;
static DWORD layerStarted=0,layerLastResult=0xffffffff;
static void LayerStep(HWND h) {
    MrnLayerResult r;
    if(layerSubmitted)MrnLayerObserve(&r);else MrnLayerHandoff(h,layerHide,&r);
    if(r.code!=layerLastResult) {
        if(r.code==LAYER_UNSUPPORTED || r.code==LAYER_EXCEPTION){
            PostMessageW(control,MRN_LAYER_MODULE_REPORT,layerModuleBase,layerModuleSize);
            PostMessageW(control,MRN_LAYER_CHECK_REPORT,layerCheck,layerObserved);
        }
        PostMessageW(control,MRN_LAYER_REPORT,r.code|(layerHide?0x100:0),r.state);
        layerLastResult=r.code;
    }
    if(r.code==LAYER_SUBMITTED)layerSubmitted=TRUE;
    BOOL pending=r.code==LAYER_SUBMITTED || r.code==LAYER_STATE_PENDING || r.code==LAYER_LOCK_BUSY;
    if(!pending || DWORD(GetTickCount()-layerStarted)>=5000) {
        KillTimer(h,MRN_LAYER_TIMER);
        if(pending)PostMessageW(control,MRN_LAYER_REPORT,LAYER_STATE_PENDING|(layerHide?0x100:0),r.state);
    }
}
static void LayerRequest(HWND h,BOOL hide) {
    KillTimer(h,MRN_LAYER_TIMER);layerHide=hide;layerSubmitted=FALSE;
    layerStarted=GetTickCount();layerLastResult=0xffffffff;
    SetTimer(h,MRN_LAYER_TIMER,100,0);LayerStep(h);
}
static LONG Read(LONG* p){return InterlockedCompareExchange(p,0,0);}
static void Restore() {
    InterlockedExchange(&wantFactory,1);
    if(factory && MrnWindowPid(factory,GetCurrentProcessId()))
        PostMessageW(factory,MRN_SHOW_A,0,(LPARAM)control);
}
static void Clear(const WCHAR* reason) {
    // All target-window posts originate on this worker: cancel follows all
    // previously forwarded input, and precedes input of the next activation.
    InterlockedExchange(&liveToken,0);InterlockedExchange(&transition,0);
    if(MrnWindowPid(target,childPid)) {
        PostMessageW(target,WM_CANCELMODE,0,0);
        ShowWindow(target,SW_HIDE);
        // CE does not consistently deliver visibility notifications for a
        // cross-process ShowWindow. Queue the standard notification explicitly.
        PostMessageW(target,WM_SHOWWINDOW,FALSE,0);
        SetWindowPos(target,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    }
    Restore();MrnLog(reason,generation,phase);target=0;phase=0;
}
static void Fault() {
    InterlockedExchange(&deliveryFault,1);
    if(MrnWindowPid(target,childPid))PostMessageW(target,WM_CANCELMODE,0,0);
}
static BOOL Responsive(HWND h) {
    DWORD_PTR result=0;
    return MrnWindowPid(h,childPid) && SendMessageTimeout(h,WM_NULL,0,0,SMTO_NORMAL,100,&result)!=0;
}
static BOOL QueueInput(UINT msg,WPARAM value,LPARAM lp) {
    LONG token=Read(&liveToken);
    if(!token || Read(&deliveryFault) ||
       !PostMessageW(control,msg,MAKELONG(value,token),lp)) {
        InterlockedExchange(&deliveryFault,1);return FALSE;
    }
    return TRUE;
}
extern "C" __declspec(dllexport) BOOL WINAPI MrnOnMessage(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_TIMER && wp==MRN_LAYER_TIMER && h==factory){LayerStep(h);return TRUE;}
    // These private messages stay entirely inside the HMI process.
    if((msg==MRN_HIDE_A || msg==MRN_SHOW_A) && control && (HWND)lp==control && h==factory) {
        if(msg==MRN_HIDE_A && wp==DWORD(Read(&liveToken)) && !Read(&wantFactory)){LayerRequest(h,TRUE);ShowWindow(h,SW_HIDE);}
        if(msg==MRN_SHOW_A && Read(&wantFactory)){LayerRequest(h,FALSE);ShowWindow(h,SW_SHOW);SetWindowPos(h,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);SetForegroundWindow(h);InvalidateRect(h,0,TRUE);}
        return TRUE;
    }
    UINT touch=MrnTouchMessage(msg);
    BOOL key=(msg==WM_KEYDOWN || msg==WM_KEYUP) && MrnKeyAllowed(wp);
    if(!touch && !key)return FALSE;
    DWORD pid=0;WCHAR title[40];title[0]=0;
    if(!GetWindowThreadProcessId(h,&pid) || pid!=GetCurrentProcessId() ||
       !GetWindowTextW(h,title,40) || !MrnEqual(title,MRN_A_TARGET))return FALSE;
    BOOL live=Read(&liveToken)!=0 && !Read(&deliveryFault) &&
        DWORD(GetTickCount()-DWORD(Read(&liveBeat)))<MRN_UI_TIMEOUT;
    if(!live && Read(&liveToken))InterlockedExchange(&deliveryFault,1);
    BOOL quarantine=Read(&transition) || !Read(&wantFactory);
    if(touch) {
        POINT pt={(short)LOWORD(lp),(short)HIWORD(lp)};
        BOOL positioned=ClientToScreen(h,&pt);
        if(msg==WM_LBUTTONDOWN) {
            if(!Read(&physicalDown))contactOwner=live?2:quarantine?3:1;
            InterlockedExchange(&physicalDown,1);
        }
        DWORD owner=contactOwner;
        if(!Read(&physicalDown))return live || quarantine;
        if(owner==2 && live) {
            if(!positioned || !QueueInput(touch,wp,MAKELPARAM(pt.x,pt.y)))contactOwner=3;
        }else if(owner==2)contactOwner=3;
        if(msg==WM_LBUTTONUP){InterlockedExchange(&physicalDown,0);contactOwner=0;}
        return owner!=1;
    }
    DWORD index=DWORD(wp)&255;
    if(msg==WM_KEYDOWN && !keyOwner[index]) {
        keyOwner[index]=live?2:quarantine?3:1;InterlockedIncrement(&heldKeys);
    }
    DWORD owner=keyOwner[index];
    if(owner==2 && live) {
        if(!QueueInput(msg==WM_KEYDOWN?MRN_KEY_DOWN:MRN_KEY_UP,wp,lp))keyOwner[index]=3;
    }else if(owner==2)keyOwner[index]=3;
    if(msg==WM_KEYUP && owner){keyOwner[index]=0;InterlockedDecrement(&heldKeys);}
    return owner?owner!=1:live || quarantine;
}
static void TryActive() {
    if(phase!=1 || Read(&physicalDown) || Read(&heldKeys))return;
    HWND b=FindWindowW(MRN_B_TITLE,0);
    if(!Responsive(b))return;
    target=b;
    ShowWindow(b,SW_SHOW);
    SetWindowPos(b,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
    SetForegroundWindow(b);
    if(!IsWindowVisible(b))return;
    PostMessageW(b,WM_SHOWWINDOW,TRUE,0);
    phase=2;InterlockedExchange(&liveBeat,GetTickCount());
    InterlockedExchange(&deliveryFault,0);InterlockedExchange(&liveToken,generation);
    InterlockedExchange(&transition,0);InterlockedExchange(&wantFactory,0);
    if(!PostMessageW(factory,MRN_HIDE_A,generation,(LPARAM)control)){Clear(L"HIDE_POST_FAILED");return;}
    InvalidateRect(b,0,TRUE);MrnLog(L"ACTIVE_STANDARD",generation,childPid);
}
static void Band() {
    if(phase){Clear(L"BAND_RETURN");return;}
    factory=FindWindowW(0,MRN_A_TARGET);
    if(!MrnWindowPid(factory,GetCurrentProcessId())){MrnLog(L"FACTORY_WINDOW_MISSING");return;}
    if(child && WaitForSingleObject(child,0)!=WAIT_TIMEOUT){CloseHandle(child);child=0;childPid=0;}
    if(!child) {
        HWND b=FindWindowW(MRN_B_TITLE,0);DWORD pid=0;
        if(b && GetWindowThreadProcessId(b,&pid)) { child=OpenProcess(SYNCHRONIZE,FALSE,pid);if(child)childPid=pid; }
    }
    if(!child) {
        PROCESS_INFORMATION pi={0};
        if(!CreateProcessW(MRN_INSTALL L"WinXPlay.exe",0,0,0,FALSE,0,0,0,0,&pi)){MrnLog(L"B_START_FAILED",GetLastError());return;}
        child=pi.hProcess;childPid=pi.dwProcessId;CloseHandle(pi.hThread);
    }
    generation=(generation+1)&65535;if(!generation)generation=1;
    requestedAt=GetTickCount();phase=1;InterlockedExchange(&transition,1);
    MrnLog(L"ENTER_STANDARD",childPid,generation);TryActive();
}
extern "C" __declspec(dllexport) LRESULT CALLBACK MrnControlMessage(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==MRN_LAYER_MODULE_REPORT){MrnLog(L"LAYER_MODULE base size",DWORD(wp),DWORD(lp));return 0;}
    if(msg==MRN_LAYER_CHECK_REPORT){MrnLog(L"LAYER_CHECK location actual",DWORD(wp),DWORD(lp));return 0;}
    if(msg==MRN_LAYER_REPORT){
        BOOL hide=(wp&0x100)!=0;MrnLog(L"LAYER_HANDOFF",hide,wp&255,DWORD(lp));
        if((wp&255)==LAYER_STATE_PROCESSED) {
            HWND redraw=hide?target:factory;
            if(hide==!Read(&wantFactory) && MrnWindowPid(redraw,hide?childPid:GetCurrentProcessId()))InvalidateRect(redraw,0,TRUE);
        }
        return 0;
    }
    if(msg>=MRN_DOWN && msg<=MRN_KEY_UP) {
        // Internal generation rejects queued events from a prior BAND session.
        if(phase!=2 || HIWORD(wp)!=DWORD(Read(&liveToken)) || Read(&deliveryFault) ||
           !MrnWindowPid(target,childPid) || !IsWindowVisible(target))return 0;
        UINT native;WPARAM keys=LOWORD(wp);
        if(msg<=MRN_UP) {
            POINT pt={(short)LOWORD(lp),(short)HIWORD(lp)};
            if(!ScreenToClient(target,&pt)){Fault();return 0;}
            native=msg==MRN_DOWN?WM_LBUTTONDOWN:msg==MRN_MOVE?WM_MOUSEMOVE:WM_LBUTTONUP;
            lp=MAKELPARAM(pt.x,pt.y);
        }else native=msg==MRN_KEY_DOWN?WM_KEYDOWN:WM_KEYUP;
        if(!PostMessageW(target,native,keys,lp))Fault();
        return 0;
    }
    if(msg==WM_TIMER && wp==MRN_TIMER) {
        if(InterlockedExchange(&pendingBand,0))Band();
        if(phase) {
            DWORD wait=child?WaitForSingleObject(child,0):WAIT_FAILED;
            if(wait==WAIT_OBJECT_0){Clear(L"B_EXITED");}
            else if(wait==WAIT_TIMEOUT) {
                if(phase==1) {
                    TryActive();
                    if(phase==1 && DWORD(GetTickCount()-requestedAt)>=MRN_START_TIMEOUT)Clear(L"B_WINDOW_NOT_READY");
                }else if(!MrnWindowPid(target,childPid) || !IsWindowVisible(target))Clear(L"B_WINDOW_HIDDEN_OR_CLOSED");
                else if(!Responsive(target))Fault();
                else {
                    InterlockedExchange(&liveBeat,GetTickCount());
                    if(Read(&deliveryFault))PostMessageW(target,WM_CANCELMODE,0,0);
                }
            }else MrnLog(L"B_PROCESS_WAIT_FAILED",childPid,child?GetLastError():ERROR_INVALID_HANDLE);
        }
        if(mrnLog!=INVALID_HANDLE_VALUE)FlushFileBuffers(mrnLog);
        return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
static DWORD WINAPI Run(LPVOID) {
    MrnOpenLog(L"A_BRIDGE");MrnLog(L"START_STANDARD_WINDOWS_V3",GetCurrentProcessId());
    generation=(GetTickCount() ^ GetCurrentProcessId())&65535;
    WNDCLASSW wc={0};wc.hInstance=module;wc.lpfnWndProc=MrnControlMessage;wc.lpszClassName=MRN_A_TITLE;
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)goto done;
    control=CreateWindowW(MRN_A_TITLE,MRN_A_TITLE,0,0,0,0,0,0,0,module,0);
    if(!control)goto done;
    if(!SetTimer(control,MRN_TIMER,250,0)){DestroyWindow(control);control=0;goto done;}
    {MSG m;while(GetMessageW(&m,0,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}}
done:
    Clear(L"BRIDGE_STOPPED");
    if(mrnLog!=INVALID_HANDLE_VALUE){CloseHandle(mrnLog);mrnLog=INVALID_HANDLE_VALUE;}
    InterlockedExchange(&started,0);return 0;
}
extern "C" __declspec(dllexport) void* WINAPI MrnBridgeStart() {
    if(InterlockedCompareExchange(&started,1,0)==0) {
        HANDLE thread=CreateThread(0,0,Run,0,0,0);
        if(!thread){InterlockedExchange(&started,0);return 0;}CloseHandle(thread);
    }
    InterlockedExchange(&pendingBand,1);return (void*)&MrnOnMessage;
}
extern "C" BOOL WINAPI DllMain(HANDLE h,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){module=(HINSTANCE)h;DisableThreadLibraryCalls((HMODULE)h);}return TRUE;
}
