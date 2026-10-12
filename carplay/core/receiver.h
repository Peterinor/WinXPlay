#ifndef MRN_RECEIVER_H
#define MRN_RECEIVER_H
#include "../interfaces/crypto_provider.h"
#include "input.h"
#include "../interfaces/media.h"
#include "../interfaces/transport.h"
#include "../interfaces/session_runtime.h"
namespace cp {
typedef void (*StatusCallback)(void *, const std::string &);
// Called on the session worker after an authenticated phone command. The
// observer must copy data/post to its UI thread, and must not block the session.
typedef void (*ControlCallback)(void *, const Value &);
// All dependencies are borrowed and must outlive Receiver. Application setup
// owns their concrete implementations; Receiver never loads platform libraries.
struct ReceiverServices {
  private:
    ReceiverServices &operator=(const ReceiverServices &);

  public:
    CryptoProvider &crypto;
    PairingStore &store;
    Transport &transport;
    SessionRuntime &runtime;
    MediaFactory &media;
    AuthProvider *identity;
    ReceiverServices(CryptoProvider &c, PairingStore &s, Transport &t, SessionRuntime &r,
                     MediaFactory &m, AuthProvider *a)
        : crypto(c), store(s), transport(t), runtime(r), media(m), identity(a) {}
};
class Receiver {
    struct Impl;
    Impl *p_;
    Receiver(const Receiver &);
    Receiver &operator=(const Receiver &);

  public:
    Receiver(ReceiverServices &, StatusCallback, void *, unsigned port = 7000,
             bool physicalDevice = false, unsigned width = 800, unsigned height = 480);
    ~Receiver();
    void start();
    void stop();
    unsigned port() const;
    std::string publicKey() const;
    std::string deviceId() const;
    std::string serialNumber() const;
    void touchInput(double, double, bool);
    bool touchContact(unsigned slot,double x,double y,bool down);
    bool inputKey(InputKey key,bool down);
    bool navigate(int x,int y,int wheel);
    void cancelInput();
    void setNightMode(bool night);
    bool requestKeyFrame();
    void setControlCallback(ControlCallback callback,void* context);
    Value report();
};
Value airplayInfo(unsigned width, unsigned height, const MediaCapabilities &);
} // namespace cp
#endif
