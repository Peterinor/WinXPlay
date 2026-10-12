// ARM contract entry exercises the same engine as AV, without COM or UI.
#include "tts_pcm.h"
extern "C" __declspec(dllexport) DWORD TtsPcmContract(DWORD seconds,DWORD sync) {
    static TtsPcm audio;
    DWORD result=audio.open(seconds,sync!=0);
    if(!result)result=audio.start();
    DWORD start=GetTickCount();
    while(!result&&!audio.done()&&GetTickCount()-start<(seconds+3)*1000){result=audio.pump();Sleep(10);}
    if(!result&&!audio.done())result=26;
    if(!audio.close())result=27;
    return result;
}
