// OS/decoder doubles exercise the adapter; they do not claim OEM audible output.
#include "../../platform/wince/audio_aac.h"
#include "../../platform/wince/aac_dmo.h"
#include "../../devices/profile.h"
#include <cstdio>
using namespace cp;
static volatile LONG objects=0,decoded=0,played=0,failDecode=0,failCreate=0;
static HANDLE gate=0,creationGate=0;
static void verify(bool b){require(b,"AAC adapter assertion failed");}
class Fake:public IMediaObject {
    LONG refs_;IMediaBuffer *pending_;bool input_,output_;
  public:
    Fake():refs_(1),pending_(0),input_(false),output_(false){InterlockedIncrement(&objects);}
    ~Fake(){Flush();InterlockedDecrement(&objects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **out){if(!out)return E_POINTER;*out=0;
        if(id!=IID_IUnknown&&id!=IID_IMediaObject)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef(){return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release(){LONG n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetStreamCount(DWORD *i,DWORD *o){*i=*o=1;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetInputStreamInfo(DWORD,DWORD *v){*v=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetOutputStreamInfo(DWORD,DWORD *v){*v=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetInputType(DWORD,DWORD,DMO_MEDIA_TYPE*){return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetOutputType(DWORD,DWORD,DMO_MEDIA_TYPE*){return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetInputType(DWORD,const DMO_MEDIA_TYPE *t,DWORD){
        if(!t||t->subtype!=aac::Adts||t->cbFormat!=sizeof(WAVEFORMATEX))return E_INVALIDARG;
        WAVEFORMATEX *w=(WAVEFORMATEX*)t->pbFormat;
        input_=w->wFormatTag==0x1600 && w->nChannels==2 && (w->nSamplesPerSec==48000||w->nSamplesPerSec==44100);
        return input_?S_OK:E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE SetOutputType(DWORD,const DMO_MEDIA_TYPE *t,DWORD){
        WAVEFORMATEX *w=(WAVEFORMATEX*)t->pbFormat;
        output_=input_&&t->subtype==aac::Pcm&&w->wBitsPerSample==16&&w->nBlockAlign==4;return output_?S_OK:E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE GetInputCurrentType(DWORD,DMO_MEDIA_TYPE*){return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetOutputCurrentType(DWORD,DMO_MEDIA_TYPE*){return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetInputSizeInfo(DWORD,DWORD*,DWORD*,DWORD*){return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetOutputSizeInfo(DWORD,DWORD *size,DWORD *align){*size=4096;*align=4;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetInputMaxLatency(DWORD,REFERENCE_TIME*){return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetInputMaxLatency(DWORD,REFERENCE_TIME){return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Flush(){if(pending_){pending_->Release();pending_=0;}return S_OK;}
    HRESULT STDMETHODCALLTYPE Discontinuity(DWORD){return Flush();}
    HRESULT STDMETHODCALLTYPE AllocateStreamingResources(){return output_?S_OK:E_FAIL;}
    HRESULT STDMETHODCALLTYPE FreeStreamingResources(){return Flush();}
    HRESULT STDMETHODCALLTYPE GetInputStatus(DWORD,DWORD *v){*v=pending_?0:DMO_INPUT_STATUSF_ACCEPT_DATA;return S_OK;}
    HRESULT STDMETHODCALLTYPE ProcessInput(DWORD,IMediaBuffer *b,DWORD,REFERENCE_TIME,REFERENCE_TIME){
        if(pending_)return DMO_E_NOTACCEPTING;BYTE *p;DWORD n;b->GetBufferAndLength(&p,&n);
        if(n<8||p[0]!=0xff||p[1]!=0xf1||(((p[3]&3)<<11)|(p[4]<<3)|(p[5]>>5))!=n)return E_INVALIDARG;
        pending_=b;b->AddRef();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ProcessOutput(DWORD,DWORD,DMO_OUTPUT_DATA_BUFFER *out,DWORD*){
        if(gate)WaitForSingleObject(gate,3000);if(failDecode)return E_FAIL;if(!pending_)return S_FALSE;
        BYTE *p;out->pBuffer->GetBufferAndLength(&p,0);memset(p,0,4096);out->pBuffer->SetLength(4096);Flush();
        InterlockedIncrement(&decoded);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Lock(LONG){return S_OK;}
};
HRESULT WINAPI fakeCreate(REFCLSID,LPUNKNOWN,DWORD,REFIID,LPVOID *out){
    if(creationGate)WaitForSingleObject(creationGate,5000);if(failCreate)return E_FAIL;*out=new Fake;return S_OK;}
#define CoCreateInstance fakeCreate
#include "../../platform/wince/audio_aac.cpp"
#undef CoCreateInstance
namespace cp {
const DeviceProfile &deviceProfile(){static DeviceProfile p;
    p.audio.aacDecoder=L"{37CA27C5-5E4E-4F0C-8DCF-FEC2B5EB789F}";return p;}
std::wstring environment(const wchar_t*,const wchar_t *fallback){return fallback;}
class Sink:public AudioOutputBackend {
  public:
    void feed(const Bytes &b){InterlockedExchangeAdd(&played,LONG(b.size()));}
    void reap(){}unsigned playedBytes()const{return unsigned(played);}void pause(bool){}
};
AudioOutputBackend *createAudioOutputBackend(const std::wstring&,unsigned,unsigned){return new Sink;}
}
static void waitFor(LONG n){for(unsigned i=0;i<300&&played<n;++i)Sleep(10);verify(played>=n);}
int main(){try{
    DeviceAudioProfile defaults;verify(defaults.input&&defaults.output);
    Bytes raw=unhex("211004608c1c");
    verify(hex(aac::adts(raw,48000,2))=="fff14c8001bffc211004608c1c");
    verify(aac::adts(raw,44100,2)[2]==0x50);
    for(unsigned n=0;n<2;++n){bool rejected=false;try{aac::adts(Bytes(n?8185:0),48000,2);}catch(...){rejected=true;}verify(rejected);}
    {aac::Decoder decoder(new Fake,48000,2);for(unsigned i=0;i<50;++i)verify(decoder.decode(raw).size()==4096);decoder.flush();}
    verify(objects==0);verify(winceAacAvailable()&&objects==0);
    AudioOutputBackend *out=createWinceAacOutput(48000,2);
    out->feed(raw);waitFor(4096);verify(out->metrics().at("aac_pcm_bytes").integer==4096);
    gate=CreateEventW(0,TRUE,FALSE,0);
    for(unsigned i=0;i<100;++i)out->feed(raw);
    Value m=out->metrics();verify(m.at("aac_q_packets").integer<=24&&m.at("aac_dropped_packets").integer>0);
    out->pause(true);LONG before=played;SetEvent(gate);Sleep(50);verify(played==before);
    out->pause(false);out->feed(raw);waitFor(before+4096);delete out;CloseHandle(gate);gate=0;verify(objects==0);
    out=createWinceAacOutput(44100,2);failDecode=1;out->feed(raw);bool failed=false;
    for(unsigned i=0;i<300&&!failed;++i){Sleep(10);try{out->reap();}catch(...){failed=true;}}
    verify(failed);delete out;failDecode=0;verify(objects==0);
    failCreate=1;failed=false;try{out=createWinceAacOutput(48000,2);}catch(...){failed=true;}verify(failed&&objects==0);
    InterlockedExchange(&cp::probeState,0);verify(!winceAacAvailable()&&objects==0);
    failCreate=0;verify(!winceAacAvailable()); // Failed startup probe is cached, no repeated load attempts.
    creationGate=CreateEventW(0,TRUE,FALSE,0);InterlockedExchange(&cp::probeState,0);
    verify(!winceAacAvailable());SetEvent(creationGate);Sleep(100);verify(!winceAacAvailable()&&objects==0);
    CloseHandle(creationGate);creationGate=0;
    puts("AAC adapter: ADTS headers, bounded input, DMO ownership, worker decode, pause generation, failure cleanup and probe passed.");return 0;
}catch(const std::exception &e){fprintf(stderr,"%s\n",e.what());return 1;}}
