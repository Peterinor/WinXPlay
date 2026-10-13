#ifndef CP_WINCE_PORTABLE_H
#define CP_WINCE_PORTABLE_H
#include <string>
namespace cp { void preparePortableReceiver();
void prepareWinceConnection(void (*status)(void *,const std::string &)); }
#endif
