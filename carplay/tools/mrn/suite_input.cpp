#include "suite.h"
#include "../../platform/common/input_messages.h"
#include <sstream>
using namespace cp;
namespace {
HANDLE file=INVALID_HANDLE_VALUE;HWND window=0;DWORD began=0,duration=60000,count=0;
bool bridge=false,ioFailed=false;std::wstring latest=L"Waiting for input";
void record(const char *source,UINT msg,WPARAM wp,LPARAM lp){
    std::ostringstream s;s<<GetTickCount()-began<<" "<<source<<" msg="<<msg<<" wp="<<DWORD(wp)<<" lp="<<DWORD(lp);
    if(msg>=WM_MOUSEFIRST&&msg<=WM_MOUSELAST)s<<" x="<<short(LOWORD(lp))<<" y="<<short(HIWORD(lp));
    latest=wide(s.str());std::string line=s.str()+"\r\n";DWORD n=0;
    if(!WriteFile(file,line.data(),DWORD(line.size()),&n,0)||n!=line.size())ioFailed=true;
    ++count;if(window)InvalidateRect(window,0,FALSE);
}
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if((m>=WM_MOUSEFIRST&&m<=WM_MOUSELAST)||(m>=WM_KEYFIRST&&m<=WM_KEYLAST)){
        record("windows-input",m,w,l);
        if(m==WM_KEYUP&&w==VK_ESCAPE){PostMessageW(h,WM_CLOSE,0,0);return 0;}
    }
    if(m>=WINXPLAY_INPUT_CONTACT&&m<=WINXPLAY_INPUT_VISIBLE){record("adapter-input",m,w,l);return 0;}
    if(m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_CAPTURECHANGED||m==WM_CANCELMODE||m==WM_SHOWWINDOW)record("window-state",m,w,l);
    if(m==WM_PAINT){PAINTSTRUCT p;HDC dc=BeginPaint(h,&p);RECT r;GetClientRect(h,&r);
        FillRect(dc,&r,(HBRUSH)GetStockObject(BLACK_BRUSH));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
        std::wstring text=L"WinXPlay input test\nTouch / keys / bridge\n"+latest+L"\nESC or timeout to exit";
        DrawTextW(dc,text.c_str(),-1,&r,DT_WORDBREAK);EndPaint(h,&p);return 0;}
    if(m==WM_TIMER&&w==1){FlushFileBuffers(file);if(GetTickCount()-began>=duration||ioFailed)PostMessageW(h,WM_CLOSE,0,0);return 0;}
    if(m==WM_CLOSE){KillTimer(h,1);DestroyWindow(h);return 0;}
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);
}
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPWSTR command,int){
    std::string mode,error;int code=1;HWND previous=0;
    HANDLE guard=CreateMutexW(0,FALSE,L"winxplay_test_input.Singleton");
    if(!guard||GetLastError()==ERROR_ALREADY_EXISTS){if(guard)CloseHandle(guard);return 3;}
    try {
        Value config=suiteConfig();mode=suiteMode(command,config,"standard");require(mode=="standard"||mode=="bridge","Unknown input mode");
        bridge=mode=="bridge";duration=suiteNumber(config,"seconds",60,300)*1000;require(duration>0,"Input duration must be positive");
        if(bridge)require(!FindWindowW(L"WinXPlay",0),"WinXPlay already owns the bridge window");
        Sleep(suiteNumber(config,"delay_seconds",10,120)*1000);began=GetTickCount();previous=GetForegroundWindow();
        for(unsigned i=1;i<10000;++i){wchar_t name[64];swprintf(name,L"/input-%04u.log",i);
            file=CreateFileW((executableDirectory()+name).c_str(),GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_NEW,0,0);
            if(file!=INVALID_HANDLE_VALUE)break;
            require(GetLastError()==ERROR_FILE_EXISTS||GetLastError()==ERROR_ALREADY_EXISTS,"Cannot create input log");}
        require(file!=INVALID_HANDLE_VALUE,"Input log slots exhausted");
        WNDCLASSW wc={0};wc.hInstance=instance;wc.lpfnWndProc=proc;wc.lpszClassName=bridge?L"WinXPlay":L"winxplay_test_input";
        require(RegisterClassW(&wc)!=0,"Input class registration failed");
        window=CreateWindowExW(0,wc.lpszClassName,L"WinXPlay input test",WS_POPUP,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),0,0,instance,0);
        require(window!=0,"Input window creation failed");require(SetTimer(window,1,250,0)!=0,"Input timer failed");
        if(!bridge){ShowWindow(window,SW_SHOW);SetForegroundWindow(window);}
        record(bridge?"waiting-for-BAND":"standard-window-ready",0,0,0);
        MSG message;int result;while((result=GetMessageW(&message,0,0,0))>0){TranslateMessage(&message);DispatchMessageW(&message);}
        require(result>=0&&!ioFailed,"Input message loop or log failed");code=0;
    }catch(const std::exception &e){error=e.what();}
    if(window&&IsWindow(window))SendMessageW(window,WM_CLOSE,0,0);
    if(previous&&IsWindow(previous))SetForegroundWindow(previous);
    if(file!=INVALID_HANDLE_VALUE){FlushFileBuffers(file);CloseHandle(file);}
    try{suiteResult("input",mode,code,error);}catch(...){code=1;}
    CloseHandle(guard);return code;
}
