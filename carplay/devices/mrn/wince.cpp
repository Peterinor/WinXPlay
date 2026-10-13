#include "../wince.h"
#include "audio_route.h"
#include "../../platform/wince/audio_recovery.h"
namespace cp {
AudioInputBackend *createMrnAudioInput(unsigned,unsigned,unsigned);
AudioInputBackend *createDeviceAudioInput(const std::wstring &name,unsigned rate,unsigned channels,unsigned samples){
    require(name==L"wave","Unknown audio input backend");return createMrnAudioInput(rate,channels,samples);
}
bool acquireDeviceDeploymentGuard(HANDLE &guard) {
    guard=0;
    if(_wcsicmp(executableDirectory().c_str(),L"\\NAND_USER\\WinXPlay")!=0)return true;
    // Retain the deployed FILEOPS mutex name for update compatibility.
    guard=CreateMutexW(0,FALSE,L"MRN_BAND_TEST_SINGLETON_V1");
    return guard && GetLastError()!=ERROR_ALREADY_EXISTS;
}
AudioOutputBackend *createDeviceAudioOutput(const std::wstring &name,unsigned rate,unsigned channels) {
    require(name==L"wave","Unknown audio output backend");
    return createRecoveringOutput(rate,channels,createMrnAudioOutput,2000,"mrn_tts_pcm_retry");
}
void describeDeviceMedia(Value &v) {
    v.dict["video_backend"]=Value::str("wince_directshow");
    v.dict["audio_output_backend"]=Value::str("mrn_tts_pcm_retry");
    v.dict["wave_device"]=Value::num(1);
    v.dict["touch_enabled"]=Value::flag(environment(L"CARPLAY_TOUCH",L"1")!=L"0");
    v.dict["audio_input_backend"]=Value::str("mrn_dictaphone_pcm");
    v.dict["capture_sample_rate"]=Value::num(22050);
    v.dict["capture_wave_device"]=Value::num(0);
    v.dict["presenter"]=Value::str("system_video_renderer");
    v.dict["physical_audio_verified"]=Value::flag(false);
}
}
