#ifndef CP_VIDEO_QUEUE_H
#define CP_VIDEO_QUEUE_H
#include "../../core/protocol.h"
#include <deque>
namespace cp {
// Caller serializes access. Each input is a complete Annex-B access unit or
// codec configuration. Never drop a reference frame and continue its GOP.
class VideoQueue {
  public:
    struct Sample { Bytes bytes; bool key; U32 queuedAt; Sample() : key(false),queuedAt(0) {} };
    enum { MaxSample = 2 * 1024 * 1024, MaxBytes = 4 * 1024 * 1024, MaxFrames = 8 };
  private:
    std::deque<Sample> samples_;
    Bytes sps_, pps_;
    size_t bytes_;
    bool started_;
    static unsigned prefix(const Bytes &b, size_t i) {
        if (i + 3 <= b.size() && !b[i] && !b[i+1]) {
            if (b[i+2] == 1) return 3;
            if (i + 4 <= b.size() && !b[i+2] && b[i+3] == 1) return 4;
        }
        return 0;
    }
  public:
    VideoQueue() : bytes_(0), started_(false) {}
    void pushRaw(const Bytes &b,U32 now=0) {
        require(!b.empty() && b.size()<=MaxSample && samples_.size()<MaxFrames &&
                bytes_+b.size()<=MaxBytes,"Raw video queue limit");
        Sample s;s.bytes=b;s.key=true;s.queuedAt=now;samples_.push_back(s);bytes_+=b.size();
    }
    bool push(const Bytes &b,U32 now=0) {
        require(!b.empty() && b.size() <= MaxSample && prefix(b,0), "Invalid Annex-B access unit");
        bool picture=false, key=false;
        size_t at=0;
        while (at < b.size()) {
            unsigned n=prefix(b,at);
            require(n && at+n < b.size(), "Truncated Annex-B NAL");
            size_t end=at+n+1;
            while (end < b.size() && !prefix(b,end)) ++end;
            unsigned type=b[at+n]&31;
            if (type==7 || type==8) {
                require(end-at<=65536, "H264 parameter set too large");
                Bytes &out=type==7?sps_:pps_;
                out.assign(b.begin()+at,b.begin()+end);
            }
            if (type>=1 && type<=5) picture=true;
            if (type==5) key=true;
            at=end;
        }
        if (!picture) return false;
        if (!started_ && !key) return false;
        require(!sps_.empty() && !pps_.empty(), "H264 picture without parameter sets");
        Sample sample;sample.key=key;sample.queuedAt=now;
        if (key) { sample.bytes=sps_; sample.bytes.insert(sample.bytes.end(),pps_.begin(),pps_.end()); }
        sample.bytes.insert(sample.bytes.end(),b.begin(),b.end());
        require(sample.bytes.size()<=MaxSample, "H264 sample limit");
        require(samples_.size()<MaxFrames && bytes_+sample.bytes.size()<=MaxBytes,
                "Video queue full; reconnect rather than corrupt reference frames");
        samples_.push_back(sample);bytes_+=sample.bytes.size();started_=true;
        return true;
    }
    bool pop(Sample &out) {
        if (samples_.empty()) return false;
        out=samples_.front();bytes_-=out.bytes.size();samples_.pop_front();return true;
    }
    void clear() { samples_.clear();sps_.clear();pps_.clear();bytes_=0;started_=false; }
    size_t size() const { return samples_.size(); }
    size_t bytes() const { return bytes_; }
    U32 oldestAge(U32 now) const { return samples_.empty()?0:U32(now-samples_.front().queuedAt); }
    bool capacityFor(size_t n) const {
        return samples_.size()<MaxFrames && bytes_+n+sps_.size()+pps_.size()<=MaxBytes;
    }
};
}
#endif
