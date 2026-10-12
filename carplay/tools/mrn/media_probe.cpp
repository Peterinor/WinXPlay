#include "../../platform/common/native_services.h"
#include <dshow.h>
#include <ddraw.h>
using namespace cp;
static std::string hexResult(HRESULT hr) {
    char b[16];
    sprintf(b, "0x%08lx", (unsigned long)hr);
    return b;
}
static std::string guidText(const GUID &id) {
    wchar_t s[64];
    return StringFromGUID2(id, s, 64) > 0 ? utf8(s) : "";
}
static Value filter(const wchar_t *id, bool instantiate) {
    Value v = Value::dictionary();
    v.dict["clsid"] = Value::str(utf8(id));
    HKEY key = 0;
    std::wstring path = L"CLSID\\";
    path += id;
    path += L"\\InprocServer32";
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, path.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t name[512] = {0};
        DWORD bytes = sizeof(name), type = 0;
        if (RegQueryValueExW(key, 0, 0, &type, (BYTE *)name, &bytes) == ERROR_SUCCESS &&
            type == REG_SZ && bytes < sizeof(name))
            v.dict["server"] = Value::str(utf8(name));
        RegCloseKey(key);
    }
    v.dict["registered"] = Value::flag(v.dict.count("server") != 0);
    if (!instantiate)
        return v;
    CLSID cls;
    HRESULT hr = CLSIDFromString(const_cast<wchar_t *>(id), &cls);
    IBaseFilter *f = 0;
    if (SUCCEEDED(hr))
        hr = CoCreateInstance(cls, 0, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void **)&f);
    v.dict["create_result"] = Value::str(hexResult(hr));
    if (!f)
        return v;
    IEnumPins *enumerator = 0;
    Value pins = Value::list();
    if (SUCCEEDED(f->EnumPins(&enumerator))) {
        IPin *pin = 0;
        unsigned count = 0;
        while (count++ < 16 && enumerator->Next(1, &pin, 0) == S_OK) {
            Value p = Value::dictionary();
            PIN_DIRECTION direction;
            if (SUCCEEDED(pin->QueryDirection(&direction)))
                p.dict["direction"] = Value::str(direction == PINDIR_INPUT ? "input" : "output");
            IEnumMediaTypes *types = 0;
            Value formats = Value::list();
            if (SUCCEEDED(pin->EnumMediaTypes(&types))) {
                AM_MEDIA_TYPE *mt = 0;
                unsigned n = 0;
                while (n++ < 64 && types->Next(1, &mt, 0) == S_OK) {
                    Value format = Value::dictionary();
                    format.dict["major"] = Value::str(guidText(mt->majortype));
                    format.dict["subtype"] = Value::str(guidText(mt->subtype));
                    format.dict["format"] = Value::str(guidText(mt->formattype));
                    formats.array.push_back(format);
                    if (mt->pbFormat)
                        CoTaskMemFree(mt->pbFormat);
                    if (mt->pUnk)
                        mt->pUnk->Release();
                    CoTaskMemFree(mt);
                }
                types->Release();
            }
            p.dict["types"] = formats;
            pins.array.push_back(p);
            pin->Release();
        }
        enumerator->Release();
    }
    f->Release();
    v.dict["pins"] = pins;
    return v;
}
int WINAPI MediaProbeMain(HINSTANCE, HINSTANCE, LPWSTR command, int) {
    Value report = Value::dictionary();
    HRESULT initialized = CoInitializeEx(0, COINIT_MULTITHREADED);
    report.dict["com_result"] = Value::str(hexResult(initialized));
    try {
        require(SUCCEEDED(initialized), "COM initialization failed");
        IGraphBuilder *graph = 0;
        HRESULT hr = CoCreateInstance(CLSID_FilterGraph, 0, CLSCTX_INPROC_SERVER, IID_IGraphBuilder,
                                      (void **)&graph);
        report.dict["directshow_graph_result"] = Value::str(hexResult(hr));
        if (graph)
            graph->Release();
        const wchar_t *ids[] = {
            L"{3d581965-7c6e-4fe9-894f-50fc821bae68}", L"{16CFBFB7-BA99-4621-8E4D-36CED1503234}",
            L"{DEA9CDC2-D56A-4051-A0DD-3ACE9E44736F}", L"{70e102b0-5556-11ce-97c0-00aa0055595a}"};
        Value filters = Value::list();
        bool active = std::wstring(command) == L"--instantiate";
        for (unsigned i = 0; i < 4; ++i)
            filters.array.push_back(filter(ids[i], active));
        report.dict["filters"] = filters;
        report.dict["filters_instantiated"] = Value::flag(active);
        HMODULE dll = LoadLibraryW(L"ddraw.dll");
        report.dict["directdraw_dll"] = Value::flag(dll != 0);
        if (dll) {
            typedef HRESULT(WINAPI * Create)(GUID *, LPDIRECTDRAW *, IUnknown *);
            Create create = (Create)GetProcAddress(dll, L"DirectDrawCreate");
            LPDIRECTDRAW draw = 0;
            hr = create ? create(0, &draw, 0) : E_NOINTERFACE;
            report.dict["directdraw_create_result"] = Value::str(hexResult(hr));
            if (draw) {
                DDCAPS caps;
                ZeroMemory(&caps, sizeof(caps));
                caps.dwSize = sizeof(caps);
                hr = draw->GetCaps(&caps, 0);
                report.dict["directdraw_caps_result"] = Value::str(hexResult(hr));
                if (SUCCEEDED(hr)) {
                    report.dict["video_memory_total"] = Value::num(caps.dwVidMemTotal);
                    report.dict["caps"] = Value::num(caps.ddsCaps.dwCaps);
                    report.dict["overlay_caps"] = Value::num(caps.dwOverlayCaps);
                }
                DWORD n = 0;
                if (SUCCEEDED(draw->GetFourCCCodes(&n, 0)) && n <= 256) {
                    std::vector<DWORD> codes(n);
                    if (n && SUCCEEDED(draw->GetFourCCCodes(&n, &codes[0])) && n <= codes.size()) {
                        Value formats = Value::list();
                        for (unsigned i = 0; i < n; ++i)
                            formats.array.push_back(Value::num(codes[i]));
                        report.dict["fourcc_codes"] = formats;
                    }
                }
                draw->Release();
            }
            FreeLibrary(dll);
        }
        report.dict["probe_completed"] = Value::flag(true);
        report.dict["hardware_decode_verified"] = Value::flag(false);
    } catch (const std::exception &e) {
        report.dict["error"] = Value::str(e.what());
    }
    if (SUCCEEDED(initialized))
        CoUninitialize();
    try {
        writeFile(executableDirectory() + L"/media-probe.plist", xmlPlist(report));
    } catch (...) {
        return 2;
    }
    return report.dict.count("error") ? 1 : 0;
}
