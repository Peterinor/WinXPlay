#ifndef MRN_CE_CRYPTO_H
#define MRN_CE_CRYPTO_H
#include "../../core/protocol.h"
namespace cp {
// Same protocol contract as the desktop provider; statically linked wolfCrypt on CE.
class WolfSslCrypto : public Crypto {
    Bytes run(bool encrypt, const Bytes &key, const Bytes &nonce, const Bytes &input,
              const Bytes &aad);

  public:
    explicit WolfSslCrypto(const std::wstring &) {}
    Bytes seal(const Bytes &key, const Bytes &nonce, const Bytes &in, const Bytes &aad) {
        return run(true, key, nonce, in, aad);
    }
    Bytes open(const Bytes &key, const Bytes &nonce, const Bytes &in, const Bytes &aad) {
        return run(false, key, nonce, in, aad);
    }
};
} // namespace cp
#endif
