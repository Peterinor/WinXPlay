// This binary links only core, the C++ runtime and test doubles. No Windows SDK
// headers or application platform, codec, crypto-provider or device code is used.
#include "../../core/receiver.h"
#include "../../core/buffered_audio.h"
#include <iostream>
#include <limits>
using namespace cp;
#define CHECK(v) require((v), #v)
namespace {
struct TestCrypto : CryptoProvider {
    Bytes random(unsigned n) {
        return Bytes(n, 7);
    }
    Bytes publicKey(const char *, const Bytes &) {
        return Bytes(32, 9);
    }
    Bytes seal(const Bytes &, const Bytes &, const Bytes &, const Bytes &) {
        throw std::runtime_error("unexpected crypto");
    }
    Bytes open(const Bytes &, const Bytes &, const Bytes &, const Bytes &) {
        throw std::runtime_error("unexpected crypto");
    }
    Bytes hash(const char *, const Bytes &) {
        throw std::runtime_error("unexpected crypto");
    }
    Bytes hkdf(const Bytes &, const std::string &, const std::string &) {
        throw std::runtime_error("unexpected crypto");
    }
    Bytes shared(const Bytes &, const Bytes &) {
        throw std::runtime_error("unexpected crypto");
    }
    Bytes sign(const Bytes &, const Bytes &) {
        throw std::runtime_error("unexpected crypto");
    }
    bool verify(const Bytes &, const Bytes &, const Bytes &) {
        throw std::runtime_error("unexpected crypto");
    }
    Bytes aesCtr(const Bytes &, const Bytes &, const Bytes &) {
        throw std::runtime_error("unexpected crypto");
    }
    Bytes mod(const Bytes &, const Bytes &, const Bytes &, char) {
        throw std::runtime_error("unexpected crypto");
    }
};
struct TestStore : PairingStore {
    Bytes data;
    bool exists() const {
        return !data.empty();
    }
    Bytes read() {
        return data;
    }
    void write(const Bytes &b) {
        data = b;
    }
};
struct TestRuntime : SessionRuntime {
    bool stopped, failPoll;
    unsigned locks, joins;
    Value saved;
    TestRuntime() : stopped(false), failPoll(false), locks(0), joins(0) {}
    void start(void (*fn)(void *), void *p) {
        fn(p);
    }
    void cancel() {
        stopped = true;
    }
    bool cancelled() const {
        return stopped;
    }
    void join() {
        ++joins;
    }
    void lock() {
        ++locks;
    }
    void unlock() {
        CHECK(locks > 0);
        --locks;
    }
    U32 milliseconds() const {
        return 123;
    }
    U64 ntpTime() const {
        return U64(4000000000ULL) << 32;
    }
    void saveReport(const Value &v) {
        saved = v;
    }
};
struct TestTransport : Transport {
    TestRuntime &runtime;
    unsigned opened, closed;
    explicit TestTransport(TestRuntime &r) : runtime(r), opened(0), closed(0) {}
    Connection bind(unsigned, bool) {
        ++opened;
        return 42;
    }
    unsigned localPort(Connection c) {
        CHECK(c == 42);
        return 12345;
    }
    Connection accept(Connection, PeerAddress &) {
        throw std::runtime_error("unexpected accept");
    }
    void close(Connection c) {
        CHECK(c == 42);
        ++closed;
    }
    void sendAll(Connection, const Bytes &) {
        throw std::runtime_error("unexpected send");
    }
    bool receive(Connection, Bytes &) {
        throw std::runtime_error("unexpected receive");
    }
    bool receiveDatagram(Connection, Bytes &, PeerAddress &) {
        throw std::runtime_error("unexpected receive");
    }
    bool sendDatagram(Connection, const Bytes &, const PeerAddress &) {
        throw std::runtime_error("unexpected send");
    }
    std::vector<Connection> poll(const std::vector<Connection> &c, unsigned ms) {
        CHECK(c.size() == 1 && c[0] == 42 && ms == 10);
        if (runtime.failPoll)
            throw std::runtime_error("injected transport failure");
        runtime.cancel();
        return std::vector<Connection>();
    }
};
struct TestMedia : MediaFactory {
    struct Output : AudioOutput {
        TestMedia &owner;
        explicit Output(TestMedia &m) : owner(m) {
            ++owner.created;
        }
        ~Output() {
            ++owner.destroyed;
        }
        void receive(const Bytes &b) {
            owner.packets.push_back(b);
        }
        unsigned playedBytes() const {
            return 0;
        }
        void pause(bool p) {
            owner.paused = p;
        }
    };
    unsigned created, destroyed;
    bool paused;
    std::vector<Bytes> packets;
    TestMedia() : created(0), destroyed(0), paused(false) {}
    MediaCapabilities capabilities() const {
        return MediaCapabilities();
    }
    VideoPipeline *video(unsigned, unsigned) {
        throw std::runtime_error("unexpected video");
    }
    AudioOutput *audio(unsigned rate, unsigned channels, bool aac) {
        CHECK(rate == 48000 && channels == 2 && aac);
        return new Output(*this);
    }
    AudioInput *microphone(unsigned, unsigned, unsigned) {
        throw std::runtime_error("unexpected microphone");
    }
};
void capabilities() {
    MediaCapabilities pcm;
    pcm.deviceId="02:00:00:00:00:01";pcm.displayUuid="12345678-1234-4234-8234-123456789abc";
    Value a = airplayInfo(800, 480, pcm);
    CHECK(!a.dict.count("mainBufferedInfo"));
    for (size_t i = 0; i < a.at("audioFormats").array.size(); ++i)
        CHECK(a.at("audioFormats").array[i].at("type").integer < 102);
    MediaCapabilities full=pcm;
    full.aac = true;
    full.bufferedAac = true;
    CHECK(airplayInfo(1280, 720, full).dict.count("mainBufferedInfo") == 1);
    CHECK(!airplayInfo(800, 480, pcm).dict.count("mainBufferedInfo"));
    pcm.microphone=false;
    pcm.touch=false;
    CHECK(pcm.audioOutput);
    a=airplayInfo(800,480,pcm);
    for(size_t i=0;i<a.at("audioFormats").array.size();++i)
        CHECK(!a.at("audioFormats").array[i].dict.count("audioInputFormats"));
}
void inputProtocol() {
    std::vector<Value> commands;
    InputQueue q(800,480);
    CHECK(!q.contact(0,0.5,0.5,true));CHECK(!q.key(InputSelect,true));
    q.nightMode(true);q.ready(true);q.drain(commands);
    CHECK(commands.size()==1 && commands[0].at("params").at("nightMode").boolean);
    CHECK(q.contact(0,0.5,0.5,true));CHECK(q.contact(1,1,1,true));
    CHECK(q.contact(0,0.25,0.25,true));CHECK(q.contact(0,0.75,0.75,true));
    CHECK(q.contact(0,0.75,0.75,false));CHECK(q.contact(1,1,1,false));
    q.drain(commands);CHECK(commands.size()==5);
    CHECK(hex(commands[0].at("hidReport").data)=="00019001f000010000000000");
    const Bytes& both=commands[2].at("hidReport").data;
    CHECK(both[1]==1 && both[7]==1 && le16(both,2)==600 && le16(both,8)==800);
    CHECK(commands.back().at("hidReport").data[1]==0 && commands.back().at("hidReport").data[7]==0);
    for(size_t i=0;i<commands.size();++i)CHECK(!commands[i].dict.count("_move"));
    CHECK(!q.contact(2,0,0,true));CHECK(!q.contact(0,std::numeric_limits<double>::infinity(),0,true));
    CHECK(!q.contact(0,std::numeric_limits<double>::quiet_NaN(),0,true));
    CHECK(q.contact(0,-5,4,true));q.drain(commands);
    CHECK(le16(commands[0].at("hidReport").data,2)==0 && le16(commands[0].at("hidReport").data,4)==480);
    q.cancel();q.drain(commands);CHECK(commands[0].at("hidReport").data[1]==0);
    CHECK(q.key(InputSelect,true));CHECK(q.navigate(-5,3,1));CHECK(q.key(InputSelect,false));q.drain(commands);
    CHECK(commands.size()==4 && hex(commands[1].at("hidReport").data)=="01fb0301");
    CHECK(hex(commands[2].at("hidReport").data)=="01000000");
    CHECK(q.key(InputPlayPause,true));CHECK(q.key(InputPlayPause,true));CHECK(q.key(InputPlayPause,false));q.drain(commands);
    CHECK(commands.size()==2 && commands[0].at("uuid").string=="2a2a2a2c");
    CHECK(commands[0].at("hidReport").data[0]==3 && commands[1].at("hidReport").data[0]==0);
    CHECK(q.key(InputAnswer,true));CHECK(q.key(InputAnswer,false));CHECK(q.key(InputHangUp,true));q.drain(commands);
    CHECK(commands.size()==4 && commands[0].at("uuid").string=="2a2a2a2d");
    CHECK(commands[0].at("hidReport").data[0]==1 && commands[2].at("hidReport").data[0]==4);
    CHECK(q.key(InputSiri,true));q.cancel();q.drain(commands);
    CHECK(commands[0].at("type").string=="requestSiri" && commands[0].at("params").at("siriAction").integer==2);
    CHECK(commands.back().at("params").at("siriAction").integer==3);
    q.ready(false);CHECK(!q.key(InputPlay,true));q.ready(true);q.drain(commands);
    CHECK(commands.size()==1 && commands[0].at("type").string=="setNightMode");
    CHECK(q.contact(0,0,0,true));
    for(unsigned i=0;i<1000;++i)CHECK(q.contact(0,double(i)/1000,0,true));
    CHECK(q.contact(0,1,0,false));CHECK(q.pending()==3);q.drain(commands);
    for(unsigned i=0;i<100;++i){q.key(InputHome,true);q.key(InputHome,false);}
    CHECK(q.pending()<=64 && q.overflows()>0);q.cancel();q.drain(commands);
    CHECK(commands.back().at("hidReport").data[0]==0);
    InputQueue disabled(800,480,false,false);disabled.ready(true);
    CHECK(!disabled.contact(0,0,0,true) && !disabled.key(InputSelect,true) && !disabled.navigate(1,0,0));
    CHECK(disabled.keyFrame());disabled.drain(commands);CHECK(commands[0].at("type").string=="forceKeyFrame");
    MediaCapabilities c;c.touch=false;c.controls=false;
    c.deviceId="02:00:00:00:00:01";c.displayUuid="12345678-1234-4234-8234-123456789abc";
    CHECK(airplayInfo(800,480,c).at("hidDevices").array.empty());
    c.touch=true;c.controls=true;CHECK(airplayInfo(800,480,c).at("hidDevices").array.size()==4);
}
void lifecycle(bool fail) {
    TestCrypto crypto;
    TestStore store;
    TestRuntime runtime;
    TestTransport network(runtime);
    TestMedia media;
    ReceiverServices services(crypto, store, network, runtime, media, 0);
    runtime.failPoll = fail;
    {
        Receiver receiver(services, 0, 0);
        CHECK(receiver.port() == 12345 && !store.data.empty());
        receiver.start();
        Value report = receiver.report();
        CHECK(report.at("session_ended").boolean);
        CHECK(bool(report.dict.count("error")) == fail);
        receiver.stop();
        receiver.stop();
        CHECK(runtime.locks == 0 && !runtime.saved.dict.empty());
        CHECK(media.created == 0);
    }
    CHECK(network.opened == 1 && network.closed == 1 && runtime.locks == 0);
}
void constructionFailure() {
    TestCrypto crypto;
    TestStore store;
    TestRuntime runtime;
    TestTransport network(runtime);
    TestMedia media;
    ReceiverServices services(crypto, store, network, runtime, media, 0);
    bool rejected = false;
    try {
        Receiver receiver(services, 0, 0, 7000, false, 1, 1);
    } catch (const std::exception &) {
        rejected = true;
    }
    CHECK(rejected && network.opened == 1 && network.closed == 1 && runtime.locks == 0);
}
void audioOwnership() {
    TestMedia media;
    {
        BufferedAudio audio(media, 48000, 1);
        Bytes packet(20, 1);
        audio.receive(0, packet);
        packet[0] = 99;
        Value command = Value::dictionary();
        command.dict["rate"] = Value::num(1);
        command.dict["rtpTime"] = Value::num(0);
        audio.control("SETRATE", command, 0);
        CHECK(media.created == 1 && media.paused);
        audio.tick(0);
        CHECK(media.packets.size() == 1 && media.packets[0][0] == 1 && media.paused);
        audio.tick(U64(1) << 32);
        CHECK(!media.paused);
        command.dict["rate"] = Value::num(0);
        audio.control("SETRATE", command, U64(1) << 32);
        CHECK(media.destroyed == 1);
        CHECK(audio.feedback(0, 123).at("timestampRawNs").integer == 123000000);
    }
    CHECK(media.created == media.destroyed);
}
void scopedLock() {
    TestRuntime runtime;
    try {
        SessionLock lock(runtime);
        throw std::runtime_error("injected");
    } catch (const std::exception &) {
    }
    CHECK(runtime.locks == 0);
}
} // namespace
int main() {
    try {
        capabilities();
        inputProtocol();
        lifecycle(false);
        lifecycle(true);
        constructionFailure();
        audioOwnership();
        scopedLock();
        std::cout << "7 portable contract groups passed; no application platform code linked\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
