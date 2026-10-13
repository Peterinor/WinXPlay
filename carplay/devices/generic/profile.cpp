#include "../profile.h"
namespace cp {
const DeviceProfile &deviceProfile() {
#ifdef UNDER_CE
    static const DeviceProfile p = {800, 480, 30, "WinCECarPlay1,1", L"unconfigured", "WinXPlay",
        {0, 0, L"CarPlay", L"\u81ea\u52a8\u8fde\u63a5\u5df2\u5f00\u542f", L"Auto-connect on"}, 0, 0};
#else
    // Windows always uses Media Foundation/D3D11; no FFmpeg runtime backend.
    static const DeviceProfile p = {1280, 720, 30, "WindowsCarPlay1,1", L"system", "WinXPlay",
        {0, 0, L"CarPlay", L"\u53f3\u952e\u8bbe\u7f6e   \u00b7   F11 \u5168\u5c4f", L"Right-click: settings   -   F11: fullscreen"}, 0, 0};
#endif
    return p;
}
} // namespace cp
