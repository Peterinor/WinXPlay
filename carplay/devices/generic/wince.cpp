#include "../wince.h"
namespace cp {
void prepareDeviceConnection(void (*)(void *,const std::string &)) {}
bool acquireDeviceDeploymentGuard(HANDLE &guard) { guard=0; return true; }
void configureDeviceWindow(HWND) {}
AudioOutputBackend *createDeviceAudioOutput(const std::wstring &name,unsigned rate,unsigned channels) {
    require(name==L"wave","Unknown audio output backend");
    return createWaveOutput(rate,channels);
}
void describeDeviceMedia(Value &) {}
}
