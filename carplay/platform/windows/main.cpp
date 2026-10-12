#include "../common/native_services.h"
#include "../common/native_cipher.h"
#include "../../core/receiver.h"
#include "../common/receiver_context.h"
#include "../common/window_input.h"
#include <memory>
#include "../common/phone_ui.h"
#include "../common/connection_ui.h"
#include <shellapi.h>
#include <sstream>
#include <algorithm>
using namespace cp;
namespace {
const UINT StatusMessage = WM_APP + 1, FrameMessage = WM_APP + 2, FinishedMessage = WM_APP + 3;
const UINT DiagnosticMessage = WM_APP + 4, ConnectionStageMessage = WM_APP + 5;
const UINT WaitingAnimationTimer = 5;
HWND window = 0;
WindowInput input;
HWND videoWindow = 0;
wxui::ConnectionFont connectionFont;
wxui::State connectionUi;
unsigned waitingTick=0;
HANDLE job = 0, cancelEvent = 0;
CRITICAL_SECTION lock;
Bytes latestFrame;
std::wstring status = L"等待 iPhone…";
std::wstring detailText;
bool closing = false, smoke = false, framePending = false;
bool gpuFrameVisible = false;
Receiver *liveReceiver = 0;
bool autoConnect = true;
unsigned pendingDiagnostic = 0;
bool diagnosticRunning = false;
unsigned connectionAttempts = 0;
const unsigned displayWidths[] = {800, 1280, 1920};
const unsigned displayHeights[] = {480, 720, 1080};
unsigned selectedDisplay = 1, activeWidth = 1280, activeHeight = 720;
bool fullscreen = false, reconnectPending = false;

WINDOWPLACEMENT savedPlacement = {sizeof(WINDOWPLACEMENT)};
LONG_PTR savedStyle = 0;
void loadDisplaySettings() {
    try {
        Value settings = parsePlist(readFile(executableDirectory() + L"/display-settings.plist"));
        for (unsigned i = 0; i < 3; ++i)
            if (settings.at("width").integer == displayWidths[i] &&
                settings.at("height").integer == displayHeights[i])
                selectedDisplay = i;
    } catch (const std::exception &) {
    }
    activeWidth = displayWidths[selectedDisplay];
    activeHeight = displayHeights[selectedDisplay];

}
void toggleFullscreen() {
    if (!fullscreen) {
        GetWindowPlacement(window, &savedPlacement);
        savedStyle = GetWindowLongPtrW(window, GWL_STYLE);
        MONITORINFO monitor = {sizeof(MONITORINFO)};
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
            return;
        fullscreen = true;
        SetWindowLongPtrW(window, GWL_STYLE, savedStyle & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                     monitor.rcMonitor.right - monitor.rcMonitor.left,
                     monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_FRAMECHANGED);
    } else {
        fullscreen = false;
        SetWindowLongPtrW(window, GWL_STYLE, savedStyle);
        SetWindowPlacement(window, &savedPlacement);
        SetWindowPos(window, 0, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    SetFocus(window);
    InvalidateRect(window, 0, FALSE);
}
void updateStatus(const std::wstring &s) {
    std::wstring *copy = new std::wstring(s);
    if (!PostMessageW(window, StatusMessage, 0, (LPARAM)copy))
        delete copy;
}
void onFrame(void *, const Bytes &b) {
    EnterCriticalSection(&lock);
    bool pending = framePending;
    latestFrame = b;
    gpuFrameVisible = b.empty();
    framePending = true;
    LeaveCriticalSection(&lock);
    if (!pending)
        PostMessageW(window, FrameMessage, 0, 0);
}
void liveStatus(void *, const std::string &s) {
    updateStatus(wide(s));
}
DWORD WINAPI stream(void *) {
    try {
        PostMessageW(window,ConnectionStageMessage,0,0);
        UsbMux mux;
        Value usb = prepareUsbCarPlay();
        if (usb.at("mode_switch_requested").boolean)
            WaitForSingleObject(cancelEvent, 3000);
        receiverAddresses();
        ReceiverContext dependencies(onFrame, 0, videoWindow);
        Receiver receiver(dependencies.services(), liveStatus, 0, 7000, true, activeWidth,
                          activeHeight);
        EnterCriticalSection(&lock);
        liveReceiver = &receiver;
        latestFrame.clear();
        LeaveCriticalSection(&lock);
        try {
            receiver.setControlCallback(postPhoneCommand,window);
        receiver.start();
            std::vector<AppleDevice> devices = mux.devices();
            require(!devices.empty(), "Connect and unlock an iPhone first");
            PostMessageW(window,ConnectionStageMessage,1,0);
            Value result = probeCarKit(mux, devices[0], &receiver, cancelEvent,liveStatus,0);
            writeDiagnosticFile(executableDirectory() + L"/session-usb.plist", xmlPlist(result));
            if (result.dict.count("error"))
                updateStatus(wide(result.at("error").string));
        } catch (...) {
            EnterCriticalSection(&lock);
            liveReceiver = 0;
            LeaveCriticalSection(&lock);
            throw;
        }
        receiver.stop();
        EnterCriticalSection(&lock);
        liveReceiver = 0;
        LeaveCriticalSection(&lock);
    } catch (const std::exception &e) {
        updateStatus(L"Connection failed: " + wide(e.what()));
    }
    PostMessageW(window, FinishedMessage, 0, 0);
    return 0;
}
Value diagnostics(bool carKit = false, HANDLE cancel = 0) {
    Value v = Value::dictionary();
    v.dict["implementation"] = Value::str("C++03 / native Win32 x64");
    v.dict["physical_carplay_verified"] = Value::flag(false);
    v.dict["mfi_identity_included"] = Value::flag(false);
    UsbMux mux;
    std::vector<AppleDevice> d = mux.devices();
    v.dict["apple_service"] = Value::str("reachable");
    v.dict["device_count"] = Value::num(d.size());
    Value devices = Value::list();
    for (size_t i = 0; i < d.size(); i++) {
        Value x = Value::dictionary();
        x.dict["connection"] = Value::str(d[i].connection);
        try {
            x.dict["ios"] = mux.deviceInfo(d[i]);
        } catch (const std::exception &e) {
            x.dict["error"] = Value::str(e.what());
        }
        if (carKit)
            x.dict["carkit"] = probeCarKit(mux, d[i], 0, cancel);
        devices.array.push_back(x);
    }
    v.dict["devices"] = devices;
    return v;
}
DWORD WINAPI detect(void *extended) {
    Value v = Value::dictionary();
    std::wstring summary;
    try {
        v = diagnostics(extended != 0, cancelEvent);
        unsigned count = unsigned(v.at("device_count").integer);
        std::wostringstream s;
        s << L"Apple 设备服务正常，检测到 " << count << L" 台设备。\r\n";
        if (count == 0)
            s << L"请用数据线连接并解锁 iPhone。";
        else
            s << L"已完成设备探测；这不代表 CarPlay 已建立。";
        if (extended && count) {
            const Value &c = v.at("devices").array[0].at("carkit");
            if (c.dict.count("mfi_authentication_accepted"))
                s << L"\r\niPhone 已接受配件认证；此项检测不验证音视频投屏。";
            else if (c.dict.count("error"))
                s << L"\r\n" << wide(c.at("error").string);
            else if (c.dict.count("mfi_certificate_requested"))
                s << L"\r\niPhone 已请求配件证书；当前缺少认证身份，尚不能投屏。";
            else if (c.dict.count("iap2_identification_requested"))
                s << L"\r\n已收到 iAP2 身份请求；尚未验证完整会话。";
        }
        summary = s.str();
    } catch (const std::exception &e) {
        v.dict["error"] = Value::str(e.what());
        summary = L"检测未完成：" + wide(e.what());
    }
    v.dict["probe"] = Value::str(extended ? "carkit" : "devices");
    try {
        writeDiagnosticFile(executableDirectory() + L"/diagnostics.plist", xmlPlist(v));
        summary += L"\r\n\r\n详细结果已保存，可右键“打开诊断目录”查看。";
    } catch (const std::exception &e) {
        summary += L"\r\n\r\n结果保存失败：" + wide(e.what());
    }
    updateStatus(summary);
    std::wstring *result = new std::wstring(summary);
    if (!PostMessageW(window, DiagnosticMessage, 0, (LPARAM)result))
        delete result;
    PostMessageW(window, FinishedMessage, 0, 0);
    return 0;
}
void saveConnectionState(bool retryPending) {
    try {
        Value state = Value::dictionary();
        state.dict["automatic"] = Value::flag(autoConnect);
        state.dict["attempts"] = Value::num(connectionAttempts);
        state.dict["retry_pending"] = Value::flag(retryPending);
        state.dict["worker_active"] = Value::flag(job != 0);
        state.dict["status"] = Value::str(utf8(status));
        writeDiagnosticFile(executableDirectory() + L"/auto-connect.plist", xmlPlist(state));
    } catch (const std::exception &) {
    }
}
void reconnect() {
    pendingDiagnostic = 0;
    KillTimer(window, 4);
    autoConnect = true;
    if (job) {
        reconnectPending = true;
        SetEvent(cancelEvent);
    } else
        SendMessageW(window, WM_COMMAND, 105, 0);
}
void showMenu(LPARAM position) {
    HMENU menu = CreatePopupMenu(), resolutions = CreatePopupMenu();
    const wchar_t *labels[] = {L"800 × 480", L"1280 × 720", L"1920 × 1080"};
    for (unsigned i = 0; i < 3; ++i)
        AppendMenuW(resolutions, MF_STRING | (i == selectedDisplay ? MF_CHECKED : 0), 200 + i,
                    labels[i]);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)resolutions, L"分辨率（选择后重连）");

    AppendMenuW(menu, MF_STRING, 108, fullscreen ? L"退出全屏\tF11 / Esc" : L"全屏\tF11");
    AppendMenuW(menu, MF_STRING, 109, L"重新连接");
    AppendMenuW(menu, MF_STRING, 110, autoConnect ? L"暂停自动连接" : L"恢复自动连接");
    AppendMenuW(menu, MF_SEPARATOR, 0, 0);
    UINT diagnosticFlags = MF_STRING | ((diagnosticRunning || pendingDiagnostic || closing) ? MF_GRAYED : 0);
    AppendMenuW(menu, diagnosticFlags, 100, L"检测设备（暂停连接）");
    AppendMenuW(menu, diagnosticFlags, 104, L"检测 CarPlay 握手（暂停连接）");
    AppendMenuW(menu, MF_STRING, 103, L"打开诊断目录");
    AppendMenuW(menu, MF_STRING, 111, L"退出");
    POINT point = {short(LOWORD(position)), short(HIWORD(position))};
    if (point.x == -1 && point.y == -1) {
        RECT client;
        GetClientRect(window, &client);
        point.x = client.right / 2;
        point.y = client.bottom / 2;
        ClientToScreen(window, &point);
    }
    UINT command =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, 0);
    DestroyMenu(menu);
    if (command)
        SendMessageW(window, WM_COMMAND, command, 0);
}
RECT pictureRect() {
    RECT r;
    GetClientRect(window, &r);
    int w = std::max(1, int(r.right)), h = std::max(1, int(r.bottom));
    double scale = std::min(w / double(activeWidth), h / double(activeHeight));
    int pw = std::max(1, int(activeWidth * scale)), ph = std::max(1, int(activeHeight * scale));
    RECT p = {(r.right - pw) / 2, (r.bottom - ph) / 2, (r.right - pw) / 2 + pw,
              (r.bottom - ph) / 2 + ph};
    return p;
}
void draw(HDC dc) {
    EnterCriticalSection(&lock);
    bool gpu = gpuFrameVisible;
    LeaveCriticalSection(&lock);
    if(gpu) return; // D3D11 owns presentation; don't erase it with GDI.
    RECT client;
    GetClientRect(window, &client);
    FillRect(dc, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
    RECT p = pictureRect();
    Bytes frame;
    EnterCriticalSection(&lock);
    frame = latestFrame;
    LeaveCriticalSection(&lock);
    if (frame.size() == size_t(activeWidth) * activeHeight * 3) {
        BITMAPINFO info;
        ZeroMemory(&info, sizeof(info));
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = activeWidth;
        info.bmiHeader.biHeight = -int(activeHeight);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 24;
        info.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, p.left, p.top, p.right - p.left, p.bottom - p.top, 0, 0, activeWidth,
                      activeHeight, &frame[0], &info, DIB_RGB_COLORS, SRCCOPY);
    } else if (!fullscreen) {
        wxui::State view=connectionUi;
        if(closing)view.phase=wxui::Stopping;
        else if(diagnosticRunning || pendingDiagnostic)view.phase=wxui::DetectingPhase;
        else if(!autoConnect){
            view.phase=detailText.empty()?wxui::PausedPhase:wxui::DiagnosticPhase;
            view.detail=detailText;
        }
        wxui::paint(dc,client,view,waitingTick,connectionFont,deviceProfile().ui);
    }
}

