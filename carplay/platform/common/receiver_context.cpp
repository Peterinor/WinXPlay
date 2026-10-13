#include "receiver_context.h"
#include "diagnostics.h"
#ifdef CARPLAY_CE_AAC
#include "../wince/audio_aac.h"
#endif
#if WINXPLAY_ENABLE_LOGS
#include "report_worker.h"
#endif
#ifdef CARPLAY_CE_DSHOW
#include "../wince/video_dshow.h"
#endif
#ifdef CARPLAY_HAS_MF
#include "../windows/mf_video_adapter.h"
#endif
#include "native_crypto.h"
#include "local_identity.h"
#include "../../core/pairing.h"
#include "legacy_media.h"
#include "../../devices/profile.h"
#include <memory>
#include <algorithm>
#include <sstream>
namespace cp {
namespace {
sockaddr_in6 nativeAddress(const PeerAddress &p) {
    require(p.address.size() == 16 && p.port <= 65535, "Invalid IPv6 peer");
    sockaddr_in6 a;
    ZeroMemory(&a, sizeof(a));
    a.sin6_family = AF_INET6;
    a.sin6_port = htons(U16(p.port));
    a.sin6_scope_id = p.scope;
    memcpy(&a.sin6_addr, &p.address[0], 16);
    return a;
}
PeerAddress peerAddress(const sockaddr_in6 &a) {
    PeerAddress p;
    memcpy(&p.address[0], &a.sin6_addr, 16);
    p.scope = a.sin6_scope_id;
    p.port = ntohs(a.sin6_port);
    return p;
}
class SocketTransport : public Transport {
  public:
    Connection bind(unsigned port, bool tcp) {
        SOCKET s =
            socket(AF_INET6, tcp ? SOCK_STREAM : SOCK_DGRAM, tcp ? IPPROTO_TCP : IPPROTO_UDP);
        require(s != INVALID_SOCKET, "Socket creation failed");
        DWORD zero = 0, one = 1, timeout = 2000;
#ifdef IPV6_V6ONLY
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&zero, sizeof(zero));
#endif
        setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *)&one, sizeof(one));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
        PeerAddress p;
        p.port = port;
        sockaddr_in6 a = nativeAddress(p);
        if (::bind(s, (sockaddr *)&a, sizeof(a)) != 0 || (tcp && listen(s, 2) != 0)) {
            closesocket(s);
            throw std::runtime_error("Cannot bind AirPlay port");
        }
        return Connection(s);
    }
    unsigned localPort(Connection c) {
        sockaddr_in6 a;
        int n = sizeof(a);
        require(getsockname(SOCKET(c), (sockaddr *)&a, &n) == 0, "Socket address failed");
        return ntohs(a.sin6_port);
    }
    Connection accept(Connection c, PeerAddress &peer) {
        sockaddr_in6 a;
        int n = sizeof(a);
        SOCKET s = ::accept(SOCKET(c), (sockaddr *)&a, &n);
        require(s != INVALID_SOCKET, "AirPlay accept failed");
        DWORD timeout = 2000, one = 1;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(one));
        peer = peerAddress(a);
        return Connection(s);
    }
    void close(Connection c) {
        if (c != InvalidConnection)
            closesocket(SOCKET(c));
    }
    void sendAll(Connection c, const Bytes &b) {
        for (size_t at = 0; at < b.size();) {
            int n = send(SOCKET(c), (const char *)&b[at], int(b.size() - at), 0);
            require(n > 0, "AirPlay socket send failed");
            at += n;
        }
    }
    bool receive(Connection c, Bytes &b) {
        b.resize(65536);
        int n = recv(SOCKET(c), (char *)&b[0], int(b.size()), 0);
        require(n >= 0, "AirPlay stream receive failed");
        b.resize(n);
        return n != 0;
    }
    bool receiveDatagram(Connection c, Bytes &b, PeerAddress &peer) {
        b.resize(65536);
        sockaddr_in6 a;
        int size = sizeof(a);
        int n = recvfrom(SOCKET(c), (char *)&b[0], int(b.size()), 0, (sockaddr *)&a, &size);
        if (n <= 0) {
            b.clear();
            return false;
        }
        b.resize(n);
        peer = peerAddress(a);
        return true;
    }
    bool sendDatagram(Connection c, const Bytes &b, const PeerAddress &p) {
        if (b.empty())
            return false;
        sockaddr_in6 a = nativeAddress(p);
        return sendto(SOCKET(c), (const char *)&b[0], int(b.size()), 0, (sockaddr *)&a,
                      sizeof(a)) == int(b.size());
    }
    std::vector<Connection> poll(const std::vector<Connection> &sockets, unsigned ms) {
        require(sockets.size() <= FD_SETSIZE, "Socket poll capacity exceeded");
        fd_set reads;
        FD_ZERO(&reads);
        for (size_t i = 0; i < sockets.size(); ++i)
            FD_SET(SOCKET(sockets[i]), &reads);
        timeval timeout = {long(ms / 1000), long(ms % 1000 * 1000)};
        if (sockets.empty()) {
            Sleep(ms);
            return std::vector<Connection>();
        }
        require(select(0, &reads, 0, 0, &timeout) != SOCKET_ERROR, "AirPlay socket poll failed");
        std::vector<Connection> ready;
        for (size_t i = 0; i < sockets.size(); ++i)
            if (FD_ISSET(SOCKET(sockets[i]), &reads))
                ready.push_back(sockets[i]);
        return ready;
    }
};
// Keep one compact line per stage/window; no media contents enter the trace.
std::string metricFields(const Value &v) {
    std::ostringstream s;
    for(std::map<std::string,Value>::const_iterator i=v.dict.begin();i!=v.dict.end();++i){
        if(i->second.type==Value::Integer)s<<i->first<<"="<<i->second.integer<<" ";
        else if(i->second.type==Value::Boolean)s<<i->first<<"="<<i->second.boolean<<" ";
        else if(i->second.type==Value::String)s<<i->first<<"="<<i->second.string<<" ";
    }
    return s.str();
}
class NativeRuntime : public SessionRuntime {
    HANDLE thread_, stop_;
    CRITICAL_SECTION mutex_;
    void (*entry_)(void *);
    void *context_;
#if WINXPLAY_ENABLE_LOGS
    std::wstring reportPath_;
    ReportWorker reports_;
    static void writeReport(const Value &report,void *path) {
        if(report.dict.count("media_metrics")){
            const Value &m=report.at("media_metrics");
            std::ostringstream key;key<<"session="<<m.at("start_tick_ms").integer<<" tick="<<m.at("tick_ms").integer<<" ";
            key<<"reports_dropped="<<report.at("diagnostic_reports_dropped").integer
               <<" reports_failed="<<report.at("diagnostic_reports_failed").integer<<" ";
            diagnosticEvent("MEDIA_RX",key.str()+metricFields(m));
            if(m.dict.count("video"))diagnosticEvent("MEDIA_VIDEO",key.str()+metricFields(m.at("video")));
            if(m.dict.count("audio"))for(size_t i=0;i<m.at("audio").array.size();++i){
                std::ostringstream stream;stream<<key.str()<<"stream="<<i<<" ";
                diagnosticEvent("MEDIA_AUDIO",stream.str()+metricFields(m.at("audio").array[i]));
            }
        }
        writeDiagnosticFile(*(std::wstring*)path,xmlPlist(report));
    }
#endif
    static DWORD WINAPI run(void *p) {
#ifdef UNDER_CE
        __try {
#endif
        NativeRuntime &r = *(NativeRuntime *)p;
        r.entry_(r.context_);
        return 0;
#ifdef UNDER_CE
        } __except(diagnosticException(GetExceptionInformation(),L"receiver")){return 1;}
#endif
    }

