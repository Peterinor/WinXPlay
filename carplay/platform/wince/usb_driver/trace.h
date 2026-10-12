// Lifecycle and bounded transfer metadata only. Never dump payloads or log from DllMain.
#ifndef WINXPLAY_ENABLE_LOGS
#define WINXPLAY_ENABLE_LOGS 1
#endif
#if WINXPLAY_ENABLE_LOGS
static char *traceHex(char *p, DWORD n) {
    const char *digits = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4) *p++ = digits[(n >> shift) & 15];
    return p;
}
static void usbTrace(const char *stage, DWORD a = 0, DWORD b = 0, DWORD c = 0) {
    DWORD saved = GetLastError();
    char line[160], *p = line;
    p = traceHex(p, GetTickCount()); *p++ = ' ';
    for (unsigned i = 0; stage[i] && i < 64; ++i) *p++ = stage[i];
    *p++ = ' '; p = traceHex(p, a);
    *p++ = ' '; p = traceHex(p, b);
    *p++ = ' '; p = traceHex(p, c);
    *p++ = '\r'; *p++ = '\n';
    // Independent of the driver's filesystem/module-path access. Each fixed
    // stage keeps its latest record until the next receiver start; no flash flush.
    HKEY key=0;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,WINXPLAY_USB_DIAGNOSTICS_KEY,0,KEY_SET_VALUE,&key)==ERROR_SUCCESS){
        WCHAR name[65],text[160];unsigned n=0;
        while(n<64 && stage[n] && stage[n]!=' '){name[n]=(WCHAR)stage[n];++n;}name[n]=0;
        unsigned count=unsigned(p-line);
        for(unsigned j=0;j<count;++j)text[j]=(WCHAR)line[j];text[count]=0;
        RegSetValueExW(key,name,0,REG_SZ,(const BYTE*)text,(count+1)*sizeof(WCHAR));
        RegCloseKey(key);
    }
    WCHAR path[MAX_PATH];
    DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length && length < MAX_PATH) {
        while (length && path[length - 1] != L'\\' && path[length - 1] != L'/') --length;
        const WCHAR leaf[] = L"usb-driver.log";
        if (length && length + sizeof(leaf) / sizeof(WCHAR) <= MAX_PATH) {
            memcpy(path + length, leaf, sizeof(leaf));
            HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
            if (file != INVALID_HANDLE_VALUE) {
                DWORD size = GetFileSize(file, 0);
                // Keep the first failure even when a device reconnects repeatedly.
                if (size != INVALID_FILE_SIZE && size < 128 * 1024) {
                    DWORD written;
                    if (SetFilePointer(file, 0, 0, FILE_END) != INVALID_SET_FILE_POINTER)
                        WriteFile(file, line, DWORD(p - line), &written, 0);
                }
                CloseHandle(file);
            }
        }
    }
    SetLastError(saved);
}
#else
static void usbTrace(const char *,DWORD=0,DWORD=0,DWORD=0){}
#endif
