// F3.21 autonomous TTS-channel / SSI PCM experiment. No process control.
#include <windows.h>
#include <mmsystem.h>

namespace voice_pcm {
enum { Rate=48000, Frames=4800, Blocks=10, BytesPerBlock=Frames*4 };
static short samples[Blocks][Frames*2];
static WAVEHDR headers[Blocks];
static HANDLE file=INVALID_HANDLE_VALUE;
static BOOL logOK=TRUE;
static DWORD start;

static void Log(const WCHAR* name,DWORD a=0,DWORD b=0){
    WCHAR line[384];DWORD written=0;
    wsprintfW(line,L"[%lu elapsed=%lu] %s a=%lu b=%lu\r\n",GetTickCount(),GetTickCount()-start,name,a,b);
    DWORD length=lstrlenW(line)*2;
    if(!WriteFile(file,line,length,&written,0)||written!=length||!FlushFileBuffers(file))logOK=FALSE;
}
static BOOL Route(HANDLE av,BOOL open){
    BYTE source=4;
    BOOL ok=DeviceIoControl(av,open?0x33:0x34,&source,1,0,0,0,0);
    DWORD error=ok?0:GetLastError();
    Log(open?L"TTS_CHANNEL_REQUEST_RESULT":L"TTS_CHANNEL_RELEASE_RESULT",ok,error);
    return ok;
}
static void Fill(){
    for(DWORD block=0;block<Blocks;++block){
        ZeroMemory(&headers[block],sizeof(WAVEHDR));
        headers[block].lpData=(LPSTR)samples[block];
        headers[block].dwBufferLength=BytesPerBlock;
        for(DWORD i=0;i<Frames;++i){
            // Two 100ms, 600Hz triangular tones per second, with 5ms ramps.
            LONG value=0;
            if(block==0||block==2){
                DWORD phase=i%80;
                value=phase<40?(LONG)phase*60-1200:3600-(LONG)phase*60;
                DWORD ramp=i<240?i:Frames-1-i<240?Frames-1-i:240;
                value=value*(LONG)ramp/240;
            }
            samples[block][i*2]=samples[block][i*2+1]=(short)value;
        }
    }
}
static void Position(HWAVEOUT wave,DWORD submitted,DWORD completed){
    MMTIME time;ZeroMemory(&time,sizeof(time));time.wType=TIME_SAMPLES;
    MMRESULT result=waveOutGetPosition(wave,&time,sizeof(time));
    Log(L"WAVE_POSITION_RESULT_TYPE",result,time.wType);
    if(!result)Log(L"WAVE_POSITION_RAW",time.u.sample);
    Log(L"BUFFERS_SUBMITTED_COMPLETED",submitted,completed);
}
}

