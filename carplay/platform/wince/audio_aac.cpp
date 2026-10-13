#include "audio_aac.h"
#include "aac_dmo.h"
#include "../../devices/profile.h"
#include "../common/diagnostics.h"
#include <deque>
#include <memory>
namespace cp { namespace {
struct Lock {CRITICAL_SECTION &m;explicit Lock(CRITICAL_SECTION &v):m(v){EnterCriticalSection(&m);}~Lock(){LeaveCriticalSection(&m);}};
struct Apartment {
    HRESULT hr;Apartment():hr(CoInitializeEx(0,COINIT_MULTITHREADED)){aac::check(hr,"AAC COM");}
    ~Apartment(){CoUninitialize();}
};
IMediaObject *createDecoder(){
    require(deviceProfile().audio.aacDecoder!=0,"No device AAC decoder selected");
    CLSID id;aac::check(CLSIDFromString(const_cast<wchar_t*>(deviceProfile().audio.aacDecoder),&id),"AAC CLSID");
    IMediaObject *object=0;
    aac::check(CoCreateInstance(id,0,CLSCTX_INPROC_SERVER,IID_IMediaObject,(void**)&object),"AAC create DMO");
    return object;
}
LONG probeState=0; // Accessed only through Interlocked APIs (CE takes LPLONG).
DWORD probeBody(){
    bool valid=false;std::string error;
    try{
        Apartment com;aac::Decoder decoder(createDecoder(),48000,2);
        const U8 silence[]={0x21,0x10,0x04,0x60,0x8c,0x1c};unsigned bytes=0;
        for(unsigned i=0;i<4;++i){Bytes pcm=decoder.decode(Bytes(silence,silence+sizeof(silence)));bytes+=unsigned(pcm.size());}
        require(bytes>=4096,"AAC self-test produced no complete PCM frame");valid=true;
    }catch(const std::exception &e){error=e.what();}
    diagnosticEvent("AAC_PROBE",valid?"passed rate=48000 channels=2 transport=ADTS buffered=0":error);
    // A timed-out probe must not enable AAC halfway through a later session.
    InterlockedCompareExchange(&probeState,valid?2:3,1);return 0;
}
DWORD WINAPI probe(void*){
#ifdef UNDER_CE
    __try{return probeBody();}
    __except(diagnosticException(GetExceptionInformation(),L"aac_probe")){InterlockedExchange(&probeState,3);return 1;}
#else
    return probeBody();
#endif
}
class Output:public AudioOutputBackend {
    mutable CRITICAL_SECTION mutex_;HANDLE stop_,ready_,initialized_,thread_;
    unsigned rate_,channels_,generation_;bool paused_;
    std::deque<Bytes> queue_;unsigned queuedBytes_;
    AudioOutputBackend *output_;std::string error_;
    U64 packets_,decoded_,pcmBytes_,dropped_,decodeMs_;DWORD decodeMax_;
    void fail(const std::string &error){Lock lock(mutex_);output_=0;error_=error;SetEvent(initialized_);}
    void fault(){fail("AAC decoder exception");}
    void run(){
        try{
            Apartment com;aac::Decoder decoder(createDecoder(),rate_,channels_);
            std::auto_ptr<AudioOutputBackend> sink(createAudioOutputBackend(environment(L"CARPLAY_AUDIO_OUT_BACKEND",L"wave"),rate_,channels_));
            {Lock lock(mutex_);output_=sink.get();}
            SetEvent(initialized_);unsigned seen=0;
            diagnosticEvent("AUDIO_AAC_STARTED","DMO worker; raw AAC-LC -> ADTS -> PCM16");
            try{
                HANDLE events[]={stop_,ready_};
                while(WaitForMultipleObjects(2,events,FALSE,INFINITE)==WAIT_OBJECT_0+1){
                    for(;;){
                        if(WaitForSingleObject(stop_,0)==WAIT_OBJECT_0)break;
                        Bytes packet;unsigned generation=0;
                        {Lock lock(mutex_);if(queue_.empty())break;packet.swap(queue_.front());queue_.pop_front();
                            queuedBytes_-=unsigned(packet.size());generation=generation_;}
                        if(seen!=generation){decoder.flush();seen=generation;}
                        DWORD begin=GetTickCount();Bytes pcm=decoder.decode(packet);DWORD elapsed=GetTickCount()-begin;
                        {Lock lock(mutex_);decodeMs_+=elapsed;if(elapsed>decodeMax_)decodeMax_=elapsed;++decoded_;
                            // Pause/overflow invalidates a packet already being decoded.
                            if(!paused_ && generation==generation_ && !pcm.empty()){sink->feed(pcm);pcmBytes_+=pcm.size();}}
                    }
                }
            }catch(...){Lock lock(mutex_);output_=0;throw;}
            {Lock lock(mutex_);output_=0;}
        }catch(const std::exception &e){fail(e.what());diagnosticEvent("AUDIO_AAC_FAILED",e.what());}
    }
    static DWORD WINAPI entry(void *p){
#ifdef UNDER_CE
        __try{static_cast<Output*>(p)->run();return 0;}
        __except(diagnosticException(GetExceptionInformation(),L"aac_decode")){
            static_cast<Output*>(p)->fault();return 1;}
#else
        static_cast<Output*>(p)->run();return 0;
#endif
    }
    void close(){if(stop_)SetEvent(stop_);if(thread_){WaitForSingleObject(thread_,INFINITE);CloseHandle(thread_);}
        if(stop_)CloseHandle(stop_);if(ready_)CloseHandle(ready_);if(initialized_)CloseHandle(initialized_);DeleteCriticalSection(&mutex_);}
  public:
    Output(unsigned rate,unsigned channels):stop_(0),ready_(0),initialized_(0),thread_(0),rate_(rate),channels_(channels),generation_(0),paused_(false),
        queuedBytes_(0),output_(0),packets_(0),decoded_(0),pcmBytes_(0),dropped_(0),decodeMs_(0),decodeMax_(0){
        InitializeCriticalSection(&mutex_);
        try{
            stop_=CreateEventW(0,TRUE,FALSE,0);ready_=CreateEventW(0,FALSE,FALSE,0);initialized_=CreateEventW(0,TRUE,FALSE,0);
            require(stop_&&ready_&&initialized_,"AAC worker events failed");
            thread_=CreateThread(0,0,entry,this,0,0);require(thread_!=0,"AAC worker failed");
            WaitForSingleObject(initialized_,INFINITE);reap();
        }catch(...){close();throw;}
    }
    ~Output(){close();}
    void feed(const Bytes &b){
        require(!b.empty()&&b.size()<=8184,"Invalid AAC packet size");Lock lock(mutex_);require(error_.empty(),error_.c_str());
        ++packets_;if(paused_)return;
        // About half a second of AAC-LC. Never block the video receiver on decode.
        if(queue_.size()>=24){queuedBytes_-=unsigned(queue_.front().size());queue_.pop_front();++dropped_;++generation_;}
        queue_.push_back(b);queuedBytes_+=unsigned(b.size());SetEvent(ready_);
    }
    void reap(){Lock lock(mutex_);require(error_.empty(),error_.c_str());if(output_)output_->reap();}
    unsigned playedBytes()const{Lock lock(mutex_);return output_?output_->playedBytes():0;}
    void pause(bool value){Lock lock(mutex_);paused_=value;queue_.clear();queuedBytes_=0;++generation_;if(output_)output_->pause(value);}
    Value metrics()const{Lock lock(mutex_);Value v=output_?output_->metrics():Value::dictionary();
        v.dict["codec"]=Value::str("AAC-LC");v.dict["aac_packets"]=Value::num(packets_);v.dict["aac_decoded"]=Value::num(decoded_);
        v.dict["aac_pcm_bytes"]=Value::num(pcmBytes_);v.dict["aac_q_packets"]=Value::num(queue_.size());v.dict["aac_q_bytes"]=Value::num(queuedBytes_);
        v.dict["aac_dropped_packets"]=Value::num(dropped_);v.dict["aac_decode_ms_total"]=Value::num(decodeMs_);v.dict["aac_decode_max_ms"]=Value::num(decodeMax_);
        if(!error_.empty())v.dict["aac_error"]=Value::str(error_);return v;
    }
};
}
bool winceAacAvailable(){
    if(!deviceProfile().audio.aacDecoder)return false;
    LONG state=InterlockedCompareExchange(&probeState,1,0);
    if(state==0){HANDLE thread=CreateThread(0,0,probe,0,0,0);
        if(thread){
            if(WaitForSingleObject(thread,3000)!=WAIT_OBJECT_0){InterlockedCompareExchange(&probeState,3,1);
                diagnosticEvent("AAC_PROBE","timed out; PCM retained for this process");}
            // No stack context is passed; a late worker owns and releases its COM resources.
            CloseHandle(thread);
        }else InterlockedExchange(&probeState,3);}
    return InterlockedCompareExchange(&probeState,0,0)==2;
}
AudioOutputBackend *createWinceAacOutput(unsigned rate,unsigned channels){return new Output(rate,channels);}
}
