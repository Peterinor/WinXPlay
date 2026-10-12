// F3.21 contracts recovered from ENTMEDIAMAIN and ENTESAIAUDIO.
// Success means the IOCTL returned TRUE, NOT that an audio connection is active.
#pragma once
extern "C" __declspec(dllexport) BOOL MrnUsbRouteCommand(HANDLE device, DWORD action) {
    BYTE input[2]={9,0};DWORD code=0,length=1;
    switch(action) {
    case 1: code=4;length=2;input[1]=1;break; // source 9 available
    case 2: code=1;break;                   // select source 9
    case 3: code=2;break;                   // unselect source 9
    case 4: code=4;length=2;break;           // source 9 unavailable
    default: SetLastError(ERROR_INVALID_PARAMETER);return FALSE;
    }
    return DeviceIoControl(device,code,input,length,0,0,0,0);
}
extern "C" __declspec(dllexport) BOOL MrnEsaiUnmute(HANDLE device) {
    return DeviceIoControl(device,8,0,0,0,0,0,0);
}
