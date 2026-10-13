#ifndef CP_MRN_AUDIO_ROUTE_H
#define CP_MRN_AUDIO_ROUTE_H
#include "../../platform/wince/media_backend.h"
namespace cp {
AudioOutputBackend *createMrnAudioOutput(unsigned rate,unsigned channels);
}
#endif
