#include "../../common/diagnostic_support.h"
static BOOL RecordEntry(const DiagnosticOptions*) {
    DiagLog(L"USB_ENTRY_REACHED_NO_WINDOW_NO_CONFIGURATION");
    return DiagLogOK();
}
extern "C" void UsbStartupCheckEntry() {
    RunDiagnostic(L"startup_probe",RecordEntry,FALSE,0);
}
