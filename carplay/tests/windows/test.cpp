#include "../../platform/common/native_services.h"
#include "../../platform/common/native_cipher.h"
#include "../../platform/common/local_identity.h"
#include "../../core/receiver.h"
#include "../../core/pairing.h"
#include "../../platform/common/native_crypto.h"
#include "../../platform/common/receiver_context.h"
#include "../../platform/windows/mf_audio.h"
#include <iostream>
#include <sstream>
#include <algorithm>
using namespace cp;
static unsigned passed = 0, failed = 0;
#define CHECK(x) require(bool(x), #x)
#define REJECT(code)                                                                               \
    {                                                                                              \
        bool rejected = false;                                                                     \
        try {                                                                                      \
            code;                                                                                  \
        } catch (const std::exception &) {                                                         \
            rejected = true;                                                                       \
        }                                                                                          \
        CHECK(rejected);                                                                           \
    }
static void test(const char *name, void (*f)()) {
    try {
        f();
        ++passed;
        std::cout << "PASS " << name << "\n";
    } catch (const std::exception &e) {
        ++failed;
        std::cout << "FAIL " << name << ": " << e.what() << "\n";
    }
}
static void accessoryIdentityTest() {
    struct Store:PairingStore {
        Bytes data;unsigned writes;Store():writes(0){}
        bool exists() const{return !data.empty();}
        Bytes read(){return data;}
        void write(const Bytes& b){data=b;++writes;}
    } first,second;
    AirCrypto c(environment(L"CARPLAY_CRYPTO",L""));
    Pairing a(c,first),b(c,second);
    CHECK(a.identifier()!=b.identifier() && a.serialNumber()!=b.serialNumber() && a.displayUuid()!=b.displayUuid());
    CHECK(a.identifier().size()==17 && a.serialNumber().size()==36 && a.displayUuid().size()==36);
    CHECK((unhex(a.identifier().substr(0,2))[0]&3)==2);
    Bytes saved=first.data;Pairing again(c,first);
    CHECK(a.identifier()==again.identifier() && a.serialNumber()==again.serialNumber() && a.displayUuid()==again.displayUuid());
    CHECK(a.publicKey()==again.publicKey() && first.writes==1 && first.data==saved);
    MediaCapabilities caps;caps.deviceId=a.identifier();caps.serialNumber=a.serialNumber();caps.displayUuid=a.displayUuid();
    Value info=airplayInfo(800,480,caps);
    CHECK(info.at("deviceID").string==a.identifier() && info.at("bluetoothIDs").array[0].string==a.identifier());
    for(size_t i=0;i<info.at("hidDevices").array.size();++i)
        CHECK(info.at("hidDevices").array[i].at("displayUUID").string==info.at("displays").array[0].at("uuid").string);
    Frame identification=accessoryIdentification(1,"WinXPlay MRN","MRNWinCECarPlay1,1",a.serialNumber().c_str());
    std::vector<Frame> fields=parameters(identification.body);
    CHECK(fields[3].id==3 && fields[3].body==join(bytes(a.serialNumber()),Bytes(1,0)));
    Value broken=parsePlist(first.data);broken.dict["seed"]=Value::blob(Bytes(1,0));first.data=binaryPlist(broken);
    REJECT(Pairing invalid(c,first));
}
static void csmTest() {
    Bytes b = csm(0xaa02, parameter(0, unhex("010203")));
    CHECK(hex(b) == "4040000daa0200070000010203");
    CsmParser parser;
    std::vector<Frame> frames;
    for (size_t i = 0; i < b.size(); i++) {
        std::vector<Frame> f = parser.feed(slice(b, i, 1));
        frames.insert(frames.end(), f.begin(), f.end());
    }
    CHECK(frames.size() == 1 && frames[0].id == 0xaa02 &&
          parameters(frames[0].body)[0].body == unhex("010203"));
    Bytes many = b;
    many.insert(many.end(), b.begin(), b.end());
    CHECK(parser.feed(many).size() == 2);
    REJECT(parameters(unhex("00030000")));
    REJECT(csm(1, Bytes(65530)));
}
static void csmResync() {
    CsmParser p;
    Bytes b(100000, 0xff);
    Bytes good = csm(1, Bytes(65529, 3));
    b.insert(b.end(), good.begin(), good.end());
    CHECK(p.feed(b).size() == 1 && p.buffered() == 0);
}
static void linkTest() {
    LinkFrame f;
    f.control = 0x40;
    f.sequence = 255;
    f.ack = 4;
    f.session = 1;
    f.body = unhex("404000060001");
    Bytes b = linkFrame(f);
    LinkFrame r = parseLink(b);
    CHECK(r.body == f.body && r.sequence == 255);
    b[8] ^= 1;
    REJECT(parseLink(b));
    b = linkFrame(f);
    b.back() ^= 1;
    REJECT(parseLink(b));
}
static void negotiationTest() {
    LinkNegotiator link;
    link.start(0);
    Bytes output = link.takeOutput();
    CHECK(slice(output, 0, 6) == unhex("ff550200ee10"));
    CHECK(parseLink(slice(output, 6, output.size() - 6)).sequence == 99);
    LinkFrame syn;
    syn.control = 0xc0;
    syn.sequence = 7;
    syn.ack = 99;
    syn.session = 0;
    syn.body = unhex("011effff0fa001f404030a0001");
    Bytes wire = linkFrame(syn);
    for (size_t i = 0; i < wire.size(); i++)
        link.feed(slice(wire, i, 1));
    CHECK(link.ready());
    CHECK(parseLink(link.takeOutput()).ack == 7);
    LinkFrame data;
    data.control = 0x40;
    data.sequence = 8;
    data.ack = 99;
    data.session = 10;
    data.body = csm(0x1d00, Bytes());
    link.feed(linkFrame(data));
    CHECK(link.takeMessages().at(0).id == 0x1d00);
    link.takeOutput();
    link.feed(linkFrame(data));
    CHECK(link.takeMessages().empty());
    link.takeOutput();
    link.send(Frame(0xaa01, parameter(0, Bytes(607, 0x30))));
    Bytes sent = link.takeOutput();
    CHECK(parseLink(sent).sequence == 100 && parseLink(sent).ack == 8);
    REJECT(link.send(Frame(0xaa03, Bytes())));
    link.tick(4000);
    CHECK(link.takeOutput() == sent);
    data.body.clear();
    data.ack = 100;
    data.session = 0;
    link.feed(linkFrame(data));
    link.send(Frame(0xaa03, parameter(0, Bytes(64, 1))));
    CHECK(parseLink(link.takeOutput()).sequence == 101);
    for (unsigned i = 2; i <= 5; ++i) {
        link.tick(i * 4000);
        link.takeOutput();
    }
    REJECT(link.tick(24000));
    LinkNegotiator timeout;
    timeout.start(0);
    timeout.takeOutput();
    timeout.tick(500);
    CHECK(!timeout.takeOutput().empty());
    REJECT(timeout.tick(10000));
}
static void ncmTest() {
    Bytes frame(484, 0x55);
    Bytes b = ntb16(frame, 65535);
    CHECK(b.size() == 513 && le16(b, 8) == 512);
    CHECK(parseNtb16(b).at(0) == frame);
    put16(b, 18, 12, true);
    REJECT(parseNtb16(b));
    b = ntb16(frame, 1);
    put16(b, 22, 65535, true);
    REJECT(parseNtb16(b));
    b = ntb16(frame, 1);
    put16(b, 20, 12, true);
    REJECT(parseNtb16(b));
}
static void tlvTest() {
    Bytes value(1024, 0xab);
    CHECK(parseTlv8(tlv8(3, value))[3] == value);
    CHECK(tlv8(2, Bytes()) == unhex("0200"));
    REJECT(parseTlv8(unhex("0302ff")));
}
static void inputTest() {
    CHECK(hex(touch(0.5, 0.5, true, 800, 480)) == "00019001f000010000000000");
    CHECK(hex(knob(false, true, false, -2, 0, 1)) == "02fe0001");
    CHECK(le16(touch(3, -1, true, 800, 480), 2) == 800);
    CHECK(pcmSwap(unhex("12345678")) == unhex("34127856"));
    REJECT(pcmSwap(Bytes(3)));
}
static void rtspTest() {
    RtspParser p;
    Bytes req = bytes("GET /info RTSP/1.0\r\nCSeq: 4\r\nContent-Length: 3\r\n\r\nabc");
    std::vector<Request> messages;
    for (size_t i = 0; i < req.size(); i++) {
        std::vector<Request> r = p.feed(slice(req, i, 1));
        messages.insert(messages.end(), r.begin(), r.end());
    }
    CHECK(messages.size() == 1 && text(messages[0].body) == "abc");
    RtspParser eventResponse;
    std::vector<Request> replies =
        eventResponse.feed(bytes("RTSP/1.0 200 OK\r\nCSeq: 3\r\nContent-Length: 0\r\n\r\n"));
    CHECK(replies.size() == 1 && replies[0].method == "RTSP/1.0");
    CHECK(text(response(messages[0], 200, Bytes())).find("CSeq: 4") != std::string::npos);
    RtspParser bad;
    REJECT(bad.feed(bytes("GET /info RTSP/1.0\r\nContent-Length: -1\r\n\r\n")));
    RtspParser duplicate;
    REJECT(
        duplicate.feed(bytes("GET / RTSP/1.0\r\nContent-Length: 0\r\ncontent-length: 0\r\n\r\n")));
    RtspParser huge;
    REJECT(huge.feed(Bytes(32769, 'x')));
}
static void plistTest() {
    Value v = parsePlist(readFile(L"fixtures/independent.bplist"));
    CHECK(v.at("name").string == utf8(L"Windows \x8f66\x673a \xd83d\xde97"));
    CHECK(v.at("data").data.size() == 256);
    CHECK(v.at("enabled").boolean);
    CHECK(v.at("number").integer == U64(0xfedcba98) * U64(0x100000000) + 0x76543210);
    Bytes out = binaryPlist(v);
    writeFile(L"fixtures/native.bplist", out);
    CHECK(parsePlist(out).at("nested").at("key").string == "a&<b>");
    CHECK(parsePlist(xmlPlist(v)).at("name").string == v.at("name").string);
    CHECK(parsePlist(readFile(L"fixtures/independent.xml")).at("data").data == v.at("data").data);
    Value negative = parsePlist(bytes("<plist><integer>-9223372036854775808</integer></plist>"));
    CHECK(negative.negative && negative.integer == 0x8000000000000000ULL);
    CHECK(parsePlist(xmlPlist(parsePlist(binaryPlist(negative)))).integer == negative.integer);
    REJECT(parsePlist(bytes("<plist><integer>-9223372036854775809</integer></plist>")));
}
static void plistReject() {
    REJECT(parsePlist(
        bytes("<!DOCTYPE plist [<!ENTITY x 'boom'>]><plist><string>&x;</string></plist>")));
    REJECT(parsePlist(bytes("<plist><dict><key>a</key><integer>1</integer><key>a</key><integer>2</"
                            "integer></dict></plist>")));
    Bytes b = readFile(L"fixtures/independent.bplist");
    b[b.size() - 26] = 255;
    REJECT(parsePlist(b));
}
static void audioCapabilitiesTest() {
    MediaCapabilities caps=applicationMediaCapabilities();
    caps.deviceId="02:00:00:00:00:01";caps.displayUuid="12345678-1234-4234-8234-123456789abc";
    Value info = parsePlist(binaryPlist(airplayInfo(800, 480, caps)));
    const Value &formats = info.at("audioFormats");
    unsigned voice = 0;
    for (size_t i = 0; i < formats.array.size(); ++i) {
        const Value &f = formats.array[i];
        const std::string &kind = f.at("audioType").string;
        if (kind == "telephony" || kind == "speechRecognition") {
            CHECK(f.at("type").integer == 100);
            U64 output = f.at("audioOutputFormats").integer,
                input = f.at("audioInputFormats").integer;
            // Real iOS 27 rejects the entire audio route if a voice entry lacks
            // a compatible input/output pair, even when only music is playing.
            CHECK((output & input) != 0);
            CHECK((output & ~U64(0x4554)) == 0);
            CHECK((input & ~U64(0x4554)) == 0);
            ++voice;
        }
    }
    CHECK(voice == 2);
}
static OpenSslCrypto *crypto = 0;
static void rfcAead() {
    Bytes key = unhex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
    Bytes nonce = unhex("070000004041424344454647");
    Bytes aad = unhex("50515253c0c1c2c3c4c5c6c7");
    Bytes plain = bytes("Ladies and Gentlemen of the class of '99: If I could offer you only one "
                        "tip for the future, sunscreen would be it.");
    Bytes result = crypto->seal(key, nonce, plain, aad);
    CHECK(hex(result) ==
          "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69"
          "da92728b1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad67594"
          "5585808b4831d7bc3ff4def08e4b7a9de576d26586cec64b61161ae10b594f09e26a7e902ecbd0600691");
    CHECK(crypto->open(key, nonce, result, aad) == plain);
    result.back() ^= 1;
    REJECT(crypto->open(key, nonce, result, aad));
}
static void controlTest() {
    Bytes key(32, 0x11);
    ControlCipher a(*crypto, key, key), b(*crypto, key, key);
    Bytes plain(40000, 0xaa), sealed = a.encrypt(plain), out;
    for (size_t i = 0; i < sealed.size(); i += 127) {
        Bytes part = b.feed(slice(sealed, i, std::min(size_t(127), sealed.size() - i)));
        out.insert(out.end(), part.begin(), part.end());
    }
    CHECK(out == plain);
    ControlCipher c(*crypto, key, key);
    sealed[7] ^= 1;
    REJECT(c.feed(sealed));
    REJECT(c.feed(Bytes()));
}
static void audioTest() {
    Bytes key(32, 5), pcm = unhex("000134120000ffff");
    Bytes packet = microphonePacket(*crypto, key, pcm, 65535, 0xffffffff, 7, 96);
    CHECK(be16(packet, 2) == 65535 && be32(packet, 4) == 0xffffffff);
    CHECK(decryptAudio(*crypto, key, packet) == pcmSwap(pcm));
    packet[4] ^= 1;
    REJECT(decryptAudio(*crypto, key, packet));
}
static void screenTest() {
    Bytes key = readFile(L"fixtures/key.bin"), wire = readFile(L"fixtures/screen.bin"), annex;
    ScreenParser parser(*crypto, key);
    Bytes auxiliary(128, 0);
    auxiliary[4] = 5;
    CHECK(parser.feed(auxiliary).empty());
    unsigned count = 0;
    for (size_t i = 0; i < wire.size(); i += 73) {
        std::vector<VideoPacket> packets =
            parser.feed(slice(wire, i, std::min(size_t(73), wire.size() - i)));
        for (size_t j = 0; j < packets.size(); j++) {
            Bytes data = packets[j].type == 1 ? avccToAnnexB(packets[j].body) : packets[j].body;
            annex.insert(annex.end(), data.begin(), data.end());
            ++count;
        }
    }
    CHECK(count > 30);
    writeFile(L"fixtures/native-decoded.h264", annex);
    Bytes bad = wire;
    size_t second = 128 + le32(wire, 0);
    bad[second + 8] ^= 1;
    ScreenParser reject(*crypto, key);
    REJECT(reject.feed(bad));
}
static void decoderTest() {
    Bytes source=readFile(L"fixtures/reference.aac"), decoded, pcm(65536);
    void *decoder=0; CHECK(SUCCEEDED(cpAacCreate(48000,2,&decoder)));
    unsigned frames=0;
    try {
        for(size_t at=0;at<source.size();) {
            CHECK(source.size()-at>=7 && source[at]==255);
            unsigned header=(source[at+1]&1)?7:9;
            unsigned size=((source[at+3]&3)<<11)|(source[at+4]<<3)|(source[at+5]>>5);
            CHECK(size>header && size<=source.size()-at);
            unsigned written=0;
            CHECK(SUCCEEDED(cpAacDecode(decoder,&source[at+header],size-header,&pcm[0],unsigned(pcm.size()),&written)));
            decoded.insert(decoded.end(),pcm.begin(),pcm.begin()+written);++frames;at+=size;
        }
        unsigned written=0;
        CHECK(SUCCEEDED(cpAacDrain(decoder,&pcm[0],unsigned(pcm.size()),&written)));
        decoded.insert(decoded.end(),pcm.begin(),pcm.begin()+written);
        CHECK(decoded.size()>=(frames-2)*1024*4 && decoded.size()<=frames*1024*4);
        CHECK(std::count(decoded.begin(),decoded.end(),U8(0))<int(decoded.size()/2));
        writeFile(L"aac-system-decoded.pcm",decoded);
    } catch(...) {cpAacDestroy(decoder);throw;}
    cpAacDestroy(decoder);
}
static void silentAudio() {
    playPcm16(Bytes(4800 * 2 * 2, 0), 48000, 2);
}
static void stateTest() {
    Session s;
    unsigned g = s.attach();
    REJECT(s.video(g));
    s.trusted(g);
    s.beginAuth(g);
    s.authenticated(g);
    s.video(g);
    CHECK(s.state() == Streaming);
    s.disconnect();
    s.video(g);
    CHECK(s.state() == Disconnected);
    unsigned next = s.attach();
    s.fail(g, "stale");
    CHECK(s.state() == UsbAvailable);
    s.fail(next, "auth missing");
    CHECK(s.state() == Faulted);
}
class SyntheticAuth : public AuthProvider {
  public:
    Bytes certificate() {
        return bytes("SYNTHETIC TEST CERTIFICATE - NOT A DEVICE IDENTITY");
    }
    Bytes signChallenge(const Bytes &digest) {
        CHECK(digest.size() == 32);
        return Bytes(64, 0x42);
    }
};
static void authTest() {
    SyntheticAuth provider;
    MfiExchange auth(provider);
    std::vector<Frame> reply = auth.receive(Frame(0xaa00));
    CHECK(reply.size() == 1 && reply[0].id == 0xaa01 &&
          parameters(reply[0].body)[0].body == provider.certificate());
    reply = auth.receive(Frame(0xaa02, parameter(0, Bytes(32, 7))));
    CHECK(reply[0].id == 0xaa03 && parameters(reply[0].body)[0].body.size() == 64);
    CHECK(auth.state() == MfiExchange::ChallengeAnswered);
    auth.receive(Frame(0xaa05));
    CHECK(auth.state() == MfiExchange::Accepted);
    MfiExchange unsolicited(provider);
    REJECT(unsolicited.receive(Frame(0xaa05)));
    MfiExchange bad(provider);
    bad.receive(Frame(0xaa00));
    REJECT(bad.receive(Frame(0xaa02, parameter(0, Bytes(31)))));
    CHECK(bad.state() == MfiExchange::Rejected);
}
static void expansionTest() {
    REJECT(parsePlist(readFile(L"fixtures/expansion.bplist")));
    Bytes b = ntb16(Bytes(40, 0xaa), 0);
    // Two entries aliasing the same large datagram must not inflate total output above NTB size.
    b = Bytes(256, 0);
    put32(b, 0, 0x484d434e, true);
    put16(b, 4, 12, true);
    put16(b, 8, 256, true);
    put16(b, 10, 12, true);
    put32(b, 12, 0x304d434e, true);
    put16(b, 16, 24, true);
    for (unsigned i = 0; i < 3; i++) {
        put16(b, 20 + i * 4, 64, true);
        put16(b, 22 + i * 4, 192, true);
    }
    REJECT(parseNtb16(b));
}
static void cancellationTest() {
    void *decoder=0;
    CHECK(FAILED(cpAacCreate(12345,2,&decoder)) && !decoder);
    CHECK(FAILED(cpAacCreate(48000,3,&decoder)) && !decoder);
    for(unsigned i=0;i<4;++i) {
        CHECK(SUCCEEDED(cpAacCreate(i%2?44100:48000,i%2?1:2,&decoder)));
        BYTE pcm[65536];unsigned written=0;
        CHECK(FAILED(cpAacDecode(decoder,0,1,pcm,sizeof(pcm),&written)));
        CHECK(SUCCEEDED(cpAacDrain(decoder,pcm,sizeof(pcm),&written)));
        CHECK(FAILED(cpAacDrain(decoder,pcm,sizeof(pcm),&written)));
        cpAacDestroy(decoder);decoder=0;
    }
}
static void fuzz() {
    unsigned seed = 7;
    for (unsigned n = 0; n < 10000; n++) {
        Bytes b(n % 256);
        for (size_t j = 0; j < b.size(); j++) {
            seed = 1664525 * seed + 1013904223;
            b[j] = U8(seed >> 24);
        }
        try {
            parsePlist(b);
        } catch (const std::exception &) {
        }
        try {
            parseNtb16(b);
        } catch (const std::exception &) {
        }
        try {
            parseLink(b);
        } catch (const std::exception &) {
        }
        try {
            parseTlv8(b);
        } catch (const std::exception &) {
        }
        CsmParser c;
        c.feed(b);
        CHECK(c.buffered() <= 65535);
    }
}
int main(int argc, char **argv) {
    if (argc == 4 && std::string(argv[1]) == "--auth-sign") {
        try {
            LocalAuth identity(
                std::string(argv[2]) == "-" ? environment(L"CARPLAY_IDENTITY", L"") : wide(argv[2]),
                environment(L"CARPLAY_CRYPTO", L""));
            std::cout << hex(identity.signChallenge(unhex(argv[3]))) << "\n";
            return 0;
        } catch (const std::exception &e) {
            std::cout << e.what() << "\n";
            return 2;
        }
    }
    if (argc == 3 && std::string(argv[1]) == "--carkit") {
        UsbMux mux(unsigned(atoi(argv[2])));
        AppleDevice device;
        device.id = 17;
        device.serial = "SYNTHETIC";
        device.connection = "USB";
        Value result = probeCarKit(mux, device);
        std::cout << text(xmlPlist(result));
        return (result.dict.count("iap2_identification_requested") ||
                result.dict.count("mfi_authentication_accepted")) &&
                       !result.dict.count("error")
                   ? 0
                   : 2;
    }
    if (argc == 3 && std::string(argv[1]) == "--mux") {
        try {
            UsbMux mux(unsigned(atoi(argv[2])));
            std::vector<AppleDevice> devices = mux.devices();
            CHECK(devices.size() == 1 && devices[0].id == 17 && devices[0].connection == "USB");
            Value info = mux.deviceInfo(devices[0]);
            CHECK(info.at("Value").string == "test-ios");
            std::cout << "MOCK_MUX_OK\n";
            return 0;
        } catch (const std::exception &e) {
            std::cout << e.what() << "\n";
            return 2;
        }
    }
    try {
        OpenSslCrypto provider(
            environment(L"CARPLAY_CRYPTO", L""));
        crypto = &provider;
        test("CSM golden vector and fragments", csmTest);
        test("CSM garbage resync and maximum frame", csmResync);
        test("iAP2 link checksums", linkTest);
        test("iAP2 negotiation retries fragments and duplicates", negotiationTest);
        test("NCM padding bounds and cycles", ncmTest);
        test("TLV8 continuation", tlvTest);
        test("HID coordinates and PCM endian", inputTest);
        test("RTSP fragmentation and hostile lengths", rtspTest);
        test("Independent XML and binary plist interoperability", plistTest);
        test("CarPlay voice formats retain compatible input and output", audioCapabilitiesTest);
        test("Persistent distinct accessory IDs and consistent protocol identity", accessoryIdentityTest);
        test("Plist rejects entities duplicate keys invalid offsets", plistReject);
        test("RFC 8439 ChaCha20-Poly1305 vector", rfcAead);
        test("Encrypted control fragments and fail-closed", controlTest);
        test("Encrypted microphone/audio RTP", audioTest);
        test("Python-encrypted H264 stream and header tamper", screenTest);
        test("System AAC decoder produces non-silent stereo PCM", decoderTest);
        test("Windows audio device accepts silent PCM", silentAudio);
        test("Session authentication gate and stale callbacks", stateTest);
        test("MFi certificate challenge and phone acceptance gating", authTest);
        test("Plist and NCM expansion budgets", expansionTest);
        test("System AAC invalid formats drain and repeated cleanup", cancellationTest);
        test("10000 malformed input rounds", fuzz);
    } catch (const std::exception &e) {
        ++failed;
        std::cout << "FATAL " << e.what() << "\n";
    }
    std::ostringstream summary;
    summary << "{\"passed\":" << passed << ",\"failed\":" << failed
            << ",\"fuzz_rounds\":10000,\"physical_iphone_verified\":false}";
    writeFile(L"test-results.json", bytes(summary.str()));
    std::cout << summary.str() << "\n";
    return failed ? 1 : 0;
}
