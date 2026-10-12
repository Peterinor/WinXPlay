#include "audio_output.h"
namespace cp {
namespace {
// Device 1/TTS routing is verified by the finite PCM test. Live device errors
// must not end video;
// malformed protocol data still fails normally before reaching the device.
class RecoveringOutput : public AudioOutputBackend {
    AudioOutputBackend *output_;
    unsigned rate_, channels_;
    DWORD attemptedAt_, reportedAt_;
    bool attempted_, paused_;
    U64 received_, discarded_, retired_;
    unsigned failures_;
    std::string lastError_;
    std::wstring backend_;
    void report(const char *stage, bool force=false) {
        DWORD now=GetTickCount();
        if(!force && DWORD(now-reportedAt_)<1000)return;
        reportedAt_=now;
        try {
            Value v=Value::dictionary();
            v.dict["backend"]=Value::str("mrn_tts_pcm_retry");
            v.dict["stage"]=Value::str(stage);
            v.dict["sample_rate"]=Value::num(rate_);v.dict["channels"]=Value::num(channels_);
            v.dict["received_pcm_bytes"]=Value::num(received_);
            v.dict["discarded_while_unavailable_bytes"]=Value::num(discarded_);
            v.dict["driver_position_bytes"]=Value::num(playedBytes());
            v.dict["device_open"]=Value::flag(output_!=0);v.dict["paused"]=Value::flag(paused_);
            v.dict["device_failures"]=Value::num(failures_);
            v.dict["retry_interval_ms"]=Value::num(2000);
            v.dict["physical_audio_verified"]=Value::flag(false);
            if(!lastError_.empty())v.dict["last_device_error"]=Value::str(lastError_);
            writeDiagnosticFile(executableDirectory()+L"/audio-recovery.plist",xmlPlist(v));
        }catch(...){} // A diagnostic storage error must not stop streaming.
    }
    void close() {
        if(output_){retired_+=output_->playedBytes();delete output_;output_=0;}
    }
    void failed(const std::exception &e) {
        lastError_=e.what();++failures_;close();
        attempted_=true;attemptedAt_=GetTickCount();report("retry_pending",true);
    }
    bool open() {
        if(output_)return true;
        DWORD now=GetTickCount();
        if(attempted_ && DWORD(now-attemptedAt_)<2000)return false;
        attempted_=true;attemptedAt_=now;
        try {
            output_=createMrnTtsOutput(rate_,channels_);
            if(paused_)output_->pause(true);
            report("device_open",true);
        }catch(const std::exception &e){failed(e);}
        return output_!=0;
    }
  public:
    RecoveringOutput(unsigned r,unsigned c):output_(0),rate_(r),channels_(c),attemptedAt_(0),
        reportedAt_(0),attempted_(false),paused_(false),received_(0),discarded_(0),retired_(0),failures_(0),
        backend_(environment(L"CARPLAY_AUDIO_OUT_BACKEND",L"wave")) {
        require(backend_==L"wave","Unknown audio output backend");
        require(r>=8000 && r<=48000 && c>=1 && c<=2,"Unsupported PCM format");
        open();
    }
    ~RecoveringOutput(){close();report("closed",true);}
    void feed(const Bytes &pcm) {
        require(!pcm.empty() && pcm.size()%(channels_*2)==0 && pcm.size()<=65536,"Invalid PCM packet");
        received_+=pcm.size();
        if(!open()){discarded_+=pcm.size();report("retry_pending");return;}
        try {output_->feed(pcm);report("receiving");}
        catch(const std::exception &e){discarded_+=pcm.size();failed(e);}
    }
    void reap() {
        if(!output_)return;
        try{output_->reap();}catch(const std::exception &e){failed(e);}
    }
    unsigned playedBytes() const {return unsigned(retired_+(output_?output_->playedBytes():0));}
    void pause(bool paused) {
        paused_=paused;
        if(output_){try{output_->pause(paused);}catch(const std::exception &e){failed(e);}}
    }
};
}
AudioOutputBackend *createMrnRecoveringOutput(unsigned rate,unsigned channels) {
    return new RecoveringOutput(rate,channels);
}
}