extern "C" __declspec(dllexport) DWORD VoicePcmTest(DWORD delaySeconds,DWORD seconds){
    using namespace voice_pcm;
    if(delaySeconds>120||!seconds||seconds>60)return 18;
    start=GetTickCount();logOK=TRUE;file=INVALID_HANDLE_VALUE;
    WCHAR base[MAX_PATH],path[MAX_PATH];DWORD last=0;
    DWORD length=GetModuleFileNameW(0,base,MAX_PATH);
    if(!length||length>=MAX_PATH)return 11;
    for(DWORD k=0;k<length;++k)if(base[k]==L'\\'||base[k]==L'/')last=k+1;
    if(!last||last>200)return 11;base[last]=0;
    for(DWORD index=1;index<=9999;++index){
        wsprintfW(path,L"%svoice_pcm_%04lu.txt",base,index);
        file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);
        if(file!=INVALID_HANDLE_VALUE)break;
        DWORD error=GetLastError();if(error!=ERROR_FILE_EXISTS&&error!=ERROR_ALREADY_EXISTS)return 12;
    }
    if(file==INVALID_HANDLE_VALUE)return 12;
    WORD bom=0xfeff;DWORD written=0;
    if(!WriteFile(file,&bom,2,&written,0)||written!=2)logOK=FALSE;
    Log(L"START VOICE_PCM_V1",delaySeconds,seconds);
    Log(L"PLAN_DEVICE_RATE",1,Rate);Log(L"PLAN_BLOCKS_BYTES",Blocks,BytesPerBlock);
    Log(L"NO_PROCESS_KILL_NO_MANUAL_VOICE_ACTIVATION",0);
    HANDLE av=INVALID_HANDLE_VALUE;HWAVEOUT wave=0;
    BOOL requested=FALSE,prepared[Blocks]={0},queued[Blocks]={0};
    DWORD code=23,submitted=0,completed=0;
    if(logOK){
        for(DWORD waited=0;waited<delaySeconds&&logOK;++waited){Sleep(1000);if((waited+1)%10==0)Log(L"START_DELAY_SECONDS",waited+1);}
    }
    if(logOK){
        do {
            DWORD count=waveOutGetNumDevs();Log(L"WAVE_DEVICE_COUNT",count);
            if(count<=1){Log(L"DEVICE_1_NOT_PRESENT");break;}
            WAVEOUTCAPS caps;ZeroMemory(&caps,sizeof(caps));
            MMRESULT result=waveOutGetDevCaps(1,&caps,sizeof(caps));Log(L"DEVICE_1_CAPS",result);
            if(!result){Log(caps.szPname,caps.wChannels,caps.dwFormats);}
            av=CreateFileW(L"AVV0:",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);
            if(av==INVALID_HANDLE_VALUE){Log(L"AVV0_OPEN_ERROR",GetLastError());break;}
            if(!logOK)break;
            requested=Route(av,TRUE);if(!requested||!logOK)break;
            Log(L"REQUEST_ACCEPTED_CONNECTION_NOT_CONFIRMED",2000);
            Sleep(2000); // settling interval only; never described as a connection acknowledgement
            if(!logOK)break;
            WAVEFORMATEX format;ZeroMemory(&format,sizeof(format));
            format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=Rate;
            format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=Rate*4;
            result=waveOutOpen(&wave,1,&format,0,0,CALLBACK_NULL);Log(L"WAVE_OPEN_RESULT",result);
            if(result){wave=0;break;}
            DWORD volume=0;result=waveOutGetVolume(wave,&volume);Log(L"WAVE_VOLUME_QUERY",result,volume);
            Fill();BOOL ready=TRUE;
            for(DWORD p=0;p<Blocks;++p){
                result=waveOutPrepareHeader(wave,&headers[p],sizeof(WAVEHDR));
                Log(L"PREPARE_INDEX_RESULT",p,result);
                if(result){ready=FALSE;break;}prepared[p]=TRUE;
            }
            if(!ready||!logOK)break;
            // Queue every initial buffer without Pause/Restart or logging in between.
            DWORD playback=GetTickCount();
            for(DWORD q=0;q<Blocks;++q){
                result=waveOutWrite(wave,&headers[q],sizeof(WAVEHDR));
                if(result){Log(L"INITIAL_WRITE_ERROR",q,result);ready=FALSE;break;}
                queued[q]=TRUE;++submitted;
            }
            Log(L"INITIAL_BUFFERS_QUEUED",submitted);if(!ready||!logOK)break;
            DWORD next=0,lastReport=GetTickCount();BOOL failed=FALSE;
            while(GetTickCount()-playback<seconds*1000&&logOK&&!failed){
                // Requeue in original order so the two-tone pattern stays stable.
                for(DWORD checked=0;checked<Blocks;++checked){
                    if(!(headers[next].dwFlags&WHDR_DONE))break;
                    ++completed;queued[next]=FALSE;
                    result=waveOutWrite(wave,&headers[next],sizeof(WAVEHDR));
                    if(result){Log(L"REFILL_WRITE_ERROR",next,result);failed=TRUE;break;}
                    queued[next]=TRUE;++submitted;next=(next+1)%Blocks;
                }
                if(GetTickCount()-lastReport>=1000){Position(wave,submitted,completed);lastReport=GetTickCount();}
                Sleep(10);
            }
            Log(L"STOP_REFILL_DRAIN_BEGIN",submitted,completed);
            DWORD drain=GetTickCount(),pending=0;
            do {
                pending=0;
                for(DWORD d=0;d<Blocks;++d)if(queued[d]){
                    if(headers[d].dwFlags&WHDR_DONE){queued[d]=FALSE;++completed;}
                    else ++pending;
                }
                if(!pending||!logOK)break;Sleep(10);
            } while(GetTickCount()-drain<3000);
            Position(wave,submitted,completed);Log(L"DRAIN_PENDING",pending);
            if(!failed&&!pending&&completed&&logOK)code=0;
            else Log(L"PCM_NOT_COMPLETED",submitted,completed);
        } while(0);
    }
    // Always release our channel, including open/prepare/write errors.
    if(wave){
        MMRESULT result=waveOutReset(wave);Log(L"WAVE_RESET_RESULT",result);if(result)code=23;
        for(DWORD u=0;u<Blocks;++u)if(prepared[u]){
            result=waveOutUnprepareHeader(wave,&headers[u],sizeof(WAVEHDR));
            Log(L"UNPREPARE_INDEX_RESULT",u,result);if(result)code=23;
        }
        result=waveOutClose(wave);Log(L"WAVE_CLOSE_RESULT",result);if(result)code=23;
    }
    if(requested&&!Route(av,FALSE))code=23;
    if(av!=INVALID_HANDLE_VALUE&&!CloseHandle(av)){Log(L"AVV0_CLOSE_ERROR",GetLastError());code=23;}
    Log(L"END_PHYSICAL_SOUND_REQUIRES_USER_CONFIRMATION",code,completed);
    if(!logOK)code=22;
    if(!CloseHandle(file))code=22;file=INVALID_HANDLE_VALUE;
    return code;
}
