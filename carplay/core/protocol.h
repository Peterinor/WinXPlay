// C++03 protocol layer. No Windows, Android, JVM, clocks, threads or device APIs.
#ifndef MRN_CARPLAY_PROTOCOL_H
#define MRN_CARPLAY_PROTOCOL_H
#include <vector>
#include <string>
#include <map>
#include <stdexcept>
namespace cp {
typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int U32;
typedef signed int I32;
#ifdef _MSC_VER
typedef unsigned __int64 U64;
typedef signed __int64 I64;
#else
typedef unsigned long long U64;
typedef signed long long I64;
#endif
typedef std::vector<U8> Bytes;
Bytes join(const Bytes &, const Bytes &);
void eraseSecret(Bytes &);
void require(bool condition, const char *message);
U16 be16(const Bytes &b, size_t p);
U32 be32(const Bytes &b, size_t p);
U16 le16(const Bytes &b, size_t p);
U32 le32(const Bytes &b, size_t p);
void put16(Bytes &b, size_t p, U16 n, bool little = false);
void put32(Bytes &b, size_t p, U32 n, bool little = false);
Bytes slice(const Bytes &b, size_t p, size_t n);
Bytes bytes(const std::string &s);
std::string text(const Bytes &b);
std::string hex(const Bytes &b);
Bytes unhex(const std::string &s);

struct Frame {
    U16 id;
    Bytes body;
    Frame(U16 i = 0, const Bytes &b = Bytes()) : id(i), body(b) {}
};
Bytes csm(U16 id, const Bytes &body);
Bytes parameter(U16 id, const Bytes &body);
std::vector<Frame> parameters(const Bytes &body);
class CsmParser {
    Bytes pending_;

  public:
    std::vector<Frame> feed(const Bytes &chunk);
    size_t buffered() const {
        return pending_.size();
    }
};
struct LinkFrame {
    U8 control, sequence, ack, session;
    Bytes body;
};
Bytes linkFrame(const LinkFrame &f);
LinkFrame parseLink(const Bytes &b);
// Bounded wired bring-up. Does not claim to implement reliable runtime retransmission.
class LinkNegotiator {
    Bytes pending_, output_;
    std::vector<Frame> messages_;
    CsmParser csm_;
    bool started_, synchronized_, ready_;
    U8 peerSequence_, localSequence_;
    U64 start_, retry_, now_, sentAt_;
    Bytes unacknowledged_;
    unsigned sendRetries_, peerMtu_;
    Bytes syn() const;
    void ack();

  public:
    LinkNegotiator();
    void start(U64 now);
    void feed(const Bytes &chunk);
    void tick(U64 now);
    void send(const Frame &message);
    Bytes takeOutput();
    std::vector<Frame> takeMessages();
    bool ready() const {
        return ready_;
    }
    bool canSend() const {
        return ready_ && unacknowledged_.empty();
    }
};
Frame accessoryIdentification(unsigned usbInterface, const char *displayName, const char *model, const char *serial);
std::vector<Frame> wiredSubscriptions();
Bytes ntb16(const Bytes &ethernet, U16 sequence);
std::vector<Bytes> parseNtb16(const Bytes &b);
Bytes tlv8(U8 type, const Bytes &value);
std::map<U8, Bytes> parseTlv8(const Bytes &b);
Bytes touch(double x, double y, bool down, unsigned width, unsigned height);
Bytes knob(bool select, bool home, bool back, int dx, int dy, int wheel);
Bytes avccToAnnexB(const Bytes &b);
Bytes nalToAnnexB(const Bytes &b);
Bytes pcmSwap(const Bytes &b);

struct Request {
    std::string method, path, protocol;
    std::map<std::string, std::string> headers;
    Bytes body;
};
class RtspParser {
    Bytes pending_;

