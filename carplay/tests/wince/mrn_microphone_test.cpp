// Production input adapter with deterministic AV/wave API substitutes.
#include "../../platform/wince/media_backend.h"
#include <mmsystem.h>
#include <cstdio>
#include <vector>
static DWORD now=100;static int failure=0,opens=0;static std::vector<unsigned> calls;
static std::vector<WAVEHDR*> buffers;
static void ck(bool ok){if(!ok){fprintf(stderr,"microphone check failed, mode %d\n",failure);ExitProcess(1);}}
static HANDLE avOpen(LPCWSTR n,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE){ck(!wcscmp(n,L"AVV0:"));return failure==1?INVALID_HANDLE_VALUE:(HANDLE)123;}
static BOOL avClose(HANDLE h){ck(h==(HANDLE)123);return TRUE;}
static DWORD ticks(){return now;}
static BOOL mockRoute(HANDLE,DWORD code,void* p,DWORD n,void*,DWORD,DWORD*,OVERLAPPED*){
    BYTE* b=(BYTE*)p;ck(b[0]==16 && n==(code==4?2:1));unsigned value=code*256+(n==2?b[1]:0);calls.push_back(value);
    return !((failure==2&&value==1025)||(failure==3&&code==1)||(failure==8&&value==1024));
}
static MMRESULT openWave(LPHWAVEIN out,UINT id,LPCWAVEFORMATEX f,DWORD_PTR,DWORD_PTR,DWORD flags){
    ++opens;ck(id==0&&f->nSamplesPerSec==22050&&f->nChannels==1&&f->wBitsPerSample==16&&flags==CALLBACK_NULL);
    if(failure==4)return MMSYSERR_ALLOCATED;*out=(HWAVEIN)456;return 0;
}
static MMRESULT prepare(HWAVEIN,WAVEHDR* h,UINT){if(failure==5)return MMSYSERR_NOMEM;h->dwFlags=WHDR_PREPARED;return 0;}
static MMRESULT add(HWAVEIN,WAVEHDR* h,UINT){
    if(failure==6)return MMSYSERR_ERROR;
    h->dwFlags=WHDR_PREPARED;bool found=false;for(size_t i=0;i<buffers.size();++i)if(buffers[i]==h)found=true;
    if(!found)buffers.push_back(h);return 0;
}
static MMRESULT mockStart(HWAVEIN){return failure==7?MMSYSERR_ERROR:0;}
static MMRESULT reset(HWAVEIN){return 0;}
static MMRESULT unprepare(HWAVEIN,WAVEHDR* h,UINT){if(failure==9)return WAVERR_STILLPLAYING;h->dwFlags=0;return 0;}
static MMRESULT closeWave(HWAVEIN){return failure==9?MMSYSERR_ERROR:0;}
#define CreateFileW avOpen
#define CloseHandle avClose
#define DeviceIoControl mockRoute
#define GetTickCount ticks
#define waveInOpen openWave
#define waveInPrepareHeader prepare
#define waveInAddBuffer add
#define waveInStart mockStart
#define waveInReset reset
#define waveInUnprepareHeader unprepare
#define waveInClose closeWave
#include "../../devices/mrn/audio_input.cpp"
static cp::Bytes ramp(unsigned n){cp::Bytes b(n*2);for(unsigned i=0;i<n;++i){short x=short(i*17);b[i*2]=BYTE(x);b[i*2+1]=BYTE(unsigned(x)>>8);}return b;}
static void conversion(){
    unsigned rates[]={8000,16000,22050,24000,32000,44100,48000};
    for(unsigned i=0;i<7;++i){unsigned r=rates[i],frames=r/50;cp::PcmInputPackets a(r,frames),b(r,frames);
        cp::Bytes input=ramp(22051);std::vector<cp::Bytes> whole,split;a.append(input,whole);
        for(size_t j=0;j<input.size();j+=2)b.append(cp::Bytes(input.begin()+j,input.begin()+j+2),split);
        ck(whole==split&&whole.size()==r/frames);
        for(size_t j=0;j<whole.size();++j)ck(whole[j].size()==frames*2);
    }
}
int main(){
    conversion();
    for(failure=0;failure<=9;++failure){
        calls.clear();buffers.clear();opens=0;now=100;cp::AudioInputBackend* input=0;bool threw=false;
        try{
            input=cp::createMrnAudioInput(32000,1,640);ck(input->poll().empty()&&opens==0);
            now=2099;ck(input->poll().empty()&&opens==0);now=2100;input->poll();
            ck(opens==1&&buffers.size()==8);
            bool duplicate=false;try{delete cp::createMrnAudioInput(16000,1,320);}catch(...){duplicate=true;}ck(duplicate);
            for(unsigned i=0;i<8;++i){WAVEHDR* h=buffers[i];ck(h->dwBufferLength==4410);
                for(unsigned j=0;j<4410;j+=2){h->lpData[j]=0x34;h->lpData[j+1]=0x12;}h->dwBytesRecorded=4410;h->dwFlags|=WHDR_DONE;}
            std::vector<cp::Bytes> packets=input->poll();ck(packets.size()==39);
            for(size_t i=0;i<packets.size();++i)for(size_t j=0;j<packets[i].size();j+=2)ck(packets[i][j]==0x34&&packets[i][j+1]==0x12);
        }catch(...){threw=true;}
        delete input;
        ck(threw==(failure>=1&&failure<=7));
        if(failure==1)ck(calls.empty());
        else{ck(calls[calls.size()-2]==1024&&calls.back()==512);}
    }
    bool blocked=false;try{delete cp::createMrnAudioInput(16000,1,320);}catch(...){blocked=true;}ck(blocked);
    puts("MRN microphone: 7-rate split-buffer continuity, exact packet framing, asynchronous settle, capture, exclusive ownership, failure cleanup and retained-buffer guard passed. Hardware not exercised.");
}
