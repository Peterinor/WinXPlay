#include "policy.h"
#include "log.h"
#include "display.h"
static HINSTANCE module;
static LONG started=0,pendingBand=0,liveToken=0,liveBeat=0,transition=0,activationToken=0;
static LONG physicalDown=0,heldKeys=0,deliveryFault=0,wantFactory=1;
static HWND control=0,factory=0,target=0;
static HANDLE child=0;
static DWORD childPid=0,requestedAt=0,generation=0,phase=0;
static BOOL faultReported=FALSE,faultCancelled=FALSE; // bridge worker only
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
static void Fault(const WCHAR* reason) {
    InterlockedExchange(&deliveryFault,1);
    if(!faultReported){MrnLog(L"INPUT_SUSPENDED",generation);MrnLog(reason);faultReported=TRUE;}
    // Cancel once per failure, not every healthy timer tick. Retry only if the
    // cancel itself could not be posted; do not resume before it is queued.
    if(!faultCancelled && MrnWindowPid(target,childPid))
        faultCancelled=PostMessageW(target,WM_CANCELMODE,0,0);
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
        if(msg==MRN_HIDE_A && wp==DWORD(Read(&activationToken)) && !Read(&wantFactory)){LayerRequest(h,TRUE);ShowWindow(h,SW_HIDE);}
        if(msg==MRN_SHOW_A && Read(&wantFactory)){LayerRequest(h,FALSE);ShowWindow(h,SW_SHOW);SetWindowPos(h,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);SetForegroundWindow(h);InvalidateRect(h,0,TRUE);}
        return TRUE;
    }
    UINT touch=MrnTouchMessage(msg);
    UINT rawKey=msg==0x8000?MrnRawVirtualKey(wp):0;
    BOOL step=msg==0x8000 && MrnTrackStep(wp);
    BOOL key=(msg==WM_KEYDOWN || msg==WM_KEYUP) && MrnKeyAllowed(wp);
    if(!touch && !key && !rawKey && !step && msg!=0x8000)return FALSE;
    DWORD pid=0;WCHAR title[40];title[0]=0;
    if(!GetWindowThreadProcessId(h,&pid) || pid!=GetCurrentProcessId() ||
       !GetWindowTextW(h,title,40) || !MrnEqual(title,MRN_A_TARGET))return FALSE;
#if WINXPLAY_ENABLE_LOGS
    // Trace unmapped raw events too. Never perform file I/O on the OEM UI thread.
    if(msg==0x8000 && control)PostMessageW(control,MRN_RAW_KEY_TRACE,wp,lp);
#endif
    if(!touch && !key && !rawKey && !step)return FALSE;
    // Read the shared timestamp before sampling now: the worker may update it
    // between calls, which must not look like an unsigned timeout wraparound.
    DWORD beat=DWORD(Read(&liveBeat));
    BOOL live=Read(&liveToken)!=0 && !Read(&deliveryFault) &&
        DWORD(GetTickCount()-beat)<MRN_UI_TIMEOUT;
    if(!live && Read(&liveToken))InterlockedExchange(&deliveryFault,1);
    BOOL quarantine=Read(&transition) || !Read(&wantFactory);
    if(step){
        if(live){
            int delta=(short)LOWORD(lp),count=delta<0?-delta:delta;
            UINT vk=MrnStepVirtualKey(lp);
            // Bound a malformed/accumulated packet's work on the OEM UI thread.
            if(count>16)count=16;
            for(int i=0;i<count;++i){
                if(!QueueInput(MRN_KEY_DOWN,vk,0) || !QueueInput(MRN_KEY_UP,vk,0))break;
            }
        }
        return live||quarantine;
    }
    if(rawKey){
        // action 0 is the original short activation path (also used by BAND).
        // Do not guess long/repeat/release timing: emit one complete HID click.
        if(live && lp==0){
            if(QueueInput(MRN_KEY_DOWN,rawKey,0))QueueInput(MRN_KEY_UP,rawKey,0);
        }
        return live||quarantine;
    }
