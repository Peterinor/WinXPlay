#ifndef CP_CE_DSHOW_H
#define CP_CE_DSHOW_H
#include "../common/native_services.h"
#include "../../interfaces/media.h"
namespace cp {
VideoPipeline *createCeSystemVideo(HWND owner, unsigned width, unsigned height);
#ifdef CARPLAY_DSHOW_TEST
VideoPipeline *createCeRgbTestVideo(HWND owner, unsigned width, unsigned height);
#endif
}
#endif
