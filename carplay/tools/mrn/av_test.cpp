// Standalone finite-output experiment. No HMI termination, registry edits,
// Audio source requests only. Process tools are launched independently by the
// user through AUTOLAUNCH_PROD. No process control or registry/firmware writes.
#include "../../platform/common/native_services.h"
#include "av_clip.h"
#include <streams.h>
#include <cmath>
#include <memory>
#include <sstream>
#include "usb_audio_route.h"
#include "tts_pcm.h"
using namespace cp;
HINSTANCE g_hInst = 0;
namespace {
const GUID SourceId = {
    0xd44d8c23, 0x05f2, 0x4b58, {0xa2, 0x62, 0xab, 0x24, 0xd6, 0xf4, 0x50, 0x0c}};
const GUID H264Type = {0x34363248, 0, 0x10, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
const wchar_t *DecoderId = L"{3D581965-7C6E-4FE9-894F-50FC821BAE68}";
const wchar_t *RendererId = L"{70E102B0-5556-11CE-97C0-00AA0055595A}";
std::wstring folder, reportPath;
HANDLE logFile = INVALID_HANDLE_VALUE, cancelled = 0, finished = 0;
CRITICAL_SECTION logLock;
DWORD beginTick;
DWORD watchdogMilliseconds = 90000;
bool audioCancelTest = false;
volatile LONG externallyCancelled=0;
HWND window = 0;
unsigned displayPhase=0;
LONG supplied = 0;
Value report = Value::dictionary();
std::string hexHr(HRESULT hr) {
    char b[24];
    sprintf(b, "0x%08lx", (unsigned long)hr);
    return b;
}
void log(const std::string &s) {
    EnterCriticalSection(&logLock);
    char prefix[40];
    sprintf(prefix, "[%lu] ", (unsigned long)(GetTickCount() - beginTick));
    std::string line = std::string(prefix) + s + "\r\n";
    DWORD n = 0;
    WriteFile(logFile, line.data(), DWORD(line.size()), &n, 0);
    FlushFileBuffers(logFile);
    LeaveCriticalSection(&logLock);
}
void check(const char *stage, HRESULT hr) {
    log(std::string(stage) + " " + hexHr(hr));
    report.dict[stage] = Value::str(hexHr(hr));
    if (FAILED(hr))
        throw std::runtime_error(std::string(stage) + " failed: " + hexHr(hr));
}
bool stopped() {
    return externallyCancelled!=0 || WaitForSingleObject(cancelled, 0) == WAIT_OBJECT_0;
}
// UI-thread-only observations. Kept separate from the worker-owned report.
void observeForeground(Value &history, const char *stage) {
    HWND active=GetForegroundWindow();
    U64 handle=U64(ULONG_PTR(active));
    if(!history.array.empty() && history.array.back().at("hwnd").integer==handle)
        return;
    if(history.array.size()>=128)return;
    Value item=Value::dictionary();DWORD pid=0;wchar_t cls[128]={0};
    if(active){GetWindowThreadProcessId(active,&pid);GetClassNameW(active,cls,128);}
    item.dict["elapsed_ms"]=Value::num(GetTickCount()-beginTick);
    item.dict["hwnd"]=Value::num(handle);item.dict["process_id"]=Value::num(pid);
    item.dict["class"]=Value::str(utf8(cls));item.dict["stage"]=Value::str(stage);
    item.dict["test_window"]=Value::flag(active && active==window);
    history.array.push_back(item);
    std::ostringstream text;text<<"FOREGROUND stage="<<stage<<" hwnd="<<handle<<" pid="<<pid<<" class="<<utf8(cls);
    log(text.str());
}
void enumerateAudio() {
    Value devices=Value::list();unsigned count=waveOutGetNumDevs();
    report.dict["wave_devices"]=Value::num(count);
    for(unsigned i=0;i<count && i<32;++i) {
        WAVEOUTCAPS caps={0};MMRESULT r=waveOutGetDevCaps(i,&caps,sizeof(caps));
        Value item=Value::dictionary();item.dict["id"]=Value::num(i);item.dict["caps_result"]=Value::num(r);
        if(!r){item.dict["name"]=Value::str(utf8(caps.szPname));item.dict["channels"]=Value::num(caps.wChannels);
            item.dict["formats"]=Value::num(caps.dwFormats);item.dict["support"]=Value::num(caps.dwSupport);}
        devices.array.push_back(item);
        std::ostringstream text;text<<"WAVE_DEVICE id="<<i<<" caps="<<r<<" name="<<(r?"unknown":utf8(caps.szPname));log(text.str());
    }
    report.dict["wave_output_devices"]=devices;
}
template <class T> class Com {
    Com(const Com &);
    Com &operator=(const Com &);

  public:
    T *p;
    Com() : p(0) {}
    ~Com() {
        if (p)
            p->Release();
    }
    T *operator->() const {
        return p;
    }
    T **out() {
        return &p;
    }
};
class TestStream : public CSourceStream {
    const AvClip &clip_;
    unsigned next_, total_;

  public:
    TestStream(HRESULT *hr, CSource *owner, const AvClip &clip, unsigned total)
        : CSourceStream(L"MRN finite memory source", hr, owner, L"Video"), clip_(clip), next_(0), total_(total) {}
    HRESULT GetMediaType(CMediaType *mt) {
        mt->InitMediaType();
        mt->SetType(&MEDIATYPE_Video);
        mt->SetSubtype(clip_.codec == 1 ? &H264Type : &MEDIASUBTYPE_RGB565);
        mt->SetFormatType(&FORMAT_VideoInfo);
        unsigned extra = clip_.codec == 2 ? 3 * sizeof(DWORD) : 0;
        VIDEOINFOHEADER *v =
            (VIDEOINFOHEADER *)mt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER) + extra);
        if (!v)
            return E_OUTOFMEMORY;
        ZeroMemory(v, sizeof(VIDEOINFOHEADER) + extra);
        v->AvgTimePerFrame = 10000000 / clip_.fps;
        v->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        v->bmiHeader.biWidth = clip_.width;
        v->bmiHeader.biHeight = clip_.height;
        v->bmiHeader.biPlanes = 1;
        v->bmiHeader.biBitCount = clip_.codec == 1 ? 24 : 16;
        v->bmiHeader.biCompression = clip_.codec == 1 ? 0x34363248 : BI_BITFIELDS;
        v->bmiHeader.biSizeImage = clip_.maxFrame;
        if (extra) {
            DWORD *m = (DWORD *)((BYTE *)v + sizeof(VIDEOINFOHEADER));
            m[0] = 0xf800;
            m[1] = 0x07e0;
            m[2] = 0x001f;
        }
        mt->SetSampleSize(clip_.maxFrame);
        mt->SetTemporalCompression(clip_.codec == 1);
        if (clip_.codec == 1)
            mt->bFixedSizeSamples = FALSE;
        return S_OK;
    }
    HRESULT DecideBufferSize(IMemAllocator *allocator, ALLOCATOR_PROPERTIES *wanted) {
        wanted->cBuffers = 3;
        wanted->cbBuffer = clip_.maxFrame;
        if (wanted->cbAlign < 1)
            wanted->cbAlign = 1;
        ALLOCATOR_PROPERTIES actual;
        HRESULT hr = allocator->SetProperties(wanted, &actual);
        if (FAILED(hr))
            return hr;
        return actual.cbBuffer >= wanted->cbBuffer && actual.cBuffers >= wanted->cBuffers ? S_OK
                                                                                          : E_FAIL;
    }
    HRESULT FillBuffer(IMediaSample *sample) {
        if (stopped() || next_ == total_)
            return S_FALSE;
        // Reuse the bounded fixture, but keep presentation timestamps increasing.
        // Its first H264 access unit includes SPS/PPS and an IDR frame.
        const AvFrame &frame = clip_.frames[next_ % clip_.frames.size()];
        BYTE *data = 0;
        HRESULT hr = sample->GetPointer(&data);
        if (FAILED(hr))
            return hr;
        if (sample->GetSize() < LONG(frame.bytes.size()))
            return E_FAIL;
        memcpy(data, &frame.bytes[0], frame.bytes.size());
        REFERENCE_TIME start = LONGLONG(next_) * 10000000 / clip_.fps;
        REFERENCE_TIME end = LONGLONG(next_ + 1) * 10000000 / clip_.fps;
        sample->SetTime(&start, &end);
        sample->SetActualDataLength(frame.bytes.size());
        sample->SetSyncPoint(frame.key);
        sample->SetDiscontinuity(next_ == 0);
        sample->SetPreroll(FALSE);
        ++next_;
        InterlockedIncrement(&supplied);
        return S_OK;
    }
};
class TestSource : public CSource {
  public:
    TestSource(HRESULT *hr, const AvClip &clip, unsigned total) : CSource(L"MRN AV test source", 0, SourceId, hr) {
        new TestStream(hr, this, clip, total);
    }
};
IPin *pin(IBaseFilter *filter, PIN_DIRECTION direction) {
    Com<IEnumPins> all;
    check("enum_pins", filter->EnumPins(all.out()));
    IPin *p = 0;
    while (all->Next(1, &p, 0) == S_OK) {
        PIN_DIRECTION d;
        if (SUCCEEDED(p->QueryDirection(&d)) && d == direction) {
            IPin *other = 0;
            HRESULT connected = p->ConnectedTo(&other);
            if (other)
                other->Release();
            if (connected == VFW_E_NOT_CONNECTED)
                return p;
        }
        p->Release();
    }
    throw std::runtime_error("No unconnected pin in requested direction");
}
void createFilter(const wchar_t *id, const char *stage, Com<IBaseFilter> &out) {
    CLSID cls;
    check("parse_clsid", CLSIDFromString(const_cast<wchar_t *>(id), &cls));
    log(std::string("BEGIN ") + stage);
    check(stage,
          CoCreateInstance(cls, 0, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void **)out.out()));
}
void connect(IGraphBuilder *graph, IBaseFilter *from, IBaseFilter *to, const char *stage) {
    Com<IPin> output, input;
    output.p = pin(from, PINDIR_OUTPUT);
    input.p = pin(to, PINDIR_INPUT);
    log(std::string("BEGIN ") + stage);
    // ConnectDirect forbids silently selecting another decoder or renderer.
    check(stage, graph->ConnectDirect(output.p, input.p, 0));
    AM_MEDIA_TYPE mt;
    ZeroMemory(&mt, sizeof(mt));
    HRESULT hr = output->ConnectionMediaType(&mt);
    if (SUCCEEDED(hr)) {
        wchar_t id[64];
        StringFromGUID2(mt.subtype, id, 64);
        log(std::string(stage) + " subtype=" + utf8(id));
        report.dict[std::string(stage) + "_subtype"] = Value::str(utf8(id));
        FreeMediaType(mt);
    }
}
class Audio {
    TtsPcm *pcm_;
  public:
    Audio():pcm_(new TtsPcm) {}
    void open(unsigned device,unsigned seconds,bool sync) {
        require(device==1,"MRN audio tests require device 1");
        log("TTS_OPEN: AVV0 request 0x33 payload 04; 2s settling; device 1");
        DWORD result=pcm_->open(seconds,sync);
        report.dict["tts_open_stage_result"]=Value::num(result);
        report.dict["tts_request_error"]=Value::num(pcm_->requestError);
        report.dict["tts_request_accepted"]=Value::flag(pcm_->requested!=FALSE);
        report.dict["source_connection_verified"]=Value::flag(false);
        report.dict["oem_routing_changed"]=Value::flag(pcm_->requested!=FALSE);
        report.dict["wave_device"]=Value::num(1);
        report.dict["wave_open_or_prepare_error"]=Value::num(pcm_->error);
        report.dict["audio_sample_rate"]=Value::num(48000);
        report.dict["audio_channels"]=Value::num(2);
        report.dict["audio_bits_per_sample"]=Value::num(16);
        require(result==0,"TTS/device 1 open failed; inspect stage/error fields");
    }
    void start() {
        if(!pcm_->wave)return;
        require(pcm_->start()==0,"Initial PCM queue failed");
        report.dict["audio_start_tick"]=Value::num(pcm_->startTick);
        log("AUDIO_STARTED_DEVICE_1_TEN_BUFFERS");
    }
    bool done() {
        if(!pcm_->wave)return true;
        require(pcm_->pump()==0,"PCM refill failed");
        return pcm_->done()!=FALSE;
    }
    void progress() {
        if(!pcm_->wave)return;
        bool complete=done();
        MMTIME time={0};time.wType=TIME_SAMPLES;
        MMRESULT r=waveOutGetPosition(pcm_->wave,&time,sizeof(time));
        report.dict["audio_position_result"]=Value::num(r);
        report.dict["audio_position_type"]=Value::num(time.wType);
        if(!r)report.dict["audio_position_raw"]=Value::num(time.u.sample);
        report.dict["audio_buffer_done"]=Value::flag(complete);
        report.dict["audio_buffers_submitted"]=Value::num(pcm_->submitted);
        report.dict["audio_buffers_completed"]=Value::num(pcm_->completed);
        report.dict["audio_submitted_bytes"]=Value::num(pcm_->submitted*19200);
        Value point=Value::dictionary();point.dict["elapsed_ms"]=Value::num(GetTickCount()-beginTick);
        point.dict["result"]=Value::num(r);point.dict["type"]=Value::num(time.wType);
        if(!r)point.dict["raw"]=Value::num(time.u.sample);
        point.dict["submitted"]=Value::num(pcm_->submitted);point.dict["completed"]=Value::num(pcm_->completed);
        point.dict["buffer_done"]=Value::flag(complete);
        if(!report.dict.count("audio_progress"))report.dict["audio_progress"]=Value::list();
        report.dict["audio_progress"].array.push_back(point);
        std::ostringstream text;text<<"AUDIO_PROGRESS submitted="<<pcm_->submitted<<" completed="<<pcm_->completed<<" position="<<time.u.sample<<" done="<<complete;log(text.str());
    }
    ~Audio() {
        bool clean=pcm_->close()!=FALSE;
        report.dict["audio_reset_result"]=Value::num(pcm_->resetError);
        report.dict["audio_unprepare_result"]=Value::num(pcm_->unprepareError);
        report.dict["audio_close_result"]=Value::num(pcm_->closeError);
        report.dict["tts_release_error"]=Value::num(pcm_->releaseError);
        report.dict["audio_cleanup_failed"]=Value::flag(!clean);
        log(clean?"AUDIO_CLOSED_TTS_RELEASED":"AUDIO_CLEANUP_FAILED");
        // Retain buffers if the driver failed to release them. Abort subsequent phases.
        if(clean)delete pcm_;
    }
};
LRESULT CALLBACK procedure(HWND w, UINT message, WPARAM wp, LPARAM lp) {
    if(message==WM_APP+20){displayPhase=unsigned(wp);InvalidateRect(w,0,TRUE);return 0;}
    if (message == WM_CLOSE || (message == WM_KEYUP && wp == VK_ESCAPE) ||
        (message == WM_LBUTTONUP && GetTickCount() - beginTick > 2000)) {
        log("USER_CANCEL_REQUEST");
        InterlockedExchange(&externallyCancelled,1);
        SetEvent(cancelled);
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r;
        GetClientRect(w, &r);
        FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        const WCHAR* labels[]={L"AV test preparing",L"1/3 AUDIO (2 rounds)\nBOTH: 1 beep / LEFT: 2 / RIGHT: 3",L"2/3 VIDEO\nMoving pattern and frame numbers; no test audio",L"3/3 SYNC\nWhite flash + beep each second"};
        DrawTextW(
            dc, labels[displayPhase<=3?displayPhase:0],
            -1, &r, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        EndPaint(w, &ps);
        return 0;
    }
    return DefWindowProcW(w, message, wp, lp);
}
void pump() {
    MSG m;
    while (PeekMessageW(&m, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}
DWORD WINAPI watchdog(void *) {
    if (WaitForSingleObject(finished, watchdogMilliseconds) == WAIT_TIMEOUT) {
        log("WATCHDOG_TIMEOUT: cancellation requested; driver may still be blocked; no forced "
            "thread termination");
        InterlockedExchange(&externallyCancelled,1);
        SetEvent(cancelled);
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    return 0;
}
// A short allocation per attempt; release the stream between attempts so the
// factory player can open its own stream while the user selects USB music.
void runAudioProbe(unsigned device, unsigned duration) {
    Value baseline = report, attempts = Value::list();
    DWORD start = GetTickCount();
    unsigned completed = 0;
    bool cleanupFailed = false;
    while (!stopped() && GetTickCount() - start + 22000 <= duration * 1000) {
        DWORD cycle = GetTickCount();
        report = baseline;
        report.dict["attempt"] = Value::num(attempts.array.size() + 1);
        report.dict["attempt_start_ms"] = Value::num(cycle - beginTick);
        log("AUDIO_PROBE_ATTEMPT_BEGIN");
        bool done = false;
        try {
            Audio audio;
            audio.open(device, 20, false);
            audio.start();
            DWORD playing = GetTickCount(), last = playing;
            log("AUDIO_PROBE_PLAYING: 20 seconds, BOTH x1 / LEFT x2 / RIGHT x3; two rounds");
            while (!stopped() && GetTickCount() - playing < 22000) {
                if (audioCancelTest && GetTickCount() - playing >= 300)
                    SetEvent(cancelled);
                if (GetTickCount() - last >= 1000) {
                    audio.progress();
                    last = GetTickCount();
                }
                if (audio.done()) break;
                Sleep(10);
            }
            audio.progress();
            done = audio.done();
            report.dict["attempt_playback_ms"] = Value::num(GetTickCount() - playing);
            if (!stopped() && !done)
                report.dict["attempt_error"] = Value::str("Audio buffer did not complete");
        } catch (const std::exception &e) {
            report.dict["attempt_error"] = Value::str(e.what());
            log(std::string("AUDIO_PROBE_ATTEMPT_ERROR ") + e.what());
        }
        cleanupFailed = report.dict.count("audio_cleanup_failed") && report.at("audio_cleanup_failed").boolean;
        if (done && !cleanupFailed) ++completed;
        report.dict["attempt_end_ms"] = Value::num(GetTickCount() - beginTick);
        attempts.array.push_back(report);
        log("AUDIO_PROBE_ATTEMPT_END");
        if (cleanupFailed) break;
        // 24-second cycle, including playback and stream release. Bound retries.
        while (!stopped() && GetTickCount() - cycle < 24000 &&
               GetTickCount() - start < duration * 1000) Sleep(50);
    }
    while (!cleanupFailed && !stopped() && GetTickCount() - start < duration * 1000)
        Sleep(50);
    report = baseline;
    report.dict["audio_attempts"] = attempts;
    report.dict["audio_completed_attempts"] = Value::num(completed);
    report.dict["playback_elapsed_ms"] = Value::num(GetTickCount() - start);
    report.dict["cancelled"] = Value::flag(stopped());
    if (cleanupFailed) report.dict["audio_cleanup_failed"] = Value::flag(true);
    require(!cleanupFailed, "Audio cleanup failed; further attempts stopped");
    require(stopped() || completed != 0, "No audio probe buffer completed; inspect audio_attempts");
}
bool optionFlag(const Value &config,const char *key) {
    if(!config.dict.count(key))return false;
    require(config.at(key).type==Value::Boolean,"Invalid boolean option");
    return config.at(key).boolean;
}
void run(const Value &config, bool selftest) {
    std::string mode = selftest ? "video" : config.at("mode").string;
    require(mode == "video" || mode == "audio" || mode == "sync", "Invalid test mode");
    report.dict["mode"] = Value::str(mode);
    report.dict["selftest_rgb_only"] = Value::flag(selftest);
    log("MODE " + mode);
    require(!optionFlag(config,"usb_route_test")&&!optionFlag(config,"esai_unmute_phase"),
            "Obsolete device-0 experiment disabled; use device-1 TTS audio/sync");
    unsigned requestedSeconds=0;
    if(config.dict.count("playback_seconds")) {
        const Value &v=config.at("playback_seconds");
        require(mode=="video" && v.type==Value::Integer && v.integer>=1 && v.integer<=60,
                "Invalid video duration (1-60 seconds; video mode only)");
        requestedSeconds=unsigned(v.integer);
    }
    enumerateAudio();
    if (config.dict.count("audio_probe_seconds")) {
        const Value &v = config.at("audio_probe_seconds");
        require(mode == "audio" && v.type == Value::Integer && !v.negative && v.integer >= 24 && v.integer <= 180,
                "Invalid audio probe duration (24-180 seconds; audio mode only)");
        require(!requestedSeconds, "Audio probe cannot use video duration");
        const Value &device = config.at("wave_device");
        require(device.type == Value::Integer && !device.negative && device.integer == 1 && device.integer < waveOutGetNumDevs(),
                "Invalid wave output device");
        report.dict["wave_device"] = device;
        report.dict["audio_probe_seconds"] = v;
        report.dict["audio_cycle_seconds"] = Value::num(24);
        report.dict["audio_tone_seconds"] = Value::num(20);
        runAudioProbe(unsigned(device.integer), unsigned(v.integer));
        return;
    }
    std::auto_ptr<AvClip> clip;
    Com<IGraphBuilder> graph;
    Com<IMediaControl> control;
    Com<IMediaEvent> events;
    Com<IVideoWindow> view;
    Com<IBaseFilter> source, decoder, renderer;
    Audio audio;
    unsigned seconds = 20;
    if(mode=="audio") {
        report.dict["audio_pattern"]=Value::str("2s silence; BOTH x1 / LEFT x2 / RIGHT x3; two 9s rounds");
        report.dict["audio_expected_pulses"]=Value::num(12);
        report.dict["playback_seconds"]=Value::num(20);
        log("AUDIO_PATTERN: 2s silence; round1 BOTH@2 LEFT@5,5.5 RIGHT@8,8.5,9; round2 BOTH@11 LEFT@14,14.5 RIGHT@17,17.5,18; times relative to PCM start");
    }
    if (mode != "audio") {
        clip.reset(new AvClip(readFile(folder + (selftest ? L"\\rgb.avt" : L"\\video.avt"))));
        require(clip->codec == (selftest ? 2U : 1U), "Unexpected fixture codec");
        seconds = (clip->frames.size() + clip->fps - 1) / clip->fps;
        unsigned totalFrames=unsigned(clip->frames.size());
        if(requestedSeconds){seconds=requestedSeconds;totalFrames=seconds*clip->fps;}
        report.dict["playback_seconds"]=Value::num(seconds);
        report.dict["planned_video_samples"]=Value::num(totalFrames);
        report.dict["fixture_frames"] = Value::num(clip->frames.size());
        report.dict["fixture_width"] = Value::num(clip->width);
        report.dict["fixture_height"] = Value::num(clip->height);
        check("create_graph", CoCreateInstance(CLSID_FilterGraph, 0, CLSCTX_INPROC_SERVER,
                                               IID_IGraphBuilder, (void **)graph.out()));
        check("media_control", graph->QueryInterface(IID_IMediaControl, (void **)control.out()));
        check("media_events", graph->QueryInterface(IID_IMediaEvent, (void **)events.out()));
        HRESULT hr = S_OK;
        TestSource *raw = new TestSource(&hr, *clip, totalFrames);
        raw->AddRef();
        source.p = raw;
        check("create_source", hr);
        check("add_source", graph->AddFilter(source.p, L"Finite memory test source"));
        createFilter(RendererId, "create_renderer", renderer);
        check("add_renderer", graph->AddFilter(renderer.p, L"System video renderer"));
        if (!selftest) {
            std::wstring id = config.dict.count("decoder_clsid")
                                  ? wide(config.at("decoder_clsid").string)
                                  : DecoderId;
            report.dict["decoder_clsid"] = Value::str(utf8(id));
            createFilter(id.c_str(), "create_decoder", decoder);
            check("add_decoder", graph->AddFilter(decoder.p, L"Selected factory decoder"));
            connect(graph.p, source.p, decoder.p, "connect_source_decoder");
            connect(graph.p, decoder.p, renderer.p, "connect_decoder_renderer");
        } else
            connect(graph.p, source.p, renderer.p, "connect_rgb_renderer");
        check("video_window", graph->QueryInterface(IID_IVideoWindow, (void **)view.out()));
        check("video_owner", view->put_Owner((OAHWND)window));
        check("video_style", view->put_WindowStyle(WS_CHILD | WS_CLIPSIBLINGS));
        check("video_message_drain", view->put_MessageDrain((OAHWND)window));
        RECT r;
        GetClientRect(window, &r);
        check("video_position", view->SetWindowPosition(0, 0, r.right, r.bottom));
        check("video_visible", view->put_Visible(OATRUE));
    }
    if (mode != "video") {
        unsigned device = 1;
        if (config.dict.count("wave_device")) {
            const Value &v = config.at("wave_device");
            require(v.type == Value::Integer && !v.negative && v.integer == 1 && v.integer < waveOutGetNumDevs(),
                    "Invalid wave output device");
            device = unsigned(v.integer);
        }
        report.dict["wave_device"] = Value::num(device);
        report.dict["wave_devices"] = Value::num(waveOutGetNumDevs());
        audio.open(device, seconds, mode == "sync");
    }
    DWORD start = GetTickCount();
    bool complete = mode == "audio";
    try {
        if (control.p) {
            log("BEGIN graph_run");
            report.dict["graph_run_begin_tick"]=Value::num(GetTickCount());
            check("graph_run", control->Run());
            report.dict["graph_run_return_tick"]=Value::num(GetTickCount());
        }
        audio.start();
        log("PLAYBACK_STARTED");
        DWORD last = start;
        while (!stopped() && GetTickCount() - start < (seconds + 2) * 1000) {
            pump();
            if (selftest && config.dict.count("cancel_test") && GetTickCount() - start >= 300) {
                log("INJECTED_CANCEL");
                SetEvent(cancelled);
            }
            if (events.p) {
                long code;
                LONG_PTR a, b;
                while (events->GetEvent(&code, &a, &b, 0) == S_OK) {
                    events->FreeEventParams(code, a, b);
                    if (code == EC_COMPLETE) {
                        complete = true;
                        log("GRAPH_COMPLETE");
                    }
                    if (code == EC_ERRORABORT) {
                        check("graph_error", HRESULT(a));
                        throw std::runtime_error("Graph aborted");
                    }
                }
            }
            if (GetTickCount() - last >= 1000) {
                audio.progress();
                std::ostringstream s;
                s << "samples_supplied=" << supplied;
                log(s.str());
                last = GetTickCount();
            }
            bool audioComplete=audio.done(); // Pump every iteration, including while video is running.
            if (complete && audioComplete && GetTickCount() - start >= seconds * 1000)
                break;
            Sleep(10);
        }
        audio.progress();
        report.dict["graph_complete"] = Value::flag(complete && mode != "audio");
        report.dict["cancelled"] = Value::flag(stopped());
        report.dict["samples_supplied"] = Value::num(supplied);
        report.dict["playback_elapsed_ms"] = Value::num(GetTickCount() - start);
        if (renderer.p) {
            Com<IQualProp> quality;
            HRESULT qr = renderer->QueryInterface(IID_IQualProp, (void **)quality.out());
            report.dict["renderer_statistics_result"] = Value::str(hexHr(qr));
            if (SUCCEEDED(qr)) {
                int drawn = 0, dropped = 0;
                if (SUCCEEDED(quality->get_FramesDrawn(&drawn)))
                    report.dict["renderer_frames_drawn"] = Value::num(drawn);
                if (SUCCEEDED(quality->get_FramesDroppedInRenderer(&dropped)))
                    report.dict["renderer_frames_dropped"] = Value::num(dropped);
            }
        }
        require(stopped() || complete, "Graph did not complete within the finite test window");
        if (!stopped() && mode != "video")
            require(report.at("audio_buffer_done").boolean, "Audio buffer did not complete");
    } catch (...) {
        SetEvent(cancelled);
        if (control.p) {
            log("BEGIN graph_stop_after_error");
            log("graph_stop_after_error " + hexHr(control->Stop()));
        }
        if (view.p) {
            view->put_Visible(OAFALSE);
            view->put_MessageDrain(0);
            view->put_Owner(0);
        }
        throw;
    }
    if (control.p && stopped() && !complete) {
        log("BEGIN cancel_drain");
        DWORD deadline = GetTickCount();
        // FillBuffer observes cancellation and emits EOS. Let the finite queue
        // drain before Stop; the CE renderer otherwise blocks during an active Receive.
        while (!complete && GetTickCount() - deadline < 2000) {
            pump();
            long event;
            LONG_PTR a, b;
            while (events->GetEvent(&event, &a, &b, 0) == S_OK) {
                events->FreeEventParams(event, a, b);
                if (event == EC_COMPLETE)
                    complete = true;
            }
            Sleep(10);
        }
        report.dict["cancel_drain_complete"] = Value::flag(complete);
        log(complete ? "CANCEL_DRAIN_COMPLETE" : "CANCEL_DRAIN_TIMEOUT");
    }
    if (control.p) {
        log("BEGIN graph_stop");
        check("graph_stop", control->Stop());
    }
    if (view.p) {
        view->put_Visible(OAFALSE);
        view->put_MessageDrain(0);
        view->put_Owner(0);
    }
    log("MEDIA_RELEASE_BEGIN");
    // Graph owns source while it is registered. Release all graph references
    // before the fixture referenced by TestStream is destroyed.
    if (graph.p) {
        graph->RemoveFilter(source.p);
        if (decoder.p)
            graph->RemoveFilter(decoder.p);
        graph->RemoveFilter(renderer.p);
    }
    if (source.p) {
        source.p->Release();
        source.p = 0;
    }
}
struct Work {
    Value config;
    bool selftest;
    int code;
    Work(const Value &v, bool s) : config(v), selftest(s), code(0) {}
};
DWORD WINAPI mediaWorker(void *context) {
    Work &work = *(Work *)context;
    HRESULT initialized = CoInitializeEx(0, COINIT_MULTITHREADED);
    try {
        check("worker_com_initialize", initialized);
        bool full=!work.selftest&&work.config.at("mode").string=="full";
        if(full) {
            Value baseline=report,phases=Value::list();
            const char* modes[]={"audio","video","sync"};
            for(unsigned phase=0;phase<3&&!stopped();++phase) {
                report=baseline;supplied=0;
                Value config=work.config;config.dict["mode"]=Value::str(modes[phase]);
                report.dict["phase"]=Value::str(modes[phase]);
                PostMessageW(window,WM_APP+20,phase+1,0);
                log(std::string("PHASE_BEGIN ")+modes[phase]);
                try {run(config,false);require(!optionFlag(report,"audio_cleanup_failed"),"Audio cleanup failed");
                    report.dict["api_test_completed"]=Value::flag(!stopped());}
                catch(const std::exception &e){work.code=1;report.dict["error"]=Value::str(e.what());log(std::string("PHASE_ERROR ")+e.what());
                    // run() cancelled its own graph for teardown. Only clear that
                    // internal cancellation after scope cleanup; never clear user/watchdog cancellation.
                    if(!externallyCancelled)ResetEvent(cancelled);}
                report.dict["samples_supplied"]=Value::num(supplied);
                bool unsafe=optionFlag(report,"audio_cleanup_failed");
                phases.array.push_back(report);report=baseline;report.dict["phases"]=phases;
                writeFile(reportPath,xmlPlist(report)); // checkpoint each completed phase
                log(std::string("PHASE_END ")+modes[phase]);
                if(unsafe)break;
                if(phase<2)for(unsigned gap=0;gap<20&&!stopped();++gap)Sleep(100);
            }
            report.dict["api_test_completed"]=Value::flag(!work.code&&!stopped()&&phases.array.size()==3);
            report.dict["cancelled"]=Value::flag(stopped());
            if(stopped()||phases.array.size()!=3)work.code=1;
        } else {
            run(work.config, work.selftest);
            require(!optionFlag(report,"audio_cleanup_failed"), "Audio cleanup failed");
            report.dict["api_test_completed"] = Value::flag(!stopped());
        }
    } catch (const std::exception &e) {
        log(std::string("ERROR ") + e.what());
        report.dict["error"] = Value::str(e.what());
        work.code = 1;
    }
    log("MEDIA_SCOPE_ENDED");
    if (SUCCEEDED(initialized))
        CoUninitialize();
    return work.code;
}
} // namespace
int WINAPI AvTestMain(HINSTANCE instance, HINSTANCE, LPWSTR command, int) {
    g_hInst = instance;
    beginTick = GetTickCount();
    InitializeCriticalSection(&logLock);
    folder = executableDirectory();
    for (unsigned i = 1; i <= 9999; ++i) {
        wchar_t name[64];
        swprintf(name, L"\\avtest-%04u.log", i);
        logFile = CreateFileW((folder + name).c_str(), GENERIC_WRITE, FILE_SHARE_READ, 0,
                              CREATE_NEW, 0, 0);
        if (logFile != INVALID_HANDLE_VALUE) {
            swprintf(name, L"\\avtest-%04u.plist", i);
            reportPath = folder + name;
            break;
        }
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            break;
    }
    if (logFile == INVALID_HANDLE_VALUE) {
        DeleteCriticalSection(&logLock);
        return 2;
    }
    cancelled = CreateEventW(0, TRUE, FALSE, 0);
    finished = CreateEventW(0, TRUE, FALSE, 0);
    HWND previous = 0;
    Value foregroundHistory=Value::list();
    HANDLE guard = 0;
    HRESULT com = CoInitializeEx(0, COINIT_MULTITHREADED);
    int code = 0;
    report.dict["version"] = Value::str("av-test-v8-channel-pattern");
    report.dict["watchdog_seconds_after_window"]=Value::num(90);
    report.dict["physical_output_verified"] = Value::flag(false);
    report.dict["hardware_decode_verified"] = Value::flag(false);
    report.dict["oem_routing_changed"] = Value::flag(false);
    try {
        require(cancelled && finished, "Cannot create test events");
        check("com_initialize", com);
        bool longTest=std::wstring(command)==L"--selftest-long" || std::wstring(command)==L"--selftest-long-cancel";
        bool cancelTest = std::wstring(command) == L"--selftest-cancel" || std::wstring(command)==L"--selftest-long-cancel";
        bool selftest = std::wstring(command) == L"--selftest" || cancelTest || longTest;
        Value config = Value::dictionary();
        if (!selftest) {
            Bytes configFile = readFile(folder + L"\\test.plist", 65536);
            if (configFile.size() >= 3 && configFile[0] == 0xef && configFile[1] == 0xbb &&
                configFile[2] == 0xbf)
                configFile.erase(configFile.begin(), configFile.begin() + 3);
            config = parsePlist(configFile);
            require(config.type == Value::Dict, "Expected test configuration dictionary");
        }
        if (cancelTest)
            config.dict["cancel_test"] = Value::flag(true);
        if(longTest)config.dict["playback_seconds"]=Value::num(60);
        audioCancelTest = std::wstring(command) == L"--selftest-audio-probe-cancel";
        bool background = false;
        if (config.dict.count("background")) {
            const Value &v = config.at("background");
            require(v.type == Value::Boolean, "Invalid background flag");
            background = v.boolean;
            require(!background || (!selftest && config.at("mode").string == "audio"),
                    "Background mode is audio only");
        }
        if (config.dict.count("audio_probe_seconds")) {
            const Value &v = config.at("audio_probe_seconds");
            require(!selftest && config.at("mode").string == "audio" &&
                    v.type == Value::Integer && !v.negative && v.integer >= 24 && v.integer <= 180,
                    "Invalid audio probe duration (24-180 seconds; audio mode only)");
            watchdogMilliseconds = DWORD(v.integer + 30) * 1000;
        }
        require(!optionFlag(config,"usb_route_test")&&!optionFlag(config,"esai_unmute_phase"),
                "Device-0 route experiment removed; select device-1 TTS tests");
        report.dict["background"] = Value::flag(background);
        report.dict["watchdog_seconds_after_window"] = Value::num(watchdogMilliseconds / 1000);
        unsigned delay=0;
        if(config.dict.count("start_delay_seconds")) {
            const Value &v=config.at("start_delay_seconds");
            require(v.type==Value::Integer && v.integer<=60,"Invalid start delay (0-60 seconds)");
            delay=unsigned(v.integer);
        }
        report.dict["start_delay_seconds"]=Value::num(delay);
        observeForeground(foregroundHistory,"before_delay");
        log("WAIT_BEFORE_WINDOW: no audio or video resources requested yet");
        DWORD delayStart=GetTickCount();
        while(GetTickCount()-delayStart<delay*1000){observeForeground(foregroundHistory,"delay");Sleep(100);}
        // Restore the UI that exists after boot, not the often-null boot foreground.
        if (!background) previous=GetForegroundWindow();
        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = procedure;
        wc.hInstance = instance;
        wc.lpszClassName = L"MRNFiniteAVTest";
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        require(RegisterClassW(&wc) != 0, "RegisterClass failed");
        window = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"MRN audio/video test",
                                 WS_POPUP, 0, 0, GetSystemMetrics(SM_CXSCREEN),
                                 GetSystemMetrics(SM_CYSCREEN), 0, 0, instance, 0);
        require(window != 0, "CreateWindow failed");
        if (!background) {
            ShowWindow(window, SW_SHOW);
            SetForegroundWindow(window);
            UpdateWindow(window);
        }
        observeForeground(foregroundHistory, background ? "background_audio" : "window_shown");
        guard = CreateThread(0, 0, watchdog, 0, 0, 0);
        require(guard != 0, "Watchdog creation failed");
        log("TEST_BEGIN; audio routes only; process startup is externally managed");
        Work work(config, selftest);
        HANDLE worker = CreateThread(0, 0, mediaWorker, &work, 0, 0);
        require(worker != 0, "Media worker creation failed");
        // CE's renderer can send synchronous window messages during Stop.
        // Keep the owner window pumping while COM/media cleanup runs in its MTA.
        DWORD lastForeground=GetTickCount();
        while (WaitForSingleObject(worker, 10) == WAIT_TIMEOUT) {
            pump();
            if(GetTickCount()-lastForeground>=250){observeForeground(foregroundHistory,"playback");lastForeground=GetTickCount();}
        }
        CloseHandle(worker);
        code = work.code;
    } catch (const std::exception &e) {
        log(std::string("ERROR ") + e.what());
        report.dict["error"] = Value::str(e.what());
        code = 1;
    }
    if (window) {
        DestroyWindow(window);
        window = 0;
    }
    if (previous && IsWindow(previous))
        report.dict["foreground_restore_request"] = Value::flag(SetForegroundWindow(previous) != 0);
    observeForeground(foregroundHistory,"after_release");
    report.dict["foreground_history"]=foregroundHistory;
    if (SUCCEEDED(com))
        CoUninitialize();
    report.dict["samples_supplied"] = Value::num(supplied);
    report.dict["exit_code"] = Value::num(code);
    try {
        writeFile(reportPath, xmlPlist(report));
    } catch (...) {
        code = 2;
        log("REPORT_WRITE_FAILED");
    }
    log("EXIT_READY");
    if (finished)
        SetEvent(finished);
    if (guard) {
        WaitForSingleObject(guard, INFINITE);
        CloseHandle(guard);
    }
    if (cancelled)
        CloseHandle(cancelled);
    if (finished)
        CloseHandle(finished);
    CloseHandle(logFile);
    DeleteCriticalSection(&logLock);
    return code;
}