  public:
    NativeRuntime() : thread_(0), stop_(0), entry_(0), context_(0)
#if WINXPLAY_ENABLE_LOGS
        ,reportPath_(environment(L"CARPLAY_REPORT",(executableDirectory()+L"/streaming-diagnostics.plist").c_str())),
        reports_(writeReport,&reportPath_)
#endif
    {
        stop_ = CreateEventW(0, TRUE, FALSE, 0);
        require(stop_ != 0, "Receiver cancellation failed");
        InitializeCriticalSection(&mutex_);
    }
    ~NativeRuntime() {
        cancel();
        join();
        if (thread_)
            CloseHandle(thread_);
        CloseHandle(stop_);
        DeleteCriticalSection(&mutex_);
    }
    void start(void (*entry)(void *), void *context) {
        require(!thread_, "Receiver already running");
        entry_ = entry;
        context_ = context;
        thread_ = CreateThread(0, 0, run, this, 0, 0);
        require(thread_ != 0, "Receiver thread creation failed");
    }
    void cancel() {
        SetEvent(stop_);
    }
    bool cancelled() const {
        return WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0;
    }
    void join() {
        if (!thread_)
            return;
#ifndef UNDER_CE
        CancelSynchronousIo(thread_);
#endif
        WaitForSingleObject(thread_, INFINITE);
#if WINXPLAY_ENABLE_LOGS
        reports_.flush();
#endif
    }
    void lock() {
        EnterCriticalSection(&mutex_);
    }
    void unlock() {
        LeaveCriticalSection(&mutex_);
    }
    U32 milliseconds() const {
        return GetTickCount();
    }
    U64 ntpTime() const {
        FILETIME f;
#ifdef UNDER_CE
        SYSTEMTIME now;
        GetSystemTime(&now);
        SystemTimeToFileTime(&now, &f);
#else
        GetSystemTimeAsFileTime(&f);
#endif
        U64 ticks = ((U64(f.dwHighDateTime) << 32) | f.dwLowDateTime) - 94354848000000000ULL;
        return ((ticks / 10000000) << 32) | (((ticks % 10000000) << 32) / 10000000);
    }
    void saveReport(const Value &report) {
#if WINXPLAY_ENABLE_LOGS
        reports_.submit(report);
#endif
    }
};
class ProtectedStore : public PairingStore {
    std::wstring path_;

