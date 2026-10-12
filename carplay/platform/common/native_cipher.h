#ifndef MRN_WINDOWS_CRYPTO_H
#define MRN_WINDOWS_CRYPTO_H
#ifdef WINXPLAY_WOLFSSL
#include "crypto_wolfssl.h"
namespace cp {
typedef WolfSslCrypto OpenSslCrypto;
}
#else
#include "../../core/protocol.h"
#include <windows.h>
namespace cp {
// Explicit OpenSSL provider loaded from a configured absolute path, never PATH/CWD.
class OpenSslCrypto : public Crypto {
    HMODULE module_;
    typedef void *(__cdecl *New)();
    typedef void(__cdecl *Free)(void *);
    typedef const void *(__cdecl *Cipher)();
    typedef int(__cdecl *Init)(void *, const void *, void *, const U8 *, const U8 *);
    typedef int(__cdecl *Update)(void *, U8 *, int *, const U8 *, int);
    typedef int(__cdecl *Final)(void *, U8 *, int *);
    typedef int(__cdecl *Ctrl)(void *, int, int, void *);
    New create_;
    Free free_;
    Cipher cipher_;
    Init encInit_, decInit_;
    Update encUpdate_, decUpdate_;
    Final encFinal_, decFinal_;
    Ctrl ctrl_;
    FARPROC symbol(const char *name);
    Bytes run(bool encrypt, const Bytes &key, const Bytes &nonce, const Bytes &input,
              const Bytes &aad);
    OpenSslCrypto(const OpenSslCrypto &);
    OpenSslCrypto &operator=(const OpenSslCrypto &);

  public:
    explicit OpenSslCrypto(const std::wstring &dll);
    ~OpenSslCrypto();
    Bytes seal(const Bytes &key, const Bytes &nonce, const Bytes &input, const Bytes &aad) {
        return run(true, key, nonce, input, aad);
    }
    Bytes open(const Bytes &key, const Bytes &nonce, const Bytes &input, const Bytes &aad) {
        return run(false, key, nonce, input, aad);
    }
};
} // namespace cp
#endif
#endif
