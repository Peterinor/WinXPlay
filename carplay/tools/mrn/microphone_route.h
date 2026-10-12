// F3.21 ENTDICTAPHONEMAIN: 1001F634 / 1001F55C / 1001F5C8.
// TRUE means request accepted, not asynchronous source activation confirmed.
#pragma once
extern "C" __declspec(dllexport) BOOL MrnMicrophoneRouteCommand(HANDLE device,DWORD action) {
    BYTE input[2]={0x10,0};DWORD code=0,length=1;
    switch(action) {
    case 1: code=4;length=2;input[1]=1;break; // available
    case 2: code=1;break;                   // select
    case 3: code=4;length=2;break;           // unavailable (OEM release order)
    case 4: code=2;break;                   // unselect
    default: SetLastError(ERROR_INVALID_PARAMETER);return FALSE;
    }
    return DeviceIoControl(device,code,input,length,0,0,0,0);
}
