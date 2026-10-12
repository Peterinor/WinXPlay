#pragma once
#include <windows.h>

struct DiagnosticOptions { DWORD delayMs; DWORD durationMs; DWORD showTestWindow; };
void DiagLog(const WCHAR* text);
void DiagValues(const WCHAR* label,DWORD a,DWORD b=0,DWORD c=0);
BOOL DiagLogOK();
// Each executable supplies one fixed function. There is no runtime tool selection.
typedef BOOL (*DiagnosticFunction)(const DiagnosticOptions* options);
void RunDiagnostic(const WCHAR* executableName,DiagnosticFunction function,
                   BOOL readOptions,DWORD defaultDelayMs,BOOL allowTestWindow=FALSE);
extern "C" __declspec(dllexport) BOOL ParseDiagnosticOptions(const BYTE* bytes,DWORD length,DiagnosticOptions* out,BOOL allowTestWindow);
