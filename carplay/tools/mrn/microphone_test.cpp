// Standard waveIn capture, with a separate F3.21 OEM dictaphone-route stage.
#include "suite.h"
#include "microphone_pcm.h"
#include "microphone_window.h"
#include "microphone_route.h"
#include <mmsystem.h>
#include <sstream>
using namespace cp;
namespace {
HANDLE micLog=INVALID_HANDLE_VALUE;
bool cancelled=false,ioFailed=false;
DWORD started=0;
std::wstring status,folder;
Value report;
void pump(){if(InterlockedCompareExchange(&microphone_ui::cancelled,0,0))cancelled=true;}
void log(const std::string& text,bool updateUi=true) {
    std::ostringstream s;s<<"["<<GetTickCount()-started<<"] "<<text<<"\r\n";
    std::string line=s.str();DWORD n=0;
    if(!WriteFile(micLog,line.data(),DWORD(line.size()),&n,0)||n!=line.size()||!FlushFileBuffers(micLog))ioFailed=true;
    status=wide(text);if(updateUi)microphone_ui::setStatus(status);
}
MMRESULT result(const char* name,MMRESULT code) {
    std::ostringstream s;s<<name<<" result="<<code;log(s.str());return code;
}
void save(){
    report.dict["paint_count"]=Value::num(InterlockedCompareExchange(&microphone_ui::paints,0,0));
    report.dict["physical_window_verified"]=Value::flag(false);
    writeFile(folder+L"/report.plist",xmlPlist(report));
}
bool writeAll(HANDLE f,const void* p,DWORD size){DWORD n=0;return WriteFile(f,p,size,&n,0)&&n==size;}
void waitMs(DWORD milliseconds){DWORD t=GetTickCount();while(!cancelled&&DWORD(GetTickCount()-t)<milliseconds){pump();Sleep(10);}}
struct MicRoute {
    HANDLE handle;bool requested,cleanupOk;
    MicRoute():handle(INVALID_HANDLE_VALUE),requested(false),cleanupOk(true){}
    bool command(DWORD action,const char* name) {
        SetLastError(0);BOOL ok=MrnMicrophoneRouteCommand(handle,action);
        DWORD error=ok?0:GetLastError();
        Value row=Value::dictionary();row.dict["operation"]=Value::str(name);
        row.dict["request_accepted"]=Value::flag(ok!=FALSE);row.dict["error"]=Value::num(error);
        report.dict["route_requests"].array.push_back(row);
        std::ostringstream s;s<<"MIC_ROUTE "<<name<<" source=0x10 accepted="<<ok<<" error="<<error;log(s.str());
        return ok!=FALSE;
    }
    bool open() {
        SetLastError(0);
        handle=CreateFileW(L"AVV0:",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);
        DWORD error=handle==INVALID_HANDLE_VALUE?GetLastError():0;
        report.dict["route_open_error"]=Value::num(error);
        if(handle==INVALID_HANDLE_VALUE){std::ostringstream s;s<<"MIC_ROUTE AVV0 open failed error="<<error;log(s.str());return false;}
        // Once a request is sent, also attempt release if its return is failure.
        requested=true;
        if(!command(1,"available"))return false;
        return command(2,"select");
    }
    void close() {
        if(handle==INVALID_HANDLE_VALUE)return;
        if(requested){
            bool unavailable=command(3,"unavailable");
            bool unselect=command(4,"unselect");
            cleanupOk=unavailable&&unselect;requested=false;
        }
        if(!CloseHandle(handle))cleanupOk=false;
        handle=INVALID_HANDLE_VALUE;
        report.dict["route_release_requests_ok"]=Value::flag(cleanupOk);
        report.dict["original_source_restored_verified"]=Value::flag(false);
    }
};
struct Capture {
    enum { Blocks=8 };
    HWAVEIN wave;
    WAVEHDR headers[Blocks];
    Bytes buffers[Blocks];
    bool prepared[Blocks];
    Capture():wave(0){ZeroMemory(headers,sizeof(headers));ZeroMemory(prepared,sizeof(prepared));}
    // Caller deliberately retains storage if the driver has not released headers.
    bool close() {
        if(!wave)return true;
        bool ok=true;
        log("waveInReset begin");
        if(result("waveInReset",waveInReset(wave)))ok=false;
        for(unsigned i=0;i<Blocks;++i)if(prepared[i]) {
            MMRESULT c=result("waveInUnprepareHeader",waveInUnprepareHeader(wave,&headers[i],sizeof(WAVEHDR)));
            if(c)ok=false;else prepared[i]=false;
        }
        MMRESULT c=result("waveInClose",waveInClose(wave));if(c)ok=false;else wave=0;
        return ok;
    }
};
bool capture(const char* stage,unsigned device,unsigned rate,unsigned channels,unsigned seconds,Value& attempt) {
    WAVEFORMATEX fmt={0};fmt.wFormatTag=WAVE_FORMAT_PCM;fmt.nChannels=WORD(channels);
    fmt.nSamplesPerSec=rate;fmt.wBitsPerSample=16;fmt.nBlockAlign=WORD(channels*2);fmt.nAvgBytesPerSec=rate*channels*2;
    attempt.dict["device"]=Value::num(device);attempt.dict["sample_rate"]=Value::num(rate);
    attempt.dict["channels"]=Value::num(channels);attempt.dict["requested_seconds"]=Value::num(seconds);
    attempt.dict["stage"]=Value::str(stage);
    std::ostringstream label;label<<stage<<" DEVICE "<<device<<" RATE "<<rate<<" CHANNELS "<<channels;
    log(label.str());log("waveInOpen QUERY begin");
    MMRESULT query=result("waveInOpen QUERY",waveInOpen(0,device,&fmt,0,0,WAVE_FORMAT_QUERY));
    attempt.dict["query_result"]=Value::num(query);
    // Still try a real open: some older drivers implement format-query poorly.
    Capture* c=new Capture;
    log("waveInOpen begin");MMRESULT opened=result("waveInOpen",waveInOpen(&c->wave,device,&fmt,0,0,CALLBACK_NULL));
    attempt.dict["open_result"]=Value::num(opened);
    if(opened){delete c;return true;}
    HANDLE wav=INVALID_HANDLE_VALUE;
    unsigned bytes=0,blocks=0;microphone_pcm::Meter meters[2];
    DWORD begin=0,last=0;
    bool valid=true;
    try {
        std::ostringstream filename;filename<<stage<<"-device-"<<device<<"-"<<rate<<"hz-"<<channels<<"ch.wav";
        attempt.dict["wav_file"]=Value::str(filename.str());
        wav=CreateFileW((folder+L"/"+wide(filename.str())).c_str(),GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_NEW,0,0);
        require(wav!=INVALID_HANDLE_VALUE,"Cannot create WAV file");
        unsigned char head[44];microphone_pcm::wavHeader(head,rate,channels,0);
        require(writeAll(wav,head,44),"Cannot write WAV header");
        for(unsigned i=0;i<Capture::Blocks;++i){
            c->buffers[i].resize(rate/10*channels*2);c->headers[i].lpData=(LPSTR)&c->buffers[i][0];
            c->headers[i].dwBufferLength=DWORD(c->buffers[i].size());
            MMRESULT code=result("waveInPrepareHeader",waveInPrepareHeader(c->wave,&c->headers[i],sizeof(WAVEHDR)));
            require(!code,"waveInPrepareHeader failed");c->prepared[i]=true;
            require(!result("waveInAddBuffer",waveInAddBuffer(c->wave,&c->headers[i],sizeof(WAVEHDR))),"waveInAddBuffer failed");
        }
        log("waveInStart begin");require(!result("waveInStart",waveInStart(c->wave)),"waveInStart failed");
        begin=last=GetTickCount();unsigned next=0;
        while(!cancelled&&!ioFailed&&DWORD(GetTickCount()-begin)<seconds*1000){
            // Bound work per pass so a misbehaving driver cannot starve cancellation.
            for(unsigned n=0;n<Capture::Blocks;++n){
                WAVEHDR& h=c->headers[next];if(!(h.dwFlags&WHDR_DONE))break;
                require(h.dwBytesRecorded<=h.dwBufferLength,"Driver returned oversized buffer");
                require(bytes+h.dwBytesRecorded<=fmt.nAvgBytesPerSec*(seconds+2),"Driver returned implausible PCM volume");
                require(microphone_pcm::measure((unsigned char*)h.lpData,h.dwBytesRecorded,channels,meters),"Unaligned PCM frame");
                require(writeAll(wav,h.lpData,h.dwBytesRecorded),"WAV write failed");
                bytes+=h.dwBytesRecorded;++blocks;h.dwBytesRecorded=0;
                MMRESULT code=waveInAddBuffer(c->wave,&h,sizeof(h));
                if(code){result("waveInAddBuffer requeue",code);require(false,"waveInAddBuffer requeue failed");}
                next=(next+1)%Capture::Blocks;
            }
            if(DWORD(GetTickCount()-last)>=1000){
                last=GetTickCount();std::ostringstream s;s<<label.str()<<" elapsed="<<(last-begin)/1000<<"/"<<seconds
                    <<" bytes="<<bytes<<" peak_L="<<meters[0].peak<<" peak_R="<<meters[1].peak;log(s.str());
                microphone_pcm::wavHeader(head,rate,channels,bytes);
                require(SetFilePointer(wav,0,0,FILE_BEGIN)!=INVALID_SET_FILE_POINTER&&writeAll(wav,head,44),"WAV header update failed");
                require(SetFilePointer(wav,0,0,FILE_END)!=INVALID_SET_FILE_POINTER&&FlushFileBuffers(wav),"WAV flush failed");
            }
            pump();Sleep(10);
        }
    }catch(const std::exception& e){valid=false;attempt.dict["error"]=Value::str(e.what());log(e.what());}
    if(begin)attempt.dict["elapsed_ms"]=Value::num(DWORD(GetTickCount()-begin));
    bool released=c->close();attempt.dict["cleanup_ok"]=Value::flag(released);
    if(released)delete c;else log("CLEANUP_FAILED: retain driver buffers, stop further capture attempts");
    if(wav!=INVALID_HANDLE_VALUE){
        unsigned char head[44];microphone_pcm::wavHeader(head,rate,channels,bytes);
        if(SetFilePointer(wav,0,0,FILE_BEGIN)==INVALID_SET_FILE_POINTER||!writeAll(wav,head,44)||!FlushFileBuffers(wav)){valid=false;ioFailed=true;}
        CloseHandle(wav);
    }
    attempt.dict["captured_pcm_bytes"]=Value::num(bytes);attempt.dict["completed_buffers"]=Value::num(blocks);
    attempt.dict["capture_completed"]=Value::flag(valid&&!cancelled&&!ioFailed&&released&&bytes>0);
    attempt.dict["signal_detected"]=Value::flag(meters[0].nonzero||meters[1].nonzero);
    attempt.dict["voice_verified"]=Value::flag(false);attempt.dict["channel_meters"]=Value::list();
    for(unsigned j=0;j<channels;++j){Value v=Value::dictionary();v.dict["samples"]=Value::num(meters[j].samples);
        v.dict["nonzero_samples"]=Value::num(meters[j].nonzero);v.dict["peak"]=Value::num(meters[j].peak);
        v.dict["clipped_samples"]=Value::num(meters[j].clipped);
        v.dict["mean_absolute"]=Value::num(meters[j].samples?unsigned(meters[j].absoluteSum/meters[j].samples):0);
        attempt.dict["channel_meters"].array.push_back(v);}
    log(!bytes?"CAPTURE_END: no PCM captured":(meters[0].nonzero||meters[1].nonzero?
        "CAPTURE_END: nonzero PCM saved; listen on PC to verify voice":"CAPTURE_END: PCM received but ALL ZERO; no voice recorded"));
    return released;
}
}
int MicrophoneTest(HINSTANCE instance,const Value& config) {
    unsigned seconds=suiteNumber(config,"seconds",6,30);require(seconds>0,"seconds must be positive");
    unsigned delay=suiteNumber(config,"delay_seconds",10,120);
    cancelled=ioFailed=false;microphone_ui::initialize();
    started=GetTickCount();report=Value::dictionary();
    report.dict["version"]=Value::str("microphone-v4");report.dict["attempts"]=Value::list();
    report.dict["route_requests"]=Value::list();report.dict["route_activation_verified"]=Value::flag(false);
    report.dict["devices"]=Value::list();report.dict["voice_verified"]=Value::flag(false);
    int exitCode=1;MicRoute route;
    try {
        for(unsigned i=1;;++i){std::ostringstream s;s<<"/microphone-"<<i;folder=executableDirectory()+wide(s.str());
            if(CreateDirectoryW(folder.c_str(),0))break;
            require(GetLastError()==ERROR_ALREADY_EXISTS,"Cannot create microphone results directory");}
        micLog=CreateFileW((folder+L"/capture.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_NEW,0,0);
        require(micLog!=INVALID_HANDLE_VALUE,"Cannot create microphone log");
        log("MICROPHONE_V4: mono PCM16 baseline 16000/22050, then OEM dictaphone source 0x10 + 22050.");save();
        report.dict["display"]=Value::str("independent-topmost");
        if(config.dict.count("display"))log("DISPLAY_CONFIG_IGNORED: independent window");
        log("BOOT_DELAY: waiting before window registration");waitMs(delay*1000);
        require(!cancelled,"Cancelled during startup");
        bool uiReady=microphone_ui::start();
        report.dict["ui_start_error"]=Value::num(microphone_ui::error);
        report.dict["foreground_accepted"]=Value::flag(microphone_ui::foregroundAccepted);
        require(uiReady,"Independent UI startup failed");
        log("WINDOW_READY: independent class, TOPMOST, dedicated UI thread; no BAND required");
        log("CAPTURE_BEGIN: say numbers, pause, repeat");save();
        unsigned devices=waveInGetNumDevs();report.dict["device_count"]=Value::num(devices);
        std::ostringstream s;s<<"waveInGetNumDevs="<<devices;log(s.str());save();
        bool safe=true;unsigned completed=0,signals=0;
        for(unsigned d=0;d<devices&&safe&&!cancelled&&!ioFailed;++d){
            WAVEINCAPS caps={0};MMRESULT code=result("waveInGetDevCaps",waveInGetDevCaps(d,&caps,sizeof(caps)));
            caps.szPname[MAXPNAMELEN-1]=0;
            Value device=Value::dictionary();device.dict["id"]=Value::num(d);device.dict["caps_result"]=Value::num(code);
            if(!code){device.dict["name"]=Value::str(utf8(caps.szPname));device.dict["channels"]=Value::num(caps.wChannels);device.dict["formats"]=Value::num(caps.dwFormats);}
            report.dict["devices"].array.push_back(device);save();
        }
        // F3.21 field unit exposes one waveIn device. Do not probe other devices
        // under an MRN-specific route whose hardware mapping is not established.
        require(devices>0,"No waveIn devices");
        const char* stages[]={"baseline-16k","baseline-22k","dictaphone-22k"};
        unsigned rates[]={16000,22050,22050};
        for(unsigned r=0;r<3&&safe&&!cancelled&&!ioFailed;++r){
            if(r==2){
                log("MIC_ROUTE: requesting OEM recording source; native recorder and calls must be idle");
                bool accepted=route.open();save();
                if(!accepted){log("MIC_ROUTE_FAILED: routed capture skipped");report.dict["routed_capture_skipped"]=Value::flag(true);break;}
                log("MIC_ROUTE_SETTLE: wait 2 seconds; request acceptance is NOT activation confirmation");
                waitMs(2000);if(cancelled||ioFailed)break;
            }
            Value attempt=Value::dictionary();safe=capture(stages[r],0,rates[r],1,seconds,attempt);
            if(attempt.dict.count("capture_completed")&&attempt.at("capture_completed").boolean)++completed;
            if(attempt.dict.count("signal_detected")&&attempt.at("signal_detected").boolean)++signals;
            report.dict["attempts"].array.push_back(attempt);save();
        }
        route.close();
        report.dict["successful_captures"]=Value::num(completed);report.dict["cancelled"]=Value::flag(cancelled);
        report.dict["signal_captures"]=Value::num(signals);
        bool scanComplete=report.dict["attempts"].array.size()==3&&safe&&!cancelled&&!ioFailed&&route.cleanupOk;
        report.dict["scan_completed"]=Value::flag(scanComplete);
        exitCode=signals&&scanComplete?0:2;
        log(signals?"SIGNAL_DETECTED: verify human voice by listening to WAV on PC":"NO_SIGNAL: no nonzero PCM captured");
        log("TEST_FINISHED: results saved. Window closes in 5 seconds.");save();
    }catch(const std::exception& e){if(micLog!=INVALID_HANDLE_VALUE)log(e.what());report.dict["error"]=Value::str(e.what());try{save();}catch(...){}exitCode=1;}
    // Includes cancellation, open/select failure, and exception exits.
    try{route.close();}catch(...){ioFailed=true;}
    bool uiExited=microphone_ui::stop();
    report.dict["ui_thread_exited"]=Value::flag(uiExited);
    report.dict["window_destroyed"]=Value::flag(microphone_ui::destroyed!=0);
    if(micLog!=INVALID_HANDLE_VALUE)log(uiExited?"UI_THREAD_EXIT_CONFIRMED":"UI_THREAD_EXIT_NOT_CONFIRMED",false);
    try{save();}catch(...){ioFailed=true;}
    if(micLog!=INVALID_HANDLE_VALUE){FlushFileBuffers(micLog);CloseHandle(micLog);micLog=INVALID_HANDLE_VALUE;}
    return ioFailed?1:exitCode;
}