  public:
    ProtectedStore()
        : path_(environment(L"CARPLAY_PAIRING_STORE",
                            (executableDirectory() + L"/receiver-identity.dat").c_str())) {}
    bool exists() const {
        if (GetFileAttributesW(path_.c_str()) != INVALID_FILE_ATTRIBUTES)
            return true;
#ifdef WINXPLAY_WOLFSSL
        return GetFileAttributesW((path_ + L".bak").c_str()) != INVALID_FILE_ATTRIBUTES;
#else
        return false;
#endif
    }
    Bytes read() {
        return protectedRead(path_);
    }
    void write(const Bytes &b) {
        protectedWrite(path_, b);
    }
};
#ifndef CARPLAY_HAS_MF
class LegacyVideo : public VideoPipeline {
    LiveVideo video_;

  public:
    LegacyVideo(VideoCallback cb, void *context, unsigned w, unsigned h)
        : video_(cb, context, 0, 2, w, h) {}
    void submit(const Bytes &b, U64) {
        video_.feed(b);
    }
    void finish() {
        video_.finish();
    }
    unsigned frames() const {
        return video_.frames();
    }
};
#endif
class LegacyAudio : public AudioOutput {
    LiveAudio audio_;

  public:
    LegacyAudio(unsigned r, unsigned c, bool aac) : audio_(r, c, aac) {}
    void receive(const Bytes &b) {
        audio_.receive(b);
    }
    unsigned playedBytes() const {
        return audio_.playedBytes();
    }
    void pause(bool p) {
        audio_.pause(p);
    }
#if defined(UNDER_CE) || defined(WINXPLAY_AUDIO_RECOVERY_HOST_TEST)
    Value metrics() const { return audio_.metrics(); }
#endif
};
class LegacyInput : public AudioInput {
    LiveMicrophone* input_;
    unsigned rate_,channels_,samples_,failures_;
    DWORD attemptedAt_,reportedAt_;
    bool attempted_;
    U64 captured_;
    std::string error_;
    void report(const char* stage,bool force=false) {
        DWORD now=GetTickCount();
        if(!force && DWORD(now-reportedAt_)<1000)return;
        reportedAt_=now;
        try {
            Value v=Value::dictionary();v.dict["stage"]=Value::str(stage);
            v.dict["sample_rate"]=Value::num(rate_);v.dict["channels"]=Value::num(channels_);
            v.dict["device_open"]=Value::flag(input_!=0);v.dict["captured_pcm_bytes"]=Value::num(captured_);
            v.dict["device_failures"]=Value::num(failures_);v.dict["last_device_error"]=Value::str(error_);
            v.dict["retry_interval_ms"]=Value::num(2000);v.dict["physical_microphone_verified"]=Value::flag(false);
            writeDiagnosticFile(executableDirectory()+L"/audio-input.plist",xmlPlist(v));
        }catch(...){}
    }
  public:
    LegacyInput(unsigned r,unsigned c,unsigned n):input_(0),rate_(r),channels_(c),samples_(n),
        failures_(0),attemptedAt_(0),reportedAt_(0),attempted_(false),captured_(0) {
        require(r>=8000 && r<=48000 && c==1 && n>=r/200 && n<=r*60/1000,"Invalid microphone format");
        report("requested",true);
    }
    ~LegacyInput(){delete input_;input_=0;report("closed",true);}
    std::vector<Bytes> poll() {
        if(!input_ && attempted_ && DWORD(GetTickCount()-attemptedAt_)<2000)return std::vector<Bytes>();
        try {
            if(!input_){attempted_=true;attemptedAt_=GetTickCount();input_=new LiveMicrophone(rate_,channels_,samples_);report("device_open",true);}
            std::vector<Bytes> packets=input_->poll();
            for(size_t i=0;i<packets.size();++i)captured_+=packets[i].size();
            report("capturing");return packets;
        }catch(const std::exception& e){
            error_=e.what();++failures_;delete input_;input_=0;
            attempted_=true;attemptedAt_=GetTickCount();report("retry_pending",true);
            return std::vector<Bytes>();
        }
    }
};
// An explicitly disabled output never opens hardware or reports discarded
// bytes as played. The current MRN profile enables output independently of input.
class DiscardAudio : public AudioOutput {
  public:
    void receive(const Bytes &) {}
    unsigned playedBytes() const { return 0; }
    void pause(bool) {}
};
class NoMicrophone : public AudioInput {
  public:
    std::vector<Bytes> poll() { return std::vector<Bytes>(); }
};
class NativeMedia : public MediaFactory {
    VideoCallback callback_;
    void *context_;
    HWND window_;

