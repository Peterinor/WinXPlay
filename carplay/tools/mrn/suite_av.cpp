#include "suite.h"
int WINAPI AvTestMain(HINSTANCE,HINSTANCE,LPWSTR,int);
int WINAPI MediaProbeMain(HINSTANCE,HINSTANCE,LPWSTR,int);
extern "C" DWORD VoicePcmTest(DWORD,DWORD);
int MicrophoneTest(HINSTANCE,const cp::Value&);
int WINAPI WinMain(HINSTANCE h,HINSTANCE prev,LPWSTR command,int show) {
    std::string mode;int code=1;std::string error;
    HANDLE guard=CreateMutexW(0,FALSE,L"winxplay_test_av.Singleton");
    if(!guard||GetLastError()==ERROR_ALREADY_EXISTS){if(guard)CloseHandle(guard);return 3;}
    try {
        cp::Value config=cp::suiteConfig();mode=cp::suiteMode(command,config,"play");
        cp::require(mode=="inspect"||mode=="instantiate"||mode=="play"||mode=="voice-pcm"||mode=="microphone"||mode=="--selftest"||
            mode=="--selftest-cancel"||mode=="--selftest-long"||mode=="--selftest-long-cancel"||
            mode=="--selftest-audio-probe-cancel","Unknown AV mode");
        unsigned delay=cp::suiteNumber(config,"delay_seconds",(mode=="voice-pcm"||mode=="play")?10:0,120);
        if(mode!="voice-pcm"&&mode!="microphone")Sleep(delay*1000);
        std::wstring argument;
        if(mode=="microphone") {
            code=MicrophoneTest(h,config);
        } else if(mode=="voice-pcm") {
            unsigned seconds=cp::suiteNumber(config,"seconds",20,60);
            code=VoicePcmTest(delay,seconds);
        } else if(mode=="inspect"||mode=="instantiate") {
            argument=mode=="instantiate"?L"--instantiate":L"";
            code=MediaProbeMain(h,prev,const_cast<LPWSTR>(argument.c_str()),show);
        } else {
            argument=mode=="play"?L"":cp::wide(mode);
            code=AvTestMain(h,prev,const_cast<LPWSTR>(argument.c_str()),show);
        }
    }catch(const std::exception &e){error=e.what();}
    try{cp::suiteResult("av",mode,code,error);}catch(...){code=1;}
    CloseHandle(guard);return code;
}
