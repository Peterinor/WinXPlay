#ifndef CP_MEDIA_H
#define CP_MEDIA_H
#include "../core/protocol.h"
namespace cp {
struct MediaCapabilities {
    bool aac, bufferedAac, audioOutput, microphone, touch, controls;
    std::string model, displayName;
    std::string deviceId, serialNumber, displayUuid;
    MediaCapabilities() : aac(false), bufferedAac(false), audioOutput(true), microphone(true),
                          touch(true), controls(true), model("CarPlayReceiver1,1"), displayName("WinXPlay") {}
};
class VideoPipeline {
  public:
    virtual ~VideoPipeline() {}
    // Annex-B data is consumed/copied before return. PTS is the raw protocol
    // timestamp (zero when unavailable); legacy backends decode immediately
    // and do not schedule by PTS.
    virtual void submit(const Bytes &, U64 pts) = 0;
    virtual void finish() = 0;
    virtual unsigned frames() const = 0;
    // Async backends surface worker/decoder failures even when no new frame arrives.
    virtual void checkStatus() const {}
};
class AudioOutput {
  public:
    virtual ~AudioOutput() {}
    // Negotiated payload: network-endian PCM16 or encoded AAC. The selected
    // adapter handles conversion; callers may reuse the buffer after return.
    virtual void receive(const Bytes &) = 0;
    virtual unsigned playedBytes() const = 0;
    virtual void pause(bool) = 0;
};
class AudioInput {
  public:
    virtual ~AudioInput() {}
    // Owned little-endian PCM buffers, not borrowed device memory.
    virtual std::vector<Bytes> poll() = 0;
};
class MediaFactory {
  public:
    virtual ~MediaFactory() {}
    // Returned objects belong to the caller. Factory outlives every object.
    virtual MediaCapabilities capabilities() const = 0;
    virtual VideoPipeline *video(unsigned width, unsigned height) = 0;
    virtual AudioOutput *audio(unsigned rate, unsigned channels, bool aac) = 0;
    virtual AudioInput *microphone(unsigned rate, unsigned channels, unsigned samples) = 0;
};
} // namespace cp
#endif
