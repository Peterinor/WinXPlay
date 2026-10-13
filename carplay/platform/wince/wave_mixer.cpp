// Shared WinCE waveOut mixer; device routing and buffering policy are injected.
#include "wave_mixer.h"
#include "../common/diagnostics.h"
#include "../common/pcm_queue.h"
#include <algorithm>
#include <mmsystem.h>
#include <sstream>
namespace cp { namespace {
enum { Streams=8 };
struct Lock { CRITICAL_SECTION &m;Lock(CRITICAL_SECTION &v):m(v){EnterCriticalSection(&m);}~Lock(){LeaveCriticalSection(&m);} };
struct Stream {
    Pcm48StereoQueue pcm;unsigned rate,channels;bool paused;U64 received,completed;
    U64 inflight,emptyFrames,feedWaitMs,feedCopyMs;unsigned queuePeak;
    Stream(unsigned r,unsigned c):pcm(r,c),rate(r),channels(c),paused(false),received(0),completed(0),inflight(0),emptyFrames(0),feedWaitMs(0),feedCopyMs(0),queuePeak(0){}
};
class Mixer {
    CRITICAL_SECTION mutex_;HANDLE stop_,thread_;HWAVEOUT wave_;
    const WaveOutputConfig &policy_;WaveAudioRoute *route_;
    Stream *streams_[Streams];unsigned references_,next_;
    std::vector<WAVEHDR> headers_;std::vector<std::vector<short> > samples_;std::vector<bool> prepared_;
    std::vector<std::vector<unsigned> > credits_;bool requested_;std::string error_;
    U64 submitted_,completed_;DWORD lastRefill_,refillGapMax_;
    U64 fillMs_,writeMs_;DWORD fillMax_,writeMax_;
    void check(DWORD code,const char *stage){if(code){std::ostringstream s;s<<stage<<" error="<<code;throw std::runtime_error(s.str());}}
    void report(const char *stage){
        try{Value v=Value::dictionary();v.dict["backend"]=Value::str(policy_.name);v.dict["stage"]=Value::str(stage);
            v.dict["wave_device"]=Value::num(policy_.deviceId);v.dict["sample_rate"]=Value::num(48000);v.dict["channels"]=Value::num(2);
            v.dict["buffers_submitted"]=Value::num(submitted_);v.dict["buffers_completed"]=Value::num(completed_);
            v.dict["route_requested"]=Value::flag(requested_);v.dict["physical_audio_verified"]=Value::flag(false);
            v.dict["source_connection_verified"]=Value::flag(false);
            U64 received=0,frames=0,dropped=0;
            {Lock lock(mutex_);for(unsigned i=0;i<Streams;++i)if(streams_[i]){
                received+=streams_[i]->received;frames+=streams_[i]->completed;dropped+=streams_[i]->pcm.dropped;
            }if(!error_.empty())v.dict["error"]=Value::str(error_);}
            v.dict["received_pcm_bytes"]=Value::num(received);v.dict["completed_content_frames"]=Value::num(frames);
            v.dict["dropped_queue_frames"]=Value::num(dropped);
            writeDiagnosticFile(executableDirectory()+L"/audio-output.plist",xmlPlist(v));
        }catch(...){}
    }
    void fill(unsigned block){
        Lock lock(mutex_);std::fill(credits_[block].begin(),credits_[block].end(),0);
        DWORD begin=GetTickCount();unsigned active[Streams],count=0;
        for(unsigned i=0;i<Streams;++i)if(streams_[i]&&!streams_[i]->paused)active[count++]=i;
        if(count<=1){
            unsigned copied=0;
            if(count){unsigned i=active[0];copied=streams_[i]->pcm.popFrames(&samples_[block][0],policy_.framesPerBuffer);
                credits_[block][i]=copied;}
            std::fill(samples_[block].begin()+copied*2,samples_[block].end(),0);
        }else{
            for(unsigned f=0;f<policy_.framesPerBuffer;++f){int left=0,right=0;
                for(unsigned n=0;n<count;++n){unsigned i=active[n];short l,r;
                    if(streams_[i]->pcm.pop(l,r)){left+=l;right+=r;++credits_[block][i];}}
                samples_[block][f*2]=short(left>32767?32767:left<-32768?-32768:left);
                samples_[block][f*2+1]=short(right>32767?32767:right<-32768?-32768:right);
            }
        }
        for(unsigned i=0;i<Streams;++i)if(streams_[i]&&!streams_[i]->paused)
            streams_[i]->emptyFrames+=policy_.framesPerBuffer-credits_[block][i];
        DWORD elapsed=GetTickCount()-begin;fillMs_+=elapsed;fillMax_=(std::max)(fillMax_,elapsed);
    }
    void writeBuffer(unsigned block,const char *stage){
        DWORD begin=GetTickCount();MMRESULT result=waveOutWrite(wave_,&headers_[block],sizeof(WAVEHDR));
        DWORD elapsed=GetTickCount()-begin;
        {Lock lock(mutex_);writeMs_+=elapsed;writeMax_=(std::max)(writeMax_,elapsed);}
        check(result,stage);
    }
    void submittedContent(unsigned block){
        Lock lock(mutex_);for(unsigned i=0;i<Streams;++i)if(streams_[i])streams_[i]->inflight+=credits_[block][i];
    }
    void close(){
        bool released=true;
        if(wave_){
            MMRESULT reset=waveOutReset(wave_);if(reset)released=false;
            for(unsigned i=0;i<policy_.bufferCount;++i)if(prepared_[i]){
                MMRESULT r=waveOutUnprepareHeader(wave_,&headers_[i],sizeof(WAVEHDR));
                if(r)released=false;else prepared_[i]=false;
            }
            MMRESULT result=waveOutClose(wave_);if(result)released=false;else wave_=0;
        }
        if(route_)route_->release();requested_=false;
        // A driver retaining WAVEHDR/data must never observe freed storage.
        if(!released){Lock lock(mutex_);error_="Audio driver did not release buffers";retained=true;}
    }
    void run(){
        try{
            if(policy_.createRoute){route_=policy_.createRoute();require(route_!=0,"Audio route factory returned null");
                route_->acquire();requested_=true;}
            report("route_settling");
            if(WaitForSingleObject(stop_,policy_.settleMs)==WAIT_OBJECT_0){close();report("closed");return;}
            WAVEFORMATEX f={0};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=2;f.nSamplesPerSec=48000;
            f.wBitsPerSample=16;f.nBlockAlign=4;f.nAvgBytesPerSec=192000;
            check(waveOutOpen(&wave_,policy_.deviceId,&f,0,0,CALLBACK_NULL),"waveOutOpen");
            for(unsigned i=0;i<policy_.bufferCount;++i){headers_[i].lpData=(LPSTR)&samples_[i][0];headers_[i].dwBufferLength=policy_.framesPerBuffer*4;
                check(waveOutPrepareHeader(wave_,&headers_[i],sizeof(WAVEHDR)),"waveOutPrepareHeader");prepared_[i]=true;fill(i);}
            // Prefill every independent buffer; do not pause the physical device.
            // Device profiles choose how many writes their driver needs to start.
            for(unsigned i=0;i<policy_.bufferCount;++i){writeBuffer(i,"waveOutWrite initial");++submitted_;submittedContent(i);}
            {Lock lock(mutex_);lastRefill_=GetTickCount();}
            diagnosticEvent("AUDIO_OUTPUT_PLAYING",policy_.name);report("playing");
            while(WaitForSingleObject(stop_,10)==WAIT_TIMEOUT){
                for(unsigned n=0;n<policy_.bufferCount && (headers_[next_].dwFlags&WHDR_DONE);++n){
                    {Lock lock(mutex_);DWORD now=GetTickCount(),gap=DWORD(now-lastRefill_);
                        if(gap>refillGapMax_)refillGapMax_=gap;lastRefill_=now;
                        for(unsigned i=0;i<Streams;++i)if(streams_[i]){
                        streams_[i]->completed+=credits_[next_][i];streams_[i]->inflight-=credits_[next_][i];}}
                    ++completed_;fill(next_);writeBuffer(next_,"waveOutWrite refill");
                    ++submitted_;submittedContent(next_);next_=(next_+1)%policy_.bufferCount;
                }
                // Per-second counters are sampled by the receiver. Do not write
                // duplicate snapshots on the time-sensitive refill thread.
            }
        }catch(const std::exception &e){Lock lock(mutex_);error_=e.what();diagnosticEvent("AUDIO_OUTPUT_FAILED",error_);}
        close();report("closed");
    }
    static DWORD WINAPI worker(void *p){
        __try{((Mixer*)p)->run();return 0;}
        __except(diagnosticException(GetExceptionInformation(),L"wave_mixer")){return 1;}
    }
  public:
    bool retained;
    Mixer(const WaveOutputConfig &policy):stop_(0),thread_(0),wave_(0),policy_(policy),route_(0),references_(0),next_(0),
        headers_(policy.bufferCount),samples_(policy.bufferCount,std::vector<short>(policy.framesPerBuffer*2)),
        prepared_(policy.bufferCount,false),credits_(policy.bufferCount,std::vector<unsigned>(Streams,0)),
        requested_(false),submitted_(0),completed_(0),lastRefill_(0),refillGapMax_(0),fillMs_(0),writeMs_(0),fillMax_(0),writeMax_(0),retained(false){
        InitializeCriticalSection(&mutex_);ZeroMemory(streams_,sizeof(streams_));
        ZeroMemory(&headers_[0],headers_.size()*sizeof(WAVEHDR));stop_=CreateEventW(0,TRUE,FALSE,0);
        if(!stop_){DeleteCriticalSection(&mutex_);throw std::runtime_error("Audio stop event failed");}
    }
    ~Mixer(){delete route_;if(thread_)CloseHandle(thread_);CloseHandle(stop_);DeleteCriticalSection(&mutex_);}
    unsigned add(Stream *s){Lock lock(mutex_);require(error_.empty(),error_.c_str());
        for(unsigned i=0;i<Streams;++i)if(!streams_[i]){streams_[i]=s;++references_;
            if(!thread_){thread_=CreateThread(0,0,worker,this,0,0);if(!thread_){streams_[i]=0;--references_;throw std::runtime_error("Audio worker failed");}}return i;}
        throw std::runtime_error("Too many shared audio streams");}
    bool remove(unsigned slot){{Lock lock(mutex_);streams_[slot]=0;--references_;
        for(unsigned b=0;b<policy_.bufferCount;++b)credits_[b][slot]=0;if(references_)return false;}
        SetEvent(stop_);if(thread_)WaitForSingleObject(thread_,INFINITE);return true;}
    void feed(unsigned slot,const Bytes &pcm){DWORD begin=GetTickCount();Lock lock(mutex_);DWORD acquired=GetTickCount();require(error_.empty(),error_.c_str());
        Stream &s=*streams_[slot];s.received+=pcm.size();if(!s.paused)s.pcm.push(pcm);
        if(s.pcm.size()>s.queuePeak)s.queuePeak=s.pcm.size();
        s.feedWaitMs+=DWORD(acquired-begin);s.feedCopyMs+=DWORD(GetTickCount()-acquired);}
    void pause(unsigned slot,bool value){Lock lock(mutex_);streams_[slot]->paused=value;streams_[slot]->pcm.clear();}
    unsigned played(unsigned slot){Lock lock(mutex_);Stream &s=*streams_[slot];return unsigned(s.completed*s.rate*s.channels*2/48000);}
    void status(){Lock lock(mutex_);require(error_.empty(),error_.c_str());}
    Value metrics(unsigned slot){Lock lock(mutex_);Stream &s=*streams_[slot];Value v=Value::dictionary();
        v.dict["q_ms"]=Value::num(s.pcm.size()/48);v.dict["q_peak_ms"]=Value::num(s.queuePeak/48);
        v.dict["device_content_ms"]=Value::num(s.inflight/48);
        v.dict["dropped_frames"]=Value::num(s.pcm.dropped);v.dict["completed_frames"]=Value::num(s.completed);
        v.dict["empty_frames"]=Value::num(s.emptyFrames);
        v.dict["refill_gap_max_ms"]=Value::num(refillGapMax_);
        v.dict["feed_lock_ms_total"]=Value::num(s.feedWaitMs);v.dict["feed_copy_ms_total"]=Value::num(s.feedCopyMs);
        v.dict["fill_ms_total"]=Value::num(fillMs_);v.dict["fill_max_ms"]=Value::num(fillMax_);
        v.dict["write_ms_total"]=Value::num(writeMs_);v.dict["write_max_ms"]=Value::num(writeMax_);
        // Shared device peak is cumulative so sampling one stream cannot erase
        // another stream's evidence. Empty frames include initial prefill.
        v.dict["paused"]=Value::flag(s.paused);s.queuePeak=s.pcm.size();return v;}
};
struct Shared {
    CRITICAL_SECTION lock;Mixer *mixer;const WaveOutputConfig *config;
    Shared():mixer(0),config(0){InitializeCriticalSection(&lock);}~Shared(){DeleteCriticalSection(&lock);}
} shared;
class Output:public AudioOutputBackend {
    Stream state_;Mixer *m_;unsigned slot_;
  public:
    Output(unsigned r,unsigned c,const WaveOutputConfig &policy):state_(r,c),m_(0){Lock lock(shared.lock);
        require(!shared.mixer || shared.config==&policy,"Shared wave output configuration changed while active");
        if(!shared.mixer){shared.mixer=new Mixer(policy);shared.config=&policy;}
        m_=shared.mixer;slot_=m_->add(&state_);
    }
    ~Output(){Lock lock(shared.lock);if(m_->remove(slot_)){
        // Preserve buffers and the error state if the driver still owns them.
        if(!m_->retained){delete m_;shared.mixer=0;shared.config=0;}
    }}
    void feed(const Bytes &pcm){m_->feed(slot_,pcm);}
    void reap(){m_->status();}
    unsigned playedBytes()const{return m_->played(slot_);}
    void pause(bool value){m_->pause(slot_,value);}
    Value metrics()const{return m_->metrics(slot_);}
};
}
AudioOutputBackend *createSharedWaveOutput(unsigned rate,unsigned channels,const WaveOutputConfig &policy){
    require(policy.name && policy.bufferCount>=1 && policy.bufferCount<=32 &&
        policy.framesPerBuffer>=1 && policy.framesPerBuffer<=48000,"Invalid wave mixer policy");
    return new Output(rate,channels,policy);
}
}
