#include "../common/native_services.h"
#include <setupapi.h>
#include <iphlpapi.h>
namespace cp {
namespace {
bool ncmAdapter(const char *name) {
    GUID guid = {0x4d36e972, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
    HDEVINFO set = SetupDiGetClassDevsW(&guid, 0, 0, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE)
        return false;
    bool found = false;
    for (DWORD i = 0; !found; ++i) {
        SP_DEVINFO_DATA item;
        ZeroMemory(&item, sizeof(item));
        item.cbSize = sizeof(item);
        if (!SetupDiEnumDeviceInfo(set, i, &item))
            break;
        wchar_t service[80] = {0};
        if (!SetupDiGetDeviceRegistryPropertyW(set, &item, SPDRP_SERVICE, 0, (PBYTE)service,
                                               sizeof(service), 0) ||
            _wcsicmp(service, L"UsbNcm"))
            continue;
        HKEY key = SetupDiOpenDevRegKey(set, &item, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE)
            continue;
        char id[128] = {0};
        DWORD size = sizeof(id), type = 0;
        if (RegQueryValueExA(key, "NetCfgInstanceId", 0, &type, (BYTE *)id, &size) ==
                ERROR_SUCCESS &&
            type == REG_SZ && size < sizeof(id) && _stricmp(id, name) == 0)
            found = true;
        RegCloseKey(key);
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}
Bytes controlUsb(HANDLE device, U8 request, U16 value, U16 index, unsigned length) {
    Bytes packet(8 + length, 0);
    packet[0] = request == 6 ? 0x80 : 0xc0;
    packet[1] = request;
    put16(packet, 2, value, true);
    put16(packet, 4, index, true);
    put16(packet, 6, U16(length), true);
    OVERLAPPED overlap;
    ZeroMemory(&overlap, sizeof(overlap));
    overlap.hEvent = CreateEventW(0, TRUE, FALSE, 0);
    require(overlap.hEvent != 0, "USB event allocation failed");
    DWORD done = 0;
    BOOL ok = DeviceIoControl(device, 0x2200a0, &packet[0], DWORD(packet.size()), &packet[0],
                              DWORD(packet.size()), 0, &overlap);
    DWORD error = GetLastError();
    if (!ok && error != ERROR_IO_PENDING) {
        CloseHandle(overlap.hEvent);
        throw std::runtime_error("Apple USB control request rejected");
    }
    if (WaitForSingleObject(overlap.hEvent, 3000) != WAIT_OBJECT_0) {
        CancelIo(device);
        GetOverlappedResult(device, &overlap, &done, TRUE);
        CloseHandle(overlap.hEvent);
        throw std::runtime_error("Apple USB control request timed out");
    }
    ok = GetOverlappedResult(device, &overlap, &done, FALSE);
    CloseHandle(overlap.hEvent);
    require(ok && done >= 8 && done <= packet.size(), "Invalid Apple USB control response");
    return slice(packet, 8, done - 8);
}
} // namespace
Value prepareUsbCarPlay() {
    // Apple's existing compatibility interface; no driver replacement required.
    GUID guid = {0xf0b32be3, 0x6678, 0x4879, {0x92, 0x30, 0xe4, 0x38, 0x45, 0xd8, 0x05, 0xee}};
    HDEVINFO set = SetupDiGetClassDevsW(&guid, 0, 0, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    require(set != INVALID_HANDLE_VALUE, "Apple USB interface enumeration failed");
    HANDLE device = INVALID_HANDLE_VALUE;
    Value result = Value::dictionary();
    try {
        SP_DEVICE_INTERFACE_DATA item;
        ZeroMemory(&item, sizeof(item));
        item.cbSize = sizeof(item);
        require(SetupDiEnumDeviceInterfaces(set, 0, &guid, 0, &item) != FALSE,
                "Connect and unlock an iPhone first");
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &item, 0, 0, &needed, 0);
        require(needed >= sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) && needed <= 65536,
                "Invalid Apple USB interface path");
        Bytes buffer(needed);
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)&buffer[0];
        detail->cbSize = sizeof(*detail);
        require(SetupDiGetDeviceInterfaceDetailW(set, &item, detail, needed, 0, 0) != FALSE,
                "Apple USB interface lookup failed");
        device = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING,
                             FILE_FLAG_OVERLAPPED, 0);
        require(device != INVALID_HANDLE_VALUE, "Cannot open Apple USB compatibility interface");
        Bytes descriptor = controlUsb(device, 6, 0x100, 0, 18);
        require(descriptor.size() == 18 && le16(descriptor, 8) == 0x05ac, "Unexpected USB device");
        bool switched = descriptor[17] < 6;
        result.dict["configuration_count"] = Value::num(descriptor[17]);
        if (switched) {
            Bytes response = controlUsb(device, 0x52, 0, 4, 1);
            require(response.size() == 1 && response[0] == 0, "iPhone rejected CarPlay USB mode");
        }
        result.dict["mode_switch_requested"] = Value::flag(switched);
        CloseHandle(device);
        SetupDiDestroyDeviceInfoList(set);
        return result;
    } catch (...) {
        if (device != INVALID_HANDLE_VALUE)
            CloseHandle(device);
        SetupDiDestroyDeviceInfoList(set);
        throw;
    }
}
std::string receiverAddresses() {
    std::wstring explicitAddress = environment(L"CARPLAY_ADDRESS", L"");
    if (!explicitAddress.empty())
        return utf8(explicitAddress);
    ULONG size = 16384;
    Bytes buffer(size);
    ULONG rc = GetAdaptersAddresses(AF_INET6, 0, 0, (IP_ADAPTER_ADDRESSES *)&buffer[0], &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        require(size <= 1024 * 1024, "Network adapter list limit");
        buffer.resize(size);
        rc = GetAdaptersAddresses(AF_INET6, 0, 0, (IP_ADAPTER_ADDRESSES *)&buffer[0], &size);
    }
    require(rc == NO_ERROR, "Network adapter enumeration failed");
    std::string result;
    for (IP_ADAPTER_ADDRESSES *a = (IP_ADAPTER_ADDRESSES *)&buffer[0]; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || !ncmAdapter(a->AdapterName))
            continue;
        for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family != AF_INET6)
                continue;
            sockaddr_in6 address = *(sockaddr_in6 *)u->Address.lpSockaddr;
            if (!IN6_IS_ADDR_LINKLOCAL(&address.sin6_addr))
                continue;
            address.sin6_scope_id = 0;
            address.sin6_port = 0;
            char text[128];
            DWORD n = sizeof(text);
            if (WSAAddressToStringA((sockaddr *)&address, sizeof(address), 0, text, &n) == 0) {
                if (!result.empty())
                    result += ';';
                result += text;
            }
        }
    }
    require(!result.empty(), "iPhone USB networking is unavailable. Run repair-usb-ncm.ps1 -Apply "
                             "in administrator PowerShell, then reconnect.");
    return result;
}
} // namespace cp
