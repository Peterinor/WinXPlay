#include "../../core/receiver.h"
#include "../common/receiver_context.h"
#include "../common/diagnostics.h"
#include "../common/window_input.h"
#include <memory>
#include "../common/phone_ui.h"
#include "../common/legacy_media.h"
#include "media_backend.h"
#include "../common/connection_ui.h"
#include "../../devices/profile.h"
#ifdef UNDER_CE
#include "../../devices/wince.h"
#include "portable.h"
#endif
#include <sstream>
#include <algorithm>
using namespace cp;
namespace {
HWND window = 0;
WindowInput input;
#ifdef CARPLAY_HAS_MF
// Desktop CE-host validation uses the Windows GPU backend; real CE is unchanged.
HWND videoWindow = 0;
LRESULT CALLBACK gpuProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if(msg==WM_NCHITTEST)return HTTRANSPARENT;
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT){PAINTSTRUCT p;BeginPaint(h,&p);EndPaint(h,&p);return 0;}
    return DefWindowProcW(h,msg,wp,lp);
}
#endif
HANDLE cancel = 0, worker = 0;
CRITICAL_SECTION mutex;
Bytes frame;
FramePresenter *presenter = 0;
bool pending = false, closing = false;
bool foregroundAllowed=true;
unsigned connectionAttempts = 0;
wxui::State connectionUi;
wxui::ConnectionFont connectionFont;
unsigned waitingTick=0;
std::wstring status = L"\u7b49\u5f85 iPhone";
Receiver *receiver = 0;
void automaticState(bool retryPending) {
    Value v=Value::dictionary();
    v.dict["automatic"]=Value::flag(!closing);
    v.dict["foreground_allowed"]=Value::flag(foregroundAllowed);
    v.dict["attempts"]=Value::num(connectionAttempts);
    v.dict["worker_active"]=Value::flag(worker!=0);
    v.dict["retry_pending"]=Value::flag(retryPending && !closing);
    v.dict["retry_delay_ms"]=Value::num(3000);
    v.dict["status"]=Value::str(utf8(status));
    try{writeDiagnosticFile(executableDirectory()+L"/auto-connect.plist",xmlPlist(v));}catch(...){}
}
void connectionStage(const char *stage) {
    std::string s(stage);
    unsigned step=(s=="pairing_iphone" || s=="restoring_phone_trust")?1:
        (s=="carplay_handshake" || s=="receiver_network_address")?2:0;
    PostMessageW(window,WM_APP+4,step,0);
    diagnosticEvent("CONNECTION_STAGE",stage);
    Value v=Value::dictionary();v.dict["stage"]=Value::str(stage);
    v.dict["uptime_ms"]=Value::num(GetTickCount());
    v.dict["video_only"]=Value::flag(!applicationMediaCapabilities().audioOutput);
    try{writeDiagnosticFile(executableDirectory()+L"/connection-stage.plist",xmlPlist(v));}catch(...){}
}
void onStatus(void *, const std::string &s) {
    std::wstring *copy = new std::wstring(wide(s));
    if (!PostMessageW(window, WM_APP + 1, 0, (LPARAM)copy))
        delete copy;
}
void onFrame(void *, const Bytes &b) {
    EnterCriticalSection(&mutex);
    frame = b;
    bool notify = !pending;
    pending = true;
    LeaveCriticalSection(&mutex);
    if (notify)
        PostMessageW(window, WM_APP + 2, 0, 0);
}
DWORD listenImpl(void *) {
    // If the phone closes a session while NCM is unavailable, do not repeatedly
    // reopen CarKit (and its permission dialog) until the network can be used.
    static bool waitingForNetwork=false;
    static unsigned trustedGeneration=0;
    DWORD began=GetTickCount();
    std::ostringstream attempt;attempt<<"attempt="<<connectionAttempts;
    diagnosticEvent("ATTEMPT_BEGIN",attempt.str());
    try {
        WSADATA data;
        require(WSAStartup(MAKEWORD(2, 2), &data) == 0, "Winsock initialization failed");
        try {
#ifdef UNDER_CE
            prepareWinceConnection(onStatus);
#endif
            connectionStage("preparing_usb");
            Value usb = prepareUsbCarPlay();
            if (usb.at("mode_switch_requested").boolean) {
                WaitForSingleObject(cancel, 3000);
                require(WaitForSingleObject(cancel, 0) != WAIT_OBJECT_0, "Connection cancelled");
                // A queued transition is not a usable USBMUX stream. The next
                // automatic attempt must query the newly enumerated device.
                throw std::runtime_error("Preparing iPhone USB configuration; waiting for re-enumeration");
            }
            connectionStage("usbmux_handshake");
            UsbMux mux;
            connectionStage("enumerating_iphone");
            std::vector<AppleDevice> devices = mux.devices();
            require(!devices.empty(), "Connect and unlock iPhone");
            if(waitingForNetwork){
                if(trustedGeneration!=devices[0].id){
                    connectionStage("restoring_phone_trust");
                    restorePhoneSession(mux,devices[0]);
                    trustedGeneration=devices[0].id;
                }
                connectionStage("receiver_network_address");
                receiverAddresses();
                waitingForNetwork=false;
            }
#ifdef UNDER_CE
            connectionStage("pairing_iphone");
            onStatus(0,"Unlock iPhone and accept Trust if requested; preparing phone pairing");
            mux.pairRecord(devices[0]);
#endif
            // Restore the trusted Lockdown/TLS session even while NCM is coming
            // up. The address is required only when announcing AirPlay endpoints
            // in probeCarKit, after accessory identification and availability.
#ifdef CARPLAY_HAS_MF
            ReceiverContext dependencies(onFrame, 0, videoWindow);
#else
            ReceiverContext dependencies(onFrame, 0, window);
#endif
            Receiver session(dependencies.services(), onStatus, 0, 7000, true,
                             deviceProfile().width, deviceProfile().height);
            session.setControlCallback(postPhoneCommand,window);
            session.start();
            connectionStage("carplay_handshake");
            EnterCriticalSection(&mutex);
            receiver = &session;
            LeaveCriticalSection(&mutex);
            Value result;
            try {
                result = probeCarKit(mux, devices[0], &session, cancel, onStatus, 0);
                waitingForNetwork=result.dict.count("waiting_for_network") &&
                    result.at("waiting_for_network").boolean;
                if(result.dict.count("lockdown_tls") && result.at("lockdown_tls").boolean)
                    trustedGeneration=devices[0].id;
            } catch (...) {
                EnterCriticalSection(&mutex);
                receiver = 0;
                LeaveCriticalSection(&mutex);
                throw;
            }
            EnterCriticalSection(&mutex);
            receiver = 0;
            LeaveCriticalSection(&mutex);
            session.stop();
            writeDiagnosticFile(executableDirectory() + L"/session-usb.plist", xmlPlist(result));
            if (result.dict.count("error")) {
                diagnosticEvent("SESSION_FAILED",result.at("failed_stage").string+" "+result.at("error").string);
                onStatus(0, result.at("error").string);
            }
        } catch (...) {
            WSACleanup();
            throw;
        }
        WSACleanup();
    } catch (const std::exception &e) {
        diagnosticEvent("ATTEMPT_FAILED",e.what());
        EnterCriticalSection(&mutex);
        receiver = 0;
        LeaveCriticalSection(&mutex);
        std::string failure=e.what();
        if(failure=="Lockdown pairing: PasswordProtected")
            onStatus(0,"Unlock iPhone and keep it awake; pairing will retry automatically");
        else if(failure=="Lockdown pairing: PairingDialogResponsePending")
            onStatus(0,"Accept Trust on iPhone; pairing will retry automatically");
        else onStatus(0, failure);
        // Preserve the failed stage; an unplug to collect logs must not erase its history.
        try {
            Value error = Value::dictionary();
            error.dict["error"] = Value::str(e.what());
            writeDiagnosticFile(executableDirectory() + L"/last-error.plist", xmlPlist(error));
        } catch (...) {
        }
    }
    std::ostringstream ended;ended<<attempt.str()<<" elapsed_ms="<<DWORD(GetTickCount()-began);
    diagnosticEvent("ATTEMPT_END",ended.str());
    PostMessageW(window, WM_APP + 3, 0, 0);
    return 0;
}
DWORD WINAPI listen(void *p){
#ifdef UNDER_CE
    __try{return listenImpl(p);}
    __except(diagnosticException(GetExceptionInformation(),L"connection")){return 1;}
#else
    return listenImpl(p);
#endif
}
RECT picture() {
    RECT r;
    GetClientRect(window, &r);
    int w = r.right, h = w * deviceProfile().height / deviceProfile().width;
    if (h > r.bottom) {
        h = r.bottom;
        w = h * deviceProfile().width / deviceProfile().height;
    }
    RECT p = {(r.right - w) / 2, (r.bottom - h) / 2, (r.right + w) / 2, (r.bottom + h) / 2};
    return p;
}
void paint(HDC dc) {
    RECT r;
    GetClientRect(window, &r);
    Bytes b;
    EnterCriticalSection(&mutex);
    b = frame;
    LeaveCriticalSection(&mutex);
    if (b.size() == deviceProfile().width * deviceProfile().height * 3) {
        FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        DecodedFrame image;
        image.width = deviceProfile().width;
        image.height = deviceProfile().height;
        image.plane[0] = &b[0];
        image.stride[0] = deviceProfile().width * 3;
        RECT p = picture();
        presenter->paint(dc, p, image);
    } else {
        wxui::paint(dc,r,connectionUi,waitingTick,connectionFont,deviceProfile().ui);
    }
}
LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    RECT inputRect={0,0,0,0};
    if(window)inputRect=picture();
    EnterCriticalSection(&mutex);
