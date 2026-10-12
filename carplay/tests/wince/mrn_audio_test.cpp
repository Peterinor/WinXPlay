// Production MRN mixer + worker; only AVV0 and waveOut are isolated substitutes.
#include "../../platform/wince/media_backend.h"
#include <mmsystem.h>
#include <cstdio>
#include <set>
static volatile LONG writes,opens,requests,releases,nonzero;
static int failure;static std::set<WAVEHDR*> initial;
static void check(bool ok){if(!ok){fprintf(stderr,"MRN audio assertion failure mode=%d\n",failure);ExitProcess(1);}}
static HANDLE avOpen(LPCWSTR name,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE){check(!wcscmp(name,L"AVV0:"));return (HANDLE)123;}
static BOOL avClose(HANDLE h){return h==(HANDLE)123?TRUE:CloseHandle(h);}
static BOOL route(HANDLE h,DWORD code,void *input,DWORD size,void*,DWORD,DWORD*,OVERLAPPED*){
    check(h==(HANDLE)123 && size==1 && *(BYTE*)input==4 && (code==0x33||code==0x34));
    InterlockedIncrement(code==0x33?&requests:&releases);if(failure==1&&code==0x33){SetLastError(5);return FALSE;}return TRUE;
}
static MMRESULT openWave(LPHWAVEOUT out,UINT id,LPCWAVEFORMATEX f,DWORD_PTR,DWORD_PTR,DWORD flags){
    check(id==1&&f->nSamplesPerSec==48000&&f->nChannels==2&&f->wBitsPerSample==16&&flags==CALLBACK_NULL);
    InterlockedIncrement(&opens);if(failure==2)return MMSYSERR_ALLOCATED;*out=(HWAVEOUT)456;return 0;
}
static MMRESULT prepare(HWAVEOUT,WAVEHDR *h,UINT size){check(size==sizeof(WAVEHDR)&&h->dwBufferLength==19200);
    if(failure==3)return MMSYSERR_NOMEM;h->dwFlags=WHDR_PREPARED;return 0;}
static MMRESULT writeWave(HWAVEOUT,WAVEHDR *h,UINT){
    if(failure==4)return MMSYSERR_ERROR;
    LONG n=InterlockedIncrement(&writes);if(n<=10){initial.insert(h);if(n==10)check(initial.size()==10);}
    const short *s=(const short*)h->lpData;for(unsigned i=0;i<h->dwBufferLength/2;++i)if(s[i]){InterlockedIncrement(&nonzero);break;}
    h->dwFlags|=WHDR_DONE;return 0;
}
static MMRESULT resetWave(HWAVEOUT){return 0;}
static MMRESULT unprepare(HWAVEOUT,WAVEHDR *h,UINT){if(failure==5)return WAVERR_STILLPLAYING;h->dwFlags=0;return 0;}
static MMRESULT closeWave(HWAVEOUT){return failure==5?MMSYSERR_ERROR:0;}
static DWORD wait(HANDLE h,DWORD ms){return WaitForSingleObject(h,ms==2000?0:ms);}
namespace cp {
LONG diagnosticException(EXCEPTION_POINTERS*,const wchar_t*){return EXCEPTION_CONTINUE_SEARCH;}
std::wstring executableDirectory(){return L"test";}
void writeFile(const std::wstring&,const Bytes&){}
}
#define CreateFileW avOpen
#define CloseHandle avClose
#define DeviceIoControl route
#define waveOutOpen openWave
#define waveOutPrepareHeader prepare
#define waveOutWrite writeWave
#define waveOutReset resetWave
#define waveOutUnprepareHeader unprepare
#define waveOutClose closeWave
#define WaitForSingleObject wait
#include "../../devices/mrn/audio_output.cpp"
#undef WaitForSingleObject
static cp::Bytes pcm(unsigned frames,unsigned channels){cp::Bytes b(frames*channels*2);
    for(size_t i=0;i<b.size();i+=2){b[i]=0x34;b[i+1]=0x12;}return b;}
static void resetTest(int mode){failure=mode;writes=opens=requests=releases=nonzero=0;initial.clear();}
static void conversion(){
    const unsigned rates[]={8000,16000,24000,32000,44100,48000};
    for(unsigned r=0;r<6;++r){cp::MrnPcmQueue whole(rates[r],1),split(rates[r],1);cp::Bytes b=pcm(1001,1);
        whole.push(b);for(size_t i=0;i<b.size();i+=2)split.push(cp::Bytes(b.begin()+i,b.begin()+i+2));
        check(whole.size()==(1000*48000+rates[r]-1)/rates[r]);short l,r1,x,y;
        while(whole.pop(l,r1)){check(split.pop(x,y)&&l==0x1234&&r1==l&&x==l&&y==l);}check(split.size()==0);
    }
    cp::MrnPcmQueue q(48000,2);for(unsigned i=0;i<10;++i)q.push(pcm(4000,2));
    check(q.size()==24000&&q.dropped>0);q.clear();check(q.size()==0);
    cp::MrnPcmQueue stereo(48000,2);cp::Bytes sample(8,0);
    sample[0]=10;sample[2]=20;sample[4]=30;sample[6]=40;stereo.push(sample);
    short l,r;check(stereo.pop(l,r)&&l==10&&r==20);
    cp::MrnPcmQueue linear(16000,1);cp::Bytes ramp(4,0);ramp[2]=0x2c;ramp[3]=1;
    linear.push(ramp);for(int expected=0;expected<300;expected+=100)check(linear.pop(l,r)&&l==expected&&r==expected);
}
int main(){
    conversion();resetTest(0);
    cp::AudioOutputBackend *a=cp::createMrnTtsOutput(48000,2),*b=cp::createMrnTtsOutput(16000,1);
    a->feed(pcm(4000,2));b->feed(pcm(2000,1));
    for(unsigned n=0;n<100&&!nonzero;++n)Sleep(10);
    check(nonzero>0&&opens==1&&requests==1&&writes>=10);
    Sleep(30);check(a->playedBytes()>0&&b->playedBytes()>0);
    a->pause(true);a->feed(pcm(1000,2));a->pause(false);a->feed(pcm(1000,2));
    delete a;check(releases==0);delete b;check(releases==1);
    for(int mode=1;mode<=4;++mode){resetTest(mode);a=cp::createMrnTtsOutput(48000,2);bool rejected=false;
        for(unsigned n=0;n<100&&!rejected;++n){Sleep(5);try{a->reap();}catch(...){rejected=true;}}
        check(rejected);delete a;check(releases==(mode==1?0:1));
    }
    resetTest(5);a=cp::createMrnTtsOutput(48000,2);Sleep(40);delete a;
    bool rejected=false;try{a=cp::createMrnTtsOutput(48000,2);}catch(...){rejected=true;}
    check(rejected&&opens==1&&releases==1);
    puts("MRN audio: six-rate packet continuity, mono/stereo, bounded queue, shared device, ten buffers, playback accounting, pause, failure cleanup and retained-buffer protection passed.");
}
