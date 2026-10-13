#include "../profile.h"
namespace cp {
const DeviceProfile &deviceProfile() {
    // F3.21's lanting.TTF identifies itself as Microsoft YaHei in its name table.
    static const UiFontResource fonts[] = {
        {L"\\NAND_RES\\COMMON\\Font\\lanting.TTF", L"Microsoft YaHei", L"\u5fae\u8f6f\u96c5\u9ed1"},
        {L"\\NAND_RES\\entNav\\GNaviRes\\simhei.ttf", L"SimHei", L"\u9ed1\u4f53"}
    };
    static const DeviceProfile p = {
        800, 480, 30, "MRNWinCECarPlay1,1", L"directshow", "WinXPlay MRN",
        {fonts, sizeof(fonts)/sizeof(fonts[0]), L"MRN  /  CarPlay",
         L"\u81ea\u52a8\u8fde\u63a5\u5df2\u5f00\u542f   \u00b7   BAND \u8fd4\u56de\u539f\u8f66\u754c\u9762",
         L"Auto-connect on   -   BAND: return to car"},
        L"{3D581965-7C6E-4FE9-894F-50FC821BAE68}",
        L"{70E102B0-5556-11CE-97C0-00AA0055595A}",
        // Independent switches: false/false leaves audio to the phone/vehicle.
        DeviceAudioProfile(false,false,L"{37CA27C5-5E4E-4F0C-8DCF-FEC2B5EB789F}")};
    return p;
}
} // namespace cp
