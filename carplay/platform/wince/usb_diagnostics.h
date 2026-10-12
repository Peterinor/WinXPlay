#if WINXPLAY_ENABLE_LOGS
// Read-only snapshot accompanying the CMX open result; no OEM routing changes.
static Value usbRegistryValue(const std::wstring &path, const wchar_t *name) {
    Value v = Value::dictionary();
    HKEY key = 0;
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &key);
    v.dict["open_error"] = Value::num(rc);
    if (rc != ERROR_SUCCESS) return v;
    WCHAR data[512] = {0};
    DWORD type = 0, size = sizeof(data) - sizeof(WCHAR);
    rc = RegQueryValueExW(key, name, 0, &type, (BYTE *)data, &size);
    RegCloseKey(key);
    v.dict["query_error"] = Value::num(rc);
    v.dict["type"] = Value::num(type);
    if (rc == ERROR_SUCCESS) {
        if (type == REG_DWORD && size == sizeof(DWORD))
            v.dict["value"] = Value::num(*(DWORD *)data);
        else if (type == REG_SZ && size % sizeof(WCHAR) == 0)
            v.dict["value"] = Value::str(utf8(data));
    }
    return v;
}
static void recordUsbOpen(DWORD error) {
    static DWORD lastError = DWORD(-1), lastTime = 0;
    DWORD now = GetTickCount();
    if (error == lastError && DWORD(now - lastTime) < 30000) return;
    try {
        Value v = Value::dictionary();
        v.dict["uptime_ms"] = Value::num(now);
        v.dict["cmx_open_error"] = Value::num(error);
        v.dict["cmx_opened"] = Value::flag(error == 0);
        v.dict["winxplay_match_dll"] = usbRegistryValue(
            L"Drivers\\USB\\LoadClients\\1452\\Default\\255_254_2\\WinXPlayUsbMux", L"DLL");
        const wchar_t *apple = L"Drivers\\USB\\LoadClients\\1452\\Default\\Default";
        v.dict["oem_apple_driver"] = usbRegistryValue(apple, L"sDriver");
        v.dict["oem_apple_configuration"] = usbRegistryValue(apple, L"bConfigurationValue");
        HKEY traceKey=0;
        LONG traceResult=RegOpenKeyExW(HKEY_LOCAL_MACHINE,WINXPLAY_USB_DIAGNOSTICS_KEY,0,KEY_READ,&traceKey);
        v.dict["driver_trace_registry_open_error"]=Value::num(traceResult);
        if(traceResult==ERROR_SUCCESS){
            Value trace=Value::dictionary();DWORD index=0;
            for(;index<64;++index){
                WCHAR name[128]={0};DWORD count=127;
                traceResult=RegEnumValueW(traceKey,index,name,&count,0,0,0,0);
                if(traceResult!=ERROR_SUCCESS)break;
                trace.dict[utf8(name)]=usbRegistryValue(WINXPLAY_USB_DIAGNOSTICS_KEY,name);
            }
            RegCloseKey(traceKey);
            v.dict["driver_registry_trace"]=trace;
            v.dict["driver_trace_registry_enum_result"]=Value::num(traceResult);
            v.dict["driver_trace_registry_truncated"]=Value::flag(index==64);
        }
        HKEY active = 0;
        LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Drivers\\Active", 0, KEY_READ, &active);
        v.dict["active_open_error"] = Value::num(rc);
        if (rc == ERROR_SUCCESS) {
            Value entries = Value::dictionary();
            DWORD index = 0;
            for (; index < 256; ++index) {
                WCHAR name[128]; DWORD count = 128;
                rc = RegEnumKeyExW(active, index, name, &count, 0, 0, 0, 0);
                if (rc != ERROR_SUCCESS) break;
                std::wstring path = std::wstring(L"Drivers\\Active\\") + name;
                Value entry = Value::dictionary();
                entry.dict["name"] = usbRegistryValue(path, L"Name");
                entry.dict["key"] = usbRegistryValue(path, L"Key");
                entries.dict[utf8(name)] = entry;
            }
            RegCloseKey(active);
            v.dict["active_enum_result"] = Value::num(rc);
            v.dict["active_truncated"] = Value::flag(index == 256);
            v.dict["active_drivers"] = entries;
        }
        v.dict["driver_trace_path"] = Value::str(utf8(executableDirectory() + L"\\usb-driver.log"));
        writeDiagnosticFile(executableDirectory() + L"/usb-connection.plist", xmlPlist(v));
        lastError = error; lastTime = now;
    } catch (...) {} // Diagnostics must not stop connection/reconnection.
}

#else
static void recordUsbOpen(DWORD){}
#endif
