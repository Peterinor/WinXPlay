#include "audio_route.h"
#include "../../platform/wince/wave_mixer.h"
#include "../../platform/common/diagnostics.h"
#include <sstream>
namespace cp { namespace {
class TtsRoute : public WaveAudioRoute {
    HANDLE handle_;bool requested_;
    void check(DWORD error,const char *stage){
        if(error){std::ostringstream s;s<<stage<<" error="<<error;throw std::runtime_error(s.str());}
    }
  public:
    TtsRoute():handle_(INVALID_HANDLE_VALUE),requested_(false){}
    ~TtsRoute(){release();}
    void acquire(){
        handle_=CreateFileW(L"AVV0:",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);
        check(handle_==INVALID_HANDLE_VALUE?GetLastError():0,"AVV0 open");
        BYTE source=4;requested_=DeviceIoControl(handle_,0x33,&source,1,0,0,0,0)!=FALSE;
        check(requested_?0:(GetLastError()?GetLastError():1),"TTS request");
        diagnosticEvent("AUDIO_TTS_REQUEST","accepted; settling asynchronously for 2000ms");
    }
    void release(){
        if(requested_){BYTE source=4;BOOL ok=DeviceIoControl(handle_,0x34,&source,1,0,0,0,0);
            diagnosticEvent("AUDIO_TTS_RELEASE",ok?"accepted":"failed");requested_=false;}
        if(handle_!=INVALID_HANDLE_VALUE){CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;}
    }
};
WaveAudioRoute *createRoute(){return new TtsRoute;}
// SSI starts after its sixth WRITE: prefill ten 100-ms buffers, keep one shared
// output, and pause logical streams in software instead of Pause/Restart.
const WaveOutputConfig policy={1,10,4800,2000,"mrn_tts_pcm",createRoute};
}
AudioOutputBackend *createMrnAudioOutput(unsigned rate,unsigned channels){
    return createSharedWaveOutput(rate,channels,policy);
}
}
