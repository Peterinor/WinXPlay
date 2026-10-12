#include "../common/native_services.h"
#include "../common/native_crypto.h"
#include "../common/diagnostics.h"
#include <wincrypt.h>
#include <mmsystem.h>
#include <sstream>
#include <algorithm>
namespace cp {
#if WINXPLAY_ENABLE_LOGS && (defined(UNDER_CE) || defined(WINXPLAY_DIAGNOSTICS_TEST))
#include "diagnostics.inc"
#endif
static void storageCheck(bool ok,const char *stage,DWORD error){
    if(ok)return;
    std::ostringstream s;s<<stage<<" win32="<<error;
    diagnosticEvent("STORAGE_ERROR",s.str());throw std::runtime_error(s.str());
}
static std::wstring nativePath(std::wstring p) {
    std::replace(p.begin(), p.end(), L'/', L'\\');
    return p;
}
#ifndef UNDER_CE
#include "../windows/usbmux_service.inc"
#include "../windows/desktop_media.inc"
#endif
std::wstring wide(const std::string &s) {
    if (s.empty())
        return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), s.size(), 0, 0);
    require(n > 0, "UTF8 conversion");
    std::wstring out(n, L' ');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), s.size(), &out[0], n);
    return out;
}
std::string utf8(const std::wstring &s) {
    if (s.empty())
        return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), s.size(), 0, 0, 0, 0);
    require(n > 0, "UTF8 conversion");
    std::string out(n, ' ');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), s.size(), &out[0], n, 0, 0);
    return out;
}
Bytes readFile(const std::wstring &path, size_t limit) {
    HANDLE f = CreateFileW(nativePath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, 0,
                           OPEN_EXISTING, 0, 0);
    storageCheck(f != INVALID_HANDLE_VALUE, "Cannot open file",GetLastError());
    DWORD high = 0, size = GetFileSize(f, &high);
    if (high || size > limit) {
        CloseHandle(f);
        require(false, "File exceeds limit");
    }
    Bytes b(size);
    DWORD count = 0;
    BOOL ok = b.empty() || ReadFile(f, &b[0], size, &count, 0);
    DWORD error=ok?0:GetLastError();CloseHandle(f);
    storageCheck(ok && (b.empty() || count == size), "Short read",error);
    return b;
}
void writeFile(const std::wstring &path, const Bytes &b) {
    HANDLE f = CreateFileW(nativePath(path).c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    storageCheck(f != INVALID_HANDLE_VALUE, "Cannot create file",GetLastError());
    DWORD count = 0;
    BOOL ok = b.empty() || WriteFile(f, &b[0], b.size(), &count, 0);
    DWORD error=ok?0:GetLastError();CloseHandle(f);
    storageCheck(ok && (b.empty() || count == b.size()), "Short write",error);
}
std::wstring executableDirectory() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(0, path, MAX_PATH);
    require(n && n < MAX_PATH, "Application path");
    std::wstring s(path, n);
    return s.substr(0, s.find_last_of(L"\\/"));
}
std::wstring environment(const wchar_t *key, const wchar_t *fallback) {
#ifndef UNDER_CE
    wchar_t value[2048];
    DWORD n = GetEnvironmentVariableW(key, value, 2048);
    if (n && n < 2048)
        return value;
#endif
    try {
        Value cfg = parsePlist(readFile(executableDirectory() + L"/carplay.plist", 65536));
        std::string k = utf8(key);
        if (cfg.dict.count(k) && cfg.at(k).type == Value::String)
            return wide(cfg.at(k).string);
    } catch (const std::exception &) {
    }
    if (std::wstring(key) == L"CARPLAY_IDENTITY")
        return executableDirectory() + L"\\identity";
    return nativePath(fallback);
}
Bytes protectedRead(const std::wstring &original) {
    std::wstring path = nativePath(original);
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((path + L".bak").c_str()) != INVALID_FILE_ATTRIBUTES)
        require(MoveFileW((path + L".bak").c_str(), path.c_str()) != FALSE,
                "Cannot recover pairing store");
    Bytes b = readFile(path, 1024 * 1024);
    require(!b.empty(), "Empty pairing store");
    DATA_BLOB in = {b.size(), &b[0]}, out = {0, 0};
    BOOL ok=CryptUnprotectData(&in, 0, 0, 0, 0, CRYPTPROTECT_UI_FORBIDDEN, &out);
    storageCheck(ok!=FALSE,"Cannot decrypt pairing store",GetLastError());
    Bytes plain(out.pbData, out.pbData + out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return plain;
}
void protectedWrite(const std::wstring &original, const Bytes &b) {
    std::wstring path = nativePath(original);
    require(!b.empty(), "Empty pairing store");
    DATA_BLOB in = {b.size(), const_cast<U8 *>(&b[0])}, out = {0, 0};
    BOOL ok=CryptProtectData(&in, L"WinXPlay", 0, 0, 0, CRYPTPROTECT_UI_FORBIDDEN, &out);
    storageCheck(ok!=FALSE,"Cannot encrypt pairing store",GetLastError());
    Bytes sealed(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    writeFile(path + L".tmp", sealed);
    // Keep a recoverable prior encrypted record across a power loss during replacement.
    DeleteFileW((path + L".bak").c_str());
    MoveFileW(path.c_str(), (path + L".bak").c_str());
    if (!MoveFileW((path + L".tmp").c_str(), path.c_str())) {
        DWORD error=GetLastError();
        MoveFileW((path + L".bak").c_str(), path.c_str());
        storageCheck(false, "Cannot commit pairing store",error);
    }
}
} // namespace cp