  public:
    NativeMedia(VideoCallback cb, void *context, HWND window) : callback_(cb), context_(context), window_(window) {}
    MediaCapabilities capabilities() const {
        return applicationMediaCapabilities();
    }
    VideoPipeline *video(unsigned w, unsigned h) {
#ifdef CARPLAY_CE_DSHOW
        return createCeSystemVideo(window_,w,h,capabilities().maxFPS);
#elif defined(CARPLAY_HAS_MF)
        Value selected=Value::dictionary();
        selected.dict["backend"]=Value::str("media_foundation_d3d11");
        selected.dict["stage"]=Value::str("starting");
        writeDiagnosticFile(executableDirectory()+L"/video-backend.plist",xmlPlist(selected));
        return createSystemVideo(window_,callback_,context_,w,h,capabilities().maxFPS);
#else
        return new LegacyVideo(callback_, context_, w, h);
#endif
    }
    AudioOutput *audio(unsigned r, unsigned c, bool aac) {
        require(!aac || capabilities().aac, "AAC is not supported by this backend");
        if(!capabilities().audioOutput)return new DiscardAudio;
        return new LegacyAudio(r, c, aac);
    }
    AudioInput *microphone(unsigned r, unsigned c, unsigned n) {
        if(!capabilities().microphone)return new NoMicrophone;
        return new LegacyInput(r, c, n);
    }
};
AuthProvider *loadIdentity() {
    std::wstring path = environment(L"CARPLAY_IDENTITY", L"");
    return path.empty() ? 0
                        : new LocalAuth(
                              path, environment(L"CARPLAY_CRYPTO",
                                                L""));
}
} // namespace
MediaCapabilities applicationMediaCapabilities() {
    MediaCapabilities c;
    c.model = deviceProfile().model;
    c.displayName = deviceProfile().displayName;
    c.maxFPS = deviceProfile().maxFPS;
    c.audioOutput = environment(L"CARPLAY_AUDIO_OUTPUT",deviceProfile().audio.output?L"1":L"0") != L"0";
    c.microphone = environment(L"CARPLAY_MICROPHONE",deviceProfile().audio.input?L"1":L"0") != L"0";
    c.touch = environment(L"CARPLAY_TOUCH",L"1") != L"0";
    c.controls = environment(L"CARPLAY_CONTROLS",L"1") != L"0";
#ifndef WINXPLAY_WOLFSSL
    c.aac = c.audioOutput;
    c.bufferedAac = c.audioOutput;
#elif defined(CARPLAY_CE_AAC)
    c.aac = c.audioOutput && environment(L"CARPLAY_AAC",L"1")!=L"0" && winceAacAvailable();
#endif
    return c;
}
std::string applicationAccessorySerialNumber() {
    AirCrypto crypto(environment(L"CARPLAY_CRYPTO",L""));
    ProtectedStore store;Pairing pairing(crypto,store);return pairing.serialNumber();
}
struct ReceiverContext::Impl {
    AirCrypto crypto;
    ProtectedStore store;
    SocketTransport network;
    NativeRuntime runtime;
    NativeMedia media;
    std::auto_ptr<AuthProvider> identity;
    ReceiverServices services;
    Impl(VideoCallback cb, void *ctx, HWND window)
        : crypto(environment(L"CARPLAY_CRYPTO", L"")),
          media(cb, ctx, window), identity(loadIdentity()),
          services(crypto, store, network, runtime, media, identity.get()) {}
};
ReceiverContext::ReceiverContext(VideoCallback cb, void *ctx, HWND window) : p_(new Impl(cb, ctx, window)) {}
ReceiverContext::~ReceiverContext() {
    delete p_;
}
ReceiverServices &ReceiverContext::services() {
    return p_->services;
}
} // namespace cp
