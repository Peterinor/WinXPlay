#ifndef CP_AUDIO_RECOVERY_H
#define CP_AUDIO_RECOVERY_H
#include "media_backend.h"
namespace cp {
typedef AudioOutputBackend *(*AudioOutputFactory)(unsigned rate,unsigned channels);
AudioOutputBackend *createRecoveringOutput(unsigned rate,unsigned channels,
    AudioOutputFactory factory,unsigned retryMs,const char *backendName);
}
#endif
