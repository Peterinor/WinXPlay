#ifndef CP_DEVICE_WINCE_H
#define CP_DEVICE_WINCE_H
#include "../platform/wince/media_backend.h"
namespace cp {
// Build-selected device policy. Generic implementations do not touch OEM state.
bool acquireDeviceDeploymentGuard(HANDLE &guard);
AudioOutputBackend *createDeviceAudioOutput(const std::wstring &name, unsigned rate, unsigned channels);
AudioInputBackend *createDeviceAudioInput(const std::wstring &name, unsigned rate, unsigned channels, unsigned samples);
void describeDeviceMedia(Value &configuration);
}
#endif
