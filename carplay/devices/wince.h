#ifndef CP_DEVICE_WINCE_H
#define CP_DEVICE_WINCE_H
#include "../platform/wince/media_backend.h"
namespace cp {
// Build-selected device policy. Generic implementations do not touch OEM state.
void prepareDeviceConnection(void (*status)(void *,const std::string &));
bool acquireDeviceDeploymentGuard(HANDLE &guard);
void configureDeviceWindow(HWND window);
AudioOutputBackend *createDeviceAudioOutput(const std::wstring &name, unsigned rate, unsigned channels);
void describeDeviceMedia(Value &configuration);
}
#endif
