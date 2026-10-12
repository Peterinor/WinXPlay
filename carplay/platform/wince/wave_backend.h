#ifndef MRN_WAVE_BACKEND_H
#define MRN_WAVE_BACKEND_H
#include "media_backend.h"
#include <mmsystem.h>
#include <deque>
namespace cp {
class WaveInput : public AudioInputBackend {
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
    WaveInput(const WaveInput &);
    WaveInput &operator=(const WaveInput &);

  public:
    WaveInput(unsigned rate, unsigned channels, unsigned samples);
    ~WaveInput();
    std::vector<Bytes> poll();
};
class WaveOutput : public AudioOutputBackend {
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
    std::deque<Block *> blocks_;
    WaveOutput(const WaveOutput &);
    WaveOutput &operator=(const WaveOutput &);

  public:
    WaveOutput(unsigned rate, unsigned channels, bool aac = false);
    ~WaveOutput();
    void feed(const Bytes &pcm);
    void reap();
    unsigned playedBytes() const;
    void receive(const Bytes &payload);
    void pause(bool paused);
};
} // namespace cp
#endif
