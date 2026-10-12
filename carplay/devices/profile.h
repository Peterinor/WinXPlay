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
struct DeviceProfile {
    unsigned width, height;
    const char *model;
    const wchar_t *videoBackend;
    const char *displayName;
    DeviceUiProfile ui;
    const wchar_t *h264Decoder, *videoRenderer;
};
const DeviceProfile &deviceProfile();
} // namespace cp
#endif
