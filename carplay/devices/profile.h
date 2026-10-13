#ifndef CP_DEVICE_PROFILE_H
#define CP_DEVICE_PROFILE_H
#include <string>
namespace cp {
struct UiFontResource {
    const wchar_t *path, *family, *localizedFamily;
};
struct DeviceUiProfile {
    const UiFontResource *fonts;
    unsigned fontCount;
    const wchar_t *caption;
    const wchar_t *connectionHint, *connectionHintEnglish;
};
// Values known today, not a plugin/device class hierarchy. OEM resource
// takeover is not implemented by declaring these defaults.
struct DeviceAudioProfile {
    bool output, input;
    const wchar_t *aacDecoder;
    DeviceAudioProfile(bool out=true,bool in=true,const wchar_t *decoder=0)
        :output(out),input(in),aacDecoder(decoder){}
};
struct DeviceProfile {
    unsigned width, height, maxFPS;
    const char *model;
    const wchar_t *videoBackend;
    const char *displayName;
    DeviceUiProfile ui;
    const wchar_t *h264Decoder, *videoRenderer;
    DeviceAudioProfile audio;
};
const DeviceProfile &deviceProfile();
} // namespace cp
#endif
