#include "../../platform/common/legacy_media.h"
#include "../../devices/mrn/audio_output.h"
#include <cstdio>
using namespace cp;
#ifdef WINXPLAY_AUDIO_RECOVERY_HOST_TEST
namespace cp {
static std::map<std::wstring,Bytes> reports;
std::wstring executableDirectory(){return L"test";}
std::wstring environment(const wchar_t *,const wchar_t *fallback){return fallback;}
void writeFile(const std::wstring &path,const Bytes &data){reports[path]=data;}
Bytes readFile(const std::wstring &path,size_t){return reports[path];}
AudioOutputBackend *createWaveOutput(unsigned,unsigned){
    throw std::runtime_error("Host recovery regression must not open real audio");
}
}
#endif
namespace {
bool busy=true,writeFails=false,realDevice=false,lastPaused=false;
unsigned opens=0,closes=0;
Bytes lastPcm;
struct FakeOutput:AudioOutputBackend {
    unsigned position;
    FakeOutput():position(0){}
    ~FakeOutput(){++closes;}
    void feed(const Bytes &pcm){
        if(writeFails)throw std::runtime_error("waveOutWrite failed: MMRESULT=1");
        lastPcm=pcm;position+=pcm.size();
    }
    void reap(){}
    unsigned playedBytes() const{return position;}
    void pause(bool value){lastPaused=value;}
};
}
namespace cp {
AudioOutputBackend *createAudioOutputBackend(const std::wstring &,unsigned r,unsigned c){
    return createMrnRecoveringOutput(r,c);
}
AudioOutputBackend *createMrnTtsOutput(unsigned r,unsigned c){
    ++opens;
    if(realDevice)return createWaveOutput(r,c);
    if(busy)throw std::runtime_error("waveOutOpen failed: MMRESULT=4");
    return new FakeOutput;
}
AudioInputBackend *createAudioInputBackend(const std::wstring &,unsigned,unsigned,unsigned){
    throw std::runtime_error("Microphone must not be opened");
}
}
static int audioOutputSmoke(){
    Value result=Value::dictionary();
    try {
        {
            LiveAudio audio(48000,2);
            require(opens==1,"Initial device attempt missing");
            audio.receive(unhex("1234abcd"));
            require(opens==1 && audio.playedBytes()==0,"Busy retry not throttled");
            Value state=parsePlist(readFile(executableDirectory()+L"/audio-recovery.plist"));
            require(state.at("last_device_error").string.find("MMRESULT=4")!=std::string::npos,"Missing device error");
            busy=false;Sleep(2100);audio.receive(unhex("1234abcd"));
            require(opens==2 && hex(lastPcm)=="3412cdab" && audio.playedBytes()==4,"PCM endian/recovery failed");
            writeFails=true;audio.receive(unhex("00010002"));
            require(closes==1 && audio.playedBytes()==4,"Write failure cleanup failed");
            audio.pause(true);audio.receive(unhex("00010002"));
            require(opens==2,"Write failure retry storm");
            writeFails=false;Sleep(2100);audio.receive(unhex("00010002"));
            require(opens==3 && lastPaused && audio.playedBytes()==8,"Recovery lost pause/position");
            audio.pause(false);require(!lastPaused,"Resume failed");
            bool invalid=false;try{audio.feed(Bytes(3,0));}catch(...){invalid=true;}
            require(invalid,"Invalid PCM was silently swallowed");
        }
        require(closes==2,"Device cleanup failed");
        Value state=parsePlist(readFile(executableDirectory()+L"/audio-recovery.plist"));
        require(state.at("device_failures").integer==2 && !state.at("physical_audio_verified").boolean,"Invalid diagnostics");
        writeFile(executableDirectory()+L"/audio-recovery-report.plist",xmlPlist(state));
        bool aacRejected=false;try{LiveAudio audio(48000,2,true);}catch(...){aacRejected=true;}
        require(aacRejected,"Unadvertised AAC was accepted without a decoder");
#ifndef WINXPLAY_AUDIO_RECOVERY_HOST_TEST
        realDevice=true;
        {
            LiveAudio audio(48000,2);audio.feed(Bytes(1920,0));Sleep(100);audio.reap();
            state=parsePlist(readFile(executableDirectory()+L"/audio-recovery.plist"));
            require(state.at("device_open").boolean && state.at("device_failures").integer==0,"Emulator waveOut did not open");
        }
        state=parsePlist(readFile(executableDirectory()+L"/audio-recovery.plist"));
        require(state.at("device_failures").integer==0,"Emulator waveOut submission failed");
        result.dict["emulator_waveout_pcm_submitted"]=Value::flag(true);
#endif
        result.dict["busy_and_write_failure_recovered"]=Value::flag(true);
        result.dict["pcm_endian_and_invalid_data_checked"]=Value::flag(true);
        result.dict["pause_preserved_on_reopen"]=Value::flag(true);
        result.dict["unadvertised_aac_rejected"]=Value::flag(aacRejected);
        result.dict["passed"]=Value::flag(true);
    }catch(const std::exception &e){result.dict["passed"]=Value::flag(false);result.dict["error"]=Value::str(e.what());}
    result.dict["physical_audio_verified"]=Value::flag(false);
    writeFile(executableDirectory()+L"/audio-output-smoke.plist",xmlPlist(result));
    return result.at("passed").boolean?0:1;
}
#ifdef WINXPLAY_AUDIO_RECOVERY_HOST_TEST
int main(){
    int status=audioOutputSmoke();
    Value report=parsePlist(readFile(L"test/audio-output-smoke.plist"));
    if(status)fprintf(stderr,"Audio recovery failed: %s\n",report.at("error").string.c_str());
    else puts("MRN audio recovery: retry throttling, PCM conversion, write failure, pause, position and AAC rejection passed.");
    return status;
}
#else
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPWSTR,int){return audioOutputSmoke();}
#endif
