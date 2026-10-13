#include "../../devices/profile.h"
#include "video_dshow.h"
#include "../common/video_queue.h"
#include "../common/diagnostics.h"
#if WINXPLAY_ENABLE_LOGS
#include "../common/report_worker.h"
#endif
#include <streams.h>
#include <sstream>
#include <algorithm>
HINSTANCE g_hInst=0; // DirectShow base classes.
namespace cp { namespace {
const GUID SourceId={0x8c29b9dc,0xd2cf,0x4849,{0xaa,0x71,0x96,0x23,0x40,0x87,0xe1,0x22}};
const GUID H264Type={0x34363248,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
struct Lock {
    CRITICAL_SECTION &m; explicit Lock(CRITICAL_SECTION &v):m(v){EnterCriticalSection(&m);}
    ~Lock(){LeaveCriticalSection(&m);}
};
template<class T> struct Com {
    T *p; Com():p(0){} ~Com(){if(p)p->Release();}
    T *operator->() const{return p;}
    T **out(){return &p;}
  private: Com(const Com&);Com &operator=(const Com&);
};
void check(HRESULT hr,const char *name) {
    if(FAILED(hr)){std::ostringstream s;s<<name<<" failed: 0x"<<std::hex<<unsigned(hr);throw std::runtime_error(s.str());}
}
struct State {
    mutable CRITICAL_SECTION mutex;
    HANDLE stop,ready;
    VideoQueue queue;
    std::string error;
    LONG supplied;
    unsigned submitted, messages, maxSample;
    U64 enqueued, nonqueued, waitMs;
    mutable unsigned queuePeak;
    mutable DWORD queueAgeMax, waitMax, supplyGapMax;
    DWORD suppliedAt;
    int drawn,dropped;
    DWORD qualityResult;
    REFERENCE_TIME lastSampleTime;
    unsigned clockedSamples;
    unsigned width,height,maxFPS;
    HWND owner;
    bool rgbTest;
    DWORD lastTrace;
    std::string lastStage;
#if WINXPLAY_ENABLE_LOGS
    ReportWorker reports;
    static void writeReport(const Value &v,void *context){
        State &s=*(State*)context;
        const std::string &stage=v.at("stage").string;
        // Only this writer touches lastStage/lastTrace; never block the renderer
        // window/message thread on diagnostic storage or its trace mutex.
        if(s.lastStage!=stage || DWORD(GetTickCount()-s.lastTrace)>=10000){
            std::ostringstream detail;detail<<stage<<" samples_supplied="<<v.at("samples_supplied").integer
                <<" queued="<<v.at("queued_frames").integer<<" width="<<v.at("width").integer<<" height="<<v.at("height").integer
                <<" received="<<v.at("samples_received").integer
                <<" drawn="<<(v.dict.count("renderer_frames_drawn")?I64(v.at("renderer_frames_drawn").integer):I64(-1))
                <<" dropped="<<(v.dict.count("renderer_frames_dropped")?I64(v.at("renderer_frames_dropped").integer):I64(-1))
                <<" messages="<<v.at("window_messages_processed").integer
                <<" free_phys="<<v.at("free_physical_bytes").integer<<" free_virtual="<<v.at("free_virtual_bytes").integer
                <<" reports_dropped="<<v.at("diagnostic_reports_dropped").integer
                <<" reports_failed="<<v.at("diagnostic_reports_failed").integer;
            if(v.dict.count("error"))detail<<" error="<<v.at("error").string;
            diagnosticEvent("VIDEO_GRAPH",detail.str());s.lastStage=stage;s.lastTrace=GetTickCount();
        }
        writeDiagnosticFile(executableDirectory()+L"/video-backend.plist",xmlPlist(v));
    }
#endif
    State(HWND w,unsigned x,unsigned y,unsigned fps,bool rgb):stop(0),ready(0),supplied(0),submitted(0),messages(0),maxSample(0),drawn(-1),dropped(-1),qualityResult(E_NOINTERFACE),lastSampleTime(0),clockedSamples(0),width(x),height(y),maxFPS(fps),owner(w),rgbTest(rgb),lastTrace(0)
#if WINXPLAY_ENABLE_LOGS
        ,reports(writeReport,this)
#endif
    {
        require(maxFPS>=1 && maxFPS<=60,"Invalid video frame rate");
        enqueued=nonqueued=waitMs=0;queuePeak=0;queueAgeMax=waitMax=supplyGapMax=suppliedAt=0;
        InitializeCriticalSection(&mutex);
        stop=CreateEventW(0,TRUE,FALSE,0);ready=CreateEventW(0,FALSE,FALSE,0);
        if(!stop||!ready){if(stop)CloseHandle(stop);if(ready)CloseHandle(ready);DeleteCriticalSection(&mutex);throw std::runtime_error("Video events failed");}
    }
    ~State(){
#if WINXPLAY_ENABLE_LOGS
        reports.flush();
#endif
        CloseHandle(stop);CloseHandle(ready);DeleteCriticalSection(&mutex);
    }
    bool stopped() const{return WaitForSingleObject(stop,0)==WAIT_OBJECT_0;}
    bool take(VideoQueue::Sample &out){
        HANDLE events[]={stop,ready};
        for(;;){
            if(stopped())return false;
            {Lock lock(mutex);if(queue.pop(out)){
                queueAgeMax=std::max(queueAgeMax,DWORD(GetTickCount()-out.queuedAt));return true;}}
            if(WaitForMultipleObjects(2,events,FALSE,INFINITE)==WAIT_OBJECT_0)return false;
        }
    }
    void report(const char *stage){
        Value v=Value::dictionary();v.dict["backend"]=Value::str("wince_directshow");
        v.dict["stage"]=Value::str(stage);v.dict["width"]=Value::num(width);v.dict["height"]=Value::num(height);
        v.dict["max_fps"]=Value::num(maxFPS);
        v.dict["sample_duration_100ns"]=Value::num(10000000/maxFPS);
        v.dict["samples_supplied"]=Value::num(InterlockedCompareExchange(&supplied,0,0));
        v.dict["window_messages_processed"]=Value::num(messages);
        v.dict["renderer_statistics_result"]=Value::num(qualityResult);
        v.dict["renderer_statistics_verified"]=Value::flag(false);
        if(drawn>=0)v.dict["renderer_frames_drawn"]=Value::num(drawn);
        if(dropped>=0)v.dict["renderer_frames_dropped"]=Value::num(dropped);
        v.dict["physical_display_verified"]=Value::flag(false);
        v.dict["audio_managed_separately"]=Value::flag(true);v.dict["clock_scheduled"]=Value::flag(true);
        {Lock lock(mutex);if(!error.empty())v.dict["error"]=Value::str(error);v.dict["queued_frames"]=Value::num(queue.size());
            v.dict["samples_received"]=Value::num(submitted);v.dict["largest_sample_bytes"]=Value::num(maxSample);
            v.dict["last_sample_time_100ns"]=Value::num(lastSampleTime);v.dict["samples_clocked"]=Value::num(clockedSamples);}
        v.dict["uptime_ms"]=Value::num(GetTickCount());
        MEMORYSTATUS memory={0};memory.dwLength=sizeof(memory);GlobalMemoryStatus(&memory);
        v.dict["free_physical_bytes"]=Value::num(memory.dwAvailPhys);
        v.dict["free_virtual_bytes"]=Value::num(memory.dwAvailVirtual);
#if WINXPLAY_ENABLE_LOGS
        try{reports.submit(v);}catch(...){}
#endif
    }
};
// The renderer creates windows on the graph thread. Pump this thread's queue,
// including while no new phone frames arrive; the application UI loop is on a
// different thread and cannot dispatch these messages.
void pumpVideoMessages(State &s){
    MSG m;
    for(unsigned i=0;i<64 && PeekMessageW(&m,0,0,0,PM_REMOVE);++i){
        if(m.message==WM_QUIT){SetEvent(s.stop);break;}
        TranslateMessage(&m);DispatchMessageW(&m);++s.messages;
    }
}
void describeVideoPacket(const Bytes &b,unsigned sequence){
    // Metadata only: never record compressed picture contents in the log.
    std::ostringstream detail;detail<<"sequence="<<sequence<<" bytes="<<b.size()<<" nal_types=";
    unsigned count=0;
    for(size_t i=0;i+3<b.size() && count<24;++i){
        if(b[i]||b[i+1])continue;
        unsigned prefix=b[i+2]==1?3:(i+4<b.size() && !b[i+2] && b[i+3]==1?4:0);
        if(!prefix)continue;
        unsigned type=b[i+prefix]&31;detail<<(count?",":"")<<type;++count;
        if(type==7 && i+prefix+3<b.size())detail<<"(profile="<<unsigned(b[i+prefix+1])
            <<",constraints="<<unsigned(b[i+prefix+2])<<",level="<<unsigned(b[i+prefix+3])<<")";
        i+=prefix;
    }
    diagnosticEvent("VIDEO_PACKET",detail.str());
}
void drainVideo(State &s,IMediaEvent *events){
    SetEvent(s.stop); // FillBuffer emits EOS; unblock a source waiting for phone data.
    if(!InterlockedCompareExchange(&s.supplied,0,0))return;
    diagnosticEvent("VIDEO_DRAIN_BEGIN");
    DWORD start=GetTickCount();bool complete=false;
    while(!complete && DWORD(GetTickCount()-start)<2000){
        pumpVideoMessages(s);
        long event;LONG_PTR a,b;
        while(events->GetEvent(&event,&a,&b,0)==S_OK){
            events->FreeEventParams(event,a,b);
            if(event==EC_COMPLETE || event==EC_ERRORABORT)complete=true;
        }
        if(!complete)Sleep(10);
    }
    diagnosticEvent(complete?"VIDEO_DRAIN_COMPLETE":"VIDEO_DRAIN_TIMEOUT");
}
class Stream:public CSourceStream {
    State &s_;unsigned sequence_;REFERENCE_TIME nextTime_;
  public:
    Stream(HRESULT *hr,CSource *owner,State &s):CSourceStream(L"CarPlay H264",hr,owner,L"Video"),s_(s),sequence_(0),nextTime_(0){}
    HRESULT DoBufferProcessingLoop(){
        __try{return CSourceStream::DoBufferProcessingLoop();}
        __except(diagnosticException(GetExceptionInformation(),L"dshow_delivery")){return E_UNEXPECTED;}
    }
    HRESULT GetMediaType(CMediaType *mt){
        mt->InitMediaType();mt->SetType(&MEDIATYPE_Video);mt->SetSubtype(s_.rgbTest?&MEDIASUBTYPE_RGB565:&H264Type);mt->SetFormatType(&FORMAT_VideoInfo);
        unsigned size=sizeof(VIDEOINFOHEADER)+(s_.rgbTest?3*sizeof(DWORD):0);
        VIDEOINFOHEADER *v=(VIDEOINFOHEADER*)mt->AllocFormatBuffer(size);if(!v)return E_OUTOFMEMORY;
        ZeroMemory(v,size);v->AvgTimePerFrame=10000000/s_.maxFPS;v->bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        v->bmiHeader.biWidth=s_.width;v->bmiHeader.biHeight=s_.height;v->bmiHeader.biPlanes=1;
        v->bmiHeader.biBitCount=s_.rgbTest?16:24;v->bmiHeader.biCompression=s_.rgbTest?BI_BITFIELDS:0x34363248;
        v->bmiHeader.biSizeImage=s_.rgbTest?s_.width*s_.height*2:VideoQueue::MaxSample;
        if(s_.rgbTest){DWORD *m=(DWORD*)((BYTE*)v+sizeof(VIDEOINFOHEADER));m[0]=0xf800;m[1]=0x07e0;m[2]=0x001f;}
        mt->SetSampleSize(v->bmiHeader.biSizeImage);mt->SetTemporalCompression(!s_.rgbTest);mt->bFixedSizeSamples=s_.rgbTest;return S_OK;
    }
    HRESULT DecideBufferSize(IMemAllocator *a,ALLOCATOR_PROPERTIES *p){
        p->cBuffers=3;p->cbBuffer=VideoQueue::MaxSample;if(p->cbAlign<1)p->cbAlign=1;
        ALLOCATOR_PROPERTIES actual;HRESULT hr=a->SetProperties(p,&actual);if(FAILED(hr))return hr;
        return actual.cbBuffer>=p->cbBuffer && actual.cBuffers>=p->cBuffers?S_OK:E_FAIL;
    }
    HRESULT FillBuffer(IMediaSample *sample){
        try {
            VideoQueue::Sample frame;if(!s_.take(frame))return S_FALSE;
            BYTE *data=0;HRESULT hr=sample->GetPointer(&data);if(FAILED(hr))return hr;
            if(sample->GetSize()<LONG(frame.bytes.size()))return E_FAIL;
            memcpy(data,&frame.bytes[0],frame.bytes.size());sample->SetActualDataLength(frame.bytes.size());
            if(sequence_<8){std::ostringstream detail;detail<<"sequence="<<sequence_<<" bytes="<<frame.bytes.size()<<" key="<<frame.key;
                diagnosticEvent("VIDEO_SAMPLE_SUPPLIED",detail.str());}
            // Keep the same default graph clock as the verified finite test.
            // Live screens can stop sending while unchanged: resume at current
            // stream time instead of dating the next frame many seconds late.
            // This is local scheduling, not recovered phone/audio PTS sync.
            REFERENCE_TIME t=nextTime_;CRefTime now;FILTER_STATE state;
            bool clocked=SUCCEEDED(m_pFilter->GetState(0,&state)) && state==State_Running &&
                SUCCEEDED(m_pFilter->StreamTime(now));
            if(clocked && now.GetUnits()>t)t=now.GetUnits();
            REFERENCE_TIME end=t+10000000/s_.maxFPS;nextTime_=end;
            sample->SetTime(&t,&end);sample->SetSyncPoint(frame.key);sample->SetDiscontinuity(sequence_==0);sample->SetPreroll(FALSE);
            {Lock lock(s_.mutex);s_.lastSampleTime=t;if(clocked)++s_.clockedSamples;
                DWORD tick=GetTickCount();if(sequence_)s_.supplyGapMax=std::max(s_.supplyGapMax,DWORD(tick-s_.suppliedAt));
                s_.suppliedAt=tick;InterlockedIncrement(&s_.supplied);}
            ++sequence_;return S_OK;
        }catch(const std::exception &e){Lock lock(s_.mutex);s_.error=e.what();SetEvent(s_.stop);return E_FAIL;}
    }
};
class Source:public CSource {
  public: Source(HRESULT *hr,State &s):CSource(L"CarPlay live memory source",0,SourceId,hr){new Stream(hr,this,s);}
};
IPin *pin(IBaseFilter *filter,PIN_DIRECTION wanted){
    Com<IEnumPins> all;check(filter->EnumPins(all.out()),"EnumPins");IPin *p=0;
    while(all->Next(1,&p,0)==S_OK){PIN_DIRECTION d;if(SUCCEEDED(p->QueryDirection(&d))&&d==wanted){
        IPin *other=0;HRESULT hr=p->ConnectedTo(&other);if(other)other->Release();if(hr==VFW_E_NOT_CONNECTED)return p;
    }p->Release();}throw std::runtime_error("No free video pin");
}
void connect(IGraphBuilder *g,IBaseFilter *a,IBaseFilter *b){Com<IPin> out,in;out.p=pin(a,PINDIR_OUTPUT);in.p=pin(b,PINDIR_INPUT);check(g->ConnectDirect(out.p,in.p,0),"ConnectDirect");}
void filter(const wchar_t *text,Com<IBaseFilter> &out){CLSID id;check(CLSIDFromString(const_cast<wchar_t*>(text),&id),"CLSID");check(CoCreateInstance(id,0,CLSCTX_INPROC_SERVER,IID_IBaseFilter,(void**)out.out()),"Create filter");}
void graph(State &s){
    Com<IGraphBuilder> g;Com<IMediaControl> control;Com<IMediaEvent> events;
    Com<IQualProp> quality;
    Com<IVideoWindow> view;Com<IBaseFilter> source,decoder,renderer;
    s.report("creating_graph");
    check(CoCreateInstance(CLSID_FilterGraph,0,CLSCTX_INPROC_SERVER,IID_IGraphBuilder,(void**)g.out()),"Create graph");
    check(g->QueryInterface(IID_IMediaControl,(void**)control.out()),"Media control");
    check(g->QueryInterface(IID_IMediaEvent,(void**)events.out()),"Media events");
    HRESULT hr=S_OK;Source *raw=new Source(&hr,s);raw->AddRef();source.p=raw;check(hr,"Create source");
    check(g->AddFilter(source.p,L"Live H264 source"),"Add source");
    require(deviceProfile().videoRenderer!=0,"Device video renderer is not configured");
    filter(deviceProfile().videoRenderer,renderer);
    check(g->AddFilter(renderer.p,L"System video renderer"),"Add renderer");
    if(!s.rgbTest){require(deviceProfile().h264Decoder!=0,"Device H264 decoder is not configured");
        filter(deviceProfile().h264Decoder,decoder);
        check(g->AddFilter(decoder.p,L"Device H264 decoder"),"Add decoder");}
    s.qualityResult=renderer->QueryInterface(IID_IQualProp,(void**)quality.out());
    if(decoder.p){connect(g.p,source.p,decoder.p);connect(g.p,decoder.p,renderer.p);}
    else connect(g.p,source.p,renderer.p);
    check(g->QueryInterface(IID_IVideoWindow,(void**)view.out()),"Video window");
    try {
        check(view->put_Owner((OAHWND)s.owner),"Video owner");
        check(view->put_WindowStyle(WS_CHILD|WS_CLIPSIBLINGS),"Video style");
        check(view->put_MessageDrain((OAHWND)s.owner),"Message drain");
        RECT r;GetClientRect(s.owner,&r);
        check(view->SetWindowPosition(0,0,r.right,r.bottom),"Video position");
        check(view->put_Visible(OATRUE),"Video visible");
        check(control->Run(),"Run graph");s.report("running");
#ifdef CARPLAY_DSHOW_TEST
        // Queued on the renderer's thread, not the application UI thread.
        PostThreadMessageW(GetCurrentThreadId(),WM_APP+73,0,0);
#endif
        DWORD last=GetTickCount();
        LONG clientWidth=r.right,clientHeight=r.bottom;
        while(!s.stopped()){
            pumpVideoMessages(s);
            RECT area;GetClientRect(s.owner,&area);
            if(area.right>0 && area.bottom>0 && (area.right!=clientWidth || area.bottom!=clientHeight)) {
                clientWidth=area.right;clientHeight=area.bottom;
                check(view->SetWindowPosition(0,0,clientWidth,clientHeight),"Resize video window");
            }
            long event;LONG_PTR a,b;
            while(events->GetEvent(&event,&a,&b,0)==S_OK){events->FreeEventParams(event,a,b);
                std::ostringstream detail;detail<<"code="<<event<<" p1="<<a<<" p2="<<b;diagnosticEvent("VIDEO_EVENT",detail.str());
                if(event==EC_ERRORABORT){check(HRESULT(a),"Video graph error");throw std::runtime_error("Video graph aborted");}
                if(event==EC_COMPLETE)throw std::runtime_error("Live video ended unexpectedly");
            }
            if(GetTickCount()-last>=1000){
                if(quality.p){int value=0;if(SUCCEEDED(quality->get_FramesDrawn(&value)))s.drawn=value;
                    if(SUCCEEDED(quality->get_FramesDroppedInRenderer(&value)))s.dropped=value;}
                s.report("running");last=GetTickCount();}
            WaitForSingleObject(s.stop,20);
        }
    }catch(...){
        drainVideo(s,events.p);diagnosticEvent("VIDEO_STOP_AFTER_ERROR");
        control->Stop();view->put_Visible(OAFALSE);view->put_MessageDrain(0);view->put_Owner(0);throw;
    }
    s.report("stopping");
    // Wake FillBuffer before Stop so the base-class streaming thread can exit.
    drainVideo(s,events.p);diagnosticEvent("VIDEO_STOP_BEGIN");HRESULT stopResult=control->Stop();
    diagnosticEvent("VIDEO_STOP_RETURNED");
    view->put_Visible(OAFALSE);view->put_MessageDrain(0);view->put_Owner(0);
    g->RemoveFilter(source.p);if(decoder.p)g->RemoveFilter(decoder.p);g->RemoveFilter(renderer.p);
    check(stopResult,"Stop graph");
    diagnosticEvent("VIDEO_RELEASE_BEGIN");
}
DWORD videoWorker(void *p){
    State &s=*(State*)p;HRESULT hr=CoInitializeEx(0,COINIT_MULTITHREADED);
    try{check(hr,"COM initialize");graph(s);}
    catch(const std::exception &e){Lock lock(s.mutex);s.error=e.what();}
    if(SUCCEEDED(hr))CoUninitialize();SetEvent(s.stop);s.report("closed");return 0;
}
DWORD WINAPI worker(void *p){
    __try{return videoWorker(p);}
    __except(diagnosticException(GetExceptionInformation(),L"dshow_graph")){return 1;}
}
class Pipeline:public VideoPipeline {
    State state_;HANDLE thread_;
  public:
    Pipeline(HWND owner,unsigned w,unsigned h,unsigned maxFPS,bool rgb=false):state_(owner,w,h,maxFPS,rgb),thread_(0){
        require(owner&&IsWindow(owner),"DirectShow needs a live display window");
        if(!g_hInst)g_hInst=GetModuleHandleW(0);
        thread_=CreateThread(0,0,worker,&state_,0,0);require(thread_!=0,"Video worker failed");
    }
    ~Pipeline(){finish();CloseHandle(thread_);}
    void submit(const Bytes &b,U64){
        require(b.size()<=VideoQueue::MaxSample,"Video access unit limit");
        DWORD start=GetTickCount();bool waited=false;
        for(;;){
            {Lock lock(state_.mutex);require(state_.error.empty(),state_.error.c_str());
                require(!state_.stopped(),"Video pipeline closed");
                if(state_.queue.capacityFor(b.size())){
                    if(!state_.rgbTest && state_.submitted<8)describeVideoPacket(b,state_.submitted);
                    bool queued=true;
                    if(state_.rgbTest){require(b.size()==state_.width*state_.height*2,"RGB test frame size");state_.queue.pushRaw(b,GetTickCount());}
                    else queued=state_.queue.push(b,GetTickCount());
                    if(queued)++state_.enqueued;else ++state_.nonqueued;
                    state_.queuePeak=std::max(state_.queuePeak,unsigned(state_.queue.size()));
                    if(waited){DWORD elapsed=GetTickCount()-start;state_.waitMs+=elapsed;state_.waitMax=std::max(state_.waitMax,elapsed);}
                    ++state_.submitted;state_.maxSample=std::max(state_.maxSample,unsigned(b.size()));
                    SetEvent(state_.ready);return;
                }
            }
            waited=true;
            if(GetTickCount()-start>=500){Lock lock(state_.mutex);DWORD elapsed=GetTickCount()-start;
                state_.waitMs+=elapsed;state_.waitMax=std::max(state_.waitMax,elapsed);
                throw std::runtime_error("Video decoder stalled; queue remains bounded");}
            Sleep(2);
        }
    }
    void finish(){SetEvent(state_.stop);WaitForSingleObject(thread_,INFINITE);
#if WINXPLAY_ENABLE_LOGS
        state_.reports.flush(); // Preserve the completed snapshot before returning.
#endif
    }
    unsigned frames() const{return unsigned(InterlockedCompareExchange(const_cast<LONG*>(&state_.supplied),0,0));}
    Value metrics() const {
        Lock lock(state_.mutex);Value v=Value::dictionary();DWORD now=GetTickCount();
        v.dict["max_fps"]=Value::num(state_.maxFPS);
        v.dict["supplied"]=Value::num(frames()); // Source samples, NOT decoded/displayed frames.
        v.dict["enqueued"]=Value::num(state_.enqueued);
        v.dict["nonqueued"]=Value::num(state_.nonqueued);
        v.dict["q_frames"]=Value::num(state_.queue.size());v.dict["q_bytes"]=Value::num(state_.queue.bytes());
        v.dict["q_peak"]=Value::num(state_.queuePeak);
        v.dict["q_age_ms"]=Value::num(state_.queue.oldestAge(now));
        v.dict["q_age_max_ms"]=Value::num(state_.queueAgeMax);
        v.dict["wait_ms_total"]=Value::num(state_.waitMs);v.dict["wait_max_ms"]=Value::num(state_.waitMax);
        v.dict["supply_gap_max_ms"]=Value::num(state_.supplyGapMax);
        v.dict["supply_idle_ms"]=Value::num(frames()?DWORD(now-state_.suppliedAt):0);
        state_.queuePeak=unsigned(state_.queue.size());state_.queueAgeMax=state_.waitMax=state_.supplyGapMax=0;
        return v;
    }
    void checkStatus() const {Lock lock(state_.mutex);require(state_.error.empty(),state_.error.c_str());}
};
} // namespace
VideoPipeline *createCeSystemVideo(HWND owner,unsigned w,unsigned h,unsigned maxFPS){return new Pipeline(owner,w,h,maxFPS);}
#ifdef CARPLAY_DSHOW_TEST
VideoPipeline *createCeRgbTestVideo(HWND owner,unsigned w,unsigned h,unsigned maxFPS){return new Pipeline(owner,w,h,maxFPS,true);}
#endif
} // namespace cp
