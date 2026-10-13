#ifndef CP_WAVE_MIXER_H
#define CP_WAVE_MIXER_H
#include "media_backend.h"
namespace cp {
// Optional device route, owned by the mixer. release() must tolerate partial
// acquisition and must not throw. It runs after waveOut buffers are released.
class WaveAudioRoute {
  public:
    virtual ~WaveAudioRoute() {}
    virtual void acquire() = 0;
    virtual void release() = 0;
};
// This backend mixes logical streams to PCM16 / 48 kHz / stereo. A device
// selects it explicitly; it does not replace the standard wave backend.
struct WaveOutputConfig {
    unsigned deviceId, bufferCount, framesPerBuffer, settleMs;
    const char *name;
    WaveAudioRoute *(*createRoute)();
};
// One build-selected physical output is shared by all streams. Keep configuration
// immutable and alive for the process lifetime, including retained buffers.
AudioOutputBackend *createSharedWaveOutput(unsigned rate,unsigned channels,const WaveOutputConfig &policy);
}
#endif