#if WINXPLAY_ENABLE_LOGS
    bool inputReceiver=receiver!=0;
#endif
    bool consumed=input.handle(h,msg,wp,lp,foregroundAllowed?receiver:0,inputRect);
    LeaveCriticalSection(&mutex);
#if WINXPLAY_ENABLE_LOGS
    if(msg==WM_LBUTTONDOWN || msg==WM_LBUTTONUP){
        std::ostringstream detail;
        detail<<"msg="<<msg<<" x="<<short(LOWORD(lp))<<" y="<<short(HIWORD(lp))
              <<" foreground="<<foregroundAllowed<<" receiver="<<inputReceiver
              <<" rect="<<inputRect.left<<","<<inputRect.top<<","<<inputRect.right<<","<<inputRect.bottom
              <<" handled="<<consumed;
        diagnosticEvent("TOUCH_WINDOW",detail.str());
    }
#endif
    if(consumed)return msg==0x0319?TRUE:0;

    switch (msg) {
    case WM_SHOWWINDOW:
        foregroundAllowed=wp!=0;
        if(!foregroundAllowed) {
            EnterCriticalSection(&mutex);
            input.handle(h,WM_CANCELMODE,0,0,receiver,inputRect);
            LeaveCriticalSection(&mutex);
        }
        automaticState(!worker);
        return 0;
    case PhoneCommandMessage:
        applyPhoneCommand(h,lp);
        return 0;
    case WM_APP + 1: {
        std::wstring *s = (std::wstring *)lp;
        status = *s;
        delete s;
        connectionUi.update(status);
        if(connectionUi.streaming)KillTimer(h,2);
        automaticState(false);
        if(!connectionUi.streaming)InvalidateRect(h, 0, FALSE);
        return 0;
    }
    case WM_APP + 4:
        connectionUi.stage(unsigned(wp));
        if(!connectionUi.streaming)InvalidateRect(h,0,FALSE);
        return 0;
    case WM_APP + 2:
#ifdef CARPLAY_HAS_MF
        if(videoWindow)ShowWindow(videoWindow,SW_SHOWNOACTIVATE);
#endif
        EnterCriticalSection(&mutex);
        pending = false;
        LeaveCriticalSection(&mutex);
        InvalidateRect(h, 0, FALSE);
        return 0;
    case WM_APP + 3:
        connectionUi.streaming=false;
        if(!closing)SetTimer(h,2,500,0);
#ifdef CARPLAY_HAS_MF
        if(videoWindow){DestroyWindow(videoWindow);videoWindow=0;}
#endif
        if (worker) {
            CloseHandle(worker);
            worker = 0;
        }
        EnterCriticalSection(&mutex);
        frame.clear();
        LeaveCriticalSection(&mutex);
        if (closing)
            DestroyWindow(h);
        else
            SetTimer(h, 1, 3000, 0);
        automaticState(!closing);
        InvalidateRect(h,0,FALSE);
        return 0;
    case WM_TIMER:
        if(wp==2) {
            ++waitingTick;
            if(!connectionUi.streaming && foregroundAllowed && IsWindowVisible(h))InvalidateRect(h,0,FALSE);
            return 0;
        }
        if (wp == 1) {
            KillTimer(h, 1);
            if (!closing && !worker) {
#ifdef CARPLAY_HAS_MF
                RECT r;GetClientRect(h,&r);
                videoWindow=CreateWindowW(L"MRN_CEHOST_GPU",L"",WS_CHILD,0,0,r.right,r.bottom,h,0,
                                         GetModuleHandleW(0),0);
                if(!videoWindow){status=L"Cannot create GPU window";InvalidateRect(h,0,FALSE);return 0;}
#endif
                ResetEvent(cancel);
                ++connectionAttempts;
                worker = CreateThread(0, 0, listen, 0, 0, 0);
                if (!worker) {
                    status=L"Connection worker failed";connectionUi.update(status);
#ifdef CARPLAY_HAS_MF
                    DestroyWindow(videoWindow);videoWindow=0;
#endif
                    SetTimer(h, 1, 3000, 0);
                }
                automaticState(!worker);
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            PostMessageW(h, WM_CLOSE, 0, 0);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
#ifdef CARPLAY_CE_DSHOW
        // DirectShow owns the live surface. Do not blit waiting artwork over it.
        if(connectionUi.streaming){EndPaint(h,&ps);return 0;}
#endif
        RECT r;
        GetClientRect(h, &r);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, r.right, r.bottom);
        if (mem && bitmap) {
            HGDIOBJ old = SelectObject(mem, bitmap);
            paint(mem);
            BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
        } else paint(dc);
        if (bitmap)
            DeleteObject(bitmap);
        if (mem)
            DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
#ifdef CARPLAY_HAS_MF
    case WM_SIZE:
        if(videoWindow)MoveWindow(videoWindow,0,0,LOWORD(lp),HIWORD(lp),FALSE);
        return 0;
#endif
    case WM_CLOSE:
        diagnosticEvent("WINDOW_CLOSE_REQUESTED");
        closing = true;
        connectionUi.phase=wxui::Stopping;
        KillTimer(h, 1);
        KillTimer(h, 2);
        SetEvent(cancel);
        automaticState(false);
        if (!worker)
            DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        diagnosticEvent("WINDOW_DESTROYED");
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
} // namespace
#ifdef UNDER_CE
int receiverMain(HINSTANCE instance, HINSTANCE, LPWSTR command, int show) {
    std::wstring arguments = command;
#else
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR command, int show) {
    std::wstring arguments = wide(command);
#endif
    if (arguments.find(L"--serve-test") == 0) {
        try {
            std::wistringstream args(arguments);
            std::wstring mode;
            unsigned port = 7100, seconds = 30, w = 800, h = 480;
            args >> mode >> port >> seconds >> w >> h;
            WSADATA data;
            require(WSAStartup(MAKEWORD(2, 2), &data) == 0, "Winsock initialization");
            ReceiverContext dependencies(0, 0);
            Receiver session(dependencies.services(), 0, 0, port, false, w, h);
            session.start();
            Value info = Value::dictionary();
            info.dict["port"] = Value::num(session.port());
            info.dict["publicKey"] = Value::str(session.publicKey());
            writeDiagnosticFile(executableDirectory() + L"/receiver-listener.plist", xmlPlist(info));
            Sleep(std::min(seconds, 300u) * 1000);
            session.stop();
            WSACleanup();
            return 0;
        } catch (const std::exception &e) {
            Value report = Value::dictionary();
            report.dict["error"] = Value::str(e.what());
            try {
                writeDiagnosticFile(executableDirectory() + L"/startup-error.plist", xmlPlist(report));
            } catch (...) {
            }
            return 1;
        }
    }
    InitializeCriticalSection(&mutex);
    cancel = CreateEventW(0, TRUE, FALSE, 0);
    HANDLE deploymentGuard=0;
#ifdef UNDER_CE
    if(!acquireDeviceDeploymentGuard(deploymentGuard)) {
        HWND existing=FindWindowW(L"WinXPlay",0);if(existing)SetForegroundWindow(existing);
        if(deploymentGuard)CloseHandle(deploymentGuard);
        return 0;
    }
#endif
    // Keep the deployed application's mutex name to exclude older instances too.
    HANDLE singleton = CreateMutexW(0, FALSE, L"MRN_CARPLAY_CE_SINGLETON");
    if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;
#ifdef UNDER_CE
    initializeCrashDiagnostics(executableDirectory().c_str());
    diagnosticEvent("RUN_BEGIN","diagnostics=1 build=" __DATE__ " " __TIME__);
#endif
    try {
#ifdef UNDER_CE
        presenter = createFramePresenter(environment(L"CARPLAY_PRESENTER", L"gdi"));
        writeDiagnosticFile(executableDirectory() + L"/media-backends.plist",
                  xmlPlist(mediaBackendConfiguration()));
#else
        presenter = createGdiPresenter();
#endif
    } catch (const std::exception &e) {
        Value report = Value::dictionary();
        report.dict["error"] = Value::str(e.what());
        try {
            writeDiagnosticFile(executableDirectory() + L"/startup-error.plist", xmlPlist(report));
        } catch (...) {
        }
        return 1;
    }
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.lpszClassName = L"WinXPlay";
    RegisterClassW(&wc);
#ifdef CARPLAY_HAS_MF
    WNDCLASSW gpu={};gpu.lpfnWndProc=gpuProc;gpu.hInstance=instance;gpu.lpszClassName=L"MRN_CEHOST_GPU";
    RegisterClassW(&gpu);
#endif
    window = CreateWindowW(wc.lpszClassName, L"WinXPlay CE", WS_POPUP | WS_CLIPCHILDREN, 0, 0,
                           GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), 0, 0,
                           instance, 0);
    if (!window)
        return 1;
    HDC fontDc=GetDC(window);
    connectionFont.initialize(fontDc,deviceProfile().ui);
    ReleaseDC(window,fontDc);
    if(foregroundAllowed)ShowWindow(window, show);
#ifdef UNDER_CE
    if(foregroundAllowed){
        SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
        SetForegroundWindow(window);
    }
#endif
    UpdateWindow(window);
    SetTimer(window, 1, 100, 0);
    SetTimer(window, 2, 500, 0);
    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    connectionFont.release();
    delete presenter;
    presenter = 0;
    CloseHandle(cancel);
    CloseHandle(singleton);
    if(deploymentGuard)CloseHandle(deploymentGuard);
    DeleteCriticalSection(&mutex);
    diagnosticEvent("RUN_END");
    return 0;
}
#ifdef UNDER_CE
#ifdef WINXPLAY_CONNECTION_TEST
int WINAPI ReceiverMain(HINSTANCE instance,HINSTANCE prior,LPWSTR command,int show){
#else
int WINAPI WinMain(HINSTANCE instance,HINSTANCE prior,LPWSTR command,int show){
#endif
    __try{return receiverMain(instance,prior,command,show);}
    __except(diagnosticException(GetExceptionInformation(),L"ui_main")){return 1;}
}
#endif
