#ifndef WINXPLAY_DIAGNOSTICS_H
#define WINXPLAY_DIAGNOSTICS_H
#include "native_services.h"
namespace cp {
#ifdef UNDER_CE
#if WINXPLAY_ENABLE_LOGS
void initializeCrashDiagnostics(const wchar_t *directory);
LONG diagnosticException(EXCEPTION_POINTERS *exception,const wchar_t *boundary);
#else
inline void initializeCrashDiagnostics(const wchar_t *){}
inline LONG diagnosticException(EXCEPTION_POINTERS *,const wchar_t *){return EXCEPTION_CONTINUE_SEARCH;}
#endif
#endif
// Pass only stage names, counters and errors; never protocol bodies or pairing material.
#if WINXPLAY_ENABLE_LOGS && (defined(UNDER_CE) || defined(WINXPLAY_DIAGNOSTICS_TEST))
void diagnosticEvent(const char *event, const std::string &detail = "");
#else
inline void diagnosticEvent(const char *, const std::string & = "") {}
#endif
}
#endif
