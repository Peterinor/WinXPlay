#ifndef MRN_LIVE_MEDIA_H
#define MRN_LIVE_MEDIA_H
#include "native_services.h"
#include <mmsystem.h>
#include <deque>
namespace cp {
#if defined(UNDER_CE) || defined(WINXPLAY_AUDIO_RECOVERY_HOST_TEST)
class AudioInputBackend;
class LiveMicrophone {
    AudioInputBackend *p_;
    LiveMicrophone(const LiveMicrophone &);
    LiveMicrophone &operator=(const LiveMicrophone &);

  public:
    LiveMicrophone(unsigned, unsigned, unsigned);
    ~LiveMicrophone();
    std::vector<Bytes> poll();
};
#else
class LiveMicrophone {
    struct Block {
        WAVEHDR header;
        Bytes data;
        explicit Block(unsigned n) : data(n) {
            ZeroMemory(&header, sizeof(header));
            header.lpData = (LPSTR)&data[0];
            header.dwBufferLength = n;
        }
    };
    HWAVEIN device_;
    std::vector<Block *> blocks_;
    size_t next_;
    void close();
    LiveMicrophone(const LiveMicrophone &);
    LiveMicrophone &operator=(const LiveMicrophone &);

  public:
    LiveMicrophone(unsigned rate, unsigned channels, unsigned samples);
    ~LiveMicrophone();
    std::vector<Bytes> poll();
};
#endif
#if defined(UNDER_CE) || defined(WINXPLAY_AUDIO_RECOVERY_HOST_TEST)
class LiveVideo {
    struct Impl;
    Impl *p_;
    LiveVideo(const LiveVideo &);
    LiveVideo &operator=(const LiveVideo &);

  public:
    LiveVideo(VideoCallback cb, void *context, unsigned audioRate = 0, unsigned audioChannels = 2,
              unsigned width = 800, unsigned height = 480);
    ~LiveVideo();
    void feed(const Bytes &annexB);
    void finish();
    unsigned frames() const;
};
#endif
#if defined(UNDER_CE) || defined(WINXPLAY_AUDIO_RECOVERY_HOST_TEST)
class AudioOutputBackend;
class LiveAudio {
    AudioOutputBackend *p_;
    bool aac_;
    LiveAudio(const LiveAudio &);
    LiveAudio &operator=(const LiveAudio &);

  public:
    LiveAudio(unsigned, unsigned, bool aac = false);
    ~LiveAudio();
    void feed(const Bytes &);
    void receive(const Bytes &);
    void reap();
    unsigned playedBytes() const;
    void pause(bool);
    Value metrics() const;
};
#else
class LiveAudio {
    struct Block {
        WAVEHDR header;
        Bytes data;
        explicit Block(const Bytes &b) : data(b) {
            ZeroMemory(&header, sizeof(header));
            header.lpData = (LPSTR)&data[0];
            header.dwBufferLength = DWORD(data.size());
        }
    };
    HWAVEOUT wave_;
    unsigned rate_, channels_;
    size_t queued_;
    void *aac_;
    std::deque<Block *> blocks_;
    LiveAudio(const LiveAudio &);
    LiveAudio &operator=(const LiveAudio &);

  public:
    LiveAudio(unsigned rate, unsigned channels, bool aac = false);
    ~LiveAudio();
    void feed(const Bytes &pcm);
    void reap();
    unsigned playedBytes() const;
    void receive(const Bytes &payload);
    void pause(bool paused);
};
#endif
} // namespace cp
#endif
