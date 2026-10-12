#include "../common/legacy_media.h"
#include "media_backend.h"
namespace cp {

LiveMicrophone::LiveMicrophone(unsigned rate, unsigned channels, unsigned samples)
    : p_(createAudioInputBackend(environment(L"CARPLAY_AUDIO_IN_BACKEND", L"wave"), rate, channels,
                                 samples)) {}
LiveMicrophone::~LiveMicrophone() {
    delete p_;
}
std::vector<Bytes> LiveMicrophone::poll() {
    return p_->poll();
}
LiveAudio::LiveAudio(unsigned rate, unsigned channels, bool aac) : p_(0) {
    require(!aac, "CE requires PCM; AAC backend not enabled");
    p_ = createAudioOutputBackend(environment(L"CARPLAY_AUDIO_OUT_BACKEND", L"wave"), rate,
                                  channels);
}
LiveAudio::~LiveAudio() {
    delete p_;
}
void LiveAudio::feed(const Bytes &b) {
    p_->feed(b);
}
void LiveAudio::receive(const Bytes &b) {
    p_->feed(pcmSwap(b));
}
void LiveAudio::reap() {
    p_->reap();
}
unsigned LiveAudio::playedBytes() const {
    return p_->playedBytes();
}
void LiveAudio::pause(bool paused) {
    p_->pause(paused);
}
} // namespace cp
