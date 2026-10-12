#include "native_crypto.h"
#include <wincrypt.h>
#include <wolfssl/wolfcrypt/chacha20_poly1305.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/ed25519.h>
#include <wolfssl/wolfcrypt/curve25519.h>
#include <wolfssl/wolfcrypt/integer.h>
extern "C" int mrn_random_seed(unsigned char *out, unsigned int n) {
    HCRYPTPROV provider = 0;
    if (!CryptAcquireContextW(&provider, 0, 0, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return -1;
    BOOL ok = CryptGenRandom(provider, n, out);
    CryptReleaseContext(provider, 0);
    return ok ? 0 : -1;
}
namespace cp {
static const U8 *ptr(const Bytes &b) {
    return b.empty() ? (const U8 *)"" : &b[0];
}
Bytes WolfSslCrypto::run(bool enc, const Bytes &key, const Bytes &nonce, const Bytes &in,
                         const Bytes &aad) {
    require(key.size() == 32 && nonce.size() == 12 && (enc || in.size() >= 16),
            "Invalid AEAD size");
    size_t count = enc ? in.size() : in.size() - 16;
    Bytes result(count + (enc ? 16 : 0) + 1);
    int rc = enc ? wc_ChaCha20Poly1305_Encrypt(ptr(key), ptr(nonce), ptr(aad), aad.size(), ptr(in),
                                               count, &result[0], &result[count])
                 : wc_ChaCha20Poly1305_Decrypt(ptr(key), ptr(nonce), ptr(aad), aad.size(), ptr(in),
                                               count, &in[count], &result[0]);
    if (rc) {
        SecureZeroMemory(&result[0], result.size());
        require(false, "AEAD authentication failed");
    }
    result.resize(count + (enc ? 16 : 0));
    return result;
}
struct AirCrypto::Impl {
    WC_RNG rng;
    CRITICAL_SECTION mutex;
    Impl() {
        require(wc_InitRng(&rng) == 0, "Cryptographic RNG initialization failed");
        InitializeCriticalSection(&mutex);
    }
    ~Impl() {
        wc_FreeRng(&rng);
        DeleteCriticalSection(&mutex);
    }
};
AirCrypto::AirCrypto(const std::wstring &path) : cipher_(path), p_(new Impl) {}
AirCrypto::~AirCrypto() {
    delete p_;
}
Bytes AirCrypto::random(unsigned n) {
    require(n > 0 && n <= 4096, "Random length");
    Bytes b(n);
    EnterCriticalSection(&p_->mutex);
    int rc = wc_RNG_GenerateBlock(&p_->rng, &b[0], n);
    LeaveCriticalSection(&p_->mutex);
    require(rc == 0, "Cryptographic RNG failed");
    return b;
}
Bytes AirCrypto::hash(const char *name, const Bytes &data) {
    enum wc_HashType type = strcmp(name, "SHA512") == 0   ? WC_HASH_TYPE_SHA512
                            : strcmp(name, "SHA256") == 0 ? WC_HASH_TYPE_SHA256
                            : strcmp(name, "SHA1") == 0   ? WC_HASH_TYPE_SHA
                                                          : WC_HASH_TYPE_NONE;
    require(type != WC_HASH_TYPE_NONE, "Unsupported hash");
    Bytes out(type == WC_HASH_TYPE_SHA512 ? 64 : type == WC_HASH_TYPE_SHA256 ? 32 : 20);
    require(wc_Hash(type, ptr(data), data.size(), &out[0], out.size()) == 0, "Hash failed");
    return out;
}
Bytes AirCrypto::hkdf(const Bytes &key, const std::string &salt, const std::string &info) {
    Bytes out(32);
    require(wc_HKDF(WC_SHA512, ptr(key), key.size(), (const U8 *)salt.data(), salt.size(),
                    (const U8 *)info.data(), info.size(), &out[0], out.size()) == 0,
            "HKDF failed");
    return out;
}
Bytes AirCrypto::publicKey(const char *type, const Bytes &seed) {
    require(seed.size() == 32, "Private key length");
    Bytes out(32);
    word32 n = 32;
    int rc = -1;
    if (strcmp(type, "ED25519") == 0) {
        ed25519_key k;
        wc_ed25519_init(&k);
        rc = wc_ed25519_import_private_only(ptr(seed), 32, &k);
        if (!rc)
            rc = wc_ed25519_make_public(&k, &out[0], 32);
        wc_ed25519_free(&k);
    } else if (strcmp(type, "X25519") == 0) {
        Bytes scalar = seed;
        scalar[0] &= 248;
        scalar[31] = (scalar[31] & 127) | 64;
        rc = wc_curve25519_make_pub(32, &out[0], 32, ptr(scalar));
        SecureZeroMemory(&scalar[0], scalar.size());
    }
    require(rc == 0 && n == 32, "Public key derivation failed");
    return out;
}
Bytes AirCrypto::shared(const Bytes &seed, const Bytes &peer) {
    require(seed.size() == 32 && peer.size() == 32, "X25519 length");
    curve25519_key a, b;
    wc_curve25519_init(&a);
    wc_curve25519_init(&b);
    Bytes out(32);
    word32 n = 32;
    int rc = wc_curve25519_import_private_ex(ptr(seed), 32, &a, EC25519_LITTLE_ENDIAN);
    if (!rc)
        rc = wc_curve25519_import_public_ex(ptr(peer), 32, &b, EC25519_LITTLE_ENDIAN);
    if (!rc)
        rc = wc_curve25519_shared_secret_ex(&a, &b, &out[0], &n, EC25519_LITTLE_ENDIAN);
    wc_curve25519_free(&a);
    wc_curve25519_free(&b);
    require(rc == 0 && n == 32 && out != Bytes(32, 0), "X25519 agreement failed");
    return out;
}
Bytes AirCrypto::sign(const Bytes &seed, const Bytes &message) {
    require(seed.size() == 32, "Ed25519 seed length");
    ed25519_key k;
    wc_ed25519_init(&k);
    Bytes pub = publicKey("ED25519", seed), out(64);
    word32 n = 64;
    int rc = wc_ed25519_import_private_key(ptr(seed), 32, ptr(pub), 32, &k);
    if (!rc)
        rc = wc_ed25519_sign_msg(ptr(message), message.size(), &out[0], &n, &k);
    wc_ed25519_free(&k);
    require(rc == 0 && n == 64, "Ed25519 signing failed");
    return out;
}
bool AirCrypto::verify(const Bytes &pub, const Bytes &message, const Bytes &sig) {
    if (pub.size() != 32 || sig.size() != 64)
        return false;
    ed25519_key k;
    wc_ed25519_init(&k);
    int valid = 0;
    int rc = wc_ed25519_import_public(ptr(pub), 32, &k);
    if (!rc)
        rc = wc_ed25519_verify_msg(ptr(sig), 64, ptr(message), message.size(), &valid, &k);
    wc_ed25519_free(&k);
    return rc == 0 && valid == 1;
}
Bytes AirCrypto::aesCtr(const Bytes &key, const Bytes &iv, const Bytes &data) {
    require(key.size() == 16 && iv.size() == 16, "AES CTR length");
    Aes a;
    wc_AesInit(&a, 0, INVALID_DEVID);
    Bytes out(data.size() + 1);
    int rc = wc_AesSetKey(&a, ptr(key), 16, ptr(iv), AES_ENCRYPTION);
    if (!rc)
        rc = wc_AesCtrEncrypt(&a, &out[0], ptr(data), data.size());
    wc_AesFree(&a);
    require(rc == 0, "AES CTR failed");
    out.resize(data.size());
    return out;
}
Bytes AirCrypto::mod(const Bytes &a, const Bytes &b, const Bytes &n, char operation) {
    require(!a.empty() && !n.empty() && a.size() <= 512 && b.size() <= 512 && n.size() <= 512,
            "Bignum size");
    mp_int x, y, m, r;
    mp_init(&x);
    mp_init(&y);
    mp_init(&m);
    mp_init(&r);
    int rc = mp_read_unsigned_bin(&x, ptr(a), a.size());
    if (!rc)
        rc = mp_read_unsigned_bin(&y, ptr(b), b.size());
    if (!rc)
        rc = mp_read_unsigned_bin(&m, ptr(n), n.size());
    if (!rc && mp_iszero(&m))
        rc = -1;
    if (!rc) {
        switch (operation) {
        case '^':
            rc = mp_exptmod(&x, &y, &m, &r);
            break;
        case '*':
            rc = mp_mulmod(&x, &y, &m, &r);
            break;
        case '+':
            rc = mp_addmod(&x, &y, &m, &r);
            break;
        case '%':
            rc = mp_mod(&x, &m, &r);
            break;
        default:
            rc = -1;
        }
    }
    Bytes out(n.size(), 0);
    int len = mp_unsigned_bin_size(&r);
    if (!rc && len > 0 && size_t(len) <= out.size())
        rc = mp_to_unsigned_bin(&r, &out[out.size() - len]);
    mp_clear(&x);
    mp_clear(&y);
    mp_clear(&m);
    mp_clear(&r);
    require(rc == 0, "Modular arithmetic failed");
    return out;
}
} // namespace cp
