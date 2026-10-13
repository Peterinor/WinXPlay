#include "../wince.h"
namespace cp {
bool acquireDeviceDeploymentGuard(HANDLE &guard) { guard=0; return true; }
AudioOutputBackend *createDeviceAudioOutput(const std::wstring &name,unsigned rate,unsigned channels) {
    require(name==L"wave","Unknown audio output backend");
    return createWaveOutput(rate,channels);
}
void describeDeviceMedia(Value &) {}
AudioInputBackend *createDeviceAudioInput(const std::wstring &name,unsigned rate,unsigned channels,unsigned samples){
    require(name==L"wave","Unknown audio input backend");return createWaveInput(rate,channels,samples);
}
}
