#include "../wince.h"
#include "portable.h"
#include "audio_output.h"
namespace cp {
void prepareDeviceConnection(void (*status)(void *,const std::string &)) {
    static bool ready=false;
    if(!ready && environment(L"CARPLAY_PORTABLE",L"0")==L"1") {
        if(status)status(0,"Preparing USB test; connect iPhone after startup");
        preparePortableReceiver();
        ready=true;
    }
}
bool acquireDeviceDeploymentGuard(HANDLE &guard) {
    guard=0;
    if(_wcsicmp(executableDirectory().c_str(),L"\\NAND_USER\\WinXPlay")!=0)return true;
    // Retain the deployed FILEOPS mutex name for update compatibility.
    guard=CreateMutexW(0,FALSE,L"MRN_BAND_TEST_SINGLETON_V1");
    return guard && GetLastError()!=ERROR_ALREADY_EXISTS;
}
void configureDeviceWindow(HWND window) {
    // Match F3.21's player owner setup. HMI layer ownership is separate.
    SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
    SetForegroundWindow(window);
}
AudioOutputBackend *createDeviceAudioOutput(const std::wstring &name,unsigned rate,unsigned channels) {
    require(name==L"wave","Unknown audio output backend");
    return createMrnRecoveringOutput(rate,channels);
}
void describeDeviceMedia(Value &v) {
    v.dict["video_backend"]=Value::str("wince_directshow");
    v.dict["audio_output_backend"]=Value::str("mrn_tts_pcm_retry");
    v.dict["wave_device"]=Value::num(1);
    v.dict["touch_enabled"]=Value::flag(environment(L"CARPLAY_TOUCH",L"1")!=L"0");
    v.dict["audio_input_backend"]=Value::str("disabled");
    v.dict["presenter"]=Value::str("system_video_renderer");
    v.dict["video_only"]=Value::flag(false);
    v.dict["audio_output_enabled"]=Value::flag(true);
    v.dict["physical_audio_verified"]=Value::flag(false);
}
}
