#include "../common/legacy_media.h"
#include "mf_audio.h"
#include <algorithm>
#include <sstream>
namespace cp {
LiveMicrophone::LiveMicrophone(unsigned rate, unsigned channels, unsigned samples)
    : device_(0), next_(0) {
    require(rate >= 8000 && rate <= 48000 && channels == 1 && samples >= rate / 200 &&
                samples <= rate * 60 / 1000,
            "Invalid microphone format");
    WAVEFORMATEX format;
    ZeroMemory(&format, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = WORD(channels);
    format.nSamplesPerSec = rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = WORD(channels * 2);
    format.nAvgBytesPerSec = rate * format.nBlockAlign;
    MMRESULT opened=waveInOpen(&device_,unsigned(_wtoi(environment(L"CARPLAY_WAVE_IN",L"-1").c_str())),
                              &format,0,0,CALLBACK_NULL);
    if(opened!=MMSYSERR_NOERROR) {
        std::ostringstream message;message<<"Cannot open Windows microphone: MMRESULT="<<opened;
        throw std::runtime_error(message.str());
    }
    try {
        for (unsigned i = 0; i < 8; ++i) {
            Block *b = new Block(samples * channels * 2);
            blocks_.push_back(b);
            require(waveInPrepareHeader(device_, &b->header, sizeof(WAVEHDR)) == 0,
                    "Microphone buffer preparation failed");
            require(waveInAddBuffer(device_, &b->header, sizeof(WAVEHDR)) == 0,
                    "Microphone queue failed");
        }
        require(waveInStart(device_) == 0, "Microphone start failed");
    } catch (...) {
        close();
        throw;
    }
}
void LiveMicrophone::close() {
    if (!device_)
        return;
    waveInStop(device_);
    waveInReset(device_);
    for (size_t i = 0; i < blocks_.size(); ++i) {
        if (blocks_[i]->header.dwFlags & WHDR_PREPARED)
            waveInUnprepareHeader(device_, &blocks_[i]->header, sizeof(WAVEHDR));
        delete blocks_[i];
    }
    blocks_.clear();
    waveInClose(device_);
    device_ = 0;
}
LiveMicrophone::~LiveMicrophone() {
    close();
}
std::vector<Bytes> LiveMicrophone::poll() {
    std::vector<Bytes> result;
    for (size_t i = 0; i < blocks_.size(); ++i) {
        Block &b = *blocks_[next_];
        if (!(b.header.dwFlags & WHDR_DONE))
            break;
        if (b.header.dwBytesRecorded)
            result.push_back(slice(b.data, 0, b.header.dwBytesRecorded));
        b.header.dwBytesRecorded = 0;
        require(waveInAddBuffer(device_, &b.header, sizeof(WAVEHDR)) == 0,
                "Microphone requeue failed");
        next_ = (next_ + 1) % blocks_.size();
    }
    return result;
}
LiveAudio::LiveAudio(unsigned rate, unsigned channels, bool aac)
    : wave_(0), rate_(rate), channels_(channels), queued_(0), aac_(0) {
    require(rate >= 8000 && rate <= 48000 && channels >= 1 && channels <= 2,
            "Unsupported PCM format");
    WAVEFORMATEX f;
    ZeroMemory(&f, sizeof(f));
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = WORD(channels);
    f.nSamplesPerSec = rate;
    f.wBitsPerSample = 16;
    f.nBlockAlign = WORD(channels * 2);
    f.nAvgBytesPerSec = rate * f.nBlockAlign;
    require(waveOutOpen(&wave_, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) == 0,
            "Cannot open live audio output");
    if (aac) {
        try {
            require(SUCCEEDED(cpAacCreate(rate,channels,&aac_)), "Cannot initialize Windows AAC decoder");
        } catch (...) {
            waveOutClose(wave_);
            throw;
        }
    }
}
void LiveAudio::pause(bool paused) {
    require((paused ? waveOutPause(wave_) : waveOutRestart(wave_)) == MMSYSERR_NOERROR,
            "Audio pause/resume failed");
}
void LiveAudio::receive(const Bytes &payload) {
    if (!aac_) {
        feed(pcmSwap(payload));
        return;
    }
    Bytes pcm(65536); unsigned written=0;
    require(SUCCEEDED(cpAacDecode(aac_,payload.empty()?0:&payload[0],unsigned(payload.size()),
                                  &pcm[0],unsigned(pcm.size()),&written)), "Windows AAC decode failed");
    pcm.resize(written); if(!pcm.empty()) feed(pcm);
}
void LiveAudio::reap() {
    while (!blocks_.empty() && (blocks_.front()->header.dwFlags & WHDR_DONE)) {
        Block *b = blocks_.front();
        waveOutUnprepareHeader(wave_, &b->header, sizeof(b->header));
        queued_ -= b->data.size();
        blocks_.pop_front();
        delete b;
    }
}
unsigned LiveAudio::playedBytes() const {
    MMTIME time;
    ZeroMemory(&time, sizeof(time));
    time.wType = TIME_BYTES;
    if (waveOutGetPosition(wave_, &time, sizeof(time)) != MMSYSERR_NOERROR)
        return 0;
    if (time.wType == TIME_BYTES)
        return time.u.cb;
    if (time.wType == TIME_SAMPLES)
        return time.u.sample * channels_ * 2;
    return 0;
}
void LiveAudio::feed(const Bytes &pcm) {
    require(!pcm.empty() && pcm.size() % (channels_ * 2) == 0 && pcm.size() <= 65536,
            "Invalid PCM packet");
    reap();
    // Bound queued latency to 500 ms. Keep the network loop responsive after a device stall.
    if (queued_ + pcm.size() > rate_ * channels_)
        return;
    Block *b = new Block(pcm);
    if (waveOutPrepareHeader(wave_, &b->header, sizeof(b->header)) != 0) {
        delete b;
        throw std::runtime_error("Audio prepare failed");
    }
    if (waveOutWrite(wave_, &b->header, sizeof(b->header)) != 0) {
        waveOutUnprepareHeader(wave_, &b->header, sizeof(b->header));
        delete b;
        throw std::runtime_error("Audio submit failed");
    }
    queued_ += pcm.size();
    blocks_.push_back(b);
}
LiveAudio::~LiveAudio() {
    cpAacDestroy(aac_);
    waveOutReset(wave_);
    for (size_t i = 0; i < blocks_.size(); ++i) {
        waveOutUnprepareHeader(wave_, &blocks_[i]->header, sizeof(WAVEHDR));
        delete blocks_[i];
    }
    waveOutClose(wave_);
}
} // namespace cp
