#ifndef WINXPLAY_CE_USB_CONTRACT
#define WINXPLAY_CE_USB_CONTRACT
#include <windows.h>
#include <winioctl.h>
#define WINXPLAY_USB_DIAGNOSTICS_KEY L"Drivers\\USB\\ClientDrivers\\WinXPlayUsbMux\\Diagnostics"
#define WINXPLAY_USB_QUERY CTL_CODE(FILE_DEVICE_UNKNOWN, 2100, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define WINXPLAY_USB_CARPLAY_MODE CTL_CODE(FILE_DEVICE_UNKNOWN, 2101, METHOD_BUFFERED, FILE_ANY_ACCESS)
struct WinXPlayUsbInfo {
    DWORD size, version, generation, vendor, product, configuration, interfaceNumber, bulkIn,
        bulkOut, ncmPresent;
};
// Registration diagnostics, separate from the USB stream IOCTL ABI.
struct WinXPlayUsbRegistration {
    DWORD size, stage, error, idResult, idError, settingsResult, settingsError, fallback;
};
enum { USB_REG_LOAD=1, USB_REG_EXPORTS, USB_REG_SETTINGS,
       USB_REG_CLIENT_KEY, USB_REG_MATCH_KEY, USB_REG_DLL_VALUE, USB_REG_VERIFY, USB_REG_DONE };
#endif
