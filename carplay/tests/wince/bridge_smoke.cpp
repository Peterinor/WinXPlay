#include "../../platform/common/native_services.h"
#include "../../../firmware/mrn/bridge/policy.h"
using namespace cp;
typedef BOOL (WINAPI *Hook)(HWND,UINT,WPARAM,LPARAM);
typedef void* (WINAPI *Start)();
static Hook hook=0;static Start band=0;static DWORD began=0,phase=0,pid=0;static HWND child=0;
static bool owned=false;static std::string failure;
static DWORD backgroundAttempt=0;
static void finish(HWND h,bool passed,const char* error) {
    Value r=Value::dictionary();r.dict["passed"]=Value::flag(passed);
    r.dict["error"]=Value::str(error);r.dict["physical_hmi_verified"]=Value::flag(false);
    r.dict["normal_receiver_ui_tested"]=Value::flag(true);r.dict["reused_pid"]=Value::num(pid);
    r.dict["test_version"]=Value::str("standard-window-v3");
    r.dict["self_hide_restored_factory"]=Value::flag(passed);
    writeFile(executableDirectory()+L"/bridge-smoke.plist",xmlPlist(r));
    if(owned && child && IsWindow(child))PostMessageW(child,WM_CLOSE,0,0);
    DestroyWindow(h);
}
static Value state(){return parsePlist(readFile(executableDirectory()+L"/auto-connect.plist"));}
static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    if(hook && hook(h,msg,wp,lp))return 0;
    if(msg==WM_TIMER) {
        Value progress=Value::dictionary();progress.dict["phase"]=Value::num(phase);
        progress.dict["elapsed_ms"]=Value::num(GetTickCount()-began);
        writeFile(executableDirectory()+L"/bridge-progress.plist",xmlPlist(progress));
        if(DWORD(GetTickCount()-began)>45000){finish(h,false,"Normal bridge lifecycle timed out");return 0;}
        HWND b=FindWindowW(MRN_B_TITLE,0);
        if(phase==5){if(b){child=b;owned=true;hook=(Hook)band();phase=0;}return 0;}
        if(phase==0 && b && IsWindowVisible(b) && !IsWindowVisible(h)) {
            child=b;GetWindowThreadProcessId(b,&pid);owned=true;
            PostMessageW(h,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(80,60));
            PostMessageW(h,WM_LBUTTONUP,0,MAKELPARAM(80,60));
            phase=1;return 0;
        }
        if(phase==1){try{backgroundAttempt=DWORD(state().at("attempts").integer);}catch(...){return 0;}band();phase=2;return 0;}
        if(phase==2 && b && !IsWindowVisible(b) && IsWindowVisible(h)) {
            try{Value v=state();
                if(!v.at("automatic").boolean){finish(h,false,"BAND disabled background reconnect");return 0;}
                if(v.at("foreground_allowed").boolean || v.at("attempts").integer<=backgroundAttempt)return 0;
            }catch(...){return 0;}
            band();phase=3;return 0;
        }
        if(phase==3 && b && IsWindowVisible(b) && !IsWindowVisible(h)) {
            DWORD nowPid=0;GetWindowThreadProcessId(b,&nowPid);
            if(nowPid!=pid){finish(h,false,"BAND created a second receiver");return 0;}
            try{if(!state().at("automatic").boolean)return 0;}catch(...){return 0;}
            ShowWindow(b,SW_HIDE);phase=6;return 0;
        }
        if(phase==6 && b && !IsWindowVisible(b) && IsWindowVisible(h)){band();phase=7;return 0;}
        if(phase==7 && b && IsWindowVisible(b) && !IsWindowVisible(h)){
            DWORD nowPid=0;GetWindowThreadProcessId(b,&nowPid);
            if(nowPid!=pid){finish(h,false,"Self-hide return replaced the receiver");return 0;}
            PostMessageW(b,WM_CLOSE,0,0);phase=4;return 0;
        }
        if(phase==4 && !b && IsWindowVisible(h)){owned=false;finish(h,true,"");return 0;}
        return 0;
    }
    if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,msg,wp,lp);
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPWSTR args,int) {
    if(FindWindowW(MRN_B_TITLE,0) || FindWindowW(MRN_A_TITLE,0))return 9;
    HMODULE module=LoadLibraryW((executableDirectory()+L"/WinXPlayBridge.dll").c_str());
    if(!module)return 10;
    band=(Start)GetProcAddress(module,L"MrnBridgeStart");if(!band)return 11;
    WNDCLASSW wc={0};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=L"WinXPlayFakeHmiTest";
    RegisterClassW(&wc);
    HWND a=CreateWindowW(wc.lpszClassName,MRN_A_TARGET,WS_POPUP|WS_VISIBLE,0,0,240,320,0,0,instance,0);
    if(!a)return 12;
    began=GetTickCount();SetTimer(a,1,200,0);
    if(args && std::wstring(args)==L"existing") {
        PROCESS_INFORMATION pi={0};
        if(!CreateProcessW((executableDirectory()+L"/WinXPlay.exe").c_str(),0,0,0,FALSE,0,0,0,0,&pi))return 13;
        CloseHandle(pi.hThread);CloseHandle(pi.hProcess);phase=5;
    } else hook=(Hook)band();
    MSG m;while(GetMessageW(&m,0,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}
    // Bridge's worker lives until this test process exits. Never unload underneath it.
    Value r=parsePlist(readFile(executableDirectory()+L"/bridge-smoke.plist"));return r.at("passed").boolean?0:1;
}
