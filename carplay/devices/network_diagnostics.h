// Included after the CE registry diagnostic helpers, inside cp's private namespace.
#if defined(WINXPLAY_DEVICE_MRN) && defined(UNDER_CE) && WINXPLAY_ENABLE_LOGS
#include "mrn/network_diagnostics.h"
#else
static void recordDeviceNetworkState(){}
#endif