void saveSnapshot() {
    RECT r;
    GetClientRect(window, &r);
    int w = r.right, h = r.bottom;
    HDC screen = GetDC(window), memory = CreateCompatibleDC(screen);
    BITMAPINFO info;
    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *bits = 0;
    HBITMAP bmp = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, 0, 0);
    HGDIOBJ old = SelectObject(memory, bmp);
    SendMessageW(window, WM_PRINT, (WPARAM)memory, PRF_CLIENT | PRF_CHILDREN);
    BITMAPFILEHEADER head;
    ZeroMemory(&head, sizeof(head));
    head.bfType = 0x4d42;
    head.bfOffBits = sizeof(head) + sizeof(BITMAPINFOHEADER);
    head.bfSize = head.bfOffBits + w * h * 4;
    Bytes data(head.bfSize);
    memcpy(&data[0], &head, sizeof(head));
    memcpy(&data[sizeof(head)], &info.bmiHeader, sizeof(BITMAPINFOHEADER));
    memcpy(&data[head.bfOffBits], bits, w * h * 4);
    writeFile(executableDirectory() + L"/ui-smoke.bmp", data);
    SelectObject(memory, old);
    DeleteObject(bmp);
    DeleteDC(memory);
    ReleaseDC(window, screen);
}
// A flip-model HWND can never return to GDI, even after its swap chain is
// released. Keep it in a disposable child; the parent always remains GDI.
LRESULT CALLBACK videoProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_NCHITTEST) return HTTRANSPARENT;
    if(msg==WM_ERASEBKGND) return 1;
    if(msg==WM_PAINT) { PAINTSTRUCT p; BeginPaint(h,&p); EndPaint(h,&p); return 0; }
    if(msg==WM_CONTEXTMENU) return SendMessageW(GetParent(h),msg,wp,lp);
    return DefWindowProcW(h,msg,wp,lp);
}
LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    RECT inputRect={0,0,0,0};
    if(window)inputRect=pictureRect();
    EnterCriticalSection(&lock);
    bool consumed=input.handle(h,msg,wp,lp,liveReceiver,inputRect);
    LeaveCriticalSection(&lock);
    if(consumed)return msg==0x0319?TRUE:0;

    switch (msg) {
    case PhoneCommandMessage: applyPhoneCommand(h,lp);return 0;
    case WM_CREATE: {
        HDC dc=GetDC(h);connectionFont.initialize(dc,deviceProfile().ui);ReleaseDC(h,dc);
        SetTimer(h,WaitingAnimationTimer,500,0);
        return 0;
    }
    case WM_COMMAND: {
        if (LOWORD(wp) == 108) {
            toggleFullscreen();
            return 0;
        }
        if (closing)
            return 0;

        if (LOWORD(wp) == 100 || LOWORD(wp) == 104) {
            if (diagnosticRunning)
                return 0;
            autoConnect = false;
            KillTimer(h, 4);
            reconnectPending = false;
            if (job) {
                pendingDiagnostic = LOWORD(wp);
                SetEvent(cancelEvent);
                status = L"正在停止连接，随后开始检测…";
            } else {
                pendingDiagnostic = 0;
                ResetEvent(cancelEvent);
                job = CreateThread(0, 0, detect, LOWORD(wp) == 104 ? (void *)1 : 0, 0, 0);
                diagnosticRunning = job != 0;
                status = job ? L"正在检测，请稍候…" : L"无法启动检测线程";
                detailText.clear();
            }
            saveConnectionState(false);
            InvalidateRect(h, 0, FALSE);
            return 0;
        }
        if (LOWORD(wp) >= 200 && LOWORD(wp) <= 202) {
            unsigned choice = LOWORD(wp) - 200;
            if (choice == selectedDisplay)
                return 0;
            try {
                Value settings = Value::dictionary();
                settings.dict["width"] = Value::num(displayWidths[choice]);
                settings.dict["height"] = Value::num(displayHeights[choice]);
                writeFile(executableDirectory() + L"/display-settings.plist", xmlPlist(settings));
                selectedDisplay = choice;
            } catch (const std::exception &e) {
                MessageBoxW(h, wide(e.what()).c_str(), L"无法保存分辨率", MB_OK | MB_ICONERROR);
                return 0;
            }
            reconnect();
            return 0;
        }
        if (LOWORD(wp) == 109) {
            reconnect();
            return 0;
        }
        if (LOWORD(wp) == 110) {
            if (autoConnect)
                SendMessageW(h, WM_COMMAND, 102, 0);
            else
                reconnect();
            InvalidateRect(h, 0, FALSE);
            return 0;
        }
        if (LOWORD(wp) == 111) {
            PostMessageW(h, WM_CLOSE, 0, 0);
            return 0;
        }
        if (LOWORD(wp) == 103) {
            ShellExecuteW(h, L"open", executableDirectory().c_str(), 0, 0, SW_SHOWNORMAL);
            return 0;
        }
        if (LOWORD(wp) == 102) {
            pendingDiagnostic = 0;
            autoConnect = false;
            KillTimer(h, 4);
            saveConnectionState(false);
            reconnectPending = false;
            SetEvent(cancelEvent);
            return 0;
        }
        if (job)
            return 0;
        ResetEvent(cancelEvent);
        if (LOWORD(wp) == 105) {
            KillTimer(h, 4);
            ++connectionAttempts;
            activeWidth = displayWidths[selectedDisplay];
            activeHeight = displayHeights[selectedDisplay];
            detailText.clear();
            status=L"正在连接 iPhone…";
            connectionUi=wxui::State();connectionUi.stage(0);
            SetTimer(h,WaitingAnimationTimer,500,0);
            {
                RECT r; GetClientRect(h,&r);
                videoWindow=CreateWindowW(L"MRN_GPU_VIDEO",L"",WS_CHILD|WS_CLIPSIBLINGS,
                    0,0,r.right,r.bottom,h,0,GetModuleHandleW(0),0);
                if(!videoWindow) { status=L"Cannot create video window"; connectionUi.phase=wxui::Failed;connectionUi.detail=status; InvalidateRect(h,0,FALSE); return 0; }
            }
            EnterCriticalSection(&lock);
            latestFrame.clear();
            gpuFrameVisible = false;
            LeaveCriticalSection(&lock);
            job = CreateThread(0, 0, stream, 0, 0, 0);
            if(!job && videoWindow) { DestroyWindow(videoWindow); videoWindow=0; }
            if(!job){connectionUi.phase=wxui::Failed;connectionUi.detail=L"Connection worker failed";}
            if (!job && autoConnect)
                SetTimer(h, 4, 3000, 0);
            saveConnectionState(!job && autoConnect);
            InvalidateRect(h, 0, FALSE);
        }
        return 0;
    }
    case StatusMessage: {
        std::wstring *s = (std::wstring *)lp;
        status = *s;
        delete s;
        connectionUi.update(status);
        InvalidateRect(h, 0, FALSE);
        return 0;
    }
    case ConnectionStageMessage:
        connectionUi.stage(unsigned(wp));
        InvalidateRect(h,0,FALSE);return 0;
    case FrameMessage:
        connectionUi.streaming=true;KillTimer(h,WaitingAnimationTimer);
        EnterCriticalSection(&lock);
        framePending = false;
        LeaveCriticalSection(&lock);
        if(videoWindow && gpuFrameVisible && !IsWindowVisible(videoWindow)) ShowWindow(videoWindow,SW_SHOWNOACTIVATE);
        InvalidateRect(h, 0, FALSE);
        return 0;
    case DiagnosticMessage: {
        std::wstring *result = (std::wstring *)lp;
        if (!closing && !autoConnect) detailText=*result;
        delete result;
        InvalidateRect(h,0,FALSE);
        return 0;
    }
    case FinishedMessage:
        connectionUi.streaming=false;
        if(!closing)SetTimer(h,WaitingAnimationTimer,500,0);
        if (job) {
            CloseHandle(job);
            job = 0;
        }
        diagnosticRunning = false;
        if(videoWindow) { DestroyWindow(videoWindow); videoWindow=0; }
        if (!smoke) {
            EnterCriticalSection(&lock);
            latestFrame.clear();
            gpuFrameVisible = false;
            LeaveCriticalSection(&lock);
            InvalidateRect(h, 0, FALSE);
        }
        if (pendingDiagnostic && !closing) {
            unsigned command = pendingDiagnostic;
            pendingDiagnostic = 0;
            SendMessageW(h, WM_COMMAND, command, 0);
        } else if (reconnectPending && !closing) {
            reconnectPending = false;
            SendMessageW(h, WM_COMMAND, 105, 0);
        } else if (autoConnect && !closing) {
            SetTimer(h, 4, 3000, 0);
            saveConnectionState(true);
        } else
            saveConnectionState(false);
        if (smoke)
            SetTimer(h, 1, 100, 0);
        return 0;
    case WM_CONTEXTMENU:
        showMenu(lp);
        return 0;
    case WM_TIMER:
        if(wp==WaitingAnimationTimer){
            ++waitingTick;
            if(!connectionUi.streaming && (autoConnect || diagnosticRunning || pendingDiagnostic) && !fullscreen && IsWindowVisible(h) && !IsIconic(h))InvalidateRect(h,0,FALSE);
            return 0;
        }
        if (wp == 4) {
            KillTimer(h, 4);
            if (autoConnect && !closing && !job)
                SendMessageW(h, WM_COMMAND, 105, 0);
            return 0;
        }
        if (wp == 1) {
            KillTimer(h, 1);
            try {
                saveSnapshot();
            } catch (...) {
            }
            PostMessageW(h, WM_CLOSE, 0, 0);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        EnterCriticalSection(&lock);
        bool gpu = gpuFrameVisible;
        LeaveCriticalSection(&lock);
        if(gpu) { EndPaint(h, &ps); return 0; }
        RECT r;
        GetClientRect(h, &r);
        HDC memory = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, r.right, r.bottom);
        if (memory && bitmap) {
            HGDIOBJ old = SelectObject(memory, bitmap);
            draw(memory);
            BitBlt(dc, 0, 0, r.right, r.bottom, memory, 0, 0, SRCCOPY);
            SelectObject(memory, old);
        } else draw(dc);
        if (bitmap)
            DeleteObject(bitmap);
        if (memory)
            DeleteDC(memory);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        draw((HDC)wp);
        return 0;
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, RGB(163, 180, 202));
        SetBkColor((HDC)wp, RGB(15, 21, 32));
        return (LRESULT)GetStockObject(NULL_BRUSH);
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if(videoWindow) {
            RECT r; GetClientRect(h,&r);
            SetWindowPos(videoWindow,0,0,0,r.right,r.bottom,SWP_NOZORDER|SWP_NOACTIVATE);
        }
        InvalidateRect(h, 0, FALSE);
        return 0;
    case WM_GETMINMAXINFO:
        if (!fullscreen) {
            MINMAXINFO *limits = (MINMAXINFO *)lp;
            limits->ptMinTrackSize.x = 640;
            limits->ptMinTrackSize.y = 360;
        }
        return 0;
    case WM_CLOSE:
        pendingDiagnostic = 0;
        closing = true;
        autoConnect = false;
        KillTimer(h, 4);
        reconnectPending = false;
        if (job) {
            SetEvent(cancelEvent);
            status = L"正在停止任务…";
            InvalidateRect(h, 0, FALSE);
            closing = true;
            SetTimer(h, 2, 100, 0);
            return 0;
        }
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        KillTimer(h,WaitingAnimationTimer);
        connectionFont.release();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    SetProcessDPIAware();
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    try {
        if (argc > 1 && std::wstring(argv[1]) == L"--serve-test") {
            UsbMux sockets;
            unsigned port = argc > 2 ? unsigned(_wtoi(argv[2])) : 7000;
            unsigned seconds = argc > 3 ? unsigned(_wtoi(argv[3])) : 45;
            unsigned w = argc > 4 ? unsigned(_wtoi(argv[4])) : 800,
                     h = argc > 5 ? unsigned(_wtoi(argv[5])) : 480;
            ReceiverContext dependencies(0, 0);
            Receiver receiver(dependencies.services(), 0, 0, port, false, w, h);
            receiver.start();
            Value info = Value::dictionary();
            info.dict["port"] = Value::num(receiver.port());
            info.dict["publicKey"] = Value::str(receiver.publicKey());
            writeDiagnosticFile(executableDirectory() + L"/receiver-listener.plist", xmlPlist(info));
            Sleep(std::min(seconds, 300u) * 1000);
            receiver.stop();
            LocalFree(argv);
            return 0;
        }
        if (argc > 1 && (std::wstring(argv[1]) == L"--diagnose" ||
                         std::wstring(argv[1]) == L"--diagnose-carkit")) {
            try {
                writeDiagnosticFile(executableDirectory() + L"/diagnostics.plist",
                          xmlPlist(diagnostics(std::wstring(argv[1]) == L"--diagnose-carkit")));
            } catch (const std::exception &e) {
                Value v = Value::dictionary();
                v.dict["error"] = Value::str(e.what());
                writeDiagnosticFile(executableDirectory() + L"/diagnostics.plist", xmlPlist(v));
                LocalFree(argv);
                return 1;
            }
            LocalFree(argv);
            return 0;
        }
        smoke = argc > 1 && std::wstring(argv[1]) == L"--ui-smoke";
        autoConnect = !smoke;
        LocalFree(argv);
        loadDisplaySettings();
        InitializeCriticalSection(&lock);
        cancelEvent = CreateEventW(0, TRUE, FALSE, 0);
        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.lpszClassName = L"MRN_NATIVE_CARPLAY";
        RegisterClassW(&wc);
        WNDCLASSW videoClass={}; videoClass.lpfnWndProc=videoProc; videoClass.hInstance=instance;
        videoClass.hCursor=wc.hCursor; videoClass.lpszClassName=L"MRN_GPU_VIDEO";
        RegisterClassW(&videoClass);
        window = CreateWindowW(wc.lpszClassName, L"WinXPlay · Windows 原生开发版",
                               WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, smoke ? -3000 : CW_USEDEFAULT,
                               smoke ? -3000 : CW_USEDEFAULT, 1280, 760, 0, 0, instance, 0);
        require(window != 0, "Cannot create native window");
        ShowWindow(window, smoke ? SW_SHOWNOACTIVATE : show);
        UpdateWindow(window);
        if (smoke) {
            detailText=L"Apple 设备服务正常，检测到 1 台设备。\n已完成设备探测；这不代表 CarPlay 已建立。\n\n详细结果已保存，可右键“打开诊断目录”查看。";
            SetTimer(window,1,300,0);
        }
        if (autoConnect)
            SendMessageW(window, WM_COMMAND, 105, 0);
        MSG msg;
        while (GetMessageW(&msg, 0, 0, 0) > 0) {
            if (msg.message == WM_KEYDOWN && !(msg.lParam & (1L << 30)) &&
                (msg.wParam == VK_F11 || (msg.wParam == VK_ESCAPE && fullscreen))) {
                toggleFullscreen();
                continue;
            }
            if (msg.message == WM_TIMER && msg.wParam == 2) {
                if (job && WaitForSingleObject(job, 0) == WAIT_OBJECT_0) {
                    CloseHandle(job);
                    job = 0;
                }
                if (!job) {
                    KillTimer(window, 2);
                    DestroyWindow(window);
                }
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (job) {
            SetEvent(cancelEvent);
            WaitForSingleObject(job, 6000);
            CloseHandle(job);
        }
        CloseHandle(cancelEvent);
        DeleteCriticalSection(&lock);
        return 0;
    } catch (const std::exception &e) {
        Value failure = Value::dictionary();
        failure.dict["error"] = Value::str(e.what());
        try {
            writeDiagnosticFile(executableDirectory() + L"/startup-error.plist", xmlPlist(failure));
        } catch (...) {
        }
        return 1;
    }
}
