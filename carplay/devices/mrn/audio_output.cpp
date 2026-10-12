// MRN F3.21: verified AVV0 TTS route + device 1 + ten independent PCM buffers.
#include "audio_output.h"
#include "../../platform/common/diagnostics.h"
#include "audio_pcm.h"
#include <mmsystem.h>
#include <sstream>
namespace cp { namespace {
enum { Blocks=10,Frames=4800,Streams=8 };
struct Lock { CRITICAL_SECTION &m;Lock(CRITICAL_SECTION &v):m(v){EnterCriticalSection(&m);}~Lock(){LeaveCriticalSection(&m);} };
struct Stream {
    MrnPcmQueue pcm;unsigned rate,channels;bool paused;U64 received,completed;
    Stream(unsigned r,unsigned c):pcm(r,c),rate(r),channels(c),paused(false),received(0),completed(0){}
};
class Mixer {
    CRITICAL_SECTION mutex_;HANDLE stop_,thread_,av_;HWAVEOUT wave_;
    Stream *streams_[Streams];unsigned references_,next_;
    WAVEHDR headers_[Blocks];short samples_[Blocks][Frames*2];bool prepared_[Blocks];
    unsigned credits_[Blocks][Streams];bool requested_;std::string error_;
    U64 submitted_,completed_;DWORD lastReport_;
    void check(DWORD code,const char *stage){if(code){std::ostringstream s;s<<stage<<" error="<<code;throw std::runtime_error(s.str());}}
    void report(const char *stage){
        try{Value v=Value::dictionary();v.dict["backend"]=Value::str("mrn_tts_pcm");v.dict["stage"]=Value::str(stage);
            v.dict["wave_device"]=Value::num(1);v.dict["sample_rate"]=Value::num(48000);v.dict["channels"]=Value::num(2);
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
        Lock lock(mutex_);ZeroMemory(credits_[block],sizeof(credits_[block]));
        for(unsigned f=0;f<Frames;++f){int left=0,right=0;
            for(unsigned i=0;i<Streams;++i)if(streams_[i]&&!streams_[i]->paused){short l,r;
                if(streams_[i]->pcm.pop(l,r)){left+=l;right+=r;++credits_[block][i];}}
            samples_[block][f*2]=short(left>32767?32767:left<-32768?-32768:left);
            samples_[block][f*2+1]=short(right>32767?32767:right<-32768?-32768:right);
        }
    }
    void close(){
        bool released=true;
        if(wave_){
            MMRESULT reset=waveOutReset(wave_);if(reset)released=false;
            for(unsigned i=0;i<Blocks;++i)if(prepared_[i]){
                MMRESULT r=waveOutUnprepareHeader(wave_,&headers_[i],sizeof(WAVEHDR));
                if(r)released=false;else prepared_[i]=false;
            }
            MMRESULT result=waveOutClose(wave_);if(result)released=false;else wave_=0;
        }
        if(requested_){BYTE source=4;BOOL ok=DeviceIoControl(av_,0x34,&source,1,0,0,0,0);
            diagnosticEvent("AUDIO_TTS_RELEASE",ok?"accepted":"failed");requested_=false;}
        if(av_!=INVALID_HANDLE_VALUE){CloseHandle(av_);av_=INVALID_HANDLE_VALUE;}
        // A driver retaining WAVEHDR/data must never observe freed storage.
        if(!released){Lock lock(mutex_);error_="Audio driver did not release buffers";retained=true;}
    }
    void run(){
        try{
            av_=CreateFileW(L"AVV0:",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);
            check(av_==INVALID_HANDLE_VALUE?GetLastError():0,"AVV0 open");
            BYTE source=4;requested_=DeviceIoControl(av_,0x33,&source,1,0,0,0,0)!=FALSE;
            check(requested_?0:(GetLastError()?GetLastError():1),"TTS request");
            diagnosticEvent("AUDIO_TTS_REQUEST","accepted; settling asynchronously for 2000ms");report("route_settling");
            if(WaitForSingleObject(stop_,2000)==WAIT_OBJECT_0){close();report("closed");return;}
            WAVEFORMATEX f={0};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=2;f.nSamplesPerSec=48000;
            f.wBitsPerSample=16;f.nBlockAlign=4;f.nAvgBytesPerSec=192000;
            check(waveOutOpen(&wave_,1,&f,0,0,CALLBACK_NULL),"waveOutOpen device 1");
            for(unsigned i=0;i<Blocks;++i){headers_[i].lpData=(LPSTR)samples_[i];headers_[i].dwBufferLength=sizeof(samples_[i]);
                check(waveOutPrepareHeader(wave_,&headers_[i],sizeof(WAVEHDR)),"waveOutPrepareHeader");prepared_[i]=true;fill(i);}
            // No Pause/Restart: the SSI driver starts on its sixth WRITE.
            for(unsigned i=0;i<Blocks;++i){check(waveOutWrite(wave_,&headers_[i],sizeof(WAVEHDR)),"waveOutWrite initial");++submitted_;}
            diagnosticEvent("AUDIO_TTS_PLAYING","device=1 rate=48000 channels=2 buffers=10");report("playing");
            while(WaitForSingleObject(stop_,10)==WAIT_TIMEOUT){
                for(unsigned n=0;n<Blocks && (headers_[next_].dwFlags&WHDR_DONE);++n){
                    {Lock lock(mutex_);for(unsigned i=0;i<Streams;++i)if(streams_[i])streams_[i]->completed+=credits_[next_][i];}
                    ++completed_;fill(next_);check(waveOutWrite(wave_,&headers_[next_],sizeof(WAVEHDR)),"waveOutWrite refill");
                    ++submitted_;next_=(next_+1)%Blocks;
                }
                if(DWORD(GetTickCount()-lastReport_)>=1000){report("playing");lastReport_=GetTickCount();}
            }
        }catch(const std::exception &e){Lock lock(mutex_);error_=e.what();diagnosticEvent("AUDIO_TTS_FAILED",error_);}
        close();report("closed");
    }
    static DWORD WINAPI worker(void *p){
        __try{((Mixer*)p)->run();return 0;}
        __except(diagnosticException(GetExceptionInformation(),L"mrn_audio")){return 1;}
    }
  public:
    bool retained;
    Mixer():stop_(0),thread_(0),av_(INVALID_HANDLE_VALUE),wave_(0),references_(0),next_(0),
        requested_(false),submitted_(0),completed_(0),lastReport_(0),retained(false){
        InitializeCriticalSection(&mutex_);ZeroMemory(streams_,sizeof(streams_));ZeroMemory(headers_,sizeof(headers_));
        ZeroMemory(prepared_,sizeof(prepared_));ZeroMemory(credits_,sizeof(credits_));stop_=CreateEventW(0,TRUE,FALSE,0);
        if(!stop_){DeleteCriticalSection(&mutex_);throw std::runtime_error("Audio stop event failed");}
    }
    ~Mixer(){if(thread_)CloseHandle(thread_);CloseHandle(stop_);DeleteCriticalSection(&mutex_);}
    unsigned add(Stream *s){Lock lock(mutex_);require(error_.empty(),error_.c_str());
        for(unsigned i=0;i<Streams;++i)if(!streams_[i]){streams_[i]=s;++references_;
            if(!thread_){thread_=CreateThread(0,0,worker,this,0,0);if(!thread_){streams_[i]=0;--references_;throw std::runtime_error("Audio worker failed");}}return i;}
        throw std::runtime_error("Too many MRN audio streams");}
    bool remove(unsigned slot){{Lock lock(mutex_);streams_[slot]=0;--references_;
        for(unsigned b=0;b<Blocks;++b)credits_[b][slot]=0;if(references_)return false;}
        SetEvent(stop_);if(thread_)WaitForSingleObject(thread_,INFINITE);return true;}
    void feed(unsigned slot,const Bytes &pcm){Lock lock(mutex_);require(error_.empty(),error_.c_str());
        Stream &s=*streams_[slot];s.received+=pcm.size();if(!s.paused)s.pcm.push(pcm);}
    void pause(unsigned slot,bool value){Lock lock(mutex_);streams_[slot]->paused=value;streams_[slot]->pcm.clear();}
    unsigned played(unsigned slot){Lock lock(mutex_);Stream &s=*streams_[slot];return unsigned(s.completed*s.rate*s.channels*2/48000);}
    void status(){Lock lock(mutex_);require(error_.empty(),error_.c_str());}
};
struct Shared {CRITICAL_SECTION lock;Mixer *m;Shared():m(0){InitializeCriticalSection(&lock);}~Shared(){DeleteCriticalSection(&lock);}} shared;
class Output:public AudioOutputBackend {
    Stream state_;Mixer *m_;unsigned slot_;
  public:
    Output(unsigned r,unsigned c):state_(r,c),m_(0){Lock lock(shared.lock);
        if(!shared.m)shared.m=new Mixer;
        m_=shared.m;slot_=m_->add(&state_);
    }
    ~Output(){Lock lock(shared.lock);if(m_->remove(slot_)){
        // Keep retained buffers AND the shared error state, preventing retries
        // from opening another SSI stream after failed driver cleanup.
        if(!m_->retained){delete m_;shared.m=0;}
    }}
    void feed(const Bytes &pcm){m_->feed(slot_,pcm);}
    void reap(){m_->status();}
    unsigned playedBytes()const{return m_->played(slot_);}
    void pause(bool value){m_->pause(slot_,value);}
};
}
AudioOutputBackend *createMrnTtsOutput(unsigned rate,unsigned channels){return new Output(rate,channels);}
}
