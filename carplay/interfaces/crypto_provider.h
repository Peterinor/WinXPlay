#ifndef CP_CRYPTO_PROVIDER_H
#define CP_CRYPTO_PROVIDER_H
#include "../core/protocol.h"
namespace cp {
// Operations actually used by pairing/session logic. No provider or OS types.
class CryptoProvider : public Crypto {
  public:
    virtual Bytes random(unsigned) = 0;
    virtual Bytes hash(const char *, const Bytes &) = 0;
    virtual Bytes hkdf(const Bytes &, const std::string &, const std::string &) = 0;
    virtual Bytes publicKey(const char *, const Bytes &) = 0;
    virtual Bytes shared(const Bytes &, const Bytes &) = 0;
    virtual Bytes sign(const Bytes &, const Bytes &) = 0;
    virtual bool verify(const Bytes &, const Bytes &, const Bytes &) = 0;
    virtual Bytes aesCtr(const Bytes &, const Bytes &, const Bytes &) = 0;
    virtual Bytes mod(const Bytes &, const Bytes &, const Bytes &, char) = 0;
};
class PairingStore {
  public:
    virtual ~PairingStore() {}
    virtual bool exists() const = 0;
    virtual Bytes read() = 0;
    virtual void write(const Bytes &) = 0;
};
} // namespace cp
#endif