#if WINXPLAY_ENABLE_LOGS
    // Keep disk I/O off the OEM input thread; trace down/up only, not moves.
    if(control && (msg==WM_LBUTTONDOWN || msg==WM_LBUTTONUP))
        PostMessageW(control,MRN_TOUCH_TRACE,MAKELONG(msg,(live?1:0)|(quarantine?2:0)),lp);
#endif
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
    // Input recovery changes liveToken to reject old queued gestures, but must
    // not invalidate this activation's already queued HMI layer handoff.
    InterlockedExchange(&activationToken,generation);
    faultReported=FALSE;faultCancelled=FALSE;
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
    if(msg==MRN_TOUCH_TRACE){MrnLog(L"TOUCH_SOURCE msg flags xy",LOWORD(wp),HIWORD(wp),DWORD(lp));return 0;}
    if(msg==MRN_RAW_KEY_TRACE){
        if(wp>=0x32 && wp<=0x37)
            MrnLog(L"RAW_STEP id delta virtual",DWORD(wp),DWORD((short)LOWORD(lp)),MrnTrackStep(wp)?MrnStepVirtualKey(lp):0,TRUE);
        else MrnLog(L"RAW_KEY id action virtual",DWORD(wp),DWORD(lp),MrnRawVirtualKey(wp));
        return 0;
    }
    if(msg>=MRN_DOWN && msg<=MRN_KEY_UP) {
        // Internal generation rejects queued events from a prior BAND session.
        if(phase!=2 || HIWORD(wp)!=DWORD(Read(&liveToken)) || Read(&deliveryFault) ||
           !MrnWindowPid(target,childPid) || !IsWindowVisible(target))return 0;
        UINT native;WPARAM keys=LOWORD(wp);
        if(msg<=MRN_UP) {
            POINT pt={(short)LOWORD(lp),(short)HIWORD(lp)};
            if(!ScreenToClient(target,&pt)){Fault(L"INPUT_COORDINATE_FAILED");return 0;}
            native=msg==MRN_DOWN?WM_LBUTTONDOWN:msg==MRN_MOVE?WM_MOUSEMOVE:WM_LBUTTONUP;
            lp=MAKELPARAM(pt.x,pt.y);
        }else native=msg==MRN_KEY_DOWN?WM_KEYDOWN:WM_KEYUP;
        BOOL posted=PostMessageW(target,native,keys,lp);
        if(msg==MRN_DOWN || msg==MRN_UP)MrnLog(L"TOUCH_FORWARD msg ok xy",native,posted,DWORD(lp));
        if(!posted)Fault(L"INPUT_POST_FAILED");
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
                else if(!Responsive(target)) {
                    // A single 100 ms probe miss can be ordinary UI scheduling.
                    // Keep forwarding until the existing heartbeat deadline;
                    // never lengthen the blocking probe on the input worker.
                    if(DWORD(GetTickCount()-DWORD(Read(&liveBeat)))>=MRN_UI_TIMEOUT)
                        Fault(L"INPUT_RESPONSE_TIMEOUT");
                }
                else {
                    InterlockedExchange(&liveBeat,GetTickCount());
                    if(Read(&deliveryFault)) {
                        Fault(L"INPUT_HEARTBEAT_OR_QUEUE_FAILED");
                        // Discard the failed gesture completely, including held
                        // keys; only a new gesture may reach the recovered UI.
                        if(faultCancelled && !Read(&physicalDown) && !Read(&heldKeys)) {
                            generation=(generation+1)&65535;if(!generation)generation=1;
                            InterlockedExchange(&liveToken,generation);
                            InterlockedExchange(&deliveryFault,0);
                            faultReported=FALSE;faultCancelled=FALSE;
                            MrnLog(L"INPUT_RECOVERED",generation);
                        }
                    }
                }
            }else MrnLog(L"B_PROCESS_WAIT_FAILED",childPid,child?GetLastError():ERROR_INVALID_HANDLE);
        }
        MrnFlushLog();
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
    MrnFlushLog(TRUE);
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
