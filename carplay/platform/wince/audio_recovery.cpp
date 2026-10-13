#include "audio_recovery.h"
namespace cp {
namespace {
// Retry device failures without ending video; invalid PCM still fails before the device call.
class RecoveringOutput : public AudioOutputBackend {
    AudioOutputBackend *output_;
    unsigned rate_, channels_;
    DWORD attemptedAt_;
    bool attempted_, paused_;
    U64 received_, discarded_, retired_;
    unsigned failures_;
    std::string lastError_;
    AudioOutputFactory factory_;unsigned retryMs_;std::string backend_;
    void report(const char *stage) {
        try {
            Value v=Value::dictionary();
            v.dict["backend"]=Value::str(backend_);
            v.dict["stage"]=Value::str(stage);
            v.dict["sample_rate"]=Value::num(rate_);v.dict["channels"]=Value::num(channels_);
            v.dict["received_pcm_bytes"]=Value::num(received_);
            v.dict["discarded_while_unavailable_bytes"]=Value::num(discarded_);
            v.dict["driver_position_bytes"]=Value::num(playedBytes());
            v.dict["device_open"]=Value::flag(output_!=0);v.dict["paused"]=Value::flag(paused_);
            v.dict["device_failures"]=Value::num(failures_);
            v.dict["retry_interval_ms"]=Value::num(retryMs_);
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
        attempted_=true;attemptedAt_=GetTickCount();report("retry_pending");
    }
    bool open() {
        if(output_)return true;
        DWORD now=GetTickCount();
        if(attempted_ && DWORD(now-attemptedAt_)<retryMs_)return false;
        attempted_=true;attemptedAt_=now;
        try {
            output_=factory_(rate_,channels_);
            require(output_!=0,"Audio output factory returned null");
            if(paused_)output_->pause(true);
            report("device_open");
        }catch(const std::exception &e){failed(e);}
        return output_!=0;
    }
  public:
    RecoveringOutput(unsigned r,unsigned c,AudioOutputFactory factory,unsigned retryMs,const char *name):output_(0),rate_(r),channels_(c),attemptedAt_(0),
        attempted_(false),paused_(false),received_(0),discarded_(0),retired_(0),failures_(0),
        factory_(factory),retryMs_(retryMs),backend_(name) {
        require(r>=8000 && r<=48000 && c>=1 && c<=2,"Unsupported PCM format");
        open();
    }
    ~RecoveringOutput(){close();report("closed");}
    void feed(const Bytes &pcm) {
        require(!pcm.empty() && pcm.size()%(channels_*2)==0 && pcm.size()<=65536,"Invalid PCM packet");
        received_+=pcm.size();
        if(!open()){discarded_+=pcm.size();return;}
        // The receiver samples live counters once a second. Only transitions
        // write this auxiliary snapshot; feeding PCM must not wait for flash.
        try {output_->feed(pcm);}
        catch(const std::exception &e){discarded_+=pcm.size();failed(e);}
    }
    void reap() {
        if(!output_)return;
        try{output_->reap();}catch(const std::exception &e){failed(e);}
    }
    unsigned playedBytes() const {return unsigned(retired_+(output_?output_->playedBytes():0));}
    Value metrics() const {
        Value v=output_?output_->metrics():Value::dictionary();
        v.dict["device_open"]=Value::flag(output_!=0);
        v.dict["failures"]=Value::num(failures_);
        v.dict["discarded_bytes"]=Value::num(discarded_);
        return v;
    }
    void pause(bool paused) {
        paused_=paused;
        if(output_){try{output_->pause(paused);}catch(const std::exception &e){failed(e);}}
    }
};
}
AudioOutputBackend *createRecoveringOutput(unsigned rate,unsigned channels,AudioOutputFactory factory,unsigned retryMs,const char *name) {
    require(factory && name && retryMs>0,"Invalid audio recovery policy");
    return new RecoveringOutput(rate,channels,factory,retryMs,name);
}
}
