#ifndef MRN_CP_PLATFORM_H
#define MRN_CP_PLATFORM_H
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "../../core/protocol.h"
namespace cp {
std::wstring wide(const std::string &s);
std::string utf8(const std::wstring &s);
Bytes readFile(const std::wstring &path, size_t limit = 16 * 1024 * 1024);
void writeFile(const std::wstring &path, const Bytes &b);
#ifndef WINXPLAY_ENABLE_LOGS
#define WINXPLAY_ENABLE_LOGS 1
#endif
#if WINXPLAY_ENABLE_LOGS
inline void writeDiagnosticFile(const std::wstring &path,const Bytes &b){writeFile(path,b);}
#else
inline void writeDiagnosticFile(const std::wstring &,const Bytes &){}
#endif
std::wstring executableDirectory();
std::wstring environment(const wchar_t *key, const wchar_t *fallback);
struct AppleDevice {
    unsigned id;
    std::string serial, connection;
};
class UsbMux {
#if defined(UNDER_CE) || defined(CARPLAY_TEST_CE_USB)
    void *native_;
#endif
    unsigned port_, tag_;
    SOCKET connectService();
    Value request(SOCKET s, const Value &message);
    UsbMux(const UsbMux &);
    UsbMux &operator=(const UsbMux &);

  public:
    explicit UsbMux(unsigned port = 27015);
    ~UsbMux();
    std::vector<AppleDevice> devices();
    Value deviceInfo(const AppleDevice &device);
    Value pairRecord(const AppleDevice &device);
#if defined(UNDER_CE) || defined(CARPLAY_TEST_CE_USB)
    unsigned usbInterface() const;
#else
    unsigned usbInterface() const {
        return 1;
    }
#endif
    SOCKET connectDevice(const AppleDevice &device, unsigned port);
};
class Receiver;
Value prepareUsbCarPlay();
std::string receiverAddresses();
Value restorePhoneSession(UsbMux &mux, const AppleDevice &device);
Value probeCarKit(UsbMux &mux, const AppleDevice &device, Receiver *receiver = 0,
                  HANDLE cancel = 0,
                  void (*status)(void *, const std::string &) = 0, void *statusContext = 0);
// Platform-private frame notification. Windows GPU presentation uses an empty
// payload; legacy CE presenters may supply BGR pixels.
typedef void (*VideoCallback)(void *context, const Bytes &bgr);
void playPcm16(const Bytes &littleEndian, unsigned rate, unsigned channels);
} // namespace cp
#endif
