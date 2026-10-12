#include <windows.h>

// 此程序仅验证模拟器中的真实 CE API，不用于推断 MRN 硬件兼容性。
static HANDLE g_log = INVALID_HANDLE_VALUE;
static bool g_logOk = true;
static void Text(const WCHAR* value) {
    DWORD count = lstrlenW(value) * sizeof(WCHAR), written = 0;
    if (!WriteFile(g_log, value, count, &written, 0) || written != count) g_logOk = false;
}
static void Number(const WCHAR* name, DWORD value) {
    WCHAR digits[16];
    int i = 15;
    digits[i] = 0;
    do { digits[--i] = L'0' + value % 10; value /= 10; } while (value && i > 0);
    Text(name); Text(digits+i); Text(L"\r\n");
}

extern "C" void SmokeEntry() {
    g_log = CreateFileW(L"\\Storage Card\\CE_ARM_SMOKE_LOG.txt", GENERIC_WRITE,
        FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (g_log == INVALID_HANDLE_VALUE) {
        MessageBoxW(0,L"Cannot create shared log.",L"CE ARM smoke: FAIL",MB_OK);
        ExitThread(1);
    }
    Text(L"\xfeff" L"CE ARM smoke v1: real OS execution\r\n");
    OSVERSIONINFO version = {0};
    version.dwOSVersionInfoSize = sizeof(version);
    BOOL versionOk = GetVersionEx(&version);
    Number(L"GetVersionEx success: ",versionOk);
    Number(L"OS major: ",version.dwMajorVersion);
    Number(L"OS minor: ",version.dwMinorVersion);
    Number(L"OS build: ",version.dwBuildNumber);
    SYSTEM_INFO info = {0};
    GetSystemInfo(&info);
    Number(L"Processor architecture: ",info.wProcessorArchitecture);
    Number(L"Processor type: ",info.dwProcessorType);
    MEMORYSTATUS memory = {0}; memory.dwLength = sizeof(memory);
    GlobalMemoryStatus(&memory);
    Number(L"Total physical memory bytes: ",memory.dwTotalPhys);
    Number(L"Available physical memory bytes: ",memory.dwAvailPhys);

    // 单独记录刷新结果；读回成功和持久化刷新成功是两个不同的保证。
    const char expected[] = "CE-ARM-IO-20261008";
    HANDLE file = CreateFileW(L"\\Storage Card\\CE_ARM_SMOKE_IO.bin", GENERIC_WRITE,
        FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    DWORD written=0, writeError=0, flushError=0;
    BOOL writeOk=FALSE, flushOk=FALSE, closeOk=FALSE;
    if (file != INVALID_HANDLE_VALUE) {
        writeOk=WriteFile(file,expected,sizeof(expected),&written,0);
        if (!writeOk) writeError=GetLastError();
        flushOk=FlushFileBuffers(file);
        if (!flushOk) flushError=GetLastError();
        closeOk=CloseHandle(file);
    } else { writeError=GetLastError(); }
    Number(L"WriteFile success: ",writeOk);
    Number(L"WriteFile error: ",writeError);
    Number(L"WriteFile bytes: ",written);
    Number(L"FlushFileBuffers success: ",flushOk);
    Number(L"FlushFileBuffers error: ",flushError);
    Number(L"CloseHandle success: ",closeOk);
    file=CreateFileW(L"\\Storage Card\\CE_ARM_SMOKE_IO.bin",GENERIC_READ,
        FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    char actual[sizeof(expected)]={0}; DWORD read=0;
    bool readbackOk=false;
    if (file != INVALID_HANDLE_VALUE) {
        readbackOk=ReadFile(file,actual,sizeof(actual),&read,0) && read==sizeof(expected);
        for (DWORD i=0; i<read && i<sizeof(expected); ++i)
            if (actual[i]!=expected[i]) readbackOk=false;
        if (!CloseHandle(file)) readbackOk=false;
    }
    Number(L"Readback match: ",readbackOk);
    bool passed=versionOk && info.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_ARM
        && writeOk && written==sizeof(expected) && closeOk && readbackOk;
    Text(passed ? L"APP_LOAD_AND_IO: PASS\r\n" : L"APP_LOAD_AND_IO: FAIL\r\n");
    Text(L"Flush result is separate; no durability or MRN compatibility claim.\r\n");
    if (!CloseHandle(g_log)) g_logOk=false;
    MessageBoxW(0, passed && g_logOk
        ? L"Real CE ARM app loaded.\nFile write + reopen + readback passed.\nSee CE_ARM_SMOKE_LOG.txt for OS and flush status."
        : L"A check failed. See CE_ARM_SMOKE_LOG.txt.",
        passed && g_logOk ? L"CE ARM smoke: PASS" : L"CE ARM smoke: FAIL", MB_OK|MB_SETFOREGROUND);
    ExitThread(passed && g_logOk ? 0 : 1);
}
