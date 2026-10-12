// Read-only CE network diagnosis. Does not register drivers, bind protocols,
// request USB modes, alter the HMI, or claim that a local address proves CarPlay.
#include "../../platform/common/native_services.h"
#include "../../platform/wince/usb_driver/contract.h"
#include <iphlpapi.h>
#include <ntddndis.h>
#include <cstdio>
#include <cstring>
using namespace cp;
namespace {
HANDLE logfile = INVALID_HANDLE_VALUE;
DWORD started;
std::wstring reportPath;
Value report = Value::dictionary();
void log(const std::string &s) {
    char prefix[40]; sprintf(prefix, "[%lu] ", GetTickCount() - started);
    std::string line = std::string(prefix) + s + "\r\n";
    DWORD n = 0;
    require(WriteFile(logfile, line.data(), DWORD(line.size()), &n, 0) && n == line.size(),
            "Log write failed");
    FlushFileBuffers(logfile);
}
Value errorValue(DWORD e) { return Value::num(e); }
void save() { writeFile(reportPath, xmlPlist(report)); }
struct Socket {
    SOCKET s;
    Socket(int type) : s(socket(AF_INET6, type, type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP)) {}
    ~Socket() { if (s != INVALID_SOCKET) closesocket(s); }
};
Value registry(const wchar_t *path, const wchar_t *name) {
    Value v = Value::dictionary(); HKEY key = 0;
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key);
    v.dict["open_error"] = errorValue(rc);
    if (rc != ERROR_SUCCESS) return v;
    BYTE data[4096] = {0}; DWORD size = sizeof(data), type = 0;
    rc = RegQueryValueExW(key, name, 0, &type, data, &size); RegCloseKey(key);
    v.dict["query_error"] = errorValue(rc); v.dict["type"] = Value::num(type);
    if (rc != ERROR_SUCCESS) return v;
    if (type == REG_DWORD && size == 4) {
        DWORD d; memcpy(&d, data, 4); v.dict["value"] = Value::num(d);
    } else if ((type == REG_SZ || type == REG_MULTI_SZ) && size % sizeof(wchar_t) == 0) {
        // Bound every scan to the bytes actually returned by the OS.
        Value strings = Value::list(); const wchar_t *p = (const wchar_t *)data;
        size_t count = size / sizeof(wchar_t), i = 0;
        while (i < count && p[i]) {
            size_t end = i; while (end < count && p[end]) ++end;
            if (end == count) { v.dict["malformed"] = Value::flag(true); break; }
            strings.array.push_back(Value::str(utf8(std::wstring(p + i, end - i))));
            i = end + 1;
        }
        v.dict["strings"] = strings;
    }
    return v;
}
std::string imagePath(const std::wstring &adapter) {
    Value v = registry((L"Comm\\" + adapter).c_str(), L"ImagePath");
    if (!v.dict.count("strings") || v.dict["strings"].array.empty()) return "";
    std::string s = v.dict["strings"].array[0].string;
    size_t slash = s.find_last_of("\\/"); if (slash != std::string::npos) s.erase(0, slash + 1);
    return s;
}
Value ndisList(HANDLE h, DWORD code, const std::wstring &name) {
    wchar_t data[8192] = {0}; DWORD size = 0;
    BOOL ok = DeviceIoControl(h, code, name.empty() ? 0 : (void *)name.c_str(),
                             name.empty() ? 0 : DWORD((name.size() + 1) * sizeof(wchar_t)),
                             data, sizeof(data), &size, 0);
    Value v = Value::dictionary(); v.dict["ok"] = Value::flag(ok != FALSE);
    v.dict["error"] = errorValue(ok ? 0 : GetLastError());
    v.dict["returned_bytes"] = Value::num(size); Value items = Value::list();
    if (ok && size <= sizeof(data) && size % sizeof(wchar_t) == 0) {
        size_t count = size / sizeof(wchar_t), i = 0;
        while (i < count && data[i]) {
            size_t end = i; while (end < count && data[end]) ++end;
            if (end == count) { v.dict["malformed"] = Value::flag(true); break; }
            items.array.push_back(Value::str(utf8(std::wstring(data + i, end - i)))); i = end + 1;
        }
    }
    v.dict["items"] = items; return v;
}
Value ndis() {
    Value v = Value::dictionary();
    log("NDIS_QUERY_BEGIN");
    HANDLE h = CreateFileW(DD_NDIS_DEVICE_NAME, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           0, OPEN_EXISTING, 0, 0);
    v.dict["open_error"] = errorValue(h == INVALID_HANDLE_VALUE ? GetLastError() : 0);
    if (h == INVALID_HANDLE_VALUE) return v;
    v.dict["protocols"] = ndisList(h, IOCTL_NDIS_GET_PROTOCOL_NAMES, L"");
    Value adapters = ndisList(h, IOCTL_NDIS_GET_ADAPTER_NAMES, L"");
    Value bindings = Value::dictionary();
    const std::vector<Value> &names = adapters.dict["items"].array;
    for (size_t i = 0; i < names.size() && i < 64; ++i)
        bindings.dict[names[i].string] = ndisList(h, IOCTL_NDIS_GET_ADAPTER_BINDINGS, wide(names[i].string));
    v.dict["adapters"] = adapters; v.dict["bindings"] = bindings;
    CloseHandle(h); log("NDIS_QUERY_END"); return v;
}
std::string addressText(const sockaddr_in6 &a) {
    wchar_t text[128]; DWORD count = 128;
    sockaddr_in6 copy = a;
    return WSAAddressToStringW((sockaddr *)&copy, sizeof(copy), 0, text, &count) == 0 ? utf8(text) : "";
}
bool linkLocal(const sockaddr_in6 &a) {
    return a.sin6_addr.s6_addr[0] == 0xfe && (a.sin6_addr.s6_addr[1] & 0xc0) == 0x80;
}
struct Selected {
    sockaddr_in6 local; bool found; unsigned candidates;
    Selected() : found(false), candidates(0) { ZeroMemory(&local, sizeof(local)); }
};
Value adapters(const std::string &requested, Selected &selected) {
    Value v = Value::dictionary(); Value items = Value::list();
    HMODULE lib = LoadLibraryW(L"iphlpapi.dll");
    if (!lib) { v.dict["load_error"] = errorValue(GetLastError()); return v; }
    typedef ULONG (WINAPI *GetAddresses)(ULONG, ULONG, PVOID, PIP_ADAPTER_ADDRESSES, PULONG);
    GetAddresses get = (GetAddresses)GetProcAddress(lib, L"GetAdaptersAddresses");
    if (!get) { v.dict["api_missing"] = Value::flag(true); FreeLibrary(lib); return v; }
    ULONG size = 16384, rc = ERROR_BUFFER_OVERFLOW; Bytes storage;
    for (int attempt = 0; attempt < 3 && rc == ERROR_BUFFER_OVERFLOW; ++attempt) {
        if (size > 1024 * 1024) break;
        storage.resize(size);
        rc = get(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                 0, (PIP_ADAPTER_ADDRESSES)&storage[0], &size);
    }
    v.dict["error"] = errorValue(rc);
    if (rc == NO_ERROR) {
        unsigned seen = 0;
        for (PIP_ADAPTER_ADDRESSES a = (PIP_ADAPTER_ADDRESSES)&storage[0]; a && seen++ < 128; a = a->Next) {
            Value item = Value::dictionary(); std::string name = a->AdapterName ? a->AdapterName : "";
            item.dict["name"] = Value::str(name);
            item.dict["ipv6_index"] = Value::num(a->Ipv6IfIndex);
            item.dict["status"] = Value::num(a->OperStatus); item.dict["mtu"] = Value::num(a->Mtu);
            std::string driver = imagePath(wide(name));
            bool ncm = _stricmp(driver.c_str(), "usbcdcncm.dll") == 0;
            item.dict["driver"] = Value::str(driver); item.dict["ncm_driver_match"] = Value::flag(ncm);
            bool target = requested.empty() ? ncm : name == requested;
            item.dict["selected_adapter"] = Value::flag(target);
            Value addresses = Value::list(); unsigned count = 0;
            for (PIP_ADAPTER_UNICAST_ADDRESS u = a->FirstUnicastAddress; u && count++ < 128; u = u->Next) {
                if (!u->Address.lpSockaddr || u->Address.iSockaddrLength < sizeof(sockaddr_in6) ||
                    u->Address.lpSockaddr->sa_family != AF_INET6) continue;
                sockaddr_in6 ip = *(sockaddr_in6 *)u->Address.lpSockaddr;
                if (linkLocal(ip) && !ip.sin6_scope_id) ip.sin6_scope_id = a->Ipv6IfIndex;
                Value address = Value::dictionary(); address.dict["address"] = Value::str(addressText(ip));
                address.dict["link_local"] = Value::flag(linkLocal(ip));
                address.dict["dad_state"] = Value::num(u->DadState);
                addresses.array.push_back(address);
                if (target && a->OperStatus == IfOperStatusUp && linkLocal(ip) && ip.sin6_scope_id &&
                    u->DadState == IpDadStatePreferred) {
                    ++selected.candidates; selected.local = ip; selected.found = true;
                }
            }
            item.dict["ipv6_addresses"] = addresses; items.array.push_back(item);
        }
    }
    // Never choose between multiple usable addresses silently.
    if (selected.candidates != 1) selected.found = false;
    v.dict["items"] = items; FreeLibrary(lib); return v;
}
bool ready(SOCKET s, bool write, DWORD deadline) {
    LONG remaining = (LONG)(deadline - GetTickCount()); if (remaining <= 0) return false;
    fd_set f, e; FD_ZERO(&f); FD_SET(s, &f); FD_ZERO(&e); FD_SET(s, &e);
    timeval t = { remaining / 1000, (remaining % 1000) * 1000 };
    int n = select(0, write ? 0 : &f, write ? &f : 0, &e, &t);
    return n > 0 && FD_ISSET(s, &f) && !FD_ISSET(s, &e);
}
bool transfer(SOCKET s, char *p, int size, bool sending, bool tcp, DWORD deadline) {
    if (!tcp && !sending) {
        if (!ready(s, false, deadline)) return false;
        std::vector<char> packet(size + 1);
        int n = recv(s, &packet[0], size + 1, 0);
        if (n != size) return false;
        memcpy(p, &packet[0], size); return true;
    }
    int offset = 0;
    while (offset < size) {
        if (!ready(s, sending, deadline)) return false;
        int n = sending ? send(s, p + offset, size - offset, 0) : recv(s, p + offset, size - offset, 0);
        if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
        if (n <= 0 || (!tcp && n != size)) return false;
        offset += n;
    }
    return true;
}
Value exchange(const sockaddr_in6 &local, const sockaddr_in6 &peer, bool tcp) {
    Value v = Value::dictionary(); v.dict["ok"] = Value::flag(false);
    v.dict["source"] = Value::str(addressText(local)); v.dict["peer"] = Value::str(addressText(peer));
    Socket s(tcp ? SOCK_STREAM : SOCK_DGRAM);
    if (s.s == INVALID_SOCKET) { v.dict["socket_error"] = errorValue(WSAGetLastError()); return v; }
    if (bind(s.s, (const sockaddr *)&local, sizeof(local)) != 0) {
        v.dict["bind_error"] = errorValue(WSAGetLastError()); return v;
    }
    u_long nonblock = 1; if (ioctlsocket(s.s, FIONBIO, &nonblock)) return v;
    DWORD begin = GetTickCount(), deadline = begin + 2500;
    int rc = connect(s.s, (const sockaddr *)&peer, sizeof(peer));
    if (rc && WSAGetLastError() != WSAEWOULDBLOCK) {
        v.dict["connect_error"] = errorValue(WSAGetLastError()); return v;
    }
    if (rc && !ready(s.s, true, deadline)) { v.dict["stage"] = Value::str("connect_timeout_or_error"); return v; }
    int socketError = 0, len = sizeof(socketError);
    if (getsockopt(s.s, SOL_SOCKET, SO_ERROR, (char *)&socketError, &len) || socketError) {
        v.dict["connect_error"] = errorValue(socketError ? socketError : WSAGetLastError()); return v;
    }
    char sent[32] = "MRN-NET-TEST-V1", received[32] = {0};
    DWORD nonce = GetTickCount(); memcpy(sent + 20, &nonce, sizeof(nonce));
    bool ok = transfer(s.s, sent, sizeof(sent), true, tcp, deadline) &&
              transfer(s.s, received, sizeof(received), false, tcp, deadline) &&
              memcmp(sent, received, sizeof(sent)) == 0;
    v.dict["ok"] = Value::flag(ok); v.dict["elapsed_ms"] = Value::num(GetTickCount() - begin);
    v.dict["stage"] = Value::str(ok ? "echo_verified" : "echo_timeout_or_mismatch"); return v;
}
struct EchoWork { SOCKET s; bool tcp; bool ok; bool corrupt;
    EchoWork(SOCKET a, bool b, bool c) : s(a), tcp(b), ok(false), corrupt(c) {} };
