// Build-time USB registration policy. Generic CE only uses the system API.
#ifdef WINXPLAY_DEVICE_MRN
#include "mrn/usb_registration.h"
#else
static BOOL deviceUsbRegistrationFallback(FARPROC,FARPROC){return FALSE;}
#endif
