#ifndef CP_MF_VIDEO_ADAPTER_H
#define CP_MF_VIDEO_ADAPTER_H
#include "../common/native_services.h"
#include "../../interfaces/media.h"
namespace cp {
VideoPipeline *createSystemVideo(HWND, VideoCallback, void *, unsigned, unsigned, unsigned maxFPS);
}
#endif
