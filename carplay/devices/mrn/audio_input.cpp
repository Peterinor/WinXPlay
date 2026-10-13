#include "../../platform/wince/media_backend.h"
#include "../../platform/common/pcm_input.h"
#include "../../platform/common/diagnostics.h"
#include <mmsystem.h>
#include <sstream>
namespace cp { namespace {
// One OEM recording source and input device; never arbitrate two local users.
LONG microphoneOwned=0;
class MrnMicrophone : public AudioInputBackend {
    struct Storage {
        HWAVEIN wave; WAVEHDR headers[8];Bytes buffers[8];bool prepared[8];
        Storage():wave(0){ZeroMemory(headers,sizeof(headers));ZeroMemory(prepared,sizeof(prepared));}
    };
    HANDLE av_;bool requested_,owned_;DWORD requestedAt_,reportedAt_;
    Storage* storage_;unsigned next_;PcmInputPackets packets_;U64 bytes_,nonzero_;
    void check(MMRESULT code,const char* stage){
        if(code){std::ostringstream s;s<<stage<<" result="<<code;throw std::runtime_error(s.str());}
    }
    bool route(DWORD code,BYTE available,bool availability){
        BYTE data[2]={0x10,available};SetLastError(0);
        BOOL ok=DeviceIoControl(av_,code,data,availability?2:1,0,0,0,0);
        DWORD error=ok?0:GetLastError();std::ostringstream s;
        s<<"source=16 ioctl="<<code<<" available="<<unsigned(available)<<" accepted="<<ok<<" error="<<error;
        diagnosticEvent("MIC_ROUTE",s.str());return ok!=FALSE;
    }
    void close(){
        bool safe=true;
        if(storage_&&storage_->wave){
            if(waveInReset(storage_->wave)!=0)safe=false;
            for(unsigned i=0;i<8;++i)if(storage_->prepared[i]){
                if(waveInUnprepareHeader(storage_->wave,&storage_->headers[i],sizeof(WAVEHDR))!=0)safe=false;
            }
            if(waveInClose(storage_->wave)!=0)safe=false;
        }
        if(safe)delete storage_; // Retain driver-owned memory on failed cleanup.
        storage_=0;
        if(requested_){bool a=route(4,0,true);bool b=route(2,0,false);
            if(!a||!b)diagnosticEvent("MIC_RELEASE_FAILED","OEM source restoration unverified");requested_=false;}
        if(av_!=INVALID_HANDLE_VALUE){CloseHandle(av_);av_=INVALID_HANDLE_VALUE;}
        if(owned_&&safe)InterlockedExchange(&microphoneOwned,0);
        if(!safe)diagnosticEvent("MIC_CLEANUP_FAILED","buffers retained; further opens blocked until process restart");
        owned_=false;
    }
    void start(){
        storage_=new Storage;
        WAVEFORMATEX f={0};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=1;
        f.nSamplesPerSec=22050;f.wBitsPerSample=16;f.nBlockAlign=2;f.nAvgBytesPerSec=44100;
        check(waveInOpen(&storage_->wave,0,&f,0,0,CALLBACK_NULL),"waveInOpen");
        for(unsigned i=0;i<8;++i){
            storage_->buffers[i].resize(4410);WAVEHDR& h=storage_->headers[i];
            h.lpData=(LPSTR)&storage_->buffers[i][0];h.dwBufferLength=4410;
            check(waveInPrepareHeader(storage_->wave,&h,sizeof(h)),"waveInPrepareHeader");storage_->prepared[i]=true;
            check(waveInAddBuffer(storage_->wave,&h,sizeof(h)),"waveInAddBuffer");
        }
        check(waveInStart(storage_->wave),"waveInStart");
        diagnosticEvent("MIC_CAPTURE_STARTED","waveIn=0 rate=22050 channels=1 bits=16");
    }
  public:
    MrnMicrophone(unsigned rate,unsigned channels,unsigned frames):av_(INVALID_HANDLE_VALUE),requested_(false),owned_(false),
        requestedAt_(0),reportedAt_(0),storage_(0),next_(0),packets_(rate,frames),bytes_(0),nonzero_(0){
        require(channels==1,"MRN microphone requires mono");
        require(InterlockedCompareExchange(&microphoneOwned,1,0)==0,"MRN microphone already owned or cleanup failed");owned_=true;
        try{
            av_=CreateFileW(L"AVV0:",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);
            require(av_!=INVALID_HANDLE_VALUE,"Microphone AVV0 open failed");
            requested_=true;require(route(4,1,true),"Microphone source availability failed");
            require(route(1,0,false),"Microphone source selection failed");requestedAt_=GetTickCount();
            std::ostringstream s;s<<"capture=22050 negotiated="<<rate<<" frames_per_packet="<<frames<<" settle_ms=2000";
            diagnosticEvent("MIC_REQUEST",s.str());
        }catch(...){close();throw;}
    }
    ~MrnMicrophone(){close();}
    std::vector<Bytes> poll(){
        std::vector<Bytes> out;
        // Do not sleep on the receiver loop: video/control continue during settle.
        if(!storage_){if(DWORD(GetTickCount()-requestedAt_)<2000)return out;start();}
        for(unsigned i=0;i<8;++i){
            WAVEHDR& h=storage_->headers[next_];if(!(h.dwFlags&WHDR_DONE))break;
            require(h.dwBytesRecorded<=h.dwBufferLength&&h.dwBytesRecorded%2==0,"Invalid microphone buffer size");
            Bytes pcm((U8*)h.lpData,(U8*)h.lpData+h.dwBytesRecorded);bytes_+=pcm.size();
            for(size_t j=0;j<pcm.size();j+=2)if(pcm[j]||pcm[j+1])++nonzero_;
            packets_.append(pcm,out);h.dwBytesRecorded=0;
            check(waveInAddBuffer(storage_->wave,&h,sizeof(h)),"waveInAddBuffer requeue");next_=(next_+1)%8;
        }
        DWORD now=GetTickCount();if(DWORD(now-reportedAt_)>=1000){
            reportedAt_=now;std::ostringstream s;s<<"pcm_bytes="<<bytes_<<" nonzero_samples="<<nonzero_;
            diagnosticEvent("MIC_CAPTURE",s.str());
        }
        return out;
    }
};
}
AudioInputBackend *createMrnAudioInput(unsigned rate,unsigned channels,unsigned samples){return new MrnMicrophone(rate,channels,samples);}
}
