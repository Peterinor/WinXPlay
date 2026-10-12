#ifndef MRN_MEDIA_BACKEND_H
#define MRN_MEDIA_BACKEND_H
#include "../common/native_services.h"
#include "../common/frame.h"
namespace cp {
class VideoBackend {
  public:
    virtual ~VideoBackend() {}
    virtual void feed(const Bytes &annexB) = 0;
    virtual void finish() = 0;
    virtual unsigned frames() const = 0;
};
class AudioOutputBackend {
  public:
    virtual ~AudioOutputBackend() {}
    virtual void feed(const Bytes &pcm) = 0;
    virtual void reap() = 0;
    virtual unsigned playedBytes() const = 0;
    virtual void pause(bool) = 0;
};
class AudioInputBackend {
  public:
    virtual ~AudioInputBackend() {}
    virtual std::vector<Bytes> poll() = 0;
};
class FramePresenter {
  public:
    virtual ~FramePresenter() {}
    virtual void paint(HDC target, const RECT &destination, const DecodedFrame &frame) = 0;
};
// Selection is explicit. Missing hardware is an error, never proof of hardware decoding.
VideoBackend *createVideoBackend(const std::wstring &name, FrameSink sink, void *context,
                                 unsigned width, unsigned height);
AudioOutputBackend *createAudioOutputBackend(const std::wstring &name, unsigned rate,
                                             unsigned channels);
AudioInputBackend *createAudioInputBackend(const std::wstring &name, unsigned rate,
                                           unsigned channels, unsigned samples);
FramePresenter *createFramePresenter(const std::wstring &name);
Value mediaBackendConfiguration();
// Implementations are separate compilation units. The registry alone binds names to factories.
AudioOutputBackend *createWaveOutput(unsigned, unsigned);
AudioInputBackend *createWaveInput(unsigned, unsigned, unsigned);
FramePresenter *createGdiPresenter();
} // namespace cp
#endif
