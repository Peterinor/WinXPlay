#include "buffered_audio.h"
namespace cp {
BufferedAudio::BufferedAudio(MediaFactory &factory, unsigned rate, U64 connection)
    : rate_(rate), connection_(connection), audio_(0), factory_(factory), bytes_(0), fed_(0),
      playing_(false), started_(false), anchored_(false), anchorRtp_(0), anchorNtp_(0),
      retiredPlayed_(0) {}
BufferedAudio::~BufferedAudio() {
    delete audio_;
}
void BufferedAudio::resetOutput() {
    if (audio_) {
        retiredPlayed_ += audio_->playedBytes();
        delete audio_;
        audio_ = 0;
    }
    fed_ = 0;
    started_ = false;
}
bool BufferedAudio::hasCapacity() const {
    return bytes_ < 8 * 1024 * 1024 - 65536 && packets_.size() < size_t(rate_) * 120 / 1024 - 128;
}
void BufferedAudio::receive(U32 timestamp, const Bytes &payload) {
    require(payload.size() > 0 && payload.size() <= 8184, "Invalid buffered AAC packet");
    require(bytes_ + payload.size() <= 8 * 1024 * 1024 &&
                packets_.size() < size_t(rate_) * 120 / 1024,
            "Buffered audio capacity exceeded");
    packets_.push_back(Packet(timestamp, payload));
    bytes_ += payload.size();
}
void BufferedAudio::discardBefore(U32 timestamp) {
    while (!packets_.empty() && I32(packets_.front().time - timestamp) < 0) {
        bytes_ -= packets_.front().payload.size();
        packets_.pop_front();
        if (fed_)
            --fed_;
    }
}
U32 BufferedAudio::position() const {
    return anchorRtp_ + (audio_ ? audio_->playedBytes() / 4 : 0);
}
U64 BufferedAudio::playedBytes() const {
    return retiredPlayed_ + (audio_ ? audio_->playedBytes() : 0);
}
void BufferedAudio::tick(U64 now) {
    if (!playing_ || !audio_)
        return;
    if (started_)
        discardBefore(position() - 1023);
    // Keep less than 400 ms in waveOut. Before the anchor, decode into a paused
    // output; after it, follow the actual Windows playback position.
    U32 until = position() + rate_ * 350 / 1000;
    while (fed_ < packets_.size() && I32(packets_[fed_].time - until) < 0) {
        audio_->receive(packets_[fed_].payload);
        ++fed_;
    }
    if (!started_ && I64(now - anchorNtp_) >= 0) {
        audio_->pause(false);
        started_ = true;
    }
}
Value BufferedAudio::anchor() const {
    Value v = Value::dictionary();
    v.dict["rtpTime"] = Value::num(anchorRtp_);
    v.dict["networkTimeSecs"] = Value::num((anchorNtp_ >> 32) - 2208988800ULL);
    v.dict["networkTimeFrac"] = Value::num((anchorNtp_ & 0xffffffffULL) << 32);
    v.dict["rate"].type = Value::Real;
    v.dict["rate"].real = playing_ ? 1.0 : 0.0;
    return v;
}
Value BufferedAudio::control(const std::string &method, const Value &body, U64 now) {
    if (method == "GETANCHOR") {
        if (!anchored_) {
            anchorNtp_ = now;
            anchored_ = true;
        }
        return anchor();
    }
    if (method == "FLUSHBUFFERED") {
        if (body.dict.count("flushUntilTS")) {
            const Value &t = body.at("flushUntilTS");
            require(t.type == Value::Integer && t.integer <= 0xffffffffULL,
                    "Invalid flush timestamp");
            anchorRtp_ = U32(t.integer);
            resetOutput();
            discardBefore(anchorRtp_);
            anchorNtp_ = now + (U64(400) << 32) / 1000;
            if (playing_) {
                audio_ = factory_.audio(rate_, 2, true);
                audio_->pause(true);
            }
        }
        return Value();
    }
    U32 start = position();
    if (body.dict.count("rtpTime")) {
        const Value &t = body.at("rtpTime");
        require(t.type == Value::Integer && t.integer <= 0xffffffffULL, "Invalid audio anchor");
        start = U32(t.integer);
    }
    double rate = 0;
    if (body.dict.count("rate")) {
        const Value &r = body.at("rate");
        require(r.type == Value::Real || r.type == Value::Integer, "Invalid audio rate");
        rate = r.type == Value::Real ? r.real : double(r.integer);
    }
    require(rate == 0.0 || rate == 1.0, "Unsupported buffered playback rate");
    if (rate == 0)
        start = position();
    resetOutput();
    discardBefore(start);
    anchorRtp_ = start;
    anchorNtp_ = now + (rate == 1.0 ? (U64(400) << 32) / 1000 : 0);
    anchored_ = true;
    playing_ = rate == 1.0;
    if (playing_) {
        audio_ = factory_.audio(rate_, 2, true);
        audio_->pause(true);
    }
    return anchor();
}
Value BufferedAudio::feedback(U64 now, U32 milliseconds) const {
    Value v = Value::dictionary();
    v.dict["type"] = Value::num(103);
    v.dict["sampleRate"] = Value::num(rate_);
    v.dict["streamConnectionID"] = Value::num(connection_);
    v.dict["timestamp"] = Value::num(now);
    v.dict["timestampRawNs"] = Value::num(U64(milliseconds) * 1000000);
    v.dict["sampleTime"] = Value::num(position());
    return v;
}
} // namespace cp
