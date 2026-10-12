// Windows-only Media Foundation H.264 -> D3D11 video processor -> swap chain.
// No STL or C++ exceptions cross the legacy compiler boundary.
#include "mf_video.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <codecapi.h>
#include <wmcodecdsp.h>
#include <d3d11.h>
#include <d3d10.h>
#include <dxgi1_2.h>

namespace {
template<class T> struct Ptr {
    T *p;
    Ptr() : p(0) {}
    ~Ptr() { reset(); }
    void reset(T *q = 0) { if (p) p->Release(); p = q; }
    T **out() { reset(); return &p; }
    T *operator->() { return p; }
private: Ptr(const Ptr &); Ptr &operator=(const Ptr &);
};
struct Packet { Packet *next; unsigned size; LONGLONG queued; unsigned char data[1]; };
struct State {
    HWND window; HANDLE thread, event, initialized; CRITICAL_SECTION lock;
    Packet *head, *tail; bool ending; CpMfFrame callback; void *context;
    CpMfStats stats; unsigned width, height;
    State(HWND w, unsigned x, unsigned y, CpMfFrame f, void *c)
        : window(w), thread(0), event(0), initialized(0), head(0), tail(0), ending(false),
          callback(f), context(c), width(x), height(y) { ZeroMemory(&stats, sizeof(stats)); InitializeCriticalSection(&lock); }
    ~State() {
        while(head) { Packet *p=head; head=p->next; HeapFree(GetProcessHeap(),0,p); }
        if(thread) CloseHandle(thread); if(event) CloseHandle(event); if(initialized) CloseHandle(initialized);
        DeleteCriticalSection(&lock);
    }
    void stage(const wchar_t *text, HRESULT hr = S_OK) {
        EnterCriticalSection(&lock); lstrcpynW(stats.stage,text,80); if(FAILED(hr)) stats.error=hr; LeaveCriticalSection(&lock);
    }
};
LONGLONG nowUs() { LARGE_INTEGER n,f; QueryPerformanceCounter(&n); QueryPerformanceFrequency(&f); return n.QuadPart*1000000/f.QuadPart; }
#define TRY(call) do { HRESULT hr_=(call); if(FAILED(hr_)) return hr_; } while(0)

struct Decoder {
    State &s; Ptr<ID3D11Device> device; Ptr<ID3D11DeviceContext> context;
    Ptr<ID3D11VideoDevice> videoDevice; Ptr<ID3D11VideoContext> videoContext;
    Ptr<IMFDXGIDeviceManager> manager; Ptr<IMFTransform> decoder;
    Ptr<IDXGISwapChain> swap; Ptr<ID3D11VideoProcessorEnumerator> enumerator;
    Ptr<ID3D11VideoProcessor> processor; Ptr<IMFSample> last;
    unsigned imageW,imageH,windowW,windowH,matrix,range; LONG sourceX,sourceY; LONGLONG sequence;
    Decoder(State &state) : s(state),imageW(state.width),imageH(state.height),windowW(0),windowH(0),
        matrix(MFVideoTransferMatrix_BT709),range(MFNominalRange_16_235),sourceX(0),sourceY(0),sequence(0) {}
    HRESULT init() {
        s.stage(L"create_d3d11_device"); D3D_FEATURE_LEVEL level;
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        TRY(D3D11CreateDevice(0,D3D_DRIVER_TYPE_HARDWARE,0,D3D11_CREATE_DEVICE_VIDEO_SUPPORT|D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                             levels,2,D3D11_SDK_VERSION,device.out(),&level,context.out()));
        Ptr<ID3D10Multithread> multi; TRY(context->QueryInterface(__uuidof(ID3D10Multithread),(void**)multi.out()));
        multi->SetMultithreadProtected(TRUE);
        TRY(device->QueryInterface(__uuidof(ID3D11VideoDevice),(void**)videoDevice.out()));
        TRY(context->QueryInterface(__uuidof(ID3D11VideoContext),(void**)videoContext.out()));
        Ptr<IDXGIDevice1> dxgi; Ptr<IDXGIAdapter> adapter; Ptr<IDXGIFactory> factory;
        TRY(device->QueryInterface(__uuidof(IDXGIDevice1),(void**)dxgi.out()));
        dxgi->SetMaximumFrameLatency(1); TRY(dxgi->GetAdapter(adapter.out()));
        DXGI_ADAPTER_DESC description; TRY(adapter->GetDesc(&description));
        EnterCriticalSection(&s.lock); lstrcpynW(s.stats.adapter,description.Description,128); LeaveCriticalSection(&s.lock);
        TRY(adapter->GetParent(__uuidof(IDXGIFactory),(void**)factory.out()));
        DXGI_SWAP_CHAIN_DESC desc={}; desc.BufferDesc.Width=1; desc.BufferDesc.Height=1;
        desc.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count=1;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2; desc.OutputWindow=s.window;
        // Flip presentation avoids corrupted DWM output seen with the legacy
        // blit-model swap chain on the tested Intel driver (surfaces were valid).
        desc.Windowed=TRUE; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        TRY(factory->CreateSwapChain(device.p,&desc,swap.out()));
        factory->MakeWindowAssociation(s.window,DXGI_MWA_NO_ALT_ENTER);
        UINT token=0; TRY(MFCreateDXGIDeviceManager(&token,manager.out())); TRY(manager->ResetDevice(device.p,token));
        s.stage(L"create_h264_decoder");
        TRY(CoCreateInstance(CLSID_CMSH264DecoderMFT,0,CLSCTX_INPROC_SERVER,IID_IMFTransform,(void**)decoder.out()));
        Ptr<IMFAttributes> attrs; TRY(decoder->GetAttributes(attrs.out()));
        UINT32 aware=0; TRY(attrs->GetUINT32(MF_SA_D3D11_AWARE,&aware)); if(!aware) return MF_E_UNSUPPORTED_D3D_TYPE;
        attrs->SetUINT32(CODECAPI_AVLowLatencyMode,TRUE);
        attrs->SetUINT32(CODECAPI_AVDecVideoAcceleration_H264,TRUE);
        TRY(decoder->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,(ULONG_PTR)manager.p));
        Ptr<IMFMediaType> input; TRY(MFCreateMediaType(input.out()));
        TRY(input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video)); TRY(input->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264));
        TRY(MFSetAttributeSize(input.p,MF_MT_FRAME_SIZE,imageW,imageH));
        TRY(MFSetAttributeRatio(input.p,MF_MT_FRAME_RATE,30,1));
        TRY(input->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
        TRY(decoder->SetInputType(0,input.p,0)); TRY(outputType());
        TRY(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,0));
        TRY(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,0)); s.stage(L"ready"); return S_OK;
    }
    HRESULT outputType() {
        s.stage(L"negotiate_nv12");
        for(DWORD i=0;i<64;++i) {
            Ptr<IMFMediaType> type; HRESULT hr=decoder->GetOutputAvailableType(0,i,type.out()); if(FAILED(hr)) return hr;
            GUID subtype; TRY(type->GetGUID(MF_MT_SUBTYPE,&subtype)); if(subtype!=MFVideoFormat_NV12) continue;
            TRY(decoder->SetOutputType(0,type.p,0));
            UINT32 w=0,h=0; TRY(MFGetAttributeSize(type.p,MF_MT_FRAME_SIZE,&w,&h));
            if(w<48||h<48||w>4096||h>2304) return E_INVALIDARG;
            imageW=w; imageH=h; sourceX=sourceY=0;
            MFVideoArea area={}; UINT32 areaSize=0;
            HRESULT aperture=type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE,(UINT8*)&area,sizeof(area),&areaSize);
            if(FAILED(aperture)) aperture=type->GetBlob(MF_MT_GEOMETRIC_APERTURE,(UINT8*)&area,sizeof(area),&areaSize);
            if(SUCCEEDED(aperture) && areaSize==sizeof(area) && area.Area.cx>0 && area.Area.cy>0 &&
               area.OffsetX.value>=0 && area.OffsetY.value>=0 &&
               unsigned(area.OffsetX.value+area.Area.cx)<=w && unsigned(area.OffsetY.value+area.Area.cy)<=h) {
                sourceX=area.OffsetX.value; sourceY=area.OffsetY.value; imageW=area.Area.cx; imageH=area.Area.cy;
            }
            matrix=imageH>576?MFVideoTransferMatrix_BT709:MFVideoTransferMatrix_BT601;
            type->GetUINT32(MF_MT_YUV_MATRIX,&matrix); type->GetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,&range);
            processor.reset(); enumerator.reset(); last.reset();
            EnterCriticalSection(&s.lock); s.stats.width=imageW; s.stats.height=imageH; LeaveCriticalSection(&s.lock);
            return S_OK;
        }
        return MF_E_INVALIDMEDIATYPE;
    }
    HRESULT target() {
        RECT client; if(!GetClientRect(s.window,&client)) return HRESULT_FROM_WIN32(GetLastError());
        unsigned w=client.right>0?client.right:1,h=client.bottom>0?client.bottom:1;
        if(w!=windowW||h!=windowH) {
            TRY(swap->ResizeBuffers(2,w,h,DXGI_FORMAT_B8G8R8A8_UNORM,0));
            windowW=w; windowH=h; processor.reset(); enumerator.reset();
        }
        if(!processor.p) {
            D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc={}; desc.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
            desc.InputFrameRate.Numerator=30; desc.InputFrameRate.Denominator=1; desc.OutputFrameRate=desc.InputFrameRate;
            desc.InputWidth=imageW; desc.InputHeight=imageH; desc.OutputWidth=windowW; desc.OutputHeight=windowH;
            desc.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
            TRY(videoDevice->CreateVideoProcessorEnumerator(&desc,enumerator.out()));
            TRY(videoDevice->CreateVideoProcessor(enumerator.p,0,processor.out()));
        }
        return S_OK;
    }
    HRESULT present(IMFSample *sample, bool count, LONGLONG queued) {
        s.stage(L"present_gpu_surface");
        Ptr<IMFMediaBuffer> buffer; Ptr<IMFDXGIBuffer> dxgi; Ptr<ID3D11Texture2D> texture,back;
        TRY(sample->GetBufferByIndex(0,buffer.out()));
        HRESULT hr=buffer->QueryInterface(__uuidof(IMFDXGIBuffer),(void**)dxgi.out());
        if(FAILED(hr)) {
            EnterCriticalSection(&s.lock); ++s.stats.cpuSamples; LeaveCriticalSection(&s.lock);
            // Explicit experiment: don't silently turn a failed GPU path into CPU copies.
            return MF_E_UNSUPPORTED_D3D_TYPE;
        }
        TRY(dxgi->GetResource(__uuidof(ID3D11Texture2D),(void**)texture.out())); UINT sub=0; TRY(dxgi->GetSubresourceIndex(&sub));
        D3D11_TEXTURE2D_DESC td; texture->GetDesc(&td); TRY(target());
        TRY(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)back.out()));
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input={}; input.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;
        input.Texture2D.ArraySlice=sub/td.MipLevels; input.Texture2D.MipSlice=sub%td.MipLevels;
        Ptr<ID3D11VideoProcessorInputView> inView;
        TRY(videoDevice->CreateVideoProcessorInputView(texture.p,enumerator.p,&input,inView.out()));
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output={}; output.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
        Ptr<ID3D11VideoProcessorOutputView> outView;
        TRY(videoDevice->CreateVideoProcessorOutputView(back.p,enumerator.p,&output,outView.out()));
        unsigned pw=windowW,ph=(unsigned)((unsigned __int64)windowW*imageH/imageW);
        if(ph>windowH) { ph=windowH; pw=(unsigned)((unsigned __int64)windowH*imageW/imageH); }
        RECT source={sourceX,sourceY,sourceX+(LONG)imageW,sourceY+(LONG)imageH};
        RECT dest={(LONG)((windowW-pw)/2),(LONG)((windowH-ph)/2),(LONG)((windowW-pw)/2+pw),(LONG)((windowH-ph)/2+ph)};
        RECT full={0,0,(LONG)windowW,(LONG)windowH};
        videoContext->VideoProcessorSetStreamFrameFormat(processor.p,0,D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        videoContext->VideoProcessorSetStreamSourceRect(processor.p,0,TRUE,&source);
        videoContext->VideoProcessorSetStreamDestRect(processor.p,0,TRUE,&dest);
        videoContext->VideoProcessorSetOutputTargetRect(processor.p,TRUE,&full);
        D3D11_VIDEO_COLOR color={}; videoContext->VideoProcessorSetOutputBackgroundColor(processor.p,FALSE,&color);
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE inColor={}; inColor.YCbCr_Matrix=matrix==MFVideoTransferMatrix_BT709;
        inColor.Nominal_Range=range==MFNominalRange_0_255?2:1;
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE outColor={}; outColor.RGB_Range=0;
        videoContext->VideoProcessorSetStreamColorSpace(processor.p,0,&inColor);
        videoContext->VideoProcessorSetOutputColorSpace(processor.p,&outColor);
        videoContext->VideoProcessorSetStreamAutoProcessingMode(processor.p,0,FALSE);
        D3D11_VIDEO_PROCESSOR_STREAM stream={}; stream.Enable=TRUE; stream.pInputSurface=inView.p;
        TRY(videoContext->VideoProcessorBlt(processor.p,outView.p,0,1,&stream));
        context->Flush();
        LONGLONG begin=nowUs(); TRY(swap->Present(0,0)); LONGLONG end=nowUs();
        if(count) {
            EnterCriticalSection(&s.lock); ++s.stats.frames; ++s.stats.gpuSamples;
            if(td.BindFlags&D3D11_BIND_DECODER) ++s.stats.decoderSurfaces;
            s.stats.totalPresentUs+=end-begin; LONGLONG elapsed=end-queued;
            s.stats.totalQueueUs+=elapsed; if((unsigned __int64)elapsed>s.stats.maxQueueUs) s.stats.maxQueueUs=elapsed;
            LeaveCriticalSection(&s.lock);
            if(s.callback) s.callback(s.context);
        }
        s.stage(L"running"); return S_OK;
    }
    HRESULT drain(LONGLONG queued) {
        for(unsigned i=0;i<128;++i) {
            MFT_OUTPUT_DATA_BUFFER output={}; DWORD status=0;
            HRESULT hr=decoder->ProcessOutput(0,1,&output,&status);
            if(output.pEvents) output.pEvents->Release();
            Ptr<IMFSample> sample; sample.reset(output.pSample);
            if(hr==MF_E_TRANSFORM_NEED_MORE_INPUT) return S_OK;
            if(hr==MF_E_TRANSFORM_STREAM_CHANGE) { TRY(outputType()); continue; }
            if(FAILED(hr)) return hr;
            if(sample.p) {
                TRY(present(sample.p,true,queued)); sample->AddRef(); last.reset(sample.p);
            }
        }
        return E_UNEXPECTED;
    }
    HRESULT submit(Packet &packet) {
        s.stage(L"decode_h264"); Ptr<IMFMediaBuffer> buffer; Ptr<IMFSample> sample;
        TRY(MFCreateMemoryBuffer(packet.size,buffer.out())); BYTE *data=0; TRY(buffer->Lock(&data,0,0));
        CopyMemory(data,packet.data,packet.size); buffer->Unlock(); TRY(buffer->SetCurrentLength(packet.size));
        TRY(MFCreateSample(sample.out())); TRY(sample->AddBuffer(buffer.p));
        TRY(sample->SetSampleTime(sequence++*333333)); TRY(sample->SetSampleDuration(333333));
        HRESULT hr=decoder->ProcessInput(0,sample.p,0);
        if(hr==MF_E_NOTACCEPTING) { TRY(drain(packet.queued)); hr=decoder->ProcessInput(0,sample.p,0); }
        if(FAILED(hr)) return hr; return drain(packet.queued);
    }
    HRESULT finish() {
        if(!decoder.p) return S_OK;
        TRY(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM,0));
        TRY(decoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN,0));
        HRESULT hr=drain(nowUs()); decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING,0);
        last.reset(); return hr;
    }
};
DWORD WINAPI worker(void *arg) {
    State &s=*(State*)arg; HRESULT com=CoInitializeEx(0,COINIT_MULTITHREADED);
    HRESULT startup=FAILED(com)?com:MFStartup(MF_VERSION,MFSTARTUP_FULL);
    const bool mfStarted=SUCCEEDED(startup);
    bool ownsWindow=!s.window;
    if(SUCCEEDED(startup) && ownsWindow) {
        s.window=CreateWindowW(L"STATIC",L"CarPlay headless media test",WS_POPUP,0,0,s.width,s.height,0,0,GetModuleHandleW(0),0);
        if(!s.window)startup=HRESULT_FROM_WIN32(GetLastError());
    }
    HRESULT result=startup;
    if(SUCCEEDED(startup)) {
        Decoder d(s); result=d.init(); if(FAILED(result)) s.stage(L"initialization_failed",result);
        SetEvent(s.initialized);
        while(SUCCEEDED(result)) {
            if(ownsWindow) { MSG msg; while(PeekMessageW(&msg,0,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);} }
            EnterCriticalSection(&s.lock); Packet *packet=s.head;
            if(packet) { s.head=packet->next; if(!s.head) s.tail=0; s.stats.queuedBytes-=packet->size; }
            bool end=s.ending; LeaveCriticalSection(&s.lock);
            if(packet) { result=d.submit(*packet); HeapFree(GetProcessHeap(),0,packet); }
            else if(end) { result=d.finish(); break; }
            else {
                WaitForSingleObject(s.event,33);
                if(d.last.p) {
                    RECT r; if(GetClientRect(s.window,&r) && r.right>0 && r.bottom>0 &&
                        ((unsigned)r.right!=d.windowW||(unsigned)r.bottom!=d.windowH)) result=d.present(d.last.p,false,nowUs());
                }
            }
        }
        if(FAILED(result)) s.stage(L"media_foundation_failed",result);
    } else { s.stage(L"startup_failed",result); SetEvent(s.initialized); }
    // Decoder and D3D objects are released on their owning worker before MF/COM.
    if(ownsWindow && s.window)DestroyWindow(s.window);
    if(mfStarted) MFShutdown(); if(SUCCEEDED(com)) CoUninitialize();
    return 0;
}
}
extern "C" HRESULT cpMfCreate(HWND window,unsigned w,unsigned h,CpMfFrame f,void *ctx,void **handle) {
    if(!handle||(window && !IsWindow(window))||w<48||h<48||w>4096||h>2304) return E_INVALIDARG; *handle=0;
    // Allocated and released in this module; no C++ objects cross the C boundary.
    State *s=new State(window,w,h,f,ctx);
    s->event=CreateEventW(0,FALSE,FALSE,0); s->initialized=CreateEventW(0,TRUE,FALSE,0);
    if(!s->event||!s->initialized) { delete s; return E_OUTOFMEMORY; }
    s->thread=CreateThread(0,0,worker,s,0,0);
    if(!s->thread) { delete s; return E_OUTOFMEMORY; }
    WaitForSingleObject(s->initialized,INFINITE);
    if(FAILED(s->stats.error)) { HRESULT hr=s->stats.error; cpMfDestroy(s); return hr; }
    *handle=s; return S_OK;
}
extern "C" HRESULT cpMfSubmit(void *handle,const unsigned char *data,unsigned size) {
    State *s=(State*)handle; if(!s||!data||!size||size>8*1024*1024) return E_INVALIDARG;
    Packet *p=(Packet*)HeapAlloc(GetProcessHeap(),0,sizeof(Packet)+size); if(!p) return E_OUTOFMEMORY;
    p->next=0; p->size=size; p->queued=nowUs(); CopyMemory(p->data,data,size);
    EnterCriticalSection(&s->lock); HRESULT hr=s->stats.error;
    if(s->ending) hr=MF_E_SHUTDOWN;
    if(s->stats.queuedBytes+size>16*1024*1024) hr=HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    if(SUCCEEDED(hr)) {
        if(s->tail) s->tail->next=p; else s->head=p; s->tail=p;
        s->stats.queuedBytes+=size; ++s->stats.submitted;
        if(s->stats.queuedBytes>s->stats.maxQueueBytes) s->stats.maxQueueBytes=s->stats.queuedBytes;
    }
    LeaveCriticalSection(&s->lock);
    if(FAILED(hr)) HeapFree(GetProcessHeap(),0,p); else SetEvent(s->event); return hr;
}
extern "C" void cpMfStats(void *handle,CpMfStats *stats) {
    State *s=(State*)handle; if(!s||!stats) return;
    EnterCriticalSection(&s->lock); *stats=s->stats; LeaveCriticalSection(&s->lock);
}
extern "C" HRESULT cpMfFinish(void *handle) {
    State *s=(State*)handle; if(!s) return E_INVALIDARG;
    EnterCriticalSection(&s->lock); s->ending=true; LeaveCriticalSection(&s->lock); SetEvent(s->event);
    WaitForSingleObject(s->thread,INFINITE); return s->stats.error;
}
extern "C" void cpMfDestroy(void *handle) { if(handle) { cpMfFinish(handle); delete (State*)handle; } }
