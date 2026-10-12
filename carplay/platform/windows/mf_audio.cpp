#include "mf_audio.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <wmcodecdsp.h>
namespace {
template<class T> struct Ptr {
    T *p; Ptr():p(0){} ~Ptr(){if(p)p->Release();}
    T** out(){if(p)p->Release();p=0;return &p;} T* operator->(){return p;}
private: Ptr(const Ptr&);Ptr&operator=(const Ptr&);
};
#define CHECK(call) do{HRESULT check_=(call);if(FAILED(check_))return check_;}while(0)
struct State {
    HANDLE request,done,thread;CRITICAL_SECTION lock;unsigned rate,channels,command;
    const BYTE *input;unsigned inputSize;BYTE *output;unsigned capacity,written;HRESULT result;bool drained;
    State(unsigned r,unsigned c):request(0),done(0),thread(0),rate(r),channels(c),command(0),input(0),inputSize(0),
        output(0),capacity(0),written(0),result(S_OK),drained(false){InitializeCriticalSection(&lock);}
    ~State(){if(thread)CloseHandle(thread);if(request)CloseHandle(request);if(done)CloseHandle(done);DeleteCriticalSection(&lock);}
};
struct Decoder {
    State &s;Ptr<IMFTransform> decoder;LONGLONG index;
    Decoder(State &state):s(state),index(0){}
    HRESULT outputType() {
        Ptr<IMFMediaType> out;CHECK(MFCreateMediaType(out.out()));
        CHECK(out->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio));CHECK(out->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM));
        CHECK(out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,s.rate));CHECK(out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,s.channels));
        CHECK(out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16));CHECK(out->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,s.channels*2));
        CHECK(out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,s.rate*s.channels*2));
        return decoder->SetOutputType(0,out.p,0);
    }
    HRESULT init() {
        CHECK(CoCreateInstance(CLSID_CMSAACDecMFT,0,CLSCTX_INPROC_SERVER,IID_IMFTransform,(void**)decoder.out()));
        const unsigned rates[]={96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000};
        unsigned frequency=0;while(frequency<12 && rates[frequency]!=s.rate)++frequency;
        if(frequency==12)return E_INVALIDARG;
        unsigned asc=(2<<11)|(frequency<<7)|(s.channels<<3);BYTE config[]={BYTE(asc>>8),BYTE(asc)};
        Ptr<IMFMediaType> in;CHECK(MFCreateMediaType(in.out()));
        CHECK(in->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio));CHECK(in->SetGUID(MF_MT_SUBTYPE,MEDIASUBTYPE_RAW_AAC1));
        CHECK(in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,s.rate));CHECK(in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,s.channels));
        CHECK(in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16));CHECK(in->SetBlob(MF_MT_USER_DATA,config,2));
        CHECK(decoder->SetInputType(0,in.p,0));CHECK(outputType());
        CHECK(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,0));
        return decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,0);
    }
    HRESULT output() {
        for(unsigned i=0;i<16;++i) {
            MFT_OUTPUT_STREAM_INFO info={};CHECK(decoder->GetOutputStreamInfo(0,&info));
            Ptr<IMFSample> supplied;Ptr<IMFMediaBuffer> storage;
            MFT_OUTPUT_DATA_BUFFER out={};DWORD status=0;
            if(!(info.dwFlags&MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
                if(info.cbSize>65536)return E_OUTOFMEMORY;
                CHECK(MFCreateSample(supplied.out()));CHECK(MFCreateMemoryBuffer(info.cbSize?info.cbSize:65536,storage.out()));
                CHECK(supplied->AddBuffer(storage.p));out.pSample=supplied.p;
            }
            HRESULT hr=decoder->ProcessOutput(0,1,&out,&status);
            if(out.pEvents)out.pEvents->Release();
            Ptr<IMFSample> provided;if(out.pSample && out.pSample!=supplied.p)provided.p=out.pSample;
            if(hr==MF_E_TRANSFORM_NEED_MORE_INPUT)return S_OK;
            if(hr==MF_E_TRANSFORM_STREAM_CHANGE){CHECK(outputType());continue;}
            if(FAILED(hr))return hr;
            if(out.pSample) {
                Ptr<IMFMediaBuffer> buffer;CHECK(out.pSample->ConvertToContiguousBuffer(buffer.out()));
                BYTE *data=0;DWORD bytes=0;CHECK(buffer->Lock(&data,0,&bytes));
                bool valid=bytes<=s.capacity-s.written && bytes%(s.channels*2)==0;
                if(valid){CopyMemory(s.output+s.written,data,bytes);s.written+=bytes;}
                buffer->Unlock();if(!valid)return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
            }
        }
        return E_UNEXPECTED;
    }
    HRESULT decode() {
        Ptr<IMFSample> sample;Ptr<IMFMediaBuffer> buffer;CHECK(MFCreateSample(sample.out()));
        CHECK(MFCreateMemoryBuffer(s.inputSize,buffer.out()));BYTE *data=0;CHECK(buffer->Lock(&data,0,0));
        CopyMemory(data,s.input,s.inputSize);buffer->Unlock();CHECK(buffer->SetCurrentLength(s.inputSize));CHECK(sample->AddBuffer(buffer.p));
        CHECK(sample->SetSampleTime(index++*1024*10000000/s.rate));CHECK(sample->SetSampleDuration(1024*10000000LL/s.rate));
        HRESULT hr=decoder->ProcessInput(0,sample.p,0);
        if(hr==MF_E_NOTACCEPTING){CHECK(output());hr=decoder->ProcessInput(0,sample.p,0);}
        if(FAILED(hr))return hr;return output();
    }
    HRESULT drain() {
        CHECK(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM,0));
        CHECK(decoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN,0));return output();
    }
};
DWORD WINAPI worker(void *p) {
    State &s=*(State*)p;HRESULT com=CoInitializeEx(0,COINIT_MULTITHREADED);
    HRESULT mf=FAILED(com)?com:MFStartup(MF_VERSION,MFSTARTUP_FULL);
    if(SUCCEEDED(mf)) {
        Decoder decoder(s);s.result=decoder.init();SetEvent(s.done);
        if(SUCCEEDED(s.result))for(;;) {
            WaitForSingleObject(s.request,INFINITE);if(s.command==3)break;
            s.written=0;s.result=s.command==2?decoder.drain():decoder.decode();SetEvent(s.done);
        }
    }else{s.result=mf;SetEvent(s.done);}
    if(SUCCEEDED(mf))MFShutdown();if(SUCCEEDED(com))CoUninitialize();return 0;
}
HRESULT invoke(State *s,unsigned command,const BYTE *input,unsigned size,BYTE *pcm,unsigned capacity,unsigned *written) {
    if(!s || !pcm || !capacity || capacity>65536 || !written)return E_INVALIDARG;
    *written=0;EnterCriticalSection(&s->lock);
    if(s->drained){LeaveCriticalSection(&s->lock);return MF_E_SHUTDOWN;}
    s->command=command;s->input=input;s->inputSize=size;s->output=pcm;s->capacity=capacity;
    if(command==2)s->drained=true;
    SetEvent(s->request);WaitForSingleObject(s->done,INFINITE);HRESULT hr=s->result;*written=s->written;
    LeaveCriticalSection(&s->lock);return hr;
}
}
extern "C" HRESULT cpAacCreate(unsigned rate,unsigned channels,void **handle) {
    if(!handle || rate<8000 || rate>48000 || channels<1 || channels>2)return E_INVALIDARG;*handle=0;
    State *s=new State(rate,channels);s->request=CreateEventW(0,FALSE,FALSE,0);s->done=CreateEventW(0,FALSE,FALSE,0);
    if(!s->request || !s->done){delete s;return E_OUTOFMEMORY;}
    s->thread=CreateThread(0,0,worker,s,0,0);if(!s->thread){delete s;return E_OUTOFMEMORY;}
    WaitForSingleObject(s->done,INFINITE);
    if(FAILED(s->result)){HRESULT hr=s->result;WaitForSingleObject(s->thread,INFINITE);delete s;return hr;}
    *handle=s;return S_OK;
}
extern "C" HRESULT cpAacDecode(void *handle,const BYTE *data,unsigned size,BYTE *pcm,unsigned capacity,unsigned *written) {
    if(!data || !size || size>8192)return E_INVALIDARG;
    return invoke((State*)handle,1,data,size,pcm,capacity,written);
}
extern "C" HRESULT cpAacDrain(void *handle,BYTE *pcm,unsigned capacity,unsigned *written) {return invoke((State*)handle,2,0,0,pcm,capacity,written);}
extern "C" void cpAacDestroy(void *handle) {
    State *s=(State*)handle;if(!s)return;
    EnterCriticalSection(&s->lock);s->command=3;SetEvent(s->request);WaitForSingleObject(s->thread,INFINITE);
    LeaveCriticalSection(&s->lock);delete s;
}
