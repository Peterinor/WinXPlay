#ifndef CP_MRN_AUDIO_OUTPUT_H
#define CP_MRN_AUDIO_OUTPUT_H
#include "../../platform/wince/media_backend.h"
namespace cp {
AudioOutputBackend *createMrnTtsOutput(unsigned, unsigned);
AudioOutputBackend *createMrnRecoveringOutput(unsigned, unsigned);
}
#endif
