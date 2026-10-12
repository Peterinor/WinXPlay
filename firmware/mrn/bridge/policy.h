#pragma once
#include <windows.h>
// Private messages are internal to the HMI process, never sent to WinXPlay.
enum { MRN_TIMER=73, MRN_HIDE_A=WM_APP+0x51b, MRN_SHOW_A=WM_APP+0x51c,
 MRN_DOWN=WM_APP+0x520,MRN_MOVE,MRN_UP,MRN_KEY_DOWN,MRN_KEY_UP,
 MRN_UI_TIMEOUT=2000,MRN_START_TIMEOUT=8000 };
#define MRN_A_TITLE L"WinXPlayBridgeV2"
#define MRN_B_TITLE L"WinXPlay"
#define MRN_A_TARGET L"DPCA_HMI_WINEA7:0"
#define MRN_INSTALL L"\\NAND_USER\\WinXPlay\\"
inline BOOL MrnWindowPid(HWND window,DWORD pid) {
    DWORD actual=0;
    return window && IsWindow(window) && GetWindowThreadProcessId(window,&actual) && actual==pid;
}
inline BOOL MrnEqual(const WCHAR* a,const WCHAR* b) {
    while(*a && *a==*b){++a;++b;}return *a==*b;
}
inline UINT MrnTouchMessage(UINT m) {
    return m==WM_LBUTTONDOWN?MRN_DOWN:m==WM_MOUSEMOVE?MRN_MOVE:m==WM_LBUTTONUP?MRN_UP:0;
}
inline BOOL MrnKeyAllowed(WPARAM key) {
    return key==VK_RETURN || key==VK_HOME || key==VK_BACK || key==VK_SPACE ||
        (key>=VK_LEFT && key<=VK_DOWN) || (key>=VK_F6 && key<=VK_F8) || (key>=0xb0 && key<=0xb3);
}
