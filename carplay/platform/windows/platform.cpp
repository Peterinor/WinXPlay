#include "../common/native_services.h"
#include <mmsystem.h>
#include <sstream>
namespace cp {
std::wstring wide(const std::string &s) {
    if (s.empty())
        return L"";
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), 0, 0);
    require(n > 0, "Invalid UTF8");
    std::wstring w(n, L' ');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
    return w;
}
std::string utf8(const std::wstring &s) {
    if (s.empty())
        return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), 0, 0, 0, 0);
    std::string out(n, ' ');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), &out[0], n, 0, 0);
    return out;
}
Bytes readFile(const std::wstring &p, size_t limit) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    require(h != INVALID_HANDLE_VALUE, "Cannot open input file");
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || U64(size.QuadPart) > limit) {
        CloseHandle(h);
        require(false, "Input file too large");
    }
    Bytes b(size_t(size.QuadPart));
    DWORD count = 0;
    BOOL ok = b.empty() || ReadFile(h, &b[0], DWORD(b.size()), &count, 0);
    CloseHandle(h);
    require(ok && (b.empty() || count == b.size()), "Short file read");
    return b;
}
void writeFile(const std::wstring &p, const Bytes &b) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    require(h != INVALID_HANDLE_VALUE, "Cannot create file");
    DWORD n = 0;
    BOOL ok = b.empty() || WriteFile(h, &b[0], DWORD(b.size()), &n, 0);
    CloseHandle(h);
    require(ok && (b.empty() || n == b.size()), "Short file write");
}
std::wstring executableDirectory() {
    wchar_t b[32768];
    DWORD n = GetModuleFileNameW(0, b, 32768);
    require(n > 0 && n < 32768, "Executable path unavailable");
    std::wstring s(b, n);
    return s.substr(0, s.find_last_of(L"\\/"));
}
std::wstring environment(const wchar_t *key, const wchar_t *fallback) {
    wchar_t b[32768];
    DWORD n = GetEnvironmentVariableW(key, b, 32768);
    if (n > 0 && n < 32768)
        return std::wstring(b, n);
    const std::wstring name(key);
    if (name == L"CARPLAY_CRYPTO")
        return executableDirectory() + L"\\libcrypto-3-x64.dll";
    if (name == L"CARPLAY_SSL")
        return executableDirectory() + L"\\libssl-3-x64.dll";
    if (name == L"CARPLAY_IDENTITY") {
        const std::wstring path = executableDirectory() + L"\\identity";
        DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
            return path;
    }
    return std::wstring(fallback);
}
#include "usbmux_service.inc"
#include "desktop_media.inc"
} // namespace cp