  public:
    std::vector<Request> feed(const Bytes &chunk);
    size_t buffered() const {
        return pending_.size();
    }
};
Bytes response(const Request &r, unsigned status, const Bytes &body,
               const std::string &contentType = "application/x-apple-binary-plist");

struct Value {
    enum Type { Null, Boolean, Integer, Real, String, Data, Array, Dict } type;
    bool boolean, negative;
    U64 integer;
    double real;
    std::string string;
    Bytes data;
    std::vector<Value> array;
    std::map<std::string, Value> dict;
    Value() : type(Null), boolean(false), negative(false), integer(0), real(0) {}
    static Value str(const std::string &s);
    static Value num(U64 n);
    static Value blob(const Bytes &b);
    static Value flag(bool b);
    static Value dictionary();
    static Value list();
    const Value &at(const std::string &key) const;
};
Value parsePlist(const Bytes &b);
Bytes xmlPlist(const Value &v);
Bytes binaryPlist(const Value &v);

// The platform owns keys and cryptography; the protocol owns framing and nonce counters.
class Crypto {
  public:
    virtual ~Crypto() {}
    virtual Bytes seal(const Bytes &key, const Bytes &nonce, const Bytes &plain,
                       const Bytes &aad) = 0;
    virtual Bytes open(const Bytes &key, const Bytes &nonce, const Bytes &sealed,
                       const Bytes &aad) = 0;
};
Bytes nonce64(U64 counter);
class ControlCipher {
    Crypto &crypto_;
    Bytes readKey_, writeKey_, pending_;
    U64 readCounter_, writeCounter_;
    bool failed_;
    ControlCipher(const ControlCipher &);
    ControlCipher &operator=(const ControlCipher &);

  public:
    ControlCipher(Crypto &c, const Bytes &readKey, const Bytes &writeKey);
    Bytes encrypt(const Bytes &plain);
    Bytes feed(const Bytes &chunk);
};
struct VideoPacket {
    U8 type;
    Bytes body;
};
class ScreenParser {
    Bytes pending_;
    U64 counter_, received_, config_;
    Crypto &crypto_;
    Bytes key_;
    bool failed_;
    ScreenParser(const ScreenParser &);
    ScreenParser &operator=(const ScreenParser &);

  public:
    ScreenParser(Crypto &c, const Bytes &key)
        : counter_(0), received_(0), config_(0), crypto_(c), key_(key), failed_(false) {}
    std::vector<VideoPacket> feed(const Bytes &chunk);
    U64 receivedFrames() const { return received_; }
    U64 decryptedFrames() const { return counter_; }
    U64 configurations() const { return config_; }
    size_t pendingBytes() const { return pending_.size(); }
};
Bytes decryptAudio(Crypto &crypto, const Bytes &key, const Bytes &packet);
Bytes microphonePacket(Crypto &crypto, const Bytes &key, const Bytes &pcm, U16 sequence,
                       U32 timestamp, U64 nonce, U8 payloadType);

enum SessionState {
    Disconnected,
    UsbAvailable,
    Trusted,
    Authenticating,
    Authenticated,
    Streaming,
    Faulted
};
class AuthProvider {
  public:
    virtual ~AuthProvider() {}
    virtual Bytes certificate() = 0;
    virtual Bytes signChallenge(const Bytes &digest) = 0;
};
class MfiExchange {
  public:
    enum State { Waiting, CertificateSent, ChallengeAnswered, Accepted, Rejected };

  private:
    AuthProvider &provider_;
    State state_;
    MfiExchange(const MfiExchange &);
    MfiExchange &operator=(const MfiExchange &);

  public:
    explicit MfiExchange(AuthProvider &provider) : provider_(provider), state_(Waiting) {}
    // Returns outbound CSM frames. Acceptance is only set upon the phone's AA05.
    std::vector<Frame> receive(const Frame &frame);
    State state() const {
        return state_;
    }
};
class Session {
    SessionState state_;
    unsigned generation_;
    std::string error_;

  public:
    Session() : state_(Disconnected), generation_(0) {}
    unsigned attach();
    void trusted(unsigned generation);
    void beginAuth(unsigned generation);
    void authenticated(unsigned generation);
    void video(unsigned generation);
    void disconnect();
    void fail(unsigned generation, const std::string &reason);
    SessionState state() const {
        return state_;
    }
    unsigned generation() const {
        return generation_;
    }
    const std::string &error() const {
        return error_;
    }
};
} // namespace cp
#endif
