#pragma once
#include "media_backend.h"
#include <dmo.h>
#include <mmreg.h>
#include <sstream>
#include <memory>
namespace cp { namespace aac {
// AACDMO accepts MPEG ADTS AAC (0x1600), not desktop RAW_AAC1 (0xff).
// Its format check was verified against the original F3.21 AACDMO binary.
static const GUID Audio={0x73647561,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
static const GUID Adts={0x1600,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
static const GUID Pcm={1,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
static const GUID WaveFormat={0x05589f81,0xc356,0x11ce,{0xbf,1,0,0xaa,0,0x55,0x59,0x5a}};
inline void check(HRESULT hr,const char *stage){if(FAILED(hr)){
    std::ostringstream s;s<<stage<<" hr=0x"<<std::hex<<unsigned(hr);throw std::runtime_error(s.str());}}
class Buffer:public IMediaBuffer {
    LONG refs_;DWORD size_;Bytes bytes_;
  public:
    explicit Buffer(unsigned capacity):refs_(1),size_(0),bytes_(capacity){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **out){
        if(!out)return E_POINTER;*out=0;
        if(id!=IID_IUnknown && id!=IID_IMediaBuffer)return E_NOINTERFACE;
        *out=static_cast<IMediaBuffer*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef(){return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release(){LONG n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE SetLength(DWORD n){if(n>bytes_.size())return E_INVALIDARG;size_=n;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetMaxLength(DWORD *n){if(!n)return E_POINTER;*n=DWORD(bytes_.size());return S_OK;}
    HRESULT STDMETHODCALLTYPE GetBufferAndLength(BYTE **p,DWORD *n){if(p)*p=&bytes_[0];if(n)*n=size_;return S_OK;}
};
struct OwnedBuffer {
    Buffer *p;explicit OwnedBuffer(unsigned n):p(new Buffer(n)){}~OwnedBuffer(){p->Release();}
  private:OwnedBuffer(const OwnedBuffer&);OwnedBuffer &operator=(const OwnedBuffer&);
};
inline Bytes adts(const Bytes &raw,unsigned rate,unsigned channels){
    require(!raw.empty()&&raw.size()<=8184 && channels==2 && (rate==48000||rate==44100),"Invalid AAC-LC access unit");
    unsigned size=unsigned(raw.size())+7,index=rate==48000?3:4;
    Bytes b(7);b[0]=0xff;b[1]=0xf1;b[2]=(1<<6)|(index<<2)|(channels>>2);
    b[3]=((channels&3)<<6)|(size>>11);b[4]=(size>>3)&255;b[5]=((size&7)<<5)|31;b[6]=0xfc;
    b.insert(b.end(),raw.begin(),raw.end());return b;
}
class Decoder {
    IMediaObject *dmo_;unsigned rate_,channels_;DWORD outputSize_;
    Decoder(const Decoder&);Decoder &operator=(const Decoder&);
    static DMO_MEDIA_TYPE type(const GUID &subtype,WAVEFORMATEX &wave){
        DMO_MEDIA_TYPE t={0};t.majortype=Audio;t.subtype=subtype;t.formattype=WaveFormat;
        t.bFixedSizeSamples=subtype==Pcm;t.lSampleSize=wave.nBlockAlign;
        t.cbFormat=sizeof(wave);t.pbFormat=reinterpret_cast<BYTE*>(&wave);return t;
    }
    void close(){if(dmo_){dmo_->Flush();dmo_->FreeStreamingResources();dmo_->Release();dmo_=0;}}
  public:
    // Takes ownership of one COM reference, including on failure.
    Decoder(IMediaObject *object,unsigned rate,unsigned channels):dmo_(object),rate_(rate),channels_(channels),outputSize_(0){
        try{
            require(dmo_ && channels==2 && (rate==48000||rate==44100),"Invalid AAC decoder configuration");
            DWORD inputs=0,outputs=0;check(dmo_->GetStreamCount(&inputs,&outputs),"AAC stream count");
            require(inputs==1&&outputs==1,"Unsupported AAC DMO stream layout");
            WAVEFORMATEX w={0};w.wFormatTag=0x1600;w.nChannels=WORD(channels);w.nSamplesPerSec=rate;w.nBlockAlign=1;
            DMO_MEDIA_TYPE t=type(Adts,w);check(dmo_->SetInputType(0,&t,0),"AAC input type");
            w.wFormatTag=WAVE_FORMAT_PCM;w.wBitsPerSample=16;w.nBlockAlign=WORD(channels*2);w.nAvgBytesPerSec=rate*w.nBlockAlign;
            t=type(Pcm,w);check(dmo_->SetOutputType(0,&t,0),"AAC PCM output type");
            DWORD alignment=0;check(dmo_->GetOutputSizeInfo(0,&outputSize_,&alignment),"AAC output size");
            require(outputSize_<=65536&&alignment<=8,"Unsupported AAC output buffer");
            if(outputSize_<8192)outputSize_=8192;
            check(dmo_->AllocateStreamingResources(),"AAC allocate");
        }catch(...){close();throw;}
    }
    ~Decoder(){close();}
    void flush(){check(dmo_->Flush(),"AAC flush");}
    void drain(Bytes &pcm){
        for(unsigned i=0;i<32;++i){
            OwnedBuffer buffer(outputSize_);DMO_OUTPUT_DATA_BUFFER out={0};out.pBuffer=buffer.p;DWORD status=0;
            HRESULT hr=dmo_->ProcessOutput(0,1,&out,&status);check(hr,"AAC decode");
            BYTE *data=0;DWORD size=0;buffer.p->GetBufferAndLength(&data,&size);
            require(size%4==0 && pcm.size()+size<=65536,"Invalid AAC PCM output");
            pcm.insert(pcm.end(),data,data+size);
            if(hr==S_FALSE || !(out.dwStatus&DMO_OUTPUT_DATA_BUFFERF_INCOMPLETE))return;
            require(size!=0,"AAC decoder made no progress");
        }
        throw std::runtime_error("AAC decoder exceeded output bound");
    }
    Bytes decode(const Bytes &raw){
        Bytes packet=adts(raw,rate_,channels_),pcm;OwnedBuffer in(unsigned(packet.size()));BYTE *data=0;
        in.p->GetBufferAndLength(&data,0);memcpy(data,&packet[0],packet.size());in.p->SetLength(DWORD(packet.size()));
        HRESULT hr=dmo_->ProcessInput(0,in.p,DMO_INPUT_DATA_BUFFERF_SYNCPOINT,0,0);
        if(hr==DMO_E_NOTACCEPTING){drain(pcm);hr=dmo_->ProcessInput(0,in.p,DMO_INPUT_DATA_BUFFERF_SYNCPOINT,0,0);}
        check(hr,"AAC input");drain(pcm);return pcm;
    }
};
}}
