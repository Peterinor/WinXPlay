#ifndef CP_MF_VIDEO_H
#define CP_MF_VIDEO_H
#include <windows.h>
// A small C boundary keeps modern Windows SDK declarations out of the shared
// C++03/CE build. This object is linked into the EXE, not loaded as a plugin.
struct CpMfStats {
    unsigned frames, gpuSamples, decoderSurfaces, cpuSamples, submitted, queuedBytes;
    unsigned width, height, maxQueueBytes;
    unsigned __int64 totalPresentUs, totalQueueUs, maxQueueUs;
    HRESULT error;
    wchar_t stage[80], adapter[128];
};
typedef void (*CpMfFrame)(void *);
extern "C" {
HRESULT cpMfCreate(HWND window, unsigned width, unsigned height, CpMfFrame callback,
                   void *context, void **handle);
HRESULT cpMfSubmit(void *handle, const unsigned char *data, unsigned size);
void cpMfStats(void *handle, CpMfStats *stats);
HRESULT cpMfFinish(void *handle);
void cpMfDestroy(void *handle);
}
#endif
