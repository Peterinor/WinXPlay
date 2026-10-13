#ifndef CP_MEDIA_METRICS_H
#define CP_MEDIA_METRICS_H
#include "protocol.h"
#include <algorithm>
namespace cp {
// Receiver-thread counters only. No payload copies or per-frame file I/O.
// Windows are sampled, not assumed to be exactly 1000 ms. Unsigned tick
// subtraction handles GetTickCount wrap for intervals shorter than 49 days.
class MediaMetrics {
    U32 start_, window_, lastRx_, lastFrame_;
    bool haveRx_, haveFrame_;
    U64 previousBytes_, previousReceived_, previousDecoded_, previousSubmitted_;
  public:
    U64 bytes, calls, received, decrypted, config, errors, submitted;
    U64 parseMs, submitMs, reportMs, audioMs, audioDecryptMs;
    U32 parseMax, submitMax, audioMax, audioDecryptMax, rxGapMax, frameGapMax, pendingBytes;
    explicit MediaMetrics(U32 now=0):start_(now),window_(now),lastRx_(now),lastFrame_(now),
        haveRx_(false),haveFrame_(false),previousBytes_(0),previousReceived_(0),
        previousDecoded_(0),previousSubmitted_(0),bytes(0),calls(0),received(0),decrypted(0),
        config(0),errors(0),submitted(0),parseMs(0),submitMs(0),reportMs(0),audioMs(0),audioDecryptMs(0),parseMax(0),
        submitMax(0),audioMax(0),audioDecryptMax(0),rxGapMax(0),frameGapMax(0),pendingBytes(0){}
    void receive(U32 now,size_t n) {
        bytes+=n;++calls;
        if(haveRx_)rxGapMax=std::max(rxGapMax,U32(now-lastRx_));
        haveRx_=true;lastRx_=now;
    }
    void parsed(U32 now,U64 complete,U64 plain,U64 configs,U32 ms) {
        received+=complete;decrypted+=plain;config+=configs;parseMs+=ms;
        parseMax=std::max(parseMax,ms);
        if(complete){if(haveFrame_)frameGapMax=std::max(frameGapMax,U32(now-lastFrame_));
            haveFrame_=true;lastFrame_=now;}
    }
    void submitTime(U32 ms){submitMs+=ms;submitMax=std::max(submitMax,ms);}
    void audioTime(U32 ms){audioMs+=ms;audioMax=std::max(audioMax,ms);}
    void audioDecryptTime(U32 ms){audioDecryptMs+=ms;audioDecryptMax=std::max(audioDecryptMax,ms);}
    Value sample(U32 now) {
        Value v=Value::dictionary();
        v.dict["version"]=Value::num(1);
        v.dict["start_tick_ms"]=Value::num(start_);
        v.dict["tick_ms"]=Value::num(now);
        v.dict["window_ms"]=Value::num(U32(now-window_));
        v.dict["n1"]=Value::str("unknown");
        v.dict["n4_decode"]=Value::str("unknown");
        v.dict["n4_display"]=Value::str("unknown");
        v.dict["rx_bytes"]=Value::num(bytes);v.dict["rx_bytes_delta"]=Value::num(bytes-previousBytes_);
        v.dict["recv_calls"]=Value::num(calls);
        v.dict["n2"]=Value::num(received);v.dict["n2_delta"]=Value::num(received-previousReceived_);
        v.dict["n3"]=Value::num(decrypted);v.dict["n3_delta"]=Value::num(decrypted-previousDecoded_);
        v.dict["submitted"]=Value::num(submitted);v.dict["submitted_delta"]=Value::num(submitted-previousSubmitted_);
        v.dict["config"]=Value::num(config);v.dict["parse_errors"]=Value::num(errors);
        v.dict["pending_bytes"]=Value::num(pendingBytes);
        v.dict["parse_ms"]=Value::num(parseMs);v.dict["parse_max_ms"]=Value::num(parseMax);
        v.dict["submit_ms"]=Value::num(submitMs);v.dict["submit_max_ms"]=Value::num(submitMax);
        v.dict["report_ms"]=Value::num(reportMs);
        v.dict["audio_process_ms"]=Value::num(audioMs);v.dict["audio_process_max_ms"]=Value::num(audioMax);
        v.dict["audio_decrypt_ms"]=Value::num(audioDecryptMs);v.dict["audio_decrypt_max_ms"]=Value::num(audioDecryptMax);
        v.dict["rx_gap_max_ms"]=Value::num(rxGapMax);v.dict["frame_gap_max_ms"]=Value::num(frameGapMax);
        v.dict["rx_idle_ms"]=Value::num(haveRx_?U32(now-lastRx_):0);
        v.dict["frame_idle_ms"]=Value::num(haveFrame_?U32(now-lastFrame_):0);
        previousBytes_=bytes;previousReceived_=received;previousDecoded_=decrypted;previousSubmitted_=submitted;
        window_=now;parseMs=submitMs=reportMs=audioMs=audioDecryptMs=0;parseMax=submitMax=audioMax=audioDecryptMax=rxGapMax=frameGapMax=0;
        return v;
    }
};
}
#endif
