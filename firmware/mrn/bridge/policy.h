#pragma once
#include <windows.h>
// Private messages are internal to the HMI process, never sent to WinXPlay.
enum { MRN_TIMER=73, MRN_HIDE_A=WM_APP+0x51b, MRN_SHOW_A=WM_APP+0x51c,
 MRN_DOWN=WM_APP+0x520,MRN_MOVE,MRN_UP,MRN_KEY_DOWN,MRN_KEY_UP,
 MRN_TOUCH_TRACE=WM_APP+0x52a,
 MRN_RAW_KEY_TRACE=WM_APP+0x52b,
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
// F3.21 raw IDs from the original HMI lookup table at 0x45100.
inline UINT MrnRawVirtualKey(WPARAM key) {
    switch(key){
    case 0x01:case 0x27:return VK_HOME; // FMUX_MODE / SWRC_MODE
    case 0x11:case 0x1e:return 0xb0;   // SEEK_UP / steering SEEKUP
    case 0x10:case 0x1f:return 0xb1;   // SEEK_DOWN / steering SEEKDOWN
    case 0x0d:case 0x21:case 0x24:return VK_RETURN;
    case 0x0b:case 0x25:return VK_BACK;
    case 0x0e:return VK_UP;
    case 0x0f:return VK_DOWN;
    default:return 0;
    }
}
// F3.21 rotary callback carries a signed 16-bit delta, not button action 0..4.
// Vehicle FILEOPS_0002 confirms 0x34 (SWRC_AUDIO_SCROLL) is this car's wheel.
// Also keep 0x36 (explicit preset/track); volume/navigation scrolls stay OEM.
inline BOOL MrnTrackStep(WPARAM key){return key==0x34 || key==0x36;}
inline UINT MrnStepVirtualKey(LPARAM value) {
    short delta=(short)LOWORD(value);
    return delta>0?0xb0:delta<0?0xb1:0;
}
