#ifndef MRN_AIRCRYPTO_H
#define MRN_AIRCRYPTO_H
#include "native_services.h"
#include "native_cipher.h"
#include "../../interfaces/crypto_provider.h"
namespace cp {
Bytes join(const Bytes &a, const Bytes &b);
class AirCrypto : public CryptoProvider {
    OpenSslCrypto cipher_;
    struct Impl;
    Impl *p_;
    AirCrypto(const AirCrypto &);
    AirCrypto &operator=(const AirCrypto &);

  public:
    explicit AirCrypto(const std::wstring &dll);
    ~AirCrypto();
    Bytes seal(const Bytes &k, const Bytes &n, const Bytes &b, const Bytes &a) {
        return cipher_.seal(k, n, b, a);
    }
    Bytes open(const Bytes &k, const Bytes &n, const Bytes &b, const Bytes &a) {
        return cipher_.open(k, n, b, a);
    }
    Bytes random(unsigned size);
    Bytes hash(const char *algorithm, const Bytes &data);
    Bytes hkdf(const Bytes &key, const std::string &salt, const std::string &info);
    Bytes publicKey(const char *type, const Bytes &seed);
    Bytes shared(const Bytes &seed, const Bytes &peer);
    Bytes sign(const Bytes &seed, const Bytes &message);
    bool verify(const Bytes &key, const Bytes &message, const Bytes &signature);
    Bytes aesCtr(const Bytes &key, const Bytes &iv, const Bytes &data);
    Bytes mod(const Bytes &a, const Bytes &b, const Bytes &n, char operation);
};
Bytes protectedRead(const std::wstring &path);
void protectedWrite(const std::wstring &path, const Bytes &data);
} // namespace cp
#endif