DWORD WINAPI echoWorker(void *arg) {
    EchoWork &w = *(EchoWork *)arg; DWORD deadline = GetTickCount() + 4000;
    if (!ready(w.s, false, deadline)) return 0;
    if (w.tcp) {
        SOCKET s = accept(w.s, 0, 0); if (s == INVALID_SOCKET) return 0;
        u_long n = 1; ioctlsocket(s, FIONBIO, &n); char bytes[32];
        w.ok = transfer(s, bytes, sizeof(bytes), false, true, deadline) &&
               transfer(s, bytes, sizeof(bytes), true, true, deadline);
        closesocket(s);
    } else {
        char bytes[64]; sockaddr_in6 from; int len = sizeof(from);
        int n = recvfrom(w.s, bytes, sizeof(bytes), 0, (sockaddr *)&from, &len);
        if (n > 0 && w.corrupt) bytes[0] ^= 1;
        w.ok = n == 32 && sendto(w.s, bytes, n, 0, (sockaddr *)&from, len) == n;
    }
    return 0;
}
Value selftest(bool tcp, bool corrupt = false) {
    Socket server(tcp ? SOCK_STREAM : SOCK_DGRAM); require(server.s != INVALID_SOCKET, "IPv6 selftest socket failed");
    sockaddr_in6 local; ZeroMemory(&local, sizeof(local)); local.sin6_family = AF_INET6;
    local.sin6_addr.s6_addr[15] = 1;
    require(bind(server.s, (sockaddr *)&local, sizeof(local)) == 0, "IPv6 loopback bind failed");
    if (tcp) require(listen(server.s, 1) == 0, "Loopback listen failed");
    sockaddr_in6 peer = local; int len = sizeof(peer);
    require(getsockname(server.s, (sockaddr *)&peer, &len) == 0, "Loopback port unavailable");
    EchoWork work(server.s, tcp, corrupt); HANDLE thread = CreateThread(0, 0, echoWorker, &work, 0, 0);
    require(thread != 0, "Selftest thread failed"); Value v = exchange(local, peer, tcp);
    WaitForSingleObject(thread, INFINITE); CloseHandle(thread);
    v.dict["server_ok"] = Value::flag(work.ok); return v;
}
unsigned configNumber(const Value &c, const char *key, unsigned fallback, unsigned min, unsigned max) {
    if (!c.dict.count(key)) return fallback;
    const Value &v = c.at(key); require(v.type == Value::Integer && !v.negative && v.integer >= min && v.integer <= max,
                                      "Invalid numeric configuration"); return (unsigned)v.integer;
}
std::string configText(const Value &c, const char *key) {
    if (!c.dict.count(key)) return "";
    require(c.at(key).type == Value::String && c.at(key).string.size() < 256, "Invalid text configuration");
    return c.at(key).string;
}
}
int WINAPI NetworkTestMain(HINSTANCE, HINSTANCE, LPWSTR command, int) {
    started = GetTickCount(); int code = 0; bool winsock = false;
    HANDLE mutex = CreateMutexW(0, FALSE, L"MRN_NETWORK_TEST_V1");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) { if (mutex) CloseHandle(mutex); return 3; }
    try {
        std::wstring folder = executableDirectory();
        for (unsigned i = 1; i < 10000; ++i) {
            wchar_t name[64]; swprintf(name, L"\\nettest-%04u.log", i);
            logfile = CreateFileW((folder + name).c_str(), GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_NEW, 0, 0);
            if (logfile != INVALID_HANDLE_VALUE) {
                swprintf(name, L"\\nettest-%04u.plist", i); reportPath = folder + name; break;
            }
            if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) break;
        }
        require(logfile != INVALID_HANDLE_VALUE, "Cannot create report beside executable");
        log("BEGIN network-test-v1; no driver, registry, protocol binding or USB mode changes");
        report.dict["version"] = Value::str("network-test-v1");
        report.dict["configuration_changed"] = Value::flag(false);
        report.dict["carplay_verified"] = Value::flag(false);
        report.dict["physical_ncm_transfer_verified"] = Value::flag(false);
        report.dict["collection_completed"] = Value::flag(false);
        Value config = Value::dictionary(); bool test = std::wstring(command) == L"--selftest";
        if (!test && GetFileAttributesW((folder + L"\\network.plist").c_str()) != INVALID_FILE_ATTRIBUTES)
            config = parsePlist(readFile(folder + L"\\network.plist", 65536));
        require(config.type == Value::Dict, "Configuration must be a dictionary");
        unsigned seconds = configNumber(config, "seconds", test ? 1 : 30, 1, 120);
        unsigned port = configNumber(config, "peer_port", 47007, 1024, 65535);
        std::string target = configText(config, "adapter_name"), peerText = configText(config, "peer_address");
        sockaddr_in6 peer; ZeroMemory(&peer, sizeof(peer));
        if (!peerText.empty()) {
            require(!target.empty(), "Peer test requires explicit adapter_name");
            require(peerText.find('%') == std::string::npos, "Omit peer scope; selected local interface supplies it");
        }
        WSADATA wsa; int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
        report.dict["winsock_error"] = Value::num(rc); require(rc == 0, "Winsock startup failed"); winsock = true;
        if (!peerText.empty()) {
            std::wstring literal = wide(peerText); int len = sizeof(peer);
            require(WSAStringToAddressW(&literal[0], AF_INET6, 0, (sockaddr *)&peer, &len) == 0 && linkLocal(peer),
                    "Peer must be an IPv6 link-local literal"); peer.sin6_port = htons((u_short)port);
        }
        { Socket s(SOCK_DGRAM); report.dict["ipv6_socket_error"] = errorValue(s.s == INVALID_SOCKET ? WSAGetLastError() : 0); }
        Value reg = Value::dictionary();
        reg.dict["afd_stacks"] = registry(L"Comm\\AFD", L"Stacks");
        reg.dict["disabled_components"] = registry(L"Comm\\Tcpip6\\Parms", L"DisabledComponents");
        reg.dict["enable_dhcpv6"] = registry(L"Comm\\Tcpip6\\Parms", L"EnableDHCPv6");
        reg.dict["ncm_image"] = registry(L"Comm\\USBCDCNCM1", L"ImagePath");
        report.dict["registry"] = reg;
        Value usb = Value::dictionary();
        HANDLE cmx = CreateFileW(L"CMX1:", GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
        usb.dict["open_error"] = errorValue(cmx == INVALID_HANDLE_VALUE ? GetLastError() : 0);
        if (cmx != INVALID_HANDLE_VALUE) {
            WinXPlayUsbInfo info; ZeroMemory(&info, sizeof(info)); DWORD bytes = 0;
            log("USB_CONTROL_QUERY_BEGIN; no mode switch");
            BOOL ok = DeviceIoControl(cmx, WINXPLAY_USB_QUERY, 0, 0, &info, sizeof(info), &bytes, 0);
            usb.dict["query_error"] = errorValue(ok ? 0 : GetLastError());
            if (ok && bytes == sizeof(info)) {
                usb.dict["ncm_descriptor_present"] = Value::flag(info.ncmPresent != 0);
                usb.dict["vendor"] = Value::num(info.vendor); usb.dict["product"] = Value::num(info.product);
            }
            CloseHandle(cmx);
        }
        report.dict["usb_control"] = usb;
        report.dict["ndisconfig_present"] = Value::flag(GetFileAttributesW(L"\\Windows\\ndisconfig.exe") != INVALID_FILE_ATTRIBUTES);
        OSVERSIONINFO os; ZeroMemory(&os, sizeof(os)); os.dwOSVersionInfoSize = sizeof(os);
        if (GetVersionEx(&os)) { report.dict["os_major"] = Value::num(os.dwMajorVersion); report.dict["os_minor"] = Value::num(os.dwMinorVersion); }
        save();
        if (test) {
            report.dict["loopback_udp"] = selftest(false); report.dict["loopback_tcp"] = selftest(true);
            report.dict["loopback_corrupt"] = selftest(false, true);
            require(report.at("loopback_udp").at("ok").boolean && report.at("loopback_tcp").at("ok").boolean,
                    "Loopback transport selftest failed");
            require(!report.at("loopback_corrupt").at("ok").boolean && report.at("loopback_corrupt").at("server_ok").boolean,
                    "Corrupt echo was not rejected");
        }
        Value snapshots = Value::list(); DWORD until = GetTickCount() + seconds * 1000; bool echoed = false;
        do {
            Value sample = Value::dictionary(); sample.dict["elapsed_ms"] = Value::num(GetTickCount() - started);
            sample.dict["ndis"] = ndis(); Selected selected;
            log("ADAPTER_ADDRESS_QUERY_BEGIN"); sample.dict["ip_adapters"] = adapters(target, selected);
            sample.dict["selected_link_local_ready"] = Value::flag(selected.found);
            sample.dict["selected_address_candidates"] = Value::num(selected.candidates);
            log(selected.found ? "SELECTED_LINK_LOCAL_READY; peer traffic not yet proven" : "NO_UNIQUE_SELECTED_LINK_LOCAL");
            if (!peerText.empty() && selected.found && !echoed) {
                require(memcmp(&peer.sin6_addr, &selected.local.sin6_addr, sizeof(peer.sin6_addr)) != 0,
                        "Peer is the local address; physical transfer cannot be tested against self");
                peer.sin6_scope_id = selected.local.sin6_scope_id;
                log("EXPLICIT_PEER_ECHO_BEGIN"); sample.dict["udp_echo"] = exchange(selected.local, peer, false);
                sample.dict["tcp_echo"] = exchange(selected.local, peer, true); echoed = true;
                bool ok = sample.at("udp_echo").at("ok").boolean && sample.at("tcp_echo").at("ok").boolean;
                sample.dict["peer_echo_verified"] = Value::flag(ok);
                // Explicitly selected non-NCM adapters may also be tested: never mislabel them.
                bool ncm = _stricmp(imagePath(wide(target)).c_str(), "usbcdcncm.dll") == 0;
                report.dict["physical_ncm_transfer_verified"] = Value::flag(ok && ncm);
                if (!ok) code = 4;
            }
            snapshots.array.push_back(sample); report.dict["snapshots"] = snapshots; save();
            LONG left = (LONG)(until - GetTickCount()); if (left <= 0) break;
            Sleep(left > 2000 ? 2000 : left);
        } while ((LONG)(until - GetTickCount()) > 0);
        if (!peerText.empty() && !echoed) { report.dict["peer_test_skipped"] = Value::str("selected_adapter_not_ready"); code = 4; }
        report.dict["collection_completed"] = Value::flag(true);
        log("COLLECTION_COMPLETE; absence of NCM is a diagnostic result, not CarPlay success");
    } catch (const std::exception &e) {
        code = 1; report.dict["error"] = Value::str(e.what());
        if (logfile != INVALID_HANDLE_VALUE) { try { log(std::string("ERROR ") + e.what()); } catch (...) {} }
    }
    report.dict["exit_code"] = Value::num(code);
    try { if (!reportPath.empty()) save(); } catch (...) { code = 2; }
    if (winsock) WSACleanup();
    if (logfile != INVALID_HANDLE_VALUE) CloseHandle(logfile);
    CloseHandle(mutex); return code;
}
